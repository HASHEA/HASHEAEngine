#include "KGFX_TextureImplDx12.h"
#ifdef _WIN32
#include "KGFX_Dx12Header.h"
#include <DirectXTex/DirectXTex.h>

namespace gfx
{
    enumTextureFormat GetDxToTexFormat(DXGI_FORMAT eFormat)
    {
        BOOL bDepth = false, bStencil = false;
        return GetDxToTexFormat(eFormat, bDepth, bStencil);
    }

    DXGI_FORMAT GetTexToDxFormat(enumTextureFormat eFormat, bool bSRGB)
    {
        BOOL bDepth = false, bStencil = false;
        DXGI_FORMAT dxgiFormat = GetTexToDxFormat(eFormat, bDepth, bStencil);
        if (bSRGB)
        {
            dxgiFormat = DirectX::MakeSRGB(dxgiFormat);
        }

        return dxgiFormat;
    }

    DXGI_FORMAT GetTexFormatToSRVFormat(DXGI_FORMAT eFormat)
    {
        DXGI_FORMAT ret = eFormat;
        switch (eFormat)
        {
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
            ret = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            break;
        case DXGI_FORMAT_D32_FLOAT:
            ret = DXGI_FORMAT_R32_FLOAT;
            break;
        default:
            break;
        }
        return ret;
    }


    enumTextureFormat GetDxToTexFormat(DXGI_FORMAT eFormat, BOOL& bDepth, BOOL& bStencil)
    {
        bDepth = false;
        bStencil = false;
        enumTextureFormat format = TEX_FORMAT_NONE;
        switch (eFormat)
        {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
            format = TEX_FORMAT_R8G8B8A8_UNORM;
            break;
        case DXGI_FORMAT_R8G8B8A8_SNORM:
            format = TEX_FORMAT_R8G8B8A8_SNORM;
            break;
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            format = TEX_FORMAT_R8G8B8A8_SRGB;
            break;
        case DXGI_FORMAT_R8G8B8A8_UINT:
            format = TEX_FORMAT_R8G8B8A8_UINT;
            break;
        case DXGI_FORMAT_R8_UNORM:
            format = TEX_FORMAT_R8_UNORM;
            break;
        case DXGI_FORMAT_R8G8_UNORM:
            format = TEX_FORMAT_R8G8_UNORM;
            break;
        case DXGI_FORMAT_R16G16_UINT:
            format = TEX_FORMAT_R16G16_UINT;
            break;
        case DXGI_FORMAT_B8G8R8X8_UNORM:
            format = TEX_FORMAT_B8G8R8_UNORM;
            break;
        case DXGI_FORMAT_B8G8R8A8_UNORM:
            format = TEX_FORMAT_B8G8R8A8_UNORM;
            break;
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            format = TEX_FORMAT_B8G8R8A8_SRGB;
            break;
        case DXGI_FORMAT_R16G16B16A16_UNORM:
            format = TEX_FORMAT_R16G16B16A16_UNORM;
            break;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
            format = TEX_FORMAT_R16G16B16A16_SFLOAT;
            break;
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
            format = TEX_FORMAT_R32G32B32A32_SFLOAT;
            break;
        case DXGI_FORMAT_R16_FLOAT:
            format = TEX_FORMAT_R16_SFLOAT;
            break;
        case DXGI_FORMAT_R16_UINT:
            format = TEX_FORMAT_R16_UINT;
            break;
        case DXGI_FORMAT_R16G16_FLOAT:
            format = TEX_FORMAT_R16G16_SFLOAT;
            break;
        case DXGI_FORMAT_R32_SINT:
            format = TEX_FORMAT_R32_SINT;
            break;
        case DXGI_FORMAT_R32_UINT:
            format = TEX_FORMAT_R32_UINT;
            break;
        case DXGI_FORMAT_R32_FLOAT:
            format = TEX_FORMAT_R32_FLOAT;
            break;
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
            bDepth = true;
            bStencil = true;
            format = TEX_FORMAT_D24_UNORM_S8_UINT;
            break;
        case DXGI_FORMAT_D16_UNORM:
            bDepth = true;
            bStencil = false;
            format = TEX_FORMAT_D16_UNORM;
            break;
        case DXGI_FORMAT_D32_FLOAT:
            bDepth = true;
            bStencil = false;
            format = TEX_FORMAT_D32_SFLOAT;
            break;
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
            bDepth = true;
            bStencil = true;
            format = TEX_FORMAT_D32_SFLOAT_S8_UINT;
            break;
        case DXGI_FORMAT_BC1_UNORM:
            format = TEX_FORMAT_BC1_RGBA_UNORM;
            break;
        case DXGI_FORMAT_BC2_UNORM:
            format = TEX_FORMAT_BC2_UNORM;
            break;
        case DXGI_FORMAT_BC3_UNORM:
            format = TEX_FORMAT_BC3_UNORM;
            break;
        case DXGI_FORMAT_BC4_UNORM:
            format = TEX_FORMAT_BC4_UNORM;
            break;
        case DXGI_FORMAT_BC5_UNORM:
            format = TEX_FORMAT_BC5_UNORM;
            break;
        case DXGI_FORMAT_B5G6R5_UNORM:
            format = TEX_FORMAT_B5G6R5_UNORM_PACK16;
            break;
        case DXGI_FORMAT_R10G10B10A2_UNORM:
            format = TEX_FORMAT_A2R10G10B10_UNORM_PACK32;
            break;
        case DXGI_FORMAT_R11G11B10_FLOAT:
            format = TEX_FORMAT_B10G11R11_UFLOAT_PACK32; //dx用这个格式替代一下
            break;
        case DXGI_FORMAT_R32G32_UINT:
            format = TEX_FORMAT_R32G32_UINT;
            break;
        case DXGI_FORMAT_R32G32B32A32_UINT:
            format = TEX_FORMAT_R32G32B32A32_UINT;
            break;
        case DXGI_FORMAT_R16G16_UNORM:
            format = TEX_FORMAT_R16G16_UNORM;
            break;
        case DXGI_FORMAT_R8_UINT:
            format = TEX_FORMAT_R8_UINT;
            break;
        case DXGI_FORMAT_R16_UNORM:
            format = TEX_FORMAT_R16_UNORM;
            break;
        default:
            break;
        }
        return format;
    }

