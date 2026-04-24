#include "VulkanDescriptorSet.h"
#include "VulkanTexture.h"
#include "VulkanBuffer.h"
#include "VulkanSampler.h"
#include "VulkanContext.h"
#include "Graphics/Buffer.h"
#include "Graphics/Texture.h"
#include "base/hlog.h"
#include <unordered_map>
namespace RHI
{
	static std::unordered_map<size_t, std::weak_ptr<VulkanDescriptorSetLayout>> g_descriptor_set_layout_cache;

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

	static VkDescriptorType get_buffer_descriptor_type(AshDescriptorType desc, VkDescriptorType fallback)
	{
		const VkDescriptorType descriptor_type = get_descriptor_type(desc);
		return descriptor_type == VK_DESCRIPTOR_TYPE_MAX_ENUM ? fallback : descriptor_type;
	}

	static VkShaderStageFlags get_vk_shader_stage_flags(AshShaderStageFlagBits stage_flags)
	{
		VkShaderStageFlags flags = 0;
		if (stage_flags & ASH_SHADER_STAGE_VERTEX_BIT) flags |= VK_SHADER_STAGE_VERTEX_BIT;
		if (stage_flags & ASH_SHADER_STAGE_TESSELLATION_CONTROL_BIT) flags |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
		if (stage_flags & ASH_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) flags |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
		if (stage_flags & ASH_SHADER_STAGE_GEOMETRY_BIT) flags |= VK_SHADER_STAGE_GEOMETRY_BIT;
		if (stage_flags & ASH_SHADER_STAGE_FRAGMENT_BIT) flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
		if (stage_flags & ASH_SHADER_STAGE_COMPUTE_BIT) flags |= VK_SHADER_STAGE_COMPUTE_BIT;
		if (stage_flags & ASH_SHADER_STAGE_RAYGEN_BIT_KHR) flags |= VK_SHADER_STAGE_RAYGEN_BIT_KHR;
		if (stage_flags & ASH_SHADER_STAGE_ANY_HIT_BIT_KHR) flags |= VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
		if (stage_flags & ASH_SHADER_STAGE_CLOSEST_HIT_BIT_KHR) flags |= VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
		if (stage_flags & ASH_SHADER_STAGE_MISS_BIT_KHR) flags |= VK_SHADER_STAGE_MISS_BIT_KHR;
		if (stage_flags & ASH_SHADER_STAGE_INTERSECTION_BIT_KHR) flags |= VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
		if (stage_flags & ASH_SHADER_STAGE_CALLABLE_BIT_KHR) flags |= VK_SHADER_STAGE_CALLABLE_BIT_KHR;
		if (stage_flags & ASH_SHADER_STAGE_TASK_BIT_EXT) flags |= VK_SHADER_STAGE_TASK_BIT_EXT;
		if (stage_flags & ASH_SHADER_STAGE_MESH_BIT_EXT) flags |= VK_SHADER_STAGE_MESH_BIT_EXT;
		return flags;
	}

	static size_t hash_descriptor_set_layout_creation(const DescriptorSetLayoutCreation& creation)
	{
		size_t hash_code = 0;
		ASH_HASH::hash_combine(hash_code, creation.set_index);
		ASH_HASH::hash_combine(hash_code, creation.num_bindings);
		ASH_HASH::hash_combine(hash_code, creation.bindless);
		ASH_HASH::hash_combine(hash_code, creation.dynamic);
		for (uint32_t i = 0; i < creation.num_bindings; ++i)
		{
			const auto& binding = creation.bindings[i];
			ASH_HASH::hash_combine(hash_code, binding.type);
			ASH_HASH::hash_combine(hash_code, binding.index);
			ASH_HASH::hash_combine(hash_code, binding.count);
			ASH_HASH::hash_combine(hash_code, binding.stage_flags);
			ASH_HASH::hash_combine(hash_code, binding.bindless);
			ASH_HASH::hash_combine(hash_code, binding.name, ASH_HASH::CStringHash{});
		}
		return hash_code;
	}

	std::shared_ptr<VulkanDescriptorSetLayout> VulkanDescriptorSetLayout::create(const DescriptorSetLayoutCreation& creation)
	{
		const size_t hash_code = hash_descriptor_set_layout_creation(creation);
		auto it = g_descriptor_set_layout_cache.find(hash_code);
		if (it != g_descriptor_set_layout_cache.end())
		{
			if (auto cached = it->second.lock())
			{
				return cached;
			}
		}

		auto layout = Ash_New_Shared<VulkanDescriptorSetLayout>();
		if (!layout->init(creation))
		{
			return nullptr;
		}
		g_descriptor_set_layout_cache[hash_code] = layout;
		return layout;
	}

