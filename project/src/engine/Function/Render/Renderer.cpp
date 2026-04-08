#include "Renderer.h"

namespace AshEngine
{
	Renderer::Renderer(RenderDevice* render_device)
		: m_render_device(render_device)
	{
	}

	Renderer::~Renderer() = default;

	bool Renderer::begin_frame()
	{
		return m_render_device && m_render_device->begin_frame();
	}

	bool Renderer::end_frame()
	{
		return m_render_device && m_render_device->end_frame();
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

	std::unique_ptr<GraphicsProgram> Renderer::create_graphics_program(const GraphicsProgramDesc& desc)
	{
		return m_render_device ? m_render_device->create_graphics_program(desc) : nullptr;
	}

	std::unique_ptr<ComputeProgram> Renderer::create_compute_program(const ComputeProgramDesc& desc)
	{
		return m_render_device ? m_render_device->create_compute_program(desc) : nullptr;
	}

	bool Renderer::begin_pass(const PassDesc& desc)
	{
		return m_render_device && m_render_device->begin_pass(desc);
	}

	void Renderer::end_pass()
	{
		if (m_render_device)
		{
			m_render_device->end_pass();
		}
	}

	bool Renderer::draw(const GraphicsDrawDesc& desc)
	{
		if (!m_render_device || !desc.program)
		{
			return false;
		}
		if (!m_render_device->bind_graphics_program(desc.program))
		{
			return false;
		}
		for (const VertexBufferBinding& binding : desc.vertex_buffers)
		{
			if (!binding.buffer || !m_render_device->bind_vertex_buffer(binding.slot, binding.buffer, binding.offset))
			{
				return false;
			}
		}
		if (desc.has_viewport)
		{
			m_render_device->set_viewport(desc.viewport);
		}
		if (desc.has_scissor)
		{
			m_render_device->set_scissor(desc.scissor);
		}
		if (desc.index_buffer)
		{
			if (!m_render_device->bind_index_buffer(desc.index_buffer, desc.index_buffer_offset))
			{
				return false;
			}
			m_render_device->draw_indexed(desc.index_count, desc.instance_count, desc.first_index, desc.vertex_offset, desc.first_instance);
			return true;
		}
		m_render_device->draw(desc.vertex_count, desc.instance_count, desc.first_vertex, desc.first_instance);
		return true;
	}

	bool Renderer::dispatch(const ComputeDispatchDesc& desc)
	{
		if (!m_render_device || !desc.program)
		{
			return false;
		}
		if (!m_render_device->bind_compute_program(desc.program))
		{
			return false;
		}
		m_render_device->dispatch(desc.group_count_x, desc.group_count_y, desc.group_count_z);
		return true;
	}
}
