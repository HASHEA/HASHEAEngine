#pragma once

#include <array>
#include <set>
#include <unordered_map>
#include "../IGFX_Private.h"
#include "KVulkanFunc.h"
#include "KBase/Public/async_task/KAsyncTask.h"
#include "KBase/Public/thread/KThread.h"
#include "KBase/Public/KG3D_Base/KG3D_Vector.h"
#include <unordered_set>
#include "KVulkanTexture.h"
//////////////////////////////////////////////////////////////////////////
#include "optick.h"
#include "KVulkanDefine.h"
#include "KVulkanPrivate.h"
#include <KMemory/Public/KAutoRefPtr.h>

#define EXPLICIT_RESOLVE 0


namespace gli
{
    class texture;
}

namespace gfx
{
    class KTextureVK;
    struct KWindow;
} // namespace gfx
#define MAX_BATCH_BARRIERS 64

#define KVK_ALLOCATER      (VkAllocationCallbacks*)nullptr

class KShaderResourceVK;

namespace vks
{
    class KVulkanSwapChain;
    struct KShaderProgram;
} // namespace vks
namespace gfx
{
    class KVulkanGraphicDevice;
    class KVulkanFence;
    class KVulkanSignalFence;
    class KVulkanStagingBuffer;
    class KVulkanStagingManager;
    class KVulkanUploadCmdBufferManager;
    class KVulkanCommandBuffer;
    class KVulkanRenderFrameBuffer;
    class KVulkanBuffer;
    class IKSpecializationConstantContainer;

    typedef int ThreadID;

    VkShaderStageFlagBits GetShaderStageFlag(gfx::ShaderStageType flag);

    struct KGfxTextureFormatInfo
    {
        enumTextureFormat eFormat;
        uint32_t          uBytesPerBlock  : 15;
        uint32_t          uWidthPerBlock  : 8;
        uint32_t          uHeightPerBlock : 8;
        uint32_t          uHasAlpha       : 1;
        VkFormat          vkFormat;
        VkFormat          vkFormatSRGB;
    };

    VkFilter             GetSamplerFilter(enumSamplerFilter filter);
    VkSamplerMipmapMode  GetSamplerMipmapMode(enumMipMapMode mode);
    VkSamplerAddressMode GetSamplerAddressMode(enumSamplerAddressMode mode);
    VkBorderColor        GetSamplerBorderColor(enumBorderColor borderColor);
    enumTextureFormat    GetTextureEnumFromFormat(VkFormat texFormat);
    VkImageAspectFlags   GetImageAspectMask(VkFormat format);
    VkFormat             GetTextureFormatFromTargetFormat(enumTextureFormat srcfmt, BOOL& bColorAttach, BOOL& bDepth, BOOL& bStencil, uint32_t& bytesStride);

    class KVulkanFence : public KGfxRef
    {
    public:
        KVulkanFence();
        virtual ~KVulkanFence();
        BOOL Create(BOOL bInitWithSignaled = false);
        BOOL Destroy();

    public:
        BOOL Query();
        void Reset();
        void Submit()
        {
            m_bSubmitted = true;
        }
        BOOL IsSubmitted() const
        {
            return m_bSubmitted;
        }
        uint64_t GetSignalFenceCounter() const
        {
            return m_uSignalFenceCounter;
        }

        VkFence GetFence();

        void SetObjectName(const char* szName);

    protected:
        BOOL CheckState();

    private:
        VkFence  m_pFence              = VK_NULL_HANDLE;
        BOOL     m_bSignaled           = false;
        BOOL     m_bSubmitted          = false;
        uint64_t m_uSignalFenceCounter = 0;
#ifdef _WIN32
        std::string m_strName;
#endif
    };

    class KVulkanSignalFence : public KSignalFence
    {
    public:
        KVulkanSignalFence() {};
        virtual ~KVulkanSignalFence() { Clear(); };

        virtual bool IsSubmitted() const override;
        virtual void Clear() override;
        virtual bool Query() override;

    public:
        BOOL  Submit(KVulkanFence* pFence);
        bool  GetCurrentValue(uint64_t* outValue) override;
        void* GetNativeHandle() override;

    protected:
        bool m_bSubmitted = false;
        KVulkanFence* m_pFence              = nullptr;
        uint64_t      m_uSignalFenceCounter = UINT64_MAX;
    };

#define semaphore_mem_lect 0
    class KVulkanSemaphore : public KGfxRef
    {
    public:
        KVulkanSemaphore();
        virtual ~KVulkanSemaphore();
        BOOL         Create();
        void         Destroy();
        VkSemaphore& GetSemaphore();

    private:
        VkSemaphore m_pSemaphore;
#if semaphore_mem_lect
        uint8_t* m_memLeckBuffer;
#endif
    };

#define GfxTextureMemLeakDetect 0

    BOOL SaveTo4ChannelTgaMemFile(std::vector<uint8_t>& memFiledata, const uint8_t* pBuffer, uint32_t uBuffersize, uint32_t width, uint32_t height);
    BOOL SaveTo4ChannelTgaFile(const char* pFileName, const uint8_t* pBuffer, uint32_t uBuffersize, uint32_t width, uint32_t height);

