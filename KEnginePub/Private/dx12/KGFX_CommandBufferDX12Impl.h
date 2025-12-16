#pragma once
#include "KGFX_BarrierDx12.h"
#include "KGFX_BufferDx12.h"
#include "KGFX_BufferImplDX12.h"
#include "KGFX_RefPtr.h"
#include "KGFX_TextureImplDx12.h"
#include "KEnginePub/Public/KProfileTools.h"


namespace gfx
{
    class KGFX_PipelineDX12;
    class KGFX_TransientHeapDX12;
    class KGFX_BufferImplDX12;

    /**
     * Translate虽然也可以独立在一个queue上，不过我们把它的功能放在Graphics和compute的queue上，并不单独独立出来
     */
    class KGFX_TranslateCommandBufferDX12
    {
        friend class KGFX_CommandBufferDX12Impl;
    public:
        KGFX_TranslateCommandBufferDX12() = default;
        ~KGFX_TranslateCommandBufferDX12();
        KGFX_TranslateCommandBufferDX12(const KGFX_TranslateCommandBufferDX12&) = delete;
        KGFX_TranslateCommandBufferDX12& operator=(const KGFX_TranslateCommandBufferDX12&) = delete;
        KGFX_TranslateCommandBufferDX12(KGFX_TranslateCommandBufferDX12&&) = delete;
        KGFX_TranslateCommandBufferDX12& operator=(KGFX_TranslateCommandBufferDX12&&) = delete;

        void Init(ID3D12GraphicsCommandList* d3dCommandList, KGFX_TransientHeapDX12* transientHeap);

        void ReInit(ID3D12GraphicsCommandList* d3dCommandList, KGFX_TransientHeapDX12* transientHeap);

        void Uninit();

        ID3D12GraphicsCommandList* GetD3D12CommandList() const;

        void UploadTextureData(IKGFX_TextureResource* pDstTexture, const KTextureCopyRegion& dstRegions, const KGfxSubResourceData& data) const;

        //void UploadSubTextureData(IKGFX_TextureResource* pDstTexture, const KTextureCopyRegion& dstRegions, const KGfxSubResourceData& data) const;

        void UpdateSubResource(IKGFX_Buffer* pGfxBuffer, uint32_t uOffset, uint32_t uSize, const void* pData) const;

        void UpdateAllResource(IKGFX_TextureResource* pGfxTexure, const std::vector<KGfxSubResourceData>& data) const;

        void CopyBuffer(IKGFX_Buffer* pSrcBuffer, IKGFX_Buffer* pDstBuffer) const;

        void CopyBufferSubRegions(IKGFX_Buffer* pSrcBuffer, IKGFX_Buffer* pDstBuffer, uint32_t uCopyRegionCount, const KBufferCopyRegion* pCopyRegions) const;

        void CopyTexture(IKGFX_TextureResource* pSrcTexture, IKGFX_TextureResource* pDstTexture) const;

        void CopyTextureSubRegions(IKGFX_TextureResource* pSrcTexture, IKGFX_TextureResource* pDstTexture, uint32_t uCopyRegionCount, const KTextureCopyRegion* pCopyRegions) const;

        void CopyTextureToBuffer(IKGFX_TextureResource* pSrcTexture, IKGFX_Buffer* pDstBuffer, const KBufferTextureCopy* pBufferTextureCopy, uint32_t NumBufferTextureCopy) const;
        /**
         * 这个barrier是立即执行的，不会走自动处理的流程
         * @param buf
         * @param pPipelineBarrier
         */
        void PipelineBarrier(KGFX_BufferDx12* buf, const KGfxBarrier& pPipelineBarrier) const;
        void PipelineBarrier(IKGFX_TextureResource* tex, const KGfxBarrier& pPipelineBarrier) const;

        void ClearTextureView(IKGFX_TextureView* view, const KClearValue& clearValue, KGFX_ClearResourceViewFlags flags) const;
        void ClearBufferView(IKGFX_BufferView* view, const KClearValue& clearValue, KGFX_ClearResourceViewFlags flags) const;

        void CommitAllTrackerBarrier();

        void ClearAllTrackerBarrier();

