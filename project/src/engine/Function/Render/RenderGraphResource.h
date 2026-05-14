#pragma once

#include "Function/Render/RenderDevice.h"
#include "Function/Render/RenderGraphFwd.h"
#include "Graphics/RHIResource.h"
#include <cstdint>
#include <memory>
#include <string>

namespace AshEngine
{
	enum class RenderGraphAccess : uint16_t
	{
		Unknown = 0,
		GraphicsSRV,
		ComputeSRV,
		GraphicsUAV,
		ComputeUAV,
		ColorAttachmentWrite,
		DepthStencilWrite,
		DepthStencilRead,
		VertexBufferRead,
		IndexBufferRead,
		ConstantBufferRead,
		CopySrc,
		CopyDst,
		Present
	};

	enum class RenderGraphDepthReadMode : uint8_t
	{
		DepthTestOnly = 0,
		DepthTestAndShaderResource
	};

	struct RenderGraphTextureDesc
	{
		uint16_t width = 1;
		uint16_t height = 1;
		RenderTextureFormat format = RenderTextureFormat::Unknown;
		bool shader_resource = true;
		bool unordered_access = false;
		bool use_optimized_clear_value = false;
		RenderColorValue optimized_clear_color{};
		RenderDepthStencilValue optimized_clear_depth_stencil{};

		static RenderGraphTextureDesc from_render_target_desc(const RenderTargetDesc& desc);
		RenderTargetDesc to_render_target_desc(const char* name) const;
	};

	RHI::AshResourceState render_graph_access_to_rhi_state(RenderGraphAccess access);
	RHI::AshResourceState render_graph_depth_read_state(RenderGraphDepthReadMode mode);
	const char* render_graph_access_name(RenderGraphAccess access);
}
