#include "VulkanDescriptorSet.h"
#include "VulkanTexture.h"
#include "VulkanBuffer.h"
#include "VulkanSampler.h"
#include "VulkanContext.h"
#include "Graphics/Buffer.h"
#include "Graphics/Texture.h"
#include "base/hlog.h"
namespace RHI
{
	static VkDescriptorType get_descriptor_type(AshDescriptorType desc)
	{
		VkDescriptorType ret = VK_DESCRIPTOR_TYPE_MAX_ENUM;
		switch (desc)
		{
		case ASH_DESCRIPTOR_TYPE_SAMPLER:
			ret = VK_DESCRIPTOR_TYPE_SAMPLER;
			break;
		case ASH_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
			ret = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			break;
		case ASH_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
			ret = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			break;
		case ASH_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			ret = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			break;
		case ASH_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
			ret = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
			break;
		case ASH_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
			ret = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
			break;
		case ASH_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
			ret = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			break;
		case ASH_DESCRIPTOR_TYPE_STORAGE_BUFFER:
			ret = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			break;
		case ASH_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
			ret = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
			break;
		case ASH_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
			ret = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
			break;
		case ASH_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
			ret = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
			break;
		case ASH_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE:
			ret = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
			break;
		default:
			break;
		}
		return ret;
	}
	std::shared_ptr<VulkanDescriptorPool> VulkanDescriptorPoolContainer::get_descriptor_pool() const
	{
		return m_pDescriptorPool;
	}
	void VulkanDescriptorPoolContainer::add_allocated(std::shared_ptr<VulkanDescriptorSet> p)
	{
		m_setAlloced.insert(p);
	}
	void VulkanDescriptorPoolContainer::remove(std::shared_ptr<VulkanDescriptorSet> pSet)
	{
		auto pPool = get_descriptor_pool();
		if (m_pDescriptorPool)
		{
			auto p = pPool;
			while (p)
			{
				if (p == m_pDescriptorPool)
				{
					m_pDescriptorPool->free_descriptor_set(pSet);
					break;
				}
				p = p->get_next();
			}
		}
		auto it = m_setAlloced.find(pSet);
		if (it != m_setAlloced.end())
		{
			m_setAlloced.erase(it);
			pSet->clear_pool_container();
		}
	}
	void VulkanDescriptorPoolContainer::clear()
	{
		for (auto& _set : m_setAlloced)
		{
			if (m_pDescriptorPool)
			{
				m_pDescriptorPool->free_descriptor_set(_set);
			}
			_set->clear_pool_container();
		}
		m_setAlloced.clear();

		if (m_pDescriptorPool)
		{
			m_pDescriptorPool.reset();
		}
	}
	VulkanDescriptorPoolContainer::VulkanDescriptorPoolContainer()
	{
		m_pDescriptorPool = nullptr;
	}
	VulkanDescriptorPoolContainer::~VulkanDescriptorPoolContainer()
	{
		clear();
	}
	VulkanDescriptorPool::VulkanDescriptorPool()
	{
	}
	VulkanDescriptorPool::~VulkanDescriptorPool()
	{
		VkDevice pDevice = VulkanContext::get_vulkan_device();
		if (m_uAllocedSet > 0)
		{
			HLogWarning( "some descriptorSet is not released !!");
		}
		if (immediate_deletion)
		{
			if (m_pDescriptorPool)
			{
				vkDestroyDescriptorPool(pDevice, m_pDescriptorPool, nullptr);
			}
			if (m_pNextPool)
			{
				m_pNextPool->immediate_deletion = true;
				m_pNextPool.reset();
			}
		}
		else
		{
			auto handle = m_pDescriptorPool;
			if (handle != VK_NULL_HANDLE)
			{
				VulkanContext::get_current_frame_deletion_queue().emplace([handle]() {
					vkDestroyDescriptorPool(VulkanContext::get_vulkan_device(), handle, VulkanContext::get_vulkan_allocation_callbacks()); });
			}
			m_pNextPool.reset();
		}
	}
	VulkanDescriptorPool& VulkanDescriptorPool::begin(uint32_t maxSet, bool bBindlessSet)
	{
		if (m_pDescriptorPool)
		{
			vkDestroyDescriptorPool(VulkanContext::get_vulkan_device(), m_pDescriptorPool, nullptr);
		}
		m_vecDescriptorPoolSize.clear();
		m_uMaxSet = maxSet;
		m_uAllocedSet = 0;
		m_bBindlessPool = false;
	}
	bool VulkanDescriptorPool::end()
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		VkResult hRetCode = VK_INCOMPLETE;
		VkDevice pDevice = VulkanContext::get_vulkan_device();

