#pragma once
#include "VulkanWrapper.h"
#include "Graphics/Texture.h"
#include "VulkanHelper.hpp"
#include "VulkanResourceTracker.h"
#include <string>
namespace RHI
{
	class VulkanSampler;
	class VulkanTexture;
	class VulkanResourceTracker;
	class VulkanTextureView :public TextureView
	{
	public:
		VulkanTextureView(const TextureViewCreation& ci, std::shared_ptr<Texture> parentTexture);
		~VulkanTextureView();
		static auto create(const TextureViewCreation& ci, std::shared_ptr<Texture> parentTexture)->std::shared_ptr<VulkanTextureView>;
	public:
		// from TextureView 
		auto get_parent_texture() -> std::shared_ptr<Texture> override;

		auto get_native_handle() -> void* override;

		auto get_name() -> const char* override;

		auto get_view_dim() -> AshResourceViewDimension override;
		auto get_subresource_range() -> const AshSubresourceRange & override;
		auto get_view_format() -> AshFormat override;
		auto get_view_type() -> AshResourceViewType override;
	public:
		/*interfaces for vulkan*/
		inline auto get_vk_image_view() -> VkImageView
		{
			return vkImageView;
		}

		inline auto get_vk_image_view_dim() -> VkImageViewType
		{
			return ash_image_view_dim_to_vk(m_sInfo.view_dim);;
		}

		inline auto get_vk_image_view_format() -> VkFormat
		{
			return get_vk_texture_format_info(m_sInfo.format).vkFormat;
		}
	private:
		VkImageView vkImageView = VK_NULL_HANDLE;
		std::weak_ptr<Texture> parentTexture;
		TextureViewCreation m_sInfo{};
		std::string m_nameStorage;
	};
	class VulkanTexture : public Texture
	{

	public:
		VulkanTexture() = default;
		VulkanTexture(const TextureCreation& ci);
		~VulkanTexture();
		static auto create(const TextureCreation& ci) -> std::shared_ptr<VulkanTexture>;
		auto init() -> void;
	public:
		// from texture
		virtual auto get_desciption()const -> const TextureCreation& override;

		auto get_native_handle() -> void* override;

		auto get_alias_texture() -> std::shared_ptr<Texture> override;

		auto get_default_rtv() -> std::shared_ptr<TextureView> override;

		auto get_default_srv() -> std::shared_ptr<TextureView> override;

		auto get_default_uav() -> std::shared_ptr<TextureView> override;

		auto is_cube_map() -> bool override;

		auto is_sparse() -> bool override;

		auto get_format() -> AshFormat override;

		auto is_render_target() -> bool override;

		auto get_mip_maps_count() -> uint8_t override;

		auto get_layer_count() -> uint16_t override;

		auto get_depth() -> uint16_t override;

		auto get_width() -> uint16_t override;

		auto get_height() -> uint16_t override;

		auto get_name() -> const char* override;	

		auto get_type() -> AshImageType override;

		auto get_resource_state() -> AshResourceState override;

		auto set_resource_state(AshResourceState state) -> void override;
	public:
		inline auto get_vk_image() const -> VkImage
		{
			return vkImage;
		}
		inline auto get_vk_format() const -> const VkFormat&
		{
			return get_vk_texture_format_info(m_sCreation.format).vkFormat;
		}

		inline auto get_vk_image_type() const -> const VkImageType&
		{
			return ash_image_type_to_vk(m_sCreation.type);
		}

		inline auto get_vma_allocation() const
		{
			return vmaAllocation;
		}
		inline auto get_vk_alias_texture() -> std::shared_ptr<VulkanTexture>
		{
			return aliasTexture;
		}

		inline auto is_swapchain_image() const -> bool
		{
			return swapchain_texture;
		}

		inline auto get_vk_aspect_flags() const -> VkImageAspectFlags
		{
			return m_uAspectFlags;
		}
		auto resolve_subresource_range(const AshSubresourceRange& range) const -> AshSubresourceRange;
        auto get_resource_tracker() -> VulkanResourceTracker&;

	private:

		AshResourceState								state						= AshResourceState::Unknown;
		bool										sparse							= false;
		bool										cube							= false;
		bool										swapchain_texture				= false;
		VkImage										vkImage							= VK_NULL_HANDLE;
		VmaAllocation								vmaAllocation					= VK_NULL_HANDLE;
		std::shared_ptr<VulkanTextureView>			defaultSRV = nullptr;
		std::shared_ptr<VulkanTextureView>			defaultRTV = nullptr;
		std::shared_ptr<VulkanTextureView>			defaultUAV = nullptr;
		std::shared_ptr<VulkanTexture>				aliasTexture					= nullptr;
		VkImageAspectFlags      m_uAspectFlags = 0;
		TextureCreation m_sCreation{};
		std::string m_nameStorage;
		VulkanResourceTracker m_ResourceLayoutTracker = { AshResourceState::Unknown };

		friend class VulkanSwapchain;

		

		

};
}
