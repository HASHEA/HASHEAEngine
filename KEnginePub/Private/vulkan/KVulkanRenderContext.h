#pragma once
#include "KEnginePub/Public/IGFX_Public.h"
#include "KVulkanUploadCmdBufferManager.h"
#include <memory_resource>

namespace gfx
{
    class KVulkanGraphicsCommandRecorder;

    class KVulkanRenderContext : public IKGFX_RenderContext
    {
    public:
        // KVulkanRenderContext implement
        KVulkanRenderContext();
        virtual ~KVulkanRenderContext();

        BOOL Init();
        void UnInit();

        virtual bool IsValid() const override;

        uint32_t GetCommandBufferId() const;
        VkCommandBuffer GetCommandBufferVk() const { return m_pVkCmdBuffer; }
        KVulkanCommandBuffer* GetVulkanCommandBuffer() const { return m_pVulkanCmdBuffer; }
        void SetCurrentFrameBuffer(gfx::IKGFX_RenderFrameBuffer* pRenderFrameBuffer) { m_pCurrentFrameBuffer = pRenderFrameBuffer; }
        gfx::IKGFX_RenderFrameBuffer* GetCurrentFrameBuffer() const { return m_pCurrentFrameBuffer; }
        void BeginRenderPass(const VkRenderPassBeginInfo& InBeginInfo, VkSubpassContents ePassContents, bool bImmediateMode);
        void EndRenderPass();
        bool IsInRenderPass() const;
        void CmdBindPipeline(enumPipelineBindPoint eBindPoint, KPipeline* pPipeline);
        void CmdBindDescriptorSets(enumPipelineBindPoint eBindPoint, const KVulkanLayout* pLayOut, uint32_t uSet, KVulkanDescriptorSet* pDescriptorSet, uint32_t uDynamicOffsetCount, const uint32_t auDynamicOffsets[]);
        void CmdPushConstants(const KVulkanLayout* cpLayOut, gfx::ShaderStageType eShaderStage, int nOffset, int nSize, const void* pValues);

        // 获取当前渲染通道（RenderPass）的计数, 当在renderPass中调用时，返回当前渲染通道的数量，否则返回(uint64_t)-1.
        uint64_t GetCurRenderPassCount() const;

    public:
        // IKGFX_RenderContext interface 
        virtual void* GetCommandBufferNativeHandle() const override;
        virtual BOOL BeginCommandBuffer() override;
        virtual void SubmitCommandBuffer(BOOL bWait = FALSE, void* pGpuCompletedSignal = nullptr) override;

    public:
        virtual void CmdClearAttachment(const KClearAttchment* pAttachment, int nCount, const NSKMath::KVectorInt2& v2Offset, const NSKMath::KVectorUint2& v2Size, uint32_t uBaseArrayLayer = 0, uint32_t uLayerCount = 1) override;
        virtual void CmdClearTextureView(IKGFX_TextureView* view, KClearValue clearValue, KGFX_ClearResourceViewFlags flags) override;
        virtual void CmdClearBufferView(IKGFX_BufferView* view, KClearValue clearValue, KGFX_ClearResourceViewFlags flags) override;

    public:
        virtual void CmdSetLineWidth(float linewidth) override;
        virtual void CmdSetDepthBias(float fConstant, float fClamp, float fSlop, bool bAutoReverse = true) override;

        virtual void CmdSetScissor(int nX, int nY, int nWidth, int nHeight) override;
        virtual void CmdSetScissor(const IKGFX_RenderFrameBuffer* piRenderFrameBuffer) override;

        virtual void CmdSetViewport(const ViewportDescription& Viewport) override;
        virtual void CmdSetViewport(const IKGFX_RenderFrameBuffer* piRenderFrameBuffer) override;

        virtual void CmdBindVertexBuffers(int nFirstBinding, int nBindingCount, IKGFX_Buffer* apBuffer[], int anOffsets[], uint32_t* stride = nullptr) override;
        virtual void CmdBindIndexBuffer(IKGFX_Buffer* pBuffer, int nOffset, enumIndexType indexType) override;

