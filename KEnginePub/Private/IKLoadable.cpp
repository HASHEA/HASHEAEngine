#include "KEnginePub/Public/IKLoadable.h"
#include "KEnginePub/Public/IKEnginePerformance.h"
#include "KBase/Public/thread/KThread.h"
#include "KBase/Public/async_task/KAsyncTaskManager.h"
#include "Engine/KGLog.h"
//////////////////////////////////////////////////////////////////////////
#include "KEnginePub/Public/KProfileTools.h"
#include "KBase/Public/KMemLeak.h"

IKLoadAble::IKLoadAble()
{
    static std::atomic<uint32_t> s_uLoadID{0};
    m_uLoadable_nID = ++s_uLoadID;

    m_Loadable_eLoadState = ELoadableState::Count;
    pLoadCallback         = nullptr;
    m_Loadable_bLoading   = false;

    KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
    ++pPerfMonitor->nLoadableCount;
}

IKLoadAble::~IKLoadAble()
{
    ASSERT(m_bLoadable_OnReleaseFlag);
    if (m_Loadable_sLoadAsyncTask)
    {
        m_Loadable_sLoadAsyncTask->Cancel();
        m_Loadable_sLoadAsyncTask.Reset();
    }

    KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
    --pPerfMonitor->nLoadableCount;
}

void IKLoadAble::OnRelease()
{
    m_bLoadable_OnReleaseFlag = true;
    if (m_Loadable_sLoadAsyncTask)
    {
        m_Loadable_sLoadAsyncTask->Cancel();
        m_Loadable_sLoadAsyncTask.Reset();
    }
}

BOOL IKLoadAble::LoadSync()
{
    PROF_CPU();

    BOOL bResult  = FALSE;
    BOOL bRetCode = FALSE;

    BOOL bAddedToLoader         = FALSE;
    bool bPostLoadInLogicThread = false;

    KG_PROCESS_ERROR(m_Loadable_eLoadState == ELoadableState::Count);
    KG_PROCESS_ERROR(EnterLoadState());

    bAddedToLoader       = TRUE;
    m_Loadable_pLauncher = nullptr;

    if (!IsLoaded())
    {
        Load();
    }

    bPostLoadInLogicThread = IsPostLoadInLogicThread();
    if ((bPostLoadInLogicThread && IsLogicThread()) || (!bPostLoadInLogicThread && IsMainThread()))
    {
        if (!IsPostLoaded() && IsLoaded() && IsMainThread())
        {
            PostLoad();
        }

        if (pLoadCallback)
        {
            pLoadCallback->DoLoadCallback(this, IsLoaded());
        }

        bRetCode = LeaveLoadState();
        ASSERT(bRetCode);
    }
    else
    {
        NSKBase::KAsyncTaskManager* pAsyncTaskMgr = NSKBase::g_GetAsyncManager();
        KGLOG_ASSERT_EXIT(pAsyncTaskMgr);

        m_Loadable_sLoadAsyncTask = pAsyncTaskMgr->AddTask(
            NSKBase::EAsyncTaskPriority::High,
            bPostLoadInLogicThread,
            nullptr,
            [this]() {
                ASSERT(IsInLoadState());
                bool bRetCode = false;
                if (IsLoaded() && !IsPostLoaded())
                {
                    bRetCode = PostLoad();
                    SetLoadState(bRetCode ? ELoadableState::PostLoaded : ELoadableState::Failed);

                    // 只有PostLoad成功才会回调
                    if (bRetCode && pLoadCallback)
                    {
                        pLoadCallback->DoLoadCallback(this, IsPostLoaded());
                    }
                }
                bRetCode = LeaveLoadState();
                ASSERT(bRetCode);

                m_Loadable_sLoadAsyncTask.Reset();
            }
        );
    }

    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL IKLoadAble::LoadAsync(NSKBase::EAsyncTaskPriority ePriority /*= NSKBase::EAsyncTaskPriority::Normal*/, void* pLauncher /*= nullptr */)
{
    PROF_CPU();

    BOOL bResult  = FALSE;
    BOOL bRetCode = FALSE;

    BOOL                        bAddedToLoader         = FALSE;
    bool                        bPostLoadInLogicThread = false;
    NSKBase::KAsyncTaskManager* pAsyncTaskMgr          = nullptr;

    KG_PROCESS_ERROR(m_Loadable_eLoadState == ELoadableState::Count);
    KG_PROCESS_ERROR(EnterLoadState());

    bAddedToLoader       = TRUE;
    m_Loadable_pLauncher = nullptr;

    bPostLoadInLogicThread = IsPostLoadInLogicThread();
    pAsyncTaskMgr          = NSKBase::g_GetAsyncManager();
    KGLOG_ASSERT_EXIT(pAsyncTaskMgr);

    m_Loadable_sLoadAsyncTask = pAsyncTaskMgr->AddTask(
        NSKBase::EAsyncTaskPriority::High,
        bPostLoadInLogicThread,
        [this]() {
            ASSERT(IsInLoadState());
            if (!IsLoaded())
            {
                BOOL bRetCode = Load();
                SetLoadState(bRetCode ? ELoadableState::Loaded : ELoadableState::Failed);
            }
        },
        [this]() {
            ASSERT(IsInLoadState());
            bool bRetCode = false;
            if (IsLoaded() && !IsPostLoaded())
            {
                bRetCode = PostLoad();
                SetLoadState(bRetCode ? ELoadableState::PostLoaded : ELoadableState::Failed);

                // 只有PostLoad成功才会回调
                if (bRetCode && pLoadCallback)
                {
                    pLoadCallback->DoLoadCallback(this, IsPostLoaded());
                }
            }
            bRetCode = LeaveLoadState();
            ASSERT(bRetCode);

            m_Loadable_sLoadAsyncTask.Reset();
        }
    );

    bResult = TRUE;
Exit0:
    return bResult;
}

void IKLoadAble::CancelLoad()
{
    if (m_Loadable_sLoadAsyncTask)
    {
        m_Loadable_sLoadAsyncTask->Cancel();
        m_Loadable_bCancelled = true;
    }
}

BOOL IKLoadAble::EnterLoadState()
{
    BOOL bExpect = FALSE;
    return m_Loadable_bLoading.compare_exchange_strong(bExpect, TRUE);
}

BOOL IKLoadAble::LeaveLoadState()
{
    BOOL bExpect = TRUE;
    return m_Loadable_bLoading.compare_exchange_strong(bExpect, FALSE);
}

IKSceneObject::IKSceneObject()
{
    KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
    ++pPerfMonitor->nSceneObjectCount;
}

IKSceneObject::~IKSceneObject()
{
    KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
    --pPerfMonitor->nSceneObjectCount;
}
