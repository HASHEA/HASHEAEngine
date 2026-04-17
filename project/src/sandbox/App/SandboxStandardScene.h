#pragma once

#include "App/SandboxFreeCameraController.h"
#include "Base/input/Input.h"
#include "Function/Asset/AssetDatabase.h"
#include "Function/Render/RenderAssetManager.h"
#include "Function/Render/RenderScene.h"
#include "Function/Render/Renderer.h"
#include "Function/Render/SceneView.h"
#include "Function/Scene/Scene.h"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <glm/glm.hpp>
#include <memory>
#include <mutex>
#include <string>

namespace AshSandbox
{
	enum class SandboxStandardSceneLoadState : uint8_t
	{
		Idle = 0,
		LoadingModel,
		Ready,
		Failed
	};

	struct SandboxStandardSceneVisibleFrameState
	{
		std::shared_ptr<AshEngine::VisibleRenderFrame> latest_snapshot = nullptr;
		std::shared_ptr<AshEngine::VisibleRenderFrame> pending_handoff = nullptr;
		uint64_t latest_snapshot_version = 0;
		uint64_t latest_handoff_version = 0;
		uint64_t latest_submitted_version = 0;
	};

	struct SandboxStandardSceneSnapshot
	{
		SandboxStandardSceneLoadState load_state = SandboxStandardSceneLoadState::Idle;
		std::string failure_detail{};
		std::filesystem::path sample_asset_path{};
		AshEngine::Scene scene{};
		AshEngine::RenderScene render_scene{};
		AshEngine::SceneView latest_scene_view{};
		AshEngine::EntityId primary_camera_entity_id = 0;
		float recommended_camera_move_speed = 8.0f;
		SandboxStandardSceneVisibleFrameState visible_frame{};
	};

	class SandboxStandardScene
	{
	public:
		SandboxStandardScene() = default;

	public:
		static auto get_canonical_sample_asset_path() -> const std::filesystem::path&;

		auto start(
			AshEngine::AssetDatabase& asset_database,
			AshEngine::Renderer& renderer,
			AshEngine::RenderAssetManager& render_asset_manager) -> bool;

		auto reset() -> void;
		auto update_logic(const AshEngine::InputState& input, uint64_t frame_index) -> bool;
		auto take_pending_visible_frame(
			std::shared_ptr<AshEngine::VisibleRenderFrame>& out_visible_frame,
			uint64_t& out_version) -> bool;
		auto note_visible_frame_submitted(uint64_t version) -> void;

		auto snapshot() const -> SandboxStandardSceneSnapshot;
		auto get_load_state() const -> SandboxStandardSceneLoadState;
		auto get_failure_detail() const -> std::string;
		auto is_ready() const -> bool;

	private:
		auto _build_runtime_snapshot(
			const std::shared_ptr<const AshEngine::Model>& model,
			uint64_t frame_index,
			SandboxStandardSceneSnapshot& out_snapshot,
			std::string& out_error) -> bool;
		auto _compute_scene_world_bounds(
			const AshEngine::Scene& scene,
			glm::vec3& out_world_min,
			glm::vec3& out_world_max,
			std::string& out_error) const -> bool;
		auto _make_default_camera_transform(
			const glm::vec3& bounds_min,
			const glm::vec3& bounds_max,
			AshEngine::TransformComponent& out_transform,
			float& out_recommended_move_speed) const -> bool;
		auto _rebuild_visible_frame(
			SandboxStandardSceneSnapshot& io_snapshot,
			uint64_t frame_index,
			std::string& out_error) -> bool;
		auto _create_primary_camera(
			AshEngine::Scene& scene,
			AshEngine::EntityId& out_camera_entity_id,
			float& out_recommended_move_speed,
			std::string& out_error) const -> bool;
		auto _consume_logic_delta_seconds() -> double;
		auto _set_failure_locked(std::string detail) -> void;

	private:
		mutable std::mutex m_mutex{};
		SandboxStandardSceneSnapshot m_snapshot{};
		AshEngine::AssetDatabase* m_asset_database = nullptr;
		AshEngine::Renderer* m_renderer = nullptr;
		AshEngine::RenderAssetManager* m_render_asset_manager = nullptr;
		std::shared_future<std::shared_ptr<const AshEngine::Model>> m_model_future{};
		SandboxFreeCameraController m_free_camera_controller{};
		std::chrono::steady_clock::time_point m_last_logic_tick_time{};
		bool m_has_logic_tick_time = false;
	};
}
