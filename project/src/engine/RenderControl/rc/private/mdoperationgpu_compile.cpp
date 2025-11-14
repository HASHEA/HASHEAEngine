/*
    filename:       mdoperationgpu_compile.cpp
    author:         Ming Dong
    date:           2016-APR-28
    description:    
*/

#include "../public/mdoperationgpu.h"
#include "mdoperationgpu_gpuutil.h"

DOME_NAMESPACE_BEGIN

/******************************************************************************
    util functions for gpu operation compiling BEGIN
******************************************************************************/
void                        MDOperationGpu::initCompiledData()
{
    m_pCompiledData = DOME_New(GpuCompileData);
}

void                        MDOperationGpu::uninitCompiledData()
{
    DOME_ASSERT (m_pCompiledData);
    {
        DOME_Del(m_pCompiledData);
        m_pCompiledData = DM_NULL;
    }
}

Bool                        MDOperationGpu::mustBeMerged() const
{
    return m_pOperator->mustBeMerged();
}

Bool                        MDOperationGpu::canMergeOperation(Int i_Index, const MDOperation* i_pOperation) const
{
    if(!m_pOperator->canMergeInput(i_Index))
        return DM_FALSE;

    if(!i_pOperation->isGpu())
        return DM_FALSE;

    DResult l_Result = R_SUCCESS;
    const MDOperationGpu* l_pGpuOperation = (const MDOperationGpu*)i_pOperation;

    // if the render target size is not same, can't merge
    DVector2i l_RTSize0 = l_pGpuOperation->getTexDataSize(0);
    DVector2i l_RTSize1 = getTexDataSize(0);
    if(l_RTSize0.x != l_RTSize1.x || l_RTSize0.y != l_RTSize1.y)
        return DM_FALSE;

    // if the meta information is not NULL, can't merge
    if(m_pMetaInfo || l_pGpuOperation->m_pMetaInfo)
        return DM_FALSE;

    return DM_TRUE;
}

Bool                        MDOperationGpu::shouldMergeOperation(Int i_Index, const MDOperation* i_pOperation) const
{
    DOME_ASSERT(i_pOperation->isGpu());
    MDOperationGpu* l_pGpuOperation = (MDOperationGpu*)i_pOperation;

    if (i_pOperation->isStandalone())
    {
        GpuCompileData* l_pCompiledData = l_pGpuOperation->getCompileResult();
        if(l_pCompiledData->m_GpuOperation.m_Complexity >= 30)
            return DM_FALSE;
        else
            return DM_TRUE;
    }
    else
    {
        GpuCompileData* l_pCompiledData = l_pGpuOperation->getCompileResult();
        Int l_RefCount = l_pGpuOperation->getRefCount();
        if(l_RefCount == 1)
            return DM_TRUE;
        if((l_RefCount * l_pCompiledData->m_GpuOperation.m_Complexity) > 200)
            return DM_FALSE;
        else
            return DM_TRUE;
    }
}

DResult                     MDOperationGpu::buildGpuOperation(GpuUtil::GpuOperation& o_GpuOperation) const
{
    o_GpuOperation.m_pOperation = this;
    o_GpuOperation.m_Complexity = m_pOperator->getComplexity();

    for (Int i = 0; i < m_ParamCount; ++i)
    {
        if (m_ParamArray[i].m_bMerged)
        {
            MDOperationGpu* l_pOperation = (MDOperationGpu*)m_ParamArray[i].m_pOperand;
            o_GpuOperation.m_OperandArray.push_back(&l_pOperation->getCompileResult()->m_GpuOperation);
            o_GpuOperation.m_Complexity += l_pOperation->getCompileResult()->m_GpuOperation.m_Complexity;
        }
        else
        {
            const MDOperand* l_pOperand = m_ParamArray[i].m_pOperand;
            GpuUtil::GpuOperand* l_pGpuOperand = DOME_New(GpuUtil::GpuOperand);
            l_pGpuOperand->m_pOperand = m_ParamArray[i].m_pOperand;
            o_GpuOperation.m_OperandArray.push_back(l_pGpuOperand);
        }
    }
    return R_SUCCESS;
}

