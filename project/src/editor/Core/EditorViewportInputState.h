#pragma once

#include "Function/Gui/UICommon.h"

#include <array>
#include <cstddef>

namespace AshEditor
{
	struct EditorViewportInputState
	{
		static constexpr size_t kMouseButtonCount = 3u;
		static constexpr size_t kKeyCount = static_cast<size_t>(AshEngine::UIKey::PageDown) + 1u;

		AshEngine::UIVec2 vecMouseScreenPosition{};
		AshEngine::UIVec2 vecMouseWheelDelta{};
		AshEngine::UIModifierFlags uModifiers = AshEngine::UIModifierFlagBits::None;
		std::array<bool, kMouseButtonCount> arrMouseDown{};
		std::array<bool, kMouseButtonCount> arrMousePressed{};
		std::array<bool, kMouseButtonCount> arrMouseReleased{};
		std::array<bool, kKeyCount> arrKeyDown{};
		std::array<bool, kKeyCount> arrKeyPressed{};

		bool IsMouseDown(AshEngine::UIMouseButton button) const
		{
			return arrMouseDown[ToMouseIndex(button)];
		}

		bool WasMousePressed(AshEngine::UIMouseButton button) const
		{
			return arrMousePressed[ToMouseIndex(button)];
		}

		bool WasMouseReleased(AshEngine::UIMouseButton button) const
		{
			return arrMouseReleased[ToMouseIndex(button)];
		}

		bool IsKeyDown(AshEngine::UIKey key) const
		{
			return arrKeyDown[ToKeyIndex(key)];
		}

		bool WasKeyPressed(AshEngine::UIKey key) const
		{
			return arrKeyPressed[ToKeyIndex(key)];
		}

		bool IsModifierDown(AshEngine::UIModifierFlags modifiers) const
		{
			return modifiers != AshEngine::UIModifierFlagBits::None && (uModifiers & modifiers) == modifiers;
		}

	private:
		static constexpr size_t ToMouseIndex(AshEngine::UIMouseButton button)
		{
			return static_cast<size_t>(button);
		}

		static constexpr size_t ToKeyIndex(AshEngine::UIKey key)
		{
			return static_cast<size_t>(key);
		}
	};
}
