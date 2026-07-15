#pragma once

#include "Function/Render/RenderGraphPass.h"
#include <memory>
#include <string>
#include <vector>

namespace AshEngine
{
	struct RenderGraphCompileResult;

	struct RenderGraphTextureNode
	{
		std::string name{};
		RenderGraphTextureDesc desc{};
		std::shared_ptr<RenderTarget> external_texture = nullptr;
		bool external = false;
		bool extracted = false;
	};

	struct RenderGraphBufferNode
	{
		std::string name{};
		RenderGraphBufferDesc desc{};
		std::shared_ptr<StorageBuffer> external_buffer = nullptr;
		bool external = false;
		bool extracted = false;
		RenderGraphAccess initial_access = RenderGraphAccess::Unknown;
	};

	class RenderGraphBuilder
	{
	public:
		RenderGraphBuilder(Renderer& renderer, const char* name);
		static ASH_API RenderGraphBuilder create_headless_for_tests(const char* name);

		RenderGraphTextureRef register_external_texture(
			const std::shared_ptr<RenderTarget>& texture,
			const char* name,
			RenderGraphAccess initial_access = RenderGraphAccess::Unknown);
		RenderGraphTextureRef register_external_texture_desc_for_tests(const RenderTargetDesc& desc, const char* name);
		RenderGraphTextureRef create_texture(const RenderGraphTextureDesc& desc, const char* name);
		void extract_texture(RenderGraphTextureRef texture);
		ASH_API RenderGraphBufferRef register_external_buffer(
			const std::shared_ptr<StorageBuffer>& buffer,
			const char* name,
			RenderGraphAccess initial_access = RenderGraphAccess::Unknown);
		ASH_API RenderGraphBufferRef register_external_buffer_desc_for_tests(
			const RenderGraphBufferDesc& desc,
			const char* name,
			RenderGraphAccess initial_access = RenderGraphAccess::Unknown);
		ASH_API RenderGraphBufferRef create_buffer(const RenderGraphBufferDesc& desc, const char* name);
		ASH_API void extract_buffer(RenderGraphBufferRef buffer);

		bool add_raster_pass(
			const char* name,
			RenderGraphPassFlags flags,
			RHI::GpuTimingMetric timing_metric,
			const std::function<void(RenderGraphRasterPassBuilder&)>& setup,
			const std::function<bool(RenderGraphRasterContext&)>& execute);

		bool add_compute_pass(
			const char* name,
			RenderGraphPassFlags flags,
			RHI::GpuTimingMetric timing_metric,
			const std::function<void(RenderGraphComputePassBuilder&)>& setup,
			const std::function<bool(RenderGraphComputeContext&)>& execute);

		bool execute();
		ASH_API bool compile_for_tests(RenderGraphCompileResult& out_result) const;
		ASH_API bool compile_cached_for_tests(RenderGraphCompileResult& out_result) const;

		size_t get_texture_count_for_tests() const;
		ASH_API size_t get_pass_count_for_tests() const;
		ASH_API const std::vector<RenderGraphTextureNode>& get_textures_for_tests() const;
		ASH_API size_t get_buffer_count_for_tests() const;
		ASH_API const std::vector<RenderGraphBufferNode>& get_buffers_for_tests() const;
		ASH_API const std::vector<RenderGraphPassNode>& get_passes_for_tests() const;

	private:
		RenderGraphBuilder(Renderer* renderer, const char* name);

		Renderer* m_renderer = nullptr;
		std::string m_name{};
		std::vector<RenderGraphTextureNode> m_textures{};
		std::vector<RenderGraphBufferNode> m_buffers{};
		std::vector<RenderGraphPassNode> m_passes{};
	};

	// Narrow DLL bridges for doctest. Each delegates to the same production API
	// or executor core; none changes telemetry ownership or normal call paths.
	ASH_API bool add_render_graph_raster_pass_for_tests(
		RenderGraphBuilder& graph,
		const char* name,
		RenderGraphPassFlags flags,
		RHI::GpuTimingMetric timing_metric,
		const std::function<void(RenderGraphRasterPassBuilder&)>& setup,
		const std::function<bool(RenderGraphRasterContext&)>& execute);
	ASH_API bool add_render_graph_compute_pass_for_tests(
		RenderGraphBuilder& graph,
		const char* name,
		RenderGraphPassFlags flags,
		RHI::GpuTimingMetric timing_metric,
		const std::function<void(RenderGraphComputePassBuilder&)>& setup,
		const std::function<bool(RenderGraphComputeContext&)>& execute);
	ASH_API bool execute_render_graph_for_tests(
		Renderer& renderer,
		std::vector<RenderGraphTextureNode>& textures,
		const std::vector<RenderGraphPassNode>& passes,
		RHI::IGpuTimingTelemetry* telemetry,
		RHI::CommandBuffer* command_buffer);
	ASH_API void run_render_graph_gpu_timing_scope_sequence_for_tests(
		RHI::IGpuTimingTelemetry* telemetry,
		RHI::CommandBuffer* command_buffer,
		const std::vector<RHI::GpuTimingMetric>& metrics);
}
