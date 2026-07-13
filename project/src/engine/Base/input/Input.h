#pragma once
#include "Base/hcore.h"
#include <array>
#include <cstdint>

namespace AshEngine
{
	constexpr uint32_t ASH_MAX_KEY_CODE = 512;
	constexpr uint32_t ASH_MAX_MOUSE_BUTTON_COUNT = 8;

	class ASH_API InputState
	{
	public:
		auto begin_frame() -> void
		{
			clear_transient_state();
		}

		auto clear_transient_state() -> void
		{
			keyPressed.fill(false);
			keyReleased.fill(false);
			mouseButtonPressed.fill(false);
			mouseButtonReleased.fill(false);
			scrollX = 0.0;
			scrollY = 0.0;
		}

		auto merge_frame_snapshot(const InputState& newer) -> void
		{
			for (uint32_t keyIndex = 0; keyIndex < ASH_MAX_KEY_CODE; ++keyIndex)
			{
				keyDown[keyIndex] = newer.keyDown[keyIndex];
				keyPressed[keyIndex] = keyPressed[keyIndex] || newer.keyPressed[keyIndex];
				keyReleased[keyIndex] = keyReleased[keyIndex] || newer.keyReleased[keyIndex];
			}

			for (uint32_t buttonIndex = 0; buttonIndex < ASH_MAX_MOUSE_BUTTON_COUNT; ++buttonIndex)
			{
				mouseButtonDown[buttonIndex] = newer.mouseButtonDown[buttonIndex];
				mouseButtonPressed[buttonIndex] =
					mouseButtonPressed[buttonIndex] || newer.mouseButtonPressed[buttonIndex];
				mouseButtonReleased[buttonIndex] =
					mouseButtonReleased[buttonIndex] || newer.mouseButtonReleased[buttonIndex];
			}

			mouseX = newer.mouseX;
			mouseY = newer.mouseY;
			scrollX += newer.scrollX;
			scrollY += newer.scrollY;
		}

		auto set_key_state(int32_t key, bool isDown, bool repeated) -> void
		{
			if (!is_valid_key(key))
			{
				return;
			}

			const uint32_t keyIndex = static_cast<uint32_t>(key);
			if (isDown)
			{
				if (!keyDown[keyIndex])
				{
					keyPressed[keyIndex] = true;
				}
				else if (!repeated)
				{
					keyPressed[keyIndex] = false;
				}
			}
			else if (keyDown[keyIndex])
			{
				keyReleased[keyIndex] = true;
			}

			keyDown[keyIndex] = isDown;
		}

		auto set_mouse_button_state(int32_t button, bool isDown) -> void
		{
			if (!is_valid_mouse_button(button))
			{
				return;
			}

			const uint32_t buttonIndex = static_cast<uint32_t>(button);
			if (isDown)
			{
				if (!mouseButtonDown[buttonIndex])
				{
					mouseButtonPressed[buttonIndex] = true;
				}
			}
			else if (mouseButtonDown[buttonIndex])
			{
				mouseButtonReleased[buttonIndex] = true;
			}

			mouseButtonDown[buttonIndex] = isDown;
		}

		auto set_mouse_position(double newX, double newY) -> void
		{
			mouseX = newX;
			mouseY = newY;
		}

		auto add_scroll_delta(double deltaX, double deltaY) -> void
		{
			scrollX += deltaX;
			scrollY += deltaY;
		}

		auto is_key_down(int32_t key) const -> bool
		{
			return is_valid_key(key) ? keyDown[static_cast<uint32_t>(key)] : false;
		}

		auto was_key_pressed(int32_t key) const -> bool
		{
			return is_valid_key(key) ? keyPressed[static_cast<uint32_t>(key)] : false;
		}

		auto was_key_released(int32_t key) const -> bool
		{
			return is_valid_key(key) ? keyReleased[static_cast<uint32_t>(key)] : false;
		}

		auto is_mouse_button_down(int32_t button) const -> bool
		{
			return is_valid_mouse_button(button) ? mouseButtonDown[static_cast<uint32_t>(button)] : false;
		}

		auto was_mouse_button_pressed(int32_t button) const -> bool
		{
			return is_valid_mouse_button(button) ? mouseButtonPressed[static_cast<uint32_t>(button)] : false;
		}

		auto was_mouse_button_released(int32_t button) const -> bool
		{
			return is_valid_mouse_button(button) ? mouseButtonReleased[static_cast<uint32_t>(button)] : false;
		}

		auto get_mouse_x() const -> double
		{
			return mouseX;
		}

		auto get_mouse_y() const -> double
		{
			return mouseY;
		}

		auto get_scroll_x() const -> double
		{
			return scrollX;
		}

		auto get_scroll_y() const -> double
		{
			return scrollY;
		}

	private:
		inline auto is_valid_key(int32_t key) const -> bool
		{
			return key >= 0 && key < static_cast<int32_t>(ASH_MAX_KEY_CODE);
		}

		inline auto is_valid_mouse_button(int32_t button) const -> bool
		{
			return button >= 0 && button < static_cast<int32_t>(ASH_MAX_MOUSE_BUTTON_COUNT);
		}

		std::array<bool, ASH_MAX_KEY_CODE> keyDown{};
		std::array<bool, ASH_MAX_KEY_CODE> keyPressed{};
		std::array<bool, ASH_MAX_KEY_CODE> keyReleased{};
		std::array<bool, ASH_MAX_MOUSE_BUTTON_COUNT> mouseButtonDown{};
		std::array<bool, ASH_MAX_MOUSE_BUTTON_COUNT> mouseButtonPressed{};
		std::array<bool, ASH_MAX_MOUSE_BUTTON_COUNT> mouseButtonReleased{};
		double mouseX = 0.0;
		double mouseY = 0.0;
		double scrollX = 0.0;
		double scrollY = 0.0;
	};
}
