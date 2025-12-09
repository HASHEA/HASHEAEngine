#include "pch.h"
/*
    filename:       mdoperatorblendrectto.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "mdoperatorblendrectto.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

MDOperatorBlendRectTo::MDOperatorBlendRectTo()
    : m_OperatorName("MDBlendRectTo")
{

}

MDOperatorBlendRectTo::~MDOperatorBlendRectTo()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorBlendRectTo::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorBlendRectTo::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorBlendRectTo::getInputCount() const
{
    return 6;
}

DSimpleTypeID       MDOperatorBlendRectTo::getInputTypeID(Int i_Index) const
{
    switch (i_Index)
    {
    case 0:
    case 1:
        return RCGlobal::k_SimpleTypeID_OSTexture2D;
    case 2:
    case 3:
    case 4:
    case 5:
        return RCGlobal::k_SimpleTypeID_DVector2f;
    }
    return RCGlobal::k_SimpleTypeID_Unknown;
}

Int                 MDOperatorBlendRectTo::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorBlendRectTo::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorBlendRectTo::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 6);
    DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());

    RCGPUDATAFORMAT l_Format;
    DResult l_Result = i_pParamList[1]->getTextureFormat(l_Format);
    DOME_ASSERT(DM_SUCC(l_Result));
    return l_Format;
}

DResult             MDOperatorBlendRectTo::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 6);
    DOME_ASSERT(i_pParamList[0]->isTexture());      // source texture
    DOME_ASSERT(i_pParamList[1]->isTexture());      // render target texture
    DOME_ASSERT(i_pParamList[2]->isFloat2());       // source uv pos
    DOME_ASSERT(i_pParamList[3]->isFloat2());       // source uv size
    DOME_ASSERT(i_pParamList[4]->isFloat2());       // target uv pos
    DOME_ASSERT(i_pParamList[5]->isFloat2());       // target uv size

    return i_pParamList[1]->getTextureSize(o_Size);
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorBlendRectTo::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    PERF_COUNTER_EX(0);
	FRAMETIMER_BEGIN(FTT_RC_CAL_BLENDRECT, FTT_RC_CAL_EXECUTE_EFFECTPASS);
    DResult l_Result;
    DOME_ASSERT(i_ParamCount == 6);
    DOME_ASSERT(i_pParamList[0]->isTexture());      // source texture
    DOME_ASSERT(i_pParamList[1]->isTexture());      // render target texture
    DOME_ASSERT(i_pParamList[2]->isFloat2());       // source uv pos
    DOME_ASSERT(i_pParamList[3]->isFloat2());       // source uv size
    DOME_ASSERT(i_pParamList[4]->isFloat2());       // target uv pos
    DOME_ASSERT(i_pParamList[5]->isFloat2());       // target uv size
    
    RCEffectManager* l_pEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pEffectMgr->getRenderer();

    OSTexture2D l_SrcTex = *i_pParamList[0]->getTexturePtr();
    OSTexture2D l_RtTex = *i_pParamList[1]->getTexturePtr();
    DVector2f   l_SrcUVPos = *i_pParamList[2]->getFloat2Ptr();
    DVector2f   l_SrcUVSize = *i_pParamList[3]->getFloat2Ptr();
    DVector2f   l_TrgUVPos = *i_pParamList[4]->getFloat2Ptr();
    DVector2f   l_TrgUVSize = *i_pParamList[5]->getFloat2Ptr();

    DVector2i l_TrgSize;
    l_Result = l_pRenderer->getTexture2DSize(l_RtTex, l_TrgSize.x, l_TrgSize.y);
    DOME_ASSERT(DM_SUCC(l_Result));

    o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[5] = IRP_AFTEREXECUTE;

    IExecuter* l_pExecuter = (IExecuter*)l_pEffectMgr->getMDExecuter(DHashString("MDCopyRect($TEX[0],$F2[0],$F2[1])"));
    DOME_ASSERT(l_pExecuter);

    l_pExecuter->setRenderTarget(l_RtTex);
    l_pExecuter->setRenderTargetViewport(Int(l_TrgUVPos.x * l_TrgSize.x), 
                                         Int(l_TrgUVPos.y * l_TrgSize.y), 
                                         Int(l_TrgUVSize.x * l_TrgSize.x), 
                                         Int(l_TrgUVSize.y * l_TrgSize.y));
    l_pExecuter->setBlendMode(RBM_ALPHABLEND);
    l_pExecuter->setUVCoef(DVector4f(l_SrcUVSize.x, l_SrcUVSize.y, l_SrcUVPos.x, l_SrcUVPos.y));
    l_pExecuter->setTextureParam(0, l_SrcTex);
    l_pExecuter->setFloat2Param(0, l_SrcUVPos);
    l_pExecuter->setFloat2Param(1, l_SrcUVSize);
    l_Result = l_pExecuter->execute();
    DOME_ASSERT(DM_SUCC(l_Result));
	FRAMETIMER_END(FTT_RC_CAL_BLENDRECT);
    return i_pParamList[1];
}

DResult             MDOperatorBlendRectTo::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    return R_SUCCESS;
}


RC_NAMESPACE_END