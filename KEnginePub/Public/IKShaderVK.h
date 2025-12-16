#pragma once
#include "KEnginePub/Public/IKHeader.h"
#include "KEnginePub/Public/IKLoadable.h"
#include "KEnginePub/Public/IGFX_Public.h"

namespace gfx
{
    class IKGFX_RenderContext;
    class IKGFX_BufferView;
}

struct KGlobalUBOBase
{    
    virtual ~KGlobalUBOBase() {};
    virtual gfx::IKGFX_BufferView* GetGfxBuffer() = 0;
    virtual gfx::IKGFX_BufferView* GetGfxBuffer(int index) = 0;
    virtual BOOL Update(gfx::IKGFX_RenderContext* pContext, const void* pData, BOOL bForce = FALSE) = 0;
    virtual int GetUpdateFrame() = 0;
};


