/*
    filename:       rceffect.cpp
    author:         Ming Dong
    date:           2016-MAY-14
    description:    
*/

#include "../public/rceffect.h"
#include "../public/rceffectmanager.h"
#include "../public/rcmanager.h"
#include "../public/mdeffect.h"
#include "../public/rcglobal.h"
#include "../../../DevEnv/Include/PerfAnalyzer.h"


RC_NAMESPACE_BEGIN

#define RC_EXTREAM_OPTIMIZE

struct RCEffect_Property
{
    DHashString             m_RefKey;
    DSimpleTypeID           m_TypeID;
    DSimpleTypedValue       m_Value;
    DString                 m_EditCtrl;
    Int                     m_Version;
};

struct RCEffect_Data
{
    typedef TArray<RCEffectNode*, IDefaultMemManager, 32>       _EffectNodeArray;
    RCEffectManager*        m_pEffectMgr;
    DHashString             m_EffectName;
    DHashString             m_EffectGuid;
    DString                 m_EffectContent;
    DString                 m_EditorData;
    Bool                    m_bDirty;
    RCParameterSys          m_ParamSys;
    Int                     m_CurFrame;

    TArray<RCEffect_Property>
                            m_EffectPropertyArray;
    TMap<DHashString, Int>
                            m_EffectPropertyMap;

    RCEffectNode*           m_pRootNode;
    _EffectNodeArray        m_EffectNodeArray;

    MDEffect*               m_pMDEffect;
};

const char* g_EditorData = "\
<EffectWidget CompositeNodeName=\"\" SelectNodeGUID=\"{B2F9B098-4295-467C-B242-53AE140E6F3C}\">                                                                                         \
    <RCNode PosX=\"-366\" GUID=\"{49E7C381-AA97-4F64-8324-8DFF38D30490}\" NodeName=\"ColorInput\" PosY=\"-128\" Type=\"RCColorInputNode\" Version=\"0.1\"/>                             \
    <RCNode PosX=\"-105\" GUID=\"{B2F9B098-4295-467C-B242-53AE140E6F3C}\" NodeName=\"Output\" PosY=\"-129\" Type=\"RCOutputNode\" Version=\"0.1\">                                      \
        <Property Value=\"default\" IsRefKey=\"False\" Type=\"DString\" RefKey=\"\" Index=\"0\"/>                                                                                       \
        <Property Value=\"0\" IsRefKey=\"False\" Type=\"F32\" RefKey=\"\" Index=\"1\"/>                                                                                                 \
        <Property Value=\"0.000000,0.000000\" IsRefKey=\"False\" Type=\"DVector2\" RefKey=\"\" Index=\"2\"/>                                                                            \
        <Property Value=\"1.000000,1.000000\" IsRefKey=\"False\" Type=\"DVector2\" RefKey=\"\" Index=\"3\"/>                                                                            \
        <Property Value=\"0.000000,0.000000\" IsRefKey=\"False\" Type=\"DVector2\" RefKey=\"\" Index=\"4\"/>                                                                            \
        <Property Value=\"1.000000,1.000000\" IsRefKey=\"False\" Type=\"DVector2\" RefKey=\"\" Index=\"5\"/>                                                                            \
    </RCNode>                                                                                                                                                                           \
    <RCConnection OutputConnectorIndex=\"0\" OutputGUID=\"{49E7C381-AA97-4F64-8324-8DFF38D30490}\" InputGUID=\"{B2F9B098-4295-467C-B242-53AE140E6F3C}\" InputConnectorIndex=\"0\"/>     \
</EffectWidget>                                                                                                                                                                         \
";

