#include "Function/Render/RenderGraphBuilder.h"
#include "Function/Render/RenderGraphCompiler.h"
#include "Function/Render/RenderScene.h"
#include "Function/Render/TerrainLod.h"
#include "Function/Render/TerrainRenderPass.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
	auto MakeAtlas(
		AshEngine::RenderGraphBuilder& graph,
		const char* name) -> AshEngine::RenderGraphTextureRef
	{
		AshEngine::RenderTargetDesc desc{};
		desc.width = static_cast<uint16_t>(
			AshEngine::k_terrain_weight_atlas_extent);
		desc.height = static_cast<uint16_t>(
			AshEngine::k_terrain_weight_atlas_extent);
		desc.format = AshEngine::RenderTextureFormat::RGBA8_UNORM;
		desc.shader_resource = true;
		desc.unordered_access = true;
		desc.name = name;
		return graph.register_external_texture_desc_for_tests(desc, name);
	}

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

TEST_CASE("Terrain atlas update declares texture UAV before GBuffer SRV")
{
	AshEngine::RenderGraphBuilder graph =
		AshEngine::RenderGraphBuilder::create_headless_for_tests("TerrainGraph");
	const AshEngine::RenderGraphTextureRef atlas =
		MakeAtlas(graph, "TerrainWeights0");
	REQUIRE(AshEngine::add_terrain_atlas_contract_for_tests(
		graph, atlas, true));

	const auto& passes = graph.get_passes_for_tests();
	REQUIRE(passes.size() == 2u);
	CHECK(passes[0].name == "TerrainWeightAtlasUpdatePass");
	CHECK(passes[0].kind == AshEngine::RenderGraphPassKind::Compute);
	REQUIRE(passes[0].texture_usages.size() == 1u);
	CHECK(passes[0].texture_usages[0].texture == atlas);
	CHECK(passes[0].texture_usages[0].access ==
		AshEngine::RenderGraphAccess::ComputeUAV);
	CHECK(passes[1].name == "TerrainAtlasGBufferReadContract");
	CHECK(passes[1].kind == AshEngine::RenderGraphPassKind::Raster);
	REQUIRE(passes[1].texture_usages.size() == 1u);
	CHECK(passes[1].texture_usages[0].texture == atlas);
	CHECK(passes[1].texture_usages[0].access ==
		AshEngine::RenderGraphAccess::GraphicsSRV);

	AshEngine::RenderGraphCompileResult compiled{};
	REQUIRE(graph.compile_for_tests(compiled));
	REQUIRE(compiled.live_pass_indices.size() == 2u);
	CHECK(compiled.live_pass_indices[0] == 0u);
	CHECK(compiled.live_pass_indices[1] == 1u);
	REQUIRE(compiled.pass_barriers[0].texture_states.size() > atlas.index);
	REQUIRE(compiled.pass_barriers[1].texture_states.size() > atlas.index);
	CHECK(compiled.pass_barriers[0].texture_states[atlas.index] ==
		RHI::AshResourceState::UAVCompute);
	CHECK(compiled.pass_barriers[1].texture_states[atlas.index] ==
		RHI::AshResourceState::SRVGraphics);
}

