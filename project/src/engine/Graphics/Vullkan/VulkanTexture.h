#pragma once
#include "VulkanWrapper.h"
#include "Graphics/Texture.h"
namespace RHI
{
	class VulkanSampler;
	class VulkanTexture;
	class VulkanTextureView :public TextureView
	{
	public:
		VulkanTextureView(const TextureViewCreation& ci, std::shared_ptr<Texture> parentTexture);
		~VulkanTextureView();
	public:
		// from TextureView 
		auto get_parent_texture() -> std::shared_ptr<Texture> override;

		auto get_native_view_handle() -> void* override;

		auto get_view_type() -> AshImageViewType override;

		auto get_view_format() -> AshFormat override;
	public:
		/*interfaces for vulkan*/
		inline auto get_vk_image_view() -> VkImageView
		{
			return vkImageView;
		}

		inline auto get_vk_image_view_type() -> VkImageViewType
		{
			return ash_image_view_type_to_vk(viewType);;
		}

		inline auto get_vk_image_view_format() -> VkFormat
		{
			return ash_format_to_vk(viewFormat);
		}
	private:
		VkImageView vkImageView = VK_NULL_HANDLE;
		std::weak_ptr<Texture> parentTexture;
		AshImageViewType viewType{};
		AshFormat viewFormat{};
		

	};
	class VulkanTexture : public Texture
	{
	public:
		VulkanTexture(const TextureCreation& ci);
		~VulkanTexture();
		static auto create(const TextureCreation& ci) -> std::shared_ptr<VulkanTexture>;
		auto init() -> void;
	public:
		// from texture
		auto get_desciption(TextureDescription& desc) -> void override;

		auto get_native_texture_handle() -> void* override;

		auto get_alias_texture() -> std::shared_ptr<Texture> override;

		auto get_default_render_target_view() -> std::shared_ptr<TextureView> override;

		auto get_default_shader_resource_view() -> std::shared_ptr<TextureView> override;

		auto get_default_unordered_access_view() -> std::shared_ptr<TextureView> override;

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

	public:
		/*interfaces for vulkan*/
		inline auto get_default_vk_image_view() -> std::shared_ptr<VulkanTextureView>
		{
			return defaultVulkanTextureView;
		}
		inline auto get_vk_image() -> VkImage
		{
			return vkImage;
		}
		inline auto get_vk_format() -> const VkFormat&
		{
			return ash_format_to_vk(format);
		}

		inline auto get_vk_image_type() -> const VkImageType&
		{
			return ash_image_type_to_vk(type);
		}

		inline auto get_vma_allocation()
		{
			return vmaAllocation;
		}
		inline auto get_vk_alias_texture() -> std::shared_ptr<VulkanTexture>
		{
			return aliasTexture;
		}
	private:
		const char*									name							= nullptr;
		uint16_t									width							= 1;
		uint16_t									height							= 1;
		uint16_t									depth							= 1;
		uint16_t									layerCount						= 0;
		uint8_t										mipmaps							= 1;
		uint8_t										render_target					= 0;
		uint8_t										compute_access					= 0;
		AshFormat									format							= ASH_FORMAT_UNDEFINED;
		AshImageType								type							= AshImageType::Ash_Texture2D;
		bool										sparse							= false;
		bool										cube							= false;
		VkImage										vkImage							= VK_NULL_HANDLE;
		VmaAllocation								vmaAllocation					= VK_NULL_HANDLE;
		std::shared_ptr<VulkanTextureView>			defaultVulkanTextureView		= nullptr;
		std::shared_ptr<VulkanTexture>				aliasTexture					= nullptr;
	
	};
}