/*
    filename:       rceffectnode.cpp
    author:         Ming Dong
    date:           2016-MAY-16
    description:    
*/
#pragma once

#include "../public/rceffectnode.h"
#include "../public/mdeffect.h"
#include "../public/rcglobal.h"
#include "../public/rceffect.h"
#include "../../../../../../../../../DevEnv/Include/PerfAnalyzer.h"


RC_NAMESPACE_BEGIN

struct _RCEffectNode_InputInfo
{
    Int                 m_Index;
    DHashString         m_Name;
    DHashString         m_XmlType;
    DSimpleTypeID       m_TypeID;
    DHashString         m_InputNodeName;
    DHashString         m_SelectorName;
    Bool                m_bRequired;
    RCEffectNode*       m_pEffectNode;
};

struct _RCEffectNode_ParamInfo
{
    Int                 m_Index;
    DHashString         m_Name;
    DHashString         m_XmlType;
    DSimpleTypeID       m_TypeID;
    DSimpleTypedValue   m_Value;
    DString             m_MetaData;
    Bool                m_bRefKey;
};

struct _RCEffectNode_OutputInfo
{
    Int                 m_Index;
    DHashString         m_Name;
    DHashString         m_XmlType;
    DSimpleTypeID       m_TypeID;

    MDOperand*          m_pResultCache;
};

typedef TDataBase<DString, _RCEffectNode_InputInfo, 16, 1>      _RCEffectNode_InputDB;
typedef TDataBase<DString, _RCEffectNode_ParamInfo, 16, 1>      _RCEffectNode_ParamDB;
typedef TDataBase<DString, _RCEffectNode_OutputInfo, 4, 1>      _RCEffectNode_OutputDB;

struct RCEffectNode_Data
{
    RCEffectNode_Data(RCEffect* i_pEffect)
    {
        m_pEffect = i_pEffect;
    }

    RCEffect*                       m_pEffect;
    DHashString                     m_NodeName;
    DHashString                     m_NodeType;
    DHashString                     m_NodeSubType;
    _RCEffectNode_InputDB           m_InputDB;
    _RCEffectNode_ParamDB           m_ParamDB;
    _RCEffectNode_OutputDB          m_OutputDB;
};


RCEffectNode::RCEffectNode(RCEffect* i_pEffect)
{
    me = DOME_New(RCEffectNode_Data)(i_pEffect);
}

RCEffectNode::~RCEffectNode()
{
    DOME_Del(me);
}

