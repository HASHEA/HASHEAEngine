#include "App/SandboxApplication.h"

#include "Base/hthreading.h"
#include "Base/hlog.h"
#include "Function/Gui/UIContext.h"
#include "Function/Render/ScenePresentationSubsystem.h"
#include <algorithm>
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

		if (!_process_pending_model_selection())
		{
			m_logicSucceeded = false;
			request_exit();
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
		_draw_model_selection_overlay();
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
		return _initialize_model_selection_options();
	}

	auto SandboxApplication::_initialize_model_selection_options() -> bool
	{
		std::vector<std::filesystem::path> asset_paths =
			SandboxStandardScene::discover_sample_asset_paths(m_assetDatabase);
		if (asset_paths.empty())
		{
			HLogError(
				"Sandbox found no selectable glTF models under '{}'.",
				(m_assetRoot / SandboxStandardScene::get_sample_asset_root_path()).string());
			return false;
		}

		const std::filesystem::path canonical_path = SandboxStandardScene::get_canonical_sample_asset_path().lexically_normal();
		auto selected_it = std::find(asset_paths.begin(), asset_paths.end(), canonical_path);
		if (selected_it == asset_paths.end())
		{
			selected_it = asset_paths.begin();
			HLogWarning(
				"Sandbox canonical sample '{}' was not found. Falling back to '{}'.",
				canonical_path.generic_string(),
				selected_it->generic_string());
		}

		std::vector<std::string> labels{};
		labels.reserve(asset_paths.size());
		for (const std::filesystem::path& path : asset_paths)
		{
			labels.push_back(SandboxStandardScene::make_sample_asset_label(path));
		}

		const size_t model_count = asset_paths.size();
		const int32_t selected_index = static_cast<int32_t>(std::distance(asset_paths.begin(), selected_it));
		{
			std::scoped_lock<std::mutex> lock(m_modelSelectionMutex);
			m_modelSelectionState.asset_paths = std::move(asset_paths);
			m_modelSelectionState.labels = std::move(labels);
			m_modelSelectionState.selected_index = selected_index;
			m_modelSelectionState.pending_index = -1;
			m_modelSelectionState.status = "Ready to load";
		}

		HLogInfo("Sandbox discovered {} selectable glTF model(s).", model_count);
		return true;
	}

	auto SandboxApplication::_start_standard_scene() -> bool
	{
		SandboxModelSelectionState selection = _get_model_selection_state_copy();
		if (selection.asset_paths.empty() ||
			selection.selected_index < 0 ||
			selection.selected_index >= static_cast<int32_t>(selection.asset_paths.size()))
		{
			HLogError("Sandbox cannot start the standard scene because no selectable model is active.");
			return false;
		}

		const std::filesystem::path selected_path = selection.asset_paths[static_cast<size_t>(selection.selected_index)];
		if (!m_standardScene.start(m_assetDatabase, selected_path))
		{
			const std::string failure_detail = m_standardScene.get_failure_detail();
			HLogError(
				"Sandbox failed to start the standard-scene runtime: {}",
				failure_detail.empty() ? "Unknown error." : failure_detail);
			_set_model_selection_status(failure_detail.empty() ? "Failed to queue model load" : failure_detail);
			return false;
		}

		HLogInfo(
			"Sandbox standard scene runtime is using '{}'.",
			selected_path.generic_string());
		_set_model_selection_status("Loading " + selected_path.generic_string());
		return true;
	}

	auto SandboxApplication::_switch_standard_scene_model(int32_t model_index) -> bool
	{
		SandboxModelSelectionState selection = _get_model_selection_state_copy();
		if (model_index < 0 || model_index >= static_cast<int32_t>(selection.asset_paths.size()))
		{
			HLogError("Sandbox received an invalid model selection index: {}.", model_index);
			return false;
		}

		const std::filesystem::path selected_path = selection.asset_paths[static_cast<size_t>(model_index)];
		_destroy_standard_scene_presentation();
		m_standardScene.reset();

		if (!m_standardScene.start(m_assetDatabase, selected_path))
		{
			const std::string failure_detail = m_standardScene.get_failure_detail();
			HLogError(
				"Sandbox failed to switch standard scene model to '{}': {}",
				selected_path.generic_string(),
				failure_detail.empty() ? "Unknown error." : failure_detail);
			_set_model_selection_status(failure_detail.empty() ? "Failed to queue model load" : failure_detail);
			return false;
		}

		{
			std::scoped_lock<std::mutex> lock(m_modelSelectionMutex);
			m_modelSelectionState.selected_index = model_index;
			m_modelSelectionState.pending_index = -1;
			m_modelSelectionState.status = "Loading " + selected_path.generic_string();
		}
		HLogInfo("Sandbox switched standard scene model to '{}'.", selected_path.generic_string());
		return true;
	}

	auto SandboxApplication::_process_pending_model_selection() -> bool
	{
		int32_t pending_index = -1;
		{
			std::scoped_lock<std::mutex> lock(m_modelSelectionMutex);
			pending_index = m_modelSelectionState.pending_index;
			m_modelSelectionState.pending_index = -1;
		}

		if (pending_index < 0)
		{
			return true;
		}

		return _switch_standard_scene_model(pending_index);
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

		_sync_model_selection_status_from_scene();
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

	auto SandboxApplication::_draw_model_selection_overlay() -> void
	{
		AshEngine::UIContext* ui = AshEngine::Application::get_ui_context();
		if (ui == nullptr || !ui->is_frame_active())
		{
			return;
		}

		SandboxModelSelectionState selection = _get_model_selection_state_copy();
		if (selection.labels.empty())
		{
			return;
		}

		SandboxStandardSceneSnapshot scene_snapshot = m_standardScene.snapshot();
		const bool is_loading = scene_snapshot.load_state == SandboxStandardSceneLoadState::LoadingModel;
		const bool has_pending_selection = selection.pending_index >= 0;
		const bool selection_enabled = !is_loading && !has_pending_selection;
		int32_t ui_selected_index = selection.selected_index;

		ui->set_next_window_position({ 10.0f, 112.0f }, AshEngine::UIConditionFlagBits::FirstUseEver);
		ui->set_next_window_size({ 560.0f, 0.0f }, AshEngine::UIConditionFlagBits::FirstUseEver);
		ui->push_style_color(AshEngine::UIStyleColorKind::WindowBg, { 0.04f, 0.05f, 0.06f, 0.88f });
		ui->push_style_color(AshEngine::UIStyleColorKind::Border, { 0.20f, 0.22f, 0.24f, 0.90f });
		const AshEngine::UIWindowFlags overlay_flags =
			AshEngine::UIWindowFlagBits::NoDocking |
			AshEngine::UIWindowFlagBits::NoSavedSettings |
			AshEngine::UIWindowFlagBits::AlwaysAutoResize;

		const bool window_visible = ui->begin_window("Sandbox Model", nullptr, overlay_flags);
		if (window_visible)
		{
			const std::string current_path =
				scene_snapshot.sample_asset_path.empty()
					? std::string("None")
					: scene_snapshot.sample_asset_path.generic_string();

			ui->set_next_item_width(420.0f);
			ui->begin_disabled(!selection_enabled);
			const bool changed = ui->combo("Model", ui_selected_index, selection.labels, 12);
			ui->end_disabled();
			if (changed && ui_selected_index != selection.selected_index)
			{
				_request_model_selection_from_ui(ui_selected_index);
			}

			ui->text("State: %s", get_standard_scene_load_state_name(scene_snapshot.load_state));
			ui->text_wrapped("Path: %s", current_path.c_str());
			if (!selection.status.empty())
			{
				ui->text_wrapped("Status: %s", selection.status.c_str());
			}
			if (!scene_snapshot.failure_detail.empty())
			{
				ui->text_colored(
					{ 1.0f, 0.35f, 0.28f, 1.0f },
					"Failure: %s",
					scene_snapshot.failure_detail.c_str());
			}
		}
		ui->end_window();
		ui->pop_style_color(2);
	}

	auto SandboxApplication::_request_model_selection_from_ui(int32_t model_index) -> void
	{
		std::scoped_lock<std::mutex> lock(m_modelSelectionMutex);
		if (model_index < 0 || model_index >= static_cast<int32_t>(m_modelSelectionState.asset_paths.size()))
		{
			return;
		}
		if (model_index == m_modelSelectionState.selected_index)
		{
			m_modelSelectionState.pending_index = -1;
			return;
		}

		m_modelSelectionState.pending_index = model_index;
		m_modelSelectionState.status =
			"Queued " + m_modelSelectionState.asset_paths[static_cast<size_t>(model_index)].generic_string();
	}

	auto SandboxApplication::_get_model_selection_state_copy() const -> SandboxModelSelectionState
	{
		std::scoped_lock<std::mutex> lock(m_modelSelectionMutex);
		return m_modelSelectionState;
	}

	auto SandboxApplication::_set_model_selection_status(const std::string& status) -> void
	{
		std::scoped_lock<std::mutex> lock(m_modelSelectionMutex);
		m_modelSelectionState.status = status;
	}

	auto SandboxApplication::_sync_model_selection_status_from_scene() -> void
	{
		const SandboxStandardSceneSnapshot snapshot = m_standardScene.snapshot();
		std::string status{};
		switch (snapshot.load_state)
		{
		case SandboxStandardSceneLoadState::Idle:
			status = "Idle";
			break;
		case SandboxStandardSceneLoadState::LoadingModel:
			status = "Loading " + snapshot.sample_asset_path.generic_string();
			break;
		case SandboxStandardSceneLoadState::Ready:
			status = "Ready " + snapshot.sample_asset_path.generic_string();
			break;
		case SandboxStandardSceneLoadState::Failed:
			status = snapshot.failure_detail.empty() ? "Failed" : snapshot.failure_detail;
			break;
		default:
			status = "Unknown";
			break;
		}
		_set_model_selection_status(status);
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
			"Sandbox summary: startup={}, logic={}, render={}, scene_loaded={}, scene_ready={}, presentation_registered={}, clean_exit={}, load_state={}, sample='{}', reports='{}', failure='{}'.",
			m_startupSucceeded ? "passed" : "failed",
			m_logicSucceeded ? "passed" : "failed",
			m_renderSucceeded ? "passed" : "failed",
			scene_loaded ? "yes" : "no",
			scene_ready ? "yes" : "no",
			presentation_registered ? "yes" : "no",
			clean_exit ? "yes" : "no",
			get_standard_scene_load_state_name(snapshot.load_state),
			snapshot.sample_asset_path.generic_string(),
			m_reportRoot.string(),
			snapshot.failure_detail.empty() ? "None" : snapshot.failure_detail);
	}
}
