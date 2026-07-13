#include "Function/Render/RenderGraphBuilder.h"
#include "Function/Render/RenderGraphCompiler.h"
#include "Base/hlog.h"
#include "Base/hprofiler.h"

namespace AshEngine
{
	RenderGraphGpuTimingScopeGuard::RenderGraphGpuTimingScopeGuard(
		RHI::IGpuTimingTelemetry* telemetry,
		RHI::CommandBuffer* command_buffer)
		: m_telemetry(telemetry)
		, m_command_buffer(command_buffer)
	{
	}

	RenderGraphGpuTimingScopeGuard::~RenderGraphGpuTimingScopeGuard()
	{
		close();
	}

	void RenderGraphGpuTimingScopeGuard::transition_to(RHI::GpuTimingMetric metric)
	{
		// Only adjacent passes coalesce. Returning to a metric after another group
		// intentionally begins it again so the telemetry contract can flag duplicates.
		const RHI::GpuTimingMetric next_group =
			is_render_graph_gpu_timing_group_metric(metric) ? metric : RHI::GpuTimingMetric::Invalid;
		if (next_group == m_current_group)
		{
			return;
		}

		if (m_scope_open && m_telemetry && m_command_buffer)
		{
			m_telemetry->end_scope(m_command_buffer, m_current_group);
		}
		m_scope_open = false;
		m_current_group = next_group;

		if (m_current_group != RHI::GpuTimingMetric::Invalid && m_telemetry && m_command_buffer)
		{
			m_scope_open = m_telemetry->begin_scope(m_command_buffer, m_current_group);
		}
	}

	void RenderGraphGpuTimingScopeGuard::close()
	{
		if (m_scope_open && m_telemetry && m_command_buffer)
		{
			m_telemetry->end_scope(m_command_buffer, m_current_group);
		}
		m_scope_open = false;
		m_current_group = RHI::GpuTimingMetric::Invalid;
	}

	namespace
	{
		class RasterContext final : public RenderGraphRasterContext
		{
		public:
			RasterContext(Renderer::GraphicsPassContext& pass_context, std::vector<RenderGraphTextureNode>& textures)
				: m_pass_context(pass_context)
				, m_textures(textures)
			{
			}

			std::shared_ptr<RenderTarget> get_texture(RenderGraphTextureRef texture) override
			{
				if (texture.index >= m_textures.size())
				{
					return nullptr;
				}
				return m_textures[texture.index].external_texture;
			}

			bool draw(const GraphicsDrawDesc& desc) override
			{
				return m_pass_context.draw(desc);
			}

		private:
			Renderer::GraphicsPassContext& m_pass_context;
			std::vector<RenderGraphTextureNode>& m_textures;
		};

		class ComputeContext final : public RenderGraphComputeContext
		{
		public:
			ComputeContext(Renderer& renderer, std::vector<RenderGraphTextureNode>& textures)
				: m_renderer(renderer)
				, m_textures(textures)
			{
			}

			std::shared_ptr<RenderTarget> get_texture(RenderGraphTextureRef texture) override
			{
				if (texture.index >= m_textures.size())
				{
					return nullptr;
				}
				return m_textures[texture.index].external_texture;
			}

			bool dispatch(const ComputeDispatchDesc& desc) override
			{
				return m_renderer.dispatch(desc);
			}

		private:
			Renderer& m_renderer;
			std::vector<RenderGraphTextureNode>& m_textures;
		};

		bool texture_is_depth_format(RenderTextureFormat format)
		{
			return format == RenderTextureFormat::D24_UNORM_S8_UINT ||
				format == RenderTextureFormat::D32_SFLOAT;
		}
	}

	bool execute_render_graph(Renderer& renderer, std::vector<RenderGraphTextureNode>& textures, const std::vector<RenderGraphPassNode>& passes)
	{
		RenderDevice* render_device = renderer.get_render_device();
		return execute_render_graph(
			renderer,
			textures,
			passes,
			render_device ? render_device->get_gpu_timing_telemetry() : nullptr,
			render_device ? render_device->get_current_command_buffer() : nullptr);
	}

	bool execute_render_graph(
		Renderer& renderer,
		std::vector<RenderGraphTextureNode>& textures,
		const std::vector<RenderGraphPassNode>& passes,
		RHI::IGpuTimingTelemetry* telemetry,
		RHI::CommandBuffer* command_buffer)
	{
		ASH_PROFILE_SCOPE_NC("RenderGraph::execute", AshEngine::Profile::Color::Render);
		ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(passes.size()));

		RenderGraphCompileResult compiled{};
		if (!RenderGraphCompiler::compile_cached(textures, passes, compiled))
		{
			return false;
		}
		ASH_PROFILE_PLOT("RenderGraph/ExecutedPasses", static_cast<int64_t>(compiled.live_pass_indices.size()));

