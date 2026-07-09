#include "VulkanHelper.hpp"
#include "VulkanContext.h"
#include "VulkanTexture.h"
#include "VulkanSampler.h"
namespace RHI
{
	namespace
	{
		static const char* get_texture_view_suffix(AshResourceViewType view_type)
		{
			switch (view_type)
			{
			case AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_RTV:
				return ".RTV";
			case AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_DSV:
				return ".DSV";
			case AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_SRV:
				return ".SRV";
			case AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_UAV:
				return ".UAV";
			default:
				return ".View";
			}
		}

		static AshResourceState resolve_vulkan_texture_creation_state(const TextureCreation& ci)
		{
			// Textures with CPU-provided initial data are born in undefined layout and
			// become their declared initial_state only after the upload command path runs.
			return ci.initial_data ? AshResourceState::Unknown : ci.initial_state;
		}
	}
	
	VulkanTexture::VulkanTexture(const TextureCreation& ci)
	{
		const AshResourceState creation_state = resolve_vulkan_texture_creation_state(ci);
		m_nameStorage = ci.name ? ci.name : "UnnamedVulkanTexture";
		m_sCreation = ci;
		m_sCreation.name = m_nameStorage.c_str();
		m_uAspectFlags = 0;
		cube = false;
		if (ci.type == AshImageType::Ash_TextureCube || ci.type == AshImageType::Ash_Texture_Cube_Array)
		{
			cube = true;
		}
		sparse = (ci.flags & AshTextureCreateFlagBits::ASH_TEXTURE_CREATE_FLAG_SPARSE) != 0;
		if (sparse && !VulkanContext::get()->get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::SparseBinding))
		{
			// sparse 是可选设备能力,调用方必须先查能力位;此处兜底降级为普通纹理
			H_ASSERTLOG(false, "Sparse texture '{0}' requested but device lacks sparse binding; creating as non-sparse.", m_nameStorage.c_str());
			sparse = false;
		}
		VkImageUsageFlags texUsageFlags = ash_texture_usage_to_vk(ci.uUsageFlags);
		texUsageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		//// Create the image
		VkImageCreateInfo vkImageCI = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		vkImageCI.format = get_vk_texture_format_info(ci.format).vkFormat;
		vkImageCI.flags = (cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0) | (sparse ? (VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT | VK_IMAGE_CREATE_SPARSE_BINDING_BIT) : 0);
		vkImageCI.imageType = ash_image_type_to_vk(ci.type);
		vkImageCI.extent.width = ci.width;
		vkImageCI.extent.height = ci.height;
		vkImageCI.extent.depth = ci.depth;
		vkImageCI.mipLevels = ci.mip_level_count;
		vkImageCI.arrayLayers = ci.array_layer_count;
		vkImageCI.samples = VK_SAMPLE_COUNT_1_BIT;
		vkImageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
		vkImageCI.usage = texUsageFlags;
		vkImageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		vkImageCI.initialLayout = creation_state == AshResourceState::Unknown ?
			VK_IMAGE_LAYOUT_UNDEFINED :
			ash_resource_state_to_vk_image_layout(creation_state);
		VmaAllocationCreateInfo memory_info{};
		switch (ci.memoryType)
		{
		case AshResourceAccessType::ASH_RESOURCE_ACCESS_GPU_ONLY:
			memory_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
			break;
		case AshResourceAccessType::ASH_RESOURCE_ACCESS_READ:
			memory_info.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
			break;
		case AshResourceAccessType::ASH_RESOURCE_ACCESS_WRITE:
			memory_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
			break;
		default:
			memory_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
			break;
		}
		if (ci.alias == nullptr)
		{
			if (sparse)
			{
				VK_CHECK_RESULT(vkCreateImage(VulkanContext::get_vulkan_device(), &vkImageCI, VulkanContext::get_vulkan_allocation_callbacks(), &vkImage));
			}
			else
			{
				const bool created = ASH_VMA_CREATE_IMAGE(VulkanContext::get(), vkImageCI, memory_info.usage, vkImage, vmaAllocation, m_sCreation.name);
				H_ASSERT(created);
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
		VulkanContext::set_resource_name(VK_OBJECT_TYPE_IMAGE, (uint64_t)vkImage, m_sCreation.name);
		m_ResourceLayoutTracker = VulkanResourceTracker(creation_state);
		state = creation_state;
		m_uAspectFlags = get_aspect_flags_from_format(ci.format);

	}

	VulkanTexture::~VulkanTexture()
	{
		
		if (immediate_deletion || swapchain_texture)
		{	
			if (defaultRTV != nullptr)
			{
				defaultRTV->immediate_deletion = true;
				defaultRTV.reset();
			}
			if (defaultSRV != nullptr)
			{
				defaultSRV->immediate_deletion = true;
				defaultSRV.reset();
			}
			if (defaultUAV != nullptr)
			{
				defaultUAV->immediate_deletion = true;
				defaultUAV.reset();
			}
			if (vkImage != VK_NULL_HANDLE && !swapchain_texture)
			{
				if (this->aliasTexture == nullptr)
				{
					if (sparse)
					{
						vkDestroyImage(VulkanContext::get_vulkan_device(), vkImage, VulkanContext::get_vulkan_allocation_callbacks());
					}
					else
					{
						ASH_VMA_DESTROY_IMAGE(VulkanContext::get(), vkImage, vmaAllocation);
					}
				}
				else
				{
					//vma allocation = 0 witch mean vma won't free memory for this alias image
					ASH_VMA_DESTROY_IMAGE(VulkanContext::get(), vkImage, vmaAllocation);
				}
			}
			if (aliasTexture.use_count() == 1)
			{
				aliasTexture->immediate_deletion = true;
			}
			vkImage = VK_NULL_HANDLE;
		}
		else
		{
			if (defaultRTV != nullptr)
			{
				defaultRTV.reset();
			}
			if (defaultSRV != nullptr)
			{
				defaultSRV.reset();
			}
			if (defaultUAV != nullptr)
			{
				defaultUAV.reset();
			}
			//push deletor into deletion queue
			auto handle = this->vkImage;
			bool isAlias = this->aliasTexture != nullptr;
			bool isSparse = sparse;
			auto alloc = vmaAllocation;
			if (vkImage != VK_NULL_HANDLE && !swapchain_texture)
			{
				VulkanContext::get_current_frame_deletion_queue().emplace([handle,isAlias, isSparse, alloc]() {
					if (!isAlias)
					{
						if (isSparse)
						{
							vkDestroyImage(VulkanContext::get_vulkan_device(), handle, VulkanContext::get_vulkan_allocation_callbacks());
						}
						else
						{
							ASH_VMA_DESTROY_IMAGE(VulkanContext::get(), handle, alloc);
						}
					}
					else
					{
						//vma allocation = 0 witch mean vma won't free memory for this alias image
						ASH_VMA_DESTROY_IMAGE(VulkanContext::get(), handle, alloc);
					}
					});
			}
		}
	}

	auto VulkanTexture::create(const TextureCreation& ci) -> std::shared_ptr<VulkanTexture>
	{
		auto ret = Ash_New_Shared<VulkanTexture>(ci);
		ret->init();
		return ret;
	}

	auto VulkanTexture::init() -> void
	{
		
	}

	auto VulkanTexture::get_native_handle() -> void*
	{
		return vkImage;
	}

	auto VulkanTexture::get_alias_texture() -> std::shared_ptr<Texture>
	{
		return aliasTexture;
	}

	auto VulkanTexture::get_default_rtv() -> std::shared_ptr<TextureView>
	{
		if (!defaultRTV && (m_sCreation.uUsageFlags & (ASH_TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | ASH_TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)))
		{
			TextureViewCreation tvc{};
			tvc.sub_resource.uMipCount = m_sCreation.mip_level_count;
			tvc.sub_resource.uArrayCount = m_sCreation.array_layer_count;
			tvc.name = m_sCreation.name;
			tvc.view_dim = ash_image_type_to_image_view_type(m_sCreation.type);
			tvc.view_type = (m_sCreation.uUsageFlags & ASH_TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) ?
				AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_DSV : AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_RTV;
			tvc.format = m_sCreation.format;
			defaultRTV = Ash_New_Shared<VulkanTextureView>(tvc, shared_from_this());
		}
		return defaultRTV;
	}

	auto VulkanTexture::get_default_srv() -> std::shared_ptr<TextureView>
	{
		if (!defaultSRV && (m_sCreation.uUsageFlags & ASH_TEXTURE_USAGE_SAMPLED_BIT))
		{
			TextureViewCreation tvc{};
			tvc.sub_resource.uMipCount = m_sCreation.mip_level_count;
			tvc.sub_resource.uArrayCount = m_sCreation.array_layer_count;
			tvc.name = m_sCreation.name;
			tvc.view_dim = ash_image_type_to_image_view_type(m_sCreation.type);
			tvc.view_type = AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_SRV;
			tvc.format = m_sCreation.format;
			defaultSRV = Ash_New_Shared<VulkanTextureView>(tvc, shared_from_this());
		}
		return defaultSRV;
	}

	auto VulkanTexture::get_default_uav() -> std::shared_ptr<TextureView>
	{
		if (!defaultUAV && (m_sCreation.uUsageFlags & ASH_TEXTURE_USAGE_STORAGE_BIT))
		{
			TextureViewCreation tvc{};
			tvc.sub_resource.uMipCount = m_sCreation.mip_level_count;
			tvc.sub_resource.uArrayCount = m_sCreation.array_layer_count;
			tvc.name = m_sCreation.name;
			tvc.view_dim = ash_image_type_to_image_view_type(m_sCreation.type);
			tvc.view_type = AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_UAV;
			tvc.format = m_sCreation.format;
			defaultUAV = Ash_New_Shared<VulkanTextureView>(tvc, shared_from_this());
		}
		return defaultUAV;
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
		return m_sCreation.format;
	}

	auto VulkanTexture::is_render_target() -> bool
	{
		return state == AshResourceState::RTV;
	}

	auto VulkanTexture::get_mip_maps_count() -> uint8_t
	{
		return m_sCreation.mip_level_count;
	}

	auto VulkanTexture::get_layer_count() -> uint16_t
	{
		return m_sCreation.array_layer_count;
	}

	auto VulkanTexture::get_depth() -> uint16_t
	{
		return m_sCreation.depth;
	}

	auto VulkanTexture::get_width() -> uint16_t
	{
		return m_sCreation.width;
	}

	auto VulkanTexture::get_height() -> uint16_t
	{
		return m_sCreation.height;
	}

	auto VulkanTexture::get_name() -> const char*
	{
		return m_sCreation.name;
	}

	auto VulkanTexture::get_type() -> AshImageType
	{
		return m_sCreation.type;
	}

	auto VulkanTexture::get_resource_state() -> AshResourceState
	{
		return state;
	}

	auto VulkanTexture::set_resource_state(AshResourceState _state) -> void
	{
		this->state = _state;
	}
	//fit
	auto VulkanTexture::resolve_subresource_range(const AshSubresourceRange& range) const -> AshSubresourceRange
	{
		const uint32_t mipLevels = m_sCreation.mip_level_count > 0 ? m_sCreation.mip_level_count : 1;
		const uint32_t arrayLayerCount = m_sCreation.array_layer_count > 0 ? m_sCreation.array_layer_count : 1;
		return range.resolve(mipLevels, arrayLayerCount);
	}

	auto VulkanTexture::get_resource_tracker() -> VulkanResourceTracker&
	{
		return m_ResourceLayoutTracker;
	}

	auto VulkanTexture::get_desciption() const -> const TextureCreation&
	{
		return m_sCreation;
	}
	
	/********************* vulkan texture view *****************************/
	VulkanTextureView::VulkanTextureView(const TextureViewCreation& ci, std::shared_ptr<Texture> _parentTexture)
	{
		parentTexture = _parentTexture;
		m_sInfo = ci;
		if (m_sInfo.name == nullptr)
		{
			const char* parentName = _parentTexture && _parentTexture->get_name() ? _parentTexture->get_name() : "UnnamedVulkanTexture";
			m_nameStorage = std::string(parentName) + get_texture_view_suffix(ci.view_type);
			m_sInfo.name = m_nameStorage.c_str();
		}
		auto parentVulkanTexture = std::static_pointer_cast<VulkanTexture>(_parentTexture);
		auto resolvedRange = parentVulkanTexture->resolve_subresource_range(ci.sub_resource);
		m_sInfo.sub_resource = resolvedRange;
		VkImageViewCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		info.image = (VkImage)_parentTexture->get_native_handle();
		info.format = get_vk_texture_format_info(ci.format).vkFormat;
		if (TextureFormat::has_depth_or_stencil(info.format)) {
			info.subresourceRange.aspectMask = 0;
			if (TextureFormat::has_depth(info.format))
			{
				info.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
			}
			if (TextureFormat::has_stencil(info.format))
			{
				info.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			}
		}
		else {
			info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		}
		info.viewType = ash_image_view_dim_to_vk(ci.view_dim);
		info.subresourceRange.baseMipLevel = resolvedRange.uBaseMipLevel;
		info.subresourceRange.levelCount = resolvedRange.uMipCount;
		info.subresourceRange.baseArrayLayer = resolvedRange.uBaseArraySlice;
		info.subresourceRange.layerCount = resolvedRange.uArrayCount;
		VK_CHECK_RESULT(vkCreateImageView(VulkanContext::get_vulkan_device(), &info, VulkanContext::get_vulkan_allocation_callbacks(), &vkImageView));
		VulkanContext::set_resource_name(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)vkImageView, m_sInfo.name);

	}