const char* g_RCEffectData = "                                                                                                                                                          \
<RCCompositor guid=\"2011FC56-0663-4C95-BB51-342447300DFE\">                                                                                                                            \
	<RCOpNode name=\"{49E7C381-AA97-4F64-8324-8DFF38D30490}\" type=\"RCColorInputNode\">                                                                                                \
		<Output index=\"0\" name=\"Output\" type=\"OSTexture2D\"/>                                                                                                                      \
	</RCOpNode>                                                                                                                                                                         \
	<RCOpNode name=\"{B2F9B098-4295-467C-B242-53AE140E6F3C}\" type=\"RCOutputNode\">                                                                                                    \
		<Parameter index=\"0\" name=\"EffectName\" type=\"DString\" refkey=\"false\" metadata=\"\" value=\"default\"/>                                                                  \
		<Parameter index=\"1\" name=\"BlendType\" type=\"F32\" refkey=\"false\" metadata=\"\" value=\"0.000000\"/>                                                                      \
		<Parameter index=\"2\" name=\"SrcUVPos\" type=\"DVector2f\" refkey=\"false\" metadata=\"\" value=\"0.000000,0.000000\"/>                                                        \
		<Parameter index=\"3\" name=\"SrcUVSize\" type=\"DVector2f\" refkey=\"false\" metadata=\"\" value=\"1.000000,1.000000\"/>                                                       \
		<Parameter index=\"4\" name=\"DstUVPos\" type=\"DVector2f\" refkey=\"false\" metadata=\"\" value=\"0.000000,0.000000\"/>                                                        \
		<Parameter index=\"5\" name=\"DstUVSize\" type=\"DVector2f\" refkey=\"false\" metadata=\"\" value=\"1.000000,1.000000\"/>                                                       \
		<Input index=\"0\" name=\"ColorInput\" type=\"OSTexture2D\" required=\"false\" operatorname=\"{49E7C381-AA97-4F64-8324-8DFF38D30490}\" selector=\"Output\"/>                    \
	</RCOpNode>                                                                                                                                                                         \
</RCCompositor>                                                                                                                                                                         \
";

RCEffect::RCEffect(const DHashString& i_EffectName, RCEffectManager* i_pEffectMgr)
{
    me = DOME_New(RCEffect_Data);
    me->m_pEffectMgr = i_pEffectMgr;
    me->m_EffectName = i_EffectName;
    me->m_bDirty = DM_TRUE;
    me->m_CurFrame = 0;
    me->m_pRootNode = DM_NULL;
    me->m_pMDEffect = DM_NULL;

    DResult l_Result = reload();
    if (DM_FAIL(l_Result))
    {
        set(g_RCEffectData, g_EditorData);
    }
}

RCEffect::RCEffect(const DHashString& i_EffectName, const DString& i_EffectContent, const DString& i_EditorData, RCEffectManager* i_pEffectMgr)
{
    me = DOME_New(RCEffect_Data);
    me->m_pEffectMgr = i_pEffectMgr;
    me->m_EffectName = i_EffectName;
    me->m_bDirty = DM_TRUE;
    me->m_CurFrame = 0;
    me->m_pRootNode = DM_NULL;
    me->m_pMDEffect = DM_NULL;

    set(i_EffectContent, i_EditorData);
}

RCEffect::~RCEffect()
{
    _clearEffectNode();

    if (me->m_pMDEffect)
    {
        DOME_Del(me->m_pMDEffect);
        me->m_pMDEffect = DM_NULL;
    }

    DOME_Del(me);
}

RCEffectManager* RCEffect::getEffectManager() const
{
    return me->m_pEffectMgr;
}

const DHashString& RCEffect::getName() const
{
    return me->m_EffectName;
}

const DHashString& RCEffect::getGuid() const
{
    return me->m_EffectGuid;
}

// load effect from file
DResult     RCEffect::reload()
{
    if (me->m_pMDEffect)
    {
        DOME_Del(me->m_pMDEffect);
        me->m_pMDEffect = DM_NULL;
    }

    DResult l_Result;
    DString l_FilePath;
    DString l_FilePath2;
    l_Result = me->m_pEffectMgr->getRenderer()->getDataPath(l_FilePath);
    DOME_ASSERT(DM_SUCC(l_Result));
    l_FilePath += me->m_EffectName.c_str();
    l_FilePath2 = l_FilePath;
    l_FilePath += ".xml";
    l_FilePath2 += ".rc";

    {
        DFile l_EffectFile(l_FilePath, DOME_GetExternalFS());
        l_Result = l_EffectFile.open(DM_FALSE);
        if(DM_FAIL(l_Result))
            return l_Result;

        Int l_FileLen = l_EffectFile.getLength();
        Int l_ReadLen = l_FileLen;
        Char* l_Buffer = (Char*)DOME_Alloc(l_FileLen + 1);
        l_EffectFile.seek(DM_TRUE, 0);
        l_EffectFile.read(l_Buffer, l_ReadLen);
        l_EffectFile.close();
        l_Buffer[l_FileLen] = 0;

        DOME_ASSERT(l_FileLen == l_ReadLen);

        me->m_EffectContent = l_Buffer;

        DOME_Free(l_Buffer);
    }

    {
        DFile l_EditorFile(l_FilePath2, DOME_GetExternalFS());
        l_Result = l_EditorFile.open(DM_FALSE);
        if (DM_SUCC(l_Result))
        {
            Int l_FileLen = l_EditorFile.getLength();
            Int l_ReadLen = l_FileLen;
            Char* l_Buffer = (Char*)DOME_Alloc(l_FileLen + 1);
            l_EditorFile.seek(DM_TRUE, 0);
            l_EditorFile.read(l_Buffer, l_ReadLen);
            l_EditorFile.close();
            l_Buffer[l_FileLen] = 0;

            DOME_ASSERT(l_FileLen == l_ReadLen);

            me->m_EditorData = l_Buffer;

            DOME_Free(l_Buffer);
        }
    }

    me->m_bDirty = DM_FALSE;

    _updateEffect();

    return R_SUCCESS;
}

