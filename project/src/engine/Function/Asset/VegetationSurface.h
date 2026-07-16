#pragma once

#include "Base/hcore.h"
#include "Function/Asset/VegetationTypes.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace AshEngine
{
	enum class VegetationSurfaceStatus : uint8_t
	{
		Ready,
		Pending,
		Outside,
		Failed
	};

	struct VegetationSurfaceIdentity
	{
		VegetationId surface_id{};
		uint64_t content_revision = 0;
		uint64_t residency_revision = 0;
		uint64_t transform_revision = 0;
	};

	struct VegetationSurfaceSampleRequest
	{
		VegetationChunkCoord chunk{};
		glm::dvec2 local_xz{ 0.0 };
	};

	struct VegetationSurfaceBounds
	{
		VegetationChunkCoord min_chunk_inclusive{};
		VegetationChunkCoord max_chunk_inclusive{};
	};

	struct VegetationSurfaceSample
	{
		uint32_t request_index = 0;
		VegetationSurfaceStatus status = VegetationSurfaceStatus::Failed;
		double world_height_meters = 0.0;
		glm::dvec3 world_normal{ 0.0 };
		std::array<uint8_t, 8> material_slot_weights{};
	};

	struct VegetationSurfaceBatchResult
	{
		VegetationSurfaceStatus status = VegetationSurfaceStatus::Failed;
		std::vector<VegetationSurfaceSample> samples{};
		std::string detail{};
	};

	struct VegetationOperationControl
	{
		std::shared_ptr<const std::atomic_bool> cancel_requested{};
		std::chrono::steady_clock::time_point deadline{};
	};

	ASH_API bool evaluate_vegetation_surface_normal(
		const glm::dvec3& world_normal,
		glm::dvec3& out_normalized_world_normal,
		uint16_t& out_slope_milliradians);

	class ASH_API IVegetationSurfaceSnapshot
	{
	public:
		virtual ~IVegetationSurfaceSnapshot() = default;
		virtual VegetationSurfaceIdentity identity() const = 0;
		virtual VegetationSurfaceBounds bounds() const = 0;
		virtual VegetationSurfaceBatchResult sample_batch(
			const std::vector<VegetationSurfaceSampleRequest>& requests,
			VegetationOperationControl control) const = 0;
	};

	ASH_API VegetationSurfaceBatchResult sample_vegetation_surface_batch(
		const IVegetationSurfaceSnapshot& snapshot,
		const std::vector<VegetationSurfaceSampleRequest>& requests,
		VegetationOperationControl control);
}
