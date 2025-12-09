#include "pch.h"
/*
    filename:       mdoperatorscenerender.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "mdoperatorscenerender.h"
#include <rc/public/iexecuter.h>
#include "../../../DevEnv/Include/PerfAnalyzer.h"

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)

MDOperatorSceneRender::MDOperatorSceneRender()
    : m_OperatorName("MDSceneRender")
{

}

MDOperatorSceneRender::~MDOperatorSceneRender()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorSceneRender::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorSceneRender::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorSceneRender::getInputCount() const
{
    return 5;
}

DSimpleTypeID       MDOperatorSceneRender::getInputTypeID(Int i_Index) const
{
    if(i_Index == 0) return RCGlobal::k_SimpleTypeID_OSTexture2D;
    return RCGlobal::k_SimpleTypeID_DVector4f;
}

Int                 MDOperatorSceneRender::getOutputCount() const
{
    return 2;
}

DSimpleTypeID       MDOperatorSceneRender::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 5);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorSceneRender::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 5);
    DOME_ASSERT(i_pParamList[0]&& i_pParamList[0]->isTexture());			//< Depth
    DOME_ASSERT(i_pParamList[1]&& i_pParamList[1]->isFloat4());				//< Param
    DOME_ASSERT(i_pParamList[2]&& i_pParamList[2]->isFloat4());				//< State
	DOME_ASSERT(i_pParamList[3]&& i_pParamList[3]->isFloat4());				//< ClearColor
    DOME_ASSERT(i_pParamList[4]&& i_pParamList[4]->isFloat4());				//< ViewPort

    DResult l_Result;
    if (i_Index == 0)
    {
        DVector4f l_Params;

        l_Result = i_pParamList[1]->getFloat4(l_Params);
        DOME_ASSERT(DM_SUCC(l_Result));

        Int l_nFormat = Int(l_Params.w + 0.5f);

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

DResult             MDOperatorSceneRender::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 5);
    DOME_ASSERT(i_pParamList[0]&& i_pParamList[0]->isTexture());
    DOME_ASSERT(i_pParamList[1]&& i_pParamList[1]->isFloat4());
    DOME_ASSERT(i_pParamList[2]&& i_pParamList[2]->isFloat4());
    DOME_ASSERT(i_pParamList[3]&& i_pParamList[3]->isFloat4());
	DOME_ASSERT(i_pParamList[4]&& i_pParamList[4]->isFloat4());

    DResult l_Result;

    if (i_Index == 0)
    {
        DVector4f l_Size;

        l_Result = i_pParamList[1]->getFloat4(l_Size);
        DOME_ASSERT(DM_SUCC(l_Result));

		if(l_Size.x >= 1.0f && l_Size.y >= 1.0f)
		{ 
			o_Size.x = Int(l_Size.x + 0.5f);
			o_Size.y = Int(l_Size.y + 0.5f);

			return R_SUCCESS;
		}

		l_Result = i_pParamList[0]->getTextureSize(o_Size);
		if (DM_SUCC(l_Result))
			return l_Result;

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
MDOperandPtr        MDOperatorSceneRender::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	PERF_COUNTER_EX(0);
	FRAMETIMER_BEGIN(FTT_RC_CAL_SCENERENDER, FTT_RC_CAL_EXECUTE_EFFECTPASS);
    DOME_ASSERT(i_ParamCount == 5);
    DOME_ASSERT(i_pParamList[0]&& i_pParamList[0]->isTexture());			//< Depth
    DOME_ASSERT(i_pParamList[1]&& i_pParamList[1]->isFloat4());				//< Param0
    DOME_ASSERT(i_pParamList[2]&& i_pParamList[2]->isFloat4());				//< Param1
	DOME_ASSERT(i_pParamList[3]&& i_pParamList[3]->isFloat4());				//< ClearColor
    DOME_ASSERT(i_pParamList[4]&& i_pParamList[4]->isFloat4());				//< ViewPort
    
    DResult l_Result;
    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

    o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;

    DSimpleTypedValue* l_pDepthValue = i_pParamList[0]->getDataPtr();
    DSimpleTypedValue* l_pParam0Value = i_pParamList[1]->getDataPtr();
	DSimpleTypedValue* l_pParam1Value = i_pParamList[2]->getDataPtr();
    DSimpleTypedValue* l_pClearColor = i_pParamList[3]->getDataPtr();
    DSimpleTypedValue* l_pViewport = i_pParamList[4]->getDataPtr();
    DVector4f l_Params0 = l_pParam0Value->getDVector4f();
	DVector4f l_Params1 = l_pParam1Value->getDVector4f();
    DVector4f l_ClearColor = l_pClearColor->getDVector4f();
    DVector4f l_Viewport = l_pViewport->getDVector4f();
    DVector2i l_RtSize;
    Int       l_RenderType;
    Int       l_BlendMode;
	F32		  l_EnableZTest;

    l_RenderType = Int(l_Params0.z + 0.5f);
    l_BlendMode = Int(l_Params0.w + 0.5f);

	l_EnableZTest = l_Params1.x;

    l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
    DOME_ASSERT(DM_SUCC(l_Result));

    RCGPUDATAFORMAT l_Format = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);
    DOME_ASSERT(l_Format == RGDF_RGBA8 || l_Format == RGDF_RGBA16F);

    OSTexture2D l_RtTex;
    l_Result = l_pRenderer->createTexture2D(l_RtTex, l_RtSize.x, l_RtSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
    DOME_ASSERT(DM_SUCC(l_Result));

    // clear render target if necessary
    if (l_ClearColor.x >= 0.0f)
    {
        l_pRenderer->clearRenderTarget(l_RtTex, l_ClearColor);
    }

	// set viewport
	RCMOD_Float4 l_ViewPort(l_Viewport.x * l_RtSize.x, l_Viewport.y * l_RtSize.y, l_Viewport.z * l_RtSize.x, l_Viewport.w * l_RtSize.y);

	RCMOD_Texture l_ColorTexture;
	*((OSTexture2D*)l_ColorTexture.getPtr()) = l_RtTex;

	RCMOD_Texture l_DepthTexture;
	*((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthValue->getValue<OSTexture2D>();

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);
    
	l_pScenePlugin->renderMainCamera_SceneRender((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_ColorTexture, &l_DepthTexture, l_ViewPort, l_RenderType, l_EnableZTest, 1.0f);

    MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_RtTex);
    DOME_ASSERT(DM_SUCC(l_Result));

	MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
	l_pOperand->addOperand(l_pRtOperand);
	l_pOperand->addOperand(i_pParamList[0]);

	FRAMETIMER_END(FTT_RC_CAL_SCENERENDER);

    return l_pOperand;
}

DResult             MDOperatorSceneRender::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

    OSTexture2D l_hTexture;
	MDOperandValue* l_pRtOperand = (MDOperandValue*)i_pResult->getSubOperand(0);
    l_hTexture = *l_pRtOperand->getTexturePtr();
    l_pRenderer->destroyTexture2D(l_hTexture);
    DOME_Del(l_pRtOperand);

	DOME_Del(i_pResult);

    return R_SUCCESS;
}


RC_NAMESPACE_END