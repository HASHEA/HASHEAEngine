#include "pch.h"
#include "MDOperatorGrassSpeedGen.h"
#include <rc/public/iexecuter.h>

RC_NAMESPACE_BEGIN

MDOperatorGrassSpeedGen::MDOperatorGrassSpeedGen()
    : m_OperatorName("MDGrassSpeedGen")
{

}

MDOperatorGrassSpeedGen::~MDOperatorGrassSpeedGen()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorGrassSpeedGen::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorGrassSpeedGen::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorGrassSpeedGen::getInputCount() const
{
    return m_nParamCount;
}

DSimpleTypeID       MDOperatorGrassSpeedGen::getInputTypeID(Int i_Index) const
{
    switch (i_Index)
    {
    case 0:
    case 1:
    case 2:
		return RCGlobal::k_SimpleTypeID_OSTexture2D;
    case 3:
    case 4:
	case 5:
	case 6:
		return RCGlobal::k_SimpleTypeID_F32;
	case 7:
		return RCGlobal::k_SimpleTypeID_DVector2f;
    }
    return RCGlobal::k_SimpleTypeID_Unknown;
}

Int                 MDOperatorGrassSpeedGen::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorGrassSpeedGen::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorGrassSpeedGen::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == m_nParamCount);
    DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());

    RCGPUDATAFORMAT l_Format;
    DResult l_Result = i_pParamList[2]->getTextureFormat(l_Format);
    DOME_ASSERT(DM_SUCC(l_Result));
    return l_Format;
}

DResult             MDOperatorGrassSpeedGen::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == m_nParamCount);
	DOME_ASSERT(i_pParamList[0]->isTexture());      // source texture
	DOME_ASSERT(i_pParamList[1]->isTexture());      // dest	texture
	DOME_ASSERT(i_pParamList[2]->isTexture());      // render target texture
	DOME_ASSERT(i_pParamList[3]->isFloat());       // delta time
    DOME_ASSERT(i_pParamList[4]->isFloat());       // k
	DOME_ASSERT(i_pParamList[5]->isFloat());       // mass
	DOME_ASSERT(i_pParamList[6]->isFloat());       // damping
	DOME_ASSERT(i_pParamList[7]->isFloat2());      // offset
   
    return i_pParamList[2]->getTextureSize(o_Size);
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorGrassSpeedGen::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    DResult l_Result;
	DOME_ASSERT(i_ParamCount == m_nParamCount);
	DOME_ASSERT(i_pParamList[0]->isTexture());      // source texture
	DOME_ASSERT(i_pParamList[1]->isTexture());      // dest	texture
	DOME_ASSERT(i_pParamList[2]->isTexture());      // render target texture
    DOME_ASSERT(i_pParamList[3]->isFloat());       // dela time
    DOME_ASSERT(i_pParamList[4]->isFloat());       // k
	DOME_ASSERT(i_pParamList[5]->isFloat());       // mass
	DOME_ASSERT(i_pParamList[6]->isFloat());       // damping
	DOME_ASSERT(i_pParamList[7]->isFloat2());      // offset
    
    RCEffectManager* l_pEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pEffectMgr->getRenderer();

    DStringHash l_CurrentFrameHash("EXTERN::PARAMETER::CurrentFrame");
    DSimpleTypedValue* l_pCurrentFrame = l_pEffectMgr->getParamSys().getParameter(l_CurrentFrameHash);
    DOME_ASSERT(l_pCurrentFrame);

    Int l_iCurrentFrame = l_pCurrentFrame->getInt();

    OSTexture2D l_DestTex = *i_pParamList[0]->getTexturePtr();
	OSTexture2D l_SrcTex;
	OSTexture2D l_RTTex;
    F32   l_fDeltaTime      = *i_pParamList[3]->getFloatPtr();
    F32   l_fK              = *i_pParamList[4]->getFloatPtr();
	F32   l_fMass           = *i_pParamList[5]->getFloatPtr();
	F32   l_fDamping        = *i_pParamList[6]->getFloatPtr();
	DVector2f   l_vOffset   = *i_pParamList[7]->getFloat2Ptr();

    if (l_iCurrentFrame % 2 == 0)
    {
	    l_SrcTex = *i_pParamList[1]->getTexturePtr();
	    l_RTTex = *i_pParamList[2]->getTexturePtr();
    }
    else
    {
	    l_SrcTex = *i_pParamList[2]->getTexturePtr();
	    l_RTTex = *i_pParamList[1]->getTexturePtr();
    }

    DVector2i l_TrgSize;
    l_Result = l_pRenderer->getTexture2DSize(l_RTTex, l_TrgSize.x, l_TrgSize.y);
    DOME_ASSERT(DM_SUCC(l_Result));

    o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[2] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[5] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[6] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[7] = IRP_AFTEREXECUTE;

    IExecuter* l_pExecuter = (IExecuter*)l_pEffectMgr->getMDExecuter(DHashString("MDGrassSpeedGenGpu($TEX[0],$TEX[1],$F1[0],$F1[1],$F1[2],$F1[3],$F2[0])"));
    DOME_ASSERT(l_pExecuter);

    l_pExecuter->setRenderTarget(l_RTTex);
	l_pExecuter->setRenderTargetViewport(Int(0), 
		Int(0), 
		Int(l_TrgSize.x), 
		Int(l_TrgSize.y));
	l_pExecuter->setUVCoef(DVector4f(1.0f, 1.0f, 0.0f, 0.0f));
    l_pExecuter->setTextureParam(0, l_SrcTex);
	l_pExecuter->setTextureParam(1, l_DestTex);
    l_pExecuter->setFloat1Param(0, l_fDeltaTime);
    l_pExecuter->setFloat1Param(1, l_fK);
	l_pExecuter->setFloat1Param(2, l_fMass);
	l_pExecuter->setFloat1Param(3, l_fDamping);
	l_pExecuter->setFloat2Param(0, l_vOffset);
    l_Result = l_pExecuter->execute();
    DOME_ASSERT(DM_SUCC(l_Result));

    return i_pParamList[2];
}

DResult             MDOperatorGrassSpeedGen::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    return R_SUCCESS;
}


RC_NAMESPACE_END