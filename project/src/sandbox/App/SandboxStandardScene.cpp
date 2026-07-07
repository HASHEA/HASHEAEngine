#include "App/SandboxStandardScene.h"

#include "Base/hlog.h"
#include "Function/Application.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <utility>

namespace AshSandbox
{
	namespace
	{
		static constexpr float k_default_camera_move_speed = 8.0f;
	}

	auto SandboxStandardScene::get_standard_scene_path() -> std::filesystem::path
	{
		// RenderGate（SDD-2026-07-07-render-gate）：--scene 覆盖默认场景路径
		if (AshEngine::Application* application = AshEngine::Application::get())
		{
			const std::string& override_path = application->get_scene_path_override();
			if (!override_path.empty())
			{
				return std::filesystem::path{ override_path };
			}
		}
		return std::filesystem::path{ "product/assets/scenes/Sandbox.scene.json" };
	}

	auto SandboxStandardScene::start(AshEngine::AssetDatabase& asset_database) -> bool
	{
		std::string failure_detail{};
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		SandboxStandardSceneSnapshot snapshot{};
		snapshot.scene_path = get_standard_scene_path();
		snapshot.load_state = SandboxStandardSceneLoadState::LoadingScene;

		if (!asset_database.is_valid())
		{
			failure_detail = "Sandbox standard scene cannot start because the AssetDatabase is invalid.";
			ASH_PROCESS_ERROR(false);
		}

		ASH_PROCESS_ERROR(_load_scene_snapshot(asset_database, snapshot, failure_detail));

		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			m_snapshot = std::move(snapshot);
			m_free_camera_controller.reset();
			m_free_camera_controller.bind_camera_entity(m_snapshot.primary_camera_entity_id);
			m_free_camera_controller.set_move_speed(m_snapshot.recommended_camera_move_speed);
			m_has_logic_tick_time = false;
		}

		HLogInfo(
			"Sandbox standard scene loaded '{}'.",
			get_standard_scene_path().generic_string());

