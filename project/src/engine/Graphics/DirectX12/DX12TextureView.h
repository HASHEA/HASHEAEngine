#pragma once
#include "DX12Wrapper.h"
#include "DX12Helper.hpp"
#include "DX12Texture.h"
#include "Graphics/Texture.h"
#include <memory>

namespace RHI
{
	class DX12Texture;
	class DX12DescriptorHeapManager;

	class DX12TextureView : public TextureView
	{
	public:
		DX12TextureView() = default;
		~DX12TextureView();

		bool init(const TextureViewCreation& ci, std::shared_ptr<DX12Texture> parentTexture, ID3D12Device* device, DX12DescriptorHeapManager* heapMgr);
		void shutdown();

		const DX12DescriptorHandle& get_descriptor_handle() const { return m_descriptorHandle; }

	public:
		auto get_native_handle() -> void* override { return nullptr; }
		auto get_name() -> const char* override { return "DX12TextureView"; }
		auto get_parent_texture() -> std::shared_ptr<Texture> override
		{
			return std::static_pointer_cast<Texture>(m_parentTexture.lock());
		}
		auto get_view_dim() -> AshResourceViewDimension override { return m_viewDesc.view_dim; }
		auto get_view_format() -> AshFormat override { return m_viewDesc.format; }
		auto get_subresource_range() -> const AshSubresourceRange& override { return m_viewDesc.sub_resource; }
		auto get_view_type() -> AshResourceViewType override { return m_viewDesc.view_type; }

	private:
		std::weak_ptr<DX12Texture> m_parentTexture;
		DX12DescriptorHandle m_descriptorHandle;
		TextureViewCreation m_viewDesc;
		DX12DescriptorHeapManager* m_heapMgr = nullptr;
	};
}