DResult                     RCEffectNode::load(const rapidxml::xml_node<>* i_pXmlNode)
{
    DResult l_Result;
    Int l_IndexVerify = 0;
    rapidxml::xml_attribute<>*      l_pXmlNodeAttrib = DM_NULL;
    rapidxml::xml_node<>*           l_pXmlInputNode = DM_NULL;
    rapidxml::xml_node<>*           l_pXmlParamNode = DM_NULL;
    rapidxml::xml_node<>*           l_pXmlOutputNode = DM_NULL;

    // read node's attributes
    l_pXmlNodeAttrib = i_pXmlNode->first_attribute("name");
    DOME_ASSERT(l_pXmlNodeAttrib);
    me->m_NodeName = l_pXmlNodeAttrib->value();

    l_pXmlNodeAttrib = i_pXmlNode->first_attribute("type");
    DOME_ASSERT(l_pXmlNodeAttrib);
    me->m_NodeType = l_pXmlNodeAttrib->value();

    l_pXmlNodeAttrib = i_pXmlNode->first_attribute("subtype");
    if(l_pXmlNodeAttrib)
        me->m_NodeSubType = l_pXmlNodeAttrib->value();

    // process all the input child nodes
    l_IndexVerify = 0;
    for (l_pXmlInputNode = i_pXmlNode->first_node("Input"); l_pXmlInputNode != DM_NULL; l_pXmlInputNode = l_pXmlInputNode->next_sibling("Input"))
    {
        Int             l_Index;
        DHashString     l_InputName;
        DHashString     l_InputType;
        DHashString     l_InputRequired;
        DHashString     l_InputNodeName;
        DHashString     l_InputSelector;

        l_pXmlNodeAttrib = l_pXmlInputNode->first_attribute("index");
        DOME_ASSERT(l_pXmlNodeAttrib);
        l_Index = DString(l_pXmlNodeAttrib->value()).toInt();
        DOME_ASSERT(l_IndexVerify == l_Index);
        l_IndexVerify ++;

        l_pXmlNodeAttrib = l_pXmlInputNode->first_attribute("name");
        DOME_ASSERT(l_pXmlNodeAttrib);
        l_InputName = l_pXmlNodeAttrib->value();

        l_pXmlNodeAttrib = l_pXmlInputNode->first_attribute("type");
        DOME_ASSERT(l_pXmlNodeAttrib);
        l_InputType = l_pXmlNodeAttrib->value();

        l_pXmlNodeAttrib = l_pXmlInputNode->first_attribute("required");
        DOME_ASSERT(l_pXmlNodeAttrib);
        l_InputRequired = l_pXmlNodeAttrib->value();

        l_pXmlNodeAttrib = l_pXmlInputNode->first_attribute("operatorname");
        DOME_ASSERT(l_pXmlNodeAttrib);
        l_InputNodeName = l_pXmlNodeAttrib->value();

        l_pXmlNodeAttrib = l_pXmlInputNode->first_attribute("selector");
        DOME_ASSERT(l_pXmlNodeAttrib);
        l_InputSelector = l_pXmlNodeAttrib->value();

        l_Result = me->m_InputDB.add(DString(l_InputName.c_str()));
        DOME_ASSERT(DM_SUCC(l_Result));

        _RCEffectNode_InputInfo* l_pInputInfo = me->m_InputDB.get(l_InputName.getHash());
        DOME_ASSERT(l_pInputInfo);

        l_pInputInfo->m_Index = l_Index;
        l_pInputInfo->m_Name = l_InputName;
        l_pInputInfo->m_XmlType = l_InputType;
        l_pInputInfo->m_TypeID = DSimpleTypeID(DStringHash(l_InputType.c_str()));
        l_pInputInfo->m_bRequired = l_InputRequired == "true" ? DM_TRUE : DM_FALSE;
        l_pInputInfo->m_InputNodeName = l_InputNodeName;
        l_pInputInfo->m_SelectorName = l_InputSelector;
        l_pInputInfo->m_pEffectNode = DM_NULL;
    }

    // process all the parameter child nodes
    l_IndexVerify = 0;
    for (l_pXmlParamNode = i_pXmlNode->first_node("Parameter"); l_pXmlParamNode != DM_NULL; l_pXmlParamNode = l_pXmlParamNode->next_sibling("Parameter"))
    {
        Int                 l_Index;
        Bool                l_bRefKey = DM_FALSE;
        DHashString         l_ParamName;
        DHashString         l_ParamType;
        DHashString         l_ParamSubType;
        DString             l_ParamStrValue;
        DString             l_ParamMetaData;

        l_pXmlNodeAttrib = l_pXmlParamNode->first_attribute("index");
        DOME_ASSERT(l_pXmlNodeAttrib);
        l_Index = DString(l_pXmlNodeAttrib->value()).toInt();
        DOME_ASSERT(l_IndexVerify == l_Index);
        l_IndexVerify ++;

        l_pXmlNodeAttrib = l_pXmlParamNode->first_attribute("name");
        DOME_ASSERT(l_pXmlNodeAttrib);
        l_ParamName = l_pXmlNodeAttrib->value();

        l_pXmlNodeAttrib = l_pXmlParamNode->first_attribute("type");
        DOME_ASSERT(l_pXmlNodeAttrib);
        l_ParamType = l_pXmlNodeAttrib->value();

        l_pXmlNodeAttrib = l_pXmlParamNode->first_attribute("value");
        DOME_ASSERT(l_pXmlNodeAttrib);
        l_ParamStrValue = l_pXmlNodeAttrib->value();

        l_pXmlNodeAttrib = l_pXmlParamNode->first_attribute("refkey");
        if (l_pXmlNodeAttrib)
        {
            l_bRefKey = (DString(l_pXmlNodeAttrib->value()) == "true") ? DM_TRUE : DM_FALSE;
        }

        l_pXmlNodeAttrib = l_pXmlParamNode->first_attribute("metadata");
        if(l_pXmlNodeAttrib)
            l_ParamMetaData = l_pXmlNodeAttrib->value();

        l_Result = me->m_ParamDB.add(DString(l_ParamName.c_str()));
        DOME_ASSERT(DM_SUCC(l_Result));

        _RCEffectNode_ParamInfo* l_pParamInfo = me->m_ParamDB.get(l_ParamName.getHash());
        DOME_ASSERT(l_pParamInfo);

        l_pParamInfo->m_Index = l_Index;
        l_pParamInfo->m_Name = l_ParamName;
        l_pParamInfo->m_XmlType = l_ParamType;
        l_pParamInfo->m_TypeID = DSimpleTypeID(DStringHash(l_ParamType.c_str()));
        l_pParamInfo->m_MetaData = l_ParamMetaData;
        l_pParamInfo->m_bRefKey = l_bRefKey;

        if (l_pParamInfo->m_bRefKey)
        {
            Int l_PropIndex = me->m_pEffect->addEffectProperty(l_ParamStrValue.c_str(), l_pParamInfo->m_TypeID, l_ParamMetaData);
            l_pParamInfo->m_Value.set(RCGlobal::k_SimpleTypeID_Int, &l_PropIndex);
        }
        else
        {
            if (l_pParamInfo->m_TypeID == RCGlobal::k_SimpleTypeID_DString)
            {
                l_pParamInfo->m_Value.set(RCGlobal::k_SimpleTypeID_DString, &l_ParamStrValue);
            }
            else if (l_pParamInfo->m_TypeID == RCGlobal::k_SimpleTypeID_F32)
            {
                F32 l_Value;
                Math::MathFromDString(l_ParamStrValue, l_Value);
                l_pParamInfo->m_Value.set(RCGlobal::k_SimpleTypeID_F32, &l_Value);
            }
            else if (l_pParamInfo->m_TypeID == RCGlobal::k_SimpleTypeID_DVector2f)
            {
                DVector2f l_Value;
                Math::MathFromDString(l_ParamStrValue, l_Value);
                l_pParamInfo->m_Value.set(RCGlobal::k_SimpleTypeID_DVector2f, &l_Value);
            }
            else if (l_pParamInfo->m_TypeID == RCGlobal::k_SimpleTypeID_DVector3f)
            {
                DVector3f l_Value;
                Math::MathFromDString(l_ParamStrValue, l_Value);
                l_pParamInfo->m_Value.set(RCGlobal::k_SimpleTypeID_DVector3f, &l_Value);
            }
            else if (l_pParamInfo->m_TypeID == RCGlobal::k_SimpleTypeID_DVector4f)
            {
                DVector4f l_Value;
                Math::MathFromDString(l_ParamStrValue, l_Value);
                l_pParamInfo->m_Value.set(RCGlobal::k_SimpleTypeID_DVector4f, &l_Value);
            }
            else if (l_pParamInfo->m_TypeID == RCGlobal::k_SimpleTypeID_DVectorLut1f)
            {
                DVectorLut1f l_Value;
                Math::MathFromDString(l_ParamStrValue, l_Value);
                l_pParamInfo->m_Value.set(RCGlobal::k_SimpleTypeID_DVectorLut1f, &l_Value);
            }
            else if (l_pParamInfo->m_TypeID == RCGlobal::k_SimpleTypeID_DVectorLut2f)
            {
                DVectorLut2f l_Value;
                Math::MathFromDString(l_ParamStrValue, l_Value);
                l_pParamInfo->m_Value.set(RCGlobal::k_SimpleTypeID_DVectorLut2f, &l_Value);
            }
            else if (l_pParamInfo->m_TypeID == RCGlobal::k_SimpleTypeID_DVectorLut3f)
            {
                DVectorLut3f l_Value;
                Math::MathFromDString(l_ParamStrValue, l_Value);
                l_pParamInfo->m_Value.set(RCGlobal::k_SimpleTypeID_DVectorLut3f, &l_Value);
            }
            else if (l_pParamInfo->m_TypeID == RCGlobal::k_SimpleTypeID_DVectorLut4f)
            {
                DVectorLut4f l_Value;
                Math::MathFromDString(l_ParamStrValue, l_Value);
                l_pParamInfo->m_Value.set(RCGlobal::k_SimpleTypeID_DVectorLut4f, &l_Value);
            }
            else
            {
                DOME_ASSERT(0);
            }
        }
    }

    // process all the output child nodes
    l_IndexVerify = 0;
    for (l_pXmlOutputNode = i_pXmlNode->first_node("Output"); l_pXmlOutputNode != DM_NULL; l_pXmlOutputNode = l_pXmlOutputNode->next_sibling("Output"))
    {
        Int         l_Index;
        DHashString l_OutputName;
        DHashString l_OutputType;

        l_pXmlNodeAttrib = l_pXmlOutputNode->first_attribute("index");
        DOME_ASSERT(l_pXmlNodeAttrib);
        l_Index = DString(l_pXmlNodeAttrib->value()).toInt();
        DOME_ASSERT(l_IndexVerify == l_Index);
        l_IndexVerify ++;

        l_pXmlNodeAttrib = l_pXmlOutputNode->first_attribute("name");
        DOME_ASSERT(l_pXmlNodeAttrib);
        l_OutputName = l_pXmlNodeAttrib->value();

        l_pXmlNodeAttrib = l_pXmlOutputNode->first_attribute("type");
        DOME_ASSERT(l_pXmlNodeAttrib);
        l_OutputType = l_pXmlNodeAttrib->value();
        

        l_Result = me->m_OutputDB.add(DString(l_OutputName.c_str()));
        DOME_ASSERT(DM_SUCC(l_Result));

        _RCEffectNode_OutputInfo* l_pOutputInfo = me->m_OutputDB.get(l_OutputName.getHash());
        DOME_ASSERT(l_pOutputInfo);

        l_pOutputInfo->m_Index = l_Index;
        l_pOutputInfo->m_Name = l_OutputName;
        l_pOutputInfo->m_XmlType = l_OutputType;
        l_pOutputInfo->m_TypeID = DSimpleTypeManager::Instance().getTypeID_Unknown();
        l_pOutputInfo->m_pResultCache = DM_NULL;
    }

    customizeLoad(i_pXmlNode);

    return R_SUCCESS;
}

