#pragma once

#include "Base/hcore.h"

#include <memory>

namespace RHI
{
	class Texture;
	class TextureView;
}

namespace AshEngine
{
	// Owning carrier for a UI-displayable GPU texture created via UIContext::create_ui_texture_rgba8.
	// TextureView only weak-references its parent Texture, so both must be held together.
	class ASH_API UITexture
	{
	public:
		UITexture() = default;

	private:
		friend class UIContext;

		std::shared_ptr<RHI::Texture> texture{};
		std::shared_ptr<RHI::TextureView> texture_view{};
	};
}
