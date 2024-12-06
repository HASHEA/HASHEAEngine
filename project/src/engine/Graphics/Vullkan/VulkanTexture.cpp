#include "VulkanHelper.hpp"
#include "VulkanContext.h"
#include "VulkanTexture.h"
namespace RHI
{
	VulkanTexture::VulkanTexture(TextureCreation& ci)
	{
		bool is_cubemap = false;
		uint32_t layerCount = ci.array_layer_count;
		if (ci.type == AshImageType::Ash_TextureCube || ci.type == AshImageType::Ash_Texture_Cube_Array)
		{
			is_cubemap = true;
		}
		const bool is_sparse_texture = (ci.flags & AshTextureFlags::Sparse_mask) == AshTextureFlags::Sparse_mask;
		//// Create the image
		VkImageCreateInfo vkImageCI = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		vkImageCI.format = ash_format_to_vk(ci.format);
		vkImageCI.flags = (is_cubemap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0) | (is_sparse_texture ? (VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT | VK_IMAGE_CREATE_SPARSE_BINDING_BIT) : 0);
		vkImageCI.imageType = ash_image_type_to_vk(ci.type);
		vkImageCI.extent.width = ci.width;
		vkImageCI.extent.height = ci.height;
		vkImageCI.extent.depth = ci.depth;
		vkImageCI.mipLevels = ci.mip_level_count;
		vkImageCI.arrayLayers = ci.array_layer_count;
		vkImageCI.samples = VK_SAMPLE_COUNT_1_BIT;
		vkImageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
		vkImageCI.usage = get_image_usage_vulkan(ci);
		vkImageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		vkImageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VmaAllocationCreateInfo memory_info{};
		memory_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
		HLogInfo("creating texture : {} ...", ci.name);
		if (ci.alias.index == k_invalid_resource)
		{
			if (is_sparse_texture)
			{
				VK_CHECK_RESULT(vkCreateImage(VulkanContext::get_vulkan_device(), &vkImageCI, VulkanContext::get_vulkan_allocation_callbacks(), &vkImage));
			}
			else
			{
				VK_CHECK_RESULT(vmaCreateImage(VulkanContext::get_vma_allocator(), &vkImageCI, &memory_info, &vkImage, &vmaAllocation,nullptr));
#ifdef ASH_DEBUG
				vmaSetAllocationName(VulkanContext::get_vma_allocator(), vmaAllocation, ci.name);
#endif // ASH_DEBUG

			}
		}
		else
		{
			VulkanTexture* aliasTexture = static_cast<VulkanTexture*>(access_texture(ci.alias));
			H_ASSERT(aliasTexture != nullptr);
			H_ASSERT(!is_sparse_texture);
			vmaAllocation = 0;
			VK_CHECK_RESULT(vmaCreateAliasingImage(VulkanContext::get_vma_allocator(), aliasTexture->get_vma_allocation(), &vkImageCI, &vkImage));
			this->aliasTexture = ci.alias;
		}
		VulkanContext::set_resource_name(VK_OBJECT_TYPE_IMAGE, (uint64_t)vkImage, ci.name);
		//create a default view
		
	}

	VulkanTexture::~VulkanTexture()
	{
	}
	auto VulkanTexture::get_desciption(TextureDescription& desc) -> void 
	{
		
	}
	auto VulkanTexture::bind(uint32_t slot) -> void 
	{
	}
	auto VulkanTexture::unbind(uint32_t slot) -> void 
	{
	}
	auto VulkanTexture::get_sampler() -> Sampler* 
	{
		return nullptr;
	}
	VulkanTextureView::VulkanTextureView(TextureViewCreation& ci)
	{

	}
	VulkanTextureView::~VulkanTextureView()
	{

	}
}