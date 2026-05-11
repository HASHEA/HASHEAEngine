#pragma once
#include "Graphics/Sampler.h"
#include "VulkanWrapper.h"
#include <memory>
#include <string>
namespace RHI
{
	class VulkanSamplerView : public SamplerView
	{
	public:
		VulkanSamplerView(const char* name, std::shared_ptr<Sampler> parent);
		~VulkanSamplerView();
	public:
		std::shared_ptr<Sampler> get_parent_sampler() override;
		auto get_native_handle() -> void* override;
		auto get_name() -> const char* override;
	private:
		std::string m_name_storage{};
		std::weak_ptr<Sampler> parentSampler;
	};
	class VulkanSampler : public Sampler
	{
	public:
		VulkanSampler(const SamplerCreation& ci);
		~VulkanSampler();
		static auto create(const SamplerCreation& ci) -> std::shared_ptr<VulkanSampler>;
	private:
		VkSampler vkSampler = VK_NULL_HANDLE;
		std::string m_name_storage{};
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
