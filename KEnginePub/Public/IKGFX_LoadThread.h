#pragma once
#include "IGFX_Public.h"

namespace gfx
{
    class IKGFX_GraphicsProgram;
};

class IKGFX_MaterialLoadThread
{
public:
    IKGFX_MaterialLoadThread() {};
    virtual ~IKGFX_MaterialLoadThread() {};
    virtual BOOL PushKGFXShaderLoadTask(gfx::IKGFX_Program* pKGFXShader, uint32_t dwOption, KEnumMtlTaskLevel uThreadLevel) = 0;
    virtual void FrameMove() = 0;
};



namespace gfx
{    
    struct GraphicsPipelineDesc;
};

class IKGFX_PipelineLoadThread: public IKEngineThreadCall
{
public:
    IKGFX_PipelineLoadThread() {};
    virtual ~IKGFX_PipelineLoadThread() {};
    virtual BOOL PushLoadTask(IKShaderResource* pShaderResource, IKPipeLineData* pPipeLine, IKGraphicsPipelineDesc* pDesc, KEnumMtlTaskLevel uThreadLevel) = 0;
    virtual BOOL IsAllowToPush() = 0;
    virtual void FrameMove() = 0;
};
