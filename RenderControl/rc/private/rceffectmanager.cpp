/*
    filename:       rceffectmanager.cpp
    author:         Ming Dong
    date:           2016-MAR-24
    description:    
*/

#include "../public/rceffectmanager.h"
#include "mdexecuter.h"
#include "../../../../../../../../../DevEnv/Include/PerfAnalyzer.h"


RC_NAMESPACE_BEGIN

struct RCEffectManager_Data
{
    DHashString                                         k_HashString_NotFound;
    typedef TMap<DHashString, RCEffect*>                _EffectMap;
    typedef TMap<DHashString, MDExecuter*>              _ExecuterMap;

    RCRenderer*                                         m_pRenderer;

    RCParameterSys                                      m_ParamSys;

    _EffectMap                                          m_EffectMap;
    RCEffect*                                           m_pCurrEffect;

    _ExecuterMap                                        m_ExecuterMap;

    TDataBase<DString, void*>              m_PluginDB;

    Bool                                                m_bExtreamOptimizationMode;

    Bool                    m_bSafeMode;
};


RCEffectManager::RCEffectManager(RCRenderer* i_pRenderer)
{
    me = DOME_New(RCEffectManager_Data);

    me->m_pRenderer = i_pRenderer;
    me->m_pCurrEffect = DM_NULL;
    me->k_HashString_NotFound = "Not Found";
    me->m_bExtreamOptimizationMode = DM_FALSE;
    me->m_bSafeMode = DM_FALSE;

//    me->loadEffect(this, DHashString("default"));
//    me->setCurrentEffect(DHashString("default"));
}

RCEffectManager::~RCEffectManager()
{
    reset();

    DOME_Del(me);
}

DResult                     RCEffectManager::setExtreamOptimizationMode(Bool i_bEnable)
{
    me->m_bExtreamOptimizationMode = i_bEnable;
    return R_SUCCESS;
}

Bool                        RCEffectManager::isExtreamOptimizationMode() const
{
    return me->m_bExtreamOptimizationMode;
}

void                        RCEffectManager::setSafeModifyMode(Bool i_bSafe)
{
    me->m_bSafeMode = i_bSafe;
}

Bool                        RCEffectManager::isSafeModifyMode() const
{
    return me->m_bSafeMode;
}

DResult                     RCEffectManager::reset()
{
        clearAllEffects();
        me->m_ParamSys.clearAllParameters();
        clearAllMDExecuter();
        return R_SUCCESS;
}

RCRenderer*                 RCEffectManager::getRenderer() const
{
    return me->m_pRenderer;
}

DResult                     RCEffectManager::clearAllMDExecuter()
{
    for (RCEffectManager_Data::_ExecuterMap::iterator it = me->m_ExecuterMap.begin(); it != me->m_ExecuterMap.end(); ++it)
    {
        DOME_Del(it->second);
        it->second = DM_NULL;
    }
    me->m_ExecuterMap.clear();
    return R_SUCCESS;
}

MDExecuter*                 RCEffectManager::getMDExecuter(const DHashString& i_Signature)
{
    RCEffectManager_Data::_ExecuterMap::iterator it = me->m_ExecuterMap.find(i_Signature);
    if (it != me->m_ExecuterMap.end())
    {
        return it->second;
    }
    else
    {
        MDExecuter* l_pExecuter = DOME_New(MDExecuter)(this, i_Signature);
        me->m_ExecuterMap[i_Signature] = l_pExecuter;
        return l_pExecuter;
    }
}

DResult                     RCEffectManager::clearAllEffects()
{
    for (RCEffectManager_Data::_EffectMap::iterator it = me->m_EffectMap.begin(); it != me->m_EffectMap.end(); ++it)
    {
        DOME_Del(it->second);
        it->second = DM_NULL;
    }
    me->m_EffectMap.clear();
    return R_SUCCESS;
}

void                        RCEffectManager::resetAllEffects()
{
    for (RCEffectManager_Data::_EffectMap::const_iterator it = me->m_EffectMap.begin(); it != me->m_EffectMap.end(); ++it)
    {
        if (it->second)
        {
            it->second->resetEffect();
        }
    }
}

DResult                     RCEffectManager::createEffect(const DHashString& i_EffectName, const DString& i_EffectContent, const DString& i_EditorData)
{
    Bool l_bFound = DM_FALSE;
    RCEffectManager_Data::_EffectMap::iterator it = me->m_EffectMap.get(i_EffectName, &l_bFound);
    if (!l_bFound)
    {
        it->second = DOME_New(RCEffect)(i_EffectName, i_EffectContent, i_EditorData, this);
        return R_SUCCESS;
    }
    else
        return R_ALREADYREGISTERED;
}

