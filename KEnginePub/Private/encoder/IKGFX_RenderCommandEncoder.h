////////////////////////////////////////////////////////////////////////////////
//
//  FileName    : IKGFX_RenderCommandEncoder.h
//  Creator     : HuaFei
//  Create Date : 2024-12
//
////////////////////////////////////////////////////////////////////////////////

#pragma once


namespace gfx
{
	class KGFX_RenderCommandEncoder
	{
    public:
        KGFX_RenderCommandEncoder() = default;
        ~KGFX_RenderCommandEncoder();

    private:
        //

    private:
		//

    private:
        bool m_bInitialized{false};
        bool m_bWriting{true};

    public:
        bool Init();
        void UnInit();
	};
} // namespace gfx
