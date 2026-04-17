#include "App/SandboxApplication.h"

#include "Base/hthreading.h"
#include "Base/hlog.h"
#include "Function/Render/Renderer.h"
#include <system_error>

namespace AshSandbox
{
	namespace
	{
		static auto get_standard_scene_load_state_name(SandboxStandardSceneLoadState load_state) -> const char*
		{
			switch (load_state)
			{
			case SandboxStandardSceneLoadState::Idle:
				return "Idle";
			case SandboxStandardSceneLoadState::LoadingModel:
				return "LoadingModel";
			case SandboxStandardSceneLoadState::Ready:
				return "Ready";
			case SandboxStandardSceneLoadState::Failed:
				return "Failed";
			default:
				return "Unknown";
			}
		}
	}

	SandboxApplication::SandboxApplication(const AshEngine::EngineInitConfig& config)
		: AshEngine::Application(config)
	{
	}

	SandboxApplication::~SandboxApplication()
	{
		_log_runtime_summary();
		m_activeVisibleFrame.reset();
		m_standardScene.reset();
	}

	auto SandboxApplication::_on_startup() -> void
	{
		HLogInfo("Sandbox render-thread startup begin.");
		if (!is_logic_thread_enabled())
		{
			HLogWarning("Sandbox logic-thread mode is unavailable. Falling back to single-threaded scene ticking.");
		}
	}

	auto SandboxApplication::_on_update() -> void
	{
		if (is_logic_thread_enabled())
		{
			return;
		}

		if (!m_logicBootstrapExecuted)
		{
			m_logicBootstrapExecuted = true;
			_on_logic_startup();
			return;
		}

		_on_logic_update();
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

		m_startupSucceeded = _start_standard_scene();
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
		if (!m_startupSucceeded)
		{
			return;
		}

		if (!_tick_standard_scene_logic())
		{
			m_logicSucceeded = false;
			request_exit();
		}
	}

