#pragma once
#include "VulkanHelper.hpp"
namespace RHI
{
	class VulkanFence
	{
	public:
		VulkanFence(bool createSignaled = true);
		~VulkanFence();
		NO_COPYABLE(VulkanFence);
		auto Reset() -> void;
		auto Wait() -> bool;
		auto WaitAndReset() -> void;
		auto CheckState() -> bool;
		auto IsSignaled() -> bool;
		inline auto GetHandle() const
		{
			return fence;
		}
		inline auto SetSignaled(bool signaled)
		{
			this->signaled = signaled;
		}
	private:
		VkFence fence;
		bool    signaled;
	};
};