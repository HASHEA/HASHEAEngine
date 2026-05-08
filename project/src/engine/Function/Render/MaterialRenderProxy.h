#pragma once

#include "Base/hcore.h"
#include "Function/Render/MaterialShaderMap.h"
#include <memory>
#include <unordered_map>
#include <vector>

namespace AshEngine
{
	class MaterialSystem;
	class RenderAssetManager;
	class Renderer;

	class ASH_API MaterialRenderProxy
	{
	public:
		struct MaterialBindingSnapshot
		{
			uint64_t version = 0;
			std::vector<uint8_t> packed_parameter_data{};
			std::unordered_map<std::string, std::shared_ptr<RenderTarget>> textures{};
			std::unordered_map<std::string, std::shared_ptr<RenderSampler>> samplers{};
		};

	public:
		explicit MaterialRenderProxy(
			std::shared_ptr<const MaterialInterface> material,
			MaterialSystem* material_system = nullptr);

	public:
		const std::shared_ptr<const MaterialInterface>& get_material() const;
		const MaterialResource* get_surface_staticmesh_basepass_resource() const;
		const MaterialResource* get_surface_staticmesh_depthonly_resource() const;
		bool prepare_surface_staticmesh(RenderAssetManager& asset_manager, Renderer& renderer);
		bool ensure_program(Renderer& renderer);
		bool update_bindings(RenderAssetManager& asset_manager);
		bool needs_surface_staticmesh_preparation() const;

	private:
		bool ensure_v2_resource_templates();
		bool create_v2_program_instance(
			const MaterialResource& template_resource,
			Renderer& renderer,
			std::unique_ptr<GraphicsProgram>& out_program) const;
		bool bind_v2_program_resources();

	private:
		std::shared_ptr<const MaterialInterface> m_material = nullptr;
		MaterialSystem* m_material_system = nullptr;
		MaterialBindingSnapshot m_binding_snapshot{};
		std::shared_ptr<UniformBuffer> m_v2_material_uniforms = nullptr;
		const MaterialResource* m_surface_staticmesh_basepass_template = nullptr;
		const MaterialResource* m_surface_staticmesh_depthonly_template = nullptr;
		MaterialResource m_surface_staticmesh_basepass_resource{};
		MaterialResource m_surface_staticmesh_depthonly_resource{};
		std::unique_ptr<GraphicsProgram> m_surface_staticmesh_basepass_program = nullptr;
		std::unique_ptr<GraphicsProgram> m_surface_staticmesh_depthonly_program = nullptr;
		uint64_t m_runtime_binding_version = 0;
		uint64_t m_bound_binding_version = 0;
		uint64_t m_material_version = 0;
		uint64_t m_v2_compile_hash = 0;
	};
}