    BOOL SaveTo4ChannelBmpMemFile(std::vector<uint8_t>& memFiledata, const uint8_t* pBuffer, uint32_t uBuffersize, uint32_t width, uint32_t height);
    BOOL SaveTo4ChannelBmpFile(const char* pFileName, const uint8_t* pBuffer, uint32_t uBuffersize, uint32_t width, uint32_t height);

    BOOL _Blit(VkFormat vkSrcFormat, VkImage srcImage, KBlitRegion blitSrc, VkFormat vkDstFormat, VkImage dstImage, KBlitRegion blitDst, bool fromSwapChain = false);

    class KVulkanRenderTarget2D : public KRenderTarget
    {
    protected:
        KVulkanTexture*                 m_pTextureResource = nullptr;
        IKGFX_TextureView*              m_pFullSRV         = nullptr;
        IKGFX_TextureView*              m_pFullUAV         = nullptr;
        IKGFX_TextureView*              m_pFullRTVorDSV    = nullptr;
        std::vector<IKGFX_TextureView*> m_MipmapSRVs;
        std::vector<IKGFX_TextureView*> m_MipmapUAVs;
        std::vector<IKGFX_TextureView*> m_MipmapRTVorDSVs;

        BOOL              m_bForDepth;
        BOOL              m_bHasStencil;
        BOOL              m_bOwnsImage;
        uint32_t          m_pixelByteSride;
        uint32_t          m_rowPitch;
        VkImageUsageFlags m_ImageUsage;

    public:
        KVulkanRenderTarget2D();
        virtual ~KVulkanRenderTarget2D();
        virtual BOOL IsRenderTarget() const override
        {
            return true;
        }

        BOOL Create(const KRenderTargetDesc* pDesc, BOOL bTileOptimize);
        BOOL Destroy();

        BOOL Blit(KRenderTarget* pSrcRT, KBlitRegion blitSrc, KBlitRegion blitDest, gfx::IKGFX_RenderContext* pCommandBuffer = nullptr);
        BOOL ReadPixels(void* pBytes, uint32_t ubytes);

        IKGFX_TextureView* GetSRV() const override;
        IKGFX_TextureView* GetUAV() const override;
        IKGFX_TextureView* GetFullRTV() const override;
        IKGFX_TextureView* GetFullDSV() const override;
        IKGFX_TextureView* GetFullSRV() const override;
        IKGFX_TextureView* GetFullUAV() const override;
        IKGFX_TextureView* GetMipSRV(uint32_t MipLevel, uint32_t uArraySlice) const override;
        IKGFX_TextureView* GetMipUAV(uint32_t MipLevel, uint32_t uArraySlice) const override;
        IKGFX_TextureView* GetMipRTV(uint32_t MipLevel, uint32_t uArraySlice) const override;
        IKGFX_TextureView* GetMipDSV(uint32_t MipLevel, uint32_t uArraySlice) const override;

        uint64_t        GetNameHash() override;
        int32_t         AddRef() override;
        int32_t         GetRef() override;
        int32_t         Release() override;
        bool            IsForDepth() override;
        bool            IsHasStencil() override;
        uint64_t        GetResourceSize() override;
        void            SetObjectName(const char* szName) override;
        bool            SaveToFile(const char* pcszSaveFilePath) override;
        uint64_t        GetId() override;
        uint32_t        GetMipMapCount() override;
        const char*     GetName() override;

        void*                  GetNativeImageHandle() const override;
        IKGFX_TextureResource* GetTextureResource() const override;

        const KGFX_TextureDesc&     GetTexDesc() const override;
        KGfxSubresourceRange ResolveSubresourceRange(const KGfxSubresourceRange& range) override;

    public:
        VkImage GetVkImage() const
        {
            return (VkImage)GetNativeImageHandle();
        }

    private:
        std::string m_szName;
    };

    class KVulkanLayout : public KGfxRef
    {
    public:
        KVulkanLayout();
        virtual ~KVulkanLayout();

        KVulkanLayout&        Begin();
        BOOL                  End(bool bCreatePipelineLayout = true);
        BOOL                  Destroy();
        KVulkanLayout&        AddLayout(uint32_t uSet, enumDescriptorType descriptorType, gfx::ShaderStageType shaderType, uint32_t binding, uint32_t descriptorCount = 1);
        KVulkanLayout&        AddBindlessLayout(uint32_t uSet, enumDescriptorType descriptorType, gfx::ShaderStageType shaderType, uint32_t binding, uint32_t maxDescriptorCount = 1);
        KVulkanLayout&        AddCombinedLayout(uint32_t uSet, enumDescriptorType descriptorType, gfx::ShaderStageType shaderType, uint32_t binding, uint32_t descriptorCount = 1, IKGFX_Sampler** pImmutableSamplers = nullptr);
        KVulkanLayout&        AddPushContantRange(gfx::ShaderStageType shaderType, size_t size, uint32_t offset);
        VkPipelineLayout      GetPipelineLayout() const;
        VkDescriptorSetLayout GetDesriptorSetLayout(uint32_t uSet) const;
        BOOL                  IsBindless(uint32_t uSet) const;
        uint32_t              GetLayoutSetCount() const;
        BOOL                  IsReady() const;
        BOOL                  IsDynamic(uint32_t uSet, uint32_t binding) const;

