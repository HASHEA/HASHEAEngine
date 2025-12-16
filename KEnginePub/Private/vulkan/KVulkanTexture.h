#pragma once

#include "KGFX_ResourceLayoutTrackerVK.h"
#include "KEnginePub/Public/IGFX_Public.h"
#include "KVulkanFunc.h"

namespace gfx
{
    class KVulkanTexture : public KGFX_DelayReleaseObject, public IKGFX_TextureResource
    {
        friend class KVulkanRenderTarget2D;

    public:
        virtual int32_t Release() override;

    public: // interface IKGFX_Resource
        virtual uintptr_t GetNativeResourceHandle() override;
        virtual void SetDebugName(const char* name) override;
        virtual const char* GetDebugName() override;

    public: // interface IKGFX_TextureResource
        virtual const KGFX_TextureDesc* GetDesc() const override;
        virtual uint32_t GetDeviceMemorySize() const override;

    public:
        KVulkanTexture();
        virtual ~KVulkanTexture();

        BOOL Create(const KGFX_TextureDesc& texDesc, const char* szDebugName);
        void Destroy();
        VkImage GetVkHandle();
        KVkDeviceMemory GetVkMemoryHandle();
        uint32_t GetVkMemoryOffset() const;
        VkImageAspectFlags GetAspectFlags() const;
        uint32_t GetDeviceMemoryAlignmentSize() const { return m_uDevivceMemAlignmentSize; }
        KGFX_ResourceLayoutTrackerVK& GetLayoutTracker();
        KGfxSubresourceRange ResolveSubresourceRange(const KGfxSubresourceRange& range) const;
        void EnableUAVOverlap(bool bEnable);
        bool IsEnableUAVOverlap() const { return m_UAVOverlap; }

    public:
        static uint32_t CalSubresourceIndex(
            uint32_t uMip,
            uint32_t uMipLevels,
            uint32_t uArraySlice
        );

        static void CalSubresourceSize(
            uint32_t uMip,
            uint32_t uWidth,
            uint32_t uHeight,
            uint32_t uDepth,
            enumTextureFormat eFormat,
            uint32_t& uRetRowPitch,
            uint32_t& uRetDepthPitch,
            uint32_t& uRetSlicePitch
        );

        static void CalSubresourceSize(
            uint32_t uMip,
            uint32_t uWidth,
            uint32_t uHeight,
            enumTextureFormat eFormat,
            uint32_t& uRetRowPitch,
            uint32_t& uRetDepthPitch
        );

        

    private:
        VkImage                 m_pvkTextureImage = VK_NULL_HANDLE;
        VmaAllocation           m_pVMAllocation = nullptr;
        KVkDeviceMemory         m_pvkDevivceMem = VK_NULL_HANDLE;
        uint32_t                m_uDevivceMemOffset = 0;
        uint32_t                m_uDevivceMemAlignmentSize = 0;
        uint32_t                m_uDevivceMemSize = 0;
        VkImageAspectFlags      m_uAspectFlags = 0;

#if GfxTextureMemLeakDetect
        char* m_memLeakDetect;
#endif
        KGFX_TextureDesc        m_texDesc;
        std::vector<VkFormat>   m_formatsUsed;
        std::string             m_szDebugName;
        KGFX_ResourceLayoutTrackerVK m_ResourceLayoutTracker = {KGfxAccess::Unknown};
        bool                    m_UAVOverlap = false;
    };

    class KVulkanTextureView : public KGFX_DelayReleaseObject, public gfx::IKGFX_TextureView
    {
        friend class KVulkanRenderTarget2D;

    public: // interface KGfxRef
        virtual int32_t Release() override;

    public: // interface IKGFX_TextureView
        IKGFX_TextureResource* GetResource() const override;
        const KGFX_TextureViewDesc& GetViewDesc() const override;
        uintptr_t GetNativeHandle() override;
        uint32_t GetBindlessHandle() override;
        void SetDebugName(const char* szDebugName) override;

    public:
        KVulkanTextureView();
        ~KVulkanTextureView();
        BOOL Create(KVulkanTexture* pResource, const KGFX_TextureViewDesc* viewDesc, const char* szDebugName, VkImageUsageFlags ImageUsageFlags = 0);
        void Destroy();
        VkImageView GetVkHandle() const;
        KVulkanTexture* GetGfxResource() const;
        bool SupportSampled() const;
        uint64_t GetCode() const;

    private:
        VkImageView m_pvkView = VK_NULL_HANDLE;
        KGFX_TextureViewDesc m_viewDesc;
        KVulkanTexture* m_pResource = nullptr;
        bool m_bSupportSampled = true;
        uint32_t m_uBindlessHandle = UINT32_MAX;
    };
}
