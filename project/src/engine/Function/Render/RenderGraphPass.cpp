#include "Function/Render/RenderGraphPass.h"

namespace AshEngine
{
	RenderGraphRasterPassBuilder::RenderGraphRasterPassBuilder(RenderGraphPassNode& pass)
		: m_pass(&pass)
	{
	}

	void RenderGraphRasterPassBuilder::read_texture(RenderGraphTextureRef texture, RenderGraphAccess access)
	{
		if (m_pass)
		{
			m_pass->texture_usages.push_back({ texture, access });
		}
	}

	void RenderGraphRasterPassBuilder::read_buffer(RenderGraphBufferRef buffer, RenderGraphAccess access)
	{
		if (m_pass)
		{
			m_pass->buffer_usages.push_back({ buffer, access, false });
		}
	}

	void RenderGraphRasterPassBuilder::write_buffer(RenderGraphBufferRef buffer, RenderGraphAccess access)
	{
		if (m_pass)
		{
			m_pass->buffer_usages.push_back({ buffer, access, true });
		}
	}

	void RenderGraphRasterPassBuilder::write_color(uint8_t slot, RenderGraphTextureRef texture, RenderLoadAction load_action, RenderColorValue clear_color)
	{
		if (m_pass)
		{
			RenderGraphTextureUsage usage{};
			usage.texture = texture;
			usage.access = RenderGraphAccess::ColorAttachmentWrite;
			usage.color_slot = slot;
			usage.load_action = load_action;
			usage.clear_color = clear_color;
			m_pass->texture_usages.push_back(usage);
		}
	}

	void RenderGraphRasterPassBuilder::write_depth(RenderGraphTextureRef texture, RenderLoadAction load_action, RenderDepthStencilValue clear_value)
	{
		if (m_pass)
		{
			RenderGraphTextureUsage usage{};
			usage.texture = texture;
			usage.access = RenderGraphAccess::DepthStencilWrite;
			usage.depth = true;
			usage.load_action = load_action;
			usage.clear_depth = clear_value;
			m_pass->texture_usages.push_back(usage);
		}
	}

	void RenderGraphRasterPassBuilder::read_depth(RenderGraphTextureRef texture, RenderGraphDepthReadMode mode)
	{
		if (m_pass)
		{
			RenderGraphTextureUsage usage{};
			usage.texture = texture;
			usage.access = RenderGraphAccess::DepthStencilRead;
			usage.depth = true;
			usage.depth_read_mode = mode;
			usage.load_action = RenderLoadAction::Load;
			m_pass->texture_usages.push_back(usage);
		}
	}

	RenderGraphComputePassBuilder::RenderGraphComputePassBuilder(RenderGraphPassNode& pass)
		: m_pass(&pass)
	{
	}

	void RenderGraphComputePassBuilder::read_texture(RenderGraphTextureRef texture, RenderGraphAccess access)
	{
		if (m_pass)
		{
			m_pass->texture_usages.push_back({ texture, access });
		}
	}

	void RenderGraphComputePassBuilder::write_texture(RenderGraphTextureRef texture, RenderGraphAccess access)
	{
		if (m_pass)
		{
			m_pass->texture_usages.push_back({ texture, access });
		}
	}

	void RenderGraphComputePassBuilder::read_buffer(RenderGraphBufferRef buffer, RenderGraphAccess access)
	{
		if (m_pass)
		{
			m_pass->buffer_usages.push_back({ buffer, access, false });
		}
	}

	void RenderGraphComputePassBuilder::write_buffer(RenderGraphBufferRef buffer, RenderGraphAccess access)
	{
		if (m_pass)
		{
			m_pass->buffer_usages.push_back({ buffer, access, true });
		}
	}
}
