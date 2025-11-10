#pragma once
#include "Base/hplatform.h"
#include "Graphics/RHICommon.h"
#include "VulkanHelper.hpp"
#include <set>
#include <memory>
namespace RHI
{
	class VulkanDescriptorSet;
	class VulkanDescriptorPool;
	class TextureView;
	class BufferView;
	class SamplerView;
	class AccelerationStructureView;
	struct VulkanDescriptorPoolContainer
	{
		std::set<std::shared_ptr<VulkanDescriptorSet>> m_setAlloced;
		std::shared_ptr<VulkanDescriptorPool> m_pDescriptorPool;
		std::shared_ptr<VulkanDescriptorPool> get_descriptor_pool() const;
		void add_allocated(std::shared_ptr<VulkanDescriptorSet> p);
		void remove(std::shared_ptr<VulkanDescriptorSet> pSet);
		void clear();
		VulkanDescriptorPoolContainer();
		~VulkanDescriptorPoolContainer();
	};
	class VulkanDescriptorPool : public RHIResource
	{
		static std::set<std::shared_ptr<VulkanDescriptorPool>> m_dirtyPool;

	public:
		VulkanDescriptorPool();
		~VulkanDescriptorPool();
		VulkanDescriptorPool& begin(uint32_t maxSet = 16, bool bBindlessSet = false);
		VulkanDescriptorPool& add_pool_item(AshDescriptorType descriptorType, uint32_t uCount = 1);
		bool             end();
		bool             free_descriptor_set(std::shared_ptr<VulkanDescriptorSet> pDescriptorSet);
		VkDescriptorPool get_vk_pool() const;
		void             increate_allocated_set();
		bool             is_full() const;

		std::shared_ptr<VulkanDescriptorPool> create_from_header();
		static bool            is_dirty_descriptor_pool(std::shared_ptr<VulkanDescriptorPool> pPool);
		std::shared_ptr<VulkanDescriptorPool> get_next();

	private:
		std::vector<VkDescriptorPoolSize> m_vecDescriptorPoolSize;
		uint32_t                          m_uMaxSet = 0;
		uint32_t                          m_uAllocedSet = 0;
		VkDescriptorPool                  m_pDescriptorPool;
		bool                              m_bBindlessPool = false;
		uint32_t                          m_uPoolID = 0;

	public:
		// request next when current pool is full
		std::shared_ptr<VulkanDescriptorPool> m_pNextPool;

		// last pool
		std::shared_ptr<VulkanDescriptorPool> m_pPreviousPool;

		// 通过 RHIResource 继承
		auto get_native_handle() -> void* override;
		auto get_name() -> const char* override;
	};
	//set and pool stay on program
	class VulkanDescriptorSet : public std::enable_shared_from_this<VulkanDescriptorSet>
	{
		friend class VulkanDescriptorPool;
	public:
		VulkanDescriptorSet(VkDescriptorSetLayout layout, std::shared_ptr<VulkanDescriptorPoolContainer> poolContainer);
		~VulkanDescriptorSet();
	public:
		VulkanDescriptorSet& begin_bind();
		VulkanDescriptorSet& add_bind_srv(uint32_t uBinding, std::shared_ptr<BufferView> srv);
		VulkanDescriptorSet& add_bind_srv(uint32_t uBinding, std::shared_ptr<TextureView> srv);
		VulkanDescriptorSet& add_bind_uav(uint32_t uBinding, std::shared_ptr<BufferView> uav);
		VulkanDescriptorSet& add_bind_uav(uint32_t uBinding, std::shared_ptr<TextureView> uav);
		VulkanDescriptorSet& add_bind_cbv(uint32_t uBinding, std::shared_ptr<BufferView> cbv);
		VulkanDescriptorSet& add_bind_sampler(uint32_t uBinding, std::shared_ptr<SamplerView> smaplerView);
		VulkanDescriptorSet& add_bind_acceleration_structure(uint32_t uBinding, std::shared_ptr<AccelerationStructureView> acclerationStructureView);
		bool end_bind();
	public:
		void clear_cache();
		void clear_pool_container();
	private:
		//cache for valid address
		std::vector<VkDescriptorImageInfo> m_vecCachedImageAndSamplerInfo; 
		std::vector<VkDescriptorBufferInfo> m_vecCachedBufferInfo;
		std::vector<VkWriteDescriptorSetAccelerationStructureKHR> m_vecCachedASInfo;
	private:
		std::shared_ptr<VulkanDescriptorPoolContainer> m_pPoolContainer = nullptr;
		VkDescriptorSetLayout m_pVkLayout = VK_NULL_HANDLE;
		VkDescriptorSet m_pVkDescriptorSet = VK_NULL_HANDLE;
		std::vector<VkWriteDescriptorSet> m_vecWriteDescriptorSets;
		// 基类持有的只是header
		std::shared_ptr<VulkanDescriptorPool> m_pRealAllocPool;
		std::vector<AshBarrier> m_vecBarrierCollection;
	};
}
