#pragma once
#include "GFXVulkan.h"

namespace gfx
{
    class KDynamicBufferRing;

#define GfxBufferMemLeakDetect 0
    class KVulkanBuffer : public IKGFX_Buffer, public KGFX_DelayReleaseObject
    {
    public:
        // interface KGfxRef
        virtual int32_t AddRef();
        virtual int32_t GetRef();
        virtual int32_t Release();

    public:
        // interface IKGFX_Resource
        virtual uintptr_t GetNativeResourceHandle() override { return (uintptr_t)m_pvkBuffer; };
        virtual void SetDebugName(const char* name) override;
        virtual const char* GetDebugName() override;

    public:
        // interface IKGFX_Buffer
        virtual const KGfxBufferDesc* GetDesc() const { return &m_bufDesc; }
        virtual BOOL Update(const void* pSrcData, uint32_t uSrcDataSize, uint32_t uDstOffset, BOOL bOverWrite) override;
        virtual uint32_t GetDynamicOffset() override { return 0; }
        virtual void* MapRange() override;
        virtual BOOL IsDynamic() override { return FALSE; }
        virtual uint64_t GetBufferDeviceAddress() override;        
    public:
        // interface KVulkanBuffer
        KVulkanBuffer();
        virtual ~KVulkanBuffer();

        virtual BOOL Create(const KGfxBufferDesc& bufDesc, const void* pData);
        virtual BOOL Destroy();
        virtual const VkDescriptorBufferInfo& GetDescriptorBufferInfo(bool bDynamic = false);

        VkBuffer GetVkBuffer();
        KVkDeviceMemory GetVkMemory() { return m_pvkDevivceMem; }
        uint32_t GetVkMemoryOffset() { return m_uDevivceMemOffset; }
        uint32_t GetVkMemorySize();
        VmaAllocation VMAGetAllocation() { return m_pVmaAllocation; }

        KVulkanStagingBuffer* GetStagingBuffer() { return m_pStagingBuffer; }
        void MapTempStagingBuffer(KVulkanStagingBuffer* pStagingBuffer);

        BOOL FlushMappedRanges();
        BOOL InvalidateMappedRanges();

        void SetUpdateRenderPassTick(uint64_t uLastUpdateRenderPassTick);
        uint64_t GetUpdateRenderPassTick() const;

        KGFX_ResourceLayoutTrackerVK& GetLayoutTracker();
        void EnableUAVOverlap(bool bEnable);
        bool IsEnableUAVOverlap() const { return m_UAVOverlap; }

        virtual uint32_t GetBufferBindlessHandle();

    public:
        uint32_t GetId();
        uint64_t GetCode();
        uint64_t uHashCode;
        uint32_t m_bNeedUpdate;
        uint32_t m_uNeedUpdateSize;
        uint32_t m_uId;
        
    private:
        VmaAllocation m_pVmaAllocation = nullptr;
        bool m_bCoherent = false;
    protected:
        VkDescriptorBufferInfo m_vkDescriptorBufferInfo;
    protected:
        VkBuffer m_pvkBuffer = VK_NULL_HANDLE;
        uint32_t m_uDevivceMemSize = 0;

        uint32_t m_uDevivceMemOffset = 0;
        uint32_t m_uDevivceMemAlignmentSize = 0;
        KVkDeviceMemory m_pvkDevivceMem = VK_NULL_HANDLE;
        VkAccessFlags m_uMemAccessFlags = 0;
        uint32_t m_uBindlessHandle = UINT32_MAX;

        KGfxBufferDesc m_bufDesc;
        KVulkanStagingBuffer* m_pStagingBuffer = nullptr;
        void* m_pMappedMemoryPtr = nullptr;
        uint32_t m_uUpDateSize = uint32_t(-1);

        int m_nFrameCount;
        KGFX_ResourceLayoutTrackerVK m_layoutTracker = { KGfxAccess::Unknown };
        bool m_UAVOverlap = false;

        // 用于记录最近一次在RenderPass范围内更新的Tick，用于避免RenderPass范围内重复更新
        uint64_t m_LastUpdateRenderPassTick = 0;

        std::string m_strVKObjectName;

#if GfxBufferMemLeakDetect
        char* m_memLeakDetect;
#endif
    };

    class KVulkanDynamicBuffer : public KVulkanBuffer
    {
    public:
        KVulkanDynamicBuffer(uint32_t uSize, gfx::BufferUsageFlags uUsageFlags, BOOL bShareMode);
        virtual ~KVulkanDynamicBuffer();

        int32_t Release() override;        
        const VkDescriptorBufferInfo& GetDescriptorBufferInfo(bool bDynamic = false) override;
        BOOL Update(const void* pSrcData, uint32_t uSrcDataSize, uint32_t uDstOffset, BOOL bOverWrite) override;
        void* MapRange() override;
        BOOL IsDynamic() override { return TRUE; }

        // Update之后才能调用
        uint32_t GetDynamicOffset() override;

        uint32_t GetAlignSize() { return (uint32_t)m_vkDescriptorBufferInfo.range; }

        virtual uint32_t GetBufferBindlessHandle() override;
    private:
        BOOL ReCreatePrivateDyNamicBufferRing();        
        BOOL m_bShareMode;
        KDynamicBufferRing *m_pPrivateDyNamicBufferRing;
        uint32_t m_uMultiple = 1u;
        //按动态buffer默认绑定的时候offset和range都是不修改的，绘制的时候才决定offset
        VkDescriptorBufferInfo m_vkStaticDescriptorBufferInfo;
    };

#define GfxGfxBufferResourceViewMemLeakDetect 0
    class KVulkanBufferResourceView : public IKGFX_BufferView, public KGFX_DelayReleaseObject
    {
    public:
        // interface KGfxBufferResourceView
        virtual IKGFX_Buffer* GetResource() override;
        virtual const KGFX_BufferViewDesc* GetViewDesc() const override;
        virtual void* GetViewHandle() override;
        virtual uint64_t GetCode() override;
        virtual void SetObjectName(const char* pcszName) override;
        uint32_t GetBindlessHandle() override;
    public:
        // interface KGfxRef
        virtual int32_t AddRef() override;
        virtual int32_t Release() override;
        virtual int32_t GetRef() override { return m_nRef; }

    public:
        // interface KVulkanBufferResourceView
        KVulkanBufferResourceView();
        virtual ~KVulkanBufferResourceView();

        BOOL Create(KVulkanBuffer* pResource, const KGFX_BufferViewDesc* pDesc);
        void Destroy();
        VkBufferView GetVkHandle();
        KVulkanBuffer* GetGfxResource();
        uintptr_t GetNativeHandle() override;

    

    private:
        VkBufferView m_pvkView = VK_NULL_HANDLE;
        KVulkanBuffer* m_pResource = nullptr;
        KGFX_BufferViewDesc m_viewDesc;
        VkBufferViewCreateInfo m_createInfo; // for debugging creation information.
#if GfxGfxBufferResourceViewMemLeakDetect
        char* m_memLeakDetect = nullptr;
#endif
        uint32_t m_uBindlessHandle = UINT32_MAX;
      
    };
}