    DXGI_FORMAT GetTexToDxFormat(enumTextureFormat eFormat, BOOL& bDepth, BOOL& bStencil)
    {
        bDepth = false;
        bStencil = false;

        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        switch (eFormat)
        {
        case TEX_FORMAT_R8G8B8A8_UNORM:
            format = DXGI_FORMAT_R8G8B8A8_UNORM;
            break;
        case TEX_FORMAT_R8G8B8A8_SNORM:
            format = DXGI_FORMAT_R8G8B8A8_SNORM;
            break;
        case TEX_FORMAT_R8G8B8A8_SRGB:
            format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            break;
        case TEX_FORMAT_R8G8B8A8_UINT:
            format = DXGI_FORMAT_R8G8B8A8_UINT;
            break;
        case TEX_FORMAT_R8_UNORM:
            format = DXGI_FORMAT_R8_UNORM;
            break;
        case TEX_FORMAT_R8G8_UNORM:
            format = DXGI_FORMAT_R8G8_UNORM;
            break;
        case TEX_FORMAT_R16G16_UINT:
            format = DXGI_FORMAT_R16G16_UINT;
            break;
        case TEX_FORMAT_B8G8R8_UNORM:
            format = DXGI_FORMAT_B8G8R8X8_UNORM;
            break;
        case TEX_FORMAT_B8G8R8A8_UNORM:
            format = DXGI_FORMAT_B8G8R8A8_UNORM;
            break;
        case TEX_FORMAT_B8G8R8A8_SRGB:
            format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            break;
        case TEX_FORMAT_R16G16B16A16_UNORM:
            format = DXGI_FORMAT_R16G16B16A16_UNORM;
            break;
        case TEX_FORMAT_R16G16B16A16_SFLOAT:
            format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            break;
        case TEX_FORMAT_R32G32B32A32_SFLOAT:
            format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            break;
        case TEX_FORMAT_R16_SFLOAT:
            format = DXGI_FORMAT_R16_FLOAT;
            break;
        case TEX_FORMAT_R16_UINT:
            format = DXGI_FORMAT_R16_UINT;
            break;
        case TEX_FORMAT_R16G16_SFLOAT:
            format = DXGI_FORMAT_R16G16_FLOAT;
            break;
        case TEX_FORMAT_R32_SINT:
            format = DXGI_FORMAT_R32_SINT;
            break;
        case TEX_FORMAT_R32_UINT:
            format = DXGI_FORMAT_R32_UINT;
            break;
        case TEX_FORMAT_R32_FLOAT:
            format = DXGI_FORMAT_R32_FLOAT;
            break;
        case TEX_FORMAT_D24_UNORM_S8_UINT:
            bDepth = true;
            bStencil = true;
            format = DXGI_FORMAT_D24_UNORM_S8_UINT;
            break;
        case TEX_FORMAT_D16_UNORM:
            bDepth = true;
            bStencil = false;
            format = DXGI_FORMAT_D16_UNORM;
            break;
        case TEX_FORMAT_D32_SFLOAT:
            bDepth = true;
            bStencil = false;
            format = DXGI_FORMAT_D32_FLOAT;
            break;
        case TEX_FORMAT_D32_SFLOAT_S8_UINT:
            bDepth = true;
            bStencil = true;
            format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
            break;
        case TEX_FORMAT_BC1_RGBA_UNORM:
            format = DXGI_FORMAT_BC1_UNORM;
            break;
        case TEX_FORMAT_BC2_UNORM:
            format = DXGI_FORMAT_BC2_UNORM;
            break;
        case TEX_FORMAT_BC3_UNORM:
            format = DXGI_FORMAT_BC3_UNORM;
            break;
        case TEX_FORMAT_BC4_UNORM:
            format = DXGI_FORMAT_BC4_UNORM;
            break;
        case TEX_FORMAT_BC5_UNORM:
            format = DXGI_FORMAT_BC5_UNORM;
            break;
        case TEX_FORMAT_B5G6R5_UNORM_PACK16:
            format = DXGI_FORMAT_B5G6R5_UNORM;
            break;
        case TEX_FORMAT_A2R10G10B10_UNORM_PACK32:
            format = DXGI_FORMAT_R10G10B10A2_UNORM;
            break;
        case TEX_FORMAT_B10G11R11_UFLOAT_PACK32:
            format = DXGI_FORMAT_R11G11B10_FLOAT; //dx用这个格式替代一下
            break;
        case TEX_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
            assert(false);
            break;
        case TEX_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
            assert(false);
            break;
        case TEX_FORMAT_ETC2_RG_UNORM_BLOCK:
            assert(false);
            break;
        case TEX_FORMAT_ASTC_4X4_UNORM_BLOCK:
            assert(false);
            break;
        case TEX_FORMAT_ASTC_6X6_UNORM_BLOCK:
            assert(false);
            break;
        case TEX_FORMAT_ASTC_8X8_UNORM_BLOCK:
            assert(false);
            break;
        case TEX_FORMAT_R32G32_UINT:
            format = DXGI_FORMAT_R32G32_UINT;
            break;
        case TEX_FORMAT_R32G32B32A32_UINT:
            format = DXGI_FORMAT_R32G32B32A32_UINT;
            break;
        case TEX_FORMAT_R16G16_UNORM:
            format = DXGI_FORMAT_R16G16_UNORM;
            break;
        case TEX_FORMAT_R8_UINT:
            format = DXGI_FORMAT_R8_UINT;
            break;
        case TEX_FORMAT_R16_UNORM:
            format = DXGI_FORMAT_R16_UNORM;
            break;
        case TEX_FORMAT_COUNT:
            assert(false);
            break;
        case TEX_FORMAT_NONE:
            return DXGI_FORMAT_UNKNOWN;
        case TEX_FORMAT_R8G8B8_UNORM:
            format = DXGI_FORMAT_R8G8_UNORM;
            break;
        case TEX_FORMAT_R64_UINT:
            format = DXGI_FORMAT_R32G32_UINT;
            break;
        case TEX_FORMAT_BC1_RGB_UNORM:
            format = DXGI_FORMAT_BC1_UNORM;
            break;
        case TEX_FORMAT_BC4_SNORM:
            format = DXGI_FORMAT_BC4_SNORM;
            break;
        case TEX_FORMAT_BC5_SNORM:
            format = DXGI_FORMAT_BC5_SNORM;
            break;
        case TEX_FORMAT_BC6H_UFLOAT:
            format = DXGI_FORMAT_BC6H_UF16;
            break;
        case TEX_FORMAT_BC6H_SFLOAT:
            format = DXGI_FORMAT_BC6H_SF16;
            break;
        case TEX_FORMAT_BC7_UNORM:
            format = DXGI_FORMAT_BC7_UNORM;
            break;
        case TEX_FORMAT_ETC2_R_UNORM_BLOCK:
            assert(false);
            break;
        case TEX_FORMAT_ETC2_R_SNORM_BLOCK:
            assert(false);
            break;
        case TEX_FORMAT_ETC2_RG_SNORM_BLOCK:
            assert(false);
            break;
        case TEX_FORMAT_BC7_SRGB_UNORM:
            format = DXGI_FORMAT_BC7_UNORM_SRGB;
            break;
        default:
            break;
        }
        return format;
    }

    DXGI_FORMAT GetTexToDxFormat(enumTextureFormat eFormat, bool& bDepth, bool& bStencil)
    {
        BOOL bD = false, bS = false;
        DXGI_FORMAT resFormat = GetTexToDxFormat(eFormat, bD, bS);
        bDepth = bD;
        bStencil = bS;
        return resFormat;
    }

    D3D12_RESOURCE_FLAGS To_D3D12_RESOURCE_FLAGS(TextureUsageFlags uUsageFlags, enumTextureFormat eFormat)
    {
        BOOL bDepth = false, bStencil = false;
        D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;

        if ((uUsageFlags & TextureUsageFlagBits::TEXTURE_USAGE_STORAGE_BIT) > 0)
        {
            flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        }

        auto DXFormat = GetTexToDxFormat(eFormat, bDepth, bStencil);
        bool bIsComrpessForamt = IsCompressedFormat(DXFormat);

        if (bDepth)
        {
            flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        }
        else if (!bIsComrpessForamt)
        {
            flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        }

        return flags;
    }


    D3D12_RESOURCE_STATES To_D3D12_RESOURCE_STATES(enumImageLayout layout)
    {
        D3D12_RESOURCE_STATES ret = D3D12_RESOURCE_STATE_COMMON;
        switch (layout)
        {
        case IMAGE_LAYOUT_UNDEFINED:
            ret = D3D12_RESOURCE_STATE_COMMON;
            break;
        case IMAGE_LAYOUT_GENERAL:
            ret = D3D12_RESOURCE_STATE_COMMON;
            break;
        case IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            ret = D3D12_RESOURCE_STATE_RENDER_TARGET;
            break;
        case IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            ret = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            break;
        case IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            ret = D3D12_RESOURCE_STATE_DEPTH_READ;
            break;
        case IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            ret = D3D12_RESOURCE_STATE_GENERIC_READ;
            break;
        case IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            ret = D3D12_RESOURCE_STATE_COPY_SOURCE;
            break;
        case IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            ret = D3D12_RESOURCE_STATE_COPY_DEST;
            break;

        case IMAGE_LAYOUT_PRESENT_SRC_KHR:
            ret = D3D12_RESOURCE_STATE_PRESENT;
            break;
        case IMAGE_LAYOUT_SHARED_PRESENT_KHR:
            ret = D3D12_RESOURCE_STATE_PRESENT;
            break;
        default:
            break;
        }
        return ret;
    }


