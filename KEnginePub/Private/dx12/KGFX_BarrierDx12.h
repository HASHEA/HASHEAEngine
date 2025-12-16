#pragma once
#ifdef _WIN32
#include <variant>
#include "KEnginePub/Public/IGFX_Public.h"
#include "KGFX_Dx12Header.h"

namespace gfx
{
    class KGFX_TextureImplDx12;
    class KGFX_BufferImplDX12;
}


namespace gfx
{
    struct TranslateResource
    {
        std::variant<KGFX_BufferImplDX12*, IKGFX_TextureResource*> resource = {};

        TranslateResource(KGFX_BufferImplDX12* pBuf) : resource(pBuf) {}

        TranslateResource(IKGFX_TextureResource* pTex) : resource(pTex) {}

        bool operator==(const TranslateResource& other) const
        {
            return resource == other.resource;
        }

        inline void VisitBindedResource(KGFX_BufferImplDX12*& retBufImpl, IKGFX_TextureResource*& refTexImpl) const
        {
            {
                std::visit([&retBufImpl, &refTexImpl](auto&& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, IKGFX_TextureResource*>)
                    {
                        refTexImpl = value;
                    }
                    else if constexpr (std::is_same_v<T, KGFX_BufferImplDX12*>)
                    {
                        retBufImpl = value;
                    }
                    }, resource);
            }
        }
    };

    struct TranslateResourceHash
    {
        size_t operator()(const gfx::TranslateResource& tr) const noexcept
        {
            //这里只能对指针做 hash
            return std::visit([](auto&& ptr)
                {
                    std::hash<void*> hash;
                    return hash(ptr);
                }, tr.resource);
        }
    };


    /**
    * CPU端暂存的barrier列表，当提交到GPU后，会清空这个列表，并把资源的layout状态更新到CPU端
    * 只能单线程运行
    */
    class KGFX_BarrierTrackerDx12
    {
    public:
        struct TranresSubBarrier
        {
            TranslateResource res = {static_cast<KGFX_BufferImplDX12*>(nullptr)};
            KD3DX12_RESOURCE_BARRIER barrier = {};
        };

        KGFX_BarrierTrackerDx12() = default;
        ~KGFX_BarrierTrackerDx12();
        KGFX_BarrierTrackerDx12(const KGFX_BarrierTrackerDx12&) = delete;
        KGFX_BarrierTrackerDx12& operator=(const KGFX_BarrierTrackerDx12&) = delete;
        KGFX_BarrierTrackerDx12(KGFX_BarrierTrackerDx12&&) = delete;
        KGFX_BarrierTrackerDx12& operator=(KGFX_BarrierTrackerDx12&&) = delete;

        void Reset();

        void CommitAllBarrier(ID3D12GraphicsCommandList* pCmdList);
        void ResourceBarrier(const KD3DX12_RESOURCE_BARRIER& barrier, KGFX_BufferImplDX12* bufImpl);
        void ResourceBarrier(const KD3DX12_RESOURCE_BARRIER& barrier, IKGFX_TextureResource* texImpl);
    private:
        void ClearAndWriteStateToRes();

        std::vector<TranresSubBarrier> m_vecResourceBarriers = {};
        std::vector<KD3DX12_RESOURCE_BARRIER> m_vecSubmitResourceBarriers = {};
    };
};



#endif
