#include "pch.h"

#include "mdoperatorforwardweatherparam.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

MDOperatorForwardWeatherParam::MDOperatorForwardWeatherParam()
    : m_OperatorName("MDForwardWeatherParam")
{

}

MDOperatorForwardWeatherParam::~MDOperatorForwardWeatherParam()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorForwardWeatherParam::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorForwardWeatherParam::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorForwardWeatherParam::getInputCount() const
{
    return 5;
}

DSimpleTypeID       MDOperatorForwardWeatherParam::getInputTypeID(Int i_Index) const
{
    switch (i_Index)
    {
    case 0:
    case 3:
    case 4:
        return RCGlobal::k_SimpleTypeID_OSTexture2D;
    case 1:
    case 2:
        return RCGlobal::k_SimpleTypeID_F32;
    }
    return RCGlobal::k_SimpleTypeID_Unknown;
}

Int                 MDOperatorForwardWeatherParam::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorForwardWeatherParam::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorForwardWeatherParam::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 5);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

    RCGPUDATAFORMAT l_Format;
    DResult l_Result = i_pParamList[0]->getTextureFormat(l_Format);
    DOME_ASSERT(DM_SUCC(l_Result));
    return l_Format;
}

DResult             MDOperatorForwardWeatherParam::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 5);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

    return i_pParamList[0]->getTextureSize(o_Size);
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorForwardWeatherParam::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    DResult l_Result;
    DOME_ASSERT(i_ParamCount == 5);
    DOME_ASSERT(i_pParamList[0]->isTexture());      // source texture
    DOME_ASSERT(i_pParamList[1]->isFloat());
    DOME_ASSERT(i_pParamList[2]->isFloat());

    RCEffectManager* l_pEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pEffectMgr->getRenderer();

    o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;

    OSTexture2D l_SrcTex = *i_pParamList[0]->getTexturePtr();

    float BorderNoiseTiling, BorderNoiseFactorTiling;

    l_Result = i_pParamList[1]->getFloat(BorderNoiseTiling);
    DOME_ASSERT(DM_SUCC(l_Result));
    l_Result = i_pParamList[2]->getFloat(BorderNoiseFactorTiling);
    DOME_ASSERT(DM_SUCC(l_Result));

	DSimpleTypedValue* l_pColorNoiseFile = i_pParamList[3]->getDataPtr();
	DSimpleTypedValue* l_pMaskFile = i_pParamList[4]->getDataPtr();

    DOME_ASSERT(l_pColorNoiseFile && l_pColorNoiseFile->getTypeID() == RCGlobal::k_SimpleTypeID_DString);
    DOME_ASSERT(l_pMaskFile && l_pMaskFile->getTypeID() == RCGlobal::k_SimpleTypeID_DString);

	DString l_ColorNoiseFile = l_pColorNoiseFile->getDString();
    DString l_ColorNoiseFileFullPath;
    {
		
		if (l_ColorNoiseFile.isBeginWith("data"))
            l_ColorNoiseFileFullPath = l_ColorNoiseFile;
		else
		{
			l_pRenderer->getDataPath(l_ColorNoiseFileFullPath);
            l_ColorNoiseFileFullPath += l_ColorNoiseFile;
		}
    }
	RCMOD_String l_rcms_ColorNoiseFile(l_ColorNoiseFileFullPath.c_str());

	DString l_MaskFile = l_pMaskFile->getDString();
	DString l_MaskFileFullPath;
	{

		if (l_MaskFile.isBeginWith("data"))
            l_MaskFileFullPath = l_MaskFile;
		else
		{
			l_pRenderer->getDataPath(l_MaskFileFullPath);
            l_MaskFileFullPath += l_MaskFile;
		}
	}

	RCMOD_String l_rcms_MaskFile(l_MaskFileFullPath.c_str());
	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pEffectMgr->getPlugin(k_KEY_ScenePlugin);

    l_pScenePlugin->ForwardWeatherParam((RCOSRendererData*)l_pRenderer->getOSRendererData(), l_rcms_ColorNoiseFile, l_rcms_MaskFile, BorderNoiseTiling, BorderNoiseFactorTiling);

    MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_SrcTex);
    DOME_ASSERT(DM_SUCC(l_Result));

    return l_pRtOperand;
}

DResult             MDOperatorForwardWeatherParam::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	DOME_Del(i_pResult);

    return R_SUCCESS;
}


RC_NAMESPACE_END