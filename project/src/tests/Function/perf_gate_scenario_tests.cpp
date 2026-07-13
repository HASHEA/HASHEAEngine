#include "Function/Application.h"
#include "Function/Diagnostics/PerfGate.h"
#include "Function/Render/Renderer.h"
#include "Function/Scene/Scene.h"
#include "Panels/ViewportPanel.h"

#include "doctest.h"

#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <json.hpp>
#include <string>
#include <vector>

namespace
{
    auto ParsePerfGateArguments(std::initializer_list<const char*> arguments) -> AshEngine::PerfGateConfig
    {
        std::vector<std::string> storage{};
        storage.reserve(arguments.size() + 1u);
        storage.emplace_back("Tests.exe");
        for (const char* pArgument : arguments)
        {
            storage.emplace_back(pArgument ? pArgument : "");
        }

        std::vector<char*> argv{};
        argv.reserve(storage.size());
        for (std::string& strArgument : storage)
        {
            argv.push_back(strArgument.data());
        }
        return AshEngine::parse_perf_gate_config(static_cast<int>(argv.size()), argv.data());
    }

    auto ReadSource(const char* pPath) -> std::string
    {
        std::ifstream input(pPath, std::ios::binary);
        REQUIRE_MESSAGE(input.is_open(), "failed to open source contract file: ", pPath);
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }

    class PerfGateConfigProbeApplication final : public AshEngine::Application
    {
    public:
        PerfGateConfigProbeApplication()
            : AshEngine::Application(MakeConfig())
        {
        }

        auto StoredPerfGateConfig() const -> const AshEngine::PerfGateConfig&
        {
            return get_perf_gate_config();
        }

        auto RequestedBackend() const -> RHI::Backend
        {
            return initConfig.backend;
        }

    private:
        static auto MakeConfig() -> AshEngine::EngineInitConfig
        {
            AshEngine::EngineInitConfig config{};
            config.title = "PerfGateConfigProbe";
            return config;
        }
    };

	class ScenarioGpuTimingContext final : public RHI::IGpuTimingContext
	{
	public:
		auto begin_frame(RHI::CommandBuffer*, uint64_t) -> RHI::GpuTimingResult override
		{
			return RHI::GpuTimingResult::Success;
		}
		auto begin_scope(RHI::CommandBuffer*, uint64_t, RHI::GpuTimingScopeHandle&) -> RHI::GpuTimingResult override
		{
			return RHI::GpuTimingResult::Success;
		}
		auto end_scope(RHI::CommandBuffer*, const RHI::GpuTimingScopeHandle&) -> RHI::GpuTimingResult override
		{
			return RHI::GpuTimingResult::Success;
		}
		auto end_frame(RHI::CommandBuffer*) -> RHI::GpuTimingResult override
		{
			return RHI::GpuTimingResult::Success;
		}
		auto try_collect(RHI::GpuTimingFrameSnapshot& outSnapshot) -> RHI::GpuTimingResult override
		{
			if (_snapshots.empty())
			{
				return RHI::GpuTimingResult::Pending;
			}
			outSnapshot = _snapshots.front();
			_snapshots.pop_front();
			return RHI::GpuTimingResult::Success;
		}
		void Push(uint64_t uFrameIndex, double fElapsedMs)
		{
			RHI::GpuTimingFrameSnapshot snapshot{};
			snapshot.submitted_frame_index = uFrameIndex;
			snapshot.frame_elapsed_ms = fElapsedMs;
			_snapshots.push_back(snapshot);
		}

	private:
		std::deque<RHI::GpuTimingFrameSnapshot> _snapshots{};
	};
}

TEST_CASE("PerfGate scenario parses fixed Empty extent and timing validation")
{
    const AshEngine::PerfGateConfig config = ParsePerfGateArguments({
        "--perf-gate",
        "--perf-gate-scenario=Empty",
        "--perf-gate-width=2560",
        "--perf-gate-height=1440",
        "--perf-gate-timing-validation"
    });

    CHECK(config.enabled);
    CHECK(config.valid);
    CHECK(config.scenario == "Empty");
    CHECK(config.render_output_width == 2560u);
    CHECK(config.render_output_height == 1440u);
    CHECK(config.timing_validation);
}

