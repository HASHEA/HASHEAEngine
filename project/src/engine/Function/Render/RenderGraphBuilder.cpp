#include "Function/Render/RenderGraphBuilder.h"
#include "Base/hlog.h"
#include <utility>

namespace AshEngine
{
	RenderGraphBuilder::RenderGraphBuilder(Renderer& renderer, const char* name)
		: RenderGraphBuilder(&renderer, name)
	{
	}

	RenderGraphBuilder::RenderGraphBuilder(Renderer* renderer, const char* name)
		: m_renderer(renderer)
		, m_name(name ? name : "RenderGraph")
	{
	}

	RenderGraphBuilder RenderGraphBuilder::create_headless_for_tests(const char* name)
	{
		return RenderGraphBuilder(nullptr, name);
	}

	RenderGraphTextureRef RenderGraphBuilder::register_external_texture(
		const std::shared_ptr<RenderTarget>& texture,
		const char* name,
		RenderGraphAccess)
	{
		if (!texture)
		{
			HLogError("RenderGraph '{}': cannot register null external texture '{}'.", m_name, name ? name : "<unnamed>");
			return {};
		}

		RenderGraphTextureNode node{};
		node.name = name ? name : "ExternalTexture";
		node.external_texture = texture;
		node.external = true;
		node.desc.width = static_cast<uint16_t>(texture->get_width());
		node.desc.height = static_cast<uint16_t>(texture->get_height());
		node.desc.format = texture->get_format();
		node.desc.shader_resource = !texture->is_depth_stencil();
		node.desc.unordered_access = false;
		m_textures.push_back(std::move(node));
		return { static_cast<uint32_t>(m_textures.size() - 1u) };
	}

	RenderGraphTextureRef RenderGraphBuilder::register_external_texture_desc_for_tests(const RenderTargetDesc& desc, const char* name)
	{
		RenderGraphTextureNode node{};
		node.name = name ? name : "ExternalTextureForTests";
		node.external = true;
		node.desc = RenderGraphTextureDesc::from_render_target_desc(desc);
		m_textures.push_back(std::move(node));
		return { static_cast<uint32_t>(m_textures.size() - 1u) };
	}

	RenderGraphTextureRef RenderGraphBuilder::create_texture(const RenderGraphTextureDesc& desc, const char* name)
	{
		RenderGraphTextureNode node{};
		node.name = name ? name : "RenderGraphTexture";
		node.desc = desc;
		node.external = false;
		m_textures.push_back(std::move(node));
		return { static_cast<uint32_t>(m_textures.size() - 1u) };
	}

	void RenderGraphBuilder::extract_texture(RenderGraphTextureRef texture)
	{
		if (texture.index < m_textures.size())
		{
			m_textures[texture.index].extracted = true;
		}
	}

	bool RenderGraphBuilder::add_raster_pass(
		const char* name,
		RenderGraphPassFlags flags,
		const std::function<void(RenderGraphRasterPassBuilder&)>& setup,
		const std::function<bool(RenderGraphRasterContext&)>& execute)
	{
		RenderGraphPassNode pass{};
		pass.name = name ? name : "RasterPass";
		pass.kind = RenderGraphPassKind::Raster;
		pass.flags = flags | RenderGraphPassFlags::Raster;
		pass.raster_execute = execute;
		RenderGraphRasterPassBuilder builder(pass);
		if (setup)
		{
			setup(builder);
		}
		m_passes.push_back(std::move(pass));
		return true;
	}

	bool RenderGraphBuilder::add_compute_pass(
		const char* name,
		RenderGraphPassFlags flags,
		const std::function<void(RenderGraphComputePassBuilder&)>& setup,
		const std::function<bool(RenderGraphComputeContext&)>& execute)
	{
		RenderGraphPassNode pass{};
		pass.name = name ? name : "ComputePass";
		pass.kind = RenderGraphPassKind::Compute;
		pass.flags = flags | RenderGraphPassFlags::Compute;
		pass.compute_execute = execute;
		RenderGraphComputePassBuilder builder(pass);
		if (setup)
		{
			setup(builder);
		}
		m_passes.push_back(std::move(pass));
		return true;
	}

	bool RenderGraphBuilder::execute()
	{
		HLogError("RenderGraph '{}': execute called before compiler/executor implementation is linked.", m_name);
		return false;
	}

	size_t RenderGraphBuilder::get_texture_count_for_tests() const
	{
		return m_textures.size();
	}

	size_t RenderGraphBuilder::get_pass_count_for_tests() const
	{
		return m_passes.size();
	}

	const std::vector<RenderGraphTextureNode>& RenderGraphBuilder::get_textures_for_tests() const
	{
		return m_textures;
	}

	const std::vector<RenderGraphPassNode>& RenderGraphBuilder::get_passes_for_tests() const
	{
		return m_passes;
	}
}
