/*
    filename:       rcoutputnode.h
    author:         Ming Dong
    date:           2016-MAY-24
    description:    
*/
#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCOutputNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCOutputNode(RCEffect* i_pEffect);
    virtual ~RCOutputNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

private:
    MDOperandValue          m_RTTexture;
    MDOperandValue          m_SrcUVPos;
    MDOperandValue          m_SrcUVSize;
    MDOperandValue          m_DstUVPos;
    MDOperandValue          m_DstUVSize;
	MDOperandValue          m_WriteFrequency;
	MDOperandValue          m_FilePath;
    MDOperandValue          m_BlendType;
};

RC_NAMESPACE_END