#include "pch.h"
/*
    filename:       mdoperatorscenerender.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "MDOperatorRenderLightsRTShadow.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)
static const int s_nInputCount = 9;

MDOperatorRenderLightsRTShadow::MDOperatorRenderLightsRTShadow()
    : m_OperatorName("MDRenderLightsRTShadow")
{

}

MDOperatorRenderLightsRTShadow::~MDOperatorRenderLightsRTShadow()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorRenderLightsRTShadow::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorRenderLightsRTShadow::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorRenderLightsRTShadow::getInputCount() const
{
    return s_nInputCount;
}

DSimpleTypeID       MDOperatorRenderLightsRTShadow::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index < 0 || i_Index >= s_nInputCount);
	DSimpleTypeID InputTypes[s_nInputCount] = {
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_F32
	};
	return InputTypes[i_Index];
}

Int                 MDOperatorRenderLightsRTShadow::getOutputCount() const
{
    return 2;
}

DSimpleTypeID       MDOperatorRenderLightsRTShadow::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == s_nInputCount);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorRenderLightsRTShadow::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == s_nInputCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isTexture());

    
    DResult l_Result;
    if (i_Index == 0)
    {
        F32 l_Params;

        l_Result = i_pParamList[8]->getFloat(l_Params);
        DOME_ASSERT(DM_SUCC(l_Result));

        Int l_nFormat = Int(l_Params + 0.5f);

        switch (l_nFormat)
        {
        case 0:
            return RGDF_RGBA8;
        case 1:
            return RGDF_RGBA16F;
        }
    }
    else if (i_Index == 1)
    {
        RCGPUDATAFORMAT l_Format;
        l_Result = i_pParamList[0]->getTextureFormat(l_Format);
        DOME_ASSERT(DM_SUCC(l_Result));
        return l_Format;
    }
    else
    {
        DOME_ASSERT(0);
    }
    return RGDF_UNKNOWN;
}

DResult             MDOperatorRenderLightsRTShadow::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == s_nInputCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isTexture());


    DResult l_Result;

    if (i_Index == 0)
    {
        DVector4f l_Size;

        l_Result = i_pParamList[0]->getTextureSize(o_Size);
        if(DM_SUCC(l_Result))
            return l_Result;

        return R_SUCCESS;
    }
    else if(i_Index == 1)
    {
        return i_pParamList[0]->getTextureSize(o_Size);
    }
    else
    {
        DOME_ASSERT(0);
    }
    return R_FAILED;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorRenderLightsRTShadow::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_RENDERLIGHTS, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == s_nInputCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isTexture());

    DResult l_Result;
    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

	o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[5] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[6] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[7] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[8] = IRP_AFTEREXECUTE;
	
	
    
    DVector2i l_RtSize;
    l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
    DOME_ASSERT(DM_SUCC(l_Result));

    RCGPUDATAFORMAT l_Format = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);
    DOME_ASSERT(l_Format == RGDF_RGBA8 || l_Format == RGDF_RGBA16F);

    OSTexture2D l_RtTex;
    l_Result = l_pRenderer->createTexture2D(l_RtTex, l_RtSize.x, l_RtSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
    DOME_ASSERT(DM_SUCC(l_Result));

	RCMOD_Texture l_ColorTexture;
	*((OSTexture2D*)l_ColorTexture.getPtr()) = l_RtTex;

	OSTexture2D l_RtTex1;
	l_Result = l_pRenderer->createTexture2D(l_RtTex1, l_RtSize.x, l_RtSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(l_Result));

	RCMOD_Texture l_ColorTexture1;
	*((OSTexture2D*)l_ColorTexture1.getPtr()) = l_RtTex1;


#define GetTexture(name, index) \
	RCMOD_Texture l_##name; \
	DSimpleTypedValue* l_p##name = i_pParamList[index]->getDataPtr();\
	*((OSTexture2D*)l_##name.getPtr()) = l_p##name->getValue<OSTexture2D>();
	
	GetTexture(DepthTexture, 6);

	RCMOD_Texture* l_OutputTextures[] = {
		&l_ColorTexture,
		&l_ColorTexture1,
		&l_DepthTexture
	};

	GetTexture(GBuffer0, 0);
	GetTexture(GBuffer1, 1);
	GetTexture(GBuffer2, 2);
	GetTexture(GBuffer3, 3);
	GetTexture(GBuffer4, 4);
	GetTexture(ResolveDepth, 5);
	RCMOD_Texture* l_InputTexture[] = {
		&l_GBuffer0,
		&l_GBuffer1,
		&l_GBuffer2,
		&l_GBuffer3,
		&l_GBuffer4,
		&l_ResolveDepth,
	};

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);
    
	l_pScenePlugin->RenderLightsRTShadow(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(),
		l_InputTexture,
		sizeof(l_InputTexture) / sizeof(l_InputTexture[0]),
		l_OutputTextures,
		sizeof(l_OutputTextures) / sizeof(l_OutputTextures[0]));

    MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_RtTex);
    DOME_ASSERT(DM_SUCC(l_Result));

	MDOperandValue* l_pRtOperand1 = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_Result = l_pRtOperand1->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_RtTex1);
	DOME_ASSERT(DM_SUCC(l_Result));

	MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
	l_pOperand->addOperand(l_pRtOperand);
	l_pOperand->addOperand(l_pRtOperand1);

	FRAMETIMER_END(FTT_RC_CAL_RENDERLIGHTS);

    return l_pOperand;
}

DResult             MDOperatorRenderLightsRTShadow::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	for (int i = 0; i < 2; ++i)
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