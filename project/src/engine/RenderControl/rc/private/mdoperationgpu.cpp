/*
    filename:       mdoperationgpu.cpp
    author:         Ming Dong
    date:           2016-APR-22
    description:    
*/

#include "../public/mdoperationgpu.h"
#include "mdoperationgpu_gpuutil.h"
#include "../public/mdeffect.h"
#include "../public/rceffect.h"
#include "../public/rceffectmanager.h"
#include "mdexecuter.h"

DOME_NAMESPACE_BEGIN

MDOperationGpu::MDOperationGpu(MDEffect* i_pMDEffect, const MDOperatorGpu* i_pOperator,
    Int i_ParamCount, const MDOperandPtr* i_ParamArray,
    const MDOperationGpu_MetaInfo* i_pMetaInfo)
    : MDOperation(i_pMDEffect)
{
    DResult l_Result;
    m_pOperator = i_pOperator;
    m_ParamCount = i_ParamCount;
    m_ParamArray = (m_ParamCount == 0) ? DM_NULL : DOME_NewArray(MDOperationGpu_Parameter, m_ParamCount);
    m_ParamOperandArray = (m_ParamCount == 0) ? DM_NULL : DOME_NewArray(MDOperandPtr, m_ParamCount);
    for (Int i = 0; i < m_ParamCount; ++i)
    {
        m_ParamArray[i].m_pOperand = i_ParamArray[i];
        m_ParamOperandArray[i] = m_ParamArray[i].m_pOperand;
        if (m_ParamArray[i].m_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)m_ParamArray[i].m_pOperand;
            l_pOperation->addRefCount();
            m_ParamArray[i].m_bOperation = DM_TRUE;
        }
        else
            m_ParamArray[i].m_bOperation = DM_FALSE;
        m_ParamArray[i].m_CanBeMerged = DM_FALSE;
        m_ParamArray[i].m_bMerged = DM_FALSE;
    }
    m_pMetaInfo = i_pMetaInfo;

    DOME_ASSERT(m_pOperator->getOutputCount() == 1);
    m_TexResultFmt = m_pOperator->getOutputTexFmt(0, m_pMDEffect, m_ParamCount, m_ParamOperandArray);

    l_Result = m_pOperator->calcOutputTexSize(0, m_pMDEffect, m_ResultSize, m_ParamCount, m_ParamOperandArray);
    DOME_ASSERT(DM_SUCC(l_Result));
 
    for (Int i = 0; i < m_ParamCount; ++i)
    {
        if (m_ParamArray[i].m_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)m_ParamArray[i].m_pOperand;
            m_ParamArray[i].m_MustBeMerged = DM_FALSE;
            if (l_pOperation->isGpu())
            {
                m_ParamArray[i].m_MustBeMerged = ((MDOperationGpu*)l_pOperation)->mustBeMerged();
            }
            m_ParamArray[i].m_CanBeMerged = canMergeOperation(i, l_pOperation);
            if(!m_ParamArray[i].m_CanBeMerged)
                l_pOperation->setStandalone();
        }
    }

    m_pResult = DM_NULL;

    initCompiledData();
}

