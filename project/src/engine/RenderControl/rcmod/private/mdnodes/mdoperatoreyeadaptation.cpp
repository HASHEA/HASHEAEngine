#include "pch.h"
#include "mdoperatoreyeadaptation.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif

RC_NAMESPACE_BEGIN

MDOperatorEyeAdaptation::MDOperatorEyeAdaptation()
    : m_OperatorName("MDEyeAdaptation")
{

}

MDOperatorEyeAdaptation::~MDOperatorEyeAdaptation()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorEyeAdaptation::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorEyeAdaptation::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorEyeAdaptation::getInputCount() const
{
    return 7;
}

DSimpleTypeID       MDOperatorEyeAdaptation::getInputTypeID(Int i_Index) const
{
    switch (i_Index)
    {
    case 0:
    case 1:
    case 2:
		return RCGlobal::k_SimpleTypeID_OSTexture2D;
    case 3:
	case 4:
	case 5:
	case 6:
		return RCGlobal::k_SimpleTypeID_F32;        
    }
    return RCGlobal::k_SimpleTypeID_Unknown;
}

Int                 MDOperatorEyeAdaptation::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorEyeAdaptation::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorEyeAdaptation::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 7);
    DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());

    RCGPUDATAFORMAT l_Format;
    DResult l_Result = i_pParamList[2]->getTextureFormat(l_Format);
    DOME_ASSERT(DM_SUCC(l_Result));
    return l_Format;
}

DResult             MDOperatorEyeAdaptation::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 7);
	DOME_ASSERT(i_pParamList[0]->isTexture());      // source texture
	DOME_ASSERT(i_pParamList[1]->isTexture());      // dest	texture
	DOME_ASSERT(i_pParamList[2]->isTexture());      // render target texture
	DOME_ASSERT(i_pParamList[3]->isFloat());        // SpeedUp
	DOME_ASSERT(i_pParamList[4]->isFloat());        // SpeedDown
	DOME_ASSERT(i_pParamList[5]->isFloat());        // MinEyeAdaptation
	DOME_ASSERT(i_pParamList[6]->isFloat());        // MaxEyeAdaptation
   
    return i_pParamList[2]->getTextureSize(o_Size);
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorEyeAdaptation::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    PERF_COUNTER_EX(0);
	FRAMETIMER_BEGIN(FTT_RC_CAL_ACCUMULATER, FTT_RC_CAL_EXECUTE_EFFECTPASS);
    DResult l_Result;
	DOME_ASSERT(i_ParamCount == 7);
	DOME_ASSERT(i_pParamList[0]->isTexture());      // source texture
	DOME_ASSERT(i_pParamList[1]->isTexture());      // dest	texture
	DOME_ASSERT(i_pParamList[2]->isTexture());      // render target texture
	DOME_ASSERT(i_pParamList[3]->isFloat());        // SpeedUp
	DOME_ASSERT(i_pParamList[4]->isFloat());        // SpeedDown
	DOME_ASSERT(i_pParamList[5]->isFloat());        // MinEyeAdaptation
	DOME_ASSERT(i_pParamList[6]->isFloat());        // MaxEyeAdaptation
    
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
    F32			l_SpeedUp = *i_pParamList[3]->getFloatPtr();
	F32			l_SpeedDown = *i_pParamList[4]->getFloatPtr();
	F32			l_MinEyeAdaptation = *i_pParamList[5]->getFloatPtr();
	F32			l_MaxEyeAdaptation = *i_pParamList[6]->getFloatPtr();

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
	o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[5] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[6] = IRP_AFTEREXECUTE;

    IExecuter* l_pExecuter = (IExecuter*)l_pEffectMgr->getMDExecuter(DHashString("MDEyeAdaptationGpu($TEX[0],$TEX[1],$F1[0],$F1[1],$F1[2],$F1[3],$F1[4])"));
    DOME_ASSERT(l_pExecuter);

    l_pExecuter->setRenderTarget(l_RTTex);
	l_pExecuter->setRenderTargetViewport(Int(0), 
		Int(0), 
		Int(l_TrgSize.x), 
		Int(l_TrgSize.y));
	l_pExecuter->setUVCoef(DVector4f(1.0f, 1.0f, 0.0f, 0.0f));
    l_pExecuter->setTextureParam(0, l_SrcTex);
	l_pExecuter->setTextureParam(1, l_DestTex);
    l_pExecuter->setFloat1Param(0, l_SpeedUp);
    l_pExecuter->setFloat1Param(1, l_SpeedDown);
	l_pExecuter->setFloat1Param(2, l_fDeltaTime);
	l_pExecuter->setFloat1Param(3, l_MinEyeAdaptation);
	l_pExecuter->setFloat1Param(4, l_MaxEyeAdaptation);
    l_Result = l_pExecuter->execute();
    DOME_ASSERT(DM_SUCC(l_Result));
	FRAMETIMER_END(FTT_RC_CAL_ACCUMULATER);

    return i_pParamList[2];
}

DResult             MDOperatorEyeAdaptation::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    return R_SUCCESS;
}


RC_NAMESPACE_END