		std::vector<std::shared_ptr<RenderTarget>> allocated_transients{};
		const auto release_allocated_transients = [&renderer, &allocated_transients]()
		{
			ASH_PROFILE_SCOPE_NC("RenderGraph::ReleaseTransients", AshEngine::Profile::Color::Upload);
			ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(allocated_transients.size()));
			for (const std::shared_ptr<RenderTarget>& render_target : allocated_transients)
			{
				renderer.release_transient_render_target(render_target);
			}
			allocated_transients.clear();
		};

		uint32_t allocated_transient_count = 0;
		{
			ASH_PROFILE_SCOPE_NC("RenderGraph::AllocateTransients", AshEngine::Profile::Color::Upload);
			ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(textures.size()));
			for (uint32_t texture_index = 0; texture_index < textures.size(); ++texture_index)
			{
				RenderGraphTextureNode& texture = textures[texture_index];
				const bool should_allocate =
					!texture.external &&
					texture_index < compiled.texture_lifetimes.size() &&
					compiled.texture_lifetimes[texture_index].used;
				if (!should_allocate)
				{
					continue;
				}

				RenderTargetDesc desc = texture.desc.to_render_target_desc(texture.name.c_str());
				texture.external_texture = renderer.acquire_transient_render_target(desc);
				if (!texture.external_texture)
				{
					HLogError("RenderGraph: failed to allocate transient texture '{}'.", texture.name);
					release_allocated_transients();
					return false;
				}
				allocated_transients.push_back(texture.external_texture);
				++allocated_transient_count;
			}
		}
		ASH_PROFILE_PLOT("RenderGraph/TransientTextures", static_cast<int64_t>(allocated_transient_count));

		RenderGraphGpuTimingScopeGuard gpu_timing_scope(telemetry, command_buffer);
		const auto fail_execution = [&gpu_timing_scope, &release_allocated_transients]()
		{
			gpu_timing_scope.close();
			release_allocated_transients();
			return false;
		};

		for (uint32_t pass_index : compiled.live_pass_indices)
		{
			const RenderGraphPassNode& pass = passes[pass_index];
			gpu_timing_scope.transition_to(pass.timing_metric);
			if (pass.kind == RenderGraphPassKind::Compute)
			{
				ASH_PROFILE_SCOPE_NC("RenderGraph::ExecuteComputePass", AshEngine::Profile::Color::Submit);
				ASH_PROFILE_SCOPE_TEXT(pass.name.c_str(), pass.name.size());
				ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(pass.texture_usages.size()));
				ComputeContext context(renderer, textures);
				if (!pass.compute_execute || !pass.compute_execute(context))
				{
					HLogError("RenderGraph: compute pass '{}' failed.", pass.name);
					return fail_execution();
				}
				continue;
			}

			ASH_PROFILE_SCOPE_NC("RenderGraph::ExecuteRasterPass", AshEngine::Profile::Color::Submit);
			ASH_PROFILE_SCOPE_TEXT(pass.name.c_str(), pass.name.size());
			ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(pass.texture_usages.size()));

			PassDesc pass_desc{};
			pass_desc.name = pass.name.c_str();
			for (const RenderGraphTextureUsage& usage : pass.texture_usages)
			{
				if (!usage.texture || usage.texture.index >= textures.size())
				{
					HLogError("RenderGraph: raster pass '{}' has invalid texture usage.", pass.name);
					return fail_execution();
				}

				std::shared_ptr<RenderTarget> target = textures[usage.texture.index].external_texture;
				if (!target)
				{
					HLogError(
						"RenderGraph: raster pass '{}' references unallocated texture '{}'.",
						pass.name,
						textures[usage.texture.index].name);
					return fail_execution();
				}

				if (usage.access == RenderGraphAccess::ColorAttachmentWrite)
				{
					if (texture_is_depth_format(target->get_format()))
					{
						HLogError("RenderGraph: pass '{}' uses depth format as color attachment.", pass.name);
						return fail_execution();
					}
					if (pass_desc.color_attachments.size() <= usage.color_slot)
					{
						pass_desc.color_attachments.resize(static_cast<size_t>(usage.color_slot) + 1u);
					}
					PassColorAttachment& attachment = pass_desc.color_attachments[usage.color_slot];
					attachment.render_target = target;
					attachment.load_action = usage.load_action;
					attachment.clear_color = usage.clear_color;
				}
				else if (usage.access == RenderGraphAccess::DepthStencilWrite || usage.access == RenderGraphAccess::DepthStencilRead)
				{
					if (!texture_is_depth_format(target->get_format()))
					{
						HLogError("RenderGraph: pass '{}' uses color format as depth attachment.", pass.name);
						return fail_execution();
					}
					pass_desc.depth_attachment.render_target = target;
					pass_desc.depth_attachment.load_action = usage.load_action;
					pass_desc.depth_attachment.clear_value = usage.clear_depth;
					pass_desc.depth_attachment.read_only = usage.access == RenderGraphAccess::DepthStencilRead;
					pass_desc.depth_attachment.final_state =
						usage.access == RenderGraphAccess::DepthStencilRead ?
						render_graph_depth_read_state(usage.depth_read_mode) :
						RHI::AshResourceState::DSVWrite;
				}
			}

			Renderer::GraphicsPassContext pass_context{};
			if (!renderer.begin_pass(pass_desc, pass_context))
			{
				HLogError("RenderGraph: begin raster pass '{}' failed.", pass.name);
				return fail_execution();
			}

			RasterContext context(pass_context, textures);
			const bool pass_result = pass.raster_execute && pass.raster_execute(context);
			pass_context.end();
			if (!pass_result)
			{
				HLogError("RenderGraph: raster pass '{}' failed.", pass.name);
				return fail_execution();
			}
		}

		gpu_timing_scope.close();
		release_allocated_transients();
		return true;
	}
}