MDOperationGpu::MDOperationGpu(MDEffect* i_pMDEffect, const MDOperatorGpu* i_pOperator,
    Int i_ParamCount, const MDOperandPtr* i_ParamArray,
    Int i_Width, Int i_Height,
    const MDOperationGpu_MetaInfo* i_pMetaInfo)
    : MDOperation(i_pMDEffect)
{
    m_pOperator = i_pOperator;
    m_ParamCount = i_ParamCount;
    m_ParamArray = (m_ParamCount == 0) ? DM_NULL : DOME_NewArray(MDOperationGpu_Parameter, m_ParamCount);
    m_ParamOperandArray = (m_ParamCount == 0) ? DM_NULL : DOME_NewArray(MDOperandPtr, m_ParamCount);
    for (Int i = 0; i < m_ParamCount; ++i)
    {
        m_ParamArray[i].m_pOperand = i_ParamArray[i];
        m_ParamOperandArray[i] = m_ParamArray[i].m_pOperand;
        if (m_ParamArray[i].m_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)m_ParamArray[i].m_pOperand;
            l_pOperation->addRefCount();
            m_ParamArray[i].m_bOperation = DM_TRUE;
        }
        else
            m_ParamArray[i].m_bOperation = DM_FALSE;
        m_ParamArray[i].m_CanBeMerged = DM_FALSE;
        m_ParamArray[i].m_bMerged = DM_FALSE;
    }
    m_pMetaInfo = i_pMetaInfo;

    DOME_ASSERT(m_pOperator->getOutputCount() == 1);
    m_TexResultFmt = m_pOperator->getOutputTexFmt(0, m_pMDEffect, m_ParamCount, m_ParamOperandArray);
    // the size is decided by the parameter
    m_ResultSize.x = i_Width;
    m_ResultSize.y = i_Height;
 
    for (Int i = 0; i < m_ParamCount; ++i)
    {
        if (m_ParamArray[i].m_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)m_ParamArray[i].m_pOperand;
            m_ParamArray[i].m_MustBeMerged = DM_FALSE;
            if (l_pOperation->isGpu())
            {
                m_ParamArray[i].m_MustBeMerged = ((MDOperationGpu*)l_pOperation)->mustBeMerged();
            }
            m_ParamArray[i].m_CanBeMerged = canMergeOperation(i, l_pOperation);
            if(!m_ParamArray[i].m_CanBeMerged)
                l_pOperation->setStandalone();
        }
    }

    m_pResult = DM_NULL;

    initCompiledData();
}

MDOperationGpu::MDOperationGpu(MDEffect* i_pMDEffect, const MDOperatorGpu* i_pOperator,
    Int i_ParamCount, const MDOperandPtr* i_ParamArray,
    RCGPUDATAFORMAT i_Format,
    const MDOperationGpu_MetaInfo* i_pMetaInfo)
    : MDOperation(i_pMDEffect)
{
    DResult l_Result;
    m_pOperator = i_pOperator;
    m_ParamCount = i_ParamCount;
    m_ParamArray = (m_ParamCount == 0) ? DM_NULL : DOME_NewArray(MDOperationGpu_Parameter, m_ParamCount);
    m_ParamOperandArray = (m_ParamCount == 0) ? DM_NULL : DOME_NewArray(MDOperandPtr, m_ParamCount);
    for (Int i = 0; i < m_ParamCount; ++i)
    {
        m_ParamArray[i].m_pOperand = i_ParamArray[i];
        m_ParamOperandArray[i] = m_ParamArray[i].m_pOperand;
        if (m_ParamArray[i].m_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)m_ParamArray[i].m_pOperand;
            l_pOperation->addRefCount();
            m_ParamArray[i].m_bOperation = DM_TRUE;
        }
        else
            m_ParamArray[i].m_bOperation = DM_FALSE;
        m_ParamArray[i].m_CanBeMerged = DM_FALSE;
        m_ParamArray[i].m_bMerged = DM_FALSE;
    }
    m_pMetaInfo = i_pMetaInfo;

    DOME_ASSERT(m_pOperator->getOutputCount() == 1);
    m_TexResultFmt = i_Format;

    l_Result = m_pOperator->calcOutputTexSize(0, m_pMDEffect, m_ResultSize, m_ParamCount, m_ParamOperandArray);
    DOME_ASSERT(DM_SUCC(l_Result));

    for (Int i = 0; i < m_ParamCount; ++i)
    {
        if (m_ParamArray[i].m_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)m_ParamArray[i].m_pOperand;
            m_ParamArray[i].m_MustBeMerged = DM_FALSE;
            if (l_pOperation->isGpu())
            {
                m_ParamArray[i].m_MustBeMerged = ((MDOperationGpu*)l_pOperation)->mustBeMerged();
            }
            m_ParamArray[i].m_CanBeMerged = canMergeOperation(i, l_pOperation);
            if (!m_ParamArray[i].m_CanBeMerged)
                l_pOperation->setStandalone();
        }
    }

    m_pResult = DM_NULL;

    initCompiledData();
}

