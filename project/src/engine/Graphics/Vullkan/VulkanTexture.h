#pragma once
#include "VulkanWrapper.h"
#include "Graphics/Texture.h"
namespace RHI
{
	class VulkanSampler;
	class VulkanTextureView :public TextureView
	{
	public:
		VulkanTextureView(TextureViewCreation& ci);
		~VulkanTextureView();
	public:
		inline auto get_vk_image_view() -> VkImageView
		{
			return vkImageView;
		}
	private:
		VkImageView vkImageView = VK_NULL_HANDLE;
	};
	class VulkanTexture : public Texture
	{
		VulkanTexture(TextureCreation& ci);
		~VulkanTexture();
	public:
		virtual auto get_desciption(TextureDescription& desc) -> void override;
		virtual auto bind(uint32_t slot) -> void override;
		virtual auto unbind(uint32_t slot) -> void override;
		virtual auto get_sampler() -> Sampler* override;
	public:
		inline auto get_image_view() -> VkImageView
		{
			return vulkanTextureView->get_vk_image_view();
		}
		inline auto get_image() -> VkImage
		{
			return vkImage;
		}
		inline auto get_format() -> const VkFormat&
		{
			return vkFormat;
		}
		inline auto get_vma_allocation()
		{
			return vmaAllocation;
		}
	private:
		VkImage					vkImage					= VK_NULL_HANDLE;
		VulkanTextureView*		vulkanTextureView		= nullptr;
		VkFormat				vkFormat{};
		VmaAllocation			vmaAllocation			= VK_NULL_HANDLE;
		VulkanSampler*			vulkanSampler			= nullptr;
	};
}