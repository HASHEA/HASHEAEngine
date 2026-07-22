#include "Function/Render/RenderGraphBuilder.h"
#include "Function/Render/RenderGraphCompiler.h"
#include "Function/Render/RenderGraphExecutor.h"
#include "Function/Render/RenderScene.h"
#include "Function/Render/TerrainLod.h"
#include "Function/Render/TerrainRenderPass.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace
{
	auto ReadSource(const char* path) -> std::string
	{
		std::ifstream input(path, std::ios::binary);
		REQUIRE(input.is_open());
		return std::string(
			std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>());
	}

	auto CountText(const std::string& source, const std::string& text) -> size_t
	{
		size_t count = 0u;
		size_t offset = 0u;
		while ((offset = source.find(text, offset)) != std::string::npos)
		{
			++count;
			offset += text.size();
		}
		return count;
	}

	auto CompactSource(const std::string& source) -> std::string
	{
		std::string compact{};
		compact.reserve(source.size());
		for (const unsigned char character : source)
		{
			if (!std::isspace(character))
			{
				compact.push_back(static_cast<char>(character));
			}
		}
		return compact;
	}

	struct TerrainGraphExecutionProbe
	{
		uint32_t staging_writes = 0u;
		uint32_t dispatches = 0u;
		uint32_t raster_draws = 0u;
		bool dispatch_result = false;
		std::vector<std::shared_ptr<AshEngine::RenderTarget>> registered_textures{};
		std::vector<std::string> registered_names{};

		static AshEngine::RenderGraphTextureRef RegisterTexture(
			void* user_data,
			AshEngine::RenderGraphBuilder& graph,
			const std::shared_ptr<AshEngine::RenderTarget>& texture,
			const char* name,
			AshEngine::RenderGraphAccess)
		{
			TerrainGraphExecutionProbe& probe =
				*static_cast<TerrainGraphExecutionProbe*>(user_data);
			probe.registered_textures.push_back(texture);
			probe.registered_names.emplace_back(name ? name : "");
			AshEngine::RenderTargetDesc desc{};
			desc.width = static_cast<uint16_t>(
				AshEngine::k_terrain_weight_atlas_extent);
			desc.height = desc.width;
			desc.format = AshEngine::RenderTextureFormat::RGBA8_UNORM;
			desc.shader_resource = true;
			desc.unordered_access = true;
			desc.name = name;
			return graph.register_external_texture_desc_for_tests(desc, name);
		}

		static bool StageWeightUpload(
			void* user_data,
			const std::shared_ptr<AshEngine::StorageBuffer>&,
			const uint8_t*,
			uint32_t)
		{
			TerrainGraphExecutionProbe& probe =
				*static_cast<TerrainGraphExecutionProbe*>(user_data);
			++probe.staging_writes;
			return true;
		}

		static bool DispatchAtlasUpdate(
			void* user_data,
			AshEngine::RenderGraphComputeContext&,
			const AshEngine::TerrainGraphResources&,
			const AshEngine::TerrainGpuComponentUpload&,
			uint32_t,
			bool)
		{
			TerrainGraphExecutionProbe& probe =
				*static_cast<TerrainGraphExecutionProbe*>(user_data);
			++probe.dispatches;
			return probe.dispatch_result;
		}

		static std::shared_ptr<AshEngine::StorageBuffer> AcquireBuffer(
			void*, const AshEngine::StorageBufferDesc&)
		{
			return std::make_shared<AshEngine::StorageBuffer>();
		}

		static void ReleaseBuffer(
			void*, const std::shared_ptr<AshEngine::StorageBuffer>&)
		{
		}

		static bool SubmitTransitions(
			void*, const AshEngine::RenderGraphResolvedBufferTransition*, size_t)
		{
			return true;
		}

		static bool BeginRaster(
			void*, const AshEngine::PassDesc&,
			AshEngine::Renderer::GraphicsPassContext&)
		{
			return true;
		}

		static bool EndRaster(
			void*, AshEngine::Renderer::GraphicsPassContext&)
		{
			return true;
		}

		AshEngine::RenderGraphExecutionOps MakeOps()
		{
			AshEngine::RenderGraphExecutionOps ops{};
			ops.user_data = this;
			ops.acquire_transient_storage_buffer = &AcquireBuffer;
			ops.release_transient_storage_buffer = &ReleaseBuffer;
			ops.submit_buffer_transitions = &SubmitTransitions;
			ops.begin_raster_pass = &BeginRaster;
			ops.end_raster_pass = &EndRaster;
			return ops;
		}

		AshEngine::TerrainRenderGraphOps MakeTerrainOps()
		{
			AshEngine::TerrainRenderGraphOps ops{};
			ops.user_data = this;
			ops.register_external_texture = &RegisterTexture;
			ops.stage_weight_upload = &StageWeightUpload;
			ops.dispatch_atlas_update = &DispatchAtlasUpdate;
			return ops;
		}
	};
}

namespace AshEngine
{
	struct TerrainRenderGraphTestSeam
	{
		struct CandidateToken
		{
			std::shared_ptr<TerrainRenderRuntimeState> runtime{};
			uint64_t epoch = 0u;
		};

		static CandidateToken InstallPublishedAndCandidate(
			TerrainRenderAsset& asset)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			auto published_snapshot = std::make_shared<TerrainAssetSnapshot>();
			published_snapshot->asset_id = 401u;
			published_snapshot->content_generation = 7u;
			published_snapshot->residency_revision = 2u;
			published_snapshot->layout = make_default_terrain_grid_layout();

			auto published_runtime = std::make_shared<TerrainRenderRuntimeState>();
			published_runtime->target_snapshot = published_snapshot;
			published_runtime->work_status = TerrainRenderWorkStatus::Ready;
			InstallResourceSentinels(published_runtime->resources);
			auto published_view = std::make_shared<TerrainPublishedRenderView>();
			published_view->snapshot = published_snapshot;
			REQUIRE(derive_terrain_render_layout(
				published_snapshot->layout, published_view->layout));
			published_view->runtime = published_runtime;
			published_view->asset_id = published_snapshot->asset_id;
			published_view->content_generation =
				published_snapshot->content_generation;
			published_view->residency_revision =
				published_snapshot->residency_revision;
			published_view->publication_epoch = 11u;
			asset.m_published_view = std::move(published_view);

			auto candidate_snapshot = std::make_shared<TerrainAssetSnapshot>(
				*published_snapshot);
			candidate_snapshot->content_generation = 8u;
			candidate_snapshot->layout.component_count_x = 2u;
			candidate_snapshot->layout.component_count_z = 1u;
			candidate_snapshot->layout.sample_count_x =
				2u * k_terrain_component_quad_count + 1u;
			candidate_snapshot->layout.sample_count_z =
				k_terrain_component_quad_count + 1u;
			auto candidate_runtime = std::make_shared<TerrainRenderRuntimeState>();
			candidate_runtime->target_snapshot = candidate_snapshot;
			candidate_runtime->work_status = TerrainRenderWorkStatus::Pending;
			InstallResourceSentinels(candidate_runtime->resources);
			auto candidate = std::make_unique<TerrainRenderCandidateState>();
			candidate->snapshot = candidate_snapshot;
			REQUIRE(derive_terrain_render_layout(
				candidate_snapshot->layout, candidate->layout));
			candidate->runtime = candidate_runtime;
			candidate->stage = TerrainRenderCandidateStage::AwaitGraphWork;
			candidate->work_status = TerrainRenderWorkStatus::Pending;
			candidate->candidate_epoch = asset.allocate_candidate_epoch_locked();
			candidate->initial_set_frozen = true;

			auto component = std::make_shared<TerrainComponentSnapshot>();
			component->coord = { 0u, 0u };
			component->content_generation = candidate_snapshot->content_generation;
			component->sample_width = k_terrain_component_sample_count;
			component->sample_height = k_terrain_component_sample_count;
			component->heights.assign(
				static_cast<size_t>(k_terrain_component_sample_count) *
					k_terrain_component_sample_count,
				0.0f);
			TerrainGpuComponentUpload upload{};
			upload.asset_id = candidate_snapshot->asset_id;
			upload.accepted_snapshot = candidate_snapshot;
			upload.component = component;
			upload.coord = component->coord;
			upload.content_generation = candidate_snapshot->content_generation;
			upload.residency_revision = candidate_snapshot->residency_revision;
			candidate->coarse_work = { upload, upload };
			const uint64_t epoch = candidate->candidate_epoch;
			asset.m_candidate_state = std::move(candidate);
			return { candidate_runtime, epoch };
		}

		static TerrainGraphResources PrepareGraph(
			TerrainRenderPass& pass,
			RenderGraphBuilder& graph,
			const VisibleRenderFrame& frame,
			const std::shared_ptr<TerrainRenderAsset>& asset,
			uint64_t render_frame_index,
			const TerrainRenderGraphOps& ops)
		{
			return pass.prepare_graph_with_ops(
				graph, frame, asset, render_frame_index, ops);
		}