	auto SandboxApplication::_on_shutdown() -> void
	{
		_log_runtime_summary();
		m_activeVisibleFrame.reset();
		m_standardScene.reset();
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
		if (!_finalize_render_assets())
		{
			HLogError("Sandbox failed to finalize pending render assets on the render thread.");
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

		bool render_succeeded = true;
		_consume_visible_frame_handoff();
		const std::shared_ptr<AshEngine::RenderTarget> output_target = renderer->get_back_buffer();
		if (!output_target)
		{
			HLogError("Sandbox could not acquire the renderer back buffer for standard-scene submission.");
			render_succeeded = false;
		}
		else if (m_activeVisibleFrame)
		{
			render_succeeded = _submit_standard_scene(output_target);
		}
		else if (m_standardScene.get_load_state() == SandboxStandardSceneLoadState::Failed)
		{
			HLogError(
				"Sandbox standard scene failed before any visible frame could be submitted: {}",
				m_standardScene.get_failure_detail());
			render_succeeded = false;
		}

		if (!renderer->end_frame())
		{
			HLogError("Sandbox failed to end the renderer frame.");
			render_succeeded = false;
		}

		if (!render_succeeded)
		{
			m_renderSucceeded = false;
			request_exit();
		}
	}

	auto SandboxApplication::_present() -> void
	{
		AshEngine::Application::_present();
	}

	auto SandboxApplication::_initialize_paths_and_assets() -> bool
	{
		std::error_code create_error{};
		std::filesystem::create_directories(m_reportRoot, create_error);
		if (create_error)
		{
			HLogError("Sandbox failed to create report directory '{}': {}.", m_reportRoot.string(), create_error.message());
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

	auto SandboxApplication::_start_standard_scene() -> bool
	{
		AshEngine::Renderer* renderer = AshEngine::Application::get_renderer();
		if (renderer == nullptr)
		{
			HLogError("Sandbox could not start the standard scene because the renderer is unavailable.");
			return false;
		}

		if (!m_standardScene.start(m_assetDatabase, *renderer, get_render_asset_manager()))
		{
			const std::string failure_detail = m_standardScene.get_failure_detail();
			HLogError(
				"Sandbox failed to start the standard-scene runtime: {}",
				failure_detail.empty() ? "Unknown error." : failure_detail);
			return false;
		}

		HLogInfo(
			"Sandbox standard scene runtime is using '{}'.",
			SandboxStandardScene::get_canonical_sample_asset_path().generic_string());
		return true;
	}

	auto SandboxApplication::_tick_standard_scene_logic() -> bool
	{
		if (!m_standardScene.update_logic(AshEngine::Application::get_input(), get_frame_index()))
		{
			const std::string failure_detail = m_standardScene.get_failure_detail();
			HLogError(
				"Sandbox standard-scene logic update failed on frame {}: {}",
				get_frame_index(),
				failure_detail.empty() ? "Unknown error." : failure_detail);
			return false;
		}

		return true;
	}

	auto SandboxApplication::_finalize_render_assets() -> bool
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(AshEngine::is_in_render_thread());

		AshEngine::RenderAssetManager& render_asset_manager = get_render_asset_manager();
		render_asset_manager.finalize_pending_assets();
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	auto SandboxApplication::_consume_visible_frame_handoff() -> void
	{
		std::shared_ptr<AshEngine::VisibleRenderFrame> pending_visible_frame{};
		uint64_t pending_version = 0;
		if (m_standardScene.take_pending_visible_frame(pending_visible_frame, pending_version) && pending_visible_frame != nullptr)
		{
			m_activeVisibleFrame = std::move(pending_visible_frame);
			m_activeVisibleFrameVersion = pending_version;
		}
	}

	auto SandboxApplication::_submit_standard_scene(const std::shared_ptr<AshEngine::RenderTarget>& output_target) -> bool
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(output_target != nullptr);
		ASH_PROCESS_ERROR(m_activeVisibleFrame != nullptr);

		m_activeVisibleFrame->output_target = output_target;
		ASH_PROCESS_ERROR(get_scene_renderer().render_visible_frame(*m_activeVisibleFrame));
		m_standardScene.note_visible_frame_submitted(m_activeVisibleFrameVersion);
		ASH_PROCESS_GUARD_END(bResult, false);
		if (!bResult)
		{
			HLogError(
				"Sandbox failed to submit the current standard-scene visible frame version {}.",
				m_activeVisibleFrameVersion);
		}
		return bResult;
	}

	auto SandboxApplication::_log_runtime_summary() -> void
	{
		if (m_summaryLogged)
		{
			return;
		}
		m_summaryLogged = true;

		const SandboxStandardSceneSnapshot snapshot = m_standardScene.snapshot();
		const bool scene_loaded = snapshot.scene.is_valid();
		const bool visible_frames_built = snapshot.visible_frame.latest_snapshot_version > 0;
		const bool frames_submitted = snapshot.visible_frame.latest_submitted_version > 0;
		const bool clean_exit =
			m_startupSucceeded &&
			m_logicSucceeded &&
			m_renderSucceeded &&
			snapshot.load_state != SandboxStandardSceneLoadState::Failed;

		HLogInfo(
			"Sandbox summary: startup={}, logic={}, render={}, scene_loaded={}, visible_frames_built={}, frames_submitted={}, clean_exit={}, load_state={}, sample='{}', reports='{}', failure='{}'.",
			m_startupSucceeded ? "passed" : "failed",
			m_logicSucceeded ? "passed" : "failed",
			m_renderSucceeded ? "passed" : "failed",
			scene_loaded ? "yes" : "no",
			visible_frames_built ? "yes" : "no",
			frames_submitted ? "yes" : "no",
			clean_exit ? "yes" : "no",
			get_standard_scene_load_state_name(snapshot.load_state),
			snapshot.sample_asset_path.generic_string(),
			m_reportRoot.string(),
			snapshot.failure_detail.empty() ? "None" : snapshot.failure_detail);
	}
}
