#include "KGFX_Dx12Healper.h"
#include "KGFX_BufferImplDX12.h"
#include "KGFX_GraphiceDeviceDx12.h"
#include "KGFX_TextureImplDx12.h"
#include "KGFX_TransientHeap.h"
#include "DirectXTex/DirectXTex/DirectXTex.h"
namespace gfx
{
    bool UploadSubBufferDataImpl(ID3D12GraphicsCommandList* cmdList, const KGFX_TransientHeapDX12* transientHeap, KGFX_BufferImplDX12* buffer, uint32_t offset, uint32_t size, const void* data)
    {
        HRESULT hresult = E_FAIL;
        bool bRet = false;
        KGFX_BufferImplDX12* uploadResource = nullptr;
        uint32_t uploadResourceOffset = 0;

        if (buffer->GetCpuAccess() != KGfxResourceAccessType::KGfxResourceAccess_Write)
        {
            bRet = transientHeap->AllocateStagingBuffer(size, uploadResource, uploadResourceOffset, KGfxResourceAccessType::KGfxResourceAccess_Write);
            assert(bRet);
        }
        else
        {
            uploadResource = buffer;
            uploadResourceOffset = offset;
        }
        KGFX_BufferImplDX12* uploadBuf = buffer->GetCpuAccess() == KGfxResourceAccessType::KGfxResourceAccess_Write ? buffer : uploadResource;
        ID3D12Resource* uploadResourceRef = uploadBuf->GetBufResource();


        byte* uploadData = nullptr;
        uploadData = static_cast<byte*>(uploadBuf->MapCpuData());
        byte* copyDst = static_cast<byte*>(uploadData) + uploadResourceOffset;

        memcpy(copyDst, data, size);

        if (buffer->GetCpuAccess() != KGfxResourceAccessType::KGfxResourceAccess_Write)
        {
            cmdList->CopyBufferRegion(buffer->GetBufResource(), offset, uploadResourceRef, uploadResourceOffset, size);
        }

        bRet = true;
        return bRet;
    }