    private:
        struct _LayoutSet
        {
            std::vector<VkDescriptorSetLayoutBinding> m_vecDescriptorSetLayoutBinding;
            VkDescriptorSetLayout                     m_pDescriptorSetLayout = nullptr;
            bool                                      bBindless              = false;
            std::vector<VkDescriptorBindingFlagsEXT>  m_vecBindingFlags;
        };
        std::vector<_LayoutSet>          m_vecLayoutSet;
        std::vector<VkPushConstantRange> m_vecPushConstantRange;
        VkPipelineLayout                 m_pPipelineLayout;
    };

    class KVulkanDescriptorSet;
    class KVulkanDescriptorPool : public KGFX_DelayReleaseObject, public KGfxRef
    {
        static std::set<KVulkanDescriptorPool*> m_dirtyPool;

    public:
        KVulkanDescriptorPool();
        ~KVulkanDescriptorPool();
        KVulkanDescriptorPool& Begin(uint32_t maxSet = 16, bool bBindlessSet = false);
        KVulkanDescriptorPool& AddPoolItem(enumDescriptorType descriptorType, uint32_t uCount = 1);
        BOOL             End();
        BOOL             FreeDescriptorSet(KVulkanDescriptorSet* pDescriptorSet);
        VkDescriptorPool GetPool();
        void             IncreaseAllocedSet();
        BOOL             IsFull();

        KVulkanDescriptorPool* CreateFromHeader();
        static BOOL            IsDirtyDescriptorPool(KVulkanDescriptorPool* pPool);
        KVulkanDescriptorPool* GetNext();

    private:
        std::vector<VkDescriptorPoolSize> m_vecDescriptorPoolSize;
        uint32_t                          m_uMaxSet     = 0;
        uint32_t                          m_uAllocedSet = 0;
        VkDescriptorPool                  m_pDescriptorPool;
        bool                              m_bBindlessPool = false;
        uint32_t                          m_uPoolID       = 0;

    public:
        // 一个pool满了，就取申请下一个pool,自动扩容
        KVulkanDescriptorPool* m_pNextPool;

        // 上一个pool
        KVulkanDescriptorPool* m_pPreviousPool;
    };

    class KVulkanDescriptorSet : public KGFX_DelayReleaseObject, public KGfxRef
    {
    public:
        KVulkanDescriptorSet(const KVulkanLayout* pLayOut, KDescriptorPoolContainer* pPoolContainer);
        virtual ~KVulkanDescriptorSet();
        void ClearPoolContainer();
        KVulkanDescriptorSet& AddBindShareUBO(uint32_t uSet, uint32_t uBinding, IKGFX_Buffer* pUBO, uint32_t uSize, uint32_t uOffset, const char* pcszBlockName = nullptr);
        KVulkanDescriptorSet& AddBindUBO(uint32_t uSet, uint32_t uBinding, uint32_t uCount, IKGFX_Buffer* pUBO[], const char* pcszBlockName = nullptr);
        KVulkanDescriptorSet& AutoBindUBO(uint32_t uSet, uint32_t uBinding, uint32_t uCount, IKGFX_Buffer* pUBO[], const char* pcszBlockName = nullptr);
        KVulkanDescriptorSet& AddBindDynamicUBO(uint32_t uSet, uint32_t uBinding, uint32_t uCount, IKGFX_Buffer* pUBO[], const char* pcszBlockName = nullptr);
        KVulkanDescriptorSet& AddBindSSBO(uint32_t uSet, uint32_t uBinding, IKGFX_Buffer* pSSBO, uint32_t uOffset, uint32_t uRange, BOOL bUAV, const char* pcszBlockName = nullptr);
        KVulkanDescriptorSet& AddBindDynamicSSBO(uint32_t uSet, uint32_t uBinding, IKGFX_Buffer* pSSBO, uint32_t uOffset, uint32_t uRange, BOOL bUAV, const char* pcszBlockName = nullptr);
        KVulkanDescriptorSet& AutoBindSSBO(uint32_t uSet, uint32_t uBinding, IKGFX_Buffer* pSSBO, uint32_t uOffset, uint32_t uRange, BOOL bUAV, const char* pcszBlockName = nullptr);
        KVulkanDescriptorSet& AddBindSampler(uint32_t uSet, uint32_t uBinding, uint32_t uCount, IKGFX_Sampler* pSampler[]);
        KVulkanDescriptorSet& AddBindCombinedSampler(uint32_t uSet, uint32_t uBinding, uint32_t uCount, IKGFX_Sampler* pSampler[], const void* pImageViews);
        KVulkanDescriptorSet& AddBindSRVArray(uint32_t uSet, uint32_t uBinding, uint32_t uNum, IKGFX_TextureView* const* ppSRVs);
        KVulkanDescriptorSet& AddBindUAVArray(uint32_t uSet, uint32_t uBinding, uint32_t uNum, IKGFX_TextureView* const* ppSRVs);
        KVulkanDescriptorSet& AddBindSRV(uint32_t uSet, uint32_t uBinding, IKGFX_TextureView* pSRV);
        KVulkanDescriptorSet& AddBindUAV(uint32_t uSet, uint32_t uBinding, IKGFX_TextureView* pUAV);
        KVulkanDescriptorSet& AddBindCBV(uint32_t uSet, uint32_t uBinding, IKGFX_BufferView* pCBV);
        KVulkanDescriptorSet& AddBindRWBufferView(uint32_t uSet, uint32_t uBinding, uint32_t uCount, IKGFX_BufferView* pBufViews[]);
        KVulkanDescriptorSet& AddBindSampleBufferView(uint32_t uSet, uint32_t uBinding, uint32_t uCount, IKGFX_BufferView* pBufViews[]);
        KVulkanDescriptorSet& AddBindAccelerationStructure(uint32_t uSet, uint32_t uBinding, KRayTracingScene* accelerationStructure);
        KVulkanDescriptorSet& Begin();
        BOOL                  End();
        BOOL                  IsInited();
        BOOL                  HasError();
        void                  AddBindDynamicUBOIdToOffsetArray(uint32_t uSet);
        uint32_t              GetDynamicUBOOffsetArrayCount(uint32_t uSet);
        uint32_t*             GetDynamicUBOOffetArray(uint32_t uSet, BOOL bDebug = false);
        BOOL                  IsFillBindData();
        const VkDescriptorSet GetDescriptorSet(uint32_t uSet);
        uint32_t              GetSetCount();
        void                  SetDescriptorSet(uint32_t uSet, VkDescriptorSet pDes);
        void                  Clear();

