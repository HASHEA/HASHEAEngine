////////////////////////////////////////////////////////////////////////////////
//
//  FileName    : IKGFX_RenderCommandBind.h
//  Creator     : HuaFei
//  Create Date : 2024-12
//
////////////////////////////////////////////////////////////////////////////////

#pragma once

namespace gfx
{
	enum class ERenderCommandBind : int
    {
        Invalid = 0,

		IndexBuffer,
        VertexBuffer,
        Sampler,
		Texture,

        End
    };

	class KRenderCommandBind
	{
    public:
        KRenderCommandBind() = delete;
        KRenderCommandBind(ERenderCommandBind eType);
        virtual ~KRenderCommandBind() = default;

	private:
        ERenderCommandBind m_eRCB_Type{ERenderCommandBind::Invalid};

	public:
        ERenderCommandBind RCB_GetType() const { return m_eRCB_Type; }
    };
} // namespace gfx