    bool UploadTextureDataImpl(ID3D12GraphicsCommandList* cmdList, KGFX_TransientHeapDX12* transientHeap, KGFX_TextureImplDx12* texture, const std::vector<KGfxSubResourceData>& data)
    {
        HRESULT hresult = E_FAIL;
        bool bRet = false;
        KGFX_BufferImplDX12* uploadResource = nullptr;
        uint32_t uploadResourceOffset = 0;
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        ID3D12Device* dxDevice = pGraphicDevice->GetDXDevice();
        auto DXtexDesc = texture->GetDXResourceDesc();
        auto texDesc = *texture->GetDesc();
        auto subResourceCount = D3D12CalcSubresource(0, 0, 1, DXtexDesc.MipLevels, DXtexDesc.ArraySize());


        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(subResourceCount);
        std::vector<uint64_t> mipRowSizeInBytes(subResourceCount);
        std::vector<uint32_t> mipNumRows(subResourceCount);

        D3D12_RESOURCE_DESC dxResDesc = DXtexDesc.ToDesc();
        std::vector<D3D12_SUBRESOURCE_DATA> subResourceData(subResourceCount);
        struct SUB_RESOURCE_INTO
        {
            uint16_t uSubTexWidth = 0;
            uint16_t uSubTexHeight = 0;
        };

        std::vector<SUB_RESOURCE_INTO> SubResourceInfos(subResourceCount);

        uint32_t uSubTexIndex = 0;

        for (uint32_t uArr = 0; uArr < texDesc.uArraySize; ++uArr)
        {
            for (uint32_t uMip = 0; uMip < texDesc.uMipLevels; uMip++)
            {
                auto& iter = SubResourceInfos[uSubTexIndex];
                ++uSubTexIndex;

                iter.uSubTexWidth = texDesc.uWidth >> uMip;
                iter.uSubTexWidth = std::max<uint16_t>(iter.uSubTexWidth, 1u);

                if (texDesc.eDimension != TextureDimensionType::Texture1D)
                {
                    iter.uSubTexHeight = texDesc.uHeight >> uMip;
                    iter.uSubTexHeight = std::max<uint16_t>(iter.uSubTexHeight, 1u);
                }
                else
                {
                    iter.uSubTexHeight = 1;
                }
            }
        }

        /// 第一计算获取这个格式在GPU端的大小
        uint64_t requiredSize = 0;
        dxDevice->GetCopyableFootprints(&dxResDesc, 0, subResourceCount, 0,
            nullptr, nullptr, nullptr, &requiredSize);

        /// 有了大小之后分配一个临时的上传资源
        bRet = transientHeap->AllocateStagingBuffer(static_cast<uint32_t>(requiredSize), uploadResource, uploadResourceOffset, KGfxResourceAccessType::KGfxResourceAccess_Write);
        assert(bRet);

        /// 根据临时资源的大小和偏移获取每个子资源的上传布局
        dxDevice->GetCopyableFootprints(&dxResDesc, 0, subResourceCount, uploadResourceOffset,
            layouts.data(), mipNumRows.data(), mipRowSizeInBytes.data(), &requiredSize);

        ID3D12Resource* uploadResourceRef = uploadResource->GetBufResource();

        BYTE* pData = nullptr;
        /// 虽然这个地方的临时资源存在偏移，但是这个偏移已经在GetCopyableFootprints中计算了，所以不需要手动偏移了
        //hresult = uploadResourceRef->Map(0, nullptr, reinterpret_cast<void**>(&pData));
        pData = (BYTE*)uploadResource->MapCpuData();
        KGLOG_PROCESS_ERROR(pData);


        for (int i = 0; i < data.size(); ++i)
        {

            if (i>4)
            {
                int j = 0;
            }

            uint32_t width = SubResourceInfos.at(i).uSubTexWidth;
            uint32_t height = SubResourceInfos.at(i).uSubTexHeight;
            assert(width <= dxResDesc.Width);
            assert(height <= dxResDesc.Height);

            size_t rowPitch = 0;
            size_t slicePitch = 0;

            /// CPU端的元数据是GLI读取来的，所以没有遵循DX12的对齐规则，需要DirectX::CP_FLAGS_BAD_DXTN_TAILS这个标记
            DirectX::ComputePitch(dxResDesc.Format, width, height, rowPitch, slicePitch, DirectX::CP_FLAGS_NONE);
            D3D12_SUBRESOURCE_DATA temp = {};
            temp.pData = data[i].pMemData;
            temp.RowPitch = data[i].uMemByteRowPitch == 0 ? rowPitch : data[i].uMemByteRowPitch;
            temp.SlicePitch = data[i].uMemByteDepthPitch == 0 ? slicePitch : data[i].uMemByteDepthPitch;
            subResourceData.at(i) = (temp);
        }


        for (UINT i = 0; i < subResourceCount; ++i)
        {
            assert(mipRowSizeInBytes[i] < 0xfffffff);
            D3D12_MEMCPY_DEST DestData = { pData + layouts[i].Offset, layouts[i].Footprint.RowPitch, static_cast<SIZE_T>(layouts[i].Footprint.RowPitch) * static_cast<SIZE_T>(mipNumRows[i]) };
            MemcpyAllResource(&DestData, &subResourceData[i], static_cast<SIZE_T>(subResourceData[i].RowPitch), mipNumRows[i], layouts[i].Footprint.Depth, false);
        }

        for (UINT i = 0; i < subResourceCount; ++i)
        {
            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = uploadResourceRef;
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint = layouts[i];

            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = reinterpret_cast<ID3D12Resource*>(texture->GetNativeResourceHandle());
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.SubresourceIndex = i;

            cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        }

        bRet = true;
    Exit0:
        return bRet;
    }