        virtual void CmdDraw(int nVertexCount, int nFirstVertex, bool bPoint = false) override;
        virtual void CmdDrawInstanced(int nVertexCount, int nFirstVertex, int nInstanceCount, int nFirstInstance) override;
        virtual void CmdDrawIndexed(int nIndexCount, int nInstanceCount, int nFirstIndex, int nVertexOffset, int nFirstInstance) override;
        virtual void CmdDrawIndexedInstanced(int nIndexCount, int nFirstIndex, int nInstanceCount, int nFirstVertex, int nFirstInstance) override;
        virtual void CmdDrawIndexedIndirect(IKGFX_Buffer* pIndirectCmdBuffer, int nOffset, int nDrawCount, int nStride, bool bRecordDrawCall = true) override;
        virtual void CmdDrawIndirect(IKGFX_Buffer* pIndirectCmdBuffer, int nOffset, int nDrawCount, int nStride) override;

    public:
        virtual BOOL CmdDispatch(int nGroupCountX, int nGroupCountY, int nGroupCountZ) override;
        virtual BOOL CmdDispatchIndirect(gfx::IKGFX_Buffer* pIndirectBuffer, int nOffset) override;

    public:
        virtual BOOL Transition(const KGfxBarrier& sBarrierInfo) override;
        virtual BOOL Transition(const std::initializer_list<KGfxBarrier>& lsBarrierInfosArray) override;
        virtual BOOL Transition(const KGfxBarrier* pBarrierInfos, uint32_t uBarrierCount) override;

    public:
        virtual void CmdUpdateSubResource(IKGFX_Buffer* pGfxBuffer, uint32_t uOffset, uint32_t uSize, const void* pData, uint32_t option = 0) override;
        virtual void CmdUpdateAllResource(IKGFX_TextureResource* pGfxTexure, std::vector<gfx::KGfxSubResourceData>& data) override;
        virtual void CmdUpdateSubResource(IKGFX_TextureResource* pGfxTexure, uint32_t uDstMipLevel, uint32_t uDstArraySlice, const KGfxCopyRegion* pDstRegion, const void* pSrcData, uint32_t uSrcRowPitch, uint32_t SrcDepthPitch) override;

        virtual void CmdCopyBuffer(IKGFX_Buffer* pSrcBuffer, IKGFX_Buffer* pDstBuffer) override;
        virtual void CmdCopyBufferSubRegions(IKGFX_Buffer* pSrcBuffer, IKGFX_Buffer* pDstBuffer, uint32_t uCopyRegionCount, const KBufferCopyRegion* pCopyRegions) override;
        virtual void CmdCopyTexture(IKGFX_TextureResource* pSrcTexture, IKGFX_TextureResource* pDstTexture) override;
        virtual void CmdCopyTextureSubRegions(IKGFX_TextureResource* pSrcTexture, IKGFX_TextureResource* pDstTexture, uint32_t uCopyRegionCount, const KTextureCopyRegion* pCopyRegions) override;

        // 拷贝纹理到缓冲区
        // pBufferTextureCopy和NumBufferTextureCopy为nullptr和0时，表示拷贝整个纹理到缓冲区
        virtual void CmdCopyTextureToBuffer(IKGFX_TextureResource* pSrcTexture, IKGFX_Buffer* pDstBuffer, const KBufferTextureCopy* pBufferTextureCopy, uint32_t NumBufferTextureCopy) override;

        // 拷贝缓冲区到纹理
        // pBufferTextureCopy和NumBufferTextureCopy为nullptr和0时，表示拷贝整个缓冲区到纹理
        virtual void CmdCopyBufferToTexture(IKGFX_Buffer* pSrcBuffer, IKGFX_TextureResource* pDstTexture, const KBufferTextureCopy* pBufferTextureCopy, uint32_t NumBufferTextureCopy) override;

    public:
        virtual void BeginDebugLabel(const char* strDebugLabel) override;
        virtual void EndDebugLabel() override;
        virtual void BeginOptickProfile() override;
        virtual void EndOptickProfile() override;

    public:
        virtual BOOL CmdInsertSignalFence(KSignalFence* pSignalFence) override;
        virtual void CmdClose() override;
        void CmdBeginUAVOverlap(IKGFX_Resource* const* ppResourceUAV, uint32_t count) override;
        void CmdEndUAVOverlap(IKGFX_Resource* const* ppResourceUAV, uint32_t count) override;

