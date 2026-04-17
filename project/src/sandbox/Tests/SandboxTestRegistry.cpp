#include "Tests/SandboxTestRegistry.h"

#include "Base/hlog.h"
#include "Demos/CodexLogoDemoRenderer.h"
#include "Function/Asset/AssetData.h"
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace AshSandbox
{
	namespace
	{
		struct SampleModelEntry
		{
			const char* path = nullptr;
			bool exercise_flattened_mesh = true;
		};

		struct PendingSampleLoad
		{
			std::filesystem::path sample_path{};
			AshEngine::AssetId asset_id = 0;
			bool exercise_flattened_mesh = false;
			std::shared_future<std::shared_ptr<const AshEngine::Model>> model_future{};
			std::shared_future<std::shared_ptr<const AshEngine::Mesh>> mesh_future{};
		};

		static constexpr std::array<SampleModelEntry, 4> k_sample_models =
		{{
			{ "models/gltfs/Avocado/glTF/Avocado.gltf", true },
			{ "models/gltfs/BoomBox/glTF/BoomBox.gltf", true },
			{ "models/gltfs/DamagedHelmet/glTF/DamagedHelmet.gltf", true },
			{ "models/gltfs/Sponza/glTF/Sponza.gltf", false },
		}};

		static constexpr const char* k_scene_render_test_name = "SceneRenderFlowSmoke";
		static constexpr uint64_t k_scene_render_min_logic_ticks = 1;
		static constexpr uint64_t k_scene_render_min_render_ticks = 1;
		static constexpr SampleModelEntry k_scene_render_sample =
		{
			"models/gltfs/BoomBox/glTF/BoomBox.gltf",
			true
		};

		static auto make_failure(std::string& out_error, const std::string& message) -> bool
		{
			out_error = message;
			return false;
		}

		static auto set_scene_render_failure(SandboxSceneRenderFlowState& state, std::string_view message) -> void
		{
			state.status = SandboxSceneRenderFlowStatus::Failed;
			state.status_detail = std::string(message);
		}

		static auto status_to_waiting_detail(SandboxSceneRenderFlowStatus status) -> const char*
		{
			switch (status)
			{
			case SandboxSceneRenderFlowStatus::ScenePrepared:
				return "Scene prepared and waiting for logic-side visible-frame integration.";
			case SandboxSceneRenderFlowStatus::AwaitingLogicIntegration:
				return "Waiting for logic-side visible-frame integration.";
			case SandboxSceneRenderFlowStatus::VisibleFrameReady:
				return "Visible frame prepared and waiting for render-side submission integration.";
			case SandboxSceneRenderFlowStatus::AwaitingRenderIntegration:
				return "Waiting for render-side scene submission integration.";
			case SandboxSceneRenderFlowStatus::RenderSubmitted:
				return "Scene-render flow exercised.";
			case SandboxSceneRenderFlowStatus::Failed:
				return "Scene-render flow failed.";
			case SandboxSceneRenderFlowStatus::Idle:
			default:
				return "Scene-render flow is idle.";
			}
		}

		static auto mark_scene_render_waiting(
			SandboxSceneRenderFlowState& state,
			SandboxSceneRenderFlowStatus status,
			const char* detail,
			bool& wait_logged,
			const char* log_message) -> void
		{
			if (state.status != SandboxSceneRenderFlowStatus::Failed)
			{
				state.status = status;
				state.status_detail = detail ? detail : status_to_waiting_detail(status);
			}

			if (!wait_logged)
			{
				HLogInfo("{}", log_message);
				wait_logged = true;
			}
		}

		class AssetPipelineSmokeTest final : public ISandboxTest
		{
		public:
			auto get_name() const -> const char* override
			{
				return "AssetPipelineSmoke";
			}

			auto on_startup(SandboxTestContext& context, std::string& out_error) -> bool override
			{
				if (!context.asset_database || !context.asset_database->is_valid())
				{
					return make_failure(out_error, "Asset database is unavailable.");
				}

				std::error_code createError{};
				std::filesystem::create_directories(context.report_root / "generated-assets", createError);
				if (createError)
				{
					return make_failure(out_error, "Failed to create generated-assets report directory: " + createError.message());
				}
				std::filesystem::create_directories(context.report_root / "generated-scenes", createError);
				if (createError)
				{
					return make_failure(out_error, "Failed to create generated-scenes report directory: " + createError.message());
				}

				std::vector<PendingSampleLoad> pendingLoads{};
				pendingLoads.reserve(k_sample_models.size());
				for (const SampleModelEntry& sample : k_sample_models)
				{
					const std::filesystem::path samplePath{ sample.path };
					const AshEngine::AssetInfo* assetInfo = context.asset_database->find_asset_by_path(samplePath);
					if (!assetInfo)
					{
						return make_failure(out_error, "Sample asset is missing from AssetDatabase: " + samplePath.generic_string());
					}

					PendingSampleLoad pending{};
					pending.sample_path = samplePath;
					pending.asset_id = assetInfo->id;
					pending.exercise_flattened_mesh = sample.exercise_flattened_mesh;
					pending.model_future = context.asset_database->load_model_by_path_async(samplePath);
					if (sample.exercise_flattened_mesh)
					{
						pending.mesh_future = context.asset_database->load_mesh_by_path_async(samplePath);
					}
					pendingLoads.push_back(std::move(pending));
				}

				for (const PendingSampleLoad& pending : pendingLoads)
				{
					const std::string samplePathString = pending.sample_path.generic_string();
					std::shared_ptr<const AshEngine::Model> model{};
					try
					{
						model = pending.model_future.get();
					}
					catch (const std::exception& exception)
					{
						return make_failure(out_error, "Model async load threw for '" + samplePathString + "': " + exception.what());
					}
					if (!model || !model->is_valid())
					{
						const std::string assetError = context.asset_database->get_asset_last_error(pending.asset_id);
						return make_failure(out_error, "Failed to load model '" + samplePathString + "': " + (assetError.empty() ? "Unknown async load failure." : assetError));
					}

					if (pending.exercise_flattened_mesh)
					{
						std::shared_ptr<const AshEngine::Mesh> mesh{};
						try
						{
							mesh = pending.mesh_future.get();
						}
						catch (const std::exception& exception)
						{
							return make_failure(out_error, "Mesh async load threw for '" + samplePathString + "': " + exception.what());
						}
						if (!mesh || !mesh->has_geometry())
						{
							const std::string assetError = context.asset_database->get_asset_last_error(pending.asset_id);
							return make_failure(out_error, "Failed to flatten mesh '" + samplePathString + "': " + (assetError.empty() ? "Unknown async load failure." : assetError));
						}
					}

					AshEngine::AshAsset prefab = AshEngine::make_ashasset_from_model(*model, pending.sample_path);
					if (!prefab.is_valid())
					{
						return make_failure(out_error, "Generated AshAsset is invalid for '" + samplePathString + "'.");
					}

					const std::string sampleStem = pending.sample_path.stem().string();
					const std::filesystem::path prefabPath = context.report_root / "generated-assets" / (sampleStem + ".ashasset");
					std::string ioError{};
					if (!AshEngine::save_ashasset_to_file(prefab, prefabPath, &ioError))
					{
						return make_failure(out_error, "Failed to save generated AshAsset '" + prefabPath.string() + "': " + ioError);
					}

					AshEngine::AshAsset reloadedPrefab{};
					if (!AshEngine::load_ashasset_from_file(prefabPath, reloadedPrefab, &ioError) || !reloadedPrefab.is_valid())
					{
						return make_failure(out_error, "Failed to reload generated AshAsset '" + prefabPath.string() + "': " + ioError);
					}

					AshEngine::Scene scene = AshEngine::Scene::create("SandboxAssetSmoke");
					if (!scene.instantiate_model(*model).is_valid())
					{
						return make_failure(out_error, "Scene::instantiate_model failed for '" + samplePathString + "'.");
					}
					if (!scene.instantiate_ashasset(reloadedPrefab).is_valid())
					{
						return make_failure(out_error, "Scene::instantiate_ashasset failed for '" + samplePathString + "'.");
					}
					if (!scene.instantiate_asset(*context.asset_database, pending.sample_path).is_valid())
					{
						return make_failure(out_error, "Scene::instantiate_asset failed for '" + samplePathString + "'.");
					}

					const std::filesystem::path scenePath = context.report_root / "generated-scenes" / (sampleStem + ".ashscene");
					if (!scene.save_to_file(scenePath, &ioError))
					{
						return make_failure(out_error, "Failed to save generated scene '" + scenePath.string() + "': " + ioError);
					}

					AshEngine::Scene reloadedScene = AshEngine::Scene::load_from_file(scenePath, &ioError);
					if (!reloadedScene.is_valid() || reloadedScene.get_entity_count() == 0)
					{
						return make_failure(out_error, "Failed to reload generated scene '" + scenePath.string() + "': " + ioError);
					}

					HLogInfo(
						"Sandbox sample '{}' passed. meshes={}, nodes={}, scene_entities={}.",
						samplePathString,
						model->meshes.size(),
						model->nodes.size(),
						reloadedScene.get_entity_count());
				}

				out_error.clear();
				return true;
			}
		};

		class SceneRenderFlowSmokeTest final : public ISandboxTest
		{
		public:
			auto get_name() const -> const char* override
			{
				return k_scene_render_test_name;
			}

			auto wants_logic_update() const -> bool override
			{
				return true;
			}

			auto wants_render() const -> bool override
			{
				return true;
			}

			auto on_startup(SandboxTestContext& context, std::string& out_error) -> bool override
			{
				if (!context.asset_database || !context.asset_database->is_valid())
				{
					return make_failure(out_error, "Asset database is unavailable.");
				}
				if (!context.scene_render_flow)
				{
					return make_failure(out_error, "Scene-render flow state is unavailable.");
				}

				const std::filesystem::path samplePath{ k_scene_render_sample.path };
				const AshEngine::AssetInfo* assetInfo = context.asset_database->find_asset_by_path(samplePath);
				if (!assetInfo)
				{
					return make_failure(out_error, "Scene-render sample asset is missing from AssetDatabase: " + samplePath.generic_string());
				}

				std::shared_ptr<const AshEngine::Model> model{};
				try
				{
					model = context.asset_database->load_model_by_path_async(samplePath).get();
				}
				catch (const std::exception& exception)
				{
					return make_failure(out_error, "Scene-render sample load threw for '" + samplePath.generic_string() + "': " + exception.what());
				}
				if (!model || !model->is_valid())
				{
					const std::string assetError = context.asset_database->get_asset_last_error(assetInfo->id);
					return make_failure(out_error, "Failed to load scene-render sample '" + samplePath.generic_string() + "': " + (assetError.empty() ? "Unknown async load failure." : assetError));
				}

				AshEngine::Scene scene = AshEngine::Scene::create("SandboxSceneRenderFlow");
				AshEngine::Entity rootEntity = scene.instantiate_model(*model, {}, "SandboxSceneRenderRoot");
				if (!rootEntity.is_valid())
				{
					return make_failure(out_error, "Scene::instantiate_model failed for scene-render smoke sample.");
				}

				AshEngine::Entity cameraEntity = scene.create_entity("SandboxSceneRenderCamera");
				if (!cameraEntity.is_valid())
				{
					return make_failure(out_error, "Failed to create scene-render smoke camera entity.");
				}

				AshEngine::TransformComponent cameraTransform = cameraEntity.get_transform_component();
				cameraTransform.position = { 0.0f, 1.0f, 4.5f };
				cameraTransform.rotation_euler_degrees = { -8.0f, 180.0f, 0.0f };
				if (!cameraEntity.set_transform_component(cameraTransform))
				{
					return make_failure(out_error, "Failed to configure scene-render smoke camera transform.");
				}

				AshEngine::CameraComponent cameraComponent{};
				cameraComponent.primary = true;
				cameraComponent.fov_y_degrees = 60.0f;
				cameraComponent.near_plane = 0.1f;
				cameraComponent.far_plane = 250.0f;
				if (!cameraEntity.add_camera_component(cameraComponent))
				{
					return make_failure(out_error, "Failed to add scene-render smoke camera component.");
				}

				std::error_code createError{};
				const std::filesystem::path flowReportRoot = context.report_root / "scene-render-flow";
				std::filesystem::create_directories(flowReportRoot, createError);
				if (createError)
				{
					return make_failure(out_error, "Failed to create scene-render-flow report directory: " + createError.message());
				}

				std::string ioError{};
				const std::filesystem::path generatedScenePath = flowReportRoot / "scene-render-flow.ashscene";
				if (!scene.save_to_file(generatedScenePath, &ioError))
				{
					return make_failure(out_error, "Failed to save scene-render smoke scene '" + generatedScenePath.string() + "': " + ioError);
				}

				{
					std::scoped_lock<std::mutex> flowLock(context.scene_render_flow->mutex);
					SandboxSceneRenderFlowState& state = *context.scene_render_flow;
					state.scene = scene;
					state.sample_asset_path = samplePath;
					state.generated_scene_path = generatedScenePath;
					state.render_scene = {};
					state.scene_view = {};
					state.visible_frame.reset();
					state.startup_frame_index = context.frame_index;
					state.logic_tick_count = 0;
					state.render_tick_count = 0;
					state.visible_frame_build_count = 0;
					state.render_submission_count = 0;
					state.scene_prepared = true;
					state.visible_frame_ready = false;
					state.render_submission_exercised = false;
					state.logic_hook_wait_logged = false;
					state.render_hook_wait_logged = false;
					state.status = SandboxSceneRenderFlowStatus::ScenePrepared;
					state.status_detail = "Prepared sample scene for the scene-render smoke path.";
				}

				HLogInfo(
					"Sandbox scene-render smoke prepared sample='{}', entities={}, scene='{}'.",
					samplePath.generic_string(),
					scene.get_entity_count(),
					generatedScenePath.string());

				out_error.clear();
				return true;
			}

			auto on_logic_update(SandboxTestContext& context, std::string& out_error) -> bool override
			{
				if (!context.scene_render_flow || !context.scene_render_hooks)
				{
					return make_failure(out_error, "Scene-render flow state or hooks are unavailable.");
				}

				std::scoped_lock<std::mutex> flowLock(context.scene_render_flow->mutex);
				SandboxSceneRenderFlowState& state = *context.scene_render_flow;
				if (!state.scene_prepared || !state.scene.is_valid())
				{
					set_scene_render_failure(state, "Scene-render smoke scene was not prepared before logic update.");
					return make_failure(out_error, state.status_detail);
				}

				++state.logic_tick_count;
				if (state.visible_frame_ready || !context.scene_render_hooks->build_visible_frame)
				{
					if (!context.scene_render_hooks->build_visible_frame && state.status == SandboxSceneRenderFlowStatus::ScenePrepared)
					{
						state.status = SandboxSceneRenderFlowStatus::AwaitingLogicIntegration;
						state.status_detail = "Scene prepared but no visible-frame hook is registered.";
					}
					out_error.clear();
					return true;
				}

				std::string hookError{};
				const bool buildSucceeded = context.scene_render_hooks->build_visible_frame(context, state, hookError);
				if (!buildSucceeded)
				{
					set_scene_render_failure(state, hookError.empty() ? "Scene-render visible-frame build failed." : hookError);
					return make_failure(out_error, state.status_detail);
				}

				++state.visible_frame_build_count;
				if (state.status == SandboxSceneRenderFlowStatus::ScenePrepared)
				{
					state.status = SandboxSceneRenderFlowStatus::AwaitingLogicIntegration;
					state.status_detail = status_to_waiting_detail(state.status);
				}
				else if (state.visible_frame_ready && state.status != SandboxSceneRenderFlowStatus::RenderSubmitted)
				{
					state.status = SandboxSceneRenderFlowStatus::VisibleFrameReady;
					state.status_detail = status_to_waiting_detail(state.status);
				}

				out_error.clear();
				return true;
			}

			auto on_render(SandboxTestContext& context, const std::shared_ptr<AshEngine::RenderTarget>& output_target, std::string& out_error) -> bool override
			{
				if (!context.scene_render_flow || !context.scene_render_hooks)
				{
					return make_failure(out_error, "Scene-render flow state or hooks are unavailable.");
				}

				std::scoped_lock<std::mutex> flowLock(context.scene_render_flow->mutex);
				SandboxSceneRenderFlowState& state = *context.scene_render_flow;
				if (!state.scene_prepared || !state.scene.is_valid())
				{
					mark_scene_render_waiting(
						state,
						SandboxSceneRenderFlowStatus::Idle,
						"Waiting for logic-thread scene preparation before the first render submission.",
						state.logic_hook_wait_logged,
						"Sandbox scene-render smoke is waiting for logic-thread scene preparation.");
					out_error.clear();
					return true;
				}

				++state.render_tick_count;
				state.logic_hook_wait_logged = false;
				if (!state.visible_frame_ready || !state.visible_frame)
				{
					mark_scene_render_waiting(
						state,
						SandboxSceneRenderFlowStatus::AwaitingLogicIntegration,
						"Waiting for logic-side visible-frame integration.",
						state.render_hook_wait_logged,
						"Sandbox scene-render smoke is waiting for logic-side visible-frame build.");
					out_error.clear();
					return true;
				}

				if (!context.scene_render_hooks->submit_frame)
				{
					state.status = state.visible_frame_ready
						? SandboxSceneRenderFlowStatus::AwaitingRenderIntegration
						: SandboxSceneRenderFlowStatus::AwaitingLogicIntegration;
					state.status_detail = status_to_waiting_detail(state.status);
					out_error.clear();
					return true;
				}

				state.render_hook_wait_logged = false;
				std::string hookError{};
				const bool submitSucceeded = context.scene_render_hooks->submit_frame(context, state, output_target, hookError);
				if (!submitSucceeded)
				{
					set_scene_render_failure(state, hookError.empty() ? "Scene-render submission failed." : hookError);
					return make_failure(out_error, state.status_detail);
				}

				++state.render_submission_count;
				if (state.render_submission_exercised)
				{
					state.status = SandboxSceneRenderFlowStatus::RenderSubmitted;
				}
				else if (state.visible_frame_ready)
				{
					state.status = SandboxSceneRenderFlowStatus::AwaitingRenderIntegration;
				}
				else
				{
					state.status = SandboxSceneRenderFlowStatus::AwaitingLogicIntegration;
				}
				state.status_detail = status_to_waiting_detail(state.status);

				out_error.clear();
				return true;
			}

			auto on_shutdown(SandboxTestContext& context) -> void override
			{
				if (!context.scene_render_flow)
				{
					return;
				}

				std::scoped_lock<std::mutex> flowLock(context.scene_render_flow->mutex);
				SandboxSceneRenderFlowState& state = *context.scene_render_flow;
				HLogInfo(
					"Sandbox scene-render smoke summary: status={}, logic_ticks={}, render_ticks={}, visible_frame_builds={}, render_submissions={}, detail='{}'.",
					get_sandbox_scene_render_flow_status_name(state.status),
					state.logic_tick_count,
					state.render_tick_count,
					state.visible_frame_build_count,
					state.render_submission_count,
					state.status_detail);

				if (state.scene_prepared
					&& state.logic_tick_count >= k_scene_render_min_logic_ticks
					&& state.render_tick_count >= k_scene_render_min_render_ticks
					&& state.visible_frame_build_count > 0
					&& state.render_submission_count > 0
					&& state.render_submission_exercised
					&& state.status == SandboxSceneRenderFlowStatus::RenderSubmitted
					&& state.status != SandboxSceneRenderFlowStatus::Failed
					&& context.request_exit)
				{
					context.request_exit();
				}
			}
		};

		class CodexLogoRenderTest final : public ISandboxTest
		{
		public:
			auto get_name() const -> const char* override
			{
				return "CodexLogoRender";
			}

			auto wants_render() const -> bool override
			{
				return true;
			}

			auto on_startup(SandboxTestContext& context, std::string& out_error) -> bool override
			{
				if (!context.renderer)
				{
					return make_failure(out_error, "Renderer is unavailable.");
				}

				out_error.clear();
				return true;
			}

			auto on_render(SandboxTestContext& context, const std::shared_ptr<AshEngine::RenderTarget>& output_target, std::string& out_error) -> bool override
			{
				(void)context;
				if (!m_renderer.render(output_target))
				{
					return make_failure(out_error, "Codex logo render pass failed.");
				}
				out_error.clear();
				return true;
			}

			auto on_shutdown(SandboxTestContext& context) -> void override
			{
				(void)context;
				m_renderer.shutdown();
			}

		private:
			CodexLogoDemoRenderer m_renderer{};
		};
	}

	auto get_sandbox_scene_render_flow_status_name(SandboxSceneRenderFlowStatus status) -> const char*
	{
		switch (status)
		{
		case SandboxSceneRenderFlowStatus::Idle:
			return "Idle";
		case SandboxSceneRenderFlowStatus::ScenePrepared:
			return "ScenePrepared";
		case SandboxSceneRenderFlowStatus::AwaitingLogicIntegration:
			return "AwaitingLogicIntegration";
		case SandboxSceneRenderFlowStatus::VisibleFrameReady:
			return "VisibleFrameReady";
		case SandboxSceneRenderFlowStatus::AwaitingRenderIntegration:
			return "AwaitingRenderIntegration";
		case SandboxSceneRenderFlowStatus::RenderSubmitted:
			return "RenderSubmitted";
		case SandboxSceneRenderFlowStatus::Failed:
			return "Failed";
		default:
			return "Unknown";
		}
	}

	auto create_default_sandbox_tests() -> std::vector<std::unique_ptr<ISandboxTest>>
	{
		std::vector<std::unique_ptr<ISandboxTest>> tests{};
		tests.push_back(std::make_unique<AssetPipelineSmokeTest>());
		tests.push_back(std::make_unique<SceneRenderFlowSmokeTest>());
		tests.push_back(std::make_unique<CodexLogoRenderTest>());
		return tests;
	}
}
