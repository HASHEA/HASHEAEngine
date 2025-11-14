/*
    filename:       rcpastetonode.h
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/
#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCGrassSpeedGenNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCGrassSpeedGenNode(RCEffect* i_pEffect);
    virtual ~RCGrassSpeedGenNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

private:
	MDOperandValue		m_ConstValue;
    MDOperandValue		m_Frame0;
	MDOperandValue		m_Frame1;
	Bool				m_bTextureCreated;
};

RC_NAMESPACE_END

