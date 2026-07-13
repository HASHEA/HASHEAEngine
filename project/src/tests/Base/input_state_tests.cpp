#include "Base/input/Input.h"
#include "doctest.h"

TEST_CASE("InputState clears transient values while preserving continuous state")
{
	AshEngine::InputState input{};
	input.set_key_state(17, true, false);
	input.set_mouse_button_state(3, true);
	input.set_mouse_position(123.0, 456.0);
	input.add_scroll_delta(1.5, -2.0);

	input.clear_transient_state();

	CHECK(input.is_key_down(17));
	CHECK_FALSE(input.was_key_pressed(17));
	CHECK_FALSE(input.was_key_released(17));
	CHECK(input.is_mouse_button_down(3));
	CHECK_FALSE(input.was_mouse_button_pressed(3));
	CHECK_FALSE(input.was_mouse_button_released(3));
	CHECK(input.get_mouse_x() == doctest::Approx(123.0));
	CHECK(input.get_mouse_y() == doctest::Approx(456.0));
	CHECK(input.get_scroll_x() == doctest::Approx(0.0));
	CHECK(input.get_scroll_y() == doctest::Approx(0.0));
}

TEST_CASE("InputState merges unconsumed frame snapshots without losing edges")
{
	AshEngine::InputState pending{};
	AshEngine::InputState frame{};

	frame.set_key_state(17, true, false);
	frame.set_key_state(23, true, false);
	frame.set_mouse_button_state(3, true);
	frame.set_mouse_position(10.0, 20.0);
	frame.add_scroll_delta(1.5, 2.0);
	pending.merge_frame_snapshot(frame);

	frame.begin_frame();
	frame.set_key_state(17, false, false);
	frame.set_mouse_button_state(3, false);
	frame.set_mouse_position(30.0, 40.0);
	frame.add_scroll_delta(-0.5, 3.0);
	pending.merge_frame_snapshot(frame);

	CHECK_FALSE(pending.is_key_down(17));
	CHECK(pending.was_key_pressed(17));
	CHECK(pending.was_key_released(17));
	CHECK(pending.is_key_down(23));
	CHECK(pending.was_key_pressed(23));
	CHECK_FALSE(pending.was_key_released(23));
	CHECK_FALSE(pending.is_mouse_button_down(3));
	CHECK(pending.was_mouse_button_pressed(3));
	CHECK(pending.was_mouse_button_released(3));
	CHECK(pending.get_mouse_x() == doctest::Approx(30.0));
	CHECK(pending.get_mouse_y() == doctest::Approx(40.0));
	CHECK(pending.get_scroll_x() == doctest::Approx(1.0));
	CHECK(pending.get_scroll_y() == doctest::Approx(5.0));
}