    protected:
        KVulkanCommandBuffer* m_pVulkanCmdBuffer{ nullptr };
        VkCommandBuffer       m_pVkCmdBuffer{ nullptr };
        gfx::IKGFX_RenderFrameBuffer* m_pCurrentFrameBuffer = nullptr;

    private:
        BOOL m_bBegun = false;
        int32_t m_nPreCommandIndex = 0;
        int32_t m_nCurCommandIndex = 0;
        KVulkanCommandBuffer* m_vCommandBuffers[MAX_SWAP_CHAIN_COUNT] = { nullptr };

        KVulkanGraphicsCommandRecorder* m_GraphicsCmdRecorder = nullptr;
        uint64_t m_nCurRenderPassCount = 1;

        uint32_t m_UAVOverlapCounter = 0;
    };

    class KVulkanGraphicsCommandRecorder
    {
    public:
        KVulkanGraphicsCommandRecorder(KVulkanRenderContext* pRenderCtx);
        ~KVulkanGraphicsCommandRecorder();

    public:
        void Begin(const VkRenderPassBeginInfo& InBeginInfo, VkSubpassContents ePassContents, bool bImmediateMode);
        void End();
        bool IsInGraphicsPipelineScope() const;
        bool IsInCmdRecording() const;
        bool IsDeferredMode() const;

        void CmdClearAttachments(VkCommandBuffer pCmdBuffer, const std::vector<VkClearAttachment>& vkClearAttachments, const VkClearRect& vkClearRect);

        void CmdSetLineWidth(VkCommandBuffer pCmdBuffer, float linewidth);
        void CmdSetDepthBias(VkCommandBuffer pCmdBuffer, float fConstant, float fClamp, float fSlope);
        void CmdSetScissor(VkCommandBuffer pCmdBuffer, const VkRect2D& ScissorRect);
        void CmdSetViewport(VkCommandBuffer pCmdBuffer, const VkViewport& Viewport);

        void CmdBindVertexBuffers(VkCommandBuffer pCmdBuffer, int nFirstBinding, int nBindingCount, VkBuffer apBuffer[], VkDeviceSize anOffsets[]);
        void CmdBindIndexBuffer(VkCommandBuffer pCmdBuffer, VkBuffer pBuffer, int nOffset, VkIndexType indexType);

        void CmdDraw(VkCommandBuffer pCmdBuffer, int nVertexCount, int nFirstVertex);
        void CmdDrawInstanced(VkCommandBuffer pCmdBuffer, int nVertexCount, int nFirstVertex, int nInstanceCount, int nFirstInstance);
        void CmdDrawIndexed(VkCommandBuffer pCmdBuffer, int nIndexCount, int nInstanceCount, int nFirstIndex, int nVertexOffset, int nFirstInstance);
        void CmdDrawIndexedIndirect(VkCommandBuffer pCmdBuffer, VkBuffer pIndirectCmdBuffer, int nOffset, int nDrawCount, int nStride);
        void CmdDrawIndirect(VkCommandBuffer pCmdBuffer, VkBuffer pIndirectCmdBuffer, int nOffset, int nDrawCount, int nStride);

        void CmdBindPipeline(VkCommandBuffer pCmdBuffer, VkPipelineBindPoint eBindPoint, VkPipeline pPipeline);
        void CmdBindDescriptorSets(VkCommandBuffer pCmdBuffer, VkPipelineBindPoint eBindPoint, VkPipelineLayout pLayout, uint32_t uSet, VkDescriptorSet pDescriptorSet, uint32_t uDynamicOffsetCount, const uint32_t auDynamicOffsets[]);
        void CmdPushConstants(VkCommandBuffer pCmdBuffer, VkPipelineLayout pLayout, VkShaderStageFlags eShaderStage, int nOffset, int nSize, const void* pValues);

        void BeginDebugLabel(VkCommandBuffer pCmdBuffer, const char* szDebugLabel);
        void EndDebugLabel(VkCommandBuffer pCmdBuffer);

        void BeginOptickProfile();
        void EndOptickProfile();

    private:
        void _PushRenderStateCmd();

