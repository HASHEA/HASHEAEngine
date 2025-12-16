#pragma once

#include "KBase/Public/KG3D_Base/KG3D_Vector.h"
#include "KEnginePub/Public/IGFX_Public.h"
#include "KEnginePub/Public/KProfileTools.h"

// 延迟释放队列, 根据GPU执行进度释放资源
template<typename T>
struct TDelayReleaseQueue
{
    struct FRAME_RELEASE_LIST
    {
        gfx::KSignalFence* Fence = nullptr;
        int nDelayFrameCounter = 0;
        KG3D_Vector<T*, 1> ReleaseObjList;
    };

    FRAME_RELEASE_LIST* m_pCurrentFrameReleaseList = nullptr;
    std::list<FRAME_RELEASE_LIST*> m_WaitReleaseList;
    std::list<FRAME_RELEASE_LIST*> m_ProcessReleaseList;
    std::list<FRAME_RELEASE_LIST*> m_FreeReleaseList;

    std::recursive_mutex m_lock;
    bool m_bUniniting = false;

    static const int EXTERNAL_DELAY_RELEASE_FRAME_COUNT = 1;

    TDelayReleaseQueue()
    {
        m_pCurrentFrameReleaseList = NewFrameReleaseList();
        CHECK_ASSERT(m_pCurrentFrameReleaseList);
    }

    FRAME_RELEASE_LIST* NewFrameReleaseList()
    {
        FRAME_RELEASE_LIST* pRet = nullptr;
        if (!m_FreeReleaseList.empty())
        {
            pRet = m_FreeReleaseList.front();
            m_FreeReleaseList.pop_front();
        }
        else
        {
            pRet = new FRAME_RELEASE_LIST();
            CHECK_ASSERT(pRet);
        }
        return pRet;
    }

    void RecycleFrameReleaseList(FRAME_RELEASE_LIST* pList)
    {
        CHECK_ASSERT(pList);

        pList->ReleaseObjList.clear();

        if (pList->Fence)
            pList->Fence->Clear();

        m_FreeReleaseList.push_back(pList);
    }

    void FrameMoveRelease(const char* pOpticName)
    {
        PROF_CPU(pOpticName);
        m_lock.lock();

        if (m_pCurrentFrameReleaseList)
        {
            // 提交当前帧的释放列表
            if (m_pCurrentFrameReleaseList->ReleaseObjList.empty())
            {
                RecycleFrameReleaseList(m_pCurrentFrameReleaseList);
            }
            else
            {
                if (m_pCurrentFrameReleaseList->Fence == nullptr)
                {
                    auto pDevice = gfx::KGFX_GetGraphicDevice();
                    CHECK_ASSERT(pDevice);

                    BOOL bRetCode = pDevice->CreateSignalFence(&m_pCurrentFrameReleaseList->Fence);
                    CHECK_ASSERT(bRetCode);
                }

                auto pRenderCtx = gfx::GetRenderContext();
                CHECK_ASSERT(pRenderCtx);

                pRenderCtx->CmdInsertSignalFence(m_pCurrentFrameReleaseList->Fence);
                m_WaitReleaseList.push_back(m_pCurrentFrameReleaseList);
            }

            m_pCurrentFrameReleaseList = nullptr;
        }

        {
            // 检测等待释放列表
            auto iter = m_WaitReleaseList.begin();
            if (iter != m_WaitReleaseList.end())
            {
                auto* pList = *iter;
                CHECK_ASSERT(pList->Fence);

                if (pList->Fence->Query())
                {
                    pList->nDelayFrameCounter = EXTERNAL_DELAY_RELEASE_FRAME_COUNT;

                    m_ProcessReleaseList.push_back(pList);
                    m_WaitReleaseList.erase(iter);
                }
            }
        }

        CHECK_ASSERT(m_pCurrentFrameReleaseList == nullptr);

        // 创建新的当前帧释放列表
        m_pCurrentFrameReleaseList = NewFrameReleaseList();
        CHECK_ASSERT(m_pCurrentFrameReleaseList);

        m_lock.unlock();

        // 处理释放
        for (auto it = m_ProcessReleaseList.begin(), e = m_ProcessReleaseList.end(); it != e; )
        {
            auto* pList = *it;
            if (pList->nDelayFrameCounter == 0)
            {
                for (auto& obj : pList->ReleaseObjList)
                {
                    SAFE_DELETE(obj);
                }
                it = m_ProcessReleaseList.erase(it);
                RecycleFrameReleaseList(pList);
            }
            else
            {
                pList->nDelayFrameCounter--;
                ++it;
            }
        }
    }

