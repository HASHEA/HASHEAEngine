#pragma once

#include "App/SandboxStandardScene.h"
#include "Function/Application.h"
#include "Function/Asset/AssetDatabase.h"
#include <filesystem>

namespace AshSandbox
{
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
		auto _on_render() -> void override;
		auto _present() -> void override;

	private:
		auto _initialize_paths_and_assets() -> bool;
		auto _start_standard_scene() -> bool;
		auto _tick_standard_scene_logic() -> bool;
		auto _register_standard_scene_presentation() -> bool;
		auto _destroy_standard_scene_presentation() -> void;
		auto _log_runtime_summary() -> void;

	private:
		std::filesystem::path m_assetRoot = "product/assets";
		std::filesystem::path m_reportRoot = "product/test-reports/sandbox";
		AshEngine::AssetDatabase m_assetDatabase{};
		SandboxStandardScene m_standardScene{};
		AshEngine::SceneOutputHandle m_mainSceneOutput{};
		AshEngine::SceneViewBindingHandle m_mainSceneBinding{};
		bool m_logicBootstrapExecuted = false;
		bool m_startupSucceeded = false;
		bool m_logicSucceeded = true;
		bool m_renderSucceeded = true;
		bool m_summaryLogged = false;
	};
}
