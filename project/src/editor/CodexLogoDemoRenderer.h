#pragma once
#include "Function/Render/Renderer.h"
#include <memory>

namespace AshEditor
{
	class CodexLogoDemoRenderer
	{
	public:
		CodexLogoDemoRenderer() = default;
		~CodexLogoDemoRenderer() = default;

	public:
		bool init();
		void shutdown();
		bool render();

	private:
		bool ensure_compute_resources(AshEngine::Renderer* renderer, uint32_t width, uint32_t height);

	private:
		bool m_initialized = false;
		std::shared_ptr<AshEngine::RenderTarget> m_back_buffer = nullptr;
		std::shared_ptr<AshEngine::RenderTarget> m_compute_target = nullptr;
		std::shared_ptr<AshEngine::StorageBuffer> m_palette_buffer = nullptr;
		std::unique_ptr<AshEngine::GraphicsProgram> m_graphics_program = nullptr;
		std::unique_ptr<AshEngine::ComputeProgram> m_compute_program = nullptr;
	};
}
