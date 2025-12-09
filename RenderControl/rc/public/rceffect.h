/*
    filename:       rceffect.h
    author:         Ming Dong
    date:           2016-MAR-24
    description:    
*/
#pragma once

#include "rcrenderer.h"
#include "rceffectnode.h"
#include "rcparametersys.h"

RC_NAMESPACE_BEGIN

class RCEffectManager;
struct RCEffect_Data;
class RC_API RCEffect
{
public:
    RCEffect(const DHashString& i_EffectName, RCEffectManager* i_pEffectMgr);
    RCEffect(const DHashString& i_EffectName, const DString& i_EffectContent, const DString& i_EditorData, RCEffectManager* i_pEffectMgr);
    ~RCEffect();

    RCEffectManager* getEffectManager() const;

    const DHashString& getName() const;

    const DHashString& getGuid() const;

    // load effect from file
    DResult     reload();

    // set effect
    DResult     set(const DString& i_EffectContent, const DString& i_EditorData);

    // get effect content
    DResult     getEditordata(DString& o_EditorData);

    // save effect to file
    DResult     save() const;

    // if the effect match to the disk file
    Bool        isModified() const;

    // get current frame number
    Int         getCurrentFrame() const;

    // build the effect
    void build();

    // execute the effect
    DResult     execute();

    // reset effect callback
    void        resetEffect();

    // parameter system
    RCParameterSys&         getParamSys();

    const RCParameterSys&   getParamSys() const;

    // effect parameter interface
    DResult                     readMetaDataElement(const Char* i_pStr, DString& o_ElementName, DString& o_ElementValue) const;
    void                        resetEffectProperty();
    Int                         addEffectProperty(const DHashString& i_RefKey, DSimpleTypeID i_TypeID, const DString& i_MetaData);
    Int                         getEffectPropertyCount() const;
    const DHashString&          getEffectPropertyRefKey(Int i_Index) const;
    DSimpleTypeID               getEffectPropertyTypeID(Int i_Index) const;
    const DString&              getEffectPropertyEditCtrl(Int i_Index) const;

    const DSimpleTypedValue*    getEffectPropertyValue(Int i_Index) const;
    DSimpleTypedValue*          getEffectPropertyValue(Int i_Index);
    DResult                     setEffectPropertyValue(Int i_Index, const DSimpleTypedValue* i_pValue);
    DResult                     getEffectPropertyValueInString(Int i_Index, DString& o_StrValue) const;
    DResult                     setEffectPropertyValueFromString(Int i_Index, const DString& i_StrValue);
    DResult                     getEffectPropertyVersion(Int i_Index, Int& o_Version);


private:
    void _clearEffectNode();
    void _updateEffect();

    RCEffect_Data*      me;
};


RC_NAMESPACE_END