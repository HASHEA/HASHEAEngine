#include "../IGFX_Private.h"
#include "KVulkanTexture.h"
#include "KVulkanInitializers.h"
#include "GFXVulkan.h"
#include "KGraphicDevice.h"
#include "KVulkanDevice.h"
#include "KGFX_GraphicDeviceVK.h"
#include "KVulkanGraphicDevice.h"
#include "KVulkanBindlessManager.h"
#include "KEnginePub/Public/IKEnginePerformance.h"
#include "KEnginePub/Public/KProfileTools.h"
#include "KBase/Public/KMemLeak.h"

namespace gfx
{
    VkSampleCountFlagBits GetSamplerCount(enumSampleCountFlag sampleCount);

    KGfxTextureFormatInfo g_gfxTextureFormatInfo[] =
        {
            // enumTextureFormat			    uBytesPerBlock	uWidthPerBlock  uHeightPerBlock uHasAlpha	VkFormat								VkFormat(SRGB)
            {TEX_FORMAT_NONE,                      0u,  0u, 0u, 0u, VK_FORMAT_UNDEFINED,                 VK_FORMAT_UNDEFINED               },

            {TEX_FORMAT_R8G8B8A8_UNORM,            4u,  1u, 1u, 1u, VK_FORMAT_R8G8B8A8_UNORM,            VK_FORMAT_R8G8B8A8_SRGB           },
            {TEX_FORMAT_R8G8B8A8_SNORM,            4u,  1u, 1u, 1u, VK_FORMAT_R8G8B8A8_SNORM,            VK_FORMAT_R8G8B8A8_SRGB           },
            {TEX_FORMAT_R8G8B8A8_SRGB,             4u,  1u, 1u, 1u, VK_FORMAT_R8G8B8A8_SRGB,             VK_FORMAT_R8G8B8A8_SRGB           },
            {TEX_FORMAT_R8G8B8A8_UINT,             4u,  1u, 1u, 1u, VK_FORMAT_R8G8B8A8_UINT,             VK_FORMAT_UNDEFINED               },

            {TEX_FORMAT_R8_UNORM,                  1u,  1u, 1u, 0u, VK_FORMAT_R8_UNORM,                  VK_FORMAT_R8_SRGB                 },
            {TEX_FORMAT_R8G8_UNORM,                2u,  1u, 1u, 0u, VK_FORMAT_R8G8_UNORM,                VK_FORMAT_R8G8_SRGB               },
            {TEX_FORMAT_R16G16_UINT,               4u,  1u, 1u, 0u, VK_FORMAT_R16G16_UINT,               VK_FORMAT_UNDEFINED               },
            {TEX_FORMAT_B8G8R8_UNORM,              3u,  1u, 1u, 0u, VK_FORMAT_B8G8R8_UNORM,              VK_FORMAT_B8G8R8_SRGB             },
            {TEX_FORMAT_R8G8B8_UNORM,              3u,  1u, 1u, 0u, VK_FORMAT_R8G8B8_UNORM,              VK_FORMAT_R8G8B8_SRGB             },

            {TEX_FORMAT_B8G8R8A8_UNORM,            4u,  1u, 1u, 1u, VK_FORMAT_B8G8R8A8_UNORM,            VK_FORMAT_B8G8R8A8_SRGB           },
            {TEX_FORMAT_B8G8R8A8_SRGB,             4u,  1u, 1u, 1u, VK_FORMAT_B8G8R8A8_SRGB,             VK_FORMAT_B8G8R8A8_SRGB           },
            {TEX_FORMAT_R16G16B16A16_UNORM,        8u,  1u, 1u, 1u, VK_FORMAT_R16G16B16A16_UNORM,        VK_FORMAT_UNDEFINED               },

            {TEX_FORMAT_R16G16B16A16_SFLOAT,       8u,  1u, 1u, 1u, VK_FORMAT_R16G16B16A16_SFLOAT,       VK_FORMAT_UNDEFINED               },
            {TEX_FORMAT_R32G32B32A32_SFLOAT,       16u, 1u, 1u, 1u, VK_FORMAT_R32G32B32A32_SFLOAT,       VK_FORMAT_UNDEFINED               },
            {TEX_FORMAT_R16_SFLOAT,                2u,  1u, 1u, 0u, VK_FORMAT_R16_SFLOAT,                VK_FORMAT_UNDEFINED               },
            {TEX_FORMAT_R16_UINT,                  2u,  1u, 1u, 0u, VK_FORMAT_R16_UINT,                  VK_FORMAT_UNDEFINED               },

            {TEX_FORMAT_R16G16_SFLOAT,             4u,  1u, 1u, 0u, VK_FORMAT_R16G16_SFLOAT,             VK_FORMAT_UNDEFINED               },
            {TEX_FORMAT_R32_SINT,                  4u,  1u, 1u, 0u, VK_FORMAT_R32_SINT,                  VK_FORMAT_UNDEFINED               },
            {TEX_FORMAT_R32_UINT,                  4u,  1u, 1u, 0u, VK_FORMAT_R32_UINT,                  VK_FORMAT_UNDEFINED               },

            {TEX_FORMAT_R32_FLOAT,                 4u,  1u, 1u, 0u, VK_FORMAT_R32_SFLOAT,                VK_FORMAT_UNDEFINED               },
            {TEX_FORMAT_D24_UNORM_S8_UINT,         4u,  1u, 1u, 0u, VK_FORMAT_D24_UNORM_S8_UINT,         VK_FORMAT_UNDEFINED               },
            {TEX_FORMAT_D16_UNORM,                 2u,  1u, 1u, 0u, VK_FORMAT_D16_UNORM,                 VK_FORMAT_UNDEFINED               },
            {TEX_FORMAT_D32_SFLOAT,                4u,  1u, 1u, 0u, VK_FORMAT_D32_SFLOAT,                VK_FORMAT_UNDEFINED               },
            {TEX_FORMAT_D32_SFLOAT_S8_UINT,        8u,  1u, 1u, 0u, VK_FORMAT_D32_SFLOAT_S8_UINT,        VK_FORMAT_UNDEFINED               }, //  24-bits that are unused

            {TEX_FORMAT_R64_UINT,                  8u,  1u, 1u, 0u, VK_FORMAT_R64_UINT,                  VK_FORMAT_UNDEFINED               },

            {TEX_FORMAT_BC1_RGB_UNORM,             8u,  4u, 4u, 0u, VK_FORMAT_BC1_RGB_UNORM_BLOCK,       VK_FORMAT_BC1_RGB_SRGB_BLOCK      },
            {TEX_FORMAT_BC1_RGBA_UNORM,            8u,  4u, 4u, 0u, VK_FORMAT_BC1_RGBA_UNORM_BLOCK,      VK_FORMAT_BC1_RGBA_SRGB_BLOCK     },
            {TEX_FORMAT_BC2_UNORM,                 16u, 4u, 4u, 0u, VK_FORMAT_BC2_UNORM_BLOCK,           VK_FORMAT_BC2_SRGB_BLOCK          },
            {TEX_FORMAT_BC3_UNORM,                 16u, 4u, 4u, 1u, VK_FORMAT_BC3_UNORM_BLOCK,           VK_FORMAT_BC3_SRGB_BLOCK          },

            {TEX_FORMAT_BC4_UNORM,                 8u,  4u, 4u, 0u, VK_FORMAT_BC4_UNORM_BLOCK,           VK_FORMAT_UNDEFINED               },
            {TEX_FORMAT_BC4_SNORM,                 8u,  4u, 4u, 0u, VK_FORMAT_BC4_SNORM_BLOCK,           VK_FORMAT_UNDEFINED               },
            {TEX_FORMAT_BC5_UNORM,                 16u, 4u, 4u, 0u, VK_FORMAT_BC5_UNORM_BLOCK,           VK_FORMAT_UNDEFINED               },
            {TEX_FORMAT_BC5_SNORM,                 16u, 4u, 4u, 0u, VK_FORMAT_BC5_SNORM_BLOCK,           VK_FORMAT_UNDEFINED               },
            {TEX_FORMAT_BC6H_UFLOAT,               16u, 4u, 4u, 0u, VK_FORMAT_BC6H_UFLOAT_BLOCK,         VK_FORMAT_UNDEFINED               },
            {TEX_FORMAT_BC6H_SFLOAT,               16u, 4u, 4u, 0u, VK_FORMAT_BC6H_SFLOAT_BLOCK,         VK_FORMAT_UNDEFINED               },
            {TEX_FORMAT_BC7_UNORM,                 16u, 4u, 4u, 1u, VK_FORMAT_BC7_UNORM_BLOCK,           VK_FORMAT_BC7_SRGB_BLOCK          },
            {TEX_FORMAT_BC7_SRGB_UNORM,            16u, 4u, 4u, 1u, VK_FORMAT_BC7_SRGB_BLOCK,            VK_FORMAT_BC7_SRGB_BLOCK          },

            {TEX_FORMAT_B5G6R5_UNORM_PACK16,       2u,  1u, 1u, 0u, VK_FORMAT_B5G6R5_UNORM_PACK16,       VK_FORMAT_UNDEFINED               },
            {TEX_FORMAT_A2R10G10B10_UNORM_PACK32,  4u,  1u, 1u, 1u, VK_FORMAT_A2B10G10R10_UNORM_PACK32,  VK_FORMAT_UNDEFINED               },

            {TEX_FORMAT_B10G11R11_UFLOAT_PACK32,   4u,  1u, 1u, 0u, VK_FORMAT_B10G11R11_UFLOAT_PACK32,   VK_FORMAT_UNDEFINED               },
            {TEX_FORMAT_ETC2_R8G8B8_UNORM_BLOCK,   8u,  4u, 4u, 0u, VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK,   VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK  },
            {TEX_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK, 16u, 4u, 4u, 1u, VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK, VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK},

            {TEX_FORMAT_ETC2_R_UNORM_BLOCK,        8u,  4u, 4u, 0u, VK_FORMAT_EAC_R11_UNORM_BLOCK,       VK_FORMAT_UNDEFINED               },
            {TEX_FORMAT_ETC2_R_SNORM_BLOCK,        8u,  4u, 4u, 0u, VK_FORMAT_EAC_R11_SNORM_BLOCK,       VK_FORMAT_UNDEFINED               },
            {TEX_FORMAT_ETC2_RG_UNORM_BLOCK,       16u, 4u, 4u, 0u, VK_FORMAT_EAC_R11G11_UNORM_BLOCK,    VK_FORMAT_UNDEFINED               },
            {TEX_FORMAT_ETC2_RG_SNORM_BLOCK,       16u, 4u, 4u, 0u, VK_FORMAT_EAC_R11G11_SNORM_BLOCK,    VK_FORMAT_UNDEFINED               },

            {TEX_FORMAT_ASTC_4X4_UNORM_BLOCK,      16u, 4u, 4u, 1u, VK_FORMAT_ASTC_4x4_UNORM_BLOCK,      VK_FORMAT_ASTC_4x4_SRGB_BLOCK     },
            {TEX_FORMAT_ASTC_6X6_UNORM_BLOCK,      16u, 6u, 6u, 1u, VK_FORMAT_ASTC_6x6_UNORM_BLOCK,      VK_FORMAT_ASTC_6x6_SRGB_BLOCK     },
            {TEX_FORMAT_ASTC_8X8_UNORM_BLOCK,      16u, 8u, 8u, 1u, VK_FORMAT_ASTC_8x8_UNORM_BLOCK,      VK_FORMAT_ASTC_8x8_SRGB_BLOCK     },
            {TEX_FORMAT_R32G32_UINT,               8u,  1u, 1u, 0u, VK_FORMAT_R32G32_UINT,               VK_FORMAT_UNDEFINED               },
            {TEX_FORMAT_R32G32B32A32_UINT,         16u, 1u, 1u, 1u, VK_FORMAT_R32G32B32A32_UINT,         VK_FORMAT_UNDEFINED               },

            {TEX_FORMAT_R16G16_UNORM,              4u,  1u, 1u, 0u, VK_FORMAT_R16G16_UNORM,              VK_FORMAT_UNDEFINED               },

            {TEX_FORMAT_R8_UINT,                   1u,  1u, 1u, 0u, VK_FORMAT_R8_UINT,                   VK_FORMAT_UNDEFINED               },

            {TEX_FORMAT_R16_UNORM,                 2u,  1u, 1u, 0u, VK_FORMAT_R16_UNORM,                 VK_FORMAT_UNDEFINED               },
    };

