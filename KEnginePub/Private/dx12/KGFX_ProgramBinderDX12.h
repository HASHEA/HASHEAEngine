#pragma once
#include <any>
#include <variant>
#include "KGFX_DescriptorCacheDX12.h"
#include "KGFX_HashFunDX12.h"
#include "Eigen/Eigen"
#include "KGFX_ShaderResourceDx12.h"

namespace gfx
{
    class KGFX_TextureViewDX12;
    class KGFX_CommandBufferDX12Impl;
    class KGFX_BufferViewDX12;
    class KGFX_SamplerDX12;

    class KGFX_ProgramBinderDx12 final : public IKGFX_ProgramBinder
    {
        friend class KGFX_GraphicsProgramDx12;
        friend class KGFX_ComputeProgramDx12;
        friend class KGFX_ProgramDescriptorCacheDX12;

    public:
        KGFX_ProgramBinderDx12();

        ~KGFX_ProgramBinderDx12() override;
        /**
         * DX12 中存在三种类型的绑定资源,root sign, root table, root const
         * root sign只能用于SRV和UAV和CBV的buffer，但是出于槽位的连续性考虑和UAV Buffer格式的兼容性考虑，所以实际root sign只有CBV
         * 所以SRV UAV SAMPLER都是通过root table来绑定的
         * 由于没有做shader的槽位去重，所以VS和FS的root table都是独立的，不能使用VISIALBLE_ALL的一个table
         * @return
         */
        BOOL BuildRootSignature();
        IKGFX_ProgramBinder& BeginBind(const RenderCommonParam* pRenderCommanParam, gfx::IKSharedPreBinder* pShareBinder) override;


        /**
         *先不使用checkcode
         * @return
         */
        void ComputeBindCode();

    private:
        void ProcessSlot();
        BOOL EndBind() override;
        /**
         * 当shader编译结束之后，其需要的资源槽位就已经确定了
         * 此处固定名字与槽位的映射关系
         * 外界可以获取对应存放view的vector的位置，可以直接使用
         */
        void ProcessReflectShaderCursor();

        void PreparePushconstAndSpecialConstBuf();

        void AddBindView(const char* pcszName, IKGFX_BufferView* pBufView);
        void AddBindView(const char* pcszName, IKGFX_TextureView* pTexView);
        void AddBindViewArray(const char* pcszName, uint32_t num, IKGFX_TextureView** pTexViews);
        void AddBindViewArray(const char* pcszName, uint32_t num, IKGFX_BufferView** pBufViews);
        void AddBindGloablCbufData(const char* pcszName, const void* pData, int dataSize = 4) const;

    public:
        BOOL UpdateMtlData() override;

        IKGFX_ProgramBinder& AddBindUAV(const_pool_str pcszName, IKGFX_BufferView* pBufView) override;
        IKGFX_ProgramBinder& AddBindUAV(const_pool_str pcszName, IKGFX_TextureView* pTexView) override;
        IKGFX_ProgramBinder& AddBindUAVArray(const_pool_str pcszName, uint32_t uNum, IKGFX_TextureView** pTexViews) override;
        IKGFX_ProgramBinder& AddBindUAVArray(const_pool_str pcszName, uint32_t uNum, IKGFX_BufferView** pBufViews) override;

        IKGFX_ProgramBinder& AddBindSRV(const_pool_str pcszName, IKGFX_BufferView* pBufView) override;
        IKGFX_ProgramBinder& AddBindSRV(const_pool_str pcszName, IKGFX_TextureView* pTexView) override;
        IKGFX_ProgramBinder& AddBindSRVArray(const_pool_str pcszName, uint32_t uNum, IKGFX_BufferView** pTexViews) override;
        IKGFX_ProgramBinder& AddBindSRVArray(const_pool_str pcszName, uint32_t uNum, IKGFX_TextureView** pTexViews) override;

        IKGFX_ProgramBinder& AddBindCBV(const_pool_str pcszName, IKGFX_BufferView* pBufView) override;
        IKGFX_ProgramBinder& AddBindAccelerationStructure(const_pool_str pcszName, KRayTracingScene* accelerationStructure) override;

