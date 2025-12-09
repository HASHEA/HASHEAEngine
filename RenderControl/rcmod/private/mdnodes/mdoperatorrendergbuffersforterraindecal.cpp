#include "pch.h"
#ifndef __MDOPERATORRENDERGBUFFERSFORTERRAINDECAL_H__
#define __MDOPERATORRENDERGBUFFERSFORTERRAINDECAL_H__

#include "mdoperatorrendergbuffersforterraindecal.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

MDOperatorRenderGBuffersForTerrainDecal::MDOperatorRenderGBuffersForTerrainDecal()
	: m_OperatorName("MDRenderGBuffersForTerrainDecal")
{

}

MDOperatorRenderGBuffersForTerrainDecal ::~MDOperatorRenderGBuffersForTerrainDecal()
{

}

/****************************
	FROM MDOperator class
****************************/
const DString& MDOperatorRenderGBuffersForTerrainDecal::getOperatorName() const
{
	return m_OperatorName;
}

Bool                MDOperatorRenderGBuffersForTerrainDecal::isGpuOperator() const
{
	return DM_FALSE;
}

Int                 MDOperatorRenderGBuffersForTerrainDecal::getInputCount() const
{
	return 7;
}

DSimpleTypeID       MDOperatorRenderGBuffersForTerrainDecal::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index >= 0 && i_Index <= 6);
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

Int                 MDOperatorRenderGBuffersForTerrainDecal::getOutputCount() const
{
	return 6;
}

DSimpleTypeID       MDOperatorRenderGBuffersForTerrainDecal::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == 7);
	DOME_ASSERT(i_Index >= 0 && i_Index < 7);
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorRenderGBuffersForTerrainDecal::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == 7);
	DOME_ASSERT(i_Index >= 0 && i_Index < 7);

	if (i_Index >= 0 && i_Index < 3)
		return RGDF_RGBA8;
	else if (i_Index == 4 || i_Index ==3)
		return RGDF_RGBA16F;
	else
		return RGDF_D24S8;
}

DResult             MDOperatorRenderGBuffersForTerrainDecal::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == 7);

	DVector4f l_Size;
	i_pMDEffect->getRCEffect()->getEffectManager()->getParamSys().getDVector4f(DStringHash("EXTERN::PARAMETER::FullScreen"), l_Size);

	o_Size.x = Int(l_Size.x + 0.1f);
	o_Size.y = Int(l_Size.y + 0.1f);

	return R_SUCCESS;
}

/****************************
	FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorRenderGBuffersForTerrainDecal::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_RENDERGBUFFERFORDECAL, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == 7);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isTexture());
	DOME_ASSERT(i_pParamList[6] && i_pParamList[6]->isTexture());

	o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[2] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[3] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[4] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[5] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[6] = IRP_AFTEREXECUTE;


	DResult l_Result;
	RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

	DVector2i l_RtSize;
	l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
	DOME_ASSERT(DM_SUCC(l_Result));

	OSTexture2D l_GBuffer0, l_GBuffer1, l_GBuffer2, l_GBuffer3, l_GBuffer4;
	RCMOD_Texture l_RMGBuffer0, l_RMGBuffer1, l_RMGBuffer2, l_RMGBuffer3, l_RMGBuffer4;


	DSimpleTypedValue* l_pGBuffer0 = i_pParamList[0]->getDataPtr();
	*((OSTexture2D*)l_RMGBuffer0.getPtr()) = l_pGBuffer0->getValue<OSTexture2D>();

	DSimpleTypedValue* l_pGBuffer1 = i_pParamList[1]->getDataPtr();
	*((OSTexture2D*)l_RMGBuffer1.getPtr()) = l_pGBuffer1->getValue<OSTexture2D>();

	DSimpleTypedValue* l_pGBuffer2 = i_pParamList[2]->getDataPtr();
	*((OSTexture2D*)l_RMGBuffer2.getPtr()) = l_pGBuffer2->getValue<OSTexture2D>();

	DSimpleTypedValue* l_pGBuffer3 = i_pParamList[3]->getDataPtr();
	*((OSTexture2D*)l_RMGBuffer3.getPtr()) = l_pGBuffer3->getValue<OSTexture2D>();

	DSimpleTypedValue* l_pGBuffer4 = i_pParamList[4]->getDataPtr();
	*((OSTexture2D*)l_RMGBuffer4.getPtr()) = l_pGBuffer4->getValue<OSTexture2D>();

	RCMOD_Texture l_DepthTexture;
	DSimpleTypedValue* l_pDepthTexture = i_pParamList[5]->getDataPtr();
	*((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthTexture->getValue<OSTexture2D>();

	RCMOD_Texture l_DecalMask;
	DSimpleTypedValue* l_pDecalMask = i_pParamList[6]->getDataPtr();
	*((OSTexture2D*)l_DecalMask.getPtr()) = l_pDecalMask->getValue<OSTexture2D>();


	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

	l_pScenePlugin->RenderGBuffersForTerrainDecal(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(),
		&l_DepthTexture,
		&l_DecalMask,
		&l_RMGBuffer0,
		&l_RMGBuffer1,
		&l_RMGBuffer2,
		&l_RMGBuffer3,
		&l_RMGBuffer4
	);

	MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
	l_pOperand->addOperand(i_pParamList[0]);
	l_pOperand->addOperand(i_pParamList[1]);
	l_pOperand->addOperand(i_pParamList[2]);
	l_pOperand->addOperand(i_pParamList[3]);
	l_pOperand->addOperand(i_pParamList[4]);
	l_pOperand->addOperand(i_pParamList[5]);

	FRAMETIMER_END(FTT_RC_CAL_RENDERGBUFFERFORTERRAINDECAL);

	return l_pOperand;
}

DResult             MDOperatorRenderGBuffersForTerrainDecal::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	DOME_Del(i_pResult);
	return R_SUCCESS;
}


RC_NAMESPACE_END

#endif