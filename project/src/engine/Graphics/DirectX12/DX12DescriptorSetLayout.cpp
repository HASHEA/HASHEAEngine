#include "DX12DescriptorSetLayout.h"
#include "DX12Context.h"
#include "Base/hlog.h"

namespace RHI
{
	DX12DescriptorSetLayout::~DX12DescriptorSetLayout()
	{
		shutdown();
	}

	bool DX12DescriptorSetLayout::init(ID3D12Device* device, const D3D12_ROOT_SIGNATURE_DESC& desc)
	{
		ComPtr<ID3DBlob> signatureBlob;
		ComPtr<ID3DBlob> errorBlob;

		HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
		                                          &signatureBlob, &errorBlob);
		if (FAILED(hr))
		{
			if (errorBlob)
			{
				HLogError("DX12DescriptorSetLayout: Root signature serialization failed: {}",
				          static_cast<const char*>(errorBlob->GetBufferPointer()));
			}
			return false;
		}

		hr = device->CreateRootSignature(0,
		                                  signatureBlob->GetBufferPointer(),
		                                  signatureBlob->GetBufferSize(),
		                                  IID_PPV_ARGS(&m_rootSignature));
		if (FAILED(hr))
		{
			HLogError("DX12DescriptorSetLayout: Failed to create root signature. HRESULT: 0x{:08X}", (uint32_t)hr);
			return false;
		}

		return true;
	}

	bool DX12DescriptorSetLayout::init_from_serialized(ID3D12Device* device, const void* data, size_t size)
	{
		HRESULT hr = device->CreateRootSignature(0, data, size, IID_PPV_ARGS(&m_rootSignature));
		if (FAILED(hr))
		{
			HLogError("DX12DescriptorSetLayout: Failed to create root signature from serialized data. HRESULT: 0x{:08X}", (uint32_t)hr);
			return false;
		}
		return true;
	}

	void DX12DescriptorSetLayout::shutdown()
	{
		if (!m_rootSignature)
		{
			return;
		}

		ComPtr<ID3D12RootSignature> deferredRootSignature = m_rootSignature;
		m_rootSignature.Reset();

		DX12Context* context = DX12Context::get();
		if (!context)
		{
			deferredRootSignature.Reset();
			return;
		}

		context->get_current_frame_deletion_queue().emplace([deferredRootSignature]() mutable {
			deferredRootSignature.Reset();
		});
	}
}