    size_t BitsPerPixel(DXGI_FORMAT fmt)
    {
        switch (fmt)
        {
        case DXGI_FORMAT_UNKNOWN:
            return 0;
        case DXGI_FORMAT_R32G32B32A32_TYPELESS:
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
        case DXGI_FORMAT_R32G32B32A32_UINT:
        case DXGI_FORMAT_R32G32B32A32_SINT:
            return 128;

        case DXGI_FORMAT_R32G32B32_TYPELESS:
        case DXGI_FORMAT_R32G32B32_FLOAT:
        case DXGI_FORMAT_R32G32B32_UINT:
        case DXGI_FORMAT_R32G32B32_SINT:
            return 96;

        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_UNORM:
        case DXGI_FORMAT_R16G16B16A16_UINT:
        case DXGI_FORMAT_R16G16B16A16_SNORM:
        case DXGI_FORMAT_R16G16B16A16_SINT:
        case DXGI_FORMAT_R32G32_TYPELESS:
        case DXGI_FORMAT_R32G32_FLOAT:
        case DXGI_FORMAT_R32G32_UINT:
        case DXGI_FORMAT_R32G32_SINT:
        case DXGI_FORMAT_R32G8X24_TYPELESS:
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
        case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
        case DXGI_FORMAT_Y416:
        case DXGI_FORMAT_Y210:
        case DXGI_FORMAT_Y216:
            return 64;

        case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        case DXGI_FORMAT_R10G10B10A2_UNORM:
        case DXGI_FORMAT_R10G10B10A2_UINT:
        case DXGI_FORMAT_R11G11B10_FLOAT:
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_R8G8B8A8_UINT:
        case DXGI_FORMAT_R8G8B8A8_SNORM:
        case DXGI_FORMAT_R8G8B8A8_SINT:
        case DXGI_FORMAT_R16G16_TYPELESS:
        case DXGI_FORMAT_R16G16_FLOAT:
        case DXGI_FORMAT_R16G16_UNORM:
        case DXGI_FORMAT_R16G16_UINT:
        case DXGI_FORMAT_R16G16_SNORM:
        case DXGI_FORMAT_R16G16_SINT:
        case DXGI_FORMAT_R32_TYPELESS:
        case DXGI_FORMAT_D32_FLOAT:
        case DXGI_FORMAT_R32_FLOAT:
        case DXGI_FORMAT_R32_UINT:
        case DXGI_FORMAT_R32_SINT:
        case DXGI_FORMAT_R24G8_TYPELESS:
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
        case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
        case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
        case DXGI_FORMAT_R8G8_B8G8_UNORM:
        case DXGI_FORMAT_G8R8_G8B8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8X8_UNORM:
        case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8X8_TYPELESS:
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        case DXGI_FORMAT_AYUV:
        case DXGI_FORMAT_Y410:
        case DXGI_FORMAT_YUY2:
            return 32;

        case DXGI_FORMAT_P010:
        case DXGI_FORMAT_P016:
            return 24;

        case DXGI_FORMAT_R8G8_TYPELESS:
        case DXGI_FORMAT_R8G8_UNORM:
        case DXGI_FORMAT_R8G8_UINT:
        case DXGI_FORMAT_R8G8_SNORM:
        case DXGI_FORMAT_R8G8_SINT:
        case DXGI_FORMAT_R16_TYPELESS:
        case DXGI_FORMAT_R16_FLOAT:
        case DXGI_FORMAT_D16_UNORM:
        case DXGI_FORMAT_R16_UNORM:
        case DXGI_FORMAT_R16_UINT:
        case DXGI_FORMAT_R16_SNORM:
        case DXGI_FORMAT_R16_SINT:
        case DXGI_FORMAT_B5G6R5_UNORM:
        case DXGI_FORMAT_B5G5R5A1_UNORM:
        case DXGI_FORMAT_A8P8:
        case DXGI_FORMAT_B4G4R4A4_UNORM:
            return 16;

        case DXGI_FORMAT_NV12:
        case DXGI_FORMAT_420_OPAQUE:
        case DXGI_FORMAT_NV11:
            return 12;

        case DXGI_FORMAT_R8_TYPELESS:
        case DXGI_FORMAT_R8_UNORM:
        case DXGI_FORMAT_R8_UINT:
        case DXGI_FORMAT_R8_SNORM:
        case DXGI_FORMAT_R8_SINT:
        case DXGI_FORMAT_A8_UNORM:
        case DXGI_FORMAT_AI44:
        case DXGI_FORMAT_IA44:
        case DXGI_FORMAT_P8:
            return 8;

        case DXGI_FORMAT_R1_UNORM:
            return 1;

        case DXGI_FORMAT_BC1_TYPELESS:
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC1_UNORM_SRGB:
        case DXGI_FORMAT_BC4_TYPELESS:
        case DXGI_FORMAT_BC4_UNORM:
        case DXGI_FORMAT_BC4_SNORM:
            return 4;

        case DXGI_FORMAT_BC2_TYPELESS:
        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC2_UNORM_SRGB:
        case DXGI_FORMAT_BC3_TYPELESS:
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC3_UNORM_SRGB:
        case DXGI_FORMAT_BC5_TYPELESS:
        case DXGI_FORMAT_BC5_UNORM:
        case DXGI_FORMAT_BC5_SNORM:
        case DXGI_FORMAT_BC6H_TYPELESS:
        case DXGI_FORMAT_BC6H_UF16:
        case DXGI_FORMAT_BC6H_SF16:
        case DXGI_FORMAT_BC7_TYPELESS:
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            return 8;

        default:
            ASSERT(0);
            return 0;
        }
    }

