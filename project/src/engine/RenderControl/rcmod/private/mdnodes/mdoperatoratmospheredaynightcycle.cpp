#include "pch.h"
#include "mdoperatoratmospheredaynightcycle.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)
static const unsigned int nParamCount = 13;
MDOperatorAtmosphereDayNightCycle::MDOperatorAtmosphereDayNightCycle()
    : m_OperatorName("MDAtmosphereDayNightCycle")
{

}

MDOperatorAtmosphereDayNightCycle::~MDOperatorAtmosphereDayNightCycle()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString& MDOperatorAtmosphereDayNightCycle::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorAtmosphereDayNightCycle::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorAtmosphereDayNightCycle::getInputCount() const
{
    return nParamCount;
}

DSimpleTypeID       MDOperatorAtmosphereDayNightCycle::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index < 0 || i_Index >= nParamCount);
	DSimpleTypeID InputTypes[nParamCount] = {
		RCGlobal::k_SimpleTypeID_OSTexture2D,		
		RCGlobal::k_SimpleTypeID_OSTexture2D,		
		RCGlobal::k_SimpleTypeID_OSTexture2D,		
		RCGlobal::k_SimpleTypeID_OSTexture2D,		
		RCGlobal::k_SimpleTypeID_DVector3f,			
		RCGlobal::k_SimpleTypeID_DVector3f,			
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_F32,
		RCGlobal::k_SimpleTypeID_DMatrix4x4f,
		RCGlobal::k_SimpleTypeID_DMatrix4x4f,
		RCGlobal::k_SimpleTypeID_DMatrix4x4f,
		RCGlobal::k_SimpleTypeID_DMatrix4x4f
	};
	return InputTypes[i_Index];
}

Int                 MDOperatorAtmosphereDayNightCycle::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorAtmosphereDayNightCycle::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorAtmosphereDayNightCycle::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);

	return RGDF_RGBA16F;
}

DResult             MDOperatorAtmosphereDayNightCycle::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    DOME_ASSERT(i_pParamList[0]	&& i_pParamList[0]->isTexture());	

	DVector2i l_ColorBufferSize;	
	i_pParamList[0]->getTextureSize(l_ColorBufferSize);

	o_Size = l_ColorBufferSize;
    return R_SUCCESS;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorAtmosphereDayNightCycle::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    PERF_COUNTER_EX(0);
	FRAMETIMER_BEGIN(FTT_RC_CAL_VOLUME_CLOUD, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == nParamCount);
	/*DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[3]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isFloat3());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isFloat3());
	DOME_ASSERT(i_pParamList[6] && i_pParamList[6]->isMatrix4x4());*/
	
//	DResult l_Result;
    
    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

	o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[5] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[6] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[7] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[8] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[9] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[10] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[11] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[12] = IRP_AFTEREXECUTE;

	DSimpleTypedValue* l_pColorOutputValue				= i_pParamList[0]->getDataPtr();
    DSimpleTypedValue* l_pColorInputValue				= i_pParamList[1]->getDataPtr();
	DSimpleTypedValue* l_pDepthInputValue				= i_pParamList[2]->getDataPtr();	
	DSimpleTypedValue* l_pGBufferAInputValue			= i_pParamList[3]->getDataPtr();
	DSimpleTypedValue* l_pf3SunDir						= i_pParamList[4]->getDataPtr();
	DSimpleTypedValue* l_pf3MoonDir						= i_pParamList[5]->getDataPtr();
	DSimpleTypedValue* l_pMilkywayValue					= i_pParamList[6]->getDataPtr();
	DSimpleTypedValue* l_pMoonValue						= i_pParamList[7]->getDataPtr();
	DSimpleTypedValue* l_pTime							= i_pParamList[8]->getDataPtr();
	DSimpleTypedValue* l_pmatParam0						= i_pParamList[9]->getDataPtr(); //day sky matrix
	DSimpleTypedValue* l_pmatParam1						= i_pParamList[10]->getDataPtr(); //color matrix
	DSimpleTypedValue* l_pmatParam2						= i_pParamList[11]->getDataPtr(); //night sky matrix
	DSimpleTypedValue* l_pmatParam3						= i_pParamList[12]->getDataPtr(); // milkyway rotation matrix
	
	RCMOD_Texture l_ColorOutputTexture;
	*((OSTexture2D*)l_ColorOutputTexture.getPtr()) = l_pColorOutputValue->getValue<OSTexture2D>();

	RCMOD_Texture l_ColorSrcTexture;
	*((OSTexture2D*)l_ColorSrcTexture.getPtr()) = l_pColorInputValue->getValue<OSTexture2D>();

	RCMOD_Texture l_DepthTexture;
	*((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthInputValue->getValue<OSTexture2D>();

	RCMOD_Texture l_GBufferATexture;
	*((OSTexture2D*)l_GBufferATexture.getPtr()) = l_pGBufferAInputValue->getValue<OSTexture2D>();

	RCMOD_Float3 l_SunDir;
	*(DVector3f*)l_SunDir.getPtr() = (l_pf3SunDir->getDVector3f());

	RCMOD_Float3 l_SkyDir;
	*(DVector3f*)l_SkyDir.getPtr() = (l_pf3MoonDir->getDVector3f());

	RCMOD_Texture l_MilkywayTexture;
	*((OSTexture2D*)l_MilkywayTexture.getPtr()) = l_pMilkywayValue->getValue<OSTexture2D>();

	RCMOD_Texture l_MoonTexture;
	*((OSTexture2D*)l_MoonTexture.getPtr()) = l_pMoonValue->getValue<OSTexture2D>();

	float l_Time;
	l_Time = l_pTime->getF32();

	RCMOD_Float4x4 l_Param0;
	*(DMatrix4x4f*)l_Param0.getPtr() = (l_pmatParam0->getDMatrix4x4f());

	RCMOD_Float4x4 l_Param1;
	*(DMatrix4x4f*)l_Param1.getPtr() = (l_pmatParam1->getDMatrix4x4f());

	RCMOD_Float4x4 l_Param2;
	*(DMatrix4x4f*)l_Param2.getPtr() = (l_pmatParam2->getDMatrix4x4f());

	RCMOD_Float4x4 l_Param3;
	*(DMatrix4x4f*)l_Param3.getPtr() = (l_pmatParam3->getDMatrix4x4f());

    // render
    const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
    RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);
	
	l_pScenePlugin->RenderAtmosphereDayNightCycle(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(), 
		&l_ColorOutputTexture,		
		&l_ColorSrcTexture, 
		&l_DepthTexture,
		&l_GBufferATexture,
		l_SunDir,
		l_SkyDir,
		l_Time,
		&l_MilkywayTexture,
		&l_MoonTexture,
		l_Param0,
		l_Param1,
		l_Param2,
		l_Param3
		);

	return i_pParamList[0];
}

DResult             MDOperatorAtmosphereDayNightCycle::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	return R_SUCCESS;
}


RC_NAMESPACE_END