        void ClearBarrier();
        void TransBarrier() const;

        virtual uint64_t GetCheckCode() const { return m_uUpdateCheckCode; }
        BOOL ValidCheck();

    protected:
        KVulkanDescriptorPool* m_pDescriptorPool;
        KDescriptorPoolContainer* m_pContainer;
        const KVulkanLayout* m_pLayout;

    public:
        std::vector<gfx::KGfxBarrier> m_vecBarriers = {}; // 记录当前set的barrier

    public:
        uint64_t m_uUpdateCheckCode;

#ifdef _DEBUG
        uint64_t m_uPreviousCheckCode = 0;
#endif
        uint64_t m_uProgramCheckCode = 66666666;
        int32_t  m_uLastUseFrameId = 0;
        bool     m_bAddedDelayRelease = false;

        struct BufferInfo
        {
            std::vector<IKGFX_Buffer*>            pGfxBuffers;
            std::vector<VkDescriptorBufferInfo> vkBufferInfos;
            uint32_t                            uBinding      = 0;
            uint32_t                            uCount        = 0;
            uint32_t                            alignSize     = 0;
            const char*                         pcszBlockName = nullptr;
        };

        struct GfxBufferInfo
        {
            GfxBufferInfo(uint32_t uBind, IKGFX_Buffer* pBuffer, const char* pBlockName)
                : uBinding(uBind)
                , pGfxBuffer(pBuffer)
                , pcszBlockName(pBlockName)
            {
            }

            uint32_t    uBinding;
            IKGFX_Buffer* pGfxBuffer;
            const char* pcszBlockName = nullptr;
        };

        struct ImageInfo
        {
            std::vector<VkDescriptorImageInfo> vkImageInfos;
#if DESCRIPTORSET_VALIDATE
            std::vector<KGfxRef*> vkImageRef;
#endif
            uint32_t                           uBinding;
            uint32_t                           uCount;
            uint32_t                           uDescriptorType;
            uint32_t                           dstArrayElement = UINT32_MAX;
#ifdef _DEBUG
            std::string strDebugName;
#endif
        };

        struct RWBufferViewInfo
        {
            std::vector<VkBufferView> vkBufferViews;
#if DESCRIPTORSET_VALIDATE
            std::vector<KGfxRef*> vkBufferRef;
#endif
            uint32_t                  uBinding;
            uint32_t                  uCount;
        };

        struct SamplerBufferViewInfo
        {
            std::vector<VkBufferView> vkBufferViews;
#if DESCRIPTORSET_VALIDATE
            std::vector<KGfxRef*> vkBufferRef;
#endif
            uint32_t                  uBinding;
            uint32_t                  uCount;
        };

        struct AccelerationStructureInfo
        {
            uint32_t                   uBinding              = UINT32_MAX;
            uint32_t                   uCount                = UINT32_MAX;
            VkAccelerationStructureKHR accelerationStructure = VK_NULL_HANDLE;
        };
        std::vector<VkDescriptorSet> m_vecDescriptorSet;

    private:
        struct _LayoutSetItem
        {
            std::vector<VkWriteDescriptorSet> m_vecWriteDescriptorSets;

            // std::vector<VkDescriptorBufferInfo> m_UBOinfos;
            std::vector<BufferInfo> m_UBOinfos;

            // std::vector<VkDescriptorBufferInfo> m_DynamicUBOinfos;
            std::vector<BufferInfo> m_DynamicUBOinfos;

            std::vector<BufferInfo> m_SSBOinfos;

            // std::vector<VkDescriptorBufferInfo> m_DynamicSSBOinfos;
            std::vector<BufferInfo> m_DynamicSSBOinfos;