    void GetSurfaceInfo(size_t width, size_t height, DXGI_FORMAT fmt, size_t* outNumBytes, size_t* outRowBytes, size_t* outNumRows)
    {
        size_t numBytes = 0;
        size_t rowBytes = 0;
        size_t numRows = 0;

        bool bc = false;
        bool packed = false;
        bool planar = false;
        size_t bpe = 0;
        switch (fmt)
        {
        case DXGI_FORMAT_BC1_TYPELESS:
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC1_UNORM_SRGB:
        case DXGI_FORMAT_BC4_TYPELESS:
        case DXGI_FORMAT_BC4_UNORM:
        case DXGI_FORMAT_BC4_SNORM:
            bc = true;
            bpe = 8;
            break;

        case DXGI_FORMAT_BC2_TYPELESS:
        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC2_UNORM_SRGB:
        case DXGI_FORMAT_BC3_TYPELESS:
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC3_UNORM_SRGB:
        case DXGI_FORMAT_BC5_TYPELESS:
        case DXGI_FORMAT_BC5_UNORM:
        case DXGI_FORMAT_BC5_SNORM:
        case DXGI_FORMAT_BC6H_TYPELESS:
        case DXGI_FORMAT_BC6H_UF16:
        case DXGI_FORMAT_BC6H_SF16:
        case DXGI_FORMAT_BC7_TYPELESS:
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            bc = true;
            bpe = 16;
            break;

        case DXGI_FORMAT_R8G8_B8G8_UNORM:
        case DXGI_FORMAT_G8R8_G8B8_UNORM:
        case DXGI_FORMAT_YUY2:
            packed = true;
            bpe = 4;
            break;

        case DXGI_FORMAT_Y210:
        case DXGI_FORMAT_Y216:
            packed = true;
            bpe = 8;
            break;

        case DXGI_FORMAT_NV12:
        case DXGI_FORMAT_420_OPAQUE:
            planar = true;
            bpe = 2;
            break;

        case DXGI_FORMAT_P010:
        case DXGI_FORMAT_P016:
            planar = true;
            bpe = 4;
            break;
        default:
            assert(false);
            break;
        }

        if (bc)
        {
            size_t numBlocksWide = 0;
            if (width > 0)
            {
                numBlocksWide = std::max<size_t>(1, (width + 3) / 4);
            }
            size_t numBlocksHigh = 0;
            if (height > 0)
            {
                numBlocksHigh = std::max<size_t>(1, (height + 3) / 4);
            }
            rowBytes = numBlocksWide * bpe;
            numRows = numBlocksHigh;
            numBytes = rowBytes * numBlocksHigh;
        }
        else if (packed)
        {
            rowBytes = ((width + 1) >> 1) * bpe;
            numRows = height;
            numBytes = rowBytes * height;
        }
        else if (fmt == DXGI_FORMAT_NV11)
        {
            rowBytes = ((width + 3) >> 2) * 4;
            numRows = height * 2; // Direct3D makes this simplifying assumption, although it is larger than the 4:1:1 data
            numBytes = rowBytes * numRows;
        }
        else if (planar)
        {
            rowBytes = ((width + 1) >> 1) * bpe;
            numBytes = rowBytes * height + (rowBytes * (height + 1) >> 1);
            numRows = height + ((height + 1) >> 1);
        }
        else
        {
            size_t bpp = BitsPerPixel(fmt);
            rowBytes = (width * bpp + 7) / 8; // round up to nearest byte
            numRows = height;
            numBytes = rowBytes * height;
        }

        if (outNumBytes)
        {
            *outNumBytes = numBytes;
        }
        if (outRowBytes)
        {
            *outRowBytes = rowBytes;
        }
        if (outNumRows)
        {
            *outNumRows = numRows;
        }
    }

    HRESULT gfx::FillInitData(size_t width,
        size_t height,
        size_t depth,
        size_t mipCount,
        size_t arraySize,
        DXGI_FORMAT format,
        size_t maxsize,
        size_t bitSize,
        const uint8_t* bitData,
        size_t& twidth,
        size_t& theight,
        size_t& tdepth,
        size_t& skipMip,
        D3D12_SUBRESOURCE_DATA* initDataArray
    )
    {
        if (!bitData || !initDataArray)
        {
            return E_POINTER;
        }

        skipMip = 0;
        twidth = 0;
        theight = 0;
        tdepth = 0;

        size_t NumBytes = 0;
        size_t RowBytes = 0;
        const uint8_t* pSrcBits = bitData;
        const uint8_t* pEndBits = bitData + bitSize;

        size_t index = 0;
        for (size_t j = 0; j < arraySize; j++)
        {
            size_t w = width;
            size_t h = height;
            size_t d = depth;
            for (size_t i = 0; i < mipCount; i++)
            {
                GetSurfaceInfo(w,
                    h,
                    format,
                    &NumBytes,
                    &RowBytes,
                    nullptr
                );

                if (mipCount <= 1 || !maxsize || (w <= maxsize && h <= maxsize && d <= maxsize))
                {
                    if (!twidth)
                    {
                        twidth = w;
                        theight = h;
                        tdepth = d;
                    }

                    ASSERT(index < mipCount * arraySize);
                    initDataArray[index]./*pSysMem*/pData = static_cast<const void*>(pSrcBits);
                    initDataArray[index]./*SysMemPitch*/RowPitch = static_cast<uint32_t>(RowBytes);
                    initDataArray[index]./*SysMemSlicePitch*/SlicePitch = static_cast<uint32_t>(NumBytes);
                    ++index;
                }
                else if (!j)
                {
                    // Count number of skipped mipmaps (first item only)
                    ++skipMip;
                }

                if (pSrcBits + NumBytes * d > pEndBits)
                {
                    return HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
                }

                pSrcBits += NumBytes * d;

                w = w >> 1;
                h = h >> 1;
                d = d >> 1;
                if (w == 0)
                {
                    w = 1;
                }
                if (h == 0)
                {
                    h = 1;
                }
                if (d == 0)
                {
                    d = 1;
                }
            }
        }
        return index > 0 ? S_OK : E_FAIL;
    }

    uint64_t gfx::GetRequiredIntermediateSize(
        ID3D12Device* pD3dDevice,
        ID3D12Resource* pDestinationResource,
        uint32_t uFirstSubresource,
        uint32_t uNumSubresources)
    {
        uint64_t uRequiredSize = 0;
        D3D12_RESOURCE_DESC Desc = pDestinationResource->GetDesc();
        pD3dDevice->GetCopyableFootprints(&Desc, uFirstSubresource, uNumSubresources, 0, nullptr, nullptr, nullptr, &uRequiredSize);
        return uRequiredSize;
    }

    void MemcpyAllResource(
        _In_ const D3D12_MEMCPY_DEST* pDest,
        _In_ const D3D12_SUBRESOURCE_DATA* pSrc,
        SIZE_T RowSizeInBytes,
        uint32_t uNumRows,
        uint32_t uNumSlices,
        BOOL bAllChunck
    )
    {
        for (uint32_t z = 0; z < uNumSlices; ++z)
        {
            uint8_t* pDestSlice = static_cast<uint8_t*>(pDest->pData) + pDest->SlicePitch * z;
            const uint8_t* pSrcSlice = static_cast<const uint8_t*>(pSrc->pData) + pSrc->SlicePitch * z;

            if (bAllChunck)
            {
                ASSERT(RowSizeInBytes * uNumRows <= pDest->SlicePitch);
                memcpy(pDestSlice, pSrcSlice, RowSizeInBytes * uNumRows);
            }
            else
            {
                for (uint32_t y = 0; y < uNumRows; ++y)
                {
                    ASSERT(pDest->RowPitch * y + RowSizeInBytes <= pDest->SlicePitch);
                    memcpy(pDestSlice + pDest->RowPitch * y,
                        pSrcSlice + pSrc->RowPitch * y,
                        RowSizeInBytes);
                }
            }
        }
    }

    void MemcpySubresource(
        _In_ const D3D12_MEMCPY_DEST* pDest,
        _In_ const D3D12_SUBRESOURCE_DATA* pSrc,
        BOOL bAllChunck,
        const DX12SubTexCopyInfo& subInfo
    )
    {
        ASSERT(pDest && pSrc);

        /// 由于源数据可能只是一小块，所以大小和贴图是没有关系的，偏移要单独来算
        uint64_t RowSizeCopySrc = subInfo.xLength * subInfo.pixelSize;
        uint64_t DepthSizeCopySrc = subInfo.yLength * RowSizeCopySrc;

        for (uint32_t z = subInfo.zOffset; z < subInfo.zOffset + subInfo.zLength; ++z)
        {
            uint8_t* pDestSlice = static_cast<uint8_t*>(pDest->pData) + pDest->SlicePitch * z;
            const uint8_t* pSrcSlice = static_cast<const uint8_t*>(pSrc->pData) + DepthSizeCopySrc * (z - subInfo.zOffset);

            if (bAllChunck)
            {
                ASSERT(DepthSizeCopySrc * subInfo.zLength <= pDest->SlicePitch);
                memcpy(pDestSlice, pSrcSlice, RowSizeCopySrc * subInfo.yLength);
            }
            else
            {
                for (uint32_t y = subInfo.yOffset; y < subInfo.yOffset + subInfo.yLength; ++y)
                {
                    ASSERT(pDest->RowPitch * y + RowSizeCopySrc <= pDest->SlicePitch);
                    uint64_t destOffset = ((pDest->RowPitch * y) + ((subInfo.xOffset) * subInfo.pixelSize));
                    uint64_t srcOffset = RowSizeCopySrc * (y - subInfo.yOffset);
                    uint64_t RowSize = RowSizeCopySrc;
                    memcpy(pDestSlice + destOffset,
                        pSrcSlice + srcOffset,
                        RowSize);
                }
            }
        }
    }