        IKGFX_ProgramBinder& SetImmutableConstValueInt(const char* pcszName, int32_t value) override;
        IKGFX_ProgramBinder& SetImmutableConstValueUInt(const char* pcszName, uint32_t value) override;
        IKGFX_ProgramBinder& SetImmutableConstValueFloat(const char* pcszName, float value) override;
        BOOL IsTextureBinded(const char* pName) override;

        int GetSRVStageStartIndex(ShaderStageType eStage) const;

        int GetSRVStageBaseIndex(ShaderStageType eStage) const;

        int GetUAVStageStartIndex(ShaderStageType eStage) const;

        int GetUAVStageBaseIndex(ShaderStageType eStage) const;

        int GetSamplerStageStartIndex(ShaderStageType eStage) const;

        int GetSamplerStageBaseIndex(ShaderStageType eStage) const;

        //IKGFX_ConstBuffer* GetSpecialBufByIndex(int index) const;

        D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorTable(ShaderStageType stage, CopyDescriptorType type) const;


        void ReSet();

        IKGFX_ProgramBinder& AddBindSampler(const char* pcszName, gfx::IKGFX_Sampler* pSampler) override;
        BOOL SetMtlParamValue(const char* szName, void* pData, uint32_t uByteSize) override;
        BOOL IsBinding() override;

        TextureType GetTextureType(const_pool_str szName) override;
    private:
        struct D3D12CPUDescriptorAndHash
        {
            D3D12_CPU_DESCRIPTOR_HANDLE d3d12Handle = {};
            uint64_t hashCode = 0;
            uint64_t hashCode2 = 0;
            bool m_CacheVaild = false;

            void SetDescriptor(SIZE_T o, SIZE_T hashPtr)
            {
                d3d12Handle.ptr = o;
                CacluHash(hashPtr);
            }

            void SetDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE o, SIZE_T hashPtr)
            {
                d3d12Handle = o;
                CacluHash(hashPtr);
            }

            void CacluHash(SIZE_T hashPtr)
            {
                m_CacheVaild = hashCode == d3d12Handle.ptr && hashCode2 == hashPtr;
                hashCode = d3d12Handle.ptr;
                hashCode2 = hashPtr;
                //Fnv1a64Append(hashCode, d3d12Handle.ptr);
            }

            bool IsVaild() const
            {
                return d3d12Handle.ptr != 0;
            }

            bool IsNoChange() const
            {
                return m_CacheVaild;
            }

        };


        bool CalcResourceHash() const;

        constexpr static bool bUseCheckCode = true;

        bool m_bBinding{ false };

        bool m_bPipelineRebuild = true;
        /**
         * 用于收集绑定过来的资源，这个每帧开始要清空一次，保证每帧都是新的资源
         */
        std::vector<D3D12CPUDescriptorAndHash> m_vecBindedResourceDscriptor = {};

        /**
           * 用于收集所有绑定过来的sampler，这个不需要清理，因为sampler是全局的
           */
        //std::vector<KGFX_SamplerDX12*> m_vecBindedSampler = {};
        std::vector<D3D12CPUDescriptorAndHash> m_vecBindedSampler = {};

        /**
         * 用于收集所有绑定过来的cbv
         */
        std::vector<IKGFX_BufferView*> m_vecBindedCBV = {};


        std::vector<gfx::KGfxBarrier> m_vecBarriers = {};

        KGFX_ProgramReflectorDx12* m_pReflector = nullptr;
        IKGFX_ConstBuffer* m_PushConstCbuf = nullptr;
        IKGFX_ConstBuffer* m_SpecialConstCbuf = {};
        bool m_SpecialConstCbufNeedUpdate = false;
        bool m_MtlCbufNeedUpdate = false;

        KGFX_ProgramDescriptorCacheDX12* m_DescriptorTableCache = {};

        KGFX_CommandBufferDX12Impl* m_pRenderContext = nullptr;
       
        bool m_bDescriptorHashVaild = false;
        bool m_bSamplerPrepared = false;
        bool m_bThisFrameFirstRender = false;
        int m_LastUpdateFrameIndex = 0;
    };
}
