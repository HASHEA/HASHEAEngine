#include "VulkanBuffer.h"
#include "Base/hassert.h"
#include "VulkanContext.h"
namespace RHI
{
	VulkanBuffer::VulkanBuffer(const BufferCreation& ci)
	{
		name = ci.name;
		usageFlags = ci.type_flags;
		usage = ci.usage;
		size = ci.size;
		ready = false;
		const bool use_global_buffer = (ci.type_flags & k_dynamic_buffer_mask) != 0;
		HLogInfo("creating buffer : {} ...", ci.name);
		if (ci.usage == AshResourceUsageType::Dynamic && use_global_buffer)// create as dynamic buffer
		{
			H_ASSERTLOG(!(ci.initial_data), "Dynamic Buffer Can not use initial data !");
			dynamic = true;
		}
		else
		{
			VkBufferCreateInfo buffer_info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
			buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | ci.type_flags;
			buffer_info.size = ci.size > 0 ? ci.size : 1;       // 0 sized creations are not permitted.
			// NOTE: technically we could map a buffer if the device exposes a heap
			// with MEMORY_PROPERTY_DEVICE_LOCAL_BIT and MEMORY_PROPERTY_HOST_VISIBLE_BIT
			// but that's usually very small (256MB) unless resizable bar is enabled.
			// We simply don't allow it for now.
			// validation
			if (ci.device_only)
			{
				H_ASSERTLOG(!ci.persistent, "Device only buffer can not be mappable !");
				H_ASSERTLOG((ci.usage == AshResourceUsageType::Dynamic || ci.usage == AshResourceUsageType::Stream), "Device only buffer only can be immutable or stream usage !");
			}
			VmaAllocationCreateInfo allocation_create_info{};
			allocation_create_info.flags = VMA_ALLOCATION_CREATE_STRATEGY_BEST_FIT_BIT;
			if (ci.persistent) {
				allocation_create_info.flags = allocation_create_info.flags | VMA_ALLOCATION_CREATE_MAPPED_BIT;
			}
			if (ci.device_only) {
				allocation_create_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
			}
			else {
				allocation_create_info.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
			}
			
			VmaAllocationInfo allocation_info{};
			VK_CHECK_RESULT(vmaCreateBuffer(VulkanContext::get_vma_allocator(),&buffer_info,&allocation_create_info,&vkBuffer,&vmaAllocation,&allocation_info));
#ifdef ASH_DEBUG
			vmaSetAllocationName(VulkanContext::get_vma_allocator(), vmaAllocation, name);
#endif // ASH_DEBUG
			VulkanContext::set_resource_name(VK_OBJECT_TYPE_BUFFER, (uint64_t)vkBuffer, name);
			vkDeviceMemory = allocation_info.deviceMemory;
			if (ci.persistent)
			{
				mappedData = static_cast<uint8_t*>(allocation_info.pMappedData);
				H_ASSERTLOG(mappedData != nullptr, "Fatal: Init Mapped Buffer Failed !");
			}
			if (ci.initial_data)
			{
				if (!ci.persistent)
				{
					void* data;
					vmaMapMemory(VulkanContext::get_vma_allocator(), vmaAllocation, &data);
					memcpy(data, ci.initial_data, (size_t)ci.size);
					vmaUnmapMemory(VulkanContext::get_vma_allocator(), vmaAllocation);
				}
				else
				{
					if (mappedData)
					{
						memcpy(mappedData, ci.initial_data, (size_t)ci.size);
					}
				}
			}
		}
		ready = true;
	}
	VulkanBuffer::~VulkanBuffer()
	{
		if (immediate_deletion)
		{
			vmaDestroyBuffer(VulkanContext::get_vma_allocator(), vkBuffer, vmaAllocation);
			HLogInfo("deleting buffer : {} ...", name);
		}
		else
		{
			if (vkBuffer != VK_NULL_HANDLE)
			{
				auto handle = vkBuffer;
				auto alloc = vmaAllocation;
				auto sname = name;
				VulkanContext::get_current_frame_deletion_queue().emplace([handle, alloc,sname]() {
					HLogInfo("deleting buffer : {} ...", sname);
					vmaDestroyBuffer(VulkanContext::get_vma_allocator(), handle, alloc);
					});
			}
		}
		ready = false;
	}
	auto VulkanBuffer::create(const BufferCreation& ci) -> std::shared_ptr<VulkanBuffer>
	{
		return Ash_New_Shared<VulkanBuffer>(ci);
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
	auto VulkanBuffer::is_dynamic() -> bool
	{
		return dynamic;
	}
	VulkanDynamicBuffer::VulkanDynamicBuffer(const BufferCreation& ci) : VulkanBuffer(ci)
	{
	}
	VulkanDynamicBuffer::~VulkanDynamicBuffer()
	{
	}
	auto VulkanDynamicBuffer::dynamic_allocate(uint32_t size, size_t alignment) -> void*
	{
		uint32_t target_size = dynamic_allocated_size + (uint32_t)memory_align(size, alignment);
		if (target_size > size)
		{
			HLogWarning("out of dynamic buffer memory when allocate !");
			return nullptr;
		}
		void* mapped_memory = dynamic_mapped_memory + dynamic_allocated_size;
		dynamic_allocated_size = target_size;
		return mapped_memory;
	}
	auto VulkanDynamicBuffer::dynamic_allocate_buffer(std::shared_ptr<VulkanBuffer> buffer, uint32_t size,  size_t alignment) -> void*
	{
		if (!buffer->is_dynamic())
		{
			return nullptr;
		}
		if (buffer->get_global_offset() == UINT32_MAX)
		{
			//allocate_new
			buffer->globalOffset = dynamic_allocated_size;
			auto memeory = dynamic_allocate(buffer->get_size(), alignment);
			//we consider that all dynamic buffers are persistent
			buffer->mappedData = (uint8_t*)memeory;	
		}
		return buffer->mappedData;
	}
}