/*
    filename:       dmrcfactory.h
    author:         Ming Dong
    date:           2016-APR-15
    description:    
*/
#pragma once

#include "rctypedefs.h"
#include "rcrenderer.h"

DOME_NAMESPACE_BEGIN

class MDEffect;
class MDOperand;
class MDOperator;
class MDOperatorCpu;
class MDOperatorGpu;
class MDOperation;
class MDOperationCpu;
class MDOperationGpu;
class MDOperationGpu_MetaInfo;
class RC_API DMRCFactory
{
    typedef const MDOperand*                MDOperandCPtr;
    typedef MDOperand*                      MDOperandPtr;
public:
    // operand factory functions
    static DResult          DestroyOperand(MDOperand* i_pOperand);
    static MDOperand*       CreateOperand(DSimpleTypedValue* i_pValue);
    static MDOperand*       CreateOperand(F32 i_Value);
    static MDOperand*       CreateOperand(const DVector2f& i_Value);
    static MDOperand*       CreateOperand(const DVector3f& i_Value);
    static MDOperand*       CreateOperand(const DVector4f& i_Value);
    static MDOperand*       CreateOperand(const OSHandle& i_Value);

    // operation factory functions
    static DResult          DestroyOperation(MDOperation* i_pOperation);

    static MDOperationCpu*  CreateOperationCpu(MDEffect* i_pMDEffect, const MDOperator* i_pOperator, Int i_ParamCount, const MDOperandPtr* i_pParamArray);

    static MDOperationGpu*  CreateOperationGpu(MDEffect* i_pMDEffect, const MDOperator* i_pOperator, 
                                            Int i_ParamCount, const MDOperandPtr* i_pParamArray, 
                                            const MDOperationGpu_MetaInfo* i_pMetaInfo = DM_NULL);
    static MDOperationGpu*  CreateOperationGpu(MDEffect* i_pMDEffect, const MDOperator* i_pOperator, 
                                            Int i_ParamCount, const MDOperandPtr* i_pParamArray, 
                                            Int i_Width, Int i_Height, 
                                            const MDOperationGpu_MetaInfo* i_pMetaInfo = DM_NULL);
    static MDOperationGpu*  CreateOperationGpu(MDEffect* i_pMDEffect, const MDOperator* i_pOperator,
                                            Int i_ParamCount, const MDOperandPtr* i_pParamArray,
                                            RCGPUDATAFORMAT i_Format,
                                            const MDOperationGpu_MetaInfo* i_pMetaInfo = DM_NULL);
    static MDOperationGpu*  CreateOperationGpu(MDEffect* i_pMDEffect, const MDOperator* i_pOperator,
                                            Int i_ParamCount, const MDOperandPtr* i_pParamArray,
                                            Int i_Width, Int i_Height, RCGPUDATAFORMAT i_Format,
                                            const MDOperationGpu_MetaInfo* i_pMetaInfo = DM_NULL);
};


DOME_NAMESPACE_END