	bool VulkanDescriptorSetLayout::init(const DescriptorSetLayoutCreation& creation)
	{
		m_creation = creation;
		m_poolContainer = Ash_New_Shared<VulkanDescriptorPoolContainer>();
		auto descriptor_pool = Ash_New_Shared<VulkanDescriptorPool>();
		descriptor_pool->begin(16, creation.bindless);

		std::vector<VkDescriptorSetLayoutBinding> bindings;
		bindings.reserve(creation.num_bindings);
		std::vector<VkDescriptorBindingFlags> binding_flags;
		binding_flags.reserve(creation.num_bindings);

		for (uint32_t i = 0; i < creation.num_bindings; ++i)
		{
			const auto& src = creation.bindings[i];
			VkDescriptorSetLayoutBinding binding{};
			binding.binding = src.index;
			binding.descriptorType = get_descriptor_type(src.type);
			binding.descriptorCount = std::max<uint32_t>(src.count, 1u);
			binding.stageFlags = get_vk_shader_stage_flags(src.stage_flags);
			bindings.push_back(binding);
			descriptor_pool->add_pool_item(src.type, std::max<uint32_t>(src.count, 1u));

			VkDescriptorBindingFlags flags = 0;
			if (src.bindless)
			{
				flags |= VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
				flags |= VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
				flags |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
			}
			binding_flags.push_back(flags);
		}

		VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
		binding_flags_info.bindingCount = static_cast<uint32_t>(binding_flags.size());
		binding_flags_info.pBindingFlags = binding_flags.empty() ? nullptr : binding_flags.data();

		VkDescriptorSetLayoutCreateInfo create_info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		create_info.bindingCount = static_cast<uint32_t>(bindings.size());
		create_info.pBindings = bindings.empty() ? nullptr : bindings.data();
		create_info.pNext = binding_flags.empty() ? nullptr : &binding_flags_info;
		if (creation.bindless)
		{
			create_info.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
		}

		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(VulkanContext::get_vulkan_device(), &create_info, VulkanContext::get_vulkan_allocation_callbacks(), &m_vkDescriptorSetLayout));
		if (!descriptor_pool->end())
		{
			return false;
		}
		m_poolContainer->m_pDescriptorPool = descriptor_pool;
		return true;
	}

	VulkanDescriptorSetLayout::~VulkanDescriptorSetLayout()
	{
		m_poolContainer.reset();
		if (m_vkDescriptorSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(VulkanContext::get_vulkan_device(), m_vkDescriptorSetLayout, VulkanContext::get_vulkan_allocation_callbacks());
			m_vkDescriptorSetLayout = VK_NULL_HANDLE;
		}
	}

	const DescriptorSetLayoutCreation& VulkanDescriptorSetLayout::get_creation() const
	{
		return m_creation;
	}

	uint32_t VulkanDescriptorSetLayout::get_set_index() const
	{
		return m_creation.set_index;
	}

	std::shared_ptr<VulkanDescriptorPoolContainer> VulkanDescriptorSetLayout::get_pool_container() const
	{
		return m_poolContainer;
	}

	auto VulkanDescriptorSetLayout::get_native_handle() -> void*
	{
		return m_vkDescriptorSetLayout;
	}

	auto VulkanDescriptorSetLayout::get_name() -> const char*
	{
		return m_creation.name;
	}

	std::shared_ptr<VulkanDescriptorPool> VulkanDescriptorPoolContainer::get_descriptor_pool() const
	{
		return m_pDescriptorPool;
	}
	void VulkanDescriptorPoolContainer::add_allocated(VulkanDescriptorSet* p)
	{
		m_setAlloced.insert(p);
	}
	void VulkanDescriptorPoolContainer::remove(VulkanDescriptorSet* pSet)
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
				m_pDescriptorPool = VK_NULL_HANDLE;
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
				m_pDescriptorPool = VK_NULL_HANDLE;
			}
			m_pNextPool.reset();
		}
	}
	VulkanDescriptorPool& VulkanDescriptorPool::begin(uint32_t maxSet, bool bBindlessSet)
	{
		if (m_pDescriptorPool)
		{
			vkDestroyDescriptorPool(VulkanContext::get_vulkan_device(), m_pDescriptorPool, nullptr);
			m_pDescriptorPool = VK_NULL_HANDLE;
		}
		m_vecDescriptorPoolSize.clear();
		m_uMaxSet = maxSet;
		m_uAllocedSet = 0;
		m_bBindlessPool = bBindlessSet;
		return *this;
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
	bool VulkanDescriptorPool::free_descriptor_set(VulkanDescriptorSet* pDescriptorSet)
	{
		VkDevice pDevice = VulkanContext::get_vulkan_device();
		std::shared_ptr<VulkanDescriptorPool> pPool = pDescriptorSet ? pDescriptorSet->m_pRealAllocPool : nullptr;
		if (pPool && pPool->m_uAllocedSet > 0)
		{
			pPool->m_uAllocedSet--;
			const VkDescriptorSet ps = pDescriptorSet->m_pVkDescriptorSet;
			if (ps)
			{
				const uint32_t current_frame = VulkanContext::get_current_frame();
				if (pDevice != VK_NULL_HANDLE && current_frame != UINT32_MAX)
				{
					const VkDescriptorPool descriptor_pool = pPool->m_pDescriptorPool;
					VulkanContext::get_current_frame_deletion_queue().emplace([pPool, descriptor_pool, ps]() {
						const VkDescriptorSet set = ps;
						vkFreeDescriptorSets(VulkanContext::get_vulkan_device(), descriptor_pool, 1, &set);
					});
				}
				else if (pDevice != VK_NULL_HANDLE)
				{
					const VkDescriptorSet* pSet = &ps;
					vkFreeDescriptorSets(pDevice, pPool->m_pDescriptorPool, 1, pSet);
				}
			}
			pDescriptorSet->m_pVkDescriptorSet = VK_NULL_HANDLE;
			pDescriptorSet->clear_cache();
			pDescriptorSet->clear_pool_container();
			if (pPool->m_uAllocedSet == 0)
			{
				if (auto pPrev = pPool->m_pPreviousPool.lock())
				{
					// Non-head pool nodes can be released once they become empty.
					auto pCur = pPool;
					auto pNext = pPool->m_pNextPool;

					pPrev->m_pNextPool = pNext;
					if (pNext)
					{
						pNext->m_pPreviousPool = pPrev;
					}
					pCur->m_pNextPool = nullptr;
					pCur->m_pPreviousPool.reset();
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
		pNewPool->m_bBindlessPool = m_bBindlessPool;

		VkDescriptorPoolCreateInfo descriptorPoolInfo{}; 
		descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		descriptorPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		descriptorPoolInfo.poolSizeCount = (uint32_t)pNewPool->m_vecDescriptorPoolSize.size();
		descriptorPoolInfo.pPoolSizes = pNewPool->m_vecDescriptorPoolSize.data();
		descriptorPoolInfo.maxSets = pNewPool->m_uMaxSet; 
		if (pNewPool->m_bBindlessPool)
		{
			descriptorPoolInfo.flags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
		}

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
		return *this;
	}
	VulkanDescriptorSet::VulkanDescriptorSet(VkDescriptorSetLayout layout, std::shared_ptr<VulkanDescriptorPoolContainer> poolContainer)
	{
		H_ASSERT(layout);
		H_ASSERT(poolContainer);
		m_pVkLayout = layout;
		m_pPoolContainer = poolContainer;
		poolContainer->add_allocated(this);
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
			m_pPoolContainer->remove(this);
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
	void VulkanDescriptorSet::prepare_write_capacity(uint32_t imageDescriptorCount, uint32_t bufferDescriptorCount, uint32_t writeCount)
	{
		m_vecCachedImageAndSamplerInfo.reserve(imageDescriptorCount);
		m_vecCachedBufferInfo.reserve(bufferDescriptorCount);
		m_vecWriteDescriptorSets.reserve(writeCount);
	}
	VulkanDescriptorSet& VulkanDescriptorSet::add_bind_srv(uint32_t uBinding, std::shared_ptr<BufferView> srv)
	{
		return add_bind_srv_array(uBinding, { srv }, ASH_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	}
	VulkanDescriptorSet& VulkanDescriptorSet::add_bind_srv(uint32_t uBinding, std::shared_ptr<TextureView> srv)
	{
		return add_bind_srv_array(uBinding, { srv }, ASH_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
	}
	VulkanDescriptorSet& VulkanDescriptorSet::add_bind_srv_array(uint32_t uBinding, const std::vector<std::shared_ptr<BufferView>>& srvs, AshDescriptorType descriptorType)
	{
		H_ASSERT(!srvs.empty());
		const size_t start_index = m_vecCachedBufferInfo.size();
		for (const auto& srv : srvs)
		{
			H_ASSERT(srv);
			auto& desc = srv->get_view_desc();
			auto pVulkanBuffer = std::static_pointer_cast<VulkanBuffer>(srv->get_parent_buffer());
			H_ASSERT(pVulkanBuffer);
			m_vecCachedBufferInfo.emplace_back(VkDescriptorBufferInfo{ pVulkanBuffer->get_vk_buffer_handle(), (VkDeviceSize)desc.uByteOffset, (VkDeviceSize)desc.uByteRange });
			m_vecBarrierCollection.emplace_back(srv->get_parent_buffer(), AshResourceState::SRVMask);
		}
		VkWriteDescriptorSet descriptorWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		descriptorWrite.dstSet = m_pVkDescriptorSet;
		descriptorWrite.dstBinding = uBinding;
		descriptorWrite.descriptorCount = static_cast<uint32_t>(srvs.size());
		descriptorWrite.descriptorType = get_buffer_descriptor_type(descriptorType, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
		descriptorWrite.pBufferInfo = m_vecCachedBufferInfo.data() + start_index;
		m_vecWriteDescriptorSets.push_back(descriptorWrite);
		return *this;
	}
	VulkanDescriptorSet& VulkanDescriptorSet::add_bind_srv_array(uint32_t uBinding, const std::vector<std::shared_ptr<TextureView>>& srvs, AshDescriptorType descriptorType)
	{
		H_ASSERT(!srvs.empty());
		const size_t start_index = m_vecCachedImageAndSamplerInfo.size();
		for (const auto& srv : srvs)
		{
			H_ASSERT(srv);
			m_vecCachedImageAndSamplerInfo.emplace_back(VkDescriptorImageInfo{ (VkSampler)VK_NULL_HANDLE, (VkImageView)srv->get_native_handle(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
			auto pVulkanTexture = std::static_pointer_cast<VulkanTexture>(srv->get_parent_texture());
			H_ASSERT(pVulkanTexture);
			auto subRange = pVulkanTexture->resolve_subresource_range(srv->get_subresource_range());
			m_vecBarrierCollection.emplace_back(srv->get_parent_texture(), AshResourceState::SRVMask, subRange);
		}
		VkWriteDescriptorSet descriptorWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		descriptorWrite.dstSet = m_pVkDescriptorSet;
		descriptorWrite.dstBinding = uBinding;
		descriptorWrite.descriptorCount = static_cast<uint32_t>(srvs.size());
		descriptorWrite.descriptorType = get_descriptor_type(descriptorType);
		if (descriptorWrite.descriptorType == VK_DESCRIPTOR_TYPE_MAX_ENUM)
		{
			descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		}
		descriptorWrite.pImageInfo = m_vecCachedImageAndSamplerInfo.data() + start_index;
		m_vecWriteDescriptorSets.push_back(descriptorWrite);
		return *this;
	}
	VulkanDescriptorSet& VulkanDescriptorSet::add_bind_uav(uint32_t uBinding, std::shared_ptr<BufferView> uav)
	{
		return add_bind_uav_array(uBinding, { uav }, ASH_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	}
	VulkanDescriptorSet& VulkanDescriptorSet::add_bind_uav(uint32_t uBinding, std::shared_ptr<TextureView> uav)
	{
		return add_bind_uav_array(uBinding, { uav }, ASH_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	}
	VulkanDescriptorSet& VulkanDescriptorSet::add_bind_uav_array(uint32_t uBinding, const std::vector<std::shared_ptr<BufferView>>& uavs, AshDescriptorType descriptorType)
	{
		H_ASSERT(!uavs.empty());
		const size_t start_index = m_vecCachedBufferInfo.size();
		for (const auto& uav : uavs)
		{
			H_ASSERT(uav);
			auto& desc = uav->get_view_desc();
			auto pVulkanBuffer = std::static_pointer_cast<VulkanBuffer>(uav->get_parent_buffer());
			H_ASSERT(pVulkanBuffer);
			m_vecCachedBufferInfo.emplace_back(VkDescriptorBufferInfo{ pVulkanBuffer->get_vk_buffer_handle(), (VkDeviceSize)desc.uByteOffset, (VkDeviceSize)desc.uByteRange });
			m_vecBarrierCollection.emplace_back(uav->get_parent_buffer(), AshResourceState::UAVMask);
		}
		VkWriteDescriptorSet descriptorWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		descriptorWrite.dstSet = m_pVkDescriptorSet;
		descriptorWrite.dstBinding = uBinding;
		descriptorWrite.descriptorCount = static_cast<uint32_t>(uavs.size());
		descriptorWrite.descriptorType = get_buffer_descriptor_type(descriptorType, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
		descriptorWrite.pBufferInfo = m_vecCachedBufferInfo.data() + start_index;
		m_vecWriteDescriptorSets.push_back(descriptorWrite);
		return *this;
	}
	VulkanDescriptorSet& VulkanDescriptorSet::add_bind_uav_array(uint32_t uBinding, const std::vector<std::shared_ptr<TextureView>>& uavs, AshDescriptorType descriptorType)
	{
		H_ASSERT(!uavs.empty());
		const size_t start_index = m_vecCachedImageAndSamplerInfo.size();
		for (const auto& uav : uavs)
		{
			H_ASSERT(uav);
			m_vecCachedImageAndSamplerInfo.emplace_back(VkDescriptorImageInfo{ (VkSampler)VK_NULL_HANDLE, (VkImageView)uav->get_native_handle(), VK_IMAGE_LAYOUT_GENERAL });
			auto pVulkanTexture = std::static_pointer_cast<VulkanTexture>(uav->get_parent_texture());
			H_ASSERT(pVulkanTexture);
			auto subRange = pVulkanTexture->resolve_subresource_range(uav->get_subresource_range());
			m_vecBarrierCollection.emplace_back(uav->get_parent_texture(), AshResourceState::UAVMask, subRange);
		}
		VkWriteDescriptorSet descriptorWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		descriptorWrite.dstSet = m_pVkDescriptorSet;
		descriptorWrite.dstBinding = uBinding;
		descriptorWrite.descriptorCount = static_cast<uint32_t>(uavs.size());
		descriptorWrite.descriptorType = get_descriptor_type(descriptorType);
		if (descriptorWrite.descriptorType == VK_DESCRIPTOR_TYPE_MAX_ENUM)
		{
			descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		}
		descriptorWrite.pImageInfo = m_vecCachedImageAndSamplerInfo.data() + start_index;
		m_vecWriteDescriptorSets.push_back(descriptorWrite);
		return *this;
	}
	VulkanDescriptorSet& VulkanDescriptorSet::add_bind_cbv(uint32_t uBinding, std::shared_ptr<BufferView> cbv)
	{
		//upper logic must avoid nullptr
		H_ASSERT(cbv);
		auto& desc = cbv->get_view_desc();
		auto pVulkanBuffer = std::static_pointer_cast<VulkanBuffer>(cbv->get_parent_buffer());
		H_ASSERT(pVulkanBuffer);
		m_vecCachedBufferInfo.emplace_back(VkDescriptorBufferInfo{ pVulkanBuffer->get_vk_buffer_handle(), (VkDeviceSize)desc.uByteOffset, (VkDeviceSize)desc.uByteRange });
		//transition
		{
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
		return add_bind_sampler_array(uBinding, { smaplerView });
	}
	VulkanDescriptorSet& VulkanDescriptorSet::add_bind_sampler_array(uint32_t uBinding, const std::vector<std::shared_ptr<SamplerView>>& samplerViews)
	{
		H_ASSERT(!samplerViews.empty());
		const size_t start_index = m_vecCachedImageAndSamplerInfo.size();
		for (const auto& samplerView : samplerViews)
		{
			H_ASSERT(samplerView);
			m_vecCachedImageAndSamplerInfo.emplace_back(VkDescriptorImageInfo{ (VkSampler)samplerView->get_native_handle(), (VkImageView)VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
		}
		VkWriteDescriptorSet descriptorWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		descriptorWrite.dstSet = m_pVkDescriptorSet;
		descriptorWrite.dstBinding = uBinding;
		descriptorWrite.descriptorCount = static_cast<uint32_t>(samplerViews.size());
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		descriptorWrite.pImageInfo = m_vecCachedImageAndSamplerInfo.data() + start_index;
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
		m_vecBarrierCollection.clear();
	}
	void VulkanDescriptorSet::clear_pool_container()
	{
		m_pPoolContainer = nullptr;
		m_pRealAllocPool = nullptr;
	}
	const std::vector<AshBarrier>& VulkanDescriptorSet::get_barriers() const
	{
		return m_vecBarrierCollection;
	}
	VkDescriptorSet VulkanDescriptorSet::get_native_handle() const
	{
		return m_pVkDescriptorSet;
	}
}
