/*
    filename:       mdeasygpuoperator.cpp
    author:         Ming Dong
    date:           2016-06-18
    description:    
*/

#include "../public/mdeasygpuoperator.h"
#include "../public/mdoperandvalue.h"
#include "../public/rcglobal.h"
#include "../public/mdeffect.h"
#include "../public/rceffectmanager.h"

RC_NAMESPACE_BEGIN

struct _OperatorInputInfo
{
    DHashString                             m_InputName;
    DString                                 m_InputTypeName;
    DSimpleTypeID                           m_InputTypeID;
    Bool                                    m_bCanBeMerged;
};
typedef TArray<_OperatorInputInfo>          _InputInfoArray;

struct MDEasyGpuOperator_Data
{
    DString                                 m_Name;
    Bool                                    m_bMustBeMerged;
    _InputInfoArray                         m_Inputs;
    RCGPUDATAFORMAT                         m_OutputTexFmt;
    Int                                     m_OutputTexFmtDecider;
    Int                                     m_SizeDecider;
    DVector2i                               m_SizeMultiplier;
    DVector2i                               m_SizeDivider;
    DVector2i                               m_SizeAdder;
    Int                                     m_Complexity;
    DString                                 m_ShaderCode;
};

MDEasyGpuOperator::MDEasyGpuOperator()
{
    me = DOME_New(MDEasyGpuOperator_Data);
    me->m_bMustBeMerged = DM_FALSE;
    me->m_OutputTexFmt = RGDF_UNKNOWN;
    me->m_OutputTexFmtDecider = -1;
    me->m_SizeDecider = -1;
    me->m_SizeMultiplier = DVector2i(1,1);
    me->m_SizeDivider = DVector2i(1,1);
    me->m_SizeAdder = DVector2i(0,0);

    me->m_Complexity = 1;
}

MDEasyGpuOperator::~MDEasyGpuOperator()
{
    DOME_Del(me);
}

const DString&      MDEasyGpuOperator::getOperatorName() const
{
    return me->m_Name;
}

Bool                MDEasyGpuOperator::isGpuOperator() const
{
    return DM_TRUE;
}

Int                 MDEasyGpuOperator::getInputCount() const
{
    return me->m_Inputs.size();
}

DSimpleTypeID       MDEasyGpuOperator::getInputTypeID(Int i_Index) const
{
    return me->m_Inputs[i_Index].m_InputTypeID;
}

Int                 MDEasyGpuOperator::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDEasyGpuOperator::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDEasyGpuOperator::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    if(me->m_OutputTexFmt != RGDF_UNKNOWN)
        return me->m_OutputTexFmt;
    else if (me->m_OutputTexFmtDecider >= 0 && me->m_OutputTexFmtDecider < i_ParamCount)
    {
        DOME_ASSERT(i_ParamCount == getInputCount());
        DOME_ASSERT(i_pParamList[me->m_OutputTexFmtDecider]);
        DOME_ASSERT(i_pParamList[me->m_OutputTexFmtDecider]->isTexture());

        RCGPUDATAFORMAT l_Format;
        DResult l_Result = i_pParamList[me->m_OutputTexFmtDecider]->getTextureFormat(l_Format);
        DOME_ASSERT(DM_SUCC(l_Result));

        return l_Format;
    }
    else
        return RGDF_RGBA8;
}

DResult             MDEasyGpuOperator::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    Int l_SizeDecider = me->m_SizeDecider;
    if(l_SizeDecider < 0)
        return R_FAILED;

    DOME_ASSERT(i_ParamCount == getInputCount());
    DOME_ASSERT(me->m_SizeDecider < i_ParamCount);
    DOME_ASSERT(i_pParamList[l_SizeDecider]);
    DOME_ASSERT(i_pParamList[l_SizeDecider]->isTexture());

    DVector2i l_OrigSize;
    i_pParamList[l_SizeDecider]->getTextureSize(l_OrigSize);
	 
    o_Size.x = max(1, l_OrigSize.x * me->m_SizeMultiplier.x / me->m_SizeDivider.x + me->m_SizeAdder.x);
    o_Size.y = max(1, l_OrigSize.y * me->m_SizeMultiplier.y / me->m_SizeDivider.y + me->m_SizeAdder.y);

    return R_SUCCESS;
}