    bool CopyTexToBufferImpl(ID3D12GraphicsCommandList* cmdList, KGFX_BufferImplDX12* dstBuffer, KGFX_TextureImplDx12* srcTexture)
    {
        bool bRet = false;
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        ID3D12Device* dxDevice = pGraphicDevice->GetDXDevice();
        auto DXtexDesc = srcTexture->GetDXResourceDesc();
        auto texDesc = *srcTexture->GetDesc();
        D3D12_RESOURCE_DESC dxResDesc = DXtexDesc.ToDesc();
        auto subResourceCount = D3D12CalcSubresource(0, 0, 1, DXtexDesc.MipLevels, DXtexDesc.ArraySize());

        struct SUB_RESOURCE_INTO
        {
            uint16_t uSubTexWidth = 0;
            uint16_t uSubTexHeight = 0;
            uint32_t subResIndex = 0;
        };
        std::vector<SUB_RESOURCE_INTO> SubResourceInfos(subResourceCount);

        uint32_t uSubTexIndex = 0;

        for (uint32_t uArr = 0; uArr < texDesc.uArraySize; ++uArr)
        {
            for (uint32_t uMip = 0; uMip < texDesc.uMipLevels; uMip++)
            {
                auto& iter = SubResourceInfos[uSubTexIndex];
                ++uSubTexIndex;

                iter.uSubTexWidth = texDesc.uWidth >> uMip;
                iter.uSubTexWidth = std::max<uint16_t>(iter.uSubTexWidth, 1u);

                if (texDesc.eDimension != TextureDimensionType::Texture1D)
                {
                    iter.uSubTexHeight = texDesc.uHeight >> uMip;
                    iter.uSubTexHeight = std::max<uint16_t>(iter.uSubTexHeight, 1u);
                }
                else
                {
                    iter.uSubTexHeight = 1;
                }

                iter.subResIndex = GetSubresourceIndex(uMip, uArr, 0, texDesc.uArraySize, texDesc.uMipLevels);
            }
        }

        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(subResourceCount);
        uint64_t requiredSize = 0;
        /// 根据临时资源的大小和偏移获取每个子资源的上传布局
        dxDevice->GetCopyableFootprints(&dxResDesc, 0, subResourceCount, 0, layouts.data(), nullptr, nullptr, &requiredSize);


        for (UINT i = 0; i < subResourceCount; ++i)
        {
            D3D12_TEXTURE_COPY_LOCATION dstRegion = {};
            dstRegion.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dstRegion.pResource = (ID3D12Resource*)(dstBuffer->GetNativeResourceHandle());
            dstRegion.PlacedFootprint = layouts.at(i);
            dstRegion.PlacedFootprint.Footprint.RowPitch = layouts.at(i).Footprint.Width * GetDX12FormatInfo(layouts.at(i).Footprint.Format).uBytesPerBlock;

            D3D12_BOX srcBox = {};
            srcBox.left = 0;
            srcBox.top = 0;
            srcBox.front = 0;
            srcBox.right = 0 + SubResourceInfos.at(i).uSubTexWidth;
            srcBox.bottom = 0 + SubResourceInfos.at(i).uSubTexHeight;
            srcBox.back = 0 + 1;

            D3D12_TEXTURE_COPY_LOCATION srcRegion = {};
            srcRegion.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            srcRegion.SubresourceIndex = SubResourceInfos.at(i).subResIndex;
            srcRegion.pResource = (ID3D12Resource*)srcTexture->GetNativeResourceHandle();


            cmdList->CopyTextureRegion(&dstRegion, 0, 0, 0, &srcRegion, &srcBox);
        }

        bRet = true;
        return bRet;
    }

