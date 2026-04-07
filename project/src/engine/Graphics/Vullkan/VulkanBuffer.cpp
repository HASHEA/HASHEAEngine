#include "Graphics/CommandBuffer.h"
#include "VulkanBuffer.h"
#include "Base/hassert.h"
#include "VulkanContext.h"
#include "VulkanResourceTracker.h"
#include <memory>
namespace RHI
{
	namespace VKBufferHelper
	{
		static VkBufferUsageFlags get_vk_buffer_usage_flags(AshBufferUsageFlags uUsageFlags)
		{
			VkBufferUsageFlags uVkBufferUsageFlags = 0;

			// These macros/constants should be defined in your engine or Vulkan headers
			if (uUsageFlags & ASH_BUFFER_USAGE_TRANSFER_SRC_BIT)
				uVkBufferUsageFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			if (uUsageFlags & ASH_BUFFER_USAGE_TRANSFER_DST_BIT)
				uVkBufferUsageFlags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			if (uUsageFlags & ASH_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT)
				uVkBufferUsageFlags |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
			if (uUsageFlags & ASH_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT)
				uVkBufferUsageFlags |= VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
			if (uUsageFlags & ASH_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
				uVkBufferUsageFlags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
			if (uUsageFlags & ASH_BUFFER_USAGE_STORAGE_BUFFER_BIT)
				uVkBufferUsageFlags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			if (uUsageFlags & ASH_BUFFER_USAGE_INDEX_BUFFER_BIT)
				uVkBufferUsageFlags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
			if (uUsageFlags & ASH_BUFFER_USAGE_VERTEX_BUFFER_BIT)
				uVkBufferUsageFlags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
			if (uUsageFlags & ASH_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
				uVkBufferUsageFlags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
#ifdef VK_KHR_ray_tracing_pipeline
			if (uUsageFlags & ASH_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR)
				uVkBufferUsageFlags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
			if (uUsageFlags & ASH_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR)
				uVkBufferUsageFlags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
			if (uUsageFlags & ASH_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR)
				uVkBufferUsageFlags |= VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR;
			if (uUsageFlags & ASH_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
				uVkBufferUsageFlags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

#endif
			// Add more mappings if your engine defines additional usage bits

			return uVkBufferUsageFlags;
		}

		static AshBufferUsageFlags get_buffer_usage_flags_from_vk(VkBufferUsageFlags vkUsageFlags)
		{
			AshBufferUsageFlags usageFlags = 0;

			if (vkUsageFlags & VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
				usageFlags |= ASH_BUFFER_USAGE_TRANSFER_SRC_BIT;
			if (vkUsageFlags & VK_BUFFER_USAGE_TRANSFER_DST_BIT)
				usageFlags |= ASH_BUFFER_USAGE_TRANSFER_DST_BIT;
			if (vkUsageFlags & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT)
				usageFlags |= ASH_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
			if (vkUsageFlags & VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT)
				usageFlags |= ASH_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
			if (vkUsageFlags & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
				usageFlags |= ASH_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
			if (vkUsageFlags & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
				usageFlags |= ASH_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			if (vkUsageFlags & VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
				usageFlags |= ASH_BUFFER_USAGE_INDEX_BUFFER_BIT;
			if (vkUsageFlags & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
				usageFlags |= ASH_BUFFER_USAGE_VERTEX_BUFFER_BIT;
			if (vkUsageFlags & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
				usageFlags |= ASH_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
#ifdef VK_KHR_ray_tracing_pipeline
			if (vkUsageFlags & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR)
				usageFlags |= ASH_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
			if (vkUsageFlags & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR)
				usageFlags |= ASH_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
			if (vkUsageFlags & VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR)
				usageFlags |= ASH_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR;
			if (vkUsageFlags & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
				usageFlags |= ASH_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
#endif
			// Add more mappings if your engine defines additional usage bits

			return usageFlags;
		}

	};
	VulkanBuffer::~VulkanBuffer()
	{
		destroy();
	}
	auto VulkanBuffer::create(const BufferCreation& ci) -> bool
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		m_pName = ci.name;
		m_sCreationInfo = ci;
		m_bReady = false;
		dynamic = !ci.force_static && (ci.access_type == AshResourceAccessType::ASH_RESOURCE_ACCESS_WRITE) && has_any_flags(ci.usage_flags, k_dynamic_buffer_mask);
		HLogInfo("creating buffer : {} ...", ci.name);
		bool bHostCoherent = false;
		VmaMemoryUsage        eMemUsage = VMA_MEMORY_USAGE_UNKNOWN;
		VkBufferUsageFlags    uBufferUsageFlags = VKBufferHelper::get_vk_buffer_usage_flags(ci.usage_flags);
		switch (ci.access_type)
		{
		case AshResourceAccessType::ASH_RESOURCE_ACCESS_GPU_ONLY:
		{
			eMemUsage = VmaMemoryUsage::VMA_MEMORY_USAGE_GPU_ONLY;
			uBufferUsageFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			break;
		}
		case AshResourceAccessType::ASH_RESOURCE_ACCESS_READ:
		{
			eMemUsage = VmaMemoryUsage::VMA_MEMORY_USAGE_GPU_TO_CPU;
			uBufferUsageFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			bHostCoherent = true;
			break;
		}
		case AshResourceAccessType::ASH_RESOURCE_ACCESS_WRITE:
		{
			eMemUsage = VmaMemoryUsage::VMA_MEMORY_USAGE_CPU_TO_GPU;
			uBufferUsageFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			bHostCoherent = true;
			break;
		}
		default:
		{
			H_ASSERT(false);
			break;
		}
		}
		m_sCreationInfo.usage_flags = VKBufferHelper::get_buffer_usage_flags_from_vk(uBufferUsageFlags);
		//vma allocate buffer
		{
			bool bRetCode = VulkanContext::get()->vma_create_buffer(m_sCreationInfo.size, uBufferUsageFlags,
				eMemUsage, m_pVkBuffer, m_pVMAAllocation, (void**)& m_pMappedData);
			ASH_LOG_PROCESS_ERROR(bRetCode);
		}
		//validate result
		{
			VmaAllocationInfo     allocationInfo = {};
			VkMemoryPropertyFlags memoryPropertyFlags = {};
			vmaGetAllocationInfo(VulkanContext::get_vma_allocator(), m_pVMAAllocation, &allocationInfo);
			ASH_LOG_PROCESS_ERROR(allocationInfo.memoryType < VulkanContext::get_device_memory_properties().memoryTypeCount && allocationInfo.memoryType < VK_MAX_MEMORY_TYPES);
			vmaGetMemoryTypeProperties(VulkanContext::get_vma_allocator(), allocationInfo.memoryType, &memoryPropertyFlags);
			m_bCoherent = (memoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
			VmaAllocationInfo sAllocInfo;
			vmaGetAllocationInfo(VulkanContext::get_vma_allocator(), m_pVMAAllocation, &sAllocInfo);
			m_vkDeviceSize = sAllocInfo.size;
			ASH_LOG_PROCESS_ERROR(m_vkDeviceSize > 0);
		}
		if (m_bCoherent)
		{
			ASH_LOG_PROCESS_ERROR(bHostCoherent == m_bCoherent);
			bool bRetCode = VulkanContext::get()->vma_map_memory(m_pVMAAllocation, (void**)&m_pMappedData);
			ASH_LOG_PROCESS_ERROR(bRetCode);
		}
		//fill descriptor info structure
		{
			ASH_LOG_PROCESS_ERROR(m_pVkBuffer);
			m_sDescriptorBufferInfo.buffer = m_pVkBuffer;
			m_sDescriptorBufferInfo.offset = 0;
			m_sDescriptorBufferInfo.range = m_sCreationInfo.size;
		}
		m_bReady = true;
		m_resourceTracker = VulkanResourceTracker(AshResourceState::Unknown);
		if (m_sCreationInfo.initial_data)
		{
			ASH_LOG_PROCESS_ERROR(update(0, m_sCreationInfo.size, m_sCreationInfo.initial_data));
		}
		ASH_SAFE_EXECUTE_END(bResult);
		if (!bResult)
		{
			//process error
			destroy();
		}
		return bResult;
	}

	auto VulkanBuffer::destroy() -> void
	{
		defaultCBV.reset();
		defaultSRV.reset();
		defaultUAV.reset();
		if (immediate_deletion)
		{
			VulkanContext::get()->vma_destroy_buffer(m_pVkBuffer, m_pVMAAllocation);
			HLogInfo("deleting buffer : {} ...", m_pName);
		}
		else
		{
			if (m_pVkBuffer != VK_NULL_HANDLE)
			{
				auto handle = m_pVkBuffer;
				auto alloc = m_pVMAAllocation;
				auto sname = m_pName;
				VulkanContext::get_current_frame_deletion_queue().emplace([handle, alloc, sname]() {
					HLogInfo("deleting buffer : {} ...", sname);
					VulkanContext::get()->vma_destroy_buffer_v(handle, alloc);
					});
				m_pVkBuffer = VK_NULL_HANDLE;
				m_pVMAAllocation = nullptr;
			}
		}
		m_bReady = false;
	}
	auto VulkanBuffer::get_size() -> uint32_t
	{
		return m_sCreationInfo.size;
	}
	auto VulkanBuffer::get_name() -> const char*
	{
		return m_pName;
	}
	auto VulkanBuffer::get_global_offset() -> uint32_t
	{
		return 0;
	}
	auto VulkanBuffer::is_ready() -> bool
	{
		return m_bReady;
	}
	auto VulkanBuffer::get_mapped_data() -> uint8_t*
	{
		return m_pMappedData;
	}
	auto VulkanBuffer::is_dynamic() -> bool
	{
		return dynamic;
	}
	auto VulkanBuffer::get_default_cbv() -> std::shared_ptr<BufferView>
	{
		if (!defaultCBV)
		{
			BufferViewCreation ci{};
			ci.view_type = AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_CBV;
			defaultCBV = Ash_New_Shared<VulkanBufferView>(ci, shared_from_this());
		}
		return defaultCBV;
	}
	auto VulkanBuffer::get_default_srv() -> std::shared_ptr<BufferView>
	{
		if (!defaultSRV)
		{
			BufferViewCreation ci{};
			ci.view_type = AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_SRV;
			if (m_sCreationInfo.struct_byte_stride > 0)
			{
				ci.uStructureStride = m_sCreationInfo.struct_byte_stride;
			}
			defaultSRV = Ash_New_Shared<VulkanBufferView>(ci, shared_from_this());
		}
		return defaultSRV;
	}
	auto VulkanBuffer::get_default_uav() -> std::shared_ptr<BufferView>
	{
		if (!defaultUAV)
		{
			BufferViewCreation ci{};
			ci.view_type = AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_UAV;
			if (m_sCreationInfo.struct_byte_stride > 0)
			{
				ci.uStructureStride = m_sCreationInfo.struct_byte_stride;
			}
			defaultUAV = Ash_New_Shared<VulkanBufferView>(ci, shared_from_this());
		}
		return defaultUAV;
	}
	auto VulkanBuffer::update(uint32_t offset, uint32_t _size, void* pData) -> bool
	{
		bool bRetCode = false;
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		ASH_LOG_PROCESS_ERROR(pData);
		ASH_LOG_PROCESS_ERROR(offset + _size <= m_sCreationInfo.size);
		if (m_sCreationInfo.access_type == AshResourceAccessType::ASH_RESOURCE_ACCESS_GPU_ONLY)
		{
			CommandBuffer* cmdBuffer = VulkanContext::get()->get_command_buffer(0);
			ASH_LOG_PROCESS_ERROR(cmdBuffer);
			cmdBuffer->begin_record();
			bRetCode = cmdBuffer->cmd_update_sub_resource(shared_from_this(), offset, _size, pData);
			ASH_LOG_PROCESS_ERROR(bRetCode);
			cmdBuffer->end_record();
			VulkanContext::get()->submit_immediately({ cmdBuffer, 1 });
		}
		else
		{
			void* dst = nullptr;
			bool mappedByThisCall = false;
			if (m_pMappedData)
			{
				dst = m_pMappedData;
			}
			else
			{
				bRetCode = VulkanContext::get()->vma_map_memory(m_pVMAAllocation, &dst);
				ASH_LOG_PROCESS_ERROR(bRetCode && dst);
				mappedByThisCall = true;
			}

			memory_copy(static_cast<uint8_t*>(dst) + offset, pData, _size);
			ASH_LOG_PROCESS_ERROR(flush_mapped_range());
			if (mappedByThisCall)
			{
				VulkanContext::get()->vma_unmap_memory(m_pVMAAllocation);
			}
			bRetCode = true;
		}
		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}
	auto VulkanBuffer::flush_mapped_range() -> bool
	{
		if (!m_bCoherent)
		{
			return VulkanContext::get()->vma_flush_allocation(m_pVMAAllocation);
		}
		return true;
	}
	auto VulkanBuffer::get_buffer_device_address() -> uint64_t
	{
		if (!(m_sCreationInfo.usage_flags & ASH_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT))
		{
			return 0;
		}
		VkBufferDeviceAddressInfo addressInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		addressInfo.buffer = m_pVkBuffer;
		return vkGetBufferDeviceAddress(VulkanContext::get_vulkan_device(), &addressInfo);
	}
	auto VulkanBuffer::get_buffer_creation_info() const -> const BufferCreation&
	{
		return m_sCreationInfo;
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
			//buffer->globalOffset = dynamic_allocated_size;
			auto memeory = dynamic_allocate(buffer->get_size(), alignment);
			//we consider that all dynamic buffers are persistent
			buffer->m_pMappedData = (uint8_t*)memeory;	
		}
		return buffer->m_pMappedData;
	}

	auto VulkanDynamicBuffer::update(uint32_t offset, uint32_t size, void* pData) -> bool
	{
		return false;
	}
	VulkanBufferView::VulkanBufferView(const BufferViewCreation& ci, std::shared_ptr<Buffer> parent)
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		ASH_PROCESS_ERROR_EXIT(parent);
		VkBufferViewCreateInfo       createInfo{ VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO };
		auto& bufferDesc = parent->get_buffer_creation_info();
		uint32_t                     uBytesRange = 0;
		ASH_PROCESS_ERROR_EXIT(bufferDesc.size > ci.uByteOffset);
	
		uBytesRange = ci.uByteRange == 0 ? bufferDesc.size - ci.uByteOffset : ci.uByteRange;
		ASH_PROCESS_ERROR_EXIT(bufferDesc.size > ci.uByteOffset);

		ASH_PROCESS_ERROR_EXIT(bufferDesc.size >= ci.uByteOffset + uBytesRange);

		auto pvkBuffer = (VkBuffer)parent->get_native_handle();
		ASH_PROCESS_ERROR_EXIT(pvkBuffer);

		if (ci.view_type == AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_UAV)
		{
			ASH_PROCESS_ERROR_EXIT((bufferDesc.access_type == AshResourceAccessType::ASH_RESOURCE_ACCESS_GPU_ONLY));
		}

		if (ci.format != ASH_FORMAT_UNDEFINED)
		{
			// Image Buffer
			const AshTextureFormatInfo* textureFormatInfo = &get_vk_texture_format_info(ci.format);

			createInfo.buffer = pvkBuffer;
			createInfo.format = textureFormatInfo->vkFormat;
			createInfo.offset = ci.uByteOffset;
			createInfo.range = uBytesRange;

			/*
			offset and range must comply with VkPhysicalDeviceLimits::minTexelBufferOffsetAlignment
			*/

			{
				bool              bColorAttach = false;
				bool              bDepth = false;
				bool              bStencil = false;
				uint32_t          uBytesStride = 4;
				AshFormat eTextureFormat = vk_format_to_ash(textureFormatInfo->vkFormat);
				get_texture_format_from_target_format(eTextureFormat, bColorAttach, bDepth, bStencil, uBytesStride);
				auto limitElements = VulkanContext::get_device_properties().limits.maxTexelBufferElements;
				if ((createInfo.range / std::max<uint32_t>(uBytesStride, 1)) > limitElements)
				{
					ASH_PROCESS_ERROR_EXIT(false);
				}
			}

			auto vkResult = vkCreateBufferView(VulkanContext::get_vulkan_device(), &createInfo, nullptr, &vkBufferView);
			ASH_PROCESS_ERROR_EXIT(vkResult == VK_SUCCESS);

			m_createInfo = createInfo;
		}
		parentBuffer = parent;

		m_ViewCreation = ci;
		m_ViewCreation.uByteRange = uBytesRange;

		ASH_SAFE_EXECUTE_END(bResult);
	}
	VulkanBufferView::~VulkanBufferView()
	{
		if (immediate_deletion)
		{
			if (vkBufferView != VK_NULL_HANDLE)
			{
				vkDestroyBufferView(VulkanContext::get_vulkan_device(), vkBufferView, VulkanContext::get_vulkan_allocation_callbacks());
			}
		}
		else
		{
			auto handle = this->vkBufferView;
			if (handle != VK_NULL_HANDLE)
			{

				VulkanContext::get_current_frame_deletion_queue().emplace([handle]() {
					vkDestroyBufferView(VulkanContext::get_vulkan_device(), handle, VulkanContext::get_vulkan_allocation_callbacks()); });
			}
		}
		parentBuffer.reset();
	}
	auto VulkanBufferView::get_native_handle() -> void*
	{
		return vkBufferView;
	}
	auto VulkanBufferView::get_name() -> const char*
	{
		return nullptr;
	}
	auto VulkanBufferView::get_parent_buffer() -> std::shared_ptr<Buffer>
	{
		return parentBuffer.lock();
	}
	auto VulkanBufferView::get_view_type() -> AshResourceViewType
	{
		return m_ViewCreation.view_type;
	}
	auto VulkanBufferView::get_view_format() -> AshFormat
	{
		return m_ViewCreation.format;
	}
	auto VulkanBufferView::get_view_desc() -> const BufferViewCreation&
	{
		return m_ViewCreation;
	}
}
