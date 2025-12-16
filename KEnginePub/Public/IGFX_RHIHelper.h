#pragma once
#include "IGFX_Public.h"
#include "KEsDrv.h"

namespace gfx
{
    namespace RHIHelper
    {
        class AutoProgramBinder
        {
        public:
            AutoProgramBinder(IKGFX_ProgramBinder& binder)
            {
                pBinder = &binder;
            }

            IKGFX_ProgramBinder* Get()
            {
                return pBinder;
            }

            AutoProgramBinder& AddBindUAV(const_pool_str pcszName, IKGFX_BufferView* pUAV)
            {
                pBinder->AddBindUAV(pcszName, pUAV);
                return *this;
            }
            AutoProgramBinder& AddBindUAV(const_pool_str pcszName, IKGFX_TextureView* pTexView)
            {
                pBinder->AddBindUAV(pcszName, pTexView);
                return *this;
            }

            // 当前接口用于绑定单寄存器的对象数组
            AutoProgramBinder& AddBindUAVArray(const_pool_str pcszName, uint32_t uNum, IKGFX_BufferView** pBufViews)
            {
                pBinder->AddBindUAVArray(pcszName, uNum, pBufViews);
                return *this;
            }
            AutoProgramBinder& AddBindUAVArray(const_pool_str pcszName, uint32_t uNum, IKGFX_TextureView** pTexViews)
            {
                pBinder->AddBindUAVArray(pcszName, uNum, pTexViews);
                return *this;
            }

            AutoProgramBinder& AddBindSRV(const_pool_str pcszName, IKGFX_BufferView* pSRV)
            {
                pBinder->AddBindSRV(pcszName, pSRV);
                return *this;
            }
            AutoProgramBinder& AddBindSRV(const_pool_str pcszName, IKGFX_TextureView* pTexView)
            {
                pBinder->AddBindSRV(pcszName, pTexView);
                return *this;
            }

            // 当前接口用于绑定单寄存器的对象数组
            AutoProgramBinder& AddBindSRVArray(const_pool_str pcszName, uint32_t uNum, IKGFX_BufferView** pBufViews)
            {
                pBinder->AddBindSRVArray(pcszName, uNum, pBufViews);
                return *this;
            }
            AutoProgramBinder& AddBindSRVArray(const_pool_str pcszName, uint32_t uNum, IKGFX_TextureView** pTexViews)
            {
                pBinder->AddBindSRVArray(pcszName, uNum, pTexViews);
                return *this;
            }

            AutoProgramBinder& AddBindCBV(const_pool_str pcszName, IKGFX_BufferView* pBufView)
            {
                pBinder->AddBindCBV(pcszName, pBufView);
                return *this;
            }

            AutoProgramBinder& AddBindAccelerationStructure(const_pool_str pcszName, KRayTracingScene* pAS)
            {
                pBinder->AddBindAccelerationStructure(pcszName, pAS);
                return *this;
            }

            AutoProgramBinder& AddBindSampler(const_pool_str pcszName, IKGFX_Sampler* pSampler)
            {
                pBinder->AddBindSampler(pcszName, pSampler);
                return *this;
            }

            AutoProgramBinder& SetImmutableConstValueInt(const_pool_str pcszName, int32_t value)
            {
                pBinder->SetImmutableConstValueInt(pcszName, value);
                return *this;
            }
            AutoProgramBinder& SetImmutableConstValueUInt(const_pool_str pcszName, uint32_t value)
            {
                pBinder->SetImmutableConstValueUInt(pcszName, value);
                return *this;
            }
            AutoProgramBinder& SetImmutableConstValueFloat(const_pool_str pcszName, float value)
            {
                pBinder->SetImmutableConstValueFloat(pcszName, value);
                return *this;
            }
            BOOL IsTextureBinded(const_pool_str pName)
            {
                return pBinder->IsTextureBinded(pName);
            }
            BOOL UpdateMtlData()
            {
                return pBinder->UpdateMtlData();
            }

            BOOL SetMtlParamValue(const_pool_str szName, void* pData, uint32_t uByteSize)
            {
                return pBinder->SetMtlParamValue(szName, pData, uByteSize);
            }
            BOOL IsBinding()
            {
                return pBinder->IsBinding();
            }
            TextureType GetTextureType(const_pool_str szName)
            {
                return pBinder->GetTextureType(szName);
            }

            /////////////////////////////////////////////////////////////////

            template <typename T>
            AutoProgramBinder& AutoBindUAV(const_pool_str pcszName, T* pObj)
            {
                if (pObj)
                {
                    pBinder->AddBindUAV(pcszName, pObj->GetUAV());
                }
                return *this;
            }

            template <typename T>
            AutoProgramBinder& AutoBindSRV(const_pool_str pcszName, T* pObj)
            {
                if (pObj)
                {
                    pBinder->AddBindSRV(pcszName, pObj->GetSRV());
                }
                return *this;
            }

