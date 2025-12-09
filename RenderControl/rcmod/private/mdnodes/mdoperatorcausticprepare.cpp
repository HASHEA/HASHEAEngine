#include "pch.h"
/*
    filename:       mdoperatorcausticprepare.cpp
    author:         Ming Dong
    date:           2019-JUN-18
    description:    
*/

#include "mdoperatorcausticprepare.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)

MDOperatorCausticPrepare::MDOperatorCausticPrepare()
    : m_OperatorName("MDCausticPrepare")
{

}

MDOperatorCausticPrepare::~MDOperatorCausticPrepare()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorCausticPrepare::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorCausticPrepare::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorCausticPrepare::getInputCount() const
{
    return 5;
}

DSimpleTypeID       MDOperatorCausticPrepare::getInputTypeID(Int i_Index) const
{
    if (i_Index == 0) 
        return RCGlobal::k_SimpleTypeID_DVector2f;
    else if (i_Index == 1 || i_Index == 3)
        return RCGlobal::k_SimpleTypeID_F32;
    return RCGlobal::k_SimpleTypeID_DVector3f;
}

Int                 MDOperatorCausticPrepare::getOutputCount() const
{
    return 2;
}

DSimpleTypeID       MDOperatorCausticPrepare::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 5);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorCausticPrepare::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 5);
    DOME_ASSERT(i_pParamList[0]&& i_pParamList[0]->isFloat2());			//< Canvas Size
    DOME_ASSERT(i_pParamList[1]&& i_pParamList[1]->isFloat());				//< Radius

    if (i_Index == 0)
    {
        return RGDF_RGBA16F;
    }
    else if (i_Index == 1)
    {
        return RGDF_BGRA8;
    }
    else
    {
        DOME_ASSERT(0);
    }
    return RGDF_UNKNOWN;
}

DResult             MDOperatorCausticPrepare::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 5);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isFloat2());			//< Canvas Size
    DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isFloat());				//< Radius

    DResult l_Result;
    DVector2f l_Size;

    l_Result = i_pParamList[0]->getFloat2(l_Size);
    DOME_ASSERT(DM_SUCC(l_Result));

	o_Size.x = Int(l_Size.x + 0.5f);
	o_Size.y = Int(l_Size.y + 0.5f);

    return R_SUCCESS;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorCausticPrepare::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    DOME_ASSERT(i_ParamCount == 5);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isFloat2());			//< Canvas Size
    DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isFloat());				//< Radius

    DResult l_Result;
    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

    o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;

    DVector3f l_LightDir = i_pParamList[2]->getDataPtr()->getDVector3f();

    DSimpleTypedValue* l_pRadiusValue = i_pParamList[1]->getDataPtr();
	F32 l_Radius = l_pRadiusValue->getF32();
    DSimpleTypedValue* l_pFixedCamValue = i_pParamList[3]->getDataPtr();
    F32 l_fFixedCam = l_pFixedCamValue->getF32();
    DSimpleTypedValue* l_pCamPosValue = i_pParamList[4]->getDataPtr();
    DVector3f l_CamPos = l_pCamPosValue->getDVector3f();
    DVector2i l_RtSize;

    l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
    DOME_ASSERT(DM_SUCC(l_Result));

    OSTexture2D l_WorldPosTex;
    l_Result = l_pRenderer->createTexture2D(l_WorldPosTex, l_RtSize.x, l_RtSize.y, 1, RGDF_RGBA32F, RBU_DEFAULT, DM_TRUE, DM_NULL);
    DOME_ASSERT(DM_SUCC(l_Result));

    OSTexture2D l_WorldNormalTex;
    l_Result = l_pRenderer->createTexture2D(l_WorldNormalTex, l_RtSize.x, l_RtSize.y, 1, RGDF_RGBA32F, RBU_DEFAULT, DM_TRUE, DM_NULL);
    DOME_ASSERT(DM_SUCC(l_Result));

	RCMOD_Texture l_WorldPosTexture;
	*((OSTexture2D*)l_WorldPosTexture.getPtr()) = l_WorldPosTex;

	RCMOD_Texture l_WorldNormalTexture;
	*((OSTexture2D*)l_WorldNormalTexture.getPtr()) = l_WorldNormalTex;

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);
    
	l_pScenePlugin->RCP_CausticPrepare((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_WorldPosTexture, &l_WorldNormalTexture, l_RtSize.x, l_RtSize.y, l_Radius, l_LightDir.getBuffer(), l_fFixedCam > 0.5f ? TRUE : FALSE, l_CamPos.getBuffer());

    MDOperandValue* l_pWPOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pWPOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_WorldPosTex);
    DOME_ASSERT(DM_SUCC(l_Result));

    MDOperandValue* l_pWNOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pWNOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_WorldNormalTex);
    DOME_ASSERT(DM_SUCC(l_Result));

	MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
	l_pOperand->addOperand(l_pWPOperand);
	l_pOperand->addOperand(l_pWNOperand);

    return l_pOperand;
}

DResult             MDOperatorCausticPrepare::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

    OSTexture2D l_hTexture;
	MDOperandValue* l_pRtOperand = (MDOperandValue*)i_pResult->getSubOperand(0);
    l_hTexture = *l_pRtOperand->getTexturePtr();
    l_pRenderer->destroyTexture2D(l_hTexture);
    DOME_Del(l_pRtOperand);

    l_pRtOperand = (MDOperandValue*)i_pResult->getSubOperand(1);
    l_hTexture = *l_pRtOperand->getTexturePtr();
    l_pRenderer->destroyTexture2D(l_hTexture);
    DOME_Del(l_pRtOperand);

	DOME_Del(i_pResult);

    return R_SUCCESS;
}


RC_NAMESPACE_END