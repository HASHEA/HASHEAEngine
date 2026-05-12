#pragma once

#include "App/SandboxFreeCameraController.h"
#include "Base/input/Input.h"
#include "Function/Asset/AssetDatabase.h"
#include "Function/Scene/Scene.h"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace AshSandbox
{
	enum class SandboxStandardSceneLoadState : uint8_t
	{
		Idle = 0,
		LoadingModel,
		Ready,
		Failed
	};

	struct SandboxStandardSceneSnapshot
	{
		SandboxStandardSceneLoadState load_state = SandboxStandardSceneLoadState::Idle;
		std::string failure_detail{};
		std::filesystem::path sample_asset_path{};
		AshEngine::Scene scene{};
		AshEngine::EntityId primary_camera_entity_id = 0;
		float recommended_camera_move_speed = 8.0f;
	};

	class SandboxStandardScene
	{
	public:
		SandboxStandardScene() = default;

	public:
		static auto get_sample_asset_root_path() -> const std::filesystem::path&;
		static auto get_canonical_sample_asset_path() -> const std::filesystem::path&;
		static auto discover_sample_asset_paths(const AshEngine::AssetDatabase& asset_database) -> std::vector<std::filesystem::path>;
		static auto make_sample_asset_label(const std::filesystem::path& sample_asset_path) -> std::string;

		auto start(
			AshEngine::AssetDatabase& asset_database) -> bool;
		auto start(
			AshEngine::AssetDatabase& asset_database,
			const std::filesystem::path& sample_asset_path) -> bool;

		auto reset() -> void;
		auto update_logic(const AshEngine::InputState& input, uint64_t frame_index) -> bool;

		auto snapshot() const -> SandboxStandardSceneSnapshot;
		auto get_load_state() const -> SandboxStandardSceneLoadState;
		auto get_failure_detail() const -> std::string;
		auto is_ready() const -> bool;
		auto get_scene() -> AshEngine::Scene*;
		auto get_scene() const -> const AshEngine::Scene*;

	private:
		auto _build_runtime_snapshot(
			const std::shared_ptr<const AshEngine::Model>& model,
			const std::filesystem::path& sample_asset_path,
			SandboxStandardSceneSnapshot& out_snapshot,
			std::string& out_error) -> bool;
		auto _create_primary_camera(
			AshEngine::Scene& scene,
			AshEngine::EntityId& out_camera_entity_id,
			float& out_recommended_move_speed,
			std::string& out_error) const -> bool;
		auto _create_default_lights(
			AshEngine::Scene& scene,
			std::string& out_error) const -> bool;
		auto _consume_logic_delta_seconds() -> double;
		auto _set_failure_locked(std::string detail) -> void;

	private:
		mutable std::mutex m_mutex{};
		SandboxStandardSceneSnapshot m_snapshot{};
		AshEngine::AssetDatabase* m_asset_database = nullptr;
		std::shared_future<std::shared_ptr<const AshEngine::Model>> m_model_future{};
		SandboxFreeCameraController m_free_camera_controller{};
		std::chrono::steady_clock::time_point m_last_logic_tick_time{};
		bool m_has_logic_tick_time = false;
	};
}
