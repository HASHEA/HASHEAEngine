/*
    filename:       rcmanager.cpp
    author:         Ming Dong
    date:           2016-MAR-25
    description:    
*/

#include "../public/rcmanager.h"
#include "mdexecuter.h"

RC_NAMESPACE_BEGIN

class RCManager::RCManager_Impl
{
public:
    RCManager_Impl()
    {
        for (Int i = 0; i < RC_MAXEFFECTMANAGER; ++i)
        {
            m_EffectManagerArray[i] = DM_NULL;
        }
    }

    ~RCManager_Impl()
    {
        for (Int i = 0; i < RC_MAXEFFECTMANAGER; ++i)
        {
            DOME_ASSERT(m_EffectManagerArray[i] == DM_NULL);
        }
        reset();
    }

    DResult             reset()
    {
        for (Int i = 0; i < RC_MAXEFFECTMANAGER; ++i)
        {
            if (m_EffectManagerArray[i] != DM_NULL)
            {
                DOME_Del(m_EffectManagerArray[i]);
                m_EffectManagerArray[i] = DM_NULL;
            }
        }
        return R_SUCCESS;
    }

    DResult             createEffectManager(Int i_Index, RCRenderer* i_pRenderer)
    {
        DOME_WARNING2(i_Index >= 0 && i_Index < RC_MAXEFFECTMANAGER, "ERROR: Can't create the effect manager.");
        if(i_Index < 0 || i_Index >= RC_MAXEFFECTMANAGER)
            return R_OUTOFRANGE;

        if(m_EffectManagerArray[i_Index])
            return R_ALREADYREGISTERED;

        m_EffectManagerArray[i_Index] = DOME_New(RCEffectManager)(i_pRenderer);
        return R_SUCCESS;
    }

    Int                 createEffectManager(RCRenderer* i_pRenderer)
    {
        for (Int i = 0; i < RC_MAXEFFECTMANAGER; ++i)
        {
            if (!m_EffectManagerArray[i])
            {
                m_EffectManagerArray[i] = DOME_New(RCEffectManager)(i_pRenderer);
                return i;
            }
        }
        return -1;
    }

    DResult             destroyEffectManager(Int i_Index)
    {
        //DOME_WARNING2(i_Index >= 0 && i_Index < RC_MAXEFFECTMANAGER, "ERROR: Can't create the effect manager.");
        if(i_Index < 0 || i_Index >= RC_MAXEFFECTMANAGER)
            return R_OUTOFRANGE;

        if(!m_EffectManagerArray[i_Index])
            return R_NOTREGISTERED;

        DOME_Del(m_EffectManagerArray[i_Index]);
        m_EffectManagerArray[i_Index] = DM_NULL;
        return R_SUCCESS;
    }

    RCEffectManager*    getEffectManager(Int i_Index)
    {
        //DOME_WARNING2(i_Index >= 0 && i_Index < RC_MAXEFFECTMANAGER, "ERROR: Can't create the effect manager.");
        if(i_Index < 0 || i_Index >= RC_MAXEFFECTMANAGER)
            return DM_NULL;

        return m_EffectManagerArray[i_Index];
    }

    DResult                 registerRCEffectNode(const DString& i_NodeType, _CreateEffectNodeFuncPtr i_CreateFunc, _DestroyEffectNodeFuncPtr i_DestroyFunc)
    {
        DResult l_Result = m_RCEffectNodeDB.add(i_NodeType);
        DM_FAIL_ASSERT_RET(l_Result);
        if(DM_FAIL(l_Result))
            return l_Result;

        _RCEffectNodeFactory l_NodeFactory;
        l_NodeFactory.m_CreateFunc = i_CreateFunc;
        l_NodeFactory.m_DestroyFunc = i_DestroyFunc;

        return m_RCEffectNodeDB.set(DStringHash(i_NodeType.c_str()), l_NodeFactory);
    }

    RCEffectNode*           createRCEffectNode(DStringHash i_NodeTypeHash, RCEffect* i_pEffect) const
    {
        const _RCEffectNodeFactory* l_pNodeFactory = m_RCEffectNodeDB.get(i_NodeTypeHash);
        if (l_pNodeFactory)
        {
            return l_pNodeFactory->m_CreateFunc(i_pEffect);
        }
        else
            return DM_NULL;
    }

    DResult                 destroyRCEffectNode(RCEffectNode* i_pEffectNode) const
    {
        const DHashString& l_NodeType = i_pEffectNode->getType();
        const _RCEffectNodeFactory* l_pNodeFactory = m_RCEffectNodeDB.get(l_NodeType.getHash());
        if (l_pNodeFactory)
        {
            return l_pNodeFactory->m_DestroyFunc(i_pEffectNode);
        }
        else
            return R_NOTREGISTERED;
    }

