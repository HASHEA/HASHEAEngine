#pragma once
#ifdef _WIN32
#include "KGFX_DescriptorDX12.h"
#include "../IGFX_Private.h"

namespace gfx
{
    class KGFX_ProgramBinderDx12;

    enum class CopyDescriptorType
    {
        SRV,
        UAV,
        SAMPLER
    };

    /**
    * 处理DX12的descriptorSet的缓存
    * 1.如果开启CPU缓存，那么就会从device上分配一个连续的CPU可见的descriptorHeap
    * 并将离散的数据拷贝到这个连续的descriptorHeap上
    * 2.在绘制发生之后，会将这个descriptorHeap的内容拷贝到GPU可见的descriptorHeap上
    *
    */
    class KGFX_ProgramDescriptorCacheDX12 :public KGfxRef
    {
    public:
        KGFX_ProgramDescriptorCacheDX12() = default;

        ~KGFX_ProgramDescriptorCacheDX12()override;
        KGFX_ProgramDescriptorCacheDX12(const KGFX_ProgramDescriptorCacheDX12&) = delete;
        KGFX_ProgramDescriptorCacheDX12& operator=(const KGFX_ProgramDescriptorCacheDX12&) = delete;
        KGFX_ProgramDescriptorCacheDX12(const KGFX_ProgramDescriptorCacheDX12&&) = delete;
        KGFX_ProgramDescriptorCacheDX12& operator=(const KGFX_ProgramDescriptorCacheDX12&&) = delete;

        void Init(KGFX_ProgramBinderDx12* shaderProgram, int srvAnduavCount, int samplerCount);

        void Reset();

        /**
         * 外部绑定结束之后设置需要提交的descriptorSet
         * @param srvSubHeap
         * @param samSubHeap
         */
        void PrepareGPUDescriptorBind(DescriptorHeapReference srvSubHeap, DescriptorHeapReference samSubHeap);

        void EndCPUDescriptorBind(bool bCPUNeed, bool bGPUNeed, bool& bSamplerPrepared);

        void ProcessGPUDescriptorBind(bool& bSamplerPrepared);

        D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorTable(ShaderStageType stage, CopyDescriptorType type) const;

        int AddRef() override;

        int GetRef() override;

        int Release() override;

    private:
        void DoGPUDescriptorCopy(CopyDescriptorType type) const;

        void DoCPUDescriptorCopy(CopyDescriptorType copyTypeonlyOneFrame) const;

        struct DescriptorSet
        {
            DescriptorTable resourceTable;
            DescriptorTable samplerTable;

            void FreeIfSupported()
            {
                resourceTable.FreeIfSupported();
                samplerTable.FreeIfSupported();
            }
        };

        KGFX_ProgramBinderDx12* m_pProgramBinder = nullptr;

        /**
         * 这个记录的是commandBuf上持有的GPU可见的descriptorHeap的地址
         * 每次绘制开始都要重新分配一个，每次更新贴图等数据的时候就需要新更新一个，一直持有最新的
         */
        DescriptorSet m_CurrentGPUDescriptorSetRef = {};

        /**
         * 这个是记录的device上持有的CPU可见的连续的descriptorHeap的地址
         * 其中资源的排序如下：
         * 与反射时创建root sign时候的顺序是一致的
         *|      SRV               |  UAV       |   Sampler |
         *|     VS  |   FS  | ...  |
         *|  O,1,2..|
         */
        DescriptorSet m_CPUDescriptorSetCache = {};

    };
};

#endif