		static std::shared_ptr<TerrainRenderRuntimeState>
			InstallVisibleIncrementalWork(TerrainRenderAsset& asset)
		{
			InstallPublishedAndCandidate(asset);
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			asset.m_candidate_state.reset();
			REQUIRE(asset.m_published_view != nullptr);
			const auto runtime = asset.m_published_view->runtime;
			REQUIRE(runtime != nullptr);
			auto target_snapshot = std::make_shared<TerrainAssetSnapshot>(
				*asset.m_published_view->snapshot);
			++target_snapshot->content_generation;
			auto component = std::make_shared<TerrainComponentSnapshot>();
			component->coord = { 0u, 0u };
			component->content_generation = target_snapshot->content_generation;
			component->sample_width = k_terrain_component_sample_count;
			component->sample_height = k_terrain_component_sample_count;
			component->heights.assign(
				static_cast<size_t>(k_terrain_component_sample_count) *
					k_terrain_component_sample_count,
				0.0f);
			TerrainGpuComponentUpload upload{};
			upload.asset_id = target_snapshot->asset_id;
			upload.accepted_snapshot = target_snapshot;
			upload.component = component;
			upload.coord = component->coord;
			upload.content_generation = target_snapshot->content_generation;
			upload.residency_revision = target_snapshot->residency_revision;
			runtime->target_snapshot = target_snapshot;
			runtime->weight_queue = { upload };
			runtime->work_status = TerrainRenderWorkStatus::Pending;
			return runtime;
		}

		static size_t CandidateCoarseWorkCount(const TerrainRenderAsset& asset)
		{
			std::scoped_lock<std::mutex> lock(asset.m_mutex);
			return asset.m_candidate_state ?
				asset.m_candidate_state->coarse_work.size() : 0u;
		}

	private:
		static void InstallResourceSentinels(TerrainRenderResourceSet& resources)
		{
			resources.height = std::make_shared<StorageBuffer>();
			resources.staging = std::make_shared<StorageBuffer>();
			resources.atlas[0] = std::make_shared<RenderTarget>();
			resources.atlas[1] = std::make_shared<RenderTarget>();
			resources.coarse = std::make_shared<RenderTarget>();
		}
	};
}

TEST_CASE("Terrain SceneRenderer integration preserves deferred pass order")
{
	const std::string source = ReadSource(
		"project/src/engine/Function/Render/SceneRenderer.cpp");
	const size_t prepare = source.find("m_terrain_render_pass.prepare_graph");
	const size_t gbuffer = source.find("\"SceneGBufferPass\"", prepare);
	const size_t terrain_gbuffer = source.find(
		"m_terrain_render_pass.render_gbuffer", gbuffer);
	const size_t ao = source.find("m_ambient_occlusion_pass.add_passes", gbuffer);
	const size_t shadow = source.find(
		"m_sunlight_shadow_pass.add_depth_passes", ao);
	const size_t lighting = source.find(
		"m_deferred_lighting_pass.add_base_pass", shadow);

	REQUIRE(prepare != std::string::npos);
	REQUIRE(gbuffer != std::string::npos);
	REQUIRE(terrain_gbuffer != std::string::npos);
	REQUIRE(ao != std::string::npos);
	REQUIRE(shadow != std::string::npos);
	REQUIRE(lighting != std::string::npos);
	CHECK(prepare < gbuffer);
	CHECK(gbuffer < terrain_gbuffer);
	CHECK(terrain_gbuffer < ao);
	CHECK(ao < shadow);
	CHECK(shadow < lighting);
	CHECK(CountText(source, "graph.add_raster_pass(\n\t\t\t\"SceneGBufferPass\"") == 1u);
}

TEST_CASE("Terrain SceneRenderer composes shadows timing and readiness")
{
	const std::string renderer = ReadSource(
		"project/src/engine/Function/Render/SceneRenderer.cpp");
	const std::string terrain_pass = ReadSource(
		"project/src/engine/Function/Render/TerrainRenderPass.cpp");
	const size_t static_shadow = renderer.find(
		"render_shadow_static_meshes_to_pass");
	const size_t terrain_shadow = renderer.find(
		"m_terrain_render_pass.render_shadow", static_shadow);

	REQUIRE(static_shadow != std::string::npos);
	REQUIRE(terrain_shadow != std::string::npos);
	CHECK(static_shadow < terrain_shadow);
	CHECK(renderer.find("m_particle_system_pass.is_capture_ready(frame) &&") !=
		std::string::npos);
	CHECK(renderer.find("m_terrain_render_pass.is_capture_ready(frame)") !=
		std::string::npos);
	CHECK(renderer.find("invalidate_history(temporal_view_key)") !=
		std::string::npos);
	CHECK(renderer.find("\"TerrainLodColor\"") != std::string::npos);
	CHECK(terrain_pass.find("Terrain.GBuffer") != std::string::npos);
	CHECK(terrain_pass.find("Terrain.Shadow") != std::string::npos);
}

TEST_CASE("Terrain SceneRenderer prepares one immutable draw for GBuffer and shadows")
{
	const std::string renderer = ReadSource(
		"project/src/engine/Function/Render/SceneRenderer.cpp");
	const std::string terrain_pass = ReadSource(
		"project/src/engine/Function/Render/TerrainRenderPass.cpp");

	const size_t prepare = renderer.find(
		"m_terrain_render_pass.prepare_draw(");
	const size_t gbuffer = renderer.find(
		"m_terrain_render_pass.render_gbuffer(");
	const size_t shadow = renderer.find(
		"m_terrain_render_pass.render_shadow(");
	REQUIRE(prepare != std::string::npos);
	REQUIRE(gbuffer != std::string::npos);
	REQUIRE(shadow != std::string::npos);
	CHECK(CountText(renderer, "m_terrain_render_pass.prepare_draw(") == 1u);
	CHECK(prepare < gbuffer);
	CHECK(prepare < shadow);
	CHECK(renderer.find("terrain_prepared_draw") != std::string::npos);

	const size_t consumer_begin = terrain_pass.find(
		"bool TerrainRenderPass::render_prepared_surface");
	REQUIRE(consumer_begin != std::string::npos);
	const std::string consumers = terrain_pass.substr(consumer_begin);
	CHECK(consumers.find("build_terrain_lod_batches") == std::string::npos);
	CHECK(consumers.find("ensure_instance_buffer") == std::string::npos);
}

TEST_CASE("Terrain prepared draw uses the rectangular snapshot row stride")
{
	constexpr uint32_t component_count_x = 8u;
	constexpr uint32_t component_x = 7u;
	constexpr uint32_t component_z = 15u;
	CHECK(component_z * component_count_x + component_x == 127u);
	CHECK(component_z * AshEngine::k_terrain_component_count + component_x == 487u);

	const std::string source = CompactSource(ReadSource(
		"project/src/engine/Function/Render/TerrainRenderPass.cpp"));
	const size_t prepare = source.find("TerrainRenderPass::prepare_draw(");
	const size_t consumer = source.find(
		"TerrainRenderPass::render_prepared_surface", prepare);
	REQUIRE(prepare != std::string::npos);
	REQUIRE(consumer != std::string::npos);
	const std::string body = source.substr(prepare, consumer - prepare);
	CHECK(body.find("prepared->published_view=resources.published_view") !=
		std::string::npos);
	CHECK(body.find(
		"prepared->asset_snapshot=prepared->published_view->snapshot") !=
		std::string::npos);
	CHECK(body.find("!accepted_layout.contains(instance.coord)") !=
		std::string::npos);
	CHECK(body.find("accepted_layout.component_linear_index(instance.coord)") !=
		std::string::npos);
	CHECK(body.find(
		"component_index>=prepared->asset_snapshot->components.size()") !=
		std::string::npos);
	CHECK(body.find("k_terrain_component_count+instance.coord.x") ==
		std::string::npos);
}

TEST_CASE("Terrain SceneRenderer pending generation blocks capture readiness")
{
	auto snapshot = std::make_shared<AshEngine::TerrainAssetSnapshot>();
	snapshot->asset_id = 91u;
	snapshot->layout = AshEngine::make_default_terrain_grid_layout();
	snapshot->height_mapping = { 0.0f, 100.0f };
	snapshot->content_generation = 1u;
	snapshot->components.resize(
		static_cast<size_t>(snapshot->layout.component_count_x) *
		snapshot->layout.component_count_z);
	auto asset = std::make_shared<AshEngine::TerrainRenderAsset>();
	REQUIRE(asset->accept_snapshot(snapshot));
	REQUIRE(asset->readiness() ==
		AshEngine::TerrainRenderReadiness::Pending);

	AshEngine::VisibleRenderFrame frame{};
	AshEngine::VisibleTerrainFrame terrain{};
	terrain.entity_id = 7u;
	terrain.asset_snapshot = snapshot;
	terrain.render_asset = asset;
	frame.terrains.push_back(terrain);

	AshEngine::TerrainRenderPass pass{};
	CHECK_FALSE(pass.is_capture_ready(frame));
}

