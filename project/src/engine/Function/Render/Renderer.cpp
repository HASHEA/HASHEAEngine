#include "Renderer.h"
#include "Base/hlog.h"
#include "Base/hprofiler.h"
#include "Function/Application.h"
#include "Function/Gui/UIContext.h"
#include "Graphics/RHIResource.h"
#include "Graphics/Swapchain.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <unordered_set>

namespace AshEngine
{
	namespace
	{
		static auto pointer_sort_key(const void* pointer) -> uintptr_t
		{
			return reinterpret_cast<uintptr_t>(pointer);
		}

		static auto compare_draw_state_key(const GraphicsDrawDesc& lhs, const GraphicsDrawDesc& rhs) -> bool
		{
			const uintptr_t lhs_program = pointer_sort_key(lhs.program);
			const uintptr_t rhs_program = pointer_sort_key(rhs.program);
			if (lhs_program != rhs_program)
			{
				return lhs_program < rhs_program;
			}

			const uintptr_t lhs_index = pointer_sort_key(lhs.index_buffer.get());
			const uintptr_t rhs_index = pointer_sort_key(rhs.index_buffer.get());
			if (lhs_index != rhs_index)
			{
				return lhs_index < rhs_index;
			}

			const size_t common_vertex_buffer_count = std::min(lhs.vertex_buffers.size(), rhs.vertex_buffers.size());
			for (size_t index = 0; index < common_vertex_buffer_count; ++index)
			{
				if (lhs.vertex_buffers[index].slot != rhs.vertex_buffers[index].slot)
				{
					return lhs.vertex_buffers[index].slot < rhs.vertex_buffers[index].slot;
				}
				const uintptr_t lhs_vertex_buffer = pointer_sort_key(lhs.vertex_buffers[index].buffer.get());
				const uintptr_t rhs_vertex_buffer = pointer_sort_key(rhs.vertex_buffers[index].buffer.get());
				if (lhs_vertex_buffer != rhs_vertex_buffer)
				{
					return lhs_vertex_buffer < rhs_vertex_buffer;
				}
				if (lhs.vertex_buffers[index].offset != rhs.vertex_buffers[index].offset)
				{
					return lhs.vertex_buffers[index].offset < rhs.vertex_buffers[index].offset;
				}
			}

			return lhs.vertex_buffers.size() < rhs.vertex_buffers.size();
		}
	}

	Renderer::GraphicsPassContext::GraphicsPassContext(Renderer* renderer)
		: m_renderer(renderer)
	{
	}

	Renderer::GraphicsPassContext::~GraphicsPassContext()
	{
		end();
	}

	Renderer::GraphicsPassContext::GraphicsPassContext(GraphicsPassContext&& other) noexcept
	{
		*this = std::move(other);
	}

	Renderer::GraphicsPassContext& Renderer::GraphicsPassContext::operator=(GraphicsPassContext&& other) noexcept
	{
		if (this == &other)
		{
			return *this;
		}

		end();

		m_renderer = other.m_renderer;
		m_active = other.m_active;
		m_desc = std::move(other.m_desc);
		m_draw_calls = std::move(other.m_draw_calls);

		if (m_active && m_renderer && m_renderer->m_active_pass == &other)
		{
			m_renderer->m_active_pass = this;
		}

		other.m_renderer = nullptr;
		other.m_active = false;
		other.m_desc = PassDesc{};
		other.m_draw_calls.clear();
		return *this;
	}

	bool Renderer::GraphicsPassContext::is_valid() const
	{
		return m_active && m_renderer != nullptr;
	}

	bool Renderer::GraphicsPassContext::draw(const GraphicsDrawDesc& desc)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(is_valid() && desc.program);
		m_draw_calls.push_back(desc);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	void Renderer::GraphicsPassContext::end()
	{
		if (m_renderer)
		{
			m_renderer->end_active_pass(this);
		}
	}

	Renderer::Renderer(RenderDevice* render_device)
		: m_render_device(render_device)
	{
	}

	Renderer::~Renderer() = default;

