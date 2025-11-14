/*
    filename:       mdeffect.cpp
    author:         Ming Dong
    date:           2016-APR-18
    description:    
*/

#include "../public/mdeffect.h"
#include "../public/microdata.h"
#include "../public/mdoperand.h"
#include "../public/mdoperandvalue.h"
#include "../public/mdoperandvalueptr.h"
#include "../public/mdoperator.h"
#include "../public/mdoperatorcpu.h"
#include "../public/mdoperatorgpu.h"
#include "../public/mdoperation.h"
#include "../public/mdoperationcpu.h"
#include "../public/mdoperationgpu.h"
#include "../public/dmrcfactory.h"
#include "../../../DevEnv/Include/PerfAnalyzer.h"


DOME_NAMESPACE_BEGIN

typedef TArray<MDOperand*, IDefaultMemManager, 1024>      _MDOperandArray;
typedef TArray<MDOperation*, IDefaultMemManager, 2048>          _MDOperationArray;

struct MDEffect_Data
{
    RCEffect*                               m_pRCEffect;
    _MDOperandArray                         m_OperandArray;
    _MDOperationArray                       m_OperationArray;
};

MDEffect::MDEffect(RCEffect* i_pRCEffect)
{
    me = DOME_New(MDEffect_Data);
    me->m_pRCEffect = i_pRCEffect;
}

MDEffect::~MDEffect()
{
    // destroy all the operation created
    Int l_NumOperation = me->m_OperationArray.size();
    for (Int i = l_NumOperation - 1; i >= 0; --i)
    {
        if (me->m_OperationArray[i])
        {
            DMRCFactory::DestroyOperation(me->m_OperationArray[i]);
        }
    }

    DOME_Del(me);
}

RCEffect*   MDEffect::getRCEffect()
{
    return me->m_pRCEffect;
}

DResult     MDEffect::begin()
{
    return R_SUCCESS;
}

DResult     MDEffect::end(Bool i_bSkipCompile)
{
    if (me->m_OperandArray.size() == 1)
    {
        DResult l_Result;
        if (i_bSkipCompile)
            l_Result = R_SUCCESS;
        else
            l_Result = me->m_OperationArray[me->m_OperationArray.size() - 1]->compile();
        if (DM_SUCC(l_Result))
        {
            return me->m_OperationArray[me->m_OperationArray.size() - 1]->postCompile();
        }
        else
            return R_FAILED;
    }
    else
        return R_FAILED;
}

DResult     MDEffect::pushOperand(MDOperand* i_pOperand)
{
    me->m_OperandArray.push_back(i_pOperand);
    return R_SUCCESS;
}

DResult     MDEffect::pushOperatorCpu(const MDOperatorCpu* i_pOperator)
{
    Int l_InputCount = i_pOperator->getInputCount();
    DOME_ASSERT2(l_InputCount <= me->m_OperandArray.size(), "The operator '%s' need %lld parameters, but there is only %lld parameters on stack.", i_pOperator->getOperatorName().c_str(), l_InputCount, me->m_OperandArray.size());

    MDOperationCpu* l_pOperation = DMRCFactory::CreateOperationCpu(this, i_pOperator, l_InputCount, l_InputCount == 0 ? DM_NULL : &me->m_OperandArray[me->m_OperandArray.size() - l_InputCount]);
    me->m_OperandArray.resize(me->m_OperandArray.size() - l_InputCount);

    me->m_OperationArray.push_back(l_pOperation);
    me->m_OperandArray.push_back(l_pOperation);

    return R_SUCCESS;
}

DResult     MDEffect::pushOperatorGpu(const MDOperatorGpu* i_pOperator, const MDOperationGpu_MetaInfo* i_pMetaInfo)
{
    PERF_COUNTER_EX(0);
    Int l_InputCount = i_pOperator->getInputCount();
    DOME_ASSERT2(l_InputCount <= me->m_OperandArray.size(), "The operator '%s' need %lld parameters, but there is only %lld parameters on stack.", i_pOperator->getOperatorName().c_str(), l_InputCount, me->m_OperandArray.size());

    MDOperationGpu* l_pOperation = DMRCFactory::CreateOperationGpu(this, i_pOperator, l_InputCount, 
        l_InputCount == 0 ? DM_NULL : &me->m_OperandArray[me->m_OperandArray.size() - l_InputCount], i_pMetaInfo);

    me->m_OperandArray.resize(me->m_OperandArray.size() - l_InputCount);

    me->m_OperationArray.push_back(l_pOperation);
    me->m_OperandArray.push_back(l_pOperation);

    return R_SUCCESS;
}