            // std::vector<VkDescriptorImageInfo>  m_SamplerDescriptorsInfos;
            std::vector<ImageInfo> m_SamplerDescriptorsInfos;

            std::vector<ImageInfo> m_ImageTextureinfos;

            // std::vector<VkDescriptorImageInfo>  m_ImageSamplerinfos;
            std::vector<ImageInfo> m_ImageSamplerinfos;

            std::vector<ImageInfo> m_RWImageinfos;

            // texel buffers
            std::vector<RWBufferViewInfo> m_RWBufferViewInfos;

            // type buffers
            std::vector<SamplerBufferViewInfo> m_SamplerBufferViewInfos;

            std::vector<GfxBufferInfo>             m_vecBindDynamicUBO;
            std::vector<uint32_t>                  m_vecDynamicUBOOffsets;
            std::vector<AccelerationStructureInfo> m_accelerationStructureInfos;
        };

        std::vector<_LayoutSetItem> m_vecLayoutItem;

        void FitLayoutItemSize(uint32_t uSet);
        bool Compare(BufferInfo& l, BufferInfo& r) { return l.uBinding < r.uBinding; }

    public:
    private:
        BOOL m_bHasError;
    private:
#ifdef _DEBUG
        // 限制一帧只能Update一次
        int m_nLastUpdateFrameMoveCount{-1};
#endif
        

#if DESCRIPTORSET_VALIDATE
        uint32_t m_uLastRefSequenceCounter = 0;
#endif
    public:
        // 基类持有的只是header
        KVulkanDescriptorPool* m_pRealAllocPool;
    };

    class KVulkanVertexDescriptor : public KGfxRef
    {
    public:
        KVulkanVertexDescriptor();
        virtual ~KVulkanVertexDescriptor();
        KVulkanVertexDescriptor&              Begin();
        KVulkanVertexDescriptor&              AddBindDescription(uint32_t binding, uint32_t stride, enumVertexInputRate inputRate);
        KVulkanVertexDescriptor&              AddAttribute(uint32_t binding, uint32_t location, enumVertexFormat format, uint32_t offset);
        BOOL                                  End();
        VkPipelineVertexInputStateCreateInfo* GetInputStateCreateInfo();
        uint64_t                              GetHashCode();

    private:
        std::vector<VkVertexInputBindingDescription>   m_vecBindingDescriptions;
        std::vector<VkVertexInputAttributeDescription> m_vecAttributeDescriptions;
        VkPipelineVertexInputStateCreateInfo           m_InputState;
        uint64_t                                       m_uHashCode = 0;
    };

    typedef struct KRenderPassDesc
    {
        std::vector<enumTextureFormat>   vecColorFormats;
        std::vector<enumImageLayout>     vecColorInitImageLayout;
        std::vector<enumImageLayout>     vecColorFinalImageLayout;
        std::vector<enumLoadActionType>  vecLoadActionsColor;
        std::vector<enumSampleCountFlag> vecSampleCount;
        enumImageLayout                  depthInitLayout;
        enumImageLayout                  depthfinalLayout;
        enumSampleCountFlag              depthSampleCount;
        uint32_t                         uRenderTargetCount;
        enumTextureFormat                enuDepthStencilFormat;
        enumLoadActionType               enuLoadActionDepth;
        enumLoadActionType               enuLoadActionStencil;
        enumStoreActionType              enuStoreActionDepth;
        enumStoreActionType              enuStoreActionStencil;
        BOOL                             bDepthReadOnly;
        BOOL                             bHasDepth;
        BOOL                             bHasStencil;
        KRenderPassDesc()
        {
            Reset();
        }
        void Reset()
        {
            uRenderTargetCount = 0;
            enuDepthStencilFormat = gfx::GetDefaultDepthStencilFormat();
            enuLoadActionDepth = LOAD_ACTION_DONTCARE;
            enuLoadActionStencil = LOAD_ACTION_DONTCARE;
            enuStoreActionDepth = STORE_ACTION_STORE;
            enuStoreActionStencil = STORE_ACTION_STORE;
            depthInitLayout = IMAGE_LAYOUT_UNDEFINED;
            depthfinalLayout = IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthSampleCount = SAMPLE_COUNT_1_BIT;
            bDepthReadOnly = false;
            bHasDepth = false;
            bHasStencil = false;
        }
    } KRenderPassDesc;

#define memlect_RenderPass 0
    class KVulkanRenderPass
    {
        friend class KGraphicDevice;
        friend class KVulkanGraphicDevice;

    private:
        VkRenderPass    m_pRenderPass;
        KRenderPassDesc m_desc;
        uint64_t        m_uRenderPassHash;
        // BOOL CreateMainRenderPass();
        // BOOL CreateOffsetRenderPass();

    protected:
        ~KVulkanRenderPass();
        KVulkanRenderPass();

    public:
        // BOOL Create(KEnumRenderPass uRenderPassId);
        //  support to create any renderpass when needed 2019.1.16
        BOOL Create(KRenderPassDesc* pDesc);
        BOOL Destroy();


