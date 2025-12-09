#include "pch.h"
/*
    filename:       mdoperatorgeneralresizetex.cpp
    author:         Ming Dong
    date:           2017-DEC-17
    description:    
*/

#include "mdoperatorgeneralresizetex.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

MDOperatorGeneralResizeTex::MDOperatorGeneralResizeTex()
    : m_OperatorName("MDGeneralResizeTex")
{

}

MDOperatorGeneralResizeTex::~MDOperatorGeneralResizeTex()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorGeneralResizeTex::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorGeneralResizeTex::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorGeneralResizeTex::getInputCount() const
{
    return 9;
}

DSimpleTypeID       MDOperatorGeneralResizeTex::getInputTypeID(Int i_Index) const
{
    switch (i_Index)
    {
    case 0:
    case 1:
        return RCGlobal::k_SimpleTypeID_OSTexture2D;
    default:
        return RCGlobal::k_SimpleTypeID_F32;
    }
    return RCGlobal::k_SimpleTypeID_Unknown;
}

Int                 MDOperatorGeneralResizeTex::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorGeneralResizeTex::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorGeneralResizeTex::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 7);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

    RCGPUDATAFORMAT l_Format;
    DResult l_Result = i_pParamList[0]->getTextureFormat(l_Format);
    DOME_ASSERT(DM_SUCC(l_Result));
    return l_Format;
}

DResult             MDOperatorGeneralResizeTex::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 3);
    DOME_ASSERT(i_pParamList[0]->isTexture());      // source texture
    DOME_ASSERT(i_pParamList[1]->isTexture());      // size texture
    DOME_ASSERT(i_pParamList[2]->isFloat());        // size mode
    DOME_ASSERT(i_pParamList[3]->isFloat());        // fixed size x
    DOME_ASSERT(i_pParamList[4]->isFloat());        // fixed size y
    DOME_ASSERT(i_pParamList[5]->isFloat());        // size scale x
    DOME_ASSERT(i_pParamList[6]->isFloat());        // size scale y
    DOME_ASSERT(i_pParamList[7]->isFloat());        // size adder x
    DOME_ASSERT(i_pParamList[8]->isFloat());        // size adder y

    DResult     l_Result;
    DVector2i   l_Tex0Size;
    DVector2i   l_Tex1Size;
    float       l_SizeMode;
    float       l_FixedWidth;
    float       l_FixedHeight;
    float       l_ScaleX;
    float       l_ScaleY;
    float       l_AdderX;
    float       l_AdderY;

    l_Result = i_pParamList[0]->getTextureSize(l_Tex0Size);
    l_Result = i_pParamList[1]->getTextureSize(l_Tex1Size);
    l_Result = i_pParamList[2]->getFloat(l_SizeMode);
    l_Result = i_pParamList[3]->getFloat(l_FixedWidth);
    l_Result = i_pParamList[4]->getFloat(l_FixedHeight);
    l_Result = i_pParamList[5]->getFloat(l_ScaleX);
    l_Result = i_pParamList[6]->getFloat(l_ScaleY);
    l_Result = i_pParamList[7]->getFloat(l_AdderX);
    l_Result = i_pParamList[8]->getFloat(l_AdderY);

    if (l_SizeMode < 0.5f)
    {
        o_Size.x = int(l_Tex0Size.x * l_ScaleX + l_AdderX);
        o_Size.y = int(l_Tex0Size.y * l_ScaleY + l_AdderY);
    }
    else if(l_SizeMode < 1.5f)
    {
        o_Size.x = int(l_Tex1Size.x * l_ScaleX + l_AdderX);
        o_Size.y = int(l_Tex1Size.y * l_ScaleY + l_AdderY);
    }
    else
    {
        o_Size.x = int(l_FixedWidth);
        o_Size.y = int(l_FixedHeight);
    }

    if (o_Size.x <= 0)
        o_Size.x = 1;
    if (o_Size.y <= 0)
        o_Size.y = 1;

    return R_SUCCESS;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorGeneralResizeTex::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    DResult l_Result;
    DOME_ASSERT(i_ParamCount == 3);
    DOME_ASSERT(i_pParamList[0]->isTexture());      // source texture
    DOME_ASSERT(i_pParamList[1]->isTexture());      // size texture
    DOME_ASSERT(i_pParamList[2]->isFloat());        // size mode
    DOME_ASSERT(i_pParamList[3]->isFloat());        // fixed size x
    DOME_ASSERT(i_pParamList[4]->isFloat());        // fixed size y
    DOME_ASSERT(i_pParamList[5]->isFloat());        // size scale x
    DOME_ASSERT(i_pParamList[6]->isFloat());        // size scale y
    DOME_ASSERT(i_pParamList[7]->isFloat());        // size adder x
    DOME_ASSERT(i_pParamList[8]->isFloat());        // size adder y

    RCEffectManager* l_pEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pEffectMgr->getRenderer();

    o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[5] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[6] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[7] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[8] = IRP_AFTEREXECUTE;

    OSTexture2D l_SrcTex = *i_pParamList[0]->getTexturePtr();

    DVector2i l_RtSize;
    l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
    DOME_ASSERT(DM_SUCC(l_Result));
    RCGPUDATAFORMAT l_Format = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);

    OSTexture2D l_RtTex;
    l_Result = l_pRenderer->createTexture2D(l_RtTex, l_RtSize.x, l_RtSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
    DOME_ASSERT(DM_SUCC(l_Result));

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pEffectMgr->getPlugin(k_KEY_ScenePlugin);

    RCMOD_Texture l_SourceTexture;
	*((OSTexture2D*)l_SourceTexture.getPtr()) = l_SrcTex;

	RCMOD_Texture l_DestTexture;
	*((OSTexture2D*)l_DestTexture.getPtr()) = l_RtTex;

    if(l_Format == RGDF_D24S8 || l_Format == RGDF_D32F)
        l_pScenePlugin->RCP_CopyDepth((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_SourceTexture, &l_DestTexture);
    else
        l_pScenePlugin->RCP_CopyTexture((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_SourceTexture, &l_DestTexture);

    MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_RtTex);
    DOME_ASSERT(DM_SUCC(l_Result));

    return l_pRtOperand;
}

DResult             MDOperatorGeneralResizeTex::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

    OSTexture2D l_hTexture;
    l_hTexture = *i_pResult->getTexturePtr();
    l_pRenderer->destroyTexture2D(l_hTexture);

	DOME_Del(i_pResult);

    return R_SUCCESS;
}


RC_NAMESPACE_END