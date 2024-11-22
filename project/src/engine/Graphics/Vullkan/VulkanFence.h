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
		auto reset() -> void;
		auto wait() -> bool;
		auto wait_and_reset() -> void;
		auto check_state() -> bool;
		auto is_signaled() -> bool;
		inline auto get_handle() const
		{
			return fence;
		}
		inline auto set_signaled(bool signaled)
		{
			this->signaled = signaled;
		}
	private:
		VkFence fence;
		bool    signaled;
	};
};