#include "Function/Render/RenderGraphCompiler.h"
#include "Base/hlog.h"
#include "Base/hprofiler.h"
#include <algorithm>

namespace AshEngine
{
	namespace
	{
		bool usage_is_write(const RenderGraphTextureUsage& usage)
		{
			return usage.access == RenderGraphAccess::ColorAttachmentWrite ||
				usage.access == RenderGraphAccess::DepthStencilWrite ||
				usage.access == RenderGraphAccess::GraphicsUAV ||
				usage.access == RenderGraphAccess::ComputeUAV ||
				usage.access == RenderGraphAccess::CopyDst;
		}

		bool usage_is_read(const RenderGraphTextureUsage& usage)
		{
			return usage.access == RenderGraphAccess::GraphicsSRV ||
				usage.access == RenderGraphAccess::ComputeSRV ||
				usage.access == RenderGraphAccess::DepthStencilRead ||
				usage.access == RenderGraphAccess::VertexBufferRead ||
				usage.access == RenderGraphAccess::IndexBufferRead ||
				usage.access == RenderGraphAccess::ConstantBufferRead ||
				usage.access == RenderGraphAccess::CopySrc ||
				usage.access == RenderGraphAccess::Present;
		}
	}

	bool RenderGraphCompiler::compile(
		const std::vector<RenderGraphTextureNode>& textures,
		const std::vector<RenderGraphPassNode>& passes,
		RenderGraphCompileResult& out_result)
	{
		ASH_PROFILE_SCOPE_NC("RenderGraphCompiler::compile", AshEngine::Profile::Color::Submit);
		ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(passes.size()));
		ASH_PROFILE_PLOT("RenderGraph/DeclaredPasses", static_cast<int64_t>(passes.size()));
		ASH_PROFILE_PLOT("RenderGraph/DeclaredTextures", static_cast<int64_t>(textures.size()));

		out_result = {};
		out_result.texture_lifetimes.resize(textures.size());
		out_result.pass_barriers.resize(passes.size());

		std::vector<int32_t> producer_for_texture(textures.size(), -1);
		std::vector<std::vector<uint32_t>> pass_read_dependencies(passes.size());
		bool valid = true;

		for (uint32_t pass_index = 0; pass_index < passes.size(); ++pass_index)
		{
			const RenderGraphPassNode& pass = passes[pass_index];
			bool pass_reads = false;
			bool pass_writes = false;

			for (const RenderGraphTextureUsage& usage : pass.texture_usages)
			{
				if (!usage.texture || usage.texture.index >= textures.size())
				{
					HLogError("RenderGraphCompiler: pass '{}' references an invalid texture.", pass.name);
					valid = false;
					break;
				}

				if (usage_is_read(usage))
				{
					pass_reads = true;
					const int32_t producer = producer_for_texture[usage.texture.index];
					if (producer >= 0)
					{
						pass_read_dependencies[pass_index].push_back(static_cast<uint32_t>(producer));
					}
					else if (!textures[usage.texture.index].external)
					{
						HLogError(
							"RenderGraphCompiler: pass '{}' reads transient texture '{}' before it is produced.",
							pass.name,
							textures[usage.texture.index].name);
						valid = false;
					}
				}

				if (usage_is_write(usage))
				{
					pass_writes = true;
					producer_for_texture[usage.texture.index] = static_cast<int32_t>(pass_index);
				}
			}

			if (!valid)
			{
				break;
			}

			if (!pass_reads && !pass_writes && !has_render_graph_pass_flag(pass.flags, RenderGraphPassFlags::NeverCull))
			{
				HLogError("RenderGraphCompiler: pass '{}' has no resource usage and is not NeverCull.", pass.name);
				valid = false;
				break;
			}
		}

		if (!valid)
		{
			return false;
		}

		std::vector<bool> live_passes(passes.size(), false);
		auto mark_live = [&](auto& self, uint32_t pass_index) -> void
		{
			if (pass_index >= passes.size() || live_passes[pass_index])
			{
				return;
			}

			live_passes[pass_index] = true;
			for (const uint32_t producer_pass : pass_read_dependencies[pass_index])
			{
				self(self, producer_pass);
			}
		};

		for (uint32_t pass_index = 0; pass_index < passes.size(); ++pass_index)
		{
			if (has_render_graph_pass_flag(passes[pass_index].flags, RenderGraphPassFlags::NeverCull))
			{
				mark_live(mark_live, pass_index);
			}
		}

		for (uint32_t texture_index = 0; texture_index < textures.size(); ++texture_index)
		{
			if (!textures[texture_index].external && !textures[texture_index].extracted)
			{
				continue;
			}

			const int32_t producer = producer_for_texture[texture_index];
			if (producer >= 0)
			{
				mark_live(mark_live, static_cast<uint32_t>(producer));
			}
		}

		uint32_t culled_pass_count = 0;
		for (uint32_t pass_index = 0; pass_index < passes.size(); ++pass_index)
		{
			if (!live_passes[pass_index])
			{
				++culled_pass_count;
				HLogInfo("RenderGraphCompiler: culled pass '{}'.", passes[pass_index].name);
				continue;
			}

			out_result.live_pass_indices.push_back(pass_index);
			for (const RenderGraphTextureUsage& usage : passes[pass_index].texture_usages)
			{
				RenderGraphTextureLifetime& lifetime = out_result.texture_lifetimes[usage.texture.index];
				lifetime.used = true;
				lifetime.first_pass = std::min(lifetime.first_pass, pass_index);
				lifetime.last_pass = lifetime.last_pass == UINT32_MAX ? pass_index : std::max(lifetime.last_pass, pass_index);

				RenderGraphPassBarrierPlan& barrier_plan = out_result.pass_barriers[pass_index];
				if (barrier_plan.texture_states.size() < textures.size())
				{
					barrier_plan.texture_states.resize(textures.size(), RHI::AshResourceState::Unknown);
				}

				RHI::AshResourceState state = RHI::AshResourceState::Unknown;
				if (usage.depth && usage.access == RenderGraphAccess::DepthStencilRead)
				{
					state = render_graph_depth_read_state(usage.depth_read_mode);
				}
				else
				{
					state = render_graph_access_to_rhi_state(usage.access);
				}

				barrier_plan.texture_states[usage.texture.index] = state;
				if (state != RHI::AshResourceState::Unknown)
				{
					barrier_plan.transitions.push_back({ usage.texture, state });
				}
			}
		}

		ASH_PROFILE_PLOT("RenderGraph/LivePasses", static_cast<int64_t>(out_result.live_pass_indices.size()));
		ASH_PROFILE_PLOT("RenderGraph/CulledPasses", static_cast<int64_t>(culled_pass_count));
		return true;
	}
}