MDOperationGpu::MDOperationGpu(MDEffect* i_pMDEffect, const MDOperatorGpu* i_pOperator,
    Int i_ParamCount, const MDOperandPtr* i_ParamArray,
    Int i_Width, Int i_Height, RCGPUDATAFORMAT i_Format,
    const MDOperationGpu_MetaInfo* i_pMetaInfo)
    : MDOperation(i_pMDEffect)
{
    m_pOperator = i_pOperator;
    m_ParamCount = i_ParamCount;
    m_ParamArray = (m_ParamCount == 0) ? DM_NULL : DOME_NewArray(MDOperationGpu_Parameter, m_ParamCount);
    m_ParamOperandArray = (m_ParamCount == 0) ? DM_NULL : DOME_NewArray(MDOperandPtr, m_ParamCount);
    for (Int i = 0; i < m_ParamCount; ++i)
    {
        m_ParamArray[i].m_pOperand = i_ParamArray[i];
        m_ParamOperandArray[i] = m_ParamArray[i].m_pOperand;
        if (m_ParamArray[i].m_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)m_ParamArray[i].m_pOperand;
            l_pOperation->addRefCount();
            m_ParamArray[i].m_bOperation = DM_TRUE;
        }
        else
            m_ParamArray[i].m_bOperation = DM_FALSE;
        m_ParamArray[i].m_CanBeMerged = DM_FALSE;
        m_ParamArray[i].m_bMerged = DM_FALSE;
    }
    m_pMetaInfo = i_pMetaInfo;

    DOME_ASSERT(m_pOperator->getOutputCount() == 1);
    m_TexResultFmt = i_Format;
    // the size is decided by the parameter
    m_ResultSize.x = i_Width;
    m_ResultSize.y = i_Height;

    for (Int i = 0; i < m_ParamCount; ++i)
    {
        if (m_ParamArray[i].m_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)m_ParamArray[i].m_pOperand;
            m_ParamArray[i].m_MustBeMerged = DM_FALSE;
            if (l_pOperation->isGpu())
            {
                m_ParamArray[i].m_MustBeMerged = ((MDOperationGpu*)l_pOperation)->mustBeMerged();
            }
            m_ParamArray[i].m_CanBeMerged = canMergeOperation(i, l_pOperation);
            if (!m_ParamArray[i].m_CanBeMerged)
                l_pOperation->setStandalone();
        }
    }

    m_pResult = DM_NULL;

    initCompiledData();
}



MDOperationGpu::~MDOperationGpu()
{
    DOME_ERROR2(!m_pResult, "ERROR: The result of cpu operation is not released!");

    uninitCompiledData();

    if (m_ParamArray)
    {
        DOME_DelArray(m_ParamArray);
        m_ParamArray = DM_NULL;
    }

    if (m_ParamOperandArray)
    {
        DOME_DelArray(m_ParamOperandArray);
        m_ParamOperandArray = DM_NULL;
    }
}

Bool                        MDOperationGpu::isOperation() const
{
    return DM_TRUE;
}

Int                         MDOperationGpu::getDataCount() const
{
    return m_pOperator->getOutputCount();
}

DSimpleTypeID               MDOperationGpu::getDataType(Int i_Index) const
{
    return m_pOperator->getOutputTypeID(i_Index, m_pMDEffect, m_ParamCount, m_ParamOperandArray);
}

const MDOperand*            MDOperationGpu::getSubOperand(Int i_Index) const
{
    DOME_ASSERT(getDataCount() == 1);
    return DM_NULL;
}

MDOperand*                  MDOperationGpu::getSubOperand(Int i_Index)
{
    DOME_ASSERT(getDataCount() == 1);
    return DM_NULL;
}

