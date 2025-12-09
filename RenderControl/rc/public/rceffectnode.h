/*
    filename:       rceffectnode.h
    author:         Ming Dong
    date:           2016-MAR-24
    description:    
*/
#pragma once

#include "rcrenderer.h"

DOME_NAMESPACE_BEGIN

class RCEffect;
class MDEffect;
struct RCEffectNode_Data;
class MDOperand;
class RC_API RCEffectNode
{
public:
    RCEffectNode(RCEffect* i_pEffect);
    virtual ~RCEffectNode();

public:// utils functions which can be called by other class
    // load effect node from xml node
    DResult                     load(const rapidxml::xml_node<>* i_pXmlNode);
    // resolve effect node inputs
    DResult                     resolveInputs(RCEffectNode* const * i_pEffectNodeArray, Int i_NumNode);

    const DHashString&          getName() const;
    const DHashString&          getType() const;
    const DHashString&          getSubType() const;
    RCEffect*                   getRCEffect() const;

    Int                         getOutputCount() const;
    Int                         getOutputIndexByName(const DHashString& i_OutputName) const;
    const DHashString&          getOutputName(Int i_Index) const;
    DSimpleTypeID               getOutputType(Int i_Index) const;

    Int                         getInputCount() const;
    Int                         getInputIndexByName(const DHashString& i_InputName) const;
    const DHashString&          getInputName(Int i_Index) const;
    DSimpleTypeID               getInputType(Int i_Index) const;
    const DHashString&          getInputNodeName(Int i_Index) const;
    const DHashString&          getInputSelector(Int i_Index) const;
    const Bool                  isInputRequired(Int i_Index) const;
    RCEffectNode*               getInputNode(Int i_Index) const;
    RCEffectNode*               getInputNodeByName(const DHashString& i_InputName) const;
    Bool                        isInputConnected(Int i_Index) const;
    Bool                        isInputConnected(const DHashString& i_InputName) const;

    Int                         getParamCount() const;
    Int                         getParamIndexByName(const DHashString& i_ParamName) const;
    const DHashString&          getParamName(Int i_Index) const;
    DSimpleTypeID               getParamType(Int i_Index) const;
    Int                         getParamVersion(Int i_Index) const;
    const DSimpleTypedValue*    getParam(Int i_Index) const;
    DSimpleTypedValue*          getParam(Int i_Index);



public:// utils functions which can be called by RCEffect class
    DResult                     cacheResult(MDEffect* i_pMDEffect, Int i_OutputSelector) const;
    DResult                     cacheResult(MDEffect* i_pMDEffect, const DHashString& i_OutputSelector) const;
    MDOperand*                  getCachedResult(Int i_OutputSelector) const;
    MDOperand*                  getCachedResult(const DHashString& i_OutputSelector) const;
    DResult                     clearResultCaches() const;
    DResult                     executePushInput(MDEffect* o_pMDEffect, Int i_InputIndex);
    DResult                     execute(MDEffect* o_pMDEffect, Int i_OutputSelector);
    DResult                     execute(MDEffect* o_pMDEffect, const DHashString& i_OutputSelector);
    
public:// virtual functions need to be implemented in RCEffect class
    virtual DResult             buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector) = 0;
    
public:// virtual functions need to be implemented in child class
    // customize parameter laod callback function
    virtual void customizeLoad(const rapidxml::xml_node<>* i_pXmlNode) {};
    // callback function, called after everything is loaded
    virtual void finishLoad() {};
    // callback function, called at beginning and each time, when the effect need to be reset.
    virtual void onReset() {};

private:
    RCEffectNode_Data*   me;
};


DOME_NAMESPACE_END