	VulkanTextureView::~VulkanTextureView()
	{
		if (immediate_deletion)
		{
			if (vkImageView != VK_NULL_HANDLE)
			{
				vkDestroyImageView(VulkanContext::get_vulkan_device(), vkImageView, VulkanContext::get_vulkan_allocation_callbacks());
			}
		}	
		else
		{
			auto handle = this->vkImageView;
			if (handle != VK_NULL_HANDLE)
			{
				
				VulkanContext::get_current_frame_deletion_queue().emplace([handle]() {
					vkDestroyImageView(VulkanContext::get_vulkan_device(), handle, VulkanContext::get_vulkan_allocation_callbacks()); });
			}		
		}
		parentTexture.reset();
	}

	auto VulkanTextureView::create(const TextureViewCreation& ci, std::shared_ptr<Texture> parentTexture) -> std::shared_ptr<VulkanTextureView>
	{
		return Ash_New_Shared<VulkanTextureView>(ci, parentTexture);
	}

	auto VulkanTextureView::get_parent_texture() -> std::shared_ptr<Texture>
	{
		return parentTexture.lock();
	}

	auto VulkanTextureView::get_native_handle() -> void*
	{
		return vkImageView;
	}

	auto VulkanTextureView::get_view_dim() -> AshResourceViewDimension
	{
		return m_sInfo.view_dim;
	}

	auto VulkanTextureView::get_view_format() -> AshFormat
	{
		return m_sInfo.format;
	}

	auto VulkanTextureView::get_view_type() -> AshResourceViewType
	{
		return m_sInfo.view_type;
	}

	auto VulkanTextureView::get_subresource_range() -> const AshSubresourceRange&
	{
		return m_sInfo.sub_resource;
	}

	auto VulkanTextureView::get_name() -> const char*
	{
		return m_sInfo.name;
	}
	
}
