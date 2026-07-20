#pragma once

#include "Base/hcore.h"
#include "Function/Render/RenderGraph.h"
#include "Graphics/Swapchain.h"

#include <cstdint>
#include <functional>

namespace AshEngine
{
	class Renderer;

	enum class RenderGraphIndirectSelfTestFailure : uint8_t
	{
		None = 0,
		BeginFrame,
		Graph,
		CaptureRequest,
		EndFrame,
		Present,
		CaptureFetch
	};

	struct RenderGraphIndirectSelfTestLifecycleOperations
	{
		void* user_data = nullptr;
		RHI::SwapchainPresentResult(*begin_frame)(void*) = nullptr;
		bool (*execute_graph)(void*) = nullptr;
		bool (*request_capture)(void*) = nullptr;
		bool (*end_frame)(void*) = nullptr;
		RHI::SwapchainPresentResult(*present)(void*) = nullptr;
		bool (*fetch_capture)(void*) = nullptr;
	};

	struct RenderGraphIndirectSelfTestLifecycleResult
	{
		bool succeeded = false;
		RHI::SwapchainPresentResult begin_result = RHI::SwapchainPresentResult::Failed;
		RenderGraphIndirectSelfTestFailure failure = RenderGraphIndirectSelfTestFailure::BeginFrame;
	};

	ASH_API RenderGraphIndirectSelfTestLifecycleResult run_render_graph_indirect_self_test_lifecycle(
		const RenderGraphIndirectSelfTestLifecycleOperations& operations);

	struct RenderGraphIndirectSelfTestResources
	{
		RenderGraphTextureRef output{};
		RenderGraphBufferRef candidate{};
		RenderGraphBufferRef visible{};
		RenderGraphBufferRef args{};
	};

	struct RenderGraphIndirectSelfTestPassCallbacks
	{
		std::function<bool(RenderGraphComputeContext&)> compute{};
		std::function<bool(RenderGraphRasterContext&)> indexed_raster{};
		std::function<bool(RenderGraphRasterContext&)> validation_raster{};
	};

	ASH_API bool add_render_graph_indirect_self_test_passes(
		RenderGraphBuilder& graph,
		const RenderGraphIndirectSelfTestResources& resources,
		const RenderGraphIndirectSelfTestPassCallbacks& callbacks);

	ASH_API bool run_render_graph_indirect_self_test(
		Renderer& renderer,
		uint64_t capture_timeout_nanoseconds = 30'000'000'000ull);
}