// set effect
DResult     RCEffect::set(const DString& i_EffectContent, const DString& i_EditorData)
{
    me->m_EffectContent = i_EffectContent;
    me->m_EditorData = i_EditorData;
    me->m_bDirty = DM_TRUE;

    if (me->m_pMDEffect)
    {
        DOME_Del(me->m_pMDEffect);
        me->m_pMDEffect = DM_NULL;
    }

    _updateEffect();

    return R_SUCCESS;
}

// get editor data
DResult     RCEffect::getEditordata(DString& o_EditorData)
{
    o_EditorData = me->m_EditorData;
    return R_SUCCESS;
}

// save effect to file
DResult     RCEffect::save() const
{
    DResult l_Result;
    DString l_FilePath;
    DString l_FilePath2;
    l_Result = me->m_pEffectMgr->getRenderer()->getDataPath(l_FilePath);
    DOME_ASSERT(DM_SUCC(l_Result));
    l_FilePath += me->m_EffectName.c_str();
    l_FilePath2 = l_FilePath;
    l_FilePath += ".xml";
    l_FilePath2 += ".rc";

    {
        DFile l_EffectFile(l_FilePath, DOME_GetExternalFS());
        l_Result = l_EffectFile.create(DM_TRUE);
        if(DM_FAIL(l_Result))
            return l_Result;

        Int l_WriteSize = me->m_EffectContent.size();
        l_EffectFile.write(me->m_EffectContent.c_str(), l_WriteSize);
        l_EffectFile.close();
    }

    {
        DFile l_EditorFile(l_FilePath2, DOME_GetExternalFS());
        l_Result = l_EditorFile.create(DM_TRUE);
        if(DM_FAIL(l_Result))
            return l_Result;

        Int l_WriteSize = me->m_EditorData.size();
        l_EditorFile.write(me->m_EditorData.c_str(), l_WriteSize);
        l_EditorFile.close();
    }

    return R_SUCCESS;
}

// if the effect match to the disk file
Bool        RCEffect::isModified() const
{
    return me->m_bDirty;
}

// get current frame number
Int         RCEffect::getCurrentFrame() const
{
    return me->m_CurFrame;
}

// execute the effect
void     RCEffect::build()
{
#ifdef RC_EXTREAM_OPTIMIZE

    if (getEffectManager()->isExtreamOptimizationMode())
    {
        // just make sure, call clearResultCache before execute also, this function should be light weight.
        for (Int i = 0; i < me->m_EffectNodeArray.size(); ++i)
            me->m_EffectNodeArray[i]->clearResultCaches();

        if (!me->m_pMDEffect)
        {
            me->m_pMDEffect = DOME_New(MDEffect)(this);


            me->m_pMDEffect->begin();
            me->m_pRootNode->execute(me->m_pMDEffect, 0);
            me->m_pMDEffect->end();

        }
        else
        {
            MDEffect l_MDEffect(this);
            l_MDEffect.begin();
            me->m_pRootNode->execute(&l_MDEffect, 0);

            me->m_pMDEffect->end(true);
        }

        for (Int i = 0; i < me->m_EffectNodeArray.size(); ++i)
            me->m_EffectNodeArray[i]->clearResultCaches();
    }
#endif
}

