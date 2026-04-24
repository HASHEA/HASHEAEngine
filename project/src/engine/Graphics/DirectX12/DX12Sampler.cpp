#include "DX12Sampler.h"
#include "DX12DescriptorHeap.h"

namespace RHI
{
	DX12Sampler::~DX12Sampler()
	{
		shutdown();
	}

	bool DX12Sampler::init(const SamplerCreation& ci, ID3D12Device* device, DX12DescriptorHeapManager* heapMgr)
	{
		m_name_storage = ci.name ? ci.name : "";
		m_creation = ci;
		m_creation.name = m_name_storage.empty() ? nullptr : m_name_storage.c_str();
		m_heapMgr = heapMgr;

		m_descriptorHandle = heapMgr->cpuSampler.allocate();

		D3D12_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = ash_to_d3d12_filter(ci.minFilter, ci.magFilter, ci.mipFilter,
		                                          ci.enable_anisotropy, ci.reductionMode, ci.enable_compare);
		samplerDesc.AddressU = ash_to_d3d12_address_mode(ci.address_mode_u);
		samplerDesc.AddressV = ash_to_d3d12_address_mode(ci.address_mode_v);
		samplerDesc.AddressW = ash_to_d3d12_address_mode(ci.address_mode_w);
		samplerDesc.MipLODBias = ci.mip_lod_bias;
		samplerDesc.MaxAnisotropy = static_cast<UINT>(ci.max_anisotropy);
		samplerDesc.ComparisonFunc = ci.enable_compare ? ash_to_d3d12_comparison(ci.compare_op) : D3D12_COMPARISON_FUNC_NEVER;
		ash_border_color_to_float4(ci.border_color, samplerDesc.BorderColor);
		samplerDesc.MinLOD = ci.min_lod;
		samplerDesc.MaxLOD = ci.max_lod;

		device->CreateSampler(&samplerDesc, m_descriptorHandle.cpuHandle);
		return true;
	}

	void DX12Sampler::shutdown()
	{
		if (m_heapMgr && m_descriptorHandle.is_valid())
		{
			m_heapMgr->cpuSampler.free(m_descriptorHandle);
			m_descriptorHandle = {};
		}
	}
}
