#include "App/SandboxApplication.h"

#include "Base/hlog.h"
#include "Function/Render/Renderer.h"
#include <system_error>

namespace AshSandbox
{
	SandboxApplication::SandboxApplication(const AshEngine::EngineInitConfig& config)
		: AshEngine::Application(config)
	{
	}

	SandboxApplication::~SandboxApplication()
	{
		_shutdown_tests();
		_log_suite_summary();
	}

	auto SandboxApplication::_on_startup() -> void
	{
		HLogInfo("Sandbox startup begin.");
		m_tests = create_default_sandbox_tests();
		if (!_initialize_paths_and_assets())
		{
			HLogError("Sandbox failed to initialize asset roots.");
			request_exit();
			return;
		}

		m_startupSucceeded = _run_startup_suite();
		if (!m_startupSucceeded)
		{
			request_exit();
			return;
		}

		HLogInfo("Sandbox startup suite completed successfully.");
	}

	auto SandboxApplication::_on_shutdown() -> void
	{
		_shutdown_tests();
		_log_suite_summary();
	}

	auto SandboxApplication::_on_render() -> void
	{
		auto* renderer = AshEngine::Application::get_renderer();
		if (!renderer)
		{
			HLogError("Sandbox could not acquire renderer.");
			m_renderSucceeded = false;
			request_exit();
			return;
		}
		if (!renderer->begin_frame())
		{
			HLogError("Sandbox failed to begin renderer frame.");
			m_renderSucceeded = false;
			request_exit();
			return;
		}

		bool renderSucceeded = true;
		const std::shared_ptr<AshEngine::RenderTarget> outputTarget = renderer->get_back_buffer();
		if (!_run_render_suite(outputTarget))
		{
			renderSucceeded = false;
			m_renderSucceeded = false;
		}

		renderer->end_frame();
		if (!renderSucceeded)
		{
			request_exit();
		}
	}

	auto SandboxApplication::_present() -> void
	{
		AshEngine::Application::_present();
	}

	auto SandboxApplication::_initialize_paths_and_assets() -> bool
	{
		std::error_code createError{};
		std::filesystem::create_directories(m_reportRoot, createError);
		if (createError)
		{
			HLogError("Sandbox failed to create report directory '{}': {}.", m_reportRoot.string(), createError.message());
			return false;
		}

		if (!std::filesystem::exists(m_assetRoot))
		{
			HLogError("Sandbox asset root does not exist: {}.", m_assetRoot.string());
			return false;
		}

		m_assetDatabase = AshEngine::AssetDatabase::create(m_assetRoot);
		if (!m_assetDatabase.is_valid() || !m_assetDatabase.refresh())
		{
			HLogError("Sandbox failed to refresh asset database '{}': {}.", m_assetRoot.string(), m_assetDatabase.get_last_error());
			return false;
		}

		HLogInfo("Sandbox asset database ready: root='{}', assets={}.", m_assetRoot.string(), m_assetDatabase.get_assets().size());
		return true;
	}

	auto SandboxApplication::_make_test_context() -> SandboxTestContext
	{
		SandboxTestContext context{};
		context.renderer = AshEngine::Application::get_renderer();
		context.asset_database = &m_assetDatabase;
		context.asset_root = m_assetRoot;
		context.report_root = m_reportRoot;
		context.frame_index = get_frame_index();
		return context;
	}

	auto SandboxApplication::_run_startup_suite() -> bool
	{
		bool allPassed = true;
		for (const auto& test : m_tests)
		{
			if (!test)
			{
				continue;
			}

			std::string error{};
			SandboxTestContext context = _make_test_context();
			if (!test->on_startup(context, error))
			{
				allPassed = false;
				HLogError("Sandbox startup test '{}' failed: {}", test->get_name(), error.empty() ? "Unknown error." : error);
				continue;
			}

			HLogInfo("Sandbox startup test '{}' passed.", test->get_name());
		}
		return allPassed;
	}

	auto SandboxApplication::_run_render_suite(const std::shared_ptr<AshEngine::RenderTarget>& output_target) -> bool
	{
		bool allPassed = true;
		for (const auto& test : m_tests)
		{
			if (!test || !test->wants_render())
			{
				continue;
			}

			std::string error{};
			SandboxTestContext context = _make_test_context();
			if (!test->on_render(context, output_target, error))
			{
				allPassed = false;
				HLogError("Sandbox render test '{}' failed on frame {}: {}", test->get_name(), get_frame_index(), error.empty() ? "Unknown error." : error);
			}
		}
		return allPassed;
	}

	auto SandboxApplication::_shutdown_tests() -> void
	{
		if (m_tests.empty())
		{
			return;
		}

		SandboxTestContext context = _make_test_context();
		for (auto& test : m_tests)
		{
			if (test)
			{
				test->on_shutdown(context);
			}
		}
		m_tests.clear();
	}

	auto SandboxApplication::_log_suite_summary() -> void
	{
		if (m_summaryLogged)
		{
			return;
		}
		m_summaryLogged = true;
		HLogInfo(
			"Sandbox summary: startup={}, render={}, reports='{}'.",
			m_startupSucceeded ? "passed" : "failed",
			m_renderSucceeded ? "passed" : "failed",
			m_reportRoot.string());
	}
}
