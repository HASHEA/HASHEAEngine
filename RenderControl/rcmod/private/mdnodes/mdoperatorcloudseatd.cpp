#include "pch.h"
/*
    filename:       mdoperatorscenerender.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "MDOperatorCloudSeaTD.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)
static const int s_InputCount = 7;
MDOperatorCloudSeaTD::MDOperatorCloudSeaTD()
    : m_OperatorName("MDCloudSeaTD")
{

}

MDOperatorCloudSeaTD::~MDOperatorCloudSeaTD()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorCloudSeaTD::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorCloudSeaTD::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorCloudSeaTD::getInputCount() const
{
    return s_InputCount;
}

DSimpleTypeID       MDOperatorCloudSeaTD::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index < 0 || i_Index >= s_InputCount);
	DSimpleTypeID InputTypes[s_InputCount] = {
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture3D,
		RCGlobal::k_SimpleTypeID_OSTexture3D,
		RCGlobal::k_SimpleTypeID_DVector4f,
		RCGlobal::k_SimpleTypeID_DVector4f,
		RCGlobal::k_SimpleTypeID_DMatrix4x4f
	};
	return InputTypes[i_Index];
}

Int                 MDOperatorCloudSeaTD::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorCloudSeaTD::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorCloudSeaTD::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	return RGDF_RGBA16F;
}

DResult             MDOperatorCloudSeaTD::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == s_InputCount);

    DResult l_Result;

    l_Result = i_pParamList[0]->getTextureSize(o_Size);
	DOME_ASSERT(DM_SUCC(l_Result));

    return R_SUCCESS;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorCloudSeaTD::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_SSSDEFERD, FTT_RC_CAL_EXECUTE_EFFECTPASS);
    DOME_ASSERT(i_ParamCount == s_InputCount);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture3D());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture3D());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isFloat4());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isFloat4());
	DOME_ASSERT(i_pParamList[6] && i_pParamList[6]->isMatrix4x4());
	
    
//    DResult l_Result;
    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

    o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[2] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[3] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[4] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[5] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[6] = IRP_INFINISHCALLBACK;

#define GetTexture2D(name, index) \
	RCMOD_Texture l_##name; \
	DSimpleTypedValue* l_p##name = i_pParamList[index]->getDataPtr();\
	*((OSTexture2D*)l_##name.getPtr()) = l_p##name->getValue<OSTexture2D>();

	GetTexture2D(SceneColor, 0);
	GetTexture2D(ResolvedDepth, 1);

	RCMOD_Texture l_Density3D;
	DSimpleTypedValue* l_pDensity3D = i_pParamList[2]->getDataPtr();
	*((OSTexture3D*)l_Density3D.getPtr()) = l_pDensity3D->getValue<OSTexture3D>();

	RCMOD_Texture l_Noise3D;
	DSimpleTypedValue* l_pNoise3D = i_pParamList[3]->getDataPtr(); 
	* ((OSTexture3D*)l_Noise3D.getPtr()) = l_pNoise3D->getValue<OSTexture3D>();

	DSimpleTypedValue* l_pParam0 = i_pParamList[4]->getDataPtr();
	DSimpleTypedValue* l_pParam1 = i_pParamList[5]->getDataPtr();
	DSimpleTypedValue* l_pParam2 = i_pParamList[6]->getDataPtr();

	RCMOD_Float4 f40;
	*(DVector4f*)f40.getPtr()= l_pParam0->getDVector4f();

	RCMOD_Float4 f41;
	*(DVector4f*)f41.getPtr() = l_pParam1->getDVector4f();

	RCMOD_Float4x4 l_Param3;
	*(DMatrix4x4f*)l_Param3.getPtr() = (l_pParam2->getDMatrix4x4f());

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);
    
	l_pScenePlugin->RenderCloudSeaTD(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(),
		&l_SceneColor, &l_ResolvedDepth, &l_Density3D, &l_Noise3D, f40, f41, l_Param3);

	return i_pParamList[0];
}

DResult             MDOperatorCloudSeaTD::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	return R_SUCCESS;
}


RC_NAMESPACE_END