TEST_CASE("Terrain candidate atlas UAV stays distinct from the published GBuffer SRV")
{
	auto asset = std::make_shared<AshEngine::TerrainRenderAsset>();
	const auto candidate =
		AshEngine::TerrainRenderGraphTestSeam::InstallPublishedAndCandidate(*asset);
	const auto published_view = asset->published_view();
	REQUIRE(published_view != nullptr);
	REQUIRE(candidate.runtime != nullptr);
	AshEngine::VisibleRenderFrame frame{};
	AshEngine::VisibleTerrainFrame terrain{};
	terrain.render_asset = asset;
	terrain.asset_snapshot = published_view->snapshot;
	frame.terrains.push_back(terrain);

	AshEngine::RenderGraphBuilder graph =
		AshEngine::RenderGraphBuilder::create_headless_for_tests("TerrainGraph");
	AshEngine::TerrainRenderPass terrain_pass{};
	TerrainGraphExecutionProbe probe{};
	probe.dispatch_result = true;
	const AshEngine::TerrainGraphResources resources =
		AshEngine::TerrainRenderGraphTestSeam::PrepareGraph(
			terrain_pass,
			graph,
			frame,
			asset,
			71u,
			probe.MakeTerrainOps());
	REQUIRE(resources.has_update_pass);
	REQUIRE(AshEngine::add_terrain_published_read_pass_for_tests(
		graph, resources, nullptr));

	const auto& passes = graph.get_passes_for_tests();
	REQUIRE(passes.size() == 2u);
	CHECK(passes[0].name == "TerrainWeightAtlasUpdatePass");
	CHECK(passes[0].kind == AshEngine::RenderGraphPassKind::Compute);
	REQUIRE(passes[0].texture_usages.size() == 3u);
	CHECK(passes[0].texture_usages[0].texture ==
		resources.update_weight_atlas_0);
	CHECK(passes[0].texture_usages[0].access ==
		AshEngine::RenderGraphAccess::ComputeUAV);
	CHECK(passes[1].name == "TerrainPublishedRaster");
	CHECK(passes[1].kind == AshEngine::RenderGraphPassKind::Raster);
	REQUIRE(passes[1].texture_usages.size() == 3u);
	CHECK(passes[1].texture_usages[0].texture == resources.weight_atlas_0);
	CHECK(passes[1].texture_usages[0].access ==
		AshEngine::RenderGraphAccess::GraphicsSRV);
	CHECK(resources.update_weight_atlas_0 != resources.weight_atlas_0);
	CHECK(candidate.runtime->resources.atlas[0] !=
		published_view->runtime->resources.atlas[0]);
	CHECK(probe.staging_writes == 1u);

	AshEngine::RenderGraphCompileResult compiled{};
	REQUIRE(graph.compile_for_tests(compiled));
	REQUIRE(compiled.live_pass_indices.size() == 2u);
	CHECK(compiled.live_pass_indices[0] == 0u);
	CHECK(compiled.live_pass_indices[1] == 1u);
	REQUIRE(compiled.pass_barriers[0].texture_states.size() >
		resources.update_weight_atlas_0.index);
	REQUIRE(compiled.pass_barriers[1].texture_states.size() >
		resources.weight_atlas_0.index);
	CHECK(compiled.pass_barriers[0].texture_states[
		resources.update_weight_atlas_0.index] ==
		RHI::AshResourceState::UAVCompute);
	CHECK(compiled.pass_barriers[1].texture_states[
		resources.weight_atlas_0.index] ==
		RHI::AshResourceState::SRVGraphics);
}

TEST_CASE("Terrain visible incremental atlas update reuses published graph identity")
{
	auto asset = std::make_shared<AshEngine::TerrainRenderAsset>();
	const auto runtime =
		AshEngine::TerrainRenderGraphTestSeam::InstallVisibleIncrementalWork(*asset);
	const auto published_view = asset->published_view();
	REQUIRE(runtime != nullptr);
	REQUIRE(published_view != nullptr);
	REQUIRE(runtime == published_view->runtime);

	AshEngine::VisibleRenderFrame frame{};
	AshEngine::VisibleTerrainFrame terrain{};
	terrain.render_asset = asset;
	terrain.asset_snapshot = published_view->snapshot;
	frame.terrains.push_back(terrain);
	AshEngine::RenderGraphBuilder graph =
		AshEngine::RenderGraphBuilder::create_headless_for_tests(
			"TerrainVisibleIncremental");
	AshEngine::TerrainRenderPass terrain_pass{};
	TerrainGraphExecutionProbe probe{};
	probe.dispatch_result = true;
	const AshEngine::TerrainGraphResources resources =
		AshEngine::TerrainRenderGraphTestSeam::PrepareGraph(
			terrain_pass,
			graph,
			frame,
			asset,
			72u,
			probe.MakeTerrainOps());

	REQUIRE(resources.has_update_pass);
	CHECK_FALSE(resources.update_is_candidate);
	CHECK(resources.update_runtime == published_view->runtime);
	CHECK(resources.update_weight_atlas_0 == resources.weight_atlas_0);
	CHECK(resources.update_weight_atlas_1 == resources.weight_atlas_1);
	CHECK(resources.update_coarse_weights == resources.coarse_weights);
	CHECK(probe.registered_textures.size() == 3u);
	REQUIRE(AshEngine::add_terrain_published_read_pass_for_tests(
		graph, resources, nullptr));

	const auto& passes = graph.get_passes_for_tests();
	REQUIRE(passes.size() == 2u);
	REQUIRE(passes[0].texture_usages.size() == 3u);
	REQUIRE(passes[1].texture_usages.size() == 3u);
	CHECK(passes[0].texture_usages[0].texture ==
		passes[1].texture_usages[0].texture);
	AshEngine::RenderGraphCompileResult compiled{};
	REQUIRE(graph.compile_for_tests(compiled));
	REQUIRE(compiled.live_pass_indices.size() == 2u);
	CHECK(compiled.pass_barriers[0].texture_states[
		resources.weight_atlas_0.index] == RHI::AshResourceState::UAVCompute);
	CHECK(compiled.pass_barriers[1].texture_states[
		resources.weight_atlas_0.index] == RHI::AshResourceState::SRVGraphics);
}

TEST_CASE("Terrain candidate graph failure fails soft and preserves published texture identity")
{
	auto asset = std::make_shared<AshEngine::TerrainRenderAsset>();
	const AshEngine::TerrainRenderGraphTestSeam::CandidateToken candidate =
		AshEngine::TerrainRenderGraphTestSeam::InstallPublishedAndCandidate(*asset);
	const auto published_view = asset->published_view();
	REQUIRE(published_view != nullptr);
	REQUIRE(candidate.runtime != nullptr);
	REQUIRE(AshEngine::TerrainRenderGraphTestSeam::CandidateCoarseWorkCount(
		*asset) == 2u);

	AshEngine::VisibleRenderFrame frame{};
	AshEngine::VisibleTerrainFrame terrain{};
	terrain.render_asset = asset;
	terrain.asset_snapshot = published_view->snapshot;
	frame.terrains.push_back(terrain);

	AshEngine::RenderGraphBuilder graph =
		AshEngine::RenderGraphBuilder::create_headless_for_tests(
			"TerrainAtomicCandidateFailure");
	AshEngine::TerrainRenderPass terrain_pass{};
	TerrainGraphExecutionProbe probe{};
	const AshEngine::TerrainGraphResources resources =
		AshEngine::TerrainRenderGraphTestSeam::PrepareGraph(
			terrain_pass,
			graph,
			frame,
			asset,
			99u,
			probe.MakeTerrainOps());
	REQUIRE(resources.weight_atlas_0);
	REQUIRE(resources.weight_atlas_1);
	REQUIRE(resources.coarse_weights);
	REQUIRE(resources.has_update_pass);
	CHECK(resources.published_view == published_view);
	CHECK(resources.update_runtime == candidate.runtime);
	CHECK(resources.update_is_candidate);
	CHECK(probe.staging_writes == 1u);
	REQUIRE(probe.registered_textures.size() == 6u);
	CHECK(probe.registered_textures[0] ==
		published_view->runtime->resources.atlas[0]);
	CHECK(probe.registered_textures[3] == candidate.runtime->resources.atlas[0]);
	CHECK(probe.registered_textures[0] != probe.registered_textures[3]);

	REQUIRE(AshEngine::add_terrain_published_read_pass_for_tests(
		graph,
		resources,
		&probe.raster_draws));

	const auto& passes = graph.get_passes_for_tests();
	REQUIRE(passes.size() == 2u);
	CHECK(passes[0].name == "TerrainWeightAtlasUpdatePass");
	REQUIRE(passes[0].texture_usages.size() == 3u);
	REQUIRE(passes[1].texture_usages.size() == 3u);
	CHECK(passes[0].texture_usages[0].texture ==
		resources.update_weight_atlas_0);
	CHECK(passes[0].texture_usages[0].access ==
		AshEngine::RenderGraphAccess::ComputeUAV);
	CHECK(passes[1].texture_usages[0].texture == resources.weight_atlas_0);
	CHECK(passes[1].texture_usages[0].access ==
		AshEngine::RenderGraphAccess::GraphicsSRV);
	CHECK(passes[0].texture_usages[0].texture !=
		passes[1].texture_usages[0].texture);

	std::vector<AshEngine::RenderGraphTextureNode> textures =
		graph.get_textures_for_tests();
	REQUIRE(textures.size() == probe.registered_textures.size());
	for (size_t index = 0u; index < textures.size(); ++index)
	{
		textures[index].external_texture = probe.registered_textures[index];
	}
	std::vector<AshEngine::RenderGraphBufferNode> buffers =
		graph.get_buffers_for_tests();
	REQUIRE(AshEngine::execute_render_graph_with_ops_for_tests(
		textures, buffers, passes, probe.MakeOps()));
	CHECK(probe.staging_writes == 1u);
	CHECK(probe.dispatches == 1u);
	CHECK(probe.raster_draws == 1u);
	CHECK(asset->latest_work_status() ==
		AshEngine::TerrainRenderWorkStatus::Failed);
	CHECK(asset->published_view() == published_view);
	CHECK(asset->published_view()->runtime == published_view->runtime);
}