// execute the effect
DResult     RCEffect::execute()
{
    PERF_COUNTER_EX(0);
#ifdef RC_EXTREAM_OPTIMIZE

    if (getEffectManager()->isExtreamOptimizationMode())
    {
        DResult l_Result = me->m_pMDEffect->execute();

        me->m_CurFrame ++;

        return l_Result;
    }
    else
    {
        if (me->m_pMDEffect)
        {
            DOME_Del(me->m_pMDEffect);
            me->m_pMDEffect = DM_NULL;
        }

        MDEffect l_MDEffect(this);

        // just make sure, call clearResultCache before execute also, this function should be light weight.
        for (Int i = 0; i < me->m_EffectNodeArray.size(); ++i)
            me->m_EffectNodeArray[i]->clearResultCaches();

        l_MDEffect.begin();
        me->m_pRootNode->execute(&l_MDEffect, 0);
        l_MDEffect.end();

        for (Int i = 0; i < me->m_EffectNodeArray.size(); ++i)
            me->m_EffectNodeArray[i]->clearResultCaches();

        DResult l_Result = l_MDEffect.execute();

        me->m_CurFrame ++;

        return l_Result;
    }

#else

    MDEffect l_MDEffect(this);

    // just make sure, call clearResultCache before execute also, this function should be light weight.
    for (Int i = 0; i < me->m_EffectNodeArray.size(); ++i)
        me->m_EffectNodeArray[i]->clearResultCaches();

    l_MDEffect.begin();
    me->m_pRootNode->execute(&l_MDEffect, 0);
    l_MDEffect.end();

    for (Int i = 0; i < me->m_EffectNodeArray.size(); ++i)
        me->m_EffectNodeArray[i]->clearResultCaches();

    DResult l_Result = l_MDEffect.execute();

    me->m_CurFrame ++;

    return l_Result;
#endif
}

RCParameterSys&         RCEffect::getParamSys()
{
    return me->m_ParamSys;
}

const RCParameterSys&   RCEffect::getParamSys() const
{
    return me->m_ParamSys;
}

DResult RCEffect::readMetaDataElement(const Char* i_pStr, DString& o_ElementName, DString& o_ElementValue) const
{
    Int l_BuffLen = OS_String::TGetStrCharCount(i_pStr) + 1;
    Char* l_pBuff = (Char*)DOME_Alloc(l_BuffLen);
    const Char* l_pSrc = i_pStr;

    // Read until '{'
    Char* l_pDst = l_pBuff;
    while(*l_pSrc != '{' && *l_pSrc != '\0')
        *l_pDst++ = *l_pSrc++;
    *l_pDst++ = 0;
    if (*l_pSrc == '\0')
    {
        DOME_Free(l_pBuff);
        return R_FAILED;
    }
    l_pSrc ++;
    o_ElementName = l_pBuff;

    // Find the paired '}'
    l_pDst = l_pBuff;
    Int l_NumLeft = 0;
    while (*l_pSrc != '}' || l_NumLeft != 0)
    {
        if (*l_pSrc == 0)
        {
            DOME_Free(l_pBuff);
            return R_FAILED;
        }
        if(*l_pSrc == '{')
            l_NumLeft ++;
        if(*l_pSrc == '}')
            l_NumLeft --;
        *l_pDst++ = *l_pSrc++;
    }
    *l_pDst++ = 0;

    o_ElementValue = l_pBuff;
    DOME_Free(l_pBuff);
    return R_SUCCESS;
}

void RCEffect::resetEffectProperty()
{
    me->m_EffectPropertyMap.clear();
    me->m_EffectPropertyArray.clear();
}

