#pragma once
#include "DX12Wrapper.h"
#include "DX12Helper.hpp"
#include "Graphics/Sampler.h"
#include <string>

namespace RHI
{
	class DX12DescriptorHeapManager;

	class DX12Sampler : public Sampler
	{
	public:
		DX12Sampler() = default;
		~DX12Sampler();

		bool init(const SamplerCreation& ci, ID3D12Device* device, DX12DescriptorHeapManager* heapMgr);
		void shutdown();

		const DX12DescriptorHandle& get_descriptor_handle() const { return m_descriptorHandle; }
		const SamplerCreation& get_creation() const { return m_creation; }

	public:
		auto get_native_handle() -> void* override { return nullptr; }
		auto get_name() -> const char* override { return m_creation.name ? m_creation.name : "DX12Sampler"; }

	private:
		DX12DescriptorHandle m_descriptorHandle;
		SamplerCreation m_creation{};
		DX12DescriptorHeapManager* m_heapMgr = nullptr;
		std::string m_name_storage{};
	};
}