    uint64_t UpdateSubresources(
        ID3D12GraphicsCommandList* pCmdList,
        ID3D12Resource* pDestinationResource,
        ID3D12Resource* pIntermediate,
        uint32_t FirstSubresource,
        uint32_t NumSubresources,
        uint64_t RequiredSize,
        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT* pLayouts,
        const uint32_t* pNumRows,
        const uint64_t* pRowSizesInBytes,
        const D3D12_SUBRESOURCE_DATA* pSrcData,
        uint32_t xoffset,
        uint32_t yoffset,
        uint32_t zoffset
    )
    {
        // Minor validation
        D3D12_RESOURCE_DESC IntermediateDesc = pIntermediate->GetDesc();
        D3D12_RESOURCE_DESC DestinationDesc = pDestinationResource->GetDesc();
        if (IntermediateDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
            IntermediateDesc.Width < RequiredSize + pLayouts[0].Offset ||
            RequiredSize > static_cast<SIZE_T>(-1) ||
            (DestinationDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
                (FirstSubresource != 0 || NumSubresources != 1)))
        {
            return 0;
        }

        BYTE* pData;
        HRESULT hr = pIntermediate->Map(0, nullptr, reinterpret_cast<void**>(&pData));
        if (FAILED(hr))
        {
            return 0;
        }

        uint32_t perPixelSize = static_cast<uint32_t>(BitsPerPixel(pLayouts[0].Footprint.Format)) / 8;
        BOOL bAllChunck = false;
        if (pLayouts[0].Footprint.RowPitch == perPixelSize * pLayouts[0].Footprint.Width)
        {
            bAllChunck = true;
        }

        for (uint32_t i = 0; i < NumSubresources; ++i)
        {
            if (pRowSizesInBytes[i] > static_cast<SIZE_T>(-1))
            {
                return 0;
            }
            D3D12_MEMCPY_DEST DestData = { pData + pLayouts[i].Offset, pLayouts[i].Footprint.RowPitch, pLayouts[i].Footprint.RowPitch * pNumRows[i] };
            DX12SubTexCopyInfo subInfo = {};
            subInfo.xOffset = 0;
            subInfo.yOffset = 0;
            subInfo.zOffset = 0;
            subInfo.xLength = pLayouts[i].Footprint.Width;
            subInfo.yLength = pNumRows[i];
            subInfo.zLength = pLayouts[i].Footprint.Depth;
            subInfo.pixelSize = perPixelSize;

            MemcpySubresource(&DestData, &pSrcData[i], bAllChunck, subInfo);
        }
        pIntermediate->Unmap(0, nullptr);

        if (DestinationDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        {
            //CD3DX12_BOX SrcBox(uint32_t(pLayouts[0].Offset), uint32_t(pLayouts[0].Offset + pLayouts[0].Footprint.Width));
            pCmdList->CopyBufferRegion(
                pDestinationResource, 0, pIntermediate, pLayouts[0].Offset, pLayouts[0].Footprint.Width);
        }
        else
        {
            for (uint32_t i = 0; i < NumSubresources; ++i)
            {
                CD3DX12_TEXTURE_COPY_LOCATION Dst(pDestinationResource, i + FirstSubresource);
                CD3DX12_TEXTURE_COPY_LOCATION Src(pIntermediate, pLayouts[i]);
                pCmdList->CopyTextureRegion(&Dst, xoffset, yoffset, zoffset, &Src, nullptr);
            }
        }
        return RequiredSize;
    }


    uint64_t UpdateSubresources(
        ID3D12Device* pD3dDevice,
        ID3D12GraphicsCommandList* pCmdList,
        ID3D12Resource* pDestinationResource,
        ID3D12Resource* pIntermediate,
        uint64_t IntermediateOffset,
        uint32_t FirstSubresource,
        uint32_t NumSubresources,
        const D3D12_SUBRESOURCE_DATA* pSrcData,
        uint32_t xoffset,
        uint32_t yoffset,
        uint32_t zoffset
    )
    {
        uint64_t uResult = 0;
        if (NumSubresources <= 10)
        {
            //量少的话，就不走堆内存，走栈效率高
            constexpr uint32_t size = (sizeof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT) + sizeof(uint64_t) + sizeof(uint64_t)) * 10;
            uint8_t pMem[size];
            uint64_t RequiredSize = 0;
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT* pLayouts = reinterpret_cast<D3D12_PLACED_SUBRESOURCE_FOOTPRINT*>(pMem);
            uint64_t* pRowSizesInBytes = reinterpret_cast<UINT64*>(pLayouts + NumSubresources);
            uint32_t* pNumRows = reinterpret_cast<uint32_t*>(pRowSizesInBytes + NumSubresources);

            D3D12_RESOURCE_DESC Desc = pDestinationResource->GetDesc();
            pD3dDevice->GetCopyableFootprints(&Desc, FirstSubresource, NumSubresources, IntermediateOffset, pLayouts, pNumRows, pRowSizesInBytes, &RequiredSize);
            uResult = UpdateSubresources(pCmdList, pDestinationResource, pIntermediate, FirstSubresource, NumSubresources, RequiredSize, pLayouts, pNumRows, pRowSizesInBytes, pSrcData, xoffset, yoffset, zoffset);
        }
        else
        {
            uint64_t uMemToAlloc = (sizeof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT) + sizeof(uint64_t) + sizeof(uint64_t)) * NumSubresources;
            void* pMem = malloc(uMemToAlloc);
            if (pMem)
            {
                uint64_t RequiredSize = 0;
                D3D12_PLACED_SUBRESOURCE_FOOTPRINT* pLayouts = static_cast<D3D12_PLACED_SUBRESOURCE_FOOTPRINT*>(pMem);
                uint64_t* pRowSizesInBytes = reinterpret_cast<UINT64*>(pLayouts + NumSubresources);
                uint32_t* pNumRows = reinterpret_cast<uint32_t*>(pRowSizesInBytes + NumSubresources);

                D3D12_RESOURCE_DESC Desc = pDestinationResource->GetDesc();
                pD3dDevice->GetCopyableFootprints(&Desc, FirstSubresource, NumSubresources, IntermediateOffset, pLayouts, pNumRows, pRowSizesInBytes, &RequiredSize);
                uResult = UpdateSubresources(pCmdList, pDestinationResource, pIntermediate, FirstSubresource, NumSubresources, RequiredSize, pLayouts, pNumRows, pRowSizesInBytes, pSrcData, xoffset, yoffset, zoffset);
                free(pMem);
            }
        }
        return uResult;
    }


    uint32_t CalcConstantBufferByteSize(uint32_t byteSize)
    {
        // Constant buffers must be a multiple of the minimum hardware
        // allocation size (usually 256 bytes).  So round up to nearest
        // multiple of 256.  We do this by adding 255 and then masking off
        // the lower 2 bytes which store all bits < 256.
        // Example: Suppose byteSize = 300.
        // (300 + 255) & ~255
        // 555 & ~255
        // 0x022B & ~0x00ff
        // 0x022B & 0xff00
        // 0x0200
        // 512
        return byteSize + 255 & ~255;
    }


    KD3DX12_RESOURCE_DESC1::KD3DX12_RESOURCE_DESC1() = default;

    KD3DX12_RESOURCE_DESC1::KD3DX12_RESOURCE_DESC1(const D3D12_RESOURCE_DESC1& o) noexcept :
        D3D12_RESOURCE_DESC1(o)
    {
    }

    KD3DX12_RESOURCE_DESC1::KD3DX12_RESOURCE_DESC1(const D3D12_RESOURCE_DESC& o) noexcept
    {
        Dimension = o.Dimension;
        Alignment = o.Alignment;
        Width = o.Width;
        Height = o.Height;
        DepthOrArraySize = o.DepthOrArraySize;
        MipLevels = o.MipLevels;
        Format = o.Format;
        SampleDesc = o.SampleDesc;
        Layout = o.Layout;
        Flags = o.Flags;
        SamplerFeedbackMipRegion = {};
    }

    KD3DX12_RESOURCE_DESC1::KD3DX12_RESOURCE_DESC1(D3D12_RESOURCE_DIMENSION dimension, UINT64 alignment, UINT64 width, UINT height, UINT16 depthOrArraySize, UINT16 mipLevels, DXGI_FORMAT format, UINT sampleCount, UINT sampleQuality, D3D12_TEXTURE_LAYOUT layout, D3D12_RESOURCE_FLAGS flags,
        UINT samplerFeedbackMipRegionWidth, UINT samplerFeedbackMipRegionHeight, UINT samplerFeedbackMipRegionDepth) noexcept
    {
        Dimension = dimension;
        Alignment = alignment;
        Width = width;
        Height = height;
        DepthOrArraySize = depthOrArraySize;
        MipLevels = mipLevels;
        Format = format;
        SampleDesc.Count = sampleCount;
        SampleDesc.Quality = sampleQuality;
        Layout = layout;
        Flags = flags;
        SamplerFeedbackMipRegion.Width = samplerFeedbackMipRegionWidth;
        SamplerFeedbackMipRegion.Height = samplerFeedbackMipRegionHeight;
        SamplerFeedbackMipRegion.Depth = samplerFeedbackMipRegionDepth;
    }

    KD3DX12_RESOURCE_DESC1 KD3DX12_RESOURCE_DESC1::Buffer(const D3D12_RESOURCE_ALLOCATION_INFO& resAllocInfo, D3D12_RESOURCE_FLAGS flags) noexcept
    {
        return KD3DX12_RESOURCE_DESC1(D3D12_RESOURCE_DIMENSION_BUFFER, resAllocInfo.Alignment, resAllocInfo.SizeInBytes,
            1, 1, 1, DXGI_FORMAT_UNKNOWN, 1, 0, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, flags, 0, 0, 0);
    }

    KD3DX12_RESOURCE_DESC1 KD3DX12_RESOURCE_DESC1::Buffer(UINT64 width, D3D12_RESOURCE_FLAGS flags, UINT64 alignment) noexcept
    {
        return KD3DX12_RESOURCE_DESC1(D3D12_RESOURCE_DIMENSION_BUFFER, alignment, width, 1, 1, 1,
            DXGI_FORMAT_UNKNOWN, 1, 0, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, flags, 0, 0, 0);
    }

    KD3DX12_RESOURCE_DESC1 KD3DX12_RESOURCE_DESC1::Tex1D(DXGI_FORMAT format, UINT64 width, UINT16 arraySize, UINT16 mipLevels, D3D12_RESOURCE_FLAGS flags, D3D12_TEXTURE_LAYOUT layout, UINT64 alignment) noexcept
    {
        return KD3DX12_RESOURCE_DESC1(D3D12_RESOURCE_DIMENSION_TEXTURE1D, alignment, width, 1, arraySize,
            mipLevels, format, 1, 0, layout, flags, 0, 0, 0);
    }

    KD3DX12_RESOURCE_DESC1 KD3DX12_RESOURCE_DESC1::Tex2D(DXGI_FORMAT format, UINT64 width, UINT height, UINT16 arraySize, UINT16 mipLevels, UINT sampleCount, UINT sampleQuality, D3D12_RESOURCE_FLAGS flags, D3D12_TEXTURE_LAYOUT layout, UINT64 alignment, UINT samplerFeedbackMipRegionWidth,
        UINT samplerFeedbackMipRegionHeight, UINT samplerFeedbackMipRegionDepth) noexcept
    {
        return KD3DX12_RESOURCE_DESC1(D3D12_RESOURCE_DIMENSION_TEXTURE2D, alignment, width, height, arraySize,
            mipLevels, format, sampleCount, sampleQuality, layout, flags, samplerFeedbackMipRegionWidth,
            samplerFeedbackMipRegionHeight, samplerFeedbackMipRegionDepth);
    }

    KD3DX12_RESOURCE_DESC1 KD3DX12_RESOURCE_DESC1::Tex3D(DXGI_FORMAT format, UINT64 width, UINT height, UINT16 depth, UINT16 mipLevels, D3D12_RESOURCE_FLAGS flags, D3D12_TEXTURE_LAYOUT layout, UINT64 alignment) noexcept
    {
        return KD3DX12_RESOURCE_DESC1(D3D12_RESOURCE_DIMENSION_TEXTURE3D, alignment, width, height, depth,
            mipLevels, format, 1, 0, layout, flags, 0, 0, 0);
    }

    UINT16 KD3DX12_RESOURCE_DESC1::Depth() const noexcept
    {
        return Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? DepthOrArraySize : 1u;
    }

    UINT16 KD3DX12_RESOURCE_DESC1::ArraySize() const noexcept
    {
        return Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D ? DepthOrArraySize : 1u;
    }

    UINT8 KD3DX12_RESOURCE_DESC1::PlaneCount(ID3D12Device* pDevice) const noexcept
    {
        return D3D12GetFormatPlaneCount(pDevice, Format);
    }

    UINT KD3DX12_RESOURCE_DESC1::Subresources(ID3D12Device* pDevice) const noexcept
    {
        return static_cast<UINT>(MipLevels) * ArraySize() * PlaneCount(pDevice);
    }

    UINT KD3DX12_RESOURCE_DESC1::CalcSubresource(UINT MipSlice, UINT ArraySlice, UINT PlaneSlice) const noexcept
    {
        return D3D12CalcSubresource(MipSlice, ArraySlice, PlaneSlice, MipLevels, ArraySize());
    }

    D3D12_RESOURCE_DESC KD3DX12_RESOURCE_DESC1::ToDesc() const noexcept
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = Dimension;
        desc.Alignment = Alignment;
        desc.Width = Width;
        desc.Height = Height;
        desc.DepthOrArraySize = DepthOrArraySize;
        desc.MipLevels = MipLevels;
        desc.Format = Format;
        desc.SampleDesc = SampleDesc;
        desc.Layout = Layout;
        desc.Flags = Flags;
        return desc;
    }

    bool operator==(const D3D12_RESOURCE_DESC1& l, const D3D12_RESOURCE_DESC1& r) noexcept
    {
        return l.Dimension == r.Dimension &&
            l.Alignment == r.Alignment &&
            l.Width == r.Width &&
            l.Height == r.Height &&
            l.DepthOrArraySize == r.DepthOrArraySize &&
            l.MipLevels == r.MipLevels &&
            l.Format == r.Format &&
            l.SampleDesc.Count == r.SampleDesc.Count &&
            l.SampleDesc.Quality == r.SampleDesc.Quality &&
            l.Layout == r.Layout &&
            l.Flags == r.Flags &&
            l.SamplerFeedbackMipRegion.Width == r.SamplerFeedbackMipRegion.Width &&
            l.SamplerFeedbackMipRegion.Height == r.SamplerFeedbackMipRegion.Height &&
            l.SamplerFeedbackMipRegion.Depth == r.SamplerFeedbackMipRegion.Depth;
    }

    bool operator!=(const D3D12_RESOURCE_DESC1& l, const D3D12_RESOURCE_DESC1& r) noexcept
    {
        return !(l == r);
    }

    D3D12_RESOURCE_DIMENSION GetTexDimension(uint32_t uDepth, uint32_t uHeight)
    {
        if (uDepth > 1)
        {
            if (uHeight > 1)
            {
                return D3D12_RESOURCE_DIMENSION_TEXTURE3D;
            }
            assert(false);
            return D3D12_RESOURCE_DIMENSION_UNKNOWN;
        }
        if (uHeight > 1)
        {
            return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        }
        return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
    }

    //uint32_t GetSubresourceIndex(uint32_t mipSlice, uint32_t arraySlice, uint32_t planeSlice, uint32_t arraySize, uint32_t mipLevels)
    //{
    //    uint32_t res = mipSlice + arraySlice * mipLevels + planeSlice * arraySize * mipLevels;
    //    assert(res < 0xff);
    //    return res;
    //}

    //uint32_t GetSubresourceIndex(SubresourceRange subRange, KGFX_TextureImplDx12* ptex)
    //{
    //    if (subRange == kEntireTexture)
    //    {
    //        return D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    //    }

    //    if (subRange.baseArrayLayer == 0 && subRange.layerCount == ptex->GetDesc()->uArraySize && subRange.mipLevel == 0 && subRange.mipLevelCount == ptex->GetDesc()->uMipLevels)
    //    {
    //        return D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    //    }

    //    return GetSubresourceIndex(subRange.mipLevel, subRange.baseArrayLayer, 0, subRange.layerCount, subRange.mipLevelCount);
    //}

    ResourceViewDimension GetDXDimensionToGfx(D3D12_RESOURCE_DIMENSION desc, bool bCube)
    {
        switch (desc)
        {
        case D3D12_RESOURCE_DIMENSION_UNKNOWN:
            return ResourceViewDimension::RESOURCE_DIMENSION_UNKNOWN;
        case D3D12_RESOURCE_DIMENSION_BUFFER:
            return ResourceViewDimension::RESOURCE_DIMENSION_BUFFER;
        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
            return ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE1D;
        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
            if (bCube)
            {
                return ResourceViewDimension::RESOURCE_DIMENSION_TEXTURECUBE;
            }
            return ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D;
        case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
            return ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE3D;
        default:
            return ResourceViewDimension::RESOURCE_DIMENSION_UNKNOWN;
        }
    }

    D3D12_BLEND GetDxBlendFactor(enumBlendType blendType)
    {
        D3D12_BLEND factor;
        switch (blendType)
        {
        case BLEND_ZERO:
            factor = D3D12_BLEND_ZERO;
            break;
        case BLEND_ONE:
            factor = D3D12_BLEND_ONE;
            break;
        case BLEND_SRC_COLOR:
            factor = D3D12_BLEND_SRC_COLOR;
            break;
        case BLEND_ONE_MINUS_SRC_COLOR:
            factor = D3D12_BLEND_INV_SRC_COLOR;
            break;
        case BLEND_DST_COLOR:
            factor = D3D12_BLEND_DEST_COLOR;
            break;
        case BLEND_ONE_MINUS_DST_COLOR:
            factor = D3D12_BLEND_INV_DEST_COLOR;
            break;
        case BLEND_SRC_ALPHA:
            factor = D3D12_BLEND_SRC_ALPHA;
            break;
        case BLEND_ONE_MINUS_SRC_ALPHA:
            factor = D3D12_BLEND_INV_SRC_ALPHA;
            break;
        case BLEND_DST_ALPHA:
            factor = D3D12_BLEND_DEST_ALPHA;
            break;
        case BLEND_ONE_MINUS_DST_ALPHA:
            factor = D3D12_BLEND_INV_DEST_ALPHA;
            break;
        case BLEND_CONSTANT_COLOR:
            factor = D3D12_BLEND_BLEND_FACTOR;
            break;
        case BLEND_ONE_MINUS_CONSTANT_COLOR:
            factor = D3D12_BLEND_INV_BLEND_FACTOR;
            break;
        case BLEND_CONSTANT_ALPHA:
            factor = D3D12_BLEND_BLEND_FACTOR;
            break;
        case BLEND_ONE_MINUS_CONSTANT_ALPHA:
            factor = D3D12_BLEND_INV_BLEND_FACTOR;
            break;
        case BLEND_SRC_ALPHA_SATURATE:
            factor = D3D12_BLEND_SRC_ALPHA_SAT;
            break;
        default:
            factor = D3D12_BLEND_ONE;
            break;
        }
        return factor;
    }

    D3D12_COMPARISON_FUNC GetDxDepthCompareOp(enumDepthType cp)
    {
        D3D12_COMPARISON_FUNC op;
        switch (cp)
        {
        case DEPTH_TEST_LESS:
            op = D3D12_COMPARISON_FUNC_LESS;
            break;
        case DEPTH_TEST_LEQUAL:
            op = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            break;
        case DEPTH_TEST_EQUAL:
            op = D3D12_COMPARISON_FUNC_EQUAL;
            break;
        case DEPTH_TEST_GEQUAL:
            op = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
            break;
        case DEPTH_TEST_GREATER:
            op = D3D12_COMPARISON_FUNC_GREATER;
            break;
        case DEPTH_TEST_NOTEQUAL:
            op = D3D12_COMPARISON_FUNC_NOT_EQUAL;
            break;
        case DEPTH_TEST_NEVER:
            op = D3D12_COMPARISON_FUNC_NEVER;
            break;
        case DEPTH_TEST_ALWAYS:
            op = D3D12_COMPARISON_FUNC_ALWAYS;
            break;
        default:
            op = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            break;
        }
        return op;
    }

    D3D12_COMPARISON_FUNC GetDxStencilCompareOp(enumStencilType st)
    {
        D3D12_COMPARISON_FUNC op;
        switch (st)
        {
        case STENCIL_TEST_LESS:
            op = D3D12_COMPARISON_FUNC_LESS;
            break;
        case STENCIL_TEST_LEQUAL:
            op = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            break;
        case STENCIL_TEST_EQUAL:
            op = D3D12_COMPARISON_FUNC_EQUAL;
            break;
        case STENCIL_TEST_GEQUAL:
            op = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
            break;
        case STENCIL_TEST_GREATER:
            op = D3D12_COMPARISON_FUNC_GREATER;
            break;
        case STENCIL_TEST_NOTEQUAL:
            op = D3D12_COMPARISON_FUNC_NOT_EQUAL;
            break;
        case STENCIL_TEST_NEVER:
            op = D3D12_COMPARISON_FUNC_NEVER;
            break;
        case STENCIL_TEST_ALWAYS:
            op = D3D12_COMPARISON_FUNC_ALWAYS;
            break;
        default:
            op = D3D12_COMPARISON_FUNC_ALWAYS;
            break;
        }
        return op;
    }

    D3D12_STENCIL_OP GetDxStencilOp(enumStencilOpType p)
    {
        D3D12_STENCIL_OP op = D3D12_STENCIL_OP_KEEP;
        switch (p)
        {
        case STENCIL_OP_KEEP:
            op = D3D12_STENCIL_OP_KEEP;
            break;
        case STENCIL_OP_ZERO:
            op = D3D12_STENCIL_OP_ZERO;
            break;
        case STENCIL_OP_REPLACE:
            op = D3D12_STENCIL_OP_REPLACE;
            break;
        case STENCIL_OP_INCREMENT_AND_CLAMP:
            op = D3D12_STENCIL_OP_INCR_SAT;
            break;
        case STENCIL_OP_DECREMENT_AND_CLAMP:
            op = D3D12_STENCIL_OP_DECR_SAT;
            break;
        case STENCIL_OP_INVERT:
            op = D3D12_STENCIL_OP_INVERT;
            break;
        case STENCIL_OP_INCREMENT_AND_WRAP:
            op = D3D12_STENCIL_OP_INCR;
            break;
        case STENCIL_OP_DECREMENT_AND_WRAP:
            op = D3D12_STENCIL_OP_DECR;
            break;
        default:
            break;
        }
        return op;
    }

    D3D12_RESOURCE_STATES GetDxResourceLayout(BufferUsageFlags usage)
    {
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;

        /*   switch (usage)
           {
           case BUFFER_USAGE_TRANSFER_SRC_BIT:
               state |= D3D12_RESOURCE_STATE_COPY_SOURCE;
               break;
           case BUFFER_USAGE_TRANSFER_DST_BIT:
               state |= D3D12_RESOURCE_STATE_COPY_DEST;
               break;
           case BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT:
               state |= (D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
               break;
           case BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT:
               state |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
               break;
           case BUFFER_USAGE_UNIFORM_BUFFER_BIT:
               state |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
               break;
           case BUFFER_USAGE_STORAGE_BUFFER_BIT:
               state |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
               break;
           case BUFFER_USAGE_INDEX_BUFFER_BIT:
               state |= D3D12_RESOURCE_STATE_INDEX_BUFFER;
               break;
           case BUFFER_USAGE_VERTEX_BUFFER_BIT:
               state |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
               break;
           case BUFFER_USAGE_INDIRECT_BUFFER_BIT:
               state |= D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
               break;
           case BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR:
               state |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
               break;
           case BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR:
               state |= D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
               break;
           case BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR:
               break;
           case BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT:
               break;
           case BUFFER_USAGE_FLAG_BITS_MAX_ENUM:
               break;
           default: ;
           }*/

        if (usage & BUFFER_USAGE_TRANSFER_SRC_BIT)
        {
            state = D3D12_RESOURCE_STATE_COPY_SOURCE;
        }

        if (usage & BUFFER_USAGE_TRANSFER_DST_BIT)
        {
            state = D3D12_RESOURCE_STATE_COPY_DEST;
        }

        if (usage & BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT)
        {
            state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        }

        if (usage & BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT)
        {
            state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }

        if (usage & BUFFER_USAGE_UNIFORM_BUFFER_BIT)
        {
            state = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        }

        if (usage & BUFFER_USAGE_STORAGE_BUFFER_BIT)
        {
            state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }

        if (usage & BUFFER_USAGE_INDEX_BUFFER_BIT)
        {
            state = D3D12_RESOURCE_STATE_INDEX_BUFFER;
        }

        if (usage & BUFFER_USAGE_VERTEX_BUFFER_BIT)
        {
            state = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        }

        if (usage & BUFFER_USAGE_INDIRECT_BUFFER_BIT)
        {
            state = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        }

        if (usage & BUFFER_USAGE_INDIRECT_BUFFER_BIT)
        {
            state = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        }

        if (usage & BUFFER_USAGE_INDIRECT_BUFFER_BIT)
        {
            state = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        }


        return state;
    }

    D3D12_FILTER_TYPE TranslateFilterMode(enumSamplerFilter mode)
    {
        switch (mode)
        {
        case FILTER_NEAREST:
            return D3D12_FILTER_TYPE::D3D12_FILTER_TYPE_POINT;
        case FILTER_LINEAR:
            return D3D12_FILTER_TYPE::D3D12_FILTER_TYPE_LINEAR;
        case FILTER_CUBIC_IMG:
        default:
            assert(false);
            return {};
        }
    }

    D3D12_FILTER_TYPE TranslateMipFilterMode(enumMipMapMode mode)
    {
        switch (mode)
        {
        case SAMPLER_MIPMAP_MODE_NEAREST:
            return D3D12_FILTER_TYPE::D3D12_FILTER_TYPE_POINT;
        case SAMPLER_MIPMAP_MODE_LINEAR:
            return D3D12_FILTER_TYPE::D3D12_FILTER_TYPE_LINEAR;
        default:
            assert(false);
            return {};
        }
    }

    D3D12_TEXTURE_ADDRESS_MODE TranslateAddressingMode(enumSamplerAddressMode mode)
    {
        switch (mode)
        {
        case SAMPLER_ADDRESS_MODE_REPEAT:
            return D3D12_TEXTURE_ADDRESS_MODE::D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        case SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
            return D3D12_TEXTURE_ADDRESS_MODE::D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        case SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
            return D3D12_TEXTURE_ADDRESS_MODE::D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        case SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
            return D3D12_TEXTURE_ADDRESS_MODE::D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        case SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE:
            return D3D12_TEXTURE_ADDRESS_MODE::D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
        default:
            assert(FALSE);
            return {};
        }
    }

    D3D12_COMPARISON_FUNC TranslateComparisonFunc(enumSamplerCompareFunc func)
    {
        switch (func)
        {
        case SAMPLER_COMPARE_OP_NEVER:
            return D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_NEVER;
        case SAMPLER_COMPARE_OP_LESS:
            return D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_LESS;
        case SAMPLER_COMPARE_OP_EQUAL:
            return D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_EQUAL;
        case SAMPLER_COMPARE_OP_LESS_OR_EQUAL:
            return D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case SAMPLER_COMPARE_OP_GREATER:
            return D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_GREATER;
        case SAMPLER_COMPARE_OP_NOT_EQUAL:
            return D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case SAMPLER_COMPARE_OP_GREATER_OR_EQUAL:
            return D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case SAMPLER_COMPARE_OP_ALWAYS:
            return D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_ALWAYS;
        default:
            assert(false);
            return {};
        }
    }

    D3D12_FILTER_REDUCTION_TYPE TranslateFilterReduction(enumTextureReductionOp op)
    {
        switch (op)
        {
        case enumTextureReductionOp::Average:
            return D3D12_FILTER_REDUCTION_TYPE::D3D12_FILTER_REDUCTION_TYPE_STANDARD;
        case enumTextureReductionOp::Comparison:
            return D3D12_FILTER_REDUCTION_TYPE::D3D12_FILTER_REDUCTION_TYPE_COMPARISON;
        case enumTextureReductionOp::Minimum:
            return D3D12_FILTER_REDUCTION_TYPE::D3D12_FILTER_REDUCTION_TYPE_MINIMUM;
        case enumTextureReductionOp::Maximum:
            return  D3D12_FILTER_REDUCTION_TYPE::D3D12_FILTER_REDUCTION_TYPE_MAXIMUM;
        default:
            assert(false);
            return {};
        }
    }

    std::array<float, 4> TranslateBorderColor(enumBorderColor color)
    {
        std::array<float, 4> resColor = {};
        switch (color)
        {
        case BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
            resColor = { 0,0,0,0 };
            break;
        case BORDER_COLOR_INT_TRANSPARENT_BLACK:
            resColor = { 0,0,0,0 };
            break;
        case BORDER_COLOR_FLOAT_OPAQUE_BLACK:
            resColor = { 0,0,0,1 };
            break;
        case BORDER_COLOR_INT_OPAQUE_BLACK:
            resColor = { 0,0,0,0 };
            break;
        case BORDER_COLOR_FLOAT_OPAQUE_WHITE:
            resColor = { 1,1,1,1 };
            break;
        case BORDER_COLOR_INT_OPAQUE_WHITE:
            resColor = { 1,1,1,1 };
            break;
        default:
            assert(false);
        }
        return  resColor;
    }
}
#endif
