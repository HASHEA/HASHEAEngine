#pragma once
#include <filesystem>
#include <functional>
#include "DMA_2.0.0/D3D12MemAlloc.h"
#include "KEnginePub/Public/IGFX_Public.h"
#include "Eigen/Eigen"


namespace gfx
{
    class KGFX_BufferImplDX12;
    class KGFX_TransientHeapDX12;
    class KGFX_TextureImplDx12;

    template <class T>
    void HashCombine(std::size_t& seed, const T& v)
    {
        std::hash<T> hasher;
        seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }

    bool UploadSubBufferDataImpl(ID3D12GraphicsCommandList* cmdList, const KGFX_TransientHeapDX12* transientHeap,
        KGFX_BufferImplDX12* buffer, uint32_t offset, uint32_t size, const void* data);

    bool UploadTextureDataImpl(ID3D12GraphicsCommandList* cmdList, KGFX_TransientHeapDX12* transientHeap, KGFX_TextureImplDx12* texture, const std::vector<KGfxSubResourceData>& data);

    bool CopyTexToBufferImpl(ID3D12GraphicsCommandList* cmdList, KGFX_BufferImplDX12* dstBuffer, KGFX_TextureImplDx12* srcTexture);

    bool UploadTextureSubDataImpl(ID3D12GraphicsCommandList* cmdList, KGFX_TransientHeapDX12* transientHeap, KGFX_TextureImplDx12* texture, const KTextureCopyRegion& copyRegin, const KGfxSubResourceData& data);

    D3D12_RESOURCE_STATES GetResourceState(KGfxAccess state);

    int CalcEffectiveArraySize(const KGFX_TextureImplDx12* tex);

    Eigen::Vector3i CalcMipSize(Eigen::Vector3i size, int mipLevel);

    bool IsCompressedFormat(DXGI_FORMAT format);

    uint32_t CalcAligned(uint32_t size, uint32_t alignment);

    uint32_t GetSubresourceMipLevel(uint32_t subresourceIndex, uint32_t mipLevelCount);

    uint32_t GetSubresourceIndex(uint32_t mipSlice, uint32_t arraySlice, uint32_t planeSlice, uint32_t arraySize, uint32_t mipLevels);

    uint32_t GetSubresourceIndex(const KGfxSubresourceRange& subRange, const KGFX_TextureImplDx12* ptex = nullptr, bool HasStencial = false);

    const std::filesystem::path& GetDX12TechRootPath();

    ResourceViewDimension TexDimToViewDim(TextureDimensionType texDim);

    TextureFormatInfo GetDX12FormatInfo(enumTextureFormat eFormat);

    TextureFormatInfo GetDX12FormatInfo(DXGI_FORMAT dxgiFormat);
}
