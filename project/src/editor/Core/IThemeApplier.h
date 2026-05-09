#pragma once

#include <cstdint>

namespace AshEngine
{
	enum class UIThemePreset : uint8_t;
}

namespace AshEditor
{
	class IThemeApplier
	{
	public:
		virtual ~IThemeApplier() = default;

		virtual void ApplyThemePreset(AshEngine::UIThemePreset preset) = 0;
	};
}
