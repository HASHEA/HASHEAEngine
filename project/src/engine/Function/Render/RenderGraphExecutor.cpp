#include "Function/Render/RenderGraphBuilder.h"
#include "Function/Render/RenderGraphCompiler.h"
#include "Base/hlog.h"

namespace AshEngine
{
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
		RenderGraphCompileResult compiled{};
		if (!RenderGraphCompiler::compile(textures, passes, compiled))
		{
			return false;
		}

		std::vector<std::shared_ptr<RenderTarget>> allocated_transients{};
		const auto release_allocated_transients = [&renderer, &allocated_transients]()
		{
			for (const std::shared_ptr<RenderTarget>& render_target : allocated_transients)
			{
				renderer.release_transient_render_target(render_target);
			}
			allocated_transients.clear();
		};

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
		}

		for (uint32_t pass_index : compiled.live_pass_indices)
		{
			const RenderGraphPassNode& pass = passes[pass_index];
			if (pass.kind == RenderGraphPassKind::Compute)
			{
				ComputeContext context(renderer, textures);
				if (!pass.compute_execute || !pass.compute_execute(context))
				{
					HLogError("RenderGraph: compute pass '{}' failed.", pass.name);
					release_allocated_transients();
					return false;
				}
				continue;
			}

			PassDesc pass_desc{};
			pass_desc.name = pass.name.c_str();
			for (const RenderGraphTextureUsage& usage : pass.texture_usages)
			{
				if (!usage.texture || usage.texture.index >= textures.size())
				{
					HLogError("RenderGraph: raster pass '{}' has invalid texture usage.", pass.name);
					release_allocated_transients();
					return false;
				}

				std::shared_ptr<RenderTarget> target = textures[usage.texture.index].external_texture;
				if (!target)
				{
					HLogError(
						"RenderGraph: raster pass '{}' references unallocated texture '{}'.",
						pass.name,
						textures[usage.texture.index].name);
					release_allocated_transients();
					return false;
				}

				if (usage.access == RenderGraphAccess::ColorAttachmentWrite)
				{
					if (texture_is_depth_format(target->get_format()))
					{
						HLogError("RenderGraph: pass '{}' uses depth format as color attachment.", pass.name);
						release_allocated_transients();
						return false;
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
						release_allocated_transients();
						return false;
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
				release_allocated_transients();
				return false;
			}

			RasterContext context(pass_context, textures);
			const bool pass_result = pass.raster_execute && pass.raster_execute(context);
			pass_context.end();
			if (!pass_result)
			{
				HLogError("RenderGraph: raster pass '{}' failed.", pass.name);
				release_allocated_transients();
				return false;
			}
		}

		release_allocated_transients();
		return true;
	}
}