		if (m_vecDescriptorPoolSize.empty())
		{
			HLogWarning( "[VulkanDescriptorPool::End] descriptor pool is empty");
		}
		else
		{
			VkDescriptorPoolCreateInfo descriptorPoolInfo{};
			descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			descriptorPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
			descriptorPoolInfo.poolSizeCount = (uint32_t)m_vecDescriptorPoolSize.size();
			descriptorPoolInfo.pPoolSizes = m_vecDescriptorPoolSize.data();
			descriptorPoolInfo.maxSets = m_uMaxSet;
			
			if (m_bBindlessPool)
			{
				descriptorPoolInfo.flags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
			}
			hRetCode = vkCreateDescriptorPool(pDevice, &descriptorPoolInfo, nullptr, &m_pDescriptorPool);
			ASH_LOG_PROCESS_ERROR(hRetCode == VK_SUCCESS);
		}
		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}
	bool VulkanDescriptorPool::free_descriptor_set(std::shared_ptr<VulkanDescriptorSet> pDescriptorSet)
	{
		VkDevice pDevice = VulkanContext::get_vulkan_device();
		std::shared_ptr<VulkanDescriptorPool> pPool = pDescriptorSet->m_pRealAllocPool;
		if (pPool && pPool->m_uAllocedSet > 0)
		{
			pPool->m_uAllocedSet--;
			const VkDescriptorSet ps = pDescriptorSet->m_pVkDescriptorSet;
			if (ps)
			{
				const VkDescriptorSet* pSet = &ps;
				vkFreeDescriptorSets(pDevice, pPool->m_pDescriptorPool, 1, pSet);
			}
			pDescriptorSet->m_pVkDescriptorSet = VK_NULL_HANDLE;
			pDescriptorSet->clear_cache();
			pDescriptorSet->clear_pool_container();
			if (pPool->m_uAllocedSet == 0)
			{
				if (pPool->m_pPreviousPool)
				{
					// 不是头结点就可以干掉了,自动删除子结点回收空间
					auto pPrev = pPool->m_pPreviousPool;
					auto pCur = pPool;
					auto pNext = pPool->m_pNextPool;

					pPrev->m_pNextPool = pNext;
					if (pNext)
					{
						pNext->m_pPreviousPool = pPrev;
					}
					pCur->m_pNextPool = nullptr;
					pCur->m_pPreviousPool = nullptr;
					pCur.reset();
				}
			}
		}
		return true;
	}
	VkDescriptorPool VulkanDescriptorPool::get_vk_pool() const
	{
		return m_pDescriptorPool;
	}
	void VulkanDescriptorPool::increate_allocated_set()
	{
		++m_uAllocedSet;
	}
	bool VulkanDescriptorPool::is_full() const
	{
		if (m_uAllocedSet < m_uMaxSet)
		{
			return false;
		}
		else
		{
			if (m_uAllocedSet > m_uMaxSet)
				H_ASSERT(false);
			{
			}

			return true;
		}
	}
	std::shared_ptr<VulkanDescriptorPool> VulkanDescriptorPool::create_from_header()
	{
		VkDevice               pDevice = VulkanContext::get_vulkan_device();
		std::shared_ptr<VulkanDescriptorPool> pNewPool = Ash_New_Shared<VulkanDescriptorPool>();

		pNewPool->m_vecDescriptorPoolSize.assign(m_vecDescriptorPoolSize.begin(), m_vecDescriptorPoolSize.end());
		pNewPool->m_uMaxSet = m_uMaxSet;
		pNewPool->m_uAllocedSet = 0;

		VkDescriptorPoolCreateInfo descriptorPoolInfo{}; 
		descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		descriptorPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		descriptorPoolInfo.poolSizeCount = (uint32_t)pNewPool->m_vecDescriptorPoolSize.size();
		descriptorPoolInfo.pPoolSizes = pNewPool->m_vecDescriptorPoolSize.data();
		descriptorPoolInfo.maxSets = pNewPool->m_uMaxSet; 

		vkCreateDescriptorPool(pDevice, &descriptorPoolInfo, nullptr, &pNewPool->m_pDescriptorPool);
		return pNewPool;
	}
	bool VulkanDescriptorPool::is_dirty_descriptor_pool(std::shared_ptr<VulkanDescriptorPool> pPool)
	{
		return false;
	}
	std::shared_ptr<VulkanDescriptorPool> VulkanDescriptorPool::get_next()
	{
		return std::shared_ptr<VulkanDescriptorPool>();
	}
	auto VulkanDescriptorPool::get_native_handle() -> void*
	{
		return m_pDescriptorPool;
	}
	auto VulkanDescriptorPool::get_name() -> const char*
	{
		return nullptr;
	}
	VulkanDescriptorPool& VulkanDescriptorPool::add_pool_item(AshDescriptorType descriptorType, uint32_t uCount)
	{
		VkDescriptorPoolSize descriptorPoolSize{};
		descriptorPoolSize.type = get_descriptor_type(descriptorType);
		descriptorPoolSize.descriptorCount = uCount * m_uMaxSet;
		m_vecDescriptorPoolSize.push_back(descriptorPoolSize);
	}
	VulkanDescriptorSet::VulkanDescriptorSet(VkDescriptorSetLayout layout, std::shared_ptr<VulkanDescriptorPoolContainer> poolContainer)
	{
		H_ASSERT(layout);
		H_ASSERT(poolContainer);
		m_pVkLayout = layout;
		m_pPoolContainer = poolContainer;
		poolContainer->add_allocated(shared_from_this());
		m_pRealAllocPool = nullptr;
		std::shared_ptr<VulkanDescriptorPool> pVulkanPoolHeader = m_pPoolContainer->get_descriptor_pool();
		if (pVulkanPoolHeader)
		{
			std::shared_ptr<VulkanDescriptorPool> pNode = pVulkanPoolHeader;
			std::shared_ptr<VulkanDescriptorPool> pPrevNode = nullptr;
			while (pNode)
			{
				if (!pNode->is_full())
				{
					break;
				}
				pPrevNode = pNode;
				pNode = pNode->m_pNextPool;
			}
			if (!pNode)
			{
				pNode = pVulkanPoolHeader->create_from_header();
				if (pPrevNode)
				{
					pPrevNode->m_pNextPool = pNode;
				}
				pNode->m_pPreviousPool = pPrevNode;
			}
			pNode->increate_allocated_set();
			m_pRealAllocPool = pNode;
		}
	}
	VulkanDescriptorSet::~VulkanDescriptorSet()
	{
		if (m_pPoolContainer)
		{
			m_pPoolContainer->remove(shared_from_this());
		}
		m_pPoolContainer = nullptr;
	}
	VulkanDescriptorSet& VulkanDescriptorSet::begin_bind()
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		ASH_LOG_PROCESS_ERROR(m_pRealAllocPool);
		ASH_LOG_PROCESS_ERROR(m_pVkLayout);
		if (!m_pVkDescriptorSet)
		{
			VkDescriptorSetAllocateInfo descriptorSetAllocateInfo{};
			descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			descriptorSetAllocateInfo.descriptorPool = m_pRealAllocPool->get_vk_pool();
			descriptorSetAllocateInfo.pSetLayouts = &m_pVkLayout;
			descriptorSetAllocateInfo.descriptorSetCount = 1;
			VkResult vkResult = vkAllocateDescriptorSets(VulkanContext::get_vulkan_device(), &descriptorSetAllocateInfo,&m_pVkDescriptorSet);
			ASH_PROCESS_ERROR_EXIT(vkResult == VK_SUCCESS);
		}
		clear_cache();
		ASH_SAFE_EXECUTE_END(bResult);
		return *this;
	}
	VulkanDescriptorSet& VulkanDescriptorSet::add_bind_srv(uint32_t uBinding, std::shared_ptr<BufferView> srv)
	{
		//upper logic must avoid nullptr
		H_ASSERT(srv);
		auto& desc = srv->get_view_desc();
		m_vecCachedBufferInfo.emplace_back((VkBuffer)srv->get_native_handle(), desc.uByteOffset, desc.uByteRange);
		//transition
		{
			auto pVulkanBuffer = std::static_pointer_cast<VulkanBuffer>(srv->get_parent_buffer());
			H_ASSERT(pVulkanBuffer);
			m_vecBarrierCollection.emplace_back(srv->get_parent_buffer(), AshResourceState::SRVMask);
		}
		//record write info
		VkWriteDescriptorSet descriptorWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		descriptorWrite.dstSet = m_pVkDescriptorSet;
		descriptorWrite.dstBinding = uBinding;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptorWrite.pBufferInfo = &m_vecCachedBufferInfo.back();
		m_vecWriteDescriptorSets.push_back(descriptorWrite);
		return *this;
	}
	VulkanDescriptorSet& VulkanDescriptorSet::add_bind_srv(uint32_t uBinding, std::shared_ptr<TextureView> srv)
	{
		//upper logic must avoid nullptr
		H_ASSERT(srv);
		m_vecCachedImageAndSamplerInfo.emplace_back(nullptr, (VkImageView)srv->get_native_handle(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		//transition
		{
			auto pVulkanTexture = std::static_pointer_cast<VulkanTexture>(srv->get_parent_texture());
			H_ASSERT(pVulkanTexture);
			auto subRange = pVulkanTexture->resolve_subresource_range(srv->get_subresource_range());
			m_vecBarrierCollection.emplace_back(srv->get_parent_texture(), AshResourceState::SRVMask, subRange);
		}
		//record write info
		VkWriteDescriptorSet descriptorWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		descriptorWrite.dstSet = m_pVkDescriptorSet;
		descriptorWrite.dstBinding = uBinding;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		descriptorWrite.pImageInfo = &m_vecCachedImageAndSamplerInfo.back();
		m_vecWriteDescriptorSets.push_back(descriptorWrite);
		return *this;
	}
	VulkanDescriptorSet& VulkanDescriptorSet::add_bind_uav(uint32_t uBinding, std::shared_ptr<BufferView> uav)
	{
		//upper logic must avoid nullptr
		H_ASSERT(uav);
		auto& desc = uav->get_view_desc();
		m_vecCachedBufferInfo.emplace_back((VkBuffer)uav->get_native_handle(), desc.uByteOffset, desc.uByteRange);
		//transition
		{
			auto pVulkanBuffer = std::static_pointer_cast<VulkanBuffer>(uav->get_parent_buffer());
			H_ASSERT(pVulkanBuffer);
			m_vecBarrierCollection.emplace_back(uav->get_parent_buffer(), AshResourceState::UAVMask);
		}
		//record write info
		VkWriteDescriptorSet descriptorWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		descriptorWrite.dstSet = m_pVkDescriptorSet;
		descriptorWrite.dstBinding = uBinding;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptorWrite.pBufferInfo = &m_vecCachedBufferInfo.back();
		m_vecWriteDescriptorSets.push_back(descriptorWrite);
		return *this;
	}
	VulkanDescriptorSet& VulkanDescriptorSet::add_bind_uav(uint32_t uBinding, std::shared_ptr<TextureView> uav)
	{
		//upper logic must avoid nullptr
		H_ASSERT(uav);
		m_vecCachedImageAndSamplerInfo.emplace_back(nullptr, (VkImageView)uav->get_native_handle(), VK_IMAGE_LAYOUT_GENERAL);
		//transition
		{
			auto pVulkanTexture = std::static_pointer_cast<VulkanTexture>(uav->get_parent_texture());
			H_ASSERT(pVulkanTexture);
			auto subRange = pVulkanTexture->resolve_subresource_range(uav->get_subresource_range());
			m_vecBarrierCollection.emplace_back(uav->get_parent_texture(), AshResourceState::UAVMask, subRange);
		}
		//record write info
		VkWriteDescriptorSet descriptorWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		descriptorWrite.dstSet = m_pVkDescriptorSet;
		descriptorWrite.dstBinding = uBinding;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		descriptorWrite.pImageInfo = &m_vecCachedImageAndSamplerInfo.back();
		m_vecWriteDescriptorSets.push_back(descriptorWrite);
		return *this;
	}
	VulkanDescriptorSet& VulkanDescriptorSet::add_bind_cbv(uint32_t uBinding, std::shared_ptr<BufferView> cbv)
	{
		//upper logic must avoid nullptr
		H_ASSERT(cbv);
		auto& desc = cbv->get_view_desc();
		m_vecCachedBufferInfo.emplace_back((VkBuffer)cbv->get_native_handle(), desc.uByteOffset, desc.uByteRange);
		//transition
		{
			auto pVulkanBuffer = std::static_pointer_cast<VulkanBuffer>(cbv->get_parent_buffer());
			H_ASSERT(pVulkanBuffer);
			m_vecBarrierCollection.emplace_back(cbv->get_parent_buffer(), AshResourceState::ConstBuffer);
		}
		//record write info
		VkWriteDescriptorSet descriptorWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		descriptorWrite.dstSet = m_pVkDescriptorSet;
		descriptorWrite.dstBinding = uBinding;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorWrite.pBufferInfo = &m_vecCachedBufferInfo.back();
		m_vecWriteDescriptorSets.push_back(descriptorWrite);
		return *this;
	}
	VulkanDescriptorSet& VulkanDescriptorSet::add_bind_sampler(uint32_t uBinding, std::shared_ptr<SamplerView> smaplerView)
	{
		//upper logic must avoid nullptr
		H_ASSERT(smaplerView);
		m_vecCachedImageAndSamplerInfo.emplace_back((VkSampler)smaplerView->get_native_handle(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		//record write info
		VkWriteDescriptorSet descriptorWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		descriptorWrite.dstSet = m_pVkDescriptorSet;
		descriptorWrite.dstBinding = uBinding;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		descriptorWrite.pImageInfo = &m_vecCachedImageAndSamplerInfo.back();
		m_vecWriteDescriptorSets.push_back(descriptorWrite);
		return *this;
	}
	VulkanDescriptorSet& VulkanDescriptorSet::add_bind_acceleration_structure(uint32_t uBinding, std::shared_ptr<AccelerationStructureView> acclerationStructureView)
	{
		H_ASSERTLOG(false, "Not Implement!");
		return *this;
	}
	bool VulkanDescriptorSet::end_bind()
	{
		vkUpdateDescriptorSets(VulkanContext::get_vulkan_device(), (uint32_t)m_vecWriteDescriptorSets.size(), m_vecWriteDescriptorSets.data(), 0, nullptr);
		return true;
	}
	void VulkanDescriptorSet::clear_cache()
	{
		m_vecCachedASInfo.clear();
		m_vecCachedBufferInfo.clear();
		m_vecCachedImageAndSamplerInfo.clear();
		m_vecWriteDescriptorSets.clear();
	}
	void VulkanDescriptorSet::clear_pool_container()
	{
		m_pPoolContainer = nullptr;
		m_pRealAllocPool = nullptr;
	}
}