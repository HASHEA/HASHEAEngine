#include "pch.h"
#include "mdoperatorfograymarch.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

static const unsigned int nParamCount = 13;
MDOperatorFogRayMarch::MDOperatorFogRayMarch()
    : m_OperatorName("MDFogRayMarch")
{

}

MDOperatorFogRayMarch::~MDOperatorFogRayMarch()
{

}

const DString& MDOperatorFogRayMarch::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorFogRayMarch::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorFogRayMarch::getInputCount() const
{
    return nParamCount;
}

DSimpleTypeID       MDOperatorFogRayMarch::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index < 0 || i_Index >= nParamCount);
	DSimpleTypeID InputTypes[nParamCount] = {
		RCGlobal::k_SimpleTypeID_OSTexture2D,		
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_DMatrix4x4f,
		RCGlobal::k_SimpleTypeID_DMatrix4x4f,
		RCGlobal::k_SimpleTypeID_DMatrix4x4f,
		RCGlobal::k_SimpleTypeID_DMatrix4x4f,
		RCGlobal::k_SimpleTypeID_DMatrix4x4f,
		RCGlobal::k_SimpleTypeID_DMatrix4x4f,
		RCGlobal::k_SimpleTypeID_DString,
		RCGlobal::k_SimpleTypeID_DString
	};
	return InputTypes[i_Index];
}

Int                 MDOperatorFogRayMarch::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorFogRayMarch::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorFogRayMarch::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);

	return RGDF_RGBA16F;
}

