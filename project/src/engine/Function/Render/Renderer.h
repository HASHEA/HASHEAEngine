#pragma once
#include "RenderDevice.h"
#include "Graphics/GpuTimingTelemetryRHI.h"
#include <array>
#include <chrono>
#include <cstddef>
#include <memory>
#include <cstdint>
#include <vector>

namespace AshEngine
{
	class Renderer;
	struct RenderGraphResolvedBufferTransition;
	bool submit_render_graph_buffer_transitions(
		Renderer& renderer,
		const RenderGraphResolvedBufferTransition* transitions,
		size_t transition_count);
	struct VertexBufferBinding
	{
		uint32_t slot = 0;
		std::shared_ptr<VertexBuffer> buffer = nullptr;
		uint64_t offset = 0;
	};

	struct VertexBufferBindingList
	{
		static constexpr size_t InlineCapacity = 4;

		void push_back(const VertexBufferBinding& binding)
		{
			if (m_inline_count < InlineCapacity)
			{
				m_inline[m_inline_count++] = binding;
				return;
			}
			m_overflow.push_back(binding);
		}

		void clear()
		{
			for (size_t index = 0; index < m_inline_count; ++index)
			{
				m_inline[index] = {};
			}
			m_inline_count = 0;
			m_overflow.clear();
		}

		size_t size() const
		{
			return m_inline_count + m_overflow.size();
		}

		bool empty() const
		{
			return size() == 0;
		}

		VertexBufferBinding& operator[](size_t index)
		{
			return index < m_inline_count ? m_inline[index] : m_overflow[index - m_inline_count];
		}

		const VertexBufferBinding& operator[](size_t index) const
		{
			return index < m_inline_count ? m_inline[index] : m_overflow[index - m_inline_count];
		}

		bool uses_inline_storage_for_tests() const
		{
			return m_overflow.empty();
		}

	private:
		std::array<VertexBufferBinding, InlineCapacity> m_inline{};
		size_t m_inline_count = 0;
		std::vector<VertexBufferBinding> m_overflow{};
	};

	struct GraphicsDrawDesc
	{
		static constexpr uint32_t InlineConstDataCapacity = 256;

		GraphicsProgram* program = nullptr;
		VertexBufferBindingList vertex_buffers{};
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
		bool inline_const_data_valid = false;
		bool reverse_z = false;
		std::array<uint8_t, InlineConstDataCapacity> inline_const_data{};
		std::vector<uint8_t> const_data{};
		// SDD-2026-07-10-gpu-particles：非空即走 cmd_draw_indirect（drawCount=1，非索引），
		// 忽略 vertex_count/instance_count/index_buffer
		std::shared_ptr<StorageBuffer> indirect_args_buffer = nullptr;
		uint64_t indirect_args_offset = 0;
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
		uint64_t render_frame_id = 0u;
		bool gpu_timing_frame_submitted = false;
		std::array<RHI::GpuFrameTimingSample, RHI::kGpuTimingFrameRingDepth>
			completed_gpu_samples{};
		uint32_t completed_gpu_sample_count = 0u;
		uint32_t frame_width = 0;
		uint32_t frame_height = 0;
		uint32_t graphics_pass_count = 0;
		uint32_t draw_call_count = 0;
		uint32_t compute_dispatch_count = 0;
		double cpu_frame_time_ms = 0.0;
		double backend_begin_frame_time_ms = 0.0;
		double render_end_frame_time_ms = 0.0;
		double present_time_ms = 0.0;
		double instantaneous_fps = 0.0;
		double average_cpu_frame_time_ms = 0.0;
		double average_fps = 0.0;
	};

	ASH_API uint32_t drain_completed_gpu_timing_samples(
		RHI::IGpuTimingTelemetry* telemetry,
		RendererFrameStats& frame_stats);

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
			bool end();

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
		RHI::SwapchainPresentResult begin_frame();
		bool end_frame();
		RHI::SwapchainPresentResult present();
		RenderDevice* get_render_device() const
		{
			return m_render_device;
		}

		std::shared_ptr<RenderTarget> get_back_buffer();
		std::shared_ptr<RenderTarget> create_render_target(const RenderTargetDesc& desc);
		std::shared_ptr<RenderTarget> create_texture_2d(const TextureUploadDesc& desc);
		std::shared_ptr<RenderTarget> create_texture_cube(const TextureCubeUploadDesc& desc);
		std::shared_ptr<RenderTarget> acquire_transient_render_target(const RenderTargetDesc& desc);
		void release_transient_render_target(const std::shared_ptr<RenderTarget>& render_target);
		void clear_transient_render_targets();

		std::shared_ptr<UniformBuffer> create_uniform_buffer(const UniformBufferDesc& desc);
		std::shared_ptr<VertexBuffer> create_vertex_buffer(const VertexBufferDesc& desc);
		std::shared_ptr<IndexBuffer> create_index_buffer(const IndexBufferDesc& desc);
		std::shared_ptr<StorageBuffer> create_storage_buffer(const StorageBufferDesc& desc);
		std::shared_ptr<StorageBuffer> acquire_transient_storage_buffer(const StorageBufferDesc& desc);
		void release_transient_storage_buffer(const std::shared_ptr<StorageBuffer>& buffer);
		void clear_transient_storage_buffers();
		std::shared_ptr<RenderSampler> create_sampler(const RenderSamplerDesc& desc, const char* debug_name = nullptr);

		std::unique_ptr<GraphicsProgram> create_graphics_program(const GraphicsProgramDesc& desc);
		std::unique_ptr<ComputeProgram> create_compute_program(const ComputeProgramDesc& desc);
		bool reflect_graphics_program(
			const GraphicsProgramDesc& desc,
			std::vector<RHI::ShaderResourceBindingLayout>& out_binding_layouts,
			RHI::ShaderParameterBlockLayout* out_parameter_block_layout = nullptr,
			const char* parameter_block_name = nullptr);

		bool begin_pass(const PassDesc& desc, GraphicsPassContext& pass_context);
		bool draw(const GraphicsDrawDesc& desc);
		bool dispatch(const ComputeDispatchDesc& desc);
		bool is_in_pass() const;
		const RendererFrameStats& get_frame_stats() const;

	private:
		bool submit_graph_buffer_transitions(
			const RenderGraphResolvedBufferTransition* transitions,
			size_t transition_count);
		bool end_active_pass(GraphicsPassContext* pass_context);

	private:
		void update_frame_timing_history(double frame_time_ms);
		void complete_frame_timing();

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
		friend class RenderGraphBuilder;
		friend bool submit_render_graph_buffer_transitions(
			Renderer& renderer,
			const RenderGraphResolvedBufferTransition* transitions,
			size_t transition_count);
	};
}
