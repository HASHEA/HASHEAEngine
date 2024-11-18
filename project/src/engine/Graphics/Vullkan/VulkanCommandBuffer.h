#pragma once
#include "VulkanHelper.hpp"
#include "VulkanWrapper.h"
#include "Graphics/CommandBuffer.h"
namespace RHI
{
	class VulkanCommandBuffer : public CommandBuffer
	{
	public:
		VulkanCommandBuffer();
		~VulkanCommandBuffer();
		NO_COPYABLE(VulkanCommandBuffer);
	private:

	};
};