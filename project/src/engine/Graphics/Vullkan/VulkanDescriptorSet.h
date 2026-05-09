#pragma once
#include "Base/hplatform.h"
#include "Graphics/RHICommon.h"
#include "Graphics/DescriptorSetLayout.h"
#include "SpvHelper.h"
#include "VulkanHelper.hpp"
#include <set>
#include <memory>
namespace RHI
{
	void shutdown_vulkan_descriptor_set_layout_cache();

	class VulkanDescriptorSetLayout;
	class VulkanDescriptorSet;
	class VulkanDescriptorPool;
	class TextureView;
	class BufferView;
	class SamplerView;
	class AccelerationStructureView;
	struct VulkanDescriptorPoolContainer
	{
		std::set<VulkanDescriptorSet*> m_setAlloced;
		std::shared_ptr<VulkanDescriptorPool> m_pDescriptorPool;
		std::shared_ptr<VulkanDescriptorPool> get_descriptor_pool() const;
		void add_allocated(VulkanDescriptorSet* p);
		void remove(VulkanDescriptorSet* pSet);
		void clear();
		VulkanDescriptorPoolContainer();
		~VulkanDescriptorPoolContainer();
	};
	class VulkanDescriptorPool : public RHIResource
	{
	public:
		VulkanDescriptorPool();
		~VulkanDescriptorPool();
		VulkanDescriptorPool& begin(uint32_t maxSet = 16, bool bBindlessSet = false);
		VulkanDescriptorPool& add_pool_item(AshDescriptorType descriptorType, uint32_t uCount = 1);
		bool             end();
		bool             free_descriptor_set(VulkanDescriptorSet* pDescriptorSet);
		VkDescriptorPool get_vk_pool() const;
		void             increate_allocated_set();
		void             release_live_set();
		void             release_resident_set();
		void             mark_full();
		bool             is_full() const;

		std::shared_ptr<VulkanDescriptorPool> create_from_header();

	private:
		std::vector<VkDescriptorPoolSize> m_vecDescriptorPoolSize;
		uint32_t                          m_uMaxSet = 0;
		uint32_t                          m_uLiveSet = 0;
		uint32_t                          m_uResidentSet = 0;
		VkDescriptorPool                  m_pDescriptorPool = VK_NULL_HANDLE;
		bool                              m_bBindlessPool = false;
		bool                              m_bExhausted = false;
		uint32_t                          m_uPoolID = 0;

	public:
		// request next when current pool is full
		std::shared_ptr<VulkanDescriptorPool> m_pNextPool;

		// Back-edge in the pool chain. Held weakly so the chain is owned
		// strictly forward (header -> next -> next -> ...); the previous link
		// must not extend the lifetime of an earlier node, otherwise the
		// chain forms a shared_ptr cycle and ~VulkanDescriptorPool never runs
		// (VUID-vkDestroyDevice-device-05137 at shutdown).
		std::weak_ptr<VulkanDescriptorPool> m_pPreviousPool;

		// RHIResource overrides.
		auto get_native_handle() -> void* override;
		auto get_name() -> const char* override;
	};
	class VulkanDescriptorSetLayout : public DescriptorSetLayout
	{
	public:
		VulkanDescriptorSetLayout() = default;
		~VulkanDescriptorSetLayout() override;

		static std::shared_ptr<VulkanDescriptorSetLayout> create(const DescriptorSetLayoutCreation& creation);
		const DescriptorSetLayoutCreation& get_creation() const;
		uint32_t get_set_index() const;
		std::shared_ptr<VulkanDescriptorPoolContainer> get_pool_container() const;
		void shutdown_pool_container();

		auto get_native_handle() -> void* override;
		auto get_name() -> const char* override;

	private:
		bool init(const DescriptorSetLayoutCreation& creation);

	private:
		DescriptorSetLayoutCreation m_creation{};
		VkDescriptorSetLayout m_vkDescriptorSetLayout = VK_NULL_HANDLE;
		std::shared_ptr<VulkanDescriptorPoolContainer> m_poolContainer = nullptr;
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
		void prepare_write_capacity(uint32_t imageDescriptorCount, uint32_t bufferDescriptorCount, uint32_t writeCount);
		VulkanDescriptorSet& add_bind_srv(uint32_t uBinding, std::shared_ptr<BufferView> srv);
		VulkanDescriptorSet& add_bind_srv(uint32_t uBinding, std::shared_ptr<TextureView> srv);
		VulkanDescriptorSet& add_bind_srv_array(uint32_t uBinding, const std::vector<std::shared_ptr<BufferView>>& srvs, AshDescriptorType descriptorType);
		VulkanDescriptorSet& add_bind_srv_array(uint32_t uBinding, const std::vector<std::shared_ptr<TextureView>>& srvs, AshDescriptorType descriptorType);
		VulkanDescriptorSet& add_bind_uav(uint32_t uBinding, std::shared_ptr<BufferView> uav);
		VulkanDescriptorSet& add_bind_uav(uint32_t uBinding, std::shared_ptr<TextureView> uav);
		VulkanDescriptorSet& add_bind_uav_array(uint32_t uBinding, const std::vector<std::shared_ptr<BufferView>>& uavs, AshDescriptorType descriptorType);
		VulkanDescriptorSet& add_bind_uav_array(uint32_t uBinding, const std::vector<std::shared_ptr<TextureView>>& uavs, AshDescriptorType descriptorType);
		VulkanDescriptorSet& add_bind_cbv(uint32_t uBinding, std::shared_ptr<BufferView> cbv);
		VulkanDescriptorSet& add_bind_sampler(uint32_t uBinding, std::shared_ptr<SamplerView> smaplerView);
		VulkanDescriptorSet& add_bind_sampler_array(uint32_t uBinding, const std::vector<std::shared_ptr<SamplerView>>& samplerViews);
		VulkanDescriptorSet& add_bind_acceleration_structure(uint32_t uBinding, std::shared_ptr<AccelerationStructureView> acclerationStructureView);
		bool end_bind();
	public:
		void clear_cache();
		void clear_pool_container();
		const std::vector<AshBarrier>& get_barriers() const;
		VkDescriptorSet get_native_handle() const;
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
		// Header node of the descriptor-pool chain used for this set.
		std::shared_ptr<VulkanDescriptorPool> m_pRealAllocPool;
		std::vector<AshBarrier> m_vecBarrierCollection;
	};
}