DResult     MDEffect::pushOperatorGpu(const MDOperatorGpu* i_pOperator, Int i_Width, Int i_Height, const MDOperationGpu_MetaInfo* i_pMetaInfo)
{
    PERF_COUNTER_EX(0);
    Int l_InputCount = i_pOperator->getInputCount();
    DOME_ASSERT2(l_InputCount <= me->m_OperandArray.size(), "The operator '%s' need %lld parameters, but there is only %lld parameters on stack.", i_pOperator->getOperatorName().c_str(), l_InputCount, me->m_OperandArray.size());

    MDOperationGpu* l_pOperation = DMRCFactory::CreateOperationGpu(this, i_pOperator, l_InputCount, 
        l_InputCount == 0 ? DM_NULL : &me->m_OperandArray[me->m_OperandArray.size() - l_InputCount],
        i_Width, i_Height,
        i_pMetaInfo);

    me->m_OperandArray.resize(me->m_OperandArray.size() - l_InputCount);

    me->m_OperationArray.push_back(l_pOperation);
    me->m_OperandArray.push_back(l_pOperation);

    return R_SUCCESS;
}

DResult     MDEffect::pushOperatorGpu(const MDOperatorGpu* i_pOperator, RCGPUDATAFORMAT i_Format, const MDOperationGpu_MetaInfo* i_pMetaInfo)
{
    PERF_COUNTER_EX(0);
    Int l_InputCount = i_pOperator->getInputCount();
    DOME_ASSERT2(l_InputCount <= me->m_OperandArray.size(), "The operator '%s' need %lld parameters, but there is only %lld parameters on stack.", i_pOperator->getOperatorName().c_str(), l_InputCount, me->m_OperandArray.size());

    MDOperationGpu* l_pOperation = DMRCFactory::CreateOperationGpu(this, i_pOperator, l_InputCount,
        l_InputCount == 0 ? DM_NULL : &me->m_OperandArray[me->m_OperandArray.size() - l_InputCount], i_Format, i_pMetaInfo);

    me->m_OperandArray.resize(me->m_OperandArray.size() - l_InputCount);

    me->m_OperationArray.push_back(l_pOperation);
    me->m_OperandArray.push_back(l_pOperation);

    return R_SUCCESS;
}

DResult     MDEffect::pushOperatorGpu(const MDOperatorGpu* i_pOperator, Int i_Width, Int i_Height, RCGPUDATAFORMAT i_Format, const MDOperationGpu_MetaInfo* i_pMetaInfo)
{
    PERF_COUNTER_EX(0);
    Int l_InputCount = i_pOperator->getInputCount();
    DOME_ASSERT2(l_InputCount <= me->m_OperandArray.size(), "The operator '%s' need %lld parameters, but there is only %lld parameters on stack.", i_pOperator->getOperatorName().c_str(), l_InputCount, me->m_OperandArray.size());

    MDOperationGpu* l_pOperation = DMRCFactory::CreateOperationGpu(this, i_pOperator, l_InputCount,
        l_InputCount == 0 ? DM_NULL : &me->m_OperandArray[me->m_OperandArray.size() - l_InputCount],
        i_Width, i_Height, i_Format,
        i_pMetaInfo);

    me->m_OperandArray.resize(me->m_OperandArray.size() - l_InputCount);

    me->m_OperationArray.push_back(l_pOperation);
    me->m_OperandArray.push_back(l_pOperation);

    return R_SUCCESS;
}

MDOperand*  MDEffect::getTopOperand()
{
    DOME_ASSERT(me->m_OperandArray.size() > 0);
    return me->m_OperandArray[me->m_OperandArray.size() - 1];
}

DResult     MDEffect::popOperand()
{
    DOME_ASSERT(me->m_OperandArray.size() > 0);
    me->m_OperandArray.pop_back();
    return R_SUCCESS;
}

DResult     MDEffect::execute()
{
    DResult l_Result = me->m_OperationArray[me->m_OperationArray.size() - 1]->execute();
    if (DM_SUCC(l_Result))
    {
        return me->m_OperationArray[me->m_OperationArray.size() - 1]->finishCallback();
    }
    else
        return l_Result;
}

DOME_NAMESPACE_END