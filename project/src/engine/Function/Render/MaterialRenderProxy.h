#pragma once

#include "Base/hcore.h"
#include "Function/Render/Material.h"
#include "Function/Render/RenderDevice.h"
#include <memory>

namespace AshEngine
{
	class RenderAssetManager;
	class Renderer;
	class TextureAsset;

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
		MaterialPassRelevance pass_relevance{};
		MaterialBlendMode blend_mode = MaterialBlendMode::Opaque;
		MaterialShadingModel shading_model = MaterialShadingModel::DefaultLit;
		std::shared_ptr<UniformBuffer> material_uniforms = nullptr;
	};

	class ASH_API MaterialRenderProxy
	{
	public:
		explicit MaterialRenderProxy(std::shared_ptr<const MaterialInterface> material);

	public:
		const std::shared_ptr<const MaterialInterface>& get_material() const;
		const MaterialResource& get_resource() const;
		GraphicsProgram* get_program() const;
		bool ensure_program(Renderer& renderer);
		bool update_bindings(RenderAssetManager& asset_manager);

	private:
		struct MaterialUniformData
		{
			glm::vec4 base_color_factor{ 1.0f };
			glm::vec4 emissive_factor_and_alpha_cutoff{ 0.0f, 0.0f, 0.0f, -1.0f };
			glm::vec4 metallic_roughness_and_flags{ 0.0f, 1.0f, 0.0f, 0.0f };
			glm::vec4 texture_flags{ 0.0f };
		};

		struct ResolvedSamplerBinding
		{
			std::string material_sampler_name{};
			std::string shader_sampler_name{};
			std::shared_ptr<RenderSampler> sampler = nullptr;
		};

	private:
		bool bind_program_resources();
		bool validate_program_sampler_layout(const std::string& material_asset_path) const;

	private:
		std::shared_ptr<const MaterialInterface> m_material = nullptr;
		MaterialResource m_resource{};
		std::shared_ptr<TextureAsset> m_base_color_texture = nullptr;
		std::shared_ptr<TextureAsset> m_normal_texture = nullptr;
		std::shared_ptr<TextureAsset> m_metallic_roughness_texture = nullptr;
		std::shared_ptr<TextureAsset> m_emissive_texture = nullptr;
		std::vector<ResolvedSamplerBinding> m_sampler_bindings{};
		std::unique_ptr<GraphicsProgram> m_program = nullptr;
		MaterialUniformData m_uniform_data{};
		uint64_t m_material_version = 0;
		uint64_t m_program_state_key = 0;
		std::string m_program_shader_macro{};
	};
}