        VkRenderPass           GetPass();
        void*                  GetRenderPassPtr();
        const KRenderPassDesc* GetDesc() const;
        void                   SetObjectName(const char* szName);
        virtual BOOL           IsDepthReadOnly();
        virtual BOOL           IsHasDepth();
        virtual BOOL           IsHasStencil();
#if memlect_RenderPass
        uint8_t* m_pMemLectBuffer;
#endif
    };

    struct KSpecializationInfo
    {
        void                  Clear();
        void                  AddItem(const KSpecializationConstant& item);
        VkSpecializationInfo* Build();
        KSpecializationInfo();
        ~KSpecializationInfo();

    private:
        std::vector<KSpecializationConstant>  m_item{};
        std::vector<VkSpecializationMapEntry> m_entry{};
        VkSpecializationInfo                  m_info{};
        uint8_t*                              m_pData;
    };

    struct kSpecializationInfoAdapter
    {
        size_t      dataSize;
        const void* pData;

        kSpecializationInfoAdapter(size_t uSize, const void* data)
            : dataSize(uSize)
            , pData(data)
        {
        }

        void AddEntry(uint32_t constId, uint32_t offset, uint32_t size)
        {
            VkSpecializationMapEntry entry;
            entry.constantID = constId;
            entry.offset     = offset;
            entry.size       = size;
            mapEntrys.push_back(entry);
        }

        size_t GetEntryCount()
        {
            return mapEntrys.size();
        }

        VkSpecializationMapEntry* GetSpecializationMapEntrys()
        {
            return &mapEntrys[0];
        }

    private:
        std::vector<VkSpecializationMapEntry> mapEntrys;
    };

    class KSpecializationConstantContainer : public IKSpecializationConstantContainer
    {
    public:
        KSpecializationConstantContainer();
        ~KSpecializationConstantContainer();
        void                           AddFloat(uint32_t stageId, uint32_t constant_id, float floatValue) override;
        void                           AddInt(uint32_t stageId, uint32_t constant_id, int32_t intValue) override;
        void                           AddUInt(uint32_t stageId, uint32_t constant_id, uint32_t uintValue) override;
        const KSpecializationConstant& GetItem(uint32_t i) override;
        uint32_t                       GetItemCount() override;

        std::vector<KSpecializationInfo>     m_Info;
        std::vector<KSpecializationConstant> m_items;
    };

    // todo 把各种状态剥离出来
    class KVulkanGraphicsPipeline : public KGraphicsPipeline
    {
    public:
        KVulkanGraphicsPipeline();
        virtual ~KVulkanGraphicsPipeline();
        // virtual BOOL Create(KEnumRenderPass renderPassid, KRenderState *pRenderState, KVulkanVertexDescriptor *pVertexDescriptor, KVulkanLayout *pLayout, uint32_t uStageCount, KShaderStage *pStage[]);
        virtual BOOL Create(GraphicsPipelineDesc* pDesc, IKSpecializationConstantContainer* pSpecializationConstantContainer);
        virtual BOOL Destroy();
        VkPipeline   GetPipeline();
        void*        GetVkPipeline() override;

    private:
        KRenderState       m_RenderState;
        KVulkanVertexDescriptor* m_pVertexDescriptor;
        VkPipeline         m_pPipeline;
    };

    class KVulkanComputePipeline : public KComputePipeline
    {
    public:
        KVulkanComputePipeline();
        virtual ~KVulkanComputePipeline();
        virtual BOOL Create(ComputePipelineDesc* pDesc, IKSpecializationConstantContainer* pSpecializationConstantContainer) override;
        virtual BOOL Destroy();
        VkPipeline   GetPipeline();
        void*        GetVkPipeline() override;

    private:
        VkPipeline m_pPipeline;
    };

    struct KStageSamplerDef : public IKStageSamplerDef
    {
    public:
        KStageSamplerDef();
        ~KStageSamplerDef();
        BOOL                SetSamplerDef(const char* pSamplerDef) override;
        const char*         GetSamplerName() override;
        void                SetSamplerName(const char* pName) override;
        gfx::KSamplerState* GetSamplerState() override;

    private:
        std::string        m_strSamplerName;
        gfx::KSamplerState m_samplerState;
    };

    class KVulkanShaderStage : public KShaderStage
    {
    public:
        KVulkanShaderStage(int nPlatform);
        ~KVulkanShaderStage();
        BOOL                             LoadShader(const char* szShaderSource, const NSKBase::tagFileLocation& sIncludeShaderLoc, const char* szShaderDef, const char* szMacro, gfx::ShaderStageType shaderType, gfx::IShaderReflector* pReflector) override;
        BOOL                             CreateShader(uint32_t* pRetHash, BOOL* pRealBuild, gfx::IShaderReflector* pReflector) override;
        BOOL                             ReCreateShader(uint32_t* pRetHash, BOOL* pRealBuild, gfx::IShaderReflector* pReflector) override;
        VkPipelineShaderStageCreateInfo& GetCreateInfo();
        BOOL                             SetSpecializationMapEntry(uint32_t uStageSpecializationMapEntryCount, KSpecializationMapEntry pMapEntry[], void* pSpecializationData, uint32_t uSpecializationDataSize) override;
        const char*                      GetShaderName() override;
        virtual const char*              GetShaderCacheFilePath() override;
        virtual uint32_t                 GetPushConstantsSize() override;
        virtual KShaderInfo*             GetShaderInfo() override;