	RHI::SwapchainPresentResult Renderer::begin_frame()
	{
		ASH_PROFILE_SCOPE_NC("Renderer::begin_frame", AshEngine::Profile::Color::Render);
		ASH_PROCESS_GUARD_RETURN(
			RHI::SwapchainPresentResult,
			bResult,
			RHI::SwapchainPresentResult::Completed,
			RHI::SwapchainPresentResult::Failed);
		m_frame_in_progress = false;
		m_frame_stats = {};
		m_frame_start_time = std::chrono::steady_clock::now();
		const auto backend_begin_start_time = std::chrono::steady_clock::now();
		const RHI::SwapchainPresentResult backend_begin_result = m_render_device
			? m_render_device->begin_frame()
			: RHI::SwapchainPresentResult::Failed;
		const auto backend_begin_end_time = std::chrono::steady_clock::now();
		m_frame_stats.backend_begin_frame_time_ms =
			std::chrono::duration<double, std::milli>(backend_begin_end_time - backend_begin_start_time).count();
		if (backend_begin_result != RHI::SwapchainPresentResult::Completed)
		{
			bResult = backend_begin_result;
			break;
		}

		m_frame_in_progress = true;
		if (std::shared_ptr<RenderTarget> back_buffer = get_back_buffer())
		{
			m_frame_stats.frame_width = back_buffer->get_width();
			m_frame_stats.frame_height = back_buffer->get_height();
		}
		ASH_PROCESS_GUARD_END(bResult, RHI::SwapchainPresentResult::Failed);
		if (bResult != RHI::SwapchainPresentResult::Completed)
		{
			m_frame_in_progress = false;
		}
		return bResult;
	}

	bool Renderer::end_frame()
	{
		ASH_PROFILE_SCOPE_NC("Renderer::end_frame", AshEngine::Profile::Color::Render);
		const auto render_end_start_time = std::chrono::steady_clock::now();
		if (m_active_pass)
		{
			end_active_pass(m_active_pass);
		}

		bool ui_result = true;
		if (Application::get() && Application::get_ui_context())
		{
			Application::get()->draw_engine_overlay();
			ui_result = Application::get_ui_context()->render();
			if (!ui_result)
			{
				HLogError("Renderer: UIContext render failed.");
			}
		}
		const bool result = m_render_device && m_render_device->end_frame();
		const auto render_end_end_time = std::chrono::steady_clock::now();
		m_frame_stats.render_end_frame_time_ms =
			std::chrono::duration<double, std::milli>(render_end_end_time - render_end_start_time).count();
		ASH_PROFILE_PLOT("Render/DrawCalls", static_cast<int64_t>(m_frame_stats.draw_call_count));
		ASH_PROFILE_PLOT("Render/Passes", static_cast<int64_t>(m_frame_stats.graphics_pass_count));
		ASH_PROFILE_PLOT("Render/Dispatches", static_cast<int64_t>(m_frame_stats.compute_dispatch_count));
		ASH_PROFILE_PLOT("Render/BeginFrameMs", m_frame_stats.backend_begin_frame_time_ms);
		ASH_PROFILE_PLOT("Render/EndFrameMs", m_frame_stats.render_end_frame_time_ms);
		return result && ui_result;
	}

	RHI::SwapchainPresentResult Renderer::present()
	{
		const auto present_start_time = std::chrono::steady_clock::now();
		RHI::SwapchainPresentResult present_result = RHI::SwapchainPresentResult::Failed;
		if (m_render_device)
		{
			present_result = m_render_device->present();
		}
		const auto present_end_time = std::chrono::steady_clock::now();
		m_frame_stats.present_time_ms =
			std::chrono::duration<double, std::milli>(present_end_time - present_start_time).count();
		ASH_PROFILE_PLOT("Render/PresentMs", m_frame_stats.present_time_ms);
		if (m_frame_in_progress)
		{
			complete_frame_timing();
		}
		return present_result;
	}

	std::shared_ptr<RenderTarget> Renderer::get_back_buffer()
	{
		return m_render_device ? m_render_device->get_back_buffer() : nullptr;
	}

	std::shared_ptr<RenderTarget> Renderer::create_render_target(const RenderTargetDesc& desc)
	{
		return m_render_device ? m_render_device->create_render_target(desc) : nullptr;
	}