        enum class CMD_ELEM_TYPE
        {
            CMD_ELEM_DRAW,
            CMD_ELEM_RNEDERSTATE,
            CMD_ELEM_PUSHCONSTANTS,
            CMD_ELEM_VERTEXBIND,
            CMD_ELEM_INDEXBIND,
            CMD_ELEM_DESCRIPTORSET,
            CMD_ELEM_PIPELINE,
            CMD_ELEM_CLEARATTACHMENTS,
            CMD_ELEM_BEGINDEBUGLABEL,
            CMD_ELEM_ENDDEBUGLABEL,
            CMD_ELEM_BEGINOPTICKPROFILE,
            CMD_ELEM_ENDOPTICKPROFILE,

            CMD_ELEM_TYPE_NUM,
            CMD_ELEM_UNKNOWN,
        };

        struct CMD_ELEM
        {
            CMD_ELEM_TYPE eCmdElemType = CMD_ELEM_TYPE::CMD_ELEM_UNKNOWN;

            CMD_ELEM(CMD_ELEM_TYPE eType) : eCmdElemType(eType) {}
            virtual ~CMD_ELEM() {}

            virtual void Execute(VkCommandBuffer pCmdBuffer, KVulkanRenderContext* pRenderCtx) = 0;
        };

        CMD_ELEM* _RequestCmdElem(CMD_ELEM_TYPE eElemType);
        void _ClearCmdRecordList();

    private:
        struct DEPTH_BIAS_PARAMS
        {
            float fConstant = 0.0f;
            float fClamp = 0.0f;
            float fSlope = 0.0f;
        };

        enum class CMD_APPLY_ELEM : uint8_t
        {
            CMD_VIEWPORT    = (1 << 0),
            CMD_SCISSOR     = (1 << 1),
            CMD_DEPTHBIAS   = (1 << 2),
            CMD_LINEWIDTH   = (1 << 3),
        };

        enum class DRAW_CMD_TYPE : uint8_t
        {
            CMD_DRAW,
            CMD_DRAW_INSTANCED,
            CMD_DRAW_INDEXED,
            CMD_DRAW_INDIRECT,
            CMD_DRAW_INDEXED_INDIRECT,
        };

        union DRAW_PARAM
        {
            struct DRAW
            {
                int nVertexCount;
                int nFirstVertex;
            } Draw;

            struct DRAW_INSTANCE
            {
                int nVertexCount;
                int nFirstVertex;
                int nInstanceCount;
                int nFirstInstance;
            } DrawInstanced;

            struct DRAW_INDEXED
            {
                int nIndexCount;
                int nInstanceCount;
                int nFirstIndex;
                int nVertexOffset;
                int nFirstInstance;
            } DrawIndexed;

            struct DRAW_INDIRECT
            {
                VkBuffer pIndirectCmdBuffer;
                int nOffset;
                int nDrawCount;
                int nStride;
            } DrawIndirect;

            struct DRAW_INDEXED_INDIRECT
            {
                VkBuffer pIndirectCmdBuffer;
                int nOffset;
                int nDrawCount;
                int nStride;
            } DrawIndexedIndirect;
        };

        struct DRAW_CMD : public CMD_ELEM
        {
            DRAW_CMD() : CMD_ELEM(CMD_ELEM_TYPE::CMD_ELEM_DRAW) {}

            DRAW_CMD_TYPE eDrawCmdType = DRAW_CMD_TYPE::CMD_DRAW;
            DRAW_PARAM DrawParam = {};

            void Execute(VkCommandBuffer pCmdBuffer, KVulkanRenderContext* pRenderCtx) override;
        };

        struct RNEDERSTATE_CMD : public CMD_ELEM
        {
            RNEDERSTATE_CMD() : CMD_ELEM(CMD_ELEM_TYPE::CMD_ELEM_RNEDERSTATE) {}

            uint8_t uCmdApplyElemBits = 0; // Flags Bits: CMD_APPLY_ELEM 
            VkViewport Viewport = {};
            VkRect2D ScissorRect = {};
            float fLineWidth = 0.0f;
            DEPTH_BIAS_PARAMS DepthBias;

            void Clear()
            {
                uCmdApplyElemBits = 0;
                Viewport = {};
                ScissorRect = {};
                fLineWidth = 0.0f;
                DepthBias = {};
            }

