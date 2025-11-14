/*
    filename:       dmrcfactory.cpp
    author:         Ming Dong
    date:           2016-APR-15
    description:    
*/

#include "../public/dmrcfactory.h"
#include "../public/mdoperandvalue.h"
#include "../public/mdoperandvalueptr.h"
#include "../public/mdoperationcpu.h"
#include "../public/mdoperationgpu.h"

DOME_NAMESPACE_BEGIN

DResult          DMRCFactory::DestroyOperand(MDOperand* i_pOperand)
{
    if (i_pOperand)
    {
        DOME_Del(i_pOperand);
        return R_SUCCESS;
    }
    return R_FAILED;
}

MDOperand*       DMRCFactory::CreateOperand(DSimpleTypedValue* i_pValue)
{
    return DOME_New(MDOperandValuePtr)(i_pValue);
}

MDOperand*       DMRCFactory::CreateOperand(F32 i_Value)
{
    MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(DSimpleTypeManager::Instance().getType_F32());
    l_pOperand->getDataPtr()->getValue<F32>() = i_Value;
    return l_pOperand;
}

MDOperand*       DMRCFactory::CreateOperand(const DVector2f& i_Value)
{
    MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(DSimpleTypeManager::Instance().getType_DVector2f());
    l_pOperand->getDataPtr()->getValue<DVector2f>() = i_Value;
    return l_pOperand;
}

MDOperand*       DMRCFactory::CreateOperand(const DVector3f& i_Value)
{
    MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(DSimpleTypeManager::Instance().getType_DVector3f());
    l_pOperand->getDataPtr()->getValue<DVector3f>() = i_Value;
    return l_pOperand;
}

MDOperand*       DMRCFactory::CreateOperand(const DVector4f& i_Value)
{
    MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(DSimpleTypeManager::Instance().getType_DVector4f());
    l_pOperand->getDataPtr()->getValue<DVector4f>() = i_Value;
    return l_pOperand;
}

MDOperand*       DMRCFactory::CreateOperand(const OSHandle& i_Value)
{
    MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(DSimpleTypeManager::Instance().getType_OSHandle());
    l_pOperand->getDataPtr()->getValue<OSHandle>() = i_Value;
    return l_pOperand;
}

DResult          DMRCFactory::DestroyOperation(MDOperation* i_pOperation)
{
    if (i_pOperation)
    {
        DOME_Del(i_pOperation);
        return R_SUCCESS;
    }
    return R_FAILED;
}

MDOperationCpu*  DMRCFactory::CreateOperationCpu(MDEffect* i_pMDEffect, const MDOperator* i_pOperator, Int i_ParamCount, const MDOperandPtr* i_pParamArray)
{
    DOME_ASSERT(!i_pOperator->isGpuOperator());
    return DOME_New(MDOperationCpu)(i_pMDEffect, (MDOperatorCpu*)i_pOperator, i_ParamCount, i_pParamArray);
}

MDOperationGpu*  DMRCFactory::CreateOperationGpu(MDEffect* i_pMDEffect, const MDOperator* i_pOperator,
    Int i_ParamCount, const MDOperandPtr* i_pParamArray,
    const MDOperationGpu_MetaInfo* i_pMetaInfo)
{
    DOME_ASSERT(i_pOperator->isGpuOperator());
    return DOME_New(MDOperationGpu)(i_pMDEffect, (MDOperatorGpu*)i_pOperator, i_ParamCount, i_pParamArray, i_pMetaInfo);
}

MDOperationGpu*  DMRCFactory::CreateOperationGpu(MDEffect* i_pMDEffect, const MDOperator* i_pOperator,
    Int i_ParamCount, const MDOperandPtr* i_pParamArray,
    Int i_Width, Int i_Height,
    const MDOperationGpu_MetaInfo* i_pMetaInfo)
{
    DOME_ASSERT(i_pOperator->isGpuOperator());
    return DOME_New(MDOperationGpu)(i_pMDEffect, (MDOperatorGpu*)i_pOperator, i_ParamCount, i_pParamArray, i_Width, i_Height, i_pMetaInfo);
}

MDOperationGpu*  DMRCFactory::CreateOperationGpu(MDEffect* i_pMDEffect, const MDOperator* i_pOperator,
    Int i_ParamCount, const MDOperandPtr* i_pParamArray,
    RCGPUDATAFORMAT i_Format,
    const MDOperationGpu_MetaInfo* i_pMetaInfo)
{
    DOME_ASSERT(i_pOperator->isGpuOperator());
    return DOME_New(MDOperationGpu)(i_pMDEffect, (MDOperatorGpu*)i_pOperator, i_ParamCount, i_pParamArray, i_Format, i_pMetaInfo);
}

MDOperationGpu*  DMRCFactory::CreateOperationGpu(MDEffect* i_pMDEffect, const MDOperator* i_pOperator,
    Int i_ParamCount, const MDOperandPtr* i_pParamArray,
    Int i_Width, Int i_Height, RCGPUDATAFORMAT i_Format,
    const MDOperationGpu_MetaInfo* i_pMetaInfo)
{
    DOME_ASSERT(i_pOperator->isGpuOperator());
    return DOME_New(MDOperationGpu)(i_pMDEffect, (MDOperatorGpu*)i_pOperator, i_ParamCount, i_pParamArray, i_Width, i_Height, i_Format, i_pMetaInfo);
}



DOME_NAMESPACE_END