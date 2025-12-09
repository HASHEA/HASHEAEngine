#include "pch.h"
/*
    filename:       mdoperatordepthgen.cpp
    author:         Ming Dong
    date:           2017-DEC-13
    description:    
*/

#include "mdoperatordepthgen.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

MDOperatorDepthGen::MDOperatorDepthGen()
    : m_OperatorName("MDDepthGen")
{

}

MDOperatorDepthGen::~MDOperatorDepthGen()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorDepthGen::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorDepthGen::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorDepthGen::getInputCount() const
{
    return 5;
}

DSimpleTypeID       MDOperatorDepthGen::getInputTypeID(Int i_Index) const
{
    return RCGlobal::k_SimpleTypeID_F32;
}

Int                 MDOperatorDepthGen::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorDepthGen::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorDepthGen::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 5);
	DOME_ASSERT(i_pParamList[0]->isFloat());      // source texture
	DOME_ASSERT(i_pParamList[1]->isFloat());
	DOME_ASSERT(i_pParamList[2]->isFloat());

    RCGPUDATAFORMAT l_Format;
	F32 l_fFormat = 0.0f;
    DResult l_Result = i_pParamList[2]->getFloat(l_fFormat);
    DOME_ASSERT(DM_SUCC(l_Result));
	if (l_fFormat < 0.5f)
		l_Format = RGDF_D24S8;
	else
		l_Format = RGDF_D32F;

    return l_Format;
}

DResult             MDOperatorDepthGen::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 5);
    DOME_ASSERT(i_pParamList[0]->isFloat());      // source texture
    DOME_ASSERT(i_pParamList[1]->isFloat());
    DOME_ASSERT(i_pParamList[2]->isFloat());

    float l_Width, l_Height;
    DResult l_Result;
    l_Result = i_pParamList[0]->getFloat(l_Width);
    if(DM_FAIL(l_Result))
        return l_Result;
    l_Result = i_pParamList[1]->getFloat(l_Height);
    if(DM_FAIL(l_Result))
        return l_Result;

    o_Size.x = (int)(l_Width + 0.1f);
    o_Size.y = (int)(l_Height + 0.1f);
    return R_SUCCESS;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorDepthGen::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    PERF_COUNTER_EX(0);
    DResult l_Result;
    DOME_ASSERT(i_ParamCount == 5);
    DOME_ASSERT(i_pParamList[0]->isFloat());      // source texture
    DOME_ASSERT(i_pParamList[1]->isFloat());
    DOME_ASSERT(i_pParamList[2]->isFloat());

    RCEffectManager* l_pEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pEffectMgr->getRenderer();

    o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;

	RCGPUDATAFORMAT l_Format;
	F32 l_fFormat = 0.0f;
	l_Result = i_pParamList[2]->getFloat(l_fFormat);
	DOME_ASSERT(DM_SUCC(l_Result));
	if (l_fFormat < 0.5f)
		l_Format = RGDF_D24S8;
	else
		l_Format = RGDF_D32F;

	float l_fWidth, l_fHeight;
	Int l_Width, l_Height;

	l_Result = i_pParamList[0]->getFloat(l_fWidth);
	DOME_ASSERT(DM_SUCC(l_Result));
	l_Result = i_pParamList[1]->getFloat(l_fHeight);
	DOME_ASSERT(DM_SUCC(l_Result));
	l_Width = (int)(l_fWidth + 0.1f);
	l_Height = (int)(l_fHeight + 0.1f);

	float l_fDepth, l_fStencil;
	l_Result = i_pParamList[3]->getFloat(l_fDepth);
	DOME_ASSERT(DM_SUCC(l_Result));
	l_Result = i_pParamList[4]->getFloat(l_fStencil);
	DOME_ASSERT(DM_SUCC(l_Result));

	OSTexture2D l_DepthTex;
    l_Result = l_pRenderer->createTexture2D(l_DepthTex, l_Width, l_Height, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(l_Result));

	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pEffectMgr->getPlugin(k_KEY_ScenePlugin);

	RCMOD_Texture l_SourceTexture;
	*((OSTexture2D*)l_SourceTexture.getPtr()) = l_DepthTex;

	l_pScenePlugin->RCP_ClearDepth((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_SourceTexture, l_fDepth, l_fStencil);


    MDOperandValue* l_pDepthOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pDepthOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_DepthTex);
    DOME_ASSERT(DM_SUCC(l_Result));

    return l_pDepthOperand;
}

DResult             MDOperatorDepthGen::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

    OSTexture2D l_hTexture;
    l_hTexture = *i_pResult->getTexturePtr();
    l_pRenderer->destroyTexture2D(l_hTexture);

	DOME_Del(i_pResult);

    return R_SUCCESS;
}


RC_NAMESPACE_END