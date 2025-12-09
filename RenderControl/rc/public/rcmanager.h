/*
    filename:       rcmanager.h
    author:         Ming Dong
    date:           2016-MAR-24
    description:    
*/
#pragma once

#include "rceffectmanager.h"

RC_NAMESPACE_BEGIN

class MDOperator;
class RC_API RCManager : public TSingleton<RCManager>
{
public:
    RCManager();
    ~RCManager();

    DResult                 reset();

    DResult                 createEffectManager(Int i_Index, RCRenderer* i_pRenderer);

    Int                     createEffectManager(RCRenderer* i_pRenderer);

    DResult                 destroyEffectManager(Int i_Index);

    RCEffectManager*        getEffectManager(Int i_Index);

    // functions for RCEffectNode
    typedef RCEffectNode* (*_CreateEffectNodeFuncPtr)(RCEffect* i_pEffect);
    typedef DResult (*_DestroyEffectNodeFuncPtr)(RCEffectNode* i_pEffectNode);
    DResult                 registerRCEffectNode(const DString& i_NodeType, _CreateEffectNodeFuncPtr i_CreateFunc, _DestroyEffectNodeFuncPtr i_DestroyFunc);
    RCEffectNode*           createRCEffectNode(DStringHash i_NodeTypeHash, RCEffect* i_pEffect) const;
    DResult                 destroyRCEffectNode(RCEffectNode* i_pEffectNode) const;

    // functions for MDOperator
    DResult                 registerMDOperator(const DString& i_OperatorName, const MDOperator* i_pOperator);
    const MDOperator*       getMDOperator(DStringHash i_Key) const;

    // functions for plugins
    DResult                 registerPlugin(const DString& i_PluginName, void* i_pPlugin);
    void*                   getPlugin(DStringHash i_Key);

    DResult                 getGpuShaderFromSignature(const DString& i_Signature, DString& o_Shader);

private:
    class RCManager_Impl;
    RCManager_Impl*         me;
};


RC_NAMESPACE_END