    DResult                 registerMDOperator(const DString& i_OperatorName, const MDOperator* i_pOperator)
    {
        DResult l_Result = m_MDOperatorDB.add(i_OperatorName);
        DM_FAIL_ASSERT_RET(l_Result);

        return m_MDOperatorDB.set(DStringHash(i_OperatorName.c_str()), i_pOperator);
    }

    const MDOperator*       getMDOperator(DStringHash i_Key) const
    {
        typedef const MDOperator*   _MDOperatorCPtr;
        const _MDOperatorCPtr* l_pValue = m_MDOperatorDB.get(i_Key);
        if(l_pValue)
            return *l_pValue;
        else
            return DM_NULL;
    }

    DResult                 registerPlugin(const DString& i_PluginName, void* i_pPlugin)
    {
        DOME_ASSERT(i_pPlugin);

        DResult l_Result = m_PluginDB.add(i_PluginName);
        DM_FAIL_ASSERT_RET(l_Result);

        return m_PluginDB.set(DStringHash(i_PluginName.c_str()), i_pPlugin);
    }

    void*                   getPlugin(DStringHash i_Key)
    {
        typedef void* _Plugin;
        _Plugin* l_pValue = m_PluginDB.get(i_Key);
        if(l_pValue)
            return *l_pValue;
        else
            return DM_NULL;
    }

private:
    RCEffectManager*    m_EffectManagerArray[RC_MAXEFFECTMANAGER];

    struct _RCEffectNodeFactory
    {
        _CreateEffectNodeFuncPtr        m_CreateFunc;
        _DestroyEffectNodeFuncPtr       m_DestroyFunc;
    };
    TDataBase<DString, _RCEffectNodeFactory>        m_RCEffectNodeDB;

    TDataBase<DString, const MDOperator*>           m_MDOperatorDB;

    TDataBase<DString, void*>          m_PluginDB;
};

RCManager::RCManager()
{
    me = DOME_New(RCManager_Impl);
}

RCManager::~RCManager()
{
    DOME_Del(me);
}

DResult                 RCManager::reset()
{
    return me->reset();
}

DResult                 RCManager::createEffectManager(Int i_Index, RCRenderer* i_pRenderer)
{
    return me->createEffectManager(i_Index, i_pRenderer);
}

Int                     RCManager::createEffectManager(RCRenderer* i_pRenderer)
{
    return me->createEffectManager(i_pRenderer);
}

DResult                 RCManager::destroyEffectManager(Int i_Index)
{
    return me->destroyEffectManager(i_Index);
}

RCEffectManager*        RCManager::getEffectManager(Int i_Index)
{
    return me->getEffectManager(i_Index);
}

DResult                 RCManager::registerRCEffectNode(const DString& i_NodeType, _CreateEffectNodeFuncPtr i_CreateFunc, _DestroyEffectNodeFuncPtr i_DestroyFunc)
{
    return me->registerRCEffectNode(i_NodeType, i_CreateFunc, i_DestroyFunc);
}

RCEffectNode*           RCManager::createRCEffectNode(DStringHash i_NodeTypeHash, RCEffect* i_pEffect) const
{
    return me->createRCEffectNode(i_NodeTypeHash, i_pEffect);
}

DResult                 RCManager::destroyRCEffectNode(RCEffectNode* i_pEffectNode) const
{
    return me->destroyRCEffectNode(i_pEffectNode);
}

DResult                 RCManager::registerMDOperator(const DString& i_OperatorName, const MDOperator* i_pOperator)
{
    return me->registerMDOperator(i_OperatorName, i_pOperator);
}

const MDOperator*       RCManager::getMDOperator(DStringHash i_Key) const
{
    return me->getMDOperator(i_Key);
}

DResult                 RCManager::registerPlugin(const DString& i_PluginName, void* i_pPlugin)
{
    return me->registerPlugin(i_PluginName, i_pPlugin);
}

void*                   RCManager::getPlugin(DStringHash i_Key)
{
    return me->getPlugin(i_Key);
}

DResult                 RCManager::getGpuShaderFromSignature(const DString& i_Signature, DString& o_Shader)
{
    DResult l_Result;
    Int l_NumTex, l_NumTex3D, l_NumTexCube, l_NumMat4x4, l_NumMat3x3, l_NumMat2x2, l_NumFloat4, l_NumFloat3, l_NumFloat2, l_NumFloat;
    l_Result = MDExecuter::compile(i_Signature, o_Shader, l_NumTex, l_NumTex3D, l_NumTexCube, l_NumMat4x4, l_NumMat3x3, l_NumMat2x2, l_NumFloat4, l_NumFloat3, l_NumFloat2, l_NumFloat);
    DOME_ASSERT(DM_SUCC(l_Result));
    return l_Result;
}

RC_NAMESPACE_END