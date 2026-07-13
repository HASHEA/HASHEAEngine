#pragma once

#include <volk/volk.h>

namespace RHI::VulkanBarrierPolicy
{
	// VulkanCommandBuffer instances currently come from the graphics queue family,
	// so graphics and compute shader stage bits are legal on every current command buffer.
	// A future dedicated compute-only command buffer must make this policy queue-aware.
	[[nodiscard]] inline constexpr auto const_buffer_stage_mask() noexcept
		-> VkPipelineStageFlags
	{
		return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
	}
} // namespace RHI::VulkanBarrierPolicy
