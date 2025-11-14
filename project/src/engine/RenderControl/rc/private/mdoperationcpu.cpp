/*
    filename:       mdoperationcpu.cpp
    author:         Ming Dong
    date:           2016-APR-22
    description:    
*/

#include "../public/mdoperationcpu.h"
#include "../public/mdoperatorcpu.h"
#include "../public/rcglobal.h"

DOME_NAMESPACE_BEGIN

MDOperationCpu::MDOperationCpu(MDEffect* i_pMDEffect, const MDOperatorCpu* i_pOperator, Int i_ParamCount, const MDOperandPtr* i_ParamArray)
    : MDOperation(i_pMDEffect)
{
    DResult l_Result;
    m_pOperator = i_pOperator;
    m_ParamCount = i_ParamCount;
    m_ParamArray = DOME_NewArray(MDOperandPtr, m_ParamCount);
    m_ParamIRPArray = DOME_NewArray(MDOperator::IRP, m_ParamCount);
    for (Int i = 0; i < m_ParamCount; ++i)
    {
        m_ParamArray[i] = i_ParamArray[i];
        if (m_ParamArray[i]->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)m_ParamArray[i];
            l_pOperation->addRefCount();
            l_pOperation->setStandalone();
        }
        m_ParamIRPArray[i] = MDOperator::IRP_NEVER;
    }

    m_OutputCount = m_pOperator->getOutputCount();
    m_OutputInfoArray = DOME_NewArray(_OutputInfo, m_OutputCount);
    for (Int i = 0; i < m_OutputCount; ++i)
    {
        m_OutputInfoArray[i].m_bTextureResult = m_pOperator->getOutputTypeID(i, m_pMDEffect, m_ParamCount, m_ParamArray) == RCGlobal::k_SimpleTypeID_OSTexture2D;
        if (m_OutputInfoArray[i].m_bTextureResult)
        {
            m_OutputInfoArray[i].m_TexResultFmt = m_pOperator->getOutputTexFmt(i, m_pMDEffect, m_ParamCount, m_ParamArray);
            l_Result = m_pOperator->calcOutputTexSize(i, m_pMDEffect, m_OutputInfoArray[i].m_TexResultSize, m_ParamCount, m_ParamArray);
            DOME_ASSERT(DM_SUCC(l_Result));
        }
        else
        {
            m_OutputInfoArray[i].m_TexResultFmt = RGDF_UNKNOWN;
        }
    }

    m_pResult = DM_NULL;
}

MDOperationCpu::~MDOperationCpu()
{
    DOME_ERROR2(!m_pResult, "ERROR: The result of cpu operation is not released!");

    if (m_ParamArray)
    {
        DOME_DelArray(m_ParamArray);
        m_ParamArray = DM_NULL;
    }
    if (m_ParamIRPArray)
    {
        DOME_DelArray(m_ParamIRPArray);
        m_ParamIRPArray = DM_NULL;
    }
    if (m_OutputInfoArray)
    {
        DOME_DelArray(m_OutputInfoArray);
        m_OutputInfoArray = DM_NULL;
    }
}

Bool                        MDOperationCpu::isOperation() const
{
    return DM_TRUE;
}

Int                         MDOperationCpu::getDataCount() const
{
    return m_pOperator->getOutputCount();
}

DSimpleTypeID               MDOperationCpu::getDataType(Int i_Index) const
{
    return m_pOperator->getOutputTypeID(i_Index, m_pMDEffect, m_ParamCount, m_ParamArray);
}

const MDOperand*            MDOperationCpu::getSubOperand(Int i_Index) const
{
    if (getDataCount() == 1 || !m_pResult)
        return DM_NULL;

    return m_pResult->getSubOperand(i_Index);
}

MDOperand*                  MDOperationCpu::getSubOperand(Int i_Index)
{
    if (getDataCount() == 1 || !m_pResult)
        return DM_NULL;

    return m_pResult->getSubOperand(i_Index);
}

const DSimpleTypedValue*    MDOperationCpu::getDataPtr() const
{
    if (getDataCount() > 1 || !m_pResult)
        return DM_NULL;

    return m_pResult->getDataPtr();
}

DSimpleTypedValue*          MDOperationCpu::getDataPtr()
{
    if (getDataCount() > 1 || !m_pResult)
        return DM_NULL;

    return m_pResult->getDataPtr();
}

