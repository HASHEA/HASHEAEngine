////////////////////////////////////////////////////////////////////////////////
//
//  FileName    : KGFX_RenderCommand.cpp
//  Creator     : HuaFei
//  Create Date : 2024-12
//
////////////////////////////////////////////////////////////////////////////////

#include "KBase/Public/core_base_macro.h"
#include "IKGFX_RenderCommand.h"

namespace gfx
{
    //////////////////////////////////////////////////////////////////////////
    KECS_OBJECT_IMPLEMENT(KRenderCommand);

    void KRenderCommand::OnConstruct()
    {
    }

    void KRenderCommand::OnRelease()
    {
    }

    //////////////////////////////////////////////////////////////////////////
    KECS_OBJECT_IMPLEMENT(KRenderCommandDraw);
    void KRenderCommandDraw::OnConstruct()
    {
    }

    void KRenderCommandDraw::OnRelease()
    {
    }

    //////////////////////////////////////////////////////////////////////////
    KECS_OBJECT_IMPLEMENT(KRenderCommandDrawIndexed);
    void KRenderCommandDrawIndexed::OnConstruct()
    {
    }

    void KRenderCommandDrawIndexed::OnRelease()
    {
    }

    //////////////////////////////////////////////////////////////////////////
    KECS_OBJECT_IMPLEMENT(KRenderCommandDrawIndirect);
    void KRenderCommandDrawIndirect::OnConstruct()
    {
    }

    void KRenderCommandDrawIndirect::OnRelease()
    {
    }

    //////////////////////////////////////////////////////////////////////////
    KECS_OBJECT_IMPLEMENT(KRenderCommandDrawIndexedIndirect);
    void KRenderCommandDrawIndexedIndirect::OnConstruct()
    {
    }

    void KRenderCommandDrawIndexedIndirect::OnRelease()
    {
    }

    //////////////////////////////////////////////////////////////////////////
    KECS_OBJECT_IMPLEMENT(KRenderCommandDispatch);
    void KRenderCommandDispatch::OnConstruct()
    {
    }

    void KRenderCommandDispatch::OnRelease()
    {
    }

    //////////////////////////////////////////////////////////////////////////
    KECS_OBJECT_IMPLEMENT(KRenderCommandDispatchIndirect);
    void KRenderCommandDispatchIndirect::OnConstruct()
    {
    }

    void KRenderCommandDispatchIndirect::OnRelease()
    {
    }
} // namespace gfx
