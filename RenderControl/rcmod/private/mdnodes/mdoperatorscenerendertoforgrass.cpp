#include "pch.h"
/*
    filename:       mdoperatorscenerendertoforgrass.cpp
    author:         Ming Dong
    date:           2021-MAR-31
    description:    
*/

#include "mdoperatorscenerendertoforgrass.h"
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
static unsigned int nParamCount = 5;
MDOperatorSceneRenderToForGrass::MDOperatorSceneRenderToForGrass()
    : m_OperatorName("MDSceneRenderToForGrass")
{

}

MDOperatorSceneRenderToForGrass::~MDOperatorSceneRenderToForGrass()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString& MDOperatorSceneRenderToForGrass::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorSceneRenderToForGrass::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorSceneRenderToForGrass::getInputCount() const
{
    return nParamCount;
}

DSimpleTypeID       MDOperatorSceneRenderToForGrass::getInputTypeID(Int i_Index) const
{
    if(i_Index == 0) return RCGlobal::k_SimpleTypeID_OSTexture2D;
    return RCGlobal::k_SimpleTypeID_DVector4f;
}

Int                 MDOperatorSceneRenderToForGrass::getOutputCount() const
{
    return 2;
}

DSimpleTypeID       MDOperatorSceneRenderToForGrass::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorSceneRenderToForGrass::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

	RCGPUDATAFORMAT l_Format;
    DResult l_Result;
    if (i_Index == 0)
    {
	    l_Result = i_pParamList[0]->getTextureFormat(l_Format);
	    DOME_ASSERT(DM_SUCC(l_Result));
	    return l_Format;
    }
    else if (i_Index == 1)
    {
	    l_Result = i_pParamList[1]->getTextureFormat(l_Format);
	    DOME_ASSERT(DM_SUCC(l_Result));
	    return l_Format;
    }
    else
        return RGDF_UNKNOWN;
}

DResult             MDOperatorSceneRenderToForGrass::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    DOME_ASSERT(i_pParamList[0]	&& i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
    DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isFloat4());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isFloat4());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());

    return i_pParamList[0]->getTextureSize(o_Size);
    if (i_Index == 0)
    {
	    return i_pParamList[0]->getTextureSize(o_Size);
    }
    else if (i_Index == 1)
    {
	    return i_pParamList[1]->getTextureSize(o_Size);
    }
    else
        return R_FAILED;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorSceneRenderToForGrass::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	PERF_COUNTER_EX(1);
	FRAMETIMER_BEGIN(FTT_RC_CAL_SCENERENDERTO, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isFloat4());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isFloat4());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());
    
    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

    o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;

	DSimpleTypedValue* l_pColorValue = i_pParamList[0]->getDataPtr();
    DSimpleTypedValue* l_pDepthValue = i_pParamList[1]->getDataPtr();
    DSimpleTypedValue* l_pParamValue = i_pParamList[2]->getDataPtr();
	DSimpleTypedValue* l_pViewport	 = i_pParamList[3]->getDataPtr();
	DSimpleTypedValue* l_pGrassTexValue = i_pParamList[4]->getDataPtr();

    DVector4f l_Params		= l_pParamValue->getDVector4f();
	DVector4f l_Viewport	=	l_pViewport->getDVector4f();
    Int       l_RenderType;
	F32		  l_EmisiiveMultiplier;
    l_RenderType = Int(l_Params.x + 0.5f);
	l_EmisiiveMultiplier = l_Params.y;
    
	// set viewport
	DVector2i l_RtSize;
	calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);

	RCMOD_Float4 l_ViewPort(l_Viewport.x * l_RtSize.x, l_Viewport.y * l_RtSize.y, l_Viewport.z * l_RtSize.x, l_Viewport.w * l_RtSize.y);
	
	RCMOD_Texture l_ColorTexture;
	*((OSTexture2D*)l_ColorTexture.getPtr()) = l_pColorValue->getValue<OSTexture2D>();

	RCMOD_Texture l_DepthTexture;
	*((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthValue->getValue<OSTexture2D>();

	RCMOD_Texture l_GrassTexture;
	*((OSTexture2D*)l_GrassTexture.getPtr()) = l_pGrassTexValue->getValue<OSTexture2D>();

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
    RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);


	l_pScenePlugin->renderMainCamera_SceneRender((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_ColorTexture, &l_DepthTexture, l_ViewPort, l_RenderType, 1.0f, l_EmisiiveMultiplier, &l_GrassTexture);
#if 0
    switch (l_RenderType)
    {
	
	case RCPI_Scene::SRTOT_ENVIRONMENT:
		l_pScenePlugin->renderMainCamera_Environment((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_DepthTexture);
		break;
	case RCPI_Scene::SRTOT_BLENDOBJECT:
		l_pScenePlugin->renderMainCamera_BlendObject((RCOSRendererData*)l_pRenderer->getOSRendererData());
		break;
	case RCPI_Scene::SRTOT_PARTICLESYSTEM:
		l_pScenePlugin->renderMainCamera_ParticleSystem((RCOSRendererData*)l_pRenderer->getOSRendererData());
		break;
	case RCPI_Scene::SRTOT_DEBUGOBJECT:
		l_pScenePlugin->renderMainCamera_DebugObject((RCOSRendererData*)l_pRenderer->getOSRendererData());
		break;
	case RCPI_Scene::SRTOT_LANDSCAPEWATER:
		l_pScenePlugin->renderMainCamera_LandScapeWater((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_ColorTexture, &l_DepthTexture);
		break;
	case RCPI_Scene::SRTOT_TERRAINWATER:
		l_pScenePlugin->renderMainCamera_TerrainWater((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_ColorTexture, &l_DepthTexture);
		break;
	default:
		break;
    }
#endif
	
	MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
	l_pOperand->addOperand(i_pParamList[0]);
	l_pOperand->addOperand(i_pParamList[1]);

	FRAMETIMER_END(FTT_RC_CAL_SCENERENDERTO);

    return l_pOperand; 
}

DResult             MDOperatorSceneRenderToForGrass::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	DOME_Del(i_pResult);
    return R_SUCCESS;
}


RC_NAMESPACE_END