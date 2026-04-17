#pragma once

#include "Base/input/Input.h"
#include "Function/Scene/Scene.h"
#include <string>

namespace AshSandbox
{
	class SandboxFreeCameraController
	{
	public:
		SandboxFreeCameraController() = default;

	public:
		auto reset() -> void;
		auto bind_camera_entity(AshEngine::EntityId camera_entity_id) -> void;
		auto get_camera_entity_id() const -> AshEngine::EntityId
		{
			return m_camera_entity_id;
		}
		auto set_move_speed(float move_speed) -> void;
		auto get_move_speed() const -> float
		{
			return m_move_speed;
		}

		auto update(
			AshEngine::Scene& scene,
			const AshEngine::InputState& input,
			double delta_seconds,
			std::string& out_error) -> bool;

	private:
		AshEngine::EntityId m_camera_entity_id = 0;
		float m_move_speed = 8.0f;
		float m_shift_multiplier = 4.0f;
		float m_mouse_sensitivity = 0.12f;
		bool m_has_last_mouse_position = false;
		double m_last_mouse_x = 0.0;
		double m_last_mouse_y = 0.0;
	};
}