    bool UploadTextureSubDataImpl(ID3D12GraphicsCommandList* cmdList, KGFX_TransientHeapDX12* transientHeap, KGFX_TextureImplDx12* texture, const KTextureCopyRegion& copyRegin, const KGfxSubResourceData& data)
    {
        struct SUB_RESOURCE_INTO
        {
            uint16_t uSubTexWidth = 0;
            uint16_t uSubTexHeight = 0;
        };

        bool bRet = false;
        KGFX_GraphicDeviceDx12* pGraphicDevice = KGFX_GetGraphicDeviceDx12Internal();
        ID3D12Device* dxDevice = pGraphicDevice->GetDXDevice();


        KGFX_BufferImplDX12* uploadResource = nullptr;
        uint32_t uploadResourceOffset = 0;

        auto dstTexDXDesc = texture->GetDXResourceDesc();

        /// 计算目标贴图的第一个子资源索引
        uint32_t dstTexFirstSubResIndex = 0;


        D3D12_RESOURCE_DESC middleTexDesc = dstTexDXDesc.ToDesc();
        middleTexDesc.Width = copyRegin.extentWidth;
        middleTexDesc.Height = copyRegin.extentHeight;
        middleTexDesc.DepthOrArraySize = copyRegin.extentDepth;
        middleTexDesc.MipLevels = 1;

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footPrint;
        uint64_t mipRowSizeInBytes;
        uint32_t mipNumRows;

        D3D12_SUBRESOURCE_DATA subResourceData;

        // 计算基 subresource = dstArraySlice * Mips + dstMip
        dstTexFirstSubResIndex = copyRegin.dstArraySlice * dstTexDXDesc.MipLevels + copyRegin.dstMipLevel;

        // 拷贝的 array 层数（若只拷贝单层则 extentDepth 应为 1）
        uint32_t arrayCountToCopy = std::max(1u, copyRegin.extentDepth);
        assert(arrayCountToCopy == 1);
        assert(copyRegin.dstArraySlice + arrayCountToCopy <= dstTexDXDesc.DepthOrArraySize);


        // 压缩格式对齐要求
        if (IsCompressedFormat(middleTexDesc.Format))
        {
            auto blockW = 4u;
            auto blockH = 4u;
            assert((copyRegin.dstLeft % blockW) == 0);
            assert((copyRegin.dstTop % blockH) == 0);
            assert((copyRegin.extentWidth % blockW) == 0);
            assert((copyRegin.extentHeight % blockH) == 0);
        }

        DX12SubTexCopyInfo copyInfo = {};

        /// 第一计算获取这个格式在GPU端的大小
        uint64_t requiredSize = 0;
        dxDevice->GetCopyableFootprints(&middleTexDesc, 0, 1, 0,
            nullptr, nullptr, nullptr, &requiredSize);

        /// 有了大小之后分配一个临时的上传资源
        bRet = transientHeap->AllocateStagingBuffer(static_cast<uint32_t>(requiredSize), uploadResource, uploadResourceOffset, KGfxResourceAccessType::KGfxResourceAccess_Write);
        assert(bRet);

        /// 根据临时资源的大小和偏移获取每个子资源的上传布局
        dxDevice->GetCopyableFootprints(&middleTexDesc, 0, 1, uploadResourceOffset,
            &footPrint, &mipNumRows, &mipRowSizeInBytes, &requiredSize);

        ID3D12Resource* uploadResourceRef = uploadResource->GetBufResource();

        BYTE* pData = nullptr;
        /// 虽然这个地方的临时资源存在偏移，但是这个偏移已经在GetCopyableFootprints中计算了，所以不需要手动偏移了
        pData = (BYTE*)uploadResource->MapCpuData();
        assert(pData);


        copyInfo.xOffset = 0;
        copyInfo.yOffset = 0;
        copyInfo.zOffset = 0;

        {

            size_t rowPitch = 0;
            size_t slicePitch = 0;

            DirectX::ComputePitch(middleTexDesc.Format, middleTexDesc.Width, middleTexDesc.Height, rowPitch, slicePitch, DirectX::CP_FLAGS_NONE);
            D3D12_SUBRESOURCE_DATA temp = {};
            temp.pData = data.pMemData;
            temp.RowPitch = data.uMemByteRowPitch == 0 ? rowPitch : data.uMemByteRowPitch;
            temp.SlicePitch = data.uMemByteDepthPitch == 0 ? slicePitch : data.uMemByteDepthPitch;
            subResourceData = (temp);
            uint32_t bitsPrePixel = (uint32_t)DirectX::BitsPerPixel(middleTexDesc.Format);
            //assert(bytesPrePixel % 8 == 0);
            assert(middleTexDesc.Height % mipNumRows == 0);

            copyInfo.pixelSize = (bitsPrePixel) * (middleTexDesc.Height / mipNumRows) / 8;
            assert(copyInfo.pixelSize > 0);
        }

        {
            assert(mipRowSizeInBytes < 0xfffffff);
            D3D12_MEMCPY_DEST DestData = { pData + footPrint.Offset, footPrint.Footprint.RowPitch, static_cast<SIZE_T>(footPrint.Footprint.RowPitch) * static_cast<SIZE_T>(mipNumRows) };
            copyInfo.xLength = copyRegin.extentWidth;
            copyInfo.yLength = mipNumRows;
            copyInfo.zLength = 1;
            MemcpySubresource(&DestData, &subResourceData, false, copyInfo);
        }


        {
            D3D12_TEXTURE_COPY_LOCATION src;
            src.pResource = uploadResourceRef;
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint = footPrint;

            D3D12_TEXTURE_COPY_LOCATION dst;
            dst.pResource = reinterpret_cast<ID3D12Resource*>(texture->GetNativeResourceHandle());
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.SubresourceIndex = dstTexFirstSubResIndex;

            D3D12_BOX srcBox = {};
            srcBox.left = 0;
            srcBox.top = 0;
            srcBox.right = copyRegin.extentWidth;
            srcBox.bottom = copyRegin.extentHeight;
            srcBox.front = 0;
            srcBox.back = 1;

            cmdList->CopyTextureRegion(&dst, copyRegin.dstLeft, copyRegin.dstTop, copyRegin.dstFront, &src, &srcBox);
        }

        bRet = true;
        return bRet;
    }


