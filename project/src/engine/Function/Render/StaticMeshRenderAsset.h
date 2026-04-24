#pragma once

#include "Base/hcore.h"
#include "Function/Asset/AssetData.h"
#include "Function/Render/RenderDevice.h"
#include "Function/Render/VertexDecl.h"
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace AshEngine
{
	class MaterialInterface;
	class MaterialRenderProxy;

	enum class StaticMeshRenderAssetState : uint8_t
	{
		Uninitialized = 0,
		Loading,
		CpuReady,
		GpuReady,
		Failed
	};

	struct ASH_API StaticMeshRenderSection
	{
		uint32_t first_index = 0;
		uint32_t index_count = 0;
		uint32_t material_slot = k_invalid_material_slot;
		MeshPrimitiveTopology topology = MeshPrimitiveTopology::Triangles;
		std::shared_ptr<const MaterialInterface> material = nullptr;
	};

	struct ASH_API ResolvedStaticMeshSection
	{
		uint32_t first_index = 0;
		uint32_t index_count = 0;
		uint32_t material_slot = k_invalid_material_slot;
		MeshPrimitiveTopology topology = MeshPrimitiveTopology::Triangles;
		std::shared_ptr<const MaterialInterface> material = nullptr;
		std::shared_ptr<MaterialRenderProxy> material_proxy = nullptr;
	};

	struct ASH_API StaticMeshRenderBounds
	{
		bool is_valid = false;
		glm::vec3 local_min{ 0.0f, 0.0f, 0.0f };
		glm::vec3 local_max{ 0.0f, 0.0f, 0.0f };
	};

	struct ASH_API StaticMeshRenderResource
	{
		std::shared_ptr<VertexBuffer> vertex_buffer = nullptr;
		std::shared_ptr<IndexBuffer> index_buffer = nullptr;
		std::shared_ptr<const VertexDecl> vertex_decl = nullptr;
		uint32_t vertex_count = 0;
		uint32_t index_count = 0;
		RenderIndexFormat index_format = RenderIndexFormat::UInt32;

		bool is_valid() const;
	};

	class ASH_API StaticMeshRenderAsset
	{
	public:
		StaticMeshRenderAsset() = default;

	public:
		bool is_cpu_ready() const;
		bool is_gpu_ready() const;
		bool has_failed() const;
		std::string get_last_error() const;

	public:
		mutable std::mutex mutex{};
		std::string asset_path{};
		uint32_t mesh_index = 0;
		std::string name{};
		StaticMeshRenderBounds bounds{};
		std::vector<MeshVertex> vertices{};
		std::vector<uint32_t> indices{};
		std::vector<StaticMeshRenderSection> sections{};
		std::vector<MaterialSlot> material_slots{};
		std::shared_ptr<StaticMeshRenderResource> resource = nullptr;
		std::shared_future<std::shared_ptr<const Model>> model_future{};
		StaticMeshRenderAssetState state = StaticMeshRenderAssetState::Uninitialized;
		std::string last_error{};
		uint64_t load_generation = 0;
	};
}