Int RCEffect::addEffectProperty(const DHashString& i_RefKey, DSimpleTypeID i_TypeID, const DString& i_MetaData)
{
    DString l_EditCtrl, l_Default;
    DString l_ElementName, l_ElementValue;
    const Char* l_pMetaData = i_MetaData.c_str();

    while (DM_SUCC(readMetaDataElement(l_pMetaData, l_ElementName, l_ElementValue)))
    {
        l_pMetaData += (l_ElementName.size() + l_ElementValue.size() + 2);
        if(l_ElementName == "RefDefault")
            l_Default = l_ElementValue;
        else if(l_ElementName == "RefEditCtrl")
            l_EditCtrl = l_ElementValue;
    }

    TMap<DHashString, Int>::iterator it = me->m_EffectPropertyMap.find(i_RefKey);
    if (it != me->m_EffectPropertyMap.end())
    {
        DOME_ASSERT(i_TypeID == me->m_EffectPropertyArray[it->second].m_TypeID);
        return it->second;
    }

    RCEffect_Property l_EffectProperty;
    l_EffectProperty.m_RefKey = i_RefKey;
    l_EffectProperty.m_TypeID = i_TypeID;
    l_EffectProperty.m_EditCtrl = l_EditCtrl;
    l_EffectProperty.m_Version = 0;
    if (l_EffectProperty.m_TypeID == RCGlobal::k_SimpleTypeID_DString)
    {
        l_EffectProperty.m_Value.initType(RCGlobal::k_SimpleTypeID_DString);
        if(l_Default.size() > 0)
            l_EffectProperty.m_Value.setDString(l_Default);
    }
    else if (l_EffectProperty.m_TypeID == RCGlobal::k_SimpleTypeID_F32)
    {
        l_EffectProperty.m_Value.initType(RCGlobal::k_SimpleTypeID_F32);
        if (l_Default.size() > 0)
        {
            F32 l_Value;
            Math::MathFromDString(l_Default, l_Value);
            l_EffectProperty.m_Value.setF32(l_Value);
        }
    }
    else if (l_EffectProperty.m_TypeID == RCGlobal::k_SimpleTypeID_DVector2f)
    {
        l_EffectProperty.m_Value.initType(RCGlobal::k_SimpleTypeID_DVector2f);
        if (l_Default.size() > 0)
        {
            DVector2f l_Value;
            Math::MathFromDString(l_Default, l_Value);
            l_EffectProperty.m_Value.setDVector2f(l_Value);
        }
    }
    else if (l_EffectProperty.m_TypeID == RCGlobal::k_SimpleTypeID_DVector3f)
    {
        l_EffectProperty.m_Value.initType(RCGlobal::k_SimpleTypeID_DVector3f);
        if (l_Default.size() > 0)
        {
            DVector3f l_Value;
            Math::MathFromDString(l_Default, l_Value);
            l_EffectProperty.m_Value.setDVector3f(l_Value);
        }
    }
    else if (l_EffectProperty.m_TypeID == RCGlobal::k_SimpleTypeID_DVector4f)
    {
        l_EffectProperty.m_Value.initType(RCGlobal::k_SimpleTypeID_DVector4f);
        if (l_Default.size() > 0)
        {
            DVector4f l_Value;
            Math::MathFromDString(l_Default, l_Value);
            l_EffectProperty.m_Value.setDVector4f(l_Value);
        }
    }
    else if (l_EffectProperty.m_TypeID == RCGlobal::k_SimpleTypeID_DVectorLut1f)
    {
        l_EffectProperty.m_Value.initType(RCGlobal::k_SimpleTypeID_DVectorLut1f);
        if (l_Default.size() > 0)
        {
            DVectorLut1f l_Value;
            Math::MathFromDString(l_Default, l_Value);
            l_EffectProperty.m_Value.setDVectorLut1f(l_Value);
        }
    }
    else if (l_EffectProperty.m_TypeID == RCGlobal::k_SimpleTypeID_DVectorLut2f)
    {
        l_EffectProperty.m_Value.initType(RCGlobal::k_SimpleTypeID_DVectorLut2f);
        if (l_Default.size() > 0)
        {
            DVectorLut2f l_Value;
            Math::MathFromDString(l_Default, l_Value);
            l_EffectProperty.m_Value.setDVectorLut2f(l_Value);
        }
    }
    else if (l_EffectProperty.m_TypeID == RCGlobal::k_SimpleTypeID_DVectorLut3f)
    {
        l_EffectProperty.m_Value.initType(RCGlobal::k_SimpleTypeID_DVectorLut3f);
        if (l_Default.size() > 0)
        {
            DVectorLut3f l_Value;
            Math::MathFromDString(l_Default, l_Value);
            l_EffectProperty.m_Value.setDVectorLut3f(l_Value);
        }
    }
    else if (l_EffectProperty.m_TypeID == RCGlobal::k_SimpleTypeID_DVectorLut4f)
    {
        l_EffectProperty.m_Value.initType(RCGlobal::k_SimpleTypeID_DVectorLut4f);
        if (l_Default.size() > 0)
        {
            DVectorLut4f l_Value;
            Math::MathFromDString(l_Default, l_Value);
            l_EffectProperty.m_Value.setDVectorLut4f(l_Value);
        }
    }

    me->m_EffectPropertyArray.push_back(l_EffectProperty);
    me->m_EffectPropertyMap.insert(i_RefKey, me->m_EffectPropertyArray.size() - 1);

    return me->m_EffectPropertyArray.size() - 1;
}

