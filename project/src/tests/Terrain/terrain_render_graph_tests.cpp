#include "Function/Render/RenderGraphBuilder.h"
#include "Function/Render/RenderGraphCompiler.h"
#include "Function/Render/TerrainRenderPass.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <fstream>
#include <iterator>
#include <string>

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