RCGPUDATAFORMAT             MDOperationCpu::getTexDataFmt(Int i_Index) const
{
    return m_pOperator->getOutputTexFmt(i_Index, m_pMDEffect, m_ParamCount, m_ParamArray);
}

DVector2i                   MDOperationCpu::getTexDataSize(Int i_Index) const
{
    DVector2i l_Size;
    DResult l_Result = m_pOperator->calcOutputTexSize(i_Index, m_pMDEffect, l_Size, m_ParamCount, m_ParamArray);
    DOME_ASSERT(DM_SUCC(l_Result));
    return l_Size;
}

Bool                        MDOperationCpu::isGpu() const
{
    return DM_FALSE;
}

DResult                     MDOperationCpu::compile()
{
    if(m_bCompiled)
        return R_SUCCESS;
    m_bCompiled = DM_TRUE;

    DResult l_Result = R_SUCCESS;
    for (Int i = 0; i < m_ParamCount; ++i)
    {
        if (m_ParamArray[i]->isOperation())
        {
            MDOperation* l_pOperation = (MDOperation*)m_ParamArray[i];
            if(DM_FAIL(l_pOperation->compile()))
                l_Result = R_FAILED;
        }
    }
    return l_Result;
}

DResult                     MDOperationCpu::postCompile()
{
    Int l_UsedCount = addUsedCount();
    if (l_UsedCount == 1)
    {
        for (Int i = 0; i < m_ParamCount; ++i)
        {
            if (m_ParamArray[i]->isOperation())
            {
                MDOperation* l_pOperation = (MDOperation*)m_ParamArray[i];
                l_pOperation->postCompile();
            }
        }
    }

    return R_SUCCESS;
}

DResult                     MDOperationCpu::execute()
{
    Bool l_bContinue = DM_TRUE;
    if (!m_pResult)
    {
        // for each operation input, make sure it is executed
        for (Int i = 0; i < m_ParamCount; ++i)
        {
            MDOperandCPtr l_pOperand = m_ParamArray[i];
            if (l_pOperand->isOperation())
            {
                MDOperation* l_pOperation = (MDOperation*)l_pOperand;
                if (DM_FAIL(l_pOperation->execute()))
                {
                    DOME_ASSERT2(0, "operation exeucte failed!");
                    l_bContinue = DM_FALSE;
                    break;
                }
            }
        }

        if (l_bContinue)
        {
            m_pResult = m_pOperator->execute(m_pMDEffect, m_ParamCount, m_ParamArray, m_ParamIRPArray);
            if (!m_pResult)
            {
                l_bContinue = DM_FALSE;
            }
        }

        if (l_bContinue)
        {
            for (Int i = 0; i < m_ParamCount; ++i)
            {
                if (m_ParamIRPArray[i] == MDOperator::IRP_AFTEREXECUTE)
                {
                    MDOperandCPtr l_pOperand = m_ParamArray[i];
                    if (l_pOperand->isOperation())
                    {
                        MDOperation* l_pOperation = (MDOperation*)l_pOperand;
                        DResult l_Result = l_pOperation->finishCallback();
                        DOME_ASSERT(DM_SUCC(l_Result));
                    }
                }
            }
        }

    }
    DOME_ASSERT(m_pResult);
    if(m_pResult)
        return R_SUCCESS;
    else
        return R_FAILED;
}

DResult                     MDOperationCpu::finishCallback()
{
    DOME_ASSERT(getUsedCount() > 0);
    DOME_ASSERT(m_pResult);

    subUsedCount();
    if (getUsedCount() == 0)
    {
        m_pOperator->destroyResult(m_pMDEffect, m_ParamCount, m_ParamArray, m_pResult);
        m_pResult = DM_NULL;
        for (Int i = 0; i < m_ParamCount; ++i)
        {
            if (m_ParamIRPArray[i] == MDOperator::IRP::IRP_INFINISHCALLBACK)
            {
                MDOperandCPtr l_pOperand = m_ParamArray[i];
                if (l_pOperand->isOperation())
                {
                    MDOperation* l_pOperation = (MDOperation*)l_pOperand;
                    DResult l_Result = l_pOperation->finishCallback();
                    DOME_ASSERT(DM_SUCC(l_Result));
                }
            }
        }
    }
    return R_SUCCESS;
}


DOME_NAMESPACE_END