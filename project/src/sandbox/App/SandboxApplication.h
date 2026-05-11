#pragma once

#include "App/SandboxStandardScene.h"
#include "Function/Application.h"
#include "Function/Asset/AssetDatabase.h"
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace AshSandbox
{
	struct SandboxModelSelectionState
	{
		std::vector<std::filesystem::path> asset_paths{};
		std::vector<std::string> labels{};
		std::string status{};
		int32_t selected_index = -1;
		int32_t pending_index = -1;
	};

	class SandboxApplication final : public AshEngine::Application
	{
	public:
		explicit SandboxApplication(const AshEngine::EngineInitConfig& config);
		~SandboxApplication() override;

	protected:
		auto _on_startup() -> void override;
		auto _on_shutdown() -> void override;
		auto _on_update() -> void override;
		auto _on_logic_startup() -> void override;
		auto _on_logic_update() -> void override;
		auto _on_gui() -> void override;
		auto _on_render() -> void override;
		auto _present() -> void override;

	private:
		auto _initialize_paths_and_assets() -> bool;
		auto _initialize_model_selection_options() -> bool;
		auto _start_standard_scene() -> bool;
		auto _switch_standard_scene_model(int32_t model_index) -> bool;
		auto _process_pending_model_selection() -> bool;
		auto _tick_standard_scene_logic() -> bool;
		auto _register_standard_scene_presentation() -> bool;
		auto _destroy_standard_scene_presentation() -> void;
		auto _draw_model_selection_overlay() -> void;
		auto _request_model_selection_from_ui(int32_t model_index) -> void;
		auto _get_model_selection_state_copy() const -> SandboxModelSelectionState;
		auto _set_model_selection_status(const std::string& status) -> void;
		auto _sync_model_selection_status_from_scene() -> void;
		auto _log_runtime_summary() -> void;

	private:
		std::filesystem::path m_assetRoot = "product/assets";
		std::filesystem::path m_reportRoot = "Intermediate/test-reports/sandbox";
		AshEngine::AssetDatabase m_assetDatabase{};
		SandboxStandardScene m_standardScene{};
		AshEngine::SceneOutputHandle m_mainSceneOutput{};
		AshEngine::SceneViewBindingHandle m_mainSceneBinding{};
		mutable std::mutex m_modelSelectionMutex{};
		SandboxModelSelectionState m_modelSelectionState{};
		bool m_logicBootstrapExecuted = false;
		bool m_startupSucceeded = false;
		bool m_logicSucceeded = true;
		bool m_renderSucceeded = true;
		bool m_summaryLogged = false;
	};
}