TEST_CASE("PerfGate scenario rejects invalid Empty extent and unsupported scenarios")
{
    const struct
    {
        const char* pScenario;
        const char* pWidth;
        const char* pHeight;
    } cases[] = {
        { "Empty", "0", "1440" },
        { "Empty", "-1", "1440" },
        { "Empty", "2560", "0" },
        { "Empty", "2560", "-1" },
        { "Empty", "8193", "1440" },
        { "Empty", "2560", "8193" },
        { "Terrain", "2560", "1440" },
    };

    for (const auto& testCase : cases)
    {
        CAPTURE(testCase.pScenario, testCase.pWidth, testCase.pHeight);
        const std::string strScenario = std::string("--perf-gate-scenario=") + testCase.pScenario;
        const std::string strWidth = std::string("--perf-gate-width=") + testCase.pWidth;
        const std::string strHeight = std::string("--perf-gate-height=") + testCase.pHeight;
        const AshEngine::PerfGateConfig config = ParsePerfGateArguments({
            "--perf-gate",
            strScenario.c_str(),
            strWidth.c_str(),
            strHeight.c_str()
        });
        CHECK_FALSE(config.valid);
        CHECK_FALSE(config.validation_error.empty());
    }
}

TEST_CASE("PerfGate scenario viewport extent policy keeps panel and fixed output modes independent")
{
    const AshEditor::ViewportOutputExtent normal =
        AshEditor::ResolveViewportOutputExtent(AshEditor::EditorViewportIds::Scene, 1733u, 911u, 2560u, 1440u);
    CHECK(normal.uWidth == 1733u);
    CHECK(normal.uHeight == 911u);

    const AshEditor::ViewportOutputExtent perf =
        AshEditor::ResolveViewportOutputExtent(AshEditor::EditorViewportIds::Game, 1733u, 911u, 2560u, 1440u);
    CHECK(perf.uWidth == 2560u);
    CHECK(perf.uHeight == 1440u);
}

TEST_CASE("PerfGate scenario report separates render output from swapchain extent and resolved backend")
{
    const std::filesystem::path outputPath =
        std::filesystem::temp_directory_path() /
        ("ashengine-perf-gate-empty-scenario-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");

    AshEngine::PerfGateConfig config{};
    config.enabled = true;
    config.scenario = "Empty";
    config.render_output_width = 2560u;
    config.render_output_height = 1440u;
    config.output_path = outputPath.string();
    config.warmup_seconds = 0.0;
    config.sample_seconds = 60.0;

    AshEngine::PerfGateController controller{};
    controller.configure(config, "Editor", RHI::Backend::DirectX12);
    controller.report_render_output_extent(2560u, 1440u);
    controller.begin(16u);

    AshEngine::RendererFrameStats frameStats{};
    frameStats.submitted_frame_index = 17u;
    frameStats.frame_width = 1920u;
    frameStats.frame_height = 1080u;
    frameStats.cpu_frame_time_ms = 2.0;
    frameStats.gpu_timing_record_result = RHI::GpuTimingResult::Success;
    controller.sample_after_frame(frameStats);
    REQUIRE(controller.write_report(false));

    CHECK(frameStats.frame_width == 1920u);
    CHECK(frameStats.frame_height == 1080u);

    std::ifstream input(outputPath);
    REQUIRE(input.is_open());
    const nlohmann::json report = nlohmann::json::parse(input);
    CHECK(report.at("backend_actual") == "DX12");
    CHECK(report.at("scenario") == "Empty");
    CHECK(report.at("readiness").at("status") == "complete");
    CHECK(report.at("readiness").at("submitted_frame_index") == 16u);
    CHECK(report.at("render_output").at("status") == "complete");
    CHECK(report.at("render_output").at("width") == 2560u);
    CHECK(report.at("render_output").at("height") == 1440u);
    CHECK(report.at("swapchain").at("width") == 1920u);
    CHECK(report.at("swapchain").at("height") == 1080u);

    input.close();
    std::error_code removeError{};
    std::filesystem::remove(outputPath, removeError);
}

TEST_CASE("PerfGate scenario stores pre-initialize input and backend override without early configuration")
{
    PerfGateConfigProbeApplication application{};
    AshEngine::PerfGateConfig config{};
    config.enabled = true;
    config.scenario = "Empty";
    config.render_output_width = 2560u;
    config.render_output_height = 1440u;

    application.set_backend_override(RHI::Backend::Vulkan);
    application.set_perf_gate_config(config);

    CHECK(application.RequestedBackend() == RHI::Backend::Vulkan);
    CHECK(application.StoredPerfGateConfig().scenario == "Empty");
    CHECK(application.StoredPerfGateConfig().render_output_width == 2560u);
    CHECK(application.StoredPerfGateConfig().render_output_height == 1440u);
}

