#include "EngineSelfTests.h"

#include "hassert.h"
#include "ds/harray.hpp"
#include "hfile.h"
#include "hlog.h"
#include "hmemory.h"
#include "Function/Asset/AssetData.h"
#include "Function/Asset/AssetDatabase.h"
#include "Function/Render/Material.h"
#include "Function/Render/TextureAsset.h"
#include "Graphics/DynamicRHI.h"
#include "Graphics/Shader.h"
#if defined(ASH_HAS_DX12)
#include "Graphics/DirectX12/DX12ResourceTracker.h"
#endif

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace AshEngine
{
	namespace
	{
		struct alignas(64) OverAlignedSelfTestType
		{
			uint32_t value = 0;
		};

		auto report_self_test_failure(const char* test_name, const char* reason) -> bool
		{
			HLogError("Engine base self-test '{}' failed: {}", test_name, reason);
			return false;
		}

		auto test_assert_macro_is_statement_safe() -> bool
		{
			bool branch_executed = false;
			if (true)
				H_ASSERT(true);
			else
				branch_executed = true;

			return !branch_executed || report_self_test_failure("H_ASSERT statement safety", "else branch was executed unexpectedly");
		}

		auto test_typed_allocation_respects_alignment() -> bool
		{
			OverAlignedSelfTestType* object = Ash_New<OverAlignedSelfTestType>();
			const uintptr_t address = reinterpret_cast<uintptr_t>(object);
			const bool aligned = object && (address % alignof(OverAlignedSelfTestType)) == 0;
			if (object)
			{
				_original_destroy(object);
				MemoryService::instance()->get_system_allocator()->deallocate(object);
			}

			return aligned || report_self_test_failure("Ash_New alignment", "over-aligned allocation was not aligned to alignof(T)");
		}

		auto test_stack_allocator_marker_rejects_forward_free() -> bool
		{
			StackAllocator allocator{};
			if (!allocator.init(256))
			{
				return report_self_test_failure("StackAllocator marker", "allocator init failed");
			}

			void* first = allocator.allocate(32, 8);
			const size_t marker = allocator.get_marker();
			void* second = allocator.allocate(32, 8);
			const bool rollback_ok = allocator.free_marker(marker);
			const size_t rollback_marker = allocator.get_marker();
			const bool forward_free_rejected = !allocator.free_marker(marker + 64);
			const size_t final_marker = allocator.get_marker();
			allocator.shutdown();

			if (!first || !second)
			{
				return report_self_test_failure("StackAllocator marker", "allocation failed");
			}
			if (!rollback_ok || rollback_marker != marker)
			{
				return report_self_test_failure("StackAllocator marker", "valid rollback did not restore the marker");
			}
			return (forward_free_rejected && final_marker == marker) ||
				report_self_test_failure("StackAllocator marker", "forward marker free was accepted");
		}

		auto test_linear_allocator_deallocate_reports_unsupported() -> bool
		{
			LinearAllocator allocator{};
			if (!allocator.init(128))
			{
				return report_self_test_failure("LinearAllocator deallocate", "allocator init failed");
			}

			void* allocation = allocator.allocate(16, 8);
			const bool deallocate_result = allocator.deallocate(allocation);
			allocator.shutdown();

			if (!allocation)
			{
				return report_self_test_failure("LinearAllocator deallocate", "allocation failed");
			}
			return (!deallocate_result) ||
				report_self_test_failure("LinearAllocator deallocate", "single-allocation deallocate reported success");
		}

		auto test_array_growth_and_initial_size() -> bool
		{
			Array<uint32_t> values{};
			if (!values.init(nullptr, 0, 3))
			{
				return report_self_test_failure("Array growth", "init with initial_size greater than capacity failed");
			}
			if (values.size() != 3 || values.capacity() < 3 || values[0] != 0 || values[1] != 0 || values[2] != 0)
			{
				values.shutdown();
				return report_self_test_failure("Array growth", "initial_size did not allocate and zero-initialize storage");
			}
			for (uint32_t value = 3; value < 8; ++value)
			{
				if (!values.push_back(value))
				{
					values.shutdown();
					return report_self_test_failure("Array growth", "push_back reported grow failure");
				}
			}
			const bool ok = values.size() == 8 &&
				values.capacity() >= 8 &&
				values[3] == 3 &&
				values[7] == 7;
			values.shutdown();
			return ok || report_self_test_failure("Array growth", "push_back did not preserve values across grow");
		}

		auto test_file_delete_reports_success() -> bool
		{
			const std::filesystem::path test_dir = "product/test-reports/engine";
			std::filesystem::create_directories(test_dir);
			const std::filesystem::path test_file = test_dir / "file_delete_self_test.tmp";
			file_write_binary(test_file.string().c_str(), const_cast<char*>("x"), 1);
			const bool delete_result = file_delete(test_file.string().c_str());
			const bool deleted = !std::filesystem::exists(test_file);

			return (delete_result && deleted) ||
				report_self_test_failure("file_delete", "successful delete did not return true");
		}

		auto test_shader_hash_uses_explicit_source_hash() -> bool
		{
			RHI::ShaderCreation first{};
			first.pBaseShaderPath = "product/test-reports/engine/self_test_shader.hlsl";
			first.pEntryPoint = "VSMain";
			first.type = RHI::ASH_SHADER_STAGE_VERTEX_BIT;
			first.source_hash = 1;

			RHI::ShaderCreation second = first;
			second.source_hash = 2;

			return (RHI::get_shader_hash(first) != RHI::get_shader_hash(second)) ||
				report_self_test_failure("Shader hash source version", "different source hashes produced the same shader key");
		}

		auto test_texture_decode_generates_rgba8_mips() -> bool
		{
			const std::filesystem::path test_dir = "product/test-reports/engine";
			std::filesystem::create_directories(test_dir);
			const std::filesystem::path bmp_path = test_dir / "mip_2x2.bmp";
			auto append_u16_le = [](std::vector<uint8_t>& bytes, uint16_t value) -> void
			{
				bytes.push_back(static_cast<uint8_t>(value & 0xffu));
				bytes.push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
			};
			auto append_u32_le = [](std::vector<uint8_t>& bytes, uint32_t value) -> void
			{
				bytes.push_back(static_cast<uint8_t>(value & 0xffu));
				bytes.push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
				bytes.push_back(static_cast<uint8_t>((value >> 16u) & 0xffu));
				bytes.push_back(static_cast<uint8_t>((value >> 24u) & 0xffu));
			};

			std::vector<uint8_t> bmp{};
			bmp.push_back('B');
			bmp.push_back('M');
			append_u32_le(bmp, 70u);
			append_u16_le(bmp, 0u);
			append_u16_le(bmp, 0u);
			append_u32_le(bmp, 54u);
			append_u32_le(bmp, 40u);
			append_u32_le(bmp, 2u);
			append_u32_le(bmp, 2u);
			append_u16_le(bmp, 1u);
			append_u16_le(bmp, 24u);
			append_u32_le(bmp, 0u);
			append_u32_le(bmp, 16u);
			append_u32_le(bmp, 0u);
			append_u32_le(bmp, 0u);
			append_u32_le(bmp, 0u);
			append_u32_le(bmp, 0u);
			const uint8_t pixels[] = {
				0u, 0u, 255u, 0u, 255u, 0u, 0u, 0u,
				255u, 0u, 0u, 255u, 255u, 255u, 0u, 0u
			};
			bmp.insert(bmp.end(), pixels, pixels + sizeof(pixels));
			{
				std::ofstream bmp_file(bmp_path, std::ios::binary | std::ios::trunc);
				bmp_file.write(reinterpret_cast<const char*>(bmp.data()), static_cast<std::streamsize>(bmp.size()));
			}

			TextureSourceData source{};
			std::string error{};
			if (!decode_texture_source_from_file(bmp_path, TextureColorSpace::Linear, source, &error))
			{
				return report_self_test_failure("Texture RGBA8 mip generation", error.empty() ? "failed to decode generated BMP" : error.c_str());
			}

			const bool ok =
				source.width == 2 &&
				source.height == 2 &&
				source.format == RenderTextureFormat::RGBA8_UNORM &&
				source.mip_level_count == 2 &&
				source.row_pitch == 8 &&
				source.pixel_data.size() == 20;
			return ok ||
				report_self_test_failure("Texture RGBA8 mip generation", "decoded texture did not contain the expected tight-packed mip chain");
		}

		auto test_dx12_validation_config_respects_build_type() -> bool
		{
			const std::filesystem::path test_dir = "product/test-reports/engine";
			std::filesystem::create_directories(test_dir);
			const std::filesystem::path config_path = test_dir / "rhi_validation_self_test.ini";
			{
				std::ofstream config_file(config_path, std::ios::trunc);
				config_file <<
					"[RHI]\n"
					"Backend=DX12\n"
					"\n"
					"[DX12Validation]\n"
					"Enabled=true\n"
					"GpuValidation=true\n";
			}

			const RHI::RuntimeRHIConfig config = RHI::load_runtime_rhi_config(config_path.string().c_str());
#if defined(ASH_DEBUG)
			const bool expected = config.dx12Validation.enableDebugLayer && config.dx12Validation.enableGpuValidation;
			return expected ||
				report_self_test_failure("DX12 validation config", "Debug build did not honor enabled DX12 validation config");
#else
			const bool forced_off = !config.dx12Validation.enableDebugLayer && !config.dx12Validation.enableGpuValidation;
			return forced_off ||
				report_self_test_failure("DX12 validation config", "Release build accepted DX12 validation config");
#endif
		}

		template <typename T>
		auto append_binary_value(std::vector<uint8_t>& bytes, const T& value) -> void
		{
			const size_t offset = bytes.size();
			bytes.resize(offset + sizeof(T));
			std::memcpy(bytes.data() + offset, &value, sizeof(T));
		}

		auto test_gltf_import_preserves_index_reuse() -> bool
		{
			const std::filesystem::path test_dir = "product/test-reports/engine";
			std::filesystem::create_directories(test_dir);
			const std::filesystem::path bin_path = test_dir / "indexed_quad.bin";
			const std::filesystem::path gltf_path = test_dir / "indexed_quad.gltf";

			std::vector<uint8_t> bytes{};
			const float positions[] = {
				0.0f, 0.0f, 0.0f,
				1.0f, 0.0f, 0.0f,
				1.0f, 1.0f, 0.0f,
				0.0f, 1.0f, 0.0f
			};
			for (float value : positions)
			{
				append_binary_value(bytes, value);
			}
			const uint16_t indices[] = { 0, 1, 2, 0, 2, 3 };
			for (uint16_t value : indices)
			{
				append_binary_value(bytes, value);
			}

			{
				std::ofstream bin_file(bin_path, std::ios::binary | std::ios::trunc);
				bin_file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			}
			{
				std::ofstream gltf_file(gltf_path, std::ios::trunc);
				gltf_file <<
					"{\n"
					"  \"asset\": { \"version\": \"2.0\" },\n"
					"  \"buffers\": [{ \"uri\": \"indexed_quad.bin\", \"byteLength\": 60 }],\n"
					"  \"bufferViews\": [\n"
					"    { \"buffer\": 0, \"byteOffset\": 0, \"byteLength\": 48, \"target\": 34962 },\n"
					"    { \"buffer\": 0, \"byteOffset\": 48, \"byteLength\": 12, \"target\": 34963 }\n"
					"  ],\n"
					"  \"accessors\": [\n"
					"    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 4, \"type\": \"VEC3\", \"min\": [0, 0, 0], \"max\": [1, 1, 0] },\n"
					"    { \"bufferView\": 1, \"componentType\": 5123, \"count\": 6, \"type\": \"SCALAR\" }\n"
					"  ],\n"
					"  \"meshes\": [{ \"primitives\": [{ \"attributes\": { \"POSITION\": 0 }, \"indices\": 1, \"mode\": 4 }] }],\n"
					"  \"nodes\": [{ \"mesh\": 0 }],\n"
					"  \"scenes\": [{ \"nodes\": [0] }],\n"
					"  \"scene\": 0\n"
					"}\n";
			}

			Model model{};
			std::string error{};
			if (!load_model_from_file(gltf_path, model, &error) || model.meshes.empty())
			{
				return report_self_test_failure("glTF indexed import", error.empty() ? "failed to load generated indexed glTF" : error.c_str());
			}

			const Mesh& mesh = model.meshes.front();
			const bool preserved =
				mesh.vertices.size() == 4 &&
				mesh.indices.size() == 6 &&
				mesh.indices[0] == 0 &&
				mesh.indices[3] == 0 &&
				!mesh.sections.empty() &&
				mesh.sections.front().vertex_count == 4 &&
				mesh.sections.front().index_count == 6;
			return preserved ||
				report_self_test_failure("glTF indexed import", "indexed primitive was expanded instead of preserving vertex reuse");
		}

		auto test_material_asset_database_prefers_disk_material_over_builtin_fallback() -> bool
		{
			const std::filesystem::path asset_root = "Intermediate/SelfTests/engine/material_asset_db";
			const std::filesystem::path material_dir = asset_root / "materials" / "v2";
			std::filesystem::create_directories(material_dir);

			const std::filesystem::path material_path = material_dir / "M_SurfacePBR.AshMat";
			{
				std::ofstream material_file(material_path, std::ios::trunc);
				if (!material_file)
				{
					return report_self_test_failure("Material disk asset priority", "failed to create disk material asset");
				}
				material_file <<
					"{\n"
					"  \"version\": 2,\n"
					"  \"class\": \"Material\",\n"
					"  \"name\": \"M_DiskSurfacePBR\",\n"
					"  \"domain\": \"Surface\",\n"
					"  \"materialShader\": \"product/assets/materials/v2/M_SurfacePBR.hlsl\"\n"
					"}\n";
			}

			AssetDatabase database = AssetDatabase::create(asset_root);
			std::shared_ptr<const MaterialInterface> material{};
			if (!database.load_material_by_path("materials/v2/M_SurfacePBR.AshMat", material) || !material)
			{
				return report_self_test_failure("Material disk asset priority", "failed to load disk material asset");
			}

			return material->get_name() == "M_DiskSurfacePBR" ||
				report_self_test_failure("Material disk asset priority", "built-in fallback shadowed an existing disk material asset");
		}

#if defined(ASH_HAS_DX12)
		auto test_dx12_resource_tracker_preserves_partial_state() -> bool
		{
			RHI::DX12ResourceTracker tracker{};
			auto* fakeResource = reinterpret_cast<ID3D12Resource*>(static_cast<uintptr_t>(0x1));
			tracker.track_resource(fakeResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 4);

			std::vector<D3D12_RESOURCE_BARRIER> barriers{};
			tracker.generate_barriers(fakeResource, D3D12_RESOURCE_STATE_COPY_DEST, barriers, 2);
			if (barriers.size() != 1 ||
				barriers[0].Transition.Subresource != 2 ||
				barriers[0].Transition.StateBefore != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE ||
				barriers[0].Transition.StateAfter != D3D12_RESOURCE_STATE_COPY_DEST)
			{
				return report_self_test_failure("DX12ResourceTracker partial state", "partial transition did not emit the expected subresource barrier");
			}

			barriers.clear();
			tracker.generate_barriers(fakeResource, D3D12_RESOURCE_STATE_RENDER_TARGET, barriers);
			if (barriers.size() != 4)
			{
				return report_self_test_failure("DX12ResourceTracker partial state", "whole-resource transition from mixed state did not expand to subresource barriers");
			}

			bool saw_copy_dest_source = false;
			for (const auto& barrier : barriers)
			{
				if (barrier.Transition.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES ||
					barrier.Transition.StateAfter != D3D12_RESOURCE_STATE_RENDER_TARGET)
				{
					return report_self_test_failure("DX12ResourceTracker partial state", "mixed transition emitted an invalid barrier");
				}
				if (barrier.Transition.Subresource == 2 &&
					barrier.Transition.StateBefore == D3D12_RESOURCE_STATE_COPY_DEST)
				{
					saw_copy_dest_source = true;
				}
			}

			return saw_copy_dest_source ||
				report_self_test_failure("DX12ResourceTracker partial state", "mixed whole transition lost the partial subresource source state");
		}
#endif
	}

	auto run_engine_base_self_tests() -> int
	{
		LogService::instance()->init(nullptr);
		MemoryService::instance()->init(nullptr);

		bool all_passed = true;
		all_passed = test_assert_macro_is_statement_safe() && all_passed;
		all_passed = test_typed_allocation_respects_alignment() && all_passed;
		all_passed = test_stack_allocator_marker_rejects_forward_free() && all_passed;
		all_passed = test_linear_allocator_deallocate_reports_unsupported() && all_passed;
		all_passed = test_array_growth_and_initial_size() && all_passed;
		all_passed = test_file_delete_reports_success() && all_passed;
		all_passed = test_shader_hash_uses_explicit_source_hash() && all_passed;
		all_passed = test_texture_decode_generates_rgba8_mips() && all_passed;
		all_passed = test_dx12_validation_config_respects_build_type() && all_passed;
		all_passed = test_gltf_import_preserves_index_reuse() && all_passed;
		all_passed = test_material_asset_database_prefers_disk_material_over_builtin_fallback() && all_passed;
#if defined(ASH_HAS_DX12)
		all_passed = test_dx12_resource_tracker_preserves_partial_state() && all_passed;
#endif

		MemoryService::instance()->shutdown();
		LogService::instance()->shutdown();
		return all_passed ? 0 : 1;
	}
}
