#pragma once

#include "Base/hcore.h"
#include "Base/hplatform.h"
#include "Function/Render/Material.h"
#include "Function/Scene/SceneComponents.h"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace AshEngine
{
	enum class MeshPrimitiveTopology : uint8_t
	{
		Triangles = 0,
		Lines,
		Points
	};

	struct MeshVertex
	{
		glm::vec3 position{ 0.0f, 0.0f, 0.0f };
		glm::vec3 normal{ 0.0f, 0.0f, 1.0f };
		glm::vec4 tangent{ 1.0f, 0.0f, 0.0f, 1.0f };
		glm::vec2 uv0{ 0.0f, 0.0f };
		glm::vec2 uv1{ 0.0f, 0.0f };
		glm::vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
	};

	struct MeshSection
	{
		std::string name{};
		uint32_t vertex_offset = 0;
		uint32_t vertex_count = 0;
		uint32_t index_offset = 0;
		uint32_t index_count = 0;
		uint32_t material_slot = k_invalid_material_slot;
		MeshPrimitiveTopology topology = MeshPrimitiveTopology::Triangles;
	};

	struct MaterialSlot
	{
		std::string name{};
		glm::vec4 base_color_factor{ 1.0f, 1.0f, 1.0f, 1.0f };
		glm::vec3 emissive_factor{ 0.0f, 0.0f, 0.0f };
		float metallic_factor = 0.0f;
		float roughness_factor = 1.0f;
		std::vector<MaterialSamplerDefinition> sampler_definitions{};
		MaterialTextureBinding base_color_texture{};
		MaterialTextureBinding normal_texture{};
		MaterialTextureBinding metallic_roughness_texture{};
		MaterialTextureBinding emissive_texture{};
	};

	struct ModelMaterialReference
	{
		uint32_t material_slot = k_invalid_material_slot;
		std::string material_path{};
	};

	struct Mesh
	{
		std::string name{};
		std::filesystem::path source_path{};
		std::vector<MeshVertex> vertices{};
		std::vector<uint32_t> indices{};
		std::vector<MeshSection> sections{};
		glm::vec3 bounds_min{ 0.0f, 0.0f, 0.0f };
		glm::vec3 bounds_max{ 0.0f, 0.0f, 0.0f };
		bool has_normals = false;
		bool has_tangents = false;
		bool has_uv0 = false;
		bool has_uv1 = false;
		bool has_vertex_colors = false;

		ASH_API bool has_geometry() const;
	};

	struct ModelNode
	{
		std::string name{};
		int32_t parent_index = -1;
		std::vector<uint32_t> children{};
		int32_t mesh_index = -1;
		glm::mat4 local_transform{ 1.0f };
	};

	struct Model
	{
		std::string name{};
		std::filesystem::path source_path{};
		std::vector<Mesh> meshes{};
		std::vector<ModelNode> nodes{};
		std::vector<uint32_t> root_nodes{};
		std::vector<MaterialSlot> material_slots{};
		std::vector<ModelMaterialReference> default_materials{};

		ASH_API bool is_valid() const;
	};

	struct AshAssetNode
	{
		std::string name{};
		int32_t parent_index = -1;
		std::vector<uint32_t> children{};
		TransformComponent transform{};
		std::optional<CameraComponent> camera{};
		std::optional<LightComponent> light{};
		std::optional<MeshComponent> mesh{};
		std::optional<EnvironmentComponent> environment{};
	};

	struct AshAsset
	{
		std::string name{};
		std::filesystem::path source_path{};
		std::vector<AshAssetNode> nodes{};
		std::vector<uint32_t> root_nodes{};

		ASH_API bool is_valid() const;
	};

	ASH_API bool load_mesh_from_file(const std::filesystem::path& path, Mesh& out_mesh, std::string* out_error = nullptr);
	ASH_API bool load_model_from_file(const std::filesystem::path& path, Model& out_model, std::string* out_error = nullptr);
	ASH_API bool load_ashasset_from_file(const std::filesystem::path& path, AshAsset& out_asset, std::string* out_error = nullptr);
	ASH_API bool save_ashasset_to_file(const AshAsset& asset, const std::filesystem::path& path, std::string* out_error = nullptr);
	ASH_API AshAsset make_ashasset_from_model(const Model& model, std::filesystem::path source_asset_path = {});
	ASH_API const Mesh* get_model_mesh_by_index(const Model& model, uint32_t mesh_index);
	ASH_API bool try_get_model_mesh_bounds(const Model& model, uint32_t mesh_index, glm::vec3& out_bounds_min, glm::vec3& out_bounds_max);
	ASH_API const MaterialSlot* get_model_material_slot_by_index(const Model& model, uint32_t material_slot);
}
