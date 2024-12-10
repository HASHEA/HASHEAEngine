#include "VulkanHelper.hpp"
#include "VulkanContext.h"
#include "VulkanTexture.h"
#include "VulkanSampler.h"
namespace RHI
{

	VulkanTexture::VulkanTexture(const TextureCreation& ci)
	{
		name = ci.name;
		width = ci.width;
		height = ci.height;
		depth = ci.depth;
		mipmaps = ci.mip_level_count;
		format = ci.format;
		type = ci.type;
		cube = false;
		layerCount = ci.array_layer_count;
		if (ci.type == AshImageType::Ash_TextureCube || ci.type == AshImageType::Ash_Texture_Cube_Array)
		{
			cube = true;
		}
		sparse = (ci.flags & AshTextureFlags::Sparse_mask) == AshTextureFlags::Sparse_mask;
		//// Create the image
		VkImageCreateInfo vkImageCI = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		vkImageCI.format = ash_format_to_vk(ci.format);
		vkImageCI.flags = (cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0) | (sparse ? (VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT | VK_IMAGE_CREATE_SPARSE_BINDING_BIT) : 0);
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
		if (ci.alias == nullptr)
		{
			if (sparse)
			{
				VK_CHECK_RESULT(vkCreateImage(VulkanContext::get_vulkan_device(), &vkImageCI, VulkanContext::get_vulkan_allocation_callbacks(), &vkImage));
			}
			else
			{
				VK_CHECK_RESULT(vmaCreateImage(VulkanContext::get_vma_allocator(), &vkImageCI, &memory_info, &vkImage, &vmaAllocation, nullptr));
#ifdef ASH_DEBUG
				vmaSetAllocationName(VulkanContext::get_vma_allocator(), vmaAllocation, ci.name);
#endif // ASH_DEBUG
			}
		}
		else
		{
			aliasTexture = std::static_pointer_cast<VulkanTexture>(ci.alias);
			H_ASSERT(ci.alias != nullptr);
			H_ASSERT(!sparse);
			vmaAllocation = 0;
			VK_CHECK_RESULT(vmaCreateAliasingImage(VulkanContext::get_vma_allocator(), aliasTexture->get_vma_allocation(), &vkImageCI, &vkImage));
			
		}
		VulkanContext::set_resource_name(VK_OBJECT_TYPE_IMAGE, (uint64_t)vkImage, ci.name);
		
	}

	VulkanTexture::~VulkanTexture()
	{
		if (defaultVulkanTextureView)
		{
			defaultVulkanTextureView.reset();
		}
		if (defaultVulkanSampler)
		{
			defaultVulkanSampler.reset();
		}
		if (vkImage != VK_NULL_HANDLE)
		{
			if (this->aliasTexture == nullptr)
			{
				if (sparse)
				{
					vkDestroyImage(VulkanContext::get_vulkan_device(), vkImage, VulkanContext::get_vulkan_allocation_callbacks());
				}
				else
				{
					vmaDestroyImage(VulkanContext::get_vma_allocator(), vkImage, vmaAllocation);
				}
			}
			else
			{
				//vma allocation = 0 witch mean vma won't free memory for this alias image
				vmaDestroyImage(VulkanContext::get_vma_allocator(), vkImage, vmaAllocation);
			}
		}
		vkImage = VK_NULL_HANDLE;
		aliasTexture.reset();
	}

	auto VulkanTexture::create(const TextureCreation& ci) -> std::shared_ptr<VulkanTexture>
	{
		auto ret = Ash_New_Shared<VulkanTexture>(ci);
		ret->init();
		return ret;
	}

	auto VulkanTexture::init() -> void
	{
		//create a default view
		TextureViewCreation tvc{};
		tvc.sub_resource.mip_level_count = mipmaps;
		tvc.sub_resource.array_layer_count = layerCount;
		tvc.name = name;
		tvc.view_type = ash_image_type_to_image_view_type(type);
		tvc.format = format;
		defaultVulkanTextureView = Ash_New_Shared<VulkanTextureView>(tvc, shared_from_this());
	}

	auto VulkanTexture::get_native_texture_handle() -> void*
	{
		return vkImage;
	}