DResult                     RCEffectNode::resolveInputs( RCEffectNode* const * i_pEffectNodeArray, Int i_NumNode)
{
    for (Int i = 0; i < me->m_InputDB.getCount(); ++i)
    {
        _RCEffectNode_InputInfo* l_pInputInfo = me->m_InputDB.get(i);
        const DHashString& l_pInputNodeName = l_pInputInfo->m_InputNodeName;

        l_pInputInfo->m_pEffectNode = DM_NULL;

        for (Int n = 0; n < i_NumNode; ++n)
        {
            if (l_pInputNodeName == i_pEffectNodeArray[n]->getName())
            {
                l_pInputInfo->m_pEffectNode = i_pEffectNodeArray[n];
                break;
            }
        }
    }

    return R_SUCCESS;
}

const DHashString&          RCEffectNode::getName() const
{
    return me->m_NodeName;
}

const DHashString&          RCEffectNode::getType() const
{
    return me->m_NodeType;
}

const DHashString&          RCEffectNode::getSubType() const
{
    return me->m_NodeSubType;
}

RCEffect*                   RCEffectNode::getRCEffect() const
{
    return me->m_pEffect;
}

Int                         RCEffectNode::getOutputCount() const
{
    return me->m_OutputDB.getCount();
}

Int                         RCEffectNode::getOutputIndexByName(const DHashString& i_OutputName) const
{
    for (Int i = 0; i < me->m_OutputDB.getCount(); ++i)
    {
        _RCEffectNode_OutputInfo* l_pOutputInfo = me->m_OutputDB.get(i);
        if(i_OutputName == l_pOutputInfo->m_Name)
            return i;
    }
    return -1;
}

