#include "Function/Render/RenderGraphResource.h"

namespace AshEngine
{
	RenderGraphTextureDesc RenderGraphTextureDesc::from_render_target_desc(const RenderTargetDesc& desc)
	{
		RenderGraphTextureDesc result{};
		result.width = desc.width;
		result.height = desc.height;
		result.format = desc.format;
		result.shader_resource = desc.shader_resource;
		result.unordered_access = desc.unordered_access;
		result.use_optimized_clear_value = desc.use_optimized_clear_value;
		result.optimized_clear_color = desc.optimized_clear_color;
		result.optimized_clear_depth_stencil = desc.optimized_clear_depth_stencil;
		return result;
	}

	RenderTargetDesc RenderGraphTextureDesc::to_render_target_desc(const char* name) const
	{
		RenderTargetDesc result{};
		result.width = width;
		result.height = height;
		result.format = format;
		result.shader_resource = shader_resource;
		result.unordered_access = unordered_access;
		result.name = name;
		result.use_optimized_clear_value = use_optimized_clear_value;
		result.optimized_clear_color = optimized_clear_color;
		result.optimized_clear_depth_stencil = optimized_clear_depth_stencil;
		return result;
	}

	RenderGraphBufferDesc RenderGraphBufferDesc::from_storage_buffer_desc(const StorageBufferDesc& desc)
	{
		RenderGraphBufferDesc result{};
		result.size = desc.size;
		result.stride = desc.stride;
		result.shader_resource = true;
		result.unordered_access = true;
		result.indirect_args = desc.indirect_args;
		return result;
	}

	StorageBufferDesc RenderGraphBufferDesc::to_storage_buffer_desc(const char* name) const
	{
		StorageBufferDesc result{};
		result.size = size;
		result.stride = stride;
		result.cpu_write = false;
		result.indirect_args = indirect_args;
		result.initial_data = nullptr;
		result.name = name;
		return result;
	}

	RHI::AshResourceState render_graph_access_to_rhi_state(RenderGraphAccess access)
	{
		switch (access)
		{
		case RenderGraphAccess::GraphicsSRV:
			return RHI::AshResourceState::SRVGraphics;
		case RenderGraphAccess::ComputeSRV:
			return RHI::AshResourceState::SRVCompute;
		case RenderGraphAccess::GraphicsUAV:
			return RHI::AshResourceState::UAVGraphics;
		case RenderGraphAccess::ComputeUAV:
			return RHI::AshResourceState::UAVCompute;
		case RenderGraphAccess::ColorAttachmentWrite:
			return RHI::AshResourceState::RTV;
		case RenderGraphAccess::DepthStencilWrite:
			return RHI::AshResourceState::DSVWrite;
		case RenderGraphAccess::DepthStencilRead:
			return RHI::AshResourceState::DSVRead;
		case RenderGraphAccess::VertexBufferRead:
			return RHI::AshResourceState::VertexBuffer;
		case RenderGraphAccess::IndexBufferRead:
			return RHI::AshResourceState::IndexBuffer;
		case RenderGraphAccess::ConstantBufferRead:
			return RHI::AshResourceState::ConstBuffer;
		case RenderGraphAccess::CopySrc:
			return RHI::AshResourceState::CopySrc;
		case RenderGraphAccess::CopyDst:
			return RHI::AshResourceState::CopyDst;
		case RenderGraphAccess::Present:
			return RHI::AshResourceState::Present;
		case RenderGraphAccess::IndirectArgs:
			return RHI::AshResourceState::IndirectArgs;
		case RenderGraphAccess::Unknown:
		default:
			return RHI::AshResourceState::Unknown;
		}
	}

	RHI::AshResourceState render_graph_depth_read_state(RenderGraphDepthReadMode mode)
	{
		RHI::AshResourceState state = RHI::AshResourceState::DSVRead;
		if (mode == RenderGraphDepthReadMode::DepthTestAndShaderResource)
		{
			state = state | RHI::AshResourceState::SRVGraphics;
		}
		return state;
	}

	const char* render_graph_access_name(RenderGraphAccess access)
	{
		switch (access)
		{
		case RenderGraphAccess::GraphicsSRV:
			return "GraphicsSRV";
		case RenderGraphAccess::ComputeSRV:
			return "ComputeSRV";
		case RenderGraphAccess::GraphicsUAV:
			return "GraphicsUAV";
		case RenderGraphAccess::ComputeUAV:
			return "ComputeUAV";
		case RenderGraphAccess::ColorAttachmentWrite:
			return "ColorAttachmentWrite";
		case RenderGraphAccess::DepthStencilWrite:
			return "DepthStencilWrite";
		case RenderGraphAccess::DepthStencilRead:
			return "DepthStencilRead";
		case RenderGraphAccess::VertexBufferRead:
			return "VertexBufferRead";
		case RenderGraphAccess::IndexBufferRead:
			return "IndexBufferRead";
		case RenderGraphAccess::ConstantBufferRead:
			return "ConstantBufferRead";
		case RenderGraphAccess::CopySrc:
			return "CopySrc";
		case RenderGraphAccess::CopyDst:
			return "CopyDst";
		case RenderGraphAccess::Present:
			return "Present";
		case RenderGraphAccess::IndirectArgs:
			return "IndirectArgs";
		case RenderGraphAccess::Unknown:
		default:
			return "Unknown";
		}
	}
}
