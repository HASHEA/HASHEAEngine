#include "VulkanSampler.h"
#include "VulkanHelper.hpp"
#include "VulkanContext.h"
namespace RHI
{
	VulkanSampler::VulkanSampler(const SamplerCreation& ci)
	{
		m_name_storage = ci.name ? ci.name : "";

		VkSamplerCreateInfo create_info{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		create_info.minFilter					= ash_filter_to_min_mag_vk(ci.minFilter);
		create_info.magFilter					= ash_filter_to_min_mag_vk(ci.magFilter);
		create_info.mipmapMode					= ash_filter_to_mip_vk(ci.mipFilter);
		create_info.addressModeU				= ash_sampler_address_mode_to_vk(ci.address_mode_u);
		create_info.addressModeV				= ash_sampler_address_mode_to_vk(ci.address_mode_v);
		create_info.addressModeW				= ash_sampler_address_mode_to_vk(ci.address_mode_w);
		create_info.anisotropyEnable			= ci.enable_anisotropy;
		create_info.compareEnable				= ci.enable_compare;
		if (ci.enable_compare)
		{
			create_info.compareOp				= ash_compare_option_to_vk(ci.compare_op);
		}
		create_info.unnormalizedCoordinates		= ci.unnormalized_coordinates;
		create_info.minLod						= ci.min_lod;
		create_info.maxLod						= ci.max_lod;	
		create_info.maxAnisotropy				= ci.max_anisotropy;
		create_info.mipLodBias					= ci.mip_lod_bias;
		create_info.borderColor					= ash_border_color_to_vk(ci.border_color);
		VkSamplerReductionModeCreateInfoEXT createInfoReduction = { VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO_EXT };
		if (ci.reductionMode != ASH_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE) {
			createInfoReduction.reductionMode = ash_sampler_reduction_mode_to_vk(ci.reductionMode);
			create_info.pNext = &createInfoReduction;
		}
		VK_CHECK_RESULT(vkCreateSampler(VulkanContext::get_vulkan_device(), &create_info, VulkanContext::get_vulkan_allocation_callbacks(), &vkSampler));
		VulkanContext::set_resource_name(
			VK_OBJECT_TYPE_SAMPLER,
			(uint64_t)vkSampler,
			m_name_storage.empty() ? nullptr : m_name_storage.c_str());
	}
	VulkanSampler::~VulkanSampler()
	{
		if (immediate_deletion)
		{
			if (vkSampler != VK_NULL_HANDLE)
			{
				vkDestroySampler(VulkanContext::get_vulkan_device(), vkSampler, VulkanContext::get_vulkan_allocation_callbacks());
			}
		}
		else
		{
			auto handle = this->vkSampler;
			auto alloc = VulkanContext::get_vulkan_allocation_callbacks();
			if (vkSampler != VK_NULL_HANDLE)
			{
				VulkanContext::get_current_frame_deletion_queue().emplace([handle,alloc]() {
					vkDestroySampler(VulkanContext::get_vulkan_device(), handle, alloc);

					});
			}
		}
		
	}
	auto VulkanSampler::create(const SamplerCreation& ci) -> std::shared_ptr<VulkanSampler>
	{
		return Ash_New_Shared<VulkanSampler>(ci);
	}
	auto VulkanSampler::get_native_handle() -> void*
	{
		return vkSampler;
	}
	auto VulkanSampler::get_name() -> const char* 
	{
		return m_name_storage.empty() ? nullptr : m_name_storage.c_str();
	}
	VulkanSamplerView::VulkanSamplerView(const char* name, std::shared_ptr<Sampler> parent)
	{
		parentSampler = parent;
		if (name)
		{
			m_name_storage = name;
		}
	}
	VulkanSamplerView::~VulkanSamplerView()
	{
		parentSampler.reset();
	}
	std::shared_ptr<Sampler> VulkanSamplerView::get_parent_sampler()
	{
		return parentSampler.lock();
	}
	auto VulkanSamplerView::get_native_handle() -> void*
	{
		auto ptr = parentSampler.lock();
		if (ptr)
		{
			return ptr->get_native_handle();
		}
		return nullptr;
	}
	auto VulkanSamplerView::get_name() -> const char*
	{
		return m_name_storage.empty() ? nullptr : m_name_storage.c_str();
	}
}