const DHashString&          RCEffectNode::getOutputName(Int i_Index) const
{
    DOME_ASSERT(i_Index >= 0 && i_Index < me->m_OutputDB.getCount());

    _RCEffectNode_OutputInfo* l_pOutputInfo = me->m_OutputDB.get(i_Index);
    return l_pOutputInfo->m_Name;
}

DSimpleTypeID               RCEffectNode::getOutputType(Int i_Index) const
{
    DOME_ASSERT(i_Index >= 0 && i_Index < me->m_OutputDB.getCount());

    _RCEffectNode_OutputInfo* l_pOutputInfo = me->m_OutputDB.get(i_Index);
    return l_pOutputInfo->m_TypeID;
}

Int                         RCEffectNode::getInputCount() const
{
    return me->m_InputDB.getCount();
}

Int                         RCEffectNode::getInputIndexByName(const DHashString& i_InputName) const
{
    for (Int i = 0; i < me->m_InputDB.getCount(); ++i)
    {
        _RCEffectNode_InputInfo* l_pInputInfo = me->m_InputDB.get(i);
        if(i_InputName == l_pInputInfo->m_Name)
            return i;
    }
    return -1;
}

const DHashString&          RCEffectNode::getInputName(Int i_Index) const
{
    DOME_ASSERT(i_Index >= 0 && i_Index < me->m_InputDB.getCount());

    _RCEffectNode_InputInfo* l_pInputInfo = me->m_InputDB.get(i_Index);
    return l_pInputInfo->m_Name;
}