	std::shared_ptr<RenderTarget> Renderer::create_texture_2d(const TextureUploadDesc& desc)
	{
		return m_render_device ? m_render_device->create_texture_2d(desc) : nullptr;
	}

	std::shared_ptr<RenderTarget> Renderer::create_texture_cube(const TextureCubeUploadDesc& desc)
	{
		return m_render_device ? m_render_device->create_texture_cube(desc) : nullptr;
	}

	std::shared_ptr<RenderTarget> Renderer::acquire_transient_render_target(const RenderTargetDesc& desc)
	{
		return m_render_device ? m_render_device->acquire_transient_render_target(desc) : nullptr;
	}

	void Renderer::release_transient_render_target(const std::shared_ptr<RenderTarget>& render_target)
	{
		if (m_render_device)
		{
			m_render_device->release_transient_render_target(render_target);
		}
	}

	void Renderer::clear_transient_render_targets()
	{
		if (m_render_device)
		{
			m_render_device->clear_transient_render_targets();
		}
	}

	std::shared_ptr<UniformBuffer> Renderer::create_uniform_buffer(const UniformBufferDesc& desc)
	{
		return m_render_device ? m_render_device->create_uniform_buffer(desc) : nullptr;
	}

	std::shared_ptr<VertexBuffer> Renderer::create_vertex_buffer(const VertexBufferDesc& desc)
	{
		return m_render_device ? m_render_device->create_vertex_buffer(desc) : nullptr;
	}

	std::shared_ptr<IndexBuffer> Renderer::create_index_buffer(const IndexBufferDesc& desc)
	{
		return m_render_device ? m_render_device->create_index_buffer(desc) : nullptr;
	}

	std::shared_ptr<StorageBuffer> Renderer::create_storage_buffer(const StorageBufferDesc& desc)
	{
		return m_render_device ? m_render_device->create_storage_buffer(desc) : nullptr;
	}

	std::shared_ptr<RenderSampler> Renderer::create_sampler(const RenderSamplerDesc& desc, const char* debug_name)
	{
		return m_render_device ? m_render_device->create_sampler(desc, debug_name) : nullptr;
	}

	std::unique_ptr<GraphicsProgram> Renderer::create_graphics_program(const GraphicsProgramDesc& desc)
	{
		return m_render_device ? m_render_device->create_graphics_program(desc) : nullptr;
	}

	std::unique_ptr<ComputeProgram> Renderer::create_compute_program(const ComputeProgramDesc& desc)
	{
		return m_render_device ? m_render_device->create_compute_program(desc) : nullptr;
	}

	bool Renderer::reflect_graphics_program(
		const GraphicsProgramDesc& desc,
		std::vector<RHI::ShaderResourceBindingLayout>& out_binding_layouts,
		RHI::ShaderParameterBlockLayout* out_parameter_block_layout,
		const char* parameter_block_name)
	{
		out_binding_layouts.clear();
		if (out_parameter_block_layout)
		{
			*out_parameter_block_layout = {};
		}
		return m_render_device ?
			m_render_device->reflect_graphics_program(desc, out_binding_layouts, out_parameter_block_layout, parameter_block_name) :
			false;
	}

