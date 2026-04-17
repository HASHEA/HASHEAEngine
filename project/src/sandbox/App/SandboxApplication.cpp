#include "App/SandboxApplication.h"

#include "Base/hlog.h"
#include "Function/Render/SceneView.h"
#include "Function/Render/Renderer.h"
#include <utility>
#include <system_error>

namespace AshSandbox
{
	namespace
	{
		static constexpr const char* k_scene_render_build_hook_message =
			"Sandbox scene-render smoke requires a real logic-side visible-frame integration hook.";
		static constexpr const char* k_scene_render_submit_hook_message =
			"Sandbox scene-render smoke requires a real render-side scene submission hook.";
	}

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
		HLogInfo("Sandbox render-thread startup begin.");
		_initialize_scene_render_flow_hooks();
		m_tests = create_default_sandbox_tests();
	}

	auto SandboxApplication::_on_logic_startup() -> void
	{
		HLogInfo("Sandbox logic-thread startup begin.");
		if (!_initialize_paths_and_assets())
		{
			HLogError("Sandbox failed to initialize asset roots.");
			m_startupSucceeded = false;
			request_exit();
			return;
		}

		m_startupSucceeded = _run_startup_suite();
		if (!m_startupSucceeded)
		{
			request_exit();
			return;
		}

		ASH_ENQUEUE_RENDER_COMMAND("SandboxLogicStartupComplete", []()
		{
			HLogInfo("Sandbox logic startup completion reached the render thread.");
		});

		HLogInfo("Sandbox logic-thread startup suite completed successfully.");
	}

	auto SandboxApplication::_on_logic_update() -> void
	{
		if (!_run_logic_suite())
		{
			m_logicSucceeded = false;
			request_exit();
		}
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
			{
				std::scoped_lock<std::mutex> flowLock(m_sceneRenderFlow.mutex);
				m_sceneRenderFlow.status = SandboxSceneRenderFlowStatus::Failed;
				m_sceneRenderFlow.status_detail = "Renderer is unavailable.";
			}
			request_exit();
			return;
		}
		if (!renderer->begin_frame())
		{
			HLogError("Sandbox failed to begin renderer frame.");
			m_renderSucceeded = false;
			{
				std::scoped_lock<std::mutex> flowLock(m_sceneRenderFlow.mutex);
				m_sceneRenderFlow.status = SandboxSceneRenderFlowStatus::Failed;
				m_sceneRenderFlow.status_detail = "Renderer begin_frame failed.";
			}
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

		get_render_asset_manager().initialize(&m_assetDatabase, AshEngine::Application::get_renderer());

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
		context.logic_thread_enabled = is_logic_thread_enabled();
		context.request_exit = [this]()
		{
			request_exit();
		};
		context.scene_render_flow = &m_sceneRenderFlow;
		context.scene_render_hooks = &m_sceneRenderHooks;
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

	auto SandboxApplication::_run_logic_suite() -> bool
	{
		bool allPassed = true;
		for (const auto& test : m_tests)
		{
			if (!test || !test->wants_logic_update())
			{
				continue;
			}

			std::string error{};
			SandboxTestContext context = _make_test_context();
			if (!test->on_logic_update(context, error))
			{
				allPassed = false;
				HLogError("Sandbox logic test '{}' failed on frame {}: {}", test->get_name(), get_frame_index(), error.empty() ? "Unknown error." : error);
			}
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

	auto SandboxApplication::_initialize_scene_render_flow_hooks() -> void
	{
		m_sceneRenderHooks.build_visible_frame =
			[](SandboxTestContext& context, SandboxSceneRenderFlowState& state, std::string& out_error) -> bool
		{
			ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
			ASH_PROCESS_ERROR(context.renderer != nullptr);
			auto* application = static_cast<SandboxApplication*>(AshEngine::Application::get());
			ASH_PROCESS_ERROR(application != nullptr);

			state.render_scene = {};
			ASH_PROCESS_ERROR(state.render_scene.rebuild_from_scene(state.scene, application->get_render_asset_manager()));

			AshEngine::SceneViewDesc view_desc{};
			const std::shared_ptr<AshEngine::RenderTarget> back_buffer = context.renderer->get_back_buffer();
			ASH_PROCESS_ERROR(back_buffer != nullptr);
			view_desc.viewport_width = back_buffer->get_width();
			view_desc.viewport_height = back_buffer->get_height();
			ASH_PROCESS_ERROR(AshEngine::build_primary_scene_view(state.scene, view_desc, state.scene_view));

			auto visible_frame = std::make_shared<AshEngine::VisibleRenderFrame>();
			ASH_PROCESS_ERROR(state.render_scene.build_visible_render_frame(context.frame_index, state.scene_view, back_buffer, *visible_frame));
			ASH_PROCESS_ERROR(!visible_frame->static_mesh_draws.empty());

			state.visible_frame = visible_frame;
			state.visible_frame_ready = true;
			state.status = SandboxSceneRenderFlowStatus::VisibleFrameReady;
			state.status_detail = "Built a visible render frame from the logical scene.";
			out_error.clear();
			ASH_PROCESS_GUARD_RETURN_END(bResult, false);
		};

		m_sceneRenderHooks.submit_frame =
			[](SandboxTestContext& context, SandboxSceneRenderFlowState& state, const std::shared_ptr<AshEngine::RenderTarget>& output_target, std::string& out_error) -> bool
		{
			ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
			ASH_PROCESS_ERROR(output_target != nullptr);
			auto* application = static_cast<SandboxApplication*>(AshEngine::Application::get());
			ASH_PROCESS_ERROR(application != nullptr);
			ASH_PROCESS_ERROR(state.visible_frame_ready);
			ASH_PROCESS_ERROR(state.visible_frame != nullptr);

			state.visible_frame->output_target = output_target;
			ASH_PROCESS_ERROR(application->get_scene_renderer().render_visible_frame(*state.visible_frame));

			state.render_submission_exercised = true;
			state.status = SandboxSceneRenderFlowStatus::RenderSubmitted;
			state.status_detail = "Submitted the visible render frame through SceneRenderer.";
			out_error.clear();
			ASH_PROCESS_GUARD_RETURN_END(bResult, false);
		};
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
			"Sandbox summary: startup={}, logic={}, render={}, scene_render_status={}, reports='{}'.",
			m_startupSucceeded ? "passed" : "failed",
			m_logicSucceeded ? "passed" : "failed",
			m_renderSucceeded ? "passed" : "failed",
			get_sandbox_scene_render_flow_status_name(m_sceneRenderFlow.status),
			m_reportRoot.string());
	}
}
