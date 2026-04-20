#pragma once
#include "RenderDevice.h"
#include <array>
#include <chrono>
#include <memory>
#include <cstdint>
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
		uint32_t const_data_size = 0;
		std::vector<uint8_t> const_data{};
	};

	struct ComputeDispatchDesc
	{
		ComputeProgram* program = nullptr;
		uint32_t group_count_x = 1;
		uint32_t group_count_y = 1;
		uint32_t group_count_z = 1;
	};

	struct RendererFrameStats
	{
		uint32_t frame_width = 0;
		uint32_t frame_height = 0;
		uint32_t graphics_pass_count = 0;
		uint32_t draw_call_count = 0;
		uint32_t compute_dispatch_count = 0;
		double cpu_frame_time_ms = 0.0;
		double instantaneous_fps = 0.0;
		double average_cpu_frame_time_ms = 0.0;
		double average_fps = 0.0;
	};

	class ASH_API Renderer
	{
	public:
		class ASH_API GraphicsPassContext
		{
		public:
			GraphicsPassContext() = default;
			~GraphicsPassContext();

			GraphicsPassContext(const GraphicsPassContext&) = delete;
			GraphicsPassContext& operator=(const GraphicsPassContext&) = delete;
			GraphicsPassContext(GraphicsPassContext&& other) noexcept;
			GraphicsPassContext& operator=(GraphicsPassContext&& other) noexcept;

		public:
			bool is_valid() const;
			bool draw(const GraphicsDrawDesc& desc);
			void end();

		private:
			explicit GraphicsPassContext(Renderer* renderer);

		private:
			Renderer* m_renderer = nullptr;
			bool m_active = false;
			PassDesc m_desc{};
			std::vector<GraphicsDrawDesc> m_draw_calls;
			friend class Renderer;
		};

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

		bool begin_pass(const PassDesc& desc, GraphicsPassContext& pass_context);
		bool draw(const GraphicsDrawDesc& desc);
		bool dispatch(const ComputeDispatchDesc& desc);
		bool is_in_pass() const;
		const RendererFrameStats& get_frame_stats() const;

	private:
		void end_active_pass(GraphicsPassContext* pass_context);

	private:
		void update_frame_timing_history(double frame_time_ms);

	private:
		RenderDevice* m_render_device = nullptr;
		GraphicsPassContext* m_active_pass = nullptr;
		RendererFrameStats m_frame_stats{};
		RendererFrameStats m_last_completed_frame_stats{};
		std::chrono::steady_clock::time_point m_frame_start_time{};
		bool m_frame_in_progress = false;
		std::array<double, 120> m_frame_time_history_ms{};
		uint32_t m_frame_time_history_count = 0;
		uint32_t m_frame_time_history_head = 0;
		double m_frame_time_history_sum_ms = 0.0;
	};
}
