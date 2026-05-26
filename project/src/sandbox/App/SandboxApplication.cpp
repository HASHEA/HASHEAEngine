#include "App/SandboxApplication.h"

#include "Base/hlog.h"
#include "Base/hthreading.h"
#include "Function/Render/ScenePresentationSubsystem.h"
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
			case SandboxStandardSceneLoadState::LoadingScene:
				return "LoadingScene";
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
		_destroy_standard_scene_presentation();
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
		_destroy_standard_scene_presentation();
		m_standardScene.reset();
	}

	auto SandboxApplication::_on_gui() -> void
	{
	}

	auto SandboxApplication::_on_render() -> void
	{
		AshEngine::Application::_on_render();
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
		if (!m_standardScene.start(m_assetDatabase))
		{
			const std::string failure_detail = m_standardScene.get_failure_detail();
			HLogError(
				"Sandbox failed to start the standard-scene runtime: {}",
				failure_detail.empty() ? "Unknown error." : failure_detail);
			return false;
		}

		HLogInfo(
			"Sandbox standard scene runtime is using '{}'.",
			SandboxStandardScene::get_standard_scene_path().generic_string());
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

		if (m_standardScene.is_ready() && !m_mainSceneBinding.is_valid())
		{
			if (!_register_standard_scene_presentation())
			{
				HLogError("Sandbox failed to register scene presentation bindings for the standard scene.");
				return false;
			}
		}
		return true;
	}

	auto SandboxApplication::_register_standard_scene_presentation() -> bool
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		AshEngine::ScenePresentationSubsystem* scene_presentation = AshEngine::Application::get_scene_presentation();
		ASH_PROCESS_ERROR(scene_presentation != nullptr);

		_destroy_standard_scene_presentation();

		AshEngine::SceneOutputDesc output_desc{};
		output_desc.debug_name = "SandboxMainWindow";
		output_desc.kind = AshEngine::SceneOutputKind::Window;
		m_mainSceneOutput = scene_presentation->create_output(output_desc);
		ASH_PROCESS_ERROR(m_mainSceneOutput.is_valid());

		AshEngine::SceneViewBindingDesc binding_desc{};
		binding_desc.debug_name = "SandboxStandardScenePrimaryCamera";
		binding_desc.scene = m_standardScene.get_scene();
		binding_desc.camera.source = AshEngine::SceneCameraSource::PrimaryCamera;
		binding_desc.output = m_mainSceneOutput;
		binding_desc.enabled = true;
		m_mainSceneBinding = scene_presentation->create_view_binding(binding_desc);
		ASH_PROCESS_ERROR(m_mainSceneBinding.is_valid());
		ASH_PROCESS_GUARD_END(bResult, false);
		if (!bResult)
		{
			_destroy_standard_scene_presentation();
		}
		return bResult;
	}

	auto SandboxApplication::_destroy_standard_scene_presentation() -> void
	{
		AshEngine::ScenePresentationSubsystem* scene_presentation = AshEngine::Application::get_scene_presentation();
		if (!scene_presentation)
		{
			m_mainSceneBinding = {};
			m_mainSceneOutput = {};
			return;
		}

		if (m_mainSceneBinding.is_valid())
		{
			scene_presentation->destroy_view_binding(m_mainSceneBinding);
			m_mainSceneBinding = {};
		}
		if (m_mainSceneOutput.is_valid())
		{
			scene_presentation->destroy_output(m_mainSceneOutput);
			m_mainSceneOutput = {};
		}
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
		const bool scene_ready = snapshot.load_state == SandboxStandardSceneLoadState::Ready;
		const bool presentation_registered = m_mainSceneOutput.is_valid() && m_mainSceneBinding.is_valid();
		const bool clean_exit =
			m_startupSucceeded &&
			m_logicSucceeded &&
			m_renderSucceeded &&
			snapshot.load_state != SandboxStandardSceneLoadState::Failed;

		HLogInfo(
			"Sandbox summary: startup={}, logic={}, render={}, scene_loaded={}, scene_ready={}, presentation_registered={}, clean_exit={}, load_state={}, scene='{}', reports='{}', failure='{}'.",
			m_startupSucceeded ? "passed" : "failed",
			m_logicSucceeded ? "passed" : "failed",
			m_renderSucceeded ? "passed" : "failed",
			scene_loaded ? "yes" : "no",
			scene_ready ? "yes" : "no",
			presentation_registered ? "yes" : "no",
			clean_exit ? "yes" : "no",
			get_standard_scene_load_state_name(snapshot.load_state),
			snapshot.scene_path.generic_string(),
			m_reportRoot.string(),
			snapshot.failure_detail.empty() ? "None" : snapshot.failure_detail);
	}
}
