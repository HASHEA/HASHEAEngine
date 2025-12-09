#include"pch.h"

#include"mdoperatorrendercloudshadowmask.h"
#include<rc/public/iexecuter.h>

#ifdef RC_PERF
#include"KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif

RC_NAMESPACE_BEGIN

MDOperatorRenderCloudShadowMask::MDOperatorRenderCloudShadowMask()
	:m_OperatorName("MDRenderCloudShadowMask")
{

}

MDOperatorRenderCloudShadowMask::~MDOperatorRenderCloudShadowMask()
{

}


const DString& MDOperatorRenderCloudShadowMask::getOperatorName()const
{
	return m_OperatorName;
}

Bool MDOperatorRenderCloudShadowMask::isGpuOperator()const
{
	return DM_FALSE;
}

Int MDOperatorRenderCloudShadowMask::getInputCount()const
{
	return 5;
}

DSimpleTypeID MDOperatorRenderCloudShadowMask::getInputTypeID(Int i_Index)const
{
	DOME_ASSERT(i_Index <= 4);
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

Int MDOperatorRenderCloudShadowMask::getOutputCount()const
{
	return 2;
}

DSimpleTypeID MDOperatorRenderCloudShadowMask::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList)const
{
	DOME_ASSERT(i_ParamCount == 5);
	DOME_ASSERT(i_Index >= 0 && i_Index <= 4);
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT MDOperatorRenderCloudShadowMask::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList)const
{
	DOME_ASSERT(i_ParamCount == 5);
	DOME_ASSERT(i_Index >= 0 && i_Index <= 4);
	if (i_Index == 3 || i_Index == 4)
	{
		return RGDF_RGBA8;
	}
	else {
		return RGDF_D24S8;
	}
}

DResult MDOperatorRenderCloudShadowMask::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList)const
{
	DOME_ASSERT(i_ParamCount == 5);

	DVector4f l_Size;
	i_pMDEffect->getRCEffect()->getEffectManager()->getParamSys().getDVector4f(DStringHash("EXTERN::PARAMETER::GBufferSize"), l_Size);

	o_Size.x = Int(l_Size.x + 0.1f);
	o_Size.y = Int(l_Size.y + 0.1f);

	return R_SUCCESS;
}

MDOperandPtr MDOperatorRenderCloudShadowMask::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint)const
{
	DOME_ASSERT(i_ParamCount == 5);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());


	o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[3] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[4] = IRP_INFINISHCALLBACK;

	DResult l_Result;
	RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

	DVector2i l_RtSize;
	l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
	DOME_ASSERT(DM_SUCC(l_Result));

	RCMOD_Texture l_DepthTexture;
	RCMOD_Texture l_CloudShadowTexture;
	RCMOD_Texture l_GBufferATexture;
	RCMOD_Texture l_GBufferCTexture;

	DSimpleTypedValue* l_pDepthTexture = i_pParamList[0]->getDataPtr();
	DSimpleTypedValue* l_pCloudShadowTexture = i_pParamList[2]->getDataPtr();
	DSimpleTypedValue* l_pGBufferA = i_pParamList[3]->getDataPtr();
	DSimpleTypedValue* l_pGBufferC = i_pParamList[4]->getDataPtr();
	*((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthTexture->getValue<OSTexture2D>();
	*((OSTexture2D*)l_CloudShadowTexture.getPtr()) = l_pCloudShadowTexture->getValue<OSTexture2D>();
	*((OSTexture2D*)l_GBufferATexture.getPtr()) = l_pGBufferA->getValue<OSTexture2D>();
	*((OSTexture2D*)l_GBufferCTexture.getPtr()) = l_pGBufferC->getValue<OSTexture2D>();

	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

	l_pScenePlugin->RenderCloudShadowMap(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(),
		&l_DepthTexture,
		&l_CloudShadowTexture,
		&l_GBufferATexture,
		&l_GBufferCTexture
	);

	MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
	l_pOperand->addOperand(i_pParamList[0]);
	l_pOperand->addOperand(i_pParamList[1]);

	return l_pOperand;
}

DResult MDOperatorRenderCloudShadowMask::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult)const
{
	DOME_Del(i_pResult);
	return R_SUCCESS;
}

RC_NAMESPACE_END