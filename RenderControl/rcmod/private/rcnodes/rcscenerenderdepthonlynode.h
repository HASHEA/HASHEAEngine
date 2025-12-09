/*
    filename:       rcscenerendernode.h
    author:         Ming Dong
    date:           2016-Aug-24
    description:    
*/
#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCSceneRenderDepthOnlyNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCSceneRenderDepthOnlyNode(RCEffect* i_pEffect);
    virtual ~RCSceneRenderDepthOnlyNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

private:
    MDOperandValue          m_DepthOperand;
    MDOperandValue          m_ParamOperand;
    MDOperandValue          m_ClearColorOperand;
    MDOperandValue          m_ViewportOperand;
};

RC_NAMESPACE_END

