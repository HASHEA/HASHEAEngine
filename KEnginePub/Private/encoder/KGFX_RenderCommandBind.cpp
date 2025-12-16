////////////////////////////////////////////////////////////////////////////////
//
//  FileName    : KGFX_RenderCommandBind.cpp
//  Creator     : HuaFei
//  Create Date : 2024-12
//
////////////////////////////////////////////////////////////////////////////////

#include "KBase/Public/core_base_macro.h"
#include "IKGFX_RenderCommandBind.h"

namespace gfx
{
    KRenderCommandBind::KRenderCommandBind(ERenderCommandBind eType)
    {
        ASSERT(eType > ERenderCommandBind::Invalid && eType < ERenderCommandBind::End);

        m_eRCB_Type = eType;
    }
} // namespace gfx