        virtual uint32_t           GetSamplerDefCount() override;
        virtual IKStageSamplerDef* GetSamplerDef(uint32_t i) override;
        virtual IKStageSamplerDef* GetSamplerDef(const char* pcszName) override;
        virtual void               ClearSamplerDef() override;
        virtual BOOL               AddSamplerDef(const char* pSamplerDef) override;
        virtual BOOL               AddSamplerDef(const char* pSamplerDefName, gfx::KSamplerState* pSamplerState) override;

        virtual const std::string& GetShaderContent() override;
        virtual const std::string& GetShaderMacro() override;
        virtual const std::string& GetShaderHeader() override;
        virtual const std::string& GetShaderBody() override;
        virtual void               SetShaderContent(const char* strMacro, const char* strHeader, const char* strBody) override;
        virtual void               SetShaderBody(std::string&& strBody) override;
        virtual void               ClearShaderContent() override;

        void                       SetMaterialID(int id) override { m_nMaterialID = id; }
        int                        GetMaterialID() override { return m_nMaterialID; }
        virtual void               SetShaderFileLoadFromCache() override;
        virtual BOOL               IsShaderFileLoadFromCache() override;
        virtual void               SetShaderFileSaveData(std::string&& strData) override;
        virtual const std::string& GetShaderFileSaveData() override;
        virtual void               SetEntryPoint(const char* szEntryPoint) override;
        virtual const char*        GetEntryPoint() override;
        virtual void* MoveOutShaderModule() override;
        KShaderInfo                    m_shaderInfo;
    private:
        vks::KShaderProgram*                  m_pShaderProgram;
        VkPipelineShaderStageCreateInfo       m_CreatInfo;
        VkSpecializationInfo                  m_specializationInfo;
        std::vector<VkSpecializationMapEntry> m_vecSpecializationMapEntries;

        std::string m_strShaderMacro;
        std::string m_strShaderHeader;
        std::string m_strShaderBody;
        std::string m_strShaderContent;


        std::vector<KStageSamplerDef*> m_vecStateSamplerDef;
        int                            m_nMaterialID;

        std::string m_strShaderFileSaveData;

        int m_nPlatform{0};
    };

    class KVulkanGfxQueue : public KGfxRef
    {
    public:
        ////////////////////////////////////////////////////////////////////
        KVulkanGfxQueue() {}
        ~KVulkanGfxQueue() {}

        void    SetQueue(VkQueue pQueue) { m_pQueue = pQueue; }
        VkQueue GetQueue() const { return m_pQueue; }
        enumForProcessType GetQueueType() const { return m_QueueType; };
        void               SetQueueType(enumForProcessType queueType) { m_QueueType = queueType; };

    private:
        enumForProcessType m_QueueType = (enumForProcessType)0;
        VkQueue m_pQueue = nullptr;
    };

    class KVulkanGraphicContext : public gfx::KGraphicContext
    {
    public:
        KVulkanGraphicContext();
        virtual ~KVulkanGraphicContext();

        BOOL Init(const gfx::KWindow* pWindowInfo) override;
        void UnInit() override;

        gfx::KWindow*                   GetWindowInfo() override;
        virtual BOOL                    ResizeWindow(gfx::KWindow* pWindow, BOOL bForce) override;
        // virtual uint32_t                GetRenderViewWidth() override;
        // virtual uint32_t                GetRenderViewHeight() override;
        virtual uint32_t                GetRenderTargetWith() override;
        virtual uint32_t                GetRenderTargetHeight() override;
        virtual gfx::enumGraphicContext GetGraphicContextId() override;
        virtual void                    ActiveView(uint32_t viewId) override {}
        virtual uint32_t                GetActiveViewId() override { return 0; }

    private:
        BOOL _InitSwapchain();
        BOOL _ResizeSwapChain();

    private:
        KWindow* m_pWindowInfo;
        VkFormat m_DepthFormat;

    public:
        virtual gfx::KVulkanSwapChain* GetSwapChains() override;

    private:
        KVulkanSwapChain* m_pSwapChain;
        uint32_t _GetSwapChainImageCount();

    public:
        virtual enumTextureFormat GetSwapChainColorFormat() override;
        virtual enumTextureFormat GetSwapChainDepthFormat() override;

    public:
        virtual std::vector<KVulkanCommandBuffer*>& GetSwapChainCommandBuffers() override;
        virtual KVulkanCommandBuffer*               GetSwapChainCommandBuffer(uint32_t id) override;

    private:
        std::vector<KVulkanCommandBuffer*> m_vecCommandBuffers;
        BOOL                         _CreateCommandBuffers();
        BOOL                         _DestroyCommandBuffers();

    public:
        virtual gfx::KVulkanFence* GetSwapChainFence(uint32_t uFenceIndex) override;

    private:
        std::vector<gfx::KVulkanFence*> m_vecSwapChainFences;
        BOOL                      _CreateSwapChainFences();
        BOOL                      _DestroySwapChainFences();

