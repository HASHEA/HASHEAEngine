#include "VulkanBuffer.h"
namespace RHI
{
	VulkanBuffer::VulkanBuffer(const BufferCreation& ci)
	{
		VkBufferCreateInfo buffer_info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	}
	VulkanBuffer::~VulkanBuffer()
	{
	}
	auto VulkanBuffer::get_size() -> uint32_t
	{
		return size;
	}
	auto VulkanBuffer::get_name() -> const char*
	{
		return name;
	}
	auto VulkanBuffer::get_global_offset() -> uint32_t
	{
		return globalOffset;
	}
	auto VulkanBuffer::is_ready() -> bool
	{
		return ready;
	}
	auto VulkanBuffer::get_mapped_data() -> uint8_t*
	{
		return mappedData;
	}
}