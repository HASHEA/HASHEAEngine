#pragma once
#include "DX12Wrapper.h"
#include "DX12Helper.hpp"
#include "Graphics/Buffer.h"

struct D3D12MA_Allocation;
namespace D3D12MA { class Allocation; class Allocator; }

namespace RHI
{
	class DX12BufferView;
	class DX12DescriptorHeapManager;

	class DX12Buffer : public Buffer
	{
	public:
		DX12Buffer() = default;
		~DX12Buffer();

		bool init(const BufferCreation& ci, ID3D12Device* device, D3D12MA::Allocator* allocator, DX12DescriptorHeapManager* heapMgr);
		void shutdown();

		ID3D12Resource* get_resource() const { return m_resource.Get(); }
		uint32_t get_stride() const { return m_stride; }
		AshResourceState get_resource_state() const { return m_resourceState; }
		void set_resource_state(AshResourceState state) { m_resourceState = state; }

	public:
		auto get_size() -> uint32_t override { return m_creation.size; }
		auto get_name() -> const char* override { return m_name.c_str(); }
		auto get_global_offset() -> uint32_t override { return 0; }
		auto is_ready() -> bool override { return m_resource != nullptr; }
		auto get_mapped_data() -> uint8_t* override { return m_mappedData; }
		auto is_dynamic() -> bool override { return m_isDynamic; }
		auto get_default_cbv() -> std::shared_ptr<BufferView> override { return m_defaultCBV; }
		auto get_default_srv() -> std::shared_ptr<BufferView> override { return m_defaultSRV; }
		auto get_default_uav() -> std::shared_ptr<BufferView> override { return m_defaultUAV; }
		auto update(uint32_t offset, uint32_t size, void* pData) -> bool override;
		auto get_buffer_device_address() -> uint64_t override;
		auto get_buffer_creation_info() const -> const BufferCreation& override { return m_creation; }
		auto get_native_handle() -> void* override { return m_resource.Get(); }

	private:
		void _create_default_views(ID3D12Device* device, DX12DescriptorHeapManager* heapMgr);

	private:
		ComPtr<ID3D12Resource> m_resource;
		D3D12MA::Allocation* m_allocation = nullptr;
		uint8_t* m_mappedData = nullptr;
		BufferCreation m_creation{};
		std::string m_name;
		uint32_t m_stride = 0;
		bool m_isDynamic = false;
		AshResourceState m_resourceState = AshResourceState::Unknown;

		std::shared_ptr<BufferView> m_defaultCBV;
		std::shared_ptr<BufferView> m_defaultSRV;
		std::shared_ptr<BufferView> m_defaultUAV;
	};
}
