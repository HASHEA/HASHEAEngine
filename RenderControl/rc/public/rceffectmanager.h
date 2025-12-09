/*
    filename:       rceffectmanager.h
    author:         Ming Dong
    date:           2016-MAR-24
    description:    
*/
#pragma once

#include "rcrenderer.h"
#include "rceffect.h"
#include "rcparametersys.h"

RC_NAMESPACE_BEGIN

struct RCEffectManager_Data;
class MDExecuter;
class RC_API RCEffectManager
{
public:
    RCEffectManager(RCRenderer* i_pRenderer);
    ~RCEffectManager();

    DResult                     setExtreamOptimizationMode(Bool i_bEnable);
    Bool                        isExtreamOptimizationMode() const;
    void                        setSafeModifyMode(Bool i_bSafe);
    Bool                        isSafeModifyMode() const;


    DResult                     reset();

    RCRenderer*                 getRenderer() const;

    // Executer management functions
    DResult                     clearAllMDExecuter();

    MDExecuter*                 getMDExecuter(const DHashString& i_Signature);

    // Effects management functions
    DResult                     clearAllEffects();

    // reset all effect
    void                        resetAllEffects();

    DResult                     createEffect(const DHashString& i_EffectName, const DString& i_EffectContent, const DString& i_EditorData);

    DResult                     loadEffect(const DHashString& i_EffectName);

    DResult                     removeEffect(const DHashString& i_EffectName);

    DResult                     saveEffect(const DHashString& i_EffectName) const;

    DResult                     updateEffect(const DHashString& i_EffectName, const DString& i_EffectContent, const DString& i_EditorData);

    Int                         getEffectCount() const;

    const DHashString&          getEffectNameByIndex(Int i_Index) const;

    const DHashString&          getCurrentEffectName() const;

    DResult                     setCurrentEffect(const DHashString& i_EffectName);

    RCEffect*                   getCurrentEffect();

    DResult                     saveCurrentEffect() const;

    DResult                     updateCurrentEffect(const DString& i_EffectContent, const DString& i_EditorData);

    DResult                     build();
    DResult                     execute();

    // parameter management functions
    RCParameterSys&             getParamSys();

    const RCParameterSys&       getParamSys() const;

    // functions for plugins
    DResult                     registerPlugin(const DString& i_PluginName, void* i_pPlugin);
    void*                       getPlugin(DStringHash i_Key);

private:
    RCEffectManager_Data*       me;
};


RC_NAMESPACE_END