	bool Renderer::begin_pass(const PassDesc& desc, GraphicsPassContext& pass_context)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_render_device && !m_active_pass && (!desc.color_attachments.empty() || desc.depth_attachment.render_target));
		pass_context.end();
		pass_context.m_renderer = this;
		pass_context.m_active = true;
		pass_context.m_desc = desc;
		pass_context.m_draw_calls.clear();
		m_active_pass = &pass_context;
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool Renderer::draw(const GraphicsDrawDesc& desc)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_active_pass && m_active_pass->m_renderer == this);
		bResult = m_active_pass->draw(desc);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool Renderer::dispatch(const ComputeDispatchDesc& desc)
	{
		ASH_PROFILE_SCOPE_NC("Renderer::dispatch", AshEngine::Profile::Color::Submit);
		ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(desc.group_count_x) * desc.group_count_y * desc.group_count_z);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_render_device && desc.program && !m_active_pass);
		ASH_PROCESS_ERROR(m_render_device->transition_compute_program_resources(desc.program));
		ASH_PROCESS_ERROR(m_render_device->bind_compute_program(desc.program));

		m_render_device->dispatch(desc.group_count_x, desc.group_count_y, desc.group_count_z);
		++m_frame_stats.compute_dispatch_count;
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool Renderer::is_in_pass() const
	{
		return m_active_pass != nullptr;
	}

	const RendererFrameStats& Renderer::get_frame_stats() const
	{
		return m_last_completed_frame_stats;
	}

	bool Renderer::submit_graph_resource_barriers(const std::vector<RHI::AshBarrier>& barriers)
	{
		return m_render_device && m_render_device->submit_graph_resource_barriers(barriers);
	}

	void Renderer::update_frame_timing_history(double frame_time_ms)
	{
		if (m_frame_time_history_count < static_cast<uint32_t>(m_frame_time_history_ms.size()))
		{
			m_frame_time_history_ms[m_frame_time_history_head] = frame_time_ms;
			m_frame_time_history_sum_ms += frame_time_ms;
			++m_frame_time_history_count;
		}
		else
		{
			m_frame_time_history_sum_ms -= m_frame_time_history_ms[m_frame_time_history_head];
			m_frame_time_history_ms[m_frame_time_history_head] = frame_time_ms;
			m_frame_time_history_sum_ms += frame_time_ms;
		}

		m_frame_time_history_head = (m_frame_time_history_head + 1u) % static_cast<uint32_t>(m_frame_time_history_ms.size());
		m_frame_stats.average_cpu_frame_time_ms =
			m_frame_time_history_count > 0 ? (m_frame_time_history_sum_ms / static_cast<double>(m_frame_time_history_count)) : 0.0;
		m_frame_stats.average_fps =
			m_frame_stats.average_cpu_frame_time_ms > 0.0 ? (1000.0 / m_frame_stats.average_cpu_frame_time_ms) : 0.0;
	}

	void Renderer::complete_frame_timing()
	{
		const auto frame_end_time = std::chrono::steady_clock::now();
		m_frame_stats.cpu_frame_time_ms = std::chrono::duration<double, std::milli>(frame_end_time - m_frame_start_time).count();
		m_frame_stats.instantaneous_fps =
			m_frame_stats.cpu_frame_time_ms > 0.0 ? (1000.0 / m_frame_stats.cpu_frame_time_ms) : 0.0;
		update_frame_timing_history(m_frame_stats.cpu_frame_time_ms);
		const RendererFrameStats completed_stats = m_frame_stats;
		m_last_completed_frame_stats = completed_stats;
		m_frame_in_progress = false;
	}

	void Renderer::end_active_pass(GraphicsPassContext* pass_context)
	{
		if (!pass_context || pass_context != m_active_pass)
		{
			return;
		}

		const char* pass_name = pass_context->m_desc.name ? pass_context->m_desc.name : "UnnamedPass";
		ASH_PROFILE_SCOPE_NC("Renderer::end_active_pass", AshEngine::Profile::Color::Submit);
		ASH_PROFILE_SCOPE_TEXT(pass_name, std::strlen(pass_name));

		if (pass_context->m_desc.allow_reorder_draws && pass_context->m_draw_calls.size() > 1)
		{
			ASH_PROFILE_SCOPE_NC("Renderer::SortDraws", AshEngine::Profile::Color::Submit);
			std::stable_sort(
				pass_context->m_draw_calls.begin(),
				pass_context->m_draw_calls.end(),
				compare_draw_state_key);
		}

		bool pass_started = false;
		bool success = m_render_device != nullptr;
		if (success)
		{
			ASH_PROFILE_SCOPE_NC("Renderer::PassTransitions", AshEngine::Profile::Color::Barrier);
			ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(pass_context->m_draw_calls.size()));
			static thread_local std::unordered_set<const GraphicsProgram*> transitioned_program_scratch{};
			static thread_local std::unordered_set<const VertexBuffer*> transitioned_vertex_buffer_scratch{};
			static thread_local std::unordered_set<const IndexBuffer*> transitioned_index_buffer_scratch{};
			static thread_local std::unordered_set<const StorageBuffer*> transitioned_indirect_args_scratch{};
			static thread_local std::vector<RHI::AshBarrier> pass_barrier_scratch{};
			struct PassBarrierScratchRelease
			{
				std::vector<RHI::AshBarrier>& barriers;
				~PassBarrierScratchRelease()
				{
					barriers.clear();
				}
			} release_pass_barrier_scratch{ pass_barrier_scratch };
			transitioned_program_scratch.clear();
			transitioned_vertex_buffer_scratch.clear();
			transitioned_index_buffer_scratch.clear();
			transitioned_indirect_args_scratch.clear();
			transitioned_program_scratch.reserve(pass_context->m_draw_calls.size());
			transitioned_vertex_buffer_scratch.reserve(pass_context->m_draw_calls.size());
			transitioned_index_buffer_scratch.reserve(pass_context->m_draw_calls.size());
			pass_barrier_scratch.clear();
			pass_barrier_scratch.reserve(pass_context->m_draw_calls.size() * 3u);
			for (size_t draw_index = 0; draw_index < pass_context->m_draw_calls.size(); ++draw_index)
			{
				const GraphicsDrawDesc& draw_desc = pass_context->m_draw_calls[draw_index];
				if (!draw_desc.program)
				{
					HLogError("Renderer: missing graphics program for pass '{}' draw {}.", pass_name, draw_index);
					success = false;
					break;
				}
				if (transitioned_program_scratch.insert(draw_desc.program).second &&
					!m_render_device->collect_graphics_program_resource_barriers(draw_desc.program, pass_barrier_scratch))
				{
					HLogError("Renderer: collect_graphics_program_resource_barriers failed for pass '{}' draw {}.", pass_name, draw_index);
					success = false;
					break;
				}

				for (size_t binding_index = 0; binding_index < draw_desc.vertex_buffers.size(); ++binding_index)
				{
					const VertexBufferBinding& binding = draw_desc.vertex_buffers[binding_index];
					if (!binding.buffer)
					{
						HLogError("Renderer: transition_vertex_buffer failed for pass '{}' draw {} slot {}.", pass_name, draw_index, binding.slot);
						success = false;
						break;
					}
					if (transitioned_vertex_buffer_scratch.insert(binding.buffer.get()).second &&
						!m_render_device->collect_vertex_buffer_barrier(binding.buffer, pass_barrier_scratch))
					{
						HLogError("Renderer: collect_vertex_buffer_barrier failed for pass '{}' draw {} slot {}.", pass_name, draw_index, binding.slot);
						success = false;
						break;
					}
				}
				if (!success)
				{
					break;
				}

				if (draw_desc.index_buffer &&
					transitioned_index_buffer_scratch.insert(draw_desc.index_buffer.get()).second &&
					!m_render_device->collect_index_buffer_barrier(draw_desc.index_buffer, pass_barrier_scratch))
				{
					HLogError("Renderer: collect_index_buffer_barrier failed for pass '{}' draw {}.", pass_name, draw_index);
					success = false;
					break;
				}

				if (draw_desc.indirect_args_buffer &&
					transitioned_indirect_args_scratch.insert(draw_desc.indirect_args_buffer.get()).second &&
					!m_render_device->collect_indirect_args_buffer_barrier(draw_desc.indirect_args_buffer, pass_barrier_scratch))
				{
					HLogError("Renderer: collect_indirect_args_buffer_barrier failed for pass '{}' draw {}.", pass_name, draw_index);
					success = false;
					break;
				}
			}
			if (success &&
				pass_context->m_desc.depth_attachment.render_target &&
				pass_context->m_desc.depth_attachment.read_only &&
				!m_render_device->collect_depth_attachment_barrier(pass_context->m_desc.depth_attachment, pass_barrier_scratch))
			{
				HLogError("Renderer: collect_depth_attachment_barrier failed for pass '{}'.", pass_name);
				success = false;
			}
			if (success && !m_render_device->submit_resource_barriers(pass_barrier_scratch))
			{
				HLogError("Renderer: submit pass resource barriers failed for pass '{}'.", pass_name);
				success = false;
			}
		}

		if (success)
		{
			ASH_PROFILE_SCOPE_NC("Renderer::BeginPass", AshEngine::Profile::Color::Pipeline);
			success = m_render_device->begin_pass(pass_context->m_desc);
			pass_started = success;
			if (success)
			{
				++m_frame_stats.graphics_pass_count;
			}
		}

		if (success)
		{
			ASH_PROFILE_SCOPE_NC("Renderer::SubmitDraws", AshEngine::Profile::Color::Draw);
			ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(pass_context->m_draw_calls.size()));
			for (size_t draw_index = 0; draw_index < pass_context->m_draw_calls.size(); ++draw_index)
			{
				const GraphicsDrawDesc& draw_desc = pass_context->m_draw_calls[draw_index];
				if (draw_desc.const_data_size > 0)
				{
					ASH_PROFILE_SCOPE_NC("Renderer::SetDrawConstants", AshEngine::Profile::Color::Submit);
					const uint8_t* const_data = nullptr;
					if (!draw_desc.const_data.empty())
					{
						const_data = draw_desc.const_data.size() >= draw_desc.const_data_size ? draw_desc.const_data.data() : nullptr;
					}
					else if (draw_desc.inline_const_data_valid &&
						draw_desc.const_data_size <= GraphicsDrawDesc::InlineConstDataCapacity)
					{
						const_data = draw_desc.inline_const_data.data();
					}
					if (!const_data || !draw_desc.program->set_const_data_block(draw_desc.const_data_size, const_data))
					{
						HLogError("Renderer: set_const_data_block failed for pass '{}' draw {}.", pass_name, draw_index);
						success = false;
						break;
					}
				}

				if (!m_render_device->bind_graphics_program(draw_desc.program, draw_desc.reverse_z))
				{
					HLogError("Renderer: bind_graphics_program failed for pass '{}' draw {}.", pass_name, draw_index);
					success = false;
					break;
				}

				for (size_t binding_index = 0; binding_index < draw_desc.vertex_buffers.size(); ++binding_index)
				{
					const VertexBufferBinding& binding = draw_desc.vertex_buffers[binding_index];
					if (!binding.buffer || !m_render_device->bind_vertex_buffer(binding.slot, binding.buffer, binding.offset))
					{
						HLogError("Renderer: bind_vertex_buffer failed for pass '{}' draw {} slot {}.", pass_name, draw_index, binding.slot);
						success = false;
						break;
					}
				}
				if (!success)
				{
					break;
				}

				if (draw_desc.has_viewport)
				{
					m_render_device->set_viewport(draw_desc.viewport);
				}
				if (draw_desc.has_scissor)
				{
					m_render_device->set_scissor(draw_desc.scissor);
				}

				if (draw_desc.indirect_args_buffer)
				{
					if (!m_render_device->draw_indirect(draw_desc.indirect_args_buffer, draw_desc.indirect_args_offset))
					{
						HLogError("Renderer: draw_indirect failed for pass '{}' draw {}.", pass_name, draw_index);
						success = false;
						break;
					}
					++m_frame_stats.draw_call_count;
					continue;
				}

				if (draw_desc.index_buffer)
				{
					if (!m_render_device->bind_index_buffer(draw_desc.index_buffer, draw_desc.index_buffer_offset))
					{
						HLogError("Renderer: bind_index_buffer failed for pass '{}' draw {}.", pass_name, draw_index);
						success = false;
						break;
					}
					m_render_device->draw_indexed(draw_desc.index_count, draw_desc.instance_count, draw_desc.first_index, draw_desc.vertex_offset, draw_desc.first_instance);
					++m_frame_stats.draw_call_count;
					continue;
				}

				m_render_device->draw(draw_desc.vertex_count, draw_desc.instance_count, draw_desc.first_vertex, draw_desc.first_instance);
				++m_frame_stats.draw_call_count;
			}
		}

		if (pass_started)
		{
			success = m_render_device->end_pass() && success;
		}

		if (!success)
		{
			HLogError("Renderer failed to submit graphics pass '{}'.", pass_name);
		}

		pass_context->m_draw_calls.clear();
		pass_context->m_desc = PassDesc{};
		pass_context->m_active = false;
		pass_context->m_renderer = nullptr;
		m_active_pass = nullptr;
	}
}
