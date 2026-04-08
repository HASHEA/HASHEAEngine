#pragma once
#include "RenderDevice.h"
#include <memory>
#include <vector>

namespace AshEngine
{
	struct VertexBufferBinding
	{
		uint32_t slot = 0;
		std::shared_ptr<VertexBuffer> buffer = nullptr;
		uint64_t offset = 0;
	};

	struct GraphicsDrawDesc
	{
		GraphicsProgram* program = nullptr;
		std::vector<VertexBufferBinding> vertex_buffers;
		std::shared_ptr<IndexBuffer> index_buffer = nullptr;
		uint64_t index_buffer_offset = 0;
		bool has_viewport = false;
		RenderViewport viewport{};
		bool has_scissor = false;
		RenderScissor scissor{};
		uint32_t vertex_count = 0;
		uint32_t index_count = 0;
		uint32_t instance_count = 1;
		uint32_t first_vertex = 0;
		uint32_t first_instance = 0;
		uint32_t first_index = 0;
		int32_t vertex_offset = 0;
	};

	struct ComputeDispatchDesc
	{
		ComputeProgram* program = nullptr;
		uint32_t group_count_x = 1;
		uint32_t group_count_y = 1;
		uint32_t group_count_z = 1;
	};

	class ASH_API Renderer
	{
	public:
		explicit Renderer(RenderDevice* render_device);
		~Renderer();

	public:
		bool begin_frame();
		bool end_frame();
		void present();

		std::shared_ptr<RenderTarget> get_back_buffer();
		std::shared_ptr<RenderTarget> create_render_target(const RenderTargetDesc& desc);
		std::shared_ptr<RenderTarget> acquire_transient_render_target(const RenderTargetDesc& desc);
		void release_transient_render_target(const std::shared_ptr<RenderTarget>& render_target);
		void clear_transient_render_targets();

		std::shared_ptr<UniformBuffer> create_uniform_buffer(const UniformBufferDesc& desc);
		std::shared_ptr<VertexBuffer> create_vertex_buffer(const VertexBufferDesc& desc);
		std::shared_ptr<IndexBuffer> create_index_buffer(const IndexBufferDesc& desc);
		std::shared_ptr<StorageBuffer> create_storage_buffer(const StorageBufferDesc& desc);

		std::unique_ptr<GraphicsProgram> create_graphics_program(const GraphicsProgramDesc& desc);
		std::unique_ptr<ComputeProgram> create_compute_program(const ComputeProgramDesc& desc);

		bool begin_pass(const PassDesc& desc);
		void end_pass();
		bool draw(const GraphicsDrawDesc& desc);
		bool dispatch(const ComputeDispatchDesc& desc);

	private:
		RenderDevice* m_render_device = nullptr;
	};
}
