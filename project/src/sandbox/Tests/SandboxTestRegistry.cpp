#include "Tests/SandboxTestRegistry.h"

#include "Base/hlog.h"
#include "Demos/CodexLogoDemoRenderer.h"
#include "Function/Asset/AssetData.h"
#include "Function/Scene/Scene.h"
#include <array>
#include <filesystem>
#include <memory>
#include <string>

namespace AshSandbox
{
	namespace
	{
		struct SampleModelEntry
		{
			const char* path = nullptr;
			bool exercise_flattened_mesh = true;
		};

		static constexpr std::array<SampleModelEntry, 4> k_sample_models =
		{{
			{ "models/gltfs/Avocado/glTF/Avocado.gltf", true },
			{ "models/gltfs/BoomBox/glTF/BoomBox.gltf", true },
			{ "models/gltfs/DamagedHelmet/glTF/DamagedHelmet.gltf", true },
			{ "models/gltfs/Sponza/glTF/Sponza.gltf", false },
		}};

		static auto make_failure(std::string& out_error, const std::string& message) -> bool
		{
			out_error = message;
			return false;
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

				for (const SampleModelEntry& sample : k_sample_models)
				{
					const std::filesystem::path samplePath{ sample.path };
					if (!context.asset_database->find_asset_by_path(samplePath))
					{
						return make_failure(out_error, "Sample asset is missing from AssetDatabase: " + samplePath.generic_string());
					}

					std::shared_ptr<const AshEngine::Model> model{};
					if (!context.asset_database->load_model_by_path(samplePath, model) || !model || !model->is_valid())
					{
						return make_failure(out_error, "Failed to load model '" + samplePath.generic_string() + "': " + context.asset_database->get_last_error());
					}

					if (sample.exercise_flattened_mesh)
					{
						std::shared_ptr<const AshEngine::Mesh> mesh{};
						if (!context.asset_database->load_mesh_by_path(samplePath, mesh) || !mesh || !mesh->has_geometry())
						{
							return make_failure(out_error, "Failed to flatten mesh '" + samplePath.generic_string() + "': " + context.asset_database->get_last_error());
						}
					}

					AshEngine::AshAsset prefab = AshEngine::make_ashasset_from_model(*model, samplePath);
					if (!prefab.is_valid())
					{
						return make_failure(out_error, "Generated AshAsset is invalid for '" + samplePath.generic_string() + "'.");
					}

					const std::string sampleStem = samplePath.stem().string();
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
						return make_failure(out_error, "Scene::instantiate_model failed for '" + samplePath.generic_string() + "'.");
					}
					if (!scene.instantiate_ashasset(reloadedPrefab).is_valid())
					{
						return make_failure(out_error, "Scene::instantiate_ashasset failed for '" + samplePath.generic_string() + "'.");
					}
					if (!scene.instantiate_asset(*context.asset_database, samplePath).is_valid())
					{
						return make_failure(out_error, "Scene::instantiate_asset failed for '" + samplePath.generic_string() + "'.");
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
						samplePath.generic_string(),
						model->meshes.size(),
						model->nodes.size(),
						reloadedScene.get_entity_count());
				}

				out_error.clear();
				return true;
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

	auto create_default_sandbox_tests() -> std::vector<std::unique_ptr<ISandboxTest>>
	{
		std::vector<std::unique_ptr<ISandboxTest>> tests{};
		tests.push_back(std::make_unique<AssetPipelineSmokeTest>());
		tests.push_back(std::make_unique<CodexLogoRenderTest>());
		return tests;
	}
}