TEST_CASE("Terrain atlas update closes over the complete asset identity")
{
	const std::string header = CompactSource(ReadSource(
		"project/src/engine/Function/Render/TerrainRenderPass.h"));
	const std::string asset_header = CompactSource(ReadSource(
		"project/src/engine/Function/Render/TerrainRenderAsset.h"));
	const std::string source = CompactSource(ReadSource(
		"project/src/engine/Function/Render/TerrainRenderPass.cpp"));

	const size_t upload_begin = asset_header.find("structTerrainGpuComponentUpload");
	const size_t upload_end = asset_header.find(
		"structTerrainAtlasSlotMetadata", upload_begin);
	REQUIRE(upload_begin != std::string::npos);
	REQUIRE(upload_end != std::string::npos);
	const std::string upload = asset_header.substr(
		upload_begin, upload_end - upload_begin);
	CHECK(upload.find("uint64_tasset_id=0u") != std::string::npos);
	CHECK(upload.find("std::weak_ptr<constTerrainAssetSnapshot>accepted_snapshot") !=
		std::string::npos);

	const size_t resources_begin = header.find("structTerrainGraphResources");
	const size_t resources_end = header.find(
		"enumclassTerrainPreparedDrawStatus", resources_begin);
	REQUIRE(resources_begin != std::string::npos);
	REQUIRE(resources_end != std::string::npos);
	const std::string resources = header.substr(
		resources_begin, resources_end - resources_begin);
	CHECK(resources.find("pending_atlas_asset_id") != std::string::npos);
	CHECK(resources.find("pending_atlas_snapshot") != std::string::npos);

	const size_t completion_begin = header.find("structTerrainAtlasCompletion");
	const size_t completion_end = header.find(
		"std::vector<TerrainInstanceBufferEntry>", completion_begin);
	REQUIRE(completion_begin != std::string::npos);
	REQUIRE(completion_end != std::string::npos);
	const std::string completion = header.substr(
		completion_begin, completion_end - completion_begin);
	CHECK(completion.find("uint64_tasset_id=0u") != std::string::npos);
	CHECK(completion.find("std::weak_ptr<constTerrainAssetSnapshot>snapshot") !=
		std::string::npos);

	const size_t atlas_begin = source.find("TerrainRenderPass::add_atlas_update_pass(");
	const size_t atlas_end = source.find("TerrainGraphResources::is_valid", atlas_begin);
	REQUIRE(atlas_begin != std::string::npos);
	REQUIRE(atlas_end != std::string::npos);
	const std::string atlas = source.substr(atlas_begin, atlas_end - atlas_begin);
	CHECK(CountText(atlas,
		"matches_pending_weight_update_locked("
		"resources.update_runtime,pending_upload)") == 2u);
	CHECK(atlas.find("slot.asset_id=pending_upload.asset_id") !=
		std::string::npos);
	const size_t prepare_graph_begin = source.find("TerrainRenderPass::prepare_graph(");
	const size_t prepare_draw_begin = source.find(
		"TerrainRenderPass::prepare_draw(", prepare_graph_begin);
	REQUIRE(prepare_graph_begin != std::string::npos);
	REQUIRE(prepare_draw_begin != std::string::npos);
	const std::string prepare_graph = source.substr(
		prepare_graph_begin, prepare_draw_begin - prepare_graph_begin);
	CHECK(prepare_graph.find(
		"resources.update_snapshot=asset->m_candidate_state->snapshot") !=
		std::string::npos);
	CHECK(prepare_graph.find(
		"resources.pending_atlas_asset_id=pending_upload.asset_id") !=
		std::string::npos);
	CHECK(prepare_graph.find(
		"resources.pending_atlas_snapshot=resources.update_snapshot") !=
		std::string::npos);

	const size_t prepared_surface_begin = source.find(
		"TerrainRenderPass::render_prepared_surface", prepare_draw_begin);
	REQUIRE(prepared_surface_begin != std::string::npos);
	const std::string prepare_draw = source.substr(
		prepare_draw_begin, prepared_surface_begin - prepare_draw_begin);
	CHECK(prepare_draw.find(
		"slot.asset_id==prepared->asset_snapshot->asset_id") !=
		std::string::npos);
	CHECK(prepare_draw.find(
		"resources.pending_atlas_asset_id==prepared->asset_snapshot->asset_id") !=
		std::string::npos);
	CHECK(prepare_draw.find(
		"resources.pending_atlas_snapshot==prepared->asset_snapshot") !=
		std::string::npos);

	const size_t capture_begin = source.find("TerrainRenderPass::is_capture_ready(");
	REQUIRE(capture_begin != std::string::npos);
	const std::string capture = source.substr(capture_begin);
	CHECK(capture.find("accepted_snapshot!=terrain.asset_snapshot") !=
		std::string::npos);
	CHECK(capture.find("completion->second.asset_id==terrain.asset_snapshot->asset_id") !=
		std::string::npos);
	CHECK(capture.find("completion_snapshot==terrain.asset_snapshot") !=
		std::string::npos);
}

TEST_CASE("Terrain atlas clean frame declares only the GBuffer read")
{
	auto asset = std::make_shared<AshEngine::TerrainRenderAsset>();
	AshEngine::TerrainRenderGraphTestSeam::InstallPublishedAndCandidate(*asset);
	const auto published_view = asset->published_view();
	REQUIRE(published_view != nullptr);
	AshEngine::VisibleRenderFrame frame{};
	AshEngine::VisibleTerrainFrame terrain{};
	terrain.render_asset = asset;
	terrain.asset_snapshot = published_view->snapshot;
	frame.terrains.push_back(terrain);
	AshEngine::RenderGraphBuilder graph =
		AshEngine::RenderGraphBuilder::create_headless_for_tests(
			"TerrainCleanGraph");
	AshEngine::TerrainRenderPass terrain_pass{};
	TerrainGraphExecutionProbe probe{};
	const AshEngine::TerrainGraphResources resources =
		AshEngine::TerrainRenderGraphTestSeam::PrepareGraph(
			terrain_pass,
			graph,
			frame,
			nullptr,
			73u,
			probe.MakeTerrainOps());
	CHECK_FALSE(resources.has_update_pass);
	CHECK(probe.staging_writes == 0u);
	CHECK(probe.registered_textures.size() == 3u);
	REQUIRE(AshEngine::add_terrain_published_read_pass_for_tests(
		graph, resources, nullptr));

	const auto& passes = graph.get_passes_for_tests();
	REQUIRE(passes.size() == 1u);
	CHECK(passes[0].kind == AshEngine::RenderGraphPassKind::Raster);
	REQUIRE(passes[0].texture_usages.size() == 3u);
	CHECK(passes[0].texture_usages[0].texture == resources.weight_atlas_0);
	CHECK(passes[0].texture_usages[0].access ==
		AshEngine::RenderGraphAccess::GraphicsSRV);

	AshEngine::RenderGraphCompileResult compiled{};
	REQUIRE(graph.compile_for_tests(compiled));
	REQUIRE(compiled.live_pass_indices.size() == 1u);
	CHECK(compiled.live_pass_indices[0] == 0u);
}

TEST_CASE("Terrain atlas shader uses raw weights and three texture UAVs")
{
	CHECK(AshEngine::k_terrain_weight_upload_stride == 0u);
	CHECK(AshEngine::k_terrain_weight_upload_bytes ==
		AshEngine::k_terrain_component_sample_count *
		AshEngine::k_terrain_component_sample_count * 8u);
	const std::string shader = ReadSource(
		"project/src/engine/Shaders/Terrain/TerrainAtlasUpdate.hlsl");
	CHECK(shader.find("ByteAddressBuffer TerrainWeightUpload") !=
		std::string::npos);
	CHECK(shader.find("RWTexture2D<unorm float4> TerrainWeightAtlas0") !=
		std::string::npos);
	CHECK(shader.find("RWTexture2D<unorm float4> TerrainWeightAtlas1") !=
		std::string::npos);
	CHECK(shader.find("RWTexture2D<unorm float4> TerrainCoarseWeights") !=
		std::string::npos);
	CHECK(shader.find("[numthreads(8, 8, 1)]") != std::string::npos);
}

TEST_CASE("Terrain RGBA8 storage images receive a Vulkan format annotation")
{
	const std::string shader = ReadSource(
		"project/src/engine/Shaders/Terrain/TerrainAtlasUpdate.hlsl");
	const std::string compiler = ReadSource(
		"project/src/engine/Graphics/Vulkan/VulkanShaderCompiler.cpp");

	CHECK(CountText(shader, "RWTexture2D<unorm float4>") == 3u);
	CHECK(compiler.find("rewrite_unorm_storage_images_for_vulkan") !=
		std::string::npos);
	CHECK(compiler.find("[[vk::image_format(\\\"rgba8\\\")]]") !=
		std::string::npos);
	CHECK(compiler.find(
		"rewrite_unorm_storage_images_for_vulkan(vulkanShaderText)") !=
		std::string::npos);
	CHECK(compiler.find("storageimageformat-v1") != std::string::npos);
}

