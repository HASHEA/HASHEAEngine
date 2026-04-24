#include "Renderer.h"
#include "Base/hlog.h"
#include "Function/Application.h"
#include "Function/Gui/UIContext.h"

namespace AshEngine
{
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

	bool Renderer::begin_frame()
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_render_device && m_render_device->begin_frame());

		m_frame_stats = {};
		m_frame_start_time = std::chrono::steady_clock::now();
		m_frame_in_progress = true;
		if (std::shared_ptr<RenderTarget> back_buffer = get_back_buffer())
		{
			m_frame_stats.frame_width = back_buffer->get_width();
			m_frame_stats.frame_height = back_buffer->get_height();
		}
		ASH_PROCESS_GUARD_END(bResult, false);
		if (!bResult)
		{
			m_frame_in_progress = false;
		}
		return bResult;
	}

	bool Renderer::end_frame()
	{
		if (m_active_pass)
		{
			end_active_pass(m_active_pass);
		}

		if (m_frame_in_progress)
		{
			const auto frame_end_time = std::chrono::steady_clock::now();
			m_frame_stats.cpu_frame_time_ms = std::chrono::duration<double, std::milli>(frame_end_time - m_frame_start_time).count();
			m_frame_stats.instantaneous_fps =
				m_frame_stats.cpu_frame_time_ms > 0.0 ? (1000.0 / m_frame_stats.cpu_frame_time_ms) : 0.0;
			update_frame_timing_history(m_frame_stats.cpu_frame_time_ms);
			m_last_completed_frame_stats = m_frame_stats;
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
		if (m_frame_in_progress)
		{
			m_frame_in_progress = false;
		}
		return result && ui_result;
	}

	void Renderer::present()
	{
		if (m_render_device)
		{
			m_render_device->present();
		}
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

	void Renderer::end_active_pass(GraphicsPassContext* pass_context)
	{
		if (!pass_context || pass_context != m_active_pass)
		{
			return;
		}

		bool pass_started = false;
		bool success = m_render_device != nullptr;
		const char* pass_name = pass_context->m_desc.name ? pass_context->m_desc.name : "UnnamedPass";
		if (success)
		{
			for (size_t draw_index = 0; draw_index < pass_context->m_draw_calls.size(); ++draw_index)
			{
				const GraphicsDrawDesc& draw_desc = pass_context->m_draw_calls[draw_index];
				if (!draw_desc.program || !m_render_device->transition_graphics_program_resources(draw_desc.program))
				{
					HLogError("Renderer: transition_graphics_program_resources failed for pass '{}' draw {}.", pass_name, draw_index);
					success = false;
					break;
				}

				for (const VertexBufferBinding& binding : draw_desc.vertex_buffers)
				{
					if (!binding.buffer || !m_render_device->transition_vertex_buffer(binding.buffer))
					{
						HLogError("Renderer: transition_vertex_buffer failed for pass '{}' draw {} slot {}.", pass_name, draw_index, binding.slot);
						success = false;
						break;
					}
				}
				if (!success)
				{
					break;
				}

				if (draw_desc.index_buffer && !m_render_device->transition_index_buffer(draw_desc.index_buffer))
				{
					HLogError("Renderer: transition_index_buffer failed for pass '{}' draw {}.", pass_name, draw_index);
					success = false;
					break;
				}
			}
		}

		if (success)
		{
			success = m_render_device->begin_pass(pass_context->m_desc);
			pass_started = success;
			if (success)
			{
				++m_frame_stats.graphics_pass_count;
			}
		}

		if (success)
		{
			for (size_t draw_index = 0; draw_index < pass_context->m_draw_calls.size(); ++draw_index)
			{
				const GraphicsDrawDesc& draw_desc = pass_context->m_draw_calls[draw_index];
				if (draw_desc.const_data_size > 0)
				{
					if (draw_desc.const_data.size() < draw_desc.const_data_size || !draw_desc.program->set_const_data_block(draw_desc.const_data_size, draw_desc.const_data.data()))
					{
						HLogError("Renderer: set_const_data_block failed for pass '{}' draw {}.", pass_name, draw_index);
						success = false;
						break;
					}
				}

				if (!m_render_device->bind_graphics_program(draw_desc.program))
				{
					HLogError("Renderer: bind_graphics_program failed for pass '{}' draw {}.", pass_name, draw_index);
					success = false;
					break;
				}

				for (const VertexBufferBinding& binding : draw_desc.vertex_buffers)
				{
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
			m_render_device->end_pass();
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
