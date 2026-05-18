#pragma once

#include <string_view>

namespace AshEditor
{
	class IThemeApplier
	{
	public:
		virtual ~IThemeApplier() = default;

		virtual void ApplyTheme(std::string_view svThemeId, std::string_view svThemeLabel = {}) = 0;
	};
}