TEST_CASE("PerfGate scenario discards pre-ready snapshots and starts from the readiness watermark")
{
	AshEngine::PerfGateConfig config{};
	config.enabled = true;
	config.timing_validation = true;
	AshEngine::PerfGateController controller{};
	controller.configure(config, "Editor", RHI::Backend::Vulkan);

	ScenarioGpuTimingContext timing{};
	timing.Push(7u, 1.0);
	controller.drain_gpu_timing(timing);
	CHECK_FALSE(controller.is_started());
	CHECK_FALSE(controller.has_failed());
	CHECK_FALSE(controller.should_request_exit());
	CHECK(controller.received_gpu_frame_count() == 0u);

	controller.begin(11u);
	CHECK(controller.is_started());
	CHECK_FALSE(controller.should_request_exit());
	timing.Push(10u, 1.1);
	timing.Push(11u, 1.15);
	controller.drain_gpu_timing(timing);
	CHECK_FALSE(controller.has_failed());
	CHECK_FALSE(controller.should_request_exit());
	CHECK(controller.received_gpu_frame_count() == 0u);

	AshEngine::RendererFrameStats stats{};
	stats.submitted_frame_index = 12u;
	stats.gpu_timing_record_result = RHI::GpuTimingResult::Success;
	controller.sample_after_frame(stats);
	CHECK(controller.expected_gpu_frame_count() == 1u);
	CHECK_FALSE(controller.should_request_exit());
	timing.Push(12u, 1.2);
	controller.drain_gpu_timing(timing);
	CHECK_FALSE(controller.has_failed());
	CHECK(controller.received_gpu_frame_count() == 1u);
	CHECK(controller.should_request_exit());
	CHECK(controller.is_complete_success());
}

TEST_CASE("PerfGate scenario rejects an allocated render output that does not match the fixed extent")
{
	AshEngine::PerfGateConfig config{};
	config.enabled = true;
	config.scenario = "Empty";
	config.render_output_width = 2560u;
	config.render_output_height = 1440u;
	AshEngine::PerfGateController controller{};
	controller.configure(config, "Editor", RHI::Backend::Vulkan);

	CHECK_FALSE(controller.has_render_output_mismatch());
	CHECK_FALSE(controller.has_failed());
	CHECK_FALSE(controller.is_complete_success());

	controller.report_render_output_extent(1920u, 1080u);
	CHECK(controller.has_render_output_mismatch());
	CHECK(controller.has_failed());
	CHECK(controller.should_request_exit());
	CHECK_FALSE(controller.is_complete_success());
}

TEST_CASE("PerfGate scenario is incomplete until readiness output and timed samples all complete")
{
	AshEngine::PerfGateConfig config{};
	config.enabled = true;
	config.timing_validation = true;
	config.scenario = "Empty";
	config.render_output_width = 2560u;
	config.render_output_height = 1440u;
	AshEngine::PerfGateController controller{};
	controller.configure(config, "Editor", RHI::Backend::Vulkan);
	CHECK_FALSE(controller.is_complete_success());

	controller.report_render_output_extent(2560u, 1440u);
	controller.begin(20u);
	CHECK_FALSE(controller.is_complete_success());

	AshEngine::RendererFrameStats stats{};
	stats.submitted_frame_index = 21u;
	stats.cpu_frame_time_ms = 1.0;
	stats.gpu_timing_record_result = RHI::GpuTimingResult::Success;
	controller.sample_after_frame(stats);
	CHECK_FALSE(controller.is_complete_success());

	ScenarioGpuTimingContext timing{};
	timing.Push(21u, 1.1);
	controller.drain_gpu_timing(timing);
	CHECK(controller.is_complete_success());
}

TEST_CASE("PerfGate scenario source contract records scene output passes inside the timed main frame")
{
    const std::string application = ReadSource("project/src/engine/Function/Application.cpp");
    const size_t render = application.find("auto Application::_on_render() -> void");
    const size_t nextFunction = application.find("auto Application::_run_scene_presentation_update_phase", render);
    REQUIRE(render != std::string::npos);
    REQUIRE(nextFunction != std::string::npos);
    const std::string renderSource = application.substr(render, nextFunction - render);
    const size_t beginFrame = renderSource.find("renderer->begin_frame()");
    const size_t submitSceneOutput = renderSource.find("_run_scene_presentation_submit_phase()");
    const size_t endFrame = renderSource.find("renderer->end_frame()");
    REQUIRE(beginFrame != std::string::npos);
    REQUIRE(submitSceneOutput != std::string::npos);
    REQUIRE(endFrame != std::string::npos);
    CHECK(beginFrame < submitSceneOutput);
    CHECK(submitSceneOutput < endFrame);
}

