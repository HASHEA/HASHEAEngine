/*
    filename:       rcscenerendernode.h
    author:         Ming Dong
    date:           2016-Aug-24
    description:    
*/
#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCSceneRenderNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCSceneRenderNode(RCEffect* i_pEffect);
    virtual ~RCSceneRenderNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

private:
    MDOperandValue          m_DepthOperand;	
    MDOperandValue          m_Param0Operand;
	MDOperandValue			m_Param1Operand;
	MDOperandValue          m_ClearColorOperand;
    MDOperandValue          m_ViewportOperand;
    MDOperandValue          m_ColorSelector;
    MDOperandValue          m_DepthSelector;
};

RC_NAMESPACE_END