DSimpleTypeID               RCEffectNode::getInputType(Int i_Index) const
{
    DOME_ASSERT(i_Index >= 0 && i_Index < me->m_InputDB.getCount());

    _RCEffectNode_InputInfo* l_pInputInfo = me->m_InputDB.get(i_Index);
    return l_pInputInfo->m_TypeID;
}

const DHashString&          RCEffectNode::getInputNodeName(Int i_Index) const
{
    DOME_ASSERT(i_Index >= 0 && i_Index < me->m_InputDB.getCount());

    _RCEffectNode_InputInfo* l_pInputInfo = me->m_InputDB.get(i_Index);
    return l_pInputInfo->m_InputNodeName;
}

const DHashString&          RCEffectNode::getInputSelector(Int i_Index) const
{
    DOME_ASSERT(i_Index >= 0 && i_Index < me->m_InputDB.getCount());

    _RCEffectNode_InputInfo* l_pInputInfo = me->m_InputDB.get(i_Index);
    return l_pInputInfo->m_SelectorName;
}

const Bool                  RCEffectNode::isInputRequired(Int i_Index) const
{
    DOME_ASSERT(i_Index >= 0 && i_Index < me->m_InputDB.getCount());

    _RCEffectNode_InputInfo* l_pInputInfo = me->m_InputDB.get(i_Index);
    return l_pInputInfo->m_bRequired;
}

