#include "pch.h"
/*
    filename:       MDOperatorAccumulator.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "MDOperatorAccumulator.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif

RC_NAMESPACE_BEGIN

MDOperatorAccumulator::MDOperatorAccumulator()
    : m_OperatorName("MDAccumulator")
{

}

MDOperatorAccumulator::~MDOperatorAccumulator()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorAccumulator::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorAccumulator::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorAccumulator::getInputCount() const
{
    return 4;
}

DSimpleTypeID       MDOperatorAccumulator::getInputTypeID(Int i_Index) const
{
    switch (i_Index)
    {
    case 0:
    case 1:
    case 2:
		return RCGlobal::k_SimpleTypeID_OSTexture2D;
    case 3:
		return RCGlobal::k_SimpleTypeID_DVector2f;        
    }
    return RCGlobal::k_SimpleTypeID_Unknown;
}

Int                 MDOperatorAccumulator::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorAccumulator::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorAccumulator::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 4);
    DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());

    RCGPUDATAFORMAT l_Format;
    DResult l_Result = i_pParamList[2]->getTextureFormat(l_Format);
    DOME_ASSERT(DM_SUCC(l_Result));
    return l_Format;
}

DResult             MDOperatorAccumulator::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 4);
	DOME_ASSERT(i_pParamList[0]->isTexture());      // source texture
	DOME_ASSERT(i_pParamList[1]->isTexture());      // dest	texture
	DOME_ASSERT(i_pParamList[2]->isTexture());      // render target texture
	DOME_ASSERT(i_pParamList[3]->isFloat2());       // time\exp
   
    return i_pParamList[2]->getTextureSize(o_Size);
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorAccumulator::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    PERF_COUNTER_EX(0);
	FRAMETIMER_BEGIN(FTT_RC_CAL_ACCUMULATER, FTT_RC_CAL_EXECUTE_EFFECTPASS);
    DResult l_Result;
	DOME_ASSERT(i_ParamCount == 4);
	DOME_ASSERT(i_pParamList[0]->isTexture());      // source texture
	DOME_ASSERT(i_pParamList[1]->isTexture());      // dest	texture
	DOME_ASSERT(i_pParamList[2]->isTexture());      // render target texture
	DOME_ASSERT(i_pParamList[3]->isFloat2());       // time\exp
    
    RCEffectManager* l_pEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pEffectMgr->getRenderer();

    DStringHash l_DeltaNameHash("EXTERN::PARAMETER::DeltaTime");
    DSimpleTypedValue* l_pDeltaTime = l_pEffectMgr->getParamSys().getParameter(l_DeltaNameHash);
    DOME_ASSERT(l_pDeltaTime);
    DStringHash l_CurrentFrameHash("EXTERN::PARAMETER::CurrentFrame");
    DSimpleTypedValue* l_pCurrentFrame = l_pEffectMgr->getParamSys().getParameter(l_CurrentFrameHash);
    DOME_ASSERT(l_pCurrentFrame);

    F32 l_fDeltaTime = l_pDeltaTime->getF32();
    Int l_iCurrentFrame = l_pCurrentFrame->getInt();

    OSTexture2D l_DestTex = *i_pParamList[0]->getTexturePtr();
	OSTexture2D l_SrcTex;
	OSTexture2D l_RTTex;
    DVector2f   l_Param = *i_pParamList[3]->getFloat2Ptr();   

    if (l_iCurrentFrame % 2 == 0)
    {
	    l_SrcTex = *i_pParamList[1]->getTexturePtr();
	    l_RTTex = *i_pParamList[2]->getTexturePtr();
    }
    else
    {
	    l_SrcTex = *i_pParamList[2]->getTexturePtr();
	    l_RTTex = *i_pParamList[1]->getTexturePtr();
    }

    DVector2i l_TrgSize;
    l_Result = l_pRenderer->getTexture2DSize(l_RTTex, l_TrgSize.x, l_TrgSize.y);
    DOME_ASSERT(DM_SUCC(l_Result));

    o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[2] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;

    IExecuter* l_pExecuter = (IExecuter*)l_pEffectMgr->getMDExecuter(DHashString("MDAccumulatorGpu($TEX[0],$TEX[1],$F1[0],$F1[1],$F1[2])"));
    DOME_ASSERT(l_pExecuter);

    l_pExecuter->setRenderTarget(l_RTTex);
	l_pExecuter->setRenderTargetViewport(Int(0), 
		Int(0), 
		Int(l_TrgSize.x), 
		Int(l_TrgSize.y));
	l_pExecuter->setUVCoef(DVector4f(1.0f, 1.0f, 0.0f, 0.0f));
    l_pExecuter->setTextureParam(0, l_SrcTex);
	l_pExecuter->setTextureParam(1, l_DestTex);
    l_pExecuter->setFloat1Param(0, l_Param.x);
    l_pExecuter->setFloat1Param(1, l_Param.y);
	l_pExecuter->setFloat1Param(2, l_fDeltaTime);
    l_Result = l_pExecuter->execute();
    DOME_ASSERT(DM_SUCC(l_Result));
	FRAMETIMER_END(FTT_RC_CAL_ACCUMULATER);

    return i_pParamList[2];
}

DResult             MDOperatorAccumulator::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    return R_SUCCESS;
}


RC_NAMESPACE_END