#pragma once
#include "RHICommon.h"
#include <string>
#include <utility>

namespace RHI
{
	struct TextureSubResource;
	class Texture;
	class Buffer;
	class Framebuffer;
	enum class AshResourceState : uint32_t;
	struct AshBarrier;
	class CommandBuffer
	{
	public:
		CommandBuffer() = default;
		virtual ~CommandBuffer() {}
	public:
	
		virtual auto begin_record() -> void = 0;
		virtual auto end_record() ->  void = 0;
		virtual auto get_state()->AshCommandBufferState = 0;
		virtual auto set_state(AshCommandBufferState state) ->void = 0;
		
	
		virtual auto get_native_handle() -> void* = 0;
	public:

		virtual auto cmd_transition_resource_state(const AshBarrier& barrierInfo) -> bool = 0;
		virtual auto cmd_transition_resource_state(const std::initializer_list<AshBarrier>& lsBarrierInfoArrray) -> bool = 0;
		virtual auto cmd_transition_resource_state(const AshBarrier* pBarrierInfo, uint32_t uBarrierCount) -> bool = 0;

		// debug_scope_name: event label for tools (RenderDoc / PIX). Must reflect the current logical pass only; never
		// fall back to framebuffer object names (cached framebuffers may retain another pass's label). If null/empty, use "namelesspass".
		virtual auto cmd_begin_render_pass(std::shared_ptr<Framebuffer> frameBuffer, const char* debug_scope_name = nullptr) -> void = 0;
		virtual auto cmd_end_render_pass() -> void = 0;
		virtual auto cmd_bind_pipeline() -> void = 0;
		virtual auto cmd_set_viewport(const Viewport& viewport) -> void = 0;
		virtual auto cmd_set_scissor(const Rect2DInt& scissor) -> void = 0;
		virtual auto cmd_bind_vertex_buffers(uint32_t firstBinding, uint32_t bindingCount, std::shared_ptr<Buffer>* buffers, const uint64_t* offsets) -> void = 0;
		virtual auto cmd_bind_index_buffer(std::shared_ptr<Buffer> buffer, uint64_t offset, AshIndexType indexType) -> void = 0;
		virtual auto cmd_draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0) -> void = 0;
		virtual auto cmd_draw_indexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0, int32_t vertexOffset = 0, uint32_t firstInstance = 0) -> void = 0;
		virtual auto cmd_dispatch(uint32_t groupCountX, uint32_t groupCountY = 1, uint32_t groupCountZ = 1) -> void = 0;
		virtual auto cmd_copy_texture(std::shared_ptr<Texture> source, std::shared_ptr<Texture> destination) -> bool = 0;
		virtual auto cmd_update_sub_resource(std::shared_ptr<Buffer>, uint32_t uOffset, uint32_t uSize, void* pData) -> bool = 0;

		bool has_error() const { return m_has_error; }
		const std::string& get_last_error() const { return m_last_error; }
		void clear_error()
		{
			m_has_error = false;
			m_last_error.clear();
		}

	protected:
		void mark_error(std::string message)
		{
			if (!m_has_error)
			{
				m_last_error = std::move(message);
			}
			m_has_error = true;
		}

	private:
		bool m_has_error = false;
		std::string m_last_error{};
	};
};
