#pragma once
#include <atlcomcli.h>
#include "KGFX_RefPtr.h"
#include "DMA_2.0.0/D3D12MemAlloc.h"

namespace gfx
{

    class KGFX_PipelineCacheDX12
    {
    public:
        KGFX_PipelineCacheDX12() = default;
        ~KGFX_PipelineCacheDX12() = default;

        KGFX_PipelineCacheDX12(const KGFX_PipelineCacheDX12& other) = delete;

        bool Init(ID3D12Device* pDevice);

        ID3D12PipelineLibrary* GetPipelineLibrary() const;;

        void WriteCacheFile();

    private:

        void ReadCacheFile();

        size_t m_PipelineCacheSize = 0;
        RefPtr<KGFX_IBlob> m_PipelineCacheBlob = {};
        CComPtr<ID3D12PipelineLibrary1> m_pipelineLibrary = nullptr;
    };
}