		ASH_PROCESS_GUARD_END(bResult, false);
		if (!bResult)
		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			m_snapshot = {};
			m_snapshot.scene_path = get_standard_scene_path();
			_set_failure_locked(
				failure_detail.empty()
				? std::string("Sandbox standard scene failed to start.")
				: std::move(failure_detail));
		}
		return bResult;
	}

	auto SandboxStandardScene::reset() -> void
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		m_snapshot = {};
		m_snapshot.scene_path = get_standard_scene_path();
		m_free_camera_controller.reset();
		m_has_logic_tick_time = false;
	}

	auto SandboxStandardScene::update_logic(const AshEngine::InputState& input, uint64_t frame_index) -> bool
	{
		(void)frame_index;
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);

		SandboxStandardSceneLoadState load_state = SandboxStandardSceneLoadState::Idle;
		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			load_state = m_snapshot.load_state;
		}

		if (load_state == SandboxStandardSceneLoadState::Failed)
		{
			ASH_PROCESS_ERROR(false);
		}

		if (load_state == SandboxStandardSceneLoadState::Ready)
		{
			SandboxStandardSceneSnapshot working_snapshot{};
			{
				std::scoped_lock<std::mutex> lock(m_mutex);
				working_snapshot = m_snapshot;
			}

			if (m_free_camera_controller.get_camera_entity_id() == 0 && working_snapshot.primary_camera_entity_id != 0)
			{
				m_free_camera_controller.bind_camera_entity(working_snapshot.primary_camera_entity_id);
				m_free_camera_controller.set_move_speed(working_snapshot.recommended_camera_move_speed);
			}

			std::string update_error{};
			const double logic_delta_seconds = _consume_logic_delta_seconds();
			if (!m_free_camera_controller.update(working_snapshot.scene, input, logic_delta_seconds, update_error))
			{
				std::scoped_lock<std::mutex> lock(m_mutex);
				_set_failure_locked(update_error.empty() ? "Failed to update the Sandbox standard scene free camera." : std::move(update_error));
				ASH_PROCESS_ERROR(false);
			}

			{
				std::scoped_lock<std::mutex> lock(m_mutex);
				m_snapshot = std::move(working_snapshot);
			}
		}

		ASH_PROCESS_GUARD_END(bResult, false);
		if (!bResult)
		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			if (m_snapshot.load_state != SandboxStandardSceneLoadState::Failed)
			{
				_set_failure_locked("Sandbox standard scene logic update failed.");
			}
		}
		return bResult;
	}

	auto SandboxStandardScene::snapshot() const -> SandboxStandardSceneSnapshot
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return m_snapshot;
	}

	auto SandboxStandardScene::get_load_state() const -> SandboxStandardSceneLoadState
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return m_snapshot.load_state;
	}

	auto SandboxStandardScene::get_failure_detail() const -> std::string
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return m_snapshot.failure_detail;
	}

	auto SandboxStandardScene::is_ready() const -> bool
	{
		return get_load_state() == SandboxStandardSceneLoadState::Ready;
	}

	auto SandboxStandardScene::get_scene() -> AshEngine::Scene*
	{
		return &m_snapshot.scene;
	}

	auto SandboxStandardScene::get_scene() const -> const AshEngine::Scene*
	{
		return &m_snapshot.scene;
	}

	auto SandboxStandardScene::_load_scene_snapshot(
		AshEngine::AssetDatabase& asset_database,
		SandboxStandardSceneSnapshot& out_snapshot,
		std::string& out_error) const -> bool
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		out_snapshot = {};
		out_snapshot.scene_path = get_standard_scene_path();

		if (!std::filesystem::exists(out_snapshot.scene_path))
		{
			out_error = "Sandbox standard scene file is missing: '" + out_snapshot.scene_path.generic_string() + "'.";
			ASH_PROCESS_ERROR(false);
		}

		out_snapshot.scene = AshEngine::Scene::load_from_file(out_snapshot.scene_path, &out_error);
		ASH_PROCESS_ERROR(out_snapshot.scene.is_valid());
		ASH_PROCESS_ERROR(_validate_referenced_assets(out_snapshot.scene, asset_database, out_error));
		ASH_PROCESS_ERROR(_find_primary_camera(
			out_snapshot.scene,
			out_snapshot.primary_camera_entity_id,
			out_snapshot.recommended_camera_move_speed,
			out_error));

		// RenderGate（SDD-2026-07-07-render-gate）：抓帧测试时固定初始相机位置，保证画面确定性
		if (AshEngine::Application* application = AshEngine::Application::get();
			application && !application->get_frame_dump_path().empty())
		{
			AshEngine::Entity camera_entity = out_snapshot.scene.find_entity(out_snapshot.primary_camera_entity_id);
			if (camera_entity.is_valid())
			{
				AshEngine::TransformComponent transform = camera_entity.get_transform_component();
				transform.position = { 0.0f, 5.0f, 0.0f };
				if (!camera_entity.set_transform_component(transform))
				{
					out_error = "Sandbox standard scene failed to pin the frame-dump camera position.";
					ASH_PROCESS_ERROR(false);
				}
				HLogInfo("Sandbox standard scene pinned frame-dump camera position to (0, 5, 0).");
			}
		}

		out_snapshot.load_state = SandboxStandardSceneLoadState::Ready;
		out_snapshot.failure_detail.clear();
		out_error.clear();
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	auto SandboxStandardScene::_find_primary_camera(
		AshEngine::Scene& scene,
		AshEngine::EntityId& out_camera_entity_id,
		float& out_recommended_move_speed,
		std::string& out_error) const -> bool
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(scene.is_valid());
		bool found_primary_camera = false;
		for (const AshEngine::Entity& entity : scene.get_entities_with_component(AshEngine::SceneComponentType::Camera))
		{
			const AshEngine::CameraComponent camera = entity.get_camera_component();
			if (camera.primary)
			{
				out_camera_entity_id = entity.get_id();
				out_recommended_move_speed = k_default_camera_move_speed;
				out_error.clear();
				found_primary_camera = true;
				break;
			}
		}

		if (!found_primary_camera)
		{
			out_error = "Sandbox standard scene does not contain a primary camera.";
			ASH_PROCESS_ERROR(false);
		}
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	auto SandboxStandardScene::_validate_referenced_assets(
		const AshEngine::Scene& scene,
		const AshEngine::AssetDatabase& asset_database,
		std::string& out_error) const -> bool
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(scene.is_valid());
		ASH_PROCESS_ERROR(asset_database.is_valid());

		for (const AshEngine::SceneMeshExtractionDesc& mesh_desc : scene.extract_mesh_entities())
		{
			const std::filesystem::path asset_path =
				std::filesystem::path(mesh_desc.asset_path).lexically_normal();
			if (asset_path.empty() || asset_database.find_asset_by_path(asset_path) == nullptr)
			{
				out_error = "Sandbox standard scene references missing mesh asset: '" + asset_path.generic_string() + "'.";
				ASH_PROCESS_ERROR(false);
			}
		}

		AshEngine::SceneEnvironmentExtractionDesc environment{};
		if (scene.extract_active_environment(environment))
		{
			const std::filesystem::path ibl_path = std::filesystem::path(environment.ibl_asset_path).lexically_normal();
			if (!ibl_path.empty() && !std::filesystem::exists(ibl_path))
			{
				out_error = "Sandbox standard scene references missing IBL asset: '" + ibl_path.generic_string() + "'.";
				ASH_PROCESS_ERROR(false);
			}

			const std::filesystem::path source_texture_path =
				std::filesystem::path(environment.source_texture_path).lexically_normal();
			if (!source_texture_path.empty() && !std::filesystem::exists(source_texture_path))
			{
				out_error = "Sandbox standard scene references missing environment source texture: '" +
					source_texture_path.generic_string() + "'.";
				ASH_PROCESS_ERROR(false);
			}
		}

		out_error.clear();
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	auto SandboxStandardScene::_consume_logic_delta_seconds() -> double
	{
		const auto now = std::chrono::steady_clock::now();
		double delta_seconds = 1.0 / 60.0;
		if (m_has_logic_tick_time)
		{
			delta_seconds = std::chrono::duration<double>(now - m_last_logic_tick_time).count();
		}
		m_last_logic_tick_time = now;
		m_has_logic_tick_time = true;
		return std::clamp(delta_seconds, 0.0, 0.1);
	}

	auto SandboxStandardScene::_set_failure_locked(std::string detail) -> void
	{
		m_snapshot.load_state = SandboxStandardSceneLoadState::Failed;
		m_snapshot.failure_detail = std::move(detail);
		HLogError("Sandbox standard scene failure: {}", m_snapshot.failure_detail);
	}
}