TEST_CASE("Terrain shared grid covers all nine LOD resolutions")
{
	for (uint8_t lod = 0u; lod < AshEngine::k_terrain_lod_count; ++lod)
	{
		const uint32_t resolution =
			AshEngine::k_terrain_component_quad_count >> lod;
		CAPTURE(lod, resolution);
		std::vector<uint32_t> indices{};
		REQUIRE(AshEngine::build_terrain_shared_grid_indices(lod, indices));
		REQUIRE(indices.size() ==
			static_cast<size_t>(6u) * resolution * resolution);
		REQUIRE(indices.size() >= 6u);
		CHECK(indices[0] == 0u);
		CHECK(indices[1] == resolution + 1u);
		CHECK(indices[2] == 1u);
		CHECK(indices[3] == 1u);
		CHECK(indices[4] == resolution + 1u);
		CHECK(indices[5] == resolution + 2u);
		const uint32_t vertex_count = (resolution + 1u) * (resolution + 1u);
		uint32_t max_index = 0u;
		for (uint32_t index : indices)
		{
			max_index = std::max(max_index, index);
		}
		CHECK(max_index == vertex_count - 1u);
	}
	std::vector<uint32_t> preserved{ 7u, 11u };
	CHECK_FALSE(AshEngine::build_terrain_shared_grid_indices(
		AshEngine::k_terrain_lod_count, preserved));
	CHECK(preserved == std::vector<uint32_t>{ 7u, 11u });

	AshEngine::TerrainLodInput input =
		AshEngine::make_full_terrain_lod_test_input();
	AshEngine::TerrainLodResult result{};
	REQUIRE(AshEngine::build_terrain_lod_batches(input, result));
	REQUIRE_FALSE(result.batches.empty());
	for (const AshEngine::TerrainLodBatch& batch : result.batches)
	{
		CHECK(batch.first_instance == 0u);
	}
}

TEST_CASE("Terrain shader bindings include fixed surface resources")
{
	const std::string common = ReadSource(
		"project/src/engine/Shaders/Terrain/TerrainCommon.hlsli");
	const std::string surface = ReadSource(
		"project/src/engine/Shaders/Terrain/TerrainSurface.hlsl");
	const std::string shader = common + surface;
	const std::vector<std::string> binding_names = {
		"TerrainHeightWords",
		"TerrainInstances",
		"TerrainWeightAtlas0",
		"TerrainWeightAtlas1",
		"TerrainCoarseWeights",
		"TerrainBaseColorLayers",
		"TerrainNormalLayers",
		"TerrainOrmLayers",
		"TerrainWeightSampler",
		"TerrainMaterialSampler"
	};
	for (const std::string& binding : binding_names)
	{
		CAPTURE(binding);
		CHECK(shader.find(binding) != std::string::npos);
	}
	CHECK(surface.find("TERRAIN_GBUFFER") != std::string::npos);
	CHECK(surface.find("TERRAIN_DEPTH_ONLY") != std::string::npos);
	CHECK(surface.find("TERRAIN_LOD_DEBUG") != std::string::npos);
	CHECK(surface.find("SV_VertexID") != std::string::npos);
	CHECK(surface.find("SV_InstanceID") != std::string::npos);
	CHECK(surface.find("AshTerrainFlags.y + instance_id") !=
		std::string::npos);
	CHECK(common.find("AshTerrainDecodeHeight") != std::string::npos);
	CHECK(common.find("AshTerrainMorphHeight") != std::string::npos);
	CHECK(surface.find("AshTerrainSelectTopFour") != std::string::npos);
}

TEST_CASE("Terrain surface constants bind rectangular layout for every pass")
{
	const std::string terrain_pass = ReadSource(
		"project/src/engine/Function/Render/TerrainRenderPass.cpp");
	const std::string surface = ReadSource(
		"project/src/engine/Shaders/Terrain/TerrainSurface.hlsl");
	const std::string atlas = ReadSource(
		"project/src/engine/Shaders/Terrain/TerrainAtlasUpdate.hlsl");
	const std::string compact_pass = CompactSource(terrain_pass);
	const std::string compact_atlas = CompactSource(atlas);

	CHECK(compact_pass.find("glm::uvec4layout{0u};") != std::string::npos);
	CHECK(terrain_pass.find(
		"static_assert(sizeof(TerrainSurfaceConstants) == 240u);") !=
		std::string::npos);
	CHECK(terrain_pass.find(
		"static_assert(offsetof(TerrainSurfaceConstants, object_to_clip) == 0u);") !=
		std::string::npos);
	CHECK(terrain_pass.find(
		"static_assert(offsetof(TerrainSurfaceConstants, previous_object_to_clip) == 64u);") !=
		std::string::npos);
	CHECK(terrain_pass.find(
		"static_assert(offsetof(TerrainSurfaceConstants, object_to_world) == 128u);") !=
		std::string::npos);
	CHECK(terrain_pass.find(
		"static_assert(offsetof(TerrainSurfaceConstants, height_spacing_uv_scale) == 192u);") !=
		std::string::npos);
	CHECK(terrain_pass.find(
		"static_assert(offsetof(TerrainSurfaceConstants, flags) == 208u);") !=
		std::string::npos);
	CHECK(terrain_pass.find(
		"static_assert(offsetof(TerrainSurfaceConstants, layout) == 224u);") !=
		std::string::npos);
	CHECK(surface.find("uint4 AshTerrainLayout;") != std::string::npos);
	CHECK(CountText(terrain_pass, "TerrainSurfaceConstants constants{};") == 1u);
	CHECK(CountText(terrain_pass, "render_prepared_surface(") == 4u);
	CHECK(compact_pass.find(
		"constants.layout={"
		"prepared_draw->asset_snapshot->layout.component_count_x,"
		"prepared_draw->asset_snapshot->layout.component_count_z,"
		"prepared_draw->asset_snapshot->layout.sample_count_x,"
		"prepared_draw->asset_snapshot->layout.sample_count_z};") !=
		std::string::npos);

	CHECK(compact_pass.find(
		"uint32_twrite_high_resolution=0u;"
		"uint32_tcomponent_count_x=0u;"
		"uint32_tcomponent_count_z=0u;"
		"uint32_tpadding=0u;") != std::string::npos);
	CHECK(terrain_pass.find(
		"static_assert(sizeof(TerrainAtlasUpdateConstants) == 32u);") !=
		std::string::npos);
	CHECK(compact_atlas.find(
		"uintAshTerrainWriteHighResolution;"
		"uintAshTerrainComponentCountX;"
		"uintAshTerrainComponentCountZ;"
		"uintAshTerrainPadding;") != std::string::npos);
	CHECK(compact_pass.find(
		"constants.component_count_x=resources.update_layout.layout.component_count_x;") !=
		std::string::npos);
	CHECK(compact_pass.find(
		"constants.component_count_z=resources.update_layout.layout.component_count_z;") !=
		std::string::npos);

	CHECK(compact_pass.find(
		"resources.update_layout=asset->m_candidate_state->layout;") !=
		std::string::npos);
	CHECK(compact_pass.find(
		"resources.update_layout=asset->m_published_view->layout;") !=
		std::string::npos);
	CHECK(compact_pass.find(
		"[this,resources,asset,pending_upload,atlas_slot,"
		"write_high_resolution,render_frame_index,graph_ops,program]") !=
		std::string::npos);

	constexpr uint32_t rectangular_component_count_x = 8u;
	constexpr uint32_t rectangular_component_x = 0u;
	constexpr uint32_t rectangular_component_z = 15u;
	constexpr uint32_t rectangular_component_index =
		rectangular_component_z * rectangular_component_count_x +
		rectangular_component_x;
	CHECK(rectangular_component_index == 120u);
	CHECK(rectangular_component_index != 480u);
}