DResult                     RCEffectManager::loadEffect(const DHashString& i_EffectName)
{
    Bool l_bFound = DM_FALSE;
    RCEffectManager_Data::_EffectMap::iterator it = me->m_EffectMap.get(i_EffectName, &l_bFound);
    if (!l_bFound)
    {
        it->second = DOME_New(RCEffect)(i_EffectName, this);
        return R_SUCCESS;
    }
    else
        return R_ALREADYREGISTERED;
}

DResult                     RCEffectManager::removeEffect(const DHashString& i_EffectName)
{
        if(me->m_pCurrEffect && i_EffectName == me->m_pCurrEffect->getName())
            me->m_pCurrEffect = DM_NULL;

        RCEffectManager_Data::_EffectMap::iterator it = me->m_EffectMap.find(i_EffectName);
        if (it != me->m_EffectMap.end())
        {
            me->m_EffectMap.erase(it);
        }
        return R_SUCCESS;
}

DResult                     RCEffectManager::saveEffect(const DHashString& i_EffectName) const
{
    RCEffectManager_Data::_EffectMap::const_iterator it = me->m_EffectMap.find(i_EffectName);
    if (it != me->m_EffectMap.end())
    {
        return it->second->save();
    }
    else
        return R_NOTFOUND;
}

DResult                     RCEffectManager::updateEffect(const DHashString& i_EffectName, const DString& i_EffectContent, const DString& i_EditorData)
{
    RCEffectManager_Data::_EffectMap::iterator it = me->m_EffectMap.find(i_EffectName);
    if (it != me->m_EffectMap.end())
    {
        return it->second->set(i_EffectContent, i_EditorData);
    }
    else
        return R_NOTFOUND;
}

Int                         RCEffectManager::getEffectCount() const
{
    return me->m_EffectMap.size();
}

const DHashString&          RCEffectManager::getEffectNameByIndex(Int i_Index) const
{
    DOME_ASSERT(i_Index >= 0 && i_Index < me->m_EffectMap.size());

    Int i = 0;
    for (RCEffectManager_Data::_EffectMap::const_iterator it = me->m_EffectMap.begin(); it != me->m_EffectMap.end(); ++it, ++i)
    {
        if (i == i_Index)
        {
            return it->first;
        }
    }
    return me->k_HashString_NotFound;
}

const DHashString&          RCEffectManager::getCurrentEffectName() const
{
    if(me->m_pCurrEffect)
        return me->m_pCurrEffect->getName();
    else
        return me->k_HashString_NotFound;
}

DResult                     RCEffectManager::setCurrentEffect(const DHashString& i_EffectName)
{
    RCEffectManager_Data::_EffectMap::iterator it = me->m_EffectMap.find(i_EffectName);
    if (it != me->m_EffectMap.end())
    {
        me->m_pCurrEffect = it->second;
        return R_SUCCESS;
    }
    else
        return R_NOTFOUND;
}

RCEffect*                   RCEffectManager::getCurrentEffect()
{
    return me->m_pCurrEffect;
}

DResult                     RCEffectManager::saveCurrentEffect() const
{
    if(me->m_pCurrEffect)
        return me->m_pCurrEffect->save();
    else
        return R_NOTFOUND;
}

DResult                     RCEffectManager::updateCurrentEffect(const DString& i_EffectContent, const DString& i_EditorData)
{
    if(me->m_pCurrEffect)
        return me->m_pCurrEffect->set(i_EffectContent, i_EditorData);
    else
        return R_NOTFOUND;
}

DResult                     RCEffectManager::build()
{
    if(me->m_pCurrEffect)
        me->m_pCurrEffect->build();

	return R_SUCCESS;
}

DResult                     RCEffectManager::execute()
{
    PERF_COUNTER_EX(0);
    if(me->m_pCurrEffect)
        return me->m_pCurrEffect->execute();
    else
        return R_NOTFOUND;
}

// parameter management functions
RCParameterSys&             RCEffectManager::getParamSys()
{
    return me->m_ParamSys;
}

const RCParameterSys&       RCEffectManager::getParamSys() const
{
    return me->m_ParamSys;
}

// functions for plugins
DResult                     RCEffectManager::registerPlugin(const DString& i_PluginName, void* i_pPlugin)
{
    DOME_ASSERT(i_pPlugin);

    DResult l_Result = me->m_PluginDB.add(i_PluginName);
    DM_FAIL_ASSERT_RET(l_Result);

    return me->m_PluginDB.set(DStringHash(i_PluginName.c_str()), i_pPlugin);
}

void*                       RCEffectManager::getPlugin(DStringHash i_Key)
{
    typedef void* _Plugin;
    _Plugin* l_pValue = me->m_PluginDB.get(i_Key);
    if(l_pValue)
        return *l_pValue;
    else
        return DM_NULL;
}

RC_NAMESPACE_END