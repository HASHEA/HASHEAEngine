#pragma once

#include "Function/Render/RenderGraphBuilder.h"
#include "Graphics/RHIResource.h"
#include <cstdint>
#include <vector>

namespace AshEngine
{
	struct RenderGraphTextureLifetime
	{
		bool used = false;
		uint32_t first_pass = UINT32_MAX;
		uint32_t last_pass = UINT32_MAX;
	};

	struct RenderGraphTextureTransition
	{
		RenderGraphTextureRef texture{};
		RHI::AshResourceState state = RHI::AshResourceState::Unknown;
	};

	struct RenderGraphPassBarrierPlan
	{
		std::vector<RenderGraphTextureTransition> transitions{};
		std::vector<RHI::AshResourceState> texture_states{};
	};

	struct RenderGraphCompileResult
	{
		std::vector<uint32_t> live_pass_indices{};
		std::vector<RenderGraphTextureLifetime> texture_lifetimes{};
		std::vector<RenderGraphPassBarrierPlan> pass_barriers{};
	};

	struct RenderGraphCompileCacheStats
	{
		uint64_t hits = 0;
		uint64_t misses = 0;
	};

	class RenderGraphCompiler
	{
	public:
		static bool compile(
			const std::vector<RenderGraphTextureNode>& textures,
			const std::vector<RenderGraphPassNode>& passes,
			RenderGraphCompileResult& out_result);
		static bool compile_cached(
			const std::vector<RenderGraphTextureNode>& textures,
			const std::vector<RenderGraphPassNode>& passes,
			RenderGraphCompileResult& out_result);
		static void reset_compile_cache_for_tests();
		static RenderGraphCompileCacheStats get_compile_cache_stats_for_tests();
	};
}