const DSimpleTypedValue*    MDOperationGpu::getDataPtr() const
{
    DOME_ASSERT(getDataCount() == 1);
    if (!m_pResult)
        return DM_NULL;

    return m_pResult->getDataPtr();
}

DSimpleTypedValue*          MDOperationGpu::getDataPtr()
{
    DOME_ASSERT(getDataCount() == 1);
    if (!m_pResult)
        return DM_NULL;

    return m_pResult->getDataPtr();
}

RCGPUDATAFORMAT             MDOperationGpu::getTexDataFmt(Int i_Index) const
{
    return m_TexResultFmt;
}

DVector2i                   MDOperationGpu::getTexDataSize(Int i_Index) const
{
    return m_ResultSize;
}


Bool                        MDOperationGpu::isGpu() const
{
    return DM_TRUE;
}

DResult                     MDOperationGpu::execute()
{
    if (mustBeMerged())
    {
        // the gpu operation should be merged,
        // it it doesn't be merged, there may be a precision problem.
        DOME_WARNING2(0, "WARNING: The gpu operation should be merged!");
    }

    if(m_pResult)
        return R_SUCCESS;

    GpuUtil::GpuCompileResult& l_CompileResult = m_pCompiledData->m_CompiledResult;

    for (Int i = 0; i < l_CompileResult.m_TextureArray.size(); ++i)
    {
        const MDOperand* l_pOperand = l_CompileResult.m_TextureArray[i];
        if (l_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)l_pOperand;
            l_pOperation->execute();
        }
    }

    for (Int i = 0; i < l_CompileResult.m_Texture3DArray.size(); ++i)
    {
        const MDOperand* l_pOperand = l_CompileResult.m_Texture3DArray[i];
        if (l_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)l_pOperand;
            l_pOperation->execute();
        }
    }

    for (Int i = 0; i < l_CompileResult.m_TextureCubeArray.size(); ++i)
    {
        const MDOperand* l_pOperand = l_CompileResult.m_TextureCubeArray[i];
        if (l_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)l_pOperand;
            l_pOperation->execute();
        }
    }

    for (Int i = 0; i < l_CompileResult.m_Matrix4x4Array.size(); ++i)
    {
        const MDOperand* l_pOperand = l_CompileResult.m_Matrix4x4Array[i];
        if (l_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)l_pOperand;
            l_pOperation->execute();
        }
    }

    for (Int i = 0; i < l_CompileResult.m_Matrix3x3Array.size(); ++i)
    {
        const MDOperand* l_pOperand = l_CompileResult.m_Matrix3x3Array[i];
        if (l_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)l_pOperand;
            l_pOperation->execute();
        }
    }

    for (Int i = 0; i < l_CompileResult.m_Matrix2x2Array.size(); ++i)
    {
        const MDOperand* l_pOperand = l_CompileResult.m_Matrix2x2Array[i];
        if (l_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)l_pOperand;
            l_pOperation->execute();
        }
    }

    for (Int i = 0; i < l_CompileResult.m_Float4Array.size(); ++i)
    {
        const MDOperand* l_pOperand = l_CompileResult.m_Float4Array[i];
        if (l_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)l_pOperand;
            l_pOperation->execute();
        }
    }

    for (Int i = 0; i < l_CompileResult.m_Float3Array.size(); ++i)
    {
        const MDOperand* l_pOperand = l_CompileResult.m_Float3Array[i];
        if (l_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)l_pOperand;
            l_pOperation->execute();
        }
    }

    for (Int i = 0; i < l_CompileResult.m_Float2Array.size(); ++i)
    {
        const MDOperand* l_pOperand = l_CompileResult.m_Float2Array[i];
        if (l_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)l_pOperand;
            l_pOperation->execute();
        }
    }

    for (Int i = 0; i < l_CompileResult.m_Float1Array.size(); ++i)
    {
        const MDOperand* l_pOperand = l_CompileResult.m_Float1Array[i];
        if (l_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)l_pOperand;
            l_pOperation->execute();
        }
    }


    MDExecuter* l_pExecuter = m_pMDEffect->getRCEffect()->getEffectManager()->getMDExecuter(DHashString(l_CompileResult.m_ShaderSignature.c_str()));
    if (!l_pExecuter)
    {
        return R_FAILED;
    }

    m_pResult = m_pOperator->createRenderTarget(m_pMDEffect, m_ResultSize, m_TexResultFmt);
    DOME_ASSERT(m_pResult);

    l_pExecuter->setRenderTarget(*m_pResult->getTexturePtr());
    l_pExecuter->setRenderTargetViewport(0, 0, m_ResultSize.x, m_ResultSize.y);
    l_pExecuter->setUVCoef(DVector4f(1.0f, 1.0f, 0.0f, 0.0f));

    DOME_ASSERT(l_CompileResult.m_TextureArray.size() == l_pExecuter->getNumTextureParam());
    for (Int i = 0; i < l_pExecuter->getNumTextureParam(); ++i)
    {
        l_pExecuter->setTextureParam(i, *(l_CompileResult.m_TextureArray[i]->getTexturePtr()));
    }

    DOME_ASSERT(l_CompileResult.m_Texture3DArray.size() == l_pExecuter->getNumTexture3DParam());
    for (Int i = 0; i < l_pExecuter->getNumTexture3DParam(); ++i)
    {
        l_pExecuter->setTexture3DParam(i, *(l_CompileResult.m_Texture3DArray[i]->getTexture3DPtr()));
    }

    DOME_ASSERT(l_CompileResult.m_TextureCubeArray.size() == l_pExecuter->getNumTextureCubeParam());
    for (Int i = 0; i < l_pExecuter->getNumTextureCubeParam(); ++i)
    {
        l_pExecuter->setTextureCubeParam(i, *(l_CompileResult.m_TextureCubeArray[i]->getTextureCubePtr()));
    }

    DOME_ASSERT(l_CompileResult.m_Matrix4x4Array.size() == l_pExecuter->getNumMatrix4x4Param());
    for (Int i = 0; i < l_pExecuter->getNumMatrix4x4Param(); ++i)
    {
        l_pExecuter->setMatrix4x4Param(i, *(l_CompileResult.m_Matrix4x4Array[i]->getMatrix4x4Ptr()));
    }

    DOME_ASSERT(l_CompileResult.m_Matrix3x3Array.size() == l_pExecuter->getNumMatrix3x3Param());
    for (Int i = 0; i < l_pExecuter->getNumMatrix3x3Param(); ++i)
    {
        l_pExecuter->setMatrix3x3Param(i, *(l_CompileResult.m_Matrix3x3Array[i]->getMatrix3x3Ptr()));
    }

    DOME_ASSERT(l_CompileResult.m_Matrix2x2Array.size() == l_pExecuter->getNumMatrix2x2Param());
    for (Int i = 0; i < l_pExecuter->getNumMatrix2x2Param(); ++i)
    {
        l_pExecuter->setMatrix2x2Param(i, *(l_CompileResult.m_Matrix2x2Array[i]->getMatrix2x2Ptr()));
    }

    DOME_ASSERT(l_CompileResult.m_Float4Array.size() == l_pExecuter->getNumFloat4Param());
    for (Int i = 0; i < l_pExecuter->getNumFloat4Param(); ++i)
    {
        l_pExecuter->setFloat4Param(i, *(l_CompileResult.m_Float4Array[i]->getFloat4Ptr()));
    }

    DOME_ASSERT(l_CompileResult.m_Float3Array.size() == l_pExecuter->getNumFloat3Param());
    for (Int i = 0; i < l_pExecuter->getNumFloat3Param(); ++i)
    {
        l_pExecuter->setFloat3Param(i, *(l_CompileResult.m_Float3Array[i]->getFloat3Ptr()));
    }

    DOME_ASSERT(l_CompileResult.m_Float2Array.size() == l_pExecuter->getNumFloat2Param());
    for (Int i = 0; i < l_pExecuter->getNumFloat2Param(); ++i)
    {
        l_pExecuter->setFloat2Param(i, *(l_CompileResult.m_Float2Array[i]->getFloat2Ptr()));
    }

    DOME_ASSERT(l_CompileResult.m_Float1Array.size() == l_pExecuter->getNumFloat1Param());
    for (Int i = 0; i < l_pExecuter->getNumFloat1Param(); ++i)
    {
        l_pExecuter->setFloat1Param(i, *(l_CompileResult.m_Float1Array[i]->getFloatPtr()));
    }

    DResult l_Result = l_pExecuter->execute();
    DOME_ASSERT(DM_SUCC(l_Result));

    for (Int i = 0; i < l_CompileResult.m_TextureArray.size(); ++i)
    {
        const MDOperand* l_pOperand = l_CompileResult.m_TextureArray[i];
        if (l_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)l_pOperand;
            l_pOperation->finishCallback();
        }
    }

    for (Int i = 0; i < l_CompileResult.m_Texture3DArray.size(); ++i)
    {
        const MDOperand* l_pOperand = l_CompileResult.m_Texture3DArray[i];
        if (l_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)l_pOperand;
            l_pOperation->finishCallback();
        }
    }

    for (Int i = 0; i < l_CompileResult.m_TextureCubeArray.size(); ++i)
    {
        const MDOperand* l_pOperand = l_CompileResult.m_TextureCubeArray[i];
        if (l_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)l_pOperand;
            l_pOperation->finishCallback();
        }
    }

    for (Int i = 0; i < l_CompileResult.m_Matrix4x4Array.size(); ++i)
    {
        const MDOperand* l_pOperand = l_CompileResult.m_Matrix4x4Array[i];
        if (l_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)l_pOperand;
            l_pOperation->finishCallback();
        }
    }

    for (Int i = 0; i < l_CompileResult.m_Matrix3x3Array.size(); ++i)
    {
        const MDOperand* l_pOperand = l_CompileResult.m_Matrix3x3Array[i];
        if (l_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)l_pOperand;
            l_pOperation->finishCallback();
        }
    }

    for (Int i = 0; i < l_CompileResult.m_Matrix2x2Array.size(); ++i)
    {
        const MDOperand* l_pOperand = l_CompileResult.m_Matrix2x2Array[i];
        if (l_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)l_pOperand;
            l_pOperation->finishCallback();
        }
    }

    for (Int i = 0; i < l_CompileResult.m_Float4Array.size(); ++i)
    {
        const MDOperand* l_pOperand = l_CompileResult.m_Float4Array[i];
        if (l_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)l_pOperand;
            l_pOperation->finishCallback();
        }
    }

    for (Int i = 0; i < l_CompileResult.m_Float3Array.size(); ++i)
    {
        const MDOperand* l_pOperand = l_CompileResult.m_Float3Array[i];
        if (l_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)l_pOperand;
            l_pOperation->finishCallback();
        }
    }

    for (Int i = 0; i < l_CompileResult.m_Float2Array.size(); ++i)
    {
        const MDOperand* l_pOperand = l_CompileResult.m_Float2Array[i];
        if (l_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)l_pOperand;
            l_pOperation->finishCallback();
        }
    }

    for (Int i = 0; i < l_CompileResult.m_Float1Array.size(); ++i)
    {
        const MDOperand* l_pOperand = l_CompileResult.m_Float1Array[i];
        if (l_pOperand->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)l_pOperand;
            l_pOperation->finishCallback();
        }
    }

    return R_SUCCESS;

}

DResult                     MDOperationGpu::finishCallback()
{
    DOME_ASSERT(getUsedCount() > 0);
    DOME_ASSERT(m_pResult);

    subUsedCount();
    if (getUsedCount() == 0)
    {
        DResult l_Result = m_pOperator->destroyRenderTarget(m_pMDEffect, m_pResult);
        DOME_ASSERT(DM_SUCC(l_Result));
        m_pResult = DM_NULL;
        return R_SUCCESS;
    }
    return R_SUCCESS;
}



DOME_NAMESPACE_END