RCEffectNode*               RCEffectNode::getInputNode(Int i_Index) const
{
    DOME_ASSERT(i_Index >= 0 && i_Index < me->m_InputDB.getCount());

    _RCEffectNode_InputInfo* l_pInputInfo = me->m_InputDB.get(i_Index);
    return l_pInputInfo->m_pEffectNode;
}

RCEffectNode*               RCEffectNode::getInputNodeByName(const DHashString& i_InputName) const
{
    _RCEffectNode_InputInfo* l_pInputInfo = me->m_InputDB.get(i_InputName.getHash());
    if (l_pInputInfo)
    {
        return l_pInputInfo->m_pEffectNode;
    }
    return DM_NULL;
}

Bool                        RCEffectNode::isInputConnected(Int i_Index) const
{
    return getInputNode(i_Index) != DM_NULL;
}

Bool                        RCEffectNode::isInputConnected(const DHashString& i_InputName) const
{
    return getInputNodeByName(i_InputName) != DM_NULL;
}

Int                         RCEffectNode::getParamCount() const
{
    return me->m_ParamDB.getCount();
}

Int                         RCEffectNode::getParamIndexByName(const DHashString& i_ParamName) const
{
    for (Int i = 0; i < me->m_ParamDB.getCount(); ++i)
    {
        _RCEffectNode_ParamInfo* l_pParamInfo = me->m_ParamDB.get(i);
        if(i_ParamName == l_pParamInfo->m_Name)
            return i;
    }
    return -1;
}

const DHashString&          RCEffectNode::getParamName(Int i_Index) const
{
    DOME_ASSERT(i_Index >= 0 && i_Index < me->m_ParamDB.getCount());

    _RCEffectNode_ParamInfo* l_pParamInfo = me->m_ParamDB.get(i_Index);
    return l_pParamInfo->m_Name;
}

DSimpleTypeID               RCEffectNode::getParamType(Int i_Index) const
{
    DOME_ASSERT(i_Index >= 0 && i_Index < me->m_ParamDB.getCount());

    _RCEffectNode_ParamInfo* l_pParamInfo = me->m_ParamDB.get(i_Index);
    return l_pParamInfo->m_TypeID;
}

Int                         RCEffectNode::getParamVersion(Int i_Index) const
{
    DOME_ASSERT(i_Index >= 0 && i_Index < me->m_ParamDB.getCount());

    _RCEffectNode_ParamInfo* l_pParamInfo = me->m_ParamDB.get(i_Index);
    if (l_pParamInfo->m_bRefKey)
    {
        Int l_Version = 0;
        DOME_ASSERT(l_pParamInfo->m_Value.getTypeID() == RCGlobal::k_SimpleTypeID_Int);
        DResult l_Result = me->m_pEffect->getEffectPropertyVersion(l_pParamInfo->m_Value.getInt(), l_Version);
        DOME_ASSERT(DM_SUCC(l_Result));
        return l_Version;
    }
    else
    {
        return 0;
    }
}

const DSimpleTypedValue*    RCEffectNode::getParam(Int i_Index) const
{
    DOME_WARNING(i_Index >= 0 && i_Index < me->m_ParamDB.getCount());

    if (i_Index < 0 || i_Index >= me->m_ParamDB.getCount())
        return DM_NULL;

    _RCEffectNode_ParamInfo* l_pParamInfo = me->m_ParamDB.get(i_Index);
    if (l_pParamInfo->m_bRefKey)
    {
        DOME_ASSERT(l_pParamInfo->m_Value.getTypeID() == RCGlobal::k_SimpleTypeID_Int);
        return me->m_pEffect->getEffectPropertyValue(l_pParamInfo->m_Value.getInt());
    }
    else
    {
        return &l_pParamInfo->m_Value;
    }
}