            AutoProgramBinder& AutoBindSRV(const_pool_str pcszName, gfx::IKGFX_TextureView* pObj)
            {
                if (pObj)
                {
                    pBinder->AddBindSRV(pcszName, pObj);
                }
                return *this;
            }

            template <typename T>
            AutoProgramBinder& AutoBindMipUAV(const_pool_str pcszName, T* pObj, uint32_t n)
            {
                if (pObj)
                {
                    pBinder->AddBindUAV(pcszName, pObj->GetMipUAV(n));
                }
                return *this;
            }

            template <typename T>
            AutoProgramBinder& AutoBindMipSRV(const_pool_str pcszName, T* pObj, uint32_t n)
            {
                if (pObj)
                {
                    pBinder->AddBindUAV(pcszName, pObj->GetMipSRV(n));
                }
                return *this;
            }

        private:
            IKGFX_ProgramBinder* pBinder = nullptr;
        };

        ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        inline void HashCombine(uint64_t& h, uint64_t k)
        {
            constexpr uint64_t m = UINT64_C(0xc6a4a7935bd1e995);
            constexpr int      r = 47;

            k *= m;
            k ^= k >> r;
            k *= m;

            h ^= k;
            h *= m;

            // Completely arbitrary number, to prevent 0's
            // from hashing to 0.
            h += 0xe6546b64;
        }

        ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // TextureUsageFlags
        inline TextureUsageFlags GetTextureUsage_ColorRenderTarget(bool bUAVUsage = false)
        {
            TextureUsageFlags uUsageFlags = TextureUsageFlagBits::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | TextureUsageFlagBits::TEXTURE_USAGE_SAMPLED_BIT;

            if (bUAVUsage)
            {
                uUsageFlags |= TextureUsageFlagBits::TEXTURE_USAGE_STORAGE_BIT;
            }

            return uUsageFlags;
        }

        inline TextureUsageFlags GetTextureUsage_DepthStencilTarget(bool bUAVUsage = false)
        {
            TextureUsageFlags uUsageFlags = TextureUsageFlagBits::TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | TextureUsageFlagBits::TEXTURE_USAGE_SAMPLED_BIT;

            if (bUAVUsage)
            {
                uUsageFlags |= TextureUsageFlagBits::TEXTURE_USAGE_STORAGE_BIT;
            }

            return uUsageFlags;
        }

        inline TextureUsageFlags GetTextureUsage_GPUReadWrite()
        {
            TextureUsageFlags uUsageFlags = TextureUsageFlagBits::TEXTURE_USAGE_SAMPLED_BIT | TextureUsageFlagBits::TEXTURE_USAGE_STORAGE_BIT;
            return uUsageFlags;
        }

        inline TextureUsageFlags GetTextureUsage_GPUReadOnly()
        {
            TextureUsageFlags uUsageFlags = TextureUsageFlagBits::TEXTURE_USAGE_SAMPLED_BIT;
            return uUsageFlags;
        }

        /// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // KGFX_TextureDesc
        inline KGFX_TextureDesc InitTexture2DDesc_GPUReadWrite(uint32_t InWidth, uint32_t InHeight, enumTextureFormat InFormat)
        {
            KGFX_TextureDesc texDesc;

            texDesc.uUsageFlags = GetTextureUsage_GPUReadWrite();
            texDesc.memoryType = KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;
            texDesc.eDimension = TextureDimensionType::Texture2D;
            texDesc.uWidth = InWidth;
            texDesc.uHeight = InHeight;
            texDesc.eFormat = InFormat;

            return texDesc;
        }

        inline KGFX_TextureDesc InitTexture2DDesc_GPUReadOnly(uint32_t InWidth, uint32_t InHeight, enumTextureFormat InFormat)
        {
            KGFX_TextureDesc texDesc;

            texDesc.uUsageFlags = GetTextureUsage_GPUReadOnly();
            texDesc.memoryType = KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;
            texDesc.eDimension = TextureDimensionType::Texture2D;
            texDesc.uWidth = InWidth;
            texDesc.uHeight = InHeight;
            texDesc.eFormat = InFormat;

            return texDesc;
        }

        inline KGFX_TextureDesc InitTexture2DDesc_ColorRenderTarget(uint32_t InWidth, uint32_t InHeight, enumTextureFormat InFormat, bool bUAVUsage = false)
        {
            KGFX_TextureDesc texDesc;

            texDesc.uUsageFlags = GetTextureUsage_ColorRenderTarget(bUAVUsage);
            texDesc.memoryType = KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;
            texDesc.eDimension = TextureDimensionType::Texture2D;
            texDesc.uWidth = InWidth;
            texDesc.uHeight = InHeight;
            texDesc.eFormat = InFormat;

            return texDesc;
        }

