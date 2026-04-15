#include "Demos/CodexLogoDemoRenderer.h"

#include "Base/hlog.h"
#include "Function/Application.h"
#include "Function/Render/Renderer.h"
#include <filesystem>

namespace AshSandbox
{
	namespace
	{
		static const char* k_demo_shader_path = "project/src/sandbox/Shaders/CodexLogoComputeDemo.hlsl";

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
			HLogError("Sandbox Codex logo shader not found: {}", k_demo_shader_path);
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
			"SandboxCodexLogoGraphicsProgram"
		});
		if (!m_graphics_program)
		{
			HLogError("Sandbox Codex logo failed to create graphics program.");
			return false;
		}

		m_compute_program = renderer->create_compute_program({
			k_demo_shader_path,
			"CSMain",
			nullptr,
			"SandboxCodexLogoComputeProgram"
		});
		if (!m_compute_program)
		{
			HLogError("Sandbox Codex logo failed to create compute program.");
			return false;
		}

		m_palette_buffer = renderer->create_storage_buffer({
			static_cast<uint32_t>(sizeof(k_palette)),
			static_cast<uint32_t>(sizeof(PaletteColor)),
			false,
			k_palette,
			"SandboxCodexLogoPaletteBuffer"
		});
		if (!m_palette_buffer)
		{
			HLogError("Sandbox Codex logo failed to create palette buffer.");
			return false;
		}

		m_initialized = true;
		return true;
	}

	void CodexLogoDemoRenderer::shutdown()
	{
		m_graphics_program.reset();
		m_compute_program.reset();
		m_palette_buffer.reset();
		m_compute_target.reset();
		m_initialized = false;
	}

	bool CodexLogoDemoRenderer::ensure_compute_resources(AshEngine::Renderer* renderer, uint32_t width, uint32_t height)
	{
		if (!renderer || width == 0 || height == 0)
		{
			HLogError("Sandbox Codex logo received invalid compute extent {}x{}.", width, height);
			return false;
		}

		if (m_compute_target &&
			m_compute_target->get_width() == width &&
			m_compute_target->get_height() == height)
		{
			return true;
		}

		AshEngine::RenderTargetDesc computeTargetDesc{};
		computeTargetDesc.width = static_cast<uint16_t>(width);
		computeTargetDesc.height = static_cast<uint16_t>(height);
		computeTargetDesc.format = AshEngine::RenderTextureFormat::RGBA32_SFLOAT;
		computeTargetDesc.shader_resource = true;
		computeTargetDesc.unordered_access = true;
		computeTargetDesc.name = "SandboxCodexLogoComputeTarget";
		computeTargetDesc.use_optimized_clear_value = true;
		computeTargetDesc.optimized_clear_color = { 0.02f, 0.04f, 0.07f, 1.0f };

		m_compute_target = renderer->create_render_target(computeTargetDesc);
		if (!m_compute_target)
		{
			HLogError("Sandbox Codex logo failed to create compute target.");
			return false;
		}

		if (!m_compute_program->set_rw_texture("OutputTexture", m_compute_target))
		{
			HLogError("Sandbox Codex logo failed to bind OutputTexture.");
			return false;
		}
		if (!m_compute_program->set_storage_buffer("PaletteBuffer", m_palette_buffer))
		{
			HLogError("Sandbox Codex logo failed to bind PaletteBuffer.");
			return false;
		}
		if (!m_graphics_program->set_texture("LogoTexture", m_compute_target))
		{
			HLogError("Sandbox Codex logo failed to bind LogoTexture.");
			return false;
		}
		return true;
	}

	bool CodexLogoDemoRenderer::render(const std::shared_ptr<AshEngine::RenderTarget>& output_target)
	{
		if (!init())
		{
			return false;
		}

		auto* renderer = AshEngine::Application::get_renderer();
		if (!renderer)
		{
			HLogError("Sandbox Codex logo could not fetch renderer.");
			return false;
		}
		if (!output_target)
		{
			HLogError("Sandbox Codex logo missing output target.");
			return false;
		}
		if (!ensure_compute_resources(renderer, output_target->get_width(), output_target->get_height()))
		{
			return false;
		}

		AshEngine::ComputeDispatchDesc dispatchDesc{};
		dispatchDesc.program = m_compute_program.get();
		dispatchDesc.group_count_x = (m_compute_target->get_width() + 7) / 8;
		dispatchDesc.group_count_y = (m_compute_target->get_height() + 7) / 8;
		dispatchDesc.group_count_z = 1;
		if (!renderer->dispatch(dispatchDesc))
		{
			HLogError("Sandbox Codex logo compute dispatch failed.");
			return false;
		}

		AshEngine::PassDesc passDesc{};
		passDesc.name = "SandboxCodexLogoPass";
		passDesc.color_attachments.push_back({
			output_target,
			AshEngine::RenderLoadAction::DontCare,
			{ 0.02f, 0.04f, 0.07f, 1.0f }
		});

		AshEngine::Renderer::GraphicsPassContext passContext;
		if (!renderer->begin_pass(passDesc, passContext))
		{
			HLogError("Sandbox Codex logo failed to begin graphics pass.");
			return false;
		}

		AshEngine::GraphicsDrawDesc drawDesc{};
		drawDesc.program = m_graphics_program.get();
		drawDesc.vertex_count = 4;
		if (!passContext.draw(drawDesc))
		{
			HLogError("Sandbox Codex logo failed to enqueue fullscreen draw.");
			passContext.end();
			return false;
		}

		passContext.end();
		return true;
	}
}