DResult                     MDOperationGpu::tryMerge(Int& o_nTexture, Int& o_nTexture3D, Int& o_nTextureCube, Int & o_nFloat4) const
{
    GpuUtil::GpuOperation l_GpuOperation;
    buildGpuOperation(l_GpuOperation);

    GpuUtil::GpuResourceContext l_Context;
    DResult l_Result = l_GpuOperation.calcResourceNeeded(&l_Context);
    if(DM_FAIL(l_Result))
        return l_Result;
    else
    {
        o_nTexture = l_Context.m_NumTextureUsed;
        o_nTexture3D = l_Context.m_NumTexture3DUsed;
        o_nTextureCube = l_Context.m_NumTextureCubeUsed;
        o_nFloat4 = l_Context.m_NumMatrix4x4Used * 4 + 
                    l_Context.m_NumMatrix3x3Used * 3 + 
                    l_Context.m_NumMatrix2x2Used * 2 + 
                    l_Context.m_NumFloat4Used + 
                    l_Context.m_NumFloat3Used + 
                    (l_Context.m_NumFloat2Used + 1) / 2 + 
                    (l_Context.m_NumFloat1Used + 3) / 4;
        return l_Result;
    }

}

MDOperationGpu::GpuCompileData* MDOperationGpu::getCompileResult()
{
    return m_pCompiledData;
}

const MDOperatorGpu*        MDOperationGpu::getOperator() const
{
    return m_pOperator;
}

DResult                     MDOperationGpu::compile()
{
    if(m_bCompiled)
        return R_SUCCESS;
    m_bCompiled = DM_TRUE;

    DResult l_Result = R_SUCCESS;
    TArray<MDOperationGpu_Parameter*> l_MustMergedOperationArray;
    TArray<MDOperationGpu_Parameter*> l_PotentialMergedOperationArray;

    for (Int i = 0; i < m_ParamCount; ++i)
    {
        if (m_ParamArray[i].m_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)m_ParamArray[i].m_pOperand;
            if(DM_FAIL(l_pOperation->compile()))
                l_Result = R_FAILED;
            if (m_ParamArray[i].m_CanBeMerged && m_ParamArray[i].m_MustBeMerged)
                l_MustMergedOperationArray.push_back(&m_ParamArray[i]);
            else if(m_ParamArray[i].m_CanBeMerged && shouldMergeOperation(i, l_pOperation))
                l_PotentialMergedOperationArray.push_back(&m_ParamArray[i]);
        }
    }

    if(DM_FAIL(l_Result))
        return l_Result;

    Int l_nTexture = 0;
    Int l_nTexture3D = 0;
    Int l_nTextureCube = 0;
    Int l_nFloat4 = 0;
    
    l_Result = tryMerge(l_nTexture, l_nTexture3D, l_nTextureCube, l_nFloat4);
    DOME_ASSERT(DM_SUCC(l_Result));
    DOME_ASSERT(l_nTexture <= k_MaxTexture && l_nTexture3D <= k_MaxTexture3D && l_nTextureCube < k_MaxTextureCube && l_nFloat4 <= k_MaxFloat4);

    for (Int i = 0; i < l_MustMergedOperationArray.size(); ++i)
    {
        l_MustMergedOperationArray[i]->m_bMerged = DM_TRUE;
        l_nTexture = 0;
        l_nTexture3D = 0;
        l_nTextureCube = 0;
        l_nFloat4 = 0;
        l_Result = tryMerge(l_nTexture, l_nTexture3D, l_nTextureCube, l_nFloat4);
        DOME_ASSERT(DM_SUCC(l_Result));
        if (l_nTexture > k_MaxTexture || l_nTexture3D > k_MaxTexture3D || l_nTextureCube > k_MaxTextureCube || l_nFloat4 > k_MaxFloat4)
        {
            l_MustMergedOperationArray[i]->m_bMerged = DM_FALSE;
        }
    }

    for (Int i = 0; i < l_PotentialMergedOperationArray.size(); ++i)
    {
        l_PotentialMergedOperationArray[i]->m_bMerged = DM_TRUE;
        l_nTexture = 0;
        l_nTexture3D = 0;
        l_nTextureCube = 0;
        l_nFloat4 = 0;
        l_Result = tryMerge(l_nTexture, l_nTexture3D, l_nTextureCube, l_nFloat4);
        DOME_ASSERT(DM_SUCC(l_Result));
        if (l_nTexture > k_MaxTexture || l_nTexture3D > k_MaxTexture3D || l_nTextureCube > k_MaxTextureCube || l_nFloat4 > k_MaxFloat4)
        {
            l_PotentialMergedOperationArray[i]->m_bMerged = DM_FALSE;
        }
    }

    buildGpuOperation(m_pCompiledData->m_GpuOperation);

    return m_pCompiledData->m_GpuOperation.compile(m_pCompiledData->m_CompiledResult);
}

