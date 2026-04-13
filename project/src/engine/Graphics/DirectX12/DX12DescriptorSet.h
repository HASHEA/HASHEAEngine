#pragma once
#include "DX12Wrapper.h"
#include "DX12Helper.hpp"
#include "Graphics/DescriptorSet.h"

namespace RHI
{
	class DX12DescriptorSet : public DescriptorSet
	{
	public:
		DX12DescriptorSet() = default;
		~DX12DescriptorSet() = default;

		void set_gpu_handle(D3D12_GPU_DESCRIPTOR_HANDLE handle) { m_gpuHandle = handle; }
		D3D12_GPU_DESCRIPTOR_HANDLE get_gpu_handle() const { return m_gpuHandle; }

	private:
		D3D12_GPU_DESCRIPTOR_HANDLE m_gpuHandle = {};
	};
}