TEST_CASE("Terrain shaders contain no fixed whole-terrain stride or extent")
{
	const std::string common = ReadSource(
		"project/src/engine/Shaders/Terrain/TerrainCommon.hlsli");
	const std::string surface = ReadSource(
		"project/src/engine/Shaders/Terrain/TerrainSurface.hlsl");
	const std::string atlas = ReadSource(
		"project/src/engine/Shaders/Terrain/TerrainAtlasUpdate.hlsl");
	const std::string renderer = ReadSource(
		"project/src/engine/Function/Render/SceneRenderer.cpp");
	const std::string compact_common = CompactSource(common);
	const std::string compact_atlas = CompactSource(atlas);

	CHECK(common.find("AshTerrainComponentCount = 32u") == std::string::npos);
	CHECK(common.find("AshTerrainCoarseExtent = 1025u") == std::string::npos);
	CHECK(compact_common.find("component_coord.y*32u+component_coord.x") ==
		std::string::npos);
	CHECK(compact_common.find("int2(8192,8192)") == std::string::npos);
	CHECK(compact_common.find("/8192.0") == std::string::npos);
	CHECK(compact_common.find(
		"component_coord.y*AshTerrainLayout.x+component_coord.x") !=
		std::string::npos);
	CHECK(CountText(compact_common,
		"constint2max_sample=int2(AshTerrainLayout.zw)-int2(1,1);") == 3u);
	CHECK(compact_common.find(
		"min(clamped_sample/AshTerrainComponentQuads,AshTerrainLayout.xy-1u)") !=
		std::string::npos);
	CHECK(compact_common.find(
		"constintvarying_max=along_x?max_sample.x:max_sample.y;") !=
		std::string::npos);
	CHECK(compact_common.find(
		"constfloat2coarse_extent=float2(AshTerrainLayout.xy*32u+1u);") !=
		std::string::npos);

	const size_t root_constants = surface.find("cbuffer AshRootConstants");
	const size_t layout = surface.find("uint4 AshTerrainLayout;");
	const size_t common_include = surface.find("#include \"TerrainCommon.hlsli\"");
	REQUIRE(root_constants != std::string::npos);
	REQUIRE(layout != std::string::npos);
	REQUIRE(common_include != std::string::npos);
	CHECK(root_constants < layout);
	CHECK(layout < common_include);
	const size_t object_to_clip = surface.find(
		"float4x4 AshTerrainObjectToClip;", root_constants);
	const size_t previous_object_to_clip = surface.find(
		"float4x4 AshTerrainPreviousObjectToClip;", object_to_clip);
	const size_t object_to_world = surface.find(
		"float4x4 AshTerrainObjectToWorld;", previous_object_to_clip);
	const size_t height_spacing = surface.find(
		"float4 AshTerrainHeightSpacingUvScale;", object_to_world);
	const size_t flags = surface.find("uint4 AshTerrainFlags;", height_spacing);
	REQUIRE(object_to_clip != std::string::npos);
	REQUIRE(previous_object_to_clip != std::string::npos);
	REQUIRE(object_to_world != std::string::npos);
	REQUIRE(height_spacing != std::string::npos);
	REQUIRE(flags != std::string::npos);
	CHECK(object_to_clip < previous_object_to_clip);
	CHECK(previous_object_to_clip < object_to_world);
	CHECK(object_to_world < height_spacing);
	CHECK(height_spacing < flags);
	CHECK(flags < layout);

	CHECK(compact_atlas.find("AshTerrainComponentCoord.x+1u=="
		"AshTerrainComponentCountX") != std::string::npos);
	CHECK(compact_atlas.find("AshTerrainComponentCoord.y+1u=="
		"AshTerrainComponentCountZ") != std::string::npos);
	CHECK(atlas.find("AshTerrainComponentCoord.x == 31u") == std::string::npos);
	CHECK(atlas.find("AshTerrainComponentCoord.y == 31u") == std::string::npos);
	CHECK(renderer.find("k_terrain_coarse_weight_extent") == std::string::npos);
	CHECK(renderer.find("coarse_weight_target->get_width()") !=
		std::string::npos);
	CHECK(renderer.find("coarse_weight_target->get_height()") !=
		std::string::npos);
}

TEST_CASE("Terrain surface treats empty weights as implicit layer zero")
{
	const std::string common = ReadSource(
		"project/src/engine/Shaders/Terrain/TerrainCommon.hlsli");
	const std::string surface = ReadSource(
		"project/src/engine/Shaders/Terrain/TerrainSurface.hlsl");
	const std::string terrain_pass = ReadSource(
		"project/src/engine/Function/Render/TerrainRenderPass.cpp");
	const std::string compact_surface = CompactSource(surface);

	CHECK(common.find("implicit_layer_zero") != std::string::npos);
	CHECK(surface.find("input.implicit_layer_zero") != std::string::npos);
	CHECK(compact_surface.find(
		"constAshTerrainInstanceweight_instance={input.component_coord,0u,0u,"
		"0.0,input.atlas_slot,input.high_resolution_weights!=0u,"
		"input.implicit_layer_zero!=0u};") != std::string::npos);
	CHECK(surface.find("float4(1.0, 0.0, 0.0, 0.0)") !=
		std::string::npos);
	CHECK(terrain_pass.find("implicit_layer_zero ? 2u : 0u") !=
		std::string::npos);
}

TEST_CASE("Terrain GBuffer normals use canonical gradients and coarse-edge interpolation")
{
	const std::string common = ReadSource(
		"project/src/engine/Shaders/Terrain/TerrainCommon.hlsli");
	const std::string surface = ReadSource(
		"project/src/engine/Shaders/Terrain/TerrainSurface.hlsl");
	const std::string compact_common = CompactSource(common);
	const std::string compact_surface = CompactSource(surface);

	CHECK(compact_surface.find(
		"#ifTERRAIN_GBUFFERfloat3position_ws:TEXCOORD0;"
		"float3normal_ws:TEXCOORD1;") !=
		std::string::npos);
	CHECK(CountText(surface, "position_ws : TEXCOORD0") == 1u);
	CHECK(CountText(surface, "normal_ws : TEXCOORD1") == 1u);
	CHECK(common.find("AshTerrainCanonicalHeightGradient") != std::string::npos);
	CHECK(common.find("AshTerrainInterpolateCanonicalEdgeGradient") !=
		std::string::npos);
	CHECK(common.find("AshTerrainShadingHeightGradient") != std::string::npos);
	CHECK(compact_common.find(
		"constfloat2canonical_gradient=AshTerrainCanonicalHeightGradient("
		"height_words,global_sample,height_offset,height_range,"
		"sample_spacing);") != std::string::npos);
	CHECK(compact_common.find(
		"constuintcoarse_neighbor_step=min((1u<<instance.lod)*2u,"
		"AshTerrainComponentQuads);") != std::string::npos);
	CHECK(compact_common.find(
		"returnAshTerrainInterpolateCanonicalEdgeGradient("
		"height_words,global_sample,coarse_neighbor_step,false,"
		"height_offset,height_range,sample_spacing);") !=
		std::string::npos);
	CHECK(compact_common.find(
		"returnAshTerrainInterpolateCanonicalEdgeGradient("
		"height_words,global_sample,coarse_neighbor_step,true,"
		"height_offset,height_range,sample_spacing);") !=
		std::string::npos);
	CHECK(common.find("AshTerrainHeightGradientAtStride") == std::string::npos);
	CHECK(common.find("AshTerrainMorphHeightGradient") == std::string::npos);
	CHECK(compact_surface.find(
		"#ifTERRAIN_GBUFFERoutput.position_ws=mul((float3x3)"
		"AshTerrainObjectToWorld,position_os);") != std::string::npos);
	CHECK(compact_surface.find(
		"constfloat2height_gradient_os=AshTerrainShadingHeightGradient("
		"TerrainHeightWords,instance,local_sample,"
		"AshTerrainHeightSpacingUvScale.x,AshTerrainHeightSpacingUvScale.y,"
		"AshTerrainHeightSpacingUvScale.z);") != std::string::npos);
	CHECK(compact_surface.find(
		"constfloat3normal_vector_ws=cross(tangent_z_ws,tangent_x_ws);") !=
		std::string::npos);
	CHECK(compact_surface.find(
		"output.normal_ws=isfinite(normal_length_squared)&&"
		"normal_length_squared>0.0?normal_vector_ws:0.0.xxx;") !=
		std::string::npos);

	const size_t shading_gradient = compact_surface.find(
		"constfloat2height_gradient_os=AshTerrainShadingHeightGradient(");
	const size_t raw_normal_output = compact_surface.find(
		"output.normal_ws=", shading_gradient);
	REQUIRE(shading_gradient != std::string::npos);
	REQUIRE(raw_normal_output != std::string::npos);
	CHECK(compact_surface.substr(
		shading_gradient,
		raw_normal_output - shading_gradient).find("rsqrt(") ==
		std::string::npos);
	CHECK(compact_surface.substr(
		shading_gradient,
		raw_normal_output - shading_gradient).find("normalize(") ==
		std::string::npos);

	const size_t vertex_shader_end = surface.find("return output;");
	const size_t pixel_gbuffer_begin = surface.find(
		"#if TERRAIN_GBUFFER", vertex_shader_end);
	const size_t lod_debug_begin = surface.find(
		"#elif TERRAIN_LOD_DEBUG", pixel_gbuffer_begin);
	REQUIRE(pixel_gbuffer_begin != std::string::npos);
	REQUIRE(lod_debug_begin != std::string::npos);
	const std::string pixel_gbuffer = CompactSource(surface.substr(
		pixel_gbuffer_begin,
		lod_debug_begin - pixel_gbuffer_begin));

	const size_t smooth_normal_check = pixel_gbuffer.find(
		"constfloatinterpolated_normal_length_squared="
		"dot(input.normal_ws,input.normal_ws);");
	const size_t derivative_fallback = pixel_gbuffer.find(
		"constfloat3position_dx_ws=ddx(input.position_ws);");
	REQUIRE(smooth_normal_check != std::string::npos);
	REQUIRE(derivative_fallback != std::string::npos);
	CHECK(smooth_normal_check < derivative_fallback);
	CHECK(pixel_gbuffer.find(
		"if(isfinite(interpolated_normal_length_squared)&&"
		"interpolated_normal_length_squared>0.0){geometric_normal="
		"input.normal_ws*rsqrt(interpolated_normal_length_squared);") !=
		std::string::npos);
	CHECK(pixel_gbuffer.find(
		"else{constfloat3position_dx_ws=ddx(input.position_ws);"
		"constfloat3position_dy_ws=ddy(input.position_ws);") !=
		std::string::npos);
	CHECK(CountText(pixel_gbuffer, "ddx(") == 1u);
	CHECK(CountText(pixel_gbuffer, "ddy(") == 1u);

	const std::string non_gbuffer_pixel_shaders = CompactSource(
		surface.substr(lod_debug_begin));
	CHECK(non_gbuffer_pixel_shaders.find("position_ws") == std::string::npos);
	CHECK(non_gbuffer_pixel_shaders.find("ddx(") == std::string::npos);
	CHECK(non_gbuffer_pixel_shaders.find("ddy(") == std::string::npos);
	CHECK(surface.find("AshTerrainLocalNormal(") == std::string::npos);
}