        /**
          * 走自动化处理的barrier，外部不可直接调用，由pragram绑定时候自动调用
          * @param buf
          * @param pPipelineBarrier
          */
        void PipelineDelayBarrier(KGFX_BufferDx12* buf, const KGfxBarrier& pPipelineBarrier);
        void PipelineDelayBarrier(IKGFX_TextureResource* tex, const KGfxBarrier& pPipelineBarrier);

        template < bool ImmediaBarrier, typename R, typename F, std::enable_if_t<std::is_invocable_r_v<void, F&, uint32_t, const D3D12_RESOURCE_BARRIER&>, int> = 0>
        void PipelineBarrierImplT(R* tex, const KGfxBarrier& pPipelineBarrier, F&& emit) const;

        void BeginDebugLabel(std::string_view name) const;

        void EndDebugLabel() const;
   
        bool m_bBeginRenderPass = false;
    private:

        /**
       * 当提交layout转换之后并不是立马触发，会先把转换的信息记录到这个管理器上
       * 当然也允许外界立马触发barrier
       */
        KGFX_BarrierTrackerDx12 m_BarrierTracker = {};

        ID3D12GraphicsCommandList* m_pD3d12CommandList = nullptr;
        /**
         * 当前cmd寄宿的提交队列
         */
        KGFX_TransientHeapDX12* m_pTransientHeap = nullptr;

        KGFX_GraphicDeviceDx12* pGraphicDevice = nullptr;
    };

