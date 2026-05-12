#pragma once

#include "Base/hcore.h"
#include "Function/Render/GBufferLayout.h"
#include <memory>
#include <vector>

namespace AshEngine
{
	class Renderer;
	class RenderTarget;

	class SceneDeferredResources
	{
	public:
		bool ensure(Renderer& renderer, uint32_t width, uint32_t height, const GBufferLayoutDesc& layout);
		void reset();

		const GBufferLayoutDesc* get_layout() const;
		const std::vector<std::shared_ptr<RenderTarget>>& get_gbuffer_targets() const;
		const std::shared_ptr<RenderTarget>& get_depth_target() const;

	private:
		uint32_t m_width = 0;
		uint32_t m_height = 0;
		uint64_t m_layout_hash = 0;
		const GBufferLayoutDesc* m_layout = nullptr;
		std::vector<std::shared_ptr<RenderTarget>> m_gbuffer_targets{};
		std::shared_ptr<RenderTarget> m_depth_target = nullptr;
	};
}