TEST_CASE("Terrain atlas clean frame declares only the GBuffer read")
{
	AshEngine::RenderGraphBuilder graph =
		AshEngine::RenderGraphBuilder::create_headless_for_tests(
			"TerrainCleanGraph");
	const AshEngine::RenderGraphTextureRef atlas =
		MakeAtlas(graph, "TerrainWeights0");
	REQUIRE(AshEngine::add_terrain_atlas_contract_for_tests(
		graph, atlas, false));

	const auto& passes = graph.get_passes_for_tests();
	REQUIRE(passes.size() == 1u);
	CHECK(passes[0].kind == AshEngine::RenderGraphPassKind::Raster);
	REQUIRE(passes[0].texture_usages.size() == 1u);
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

TEST_CASE("Terrain GBuffer normals follow the morphed raster surface")
{
	const std::string surface = ReadSource(
		"project/src/engine/Shaders/Terrain/TerrainSurface.hlsl");
	const std::string compact_surface = CompactSource(surface);

	CHECK(compact_surface.find(
		"#ifTERRAIN_GBUFFERfloat3position_ws:TEXCOORD0;") !=
		std::string::npos);
	CHECK(CountText(surface, "position_ws : TEXCOORD0") == 1u);
	CHECK(surface.find("normal_ws : TEXCOORD0") == std::string::npos);
	CHECK(compact_surface.find(
		"#ifTERRAIN_GBUFFERoutput.position_ws=mul((float3x3)"
		"AshTerrainObjectToWorld,position_os);") != std::string::npos);
	CHECK(compact_surface.find(
		"output.position_ws=mul(AshTerrainObjectToWorld,"
		"float4(position_os,1.0)).xyz;") == std::string::npos);

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

	CHECK(pixel_gbuffer.find(
		"constfloat3terrain_up_vector_ws=mul((float3x3)"
		"AshTerrainObjectToWorld,float3(0.0,1.0,0.0));") != std::string::npos);
	CHECK(pixel_gbuffer.find(
		"constfloatterrain_up_length_squared=dot(terrain_up_vector_ws,"
		"terrain_up_vector_ws);") != std::string::npos);
	CHECK(pixel_gbuffer.find(
		"float3terrain_up_ws=float3(0.0,1.0,0.0);if(isfinite("
		"terrain_up_length_squared)&&terrain_up_length_squared>0.0){"
		"terrain_up_ws=terrain_up_vector_ws*rsqrt("
		"terrain_up_length_squared);}") != std::string::npos);

	CHECK(pixel_gbuffer.find(
		"constfloat3position_dx_ws=ddx(input.position_ws);"
		"constfloat3position_dy_ws=ddy(input.position_ws);") != std::string::npos);
	CHECK(pixel_gbuffer.find(
		"constfloatposition_dx_length_squared=dot(position_dx_ws,position_dx_ws);"
		"constfloatposition_dy_length_squared=dot(position_dy_ws,position_dy_ws);") !=
		std::string::npos);
	CHECK(pixel_gbuffer.find(
		"if(isfinite(position_dx_length_squared)&&position_dx_length_squared>0.0&&"
		"isfinite(position_dy_length_squared)&&position_dy_length_squared>0.0){"
		"constfloat3position_dx_direction_ws=position_dx_ws*rsqrt("
		"position_dx_length_squared);constfloat3position_dy_direction_ws="
		"position_dy_ws*rsqrt(position_dy_length_squared);") != std::string::npos);
	const bool cross_uses_derivative_directions = pixel_gbuffer.find(
		"constfloat3geometric_normal_cross=cross(position_dx_direction_ws,"
		"position_dy_direction_ws);") != std::string::npos || pixel_gbuffer.find(
		"constfloat3geometric_normal_cross=cross(position_dy_direction_ws,"
		"position_dx_direction_ws);") != std::string::npos;
	CHECK(cross_uses_derivative_directions);
	CHECK(pixel_gbuffer.find(
		"constfloatgeometric_normal_length_squared=dot(geometric_normal_cross,"
		"geometric_normal_cross);") != std::string::npos);
	CHECK(pixel_gbuffer.find("float3geometric_normal=terrain_up_ws;") !=
		std::string::npos);
	CHECK(pixel_gbuffer.find(
		"if(isfinite(geometric_normal_length_squared)&&"
		"geometric_normal_length_squared>1e-8){geometric_normal="
		"geometric_normal_cross*rsqrt(geometric_normal_length_squared);"
		"if(dot(geometric_normal,terrain_up_ws)<0.0){"
		"geometric_normal=-geometric_normal;}}") != std::string::npos);
	CHECK(pixel_gbuffer.find("geometric_normal_length_squared>1e-12") ==
		std::string::npos);

	const std::string non_gbuffer_pixel_shaders = CompactSource(
		surface.substr(lod_debug_begin));
	CHECK(non_gbuffer_pixel_shaders.find("position_ws") == std::string::npos);
	CHECK(non_gbuffer_pixel_shaders.find("ddx(") == std::string::npos);
	CHECK(non_gbuffer_pixel_shaders.find("ddy(") == std::string::npos);
	CHECK(surface.find("AshTerrainLocalNormal(") == std::string::npos);
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
