////////////////////////////////////////////////////////////////////////////////
//
//  FileName    : IKGFX_RenderAction.h
//  Creator     : HuaFei
//  Create Date : 2024-12
//
////////////////////////////////////////////////////////////////////////////////

#pragma once

namespace gfx
{
    enum class ERenderAction : int
    {
        Invalid = 0,
        RendererInit,
        RendererShutdownBegin,
     
		PreDraw,
        CreateVertexLayout,
        CreateIndexBuffer,
        CreateVertexBuffer,
        CreateDynamicIndexBuffer,
        UpdateDynamicIndexBuffer,
        CreateDynamicVertexBuffer,
        UpdateDynamicVertexBuffer,
        CreateShader,
        CreateProgram,
        CreateTexture,
        UpdateTexture,
        ResizeTexture,
        CreateFrameBuffer,
        CreateUniform,
        UpdateViewName,
        InvalidateOcclusionQuery,
        SetName,

        PostDraw,
        RendererShutdownEnd,
        DestroyVertexLayout,
        DestroyIndexBuffer,
        DestroyVertexBuffer,
        DestroyDynamicIndexBuffer,
        DestroyDynamicVertexBuffer,
        DestroyShader,
        DestroyProgram,
        DestroyTexture,
        DestroyFrameBuffer,
        DestroyUniform,
        ReadTexture,

		End
    };

    struct alignas(4) tagRenderAction
    {
        ERenderAction m_eRA_Type{ERenderAction::Invalid};
        int           m_nRA_Size{0};
    };

	// Keep tagRenderAction POD-like and compact; some external code paths serialize it directly.
	//

} // namespace gfx