Int RCEffect::getEffectPropertyCount() const
{
    return me->m_EffectPropertyArray.size();
}

const DHashString& RCEffect::getEffectPropertyRefKey(Int i_Index) const
{
    return me->m_EffectPropertyArray[i_Index].m_RefKey;
}

DSimpleTypeID RCEffect::getEffectPropertyTypeID(Int i_Index) const
{
    return me->m_EffectPropertyArray[i_Index].m_TypeID;
}

const DString& RCEffect::getEffectPropertyEditCtrl(Int i_Index) const
{
    return me->m_EffectPropertyArray[i_Index].m_EditCtrl;
}

const DSimpleTypedValue* RCEffect::getEffectPropertyValue(Int i_Index) const
{
    DOME_ASSERT(i_Index >= 0 && i_Index < me->m_EffectPropertyArray.size());
    return &me->m_EffectPropertyArray[i_Index].m_Value;
}

DSimpleTypedValue* RCEffect::getEffectPropertyValue(Int i_Index)
{
    DOME_ASSERT(i_Index >= 0 && i_Index < me->m_EffectPropertyArray.size());
    return &me->m_EffectPropertyArray[i_Index].m_Value;
}

DResult RCEffect::setEffectPropertyValue(Int i_Index, const DSimpleTypedValue* i_pValue)
{
    if (me->m_pMDEffect && (!getEffectManager()->isSafeModifyMode()))
    {
        DOME_Del(me->m_pMDEffect);
        me->m_pMDEffect = DM_NULL;
    }

    DOME_ASSERT(i_pValue);
    DOME_ASSERT(i_Index >= 0 && i_Index < me->m_EffectPropertyArray.size());
    me->m_EffectPropertyArray[i_Index].m_Value = *i_pValue;
    me->m_EffectPropertyArray[i_Index].m_Version ++;
    return R_SUCCESS;
}

DResult RCEffect::getEffectPropertyValueInString(Int i_Index, DString& o_StrValue) const
{
    DOME_ASSERT(i_Index >= 0 && i_Index < me->m_EffectPropertyArray.size());
    if (me->m_EffectPropertyArray[i_Index].m_TypeID == RCGlobal::k_SimpleTypeID_DString)
    {
        o_StrValue = me->m_EffectPropertyArray[i_Index].m_Value.getDString();
        return R_SUCCESS;
    }
    else if (me->m_EffectPropertyArray[i_Index].m_TypeID == RCGlobal::k_SimpleTypeID_F32)
    {
        return Math::MathToDString(me->m_EffectPropertyArray[i_Index].m_Value.getF32(), o_StrValue);
    }
    else if (me->m_EffectPropertyArray[i_Index].m_TypeID == RCGlobal::k_SimpleTypeID_DVector2f)
    {
        return Math::MathToDString(me->m_EffectPropertyArray[i_Index].m_Value.getDVector2f(), o_StrValue);
    }
    else if (me->m_EffectPropertyArray[i_Index].m_TypeID == RCGlobal::k_SimpleTypeID_DVector3f)
    {
        return Math::MathToDString(me->m_EffectPropertyArray[i_Index].m_Value.getDVector3f(), o_StrValue);
    }
    else if (me->m_EffectPropertyArray[i_Index].m_TypeID == RCGlobal::k_SimpleTypeID_DVector4f)
    {
        return Math::MathToDString(me->m_EffectPropertyArray[i_Index].m_Value.getDVector4f(), o_StrValue);
    }
    else if (me->m_EffectPropertyArray[i_Index].m_TypeID == RCGlobal::k_SimpleTypeID_DVectorLut1f)
    {
        return Math::MathToDString(me->m_EffectPropertyArray[i_Index].m_Value.getDVectorLut1f(), o_StrValue);
    }
    else if (me->m_EffectPropertyArray[i_Index].m_TypeID == RCGlobal::k_SimpleTypeID_DVectorLut2f)
    {
        return Math::MathToDString(me->m_EffectPropertyArray[i_Index].m_Value.getDVectorLut2f(), o_StrValue);
    }
    else if (me->m_EffectPropertyArray[i_Index].m_TypeID == RCGlobal::k_SimpleTypeID_DVectorLut3f)
    {
        return Math::MathToDString(me->m_EffectPropertyArray[i_Index].m_Value.getDVectorLut3f(), o_StrValue);
    }
    else if (me->m_EffectPropertyArray[i_Index].m_TypeID == RCGlobal::k_SimpleTypeID_DVectorLut4f)
    {
        return Math::MathToDString(me->m_EffectPropertyArray[i_Index].m_Value.getDVectorLut4f(), o_StrValue);
    }

    DOME_ASSERT2(0, "Warning: your type is not supported!");
    return R_FAILED;
}

