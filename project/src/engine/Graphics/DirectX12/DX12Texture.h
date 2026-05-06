#pragma once
#include "DX12Wrapper.h"
#include "DX12Helper.hpp"
#include "Graphics/Texture.h"

namespace D3D12MA { class Allocation; class Allocator; }

namespace RHI
{
	class DX12TextureView;
	class DX12DescriptorHeapManager;

	class DX12Texture : public Texture
	{
	public:
		DX12Texture() = default;
		~DX12Texture();

		bool init(const TextureCreation& ci, ID3D12Device* device, D3D12MA::Allocator* allocator, DX12DescriptorHeapManager* heapMgr);
		// For swapchain textures
		bool init_from_swapchain(ID3D12Resource* resource, AshFormat format, uint16_t width, uint16_t height, ID3D12Device* device, DX12DescriptorHeapManager* heapMgr, const char* debugName = nullptr);
		void shutdown();

		ID3D12Resource* get_resource() const { return m_resource.Get(); }

	public:
		auto get_desciption() const -> const TextureCreation& override { return m_creation; }
		auto get_alias_texture() -> std::shared_ptr<Texture> override { return m_alias; }
		auto get_default_rtv() -> std::shared_ptr<TextureView> override { return m_defaultRTV; }
		auto get_default_srv() -> std::shared_ptr<TextureView> override { return m_defaultSRV; }
		auto get_default_uav() -> std::shared_ptr<TextureView> override { return m_defaultUAV; }
		auto is_cube_map() -> bool override { return m_creation.type == Ash_TextureCube || m_creation.type == Ash_Texture_Cube_Array; }
		auto is_sparse() -> bool override { return (m_creation.flags & ASH_TEXTURE_CREATE_FLAG_SPARSE) != 0; }
		auto get_format() -> AshFormat override { return m_creation.format; }
		auto is_render_target() -> bool override { return (m_creation.uUsageFlags & ASH_TEXTURE_USAGE_COLOR_ATTACHMENT_BIT) != 0; }
		auto get_mip_maps_count() -> uint8_t override { return m_creation.mip_level_count; }
		auto get_layer_count() -> uint16_t override { return m_creation.array_layer_count; }
		auto get_depth() -> uint16_t override { return m_creation.depth; }
		auto get_width() -> uint16_t override { return m_creation.width; }
		auto get_height() -> uint16_t override { return m_creation.height; }
		auto get_type() -> AshImageType override { return m_creation.type; }
		auto get_resource_state() -> AshResourceState override { return m_resourceState; }
		auto set_resource_state(AshResourceState state) -> void override { m_resourceState = state; }
		auto get_native_handle() -> void* override { return m_resource.Get(); }
		auto get_name() -> const char* override { return m_name.c_str(); }

	private:
		void _create_default_views(ID3D12Device* device, DX12DescriptorHeapManager* heapMgr);

	private:
		ComPtr<ID3D12Resource> m_resource;
		D3D12MA::Allocation* m_allocation = nullptr;
		TextureCreation m_creation{};
		AshResourceState m_resourceState = AshResourceState::Unknown;
		std::string m_name;
		bool m_isSwapchainTexture = false;

		std::shared_ptr<Texture> m_alias;
		std::shared_ptr<TextureView> m_defaultRTV;
		std::shared_ptr<TextureView> m_defaultSRV;
		std::shared_ptr<TextureView> m_defaultUAV;
	};
}
