#pragma once
#include "DX12Wrapper.h"
#include "Graphics/DescriptorSetLayout.h"
#include <vector>

namespace RHI
{
	class DX12DescriptorSetLayout : public DescriptorSetLayout
	{
	public:
		DX12DescriptorSetLayout() = default;
		~DX12DescriptorSetLayout();

		bool init(ID3D12Device* device, const D3D12_ROOT_SIGNATURE_DESC& desc);
		bool init_from_serialized(ID3D12Device* device, const void* data, size_t size);
		void shutdown();

		ID3D12RootSignature* get_root_signature() const { return m_rootSignature.Get(); }

	public:
		auto get_native_handle() -> void* override { return m_rootSignature.Get(); }
		auto get_name() -> const char* override { return "DX12DescriptorSetLayout"; }

	private:
		ComPtr<ID3D12RootSignature> m_rootSignature;
	};
}
