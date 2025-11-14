#include "pch.h"
/*
    filename:       MDOperatorConvert2Texture.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "MDOperatorConvert2Texture.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

MDOperatorConvert2Texture::MDOperatorConvert2Texture()
    : m_OperatorName("MDConvert2Texture")
{

}

MDOperatorConvert2Texture::~MDOperatorConvert2Texture()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorConvert2Texture::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorConvert2Texture::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorConvert2Texture::getInputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorConvert2Texture::getInputTypeID(Int i_Index) const
{
    return RCGlobal::k_SimpleTypeID_Unknown;
}

Int                 MDOperatorConvert2Texture::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorConvert2Texture::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 1);
    DOME_ASSERT(i_pParamList[0]->getDataCount() == 1);
    return RCGlobal::k_SimpleTypeID_OSTexture2D; /*i_pParamList[0]->getDataType(0)*/
}

RCGPUDATAFORMAT     MDOperatorConvert2Texture::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 1);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

	RCGPUDATAFORMAT l_Format;
	DResult l_Result = i_pParamList[0]->getTextureFormat(l_Format);
	DOME_ASSERT(DM_SUCC(l_Result));
	return l_Format;
}

DResult             MDOperatorConvert2Texture::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_Index == 0);
	DOME_ASSERT(i_ParamCount == 1);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

    return i_pParamList[0]->getTextureSize(o_Size);
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorConvert2Texture::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    PERF_COUNTER_EX(0);
	FRAMETIMER_BEGIN(FTT_RC_CAL_C2TEX, FTT_RC_CAL_EXECUTE_EFFECTPASS);
    DOME_ASSERT(i_ParamCount == 1);
    DOME_ASSERT(i_pParamList[0]);      // source value
      
    o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;

    DSimpleTypedValue* l_pSrc = i_pParamList[0]->getDataPtr();   
    DOME_ASSERT(l_pSrc);
	DOME_ASSERT(l_pSrc->getTypeID() == RCGlobal::k_SimpleTypeID_OSTexture2D);
	FRAMETIMER_END(FTT_RC_CAL_C2TEX);
    return i_pParamList[0];
}

DResult             MDOperatorConvert2Texture::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	return R_SUCCESS;
}


RC_NAMESPACE_END