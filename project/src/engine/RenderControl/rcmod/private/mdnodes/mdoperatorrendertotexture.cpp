#include "pch.h"
/*
    filename:       mdoperatorscenerender.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "MDOperatorRenderToTexture.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)
static const unsigned int nParamCount = 3;
MDOperatorRenderToTexture::MDOperatorRenderToTexture()
    : m_OperatorName("MDRenderToTexture")
{
	m_nFileIndex = 0;
	m_fTimer = -1.0f;
	m_emState = MDWT_WAITING;
}

MDOperatorRenderToTexture::~MDOperatorRenderToTexture() 
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorRenderToTexture::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorRenderToTexture::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorRenderToTexture::getInputCount() const
{
    return nParamCount;
}

DSimpleTypeID       MDOperatorRenderToTexture::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index < 0 || i_Index >= nParamCount);
	DSimpleTypeID InputTypes[nParamCount] = {
		RCGlobal::k_SimpleTypeID_OSTexture2D,		//Color		Output
		RCGlobal::k_SimpleTypeID_F32,				//Color		Output
		RCGlobal::k_SimpleTypeID_DString			//Color		Output
	};
	return InputTypes[i_Index];
}

Int                 MDOperatorRenderToTexture::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorRenderToTexture::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorRenderToTexture::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);

	return RGDF_RGBA16F;
}

DResult             MDOperatorRenderToTexture::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
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
MDOperandPtr        MDOperatorRenderToTexture::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_VOLUME_CLOUD, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isFloat());
	//DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isString ());
	
	DResult l_Result;
    
    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    

	o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;

	DSimpleTypedValue* l_pColorOutputValue	= i_pParamList[0]->getDataPtr();
	DSimpleTypedValue* l_pWriteFrequency	= i_pParamList[1]->getDataPtr();
	DSimpleTypedValue* l_pStrFilePath		= i_pParamList[2]->getDataPtr();

	RCMOD_Texture l_ColorOutputTexture;
	*((OSTexture2D*)l_ColorOutputTexture.getPtr()) = l_pColorOutputValue->getValue<OSTexture2D>();

	float l_WriteFrequency = l_pWriteFrequency->getF32();

	DString l_FilePath = l_pStrFilePath->getDString();
	RCMOD_String l_rcms_FilePath(l_FilePath.c_str());

	switch (m_emState)
	{
	case dome::MDWT_WAITING:
	{
		m_fTimer--;
		if (m_fTimer < 0)
		{
			m_fTimer = l_WriteFrequency;
			m_emState = MDWT_WRITING;
		}
	}
		break;
	case dome::MDWT_WRITING:
	{
		_WriteToTexture(l_pRCEffectMgr, &l_ColorOutputTexture, l_rcms_FilePath);
		m_emState = MDWT_WAITING;
	}
		break;
	default:
		break;
	}

	MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_Result = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, l_ColorOutputTexture.getPtr());
	DOME_ASSERT(DM_SUCC(l_Result));

	FRAMETIMER_END(FTT_RC_CAL_RENDERCOMPOSITE);

	return l_pRtOperand;
}

DResult             MDOperatorRenderToTexture::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	return R_SUCCESS;
}

void MDOperatorRenderToTexture::_WriteToTexture(RCEffectManager* i_EffectManager, RCMOD_Texture* i_ColorSrcTexture, RCMOD_String i_FilePath) const
{
	char pszFinalFilePath[MAX_PATH] = { 0 };
	char pszDrive[_MAX_DRIVE] = { 0 };
	char pszDir[_MAX_DIR] = { 0 };
	char pszFname[_MAX_FNAME] = { 0 };
	char pszExt[_MAX_EXT] = { 0 };
	_splitpath_s(i_FilePath.c_str(), pszDrive, pszDir, pszFname, pszExt);
	if (strcmp(pszExt, "") == 0)
	{
		strcpy_s(pszExt, _MAX_EXT, "dds");
	}

	sprintf_s(pszFinalFilePath, MAX_PATH, "%s%s%s_%d.%s", pszDrive, pszDir, pszFname, m_nFileIndex++, pszExt);

	RCRenderer* l_pRenderer = i_EffectManager->getRenderer();

	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)i_EffectManager->getPlugin(k_KEY_ScenePlugin);

	l_pScenePlugin->RenderRenderToTexture(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(),
		i_ColorSrcTexture,
		pszFinalFilePath
	);
}


RC_NAMESPACE_END