#include "Graphics/Vulkan/VulkanBarrierPolicy.h"
#include "doctest.h"

TEST_CASE("Vulkan barrier policy maps ConstBuffer to vertex, fragment, and compute stages")
{
	constexpr VkPipelineStageFlags expected =
		static_cast<VkPipelineStageFlags>(
			VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

	constexpr VkPipelineStageFlags actual =
		RHI::VulkanBarrierPolicy::const_buffer_stage_mask();

	CHECK((actual & VK_PIPELINE_STAGE_VERTEX_SHADER_BIT) != 0u);
	CHECK((actual & VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT) != 0u);
	CHECK((actual & VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT) != 0u);
	CHECK(actual == expected);
	CHECK((actual & VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT) == 0u);
	CHECK((actual & VK_PIPELINE_STAGE_ALL_COMMANDS_BIT) == 0u);
}
