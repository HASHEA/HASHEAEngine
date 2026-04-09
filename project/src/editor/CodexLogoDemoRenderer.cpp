#include "CodexLogoDemoRenderer.h"
#include "Function/Application.h"
#include "Function/Render/Renderer.h"
#include "Base/hlog.h"
#include <filesystem>

namespace AshEditor
{
	namespace
	{
		static const char* k_demo_shader_path = "project/src/editor/Shaders/CodexLogoComputeDemo.hlsl";
		static bool g_logged_demo_submit = false;

		struct PaletteColor
		{
			float value[4];
		};

		static const PaletteColor k_palette[] =
		{
			{ 0.033f, 0.050f, 0.090f, 1.0f },
			{ 0.020f, 0.110f, 0.120f, 1.0f },
			{ 0.070f, 0.910f, 0.780f, 1.0f },
			{ 0.760f, 1.000f, 0.960f, 1.0f }
		};
	}

	bool CodexLogoDemoRenderer::init()
	{
		if (m_initialized)
		{
			return true;
		}

		if (!std::filesystem::exists(k_demo_shader_path))
		{
			HLogError("Codex logo demo shader not found: {}", k_demo_shader_path);
			return false;
		}

		auto* renderer = AshEngine::Application::get_renderer();
		if (!renderer)
		{
			return false;
		}
		m_graphics_program = renderer->create_graphics_program({
			k_demo_shader_path,
			"VSMain",
			"PSMain",
			nullptr,
			{ AshEngine::RenderCullMode::None, AshEngine::RenderPrimitiveTopology::TriangleStrip, false, false },
			"CodexLogoGraphicsProgram"
		});
		if (!m_graphics_program)
		{
			HLogError("Codex logo demo failed to create graphics program.");
			return false;
		}

		m_compute_program = renderer->create_compute_program({
			k_demo_shader_path,
			"CSMain",
			nullptr,
			"CodexLogoComputeProgram"
		});
		if (!m_compute_program)
		{
			HLogError("Codex logo demo failed to create compute program.");
			return false;
		}

		m_palette_buffer = renderer->create_storage_buffer({
			static_cast<uint32_t>(sizeof(k_palette)),
			static_cast<uint32_t>(sizeof(PaletteColor)),
			false,
			k_palette,
			"CodexLogoPaletteBuffer"
		});
		if (!m_palette_buffer)
		{
			HLogError("Codex logo demo failed to create palette buffer.");
			return false;
		}

		m_back_buffer = renderer->get_back_buffer();
		m_initialized = m_back_buffer != nullptr;
		return m_initialized;
	}

	void CodexLogoDemoRenderer::shutdown()
	{
		m_graphics_program.reset();
		m_compute_program.reset();
		m_palette_buffer.reset();
		m_compute_target.reset();
		m_back_buffer.reset();
		m_initialized = false;
	}

	bool CodexLogoDemoRenderer::ensure_compute_resources(AshEngine::Renderer* renderer, uint32_t width, uint32_t height)
	{
		if (!renderer || width == 0 || height == 0)
		{
			HLogError("Codex logo demo received invalid compute target extent {}x{}.", width, height);
			return false;
		}

		if (m_compute_target &&
			m_compute_target->get_width() == width &&
			m_compute_target->get_height() == height)
		{
			return true;
		}

		m_compute_target = renderer->create_render_target({
			static_cast<uint16_t>(width),
			static_cast<uint16_t>(height),
			AshEngine::RenderTextureFormat::RGBA32_SFLOAT,
			true,
			true,
			"CodexLogoComputeTarget"
		});
		if (!m_compute_target)
		{
			HLogError("Codex logo demo failed to create compute target.");
			return false;
		}

		if (!m_compute_program->set_rw_texture("OutputTexture", m_compute_target))
		{
			HLogError("Codex logo demo failed to bind compute OutputTexture.");
			return false;
		}
		if (!m_compute_program->set_storage_buffer("PaletteBuffer", m_palette_buffer))
		{
			HLogError("Codex logo demo failed to bind compute PaletteBuffer.");
			return false;
		}
		if (!m_graphics_program->set_texture("LogoTexture", m_compute_target))
		{
			HLogError("Codex logo demo failed to bind graphics LogoTexture.");
			return false;
		}
		return true;
	}

	bool CodexLogoDemoRenderer::render()
	{
		if (!init())
		{
			HLogError("Codex logo demo init failed.");
			return false;
		}

		auto* renderer = AshEngine::Application::get_renderer();
		if (!renderer)
		{
			HLogError("Codex logo demo could not fetch renderer.");
			return false;
		}
		m_back_buffer = renderer->get_back_buffer();
		if (!m_back_buffer)
		{
			HLogError("Codex logo demo could not fetch back buffer.");
			return false;
		}
		if (!ensure_compute_resources(renderer, m_back_buffer->get_width(), m_back_buffer->get_height()))
		{
			HLogError("Codex logo demo failed to prepare compute resources.");
			return false;
		}

		AshEngine::ComputeDispatchDesc dispatch_desc{};
		dispatch_desc.program = m_compute_program.get();
		dispatch_desc.group_count_x = (m_compute_target->get_width() + 7) / 8;
		dispatch_desc.group_count_y = (m_compute_target->get_height() + 7) / 8;
		dispatch_desc.group_count_z = 1;
		if (!renderer->dispatch(dispatch_desc))
		{
			HLogError("Codex logo demo compute dispatch failed.");
			return false;
		}

		AshEngine::PassDesc pass_desc{};
		pass_desc.name = "CodexLogoDemoPass";
		pass_desc.color_attachments.push_back({
			m_back_buffer,
			AshEngine::RenderLoadAction::Clear,
			{ 0.02f, 0.04f, 0.07f, 1.0f }
		});

		AshEngine::Renderer::GraphicsPassContext pass_context;
		if (!renderer->begin_pass(pass_desc, pass_context))
		{
			HLogError("Codex logo demo failed to begin graphics pass.");
			return false;
		}

		AshEngine::GraphicsDrawDesc draw_desc{};
		draw_desc.program = m_graphics_program.get();
		draw_desc.vertex_count = 4;
		if (!pass_context.draw(draw_desc))
		{
			HLogError("Codex logo demo failed to enqueue fullscreen draw.");
			pass_context.end();
			return false;
		}

		pass_context.end();
		if (!g_logged_demo_submit)
		{
			HLogInfo("Codex logo demo submitted compute + fullscreen draw.");
			g_logged_demo_submit = true;
		}
		return true;
	}
}
