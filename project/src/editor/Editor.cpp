#include "Editor.h"

#include "App/EditorApplication.h"
#include "Base/hlog.h"
#include "Core/EditorIds.h"
#include "Core/EditorViewportTypes.h"
#include "Panels/ViewportPanel.h"
#include "Services/EditorViewportService.h"

namespace AshEditor
{
	Editor::Editor(const AshEngine::EngineInitConfig& refConfig)
		: AshEngine::Application(refConfig)
	{
	}

	Editor::~Editor()
	{
		ShutdownEditor();
	}

	void Editor::_on_startup()
	{
		AshEngine::Application::_on_startup();
		HLogInfo("Ash Editor Start !");
		BootstrapEditor();
	}

	void Editor::_on_shutdown()
	{
		ShutdownEditor();
		AshEngine::Application::_on_shutdown();
	}

	void Editor::BootstrapEditor()
	{
		_bootstrapAttempted = true;
		if (_upEditorApplication)
		{
			_bootstrapFailed = false;
			return;
		}

		const AshEngine::PerfGateConfig& refPerfGateConfig = get_perf_gate_config();
		_bPerfGateBenchmark = refPerfGateConfig.enabled && refPerfGateConfig.scenario == "Empty";
		_bPerfGateRenderOutputReady = false;
		ConfigurePerfGateViewportOutputExtent(
			_bPerfGateBenchmark ? refPerfGateConfig.render_output_width : 0u,
			_bPerfGateBenchmark ? refPerfGateConfig.render_output_height : 0u);

		EditorApplicationStartupOptions startupOptions{};
		startupOptions.pathSceneOverride = get_scene_path_override();
		startupOptions.bDeterministicBenchmarkLayout = _bPerfGateBenchmark;
		_upEditorApplication = std::make_unique<EditorApplication>();
		if (!_upEditorApplication->Initialize(startupOptions))
		{
			_bootstrapFailed = true;
			HLogError("Editor application bootstrap failed.");
			ShutdownEditor();
			request_exit();
		}
		else
		{
			_bootstrapFailed = false;
		}
	}

	void Editor::ShutdownEditor()
	{
		if (_upEditorApplication)
		{
			_upEditorApplication->Shutdown();
			_upEditorApplication.reset();
		}
		ConfigurePerfGateViewportOutputExtent(0u, 0u);
		_bPerfGateBenchmark = false;
		_bPerfGateRenderOutputReady = false;
	}

	void Editor::_on_update()
	{
		AshEngine::Application::_on_update();
		if (_upEditorApplication)
		{
			_upEditorApplication->Update();
			_upEditorApplication->SyncRuntimeScenePresentations();
		}
	}

	void Editor::_on_gui()
	{
		if (_upEditorApplication)
		{
			_upEditorApplication->DrawGui();
		}
	}

	void Editor::_on_render_debug()
	{
		AshEngine::Application::_on_render_debug();
	}

	void Editor::_on_render()
	{
		AshEngine::Application::_on_render();
		if (!_bPerfGateBenchmark || !_upEditorApplication)
		{
			return;
		}

		const EditorViewportInstance* pPrimaryViewport = _upEditorApplication->GetPrimaryViewport();
		if (!pPrimaryViewport || pPrimaryViewport->strId != EditorViewportIds::Game)
		{
			return;
		}

		AshEngine::SceneViewStats stats{};
		if (_upEditorApplication->GetViewportService().TryGetSceneViewStats(pPrimaryViewport->strId, stats) &&
			stats.output_allocated)
		{
			report_perf_gate_render_output_extent(
				stats.allocated_output_width,
				stats.allocated_output_height);
			const AshEngine::PerfGateConfig& refPerfGateConfig = get_perf_gate_config();
			_bPerfGateRenderOutputReady =
				stats.allocated_output_width == refPerfGateConfig.render_output_width &&
				stats.allocated_output_height == refPerfGateConfig.render_output_height;
		}
	}

	auto Editor::_get_automation_readiness() const -> AshEngine::ApplicationReadiness
	{
		if (_bootstrapFailed || (_upEditorApplication && _upEditorApplication->HasAutomationFailure()))
		{
			return AshEngine::ApplicationReadiness::Failed;
		}
		if (_bPerfGateBenchmark && !_bPerfGateRenderOutputReady)
		{
			return AshEngine::ApplicationReadiness::Pending;
		}
		if (_bootstrapAttempted && _upEditorApplication && _upEditorApplication->IsAutomationReady())
		{
			return AshEngine::ApplicationReadiness::Ready;
		}
		return AshEngine::ApplicationReadiness::Pending;
	}
}
