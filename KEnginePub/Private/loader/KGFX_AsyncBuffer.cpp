#include "KGFX_AsyncBuffer.h"
#include "KBase/Public/thread/KThread.h"
#include "KBase/Public/async_task/KAsyncTaskManager.h"

#include "KBase/Public/KMemLeak.h"

namespace gfx
{
    KGFX_AsyncBuffer::KGFX_AsyncBuffer()
    {
    }

    KGFX_AsyncBuffer::~KGFX_AsyncBuffer()
    {
        ASSERT(m_nRef == 0);
        if (m_bufferCreateAsyncTask)
        {
            m_bufferCreateAsyncTask->Cancel();
            m_bufferCreateAsyncTask.Reset();
        }

        SAFE_FREE(m_pAsyncInitData);

        SAFE_RELEASE(m_pBuffer);
    }

    BOOL KGFX_AsyncBuffer::Create(const KGfxBufferDesc& bufDesc, const void* pData, NSKBase::KAsyncTask::TaskFinish&& fnTaskComplete)
    {
        ASSERT(m_pBuffer == nullptr);

        BOOL bRet = FALSE;

        m_stAsyncBufferDesc = bufDesc;

        if (m_bufferCreateAsyncTask)
        {
            m_bufferCreateAsyncTask->Cancel();
            m_bufferCreateAsyncTask.Reset();
        }

        SAFE_FREE(m_pAsyncInitData);

        // 判断下类型（UBO 强制同步）
        if (bufDesc.uUsageFlags == gfx::BUFFER_USAGE_UNIFORM_BUFFER_BIT && DrvOption::bSupportDynamicUBO && !bufDesc.bForceStatic)
        {
            bRet = gfx::KGFX_GetGraphicDevice()->CreateBuffer(&m_pBuffer, m_stAsyncBufferDesc, pData);
            {
                std::lock_guard<std::mutex> lock(m_mtxBufferState);
                m_eBufferState = bRet ? KGfxAsyncBufferState::Created : KGfxAsyncBufferState::CreatedFailed;
            }

            if (fnTaskComplete)
            {
                fnTaskComplete();
            }
        }
        else
        {
            if (pData)
            {
                m_pAsyncInitData = malloc(m_stAsyncBufferDesc.uByteWidth);
                // safe copy
                memcpy(m_pAsyncInitData, pData, m_stAsyncBufferDesc.uByteWidth);
            }

            NSKBase::KAsyncTask::TaskExecute fnTaskExecute = [this]() {
                BOOL bRetCode = gfx::KGFX_GetGraphicDevice()->CreateBuffer(&m_pBuffer, m_stAsyncBufferDesc, nullptr);
                if (!bRetCode)
                {
                    std::lock_guard<std::mutex> lock(m_mtxBufferState);
                    m_eBufferState = KGfxAsyncBufferState::CreatedFailed;
                }
            };

            NSKBase::KAsyncTask::TaskExecute fnTaskFinishComplete = [this, fnTaskComplete] {
                if (m_eBufferState != KGfxAsyncBufferState::CreatedFailed)
                {
                    if (m_pAsyncInitData)
                    {
                        CHECK_ASSERT(IsMainThread());

                        auto pRenderCtx = gfx::GetRenderContext();
                        CHECK_ASSERT(pRenderCtx);

                        pRenderCtx->CmdUpdateSubResource(m_pBuffer, 0, m_stAsyncBufferDesc.uByteWidth, m_pAsyncInitData, 0);
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(m_mtxBufferState);
                    m_eBufferState = KGfxAsyncBufferState::Created;
                }

                SAFE_FREE(m_pAsyncInitData);

                if (fnTaskComplete)
                {
                    fnTaskComplete();
                }
            };

            // add syncTask
            m_bufferCreateAsyncTask = NSKBase::g_GetAsyncManager()->AddTask(
                NSKBase::EAsyncTaskPriority::High,
                false, // only on render thread
                std::move(fnTaskExecute),
                std::move(fnTaskFinishComplete)
            );

            bRet = true;
        }

        return bRet;
    }

    IKGFX_Buffer* KGFX_AsyncBuffer::GetBuffer()
    {
        // 检查状态
        std::lock_guard<std::mutex> lock(m_mtxBufferState);
        if (m_eBufferState == KGfxAsyncBufferState::Created && m_pBuffer)
        {
            return m_pBuffer;
        }

        return nullptr;
    }

    gfx::KGfxAsyncBufferState KGFX_AsyncBuffer::GetBufferState()
    {
        std::lock_guard<std::mutex> lock(m_mtxBufferState);
        return m_eBufferState;
    }


    void KGFX_AsyncBuffer::SetObjectName(const char* szName)
    {
        std::lock_guard<std::mutex> lock(m_mtxBufferState);
        if (m_eBufferState == KGfxAsyncBufferState::Created && m_pBuffer)
        {
            return m_pBuffer->SetDebugName(szName);
        }

        return;
    }

    BOOL KGFX_AsyncBuffer::Update(const void* pSrcData, uint32_t uSrcDataSize, uint32_t uDstOffset, BOOL bReset)
    {
        std::lock_guard<std::mutex> lock(m_mtxBufferState);
        if (m_eBufferState == KGfxAsyncBufferState::Created && m_pBuffer)
        {
            return m_pBuffer->Update(pSrcData, uSrcDataSize, uDstOffset, bReset);
        }

        return FALSE;
    }

    uint32_t KGFX_AsyncBuffer::GetDynamicOffset()
    {
        ASSERT(m_eBufferState == KGfxAsyncBufferState::Created && m_pBuffer);
        std::lock_guard<std::mutex> lock(m_mtxBufferState);
        if (m_eBufferState == KGfxAsyncBufferState::Created && m_pBuffer)
        {
            return m_pBuffer->GetDynamicOffset();
        }

        return 0;
    }

    void* KGFX_AsyncBuffer::MapRange()
    {
        ASSERT(m_eBufferState == KGfxAsyncBufferState::Created && m_pBuffer);
        std::lock_guard<std::mutex> lock(m_mtxBufferState);
        if (m_eBufferState == KGfxAsyncBufferState::Created && m_pBuffer)
        {
            return m_pBuffer->MapRange();
        }

        return nullptr;
    }

    BOOL KGFX_AsyncBuffer::IsDynamic()
    {
        ASSERT(m_eBufferState == KGfxAsyncBufferState::Created && m_pBuffer);
        std::lock_guard<std::mutex> lock(m_mtxBufferState);
        if (m_eBufferState == KGfxAsyncBufferState::Created && m_pBuffer)
        {
            return m_pBuffer->IsDynamic();
        }
        return false;
    }

    uint64_t KGFX_AsyncBuffer::GetBufferDeviceAddress()
    {
        ASSERT(m_eBufferState == KGfxAsyncBufferState::Created && m_pBuffer);
        std::lock_guard<std::mutex> lock(m_mtxBufferState);
        if (m_eBufferState == KGfxAsyncBufferState::Created && m_pBuffer)
        {
            return m_pBuffer->GetBufferDeviceAddress();
        }
        return 0;
    }
}