        inline KGFX_TextureDesc InitTexture2DDesc_DepthStencilTarget(uint32_t InWidth, uint32_t InHeight, enumTextureFormat InFormat, bool bUAVUsage = false)
        {
            KGFX_TextureDesc texDesc;

            texDesc.uUsageFlags = GetTextureUsage_DepthStencilTarget(bUAVUsage);
            texDesc.memoryType = KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;
            texDesc.eDimension = TextureDimensionType::Texture2D;
            texDesc.uWidth = InWidth;
            texDesc.uHeight = InHeight;
            texDesc.eFormat = InFormat;

            return texDesc;
        }

        inline uint64_t GetTextureDescHash(const KGFX_TextureDesc& texDesc)
        {
            uint64_t uHash = 0;
            HashCombine(uHash, (uint64_t)texDesc.eDimension);
            HashCombine(uHash, (uint64_t)texDesc.memoryType);
            HashCombine(uHash, (uint64_t)texDesc.uUsageFlags);
            HashCombine(uHash, (uint64_t)texDesc.uWidth);
            HashCombine(uHash, (uint64_t)texDesc.uHeight);
            HashCombine(uHash, (uint64_t)texDesc.uDepth);
            HashCombine(uHash, (uint64_t)texDesc.uArraySize);
            HashCombine(uHash, (uint64_t)texDesc.uMipLevels);
            HashCombine(uHash, (uint64_t)texDesc.bSRGB);
            HashCombine(uHash, (uint64_t)texDesc.eSampleCount);
            HashCombine(uHash, (uint64_t)texDesc.sampleQuality);
            return uHash;
        }

        ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // KGFX_TextureViewDesc
        inline KGFX_TextureViewDesc InitTexture2DViewDesc_SRV(gfx::enumTextureFormat InFormat)
        {
            gfx::KGFX_TextureViewDesc srvDesc;
            srvDesc.eViewType = gfx::KGfxResourceViewType::RESOURCE_VIEW_TYPE_SRV;
            srvDesc.eFormat = InFormat;
            srvDesc.eViewDimension = gfx::ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D;
            return srvDesc;
        }

        inline KGFX_TextureViewDesc InitTexture2DViewDesc_UAV(gfx::enumTextureFormat InFormat)
        {
            gfx::KGFX_TextureViewDesc uavDesc;
            uavDesc.eViewType = gfx::KGfxResourceViewType::RESOURCE_VIEW_TYPE_UAV;
            uavDesc.eFormat = InFormat;
            uavDesc.eViewDimension = gfx::ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D;
            return uavDesc;
        }

        inline KGFX_TextureViewDesc InitTexture2DArrayViewDesc_SRV(gfx::enumTextureFormat InFormat, uint32_t uBaseArrayIndex = 0, uint32_t uBaseArrayNum = UINT32_MAX)
        {
            gfx::KGFX_TextureViewDesc srvDesc;
            srvDesc.eViewType = gfx::KGfxResourceViewType::RESOURCE_VIEW_TYPE_SRV;
            srvDesc.eFormat = InFormat;
            srvDesc.eViewDimension = gfx::ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D_ARRAY;
            srvDesc.sSubresourceRange.uBaseArraySlice = uBaseArrayIndex;
            srvDesc.sSubresourceRange.uArrayCount = uBaseArrayNum;
            return srvDesc;
        }

        inline KGFX_TextureViewDesc InitTexture2DArrayViewDesc_UAV(gfx::enumTextureFormat InFormat, uint32_t uBaseArrayIndex = 0, uint32_t uBaseArrayNum = UINT32_MAX)
        {
            gfx::KGFX_TextureViewDesc uavDesc;
            uavDesc.eViewType = gfx::KGfxResourceViewType::RESOURCE_VIEW_TYPE_UAV;
            uavDesc.eFormat = InFormat;
            uavDesc.eViewDimension = gfx::ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D_ARRAY;
            uavDesc.sSubresourceRange.uBaseArraySlice = uBaseArrayIndex;
            uavDesc.sSubresourceRange.uArrayCount = uBaseArrayNum;
            return uavDesc;
        }

        inline uint64_t GetTextureViewDescHash(const KGFX_TextureViewDesc& viewDesc)
        {
            uint64_t uHash = 0;
            HashCombine(uHash, (uint64_t)viewDesc.eViewType);
            HashCombine(uHash, (uint64_t)viewDesc.eFormat);
            HashCombine(uHash, (uint64_t)viewDesc.eViewDimension);
            HashCombine(uHash, (uint64_t)viewDesc.uAspectFlags);
            HashCombine(uHash, (uint64_t)viewDesc.sSubresourceRange.uBaseMipLevel);
            HashCombine(uHash, (uint64_t)viewDesc.sSubresourceRange.uBaseArraySlice);
            HashCombine(uHash, (uint64_t)viewDesc.sSubresourceRange.uMipCount);
            HashCombine(uHash, (uint64_t)viewDesc.sSubresourceRange.uArrayCount);
            return uHash;
        }
    }
}
