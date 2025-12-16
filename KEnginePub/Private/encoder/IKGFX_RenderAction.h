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

	// Fatal: tagRenderActon 子类成员都得是普通数据类型，不能含有模板容器等，才能满足二进制读写
	//

} // namespace gfx
