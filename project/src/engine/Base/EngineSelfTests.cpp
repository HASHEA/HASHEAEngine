#include "EngineSelfTests.h"

#include "hassert.h"
#include "ds/harray.hpp"
#include "hfile.h"
#include "hlog.h"
#include "hmemory.h"
#include "ProcessMemoryDiagnostics.h"
#include "Function/Asset/AssetData.h"
#include "Function/Asset/AssetDatabase.h"
#include "Function/Diagnostics/PerfGate.h"
#include "Function/Render/AmbientOcclusionConfig.h"
#include "Function/Render/BloomConfig.h"
#include "Function/Render/DeferredLightingPass.h"
#include "Function/Render/DirectionalShadowConfig.h"
#include "Function/Render/DirectionalShadowCascadeMath.h"
#include "Function/Render/DirectionalLightShadowPass.h"
#include "Function/Render/EnvironmentMapAsset.h"
#include "Function/Render/EnvironmentMapBaker.h"
#include "Function/Render/GBufferLayout.h"
#include "Function/Render/DebugDrawService.h"
#include "Function/Render/Material.h"
#include "Function/Render/RenderDevice.h"
#include "Function/Render/RenderDebugView.h"
#include "Function/Render/RenderFormatUtils.h"
#include "Function/Render/RenderGraph.h"
#include "Function/Render/RenderScene.h"
#include "Function/Render/RenderFeatureConfig.h"
#include "Function/Render/SceneDeferredGraphResources.h"
#include "Function/Render/SceneRenderer.h"
#include "Function/Render/SceneView.h"
#include "Function/Render/SunLightShadowPass.h"
#include "Function/Render/TextureAsset.h"
#include "Function/Render/VolumetricLightingConfig.h"
#include "Function/Render/VolumetricLightingPass.h"
#include "Function/Scene/Scene.h"
#include "Function/Scene/SceneQuery.h"
#include "Graphics/DynamicRHI.h"
#include "Graphics/Pipeline.h"
#include "Graphics/RHIResource.h"
#include "Graphics/Shader.h"
#if defined(ASH_HAS_VULKAN)
#include "Graphics/Vulkan/VulkanSwapchain.h"
#endif
#if defined(ASH_HAS_DX12)
#include "Graphics/DirectX12/DX12ResourceTracker.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

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

		auto file_contains_all(const char* path, const std::vector<const char*>& needles) -> bool
		{
			std::ifstream file(path);
			if (!file.is_open())
			{
				return false;
			}
			const std::string source{
				std::istreambuf_iterator<char>(file),
				std::istreambuf_iterator<char>()
			};
			for (const char* needle : needles)
			{
				if (source.find(needle) == std::string::npos)
				{
					return false;
				}
			}
			return true;
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

		auto test_memory_service_reports_heap_statistics() -> bool
		{
			HeapMemoryStats before = MemoryService::instance()->get_heap_stats();
			void* allocation = Ash_Alloc(nullptr, 128, 16);
			HeapMemoryStats during = MemoryService::instance()->get_heap_stats();
			Ash_Free(nullptr, allocation);
			HeapMemoryStats after = MemoryService::instance()->get_heap_stats();

			if (!allocation)
			{
				return report_self_test_failure("MemoryService heap stats", "allocation failed");
			}
			if (during.current_allocated_bytes <= before.current_allocated_bytes)
			{
				return report_self_test_failure("MemoryService heap stats", "current bytes did not increase after allocation");
			}
			if (during.peak_allocated_bytes < during.current_allocated_bytes)
			{
				return report_self_test_failure("MemoryService heap stats", "peak bytes were lower than current bytes");
			}
			if (during.live_allocation_count <= before.live_allocation_count)
			{
				return report_self_test_failure("MemoryService heap stats", "live allocation count did not increase");
			}
			return (after.current_allocated_bytes == before.current_allocated_bytes) ||
				report_self_test_failure("MemoryService heap stats", "current bytes did not return to the original value");
		}

		auto test_process_memory_snapshot_is_available() -> bool
		{
			const ProcessMemorySnapshot snapshot = get_current_process_memory_snapshot();
#if defined(ASH_WINDOWS)
			if (!snapshot.supported)
			{
				return report_self_test_failure("Process memory snapshot", "Windows process memory snapshot reported unsupported");
			}
			if (snapshot.working_set_bytes == 0 || snapshot.private_bytes == 0)
			{
				return report_self_test_failure("Process memory snapshot", "Windows process memory counters were zero");
			}
#else
			if (snapshot.supported)
			{
				return report_self_test_failure("Process memory snapshot", "non-Windows process memory snapshot unexpectedly reported supported");
			}
#endif
			return true;
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

#if defined(ASH_HAS_VULKAN)
		auto test_vulkan_swapchain_forced_recreate_ignores_matching_extent() -> bool
		{
			const VkExtent2D current_surface_extent{ 1280u, 720u };
			const VkExtent2D active_swapchain_extent{ 1280u, 720u };
			const VkExtent2D resized_surface_extent{ 1600u, 900u };

			const bool unchanged_without_force =
				!RHI::VulkanSwapchain::should_recreate_for_surface_extent(
					true,
					false,
					current_surface_extent,
					active_swapchain_extent);
			const bool recreated_with_force =
				RHI::VulkanSwapchain::should_recreate_for_surface_extent(
					true,
					true,
					current_surface_extent,
					active_swapchain_extent);
			const bool recreated_on_extent_change =
				RHI::VulkanSwapchain::should_recreate_for_surface_extent(
					true,
					false,
					resized_surface_extent,
					active_swapchain_extent);

			return (unchanged_without_force && recreated_with_force && recreated_on_extent_change) ||
				report_self_test_failure("Vulkan swapchain forced recreate", "resize/out-of-date paths can still skip required recreation");
		}

		auto test_vulkan_acquire_wait_covers_initial_swapchain_barrier() -> bool
		{
			std::ifstream context_file("project/src/engine/Graphics/Vulkan/VulkanContext.cpp");
			if (!context_file.is_open())
			{
				return report_self_test_failure("Vulkan acquire wait stage", "failed to open VulkanContext.cpp");
			}
			std::ifstream command_buffer_file("project/src/engine/Graphics/Vulkan/VulkanCommandBuffer.cpp");
			if (!command_buffer_file.is_open())
			{
				return report_self_test_failure("Vulkan acquire wait stage", "failed to open VulkanCommandBuffer.cpp");
			}
			std::ifstream imgui_vulkan_file("project/thirdparty/ImGui/imgui_impl_vulkan.cpp");
			if (!imgui_vulkan_file.is_open())
			{
				return report_self_test_failure("Vulkan acquire wait stage", "failed to open imgui_impl_vulkan.cpp");
			}

			const std::string context_source{
				std::istreambuf_iterator<char>(context_file),
				std::istreambuf_iterator<char>()
			};
			const std::string command_buffer_source{
				std::istreambuf_iterator<char>(command_buffer_file),
				std::istreambuf_iterator<char>()
			};
			const std::string imgui_vulkan_source{
				std::istreambuf_iterator<char>(imgui_vulkan_file),
				std::istreambuf_iterator<char>()
			};
			auto count_occurrences = [](const std::string& haystack, const char* needle) -> uint32_t
			{
				uint32_t count = 0u;
				size_t position = 0u;
				const std::string_view needle_view{ needle };
				while ((position = haystack.find(needle_view, position)) != std::string::npos)
				{
					++count;
					position += needle_view.size();
				}
				return count;
			};

			const bool sync2_waits_cover_initial_barriers =
				count_occurrences(context_source, "vulkanRenderBeginSemaphore, 0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR") >= 2u;
			const bool legacy_timeline_wait_covers_initial_barriers =
				context_source.find("wait_stages.push_back(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);") != std::string::npos;
			const bool legacy_binary_wait_covers_initial_barriers =
				context_source.find("VkPipelineStageFlags flag = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;") != std::string::npos;
			const bool swapchain_barriers_join_acquire_wait_scope =
				command_buffer_source.find("pTexture->is_swapchain_image()") != std::string::npos &&
				command_buffer_source.find("srcStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;") != std::string::npos;
			const bool imgui_viewport_barrier_joins_acquire_wait_scope =
				imgui_vulkan_source.find("vkCmdPipelineBarrier(fd->CommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT") != std::string::npos;

			return (sync2_waits_cover_initial_barriers &&
				legacy_timeline_wait_covers_initial_barriers &&
				legacy_binary_wait_covers_initial_barriers &&
				swapchain_barriers_join_acquire_wait_scope &&
				imgui_viewport_barrier_joins_acquire_wait_scope) ||
				report_self_test_failure(
					"Vulkan acquire wait stage",
					"swapchain acquire semaphore waits must cover initial layout/copy/clear barriers on acquired images, including ImGui secondary viewports");
		}
#endif

		auto test_render_memory_stats_default_to_unsupported() -> bool
		{
			RHI::RenderMemoryStats stats{};
			const bool ok =
				!stats.supported &&
				stats.gpu_allocator_current_bytes == 0 &&
				stats.gpu_allocator_peak_bytes == 0 &&
				stats.gpu_allocator_shutdown_live_bytes == 0;
			return ok ||
				report_self_test_failure("RenderMemoryStats defaults", "default stats were not unsupported zeroes");
		}

		auto test_perf_gate_config_parser_defaults_to_disabled() -> bool
		{
			char arg0[] = "Sandbox.exe";
			char* argv[] = { arg0 };
			const PerfGateConfig config = parse_perf_gate_config(1, argv);
			return (!config.enabled) ||
				report_self_test_failure("PerfGate config disabled default", "parser enabled PerfGate without --perf-gate");
		}

		auto test_perf_gate_config_parser_reads_arguments() -> bool
		{
			char arg0[] = "Sandbox.exe";
			char arg1[] = "--perf-gate";
			char arg2[] = "--perf-gate-profile=Standard";
			char arg3[] = "--perf-gate-output=Intermediate/test-reports/perf-gate/test/run.json";
			char arg4[] = "--perf-gate-warmup-seconds=1.5";
			char arg5[] = "--perf-gate-sample-seconds=2.5";
			char arg6[] = "--perf-gate-target=Sandbox";
			char* argv[] = {
				arg0,
				arg1,
				arg2,
				arg3,
				arg4,
				arg5,
				arg6
			};
			const PerfGateConfig config = parse_perf_gate_config(7, argv);
			const bool ok =
				config.enabled &&
				config.profile == "Standard" &&
				config.target_name == "Sandbox" &&
				config.output_path == "Intermediate/test-reports/perf-gate/test/run.json" &&
				config.warmup_seconds == 1.5 &&
				config.sample_seconds == 2.5;
			return ok ||
				report_self_test_failure("PerfGate config parser", "parser did not preserve perf-gate arguments");
		}

		auto test_perf_gate_frame_summary_percentiles_are_stable() -> bool
		{
			std::vector<double> samples = { 0.40, 0.10, 0.20, 0.30 };
			const PerfGateFrameTimeSummary summary = summarize_perf_gate_frame_times(samples);
			const bool ok =
				summary.sample_count == 4 &&
				summary.min_ms == 0.10 &&
				summary.max_ms == 0.40 &&
				summary.avg_ms > 0.249 &&
				summary.avg_ms < 0.251 &&
				summary.p50_ms == 0.20 &&
				summary.p95_ms == 0.40 &&
				summary.p99_ms == 0.40;
			return ok ||
				report_self_test_failure("PerfGate frame summary", "percentiles or averages were not stable");
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

		static auto write_test_hdr_equirectangular(const std::filesystem::path& path) -> bool
		{
			const int width = 8;
			const int height = 4;
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			if (!output.is_open())
			{
				return false;
			}

			output << "#?RADIANCE\n";
			output << "FORMAT=32-bit_rle_rgbe\n";
			output << "\n";
			output << "-Y " << height << " +X " << width << "\n";

			auto write_rgbe = [&output](float r, float g, float b) -> void
			{
				const float max_component = std::max({ r, g, b, 1e-8f });
				const int exponent = static_cast<int>(std::floor(std::log2(max_component))) + 1;
				const float scale = std::ldexp(1.0f, 8 - exponent);
				const uint8_t rgbe[4] = {
					static_cast<uint8_t>(std::clamp(r * scale, 0.0f, 255.0f)),
					static_cast<uint8_t>(std::clamp(g * scale, 0.0f, 255.0f)),
					static_cast<uint8_t>(std::clamp(b * scale, 0.0f, 255.0f)),
					static_cast<uint8_t>(exponent + 128)
				};
				output.write(reinterpret_cast<const char*>(rgbe), sizeof(rgbe));
			};

			for (int y = 0; y < height; ++y)
			{
				for (int x = 0; x < width; ++x)
				{
					write_rgbe(
						static_cast<float>(x) / static_cast<float>(width - 1),
						static_cast<float>(y) / static_cast<float>(height - 1),
						0.25f);
				}
			}
			return output.good();
		}

		static auto write_constant_test_hdr_equirectangular(
			const std::filesystem::path& path,
			const glm::vec3& radiance) -> bool
		{
			const int width = 8;
			const int height = 4;
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			if (!output.is_open())
			{
				return false;
			}

			output << "#?RADIANCE\n";
			output << "FORMAT=32-bit_rle_rgbe\n";
			output << "\n";
			output << "-Y " << height << " +X " << width << "\n";

			auto write_rgbe = [&output](float r, float g, float b) -> void
			{
				const float max_component = std::max({ r, g, b, 1e-8f });
				const int exponent = static_cast<int>(std::floor(std::log2(max_component))) + 1;
				const float scale = std::ldexp(1.0f, 8 - exponent);
				const uint8_t rgbe[4] = {
					static_cast<uint8_t>(std::clamp(r * scale, 0.0f, 255.0f)),
					static_cast<uint8_t>(std::clamp(g * scale, 0.0f, 255.0f)),
					static_cast<uint8_t>(std::clamp(b * scale, 0.0f, 255.0f)),
					static_cast<uint8_t>(exponent + 128)
				};
				output.write(reinterpret_cast<const char*>(rgbe), sizeof(rgbe));
			};

			for (int y = 0; y < height; ++y)
			{
				for (int x = 0; x < width; ++x)
				{
					write_rgbe(radiance.r, radiance.g, radiance.b);
				}
			}
			return output.good();
		}

		static auto half_bits_to_float(uint16_t bits) -> float
		{
			const bool negative = (bits & 0x8000u) != 0u;
			const uint16_t exponent = static_cast<uint16_t>((bits >> 10u) & 0x1Fu);
			const uint16_t mantissa = static_cast<uint16_t>(bits & 0x03FFu);

			float value = 0.0f;
			if (exponent == 0u)
			{
				value = mantissa == 0u ? 0.0f : std::ldexp(static_cast<float>(mantissa), -24);
			}
			else if (exponent == 31u)
			{
				uint32_t float_bits = 0x7F800000u | (static_cast<uint32_t>(mantissa) << 13u);
				std::memcpy(&value, &float_bits, sizeof(value));
			}
			else
			{
				value = std::ldexp(
					1.0f + static_cast<float>(mantissa) / 1024.0f,
					static_cast<int>(exponent) - 15);
			}

			return negative ? -value : value;
		}

		static auto read_rgba16_rgb_pixel(
			const TextureSubresourcePayload& subresource,
			uint32_t x,
			uint32_t y) -> glm::vec3
		{
			const size_t offset = static_cast<size_t>(y) * subresource.row_pitch + static_cast<size_t>(x) * 8u;
			if (offset + 6u > subresource.pixel_data.size())
			{
				return glm::vec3(0.0f);
			}

			uint16_t half_pixels[3] = {};
			std::memcpy(half_pixels, subresource.pixel_data.data() + offset, sizeof(half_pixels));
			return glm::vec3(
				half_bits_to_float(half_pixels[0]),
				half_bits_to_float(half_pixels[1]),
				half_bits_to_float(half_pixels[2]));
		}

		static auto read_rg16_pixel(
			const Texture2DPayload& payload,
			uint32_t x,
			uint32_t y) -> glm::vec2
		{
			const size_t offset = static_cast<size_t>(y) * payload.row_pitch + static_cast<size_t>(x) * 4u;
			if (offset + 4u > payload.pixel_data.size())
			{
				return glm::vec2(0.0f);
			}

			uint16_t half_pixels[2] = {};
			std::memcpy(half_pixels, payload.pixel_data.data() + offset, sizeof(half_pixels));
			return glm::vec2(
				half_bits_to_float(half_pixels[0]),
				half_bits_to_float(half_pixels[1]));
		}

		auto test_environment_map_cpu_baker_generates_required_payloads() -> bool
		{
			const std::filesystem::path hdr_path = engine_self_test_dir() / "environment_baker_test.hdr";
			if (!write_test_hdr_equirectangular(hdr_path))
			{
				return report_self_test_failure("EnvironmentMap baker", "failed to write source HDR fixture");
			}

			EnvironmentMapBuildDesc desc{};
			desc.source_texture_path = hdr_path.string();
			desc.radiance_size = 4;
			desc.irradiance_size = 2;
			desc.prefilter_size = 4;
			desc.prefilter_mip_count = 3;
			desc.brdf_lut_size = 4;
			desc.sample_count = 64;

			EnvironmentMapCookedData data{};
			EnvironmentBakeReport report{};
			if (!EnvironmentMapBaker::bake_to_cooked_data(desc, data, &report))
			{
				return report_self_test_failure("EnvironmentMap baker", report.message.empty() ? "bake failed" : report.message.c_str());
			}

			const bool ok =
				data.radiance.subresources.size() == 6u &&
				data.irradiance.subresources.size() == 6u &&
				data.prefiltered_specular.subresources.size() == 18u &&
				data.brdf_lut.width == 4u &&
				!data.brdf_lut.pixel_data.empty();
			return ok || report_self_test_failure("EnvironmentMap baker", "baker did not generate all required payloads");
		}

		auto test_environment_map_cpu_baker_uses_irradiance_units() -> bool
		{
			const std::filesystem::path hdr_path = engine_self_test_dir() / "environment_baker_constant_test.hdr";
			const glm::vec3 source_radiance(1.0f, 0.5f, 0.25f);
			if (!write_constant_test_hdr_equirectangular(hdr_path, source_radiance))
			{
				return report_self_test_failure("EnvironmentMap baker irradiance", "failed to write constant source HDR fixture");
			}

			EnvironmentMapBuildDesc desc{};
			desc.source_texture_path = hdr_path.string();
			desc.radiance_size = 2;
			desc.irradiance_size = 1;
			desc.prefilter_size = 1;
			desc.prefilter_mip_count = 1;
			desc.brdf_lut_size = 2;
			desc.sample_count = 64;

			EnvironmentMapCookedData data{};
			EnvironmentBakeReport report{};
			if (!EnvironmentMapBaker::bake_to_cooked_data(desc, data, &report))
			{
				return report_self_test_failure(
					"EnvironmentMap baker irradiance",
					report.message.empty() ? "bake failed" : report.message.c_str());
			}
			if (data.irradiance.subresources.empty())
			{
				return report_self_test_failure("EnvironmentMap baker irradiance", "bake did not produce irradiance payload");
			}

			const glm::vec3 expected_irradiance = source_radiance * glm::pi<float>();
			const glm::vec3 irradiance = read_rgba16_rgb_pixel(data.irradiance.subresources.front(), 0u, 0u);
			const bool ok =
				std::abs(irradiance.r - expected_irradiance.r) < 0.02f &&
				std::abs(irradiance.g - expected_irradiance.g) < 0.02f &&
				std::abs(irradiance.b - expected_irradiance.b) < 0.02f;
			return ok ||
				report_self_test_failure(
					"EnvironmentMap baker irradiance",
					"constant radiance must bake to hemispherical irradiance in pi-scaled units");
		}

		auto test_environment_map_cpu_baker_brdf_lut_is_energy_bounded() -> bool
		{
			const std::filesystem::path hdr_path = engine_self_test_dir() / "environment_baker_brdf_test.hdr";
			if (!write_constant_test_hdr_equirectangular(hdr_path, glm::vec3(1.0f)))
			{
				return report_self_test_failure("EnvironmentMap baker BRDF LUT", "failed to write constant source HDR fixture");
			}

			EnvironmentMapBuildDesc desc{};
			desc.source_texture_path = hdr_path.string();
			desc.radiance_size = 1;
			desc.irradiance_size = 1;
			desc.prefilter_size = 1;
			desc.prefilter_mip_count = 1;
			desc.brdf_lut_size = 16;
			desc.sample_count = 128;

			EnvironmentMapCookedData data{};
			EnvironmentBakeReport report{};
			if (!EnvironmentMapBaker::bake_to_cooked_data(desc, data, &report))
			{
				return report_self_test_failure(
					"EnvironmentMap baker BRDF LUT",
					report.message.empty() ? "bake failed" : report.message.c_str());
			}

			bool ok = data.brdf_lut.width == 16u && data.brdf_lut.height == 16u;
			for (uint32_t y = 0; ok && y < data.brdf_lut.height; ++y)
			{
				for (uint32_t x = 0; ok && x < data.brdf_lut.width; ++x)
				{
					const glm::vec2 brdf = read_rg16_pixel(data.brdf_lut, x, y);
					ok = brdf.x >= 0.0f &&
						brdf.y >= 0.0f &&
						brdf.x <= 1.05f &&
						brdf.y <= 1.05f &&
						(brdf.x + brdf.y) <= 1.05f;
				}
			}
			return ok ||
				report_self_test_failure(
					"EnvironmentMap baker BRDF LUT",
					"split-sum BRDF LUT scale/bias must not amplify constant specular environment energy");
		}

		auto test_ashibl_round_trip_uncompressed_payloads() -> bool
		{
			const std::filesystem::path path = engine_self_test_dir() / "round_trip.ashibl";

			EnvironmentMapCookedData data{};
			data.build_desc.source_texture_path = "assets/env/generated.hdr";
			data.build_desc.radiance_size = 2;
			data.build_desc.irradiance_size = 1;
			data.build_desc.prefilter_size = 2;
			data.build_desc.prefilter_mip_count = 2;
			data.build_desc.brdf_lut_size = 2;
			data.source_content_hash = 0x12345678ull;
			fill_environment_map_test_pattern(data);

			std::string error{};
			if (!write_ashibl_file(path, data, &error))
			{
				return report_self_test_failure("AshIBL round trip", error.empty() ? "write failed" : error.c_str());
			}

			EnvironmentMapCookedData loaded{};
			if (!read_ashibl_file(path, loaded, &error))
			{
				return report_self_test_failure("AshIBL round trip", error.empty() ? "read failed" : error.c_str());
			}

			auto cube_payloads_match = [](const TextureCubePayload& lhs, const TextureCubePayload& rhs) -> bool
			{
				if (lhs.format != rhs.format ||
					lhs.width != rhs.width ||
					lhs.height != rhs.height ||
					lhs.mip_count != rhs.mip_count ||
					lhs.subresources.size() != rhs.subresources.size())
				{
					return false;
				}
				for (size_t index = 0; index < lhs.subresources.size(); ++index)
				{
					const TextureSubresourcePayload& lhs_subresource = lhs.subresources[index];
					const TextureSubresourcePayload& rhs_subresource = rhs.subresources[index];
					if (lhs_subresource.mip_level != rhs_subresource.mip_level ||
						lhs_subresource.array_layer != rhs_subresource.array_layer ||
						lhs_subresource.width != rhs_subresource.width ||
						lhs_subresource.height != rhs_subresource.height ||
						lhs_subresource.row_pitch != rhs_subresource.row_pitch ||
						lhs_subresource.pixel_data != rhs_subresource.pixel_data)
					{
						return false;
					}
				}
				return true;
			};

			const bool ok =
				loaded.source_content_hash == data.source_content_hash &&
				cube_payloads_match(loaded.radiance, data.radiance) &&
				cube_payloads_match(loaded.irradiance, data.irradiance) &&
				cube_payloads_match(loaded.prefiltered_specular, data.prefiltered_specular) &&
				loaded.brdf_lut.format == data.brdf_lut.format &&
				loaded.brdf_lut.width == data.brdf_lut.width &&
				loaded.brdf_lut.height == data.brdf_lut.height &&
				loaded.brdf_lut.row_pitch == data.brdf_lut.row_pitch &&
				loaded.brdf_lut.pixel_data == data.brdf_lut.pixel_data;
			return ok || report_self_test_failure("AshIBL round trip", "loaded payloads did not match written data");
		}

		auto test_environment_asset_key_and_fallback_policy() -> bool
		{
			const std::string cooked_key = make_environment_map_asset_key("assets/env/studio.ashibl", "");
			const std::string runtime_key = make_environment_map_asset_key("", "assets/env/studio.hdr");
			const std::string fallback_key = make_environment_map_asset_key("", "");

			const bool ok =
				cooked_key == "ashibl:assets/env/studio.ashibl" &&
				runtime_key == "source:assets/env/studio.hdr" &&
				fallback_key == "fallback:";
			return ok || report_self_test_failure("Environment asset key", "environment asset keys are not stable");
		}

		auto test_environment_source_cache_path_uses_content_hash() -> bool
		{
			const std::filesystem::path hdr_path = engine_self_test_dir() / "environment_cache_hash_test.hdr";
			if (!write_test_hdr_equirectangular(hdr_path))
			{
				return report_self_test_failure("Environment source cache path", "failed to write source HDR fixture");
			}

			const uint64_t source_hash = hash_environment_source_file(hdr_path);
			const std::filesystem::path cache_path = make_environment_map_source_cache_path(source_hash);
			const bool ok =
				source_hash != 0ull &&
				cache_path.parent_path().generic_string() == "product/caches/EnvironmentCaches" &&
				cache_path.filename().generic_string() == std::to_string(source_hash) + ".ashibl";
			return ok || report_self_test_failure("Environment source cache path", "source cache path must be stable and content-hash based");
		}

		auto test_environment_runtime_source_bake_does_not_block_request_path() -> bool
		{
			std::ifstream source_file("project/src/engine/Function/Render/RenderAssetManager.cpp");
			std::stringstream source_buffer{};
			source_buffer << source_file.rdbuf();
			const std::string source = source_buffer.str();

			const bool ok =
				source.find("hash_environment_source_file") != std::string::npos &&
				source.find("make_environment_map_source_cache_path") != std::string::npos &&
				source.find("EnvironmentMapBaker::bake_to_cooked_data") == std::string::npos;
			return ok || report_self_test_failure("Environment runtime bake", "runtime source bake must not block the render request path");
		}

		auto test_environment_shaders_use_compact_root_constants() -> bool
		{
			std::ifstream lighting_file("project/src/engine/Shaders/Deferred/DeferredEnvironmentLighting.hlsl");
			std::ifstream sky_file("project/src/engine/Shaders/Deferred/SkyBackground.hlsl");
			std::ifstream common_file("project/src/engine/Shaders/Deferred/EnvironmentCommon.hlsli");
			std::stringstream lighting_buffer{};
			std::stringstream sky_buffer{};
			std::stringstream common_buffer{};
			lighting_buffer << lighting_file.rdbuf();
			sky_buffer << sky_file.rdbuf();
			common_buffer << common_file.rdbuf();
			const std::string lighting_source = lighting_buffer.str();
			const std::string sky_source = sky_buffer.str();
			const std::string common_source = common_buffer.str();

			const bool ok =
				lighting_source.find("#include \"DeferredCommon.hlsli\"") == std::string::npos &&
				sky_source.find("#include \"DeferredCommon.hlsli\"") == std::string::npos &&
				common_source.find("float4 AshEnvironmentParams") != std::string::npos &&
				common_source.find("AshLightWorldToClip") == std::string::npos;
			return ok || report_self_test_failure("Environment compact root constants", "environment shaders must stay within the DX12 root signature budget");
		}

		auto test_environment_shader_applies_lambert_to_irradiance() -> bool
		{
			std::ifstream lighting_file("project/src/engine/Shaders/Deferred/DeferredEnvironmentLighting.hlsl");
			std::ifstream common_file("project/src/engine/Shaders/Deferred/EnvironmentCommon.hlsli");
			std::stringstream lighting_buffer{};
			std::stringstream common_buffer{};
			lighting_buffer << lighting_file.rdbuf();
			common_buffer << common_file.rdbuf();
			const std::string lighting_source = lighting_buffer.str();
			const std::string common_source = common_buffer.str();

			const bool ok =
				common_source.find("AshDiffuseLambert") != std::string::npos &&
				lighting_source.find("AshDiffuseLambert(surface.base_color)") != std::string::npos &&
				lighting_source.find("irradiance * surface.base_color") == std::string::npos;
			return ok ||
				report_self_test_failure(
					"Environment Lambert diffuse",
					"diffuse IBL must apply the Lambert BRDF to baked irradiance");
		}

		auto test_environment_passes_use_split_intensity_controls() -> bool
		{
			std::ifstream lighting_file("project/src/engine/Function/Render/EnvironmentLightingPass.cpp");
			std::ifstream sky_file("project/src/engine/Function/Render/SkyBackgroundPass.cpp");
			std::stringstream lighting_buffer{};
			std::stringstream sky_buffer{};
			lighting_buffer << lighting_file.rdbuf();
			sky_buffer << sky_file.rdbuf();
			const std::string lighting_source = lighting_buffer.str();
			const std::string sky_source = sky_buffer.str();

			const bool ok =
				lighting_source.find("environment.intensity * environment.lighting_intensity") != std::string::npos &&
				sky_source.find("environment.intensity * environment.background_intensity") != std::string::npos;
			return ok ||
				report_self_test_failure(
					"Environment split intensity",
					"sky background and IBL lighting must use separate EnvironmentComponent intensity multipliers");
		}

		auto test_texture_cube_upload_contract() -> bool
		{
			const bool rg16_ok =
				render_texture_format_to_rhi(RenderTextureFormat::RG16_SFLOAT) == RHI::ASH_FORMAT_R16G16_SFLOAT &&
				calculate_render_texture_tight_row_pitch(RenderTextureFormat::RG16_SFLOAT, 4u) == 16u;
			if (!rg16_ok)
			{
				return report_self_test_failure("Texture cube upload contract", "RG16_SFLOAT format mapping or row pitch is invalid");
			}

			std::array<uint8_t, 8u * 4u * 4u * 6u> pixels{};
			std::array<TextureSubresourceUploadDesc, 6> faces{};
			for (uint32_t face = 0; face < 6; ++face)
			{
				faces[face].mip_level = 0;
				faces[face].array_layer = face;
				faces[face].data = pixels.data() + face * 8u * 4u * 4u;
				faces[face].row_pitch = 8u * 4u;
				faces[face].slice_pitch = 8u * 4u * 4u;
			}

			TextureCubeUploadDesc desc{};
			desc.width = 4;
			desc.height = 4;
			desc.format = RenderTextureFormat::RGBA16_SFLOAT;
			desc.mip_level_count = 1;
			desc.subresources = faces.data();
			desc.subresource_count = static_cast<uint32_t>(faces.size());

			std::string error{};
			const bool valid_ok = validate_texture_cube_upload_desc(desc, &error);
			desc.subresource_count = 5;
			const bool invalid_rejected = !validate_texture_cube_upload_desc(desc, &error);
			return (valid_ok && invalid_rejected) ||
				report_self_test_failure("Texture cube upload contract", "valid cube desc was rejected or invalid desc was accepted");
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

		auto test_render_feature_config_registers_vsync_without_reverse_z() -> bool
		{
			const std::filesystem::path test_dir = engine_self_test_dir();
			const std::filesystem::path config_path = test_dir / "render_feature_self_test.ini";
			{
				std::ofstream config_file(config_path, std::ios::trunc);
				config_file <<
					"[Rendering]\n"
					"ReverseZ=true\n"
					"VSync=true\n";
			}

			const RenderFeatureConfig config = load_runtime_render_feature_config(config_path.string().c_str());
			uint32_t descriptor_count = 0;
			const RenderSwitchDescriptor* descriptors = get_render_switch_descriptors(descriptor_count);
			bool found_reverse_z = false;
			bool found_vsync = false;
			bool vsync_enabled = false;
			for (uint32_t index = 0; index < descriptor_count; ++index)
			{
				const RenderSwitchDescriptor& descriptor = descriptors[index];
				if (descriptor.section != nullptr &&
					descriptor.key != nullptr &&
					std::string(descriptor.section) == "Rendering" &&
					std::string(descriptor.key) == "ReverseZ")
				{
					found_reverse_z = true;
					break;
				}
				if (descriptor.section != nullptr &&
					descriptor.key != nullptr &&
					std::string(descriptor.section) == "Rendering" &&
					std::string(descriptor.key) == "VSync")
				{
					found_vsync = true;
					vsync_enabled = config.is_enabled(descriptor.id);
				}
			}

			return (!found_reverse_z && found_vsync && vsync_enabled) ||
				report_self_test_failure("Render feature config", "VSync should be registered as an Engine.ini render switch while ReverseZ remains camera-local");
		}

		auto test_engine_ini_excludes_scene_render_config_sections() -> bool
		{
			std::ifstream engine_ini("product/config/Engine.ini");
			if (!engine_ini.is_open())
			{
				return report_self_test_failure("Engine.ini config authority", "failed to open product/config/Engine.ini");
			}
			const std::string engine_ini_source{
				std::istreambuf_iterator<char>(engine_ini),
				std::istreambuf_iterator<char>() };
			if (engine_ini_source.find("[AmbientOcclusion]") != std::string::npos ||
				engine_ini_source.find("[DirectionalShadows]") != std::string::npos ||
				engine_ini_source.find("[Bloom]") != std::string::npos ||
				engine_ini_source.find("[VolumetricLighting]") != std::string::npos ||
				engine_ini_source.find("[ToneMap]") != std::string::npos)
			{
				return report_self_test_failure("Engine.ini config authority", "scene render config sections must live in scene JSON, not Engine.ini");
			}

			const char* obsolete_tokens[] = {
				"load_runtime_ambient_occlusion_config",
				"set_runtime_ambient_occlusion_config",
				"get_runtime_ambient_occlusion_config",
				"load_runtime_directional_shadow_config",
				"set_runtime_directional_shadow_config",
				"get_runtime_directional_shadow_config"
			};
			const char* files_to_check[] = {
				"project/src/engine/Function/Application.cpp",
				"project/src/engine/Function/Render/AmbientOcclusionConfig.h",
				"project/src/engine/Function/Render/AmbientOcclusionConfig.cpp",
				"project/src/engine/Function/Render/DirectionalShadowConfig.h",
				"project/src/engine/Function/Render/DirectionalShadowConfig.cpp"
			};

			for (const char* file_path : files_to_check)
			{
				std::ifstream source_file(file_path);
				if (!source_file.is_open())
				{
					return report_self_test_failure("Engine.ini config authority", "failed to open source file for config authority check");
				}
				const std::string source{
					std::istreambuf_iterator<char>(source_file),
					std::istreambuf_iterator<char>() };
				for (const char* token : obsolete_tokens)
				{
					if (source.find(token) != std::string::npos)
					{
						return report_self_test_failure("Engine.ini config authority", "scene-owned AO or directional shadow runtime INI APIs must not be present");
					}
				}
			}

			return true;
		}

		auto test_bloom_config_defaults_and_sanitization() -> bool
		{
			BloomConfig defaults = make_default_bloom_config();
			if (defaults.enabled ||
				defaults.quality != BloomQuality::High ||
				defaults.intensity != 0.6f ||
				defaults.threshold != 1.0f ||
				defaults.soft_knee != 0.5f ||
				defaults.size_scale != 1.0f ||
				defaults.debug_view != BloomDebugView::Off ||
				defaults.stages.size() != 6u)
			{
				return report_self_test_failure("Bloom config", "default bloom config does not match the design contract");
			}

			BloomQuality quality = BloomQuality::Low;
			BloomDebugView debug_view = BloomDebugView::Off;
			if (!try_parse_bloom_quality("Epic", quality) || quality != BloomQuality::Epic)
			{
				return report_self_test_failure("Bloom config", "failed to parse Epic quality");
			}
			if (!try_parse_bloom_debug_view("CompositeHDR", debug_view) || debug_view != BloomDebugView::CompositeHDR)
			{
				return report_self_test_failure("Bloom config", "failed to parse CompositeHDR debug view");
			}

			BloomConfig invalid = defaults;
			invalid.enabled = true;
			invalid.intensity = -4.0f;
			invalid.threshold = 1000.0f;
			invalid.soft_knee = -1.0f;
			invalid.size_scale = 50.0f;
			invalid.stages[0].size = -8.0f;
			invalid.stages[0].tint = glm::vec3(-1.0f, 16.0f, 0.5f);

			const BloomConfig sanitized = sanitize_bloom_config(invalid, defaults);
			const bool sanitized_ok =
				sanitized.enabled &&
				sanitized.intensity == 0.0f &&
				sanitized.threshold == 64.0f &&
				sanitized.soft_knee == 0.0f &&
				sanitized.size_scale == 8.0f &&
				sanitized.stages[0].size == 0.0f &&
				sanitized.stages[0].tint.x == 0.0f &&
				sanitized.stages[0].tint.y == 8.0f &&
				sanitized.stages[0].tint.z == 0.5f;
			if (!sanitized_ok)
			{
				return report_self_test_failure("Bloom config", "sanitize_bloom_config did not clamp fields as expected");
			}

			return true;
		}

		auto test_volumetric_lighting_config_defaults_and_sanitization() -> bool
		{
			VolumetricLightingConfig defaults = make_default_volumetric_lighting_config();
			if (defaults.enabled ||
				defaults.quality != VolumetricLightingQuality::Medium ||
				defaults.froxel_resolution_scale != 0.5f ||
				defaults.froxel_depth_slices != 64u ||
				defaults.max_lights != 64u ||
				defaults.density != 0.02f ||
				defaults.scattering_intensity != 1.0f ||
				defaults.extinction_scale != 1.0f ||
				defaults.anisotropy != 0.35f ||
				!defaults.history ||
				defaults.history_blend != 0.9f ||
				defaults.screen_space_fallback ||
				defaults.debug_view != VolumetricLightingDebugView::Off)
			{
				return report_self_test_failure("VolumetricLighting config", "default config does not match design contract");
			}

			VolumetricLightingQuality quality = VolumetricLightingQuality::Low;
			VolumetricLightingDebugView debug_view = VolumetricLightingDebugView::Off;
			if (!try_parse_volumetric_lighting_quality("Epic", quality) || quality != VolumetricLightingQuality::Epic)
			{
				return report_self_test_failure("VolumetricLighting config", "failed to parse Epic quality");
			}
			if (!try_parse_volumetric_lighting_debug_view("IntegratedLighting", debug_view) ||
				debug_view != VolumetricLightingDebugView::IntegratedLighting)
			{
				return report_self_test_failure("VolumetricLighting config", "failed to parse IntegratedLighting debug view");
			}

			VolumetricLightingConfig invalid = defaults;
			invalid.enabled = true;
			invalid.froxel_resolution_scale = 2.0f;
			invalid.froxel_depth_slices = 4096u;
			invalid.max_lights = 10000u;
			invalid.density = -4.0f;
			invalid.scattering_intensity = -8.0f;
			invalid.extinction_scale = -2.0f;
			invalid.anisotropy = 4.0f;
			invalid.history_blend = 1.0f;

			const VolumetricLightingConfig sanitized =
				sanitize_volumetric_lighting_config(invalid, defaults);
			const bool sanitized_ok =
				sanitized.enabled &&
				sanitized.froxel_resolution_scale == 1.0f &&
				sanitized.froxel_depth_slices == 128u &&
				sanitized.max_lights == 256u &&
				sanitized.density == 0.0f &&
				sanitized.scattering_intensity == 0.0f &&
				sanitized.extinction_scale == 0.0f &&
				sanitized.anisotropy == 0.95f &&
				sanitized.history_blend == 0.98f;
			if (!sanitized_ok)
			{
				return report_self_test_failure("VolumetricLighting config", "sanitize did not clamp fields as expected");
			}

			return true;
		}

		auto test_bloom_shader_source_contract() -> bool
		{
			const bool setup_ok = file_contains_all(
				"project/src/engine/Shaders/Deferred/BloomSetup.hlsl",
				{ "VSMain", "PSMain", "SceneHDRLinear", "AshBloomThresholdSoftKnee", "SceneLinearClampSampler" });
			const bool downsample_ok = file_contains_all(
				"project/src/engine/Shaders/Deferred/BloomDownsample.hlsl",
				{ "VSMain", "PSMain", "BloomInput", "AshBloomSourceSize", "SceneLinearClampSampler" });
			const bool upsample_ok = file_contains_all(
				"project/src/engine/Shaders/Deferred/BloomUpsample.hlsl",
				{ "VSMain", "PSMain", "BloomLowInput", "BloomHighInput", "AshBloomStageTintRadius", "SceneLinearClampSampler" });
			const bool composite_ok = file_contains_all(
				"project/src/engine/Shaders/Deferred/BloomComposite.hlsl",
				{ "VSMain", "PSMain", "SceneHDRLinear", "SceneBloomFinal", "AshBloomCompositeParams", "SceneLinearClampSampler" });

			return (setup_ok && downsample_ok && upsample_ok && composite_ok) ||
				report_self_test_failure("Bloom shader source contract", "bloom shaders are missing required entry points or binding names");
		}

		auto test_bloom_pass_source_contract() -> bool
		{
			const bool header_ok = file_contains_all(
				"project/src/engine/Function/Render/BloomPass.h",
				{ "BloomPassOutputs", "final_bloom", "composite_hdr", "std::array<RenderGraphTextureRef, 6>" });
			const bool source_ok = file_contains_all(
				"project/src/engine/Function/Render/BloomPass.cpp",
				{
					"quality_mip_count",
					"select_debug_texture",
					"sanitize_bloom_config",
					"sanitized_config.enabled",
					"SceneBloomSetupPass",
					"SceneBloomDownsamplePass",
					"SceneBloomUpsamplePass",
					"SceneBloomCompositePass",
					"RenderTextureFormat::RGBA16_SFLOAT",
					"RenderGraphAccess::GraphicsSRV",
					"ASH_PROFILE_SCOPE_NC"
				});
			return (header_ok && source_ok) ||
				report_self_test_failure("Bloom pass source contract", "BloomPass is missing the RenderGraph pass chain or debug selection contract");
		}

		auto test_volumetric_lighting_shader_source_contract() -> bool
		{
			const bool common_ok = file_contains_all(
				"project/src/engine/Shaders/Deferred/VolumetricLightingCommon.hlsli",
				{
					"AshVolumetricFullscreen",
					"AshVolumetricPhaseHG",
					"AshVolumetricAtlasUV",
					"AshVolumetricSceneDepthIsBackground",
					"AshVolumetricReconstructWorldPosition",
					"AshVolumetricReconstructWorldPositionAtViewDepth",
					"AshVolumetricSliceViewDepth",
					"AshVolumetricVisibleDepth01(float2 uv, float scene_depth)",
					"AshHistoryViewProjection",
					"AshVolumetricAtlasUVFromTileUV",
					"AshVolumetricCurrentViewForwardWS",
					"AshRootConstants"
				});
			const bool density_ok = file_contains_all(
				"project/src/engine/Shaders/Deferred/VolumetricDensity.hlsl",
				{ "CSMain", "SceneVolumetricDensity", "AshVolumetricConfig0" });
			const bool injection_ok = file_contains_all(
				"project/src/engine/Shaders/Deferred/VolumetricLightInjection.hlsl",
				{
					"CSMain",
					"SceneVolumetricDensity",
					"SceneVolumetricScattering",
					"SceneVolumetricLights",
					"AshVolumetricReconstructWorldPositionAtViewDepth",
					"AshVolumetricPhaseHG",
					"DirectionalShadowDynamicAtlas",
					"SceneDirectionalShadowCascades",
					"AshVolumetricSampleSunlightShadow",
					"ComputeCascadeTransitionWeight",
					"view_depth >= cascade.split_depth_bias.x && view_depth <= cascade.split_depth_bias.y",
					"kVolumetricScatteringDensityNormalization",
					"kVolumetricDirectionalVisibilityScale",
					"kVolumetricLocalVisibilityScale",
					"light.cone_shadow.w",
					"sunlight_shadow"
				});
			const bool temporal_ok = file_contains_all(
				"project/src/engine/Shaders/Deferred/VolumetricTemporal.hlsl",
				{
					"CSMain",
					"SceneVolumetricScattering",
					"SceneVolumetricScatteringHistory",
					"SceneVolumetricHistoryValidity",
					"AshVolumetricReprojectHistoryUV",
					"AshVolumetricPreviousViewDepth01",
					"AshVolumetricClampHistory",
					"previous_max_view_depth = max(AshVolumetricConfig0.w, 0.01)",
					"float blend = saturate(AshVolumetricConfig1.y)"
				});
			const bool integrate_ok = file_contains_all(
				"project/src/engine/Shaders/Deferred/VolumetricIntegrate.hlsl",
				{
					"CSMain",
					"SceneDepth",
					"SceneVolumetricScatteringTemporal",
					"SceneVolumetricIntegratedLighting",
					"AshVolumetricConfig1",
					"segment_length",
					"segment_view_length = segment_length * AshVolumetricMaxViewDepth()",
					"kVolumetricScatteringWorldScale",
					"lighting += scattering.rgb * transmittance * segment_view_length * kVolumetricScatteringWorldScale",
					"kVolumetricExtinctionWorldScale",
					"SceneVolumetricIntegratedLighting[dispatch_id.xy] = float4(lighting, transmittance)"
				});
			const bool composite_ok = file_contains_all(
				"project/src/engine/Shaders/Deferred/VolumetricComposite.hlsl",
				{
					"VSMain",
					"PSMain",
					"SceneHDRLinear",
					"SceneVolumetricIntegratedLighting",
					"hdr * transmittance + volumetric"
				});
			const bool fallback_ok = file_contains_all(
				"project/src/engine/Shaders/Deferred/LightShaftScreenSpace.hlsl",
				{ "VSMain", "PSMain", "SceneDepth", "PSScreenSpaceLightShaftOutput" });

			return (common_ok && density_ok && injection_ok && temporal_ok && integrate_ok && composite_ok && fallback_ok) ||
				report_self_test_failure("VolumetricLighting shader source contract", "shader sources are missing required entry points or binding names");
		}

		auto test_volumetric_lighting_pass_source_contract() -> bool
		{
			const bool header_ok = file_contains_all(
				"project/src/engine/Function/Render/VolumetricLightingPass.h",
				{
					"VolumetricLightingPassOutputs",
					"SceneVolumetricCompositeHDR",
					"atlas_width",
					"atlas_height",
					"add_passes",
					"VolumetricHistoryEntry",
					"view_projection",
					"view_forward",
					"light_scene_revision",
					"m_history_entries"
				});
			const bool source_ok = file_contains_all(
				"project/src/engine/Function/Render/VolumetricLightingPass.cpp",
				{
					"SceneVolumetricDensityPass",
					"SceneVolumetricLightInjectionPass",
					"SceneVolumetricTemporalPass",
					"SceneVolumetricIntegratePass",
					"SceneVolumetricCompositePass",
					"select_debug_texture",
					"SceneLightShaftScreenSpacePass",
					"RenderGraphAccess::ComputeUAV",
					"RenderGraphAccess::ComputeSRV",
					"ASH_PROFILE_SCOPE_NC",
					"m_density_program->set_rw_texture(\"SceneVolumetricDensity\"",
					"m_light_injection_program->set_texture(\"SceneVolumetricDensity\"",
					"deferred_resources.sunlight_shadow_dynamic_atlas",
					"m_light_injection_program->set_texture(\"DirectionalShadowDynamicAtlas\"",
					"m_light_injection_program->set_storage_buffer(\"SceneVolumetricLights\"",
					"m_light_injection_program->set_storage_buffer(\"SceneDirectionalShadowCascades\"",
					"m_integrate_program->set_texture(\"SceneDepth\"",
					"m_integrate_program->set_rw_texture(\"SceneVolumetricIntegratedLighting\"",
					"m_composite_program->set_texture(\"SceneHDRLinear\"",
					"SceneVolumetricScatteringHistory",
					"SceneVolumetricHistoryWrite",
					"history_state_compatible",
					"history_view_projection",
					"static_assert(sizeof(VolumetricRootConstants) <= 224u)",
					"screen_light_position_and_params = glm::vec4(history_entry->camera_position",
					"config0 = glm::vec4(history_entry->view_forward",
					"temporal_constants.config1.y = history_state_compatible ? sanitized.history_blend : 0.0f",
					"m_logged_runtime_state",
					"sanitized.debug_view",
					"outputs.scene_hdr_linear = debug_texture",
					"context.dispatch",
					"context.draw"
				});

			return (header_ok && source_ok) ||
				report_self_test_failure("VolumetricLighting pass source contract", "pass source is missing graph or profiling contract");
		}

		auto test_volumetric_lighting_pass_adds_expected_graph_chain_for_tests() -> bool
		{
			RenderGraphBuilder graph = RenderGraphBuilder::create_headless_for_tests("VolumetricLightingGraphSelfTest");
			RenderTargetDesc hdr_desc{};
			hdr_desc.width = 128;
			hdr_desc.height = 64;
			hdr_desc.format = RenderTextureFormat::RGBA16_SFLOAT;
			hdr_desc.shader_resource = true;
			RenderTargetDesc depth_desc = hdr_desc;
			depth_desc.format = RenderTextureFormat::D32_SFLOAT;
			RenderGraphTextureRef hdr = graph.register_external_texture_desc_for_tests(hdr_desc, "SceneHDRLinear");
			RenderGraphTextureRef depth = graph.register_external_texture_desc_for_tests(depth_desc, "SceneDeferredDepth");

			VolumetricLightingConfig config = make_default_volumetric_lighting_config();
			config.enabled = true;
			config.history = true;
			config.screen_space_fallback = false;

			const bool ok = VolumetricLightingPass::add_passes_for_tests(graph, hdr, depth, 128, 64, config);
			if (!ok)
			{
				return report_self_test_failure("VolumetricLighting graph", "test graph helper failed");
			}

			const std::vector<RenderGraphPassNode>& passes = graph.get_passes_for_tests();
			const std::vector<RenderGraphTextureNode>& textures = graph.get_textures_for_tests();
			const auto has_pass = [&passes](const char* name) -> bool
			{
				return std::any_of(passes.begin(), passes.end(), [name](const RenderGraphPassNode& pass)
				{
					return pass.name == name;
				});
			};
			const auto has_texture = [&textures](const char* name) -> bool
			{
				return std::any_of(textures.begin(), textures.end(), [name](const RenderGraphTextureNode& texture)
				{
					return texture.name == name;
				});
			};
			const auto pass_reads_texture = [&passes](const char* pass_name, RenderGraphTextureRef texture, RenderGraphAccess access) -> bool
			{
				return std::any_of(passes.begin(), passes.end(), [pass_name, texture, access](const RenderGraphPassNode& pass)
				{
					if (pass.name != pass_name)
					{
						return false;
					}
					return std::any_of(pass.texture_usages.begin(), pass.texture_usages.end(), [texture, access](const RenderGraphTextureUsage& usage)
					{
						return usage.texture == texture && usage.access == access;
					});
				});
			};

			const bool graph_ok =
				has_pass("SceneVolumetricDensityPass") &&
				has_pass("SceneVolumetricLightInjectionPass") &&
				has_pass("SceneVolumetricTemporalPass") &&
				has_pass("SceneVolumetricIntegratePass") &&
				has_pass("SceneVolumetricCompositePass") &&
				has_texture("SceneVolumetricDensity") &&
				has_texture("SceneVolumetricScattering") &&
				has_texture("SceneVolumetricScatteringTemporal") &&
				has_texture("SceneVolumetricIntegratedLighting") &&
				has_texture("SceneVolumetricCompositeHDR") &&
				pass_reads_texture("SceneVolumetricIntegratePass", depth, RenderGraphAccess::ComputeSRV);
			if (!graph_ok)
			{
				return report_self_test_failure("VolumetricLighting graph", "graph chain is missing expected passes or textures");
			}

			RenderGraphBuilder fallback_graph = RenderGraphBuilder::create_headless_for_tests("VolumetricLightingFallbackGraphSelfTest");
			RenderGraphTextureRef fallback_hdr = fallback_graph.register_external_texture_desc_for_tests(hdr_desc, "SceneHDRLinear");
			RenderGraphTextureRef fallback_depth = fallback_graph.register_external_texture_desc_for_tests(depth_desc, "SceneDeferredDepth");
			config.screen_space_fallback = true;
			const bool fallback_added = VolumetricLightingPass::add_passes_for_tests(
				fallback_graph,
				fallback_hdr,
				fallback_depth,
				128,
				64,
				config);
			if (!fallback_added)
			{
				return report_self_test_failure("VolumetricLighting graph", "fallback graph helper failed");
			}

			const std::vector<RenderGraphPassNode>& fallback_passes = fallback_graph.get_passes_for_tests();
			const std::vector<RenderGraphTextureNode>& fallback_textures = fallback_graph.get_textures_for_tests();
			const auto has_fallback_pass = [&fallback_passes](const char* name) -> bool
			{
				return std::any_of(fallback_passes.begin(), fallback_passes.end(), [name](const RenderGraphPassNode& pass)
				{
					return pass.name == name;
				});
			};
			const auto has_fallback_texture = [&fallback_textures](const char* name) -> bool
			{
				return std::any_of(fallback_textures.begin(), fallback_textures.end(), [name](const RenderGraphTextureNode& texture)
				{
					return texture.name == name;
				});
			};
			const bool fallback_ok =
				has_fallback_pass("SceneLightShaftScreenSpacePass") &&
				has_fallback_texture("SceneLightShaftOcclusionMask") &&
				has_fallback_texture("SceneLightShaftScreenSpaceCompositeHDR");
			return fallback_ok ||
				report_self_test_failure("VolumetricLighting graph", "screen-space fallback pass or outputs were not added");
		}

		auto test_volumetric_lighting_atlas_budget_contract() -> bool
		{
			RenderGraphBuilder graph = RenderGraphBuilder::create_headless_for_tests("VolumetricLightingAtlasBudgetSelfTest");
			RenderTargetDesc hdr_desc{};
			hdr_desc.width = 1920;
			hdr_desc.height = 1080;
			hdr_desc.format = RenderTextureFormat::RGBA16_SFLOAT;
			hdr_desc.shader_resource = true;
			RenderTargetDesc depth_desc = hdr_desc;
			depth_desc.format = RenderTextureFormat::D32_SFLOAT;
			RenderGraphTextureRef hdr = graph.register_external_texture_desc_for_tests(hdr_desc, "SceneHDRLinear");
			RenderGraphTextureRef depth = graph.register_external_texture_desc_for_tests(depth_desc, "SceneDeferredDepth");

			VolumetricLightingConfig config = make_default_volumetric_lighting_config();
			config.enabled = true;
			config.quality = VolumetricLightingQuality::High;
			config.froxel_resolution_scale = 0.5f;
			config.froxel_depth_slices = 64u;
			config.history = true;
			config.screen_space_fallback = false;

			if (!VolumetricLightingPass::add_passes_for_tests(graph, hdr, depth, 1920, 1080, config))
			{
				return report_self_test_failure("VolumetricLighting atlas budget", "test graph helper failed");
			}

			const std::vector<RenderGraphTextureNode>& textures = graph.get_textures_for_tests();
			const auto density_it = std::find_if(textures.begin(), textures.end(), [](const RenderGraphTextureNode& texture)
			{
				return texture.name == "SceneVolumetricDensity";
			});
			if (density_it == textures.end())
			{
				return report_self_test_failure("VolumetricLighting atlas budget", "density texture is missing");
			}

			const uint64_t atlas_pixels =
				static_cast<uint64_t>(density_it->desc.width) * static_cast<uint64_t>(density_it->desc.height);
			const bool budget_ok =
				density_it->desc.width < 7680u &&
				density_it->desc.height < 4320u &&
				atlas_pixels <= 2ull * 1024ull * 1024ull;
			return budget_ok ||
				report_self_test_failure("VolumetricLighting atlas budget", "high-quality 1080p froxel atlas exceeds the runtime memory budget");
		}

		auto test_resize_clears_render_size_caches_contract() -> bool
		{
			std::ifstream application_file("project/src/engine/Function/Application.cpp");
			std::ifstream render_device_file("project/src/engine/Function/Render/RenderDevice.cpp");
			std::ifstream scene_renderer_header_file("project/src/engine/Function/Render/SceneRenderer.h");
			std::ifstream scene_renderer_source_file("project/src/engine/Function/Render/SceneRenderer.cpp");
			std::ifstream volumetric_header_file("project/src/engine/Function/Render/VolumetricLightingPass.h");
			if (!application_file.is_open() ||
				!render_device_file.is_open() ||
				!scene_renderer_header_file.is_open() ||
				!scene_renderer_source_file.is_open() ||
				!volumetric_header_file.is_open())
			{
				return report_self_test_failure("Resize render cache contract", "failed to open render resize source files");
			}

			const std::string application_source{
				std::istreambuf_iterator<char>(application_file),
				std::istreambuf_iterator<char>() };
			const std::string render_device_source{
				std::istreambuf_iterator<char>(render_device_file),
				std::istreambuf_iterator<char>() };
			const std::string scene_renderer_header{
				std::istreambuf_iterator<char>(scene_renderer_header_file),
				std::istreambuf_iterator<char>() };
			const std::string scene_renderer_source{
				std::istreambuf_iterator<char>(scene_renderer_source_file),
				std::istreambuf_iterator<char>() };
			const std::string volumetric_header{
				std::istreambuf_iterator<char>(volumetric_header_file),
				std::istreambuf_iterator<char>() };

			const bool application_ok =
				application_source.find("WindowEventType::Resize") != std::string::npos &&
				application_source.find("renderer->clear_transient_render_targets()") != std::string::npos &&
				application_source.find("sceneRenderer.handle_output_resized()") != std::string::npos;
			const bool render_device_ok =
				render_device_source.find("last_swapchain_width") != std::string::npos &&
				render_device_source.find("last_swapchain_height") != std::string::npos &&
				render_device_source.find("RenderDevice: swapchain extent changed") != std::string::npos &&
				render_device_source.find("clear_transient_render_targets()") != std::string::npos;
			const bool scene_renderer_ok =
				scene_renderer_header.find("handle_output_resized") != std::string::npos &&
				scene_renderer_source.find("m_volumetric_lighting_pass.clear_history()") != std::string::npos;
			const bool volumetric_ok =
				volumetric_header.find("clear_history") != std::string::npos;

			return (application_ok && render_device_ok && scene_renderer_ok && volumetric_ok) ||
				report_self_test_failure("Resize render cache contract", "window resize must clear transient RT caches and persistent volumetric history");
		}

		auto test_scene_renderer_bloom_integration_contract() -> bool
		{
			std::ifstream header_file("project/src/engine/Function/Render/SceneRenderer.h");
			std::ifstream source_file("project/src/engine/Function/Render/SceneRenderer.cpp");
			if (!header_file.is_open() || !source_file.is_open())
			{
				return report_self_test_failure("SceneRenderer bloom integration", "failed to open SceneRenderer source files");
			}
			const std::string header{
				std::istreambuf_iterator<char>(header_file),
				std::istreambuf_iterator<char>()
			};
			const std::string source{
				std::istreambuf_iterator<char>(source_file),
				std::istreambuf_iterator<char>()
			};

			const bool header_ok =
				header.find("#include \"Function/Render/BloomPass.h\"") != std::string::npos &&
				header.find("BloomPass m_bloom_pass") != std::string::npos;
			const size_t sky_pos = source.find("m_sky_background_pass.add_pass");
			const size_t bloom_pos = source.find("m_bloom_pass.add_passes");
			const size_t tone_pos = source.find("m_post_process_tone_map_pass.add_pass");
			const bool order_ok =
				sky_pos != std::string::npos &&
				bloom_pos != std::string::npos &&
				tone_pos != std::string::npos &&
				sky_pos < bloom_pos &&
				bloom_pos < tone_pos;
			const bool debug_ok =
				source.find("\"SceneBloomSetup\"") != std::string::npos &&
				source.find("\"SceneBloomMip\"") != std::string::npos &&
				source.find("\"SceneBloomFinal\"") != std::string::npos &&
				source.find("\"SceneBloomCompositeHDR\"") != std::string::npos;

			return (header_ok && order_ok && debug_ok) ||
				report_self_test_failure("SceneRenderer bloom integration", "bloom pass is not owned, ordered, or debug-registered correctly");
		}

		auto test_scene_renderer_volumetric_lighting_integration_contract() -> bool
		{
			std::ifstream header_file("project/src/engine/Function/Render/SceneRenderer.h");
			std::ifstream source_file("project/src/engine/Function/Render/SceneRenderer.cpp");
			if (!header_file.is_open() || !source_file.is_open())
			{
				return report_self_test_failure("SceneRenderer volumetric lighting integration", "failed to open SceneRenderer source files");
			}
			const std::string header{
				std::istreambuf_iterator<char>(header_file),
				std::istreambuf_iterator<char>()
			};
			const std::string source{
				std::istreambuf_iterator<char>(source_file),
				std::istreambuf_iterator<char>()
			};

			const bool header_ok =
				header.find("#include \"Function/Render/VolumetricLightingPass.h\"") != std::string::npos &&
				header.find("VolumetricLightingPass m_volumetric_lighting_pass") != std::string::npos;
			const size_t sky_pos = source.find("m_sky_background_pass.add_pass");
			const size_t volumetric_pos = source.find("m_volumetric_lighting_pass.add_passes");
			const size_t bloom_pos = source.find("m_bloom_pass.add_passes");
			const size_t tone_pos = source.find("m_post_process_tone_map_pass.add_pass");
			const bool order_ok =
				sky_pos != std::string::npos &&
				volumetric_pos != std::string::npos &&
				bloom_pos != std::string::npos &&
				tone_pos != std::string::npos &&
				sky_pos < volumetric_pos &&
				volumetric_pos < bloom_pos &&
				bloom_pos < tone_pos;
			const bool debug_ok =
				source.find("\"SceneVolumetricDensity\"") != std::string::npos &&
				source.find("\"SceneVolumetricScattering\"") != std::string::npos &&
				source.find("\"SceneVolumetricIntegratedLighting\"") != std::string::npos &&
				source.find("\"SceneVolumetricCompositeHDR\"") != std::string::npos &&
				source.find("volumetric_outputs.atlas_width") != std::string::npos &&
				source.find("volumetric_outputs.atlas_height") != std::string::npos;

			return (header_ok && order_ok && debug_ok) ||
				report_self_test_failure(
					"SceneRenderer volumetric lighting integration",
					"pass is not owned, ordered, or debug-registered correctly");
		}

		auto test_sunlight_shadow_planner_rejects_multiple_sunlights() -> bool
		{
			DirectionalShadowConfig config = make_default_directional_shadow_config();
			config.default_cascade_count = 2u;

			VisibleRenderFrame frame{};
			frame.reverse_z = true;
			frame.camera_position = { 0.0f, 0.0f, 0.0f };
			frame.view = glm::lookAtLH(glm::vec3(0.0f, 0.0f, -10.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
			frame.projection = glm::perspectiveLH_ZO(glm::radians(60.0f), 1.0f, 0.1f, 1000.0f);
			frame.view_projection = frame.projection * frame.view;

			for (uint32_t index = 0; index < 2u; ++index)
			{
				VisibleLightData light{};
				light.entity_id = 100u + index;
				light.type = LightType::Directional;
				light.direction_ws = glm::normalize(glm::vec3(-0.3f, -1.0f, -0.2f));
				light.casts_shadow = true;
				light.sunlight = true;
				light.shadow_priority = 255u - index;
				frame.lights.push_back(light);
			}

			DirectionalShadowFramePlan plan{};
			const bool ok_plan = build_sunlight_shadow_frame_plan_for_tests(frame, config, 1920u, 1080u, plan);
			const bool ok =
				!ok_plan &&
				plan.input_directional_shadow_light_count == 2u &&
				plan.shadowed_lights.empty() &&
				plan.cascades.empty();
			return ok || report_self_test_failure("SunLightShadow planner", "planner accepted multiple sunlight candidates");
		}

		auto test_sunlight_shadow_planner_ignores_ordinary_directional_lights() -> bool
		{
			DirectionalShadowConfig config = make_default_directional_shadow_config();
			config.default_cascade_count = 2u;
			config.dynamic_atlas_size = 2048u;
			config.near_cascade_resolution = 1024u;
			config.outer_cascade_resolution = 512u;

			VisibleRenderFrame frame{};
			frame.reverse_z = false;
			frame.camera_position = { 0.0f, 2.0f, -8.0f };
			frame.view = glm::lookAtLH(frame.camera_position, glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
			frame.projection = glm::perspectiveLH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
			frame.view_projection = frame.projection * frame.view;

			VisibleLightData ordinary{};
			ordinary.entity_id = 1u;
			ordinary.type = LightType::Directional;
			ordinary.direction_ws = glm::normalize(glm::vec3(-0.3f, -1.0f, -0.1f));
			ordinary.casts_shadow = true;
			ordinary.sunlight = false;
			frame.lights.push_back(ordinary);

			VisibleLightData sunlight = ordinary;
			sunlight.entity_id = 2u;
			sunlight.sunlight = true;
			frame.lights.push_back(sunlight);

			DirectionalShadowFramePlan plan{};
			const bool ok_plan = build_sunlight_shadow_frame_plan_for_tests(frame, config, 1920u, 1080u, plan);
			const bool ok =
				ok_plan &&
				plan.input_directional_shadow_light_count == 1u &&
				plan.shadowed_lights.size() == 1u &&
				plan.shadowed_lights[0].frame_light_index == 1u;
			return ok || report_self_test_failure("SunLightShadow planner", "sunlight planner included ordinary directional lights");
		}

		auto test_sunlight_shadow_planner_releases_partial_cascade_tiles() -> bool
		{
			DirectionalShadowConfig config = make_default_directional_shadow_config();
			config.dynamic_atlas_size = 2048;
			config.near_cascade_resolution = 2048;
			config.outer_cascade_resolution = 1024;
			config.default_cascade_count = 2;

			VisibleRenderFrame frame{};
			frame.reverse_z = false;
			frame.camera_position = { 0.0f, 2.0f, -8.0f };
			frame.view = glm::lookAtLH(frame.camera_position, glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
			frame.projection = glm::perspectiveLH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
			frame.view_projection = frame.projection * frame.view;

			VisibleLightData oversized_light{};
			oversized_light.entity_id = 1u;
			oversized_light.type = LightType::Directional;
			oversized_light.direction_ws = glm::normalize(glm::vec3(-0.25f, -1.0f, -0.1f));
			oversized_light.casts_shadow = true;
			oversized_light.sunlight = true;
			oversized_light.shadow_priority = 10u;
			oversized_light.shadow_cascade_count = 2u;
			frame.lights.push_back(oversized_light);

			DirectionalShadowFramePlan plan{};
			const bool ok_plan = build_sunlight_shadow_frame_plan_for_tests(frame, config, 1920u, 1080u, plan);
			const bool ok =
				ok_plan &&
				plan.skipped_shadow_light_count == 1u &&
				plan.shadowed_lights.empty() &&
				plan.cascades.empty();
			return ok ||
				report_self_test_failure("SunLightShadow planner", "partial cascade allocations from a skipped sunlight were not released");
		}

		auto test_directional_light_shadow_planner_handles_each_ordinary_light_without_global_budget() -> bool
		{
			DirectionalShadowConfig config = make_default_directional_shadow_config();
			config.default_cascade_count = 2u;
			config.dynamic_atlas_size = 2048u;
			config.near_cascade_resolution = 1024u;
			config.outer_cascade_resolution = 512u;

			VisibleRenderFrame frame{};
			frame.reverse_z = false;
			frame.camera_position = { 0.0f, 2.0f, -8.0f };
			frame.view = glm::lookAtLH(frame.camera_position, glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
			frame.projection = glm::perspectiveLH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
			frame.view_projection = frame.projection * frame.view;

			for (uint32_t index = 0; index < 12u; ++index)
			{
				VisibleLightData light{};
				light.entity_id = 100u + index;
				light.type = LightType::Directional;
				light.direction_ws = glm::normalize(glm::vec3(-0.3f, -1.0f, -0.1f));
				light.casts_shadow = true;
				light.sunlight = false;
				light.shadow_priority = index;
				frame.lights.push_back(light);
			}

			for (uint32_t light_index = 0; light_index < static_cast<uint32_t>(frame.lights.size()); ++light_index)
			{
				DirectionalShadowFramePlan plan{};
				const bool ok_plan = build_directional_light_shadow_frame_plan_for_tests(
					frame,
					light_index,
					config,
					1920u,
					1080u,
					plan);
				const bool ok =
					ok_plan &&
					plan.input_directional_shadow_light_count == 1u &&
					plan.skipped_shadow_light_count == 0u &&
					plan.shadowed_lights.size() == 1u &&
					plan.shadowed_lights[0].frame_light_index == light_index &&
					plan.cascades.size() == 2u;
				if (!ok)
				{
					return report_self_test_failure("DirectionalLightShadow planner", "ordinary directional light was skipped by a global shadow budget");
				}
			}

			return true;
		}

		auto test_directional_light_shadow_planner_uses_uncached_cascades() -> bool
		{
			DirectionalShadowConfig config = make_default_directional_shadow_config();
			config.default_cascade_count = 3u;
			config.dynamic_atlas_size = 2048u;
			config.near_cascade_resolution = 1024u;
			config.outer_cascade_resolution = 512u;

			VisibleRenderFrame frame{};
			frame.reverse_z = false;
			frame.camera_position = { 0.0f, 2.0f, -8.0f };
			frame.view = glm::lookAtLH(frame.camera_position, glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
			frame.projection = glm::perspectiveLH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
			frame.view_projection = frame.projection * frame.view;

			VisibleLightData light{};
			light.entity_id = 42u;
			light.type = LightType::Directional;
			light.direction_ws = glm::normalize(glm::vec3(-0.25f, -1.0f, -0.1f));
			light.casts_shadow = true;
			light.sunlight = false;
			frame.lights.push_back(light);

			DirectionalShadowFramePlan plan{};
			if (!build_directional_light_shadow_frame_plan_for_tests(frame, 0u, config, 1920u, 1080u, plan) ||
				plan.cascades.size() != 3u)
			{
				return report_self_test_failure("DirectionalLightShadow planner", "ordinary directional light did not create expected cascades");
			}

			for (const DirectionalShadowCascadePlan& cascade : plan.cascades)
			{
				if (cascade.cache_mode != DirectionalShadowCacheMode::NearEveryFrame || cascade.has_static_cache_tile)
				{
					return report_self_test_failure("DirectionalLightShadow planner", "ordinary directional light used static-cache cascade state");
				}
			}

			return true;
		}

		auto test_directional_shadow_static_cache_reuses_evicted_tiles() -> bool
		{
			DirectionalShadowConfig config = make_default_directional_shadow_config();
			config.default_cascade_count = 4;
			config.near_cascade_resolution = 2048;
			config.outer_cascade_resolution = 1024;
			config.dynamic_atlas_size = 4096;
			config.static_cache_atlas_size = 4096;
			config.static_cache_budget_mb = 64;

			SunLightShadowPass runtime_pass{};
			for (uint32_t index = 0; index < 6u; ++index)
			{
				VisibleRenderFrame frame{};
				frame.static_scene_revision = 1u;
				frame.reverse_z = false;
				frame.camera_position = { 0.0f, 2.0f, -8.0f };
				frame.view = glm::lookAtLH(frame.camera_position, glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
				frame.projection = glm::perspectiveLH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
				frame.view_projection = frame.projection * frame.view;

				VisibleLightData light{};
				light.entity_id = 1000u + index;
				light.type = LightType::Directional;
				light.direction_ws = glm::normalize(glm::vec3(-0.25f, -1.0f, -0.1f));
				light.casts_shadow = true;
				light.sunlight = true;
				frame.lights.push_back(light);

				DirectionalShadowFramePlan plan{};
				if (!SunLightShadowDetail::build_sunlight_shadow_frame_plan_internal(
					frame,
					config,
					1920u,
					1080u,
					&runtime_pass,
					plan))
				{
					return report_self_test_failure("DirectionalShadow static cache", "runtime planner failed while filling cache");
				}
				if (plan.shadowed_lights.size() != 1u || plan.cascades.size() != 4u)
				{
					return report_self_test_failure("DirectionalShadow static cache", "evicted static cache tiles were not reused");
				}
				for (uint32_t cascade_index = 1u; cascade_index < 4u; ++cascade_index)
				{
					if (!plan.cascades[cascade_index].has_static_cache_tile ||
						plan.cascades[cascade_index].cache_mode == DirectionalShadowCacheMode::Uncached)
					{
						return report_self_test_failure("DirectionalShadow static cache", "outer cascades fell back to uncached after eviction");
					}
				}
			}

			return true;
		}

		auto test_directional_shadow_static_cache_invalidates_when_cascade_matrix_changes() -> bool
		{
			DirectionalShadowConfig config = make_default_directional_shadow_config();
			config.default_cascade_count = 4;
			config.default_shadow_distance = 160.0f;
			config.near_shadow_distance = 16.0f;
			config.near_cascade_resolution = 2048;
			config.outer_cascade_resolution = 1024;
			config.dynamic_atlas_size = 4096;
			config.static_cache_atlas_size = 4096;
			config.static_cache_budget_mb = 256;

			auto make_frame = [](const glm::vec3& camera_position, const glm::vec3& camera_target) -> VisibleRenderFrame
			{
				VisibleRenderFrame frame{};
				frame.static_scene_revision = 7u;
				frame.reverse_z = false;
				frame.camera_position = camera_position;
				frame.view = glm::lookAtLH(camera_position, camera_target, glm::vec3(0.0f, 1.0f, 0.0f));
				frame.projection = glm::perspectiveLH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
				frame.view_projection = frame.projection * frame.view;

				VisibleLightData light{};
				light.entity_id = 42u;
				light.type = LightType::Directional;
				light.direction_ws = glm::normalize(glm::vec3(-0.25f, -1.0f, -0.1f));
				light.casts_shadow = true;
				light.sunlight = true;
				frame.lights.push_back(light);
				return frame;
			};

			SunLightShadowPass runtime_pass{};
			const VisibleRenderFrame first_frame = make_frame(
				glm::vec3(0.0f, 2.0f, -8.0f),
				glm::vec3(0.0f, 2.0f, 0.0f));
			DirectionalShadowFramePlan first_plan{};
			if (!SunLightShadowDetail::build_sunlight_shadow_frame_plan_internal(
				first_frame,
				config,
				1920u,
				1080u,
				&runtime_pass,
				first_plan) ||
				first_plan.cascades.size() != 4u)
			{
				return report_self_test_failure("DirectionalShadow static cache", "failed to seed static cache for matrix invalidation test");
			}

			DirectionalShadowFramePlan same_view_plan{};
			if (!SunLightShadowDetail::build_sunlight_shadow_frame_plan_internal(
				first_frame,
				config,
				1920u,
				1080u,
				&runtime_pass,
				same_view_plan) ||
				same_view_plan.cascades.size() != 4u)
			{
				return report_self_test_failure("DirectionalShadow static cache", "failed to rebuild same-view static cache plan");
			}

			bool same_view_cached = true;
			for (uint32_t cascade_index = 1u; cascade_index < 4u; ++cascade_index)
			{
				same_view_cached = same_view_cached &&
					same_view_plan.cascades[cascade_index].cache_mode == DirectionalShadowCacheMode::StaticCached;
			}
			if (!same_view_cached)
			{
				return report_self_test_failure("DirectionalShadow static cache", "unchanged cascade matrices did not reuse static cache");
			}

			const VisibleRenderFrame rotated_frame = make_frame(
				glm::vec3(0.0f, 2.0f, -8.0f),
				glm::vec3(8.0f, 2.0f, 0.0f));
			DirectionalShadowFramePlan rotated_plan{};
			if (!SunLightShadowDetail::build_sunlight_shadow_frame_plan_internal(
				rotated_frame,
				config,
				1920u,
				1080u,
				&runtime_pass,
				rotated_plan) ||
				rotated_plan.cascades.size() != 4u)
			{
				return report_self_test_failure("DirectionalShadow static cache", "failed to rebuild rotated-view static cache plan");
			}

			bool rotated_view_refreshed = true;
			for (uint32_t cascade_index = 1u; cascade_index < 4u; ++cascade_index)
			{
				rotated_view_refreshed = rotated_view_refreshed &&
					rotated_plan.cascades[cascade_index].cache_mode == DirectionalShadowCacheMode::StaticRefresh;
			}
			return rotated_view_refreshed ||
				report_self_test_failure("DirectionalShadow static cache", "camera-dependent cascade matrices reused stale static cache");
		}

		auto test_directional_shadow_planner_builds_monotonic_cascades() -> bool
		{
			DirectionalShadowConfig config = make_default_directional_shadow_config();
			config.default_cascade_count = 4;
			config.default_shadow_distance = 160.0f;
			config.near_shadow_distance = 12.0f;
			config.split_lambda = 0.65f;

			VisibleRenderFrame frame{};
			frame.reverse_z = false;
			frame.camera_position = { 0.0f, 2.0f, -8.0f };
			frame.view = glm::lookAtLH(frame.camera_position, glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
			frame.projection = glm::perspectiveLH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
			frame.view_projection = frame.projection * frame.view;
			VisibleLightData light{};
			light.entity_id = 1;
			light.type = LightType::Directional;
			light.direction_ws = glm::normalize(glm::vec3(-0.25f, -1.0f, -0.1f));
			light.casts_shadow = true;
			light.sunlight = true;
			frame.lights.push_back(light);

			DirectionalShadowFramePlan plan{};
			if (!build_sunlight_shadow_frame_plan_for_tests(frame, config, 1920u, 1080u, plan) || plan.cascades.size() != 4u)
			{
				return report_self_test_failure("DirectionalShadow planner", "planner did not create four cascades");
			}

			bool monotonic = plan.cascades[0].cache_mode == DirectionalShadowCacheMode::NearEveryFrame &&
				std::abs(plan.cascades[0].split_far - config.near_shadow_distance) <= 0.001f;
			for (size_t index = 1; index < plan.cascades.size(); ++index)
			{
				monotonic = monotonic &&
					plan.cascades[index - 1].split_near < plan.cascades[index - 1].split_far &&
					plan.cascades[index - 1].split_far <= plan.cascades[index].split_near + 0.001f &&
					plan.cascades[index].split_near < plan.cascades[index].split_far;
			}
			return monotonic ||
				report_self_test_failure("DirectionalShadow planner", "cascade splits were not monotonic");
		}

		auto test_visible_static_mesh_draws_carry_shadow_mobility() -> bool
		{
			VisibleStaticMeshDraw static_draw{};
			static_draw.entity_id = 1;
			static_draw.mobility = SceneMobility::Static;
			VisibleStaticMeshDraw movable_draw{};
			movable_draw.entity_id = 2;
			movable_draw.mobility = SceneMobility::Movable;

			VisibleRenderFrame frame{};
			frame.shadow_caster_static_mesh_draws.push_back(static_draw);
			frame.shadow_caster_static_mesh_draws.push_back(movable_draw);

			const uint32_t static_count = count_shadow_casters_for_tests(frame, ShadowCasterMobilityFilter::StaticOnly);
			const uint32_t dynamic_count = count_shadow_casters_for_tests(frame, ShadowCasterMobilityFilter::DynamicOnly);
			const uint32_t all_count = count_shadow_casters_for_tests(frame, ShadowCasterMobilityFilter::All);
			const bool ok = static_count == 1u && dynamic_count == 1u && all_count == 2u;
			return ok || report_self_test_failure("DirectionalShadow caster filter", "shadow caster mobility filter did not classify draws");
		}

		auto test_directional_shadow_mask_normal_bias_offsets_along_normal() -> bool
		{
			std::ifstream shader_file("project/src/engine/Shaders/Shadow/DirectionalShadowMask.hlsl");
			if (!shader_file.is_open())
			{
				return report_self_test_failure("DirectionalShadow mask shader", "failed to open DirectionalShadowMask.hlsl");
			}
			const std::string shader_source{
				std::istreambuf_iterator<char>(shader_file),
				std::istreambuf_iterator<char>() };
			const bool ok =
				shader_source.find("return position_ws + normal_ws * normal_bias;") != std::string::npos &&
				shader_source.find("normal_bias * light_dir_to_light") == std::string::npos;
			return ok || report_self_test_failure("DirectionalShadow mask shader", "normal bias was not applied along the surface normal");
		}

		auto test_directional_shadow_mask_blends_cascade_transition() -> bool
		{
			std::ifstream shader_file("project/src/engine/Shaders/Shadow/DirectionalShadowMask.hlsl");
			if (!shader_file.is_open())
			{
				return report_self_test_failure(
					"DirectionalShadow mask shader",
					"failed to open DirectionalShadowMask.hlsl");
			}

			const std::string shader_source{
				std::istreambuf_iterator<char>(shader_file),
				std::istreambuf_iterator<char>() };
			const bool computes_transition =
				shader_source.find("ComputeCascadeTransitionWeight") != std::string::npos;
			const bool samples_next_cascade =
				shader_source.find("SampleCascadeShadow(next_buffer_index, position_ws, normal_ws)") != std::string::npos;
			const bool blends_cascades =
				shader_source.find("lerp(shadow, next_shadow, transition_weight)") != std::string::npos;
			const bool ok = computes_transition && samples_next_cascade && blends_cascades;
			return ok || report_self_test_failure(
				"DirectionalShadow mask shader",
				"cascade splits must blend to the next cascade instead of hard switching");
		}

		auto test_directional_shadow_cascade_buffer_uses_two_dimensional_texel_size() -> bool
		{
			std::ifstream mask_shader_file("project/src/engine/Shaders/Shadow/DirectionalShadowMask.hlsl");
			std::ifstream sunlight_pass_file("project/src/engine/Function/Render/SunLightShadowPass.cpp");
			std::ifstream directional_pass_file("project/src/engine/Function/Render/DirectionalLightShadowPass.cpp");
			if (!mask_shader_file.is_open() || !sunlight_pass_file.is_open() || !directional_pass_file.is_open())
			{
				return report_self_test_failure(
					"DirectionalShadow cascade buffer layout",
					"failed to open shadow mask or directional shadow pass source");
			}

			const std::string mask_shader_source{
				std::istreambuf_iterator<char>(mask_shader_file),
				std::istreambuf_iterator<char>() };
			const std::string sunlight_pass_source{
				std::istreambuf_iterator<char>(sunlight_pass_file),
				std::istreambuf_iterator<char>() };
			const std::string directional_pass_source{
				std::istreambuf_iterator<char>(directional_pass_file),
				std::istreambuf_iterator<char>() };
			const bool shader_consumes_xy =
				mask_shader_source.find("cascade.texel_size_flags.xy") != std::string::npos;
			const bool sunlight_uploads_xy =
				sunlight_pass_source.find("const float atlas_texel_size = 1.0f / std::max(atlas_size_f, 1.0f);") != std::string::npos &&
				sunlight_pass_source.find("shader_data.texel_size_flags = glm::vec4(\n\t\t\t\tatlas_texel_size,\n\t\t\t\tatlas_texel_size,") != std::string::npos;
			const bool directional_uploads_xy =
				directional_pass_source.find("const float atlas_texel_size = 1.0f / std::max(atlas_size_f, 1.0f);") != std::string::npos &&
				directional_pass_source.find("shader_data.texel_size_flags = {\n\t\t\t\tatlas_texel_size,\n\t\t\t\tatlas_texel_size,") != std::string::npos;
			const bool ok = shader_consumes_xy && sunlight_uploads_xy && directional_uploads_xy;
			return ok || report_self_test_failure(
				"DirectionalShadow cascade buffer layout",
				"cascade texel_size_flags.xy must both contain atlas-normalized texel size for PCF sampling");
		}

		auto test_directional_shadow_cascade_projection_snaps_to_texel_grid() -> bool
		{
			const glm::vec3 light_direction = glm::normalize(glm::vec3(-0.25f, -1.0f, -0.1f));
			auto make_frame = [](const glm::vec3& camera_position) -> VisibleRenderFrame
			{
				VisibleRenderFrame frame{};
				frame.reverse_z = false;
				frame.camera_position = camera_position;
				frame.view = glm::lookAtLH(camera_position, camera_position + glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
				frame.projection = glm::perspectiveLH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
				frame.view_projection = frame.projection * frame.view;
				return frame;
			};

			const VisibleRenderFrame frame_a = make_frame(glm::vec3(0.0f, 2.0f, -8.0f));
			const VisibleRenderFrame frame_b = make_frame(glm::vec3(0.01f, 2.0f, -8.0f));
			const uint32_t shadow_resolution = 1024u;
			const glm::mat4 shadow_a = build_directional_shadow_cascade_light_view_projection(
				frame_a,
				light_direction,
				16.0f,
				160.0f,
				shadow_resolution);
			const glm::mat4 shadow_b = build_directional_shadow_cascade_light_view_projection(
				frame_b,
				light_direction,
				16.0f,
				160.0f,
				shadow_resolution);

			const glm::vec4 world_anchor(0.0f, 0.0f, 0.0f, 1.0f);
			const glm::vec4 clip_a = shadow_a * world_anchor;
			const glm::vec4 clip_b = shadow_b * world_anchor;
			const glm::vec2 ndc_a = glm::vec2(clip_a) / std::max(clip_a.w, 1e-6f);
			const glm::vec2 ndc_b = glm::vec2(clip_b) / std::max(clip_b.w, 1e-6f);
			const glm::vec2 texel_a = (ndc_a * glm::vec2(0.5f, -0.5f) + glm::vec2(0.5f, 0.5f)) * static_cast<float>(shadow_resolution);
			const glm::vec2 texel_b = (ndc_b * glm::vec2(0.5f, -0.5f) + glm::vec2(0.5f, 0.5f)) * static_cast<float>(shadow_resolution);

			const bool ok = glm::length(texel_a - texel_b) <= 0.0001f;
			return ok || report_self_test_failure(
				"DirectionalShadow stable cascade",
				"small camera movement shifted the cascade projection across shadow texels");
		}

		auto test_directional_shadow_cascade_projection_size_is_rotation_stable() -> bool
		{
			const glm::vec3 light_direction = glm::normalize(glm::vec3(-0.25f, -1.0f, -0.1f));
			auto make_frame = [](const glm::vec3& camera_position, float yaw_degrees) -> VisibleRenderFrame
			{
				const float yaw = glm::radians(yaw_degrees);
				const glm::vec3 forward = glm::normalize(glm::vec3(std::sin(yaw), 0.0f, std::cos(yaw)));
				VisibleRenderFrame frame{};
				frame.reverse_z = false;
				frame.camera_position = camera_position;
				frame.view = glm::lookAtLH(camera_position, camera_position + forward, glm::vec3(0.0f, 1.0f, 0.0f));
				frame.projection = glm::perspectiveLH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
				frame.view_projection = frame.projection * frame.view;
				return frame;
			};
			auto shadow_texel_world_size = [](const glm::mat4& shadow_matrix, uint32_t shadow_resolution) -> glm::vec2
			{
				const glm::vec3 clip_x_row(shadow_matrix[0][0], shadow_matrix[1][0], shadow_matrix[2][0]);
				const glm::vec3 clip_y_row(shadow_matrix[0][1], shadow_matrix[1][1], shadow_matrix[2][1]);
				const float resolution = static_cast<float>(std::max(shadow_resolution, 1u));
				return glm::vec2(
					2.0f / std::max(glm::length(clip_x_row), 1e-6f),
					2.0f / std::max(glm::length(clip_y_row), 1e-6f)) / resolution;
			};

			const VisibleRenderFrame frame_a = make_frame(glm::vec3(0.0f, 2.0f, -8.0f), 0.0f);
			const VisibleRenderFrame frame_b = make_frame(glm::vec3(0.0f, 2.0f, -8.0f), 11.0f);
			const uint32_t shadow_resolution = 1024u;
			const glm::mat4 near_shadow_a = build_directional_shadow_cascade_light_view_projection(
				frame_a,
				light_direction,
				0.01f,
				16.0f,
				shadow_resolution);
			const glm::mat4 near_shadow_b = build_directional_shadow_cascade_light_view_projection(
				frame_b,
				light_direction,
				0.01f,
				16.0f,
				shadow_resolution);
			const glm::mat4 far_shadow_a = build_directional_shadow_cascade_light_view_projection(
				frame_a,
				light_direction,
				16.0f,
				160.0f,
				shadow_resolution);
			const glm::mat4 far_shadow_b = build_directional_shadow_cascade_light_view_projection(
				frame_b,
				light_direction,
				16.0f,
				160.0f,
				shadow_resolution);

			const glm::vec2 near_texel_a = shadow_texel_world_size(near_shadow_a, shadow_resolution);
			const glm::vec2 near_texel_b = shadow_texel_world_size(near_shadow_b, shadow_resolution);
			const glm::vec2 far_texel_a = shadow_texel_world_size(far_shadow_a, shadow_resolution);
			const glm::vec2 far_texel_b = shadow_texel_world_size(far_shadow_b, shadow_resolution);
			const bool ok =
				glm::length(near_texel_a - near_texel_b) <= 0.00001f &&
				glm::length(far_texel_a - far_texel_b) <= 0.00001f;
			return ok || report_self_test_failure(
				"DirectionalShadow stable cascade rotation",
				"camera rotation changed the cascade projection texel world size");
		}

		auto test_directional_shadow_static_cache_copy_uses_atlas_uv_scale() -> bool
		{
			DirectionalShadowAtlasTile target_tile{};
			target_tile.x = 2048u;
			target_tile.y = 0u;
			target_tile.width = 1024u;
			target_tile.height = 1024u;
			target_tile.resolution = 1024u;

			DirectionalShadowAtlasTile source_tile{};
			source_tile.x = 1024u;
			source_tile.y = 2048u;
			source_tile.width = 1024u;
			source_tile.height = 1024u;
			source_tile.resolution = 1024u;

			const glm::vec4 scale_bias =
				make_directional_shadow_static_cache_copy_scale_bias_for_tests(target_tile, source_tile, 4096.0f);
			const bool ok =
				std::abs(scale_bias.x - 0.25f) <= 0.0001f &&
				std::abs(scale_bias.y - 0.25f) <= 0.0001f &&
				std::abs(scale_bias.z - 0.25f) <= 0.0001f &&
				std::abs(scale_bias.w - 0.5f) <= 0.0001f;
			return ok || report_self_test_failure(
				"DirectionalShadow static cache copy",
				"static cache copy did not map tile-local UVs into atlas-normalized source UVs");
		}

		auto test_directional_shadow_static_cache_copy_declares_graph_read() -> bool
		{
			std::ifstream pass_file("project/src/engine/Function/Render/SunLightShadowPass.cpp");
			if (!pass_file.is_open())
			{
				return report_self_test_failure("DirectionalShadow static cache graph", "failed to open SunLightShadowPass.cpp");
			}

			const std::string pass_source{
				std::istreambuf_iterator<char>(pass_file),
				std::istreambuf_iterator<char>() };
			const bool ok =
				pass_source.find("needs_static_cache_read") != std::string::npos &&
				pass_source.find("pass.read_texture(static_cache_atlas, RenderGraphAccess::GraphicsSRV)") != std::string::npos &&
				pass_source.find("m_depth_copy_program->set_texture(\"DirectionalShadowStaticCache\", static_cache)") != std::string::npos;
			return ok || report_self_test_failure(
				"DirectionalShadow static cache graph",
				"static-cache copy pass binds the cache texture without declaring the RenderGraph SRV read");
		}

		auto test_directional_shadow_graph_adds_depth_before_lighting() -> bool
		{
			RenderGraphBuilder graph = RenderGraphBuilder::create_headless_for_tests("DirectionalShadowGraphSelfTest");
			RenderTargetDesc output_desc{};
			output_desc.width = 128;
			output_desc.height = 128;
			output_desc.format = RenderTextureFormat::RGBA8_UNORM;
			RenderGraphTextureRef output = graph.register_external_texture_desc_for_tests(output_desc, "SceneOutput");

			RenderGraphTextureDesc depth_desc{};
			depth_desc.width = 128;
			depth_desc.height = 128;
			depth_desc.format = RenderTextureFormat::D32_SFLOAT;
			depth_desc.shader_resource = true;
			RenderGraphTextureRef dynamic_atlas = graph.create_texture(depth_desc, "DirectionalShadowDynamicAtlas");

			DirectionalShadowFramePlan plan{};
			DirectionalShadowCascadePlan cascade{};
			cascade.cache_mode = DirectionalShadowCacheMode::NearEveryFrame;
			cascade.dynamic_tile = { 0u, 0u, 1024u, 1024u, 1024u };
			plan.cascades.push_back(cascade);

			add_directional_shadow_depth_passes_for_tests(graph, dynamic_atlas, plan);
			graph.add_raster_pass(
				"ShadowConsumer",
				RenderGraphPassFlags::None,
				[&](RenderGraphRasterPassBuilder& pass)
				{
					pass.read_texture(dynamic_atlas, RenderGraphAccess::GraphicsSRV);
					pass.write_color(0, output, RenderLoadAction::Clear, {});
				},
				[](RenderGraphRasterContext&)
				{
					return true;
				});

			RenderGraphCompileResult result{};
			const bool ok =
				graph.compile_for_tests(result) &&
				result.live_pass_indices.size() >= 2u &&
				result.pass_barriers.back().transitions.size() >= 1u;
			return ok || report_self_test_failure("DirectionalShadow graph", "shadow depth producer was not preserved before consumer");
		}

		auto test_directional_shadow_deferred_graph_contract() -> bool
		{
			RenderGraphBuilder graph = RenderGraphBuilder::create_headless_for_tests("DirectionalShadowDeferredGraphSelfTest");

			RenderTargetDesc output_desc{};
			output_desc.width = 64;
			output_desc.height = 64;
			output_desc.format = RenderTextureFormat::RGBA8_UNORM;
			RenderGraphTextureRef output = graph.register_external_texture_desc_for_tests(output_desc, "SceneOutput");

			SceneDeferredGraphResources resources{};
			resources.gbuffer_targets.reserve(5u);
			for (uint32_t index = 0; index < 5u; ++index)
			{
				RenderGraphTextureDesc gbuffer_desc{};
				gbuffer_desc.width = 64;
				gbuffer_desc.height = 64;
				gbuffer_desc.format = RenderTextureFormat::RGBA8_UNORM;
				gbuffer_desc.shader_resource = true;
				resources.gbuffer_targets.push_back(graph.create_texture(gbuffer_desc, "SceneGBuffer"));
			}

			RenderGraphTextureDesc depth_desc{};
			depth_desc.width = 64;
			depth_desc.height = 64;
			depth_desc.format = RenderTextureFormat::D32_SFLOAT;
			depth_desc.shader_resource = true;
			resources.depth = graph.create_texture(depth_desc, "SceneDeferredDepth");

			RenderGraphTextureDesc ambient_occlusion_desc{};
			ambient_occlusion_desc.width = 64;
			ambient_occlusion_desc.height = 64;
			ambient_occlusion_desc.format = RenderTextureFormat::RGBA8_UNORM;
			ambient_occlusion_desc.shader_resource = true;
			resources.ambient_occlusion = graph.create_texture(ambient_occlusion_desc, "SceneAmbientOcclusion");

			RenderGraphTextureDesc lighting_desc{};
			lighting_desc.width = 64;
			lighting_desc.height = 64;
			lighting_desc.format = RenderTextureFormat::RGBA16_SFLOAT;
			lighting_desc.shader_resource = true;
			resources.lighting_diffuse = graph.create_texture(lighting_desc, "SceneDeferredLightingDiffuse");
			resources.lighting_specular = graph.create_texture(lighting_desc, "SceneDeferredLightingSpecular");
			resources.scene_hdr_linear = graph.create_texture(lighting_desc, "SceneDeferredSceneHDRLinear");

			RenderGraphTextureDesc shadow_depth_desc{};
			shadow_depth_desc.width = 256;
			shadow_depth_desc.height = 256;
			shadow_depth_desc.format = RenderTextureFormat::D32_SFLOAT;
			shadow_depth_desc.shader_resource = true;
			RenderGraphTextureRef transient_atlas =
				graph.create_texture(shadow_depth_desc, "DirectionalShadowDynamicAtlas");

			RenderGraphTextureDesc shadow_mask_desc{};
			shadow_mask_desc.width = 64;
			shadow_mask_desc.height = 64;
			shadow_mask_desc.format = RenderTextureFormat::RGBA8_UNORM;
			shadow_mask_desc.shader_resource = true;
			RenderGraphTextureRef first_shadow_mask =
				graph.create_texture(shadow_mask_desc, "SceneDirectionalShadowMask");
			RenderGraphTextureRef second_shadow_mask =
				graph.create_texture(shadow_mask_desc, "SceneDirectionalShadowMask");

			graph.add_raster_pass(
				"SceneGBufferPass",
				RenderGraphPassFlags::None,
				[&](RenderGraphRasterPassBuilder& pass)
				{
					for (uint8_t index = 0; index < static_cast<uint8_t>(resources.gbuffer_targets.size()); ++index)
					{
						pass.write_color(index, resources.gbuffer_targets[index], RenderLoadAction::Clear, {});
					}
					pass.write_depth(resources.depth, RenderLoadAction::Clear, {});
				},
				[](RenderGraphRasterContext&)
				{
					return true;
				});

			graph.add_raster_pass(
				"SceneAmbientOcclusionPass",
				RenderGraphPassFlags::None,
				[&](RenderGraphRasterPassBuilder& pass)
				{
					pass.read_texture(resources.gbuffer_targets[4], RenderGraphAccess::GraphicsSRV);
					pass.read_texture(resources.depth, RenderGraphAccess::GraphicsSRV);
					pass.write_color(0, resources.ambient_occlusion, RenderLoadAction::Clear, { 1.0f, 1.0f, 1.0f, 1.0f });
				},
				[](RenderGraphRasterContext&)
				{
					return true;
				});

			graph.add_raster_pass(
				"SceneDirectionalShadowDepthPass",
				RenderGraphPassFlags::None,
				[&](RenderGraphRasterPassBuilder& pass)
				{
					pass.write_depth(transient_atlas, RenderLoadAction::Clear, {});
				},
				[](RenderGraphRasterContext&)
				{
					return true;
				});

			graph.add_raster_pass(
				"SceneDeferredLightingBasePass",
				RenderGraphPassFlags::None,
				[&](RenderGraphRasterPassBuilder& pass)
				{
					for (RenderGraphTextureRef gbuffer : resources.gbuffer_targets)
					{
						pass.read_texture(gbuffer, RenderGraphAccess::GraphicsSRV);
					}
					pass.read_depth(resources.depth, RenderGraphDepthReadMode::DepthTestAndShaderResource);
					pass.read_texture(resources.ambient_occlusion, RenderGraphAccess::GraphicsSRV);
					pass.write_color(0, resources.lighting_diffuse, RenderLoadAction::Clear, {});
					pass.write_color(1, resources.lighting_specular, RenderLoadAction::Clear, {});
				},
				[](RenderGraphRasterContext&)
				{
					return true;
				});

			graph.add_raster_pass(
				"SceneDirectionalShadowMaskPass_0",
				RenderGraphPassFlags::None,
				[&](RenderGraphRasterPassBuilder& pass)
				{
					pass.read_texture(resources.depth, RenderGraphAccess::GraphicsSRV);
					pass.read_texture(transient_atlas, RenderGraphAccess::GraphicsSRV);
					pass.read_texture(resources.gbuffer_targets[4], RenderGraphAccess::GraphicsSRV);
					pass.write_color(0, first_shadow_mask, RenderLoadAction::Clear, { 1.0f, 1.0f, 1.0f, 1.0f });
				},
				[](RenderGraphRasterContext&)
				{
					return true;
				});

			graph.add_raster_pass(
				"SceneDeferredDirectionalLightingShadowedPass_0",
				RenderGraphPassFlags::None,
				[&](RenderGraphRasterPassBuilder& pass)
				{
					for (RenderGraphTextureRef gbuffer : resources.gbuffer_targets)
					{
						pass.read_texture(gbuffer, RenderGraphAccess::GraphicsSRV);
					}
					pass.read_depth(resources.depth, RenderGraphDepthReadMode::DepthTestAndShaderResource);
					pass.read_texture(resources.ambient_occlusion, RenderGraphAccess::GraphicsSRV);
					pass.read_texture(first_shadow_mask, RenderGraphAccess::GraphicsSRV);
					pass.write_color(0, resources.lighting_diffuse, RenderLoadAction::Load, {});
					pass.write_color(1, resources.lighting_specular, RenderLoadAction::Load, {});
				},
				[](RenderGraphRasterContext&)
				{
					return true;
				});

			graph.add_raster_pass(
				"SceneDirectionalLightShadowMaskPass_1",
				RenderGraphPassFlags::None,
				[&](RenderGraphRasterPassBuilder& pass)
				{
					pass.read_texture(resources.depth, RenderGraphAccess::GraphicsSRV);
					pass.read_texture(transient_atlas, RenderGraphAccess::GraphicsSRV);
					pass.read_texture(resources.gbuffer_targets[4], RenderGraphAccess::GraphicsSRV);
					pass.write_color(0, second_shadow_mask, RenderLoadAction::Clear, { 1.0f, 1.0f, 1.0f, 1.0f });
				},
				[](RenderGraphRasterContext&)
				{
					return true;
				});

			graph.add_raster_pass(
				"SceneDeferredDirectionalLightingShadowedPass_1",
				RenderGraphPassFlags::None,
				[&](RenderGraphRasterPassBuilder& pass)
				{
					for (RenderGraphTextureRef gbuffer : resources.gbuffer_targets)
					{
						pass.read_texture(gbuffer, RenderGraphAccess::GraphicsSRV);
					}
					pass.read_depth(resources.depth, RenderGraphDepthReadMode::DepthTestAndShaderResource);
					pass.read_texture(resources.ambient_occlusion, RenderGraphAccess::GraphicsSRV);
					pass.read_texture(second_shadow_mask, RenderGraphAccess::GraphicsSRV);
					pass.write_color(0, resources.lighting_diffuse, RenderLoadAction::Load, {});
					pass.write_color(1, resources.lighting_specular, RenderLoadAction::Load, {});
				},
				[](RenderGraphRasterContext&)
				{
					return true;
				});

			graph.add_raster_pass(
				"SceneDeferredCompositePass",
				RenderGraphPassFlags::None,
				[&](RenderGraphRasterPassBuilder& pass)
				{
					pass.read_texture(resources.lighting_diffuse, RenderGraphAccess::GraphicsSRV);
					pass.read_texture(resources.lighting_specular, RenderGraphAccess::GraphicsSRV);
					pass.write_color(0, resources.scene_hdr_linear, RenderLoadAction::Clear, {});
				},
				[](RenderGraphRasterContext&)
				{
					return true;
				});

			graph.add_raster_pass(
				"SceneDeferredToneMapPass",
				RenderGraphPassFlags::None,
				[&](RenderGraphRasterPassBuilder& pass)
				{
					pass.read_texture(resources.scene_hdr_linear, RenderGraphAccess::GraphicsSRV);
					pass.write_color(0, output, RenderLoadAction::Clear, {});
				},
				[](RenderGraphRasterContext&)
				{
					return true;
				});

			RenderGraphCompileResult result{};
			const bool compiled = graph.compile_for_tests(result);
			const bool ok =
				compiled &&
				result.live_pass_indices.size() == 10u &&
				result.texture_lifetimes[first_shadow_mask.index].first_pass == 4u &&
				result.texture_lifetimes[first_shadow_mask.index].last_pass == 5u &&
				result.texture_lifetimes[second_shadow_mask.index].first_pass == 6u &&
				result.texture_lifetimes[second_shadow_mask.index].last_pass == 7u &&
				result.texture_lifetimes[resources.lighting_diffuse.index].first_pass == 3u &&
				result.texture_lifetimes[resources.lighting_diffuse.index].last_pass == 8u;
			return ok ||
				report_self_test_failure(
					"DirectionalShadow deferred graph",
					"shadow mask pass was not preserved between base and shadowed lighting");
		}

		auto test_deferred_lighting_pass_exposes_explicit_light_submission_api() -> bool
		{
			(void)&DeferredLightingPass::add_base_pass;
			(void)&DeferredLightingPass::add_directional_light_pass;
			(void)&DeferredLightingPass::add_point_light_pass;
			(void)&DeferredLightingPass::add_spot_light_pass;
			return true;
		}

		auto test_scene_deferred_graph_resources_split_directional_shadow_refs() -> bool
		{
			SceneDeferredGraphResources resources{};
			(void)resources.sunlight_shadow_dynamic_atlas;
			(void)resources.sunlight_shadow_static_cache;
			(void)resources.sunlight_shadow_mask;
			(void)resources.sunlight_shadow_cascade_debug;
			(void)resources.directional_light_shadow_transient_atlas;
			(void)resources.directional_light_shadow_transient_mask;
			return true;
		}

		auto test_ambient_occlusion_temporal_pipeline_contract() -> bool
		{
			std::ifstream config_header_file("project/src/engine/Function/Render/AmbientOcclusionConfig.h");
			if (!config_header_file.is_open())
			{
				return report_self_test_failure("Temporal AO contract", "failed to open AmbientOcclusionConfig.h");
			}
			const std::string config_header_source{
				std::istreambuf_iterator<char>(config_header_file),
				std::istreambuf_iterator<char>() };

			std::ifstream config_source_file("project/src/engine/Function/Render/AmbientOcclusionConfig.cpp");
			if (!config_source_file.is_open())
			{
				return report_self_test_failure("Temporal AO contract", "failed to open AmbientOcclusionConfig.cpp");
			}
			const std::string config_source{
				std::istreambuf_iterator<char>(config_source_file),
				std::istreambuf_iterator<char>() };

			std::ifstream pass_header_file("project/src/engine/Function/Render/AmbientOcclusionPass.h");
			if (!pass_header_file.is_open())
			{
				return report_self_test_failure("Temporal AO contract", "failed to open AmbientOcclusionPass.h");
			}
			const std::string pass_header_source{
				std::istreambuf_iterator<char>(pass_header_file),
				std::istreambuf_iterator<char>() };

			std::ifstream pass_source_file("project/src/engine/Function/Render/AmbientOcclusionPass.cpp");
			if (!pass_source_file.is_open())
			{
				return report_self_test_failure("Temporal AO contract", "failed to open AmbientOcclusionPass.cpp");
			}
			const std::string pass_source{
				std::istreambuf_iterator<char>(pass_source_file),
				std::istreambuf_iterator<char>() };

			std::ifstream temporal_shader_file("project/src/engine/Shaders/Deferred/AmbientOcclusionTemporal.hlsl");
			if (!temporal_shader_file.is_open())
			{
				return report_self_test_failure("Temporal AO contract", "failed to open AmbientOcclusionTemporal.hlsl");
			}
			const std::string temporal_shader_source{
				std::istreambuf_iterator<char>(temporal_shader_file),
				std::istreambuf_iterator<char>() };

			std::ifstream debug_shader_file("project/src/engine/Shaders/Deferred/AmbientOcclusionDebug.hlsl");
			if (!debug_shader_file.is_open())
			{
				return report_self_test_failure("Temporal AO contract", "failed to open AmbientOcclusionDebug.hlsl");
			}
			const std::string debug_shader_source{
				std::istreambuf_iterator<char>(debug_shader_file),
				std::istreambuf_iterator<char>() };

			const bool ok =
				config_header_source.find("bool temporal") != std::string::npos &&
				config_header_source.find("temporal_blend") != std::string::npos &&
				config_header_source.find("temporal_depth_threshold") != std::string::npos &&
				config_header_source.find("temporal_normal_threshold") != std::string::npos &&
				config_header_source.find("TemporalAO") != std::string::npos &&
				config_header_source.find("HistoryWeight") != std::string::npos &&
				config_source.find("temporal_blend") != std::string::npos &&
				config_source.find("temporal_depth_threshold") != std::string::npos &&
				config_source.find("temporal_normal_threshold") != std::string::npos &&
				pass_header_source.find("m_temporal_program") != std::string::npos &&
				pass_header_source.find("m_temporal_history_ao") != std::string::npos &&
				pass_header_source.find("m_temporal_history_meta") != std::string::npos &&
				pass_source.find("AmbientOcclusionTemporal.hlsl") != std::string::npos &&
				pass_source.find("SceneAmbientOcclusionTemporalPass") != std::string::npos &&
				pass_source.find("SceneAmbientOcclusionHistory") != std::string::npos &&
				pass_source.find("SceneAmbientOcclusionHistoryMeta") != std::string::npos &&
				pass_source.find("gbuffer_targets[3]") != std::string::npos &&
				pass_source.find("write_color(1") != std::string::npos &&
				pass_source.find("write_color(2") != std::string::npos &&
				temporal_shader_source.find("SceneAmbientOcclusionHistory") != std::string::npos &&
				temporal_shader_source.find("SceneAmbientOcclusionHistoryMeta") != std::string::npos &&
				temporal_shader_source.find("previous_uv = input.uv - motion.xy") != std::string::npos &&
				temporal_shader_source.find("motion.a") != std::string::npos &&
				temporal_shader_source.find("abs(previous_depth - history_meta.r)") != std::string::npos &&
				temporal_shader_source.find("dot(previous_normal_ws, current.normal_ws)") != std::string::npos &&
				debug_shader_source.find("debug_view == 6u") != std::string::npos &&
				debug_shader_source.find("debug_view == 7u") != std::string::npos;
			return ok ||
				report_self_test_failure("Temporal AO contract", "Temporal AO config, history pass, reprojection shader, or debug views are incomplete");
		}

		auto test_ambient_occlusion_downsampled_scene_uv_contract() -> bool
		{
			const bool common_ok = file_contains_all(
				"project/src/engine/Shaders/Deferred/AmbientOcclusionCommon.hlsli",
				{
					"AshAOSamplesSceneTexturesFromDownsampledTarget",
					"return AshAOParams0.w > 0.5",
					"AshAOSceneTexelSize() * (AshAOSamplesSceneTexturesFromDownsampledTarget() ? 0.5 : 0.0)",
					"SceneDepth.SampleLevel(ScenePointClampSampler, AshAOAdjustedSceneUv(uv), 0)",
					"SceneGBufferE.SampleLevel(ScenePointClampSampler, AshAOAdjustedSceneUv(uv), 0)"
				});
			const bool ssao_ok = file_contains_all(
				"project/src/engine/Shaders/Deferred/AmbientOcclusionSSAO.hlsl",
				{
					"AshAOLoadSurface(input.uv)",
					"AshAOLoadSurface(sample_uv)"
				});
			const bool blur_ok = file_contains_all(
				"project/src/engine/Shaders/Deferred/AmbientOcclusionBlur.hlsl",
				{
					"AshAOSampleSceneDepth(input.uv)",
					"AshAOSampleSceneDepth(sample_uv)"
				});
			const bool pass_ok = file_contains_all(
				"project/src/engine/Function/Render/AmbientOcclusionPass.cpp",
				{
					"scene_textures_sampled_from_downsampled_target",
					"scene_textures_sampled_from_downsampled_target ? 1.0f : 0.0f",
					"make_root_constants(frame, output, m_config, m_config.half_resolution)",
					"make_root_constants(frame, output, m_config, m_config.half_resolution, temporal_blend)",
					"make_root_constants(frame, output, m_config, false)"
				});
			return (common_ok && ssao_ok && blur_ok && pass_ok) ||
				report_self_test_failure("Ambient AO downsampled scene UV", "downsampled AO scene texture reads must land on full-resolution depth texel centers");
		}

		auto test_frame_dump_streaming_quiesce_contract() -> bool
		{
			const bool manager_ok = file_contains_all(
				"project/src/engine/Function/Render/RenderAssetManager.h",
				{
					"bool has_requested_render_assets() const",
					"bool has_pending_render_assets() const"
				});
			const bool application_ok = file_contains_all(
				"project/src/engine/Function/Application.cpp",
				{
					"k_frame_dump_quiesce_frames",
					"renderAssetManager.has_requested_render_assets()",
					"!renderAssetManager.has_pending_render_assets()",
					"frameDumpQuiesceFrameCount = streamingQuiesced ? frameDumpQuiesceFrameCount + 1 : 0",
					"smoke frame limit reached before render asset streaming quiesced"
				});
			return (manager_ok && application_ok) ||
				report_self_test_failure("Frame dump streaming quiesce", "frame dump must be driven by the render asset streaming quiesce signal with the frame limit as timeout fallback");
		}

		auto test_render_debug_view_config_parses_runtime_selection() -> bool
		{
			const std::filesystem::path test_dir = engine_self_test_dir();
			const std::filesystem::path config_path = test_dir / "render_debug_view_self_test.ini";
			{
				std::ofstream config_file(config_path, std::ios::trunc);
				config_file <<
					"[RenderDebugView]\n"
					"Enabled=true\n"
					"Selected=SceneGBufferE\n";
			}

			const RenderDebugViewConfig config = load_runtime_render_debug_view_config(config_path.string().c_str());
			if (!config.enabled || config.selected != "SceneGBufferE")
			{
				return report_self_test_failure("RenderDebugView config", "valid config did not preserve enabled state and selected RT");
			}

			const std::filesystem::path invalid_config_path = test_dir / "render_debug_view_invalid_self_test.ini";
			{
				std::ofstream config_file(invalid_config_path, std::ios::trunc);
				config_file <<
					"[RenderDebugView]\n"
					"Enabled=not-a-bool\n"
					"Selected=SceneDeferredDepth\n";
			}

			const RenderDebugViewConfig invalid_config = load_runtime_render_debug_view_config(invalid_config_path.string().c_str());
			const RenderDebugViewConfig defaults = make_default_render_debug_view_config();
			return (!invalid_config.enabled &&
				invalid_config.enabled == defaults.enabled &&
				invalid_config.selected == "SceneDeferredDepth") ||
				report_self_test_failure("RenderDebugView config", "invalid bool should fall back while selected string is preserved");
		}

		auto test_render_debug_view_registry_replaces_duplicate_items() -> bool
		{
			RenderGraphBuilder graph = RenderGraphBuilder::create_headless_for_tests("RenderDebugViewRegistrySelfTest");

			RenderTargetDesc desc_a{};
			desc_a.width = 64;
			desc_a.height = 64;
			desc_a.format = RenderTextureFormat::RGBA8_UNORM;
			RenderGraphTextureRef texture_a = graph.register_external_texture_desc_for_tests(desc_a, "SceneGBufferA");

			RenderTargetDesc desc_b{};
			desc_b.width = 128;
			desc_b.height = 64;
			desc_b.format = RenderTextureFormat::RGBA16_SFLOAT;
			RenderGraphTextureRef texture_b = graph.register_external_texture_desc_for_tests(desc_b, "SceneGBufferAUpdated");

			RenderDebugViewFrameRegistry registry{};
			registry.begin_frame();
			registry.register_item({
				"SceneGBufferA",
				"GBuffer A",
				texture_a,
				RenderDebugVisualization::Color,
				RenderTextureFormat::RGBA8_UNORM,
				64u,
				64u });
			registry.register_item({
				"SceneGBufferA",
				"GBuffer A Updated",
				texture_b,
				RenderDebugVisualization::Normal,
				RenderTextureFormat::RGBA16_SFLOAT,
				128u,
				64u });

			const RenderDebugViewItem* item = registry.find_item("SceneGBufferA");
			const bool ok =
				registry.get_items().size() == 1u &&
				item != nullptr &&
				item->texture == texture_b &&
				item->display_name == "GBuffer A Updated" &&
				item->visualization == RenderDebugVisualization::Normal &&
				item->format == RenderTextureFormat::RGBA16_SFLOAT &&
				item->width == 128u &&
				item->height == 64u;
			return ok ||
				report_self_test_failure("RenderDebugView registry", "duplicate debug item did not update in-place");
		}

		auto test_render_debug_view_graph_pass_contract() -> bool
		{
			RenderGraphBuilder graph = RenderGraphBuilder::create_headless_for_tests("RenderDebugViewGraphSelfTest");

			RenderTargetDesc output_desc{};
			output_desc.width = 64;
			output_desc.height = 64;
			output_desc.format = RenderTextureFormat::RGBA8_UNORM;
			RenderGraphTextureRef output = graph.register_external_texture_desc_for_tests(output_desc, "SceneOutput");

			RenderGraphTextureDesc selected_desc{};
			selected_desc.width = 64;
			selected_desc.height = 64;
			selected_desc.format = RenderTextureFormat::RGBA16_SFLOAT;
			selected_desc.shader_resource = true;
			RenderGraphTextureRef selected = graph.create_texture(selected_desc, "SceneDeferredSceneHDRLinear");

			graph.add_raster_pass(
				"SelectedProducer",
				RenderGraphPassFlags::None,
				[&](RenderGraphRasterPassBuilder& pass)
				{
					pass.write_color(0, selected, RenderLoadAction::Clear, {});
				},
				[](RenderGraphRasterContext&)
				{
					return true;
				});

			if (!RenderDebugView::add_pass_for_tests(graph, selected, output))
			{
				return report_self_test_failure("RenderDebugView graph pass", "failed to add debug view pass in headless graph");
			}

			RenderGraphCompileResult result{};
			if (!graph.compile_for_tests(result))
			{
				return report_self_test_failure("RenderDebugView graph pass", "debug view graph did not compile");
			}

			bool saw_selected_srv = false;
			bool saw_output_rtv = false;
			if (result.pass_barriers.size() > 1u)
			{
				for (const auto& transition : result.pass_barriers[1].transitions)
				{
					if (transition.texture == selected && transition.state == RHI::AshResourceState::SRVGraphics)
					{
						saw_selected_srv = true;
					}
					if (transition.texture == output && transition.state == RHI::AshResourceState::RTV)
					{
						saw_output_rtv = true;
					}
				}
			}

			RenderGraphBuilder bypass_graph = RenderGraphBuilder::create_headless_for_tests("RenderDebugViewBypassSelfTest");
			RenderGraphTextureRef bypass_output = bypass_graph.register_external_texture_desc_for_tests(output_desc, "SceneOutput");
			const bool bypass_off = RenderDebugView::should_bypass_debug_pass("Off");
			const bool bypass_scene_output = RenderDebugView::should_bypass_debug_pass("SceneOutput");
			const bool bypass_added = RenderDebugView::add_pass_for_tests(bypass_graph, bypass_output, bypass_output);

			const bool ok =
				result.live_pass_indices.size() == 2u &&
				result.live_pass_indices[0] == 0u &&
				result.live_pass_indices[1] == 1u &&
				saw_selected_srv &&
				saw_output_rtv &&
				bypass_off &&
				bypass_scene_output &&
				bypass_added &&
				bypass_graph.get_pass_count_for_tests() == 0u;
			return ok ||
				report_self_test_failure("RenderDebugView graph pass", "debug pass did not declare selected SRV/output RTV or bypass correctly");
		}

		auto test_render_debug_view_linear_hdr_uses_raw_preview() -> bool
		{
			std::ifstream shader_file("project/src/engine/Shaders/Debug/RenderDebugView.hlsl");
			if (!shader_file.is_open())
			{
				return report_self_test_failure("RenderDebugView LinearHDR", "failed to open RenderDebugView.hlsl");
			}
			const std::string shader_source{
				std::istreambuf_iterator<char>(shader_file),
				std::istreambuf_iterator<char>() };

			const size_t branch_begin = shader_source.find("if (mode == 1u)");
			if (branch_begin == std::string::npos)
			{
				return report_self_test_failure("RenderDebugView LinearHDR", "LinearHDR shader branch was not found");
			}
			const size_t branch_end = shader_source.find("else if", branch_begin);
			if (branch_end == std::string::npos)
			{
				return report_self_test_failure("RenderDebugView LinearHDR", "LinearHDR shader branch end was not found");
			}
			const std::string linear_hdr_branch = shader_source.substr(branch_begin, branch_end - branch_begin);

			const bool ok =
				linear_hdr_branch.find("AshDebugVisualizeLinearHDR") != std::string::npos &&
				linear_hdr_branch.find("AshDebugACESFilm") == std::string::npos &&
				linear_hdr_branch.find("ACES") == std::string::npos;
			return ok ||
				report_self_test_failure("RenderDebugView LinearHDR", "LinearHDR debug path must show raw pre-tonemap HDR, not an ACES-tonemapped preview");
		}

		auto test_reverse_z_projection_maps_near_far_depths() -> bool
		{
			Scene scene = Scene::create("ReverseZProjectionSelfTest");
			Entity camera_entity = scene.create_entity("Camera");
			CameraComponent camera{};
			camera.primary = true;
			camera.reverse_z = true;
			camera.near_plane = 0.5f;
			camera.far_plane = 100.0f;
			camera_entity.add_camera_component(camera);

			SceneView view{};
			SceneViewDesc desc{};
			desc.viewport_width = 128;
			desc.viewport_height = 64;
			if (!build_primary_scene_view(scene, desc, view))
			{
				return report_self_test_failure("ReverseZ projection", "failed to build scene view");
			}

			const glm::vec4 near_clip = view.projection * glm::vec4(0.0f, 0.0f, camera.near_plane, 1.0f);
			const glm::vec4 far_clip = view.projection * glm::vec4(0.0f, 0.0f, camera.far_plane, 1.0f);
			const float near_ndc = near_clip.z / near_clip.w;
			const float far_ndc = far_clip.z / far_clip.w;
			const bool ok =
				view.reverse_z &&
				std::abs(near_ndc - 1.0f) < 0.0001f &&
				std::abs(far_ndc - 0.0f) < 0.0001f;
			return ok ||
				report_self_test_failure("ReverseZ projection", "camera reverse_z did not reverse near/far depth mapping");
		}

		auto test_reverse_z_flips_depth_clear_and_compare() -> bool
		{
			GraphicsProgramState state{};
			state.depth_test = true;
			state.depth_write = true;
			state.depth_compare = RenderCompareOp::LessEqual;

			RHI::PipelineCreation pipeline{};
			fill_pipeline_state_from_graphics_program_state(state, pipeline, 1u, true);
			const bool ok =
				get_scene_view_default_depth_clear_value(true) == 0.0f &&
				get_scene_view_near_clip_depth(true) == 1.0f &&
				get_scene_view_far_clip_depth(true) == 0.0f &&
				resolve_scene_view_depth_clear_value(1.0f, true) == 0.0f &&
				resolve_scene_view_depth_clear_value(0.5f, true) == 0.5f &&
				pipeline.depth_stencil.depth_comparison == RHI::ASH_COMPARE_OP_GREATER_OR_EQUAL;
			return ok ||
				report_self_test_failure("ReverseZ depth state", "clear depth or depth compare did not follow ReverseZ");
		}

		auto test_deferred_shader_background_depth_uses_reverse_z_flag() -> bool
		{
			std::ifstream shader_file("project/src/engine/Shaders/Deferred/DeferredCommon.hlsli");
			if (!shader_file.is_open())
			{
				return report_self_test_failure("Deferred ReverseZ shader", "failed to open DeferredCommon.hlsli");
			}
			const std::string shader_source{
				std::istreambuf_iterator<char>(shader_file),
				std::istreambuf_iterator<char>() };
			const bool ok =
				shader_source.find("AshSceneDepthIsBackground") != std::string::npos &&
				shader_source.find("AshCameraPositionAndFlags.w") != std::string::npos;
			return ok ||
				report_self_test_failure("Deferred ReverseZ shader", "deferred surface decode does not branch background depth on the ReverseZ flag");
		}

		auto test_deferred_light_volume_draws_carry_reverse_z_flag() -> bool
		{
			std::ifstream source_file("project/src/engine/Function/Render/DeferredLightingPass.cpp");
			if (!source_file.is_open())
			{
				return report_self_test_failure("Deferred ReverseZ light volumes", "failed to open DeferredLightingPass.cpp");
			}
			const std::string source{
				std::istreambuf_iterator<char>(source_file),
				std::istreambuf_iterator<char>() };
			const bool ok = source.find("draw_desc.reverse_z = view_context.reverse_z") != std::string::npos;
			return ok ||
				report_self_test_failure("Deferred ReverseZ light volumes", "deferred light volume draws do not carry the view ReverseZ flag");
		}

		auto test_static_mesh_gbuffer_shader_writes_motion_vectors() -> bool
		{
			std::ifstream shader_file("project/src/engine/Shaders/MaterialV2/Families/SurfaceStaticMeshGBuffer.hlsl");
			if (!shader_file.is_open())
			{
				return report_self_test_failure("StaticMesh GBuffer motion vectors", "failed to open SurfaceStaticMeshGBuffer.hlsl");
			}
			const std::string shader_source{
				std::istreambuf_iterator<char>(shader_file),
				std::istreambuf_iterator<char>() };
			const bool ok =
				shader_source.find("previous_object_to_clip") != std::string::npos &&
				shader_source.find("current_uv - previous_uv") != std::string::npos &&
				shader_source.find("output.target3 = float4(motion_vector") != std::string::npos;
			return ok ||
				report_self_test_failure("StaticMesh GBuffer motion vectors", "GBufferD is not populated from current and previous clip-space positions");
		}

		auto test_surface_static_mesh_shader_hash_tracks_shared_includes() -> bool
		{
			std::ifstream registry_file("project/src/engine/Function/Render/EngineShaderFamilyRegistry.cpp");
			if (!registry_file.is_open())
			{
				return report_self_test_failure("SurfaceStaticMesh shader include hash", "failed to open EngineShaderFamilyRegistry.cpp");
			}
			const std::string registry_source{
				std::istreambuf_iterator<char>(registry_file),
				std::istreambuf_iterator<char>() };

			std::ifstream shader_map_file("project/src/engine/Function/Render/MaterialShaderMap.cpp");
			if (!shader_map_file.is_open())
			{
				return report_self_test_failure("SurfaceStaticMesh shader include hash", "failed to open MaterialShaderMap.cpp");
			}
			const std::string shader_map_source{
				std::istreambuf_iterator<char>(shader_map_file),
				std::istreambuf_iterator<char>() };

			std::ifstream proxy_file("project/src/engine/Function/Render/MaterialRenderProxy.cpp");
			if (!proxy_file.is_open())
			{
				return report_self_test_failure("SurfaceStaticMesh shader include hash", "failed to open MaterialRenderProxy.cpp");
			}
			const std::string proxy_source{
				std::istreambuf_iterator<char>(proxy_file),
				std::istreambuf_iterator<char>() };

			const bool ok =
				registry_source.find("AshVertexDeclLocations.hlsli") != std::string::npos &&
				registry_source.find("AshSurfaceDomain.hlsli") != std::string::npos &&
				shader_map_source.find("hash_engine_shader_family_file_signatures(hash_value, family)") != std::string::npos &&
				proxy_source.find("hash_engine_shader_family_file_signatures(hash_value, resource.usage.family)") != std::string::npos;
			return ok ||
				report_self_test_failure("SurfaceStaticMesh shader include hash", "shared Surface.StaticMesh includes are not part of material shader signatures");
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

		auto test_scene_renderer_instance_buffer_slots_are_isolated_between_view_submits() -> bool
		{
			size_t next_slot = 0;
			const size_t scene_view_base = SceneRenderer::reserve_instance_buffer_slot_range(next_slot, 1);
			const size_t game_view_base = SceneRenderer::reserve_instance_buffer_slot_range(next_slot, 1);
			const size_t scene_view_slot = SceneRenderer::resolve_instance_buffer_slot(scene_view_base, 0);
			const size_t game_view_slot = SceneRenderer::resolve_instance_buffer_slot(game_view_base, 0);
			if (scene_view_slot == game_view_slot)
			{
				return report_self_test_failure("SceneRenderer instance buffer slots", "two view submits reused the same instance buffer slot");
			}

			const size_t batched_view_base = SceneRenderer::reserve_instance_buffer_slot_range(next_slot, 3);
			if (SceneRenderer::resolve_instance_buffer_slot(batched_view_base, 0) != batched_view_base ||
				SceneRenderer::resolve_instance_buffer_slot(batched_view_base, 1) != batched_view_base + 1 ||
				SceneRenderer::resolve_instance_buffer_slot(batched_view_base, 2) != batched_view_base + 2)
			{
				return report_self_test_failure("SceneRenderer instance buffer slots", "batched view slots were not contiguous");
			}

			return true;
		}

		auto test_scene_renderer_instance_buffer_slots_are_lagged_between_frames() -> bool
		{
			const size_t logical_slot = SceneRenderer::resolve_instance_buffer_slot(0, 0);
			const size_t frame0_slot =
				SceneRenderer::resolve_frame_lagged_instance_buffer_slot(logical_slot, 0);
			const size_t frame1_slot =
				SceneRenderer::resolve_frame_lagged_instance_buffer_slot(logical_slot, 1);
			const size_t frame2_slot =
				SceneRenderer::resolve_frame_lagged_instance_buffer_slot(logical_slot, 2);
			if (frame0_slot == frame1_slot || frame0_slot == frame2_slot || frame1_slot == frame2_slot)
			{
				return report_self_test_failure(
					"SceneRenderer frame-lagged instance buffer slots",
					"consecutive frames reused the same physical instance buffer slot");
			}

			const size_t wrapped_slot =
				SceneRenderer::resolve_frame_lagged_instance_buffer_slot(
					logical_slot,
					SceneRenderer::instance_buffer_frame_lag());
			if (wrapped_slot != frame0_slot)
			{
				return report_self_test_failure(
					"SceneRenderer frame-lagged instance buffer slots",
					"instance buffer slot ring did not wrap after the configured frame lag");
			}

			return true;
		}

		auto test_scene_renderer_temporal_history_uses_render_frame_epoch() -> bool
		{
			std::ifstream renderer_file("project/src/engine/Function/Render/SceneRenderer.cpp");
			if (!renderer_file.is_open())
			{
				return report_self_test_failure(
					"SceneRenderer temporal history",
					"failed to open SceneRenderer.cpp");
			}

			const std::string renderer_source{
				std::istreambuf_iterator<char>(renderer_file),
				std::istreambuf_iterator<char>() };
			const bool ok =
				renderer_source.find("resolve_render_frame_index(frame)") != std::string::npos &&
				renderer_source.find("begin_instance_buffer_frame(render_frame_index)") != std::string::npos &&
				renderer_source.find("find_previous_temporal_view_state(temporal_view_key)") != std::string::npos &&
				renderer_source.find("commit_temporal_view_state(temporal_view_key, frame)") != std::string::npos &&
				renderer_source.find("found->second.frame_index >= frame_index") == std::string::npos &&
				renderer_source.find("resolve_frame_lagged_instance_buffer_slot(logical_instance_buffer_slot, frame.frame_index)") == std::string::npos;
			return ok ||
				report_self_test_failure(
					"SceneRenderer temporal history",
					"motion vector history or instance-buffer ring still depends on the logic-side VisibleRenderFrame frame_index");
		}

		auto test_scene_renderer_temporal_history_is_gbuffer_only() -> bool
		{
			std::ifstream renderer_file("project/src/engine/Function/Render/SceneRenderer.cpp");
			if (!renderer_file.is_open())
			{
				return report_self_test_failure(
					"SceneRenderer temporal history",
					"failed to open SceneRenderer.cpp");
			}

			const std::string renderer_source{
				std::istreambuf_iterator<char>(renderer_file),
				std::istreambuf_iterator<char>() };
			const bool ok =
				renderer_source.find("should_use_temporal_history_for_pass(PassFamily pass_family)") != std::string::npos &&
				renderer_source.find("pass_family == PassFamily::GBuffer") != std::string::npos &&
				renderer_source.find("if (should_use_temporal_history_for_pass(pass_family))") != std::string::npos;
			return ok ||
				report_self_test_failure(
					"SceneRenderer temporal history",
					"non-GBuffer passes can still inherit camera temporal history in their instance buffers");
		}

		auto test_renderer_frame_stats_cover_presented_frame() -> bool
		{
			std::ifstream renderer_source_file("project/src/engine/Function/Render/Renderer.cpp");
			if (!renderer_source_file.is_open())
			{
				return report_self_test_failure(
					"Renderer frame stats timing",
					"failed to open Renderer.cpp");
			}

			std::ifstream renderer_header_file("project/src/engine/Function/Render/Renderer.h");
			if (!renderer_header_file.is_open())
			{
				return report_self_test_failure(
					"Renderer frame stats timing",
					"failed to open Renderer.h");
			}

			const std::string renderer_source{
				std::istreambuf_iterator<char>(renderer_source_file),
				std::istreambuf_iterator<char>() };
			const std::string renderer_header{
				std::istreambuf_iterator<char>(renderer_header_file),
				std::istreambuf_iterator<char>() };

			const size_t start_time_pos = renderer_source.find("m_frame_start_time = std::chrono::steady_clock::now();");
			const size_t begin_frame_pos = renderer_source.find("m_render_device && m_render_device->begin_frame()");
			const size_t present_pos = renderer_source.find("m_render_device->present();");
			const size_t complete_frame_pos = renderer_source.find("complete_frame_timing();");
			const bool starts_before_backend_begin =
				start_time_pos != std::string::npos &&
				begin_frame_pos != std::string::npos &&
				start_time_pos < begin_frame_pos;
			const bool completes_after_present =
				present_pos != std::string::npos &&
				complete_frame_pos != std::string::npos &&
				present_pos < complete_frame_pos;
			const bool end_frame_no_longer_completes =
				renderer_source.find("m_last_completed_frame_stats = m_frame_stats;") == std::string::npos;
			const bool declares_completion_helper =
				renderer_header.find("void complete_frame_timing();") != std::string::npos;

			return (starts_before_backend_begin && completes_after_present && end_frame_no_longer_completes && declares_completion_helper) ||
				report_self_test_failure(
					"Renderer frame stats timing",
					"RendererFrameStats must measure from before backend begin_frame wait through present completion");
		}

		auto test_renderer_frame_stats_expose_frame_pacing_breakdown() -> bool
		{
			std::ifstream renderer_source_file("project/src/engine/Function/Render/Renderer.cpp");
			std::ifstream renderer_header_file("project/src/engine/Function/Render/Renderer.h");
			if (!renderer_source_file.is_open() || !renderer_header_file.is_open())
			{
				return report_self_test_failure(
					"Renderer frame pacing stats",
					"failed to open Renderer source or header");
			}

			const std::string renderer_source{
				std::istreambuf_iterator<char>(renderer_source_file),
				std::istreambuf_iterator<char>() };
			const std::string renderer_header{
				std::istreambuf_iterator<char>(renderer_header_file),
				std::istreambuf_iterator<char>() };

			const bool declares_breakdown =
				renderer_header.find("backend_begin_frame_time_ms") != std::string::npos &&
				renderer_header.find("render_end_frame_time_ms") != std::string::npos &&
				renderer_header.find("present_time_ms") != std::string::npos;
			const bool measures_backend_begin =
				renderer_source.find("m_frame_stats.backend_begin_frame_time_ms") != std::string::npos;
			const bool measures_end_frame =
				renderer_source.find("m_frame_stats.render_end_frame_time_ms") != std::string::npos;
			const bool measures_present =
				renderer_source.find("m_frame_stats.present_time_ms") != std::string::npos;

			return (declares_breakdown && measures_backend_begin && measures_end_frame && measures_present) ||
				report_self_test_failure(
					"Renderer frame pacing stats",
					"RendererFrameStats must expose backend begin-frame, render end-frame, and present CPU timing breakdowns");
		}

		auto test_rhi_frame_slots_match_default_triple_buffering() -> bool
		{
			std::ifstream vulkan_header_file("project/src/engine/Graphics/Vulkan/VulkanContext.h");
			std::ifstream dx12_header_file("project/src/engine/Graphics/DirectX12/DX12Context.h");
			if (!vulkan_header_file.is_open() || !dx12_header_file.is_open())
			{
				return report_self_test_failure(
					"RHI frame slot depth",
					"failed to open Vulkan or DX12 context headers");
			}

			const std::string vulkan_header{
				std::istreambuf_iterator<char>(vulkan_header_file),
				std::istreambuf_iterator<char>() };
			const std::string dx12_header{
				std::istreambuf_iterator<char>(dx12_header_file),
				std::istreambuf_iterator<char>() };

			const bool ok =
				vulkan_header.find("k_max_frames = 3") != std::string::npos &&
				dx12_header.find("k_dx12_max_frames = 3") != std::string::npos;
			return ok ||
				report_self_test_failure(
					"RHI frame slot depth",
					"Vulkan and DX12 frame-resource rings must match the default 3-buffer swapchain");
		}

		auto test_scene_presentation_reuses_prepared_material_proxy() -> bool
		{
			std::ifstream source_file("project/src/engine/Function/Render/ScenePresentationSubsystem.cpp");
			if (!source_file.is_open())
			{
				return report_self_test_failure(
					"ScenePresentation material proxy prepare",
					"failed to open ScenePresentationSubsystem.cpp");
			}

			const std::string source{
				std::istreambuf_iterator<char>(source_file),
				std::istreambuf_iterator<char>() };
			const size_t request_pos = source.find("material_proxy = asset_manager.request_material_render_proxy(section.material);");
			const size_t conditional_prepare_pos = source.find("if (material_proxy->needs_surface_staticmesh_preparation())");
			const size_t prepare_pos = source.find("material_proxy->prepare_surface_staticmesh(asset_manager, renderer)");
			const bool ok =
				request_pos != std::string::npos &&
				conditional_prepare_pos != std::string::npos &&
				prepare_pos != std::string::npos &&
				request_pos < conditional_prepare_pos &&
				conditional_prepare_pos < prepare_pos;
			return ok ||
				report_self_test_failure(
					"ScenePresentation material proxy prepare",
					"prepared cached MaterialRenderProxy instances must not be prepared again every visible frame");
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
			state.primitive_topology = RenderPrimitiveTopology::LineList;
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
			const bool topology_ok = pipeline.primitiveTopology == RHI::ASH_PRIMITIVE_TOPOLOGY_LINE_LIST;
			return (depth_ok && blend_ok && topology_ok) ||
				report_self_test_failure("Deferred light volume render state", "depth compare, additive blend, or line-list topology mapping is invalid");
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

		auto test_render_graph_builder_records_raster_usage() -> bool
		{
			RenderGraphBuilder graph = RenderGraphBuilder::create_headless_for_tests("RenderGraphBuilderSelfTest");

			RenderTargetDesc external_desc{};
			external_desc.width = 64;
			external_desc.height = 64;
			external_desc.format = RenderTextureFormat::RGBA8_UNORM;
			RenderGraphTextureRef output = graph.register_external_texture_desc_for_tests(external_desc, "Output");

			RenderGraphTextureDesc transient_desc{};
			transient_desc.width = 64;
			transient_desc.height = 64;
			transient_desc.format = RenderTextureFormat::RGBA8_UNORM;
			transient_desc.shader_resource = true;
			RenderGraphTextureRef intermediate = graph.create_texture(transient_desc, "Intermediate");

			bool setup_called = false;
			const bool added = graph.add_raster_pass(
				"SelfTestRasterPass",
				RenderGraphPassFlags::None,
				[&](RenderGraphRasterPassBuilder& pass)
				{
					setup_called = true;
					pass.read_texture(intermediate, RenderGraphAccess::GraphicsSRV);
					pass.write_color(0, output, RenderLoadAction::Clear, {});
				},
				[](RenderGraphRasterContext&)
				{
					return true;
				});

			bool ok = added && setup_called;
			ok = ok && graph.get_texture_count_for_tests() == 2;
			ok = ok && graph.get_pass_count_for_tests() == 1;
			return ok || report_self_test_failure("RenderGraph builder raster usage", "builder did not record expected pass/resource usage");
		}

		auto test_render_graph_compiler_culls_dead_passes_and_keeps_roots() -> bool
		{
			RenderGraphBuilder graph = RenderGraphBuilder::create_headless_for_tests("RenderGraphCompilerSelfTest");

			RenderTargetDesc output_desc{};
			output_desc.width = 64;
			output_desc.height = 64;
			output_desc.format = RenderTextureFormat::RGBA8_UNORM;
			RenderGraphTextureRef output = graph.register_external_texture_desc_for_tests(output_desc, "Output");

			RenderGraphTextureDesc temp_desc{};
			temp_desc.width = 64;
			temp_desc.height = 64;
			temp_desc.format = RenderTextureFormat::RGBA8_UNORM;
			temp_desc.shader_resource = true;
			RenderGraphTextureRef live_temp = graph.create_texture(temp_desc, "LiveTemp");
			RenderGraphTextureRef dead_temp = graph.create_texture(temp_desc, "DeadTemp");

			graph.add_raster_pass(
				"LiveProducer",
				RenderGraphPassFlags::None,
				[&](RenderGraphRasterPassBuilder& pass)
				{
					pass.write_color(0, live_temp, RenderLoadAction::Clear, {});
				},
				[](RenderGraphRasterContext&)
				{
					return true;
				});

			graph.add_raster_pass(
				"LiveConsumer",
				RenderGraphPassFlags::None,
				[&](RenderGraphRasterPassBuilder& pass)
				{
					pass.read_texture(live_temp, RenderGraphAccess::GraphicsSRV);
					pass.write_color(0, output, RenderLoadAction::Clear, {});
				},
				[](RenderGraphRasterContext&)
				{
					return true;
				});

			graph.add_raster_pass(
				"DeadProducer",
				RenderGraphPassFlags::None,
				[&](RenderGraphRasterPassBuilder& pass)
				{
					pass.write_color(0, dead_temp, RenderLoadAction::Clear, {});
				},
				[](RenderGraphRasterContext&)
				{
					return true;
				});

			graph.add_compute_pass(
				"SideEffectCompute",
				RenderGraphPassFlags::NeverCull,
				[](RenderGraphComputePassBuilder&)
				{
				},
				[](RenderGraphComputeContext&)
				{
					return true;
				});

			RenderGraphCompileResult result{};
			const bool compiled = graph.compile_for_tests(result);
			bool ok = compiled;
			ok = ok && result.live_pass_indices.size() == 3;
			ok = ok && result.live_pass_indices[0] == 0;
			ok = ok && result.live_pass_indices[1] == 1;
			ok = ok && result.live_pass_indices[2] == 3;
			ok = ok && result.texture_lifetimes[live_temp.index].first_pass == 0;
			ok = ok && result.texture_lifetimes[live_temp.index].last_pass == 1;
			ok = ok && result.texture_lifetimes[dead_temp.index].used == false;
			ok = ok && result.pass_barriers[0].transitions.size() == 1;
			ok = ok && result.pass_barriers[0].transitions[0].texture == live_temp;
			ok = ok && result.pass_barriers[0].transitions[0].state == RHI::AshResourceState::RTV;
			ok = ok && result.pass_barriers[1].transitions.size() == 2;
			ok = ok && result.pass_barriers[1].transitions[0].texture == live_temp;
			ok = ok && result.pass_barriers[1].transitions[0].state == RHI::AshResourceState::SRVGraphics;
			ok = ok && result.pass_barriers[1].transitions[1].texture == output;
			ok = ok && result.pass_barriers[1].transitions[1].state == RHI::AshResourceState::RTV;
			return ok || report_self_test_failure("RenderGraph compiler culling", "compiler did not cull dead passes or preserve roots");
		}

		auto test_render_graph_compile_cache_reuses_stable_topology() -> bool
		{
			RenderGraphCompiler::reset_compile_cache_for_tests();

			RenderTargetDesc output_desc{};
			output_desc.width = 64;
			output_desc.height = 64;
			output_desc.format = RenderTextureFormat::RGBA8_UNORM;

			RenderGraphTextureDesc temp_desc{};
			temp_desc.width = 64;
			temp_desc.height = 64;
			temp_desc.format = RenderTextureFormat::RGBA16_SFLOAT;
			temp_desc.shader_resource = true;

			auto build_graph = [&](uint32_t output_width) -> RenderGraphBuilder
			{
				RenderGraphBuilder graph = RenderGraphBuilder::create_headless_for_tests("RenderGraphCacheSelfTest");
				RenderTargetDesc per_graph_output_desc = output_desc;
				per_graph_output_desc.width = output_width;
				RenderGraphTextureRef output = graph.register_external_texture_desc_for_tests(per_graph_output_desc, "Output");
				RenderGraphTextureRef temp = graph.create_texture(temp_desc, "Temp");
				graph.add_raster_pass(
					"Producer",
					RenderGraphPassFlags::None,
					[&](RenderGraphRasterPassBuilder& pass)
					{
						pass.write_color(0, temp, RenderLoadAction::Clear, {});
					},
					[](RenderGraphRasterContext&)
					{
						return true;
					});
				graph.add_raster_pass(
					"Consumer",
					RenderGraphPassFlags::None,
					[&](RenderGraphRasterPassBuilder& pass)
					{
						pass.read_texture(temp, RenderGraphAccess::GraphicsSRV);
						pass.write_color(0, output, RenderLoadAction::Clear, {});
					},
					[](RenderGraphRasterContext&)
					{
						return true;
					});
				return graph;
			};

			RenderGraphCompileResult first{};
			RenderGraphCompileResult second{};
			RenderGraphCompileResult resized{};
			RenderGraphBuilder graph = build_graph(64);
			RenderGraphBuilder same_graph = build_graph(64);
			RenderGraphBuilder resized_graph = build_graph(128);

			const bool first_ok = graph.compile_cached_for_tests(first);
			const RenderGraphCompileCacheStats after_first = RenderGraphCompiler::get_compile_cache_stats_for_tests();
			const bool second_ok = same_graph.compile_cached_for_tests(second);
			const RenderGraphCompileCacheStats after_second = RenderGraphCompiler::get_compile_cache_stats_for_tests();
			const bool resized_ok = resized_graph.compile_cached_for_tests(resized);
			const RenderGraphCompileCacheStats after_resized = RenderGraphCompiler::get_compile_cache_stats_for_tests();

			bool ok = first_ok && second_ok && resized_ok;
			ok = ok && after_first.misses == 1u && after_first.hits == 0u;
			ok = ok && after_second.misses == 1u && after_second.hits == 1u;
			ok = ok && after_resized.misses == 1u && after_resized.hits == 2u;
			ok = ok && first.live_pass_indices == second.live_pass_indices;
			ok = ok && first.live_pass_indices == resized.live_pass_indices;
			return ok || report_self_test_failure("RenderGraph compile cache", "stable topology did not reuse the cached compile result");
		}

		auto test_scene_deferred_graph_resources_describe_live_pass_chain() -> bool
		{
			RenderGraphBuilder graph = RenderGraphBuilder::create_headless_for_tests("SceneDeferredGraphSelfTest");

			RenderTargetDesc output_desc{};
			output_desc.width = 64;
			output_desc.height = 64;
			output_desc.format = RenderTextureFormat::RGBA8_UNORM;
			RenderGraphTextureRef output = graph.register_external_texture_desc_for_tests(output_desc, "SceneOutput");

			SceneDeferredGraphResources resources{};
			resources.gbuffer_targets.reserve(5u);
			for (uint32_t index = 0; index < 5u; ++index)
			{
				RenderGraphTextureDesc gbuffer_desc{};
				gbuffer_desc.width = 64;
				gbuffer_desc.height = 64;
				gbuffer_desc.format = RenderTextureFormat::RGBA8_UNORM;
				gbuffer_desc.shader_resource = true;
				resources.gbuffer_targets.push_back(graph.create_texture(gbuffer_desc, "SceneGBuffer"));
			}

			RenderGraphTextureDesc depth_desc{};
			depth_desc.width = 64;
			depth_desc.height = 64;
			depth_desc.format = RenderTextureFormat::D32_SFLOAT;
			depth_desc.shader_resource = true;
			resources.depth = graph.create_texture(depth_desc, "SceneDeferredDepth");

			RenderGraphTextureDesc ambient_occlusion_desc{};
			ambient_occlusion_desc.width = 64;
			ambient_occlusion_desc.height = 64;
			ambient_occlusion_desc.format = RenderTextureFormat::RGBA8_UNORM;
			ambient_occlusion_desc.shader_resource = true;
			resources.ambient_occlusion = graph.create_texture(ambient_occlusion_desc, "SceneAmbientOcclusion");

			RenderGraphTextureDesc lighting_desc{};
			lighting_desc.width = 64;
			lighting_desc.height = 64;
			lighting_desc.format = RenderTextureFormat::RGBA16_SFLOAT;
			lighting_desc.shader_resource = true;
			resources.lighting_diffuse = graph.create_texture(lighting_desc, "SceneDeferredLightingDiffuse");
			resources.lighting_specular = graph.create_texture(lighting_desc, "SceneDeferredLightingSpecular");
			resources.scene_hdr_linear = graph.create_texture(lighting_desc, "SceneDeferredSceneHDRLinear");

			graph.add_raster_pass(
				"SceneGBufferPass",
				RenderGraphPassFlags::None,
				[&](RenderGraphRasterPassBuilder& pass)
				{
					for (uint8_t index = 0; index < static_cast<uint8_t>(resources.gbuffer_targets.size()); ++index)
					{
						pass.write_color(index, resources.gbuffer_targets[index], RenderLoadAction::Clear, {});
					}
					pass.write_depth(resources.depth, RenderLoadAction::Clear, {});
				},
				[](RenderGraphRasterContext&)
				{
					return true;
				});

			graph.add_raster_pass(
				"SceneAmbientOcclusionPass",
				RenderGraphPassFlags::None,
				[&](RenderGraphRasterPassBuilder& pass)
				{
					pass.read_texture(resources.gbuffer_targets[4], RenderGraphAccess::GraphicsSRV);
					pass.read_texture(resources.depth, RenderGraphAccess::GraphicsSRV);
					pass.write_color(0, resources.ambient_occlusion, RenderLoadAction::Clear, { 1.0f, 1.0f, 1.0f, 1.0f });
				},
				[](RenderGraphRasterContext&)
				{
					return true;
				});

			graph.add_raster_pass(
				"SceneDeferredLightingBasePass",
				RenderGraphPassFlags::None,
				[&](RenderGraphRasterPassBuilder& pass)
				{
					for (RenderGraphTextureRef gbuffer : resources.gbuffer_targets)
					{
						pass.read_texture(gbuffer, RenderGraphAccess::GraphicsSRV);
					}
					pass.read_depth(resources.depth, RenderGraphDepthReadMode::DepthTestAndShaderResource);
					pass.read_texture(resources.ambient_occlusion, RenderGraphAccess::GraphicsSRV);
					pass.write_color(0, resources.lighting_diffuse, RenderLoadAction::Clear, {});
					pass.write_color(1, resources.lighting_specular, RenderLoadAction::Clear, {});
				},
				[](RenderGraphRasterContext&)
				{
					return true;
				});

			graph.add_raster_pass(
				"SceneDeferredEnvironmentLightingPass",
				RenderGraphPassFlags::None,
				[&](RenderGraphRasterPassBuilder& pass)
				{
					for (RenderGraphTextureRef gbuffer : resources.gbuffer_targets)
					{
						pass.read_texture(gbuffer, RenderGraphAccess::GraphicsSRV);
					}
					pass.read_texture(resources.ambient_occlusion, RenderGraphAccess::GraphicsSRV);
					pass.read_depth(resources.depth, RenderGraphDepthReadMode::DepthTestAndShaderResource);
					pass.write_color(0, resources.lighting_diffuse, RenderLoadAction::Load, {});
					pass.write_color(1, resources.lighting_specular, RenderLoadAction::Load, {});
				},
				[](RenderGraphRasterContext&)
				{
					return true;
				});

			RenderGraphTextureRef scene_hdr_with_sky = graph.create_texture(lighting_desc, "SceneDeferredSceneHDRWithSky");

			graph.add_raster_pass(
				"SceneDeferredCompositePass",
				RenderGraphPassFlags::None,
				[&](RenderGraphRasterPassBuilder& pass)
				{
					pass.read_texture(resources.lighting_diffuse, RenderGraphAccess::GraphicsSRV);
					pass.read_texture(resources.lighting_specular, RenderGraphAccess::GraphicsSRV);
					pass.write_color(0, resources.scene_hdr_linear, RenderLoadAction::Clear, {});
				},
				[](RenderGraphRasterContext&)
				{
					return true;
				});

			graph.add_raster_pass(
				"SceneSkyBackgroundPass",
				RenderGraphPassFlags::None,
				[&](RenderGraphRasterPassBuilder& pass)
				{
					pass.read_texture(resources.depth, RenderGraphAccess::GraphicsSRV);
					pass.read_texture(resources.scene_hdr_linear, RenderGraphAccess::GraphicsSRV);
					pass.write_color(0, scene_hdr_with_sky, RenderLoadAction::Clear, {});
				},
				[](RenderGraphRasterContext&)
				{
					return true;
				});

			graph.add_raster_pass(
				"SceneDeferredToneMapPass",
				RenderGraphPassFlags::None,
				[&](RenderGraphRasterPassBuilder& pass)
				{
					pass.read_texture(scene_hdr_with_sky, RenderGraphAccess::GraphicsSRV);
					pass.write_color(0, output, RenderLoadAction::Clear, {});
				},
				[](RenderGraphRasterContext&)
				{
					return true;
				});

			RenderGraphCompileResult result{};
			const bool compiled = graph.compile_for_tests(result);
			bool ok = compiled;
			ok = ok && resources.gbuffer_targets.size() == 5u;
			ok = ok && resources.depth;
			ok = ok && resources.ambient_occlusion;
			ok = ok && resources.lighting_diffuse;
			ok = ok && resources.lighting_specular;
			ok = ok && resources.scene_hdr_linear;
			ok = ok && scene_hdr_with_sky;
			ok = ok && result.live_pass_indices.size() == 7u;
			ok = ok && result.live_pass_indices[0] == 0u;
			ok = ok && result.live_pass_indices[1] == 1u;
			ok = ok && result.live_pass_indices[2] == 2u;
			ok = ok && result.live_pass_indices[3] == 3u;
			ok = ok && result.live_pass_indices[4] == 4u;
			ok = ok && result.live_pass_indices[5] == 5u;
			ok = ok && result.live_pass_indices[6] == 6u;
			ok = ok && result.texture_lifetimes[resources.lighting_diffuse.index].first_pass == 2u;
			ok = ok && result.texture_lifetimes[resources.lighting_diffuse.index].last_pass == 4u;
			ok = ok && result.texture_lifetimes[resources.lighting_specular.index].first_pass == 2u;
			ok = ok && result.texture_lifetimes[resources.lighting_specular.index].last_pass == 4u;
			ok = ok && result.texture_lifetimes[resources.scene_hdr_linear.index].first_pass == 4u;
			ok = ok && result.texture_lifetimes[resources.scene_hdr_linear.index].last_pass == 5u;
			ok = ok && result.texture_lifetimes[scene_hdr_with_sky.index].first_pass == 5u;
			ok = ok && result.texture_lifetimes[scene_hdr_with_sky.index].last_pass == 6u;
			return ok || report_self_test_failure("Scene deferred render graph resources", "deferred graph chain was not preserved through compilation");
		}

		auto test_render_pass_attachment_final_state_defaults_to_unknown() -> bool
		{
			PassColorAttachment color{};
			PassDepthAttachment depth{};
			bool ok = color.final_state == RHI::AshResourceState::Unknown;
			ok = ok && depth.final_state == RHI::AshResourceState::Unknown;
			color.final_state = RHI::AshResourceState::SRVGraphics;
			depth.final_state = RHI::AshResourceState::DSVRead | RHI::AshResourceState::SRVGraphics;
			ok = ok && color.final_state == RHI::AshResourceState::SRVGraphics;
			ok = ok && depth.final_state == (RHI::AshResourceState::DSVRead | RHI::AshResourceState::SRVGraphics);
			return ok || report_self_test_failure("RenderGraph attachment final state", "attachment final states are not configurable");
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
			directional_light.casts_shadow = true;
			directional_light.sunlight = true;
			directional_light.shadow_priority = 42u;
			directional_light.shadow_distance = 150.0f;
			directional_light.shadow_cascade_count = 3u;
			directional_light.near_shadow_distance = 18.0f;
			directional.add_light_component(directional_light);

			Entity point = scene.create_entity("PointLight");
			TransformComponent point_transform = point.get_transform_component();
			point_transform.position = { 1.0f, 2.0f, 3.0f };
			point.set_transform_component(point_transform);
			LightComponent point_light{};
			point_light.type = LightType::Point;
			point_light.range = 7.0f;
			point_light.intensity = 3.0f;
			point_light.sunlight = true;
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
				glm::length(frame.lights[0].direction_ws) > 0.9f &&
				frame.lights[0].casts_shadow &&
				frame.lights[0].sunlight &&
				frame.lights[0].shadow_priority == 42u &&
				frame.lights[0].shadow_distance == 150.0f &&
				frame.lights[0].shadow_cascade_count == 3u &&
				frame.lights[0].near_shadow_distance == 18.0f;
			const bool point_ok =
				frame.lights[1].type == LightType::Point &&
				frame.lights[1].position_ws == glm::vec3(1.0f, 2.0f, 3.0f) &&
				!frame.lights[1].sunlight &&
				frame.lights[1].range == 7.0f;
			return (directional_ok && point_ok) ||
				report_self_test_failure("RenderScene light snapshot", "light data was not extracted with stable transform data");
		}

		auto test_scene_light_sunlight_json_round_trip() -> bool
		{
			const std::filesystem::path test_dir = engine_self_test_dir() / "scene_sunlight";
			std::filesystem::create_directories(test_dir);
			const std::filesystem::path scene_path = test_dir / "sunlight.scene.json";

			{
				std::ofstream file(scene_path, std::ios::trunc);
				file <<
					"{\n"
					"  \"version\": 4,\n"
					"  \"name\": \"SunlightRoundTrip\",\n"
					"  \"next_entity_id\": 3,\n"
					"  \"entities\": [\n"
					"    {\n"
					"      \"id\": 1,\n"
					"      \"name\": \"Sun\",\n"
					"      \"transform\": {},\n"
					"      \"light\": {\n"
					"        \"type\": 0,\n"
					"        \"sunlight\": true,\n"
					"        \"casts_shadow\": true\n"
					"      }\n"
					"    },\n"
					"    {\n"
					"      \"id\": 2,\n"
					"      \"name\": \"Point\",\n"
					"      \"transform\": {},\n"
					"      \"light\": {\n"
					"        \"type\": 1,\n"
					"        \"sunlight\": true,\n"
					"        \"range\": 8.0\n"
					"      }\n"
					"    }\n"
					"  ]\n"
					"}\n";
			}

			std::string error{};
			Scene scene = Scene::load_from_file(scene_path, &error);
			if (!scene.is_valid())
			{
				return report_self_test_failure("Scene sunlight JSON", error.empty() ? "sunlight scene failed to load" : error.c_str());
			}

			Entity sun = scene.find_entity(1u);
			Entity point = scene.find_entity(2u);
			if (!sun.is_valid() || !point.is_valid() || !sun.has_light_component() || !point.has_light_component())
			{
				return report_self_test_failure("Scene sunlight JSON", "sunlight test entities were not loaded");
			}

			const LightComponent sun_light = sun.get_light_component();
			const LightComponent point_light = point.get_light_component();
			if (!sun_light.sunlight || point_light.sunlight)
			{
				return report_self_test_failure("Scene sunlight JSON", "directional sunlight was not preserved or point sunlight was not sanitized");
			}

			const std::filesystem::path saved_path = test_dir / "sunlight.saved.scene.json";
			if (!scene.save_to_file(saved_path, &error))
			{
				return report_self_test_failure("Scene sunlight JSON", error.empty() ? "sunlight scene failed to save" : error.c_str());
			}

			Scene round_trip = Scene::load_from_file(saved_path, &error);
			if (!round_trip.is_valid())
			{
				return report_self_test_failure("Scene sunlight JSON", error.empty() ? "saved sunlight scene failed to load" : error.c_str());
			}

			const bool ok =
				round_trip.find_entity(1u).get_light_component().sunlight &&
				!round_trip.find_entity(2u).get_light_component().sunlight;
			return ok || report_self_test_failure("Scene sunlight JSON", "sunlight did not survive save/load round trip");
		}

		auto test_scene_rejects_multiple_directional_sunlights() -> bool
		{
			const std::filesystem::path test_dir = engine_self_test_dir() / "scene_sunlight";
			std::filesystem::create_directories(test_dir);
			const std::filesystem::path scene_path = test_dir / "two_suns.scene.json";

			{
				std::ofstream file(scene_path, std::ios::trunc);
				file <<
					"{\n"
					"  \"version\": 4,\n"
					"  \"name\": \"TwoSuns\",\n"
					"  \"next_entity_id\": 3,\n"
					"  \"entities\": [\n"
					"    { \"id\": 1, \"name\": \"SunA\", \"transform\": {}, \"light\": { \"type\": 0, \"sunlight\": true } },\n"
					"    { \"id\": 2, \"name\": \"SunB\", \"transform\": {}, \"light\": { \"type\": 0, \"sunlight\": true } }\n"
					"  ]\n"
					"}\n";
			}

			std::string error{};
			Scene scene = Scene::load_from_file(scene_path, &error);
			const bool ok =
				!scene.is_valid() &&
				error.find("sunlight") != std::string::npos;
			return ok || report_self_test_failure("Scene sunlight validation", "scene accepted two directional sunlight components");
		}

		auto test_scene_environment_metadata_creates_sunlight() -> bool
		{
			const std::filesystem::path test_dir = engine_self_test_dir() / "scene_sunlight";
			std::filesystem::create_directories(test_dir);
			const std::filesystem::path ibl_path = test_dir / "metadata_sun.ashibl";
			const std::filesystem::path scene_path = test_dir / "environment_sun.scene.json";

			EnvironmentMapCookedData cooked{};
			fill_environment_map_test_pattern(cooked);
			cooked.dominant_light.valid = true;
			cooked.dominant_light.direction = glm::normalize(glm::vec3(0.25f, 0.9f, 0.15f));
			cooked.dominant_light.luminance = 8.0f;
			cooked.dominant_light.source = "self-test";

			std::string error{};
			if (!write_ashibl_file(ibl_path, cooked, &error))
			{
				return report_self_test_failure("Scene environment sunlight", error.empty() ? "failed to write test ashibl" : error.c_str());
			}

			{
				std::ofstream file(scene_path, std::ios::trunc);
				file <<
					"{\n"
					"  \"version\": 4,\n"
					"  \"name\": \"EnvironmentSun\",\n"
					"  \"next_entity_id\": 2,\n"
					"  \"entities\": [\n"
					"    {\n"
					"      \"id\": 1,\n"
					"      \"name\": \"Environment\",\n"
					"      \"transform\": {},\n"
					"      \"environment\": {\n"
					"        \"active\": true,\n"
					"        \"ibl_asset_path\": \"" << ibl_path.generic_string() << "\"\n"
					"      }\n"
					"    }\n"
					"  ]\n"
					"}\n";
			}

			Scene scene = Scene::load_from_file(scene_path, &error);
			if (!scene.is_valid())
			{
				return report_self_test_failure("Scene environment sunlight", error.empty() ? "environment scene failed to load" : error.c_str());
			}

			Entity environment_sun{};
			for (const Entity& entity : scene.get_entities())
			{
				if (entity.is_valid() && entity.get_name() == "EnvironmentSunLight")
				{
					environment_sun = entity;
					break;
				}
			}

			const bool ok =
				environment_sun.is_valid() &&
				environment_sun.has_light_component() &&
				environment_sun.get_light_component().type == LightType::Directional &&
				environment_sun.get_light_component().sunlight &&
				environment_sun.get_light_component().casts_shadow;
			return ok || report_self_test_failure("Scene environment sunlight", "environment metadata did not create a shadow-casting sunlight");
		}

		auto test_scene_environment_metadata_preserves_existing_sunlight() -> bool
		{
			const std::filesystem::path test_dir = engine_self_test_dir() / "scene_sunlight";
			std::filesystem::create_directories(test_dir);
			const std::filesystem::path ibl_path = test_dir / "preserve_sun.ashibl";
			const std::filesystem::path scene_path = test_dir / "preserve_sun.scene.json";

			EnvironmentMapCookedData cooked{};
			fill_environment_map_test_pattern(cooked);
			cooked.dominant_light.valid = true;
			cooked.dominant_light.direction = glm::normalize(glm::vec3(0.25f, 0.9f, 0.15f));
			cooked.dominant_light.luminance = 8.0f;
			cooked.dominant_light.source = "self-test";

			std::string error{};
			if (!write_ashibl_file(ibl_path, cooked, &error))
			{
				return report_self_test_failure(
					"Scene environment sunlight preserve",
					error.empty() ? "failed to write test ashibl" : error.c_str());
			}

			const glm::vec3 saved_rotation{ 12.0f, -34.0f, 56.0f };
			{
				std::ofstream file(scene_path, std::ios::trunc);
				file <<
					"{\n"
					"  \"version\": 4,\n"
					"  \"name\": \"PreserveSun\",\n"
					"  \"next_entity_id\": 3,\n"
					"  \"entities\": [\n"
					"    {\n"
					"      \"id\": 1,\n"
					"      \"name\": \"Environment\",\n"
					"      \"transform\": {},\n"
					"      \"environment\": {\n"
					"        \"active\": true,\n"
					"        \"ibl_asset_path\": \"" << ibl_path.generic_string() << "\"\n"
					"      }\n"
					"    },\n"
					"    {\n"
					"      \"id\": 2,\n"
					"      \"name\": \"SavedSunLight\",\n"
					"      \"transform\": {\n"
					"        \"rotation_euler_degrees\": [" << saved_rotation.x << ", " << saved_rotation.y << ", " << saved_rotation.z << "]\n"
					"      },\n"
					"      \"light\": {\n"
					"        \"type\": 0,\n"
					"        \"sunlight\": true,\n"
					"        \"casts_shadow\": true\n"
					"      }\n"
					"    }\n"
					"  ]\n"
					"}\n";
			}

			Scene scene = Scene::load_from_file(scene_path, &error);
			if (!scene.is_valid())
			{
				return report_self_test_failure(
					"Scene environment sunlight preserve",
					error.empty() ? "preserve sunlight scene failed to load" : error.c_str());
			}

			Entity saved_sun = scene.find_entity(2u);
			if (!saved_sun.is_valid() || !saved_sun.has_light_component())
			{
				return report_self_test_failure("Scene environment sunlight preserve", "saved sunlight entity was missing after load");
			}

			const TransformComponent transform = saved_sun.get_transform_component();
			const bool ok =
				saved_sun.get_light_component().sunlight &&
				std::abs(transform.rotation_euler_degrees.x - saved_rotation.x) <= 0.001f &&
				std::abs(transform.rotation_euler_degrees.y - saved_rotation.y) <= 0.001f &&
				std::abs(transform.rotation_euler_degrees.z - saved_rotation.z) <= 0.001f;
			return ok ||
				report_self_test_failure(
					"Scene environment sunlight preserve",
					"existing sunlight transform was overwritten from environment metadata");
		}

		auto test_render_scene_copies_scene_render_config_to_visible_frame() -> bool
		{
			Scene scene = Scene::create("RenderConfigSnapshotSelfTest");
			SceneRenderConfig config = make_default_scene_render_config();
			config.ambient_occlusion.mode = AmbientOcclusionMode::HBAO;
			config.ambient_occlusion.quality = AmbientOcclusionQuality::High;
			config.directional_shadows.enabled = false;
			config.bloom.enabled = true;
			config.bloom.quality = BloomQuality::Epic;
			config.bloom.intensity = 1.25f;
			config.bloom.threshold = 0.75f;
			config.bloom.debug_view = BloomDebugView::Final;
			config.volumetric_lighting.enabled = true;
			config.volumetric_lighting.quality = VolumetricLightingQuality::High;
			config.volumetric_lighting.froxel_resolution_scale = 0.75f;
			config.volumetric_lighting.froxel_depth_slices = 96u;
			config.volumetric_lighting.max_lights = 48u;
			config.volumetric_lighting.debug_view = VolumetricLightingDebugView::CompositeHDR;
			config.tonemap.exposure = 1.25f;
			if (!scene.set_render_config(config))
			{
				return report_self_test_failure("RenderScene render config snapshot", "failed to set scene render config");
			}

			RenderScene render_scene{};
			if (!render_scene.rebuild_render_config_from_scene(scene))
			{
				return report_self_test_failure("RenderScene render config snapshot", "failed to rebuild render config from scene");
			}

			VisibleRenderFrame light_frame{};
			if (!render_scene.build_visible_light_frame(light_frame))
			{
				return report_self_test_failure("RenderScene render config snapshot", "failed to build light-only visible frame");
			}

			SceneViewDesc view_desc{};
			view_desc.viewport_width = 128;
			view_desc.viewport_height = 64;
			SceneView view{};
			if (!build_scene_view_from_matrices(
				view_desc,
				glm::mat4(1.0f),
				glm::mat4(1.0f),
				glm::vec3(0.0f),
				view))
			{
				return report_self_test_failure("RenderScene render config snapshot", "failed to build test scene view");
			}

			VisibleRenderFrame full_frame{};
			if (!render_scene.build_visible_render_frame(7u, view, full_frame))
			{
				return report_self_test_failure("RenderScene render config snapshot", "failed to build full visible frame");
			}

			const bool ok =
				scene_render_config_equal(light_frame.render_config, config) &&
				scene_render_config_equal(full_frame.render_config, config) &&
				light_frame.render_config.bloom.enabled &&
				light_frame.render_config.bloom.quality == BloomQuality::Epic &&
				light_frame.render_config.bloom.intensity == 1.25f &&
				full_frame.render_config.bloom.enabled &&
				full_frame.render_config.bloom.debug_view == BloomDebugView::Final &&
				full_frame.render_config.volumetric_lighting.enabled &&
				full_frame.render_config.volumetric_lighting.quality == VolumetricLightingQuality::High &&
				full_frame.render_config.volumetric_lighting.froxel_resolution_scale == 0.75f &&
				full_frame.render_config.volumetric_lighting.debug_view == VolumetricLightingDebugView::CompositeHDR &&
				full_frame.render_config.tonemap.exposure == 1.25f;
			return ok ||
				report_self_test_failure("RenderScene render config snapshot", "VisibleRenderFrame did not carry the scene render config");
		}

		auto test_scene_extracts_single_active_environment() -> bool
		{
			Scene scene = Scene::create("Environment Test");

			Entity inactive = scene.create_entity("Inactive Environment");
			EnvironmentComponent inactive_env{};
			inactive_env.active = false;
			inactive_env.ibl_asset_path = "assets/env/inactive.ashibl";
			inactive.add_environment_component(inactive_env);

			Entity first = scene.create_entity("First Environment");
			EnvironmentComponent first_env{};
			first_env.active = true;
			first_env.ibl_asset_path = "assets/env/first.ashibl";
			first_env.source_texture_path = "assets/env/first.hdr";
			first_env.intensity = 2.0f;
			first_env.lighting_intensity = 0.25f;
			first_env.background_intensity = 1.5f;
			first_env.rotation_degrees = 45.0f;
			first_env.visible_background = true;
			first_env.affect_lighting = true;
			first.add_environment_component(first_env);

			Entity second = scene.create_entity("Second Environment");
			EnvironmentComponent second_env{};
			second_env.active = true;
			second_env.ibl_asset_path = "assets/env/second.ashibl";
			second.add_environment_component(second_env);

			SceneEnvironmentExtractionDesc extracted{};
			if (!scene.extract_active_environment(extracted))
			{
				return report_self_test_failure("Scene environment extraction", "active environment was not extracted");
			}

			const bool ok =
				extracted.entity_id == first.get_id() &&
				extracted.ibl_asset_path == "assets/env/first.ashibl" &&
				extracted.source_texture_path == "assets/env/first.hdr" &&
				extracted.intensity == 2.0f &&
				extracted.lighting_intensity == 0.25f &&
				extracted.background_intensity == 1.5f &&
				extracted.rotation_degrees == 45.0f &&
				extracted.visible_background &&
				extracted.affect_lighting;
			return ok || report_self_test_failure("Scene environment extraction", "first active environment was not selected deterministically");
		}

		auto test_scene_render_versions_separate_transform_from_primitive_changes() -> bool
		{
			Scene scene = Scene::create("SceneRenderVersionSelfTest");
			const uint64_t initial_primitive_version = scene.get_render_primitive_version();
			const uint64_t initial_transform_version = scene.get_render_transform_version();
			const uint64_t initial_light_version = scene.get_render_light_version();

			Entity entity = scene.create_entity("Mesh");
			MeshComponent mesh{};
			mesh.asset_path = "test.gltf";
			entity.add_mesh_component(mesh);

			const uint64_t mesh_primitive_version = scene.get_render_primitive_version();
			const uint64_t mesh_transform_version = scene.get_render_transform_version();
			TransformComponent transform = entity.get_transform_component();
			transform.position = { 10.0f, 0.0f, 0.0f };
			entity.set_transform_component(transform);

			const uint64_t before_light_primitive_version = scene.get_render_primitive_version();
			LightComponent light{};
			entity.add_light_component(light);

			const bool ok =
				mesh_primitive_version != initial_primitive_version &&
				mesh_transform_version != initial_transform_version &&
				scene.get_render_light_version() != initial_light_version &&
				scene.get_render_primitive_version() == mesh_primitive_version &&
				scene.get_render_primitive_version() == before_light_primitive_version &&
				scene.get_render_transform_version() != mesh_transform_version;
			return ok ||
				report_self_test_failure("Scene render versions", "transform-only changes invalidated primitive rebuild state");
		}

		auto test_scene_environment_version_isolated_from_primitives() -> bool
		{
			Scene scene = Scene::create("Environment Version Test");
			Entity environment = scene.create_entity("Environment");

			const uint64_t primitive_before = scene.get_render_primitive_version();
			const uint64_t transform_before = scene.get_render_transform_version();
			const uint64_t light_before = scene.get_render_light_version();
			const uint64_t environment_before = scene.get_render_environment_version();

			EnvironmentComponent component{};
			component.active = true;
			component.ibl_asset_path = "assets/env/test.ashibl";
			environment.add_environment_component(component);

			const bool ok =
				scene.get_render_primitive_version() == primitive_before &&
				scene.get_render_transform_version() == transform_before &&
				scene.get_render_light_version() == light_before &&
				scene.get_render_environment_version() != environment_before;
			return ok || report_self_test_failure("Scene environment versions", "environment changes invalidated unrelated render versions");
		}

		auto test_scene_render_config_version_isolated_from_other_render_versions() -> bool
		{
			Scene scene = Scene::create("SceneRenderConfigVersionSelfTest");
			const uint64_t primitive_before = scene.get_render_primitive_version();
			const uint64_t transform_before = scene.get_render_transform_version();
			const uint64_t light_before = scene.get_render_light_version();
			const uint64_t environment_before = scene.get_render_environment_version();
			const uint64_t render_config_before = scene.get_render_config_version();

			SceneRenderConfig config = scene.get_render_config();
			config.ambient_occlusion.mode = AmbientOcclusionMode::HBAO;
			config.ambient_occlusion.quality = AmbientOcclusionQuality::High;

			if (!scene.set_render_config(config))
			{
				return report_self_test_failure("Scene render config versions", "set_render_config returned false for a valid scene");
			}

			const bool ok =
				scene.get_render_primitive_version() == primitive_before &&
				scene.get_render_transform_version() == transform_before &&
				scene.get_render_light_version() == light_before &&
				scene.get_render_environment_version() == environment_before &&
				scene.get_render_config_version() != render_config_before;
			return ok ||
				report_self_test_failure("Scene render config versions", "render config changes invalidated unrelated render versions");
		}

		auto test_scene_render_config_json_defaults_and_round_trip() -> bool
		{
			const std::filesystem::path test_dir = engine_self_test_dir() / "scene_render_config";
			std::filesystem::create_directories(test_dir);

			const std::filesystem::path old_scene_path = test_dir / "old_scene.scene.json";
			{
				std::ofstream file(old_scene_path, std::ios::trunc);
				file <<
					"{\n"
					"  \"version\": 3,\n"
					"  \"name\": \"OldScene\",\n"
					"  \"next_entity_id\": 1,\n"
					"  \"entities\": []\n"
					"}\n";
			}

			std::string error{};
			Scene old_scene = Scene::load_from_file(old_scene_path, &error);
			const SceneRenderConfig defaults = make_default_scene_render_config();
			if (!old_scene.is_valid() || !scene_render_config_equal(old_scene.get_render_config(), defaults))
			{
				return report_self_test_failure("Scene render config JSON", "version 3 scene without scene_config did not load defaults");
			}

			const std::filesystem::path scene_path = test_dir / "configured_scene.scene.json";
			{
				std::ofstream file(scene_path, std::ios::trunc);
				file <<
					"{\n"
					"  \"version\": 4,\n"
					"  \"name\": \"ConfiguredScene\",\n"
					"  \"next_entity_id\": 1,\n"
					"  \"scene_config\": {\n"
					"    \"ambient_occlusion\": {\n"
					"      \"mode\": \"HBAO\",\n"
					"      \"quality\": \"High\",\n"
					"      \"radius\": 99.0,\n"
					"      \"intensity\": 2.5,\n"
					"      \"power\": 2.0,\n"
					"      \"half_resolution\": true,\n"
					"      \"blur\": false,\n"
					"      \"temporal\": true,\n"
					"      \"temporal_blend\": 0.9,\n"
					"      \"temporal_depth_threshold\": 0.02,\n"
					"      \"temporal_normal_threshold\": 0.5\n"
					"    },\n"
					"    \"directional_shadows\": {\n"
					"      \"enabled\": true,\n"
					"      \"default_cascade_count\": 7,\n"
					"      \"default_shadow_distance\": 240.0,\n"
					"      \"near_shadow_distance\": 24.0,\n"
					"      \"split_lambda\": 0.8,\n"
					"      \"near_cascade_resolution\": 3000,\n"
					"      \"outer_cascade_resolution\": 300,\n"
					"      \"dynamic_atlas_size\": 3000,\n"
					"      \"static_cache_atlas_size\": 3000,\n"
					"      \"static_cache_budget_mb\": 96,\n"
					"      \"depth_bias\": 0.002,\n"
					"      \"normal_bias\": 0.06,\n"
					"      \"pcf_radius\": 2\n"
					"    },\n"
					"    \"bloom\": {\n"
					"      \"enabled\": true,\n"
					"      \"quality\": \"Epic\",\n"
					"      \"intensity\": 1.5,\n"
					"      \"threshold\": -4.0,\n"
					"      \"soft_knee\": 2.0,\n"
					"      \"size_scale\": 10.0,\n"
					"      \"debug_view\": \"CompositeHDR\",\n"
					"      \"stages\": [\n"
					"        { \"size\": 0.5, \"tint\": [1.0, 1.0, 1.0] },\n"
					"        { \"size\": 1.5, \"tint\": [1.0, 0.8, 0.7] },\n"
					"        { \"size\": 2.5, \"tint\": [0.8, 0.9, 1.0] },\n"
					"        { \"size\": 4.5, \"tint\": [0.7, 0.8, 1.0] },\n"
					"        { \"size\": 8.5, \"tint\": [0.6, 0.7, 1.0] },\n"
					"        { \"size\": 20.0, \"tint\": [-1.0, 9.0, 0.25] }\n"
					"      ]\n"
					"    },\n"
					"    \"volumetric_lighting\": {\n"
					"      \"enabled\": true,\n"
					"      \"quality\": \"Epic\",\n"
					"      \"froxel_resolution_scale\": 2.0,\n"
					"      \"froxel_depth_slices\": 4096,\n"
					"      \"max_lights\": 2048,\n"
					"      \"density\": -4.0,\n"
					"      \"scattering_intensity\": 32.0,\n"
					"      \"extinction_scale\": 20.0,\n"
					"      \"anisotropy\": 4.0,\n"
					"      \"history\": false,\n"
					"      \"history_blend\": 1.0,\n"
					"      \"screen_space_fallback\": true,\n"
					"      \"debug_view\": \"ScreenSpaceFinal\"\n"
					"    },\n"
					"    \"tonemap\": {\n"
					"      \"exposure\": 100.0\n"
					"    }\n"
					"  },\n"
					"  \"entities\": []\n"
					"}\n";
			}

			Scene configured_scene = Scene::load_from_file(scene_path, &error);
			if (!configured_scene.is_valid())
			{
				return report_self_test_failure("Scene render config JSON", error.empty() ? "configured scene did not load" : error.c_str());
			}

			const SceneRenderConfig loaded = configured_scene.get_render_config();
			const bool parsed_ok =
				loaded.ambient_occlusion.mode == AmbientOcclusionMode::HBAO &&
				loaded.ambient_occlusion.quality == AmbientOcclusionQuality::High &&
				loaded.ambient_occlusion.radius == 20.0f &&
				loaded.ambient_occlusion.half_resolution &&
				!loaded.ambient_occlusion.blur &&
				loaded.ambient_occlusion.temporal &&
				loaded.ambient_occlusion.temporal_blend == 0.9f &&
				loaded.ambient_occlusion.temporal_depth_threshold == 0.02f &&
				loaded.ambient_occlusion.temporal_normal_threshold == 0.5f &&
				loaded.directional_shadows.default_cascade_count == 4u &&
				loaded.directional_shadows.near_cascade_resolution == 4096u &&
				loaded.directional_shadows.outer_cascade_resolution == 512u &&
				loaded.directional_shadows.dynamic_atlas_size == 4096u &&
				loaded.directional_shadows.static_cache_atlas_size == 4096u &&
				loaded.bloom.enabled &&
				loaded.bloom.quality == BloomQuality::Epic &&
				loaded.bloom.intensity == 1.5f &&
				loaded.bloom.threshold == -1.0f &&
				loaded.bloom.soft_knee == 1.0f &&
				loaded.bloom.size_scale == 8.0f &&
				loaded.bloom.debug_view == BloomDebugView::CompositeHDR &&
				loaded.bloom.stages[5].size == 16.0f &&
				loaded.bloom.stages[5].tint.x == 0.0f &&
				loaded.bloom.stages[5].tint.y == 8.0f &&
				loaded.bloom.stages[5].tint.z == 0.25f &&
				loaded.volumetric_lighting.enabled &&
				loaded.volumetric_lighting.quality == VolumetricLightingQuality::Epic &&
				loaded.volumetric_lighting.froxel_resolution_scale == 1.0f &&
				loaded.volumetric_lighting.froxel_depth_slices == 128u &&
				loaded.volumetric_lighting.max_lights == 256u &&
				loaded.volumetric_lighting.density == 0.0f &&
				loaded.volumetric_lighting.scattering_intensity == 16.0f &&
				loaded.volumetric_lighting.extinction_scale == 16.0f &&
				loaded.volumetric_lighting.anisotropy == 0.95f &&
				!loaded.volumetric_lighting.history &&
				loaded.volumetric_lighting.history_blend == 0.98f &&
				loaded.volumetric_lighting.screen_space_fallback &&
				loaded.volumetric_lighting.debug_view == VolumetricLightingDebugView::ScreenSpaceFinal &&
				loaded.tonemap.exposure == 64.0f;
			if (!parsed_ok)
			{
				return report_self_test_failure("Scene render config JSON", "scene_config fields were not parsed and sanitized as expected");
			}

			const std::filesystem::path saved_path = test_dir / "saved_scene.scene.json";
			if (!configured_scene.save_to_file(saved_path, &error))
			{
				return report_self_test_failure("Scene render config JSON", error.empty() ? "failed to save configured scene" : error.c_str());
			}

			std::ifstream saved_file(saved_path);
			const std::string saved_source{
				std::istreambuf_iterator<char>(saved_file),
				std::istreambuf_iterator<char>() };
			if (saved_source.find("render_debug_view") != std::string::npos)
			{
				return report_self_test_failure("Scene render config JSON", "scene_config must not serialize process-level RenderDebugView settings");
			}

			Scene round_trip_scene = Scene::load_from_file(saved_path, &error);
			if (!round_trip_scene.is_valid() || !scene_render_config_equal(round_trip_scene.get_render_config(), loaded))
			{
				return report_self_test_failure("Scene render config JSON", "scene_config did not survive save/load round trip");
			}

			return true;
		}

		auto test_sandbox_scene_enables_volumetric_lighting() -> bool
		{
			std::ifstream scene_file("product/assets/scenes/Sandbox.scene.json");
			if (!scene_file.is_open())
			{
				return report_self_test_failure("Sandbox volumetric scene config", "failed to open Sandbox.scene.json");
			}
			const std::string source{
				std::istreambuf_iterator<char>(scene_file),
				std::istreambuf_iterator<char>() };
			const bool ok =
				source.find("\"volumetric_lighting\"") != std::string::npos &&
				source.find("\"enabled\": true") != std::string::npos &&
				source.find("\"debug_view\": \"Off\"") != std::string::npos &&
				source.find("\"debug_view\": \"true\"") == std::string::npos &&
				source.find("\"density\": 0.0300000037252903") != std::string::npos &&
				source.find("\"scattering_intensity\": 2.0") != std::string::npos &&
				source.find("\"froxel_resolution_scale\": 0.25") != std::string::npos;
			return ok ||
				report_self_test_failure("Sandbox volumetric scene config", "standard Sandbox scene does not expose artist-visible volumetric lighting settings");
		}

		auto test_graphics_draw_desc_keeps_common_vertex_bindings_inline() -> bool
		{
			GraphicsDrawDesc desc{};
			std::weak_ptr<VertexBuffer> released_buffer{};
			{
				std::shared_ptr<VertexBuffer> buffer = std::make_shared<VertexBuffer>();
				released_buffer = buffer;
				desc.vertex_buffers.push_back({ 0u, buffer, 0u });
			}
			desc.vertex_buffers.push_back({ 1u, nullptr, 16u });
			const bool inline_ok =
				desc.vertex_buffers.size() == 2u &&
				desc.vertex_buffers[0].slot == 0u &&
				desc.vertex_buffers[1].offset == 16u &&
				desc.vertex_buffers.uses_inline_storage_for_tests();
			desc.vertex_buffers.clear();
			const bool ok = inline_ok && released_buffer.expired();
			return ok ||
				report_self_test_failure("GraphicsDrawDesc inline vertex bindings", "common vertex bindings spilled to heap storage");
		}

		auto test_debug_draw_service_records_and_clears_frame_lines() -> bool
		{
			DebugDrawService debug_draw{};
			debug_draw.draw_line(
				glm::vec3(0.0f, 0.0f, 0.0f),
				glm::vec3(1.0f, 0.0f, 0.0f),
				glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),
				2.0f);

			std::vector<DebugDrawLine> lines{};
			debug_draw.snapshot_lines(lines);
			if (!lines.empty())
			{
				return report_self_test_failure("DebugDrawService frame lines", "lines were visible to snapshot before commit_frame");
			}

			debug_draw.commit_frame();
			debug_draw.snapshot_lines(lines);
			if (lines.size() != 1u ||
				lines.front().start != glm::vec3(0.0f, 0.0f, 0.0f) ||
				lines.front().end != glm::vec3(1.0f, 0.0f, 0.0f) ||
				lines.front().color != glm::vec4(1.0f, 0.0f, 0.0f, 1.0f) ||
				lines.front().thickness != 2.0f)
			{
				return report_self_test_failure("DebugDrawService frame lines", "draw_line did not preserve submitted line data");
			}

			debug_draw.clear_frame();
			lines.clear();
			debug_draw.snapshot_lines(lines);
			return lines.empty() ||
				report_self_test_failure("DebugDrawService frame lines", "clear_frame did not remove frame-local lines");
		}

		auto test_debug_draw_service_expands_shapes_to_line_list() -> bool
		{
			DebugDrawService debug_draw{};
			debug_draw.draw_box(
				glm::vec3(-1.0f, -2.0f, -3.0f),
				glm::vec3(1.0f, 2.0f, 3.0f),
				glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
			debug_draw.draw_circle(
				glm::vec3(0.0f),
				glm::vec3(0.0f, 1.0f, 0.0f),
				2.0f,
				glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
				8);
			debug_draw.draw_cone(
				glm::vec3(0.0f),
				glm::vec3(0.0f, 0.0f, 1.0f),
				3.0f,
				30.0f,
				glm::vec4(1.0f, 1.0f, 0.0f, 1.0f),
				8);
			debug_draw.commit_frame();

			std::vector<DebugDrawLine> lines{};
			debug_draw.snapshot_lines(lines);
			constexpr size_t expected_box_lines = 12u;
			constexpr size_t expected_circle_lines = 8u;
			constexpr size_t expected_cone_lines = 16u;
			if (lines.size() != expected_box_lines + expected_circle_lines + expected_cone_lines)
			{
				return report_self_test_failure("DebugDrawService shape expansion", "shape helpers did not expand to the expected line count");
			}

			const bool box_has_first_edge =
				lines.front().start == glm::vec3(-1.0f, -2.0f, -3.0f) &&
				lines.front().end == glm::vec3(1.0f, -2.0f, -3.0f);
			return box_has_first_edge ||
				report_self_test_failure("DebugDrawService shape expansion", "box edges were not emitted in stable min/max order");
		}

		auto write_scene_query_bounds_model(const std::filesystem::path& root_dir) -> std::filesystem::path
		{
			const std::filesystem::path bin_path = root_dir / "scene_query_bounds_box.bin";
			const std::filesystem::path gltf_path = root_dir / "scene_query_bounds_box.gltf";

			std::vector<uint8_t> bytes{};
			const float positions[] = {
				-1.0f, -1.0f, -1.0f,
				 1.0f, -1.0f, -1.0f,
				 1.0f,  1.0f, -1.0f,
				-1.0f,  1.0f, -1.0f,
				-1.0f, -1.0f,  1.0f,
				 1.0f, -1.0f,  1.0f,
				 1.0f,  1.0f,  1.0f,
				-1.0f,  1.0f,  1.0f
			};
			for (float value : positions)
			{
				append_binary_value(bytes, value);
			}
			const uint16_t indices[] = {
				0, 1, 2, 0, 2, 3,
				4, 6, 5, 4, 7, 6,
				0, 4, 5, 0, 5, 1,
				1, 5, 6, 1, 6, 2,
				2, 6, 7, 2, 7, 3,
				3, 7, 4, 3, 4, 0
			};
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
					"  \"buffers\": [{ \"uri\": \"scene_query_bounds_box.bin\", \"byteLength\": 168 }],\n"
					"  \"bufferViews\": [\n"
					"    { \"buffer\": 0, \"byteOffset\": 0, \"byteLength\": 96, \"target\": 34962 },\n"
					"    { \"buffer\": 0, \"byteOffset\": 96, \"byteLength\": 72, \"target\": 34963 }\n"
					"  ],\n"
					"  \"accessors\": [\n"
					"    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 8, \"type\": \"VEC3\", \"min\": [-1, -1, -1], \"max\": [1, 1, 1] },\n"
					"    { \"bufferView\": 1, \"componentType\": 5123, \"count\": 36, \"type\": \"SCALAR\" }\n"
					"  ],\n"
					"  \"meshes\": [{ \"primitives\": [{ \"attributes\": { \"POSITION\": 0 }, \"indices\": 1, \"mode\": 4 }] }],\n"
					"  \"nodes\": [{ \"mesh\": 0 }],\n"
					"  \"scenes\": [{ \"nodes\": [0] }],\n"
					"  \"scene\": 0\n"
					"}\n";
			}

			return gltf_path;
		}

		auto test_scene_view_builds_from_override_matrices() -> bool
		{
			SceneView view{};
			SceneViewDesc desc{};
			desc.viewport_width = 128;
			desc.viewport_height = 64;

			const glm::mat4 view_matrix = glm::lookAtLH(
				glm::vec3(0.0f, 0.0f, -5.0f),
				glm::vec3(0.0f, 0.0f, 0.0f),
				glm::vec3(0.0f, 1.0f, 0.0f));
			const glm::mat4 projection_matrix = glm::perspectiveLH_ZO(glm::radians(60.0f), 2.0f, 0.1f, 100.0f);

			if (!build_scene_view_from_matrices(desc, view_matrix, projection_matrix, glm::vec3(0.0f, 0.0f, -5.0f), view))
			{
				return report_self_test_failure("SceneView matrix override", "failed to build a view from explicit matrices");
			}

			const bool ok =
				view.is_valid &&
				view.camera_entity_id == 0 &&
				view.desc.viewport_width == 128 &&
				view.view == view_matrix &&
				view.projection == projection_matrix &&
				view.view_projection == projection_matrix * view_matrix &&
				view.camera_position == glm::vec3(0.0f, 0.0f, -5.0f);
			return ok ||
				report_self_test_failure("SceneView matrix override", "explicit matrices were not copied into SceneView");
		}

		auto test_scene_query_bounds_ray_and_picking() -> bool
		{
			const std::filesystem::path test_root = engine_self_test_dir() / "scene_query_assets";
			std::filesystem::create_directories(test_root);
			const std::filesystem::path model_path = write_scene_query_bounds_model(test_root);
			AssetDatabase database = AssetDatabase::create(test_root);
			if (!database.is_valid() || !database.refresh())
			{
				return report_self_test_failure("SceneQuery bounds and picking", "failed to create test asset database");
			}

			Scene scene = Scene::create("SceneQuerySelfTest");
			Entity entity = scene.create_entity("Box");
			TransformComponent transform = entity.get_transform_component();
			transform.position = { 3.0f, 0.0f, 5.0f };
			transform.scale = { 2.0f, 1.0f, 1.0f };
			entity.set_transform_component(transform);
			MeshComponent mesh{};
			mesh.asset_path = model_path.filename().generic_string();
			mesh.mesh_index = 0;
			entity.add_mesh_component(mesh);

			SceneWorldBounds bounds{};
			if (!get_entity_world_bounds(scene, database, entity.get_id(), bounds))
			{
				return report_self_test_failure("SceneQuery bounds and picking", "failed to query entity world bounds");
			}

			const bool bounds_ok =
				bounds.is_valid &&
				bounds.min == glm::vec3(1.0f, -1.0f, 4.0f) &&
				bounds.max == glm::vec3(5.0f, 1.0f, 6.0f) &&
				bounds.center == glm::vec3(3.0f, 0.0f, 5.0f);
			if (!bounds_ok)
			{
				return report_self_test_failure("SceneQuery bounds and picking", "world bounds did not include transform scale and translation");
			}

			const SceneRay center_ray = screen_to_world_ray(
				50.0f,
				50.0f,
				100.0f,
				100.0f,
				glm::mat4(1.0f),
				glm::orthoLH_ZO(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 10.0f));
			if (glm::length(center_ray.direction - glm::vec3(0.0f, 0.0f, 1.0f)) > 0.0001f)
			{
				return report_self_test_failure("SceneQuery bounds and picking", "screen center ray did not point along +Z");
			}

			SceneRay ray{};
			ray.origin = { 3.0f, 0.0f, 0.0f };
			ray.direction = { 0.0f, 0.0f, 1.0f };
			const std::vector<SceneRayHit> hits = ray_cast_scene(scene, database, ray);
			const bool hit_ok =
				hits.size() == 1u &&
				hits.front().entity_id == entity.get_id() &&
				hits.front().distance >= 3.99f &&
				hits.front().distance <= 4.01f;
			return hit_ok ||
				report_self_test_failure("SceneQuery bounds and picking", "ray cast did not hit the expected entity bounds");
		}

		auto test_scene_instantiates_asset_id_with_world_transform() -> bool
		{
			const std::filesystem::path test_root = engine_self_test_dir() / "scene_instantiation_assets";
			std::filesystem::create_directories(test_root);
			const std::filesystem::path prefab_path = test_root / "placed_prefab.ashasset";
			{
				std::ofstream prefab_file(prefab_path, std::ios::trunc);
				prefab_file <<
					"{\n"
					"  \"version\": 2,\n"
					"  \"name\": \"PlacedPrefab\",\n"
					"  \"nodes\": [\n"
					"    {\n"
					"      \"name\": \"PrefabRoot\",\n"
					"      \"parent\": -1,\n"
					"      \"transform\": {\n"
					"        \"position\": [0, 0, 0],\n"
					"        \"rotation_euler_degrees\": [0, 0, 0],\n"
					"        \"scale\": [1, 1, 1]\n"
					"      }\n"
					"    }\n"
					"  ]\n"
					"}\n";
			}

			AssetDatabase database = AssetDatabase::create(test_root);
			if (!database.is_valid() || !database.refresh())
			{
				return report_self_test_failure("Scene asset-id instantiation", "failed to create test asset database");
			}
			const AssetInfo* asset_info = database.find_asset_by_path(prefab_path.filename());
			if (!asset_info)
			{
				return report_self_test_failure("Scene asset-id instantiation", "failed to find generated prefab asset");
			}

			Scene scene = Scene::create("AssetIdInstantiationSelfTest");
			SceneInstantiationDesc desc{};
			desc.use_world_transform = true;
			desc.world_position = { 10.0f, 2.0f, 3.0f };
			desc.world_rotation_euler_degrees = { 0.0f, 90.0f, 0.0f };
			desc.world_scale = { 2.0f, 2.0f, 2.0f };
			desc.root_name_override = "PlacedRoot";

			Entity root = instantiate_asset(scene, database, asset_info->id, desc);
			if (!root.is_valid())
			{
				return report_self_test_failure("Scene asset-id instantiation", "failed to instantiate prefab asset id");
			}

			const TransformComponent root_transform = root.get_transform_component();
			const bool ok =
				root.get_name() == "PlacedRoot" &&
				glm::length(root_transform.position - desc.world_position) < 0.0001f &&
				glm::length(root_transform.rotation_euler_degrees - desc.world_rotation_euler_degrees) < 0.001f &&
				glm::length(root_transform.scale - desc.world_scale) < 0.0001f;
			return ok ||
				report_self_test_failure("Scene asset-id instantiation", "world placement transform was not applied to the instantiated root");
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
		all_passed = test_memory_service_reports_heap_statistics() && all_passed;
		all_passed = test_process_memory_snapshot_is_available() && all_passed;
		all_passed = test_stack_allocator_marker_rejects_forward_free() && all_passed;
		all_passed = test_linear_allocator_deallocate_reports_unsupported() && all_passed;
		all_passed = test_array_growth_and_initial_size() && all_passed;
		all_passed = test_file_delete_reports_success() && all_passed;
		all_passed = test_file_read_text_and_extension_are_safe() && all_passed;
		all_passed = test_shader_hash_uses_explicit_source_hash() && all_passed;
		all_passed = test_subresource_range_resolve_clamps_defaults() && all_passed;
		all_passed = test_ash_barrier_copy_move_is_safe() && all_passed;
#if defined(ASH_HAS_VULKAN)
		all_passed = test_vulkan_swapchain_forced_recreate_ignores_matching_extent() && all_passed;
		all_passed = test_vulkan_acquire_wait_covers_initial_swapchain_barrier() && all_passed;
#endif
		all_passed = test_render_memory_stats_default_to_unsupported() && all_passed;
		all_passed = test_perf_gate_config_parser_defaults_to_disabled() && all_passed;
		all_passed = test_perf_gate_config_parser_reads_arguments() && all_passed;
		all_passed = test_perf_gate_frame_summary_percentiles_are_stable() && all_passed;
		all_passed = test_texture_decode_generates_rgba8_mips() && all_passed;
		all_passed = test_texture_decode_supports_dds_bc1() && all_passed;
		all_passed = test_texture_decode_supports_ktx2_bc7() && all_passed;
		all_passed = test_environment_map_cpu_baker_generates_required_payloads() && all_passed;
		all_passed = test_environment_map_cpu_baker_uses_irradiance_units() && all_passed;
		all_passed = test_environment_map_cpu_baker_brdf_lut_is_energy_bounded() && all_passed;
		all_passed = test_ashibl_round_trip_uncompressed_payloads() && all_passed;
		all_passed = test_environment_asset_key_and_fallback_policy() && all_passed;
		all_passed = test_environment_source_cache_path_uses_content_hash() && all_passed;
		all_passed = test_environment_runtime_source_bake_does_not_block_request_path() && all_passed;
		all_passed = test_environment_shaders_use_compact_root_constants() && all_passed;
		all_passed = test_environment_shader_applies_lambert_to_irradiance() && all_passed;
		all_passed = test_environment_passes_use_split_intensity_controls() && all_passed;
		all_passed = test_texture_cube_upload_contract() && all_passed;
		all_passed = test_dx12_validation_config_respects_build_type() && all_passed;
		all_passed = test_render_feature_config_registers_vsync_without_reverse_z() && all_passed;
		all_passed = test_engine_ini_excludes_scene_render_config_sections() && all_passed;
		all_passed = test_bloom_config_defaults_and_sanitization() && all_passed;
		all_passed = test_volumetric_lighting_config_defaults_and_sanitization() && all_passed;
		all_passed = test_bloom_shader_source_contract() && all_passed;
		all_passed = test_bloom_pass_source_contract() && all_passed;
		all_passed = test_volumetric_lighting_shader_source_contract() && all_passed;
		all_passed = test_volumetric_lighting_pass_source_contract() && all_passed;
		all_passed = test_volumetric_lighting_pass_adds_expected_graph_chain_for_tests() && all_passed;
		all_passed = test_volumetric_lighting_atlas_budget_contract() && all_passed;
		all_passed = test_resize_clears_render_size_caches_contract() && all_passed;
		all_passed = test_scene_renderer_bloom_integration_contract() && all_passed;
		all_passed = test_scene_renderer_volumetric_lighting_integration_contract() && all_passed;
		all_passed = test_ambient_occlusion_temporal_pipeline_contract() && all_passed;
		all_passed = test_ambient_occlusion_downsampled_scene_uv_contract() && all_passed;
		all_passed = test_frame_dump_streaming_quiesce_contract() && all_passed;
		all_passed = test_render_debug_view_config_parses_runtime_selection() && all_passed;
		all_passed = test_render_debug_view_registry_replaces_duplicate_items() && all_passed;
		all_passed = test_render_debug_view_graph_pass_contract() && all_passed;
		all_passed = test_render_debug_view_linear_hdr_uses_raw_preview() && all_passed;
		all_passed = test_reverse_z_projection_maps_near_far_depths() && all_passed;
		all_passed = test_reverse_z_flips_depth_clear_and_compare() && all_passed;
		all_passed = test_deferred_shader_background_depth_uses_reverse_z_flag() && all_passed;
		all_passed = test_deferred_light_volume_draws_carry_reverse_z_flag() && all_passed;
		all_passed = test_static_mesh_gbuffer_shader_writes_motion_vectors() && all_passed;
		all_passed = test_surface_static_mesh_shader_hash_tracks_shared_includes() && all_passed;
		all_passed = test_gltf_import_preserves_index_reuse() && all_passed;
		all_passed = test_material_asset_database_prefers_disk_material_over_builtin_fallback() && all_passed;
		all_passed = test_scene_renderer_batches_only_when_multiple_static_mesh_draws_are_visible() && all_passed;
		all_passed = test_scene_renderer_instance_buffer_slots_are_isolated_between_view_submits() && all_passed;
		all_passed = test_scene_renderer_instance_buffer_slots_are_lagged_between_frames() && all_passed;
		all_passed = test_scene_renderer_temporal_history_uses_render_frame_epoch() && all_passed;
		all_passed = test_scene_renderer_temporal_history_is_gbuffer_only() && all_passed;
		all_passed = test_renderer_frame_stats_cover_presented_frame() && all_passed;
		all_passed = test_renderer_frame_stats_expose_frame_pacing_breakdown() && all_passed;
		all_passed = test_rhi_frame_slots_match_default_triple_buffering() && all_passed;
		all_passed = test_scene_presentation_reuses_prepared_material_proxy() && all_passed;
		all_passed = test_deferred_hq_gbuffer_layout_contract() && all_passed;
		all_passed = test_deferred_shading_model_ids_are_stable() && all_passed;
		all_passed = test_material_asset_loads_declared_shading_model() && all_passed;
		all_passed = test_graphics_program_state_maps_deferred_light_volume_state() && all_passed;
		all_passed = test_deferred_read_only_depth_attachment_state() && all_passed;
		all_passed = test_render_graph_access_maps_to_rhi_states() && all_passed;
		all_passed = test_render_graph_builder_records_raster_usage() && all_passed;
		all_passed = test_render_graph_compiler_culls_dead_passes_and_keeps_roots() && all_passed;
		all_passed = test_render_graph_compile_cache_reuses_stable_topology() && all_passed;
		all_passed = test_scene_deferred_graph_resources_describe_live_pass_chain() && all_passed;
		all_passed = test_render_pass_attachment_final_state_defaults_to_unknown() && all_passed;
		all_passed = test_render_scene_extracts_light_snapshot() && all_passed;
		all_passed = test_scene_light_sunlight_json_round_trip() && all_passed;
		all_passed = test_scene_rejects_multiple_directional_sunlights() && all_passed;
		all_passed = test_scene_environment_metadata_creates_sunlight() && all_passed;
		all_passed = test_scene_environment_metadata_preserves_existing_sunlight() && all_passed;
		all_passed = test_render_scene_copies_scene_render_config_to_visible_frame() && all_passed;
		all_passed = test_scene_extracts_single_active_environment() && all_passed;
		all_passed = test_sunlight_shadow_planner_rejects_multiple_sunlights() && all_passed;
		all_passed = test_sunlight_shadow_planner_ignores_ordinary_directional_lights() && all_passed;
		all_passed = test_sunlight_shadow_planner_releases_partial_cascade_tiles() && all_passed;
		all_passed = test_directional_light_shadow_planner_handles_each_ordinary_light_without_global_budget() && all_passed;
		all_passed = test_directional_light_shadow_planner_uses_uncached_cascades() && all_passed;
		all_passed = test_directional_shadow_static_cache_reuses_evicted_tiles() && all_passed;
		all_passed = test_directional_shadow_static_cache_invalidates_when_cascade_matrix_changes() && all_passed;
		all_passed = test_directional_shadow_planner_builds_monotonic_cascades() && all_passed;
		all_passed = test_visible_static_mesh_draws_carry_shadow_mobility() && all_passed;
		all_passed = test_directional_shadow_mask_normal_bias_offsets_along_normal() && all_passed;
		all_passed = test_directional_shadow_mask_blends_cascade_transition() && all_passed;
		all_passed = test_directional_shadow_cascade_buffer_uses_two_dimensional_texel_size() && all_passed;
		all_passed = test_directional_shadow_cascade_projection_snaps_to_texel_grid() && all_passed;
		all_passed = test_directional_shadow_cascade_projection_size_is_rotation_stable() && all_passed;
		all_passed = test_directional_shadow_static_cache_copy_uses_atlas_uv_scale() && all_passed;
		all_passed = test_directional_shadow_static_cache_copy_declares_graph_read() && all_passed;
		all_passed = test_directional_shadow_graph_adds_depth_before_lighting() && all_passed;
		all_passed = test_directional_shadow_deferred_graph_contract() && all_passed;
		all_passed = test_deferred_lighting_pass_exposes_explicit_light_submission_api() && all_passed;
		all_passed = test_scene_deferred_graph_resources_split_directional_shadow_refs() && all_passed;
		all_passed = test_scene_render_versions_separate_transform_from_primitive_changes() && all_passed;
		all_passed = test_scene_environment_version_isolated_from_primitives() && all_passed;
		all_passed = test_scene_render_config_version_isolated_from_other_render_versions() && all_passed;
		all_passed = test_scene_render_config_json_defaults_and_round_trip() && all_passed;
		all_passed = test_sandbox_scene_enables_volumetric_lighting() && all_passed;
		all_passed = test_graphics_draw_desc_keeps_common_vertex_bindings_inline() && all_passed;
		all_passed = test_debug_draw_service_records_and_clears_frame_lines() && all_passed;
		all_passed = test_debug_draw_service_expands_shapes_to_line_list() && all_passed;
		all_passed = test_scene_view_builds_from_override_matrices() && all_passed;
		all_passed = test_scene_query_bounds_ray_and_picking() && all_passed;
		all_passed = test_scene_instantiates_asset_id_with_world_transform() && all_passed;
#if defined(ASH_HAS_DX12)
		all_passed = test_dx12_resource_tracker_preserves_partial_state() && all_passed;
#endif

		MemoryService::instance()->shutdown();
		LogService::instance()->shutdown();
		return all_passed ? 0 : 1;
	}
}