TEST_CASE("Terrain canonical edge normals match adjacent LOD interpolation")
{
	const std::string common = ReadSource(
		"project/src/engine/Shaders/Terrain/TerrainCommon.hlsli");
	REQUIRE(common.find("AshTerrainShadingHeightGradient") !=
		std::string::npos);

	struct Gradient
	{
		double x = 0.0;
		double z = 0.0;
	};
	struct Instance
	{
		uint32_t component_x = 0u;
		uint32_t component_z = 0u;
		uint32_t lod = 0u;
		uint32_t edge_mask = 0u;
		uint32_t local_x = 0u;
		uint32_t local_z = 0u;
		double morph = 0.0;
	};
	constexpr int component_quads = 256;
	constexpr int max_sample = 8192;
	auto height = [](double x, double z)
	{
		return 0.000001 * x * x * x +
			0.000002 * z * z * z +
			0.0000001 * x * z * z;
	};
	auto canonical_gradient = [&](int global_x, int global_z)
	{
		global_x = std::clamp(global_x, 0, max_sample);
		global_z = std::clamp(global_z, 0, max_sample);
		const int west = std::max(global_x - 1, 0);
		const int east = std::min(global_x + 1, max_sample);
		const int north = std::max(global_z - 1, 0);
		const int south = std::min(global_z + 1, max_sample);
		return Gradient{
			(height(east, global_z) - height(west, global_z)) /
				static_cast<double>(east - west),
			(height(global_x, south) - height(global_x, north)) /
				static_cast<double>(south - north)
		};
	};
	auto interpolate = [](Gradient begin, Gradient end, double fraction)
	{
		return Gradient{
			begin.x + (end.x - begin.x) * fraction,
			begin.z + (end.z - begin.z) * fraction
		};
	};
	auto edge_gradient = [&](int global_x,
		int global_z,
		int coarse_step,
		bool along_x)
	{
		const int varying = along_x ? global_x : global_z;
		const int segment_begin = (varying / coarse_step) * coarse_step;
		const int segment_end = std::min(
			segment_begin + coarse_step, max_sample);
		if (segment_end <= segment_begin)
		{
			return canonical_gradient(global_x, global_z);
		}
		const Gradient begin = along_x
			? canonical_gradient(segment_begin, global_z)
			: canonical_gradient(global_x, segment_begin);
		const Gradient end = along_x
			? canonical_gradient(segment_end, global_z)
			: canonical_gradient(global_x, segment_end);
		return interpolate(
			begin,
			end,
			static_cast<double>(varying - segment_begin) /
				static_cast<double>(segment_end - segment_begin));
	};
	auto shading_gradient = [&](const Instance& instance)
	{
		const int global_x = static_cast<int>(instance.component_x) *
			component_quads + static_cast<int>(instance.local_x);
		const int global_z = static_cast<int>(instance.component_z) *
			component_quads + static_cast<int>(instance.local_z);
		const bool west = instance.local_x == 0u &&
			(instance.edge_mask & 1u) != 0u;
		const bool east = instance.local_x == component_quads &&
			(instance.edge_mask & 2u) != 0u;
		const bool north = instance.local_z == 0u &&
			(instance.edge_mask & 4u) != 0u;
		const bool south = instance.local_z == component_quads &&
			(instance.edge_mask & 8u) != 0u;
		const int coarse_neighbor_step = std::min(
			(1 << instance.lod) * 2, component_quads);
		(void)instance.morph;
		if (west || east)
		{
			return edge_gradient(
				global_x, global_z, coarse_neighbor_step, false);
		}
		if (north || south)
		{
			return edge_gradient(
				global_x, global_z, coarse_neighbor_step, true);
		}
		return canonical_gradient(global_x, global_z);
	};
	auto check_equal = [](Gradient actual, Gradient expected)
	{
		CHECK(actual.x == doctest::Approx(expected.x).epsilon(1e-10));
		CHECK(actual.z == doctest::Approx(expected.z).epsilon(1e-10));
	};

	// Same-LOD components reach one shared global sample through different
	// component-local coordinates.
	const Instance same_lod_west{ 0u, 0u, 2u, 0u, 256u, 12u, 0.0 };
	const Instance same_lod_east{ 1u, 0u, 2u, 0u, 0u, 12u, 1.0 };
	check_equal(
		shading_gradient(same_lod_west),
		shading_gradient(same_lod_east));

	// Fine LOD1 z=10 is halfway between the adjacent LOD2 edge vertices z=8
	// and z=12. The fine edge mask must select that exact raster interpolation.
	const Instance fine_edge{ 0u, 0u, 1u, 2u, 256u, 10u, 0.0 };
	const Instance coarse_begin{ 1u, 0u, 2u, 0u, 0u, 8u, 0.0 };
	const Instance coarse_end{ 1u, 0u, 2u, 0u, 0u, 12u, 1.0 };
	const Gradient coarse_raster_interpolation = interpolate(
		shading_gradient(coarse_begin),
		shading_gradient(coarse_end),
		0.5);
	const Gradient fine_edge_gradient = shading_gradient(fine_edge);
	check_equal(fine_edge_gradient, coarse_raster_interpolation);
	Instance fine_without_edge_mask = fine_edge;
	fine_without_edge_mask.edge_mask = 0u;
	CHECK(std::abs(
		shading_gradient(fine_without_edge_mask).z -
		fine_edge_gradient.z) > 1e-9);

	// Shading intentionally ignores geomorph factor; all three morph states
	// must preserve the same seam normal field.
	Instance fine_half_morph = fine_edge;
	fine_half_morph.morph = 0.5;
	Instance fine_full_morph = fine_edge;
	fine_full_morph.morph = 1.0;
	check_equal(shading_gradient(fine_half_morph), fine_edge_gradient);
	check_equal(shading_gradient(fine_full_morph), fine_edge_gradient);

	// A corner with two coarse neighbours is an endpoint of both segments, so
	// edge priority cannot change its canonical gradient.
	const Instance double_coarse_corner{
		0u, 0u, 1u, 2u | 8u, 256u, 256u, 0.5 };
	check_equal(
		shading_gradient(double_coarse_corner),
		canonical_gradient(256, 256));

	// Global outer boundaries use finite one-sided full-resolution differences.
	const Instance global_west{ 0u, 0u, 0u, 0u, 0u, 16u, 0.0 };
	const Instance global_east{ 31u, 0u, 0u, 0u, 256u, 16u, 0.0 };
	const Gradient west_gradient = shading_gradient(global_west);
	const Gradient east_gradient = shading_gradient(global_east);
	CHECK(west_gradient.x == doctest::Approx(
		height(1.0, 16.0) - height(0.0, 16.0)).epsilon(1e-10));
	CHECK(east_gradient.x == doctest::Approx(
		height(8192.0, 16.0) - height(8191.0, 16.0)).epsilon(1e-10));

	// The previous stride-4 derivative is not the adjacent coarse raster
	// interpolation and therefore cannot be used as the fine-edge contract.
	const Gradient rejected_stride_gradient{
		(height(260.0, 10.0) - height(252.0, 10.0)) / 8.0,
		(height(256.0, 14.0) - height(256.0, 6.0)) / 8.0
	};
	CHECK(std::abs(
		rejected_stride_gradient.z - coarse_raster_interpolation.z) > 1e-9);

	struct Vec3
	{
		double x = 0.0;
		double y = 0.0;
		double z = 0.0;
	};
	auto transform_raw_normal = [](Gradient value)
	{
		// cross(diag(2,3,4) * (0,dz,1),
		//       diag(2,3,4) * (1,dx,0))
		return Vec3{ -12.0 * value.x, 8.0, -6.0 * value.z };
	};
	auto interpolate_vec3 = [](Vec3 begin, Vec3 end, double fraction)
	{
		return Vec3{
			begin.x + (end.x - begin.x) * fraction,
			begin.y + (end.y - begin.y) * fraction,
			begin.z + (end.z - begin.z) * fraction
		};
	};
	const Vec3 coarse_world_interpolation = interpolate_vec3(
		transform_raw_normal(shading_gradient(coarse_begin)),
		transform_raw_normal(shading_gradient(coarse_end)),
		0.5);
	const Vec3 fine_world_normal = transform_raw_normal(
		fine_edge_gradient);
	CHECK(fine_world_normal.x ==
		doctest::Approx(coarse_world_interpolation.x));
	CHECK(fine_world_normal.y ==
		doctest::Approx(coarse_world_interpolation.y));
	CHECK(fine_world_normal.z ==
		doctest::Approx(coarse_world_interpolation.z));
}