DResult             MDOperatorFogRayMarch::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
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
MDOperandPtr        MDOperatorFogRayMarch::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    PERF_COUNTER_EX(0);
	FRAMETIMER_BEGIN(FTT_RC_CAL_FOG_RAYMARCH, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	
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
	o_pInputReleasePoint[9] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[10] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[11] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[12] = IRP_AFTEREXECUTE;

	DSimpleTypedValue* l_pColorInputValue	= i_pParamList[0]->getDataPtr();
	DSimpleTypedValue* l_pDepthInputValue	= i_pParamList[1]->getDataPtr();
	DSimpleTypedValue* l_pBlueNoiseValue	= i_pParamList[2]->getDataPtr();
	DSimpleTypedValue* l_pSkyBlurInputValue = i_pParamList[3]->getDataPtr();
	DSimpleTypedValue* l_pSkyInputValue		= i_pParamList[4]->getDataPtr();
	DSimpleTypedValue* l_pmatParam0			= i_pParamList[5]->getDataPtr();
	DSimpleTypedValue* l_pmatParam1			= i_pParamList[6]->getDataPtr();
	DSimpleTypedValue* l_pmatParam2			= i_pParamList[7]->getDataPtr();
	DSimpleTypedValue* l_pmatParam3			= i_pParamList[8]->getDataPtr();
	DSimpleTypedValue* l_pmatParam4			= i_pParamList[9]->getDataPtr();
	DSimpleTypedValue* l_pmatParam5 = i_pParamList[10]->getDataPtr();
	DSimpleTypedValue* l_BaseNoiseValue = i_pParamList[11]->getDataPtr();
	DSimpleTypedValue* l_DetailNoiseValuie = i_pParamList[12]->getDataPtr();
	
	DVector2i l_RtSize;
	l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
	DOME_ASSERT(DM_SUCC(l_Result));

	RCGPUDATAFORMAT l_Format = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);
	DOME_ASSERT(l_Format == RGDF_RGBA8 || l_Format == RGDF_RGBA16F);

	OSTexture2D l_RtTex;
	l_Result = l_pRenderer->createTexture2D(l_RtTex, l_RtSize.x, l_RtSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(l_Result));

	DVector4f f4clearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
	l_pRenderer->clearRenderTarget(l_RtTex, f4clearColor);

	RCMOD_Texture l_ColorSrcTexture;
	*((OSTexture2D*)l_ColorSrcTexture.getPtr()) = l_pColorInputValue->getValue<OSTexture2D>();

	RCMOD_Texture l_ColorOutputTexture;
	*((OSTexture2D*)l_ColorOutputTexture.getPtr()) = l_RtTex;

	RCMOD_Texture l_DepthTexture;
	*((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthInputValue->getValue<OSTexture2D>();

	RCMOD_Texture l_BlueNoiseTexture;
	*((OSTexture2D*)l_BlueNoiseTexture.getPtr()) = l_pBlueNoiseValue->getValue<OSTexture2D>();

	RCMOD_Texture l_SkyBlurSrcTexture;
	*((OSTexture2D*)l_SkyBlurSrcTexture.getPtr()) = l_pSkyBlurInputValue->getValue<OSTexture2D>();

	RCMOD_Texture l_SkySrcTexture;
	*((OSTexture2D*)l_SkySrcTexture.getPtr()) = l_pSkyInputValue->getValue<OSTexture2D>();

	RCMOD_Float4x4 l_Param0;
	*(DMatrix4x4f*)l_Param0.getPtr() = (l_pmatParam0->getDMatrix4x4f());

	RCMOD_Float4x4 l_Param1;
	*(DMatrix4x4f*)l_Param1.getPtr() = (l_pmatParam1->getDMatrix4x4f());

	RCMOD_Float4x4 l_Param2;
	*(DMatrix4x4f*)l_Param2.getPtr() = (l_pmatParam2->getDMatrix4x4f());

	RCMOD_Float4x4 l_Param3;
	*(DMatrix4x4f*)l_Param3.getPtr() = (l_pmatParam3->getDMatrix4x4f());

	RCMOD_Float4x4 l_Param4;
	*(DMatrix4x4f*)l_Param4.getPtr() = (l_pmatParam4->getDMatrix4x4f());

	RCMOD_Float4x4 l_Param5;
	*(DMatrix4x4f*)l_Param5.getPtr() = (l_pmatParam5->getDMatrix4x4f());

	DString l_BaseNoiseFile;
	l_BaseNoiseFile = l_BaseNoiseValue->getDString();
	DString l_BaseNoiseFullPath;
	{

		if (l_BaseNoiseFile.isBeginWith("data"))
			l_BaseNoiseFullPath = l_BaseNoiseFile;
		else
		{
			l_pRenderer->getDataPath(l_BaseNoiseFullPath);
			l_BaseNoiseFullPath += l_BaseNoiseFile;
		}
	}
	DString l_DetailNoiseFile;
	l_DetailNoiseFile = l_DetailNoiseValuie->getDString();
	DString l_DetailNoiseFullPath;
	{

		if (l_DetailNoiseFile.isBeginWith("data"))
			l_DetailNoiseFullPath = l_DetailNoiseFile;
		else
		{
			l_pRenderer->getDataPath(l_DetailNoiseFullPath);
			l_DetailNoiseFullPath += l_DetailNoiseFile;
		}
	}
	
	RCMOD_String l_rc_BaseNoiseFile(l_BaseNoiseFullPath.c_str());
	RCMOD_String l_rc_DetailNoiseFile(l_DetailNoiseFullPath.c_str());
    // render
    const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
    RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);
	
	l_pScenePlugin->RenderFogRayMarch(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(),
		&l_ColorOutputTexture,
		&l_ColorSrcTexture,
		&l_DepthTexture,
		&l_BlueNoiseTexture,
		&l_SkyBlurSrcTexture,
		&l_SkySrcTexture,
		l_Param0,
		l_Param1,
		l_Param2,
		l_Param3,
		l_Param4,
		l_Param5,
		l_rc_BaseNoiseFile,
		l_rc_DetailNoiseFile
	); 

	MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);

	l_Result = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_RtTex);
	DOME_ASSERT(DM_SUCC(l_Result));

	return l_pRtOperand;
}

DResult             MDOperatorFogRayMarch::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	OSTexture2D l_hTexture;
	l_hTexture = *i_pResult->getTexturePtr();
	l_pRenderer->destroyTexture2D(l_hTexture);
	DOME_Del(i_pResult);

	return R_SUCCESS;
}


RC_NAMESPACE_END