            void Execute(VkCommandBuffer pCmdBuffer, KVulkanRenderContext* pRenderCtx) override;
        };

        struct PUSHCONSTANT_CMD : public CMD_ELEM
        {
            PUSHCONSTANT_CMD() : CMD_ELEM(CMD_ELEM_TYPE::CMD_ELEM_PUSHCONSTANTS) {}

            std::vector<uint8_t> PushConstants;
            VkPipelineLayout PipelineLayout = nullptr;
            VkShaderStageFlags StageFlags = 0;
            int nOffset = 0;

            void Execute(VkCommandBuffer pCmdBuffer, KVulkanRenderContext* pRenderCtx) override;
        };

        struct VERTEXBIND_CMD : public CMD_ELEM
        {
            VERTEXBIND_CMD() : CMD_ELEM(CMD_ELEM_TYPE::CMD_ELEM_VERTEXBIND) {}

            VkBuffer vkBuffer[KMAX_BIND_VERT_STREAM] = { nullptr };
            VkDeviceSize uOffset[KMAX_BIND_VERT_STREAM] = { 0 };
            int nFirstBinding = 0;
            int nBindingCount = 0;

            void Execute(VkCommandBuffer pCmdBuffer, KVulkanRenderContext* pRenderCtx) override;
        };

        struct INDEXBIND_CMD : public CMD_ELEM
        {
            INDEXBIND_CMD() : CMD_ELEM(CMD_ELEM_TYPE::CMD_ELEM_INDEXBIND) {}

            VkBuffer vkBuffer = nullptr;
            int nOffset = 0;
            VkIndexType eIndexType = VK_INDEX_TYPE_UINT16;

            void Execute(VkCommandBuffer pCmdBuffer, KVulkanRenderContext* pRenderCtx) override;
        };

        struct DESCRIPTORSET_CMD : public CMD_ELEM
        {
            DESCRIPTORSET_CMD() : CMD_ELEM(CMD_ELEM_TYPE::CMD_ELEM_DESCRIPTORSET) {}

            VkPipelineLayout vkLayout = nullptr;
            uint32_t uSet = 0;
            VkDescriptorSet DescriptorSet = nullptr;
            std::vector<uint32_t> DynamicOffsets;

            void Execute(VkCommandBuffer pCmdBuffer, KVulkanRenderContext* pRenderCtx) override;
        };

        struct PIPELINE_CMD : public CMD_ELEM
        {
            PIPELINE_CMD() : CMD_ELEM(CMD_ELEM_TYPE::CMD_ELEM_PIPELINE) {}

            VkPipeline vkPipeline = nullptr;

            void Execute(VkCommandBuffer pCmdBuffer, KVulkanRenderContext* pRenderCtx) override;
        };

        struct CLEARATTACHMENTS_CMD : public CMD_ELEM
        {
            CLEARATTACHMENTS_CMD() : CMD_ELEM(CMD_ELEM_TYPE::CMD_ELEM_CLEARATTACHMENTS) {}

            std::vector<VkClearAttachment> ClearAttchments;
            VkClearRect ClearRect;

            void Execute(VkCommandBuffer pCmdBuffer, KVulkanRenderContext* pRenderCtx) override;
        };

        struct BEGINDEBUGLABEL_CMD : public CMD_ELEM
        {
            BEGINDEBUGLABEL_CMD() : CMD_ELEM(CMD_ELEM_TYPE::CMD_ELEM_BEGINDEBUGLABEL) {}

            std::string szDebugLabel;
            void Execute(VkCommandBuffer pCmdBuffer, KVulkanRenderContext* pRenderCtx) override;
        };

        struct ENDDEBUGLABEL_CMD : public CMD_ELEM
        {
            ENDDEBUGLABEL_CMD() : CMD_ELEM(CMD_ELEM_TYPE::CMD_ELEM_ENDDEBUGLABEL) {}

            void Execute(VkCommandBuffer pCmdBuffer, KVulkanRenderContext* pRenderCtx) override;
        };

        struct BEGINOPTICKPROFILE_CMD : public CMD_ELEM
        {
            BEGINOPTICKPROFILE_CMD() : CMD_ELEM(CMD_ELEM_TYPE::CMD_ELEM_BEGINOPTICKPROFILE) {}