    D3D12_RESOURCE_STATES GetResourceState(KGfxAccess state)
    {
        switch (state)
        {
        case KGfxAccess::Unknown:
            return D3D12_RESOURCE_STATE_COMMON;
        case KGfxAccess::Present:
            return D3D12_RESOURCE_STATE_PRESENT;
        case KGfxAccess::IndirectArgs:
            return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        case KGfxAccess::SRVCompute:
            return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        case KGfxAccess::SRVGraphics:
            return D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
        case KGfxAccess::CopySrc:
            return D3D12_RESOURCE_STATE_COPY_SOURCE;
        case KGfxAccess::ResolveSrc:
            return D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
        case KGfxAccess::DSVRead:
            return D3D12_RESOURCE_STATE_DEPTH_READ;
        case KGfxAccess::UAVCompute:
            return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        case KGfxAccess::UAVGraphics:
            return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        case KGfxAccess::RTV:
            return D3D12_RESOURCE_STATE_RENDER_TARGET;
        case KGfxAccess::CopyDst:
            return D3D12_RESOURCE_STATE_COPY_DEST;
        case KGfxAccess::ResolveDst:
            return D3D12_RESOURCE_STATE_RESOLVE_DEST;
        case KGfxAccess::DSVWrite:
            return D3D12_RESOURCE_STATE_DEPTH_WRITE;
        case KGfxAccess::SRVMask:
            return D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
        case KGfxAccess::UAVMask:
            return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        case KGfxAccess::SRVGraphicsPixel:
            return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        case KGfxAccess::SRVGraphicsNonPixel:
            return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        case KGfxAccess::VertexBuffer:
            return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        case KGfxAccess::IndexBuffer:
            return D3D12_RESOURCE_STATE_INDEX_BUFFER;
        case KGfxAccess::ConstBuffer:
            return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        case KGfxAccess::ShadingRateSource:
            return D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE;
        case KGfxAccess::CPURead:
            return D3D12_RESOURCE_STATE_COPY_DEST;
        case KGfxAccess::BVHRead:
            return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        case KGfxAccess::BVHRead | KGfxAccess::IndexBuffer:
            return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_INDEX_BUFFER;
        case KGfxAccess::BVHRead | KGfxAccess::VertexBuffer:
            return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        default:
            /// 运行到这表示是不可以转换的layout，外界需要查看自己的调用
            assert(false);
            return D3D12_RESOURCE_STATE_COMMON;
        }
    }

    int CalcEffectiveArraySize(const KGFX_TextureImplDx12* tex)
    {
        KGfxSubresourceRange subresRange;
        subresRange = tex->ResolveSubresourceRange(subresRange);

        return subresRange.uArrayCount;
    }

    static int CalcMipSize(int size, int level)
    {
        size = size >> level;
        return size > 0 ? size : 1;
    }

    Eigen::Vector3i CalcMipSize(Eigen::Vector3i size, int mipLevel)
    {
        Eigen::Vector3i rs = {};
        rs.x() = CalcMipSize(size.x(), mipLevel);
        rs.y() = CalcMipSize(size.y(), mipLevel);
        rs.z() = CalcMipSize(size.z(), mipLevel);
        return rs;
    }

