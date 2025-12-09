#include "pch.h"
/*
    filename:       mdoperatorscenerender.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "mdoperatorrtxrendergbuffers.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

MDOperatorRTXRenderGBuffers::MDOperatorRTXRenderGBuffers()
    : m_OperatorName("MDRTXRenderGBuffers")
{

}

MDOperatorRTXRenderGBuffers::~MDOperatorRTXRenderGBuffers()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorRTXRenderGBuffers::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorRTXRenderGBuffers::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorRTXRenderGBuffers::getInputCount() const
{
    return 8;
}

DSimpleTypeID       MDOperatorRTXRenderGBuffers::getInputTypeID(Int i_Index) const
{
    switch (i_Index)
    {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
        return RCGlobal::k_SimpleTypeID_OSTexture2D;
    case 5:
    case 6:
    case 7:
        return RCGlobal::k_SimpleTypeID_F32;
    }
    return RCGlobal::k_SimpleTypeID_Unknown;
}

Int                 MDOperatorRTXRenderGBuffers::getOutputCount() const
{
    return 7;
}

DSimpleTypeID       MDOperatorRTXRenderGBuffers::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 8);
    DOME_ASSERT(i_Index >= 0 && i_Index < 7);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorRTXRenderGBuffers::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 8);
    DOME_ASSERT(i_Index >= 0 && i_Index < 7);
    
	if (i_Index >= 0 && i_Index < 5)
		return RGDF_RGBA16F;
	else
		return RGDF_D24S8;
}

DResult             MDOperatorRTXRenderGBuffers::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == 8);

    DVector4f l_Size;
    i_pMDEffect->getRCEffect()->getEffectManager()->getParamSys().getDVector4f(DStringHash("EXTERN::PARAMETER::GBufferSize"), l_Size);

    o_Size.x = Int(l_Size.x + 0.1f);
    o_Size.y = Int(l_Size.y + 0.1f);

    return R_SUCCESS;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorRTXRenderGBuffers::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_RENDERGBUFFER, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == 8);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
    DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());
    DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isFloat());
	DOME_ASSERT(i_pParamList[6] && i_pParamList[6]->isFloat());
	DOME_ASSERT(i_pParamList[7] && i_pParamList[7]->isFloat());

    o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[3] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[4] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[5] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[6] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[7] = IRP_AFTEREXECUTE;

    DResult l_Result;
    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

    DVector2i l_RtSize;
    l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
    DOME_ASSERT(DM_SUCC(l_Result));

    OSTexture2D l_GBuffer0, l_GBuffer1, l_GBuffer2, l_GBuffer3, l_GBuffer4;
    RCMOD_Texture l_RMGBuffer0, l_RMGBuffer1, l_RMGBuffer2, l_RMGBuffer3, l_RMGBuffer4;

    l_Result = l_pRenderer->createTexture2D(l_GBuffer0, l_RtSize.x, l_RtSize.y, 1, RGDF_RGBA8, RBU_DEFAULT, DM_TRUE, DM_NULL);
    DOME_ASSERT(DM_SUCC(l_Result));
	*((OSTexture2D*)l_RMGBuffer0.getPtr()) = l_GBuffer0;

    l_Result = l_pRenderer->createTexture2D(l_GBuffer1, l_RtSize.x, l_RtSize.y, 1, RGDF_RGBA8, RBU_DEFAULT, DM_TRUE, DM_NULL);
    DOME_ASSERT(DM_SUCC(l_Result));
	*((OSTexture2D*)l_RMGBuffer1.getPtr()) = l_GBuffer1;

    l_Result = l_pRenderer->createTexture2D(l_GBuffer2, l_RtSize.x, l_RtSize.y, 1, RGDF_RGBA8, RBU_DEFAULT, DM_TRUE, DM_NULL);
    DOME_ASSERT(DM_SUCC(l_Result));
	*((OSTexture2D*)l_RMGBuffer2.getPtr()) = l_GBuffer2;

    l_Result = l_pRenderer->createTexture2D(l_GBuffer3, l_RtSize.x, l_RtSize.y, 1, RGDF_RGBA16F, RBU_DEFAULT, DM_TRUE, DM_NULL);
    DOME_ASSERT(DM_SUCC(l_Result));
	*((OSTexture2D*)l_RMGBuffer3.getPtr()) = l_GBuffer3;

    l_Result = l_pRenderer->createTexture2D(l_GBuffer4, l_RtSize.x, l_RtSize.y, 1, RGDF_RGBA8, RBU_DEFAULT, DM_TRUE, DM_NULL);
    DOME_ASSERT(DM_SUCC(l_Result));
	*((OSTexture2D*)l_RMGBuffer4.getPtr()) = l_GBuffer4;

    RCMOD_Texture l_Input0;
    DSimpleTypedValue* l_pInput0 = i_pParamList[0]->getDataPtr();
    *((OSTexture2D*)l_Input0.getPtr()) = l_pInput0->getValue<OSTexture2D>();

	RCMOD_Texture l_Input1;
	DSimpleTypedValue* l_pInput1 = i_pParamList[1]->getDataPtr();
	*((OSTexture2D*)l_Input1.getPtr()) = l_pInput1->getValue<OSTexture2D>();

	RCMOD_Texture l_Input2;
	DSimpleTypedValue* l_pInput2 = i_pParamList[2]->getDataPtr();
	*((OSTexture2D*)l_Input2.getPtr()) = l_pInput2->getValue<OSTexture2D>();

	RCMOD_Texture l_Input3;
	DSimpleTypedValue* l_pInput3 = i_pParamList[3]->getDataPtr();
	*((OSTexture2D*)l_Input3.getPtr()) = l_pInput3->getValue<OSTexture2D>();

	RCMOD_Texture l_Input4;
	DSimpleTypedValue* l_pInput4 = i_pParamList[4]->getDataPtr();
	*((OSTexture2D*)l_Input4.getPtr()) = l_pInput4->getValue<OSTexture2D>();

    float l_RoughnessThreshold = i_pParamList[5]->getDataPtr()->getF32();
    float l_fReflectionOpaqueTMax = i_pParamList[6]->getDataPtr()->getF32();
    float l_fReflectionNonOpaqueTMax = i_pParamList[7]->getDataPtr()->getF32();

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);
    
	l_pScenePlugin->RTXRenderGBuffers(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(),
        &l_Input0,
		&l_Input1,
		&l_Input2,
		&l_Input3,
		&l_Input4,
        l_RoughnessThreshold,
        l_fReflectionOpaqueTMax,
        l_fReflectionNonOpaqueTMax,
        &l_RMGBuffer0,
        &l_RMGBuffer1,
        &l_RMGBuffer2,
        &l_RMGBuffer3,
        &l_RMGBuffer4
		);

    MDOperandValue* l_pGB0Operand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pGB0Operand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_GBuffer0);
    DOME_ASSERT(DM_SUCC(l_Result));

    MDOperandValue* l_pGB1Operand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pGB1Operand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_GBuffer1);
    DOME_ASSERT(DM_SUCC(l_Result));

    MDOperandValue* l_pGB2Operand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pGB2Operand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_GBuffer2);
    DOME_ASSERT(DM_SUCC(l_Result));

    MDOperandValue* l_pGB3Operand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pGB3Operand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_GBuffer3);
    DOME_ASSERT(DM_SUCC(l_Result));

    MDOperandValue* l_pGB4Operand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pGB4Operand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_GBuffer4);
    DOME_ASSERT(DM_SUCC(l_Result));

	MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
	l_pOperand->addOperand(l_pGB0Operand);
	l_pOperand->addOperand(l_pGB1Operand);
	l_pOperand->addOperand(l_pGB2Operand);
	l_pOperand->addOperand(l_pGB3Operand);
	l_pOperand->addOperand(l_pGB4Operand);
	l_pOperand->addOperand(i_pParamList[3]);
	l_pOperand->addOperand(i_pParamList[4]);

	FRAMETIMER_END(FTT_RC_CAL_RENDERGBUFFER);

    return l_pOperand;
}

DResult             MDOperatorRTXRenderGBuffers::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	for (int i = 0; i < 5; ++i)
	{
		OSTexture2D l_hTexture;
		MDOperandValue* l_pRtOperand = (MDOperandValue*)i_pResult->getSubOperand(i);
		l_hTexture = *l_pRtOperand->getTexturePtr();
		l_pRenderer->destroyTexture2D(l_hTexture);
		DOME_Del(l_pRtOperand);
	}
	DOME_Del(i_pResult);

    return R_SUCCESS;
}


RC_NAMESPACE_END