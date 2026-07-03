#pragma once
#include "VulkanHelper.hpp"
#include "VulkanWrapper.h"
#include "Graphics/CommandBuffer.h"
#include "Base/ds/harray.hpp"
using namespace AshEngine;
namespace RHI
{
	static const uint32_t k_descriptor_sets_pool_size = 4096;
	static const uint32_t k_global_pool_elements = 128;
	static const uint32_t k_secondary_command_buffers_count = 2;
	struct FramePool;
	struct VulkanCommandBufferManager;
	class RenderPass;
	//TODO: improve command buffer store and get !!!!!!!
	class VulkanCommandBuffer : public CommandBuffer
	{
	public:
		VulkanCommandBuffer() = default;
		~VulkanCommandBuffer() {};
		//NO_COPYABLE(VulkanCommandBuffer);
	private:	
		auto init(uint32_t poolIndex, bool secondary) -> void;
		auto shutdown() -> void;
	public:
		inline auto const get_vkCommandBuffer() const
		{
			return vkCommandBuffer;
		}
		inline auto is_secondary() const
		{
			return secondary;
		}
	public:
		//rhi interfaces
		virtual auto begin_record()  -> void override;
		virtual auto end_record() -> void override;
		virtual auto get_state() -> AshCommandBufferState override;
		auto set_state(AshCommandBufferState state) -> void override;
		auto get_native_handle() -> void* override;
		auto cmd_transition_resource_state(const AshBarrier& barrierInfo) -> bool override;

		auto cmd_transition_resource_state(const std::initializer_list<AshBarrier>& lsBarrierInfoArrray) -> bool override;

		auto cmd_transition_resource_state(const AshBarrier* pBarrierInfo, uint32_t uBarrierCount) -> bool override;
		auto cmd_begin_render_pass(std::shared_ptr<Framebuffer> frameBuffer, const char* debug_scope_name = nullptr) -> void override;
		auto cmd_end_render_pass() -> void override;
		auto cmd_bind_pipeline() -> void override;
		auto cmd_set_viewport(const Viewport& viewport) -> void override;
		auto cmd_set_scissor(const Rect2DInt& scissor) -> void override;
		auto cmd_bind_vertex_buffers(uint32_t firstBinding, uint32_t bindingCount, std::shared_ptr<Buffer>* buffers, const uint64_t* offsets) -> void override;
		auto cmd_bind_index_buffer(std::shared_ptr<Buffer> buffer, uint64_t offset, AshIndexType indexType) -> void override;
		auto cmd_draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0) -> void override;
		auto cmd_draw_indexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0, int32_t vertexOffset = 0, uint32_t firstInstance = 0) -> void override;
		auto cmd_dispatch(uint32_t groupCountX, uint32_t groupCountY = 1, uint32_t groupCountZ = 1) -> void override;
		auto cmd_copy_texture(std::shared_ptr<Texture> source, std::shared_ptr<Texture> destination) -> bool override;
		auto cmd_copy_texture_region_to_buffer(
			std::shared_ptr<Texture> source,
			uint32_t x,
			uint32_t y,
			std::shared_ptr<Buffer> destination,
			uint64_t buffer_offset) -> bool override;
		auto cmd_copy_texture_to_buffer(
			std::shared_ptr<Texture> source,
			std::shared_ptr<Buffer> destination,
			uint64_t buffer_offset,
			uint32_t row_pitch_bytes) -> bool override;
		auto cmd_update_sub_resource(std::shared_ptr<Buffer>, uint32_t uOffset, uint32_t uSize, void* pData) -> bool override;
		auto cmd_update_texture_sub_resource(std::shared_ptr<Texture> texture, const void* pData) -> bool;
	private:
		VkCommandBuffer							vkCommandBuffer				= VK_NULL_HANDLE;
		VkCommandPool							vkCommandPool				= VK_NULL_HANDLE;
		VkDescriptorPool						vk_descriptor_pool			= VK_NULL_HANDLE;
		std::shared_ptr<RenderPass>				currentBoundRenderPass		= nullptr;
		std::shared_ptr<Framebuffer>			currentBoundFramebuffer		= nullptr;
		bool active_render_pass_debug_label = false;
		bool secondary = false;
		AshCommandBufferState state = AshCommandBufferState::ASH_Idle;
		

		friend class VulkanCommandBufferManager;		
	};
	struct VulkanCommandBufferManager
	{
		auto init(uint32_t numThread) -> void;
		auto shutdown() -> void;
		auto reset(uint32_t frameIndex) -> void;
		auto get_command_buffer(uint32_t frameIndex, uint32_t threadIndex) -> VulkanCommandBuffer*;
		auto get_secondary_command_buffer(uint32_t frameIndex, uint32_t threadIndex) -> VulkanCommandBuffer*;
		Array<VulkanCommandBuffer>    commandBuffers;
		Array<VulkanCommandBuffer>    secondaryCommandBuffers;
		Array<uint8_t>				  usedBuffers;       // Track how many buffers were used per thread per frame.
		Array<uint8_t>				  usedSecondaryCommandBuffers;
		uint32_t                      numPoolsPerFrame = 0;
		uint32_t                      numCommandBuffersPerThread = 3;
		uint32_t					  pool_from_indices(uint32_t frame_index, uint32_t thread_index);
	};	
};
