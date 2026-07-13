#include "App/SandboxFreeCameraController.h"
#include "doctest.h"

namespace
{
	struct CameraFixture
	{
		CameraFixture()
		{
			camera = scene.create_entity("Camera");
			REQUIRE(camera.is_valid());
			REQUIRE(camera.add_camera_component(AshEngine::CameraComponent{}));
			controller.bind_camera_entity(camera.get_id());
		}

		AshEngine::Scene scene = AshEngine::Scene::create("Sandbox Free Camera Test");
		AshEngine::Entity camera{};
		AshSandbox::SandboxFreeCameraController controller{};
	};
}

TEST_CASE("Sandbox free camera leaves the scene transform version unchanged while idle")
{
	CameraFixture fixture{};
	AshEngine::InputState input{};
	std::string error{};
	const uint64_t transform_version_before = fixture.scene.get_render_transform_version();

	REQUIRE(fixture.controller.update(fixture.scene, input, 1.0 / 60.0, error));

	CHECK(error.empty());
	CHECK(fixture.scene.get_render_transform_version() == transform_version_before);
}

TEST_CASE("Sandbox free camera applies one scroll batch once")
{
	CameraFixture fixture{};
	AshEngine::InputState input{};
	std::string error{};
	const uint64_t transform_version_before = fixture.scene.get_render_transform_version();
	input.add_scroll_delta(0.0, 1.0);

	REQUIRE(fixture.controller.update(fixture.scene, input, 1.0 / 60.0, error));
	CHECK(fixture.controller.get_move_speed() == doctest::Approx(10.0f));
	CHECK(fixture.scene.get_render_transform_version() == transform_version_before);

	input.clear_transient_state();
	REQUIRE(fixture.controller.update(fixture.scene, input, 1.0 / 60.0, error));
	CHECK(fixture.controller.get_move_speed() == doctest::Approx(10.0f));
	CHECK(fixture.scene.get_render_transform_version() == transform_version_before);
}