    template <bool ImmediaBarrier, typename R, typename F, std::enable_if_t<std::is_invocable_r_v<void, F&, uint32_t,const D3D12_RESOURCE_BARRIER&>, int>>
    void KGFX_TranslateCommandBufferDX12::PipelineBarrierImplT(R* tex, const KGfxBarrier& pPipelineBarrier, F&& emit) const
    {

        D3D12_RESOURCE_BARRIER barrier{};
        uint32_t subIndex = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
 
        auto& layoutTrackerPtr = [&]() -> auto&
            {
                if constexpr (std::is_same_v<R, KGFX_BufferDx12>)
                {
                    return tex->GetBufferImpl()->GetLayoutTracker();
                }
                else
                {
                    subIndex = GetSubresourceIndex(static_cast<KGfxSubresourceRange>(pPipelineBarrier), tex);
                    return tex->GetLayoutTracker();
                }
            }();


        bool bHasSubRes = (subIndex == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) && !layoutTrackerPtr.AllSubResHasSameLayout();

        auto srcState = (pPipelineBarrier.eSRCAccess == KGfxAccess::Unknown)
            ? layoutTrackerPtr.GetSubresourceState(subIndex)
            : GetResourceState(pPipelineBarrier.eSRCAccess);

        auto dstState = GetResourceState(pPipelineBarrier.eDSTAccess);

        if (srcState == dstState && srcState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        {
            if (!layoutTrackerPtr.m_bUAVOverLap && !m_bBeginRenderPass)
            {
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                barrier.UAV.pResource = reinterpret_cast<ID3D12Resource*>(tex->GetNativeResourceHandle());
                std::invoke(std::forward<F>(emit), 1u, barrier);
            }
        }
        else
        {
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.StateBefore = srcState;
            barrier.Transition.StateAfter = dstState;
            barrier.Transition.pResource = reinterpret_cast<ID3D12Resource*>(tex->GetNativeResourceHandle());

            if (bHasSubRes)
            {
                const auto& subResMap = layoutTrackerPtr.GetSubResStateMap();
                for (auto& subRes : subResMap)
                {
                    barrier.Transition.StateBefore = subRes.second;
                    barrier.Transition.Subresource = subRes.first;
                    if (barrier.Transition.StateBefore != barrier.Transition.StateAfter)
                        std::invoke((emit), 1u, barrier);
                }
            }
            else if (barrier.Transition.StateBefore != barrier.Transition.StateAfter)
            {
                barrier.Transition.Subresource = subIndex;
                std::invoke(std::forward<F>(emit), 1u, barrier);
            }
        }

        if constexpr (ImmediaBarrier)
        {
            layoutTrackerPtr.SetSubresourceState(dstState, subIndex);
        }
    }

    /**
     * 将来或许需要把CS单独到一个queue上，但是实现的细节有待商榷
     */
    class KGFX_ComputeCommandBufferDX12
    {
        friend class KGFX_CommandBufferDX12Impl;
    public:
        KGFX_ComputeCommandBufferDX12() = default;
        ~KGFX_ComputeCommandBufferDX12();
        KGFX_ComputeCommandBufferDX12(const KGFX_ComputeCommandBufferDX12&) = delete;
        KGFX_ComputeCommandBufferDX12& operator=(const KGFX_ComputeCommandBufferDX12&) = delete;
        KGFX_ComputeCommandBufferDX12(KGFX_ComputeCommandBufferDX12&&) = delete;
        KGFX_ComputeCommandBufferDX12& operator=(KGFX_ComputeCommandBufferDX12&&) = delete;

        void Init(ID3D12GraphicsCommandList* d3dCommandList, KGFX_TransientHeapDX12* transientHeap);

        void ReInit(ID3D12GraphicsCommandList* d3dCommandList, KGFX_TransientHeapDX12* transientHeap);

        void Uninit();

        void Dispatch(int nGroupCountX, int nGroupCountY, int nGroupCountZ) const;

        inline void DispatchIndirect(IKGFX_Buffer* pIndirectBuffer, int nOffset) const;

        void BindRootSignature(ID3D12RootSignature* pSignature) const;

        void SetPipelineState(ID3D12PipelineState* pso) const;

        inline ID3D12GraphicsCommandList* GetD3D12CommandList() const;

        void SetDescriptorHeaps() const;
    private:

        ID3D12GraphicsCommandList* m_pD3d12CommandList = nullptr;
        /**
         * 当前cmd寄宿的提交队列
         */
        KGFX_TransientHeapDX12* m_pTransientHeap = nullptr;

        KGFX_GraphicDeviceDx12* pGraphicDevice = nullptr;
    };

    /**
     * 如果要分开CS和GS两个queue的话
     */
    class KGFX_GraphicsCommandBufferDX12
    {
        friend class KGFX_CommandBufferDX12Impl;
    public:

        KGFX_GraphicsCommandBufferDX12() = default;
        ~KGFX_GraphicsCommandBufferDX12();
        KGFX_GraphicsCommandBufferDX12(const KGFX_GraphicsCommandBufferDX12&) = delete;
        KGFX_GraphicsCommandBufferDX12& operator=(const KGFX_GraphicsCommandBufferDX12&) = delete;
        KGFX_GraphicsCommandBufferDX12(KGFX_GraphicsCommandBufferDX12&&) = delete;
        KGFX_GraphicsCommandBufferDX12& operator=(KGFX_GraphicsCommandBufferDX12&&) = delete;

        void Init(ID3D12GraphicsCommandList* d3dCommandList, KGFX_TransientHeapDX12* transientHeap);

        void ReInit(ID3D12GraphicsCommandList* d3dCommandList, KGFX_TransientHeapDX12* transientHeap);

        void Uninit();

        ID3D12GraphicsCommandList* GetD3D12CommandList() const;

        ID3D12GraphicsCommandList1* GetD3D12CommandList1() const;

        ID3D12GraphicsCommandList4* GetD3D12CommandList4() const;

        ID3D12GraphicsCommandList6* GetD3D12CommandList6() const;

        ID3D12GraphicsCommandList9* GetD3D12CommandList9() const;

        void SetScissorRects(const std::vector<D3D12_RECT>& scissorRects) const;

        void SetViewPorts(const std::vector<D3D12_VIEWPORT>& scissorRects) const;

        void BindVertexBuffers(int nFirstBinding, int nBindingCount, const D3D12_VERTEX_BUFFER_VIEW* vertexBufViews) const;

        void BindIndexBuffer(const D3D12_INDEX_BUFFER_VIEW* pIndexBufferView) const;

        void DrawInstanced(int nVertexCount, int nFirstVertex, int nInstanceCount, int nFirstInstance) const;

        void DrawIndirect(int maxDrawCount, ID3D12Resource* argBuf, uint32_t argBufOffset) const;

        void DrawIndexedIndirect(int maxDrawCount, ID3D12Resource* argBuf, uint32_t argBufOffset) const;

        void DrawIndexedInstanced(int nIndexCount, int nInstanceCount, int nIndexOffset, int nVertexOffset, int nInstanceOffset) const;

        void SetPipelineState(ID3D12PipelineState* pso) const;

        void SetDescriptorHeaps() const;

        void SetStencilRef(uint32_t stencilRef) const;

        void SetDipthBias(float DepthBias, float DepthBiasClamp, float SlopeScaledDepthBias) const;
    private:

        /**
         * ID3D12GraphicsCommandList*: 父cmdlist
         * ID3D12GraphicsCommandList1*: 用于支持采样点的自定义
         * ID3D12GraphicsCommandList4*: 用于支持光追
         * ID3D12GraphicsCommandList6*: 用于支持meshShader
         */
        using D3D12CommandListTuple = std::tuple<ID3D12GraphicsCommandList*, ID3D12GraphicsCommandList1*, ID3D12GraphicsCommandList4*, ID3D12GraphicsCommandList6*, ID3D12GraphicsCommandList9*>;


        D3D12CommandListTuple m_D3d12CommandLists = {};

        /**
         * 当前cmd寄宿的提交队列
         */
        KGFX_TransientHeapDX12* m_pTransientHeap = nullptr;

        KGFX_GraphicDeviceDx12* pGraphicDevice = nullptr;
    };





    class KGFX_CommandBufferDX12Impl final : public IKGFX_RenderContext
    {
        friend class KGFX_TransientHeapDX12;
        friend class KGFX_CommandQueueDX12Impl;
    public:
        KGFX_CommandBufferDX12Impl() = default;
        ~KGFX_CommandBufferDX12Impl() override;

        KGFX_CommandBufferDX12Impl(const KGFX_CommandBufferDX12Impl&) = delete;
        KGFX_CommandBufferDX12Impl& operator=(KGFX_CommandBufferDX12Impl&) = delete;
        KGFX_CommandBufferDX12Impl(const KGFX_CommandBufferDX12Impl&&) = delete;
        KGFX_CommandBufferDX12Impl& operator=(KGFX_CommandBufferDX12Impl&&) = delete;

        void Init();

        void ReInit(ID3D12GraphicsCommandList* d3dCommandList, KGFX_TransientHeapDX12* transientHeap);

        virtual bool IsValid() const override;

        bool CreateTransientHeap();

        BOOL BeginCommandBuffer() override;
        void SubmitCommandBuffer(BOOL bWait = FALSE, void* pGpuCompletedSignal = nullptr) override;
        void* GetCommandBufferNativeHandle() const override;

        void FlushResourceBarriers();

        void ClearResourceBarriers();

        /**
         * Copy resources.
         */
        void CmdCopyBuffer(IKGFX_Buffer* pSrcBuffer, IKGFX_Buffer* pDstBuffer) override;
        void CmdCopyBufferSubRegions(IKGFX_Buffer* pSrcBuffer, IKGFX_Buffer* pDstBuffer, uint32_t uCopyRegionCount, const KBufferCopyRegion* pCopyRegions) override;
        void CmdCopyTexture(IKGFX_TextureResource* pSrcTexture, IKGFX_TextureResource* pDstTexture) override;
        void CmdCopyTextureSubRegions(IKGFX_TextureResource* pSrcTexture, IKGFX_TextureResource* pDstTexture, uint32_t uCopyRegionCount, const KTextureCopyRegion* pCopyRegions) override;

        // 拷贝纹理到缓冲区
        // pBufferTextureCopy和NumBufferTextureCopy为nullptr和0时，表示拷贝整个纹理到缓冲区
        void CmdCopyTextureToBuffer(IKGFX_TextureResource* pSrcTexture, IKGFX_Buffer* pDstBuffer, const KBufferTextureCopy* pBufferTextureCopy, uint32_t NumBufferTextureCopy) override;

        // 拷贝缓冲区到纹理
        // pBufferTextureCopy和NumBufferTextureCopy为nullptr和0时，表示拷贝整个缓冲区到纹理
        void CmdCopyBufferToTexture(IKGFX_Buffer* pSrcBuffer, IKGFX_TextureResource* pDstTexture, const KBufferTextureCopy* pBufferTextureCopy, uint32_t NumBufferTextureCopy) override;

        void CmdUploadTextureData(IKGFX_TextureResource* pDstTexture, const KTextureCopyRegion& dstRegions, const KGfxSubResourceData& data) const;

        void CmdBindVertexBuffers(int nFirstBinding, int nBindingCount, IKGFX_Buffer* apBuffer[], int anOffsets[], uint32_t* stride = nullptr) override;

        void CmdBindIndexBuffer(IKGFX_Buffer* pBuffer, int nOffset, enumIndexType indexType) override;

        void CmdDraw(int nVertexCount, int nFirstVertex, bool bPoint = false) override;

        void CmdDrawInstanced(int nVertexCount, int nFirstVertex, int nInstanceCount, int nFirstInstance) override;

        void CmdDrawIndexed(int nIndexCount, int nInstanceCount, int nFirstIndex, int nVertexOffset, int nFirstInstance) override;

        void CmdDrawIndexedInstanced(int nIndexCount, int nFirstIndex, int nInstanceCount, int nFirstVertex, int nFirstInstance) override;

        void CmdDrawIndexedIndirect(IKGFX_Buffer* pInderiectCommandBuffer, int nOffset, int nDrawCount, int nStride, bool bRecordDrawCall = true) override;

        void CmdDrawIndirect(IKGFX_Buffer* pInderiectCommandBuffer, int nOffset, int nDrawCount, int nStride) override;

        void CmdSetLineWidth(float linewidth) override;

        void CmdBindPipeline(enumPipelineBindPoint eBindPoint, const KGFX_PipelineDX12* pPipeline);

        void CmdSetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY PrimitiveState);

        void CmdSetGraphicsRootCbuf(uint32_t RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation);

        void CmdSetGraphicsDescriptorTable(uint32_t RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor);

        void CmdSetGraphicsRootSignature(ID3D12RootSignature* pRootSignature);

        void CmdSetComputeRootSignature(ID3D12RootSignature* pRootSignature);

        void CmdSetComputeRootCbuf(uint32_t RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation);

        void CmdSetComputeDescriptorTable(uint32_t RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor);

        void CmdClearTextureView(IKGFX_TextureView* view, KClearValue clearValue, KGFX_ClearResourceViewFlags flags) override;
        void CmdClearBufferView(IKGFX_BufferView* view, KClearValue clearValue, KGFX_ClearResourceViewFlags flags) override;

        BOOL CmdDispatch(int nGroupCountX, int nGroupCountY, int nGroupCountZ) override;

        BOOL CmdDispatchIndirect(IKGFX_Buffer* pIndirectBuffer, int nOffset) override;

        void Reset();

        void CmdClearAttachment(const KClearAttchment* pAttachment, int nCount, const NSKMath::KVectorInt2& v2Offset, const NSKMath::KVectorUint2& v2Size, uint32_t uBaseArrayLayer, uint32_t uLayerCount) override;
        void CmdSetDepthBias(float fConstant, float fClamp, float fSlop, bool bAutoReverse) override;
        void CmdSetScissor(int nX, int nY, int nWidth, int nHeight) override;
        void CmdSetScissor(const IKGFX_RenderFrameBuffer* piRenderFrameBuffer) override;
        void CmdSetViewport(const ViewportDescription& Viewport) override;
        void CmdSetViewport(const IKGFX_RenderFrameBuffer* piRenderFrameBuffer) override;

        void CmdUpdateSubResource(IKGFX_Buffer* pGfxBuffer, uint32_t uOffset, uint32_t uSize, const void* pData, uint32_t option) override;
        void CmdUpdateAllResource(IKGFX_TextureResource* pGfxTexure, std::vector<gfx::KGfxSubResourceData>& data) override;
        void CmdUpdateSubResource(IKGFX_TextureResource* pGfxTexure, uint32_t uDstMipLevel, uint32_t uDstArraySlice, const KGfxCopyRegion* pDstRegion, const void* pSrcData, uint32_t uSrcRowPitch, uint32_t SrcDepthPitch) override;

        void BeginDebugLabel(const char* strDebugLabel) override;
        void EndDebugLabel() override;
        void BeginOptickProfile() override;
        void EndOptickProfile() override;

        void CmdBeginUAVOverlap(IKGFX_Resource* const* ppResourceUAV, uint32_t count) override;

        void CmdEndUAVOverlap(IKGFX_Resource* const* ppResourceUAV, uint32_t count) override;

        BOOL CmdInsertSignalFence(KSignalFence* pSignalFence) override;

        /**
         * 这个barrier是立即执行的，不会走自动处理的流程
         */
        BOOL Transition(const KGfxBarrier& sBarrierInfo) override;
        BOOL Transition(const std::initializer_list<KGfxBarrier>& lsBarrierInfosArray) override;
        BOOL Transition(const KGfxBarrier* pBarrierInfos, uint32_t uBarrierCount) override;

        void CmdClose() override;

        bool GetIsClose() const;

        void BeginRenderPass();

        void EndRenderPass();

        KGFX_TransientHeapDX12* GetUsedTransientHeap() const;

        void CmdSetDescriptorHeaps();

        /**
          * 走自动化处理的barrier，外部不可直接调用，由pragram绑定时候自动调用
          * @param buf
          * @param pPipelineBarrier
          */
        void PipelineBarrier(IKGFX_Buffer* buf, const KGfxBarrier& pPipelineBarrier);
        void PipelineBarrier(IKGFX_TextureResource* tex, const KGfxBarrier& pPipelineBarrier);

        ID3D12GraphicsCommandList* GetD3D12CommandList() const;

        //for ray tracing
        ID3D12GraphicsCommandList4* GetD3D12CommandList4()const;

        std::vector<ID3D12CommandList*> GetSubmitCmdLists() const;


        void SetCurrentFrameBuffer(gfx::IKGFX_RenderFrameBuffer* pRenderFrameBuffer);
        gfx::IKGFX_RenderFrameBuffer* GetCurrentFrameBuffer() const;

    protected:
        gfx::IKGFX_RenderFrameBuffer* m_pCurrentFrameBuffer = nullptr;

    public:
        void InvalidateDescriptorHeapBinding();


    private:
        /// <summary>
        ///  用于Graphics queue的cmdlist，由于Graphics queue同时具有compute graphics translate 功能，所以我们需要所有三个encoder都在这里
        /// </summary>
        KGFX_GraphicsCommandBufferDX12 m_GraphicsCommandEncoder = {};
        KGFX_ComputeCommandBufferDX12 m_ComputeCommandEncoder = {};
        KGFX_TranslateCommandBufferDX12 m_TranslateCommandEncoder = {};
        std::array<D3D12_GPU_VIRTUAL_ADDRESS, 12> m_vecGraphicsRootCBV = {};
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 24> m_vecGraphicsDescriptorTable = {};

        std::array<D3D12_GPU_VIRTUAL_ADDRESS, 12> m_vecComputeRootCBV = {};
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 24> m_vecComputeDescriptorTable = {};

        bool m_DescriptorHeapsBound = false;
        bool m_CmdBufClosed = false;

        ID3D12GraphicsCommandList* m_d3dCommandList = nullptr;

        D3D12_PRIMITIVE_TOPOLOGY m_PrimitiveState = {};
        ID3D12RootSignature* m_pGraphicsRootSignature = nullptr;
        ID3D12RootSignature* m_pComputeRootSignature = nullptr;
        //KGFX_TransientHeapDX12* m_pTransientHeap = nullptr;

        /**
         * 如果两个绑定对应一个PSO，就不重复绑定了
         */
        ID3D12PipelineState* m_CurrerntPso = nullptr;
        const KGFX_PipelineDX12* m_DX12Pso = nullptr;
        std::vector<KGFX_TransientHeapDX12*> m_VecTransientHeap = {};

        uint32_t m_uCurrentTransHeapID = -1;
        KGFX_GraphicDeviceDx12* pGraphicDevice = nullptr;
        Optick::GPUContext m_prevContext = {};
    };


}
