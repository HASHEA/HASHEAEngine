/*
    filename:       mdeffect.h
    author:         Ming Dong
    date:           2016-APR-18
    description:    
*/
#pragma once

#include "microdata.h"
#include "rcrenderer.h"

DOME_NAMESPACE_BEGIN

class MDOperand;
class MDOperator;
class MDOperatorCpu;
class MDOperatorGpu;
class MDOperation;
class MDMetaOP;
class MDMetaInfo;
class RCEffect;
class MDOperationGpu_MetaInfo;
struct MDEffect_Data;
class RC_API MDEffect
{
public:
    MDEffect(RCEffect* i_pRCEffect);
    ~MDEffect();

    RCEffect*   getRCEffect();

    DResult     begin();
    DResult     end(Bool i_bSkipCompile = DM_FALSE);

    DResult     pushOperand(MDOperand* i_pOperand);
    DResult     pushOperatorCpu(const MDOperatorCpu* i_pOperator);
    DResult     pushOperatorGpu(const MDOperatorGpu* i_pOperator, const MDOperationGpu_MetaInfo* i_pMetaInfo = DM_NULL);
    DResult     pushOperatorGpu(const MDOperatorGpu* i_pOperator, Int i_Width, Int i_Height, const MDOperationGpu_MetaInfo* i_pMetaInfo = DM_NULL);
    DResult     pushOperatorGpu(const MDOperatorGpu* i_pOperator, RCGPUDATAFORMAT i_Format, const MDOperationGpu_MetaInfo* i_pMetaInfo = DM_NULL);
    DResult     pushOperatorGpu(const MDOperatorGpu* i_pOperator, Int i_Width, Int i_Height, RCGPUDATAFORMAT i_Format, const MDOperationGpu_MetaInfo* i_pMetaInfo = DM_NULL);
    MDOperand*  getTopOperand();
    DResult     popOperand();

    DResult     execute();

private:
    MDEffect_Data*   me;
};


DOME_NAMESPACE_END