    public:
        virtual std::vector<KRenderTarget*>& GetSwapChainRenderTarget() override;

    public:
        virtual KRenderTarget* GetSwapChainDepthStencilRT() override;

    private:
        KRenderTarget* m_pDepthStencil;
        BOOL           _CreateDepthStencilRT();
        BOOL           _DestroyDepthStencilRT();

    public:
        virtual uint32_t       GetSwapChainImageIndex() override;
        virtual void           ActiveSwapChainImage(uint32_t id) override;
        virtual uint32_t       GetSwapChainImageCount() override;
        virtual KRenderTarget* GetCurSwapChainRenderTarget() override;

    private:
        uint32_t m_uCurrentBufferIndex;

    public:
        virtual void GetRenderCompleteSemaphoreA(gfx::KVulkanSemaphore** pSemaphoreA, uint32_t uImageSemaphoreId) override;
        virtual void GetImageAcquiredSemaphoreA(gfx::KVulkanSemaphore** pSemaphoreA, uint32_t uImageSemaphoreId) override;

    public:
        std::vector<KVulkanSemaphore*> m_vecRenderCompleteSemaphoreA;
        std::vector<KVulkanSemaphore*> m_vecImageAcquiredSemaphoreA;

    private:
        BOOL _CreateSwapChainSemaphoreA();
        BOOL _DestroySwapChainSemaphoreA();
    };

    class KVulkanQueryHeap : public KQueryHeap
    {
    public:
        KVulkanQueryHeap();
        virtual ~KVulkanQueryHeap();

    public:
        VkQueryPool pVkQueryPool = nullptr;
    };
    class KVulkanSampler;
    class KVulkanSamplerBindlessView : public IKGFX_SamplerBindlessView
    {
    public:
        KVulkanSamplerBindlessView(KVulkanSampler* resouce);
        ~KVulkanSamplerBindlessView();
    public:
        // 通过 IKGFX_SamplerBindlessView 继承
        uint32_t GetBindlessHandle() override;
        const KSamplerState& GetSamplerState() override;
        IKGFX_Sampler* GetResource() override;
    private:
        uint32_t m_uBindlessHandle = UINT32_MAX;
        KVulkanSampler* m_pResouce = nullptr;
    };

    class KVulkanSampler : public IKGFX_Sampler
    {
    public:
        KVulkanSampler();
        ~KVulkanSampler();
        BOOL                 Create(const KSamplerState* pSamplerState);
        BOOL                 Destroy();
        const KSamplerState& GetSamplerState() override;
        uint64_t             GetCode();
        virtual uintptr_t    GetNativeHandle() override;
        IKGFX_SamplerBindlessView* GetBindlessView() override;
    public:
        VkSampler  GetVKSampler();
        VkSampler* GetVkSamplerPtr();

    private:

        VkSampler     m_pVkSampler = nullptr;
        KSamplerState m_samplerSate;
        IKGFX_SamplerBindlessView* m_pBindlessView = nullptr;
      
    };

    class KGFX_SwapchainVK : public IKGFX_Swapchain
    {
    public:
        KGFX_SwapchainVK();
        virtual ~KGFX_SwapchainVK();

        BOOL Init(const gfx::KWindow* pWindowInfo) override;
        BOOL UnInit() override;

        BOOL BeginRender() override;
        BOOL Present() override;

        BOOL          OnResize() override;
        gfx::KWindow* GetWindow() override;

        uint32_t            GetSwapChainRTCount() override;
        gfx::KRenderTarget* GetCurerntSwapChainRT() override;
        gfx::KRenderTarget* GetDepthStencilRT() override;

        BOOL     IsAcquiredImage() override;
        BOOL     IsBeginRender() override;

        enumTextureFormat GetSwapChainColorFormat() override;
        enumTextureFormat GetSwapChainDepthFormat() override;

    private:
        BOOL BeginSwapchainCommand();
        BOOL EndSwapchainCommand();
        BOOL SwapChainPresent(KVulkanSemaphore* pWaitSemaphore);

    private:
        gfx::KGraphicContext*   m_pGraphicContext{nullptr};
        BOOL                    m_bAcquiredImage{false};
        gfx::KVulkanSemaphore*  m_pCurrentImageAcquiredSemaphoreA{nullptr};
        uint32_t                m_uCurrentImageSemaphoreId{0};

        gfx::KWindow* m_pWindow{nullptr};
        BOOL          m_bOnResizeProcessing{false};
        BOOL          m_bBeginRender{false};
        BOOL          m_bMainCommandBegan{false};
        float         m_fPresentTimeMs{0.0f};
    };

    extern VkFormat GetTextureFormatFromTargetFormat(enumTextureFormat srcfmt, BOOL& bColorAttach, BOOL& bDepth, BOOL& bStencil, uint32_t& bytesStride);

    using RenderPassMap = std::unordered_map<uint64_t, KVulkanRenderPass*>;
    extern RenderPassMap& get_render_pass_map();

    using FrameBufferMap = std::unordered_map<uint64_t, KVulkanRenderFrameBuffer*>;
    // extern FrameBufferMap& get_frame_buffer_map();
} // namespace gfx
