#include "Function/Application.h"
#include "Function/Render/Renderer.h"
#include "doctest.h"

#include <vector>

namespace
{
	class LogicInputProbeApplication final : public AshEngine::Application
	{
	public:
		LogicInputProbeApplication()
			: AshEngine::Application(make_config())
		{
			logicThreadEnabled = true;
			REQUIRE(scenePresentation.initialize(&probeRenderer, &renderAssetManager, &sceneRenderer));
		}

		auto publish_frame(const AshEngine::InputState& frame) -> void
		{
			inputState = frame;
			_publish_logic_input_snapshot();
		}

		auto consume_pending() -> AshEngine::InputState
		{
			_consume_logic_input_snapshot();
			return logicInputState;
		}

		auto run_two_logic_ticks() -> void
		{
			observed_pressed.clear();
			observed_released.clear();
			observed_scroll_y.clear();
			logicThreadStopRequested.store(false, std::memory_order_release);
			_logic_thread_main();
		}

		auto publish_on_startup(const AshEngine::InputState& frame) -> void
		{
			startup_frame = frame;
			should_publish_on_startup = true;
		}

		std::vector<bool> observed_pressed{};
		std::vector<bool> observed_released{};
		std::vector<double> observed_scroll_y{};

	private:
		AshEngine::Renderer probeRenderer{ nullptr };
		AshEngine::InputState startup_frame{};
		bool should_publish_on_startup = false;

		static auto make_config() -> AshEngine::EngineInitConfig
		{
			AshEngine::EngineInitConfig config{};
			config.threading.logic_thread_idle_sleep_ms = 0;
			return config;
		}

		auto _on_logic_startup() -> void override
		{
			if (should_publish_on_startup)
			{
				publish_frame(startup_frame);
			}
		}

		auto _on_logic_update() -> void override
		{
			const AshEngine::InputState& input = AshEngine::Application::get_input();
			observed_pressed.push_back(input.was_key_pressed(17));
			observed_released.push_back(input.was_key_released(17));
			observed_scroll_y.push_back(input.get_scroll_y());
			if (observed_pressed.size() >= 2u)
			{
				logicThreadStopRequested.store(true, std::memory_order_release);
			}
		}
	};
}

TEST_CASE("Application logic mailbox merges render frames that have not been consumed")
{
	LogicInputProbeApplication application{};
	AshEngine::InputState render_input{};
	render_input.set_key_state(17, true, false);
	render_input.add_scroll_delta(0.0, 1.0);
	application.publish_frame(render_input);

	render_input.begin_frame();
	render_input.set_key_state(17, false, false);
	render_input.add_scroll_delta(0.0, 2.0);
	application.publish_frame(render_input);

	const AshEngine::InputState logic_input = application.consume_pending();
	CHECK_FALSE(logic_input.is_key_down(17));
	CHECK(logic_input.was_key_pressed(17));
	CHECK(logic_input.was_key_released(17));
	CHECK(logic_input.get_scroll_y() == doctest::Approx(3.0));
}

TEST_CASE("Application logic loop exposes one pressed edge to only one update")
{
	LogicInputProbeApplication application{};
	AshEngine::InputState render_input{};
	render_input.set_key_state(17, true, false);
	application.publish_frame(render_input);

	application.run_two_logic_ticks();

	REQUIRE(application.observed_pressed.size() == 2u);
	CHECK(application.observed_pressed[0]);
	CHECK_FALSE(application.observed_pressed[1]);
}

TEST_CASE("Application logic startup does not consume or discard pending transients")
{
	LogicInputProbeApplication application{};
	AshEngine::InputState render_input{};
	render_input.set_key_state(17, true, false);
	render_input.add_scroll_delta(0.0, 1.0);
	application.publish_frame(render_input);

	render_input.begin_frame();
	render_input.set_key_state(17, false, false);
	render_input.add_scroll_delta(0.0, 2.0);
	application.publish_on_startup(render_input);

	application.run_two_logic_ticks();

	REQUIRE(application.observed_pressed.size() == 2u);
	CHECK(application.observed_pressed[0]);
	CHECK(application.observed_released[0]);
	CHECK(application.observed_scroll_y[0] == doctest::Approx(3.0));
	CHECK_FALSE(application.observed_pressed[1]);
	CHECK_FALSE(application.observed_released[1]);
	CHECK(application.observed_scroll_y[1] == doctest::Approx(0.0));
}