	auto VulkanTexture::get_alias_texture() -> std::shared_ptr<Texture>
	{
		return aliasTexture;
	}

	auto VulkanTexture::get_default_render_target_view() -> std::shared_ptr<TextureView>
	{
		return defaultVulkanTextureView;
	}

	auto VulkanTexture::get_default_shader_resource_view() -> std::shared_ptr<TextureView>
	{
		return defaultVulkanTextureView;
	}

	auto VulkanTexture::get_default_unordered_access_view() -> std::shared_ptr<TextureView>
	{
		return defaultVulkanTextureView;
	}

	auto VulkanTexture::is_cube_map() -> bool
	{
		return cube;
	}

	auto VulkanTexture::is_sparse() -> bool
	{
		return sparse;
	}

	auto VulkanTexture::get_format() -> AshFormat
	{
		return format;
	}

	auto VulkanTexture::is_render_target() -> bool
	{
		return render_target;
	}

	auto VulkanTexture::get_mip_maps_count() -> uint8_t
	{
		return mipmaps;
	}

	auto VulkanTexture::get_layer_count() -> uint16_t
	{
		return layerCount;
	}

	auto VulkanTexture::get_depth() -> uint16_t
	{
		return depth;
	}

	auto VulkanTexture::get_width() -> uint16_t
	{
		return width;
	}

	auto VulkanTexture::get_height() -> uint16_t
	{
		return height;
	}

	auto VulkanTexture::get_name() -> const char*
	{
		return name;
	}

	auto VulkanTexture::get_type() -> AshImageType
	{
		return type;
	}

	auto VulkanTexture::get_desciption(TextureDescription& desc) -> void 
	{
		desc.native_handle = vkImage;
		desc.name = name;
		desc.width = width;
		desc.height = height;
		desc.depth = depth;
		desc.mipmaps = mipmaps;
		desc.render_target = render_target;
		desc.compute_access = compute_access;
		desc.format = format;
		desc.type = type;
	}
	
	auto VulkanTexture::get_default_sampler() -> std::shared_ptr <Sampler>
	{
		return defaultVulkanSampler;
	}



	/********************* vulkan texture view *****************************/
	VulkanTextureView::VulkanTextureView(const TextureViewCreation& ci, std::shared_ptr<Texture> _parentTexture)
	{
		parentTexture = std::weak_ptr<Texture>(_parentTexture);
		viewType = ci.view_type;
		VkImageViewCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		info.image = (VkImage)_parentTexture->get_native_texture_handle();
		info.format = ash_format_to_vk(ci.format);
		if (TextureFormat::has_depth_or_stencil(info.format)) {

			info.subresourceRange.aspectMask = TextureFormat::has_depth(info.format) ? VK_IMAGE_ASPECT_DEPTH_BIT : 0;
		}
		else {
			info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		}
		info.viewType = ash_image_view_type_to_vk(ci.view_type);
		info.subresourceRange.baseMipLevel = ci.sub_resource.mip_base_level;
		info.subresourceRange.levelCount = ci.sub_resource.mip_level_count;
		info.subresourceRange.baseArrayLayer = ci.sub_resource.array_base_layer;
		info.subresourceRange.layerCount = ci.sub_resource.array_layer_count;
		VK_CHECK_RESULT(vkCreateImageView(VulkanContext::get_vulkan_device(), &info, VulkanContext::get_vulkan_allocation_callbacks(), &vkImageView));
		VulkanContext::set_resource_name(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)vkImageView, ci.name);

	}

	VulkanTextureView::~VulkanTextureView()
	{
		if (vkImageView != VK_NULL_HANDLE)
		{
			vkDestroyImageView(VulkanContext::get_vulkan_device(), vkImageView, VulkanContext::get_vulkan_allocation_callbacks());
		}
	}

	auto VulkanTextureView::get_parent_texture() -> std::shared_ptr<Texture>
	{
		return parentTexture.lock();
	}

	auto VulkanTextureView::get_native_view_handle() -> void*
	{
		return vkImageView;
	}

	auto VulkanTextureView::get_view_type() -> AshImageViewType
	{
		return viewType;
	}

	auto VulkanTextureView::get_view_format() -> AshFormat
	{
		return viewFormat;
	}
	
}