TEST_CASE("Directional shadow receiver plane stays quad uniform and fails soft")
{
	const std::string shader = ReadSource(
		"project/src/engine/Shaders/Shadow/DirectionalShadowMask.hlsl");
	const std::string compact = CompactSource(shader);
	const size_t pixel_shader = compact.find(
		"float4PSMain(VSFullscreenOutputinput):SV_Target0{");
	const size_t reconstruct = compact.find(
		"constfloat3position_ws=ReconstructWorldPosition(input.uv,scene_depth);",
		pixel_shader);
	const size_t derivative = compact.find(
		"constfloat3position_dx_ws=ddx(position_ws);", reconstruct);
	const size_t background_branch = compact.find(
		"if(IsBackgroundDepth(scene_depth)){return1.0.xxxx;}", pixel_shader);
	REQUIRE(pixel_shader != std::string::npos);
	REQUIRE(reconstruct != std::string::npos);
	REQUIRE(derivative != std::string::npos);
	REQUIRE(background_branch != std::string::npos);
	CHECK(reconstruct < derivative);
	CHECK(derivative < background_branch);

	CHECK(compact.find(
		"constfloatshadow_clip_w_squared=shadow_clip.w*shadow_clip.w;") !=
		std::string::npos);
	CHECK(compact.find(
		"if(!isfinite(shadow_clip_w_squared)||"
		"shadow_clip_w_squared<=1e-12){returnfalse;}") !=
		std::string::npos);
	CHECK(compact.find(
		"constfloat3shadow_ndc_dx=(shadow_clip_dx.xyz*shadow_clip.w-"
		"shadow_clip.xyz*shadow_clip_dx.w)/shadow_clip_w_squared;") !=
		std::string::npos);
	CHECK(compact.find(
		"constfloat3shadow_ndc_dy=(shadow_clip_dy.xyz*shadow_clip.w-"
		"shadow_clip.xyz*shadow_clip_dy.w)/shadow_clip_w_squared;") !=
		std::string::npos);
	CHECK(compact.find(
		"constfloat2atlas_uv_dx=shadow_ndc_dx.xy*float2(0.5,-0.5)*"
		"cascade.atlas_uv_scale_bias.xy;") != std::string::npos);
	CHECK(compact.find(
		"jacobian_determinant*jacobian_determinant<="
		"kReceiverPlaneJacobianRelativeEpsilon*jacobian_scale_squared") !=
		std::string::npos);
	CHECK(compact.find(
		"isfinite(depth_offset)&&abs(depth_offset)<="
		"kReceiverPlaneMaximumDepthOffset?depth_offset:0.0") !=
		std::string::npos);

	struct Vec2
	{
		double x = 0.0;
		double y = 0.0;
	};
	struct Vec3
	{
		double x = 0.0;
		double y = 0.0;
		double z = 0.0;
	};
	struct Vec4
	{
		double x = 0.0;
		double y = 0.0;
		double z = 0.0;
		double w = 0.0;
	};
	auto projective_derivative = [](Vec4 clip, Vec4 clip_derivative, Vec3& derivative)
	{
		derivative = {};
		const double w_squared = clip.w * clip.w;
		if (!std::isfinite(w_squared) || w_squared <= 1e-12)
		{
			return false;
		}
		derivative = {
			(clip_derivative.x * clip.w - clip.x * clip_derivative.w) /
				w_squared,
			(clip_derivative.y * clip.w - clip.y * clip_derivative.w) /
				w_squared,
			(clip_derivative.z * clip.w - clip.z * clip_derivative.w) /
				w_squared
		};
		return std::isfinite(derivative.x) &&
			std::isfinite(derivative.y) &&
			std::isfinite(derivative.z);
	};

	Vec3 projected{};
	REQUIRE(projective_derivative(
		{ 4.0, 6.0, 8.0, 2.0 },
		{ 1.0, 2.0, 3.0, 0.5 },
		projected));
	CHECK(projected.x == doctest::Approx(0.0));
	CHECK(projected.y == doctest::Approx(0.25));
	CHECK(projected.z == doctest::Approx(0.5));

	for (const double invalid_w : {
		1e-7,
		0.0,
		std::numeric_limits<double>::quiet_NaN(),
		std::numeric_limits<double>::infinity() })
	{
		projected = { 7.0, 8.0, 9.0 };
		CHECK_FALSE(projective_derivative(
			{ 4.0, 6.0, 8.0, invalid_w },
			{ 1.0, 2.0, 3.0, 0.5 },
			projected));
		CHECK(projected.x == 0.0);
		CHECK(projected.y == 0.0);
		CHECK(projected.z == 0.0);
	}
	auto dot = [](Vec2 lhs, Vec2 rhs)
	{
		return lhs.x * rhs.x + lhs.y * rhs.y;
	};
	auto solve_gradient = [&](Vec2 ndc_xy_dx,
		Vec2 ndc_xy_dy,
		double depth_dx,
		double depth_dy,
		Vec2 tile_scale,
		Vec2& gradient)
	{
		const Vec2 atlas_uv_dx{
			ndc_xy_dx.x * 0.5 * tile_scale.x,
			ndc_xy_dx.y * -0.5 * tile_scale.y
		};
		const Vec2 atlas_uv_dy{
			ndc_xy_dy.x * 0.5 * tile_scale.x,
			ndc_xy_dy.y * -0.5 * tile_scale.y
		};
		const double determinant =
			atlas_uv_dx.x * atlas_uv_dy.y -
			atlas_uv_dx.y * atlas_uv_dy.x;
		const double scale_squared =
			dot(atlas_uv_dx, atlas_uv_dx) *
			dot(atlas_uv_dy, atlas_uv_dy);
		if (!std::isfinite(depth_dx) || !std::isfinite(depth_dy) ||
			!std::isfinite(determinant) || !std::isfinite(scale_squared) ||
			scale_squared <= 0.0 ||
			determinant * determinant <= 1e-8 * scale_squared)
		{
			gradient = {};
			return false;
		}
		gradient = {
			(depth_dx * atlas_uv_dy.y - depth_dy * atlas_uv_dx.y) /
				determinant,
			(depth_dy * atlas_uv_dx.x - depth_dx * atlas_uv_dy.x) /
				determinant
		};
		return std::isfinite(gradient.x) && std::isfinite(gradient.y);
	};

	const Vec2 expected_gradient{ 1.5, -0.75 };
	const Vec2 tile_scale{ 0.25, 0.5 };
	const Vec2 ndc_xy_dx{ 0.2, 0.1 };
	const Vec2 ndc_xy_dy{ -0.1, 0.3 };
	const Vec2 atlas_uv_dx{
		ndc_xy_dx.x * 0.5 * tile_scale.x,
		ndc_xy_dx.y * -0.5 * tile_scale.y
	};
	const Vec2 atlas_uv_dy{
		ndc_xy_dy.x * 0.5 * tile_scale.x,
		ndc_xy_dy.y * -0.5 * tile_scale.y
	};
	Vec2 recovered{};
	REQUIRE(solve_gradient(
		ndc_xy_dx,
		ndc_xy_dy,
		dot(expected_gradient, atlas_uv_dx),
		dot(expected_gradient, atlas_uv_dy),
		tile_scale,
		recovered));
	CHECK(recovered.x == doctest::Approx(expected_gradient.x));
	CHECK(recovered.y == doctest::Approx(expected_gradient.y));
	const Vec2 atlas_texel_tap{ 1.0 / 4096.0, -2.0 / 4096.0 };
	CHECK(dot(recovered, atlas_texel_tap) ==
		doctest::Approx(0.000732421875));

	Vec2 fallback{ 7.0, 9.0 };
	CHECK_FALSE(solve_gradient(
		{ 1.0, 2.0 },
		{ 2.0, 4.0 + 1e-12 },
		0.1,
		0.2,
		{ 1.0, 1.0 },
		fallback));
	CHECK(fallback.x == 0.0);
	CHECK(fallback.y == 0.0);
	CHECK_FALSE(solve_gradient(
		{ 1.0, 0.0 },
		{ 0.0, 1.0 },
		std::numeric_limits<double>::quiet_NaN(),
		0.2,
		{ 1.0, 1.0 },
		fallback));

	auto guarded_offset = [](Vec2 gradient, Vec2 tap, bool valid)
	{
		const double offset = gradient.x * tap.x + gradient.y * tap.y;
		return valid && std::isfinite(offset) && std::abs(offset) <= 0.05 ?
			offset : 0.0;
	};
	CHECK(guarded_offset({ 1.0, 0.0 }, { 0.049, 0.0 }, true) ==
		doctest::Approx(0.049));
	CHECK(guarded_offset({ 1.0, 0.0 }, { 0.051, 0.0 }, true) == 0.0);
	CHECK(guarded_offset(
		{ std::numeric_limits<double>::infinity(), 0.0 },
		{ 0.001, 0.0 },
		true) == 0.0);
}

TEST_CASE("Terrain instance storage uses the cross-backend staging upload path")
{
	const std::string source = ReadSource(
		"project/src/engine/Function/Render/TerrainRenderPass.cpp");
	const size_t function = source.find(
		"TerrainRenderPass::ensure_instance_buffer");
	const size_t descriptor = source.find("StorageBufferDesc desc{};", function);
	const size_t creation = source.find(
		"m_renderer->create_storage_buffer(desc)", descriptor);

	REQUIRE(function != std::string::npos);
	REQUIRE(descriptor != std::string::npos);
	REQUIRE(creation != std::string::npos);
	const std::string descriptor_setup = source.substr(
		descriptor,
		creation - descriptor);
	CHECK(descriptor_setup.find("desc.cpu_write = true") ==
		std::string::npos);
}