    static_assert(countof(g_gfxTextureFormatInfo) == TEX_FORMAT_COUNT, "g_gfxTextureFormatInfo must be the same as TEX_FORMAT_COUNT.");

    const KGfxTextureFormatInfo& GetTextureFormatInfoVk(enumTextureFormat eFormat)
    {
        CHECK_ASSERT(eFormat < TEX_FORMAT_COUNT);
        CHECK_ASSERT(g_gfxTextureFormatInfo[eFormat].eFormat == eFormat);
        return g_gfxTextureFormatInfo[eFormat];
    }

    VkImageAspectFlags GetAspectFlagsFromFormat(enumTextureFormat eGfxFormat)
    {
        switch (eGfxFormat)
        {
        case TEX_FORMAT_D24_UNORM_S8_UINT:
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        case TEX_FORMAT_D16_UNORM:
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        case TEX_FORMAT_D32_SFLOAT:
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        case TEX_FORMAT_D32_SFLOAT_S8_UINT:
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        default:
            return VK_IMAGE_ASPECT_COLOR_BIT;
        }
    }

    KVulkanTexture::KVulkanTexture()
    {
        ZeroMemory(&m_texDesc, sizeof(m_texDesc));
        m_szDebugName = "Unknown Name";
#if GfxTextureMemLeakDetect
        static int gfxMem_Count = 0;
        gfxMem_Count++;
        if (gfxMem_Count == 201)
        {
            // 看泄多少字节，这里下断点就是现场了
            int x = 0;
        }
        m_memLeakDetect = new char[gfxMem_Count];
#endif
    }

