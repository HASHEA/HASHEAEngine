#pragma once

#include "IKHeader.h"
#include <cstdint>
#include <KMemory/Public/KAutoRefPtr.h>
#include "KBase/Public/async_task/KAsyncTask.h"

enum KEmuLoadAbleType
{
    LOADABLE_RESOURCE,
    LOADABLE_TEXTURE,
    LOADABLE_TEXTURE_VK,
    LOADABLE_IKMODEL,
    LOADABLE_SCENE_CONTAINER,
    LOADABLE_MATIAL,
    LOADABLE_MATIAL_INSPACK_VK,
    LOADABLE_MATIAL_SUBSET_TECH_VK,
    LOADABLE_RENDER_TEXTURE,
    LOADABLE_VT_TILE,
    LOADABLE_VG_PAGE,
    LOADABLE_CLOUD_POINT,
    LOADABLE_PSS_DATA,
    LOADABLE_RENDER_SCENE_BLOCK,
    LOADABLE_MODEL_ASSET,
    LOADABLE_MODELINST_ASSET,
    LOADABLE_TEXTURESET_ASSET,
    LOADABLE_ANIMODEL_ASSET,
    LOADABLE_ANIMODELINST_ASSET,
    LOADABLE_PARTICLE_DATA,
    LOADABLE_RUST_ASSET,
    LOADABLE_ASSEMBLE_ASSET,
    LOADABLE_SKELETON_ASSET,
    LOADABLE_CLIP_ASSET,
};

enum class ELoadableState : int
{
    Loading = 0,
    Loaded,
    PostLoaded,
    Failed,
    Count,
};

class IKLoadAble;
struct IKLoadAbleCallback
{
    virtual BOOL DoLoadCallback(IKLoadAble* pLoadAble, BOOL bLoadSuccess) = 0;
};

class IKLoadAble : public NSKBase::IKRefObject
{
public:
    IKLoadAble();
    virtual ~IKLoadAble();

    virtual void OnRelease() override;

public:
    BOOL LoadSync();
    BOOL LoadAsync(
        NSKBase::EAsyncTaskPriority ePriority = NSKBase::EAsyncTaskPriority::Normal,
        void*                       pLauncher = nullptr
    );

public:
    virtual BOOL             Load()                          = 0;
    virtual BOOL             PostLoad()                      = 0;
    virtual KEmuLoadAbleType GetLoadAbleType()               = 0;
    virtual BOOL             IsPostLoadInLogicThread() const = 0;

    inline uint64_t GetLoadSortKey() const { return (uint64_t)m_uLoadable_nPriority << 32 | m_uLoadable_nID; }

    virtual ELoadableState GetLoadState() final { return m_Loadable_eLoadState; }
    virtual void           SetLoadState(ELoadableState eLoadState) final { m_Loadable_eLoadState = eLoadState; }
    virtual BOOL           IsLoaded() /*final */ { return m_Loadable_eLoadState == ELoadableState::Loaded || m_Loadable_eLoadState == ELoadableState::PostLoaded; }
    virtual BOOL           IsLoadFailed() final { return m_Loadable_eLoadState == ELoadableState::Failed; }
    virtual BOOL           IsLoading() final { return m_Loadable_eLoadState == ELoadableState::Loading; }

    virtual BOOL IsPostLoaded() const final { return m_Loadable_eLoadState == ELoadableState::PostLoaded; }
    virtual void SetPostLoaded() final
    {
        m_Loadable_eLoadState = ELoadableState::PostLoaded;
    }
    virtual void CancelLoad() final;
    virtual BOOL IsCancelledLoad() final { return m_Loadable_bCancelled; }

public:
    virtual BOOL IsInLoadState() final { return m_Loadable_bLoading; }
    virtual BOOL EnterLoadState() final;
    virtual BOOL LeaveLoadState() final;

public:
    inline void* GetLauncher() { return m_Loadable_pLauncher; }
    inline void  SetLauncher(void* pLauncher) { m_Loadable_pLauncher = pLauncher; }

private:
    mutable BOOL                        m_Loadable_bCancelled{false};
    mutable std::atomic<ELoadableState> m_Loadable_eLoadState;
    void*                               m_Loadable_pLauncher = nullptr;
    std::atomic<BOOL>                   m_Loadable_bLoading;

    KAutoRefPtr<NSKBase::KAsyncTask> m_Loadable_sLoadAsyncTask;

public:
    IKLoadAbleCallback* pLoadCallback                 = nullptr;
    uint32_t            m_uLoadable_nPriority         = 0;
    uint32_t            m_uLoadable_nID               = 0;
    bool                m_bLoadable_PostInLogicThread = true;
    bool                m_bLoadable_OnReleaseFlag     = false;
};

class IKSceneObject : public IKLoadAble
{
public:
    IKSceneObject();
    virtual ~IKSceneObject();

    virtual const NSKMath::KVec3& GetBoundingBoxMax()                      = 0;
    virtual const NSKMath::KVec3& GetBoundingBoxMin()                      = 0;
    virtual void                  GetLoadAbleName(char* pName, size_t len) = 0;
};


struct IKShaderResource
{
    virtual ~IKShaderResource(){}
};

struct IKPipeLineData
{
    virtual ~IKPipeLineData(){}
};

struct IKGraphicsPipelineDesc
{
    virtual ~IKGraphicsPipelineDesc(){}
};

struct IKEngineThreadCall
{
    virtual ~IKEngineThreadCall(){}
    virtual void EngineThreadCall() = 0;
};

#define ENABLE_RESOURCE_MULTITHREAD_LOAD true
