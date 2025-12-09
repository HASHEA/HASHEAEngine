#include "pch.h"
/*
    filename:       mdoperatormipmapsel.cpp
    author:         Ming Dong
    date:           2016-Aug-11
    description:    
*/

#include "mdoperatormipmapsel.h"
#include <rc/public/iexecuter.h>
#include <rc/public/mdoperation.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

MDOperatorMipmapSel::MDOperatorMipmapSel()
    : m_OperatorName("MDMipmapSel")
{

}

MDOperatorMipmapSel::~MDOperatorMipmapSel()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorMipmapSel::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorMipmapSel::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorMipmapSel::getInputCount() const
{
    return 2;
}

DSimpleTypeID       MDOperatorMipmapSel::getInputTypeID(Int i_Index) const
{
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

Int                 MDOperatorMipmapSel::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorMipmapSel::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 2);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorMipmapSel::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 2);
    if (getOutputTypeID(i_Index, i_pMDEffect, i_ParamCount, i_pParamList) == RCGlobal::k_SimpleTypeID_OSTexture2D)
    {
        DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

        RCGPUDATAFORMAT l_Format;
        DResult l_Result = i_pParamList[0]->getTextureFormat(l_Format);
        DOME_ASSERT(DM_SUCC(l_Result));
        return l_Format;
    }
    else
        return RGDF_UNKNOWN;
}

DResult             MDOperatorMipmapSel::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 2);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
    DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isFloat());

    DResult l_Result;
    RCEffectManager* l_pEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pEffectMgr->getRenderer();
    F32 l_fMipmapLevel;
    Int l_MipmapLevel;

    l_Result = i_pParamList[1]->getFloat(l_fMipmapLevel);
    DOME_ASSERT(DM_SUCC(l_Result));
    l_MipmapLevel = (Int)l_fMipmapLevel;

    l_Result = i_pParamList[0]->getTextureSize(o_Size);
    DOME_ASSERT(DM_SUCC(l_Result));

    if (l_fMipmapLevel < 0.0f)
        o_Size.set(1, 1);
    else
    {
        while (l_MipmapLevel > 0)
        {
            l_MipmapLevel--;
            o_Size.x /= 2;
            if (o_Size.x < 1)
                o_Size.x = 1;

            o_Size.y /= 2;
            if (o_Size.y < 1)
                o_Size.y = 1;
        }
    }
    return R_SUCCESS;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorMipmapSel::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_MIPMAPSEL, FTT_RC_CAL_EXECUTE_EFFECTPASS);
    DOME_ASSERT(i_ParamCount == 2);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
    DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isFloat());

    
    DResult l_Result;
    RCEffectManager* l_pEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pEffectMgr->getRenderer();

    o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;

    OSTexture2D l_SrcTex = *i_pParamList[0]->getTexturePtr();
    F32 l_fMipmapLevel;
    Int l_MipmapLevel = 0;
    OSTexture2D l_ResultTex;

     l_Result = i_pParamList[1]->getFloat(l_fMipmapLevel);
    DOME_ASSERT(DM_SUCC(l_Result));
    l_MipmapLevel = Int(l_fMipmapLevel + 0.1f);

    if (l_fMipmapLevel < 0.0f)
    {
        l_pRenderer->getTexture2DMipmaps(l_SrcTex, l_MipmapLevel);
        l_MipmapLevel = Math::Abs(l_MipmapLevel) - 1;
        if(l_MipmapLevel < 0)
            l_MipmapLevel = 0;
    }

    Int l_Width = 1, l_Height = 1;
    RCGPUDATAFORMAT l_Format;

    l_Result = l_pRenderer->getTexture2DSize(l_SrcTex, l_MipmapLevel, l_Width, l_Height);
    DOME_ASSERT(DM_SUCC(l_Result));

    l_Format = l_pRenderer->getTexture2DFormat(l_SrcTex);

    l_Result = l_pRenderer->createTexture2D(l_ResultTex, l_Width, l_Height, 1, l_Format, RBU_DEFAULT, DM_TRUE, NULL);
    DOME_ASSERT(DM_SUCC(l_Result));

    l_Result = l_pRenderer->copyTexture2DMipmap(l_ResultTex, 0, l_SrcTex, l_MipmapLevel);
    DOME_ASSERT(DM_SUCC(l_Result));

    MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_ResultTex);
    DOME_ASSERT(DM_SUCC(l_Result));

	FRAMETIMER_END(FTT_RC_CAL_MIPMAPSEL);

    return l_pOperand;
}

DResult             MDOperatorMipmapSel::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    DOME_ASSERT(i_pResult->getTexturePtr());

    OSTexture2D l_ResultTex = *i_pResult->getTexturePtr();
    DOME_Del(i_pResult);

    RCEffectManager* l_pEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pEffectMgr->getRenderer();

    return l_pRenderer->destroyTexture2D(l_ResultTex);
}


RC_NAMESPACE_END