    KVulkanTexture::~KVulkanTexture()
    {
        Destroy();
#if GfxTextureMemLeakDetect
        SAFE_DELETE_ARRAY(m_memLeakDetect);
#endif
    }

    int32_t KVulkanTexture::Release()
    {
        int nRef = --m_nRef;
        ASSERT(nRef >= 0);
        if (nRef == 0)
        {
            if (m_pvkTextureImage != VK_NULL_HANDLE)
            {
                auto piDevice = KGFX_GetGraphicDeviceVKInternal();
                CHECK_ASSERT(piDevice);

                piDevice->GC_DelayReleaseObject(this);
            }
            else
            {
                // 如果没有创建过纹理，直接释放
                delete this;
            }
        }

        return nRef;
    }

    uintptr_t KVulkanTexture::GetNativeResourceHandle()
    {
        return reinterpret_cast<uintptr_t>(m_pvkTextureImage);
    }

    void KVulkanTexture::SetDebugName(const char* szDebugName)
    {
        if (szDebugName)
        {
            m_szDebugName = szDebugName;
            GetVulkanDevice()->SetObjectLabel(m_pvkTextureImage, szDebugName);
        }
    }

    const char* KVulkanTexture::GetDebugName()
    {
        return m_szDebugName.c_str();
    }

    const KGFX_TextureDesc* KVulkanTexture::GetDesc() const
    {
        return &m_texDesc;
    }

    uint32_t KVulkanTexture::GetDeviceMemorySize() const
    {
        return m_uDevivceMemSize;
    }


