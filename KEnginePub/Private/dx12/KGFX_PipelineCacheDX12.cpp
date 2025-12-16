#include "KGFX_PipelineCacheDX12.h"
#include <cassert>
#include "KGFX_DXCComplieDx12.h"
#include "KGFX_ShaderCache.h"
#include "KEnginePub/Private/comm/KGFX_ShaderHelper.h"


namespace gfx
{

    bool KGFX_PipelineCacheDX12::Init(ID3D12Device* pDevice)
    {
        assert(pDevice);
        bool bRet = false;
        CComPtr<ID3D12Device1> device1;
        const void* pLibBolob = nullptr;
        uint32_t blobSize = 0;
        HRESULT hresult = pDevice->QueryInterface(IID_PPV_ARGS(&device1));
        KGLOG_COM_PROCESS_ERROR(hresult);

        ReadCacheFile();
        pLibBolob = m_PipelineCacheBlob ? m_PipelineCacheBlob->GetBufferPointer() : nullptr;
        blobSize = m_PipelineCacheBlob ? static_cast<uint32_t>(m_PipelineCacheBlob->GetBufferSize()) : 0;

        hresult = device1->CreatePipelineLibrary(pLibBolob, blobSize, IID_PPV_ARGS(&m_pipelineLibrary));
        switch (hresult)
        {
        case S_OK:
            break;
        case E_INVALIDARG: // The provided Library is corrupted or unrecognized.
        case D3D12_ERROR_ADAPTER_NOT_FOUND: // The provided Library contains data for different hardware (Don't really need to clear the cache, could have a cache per adapter).
        case D3D12_ERROR_DRIVER_VERSION_MISMATCH: // The provided Library contains data from an old driver or runtime. We need to re-create it.
        {
            auto cachePtr = m_PipelineCacheBlob.Detach();
            SAFE_DELETE(cachePtr);
            hresult = (device1->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&m_pipelineLibrary)));
            KGLOG_COM_PROCESS_ERROR(hresult);
            break;
        }
        default:
            assert(false);
            KGLOG_COM_PROCESS_ERROR(E_FAIL);
        }

        if (m_pipelineLibrary)
        {
            m_pipelineLibrary->SetName(L"DX12PipellineCache");
        }

        bRet = true;
    Exit0:
        return bRet;
    }

    ID3D12PipelineLibrary* KGFX_PipelineCacheDX12::GetPipelineLibrary() const
    {
        return m_pipelineLibrary;
    }

    void KGFX_PipelineCacheDX12::WriteCacheFile()
    {
        HRESULT hresult = E_FAIL;
        bool bRet = false;
        if (m_pipelineLibrary)
        {
            assert(m_pipelineLibrary->GetSerializedSize() <= UINT_MAX);
            auto librarySize = m_pipelineLibrary->GetSerializedSize();
            if (librarySize > 0)
            {
                m_PipelineCacheSize = librarySize;
                ScopedAllocation scopeCache = {};
                scopeCache.Allocate(librarySize);

                hresult = m_pipelineLibrary->Serialize(scopeCache.GetData(), librarySize);
                KGLOG_COM_PROCESS_ERROR(hresult);

                RefPtr <KGFX_IBlob> cacheBlob = {};
                cacheBlob.Attch(RawBlob::MoveCreate(scopeCache));

                bRet = KGFX_GetDXCComplier()->GetCacheManager()->WritePipelineCache(cacheBlob);
                KGLOG_PROCESS_ERROR(bRet);
            }
        }

    Exit0:
        return;
    }

    void KGFX_PipelineCacheDX12::ReadCacheFile()
    {
        KGFX_GetDXCComplier()->GetCacheManager()->ReadPipelineCache(&m_PipelineCacheBlob);
       
        if (m_PipelineCacheBlob)
        {
            m_PipelineCacheSize = m_PipelineCacheBlob.Get()->GetBufferSize();
        }
    }


}
