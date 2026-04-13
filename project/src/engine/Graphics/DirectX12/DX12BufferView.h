#pragma once
#include "DX12Wrapper.h"
#include "DX12Helper.hpp"
#include "DX12Buffer.h"
#include "Graphics/Buffer.h"
#include <memory>

namespace RHI
{
	class DX12Buffer;
	class DX12DescriptorHeapManager;

	class DX12BufferView : public BufferView
	{
	public:
		DX12BufferView() = default;
		~DX12BufferView();

		bool init(const BufferViewCreation& ci, std::shared_ptr<DX12Buffer> parentBuffer, ID3D12Device* device, DX12DescriptorHeapManager* heapMgr);
		void shutdown();

		const DX12DescriptorHandle& get_descriptor_handle() const { return m_descriptorHandle; }

	public:
		auto get_native_handle() -> void* override { return nullptr; }
		auto get_name() -> const char* override { return "DX12BufferView"; }
		auto get_parent_buffer() -> std::shared_ptr<Buffer> override
		{
			return std::static_pointer_cast<Buffer>(m_parentBuffer.lock());
		}
		auto get_view_type() -> AshResourceViewType override { return m_viewDesc.view_type; }
		auto get_view_format() -> AshFormat override { return m_viewDesc.format; }
		auto get_view_desc() -> const BufferViewCreation& override { return m_viewDesc; }

	private:
		std::weak_ptr<DX12Buffer> m_parentBuffer;
		DX12DescriptorHandle m_descriptorHandle;
		BufferViewCreation m_viewDesc;
		DX12DescriptorHeapManager* m_heapMgr = nullptr;
	};
}
