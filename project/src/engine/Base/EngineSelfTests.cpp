#include "EngineSelfTests.h"

#include "hassert.h"
#include "ds/harray.hpp"
#include "hfile.h"
#include "hlog.h"
#include "hmemory.h"
#include "Function/Asset/AssetData.h"
#include "Function/Asset/AssetDatabase.h"
#include "Function/Render/GBufferLayout.h"
#include "Function/Render/Material.h"
#include "Function/Render/RenderDevice.h"
#include "Function/Render/RenderGraph.h"
#include "Function/Render/RenderScene.h"
#include "Function/Render/SceneRenderer.h"
#include "Function/Render/TextureAsset.h"
#include "Function/Scene/Scene.h"
#include "Graphics/DynamicRHI.h"
#include "Graphics/Pipeline.h"
#include "Graphics/RHIResource.h"
#include "Graphics/Shader.h"
#if defined(ASH_HAS_DX12)
#include "Graphics/DirectX12/DX12ResourceTracker.h"
#endif

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <utility>
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

		auto engine_self_test_dir() -> std::filesystem::path
		{
			const std::filesystem::path test_dir = "Intermediate/test-temp/engine";
			std::filesystem::create_directories(test_dir);
			return test_dir;
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
			const std::filesystem::path test_dir = engine_self_test_dir();
			const std::filesystem::path test_file = test_dir / "file_delete_self_test.tmp";
			file_write_binary(test_file.string().c_str(), const_cast<char*>("x"), 1);
			const bool delete_result = file_delete(test_file.string().c_str());
			const bool deleted = !std::filesystem::exists(test_file);

			return (delete_result && deleted) ||
				report_self_test_failure("file_delete", "successful delete did not return true");
		}

		auto test_file_read_text_and_extension_are_safe() -> bool
		{
			const std::filesystem::path test_dir = engine_self_test_dir();
			const std::filesystem::path test_file = test_dir / "file_read_text_self_test.txt";
			const char text[] = "AshEngineText";
			file_write_binary(test_file.string().c_str(), const_cast<char*>(text), sizeof(text) - 1);

			size_t text_size = 0;
			char* text_data = file_read_text(test_file.string().c_str(), text_size, nullptr);
			const bool text_ok =
				text_data != nullptr &&
				text_size == sizeof(text) - 1 &&
				std::memcmp(text_data, text, sizeof(text) - 1) == 0;
			if (text_data)
			{
				MemoryService::instance()->get_system_allocator()->deallocate(text_data);
			}

			char no_extension_path[] = "Intermediate/test-temp/engine/no_extension";
			const bool extension_ok = file_extension_from_path(no_extension_path) == nullptr;

			return (text_ok && extension_ok) ||
				report_self_test_failure("file text helpers", "text read or extension parsing returned invalid data");
		}

		auto test_shader_hash_uses_explicit_source_hash() -> bool
		{
			RHI::ShaderCreation first{};
			first.pBaseShaderPath = "Intermediate/test-temp/engine/self_test_shader.hlsl";
			first.pEntryPoint = "VSMain";
			first.type = RHI::ASH_SHADER_STAGE_VERTEX_BIT;
			first.source_hash = 1;

			RHI::ShaderCreation second = first;
			second.source_hash = 2;

			return (RHI::get_shader_hash(first) != RHI::get_shader_hash(second)) ||
				report_self_test_failure("Shader hash source version", "different source hashes produced the same shader key");
		}

		auto test_subresource_range_resolve_clamps_defaults() -> bool
		{
			RHI::AshSubresourceRange all{};
			RHI::AshSubresourceRange resolved_all = all.resolve(4, 3);
			const bool all_ok =
				resolved_all.uBaseMipLevel == 0 &&
				resolved_all.uMipCount == 4 &&
				resolved_all.uBaseArraySlice == 0 &&
				resolved_all.uArrayCount == 3;

			RHI::AshSubresourceRange partial{ 8, 9, RHI::AshSubresourceRange::s_All, RHI::AshSubresourceRange::s_All };
			RHI::AshSubresourceRange resolved_partial = partial.resolve(4, 3);
			const bool clamp_ok =
				resolved_partial.uBaseMipLevel == 3 &&
				resolved_partial.uMipCount == 1 &&
				resolved_partial.uBaseArraySlice == 2 &&
				resolved_partial.uArrayCount == 1;

			return (all_ok && clamp_ok) ||
				report_self_test_failure("AshSubresourceRange resolve", "default or out-of-range subresource range did not resolve correctly");
		}

		auto test_ash_barrier_copy_move_is_safe() -> bool
		{
			RHI::AshBarrier empty_barrier{};
			RHI::AshBarrier empty_copy = empty_barrier;
			RHI::AshBarrier moved_copy = std::move(empty_copy);

			const bool empty_ok =
				moved_copy.eType == RHI::AshBarrier::EType::Unknown &&
				!moved_copy.pTexture &&
				!moved_copy.pBuffer;

			RHI::AshBarrier texture_barrier{};
			texture_barrier.eType = RHI::AshBarrier::EType::Texture;
			texture_barrier.eDSTAccess = RHI::AshResourceState::SRVGraphics;
			RHI::AshBarrier copied_texture_barrier = texture_barrier;

			const bool typed_ok =
				copied_texture_barrier.eType == RHI::AshBarrier::EType::Texture &&
				!copied_texture_barrier.pTexture &&
				!copied_texture_barrier.pBuffer &&
				copied_texture_barrier.eDSTAccess == RHI::AshResourceState::SRVGraphics;

			return (empty_ok && typed_ok) ||
				report_self_test_failure("AshBarrier value semantics", "copy/move touched invalid barrier resource storage");
		}

		auto test_texture_decode_generates_rgba8_mips() -> bool
		{
			const std::filesystem::path test_dir = engine_self_test_dir();
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

		auto test_texture_decode_supports_dds_bc1() -> bool
		{
			const std::filesystem::path dds_path = engine_self_test_dir() / "cooked_bc1.dds";
			auto append_u32_le = [](std::vector<uint8_t>& bytes, uint32_t value) -> void
			{
				bytes.push_back(static_cast<uint8_t>(value & 0xffu));
				bytes.push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
				bytes.push_back(static_cast<uint8_t>((value >> 16u) & 0xffu));
				bytes.push_back(static_cast<uint8_t>((value >> 24u) & 0xffu));
			};
			auto append_fourcc = [&append_u32_le](std::vector<uint8_t>& bytes, const char (&tag)[5]) -> void
			{
				append_u32_le(
					bytes,
					static_cast<uint32_t>(tag[0]) |
					(static_cast<uint32_t>(tag[1]) << 8u) |
					(static_cast<uint32_t>(tag[2]) << 16u) |
					(static_cast<uint32_t>(tag[3]) << 24u));
			};

			std::vector<uint8_t> dds{};
			append_fourcc(dds, "DDS ");
			append_u32_le(dds, 124u);
			append_u32_le(dds, 0x0002100Fu);
			append_u32_le(dds, 4u);
			append_u32_le(dds, 4u);
			append_u32_le(dds, 8u);
			append_u32_le(dds, 0u);
			append_u32_le(dds, 1u);
			for (uint32_t index = 0; index < 11u; ++index)
			{
				append_u32_le(dds, 0u);
			}
			append_u32_le(dds, 32u);
			append_u32_le(dds, 0x00000004u);
			append_fourcc(dds, "DXT1");
			append_u32_le(dds, 0u);
			append_u32_le(dds, 0u);
			append_u32_le(dds, 0u);
			append_u32_le(dds, 0u);
			append_u32_le(dds, 0u);
			append_u32_le(dds, 0x00001000u);
			append_u32_le(dds, 0u);
			append_u32_le(dds, 0u);
			append_u32_le(dds, 0u);
			append_u32_le(dds, 0u);
			const uint8_t bc1_block[8] = { 0x00u, 0xF8u, 0xE0u, 0x07u, 0x00u, 0x00u, 0x00u, 0x00u };
			dds.insert(dds.end(), bc1_block, bc1_block + sizeof(bc1_block));
			{
				std::ofstream dds_file(dds_path, std::ios::binary | std::ios::trunc);
				dds_file.write(reinterpret_cast<const char*>(dds.data()), static_cast<std::streamsize>(dds.size()));
			}

			TextureSourceData source{};
			std::string error{};
			if (!decode_texture_source_from_file(dds_path, TextureColorSpace::SRGB, source, &error))
			{
				return report_self_test_failure("Texture DDS BC1 decode", error.empty() ? "failed to decode generated DDS" : error.c_str());
			}

			const bool ok =
				source.width == 4 &&
				source.height == 4 &&
				source.format == RenderTextureFormat::BC1_RGBA_SRGB_UNORM &&
				source.color_space == TextureColorSpace::SRGB &&
				source.mip_level_count == 1 &&
				source.row_pitch == 8 &&
				source.pixel_data.size() == sizeof(bc1_block);
			return ok ||
				report_self_test_failure("Texture DDS BC1 decode", "decoded DDS metadata or payload layout was invalid");
		}

		auto test_texture_decode_supports_ktx2_bc7() -> bool
		{
			const std::filesystem::path ktx_path = engine_self_test_dir() / "cooked_bc7.ktx2";
			auto append_u32_le = [](std::vector<uint8_t>& bytes, uint32_t value) -> void
			{
				bytes.push_back(static_cast<uint8_t>(value & 0xffu));
				bytes.push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
				bytes.push_back(static_cast<uint8_t>((value >> 16u) & 0xffu));
				bytes.push_back(static_cast<uint8_t>((value >> 24u) & 0xffu));
			};
			auto append_u64_le = [](std::vector<uint8_t>& bytes, uint64_t value) -> void
			{
				for (uint32_t shift = 0; shift < 64u; shift += 8u)
				{
					bytes.push_back(static_cast<uint8_t>((value >> shift) & 0xffu));
				}
			};

			std::vector<uint8_t> ktx{};
			const uint8_t identifier[] = { 0xABu, 'K', 'T', 'X', ' ', '2', '0', 0xBBu, '\r', '\n', 0x1Au, '\n' };
			ktx.insert(ktx.end(), identifier, identifier + sizeof(identifier));
			append_u32_le(ktx, 145u);
			append_u32_le(ktx, 1u);
			append_u32_le(ktx, 4u);
			append_u32_le(ktx, 4u);
			append_u32_le(ktx, 0u);
			append_u32_le(ktx, 0u);
			append_u32_le(ktx, 1u);
			append_u32_le(ktx, 1u);
			append_u32_le(ktx, 0u);
			append_u32_le(ktx, 0u);
			append_u32_le(ktx, 0u);
			append_u32_le(ktx, 0u);
			append_u32_le(ktx, 0u);
			append_u64_le(ktx, 0u);
			append_u64_le(ktx, 0u);
			append_u64_le(ktx, 104u);
			append_u64_le(ktx, 16u);
			append_u64_le(ktx, 16u);
			const uint8_t bc7_block[16] = {
				0x00u, 0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u,
				0x88u, 0x99u, 0xAAu, 0xBBu, 0xCCu, 0xDDu, 0xEEu, 0xFFu
			};
			ktx.insert(ktx.end(), bc7_block, bc7_block + sizeof(bc7_block));
			{
				std::ofstream ktx_file(ktx_path, std::ios::binary | std::ios::trunc);
				ktx_file.write(reinterpret_cast<const char*>(ktx.data()), static_cast<std::streamsize>(ktx.size()));
			}

			TextureSourceData source{};
			std::string error{};
			if (!decode_texture_source_from_file(ktx_path, TextureColorSpace::Linear, source, &error))
			{
				return report_self_test_failure("Texture KTX2 BC7 decode", error.empty() ? "failed to decode generated KTX2" : error.c_str());
			}

			const bool ok =
				source.width == 4 &&
				source.height == 4 &&
				source.format != RenderTextureFormat::Unknown &&
				source.mip_level_count == 1 &&
				source.row_pitch == 16 &&
				source.pixel_data.size() == sizeof(bc7_block);
			return ok ||
				report_self_test_failure("Texture KTX2 BC7 decode", "decoded KTX2 metadata or payload layout was invalid");
		}

		auto test_dx12_validation_config_respects_build_type() -> bool
		{
			const std::filesystem::path test_dir = engine_self_test_dir();
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
			const std::filesystem::path test_dir = engine_self_test_dir();
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
			const std::filesystem::path asset_root = "Intermediate/test-temp/engine/material_asset_db";
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
			std::shared_ptr<const MaterialInterface> sync_material{};
			if (!database.load_material_by_path("materials/v2/M_SurfacePBR.AshMat", sync_material) || !sync_material)
			{
				return report_self_test_failure("Material disk asset priority", "failed to load disk material asset");
			}

			if (sync_material->get_name() != "M_DiskSurfacePBR")
			{
				return report_self_test_failure("Material disk asset priority", "built-in fallback shadowed an existing disk material asset");
			}

			std::shared_ptr<const MaterialInterface> async_material = database.load_material_by_path_async("materials/v2/M_SurfacePBR.AshMat").get();
			if (!async_material)
			{
				return report_self_test_failure("Material disk asset priority", "failed to async load disk material asset");
			}

			return async_material->get_name() == "M_DiskSurfacePBR" ||
				report_self_test_failure("Material disk asset priority", "built-in fallback shadowed an existing disk material asset");
		}

		auto test_scene_renderer_batches_only_when_multiple_static_mesh_draws_are_visible() -> bool
		{
			if (SceneRenderer::should_use_instanced_static_mesh_path(0))
			{
				return report_self_test_failure("SceneRenderer batch policy", "empty frames should not use the instanced path");
			}
			if (SceneRenderer::should_use_instanced_static_mesh_path(1))
			{
				return report_self_test_failure("SceneRenderer batch policy", "single static mesh frames should use the direct draw path");
			}
			return SceneRenderer::should_use_instanced_static_mesh_path(2) ||
				report_self_test_failure("SceneRenderer batch policy", "multiple static mesh draws should keep the instancing path available");
		}

		auto test_deferred_hq_gbuffer_layout_contract() -> bool
		{
			const GBufferLayoutDesc& layout = get_deferred_hq_gbuffer_layout();
			if (layout.attachments.size() != 5)
			{
				return report_self_test_failure("DeferredHQ GBuffer layout", "layout did not expose five color attachments");
			}
			if (layout.attachments[0].format != RenderTextureFormat::RGBA8_UNORM ||
				layout.attachments[1].format != RenderTextureFormat::RGBA8_UNORM ||
				layout.attachments[2].format != RenderTextureFormat::RGBA8_UNORM ||
				layout.attachments[3].format != RenderTextureFormat::RGBA16_SFLOAT ||
				layout.attachments[4].format != RenderTextureFormat::RGBA16_SFLOAT)
			{
				return report_self_test_failure("DeferredHQ GBuffer layout", "attachment formats changed unexpectedly");
			}

			const GBufferSemanticMapping* motion =
				find_gbuffer_semantic_mapping(layout, GBufferSemantic::MotionVector3D);
			const GBufferSemanticMapping* normal =
				find_gbuffer_semantic_mapping(layout, GBufferSemantic::NormalOct);
			const bool ok =
				layout.layout_hash != 0 &&
				motion && motion->attachment_index == 3 && motion->component_mask == 0x7u &&
				normal && normal->attachment_index == 4 && normal->component_mask == 0x3u;
			return ok ||
				report_self_test_failure("DeferredHQ GBuffer layout", "semantic mappings did not match the design contract");
		}

		auto test_deferred_shading_model_ids_are_stable() -> bool
		{
			const bool ok =
				get_material_shading_model_id(MaterialShadingModel::Empty) == 0u &&
				get_material_shading_model_id(MaterialShadingModel::DefaultLitGGX) == 1u &&
				get_material_shading_model_id(MaterialShadingModel::Unlit) == 2u &&
				get_material_shading_model_id(MaterialShadingModel::BlinnPhong) == 3u;
			return ok ||
				report_self_test_failure("Deferred shading model ids", "GBuffer shading model ids changed unexpectedly");
		}

		auto test_material_asset_loads_declared_shading_model() -> bool
		{
			const std::filesystem::path asset_root = "Intermediate/test-temp/engine/material_shading_model";
			std::filesystem::create_directories(asset_root);

			const std::filesystem::path material_path = asset_root / "M_Unlit.AshMat";
			{
				std::ofstream material_file(material_path, std::ios::trunc);
				if (!material_file)
				{
					return report_self_test_failure("Material shading model load", "failed to create disk material asset");
				}
				material_file <<
					"{\n"
					"  \"version\": 2,\n"
					"  \"class\": \"Material\",\n"
					"  \"name\": \"M_Unlit\",\n"
					"  \"domain\": \"Surface\",\n"
					"  \"shading_model\": \"unlit\",\n"
					"  \"materialShader\": \"product/assets/materials/v2/M_SurfacePBR.hlsl\"\n"
					"}\n";
			}

			std::shared_ptr<MaterialInterface> material{};
			std::string error{};
			if (!load_material_from_file(material_path, material, &error) || !material)
			{
				return report_self_test_failure("Material shading model load", error.empty() ? "failed to load disk material asset" : error.c_str());
			}

			return material->get_shading_model() == MaterialShadingModel::Unlit ||
				report_self_test_failure("Material shading model load", "declared shading_model was not applied");
		}

		auto test_graphics_program_state_maps_deferred_light_volume_state() -> bool
		{
			GraphicsProgramState state{};
			state.cull_mode = RenderCullMode::None;
			state.depth_test = true;
			state.depth_write = false;
			state.depth_compare = RenderCompareOp::GreaterEqual;
			state.blend_mode = RenderBlendMode::Additive;

			RHI::PipelineCreation pipeline{};
			fill_pipeline_state_from_graphics_program_state(state, pipeline);
			const bool depth_ok =
				pipeline.depth_stencil.depth_enable == 1 &&
				pipeline.depth_stencil.depth_write_enable == 0 &&
				pipeline.depth_stencil.depth_comparison == RHI::ASH_COMPARE_OP_GREATER_OR_EQUAL;
			const bool blend_ok =
				pipeline.blend_state.active_states == 1u &&
				pipeline.blend_state.blend_states[0].blend_enabled == 1 &&
				pipeline.blend_state.blend_states[0].source_color == RHI::ASH_BLEND_FACTOR_ONE &&
				pipeline.blend_state.blend_states[0].destination_color == RHI::ASH_BLEND_FACTOR_ONE;
			return (depth_ok && blend_ok) ||
				report_self_test_failure("Deferred light volume render state", "depth compare or additive blend mapping is invalid");
		}

		auto test_deferred_read_only_depth_attachment_state() -> bool
		{
			const RHI::AshResourceState sampled_read_only =
				get_depth_attachment_resource_state(true, true);
			const RHI::AshResourceState writeable =
				get_depth_attachment_resource_state(false, true);
			const bool ok =
				(sampled_read_only & RHI::AshResourceState::DSVRead) == RHI::AshResourceState::DSVRead &&
				(sampled_read_only & RHI::AshResourceState::SRVGraphics) == RHI::AshResourceState::SRVGraphics &&
				(sampled_read_only & RHI::AshResourceState::DSVWrite) == RHI::AshResourceState::Unknown &&
				writeable == RHI::AshResourceState::DSVWrite;
			return ok ||
				report_self_test_failure("Deferred read-only depth attachment", "read-only depth did not preserve DSV read plus shader read state");
		}

		auto test_render_graph_access_maps_to_rhi_states() -> bool
		{
			bool ok = true;
			ok = ok && render_graph_access_to_rhi_state(RenderGraphAccess::GraphicsSRV) == RHI::AshResourceState::SRVGraphics;
			ok = ok && render_graph_access_to_rhi_state(RenderGraphAccess::ComputeSRV) == RHI::AshResourceState::SRVCompute;
			ok = ok && render_graph_access_to_rhi_state(RenderGraphAccess::GraphicsUAV) == RHI::AshResourceState::UAVGraphics;
			ok = ok && render_graph_access_to_rhi_state(RenderGraphAccess::ComputeUAV) == RHI::AshResourceState::UAVCompute;
			ok = ok && render_graph_access_to_rhi_state(RenderGraphAccess::ColorAttachmentWrite) == RHI::AshResourceState::RTV;
			ok = ok && render_graph_access_to_rhi_state(RenderGraphAccess::DepthStencilWrite) == RHI::AshResourceState::DSVWrite;
			ok = ok && render_graph_access_to_rhi_state(RenderGraphAccess::DepthStencilRead) == RHI::AshResourceState::DSVRead;
			ok = ok && render_graph_access_to_rhi_state(RenderGraphAccess::CopySrc) == RHI::AshResourceState::CopySrc;
			ok = ok && render_graph_access_to_rhi_state(RenderGraphAccess::CopyDst) == RHI::AshResourceState::CopyDst;
			ok = ok && render_graph_access_to_rhi_state(RenderGraphAccess::Present) == RHI::AshResourceState::Present;

			const RHI::AshResourceState depth_sample_state =
				render_graph_depth_read_state(RenderGraphDepthReadMode::DepthTestAndShaderResource);
			ok = ok && depth_sample_state == (RHI::AshResourceState::DSVRead | RHI::AshResourceState::SRVGraphics);

			return ok || report_self_test_failure("RenderGraph access mapping", "graph access did not map to the expected RHI state");
		}

		auto test_render_scene_extracts_light_snapshot() -> bool
		{
			Scene scene = Scene::create("LightSnapshotSelfTest");
			Entity directional = scene.create_entity("DirectionalLight");
			TransformComponent directional_transform = directional.get_transform_component();
			directional_transform.rotation_euler_degrees = { 0.0f, 90.0f, 0.0f };
			directional.set_transform_component(directional_transform);
			LightComponent directional_light{};
			directional_light.type = LightType::Directional;
			directional_light.color = { 0.25f, 0.5f, 1.0f };
			directional_light.intensity = 2.0f;
			directional.add_light_component(directional_light);

			Entity point = scene.create_entity("PointLight");
			TransformComponent point_transform = point.get_transform_component();
			point_transform.position = { 1.0f, 2.0f, 3.0f };
			point.set_transform_component(point_transform);
			LightComponent point_light{};
			point_light.type = LightType::Point;
			point_light.range = 7.0f;
			point_light.intensity = 3.0f;
			point.add_light_component(point_light);

			RenderScene render_scene{};
			VisibleRenderFrame frame{};
			if (!render_scene.rebuild_lights_from_scene(scene) || !render_scene.build_visible_light_frame(frame))
			{
				return report_self_test_failure("RenderScene light snapshot", "failed to build render-scene light snapshot");
			}

			if (frame.lights.size() != 2u)
			{
				return report_self_test_failure("RenderScene light snapshot", "visible frame did not contain the expected lights");
			}
			const bool directional_ok =
				frame.lights[0].type == LightType::Directional &&
				glm::length(frame.lights[0].direction_ws) > 0.9f;
			const bool point_ok =
				frame.lights[1].type == LightType::Point &&
				frame.lights[1].position_ws == glm::vec3(1.0f, 2.0f, 3.0f) &&
				frame.lights[1].range == 7.0f;
			return (directional_ok && point_ok) ||
				report_self_test_failure("RenderScene light snapshot", "light data was not extracted with stable transform data");
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
		all_passed = test_file_read_text_and_extension_are_safe() && all_passed;
		all_passed = test_shader_hash_uses_explicit_source_hash() && all_passed;
		all_passed = test_subresource_range_resolve_clamps_defaults() && all_passed;
		all_passed = test_ash_barrier_copy_move_is_safe() && all_passed;
		all_passed = test_texture_decode_generates_rgba8_mips() && all_passed;
		all_passed = test_texture_decode_supports_dds_bc1() && all_passed;
		all_passed = test_texture_decode_supports_ktx2_bc7() && all_passed;
		all_passed = test_dx12_validation_config_respects_build_type() && all_passed;
		all_passed = test_gltf_import_preserves_index_reuse() && all_passed;
		all_passed = test_material_asset_database_prefers_disk_material_over_builtin_fallback() && all_passed;
		all_passed = test_scene_renderer_batches_only_when_multiple_static_mesh_draws_are_visible() && all_passed;
		all_passed = test_deferred_hq_gbuffer_layout_contract() && all_passed;
		all_passed = test_deferred_shading_model_ids_are_stable() && all_passed;
		all_passed = test_material_asset_loads_declared_shading_model() && all_passed;
		all_passed = test_graphics_program_state_maps_deferred_light_volume_state() && all_passed;
		all_passed = test_deferred_read_only_depth_attachment_state() && all_passed;
		all_passed = test_render_graph_access_maps_to_rhi_states() && all_passed;
		all_passed = test_render_scene_extracts_light_snapshot() && all_passed;
#if defined(ASH_HAS_DX12)
		all_passed = test_dx12_resource_tracker_preserves_partial_state() && all_passed;
#endif

		MemoryService::instance()->shutdown();
		LogService::instance()->shutdown();
		return all_passed ? 0 : 1;
	}
}