DResult                     MDOperationGpu::postCompile()
{
    Int l_UsedCount = addUsedCount();
    if (l_UsedCount > 1)
    {
        return R_SUCCESS;
    }

    GpuUtil::GpuCompileResult& l_CompileResult = m_pCompiledData->m_CompiledResult;

    for (Int i = 0; i < l_CompileResult.m_TextureArray.size(); ++i)
    {
        const MDOperand* l_pOperand = l_CompileResult.m_TextureArray[i];
        if (l_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)l_pOperand;
            l_pOperation->postCompile();
        }
    }

    for (Int i = 0; i < l_CompileResult.m_Texture3DArray.size(); ++i)
    {
        const MDOperand* l_pOperand = l_CompileResult.m_Texture3DArray[i];
        if (l_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)l_pOperand;
            l_pOperation->postCompile();
        }
    }

    for (Int i = 0; i < l_CompileResult.m_TextureCubeArray.size(); ++i)
    {
        const MDOperand* l_pOperand = l_CompileResult.m_TextureCubeArray[i];
        if (l_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)l_pOperand;
            l_pOperation->postCompile();
        }
    }

    for (Int i = 0; i < l_CompileResult.m_Matrix4x4Array.size(); ++i)
    {
        const MDOperand* l_pOperand = l_CompileResult.m_Matrix4x4Array[i];
        if (l_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)l_pOperand;
            l_pOperation->postCompile();
        }
    }

    for (Int i = 0; i < l_CompileResult.m_Matrix3x3Array.size(); ++i)
    {
        const MDOperand* l_pOperand = l_CompileResult.m_Matrix3x3Array[i];
        if (l_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)l_pOperand;
            l_pOperation->postCompile();
        }
    }

    for (Int i = 0; i < l_CompileResult.m_Matrix2x2Array.size(); ++i)
    {
        const MDOperand* l_pOperand = l_CompileResult.m_Matrix2x2Array[i];
        if (l_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)l_pOperand;
            l_pOperation->postCompile();
        }
    }

    for (Int i = 0; i < l_CompileResult.m_Float4Array.size(); ++i)
    {
        const MDOperand* l_pOperand = l_CompileResult.m_Float4Array[i];
        if (l_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)l_pOperand;
            l_pOperation->postCompile();
        }
    }

    for (Int i = 0; i < l_CompileResult.m_Float3Array.size(); ++i)
    {
        const MDOperand* l_pOperand = l_CompileResult.m_Float3Array[i];
        if (l_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)l_pOperand;
            l_pOperation->postCompile();
        }
    }

    for (Int i = 0; i < l_CompileResult.m_Float2Array.size(); ++i)
    {
        const MDOperand* l_pOperand = l_CompileResult.m_Float2Array[i];
        if (l_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)l_pOperand;
            l_pOperation->postCompile();
        }
    }

    for (Int i = 0; i < l_CompileResult.m_Float1Array.size(); ++i)
    {
        const MDOperand* l_pOperand = l_CompileResult.m_Float1Array[i];
        if (l_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)l_pOperand;
            l_pOperation->postCompile();
        }
    }

    return R_SUCCESS;
}
/******************************************************************************
    util functions for gpu operation compiling END
******************************************************************************/




DOME_NAMESPACE_END