TEST_CASE("PerfGate scenario deterministic Empty scene contains only canonical environment entities")
{
    std::ifstream sceneInput("product/assets/scenes/TerrainPerfEmpty.scene.json");
    std::ifstream manifestInput("product/assets/scenes/TerrainPerfEmpty.manifest.json");
    REQUIRE(sceneInput.is_open());
    REQUIRE(manifestInput.is_open());

    const nlohmann::json scene = nlohmann::json::parse(sceneInput);
    const nlohmann::json manifest = nlohmann::json::parse(manifestInput);
    CHECK(scene.at("version") == 5);
    CHECK(scene.at("entities").size() == 4u);

    uint32_t cameraCount = 0u;
    uint32_t environmentCount = 0u;
    uint32_t lightCount = 0u;
    for (const nlohmann::json& entity : scene.at("entities"))
    {
        cameraCount += entity.contains("camera") ? 1u : 0u;
        environmentCount += entity.contains("environment") ? 1u : 0u;
        lightCount += entity.contains("light") ? 1u : 0u;
        CHECK_FALSE(entity.contains("terrain"));
        CHECK_FALSE(entity.contains("mesh"));
        CHECK_FALSE(entity.contains("particle"));
    }
    CHECK(cameraCount == 1u);
    CHECK(environmentCount == 1u);
    CHECK(lightCount == 1u);

	std::string loadError{};
	const AshEngine::Scene loadedScene = AshEngine::Scene::load_from_file(
		"product/assets/scenes/TerrainPerfEmpty.scene.json",
		&loadError);
	REQUIRE_MESSAGE(loadedScene.is_valid(), loadError);
	CHECK(loadedScene.get_name() == "TerrainPerfEmpty");
	CHECK(loadedScene.get_entity_count() == 4u);
	uint32_t loadedCameraCount = 0u;
	uint32_t loadedEnvironmentCount = 0u;
	uint32_t loadedLightCount = 0u;
	for (const AshEngine::Entity& entity : loadedScene.get_entities())
	{
		CHECK_FALSE(entity.has_mesh_component());
		CHECK_FALSE(entity.has_particle_component());
		if (entity.has_camera_component())
		{
			++loadedCameraCount;
			CHECK(entity.get_camera_component().primary);
		}
		if (entity.has_environment_component())
		{
			++loadedEnvironmentCount;
			CHECK(entity.get_environment_component().active);
		}
		if (entity.has_light_component())
		{
			++loadedLightCount;
			const AshEngine::LightComponent light = entity.get_light_component();
			CHECK(light.type == AshEngine::LightType::Directional);
			CHECK(light.casts_shadow);
			CHECK(light.sunlight);
			CHECK(light.shadow_cascade_count > 0u);
			CHECK(light.shadow_distance > 0.0f);
		}
	}
	CHECK(loadedCameraCount == 1u);
	CHECK(loadedEnvironmentCount == 1u);
	CHECK(loadedLightCount == 1u);
	const AshEngine::SceneRenderConfig& renderConfig = loadedScene.get_render_config();
	CHECK(renderConfig.ambient_occlusion.mode == AshEngine::AmbientOcclusionMode::Off);
	CHECK_FALSE(renderConfig.bloom.enabled);
	CHECK(renderConfig.directional_shadows.enabled);
	CHECK_FALSE(renderConfig.temporal_aa.enabled);
	CHECK_FALSE(renderConfig.volumetric_lighting.enabled);
	CHECK(renderConfig.tonemap.exposure == doctest::Approx(1.0f));

    CHECK(manifest.at("schema_version") == 1);
	CHECK(manifest.at("scene") == "TerrainPerfEmpty.scene.json");
    CHECK(manifest.at("hash_algorithm") == "SHA-256");
    CHECK(manifest.at("canonical_contract_sha256").get<std::string>().size() == 64u);
	const nlohmann::json& canonicalContract = manifest.at("canonical_contract");
	CHECK(canonicalContract.dump() == manifest.at("canonical_contract_json").get<std::string>());
	CHECK(canonicalContract.at("camera_entity") == scene.at("entities").at(1));
	CHECK(canonicalContract.at("environment_entity") == scene.at("entities").at(2));
	CHECK(canonicalContract.at("light_entity") == scene.at("entities").at(3));
	CHECK(canonicalContract.at("render_config") == scene.at("scene_config"));
}
