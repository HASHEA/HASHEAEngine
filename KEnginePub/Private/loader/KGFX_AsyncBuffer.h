#pragma once
#include "KEnginePub/Public/IGFX_Public.h"

namespace gfx
{
    enum class KGfxAsyncBufferState
    {
        Invalid,
        Created,       // 创建完成，数据更新
        CreatedFailed, // 创建失败
    };

    class KGFX_AsyncBuffer : public KGfxRef
    {
    public:
        KGFX_AsyncBuffer();
        virtual ~KGFX_AsyncBuffer();

        BOOL     Create(const KGfxBufferDesc& bufDesc, const void* pData, NSKBase::KAsyncTask::TaskFinish&& fnTaskComplete);
        const KGfxBufferDesc* GetDesc() { return &m_stAsyncBufferDesc; }
        IKGFX_Buffer* GetBuffer();
        void     SetObjectName(const char* szName);
        BOOL     Update(const void* pSrcData, uint32_t uSrcDataSize, uint32_t uDstOffset, BOOL bReset);
        uint32_t GetDynamicOffset();
        void*    MapRange();
        BOOL     IsDynamic();
        uint64_t GetBufferDeviceAddress();

    public:
        KGfxAsyncBufferState GetBufferState();

    private:
        IKGFX_Buffer*                    m_pBuffer = nullptr; // 异步加载的Buffer
        KAutoRefPtr<NSKBase::KAsyncTask> m_bufferCreateAsyncTask;
        void*                            m_pAsyncInitData = nullptr;
        KGfxBufferDesc                   m_stAsyncBufferDesc;
        KGfxAsyncBufferState             m_eBufferState = KGfxAsyncBufferState::Invalid; // buffer状态
        std::mutex                       m_mtxBufferState;                               // buffer状态互斥锁
    };
}