            void Execute(VkCommandBuffer pCmdBuffer, KVulkanRenderContext* pRenderCtx) override;
        };

        struct ENDOPTICKPROFILE_CMD : public CMD_ELEM
        {
            ENDOPTICKPROFILE_CMD() : CMD_ELEM(CMD_ELEM_TYPE::CMD_ELEM_ENDOPTICKPROFILE) {}

            void Execute(VkCommandBuffer pCmdBuffer, KVulkanRenderContext* pRenderCtx) override;
        };

        bool m_bBeginRecord = false;
        bool m_bDeferredMode = false;
        KVulkanRenderContext* m_pCurrentRenderCtx = nullptr;
        VkRenderPassBeginInfo m_RenderBeginInfo = { };
        VkSubpassContents m_ePassContents = VK_SUBPASS_CONTENTS_INLINE;

        RNEDERSTATE_CMD m_CurRenderStateCmd;
        std::vector<CMD_ELEM*> m_CmdRecordList;

        template<typename T>
        class CmdPool
        {
        public:
            CmdPool(std::pmr::unsynchronized_pool_resource& InCmdEventPoolResource)
                : m_Cmds(&InCmdEventPoolResource)
            {}

            T* Alloc()
            {
                return &m_Cmds.emplace_back();
            }

            void Clear()
            {
                m_Cmds.clear();
            }

        private:
            std::pmr::list<T> m_Cmds;
        };

        using DRAW_CMD_LIST = CmdPool<DRAW_CMD>;
        using RNEDERSTATE_CMD_LIST = CmdPool<RNEDERSTATE_CMD>;
        using PUSHCONSTANT_CMD_LIST = CmdPool<PUSHCONSTANT_CMD>;
        using VERTEXBIND_CMD_LIST = CmdPool<VERTEXBIND_CMD>;
        using INDEXBIND_CMD_LIST = CmdPool<INDEXBIND_CMD>;
        using DESCRIPTORSET_CMD_LIST = CmdPool<DESCRIPTORSET_CMD>;
        using PIPELINE_CMD_LIST = CmdPool<PIPELINE_CMD>;
        using CLEARATTACHMENTS_CMD_LIST = CmdPool<CLEARATTACHMENTS_CMD>;
        using BEGINDEBUGLABEL_CMD_LIST = CmdPool<BEGINDEBUGLABEL_CMD>;
        using ENDDEBUGLABEL_CMD_LIST = CmdPool<ENDDEBUGLABEL_CMD>;
        using BEGINOPTICKPROFILE_CMD_LIST = CmdPool<BEGINOPTICKPROFILE_CMD>;
        using ENDOPTICKPROFILE_CMD_LIST = CmdPool<ENDOPTICKPROFILE_CMD>;

        std::pmr::unsynchronized_pool_resource m_CmdEventPoolResource;
        DRAW_CMD_LIST*                  m_PoolDrawCmds = nullptr;
        RNEDERSTATE_CMD_LIST*           m_PoolRenderStateCmds = nullptr;
        PUSHCONSTANT_CMD_LIST*          m_PoolPushConstantCmds = nullptr;
        VERTEXBIND_CMD_LIST*            m_PoolVertexBindCmds = nullptr;
        INDEXBIND_CMD_LIST*             m_PoolIndexBindCmds = nullptr;
        DESCRIPTORSET_CMD_LIST*         m_PoolDescriptorSetCmds = nullptr;
        PIPELINE_CMD_LIST*              m_PoolPipelineCmds = nullptr;
        CLEARATTACHMENTS_CMD_LIST*      m_PoolClearAttachmentsCmds = nullptr;
        BEGINDEBUGLABEL_CMD_LIST*       m_PoolBeginDebugLabelCmds = nullptr;
        ENDDEBUGLABEL_CMD_LIST*         m_PoolEndDebugLabelCmds = nullptr;
        BEGINOPTICKPROFILE_CMD_LIST*    m_PoolBeginOptickProfileCmds = nullptr;
        ENDOPTICKPROFILE_CMD_LIST*      m_PoolEndOptickProfileCmds = nullptr;
    };
}
