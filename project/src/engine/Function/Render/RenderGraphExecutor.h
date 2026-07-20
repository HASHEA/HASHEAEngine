#pragma once

#include "Function/Render/RenderGraphBuilder.h"
#include <cstddef>

namespace AshEngine
{
	struct RenderGraphResolvedBufferTransition
	{
		std::shared_ptr<StorageBuffer> buffer = nullptr;
		RHI::AshResourceState state = RHI::AshResourceState::Unknown;
	};

	// Stack-owned execution seam shared by the production path and deterministic
	// executor tests. Function pointers avoid per-pass allocations.
	struct RenderGraphExecutionOps
	{
		void* user_data = nullptr;
		std::shared_ptr<StorageBuffer> (*acquire_transient_storage_buffer)(
			void* user_data,
			const StorageBufferDesc& desc) = nullptr;
		void (*release_transient_storage_buffer)(
			void* user_data,
			const std::shared_ptr<StorageBuffer>& buffer) = nullptr;
		bool (*submit_buffer_transitions)(
			void* user_data,
			const RenderGraphResolvedBufferTransition* transitions,
			size_t transition_count) = nullptr;
		bool (*begin_raster_pass)(
			void* user_data,
			const PassDesc& desc,
			Renderer::GraphicsPassContext& pass_context) = nullptr;
		bool (*end_raster_pass)(
			void* user_data,
			Renderer::GraphicsPassContext& pass_context) = nullptr;
	};

	ASH_API bool execute_render_graph_with_ops_for_tests(
		std::vector<RenderGraphTextureNode>& textures,
		std::vector<RenderGraphBufferNode>& buffers,
		const std::vector<RenderGraphPassNode>& passes,
		const RenderGraphExecutionOps& ops);
}