DResult RCEffect::setEffectPropertyValueFromString(Int i_Index, const DString& i_StrValue)
{
    if (me->m_pMDEffect && (!getEffectManager()->isSafeModifyMode()))
    {
        DOME_Del(me->m_pMDEffect);
        me->m_pMDEffect = DM_NULL;
    }

    DOME_ASSERT(i_Index >= 0 && i_Index < me->m_EffectPropertyArray.size());
    me->m_EffectPropertyArray[i_Index].m_Version ++;
    if (me->m_EffectPropertyArray[i_Index].m_TypeID == RCGlobal::k_SimpleTypeID_DString)
    {
        me->m_EffectPropertyArray[i_Index].m_Value.setDString(i_StrValue);
        return R_SUCCESS;
    }
    else if (me->m_EffectPropertyArray[i_Index].m_TypeID == RCGlobal::k_SimpleTypeID_F32)
    {
        F32 l_Value;
        Math::MathFromDString(i_StrValue, l_Value);
        return me->m_EffectPropertyArray[i_Index].m_Value.setF32(l_Value);
    }
    else if (me->m_EffectPropertyArray[i_Index].m_TypeID == RCGlobal::k_SimpleTypeID_DVector2f)
    {
        DVector2f l_Value;
        Math::MathFromDString(i_StrValue, l_Value);
        return me->m_EffectPropertyArray[i_Index].m_Value.setDVector2f(l_Value);
    }
    else if (me->m_EffectPropertyArray[i_Index].m_TypeID == RCGlobal::k_SimpleTypeID_DVector3f)
    {
        DVector3f l_Value;
        Math::MathFromDString(i_StrValue, l_Value);
        return me->m_EffectPropertyArray[i_Index].m_Value.setDVector3f(l_Value);
    }
    else if (me->m_EffectPropertyArray[i_Index].m_TypeID == RCGlobal::k_SimpleTypeID_DVector4f)
    {
        DVector4f l_Value;
        Math::MathFromDString(i_StrValue, l_Value);
        return me->m_EffectPropertyArray[i_Index].m_Value.setDVector4f(l_Value);
    }
    else if (me->m_EffectPropertyArray[i_Index].m_TypeID == RCGlobal::k_SimpleTypeID_DVectorLut1f)
    {
        DVectorLut1f l_Value;
        Math::MathFromDString(i_StrValue, l_Value);
        return me->m_EffectPropertyArray[i_Index].m_Value.setDVectorLut1f(l_Value);
    }
    else if (me->m_EffectPropertyArray[i_Index].m_TypeID == RCGlobal::k_SimpleTypeID_DVectorLut2f)
    {
        DVectorLut2f l_Value;
        Math::MathFromDString(i_StrValue, l_Value);
        return me->m_EffectPropertyArray[i_Index].m_Value.setDVectorLut2f(l_Value);
    }
    else if (me->m_EffectPropertyArray[i_Index].m_TypeID == RCGlobal::k_SimpleTypeID_DVectorLut3f)
    {
        DVectorLut3f l_Value;
        Math::MathFromDString(i_StrValue, l_Value);
        return me->m_EffectPropertyArray[i_Index].m_Value.setDVectorLut3f(l_Value);
    }
    else if (me->m_EffectPropertyArray[i_Index].m_TypeID == RCGlobal::k_SimpleTypeID_DVectorLut4f)
    {
        DVectorLut4f l_Value;
        Math::MathFromDString(i_StrValue, l_Value);
        return me->m_EffectPropertyArray[i_Index].m_Value.setDVectorLut4f(l_Value);
    }

    DOME_ASSERT2(0, "Warning: your type is not supported!");
    return R_FAILED;
}