Bool                MDEasyGpuOperator::canMergeInput(Int i_Index) const
{
    return me->m_Inputs[i_Index].m_bCanBeMerged;
}

Bool                MDEasyGpuOperator::mustBeMerged() const
{
    return me->m_bMustBeMerged;
}

Int                 MDEasyGpuOperator::getComplexity() const
{
    return me->m_Complexity;
}

MDOperand*          MDEasyGpuOperator::createRenderTarget(MDEffect* i_pMDEffect, const DVector2i& i_Size, RCGPUDATAFORMAT i_TexFmt) const
{
    DResult l_Result;
    MDOperandValue* l_pRT = DOME_New(MDOperandValue)(DSimpleTypeManager::Instance().getTypeByID(RCGlobal::k_SimpleTypeID_OSTexture2D));
    RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

    OSTexture2D l_hTexture;
    l_Result = l_pRenderer->createTexture2D(l_hTexture, i_Size.x, i_Size.y, 1, i_TexFmt, RBU_DEFAULT, DM_TRUE, DM_NULL);
    DOME_ASSERT(DM_SUCC(l_Result));

    l_Result = l_pRT->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_hTexture);
    DOME_ASSERT(DM_SUCC(l_Result));

    return l_pRT;
}

DResult             MDEasyGpuOperator::destroyRenderTarget(MDEffect* i_pMDEffect, MDOperand* i_pRT) const
{
    RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

    OSTexture2D l_hTexture;
    l_hTexture = *i_pRT->getTexturePtr();
    l_pRenderer->destroyTexture2D(l_hTexture);
    DOME_Del(i_pRT);
    return R_SUCCESS;
}

const DString&      MDEasyGpuOperator::getGlobalShader() const
{
    return me->m_ShaderCode;
}


void                MDEasyGpuOperator::reset()
{
    me->m_Inputs.clear();
    me->m_OutputTexFmt = RGDF_UNKNOWN;
    me->m_OutputTexFmtDecider = -1;
    me->m_SizeDecider = -1;
    me->m_SizeMultiplier = DVector2i(1,1);
    me->m_SizeDivider = DVector2i(1,1);
    me->m_SizeAdder = DVector2i(0,0);

    me->m_Complexity = 1;
}

void                MDEasyGpuOperator::setOperatorName(const DString& i_OperatorName)
{
    me->m_Name = i_OperatorName;
}

void                MDEasyGpuOperator::setMustBeMerged(Bool i_bMustBeMerged)
{
    me->m_bMustBeMerged = i_bMustBeMerged;
}

void                MDEasyGpuOperator::addInput(const DHashString& i_InputName, const DHashString& i_InputTypeName, Bool i_bCanbeMerged)
{
    _OperatorInputInfo l_InputInfo;
    l_InputInfo.m_InputName = i_InputName;
    l_InputInfo.m_InputTypeID = DSimpleTypeID(i_InputTypeName.getHash());
    l_InputInfo.m_InputTypeName = i_InputTypeName.c_str();
    l_InputInfo.m_bCanBeMerged = i_bCanbeMerged;

    me->m_Inputs.push_back(l_InputInfo);
}

void                MDEasyGpuOperator::setOutputTextureFmt(RCGPUDATAFORMAT i_Format, Int i_FormatDecider)
{
    me->m_OutputTexFmt = i_Format;
    me->m_OutputTexFmtDecider = i_FormatDecider;
}

void                MDEasyGpuOperator::setOperatorComplexity(Int i_Complexity)
{
    me->m_Complexity = i_Complexity;
}

void                MDEasyGpuOperator::setOperatorShaderCode(const DString& i_ShaderCode)
{
    me->m_ShaderCode = i_ShaderCode;
}

void                MDEasyGpuOperator::setResultSizeInfo(Int i_SizeDecider, const DVector2i& i_SizeMultiplier, const DVector2i& i_SizeDivider, const DVector2i& i_SizeAdder)
{
    me->m_SizeDecider = i_SizeDecider;
    me->m_SizeMultiplier = i_SizeMultiplier;
    me->m_SizeDivider = i_SizeDivider;
    me->m_SizeAdder = i_SizeAdder;
}

RC_NAMESPACE_END