/*
    filename:       rcmipmapgennode.h
    author:         Ming Dong
    date:           2016-Aug-11
    description:    
*/
#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCMipmapGenNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCMipmapGenNode(RCEffect* i_pEffect);
    virtual ~RCMipmapGenNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

private:
};

RC_NAMESPACE_END

