#pragma once

#include "Function/Application.h"
#include "Function/Asset/AssetDatabase.h"
#include "Tests/SandboxTestRegistry.h"
#include <filesystem>
#include <memory>
#include <vector>

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
		auto _on_render() -> void override;
		auto _present() -> void override;

	private:
		auto _initialize_paths_and_assets() -> bool;
		auto _make_test_context() -> SandboxTestContext;
		auto _run_startup_suite() -> bool;
		auto _run_render_suite(const std::shared_ptr<AshEngine::RenderTarget>& output_target) -> bool;
		auto _shutdown_tests() -> void;
		auto _log_suite_summary() -> void;

	private:
		std::filesystem::path m_assetRoot = "product/assets";
		std::filesystem::path m_reportRoot = "product/test-reports/sandbox";
		AshEngine::AssetDatabase m_assetDatabase{};
		std::vector<std::unique_ptr<ISandboxTest>> m_tests{};
		bool m_startupSucceeded = false;
		bool m_renderSucceeded = true;
		bool m_summaryLogged = false;
	};
}
