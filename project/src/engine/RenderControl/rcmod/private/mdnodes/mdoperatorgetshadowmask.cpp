#include "pch.h"
/*
    filename:       mdoperatorgetshadowmask.h
    author:         LJK
    date:           2023-4-18
    description:    
*/

#include <rc/public/iexecuter.h>
#include "mdoperatorgetshadowmask.h"

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN
MDOperatorGetShadowMask::MDOperatorGetShadowMask()
    : m_OperatorName("MDGetShadowMask")
{

}

MDOperatorGetShadowMask::~MDOperatorGetShadowMask()
{

}


const DString& MDOperatorGetShadowMask::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorGetShadowMask::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorGetShadowMask::getInputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorGetShadowMask::getInputTypeID(Int i_Index) const
{
    DOME_ASSERT(i_Index == 0);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

Int                 MDOperatorGetShadowMask::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorGetShadowMask::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorGetShadowMask::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
	return RGDF_RGBA8;
}

DResult             MDOperatorGetShadowMask::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DVector4f l_Size;
    i_pMDEffect->getRCEffect()->getEffectManager()->getParamSys().getDVector4f(DStringHash("EXTERN::PARAMETER::GBufferSize"), l_Size);

    o_Size.x = Int(l_Size.x + 0.1f);
    o_Size.y = Int(l_Size.y + 0.1f);

    return R_SUCCESS;
}

MDOperandPtr        MDOperatorGetShadowMask::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_GETSHADOWMASK, FTT_RC_CAL_EXECUTE_GETSHADOWMASK);
	DOME_ASSERT(i_ParamCount == 1);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;

    DResult l_Result;
    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

    DVector2i l_RtSize;
    l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
    DOME_ASSERT(DM_SUCC(l_Result));

	OSTexture2D copyedShadowMask;
	RCMOD_Texture rccopyedShadowMask;

    l_Result = l_pRenderer->createTexture2D(copyedShadowMask, l_RtSize.x, l_RtSize.y, 1, RGDF_RGBA8, RBU_DEFAULT, DM_TRUE, DM_NULL);
    DOME_ASSERT(DM_SUCC(l_Result));
	*((OSTexture2D*)rccopyedShadowMask.getPtr()) = copyedShadowMask;

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);
    
	l_pScenePlugin->GetShadowMask((RCOSRendererData*)l_pRenderer->getOSRendererData(), &rccopyedShadowMask);

    MDOperandValue* l_pGB0Operand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pGB0Operand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &copyedShadowMask);
    DOME_ASSERT(DM_SUCC(l_Result));

	MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
	l_pOperand->addOperand(l_pGB0Operand);

	FRAMETIMER_END(FTT_RC_CAL_EXECUTE_GETSHADOWMASK);
    return l_pOperand;
}

DResult MDOperatorGetShadowMask::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
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