    // Converts TextureUsageFlags to VkImageUsageFlags
    VkImageUsageFlags TextureUsageFlagsToVkImageUsageFlags(TextureUsageFlags usageFlags)
    {
        VkImageUsageFlags vkUsage = 0;

        if (usageFlags & TEXTURE_USAGE_TRANSFER_SRC_BIT)
            vkUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if (usageFlags & TEXTURE_USAGE_TRANSFER_DST_BIT)
            vkUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (usageFlags & TEXTURE_USAGE_SAMPLED_BIT)
            vkUsage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        if (usageFlags & TEXTURE_USAGE_STORAGE_BIT)
            vkUsage |= VK_IMAGE_USAGE_STORAGE_BIT;
        if (usageFlags & TEXTURE_USAGE_COLOR_ATTACHMENT_BIT)
            vkUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (usageFlags & TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
            vkUsage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if (usageFlags & TEXTURE_USAGE_TRANSIENT_ATTACHMENT_BIT)
            vkUsage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
        if (usageFlags & TEXTURE_USAGE_INPUT_ATTACHMENT_BIT)
            vkUsage |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
        
        // Add more mappings if needed
        return vkUsage;
    }

    TextureUsageFlags VkImageUsageFlagsToTextureUsageFlags(VkImageUsageFlags vkUsage)
    {
        TextureUsageFlags usageFlags = 0;

        if (vkUsage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
            usageFlags |= TEXTURE_USAGE_TRANSFER_SRC_BIT;
        if (vkUsage & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
            usageFlags |= TEXTURE_USAGE_TRANSFER_DST_BIT;
        if (vkUsage & VK_IMAGE_USAGE_SAMPLED_BIT)
            usageFlags |= TEXTURE_USAGE_SAMPLED_BIT;
        if (vkUsage & VK_IMAGE_USAGE_STORAGE_BIT)
            usageFlags |= TEXTURE_USAGE_STORAGE_BIT;
        if (vkUsage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
            usageFlags |= TEXTURE_USAGE_COLOR_ATTACHMENT_BIT;
        if (vkUsage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
            usageFlags |= TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if (vkUsage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT)
            usageFlags |= TEXTURE_USAGE_TRANSIENT_ATTACHMENT_BIT;
        if (vkUsage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
            usageFlags |= TEXTURE_USAGE_INPUT_ATTACHMENT_BIT;

        // Add more mappings if needed
        return usageFlags;
    }

    void GenerateImageCreateInfo(VkImageCreateInfo& ImageCreateInfo, VkImageFormatListCreateInfoKHR& ImageFormatListCreateInfo, std::vector<VkFormat>& FormatsUsed, KGFX_TextureDesc& texDesc, bool bForceLinearImageTile)
    {
        BOOL                  bResult  = false;
        BOOL                  bRetCode = false;
        VkResult              vkResult = VK_ERROR_UNKNOWN;
        VkFormatProperties    formatProperties;
        vks::KVulkanDevice*   pkvkDevice       = GetVulkanDevice();
        uint32_t              subResourceCount = 0;
        uint32_t              uMaxMipLevels    = 0;

        ImageCreateInfo       = {};
        ImageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;

        ImageFormatListCreateInfo       = {};
        ImageFormatListCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO_KHR;

        auto& textureFormatInfo = GetTextureFormatInfoVk(texDesc.eFormat);
        auto  vkFormat          = textureFormatInfo.vkFormat;
        auto  vkFormatSRGB      = textureFormatInfo.vkFormatSRGB;

        VkPhysicalDeviceProperties DeviceProperties = pkvkDevice->GetPhysicalDeviceProperties();

        VkImageUsageFlags          texUsageFlags    = TextureUsageFlagsToVkImageUsageFlags(texDesc.uUsageFlags);
        texUsageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        switch (texDesc.eDimension)
        {
        case TextureDimensionType::Texture1D:
            texDesc.uHeight = 1;
            texDesc.uDepth  = 1;
            break;
        case TextureDimensionType::Texture2D:
            texDesc.uDepth = 1;
            break;
        }

        uMaxMipLevels = NSKMath::CalFullMipLevels(NSKMath::Max3(texDesc.uWidth, texDesc.uHeight, texDesc.uDepth));
        if (texDesc.uMipLevels == 0)
            texDesc.uMipLevels = uMaxMipLevels;

        subResourceCount                = texDesc.uMipLevels * texDesc.uArraySize;
        VkImageAspectFlags uAspectFlags = GetAspectFlagsFromFormat(texDesc.eFormat);

        switch (texDesc.eDimension)
        {
        case TextureDimensionType::Texture1D:
            ImageCreateInfo.imageType = VkImageType::VK_IMAGE_TYPE_1D;
            CHECK_ASSERT((uint32_t)texDesc.uWidth <= DeviceProperties.limits.maxImageDimension1D);
            break;
        case TextureDimensionType::Texture2D:
            ImageCreateInfo.imageType = VkImageType::VK_IMAGE_TYPE_2D;
            CHECK_ASSERT((uint32_t)texDesc.uWidth <= DeviceProperties.limits.maxImageDimension2D);
            CHECK_ASSERT((uint32_t)texDesc.uHeight <= DeviceProperties.limits.maxImageDimension2D);
            break;
        case TextureDimensionType::Texture3D:
            ImageCreateInfo.imageType = VkImageType::VK_IMAGE_TYPE_3D;
            CHECK_ASSERT((uint32_t)texDesc.uWidth <= DeviceProperties.limits.maxImageDimension2D);
            CHECK_ASSERT((uint32_t)texDesc.uHeight <= DeviceProperties.limits.maxImageDimension2D);
            CHECK_ASSERT((uint32_t)texDesc.uDepth <= DeviceProperties.limits.maxImageDimension3D);
            break;
        case TextureDimensionType::TextureCube:
            ImageCreateInfo.imageType = VkImageType::VK_IMAGE_TYPE_2D;
            CHECK_ASSERT((uint32_t)texDesc.uWidth <= DeviceProperties.limits.maxImageDimension2D);
            CHECK_ASSERT((uint32_t)texDesc.uHeight <= DeviceProperties.limits.maxImageDimension2D);
            CHECK_ASSERT((uint32_t)texDesc.uArraySize >= 6);
            CHECK_ASSERT((uint32_t)texDesc.uArraySize % 6 == 0);
            break;

        default:
            DEBUG_BREAK();
        }

        const bool bNeedsMutableFormat = texDesc.eFormat == enumTextureFormat::TEX_FORMAT_R64_UINT || vkFormatSRGB != VK_FORMAT_UNDEFINED;
        if (bNeedsMutableFormat)
        {
            ImageFormatListCreateInfo.pNext = ImageCreateInfo.pNext;
            ImageCreateInfo.pNext           = &ImageFormatListCreateInfo;

            if (texDesc.eFormat == enumTextureFormat::TEX_FORMAT_R64_UINT)
            {
                FormatsUsed.push_back(vkFormat);
                FormatsUsed.push_back(GetTextureFormatInfoVk(enumTextureFormat::TEX_FORMAT_R32G32_UINT).vkFormat);
            }
            else if (vkFormatSRGB != VK_FORMAT_UNDEFINED)
            {
                FormatsUsed.push_back(vkFormat);
                FormatsUsed.push_back(vkFormatSRGB);
            }

            ImageFormatListCreateInfo.pViewFormats    = FormatsUsed.data();
            ImageFormatListCreateInfo.viewFormatCount = (uint32_t)FormatsUsed.size();

            ImageCreateInfo.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
        }

        ImageCreateInfo.format                = vkFormat;
        ImageCreateInfo.mipLevels             = texDesc.uMipLevels;
        ImageCreateInfo.arrayLayers           = texDesc.uArraySize;
        ImageCreateInfo.samples               = GetSamplerCount(texDesc.eSampleCount);
        ImageCreateInfo.tiling                = bForceLinearImageTile ? VkImageTiling::VK_IMAGE_TILING_LINEAR : VkImageTiling::VK_IMAGE_TILING_OPTIMAL;
        ImageCreateInfo.usage                 = texUsageFlags;
        ImageCreateInfo.sharingMode           = VkSharingMode::VK_SHARING_MODE_EXCLUSIVE;
        ImageCreateInfo.queueFamilyIndexCount = 0;
        ImageCreateInfo.pQueueFamilyIndices   = nullptr;
        ImageCreateInfo.initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED;

        ImageCreateInfo.extent.width  = texDesc.uWidth;
        ImageCreateInfo.extent.height = texDesc.uHeight;
        ImageCreateInfo.extent.depth  = texDesc.uDepth;

        if (texDesc.eDimension == TextureDimensionType::TextureCube)
            ImageCreateInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

        // Get device properties for the requested texture format
        vks::vkGetPhysicalDeviceFormatProperties(GetVkPhysicalDevice(), vkFormat, &formatProperties);

        const VkFormatFeatureFlags FormatFlags = ImageCreateInfo.tiling == VK_IMAGE_TILING_LINEAR ?
                                                     formatProperties.linearTilingFeatures :
                                                     formatProperties.optimalTilingFeatures;

        if ((FormatFlags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0)
        {
            ASSERT((texDesc.uUsageFlags & TEXTURE_USAGE_SAMPLED_BIT) == 0 && "Texture format does not support sampled image usage.");
            ImageCreateInfo.usage &= ~VK_IMAGE_USAGE_SAMPLED_BIT;
        }

        if ((FormatFlags & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0)
        {
            ASSERT((texDesc.uUsageFlags & TEXTURE_USAGE_STORAGE_BIT) == 0 && "Texture format does not support storage image usage.");
            ImageCreateInfo.usage &= ~VK_IMAGE_USAGE_STORAGE_BIT;
        }

        if ((FormatFlags & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) == 0)
        {
            ASSERT((texDesc.uUsageFlags & TEXTURE_USAGE_COLOR_ATTACHMENT_BIT) == 0 && "Texture format does not support color attachment usage.");
            ImageCreateInfo.usage &= ~VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        }

        if ((FormatFlags & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) == 0)
        {
            ASSERT((texDesc.uUsageFlags & TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) == 0 && "Texture format does not support depth stencil attachment usage.");
            ImageCreateInfo.usage &= ~VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        }

        if ((FormatFlags & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT) == 0)
        {
            // this flag is used unconditionally, strip it without warnings
            ImageCreateInfo.usage &= ~VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }

        if ((FormatFlags & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) == 0)
        {
            // this flag is used unconditionally, strip it without warnings
            ImageCreateInfo.usage &= ~VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }
    }

    BOOL KVulkanTexture::Create(const KGFX_TextureDesc& texDesc, const char* szDebugName)
    {
        BOOL                  bResult  = false;
        BOOL                  bRetCode = false;
        VkResult              vkResult = VK_ERROR_UNKNOWN;
        VkMemoryAllocateInfo  memAlloc = vks::initializers::MemoryAllocateInfo();
        VkMemoryRequirements  memReqs;
        VkDevice              pvkDevice        = GetVkDevice();
        vks::KVulkanDevice*   pkvkDevice       = GetVulkanDevice();
        uint32_t              subResourceCount = 0;
        uint32_t              uMaxMipLevels    = 0;

        auto& textureFormatInfo = GetTextureFormatInfoVk(texDesc.eFormat);

        auto vkFormat = textureFormatInfo.vkFormat;
        KGLOG_ASSERT_EXIT(vkFormat != VK_FORMAT_UNDEFINED);
        KGLOG_ASSERT_EXIT(szDebugName);

        m_uAspectFlags = 0;
        m_texDesc      = texDesc;
        m_formatsUsed.clear();

        {
            VkImageCreateInfo              imageCreateInfo           = {};
            VkImageFormatListCreateInfoKHR imageFormatListCreateInfo = {};

            GenerateImageCreateInfo(imageCreateInfo, imageFormatListCreateInfo, m_formatsUsed, m_texDesc, false);
            m_texDesc.uUsageFlags = VkImageUsageFlagsToTextureUsageFlags(imageCreateInfo.usage);

            m_uAspectFlags = GetAspectFlagsFromFormat(m_texDesc.eFormat);

            // #if X3D_VK_USE_VMA
            if (DrvOption::bX3D_VK_USE_VMA)
            {
                ASSERT(!m_pvkTextureImage && !m_pVMAllocation);
                bRetCode = pkvkDevice->VMACreateImage(imageCreateInfo, VmaMemoryUsage::VMA_MEMORY_USAGE_GPU_ONLY, m_pvkTextureImage, m_pVMAllocation);
                KGLOG_ASSERT_EXIT(bRetCode);
                ASSERT(m_pvkTextureImage && m_pVMAllocation);

                m_uDevivceMemSize = pkvkDevice->VMAGetAllocSize(m_pVMAllocation);
            }
            else
            {
                // #else
                vkResult = vks::vkCreateImage(pvkDevice, &imageCreateInfo, KVK_ALLOCATER, &m_pvkTextureImage);
                KGLOG_COM_ASSERT_EXIT(vkResult);

                vks::vkGetImageMemoryRequirements(pvkDevice, m_pvkTextureImage, &memReqs);

                memAlloc.allocationSize  = memReqs.size;
                memAlloc.memoryTypeIndex = pkvkDevice->GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

                m_uDevivceMemSize          = (uint32_t)memReqs.size;
                m_uDevivceMemAlignmentSize = (uint32_t)memReqs.alignment;

                vkResult = pkvkDevice->AllocateMemory(pvkDevice, &memAlloc, KVK_ALLOCATER, &m_pvkDevivceMem, &m_uDevivceMemOffset, (uint32_t)memReqs.alignment);
                KGLOG_COM_ASSERT_EXIT(vkResult);

                vkResult = vks::vkBindImageMemory(pvkDevice, m_pvkTextureImage, m_pvkDevivceMem, m_uDevivceMemOffset);
                KGLOG_COM_ASSERT_EXIT(vkResult);
            }
            // #endif

            m_ResourceLayoutTracker = KGFX_ResourceLayoutTrackerVK(KGfxAccess::Unknown);
            KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
            pPerfMonitor->m_sVkImage.UsageImageCountInc(m_texDesc.uUsageFlags);
        }

        SetDebugName(szDebugName);

        bResult = true;
    Exit0:
        if (!bResult)
        {
            Destroy();
        }
        return bResult;
    }

    void KVulkanTexture::Destroy()
    {
        if (m_pvkTextureImage)
        {
            VkDevice            pvkDevice  = GetVkDevice();
            vks::KVulkanDevice* pkvkDevice = GetVulkanDevice();

            // #if X3D_VK_USE_VMA
            if (DrvOption::bX3D_VK_USE_VMA)
            {
                ASSERT(m_pVMAllocation);
                pkvkDevice->VMADestroyImage(m_pvkTextureImage, m_pVMAllocation);
                ASSERT(!m_pvkTextureImage && !m_pVMAllocation);
            }
            // #else
            else
            {
                vks::vkDestroyImage(pvkDevice, m_pvkTextureImage, KVK_ALLOCATER);
                m_pvkTextureImage = VK_NULL_HANDLE;

                pkvkDevice->FreeMemory(pvkDevice, m_pvkDevivceMem, KVK_ALLOCATER, m_uDevivceMemOffset, m_uDevivceMemSize);

                m_pvkDevivceMem            = VK_NULL_HANDLE;
                m_uDevivceMemOffset        = 0;
                m_uDevivceMemSize          = 0;
                m_uDevivceMemAlignmentSize = 0;

                KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
                pPerfMonitor->m_sVkImage.UsageImageCountDec(m_texDesc.uUsageFlags);
            }
            // #endif
        }
    }

    VkImage KVulkanTexture::GetVkHandle()
    {
        return m_pvkTextureImage;
    }

    // #if !X3D_VK_USE_VMA
    KVkDeviceMemory KVulkanTexture::GetVkMemoryHandle()
    {
        return m_pvkDevivceMem;
    }

    uint32_t KVulkanTexture::GetVkMemoryOffset() const
    {
        return m_uDevivceMemOffset;
    }
    // #endif

    VkImageAspectFlags KVulkanTexture::GetAspectFlags() const
    {
        return m_uAspectFlags;
    }

    KGFX_ResourceLayoutTrackerVK& KVulkanTexture::GetLayoutTracker()
    {
        return m_ResourceLayoutTracker;
    }

    KGfxSubresourceRange KVulkanTexture::ResolveSubresourceRange(const KGfxSubresourceRange& range) const
    {
        KGfxSubresourceRange resolved = range;

        resolved.uBaseMipLevel = std::min<uint32_t>(resolved.uBaseMipLevel, m_texDesc.uMipLevels - 1);
        resolved.uMipCount = std::min<uint32_t>(resolved.uMipCount, m_texDesc.uMipLevels - resolved.uBaseMipLevel);

        uint32_t arrayLayerCount = m_texDesc.uArraySize;
        resolved.uBaseArraySlice = std::min<uint32_t>(resolved.uBaseArraySlice, arrayLayerCount - 1);
        resolved.uArrayCount = std::min<uint32_t>(resolved.uArrayCount, arrayLayerCount - resolved.uBaseArraySlice);

        return resolved;
    }

    void KVulkanTexture::EnableUAVOverlap(bool bEnable)
    {
        m_UAVOverlap = bEnable;
    }

    uint32_t KVulkanTexture::CalSubresourceIndex(uint32_t uMip, uint32_t uMipLevels, uint32_t uArraySlice)
    {
        CHECK_ASSERT(uMip < uMipLevels);
        return uArraySlice * uMipLevels + uMip;
    }

    void KVulkanTexture::CalSubresourceSize(
        uint32_t          uMip,
        uint32_t          uWidth,
        uint32_t          uHeight,
        uint32_t          uDepth,
        enumTextureFormat eFormat,
        uint32_t&         uRetRowPitch,
        uint32_t&         uRetDepthPitch,
        uint32_t&         uRetSlicePitch
    )
    {
        auto& textureFormatInfo = GetTextureFormatInfoVk(eFormat);
        if (textureFormatInfo.uHeightPerBlock == 1 && textureFormatInfo.uWidthPerBlock == 1)
        {
            uint32_t uMipWidth  = std::max(1u, uWidth >> uMip);
            uint32_t uMipHeight = std::max(1u, uHeight >> uMip);
            uint32_t uMipuDepth = std::max(1u, uDepth >> uMip);

            uRetRowPitch   = uMipWidth * textureFormatInfo.uBytesPerBlock;
            uRetDepthPitch = uMipHeight * uRetRowPitch;
            uRetSlicePitch = uMipuDepth * uRetDepthPitch;
        }
        else
        {
            uint32_t uMipWidth  = NSKMath::CeilIntDIV(std::max(1u, uWidth >> uMip), textureFormatInfo.uWidthPerBlock);
            uint32_t uMipHeight = NSKMath::CeilIntDIV(std::max(1u, uHeight >> uMip), textureFormatInfo.uHeightPerBlock);
            uint32_t uMipuDepth = std::max(1u, uDepth >> uMip);

            uRetRowPitch   = uMipWidth * textureFormatInfo.uBytesPerBlock;
            uRetDepthPitch = uMipHeight * uRetRowPitch;
            uRetSlicePitch = uMipuDepth * uRetDepthPitch;
        }
    }

    void KVulkanTexture::CalSubresourceSize(uint32_t uMip, uint32_t uWidth, uint32_t uHeight, enumTextureFormat eFormat, uint32_t& uRetRowPitch, uint32_t& uRetDepthPitch)
    {
        auto& textureFormatInfo = GetTextureFormatInfoVk(eFormat);
        if (textureFormatInfo.uHeightPerBlock == 1 && textureFormatInfo.uWidthPerBlock == 1)
        {
            uint32_t uMipWidth  = std::max(1u, uWidth >> uMip);
            uint32_t uMipHeight = std::max(1u, uHeight >> uMip);

            uRetRowPitch   = uMipWidth * textureFormatInfo.uBytesPerBlock;
            uRetDepthPitch = uMipHeight * uRetRowPitch;
        }
        else
        {
            uint32_t uMipWidth  = NSKMath::CeilIntDIV(std::max(1u, uWidth >> uMip), textureFormatInfo.uWidthPerBlock);
            uint32_t uMipHeight = NSKMath::CeilIntDIV(std::max(1u, uHeight >> uMip), textureFormatInfo.uHeightPerBlock);

            uRetRowPitch   = uMipWidth * textureFormatInfo.uBytesPerBlock;
            uRetDepthPitch = uMipHeight * uRetRowPitch;
        }
    }


    /////////////////////////////////////////////////////////////////////////////////////////////////////
    KVulkanTextureView::KVulkanTextureView()
    {
        ZeroMemory(&m_viewDesc, sizeof(m_viewDesc));
    }

    KVulkanTextureView::~KVulkanTextureView()
    {
        Destroy();
    }

    void KVulkanTextureView::SetDebugName(const char* szName)
    {
        GetVulkanDevice()->SetObjectLabel(m_pvkView, szName);
    }

    int KVulkanTextureView::Release()
    {
        int nRef = --m_nRef;
        ASSERT(nRef >= 0);
        if (nRef == 0)
        {
            if (m_pvkView != VK_NULL_HANDLE)
            {
                auto piDevice = KGFX_GetGraphicDeviceVKInternal();
                CHECK_ASSERT(piDevice);

                // KVulkanTextureView依赖引用m_pResource，当KVulkanTextureView延迟释放时，也应该同步延迟释放m_pResource.
                piDevice->GC_DelayReleaseObject(this, [this]() { SAFE_RELEASE(m_pResource); });
            }
            else
            {
                // 如果没有创建过纹理视图，直接释放
                delete this;
            }
        }

        return nRef;
    }

    bool KVulkanTextureView::SupportSampled() const
    {
        return m_bSupportSampled;
    }

    uint64_t KVulkanTextureView::GetCode() const
    {
        return (uint64_t)m_pvkView;
    }

    BOOL KVulkanTextureView::Create(KVulkanTexture* pResource, const KGFX_TextureViewDesc* viewDesc, const char* szDebugName, VkImageUsageFlags ImageUsageFlags)
    {
        BOOL                  bResult    = false;
        VkResult              vkResult   = VK_ERROR_UNKNOWN;
        VkDevice              pvkDevice  = GetVkDevice();
        VkImageViewCreateInfo createInfo = vks::initializers::ImageViewCreateInfo();

        CHECK_ASSERT(pvkDevice);
        CHECK_ASSERT(pResource);
        CHECK_ASSERT(szDebugName);
        auto& texDesc = *(pResource->GetDesc());

        auto& textureFormatInfo = GetTextureFormatInfoVk(viewDesc->eFormat);
        m_viewDesc              = *viewDesc;

        if (m_viewDesc.sSubresourceRange.uMipCount == UINT32_MAX)
        {
            KGLOG_ASSERT_EXIT((uint32_t)m_viewDesc.sSubresourceRange.uBaseMipLevel < texDesc.uMipLevels);
            m_viewDesc.sSubresourceRange.uMipCount = texDesc.uMipLevels - m_viewDesc.sSubresourceRange.uBaseMipLevel;
        }
        KGLOG_ASSERT_EXIT((uint32_t)(m_viewDesc.sSubresourceRange.uBaseMipLevel + m_viewDesc.sSubresourceRange.uMipCount) <= texDesc.uMipLevels);

        if (m_viewDesc.sSubresourceRange.uArrayCount == UINT32_MAX)
        {
            KGLOG_ASSERT_EXIT((uint32_t)m_viewDesc.sSubresourceRange.uBaseArraySlice < texDesc.uArraySize);
            m_viewDesc.sSubresourceRange.uArrayCount = texDesc.uArraySize - m_viewDesc.sSubresourceRange.uBaseArraySlice;
        }
        KGLOG_ASSERT_EXIT((uint32_t)(m_viewDesc.sSubresourceRange.uBaseArraySlice + m_viewDesc.sSubresourceRange.uArrayCount) <= texDesc.uArraySize);

        if (viewDesc->eViewType == KGfxResourceViewType::RESOURCE_VIEW_TYPE_SRV)
        {
            if (viewDesc->eFormat != enumTextureFormat::TEX_FORMAT_R64_UINT)
            {
                KGLOG_ASSERT_EXIT(texDesc.uUsageFlags & TEXTURE_USAGE_SAMPLED_BIT);
            }
            else
            {
                // R64_UINT 需要特殊处理
                KGLOG_ASSERT_EXIT(texDesc.uUsageFlags & TEXTURE_USAGE_STORAGE_BIT);
                m_bSupportSampled = false;
            }
        }
        else if (viewDesc->eViewType == KGfxResourceViewType::RESOURCE_VIEW_TYPE_UAV)
        {
            KGLOG_ASSERT_EXIT(texDesc.uUsageFlags & TEXTURE_USAGE_STORAGE_BIT);
        }
        else if (viewDesc->eViewType == KGfxResourceViewType::RESOURCE_VIEW_TYPE_RTV)
        {
            KGLOG_ASSERT_EXIT(texDesc.uUsageFlags & TEXTURE_USAGE_COLOR_ATTACHMENT_BIT);
        }
        else if (viewDesc->eViewType == KGfxResourceViewType::RESOURCE_VIEW_TYPE_DSV)
        {
            KGLOG_ASSERT_EXIT(texDesc.uUsageFlags & TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
        }
        else
        {
            KGLogPrintf(KGLOG_ERR, "KVulkanGfxTextureView::Create: Unsupported view type %d.\n", (int)viewDesc->eViewType);
            goto Exit0;
        }

        switch (texDesc.eDimension)
        {
        case TextureDimensionType::Texture1D:
            {
                if (m_viewDesc.eViewDimension == ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE1D)
                    createInfo.viewType = VkImageViewType::VK_IMAGE_VIEW_TYPE_1D;
                else if (m_viewDesc.eViewDimension == ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE1D_ARRAY)
                    createInfo.viewType = VkImageViewType::VK_IMAGE_VIEW_TYPE_1D_ARRAY;
                else
                    DEBUG_BREAK();
            }
            break;
        case TextureDimensionType::Texture2D:
            {
                if (m_viewDesc.eViewDimension == ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D)
                    createInfo.viewType = VkImageViewType::VK_IMAGE_VIEW_TYPE_2D;
                else if (m_viewDesc.eViewDimension == ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D_ARRAY)
                    createInfo.viewType = VkImageViewType::VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                else
                    DEBUG_BREAK();
            }
            break;
        case TextureDimensionType::TextureCube:
            {
                KGLOG_ASSERT_EXIT(
                    m_viewDesc.eViewDimension == ResourceViewDimension::RESOURCE_DIMENSION_TEXTURECUBE ||
                    m_viewDesc.eViewDimension == ResourceViewDimension::RESOURCE_DIMENSION_TEXTURECUBE_ARRAY ||
                    m_viewDesc.eViewDimension == ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D ||
                    m_viewDesc.eViewDimension == ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D_ARRAY
                );

                if (m_viewDesc.eViewDimension == ResourceViewDimension::RESOURCE_DIMENSION_TEXTURECUBE)
                {
                    createInfo.viewType = VkImageViewType::VK_IMAGE_VIEW_TYPE_CUBE;
                    KGLOG_ASSERT_EXIT(m_viewDesc.sSubresourceRange.uArrayCount == 6);
                }
                else if (m_viewDesc.eViewDimension == ResourceViewDimension::RESOURCE_DIMENSION_TEXTURECUBE_ARRAY)
                {
                    createInfo.viewType = VkImageViewType::VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
                    KGLOG_ASSERT_EXIT(m_viewDesc.sSubresourceRange.uArrayCount > 6 && ((m_viewDesc.sSubresourceRange.uArrayCount % 6) == 0));
                }
                else if (m_viewDesc.eViewDimension == ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D)
                {
                    createInfo.viewType = VkImageViewType::VK_IMAGE_VIEW_TYPE_2D;
                }
                else if (m_viewDesc.eViewDimension == ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D_ARRAY)
                {
                    createInfo.viewType = VkImageViewType::VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                }
                else
                {
                    DEBUG_BREAK();
                }
            }
            break;
        case TextureDimensionType::Texture3D:
            {
                KGLOG_ASSERT_EXIT(m_viewDesc.eViewDimension == ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE3D);
                createInfo.viewType = VkImageViewType::VK_IMAGE_VIEW_TYPE_3D;
            }
            break;
        default:
            DEBUG_BREAK();
            break;
        }

        createInfo.format = !texDesc.bSRGB ? textureFormatInfo.vkFormat : textureFormatInfo.vkFormatSRGB;
        KGLOG_ASSERT_EXIT(createInfo.format != VK_FORMAT_UNDEFINED);

        {
            VkImageAspectFlags aspectMask = GetAspectFlagsFromFormat(m_viewDesc.eFormat);

            if ((aspectMask & VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT) && (m_viewDesc.uAspectFlags & TextureAspectFlagBits::TEXTURE_ASPECT_COLOR_BIT))
            {
                createInfo.subresourceRange.aspectMask = VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT;
            }
            else if ((aspectMask & VkImageAspectFlagBits::VK_IMAGE_ASPECT_DEPTH_BIT) && (m_viewDesc.uAspectFlags & TextureAspectFlagBits::TEXTURE_ASPECT_DEPTH_BIT))
            {
                createInfo.subresourceRange.aspectMask = VkImageAspectFlagBits::VK_IMAGE_ASPECT_DEPTH_BIT;
            }
            else if (aspectMask & VkImageAspectFlagBits::VK_IMAGE_ASPECT_STENCIL_BIT && (m_viewDesc.uAspectFlags & TextureAspectFlagBits::TEXTURE_ASPECT_STENCIL_BIT))
            {
                createInfo.subresourceRange.aspectMask = VkImageAspectFlagBits::VK_IMAGE_ASPECT_STENCIL_BIT;
            }
            else
            {
                createInfo.subresourceRange.aspectMask = VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT;
            }
        }

        createInfo.subresourceRange.baseMipLevel   = m_viewDesc.sSubresourceRange.uBaseMipLevel;
        createInfo.subresourceRange.levelCount     = m_viewDesc.sSubresourceRange.uMipCount;
        createInfo.subresourceRange.baseArrayLayer = m_viewDesc.sSubresourceRange.uBaseArraySlice;
        createInfo.subresourceRange.layerCount     = m_viewDesc.sSubresourceRange.uArrayCount;

        createInfo.image        = pResource->GetVkHandle();
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        if (ImageUsageFlags != 0)
        {
            VkImageViewUsageCreateInfo viewUsageCreateInfo = {};
            viewUsageCreateInfo.sType                      = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO;
            viewUsageCreateInfo.usage                      = ImageUsageFlags;

            viewUsageCreateInfo.pNext = (void*)createInfo.pNext;
            createInfo.pNext          = &viewUsageCreateInfo;
        }

        vkResult = vks::vkCreateImageView(pvkDevice, &createInfo, KVK_ALLOCATER, &m_pvkView);
        KGLOG_COM_ASSERT_EXIT(vkResult);

        m_pResource = pResource;
        m_pResource->AddRef();

        SetDebugName(szDebugName);
        bResult = true;
    Exit0:
        if (!bResult)
        {
            Destroy();
        }
        return bResult;
    }

    void KVulkanTextureView::Destroy()
    {
        //remove from bindless
        if (m_uBindlessHandle != UINT32_MAX)
        {
            ASSERT(IS_BINDLESS_ENABLED);
            //m_uBindlessHandle = VUlkanBindlessManager::
            KVulkanGraphicDevice* pGraphicDevice = (KVulkanGraphicDevice*)GetGraphicDevice();
            KVulkanBindlessManager* pBindlessMgr = pGraphicDevice->GetBindlessManager();
            pBindlessMgr->ReleaseResourceBindlessSlot(m_uBindlessHandle);
            m_uBindlessHandle = UINT32_MAX;
        }

        if (m_pvkView)
        {
            VkDevice pvkDevice = GetVkDevice();
            CHECK_ASSERT(pvkDevice);

            vks::vkDestroyImageView(pvkDevice, m_pvkView, KVK_ALLOCATER);
            m_pvkView = VK_NULL_HANDLE;
        }

        SAFE_RELEASE(m_pResource);
    }

    VkImageView KVulkanTextureView::GetVkHandle() const
    {
        return m_pvkView;
    }

    KVulkanTexture* KVulkanTextureView::GetGfxResource() const
    {
        return m_pResource;
    }

    IKGFX_TextureResource* KVulkanTextureView::GetResource() const
    {
        return GetGfxResource();
    }

    const KGFX_TextureViewDesc& KVulkanTextureView::GetViewDesc() const
    {
        return m_viewDesc;
    }

    uintptr_t KVulkanTextureView::GetNativeHandle()
    {
        return reinterpret_cast<uintptr_t>(GetVkHandle());
    }

    uint32_t KVulkanTextureView::GetBindlessHandle()
    {
        ASSERT(IS_BINDLESS_ENABLED);
        if (m_uBindlessHandle == UINT32_MAX)
        {
            KVulkanGraphicDevice* pGraphicDevice = (KVulkanGraphicDevice*)GetGraphicDevice();
            KVulkanBindlessManager* pBindlessMgr = pGraphicDevice->GetBindlessManager();
            m_uBindlessHandle = pBindlessMgr->RequestResourceBindlessSolt();
        }
        return m_uBindlessHandle;
    }
} // namespace gfx