    bool IsCompressedFormat(DXGI_FORMAT format)
    {
        switch (format)
        {
        case DXGI_FORMAT_BC1_TYPELESS:
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC1_UNORM_SRGB:
        case DXGI_FORMAT_BC2_TYPELESS:
        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC2_UNORM_SRGB:
        case DXGI_FORMAT_BC3_TYPELESS:
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC3_UNORM_SRGB:
        case DXGI_FORMAT_BC4_TYPELESS:
        case DXGI_FORMAT_BC4_UNORM:
        case DXGI_FORMAT_BC4_SNORM:
        case DXGI_FORMAT_BC5_TYPELESS:
        case DXGI_FORMAT_BC5_UNORM:
        case DXGI_FORMAT_BC5_SNORM:
        case DXGI_FORMAT_BC6H_TYPELESS:
        case DXGI_FORMAT_BC6H_UF16:
        case DXGI_FORMAT_BC6H_SF16:
        case DXGI_FORMAT_BC7_TYPELESS:
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            return true;
        default:
            return false;
        }
    }


    uint32_t CalcAligned(uint32_t size, uint32_t alignment)
    {
        return size + alignment - 1 & ~(alignment - 1);
    }

    uint32_t GetSubresourceMipLevel(uint32_t subresourceIndex, uint32_t mipLevelCount)
    {
        return subresourceIndex % mipLevelCount;
    }

    uint32_t GetSubresourceIndex(uint32_t mipSlice, uint32_t arraySlice, uint32_t planeSlice, uint32_t arraySize, uint32_t mipLevels)
    {
        uint32_t res = mipSlice + arraySlice * mipLevels + planeSlice * arraySize * mipLevels;
        assert(res < 0xff);
        return res;
    }

    uint32_t GetSubresourceIndex(const KGfxSubresourceRange& subRange, const KGFX_TextureImplDx12* ptex, bool HasStencial)
    {
        if (subRange.IsWholeResource())
        {
            return D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        }

        if (ptex)
        {
            assert(subRange.uArrayCount <= ptex->GetDesc()->uArraySize);
            assert(subRange.uMipCount <= ptex->GetDesc()->uMipLevels == 0 ? D3D12_REQ_MIP_LEVELS : ptex->GetDesc()->uMipLevels);
            if (subRange.uBaseArraySlice == 0 && subRange.uArrayCount == ptex->GetDesc()->uArraySize && subRange.uBaseMipLevel == 0 && subRange.uMipCount == ptex->GetDesc()->uMipLevels)
            {
                return D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            }
        }

        uint32_t subIndex = GetSubresourceIndex(subRange.uBaseMipLevel, subRange.uBaseArraySlice, HasStencial, subRange.uArrayCount, subRange.uMipCount);

        return subIndex;
    }

    const std::filesystem::path& GetDX12TechRootPath()
    {
        static std::filesystem::path ShaderRootPath("enginedata/material/tech");
        return ShaderRootPath;
    }


    ResourceViewDimension TexDimToViewDim(TextureDimensionType texDim)
    {
        switch (texDim)
        {
        case TextureDimensionType::Texture1D:
            return ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE1D;
        case TextureDimensionType::Texture2D:
            return ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D;
        case TextureDimensionType::TextureCube:
            return ResourceViewDimension::RESOURCE_DIMENSION_TEXTURECUBE;
        case TextureDimensionType::Texture3D:
            return ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE3D;
        default:
            assert(false);
            return ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D;
        }
    }

    TextureFormatInfo GetDX12FormatInfo(enumTextureFormat eFormat)
    {
        DXGI_FORMAT dxgiFormat = GetTexToDxFormat(eFormat, false);
        return GetDX12FormatInfo(dxgiFormat);
    }

    TextureFormatInfo GetDX12FormatInfo(DXGI_FORMAT dxgiFormat)
    {
        TextureFormatInfo info = {};
        uint32_t blockSize = static_cast<uint32_t>(DirectX::BytesPerBlock(dxgiFormat));
        uint32_t pixelSize = static_cast<uint32_t>(DirectX::BitsPerPixel(dxgiFormat));
        info.uBytesPerBlock = blockSize == 0 ? (pixelSize / 8) : blockSize;
        assert(info.uBytesPerBlock > 0);
        assert((blockSize * 8) % pixelSize == 0);
        uint32_t preBlockSize = static_cast<uint32_t>(sqrt((blockSize * 8) / pixelSize));
        info.uWidthPerBlock = blockSize == 0 ? 1 : preBlockSize;
        info.uHeightPerBlock = info.uWidthPerBlock;
        return info;
    }

}