DResult RCEffect::getEffectPropertyVersion(Int i_Index, Int& o_Version)
{
    DOME_ASSERT(i_Index >= 0 && i_Index < me->m_EffectPropertyArray.size());
    o_Version = me->m_EffectPropertyArray[i_Index].m_Version;
    return R_SUCCESS;
}

void RCEffect::_clearEffectNode()
{
    if (me->m_pMDEffect)
    {
        DOME_Del(me->m_pMDEffect);
        me->m_pMDEffect = DM_NULL;
    }

    for (Int i = 0; i < me->m_EffectNodeArray.size(); ++i)
    {
        RCManager::Instance().destroyRCEffectNode(me->m_EffectNodeArray[i]);
    }
    me->m_EffectNodeArray.clear();
    me->m_pRootNode = DM_NULL;
}

void RCEffect::_updateEffect()
{
    me->m_CurFrame = 0;
    _clearEffectNode();
    me->m_ParamSys.clearAllParameters();
    resetEffectProperty();

    Char* l_Buffer = (Char*)DOME_Alloc(me->m_EffectContent.size() + 1);
    OS_String::TStrCopy(l_Buffer, me->m_EffectContent.c_str());

    rapidxml::xml_document<> l_XmlDoc;
    rapidxml::xml_node<>* l_pXmlEffectRoot = DM_NULL;
    rapidxml::xml_attribute<>* l_pXmlAttrib = DM_NULL;
    l_XmlDoc.parse<0>(l_Buffer);
    l_pXmlEffectRoot = l_XmlDoc.first_node("RCCompositor");
    if (!l_pXmlEffectRoot)
    {
        DOME_WARNING(0);
        return;
    }

    // load effect guid
    l_pXmlAttrib = l_pXmlEffectRoot->first_attribute("guid");
    if (l_pXmlAttrib)
    {
        me->m_EffectGuid = l_pXmlAttrib->value();
    }
    else
    {
        DOME_NS::DGuid l_Guid;
        l_Guid.makeUnique();
        l_Guid.toStringT<DHashString>(me->m_EffectGuid, DM_FALSE);
    }

    // Load all the effect nodes
    for (rapidxml::xml_node<>* l_pXmlEffectNode = l_pXmlEffectRoot->first_node("RCOpNode");
        l_pXmlEffectNode != DM_NULL;
        l_pXmlEffectNode = l_pXmlEffectNode->next_sibling("RCOpNode"))
    {
        rapidxml::xml_attribute<>* l_pXmlNodeAttrib = l_pXmlEffectNode->first_attribute("type");
        DOME_ASSERT(l_pXmlNodeAttrib);
        const Char* l_pStrType = l_pXmlNodeAttrib->value();
        DStringHash l_TypeID(l_pStrType);
        RCEffectNode* l_pEffectNode = RCManager::Instance().createRCEffectNode(l_TypeID, this);
        DOME_ASSERT(l_pEffectNode);

        l_pEffectNode->load(l_pXmlEffectNode);

        me->m_EffectNodeArray.push_back(l_pEffectNode);

        const static DStringHash k_TypeID_RCOutputNode("RCOutputNode");
        if (l_TypeID == k_TypeID_RCOutputNode)
        {
            DOME_ASSERT(me->m_pRootNode == DM_NULL);
            me->m_pRootNode = l_pEffectNode;
        }
    }

    DOME_Free(l_Buffer);

    // resolve node's input
    for (Int i = me->m_EffectNodeArray.size() - 1; i >= 0; --i)
    {
        me->m_EffectNodeArray[i]->resolveInputs(&me->m_EffectNodeArray[0], me->m_EffectNodeArray.size());
        me->m_EffectNodeArray[i]->finishLoad();
    }

    resetEffect();
}

void RCEffect::resetEffect()
{
    for (Int i = me->m_EffectNodeArray.size() - 1; i >= 0; --i)
    {
        me->m_EffectNodeArray[i]->onReset();
    }
}

RC_NAMESPACE_END