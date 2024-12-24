#pragma once
#include "Graphics/Sampler.h"
#include "VulkanWrapper.h"
#include <memory>
namespace RHI
{
	class VulkanSampler : public Sampler
	{
	public:
		VulkanSampler(const SamplerCreation& ci);
		~VulkanSampler();
		static auto create(const SamplerCreation& ci) -> std::shared_ptr<VulkanSampler>;
	private:
		VkSampler vkSampler = VK_NULL_HANDLE;
		const char* name = nullptr;
	public:
		// from Sampler
		auto get_native_handle() -> void* override;
		auto get_name() -> const char* override;
	public:
		//for vk
		inline auto get_vk_sampler() -> VkSampler
		{
			return vkSampler;
		}

	};
}