DSimpleTypedValue*          RCEffectNode::getParam(Int i_Index)
{
    DOME_WARNING(i_Index >= 0 && i_Index < me->m_ParamDB.getCount());

    if (i_Index < 0 || i_Index >= me->m_ParamDB.getCount())
        return DM_NULL;

    _RCEffectNode_ParamInfo* l_pParamInfo = me->m_ParamDB.get(i_Index);
    if (l_pParamInfo->m_bRefKey)
    {
        DOME_ASSERT(l_pParamInfo->m_Value.getTypeID() == RCGlobal::k_SimpleTypeID_Int);
        return me->m_pEffect->getEffectPropertyValue(l_pParamInfo->m_Value.getInt());
    }
    else
    {
        return &l_pParamInfo->m_Value;
    }
}

DResult                     RCEffectNode::cacheResult(MDEffect* i_pMDEffect, Int i_OutputSelector) const
{
    DOME_ASSERT(i_OutputSelector >= 0 && i_OutputSelector < me->m_OutputDB.getCount());

    if (i_OutputSelector >= 0 && i_OutputSelector < me->m_OutputDB.getCount())
    {
        _RCEffectNode_OutputInfo* l_pOutputInfo = me->m_OutputDB.get(i_OutputSelector);
        if (l_pOutputInfo)
        {
            if(l_pOutputInfo->m_pResultCache)
                return R_ALREADYREGISTERED;
            else
            {
                l_pOutputInfo->m_pResultCache = i_pMDEffect->getTopOperand();
                return R_SUCCESS;
            }
        }
        else
            return R_FAILED;
    }
    else
        return R_OUTOFRANGE;
}

DResult                     RCEffectNode::cacheResult(MDEffect* i_pMDEffect, const DHashString& i_OutputSelector) const
{
    return cacheResult(i_pMDEffect, getOutputIndexByName(i_OutputSelector));
}

MDOperand*            RCEffectNode::getCachedResult(Int i_OutputSelector) const
{
//    DOME_ASSERT(i_OutputSelector >= 0 && i_OutputSelector < me->m_OutputDB.getCount());

    if (i_OutputSelector >= 0 && i_OutputSelector < me->m_OutputDB.getCount())
    {
        _RCEffectNode_OutputInfo* l_pOutputInfo = me->m_OutputDB.get(i_OutputSelector);
        if (l_pOutputInfo)
        {
            return l_pOutputInfo->m_pResultCache;
        }
        else
            return DM_NULL;
    }
    else
        return DM_NULL;
}

MDOperand*            RCEffectNode::getCachedResult(const DHashString& i_OutputSelector) const
{
    return getCachedResult(getOutputIndexByName(i_OutputSelector));
}

DResult                     RCEffectNode::clearResultCaches() const
{
    for (Int i = 0; i < getOutputCount(); ++i)
    {
        _RCEffectNode_OutputInfo* l_pOutputInfo = me->m_OutputDB.get(i);
        l_pOutputInfo->m_pResultCache = DM_NULL;
    }

    return R_SUCCESS;
}

DResult                     RCEffectNode::executePushInput(MDEffect* o_pMDEffect, Int i_InputIndex)
{
    PERF_COUNTER_EX(0);
    RCEffectNode* l_pInputNode = getInputNode(i_InputIndex);
    if (l_pInputNode)
    {
        const DHashString& l_InputSelector = getInputSelector(i_InputIndex);
        return l_pInputNode->execute(o_pMDEffect, l_InputSelector);
    }
    return R_FAILED;
}

DResult                     RCEffectNode::execute(MDEffect* o_pMDEffect, Int i_OutputSelector)
{
    MDOperand* l_CachedResult = getCachedResult(i_OutputSelector);
    if (l_CachedResult)
    {
        o_pMDEffect->pushOperand(l_CachedResult);
        return R_SUCCESS;
    }
    return buildMDEffect(o_pMDEffect, i_OutputSelector);
}

DResult                     RCEffectNode::execute(MDEffect* o_pMDEffect, const DHashString& i_OutputSelector)
{
    return execute(o_pMDEffect, getOutputIndexByName(i_OutputSelector));
}


RC_NAMESPACE_END