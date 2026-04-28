#pragma once

#include "Base/hcore.h"
#include "Function/Render/EngineShaderFamilyRegistry.h"
#include "Function/Render/Material.h"
#include "Function/Render/MaterialShaderSourceBuilder.h"
#include "Function/Render/RenderDevice.h"
#include "Graphics/Shader.h"
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace AshEngine
{
	class Renderer;

	inline constexpr const char* k_material_parameter_block_name = "AshMaterialParameters";

	struct ASH_API MaterialBindingLayoutEntry
	{
		std::string name{};
		RHI::ShaderResourceBindingType type = RHI::ShaderResourceBindingType::Unknown;
		uint32_t bind_point = 0;
	};

	struct ASH_API MaterialPassRelevance
	{
		bool supports_surface = false;
		bool supports_depth_prepass = false;
		bool supports_base_pass = false;
		bool is_masked = false;
		bool is_transparent = false;
		MaterialDomain domain = MaterialDomain::Surface;
	};

	struct ASH_API MaterialResource
	{
		MaterialUsageDesc usage{};
		uint64_t combined_source_hash = 0;
		std::string base_shader_path{};
		std::string user_shader_path{};
		std::string generated_bindings_path{};
		std::string shader_macro{};
		std::string program_name{};
		std::string parameter_block_name{};
		GraphicsProgramState program_state{};
		std::shared_ptr<const VertexDecl> vertex_decl = nullptr;
		std::vector<MaterialBindingLayoutEntry> binding_layout{};
		RHI::ShaderParameterBlockLayout parameter_block_layout{};
		GraphicsProgram* program = nullptr;
		MaterialPassRelevance pass_relevance{};
		MaterialBlendMode blend_mode = MaterialBlendMode::Opaque;
		MaterialShadingModel shading_model = MaterialShadingModel::DefaultLit;
		std::shared_ptr<UniformBuffer> material_uniforms = nullptr;
	};

	class ASH_API MaterialShaderMap
	{
	public:
		~MaterialShaderMap();

		bool initialize(Renderer* renderer, const EngineShaderFamilyRegistry* family_registry);
		void shutdown();

		const MaterialResource* find_or_create_resource(
			const MaterialInterface& material,
			const MaterialUsageDesc& usage,
			std::string* out_error = nullptr);

	private:
		struct MaterialPermutationKey
		{
			uint64_t compile_hash = 0;
			MaterialUsageDesc usage{};

			bool operator==(const MaterialPermutationKey& rhs) const
			{
				return compile_hash == rhs.compile_hash && usage == rhs.usage;
			}
		};

		struct MaterialPermutationKeyHash
		{
			size_t operator()(const MaterialPermutationKey& key) const noexcept
			{
				size_t hash_value = 0;
				ASH_HASH::hash_combine(hash_value, key.compile_hash);
				ASH_HASH::hash_combine(hash_value, static_cast<uint8_t>(key.usage.domain));
				ASH_HASH::hash_combine(hash_value, static_cast<uint8_t>(key.usage.family));
				ASH_HASH::hash_combine(hash_value, static_cast<uint8_t>(key.usage.pass));
				ASH_HASH::hash_combine(hash_value, key.usage.capability_mask);
				return hash_value;
			}
		};

		struct CachedMaterialResource
		{
			MaterialResource resource{};
		};

	private:
		Renderer* m_renderer = nullptr;
		const EngineShaderFamilyRegistry* m_family_registry = nullptr;
		std::filesystem::path m_generated_source_root{};
		std::unique_ptr<MaterialShaderSourceBuilder> m_source_builder = nullptr;
		std::unordered_map<MaterialPermutationKey, std::unique_ptr<CachedMaterialResource>, MaterialPermutationKeyHash> m_resources{};
	};
}
