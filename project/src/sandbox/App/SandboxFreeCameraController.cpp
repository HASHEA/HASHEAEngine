#include "App/SandboxFreeCameraController.h"

#include <algorithm>
#include <cmath>
#include <glm/gtx/euler_angles.hpp>
#include <GLFW/glfw3.h>

namespace AshSandbox
{
	namespace
	{
		static constexpr float k_default_move_speed = 8.0f;
		static constexpr float k_min_move_speed = 0.25f;
		static constexpr float k_max_move_speed = 256.0f;
		static constexpr float k_scroll_speed_scale = 1.25f;
	}

	auto SandboxFreeCameraController::reset() -> void
	{
		m_camera_entity_id = 0;
		m_move_speed = k_default_move_speed;
		m_has_last_mouse_position = false;
		m_last_mouse_x = 0.0;
		m_last_mouse_y = 0.0;
	}

	auto SandboxFreeCameraController::bind_camera_entity(AshEngine::EntityId camera_entity_id) -> void
	{
		m_camera_entity_id = camera_entity_id;
		m_has_last_mouse_position = false;
		m_last_mouse_x = 0.0;
		m_last_mouse_y = 0.0;
	}

	auto SandboxFreeCameraController::set_move_speed(float move_speed) -> void
	{
		m_move_speed = std::clamp(move_speed, k_min_move_speed, k_max_move_speed);
	}

	auto SandboxFreeCameraController::update(
		AshEngine::Scene& scene,
		const AshEngine::InputState& input,
		double delta_seconds,
		std::string& out_error) -> bool
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		out_error.clear();

		ASH_PROCESS_ERROR(scene.is_valid());
		if (m_camera_entity_id == 0)
		{
			out_error = "Sandbox free camera is not bound to a camera entity.";
			ASH_PROCESS_ERROR(false);
		}

		AshEngine::Entity camera_entity = scene.find_entity(m_camera_entity_id);
		if (!camera_entity.is_valid())
		{
			out_error = "Sandbox free camera could not resolve its bound camera entity in the scene.";
			ASH_PROCESS_ERROR(false);
		}
		if (!camera_entity.has_camera_component())
		{
			out_error = "Sandbox free camera is bound to an entity without a CameraComponent.";
			ASH_PROCESS_ERROR(false);
		}

		AshEngine::TransformComponent transform = camera_entity.get_transform_component();
		if (std::abs(input.get_scroll_y()) > 0.0)
		{
			set_move_speed(static_cast<float>(m_move_speed * std::pow(k_scroll_speed_scale, input.get_scroll_y())));
		}

		const bool mouse_look_active = input.is_mouse_button_down(GLFW_MOUSE_BUTTON_RIGHT);
		const double current_mouse_x = input.get_mouse_x();
		const double current_mouse_y = input.get_mouse_y();
		if (mouse_look_active)
		{
			if (!m_has_last_mouse_position || input.was_mouse_button_pressed(GLFW_MOUSE_BUTTON_RIGHT))
			{
				m_last_mouse_x = current_mouse_x;
				m_last_mouse_y = current_mouse_y;
				m_has_last_mouse_position = true;
			}

			const double mouse_delta_x = current_mouse_x - m_last_mouse_x;
			const double mouse_delta_y = current_mouse_y - m_last_mouse_y;
			m_last_mouse_x = current_mouse_x;
			m_last_mouse_y = current_mouse_y;

			transform.rotation_euler_degrees.y += static_cast<float>(mouse_delta_x * m_mouse_sensitivity);
			transform.rotation_euler_degrees.x += static_cast<float>(mouse_delta_y * m_mouse_sensitivity);
			transform.rotation_euler_degrees.x = std::clamp(transform.rotation_euler_degrees.x, -89.0f, 89.0f);
		}
		else
		{
			m_has_last_mouse_position = false;
		}

		glm::vec3 move_direction{ 0.0f, 0.0f, 0.0f };
		const glm::mat4 rotation_matrix = glm::yawPitchRoll(
			glm::radians(transform.rotation_euler_degrees.y),
			glm::radians(transform.rotation_euler_degrees.x),
			glm::radians(transform.rotation_euler_degrees.z));
		const glm::vec3 forward = glm::normalize(glm::vec3(rotation_matrix * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
		const glm::vec3 right = glm::normalize(glm::vec3(rotation_matrix * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)));
		const glm::vec3 up{ 0.0f, 1.0f, 0.0f };

		if (input.is_key_down(GLFW_KEY_W))
		{
			move_direction += forward;
		}
		if (input.is_key_down(GLFW_KEY_S))
		{
			move_direction -= forward;
		}
		if (input.is_key_down(GLFW_KEY_D))
		{
			move_direction += right;
		}
		if (input.is_key_down(GLFW_KEY_A))
		{
			move_direction -= right;
		}
		if (input.is_key_down(GLFW_KEY_E))
		{
			move_direction += up;
		}
		if (input.is_key_down(GLFW_KEY_Q))
		{
			move_direction -= up;
		}

		const float direction_length = glm::length(move_direction);
		if (direction_length > 0.0f)
		{
			move_direction /= direction_length;
			float speed = m_move_speed;
			if (input.is_key_down(GLFW_KEY_LEFT_SHIFT) || input.is_key_down(GLFW_KEY_RIGHT_SHIFT))
			{
				speed *= m_shift_multiplier;
			}
			const float clamped_delta_seconds = static_cast<float>(std::clamp(delta_seconds, 0.0, 0.1));
			transform.position += move_direction * speed * clamped_delta_seconds;
		}

		if (!camera_entity.set_transform_component(transform))
		{
			out_error = "Sandbox free camera failed to update the scene camera transform.";
			ASH_PROCESS_ERROR(false);
		}

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}
}