    BOOL DestroyResource(const char* pOpticName, T*& pRes, std::function<void()> pfunSyncReleaseCall = nullptr)
    {
        PROF_CPU(pOpticName);
        BOOL bRet = false;

        KG_PROCESS_ERROR(pRes);

        if (!m_bUniniting)
        {
            m_lock.lock();

            CHECK_ASSERT(m_pCurrentFrameReleaseList);
            m_pCurrentFrameReleaseList->ReleaseObjList.push_back(pRes);

            m_lock.unlock();

            if (pfunSyncReleaseCall)
                pfunSyncReleaseCall();
        }
        else
        {
            SAFE_DELETE(pRes);
        }

        pRes = nullptr;

        bRet = true;
    Exit0:
        return bRet;
    }

    void ReleaseAll(const char* pOpticName)
    {
        PROF_CPU(pOpticName);
        m_lock.lock();

        // 释放处理中列表
        for (auto& it : m_ProcessReleaseList)
        {
            for (auto& obj : it->ReleaseObjList)
            {
                SAFE_DELETE(obj);
            }
            RecycleFrameReleaseList(it);
        }
        m_ProcessReleaseList.clear();

        gfx::KGFX_GetGraphicDevice()->DeviceWaitIdle();

        // 释放等待中列表
        for (auto& it : m_WaitReleaseList)
        {
            for (auto& obj : it->ReleaseObjList)
            {
                SAFE_DELETE(obj);
            }
            RecycleFrameReleaseList(it);
        }
        m_WaitReleaseList.clear();

        // 释放当前帧列表
        if (m_pCurrentFrameReleaseList)
        {
            for (auto& it : m_pCurrentFrameReleaseList->ReleaseObjList)
            {
                SAFE_DELETE(it);
            }
            RecycleFrameReleaseList(m_pCurrentFrameReleaseList);
        }

        // 创建新的当前帧释放列表
        m_pCurrentFrameReleaseList = NewFrameReleaseList();
        CHECK_ASSERT(m_pCurrentFrameReleaseList);

        m_lock.unlock();
    }

    void Uninit(const char* pOpticName)
    {
        PROF_CPU(pOpticName);
        m_lock.lock();

        m_bUniniting = true;

        // 释放所有资源
        ReleaseAll(pOpticName);
        m_bUniniting = false;

        // 释放当前帧列表
        RecycleFrameReleaseList(m_pCurrentFrameReleaseList);
        m_pCurrentFrameReleaseList = nullptr;

        // 释放空闲列表
        for (auto& it : m_FreeReleaseList)
        {
            SAFE_RELEASE(it->Fence);
            SAFE_DELETE(it);
        }

        m_lock.unlock();
    }
};

// 延迟释放队列, 根据固定帧数释放资源
template<typename T>
struct TDelayReleaseQueue_FixedDelayCount
{
    std::list<T*> m_lsResource;
    std::recursive_mutex m_lock;
    bool m_bUniniting = false;

    void FrameMoveRelease(const char* pOpticName)
    {
        PROF_CPU(pOpticName);
        m_lock.lock();
        for (auto it = m_lsResource.begin(), e = m_lsResource.end(); it != e;)
        {
            auto* pRes = *it;
            if (pRes->m_delayReleaseCounter == 0)
            {
                it = m_lsResource.erase(it);
                SAFE_DELETE(pRes);
            }
            else
            {
                pRes->m_delayReleaseCounter--;
                ++it;
            }
        }
        m_lock.unlock();
    }

    BOOL DestroyResource(const char* pOpticName, T*& pRes, std::function<void()> pfunSyncReleaseCall = nullptr)
    {
        PROF_CPU(pOpticName);
        BOOL bRet = false;
        KG_PROCESS_ERROR(pRes);

        if (!m_bUniniting)
        {
            pRes->m_delayReleaseCounter = DELAY_RELEASE_FRAME_COUNT;

            m_lock.lock();
            m_lsResource.push_back(pRes);
            m_lock.unlock();

            if (pfunSyncReleaseCall)
                pfunSyncReleaseCall();
        }
        else
        {
            SAFE_DELETE(pRes);
        }

        pRes = nullptr;

        bRet = true;
    Exit0:
        return bRet;
    }

    void ReleaseAll(const char* pOpticName)
    {
        PROF_CPU(pOpticName);
        m_lock.lock();
        for (auto it = m_lsResource.begin(), e = m_lsResource.end(); it != e;)
        {
            auto* pRes = *it;
            it = m_lsResource.erase(it);
            SAFE_DELETE(pRes);
        }
        m_lsResource.clear();
        m_lock.unlock();
    }

    void Uninit(const char* pOpticName)
    {
        PROF_CPU(pOpticName);
        m_lock.lock();
        m_bUniniting = true;
        for (auto& it : m_lsResource)
        {
            SAFE_DELETE(it);
        }
        m_lsResource.clear();
        m_bUniniting = false;
        m_lock.unlock();
    }
};
