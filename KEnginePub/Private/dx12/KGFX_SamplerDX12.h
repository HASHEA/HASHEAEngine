#pragma once
#include "KGFX_Dx12Header.h"
#include "KGFX_Dx12Healper.h"
#include "KEnginePub/Public/IGFX_Public.h"

namespace gfx
{
    class KGFX_SamplerDX12;

    class KGFX_SamplerBindlessViewDX12 final : public IKGFX_SamplerBindlessView
    {
    public:
        KGFX_SamplerBindlessViewDX12(KGFX_SamplerDX12* pSampler);
        ~KGFX_SamplerBindlessViewDX12();
    public:
        // 通过 IKGFX_SamplerBindlessView 继承
        uint32_t GetBindlessHandle() override;
        const KSamplerState& GetSamplerState() override;
        IKGFX_Sampler* GetResource() override;

    private:
        BindlessDescriptor m_BindlessDescriptor = {};
        KGFX_SamplerDX12* m_Sampler = nullptr;
    };
    class KGFX_SamplerDX12 final : public IKGFX_Sampler
    {
        //struct SamplerStateHasher
        //{
        //    static inline uint32_t HashFloatBits(float v)
        //    {
        //        // 避免 -0/+0 视为不同，可按需要统一成 0
        //        if (v == 0.0f) v = 0.0f;
        //        uint32_t bits;
        //        static_assert(sizeof(float) == sizeof(uint32_t), "float size");
        //        std::memcpy(&bits, &v, sizeof(uint32_t));
        //        return bits;
        //    }

        //    size_t operator()(const KSamplerState& key) const
        //    {
        //        size_t h = 0;

        //        // 打包位域（保证与相等判断一致）
        //        //uint32_t packed =
        //        //    (uint32_t(key.enuAddressModeU) << 0) |
        //        //    (uint32_t(key.enuAddressModeV) << 3) |
        //        //    (uint32_t(key.enuAddressModeW) << 6) |
        //        //    (uint32_t(key.enuBorderColor) << 9) |
        //        //    (uint32_t(key.enuCompareFunc) << 12) |
        //        //    (uint32_t(key.enuMagFilter) << 15) |
        //        //    (uint32_t(key.enuMinFilter) << 17) |
        //        //    (uint32_t(key.enuMipmapMode) << 19) |
        //        //    (uint32_t(key.enuTextureReductionOp) << 20);

        //        HashCombine(h, key.u);
        //        HashCombine(h, HashFloatBits(key.fMipLodBias));
        //        HashCombine(h, HashFloatBits(key.finialMipBias));
        //        HashCombine(h, HashFloatBits(key.fMaxAnisotropy));
        //        HashCombine(h, HashFloatBits(key.fToMinLod));
        //        HashCombine(h, HashFloatBits(key.fToMaxLod));

        //        return h;
        //    }
        //};

    public:
        KGFX_SamplerDX12() = default;
        ~KGFX_SamplerDX12()override;
        KGFX_SamplerDX12(const KGFX_SamplerDX12& other) = delete;
        KGFX_SamplerDX12(const KGFX_SamplerDX12&& other) = delete;
        KGFX_SamplerDX12& operator =(const KGFX_SamplerDX12& other) = delete;
        KGFX_SamplerDX12& operator =(const KGFX_SamplerDX12&& other) = delete;

        void Init(D3D12Descriptor handle,const KSamplerState& samplerState);

        static std::unordered_map<const_pool_str, KGFX_SamplerDX12*>& GetSamplerPool();

        const KSamplerState& GetSamplerState() override;

        uintptr_t GetNativeHandle() override;

        static void ClearSamplerPool();

        IKGFX_SamplerBindlessView* GetBindlessView() override;
    private:
     
        static std::unordered_map<const_pool_str, KGFX_SamplerDX12*> g_AllSamplerPool;
        KSamplerState m_SamplerState = {};
        D3D12Descriptor m_D3d12Descriptor = {};
        KGFX_SamplerBindlessViewDX12* m_pSamplerBindlesView = nullptr;

    };
}
