#include "Function/Asset/VegetationSurface.h"

#include "Function/Asset/VegetationCodec.h"

#include <algorithm>
#include <cmath>

namespace AshEngine
{
	namespace
	{
		VegetationSurfaceBatchResult failed_batch(const char* detail)
		{
			VegetationSurfaceBatchResult result{};
			result.status = VegetationSurfaceStatus::Failed;
			result.detail = detail;
			return result;
		}

		bool operation_is_active(const VegetationOperationControl& control)
		{
			return control.cancel_requested != nullptr &&
				control.deadline != std::chrono::steady_clock::time_point{} &&
				!control.cancel_requested->load(std::memory_order_relaxed) &&
				std::chrono::steady_clock::now() < control.deadline;
		}

		bool identity_is_valid(const VegetationSurfaceIdentity& identity)
		{
			return std::any_of(identity.surface_id.begin(), identity.surface_id.end(),
				[](const uint8_t byte) { return byte != 0; });
		}

		bool identities_match(
			const VegetationSurfaceIdentity& left,
			const VegetationSurfaceIdentity& right)
		{
			return left.surface_id == right.surface_id &&
				left.content_revision == right.content_revision &&
				left.residency_revision == right.residency_revision &&
				left.transform_revision == right.transform_revision;
		}

		bool bounds_are_valid(const VegetationSurfaceBounds& bounds)
		{
			return bounds.min_chunk_inclusive.x <= bounds.max_chunk_inclusive.x &&
				bounds.min_chunk_inclusive.z <= bounds.max_chunk_inclusive.z;
		}

		bool request_is_valid(const VegetationSurfaceSampleRequest& request)
		{
			constexpr double k_chunk_size = 256.0;
			return std::isfinite(request.local_xz.x) &&
				std::isfinite(request.local_xz.y) &&
				request.local_xz.x >= 0.0 && request.local_xz.x < k_chunk_size &&
				request.local_xz.y >= 0.0 && request.local_xz.y < k_chunk_size;
		}

		bool non_ready_payload_is_zero(const VegetationSurfaceSample& sample)
		{
			if (sample.world_height_meters != 0.0 ||
				sample.world_normal.x != 0.0 ||
				sample.world_normal.y != 0.0 ||
				sample.world_normal.z != 0.0)
			{
				return false;
			}
			return std::all_of(
				sample.material_slot_weights.begin(),
				sample.material_slot_weights.end(),
				[](const uint8_t weight) { return weight == 0; });
		}

		void canonicalize_non_ready_payload(VegetationSurfaceSample& sample)
		{
			sample.world_height_meters = 0.0;
			sample.world_normal = glm::dvec3(0.0);
			sample.material_slot_weights.fill(0);
		}

	}

	bool evaluate_vegetation_surface_normal(
		const glm::dvec3& world_normal,
		glm::dvec3& out_normalized_world_normal,
		uint16_t& out_slope_milliradians)
	{
		if (!std::isfinite(world_normal.x) ||
			!std::isfinite(world_normal.y) ||
			!std::isfinite(world_normal.z))
		{
			return false;
		}
		const double length = std::sqrt(
			world_normal.x * world_normal.x +
			world_normal.y * world_normal.y +
			world_normal.z * world_normal.z);
		if (!std::isfinite(length) || length <= 1.0e-20)
		{
			return false;
		}

		const glm::dvec3 normalized = world_normal / length;
		if (!std::isfinite(normalized.x) ||
			!std::isfinite(normalized.y) ||
			!std::isfinite(normalized.z))
		{
			return false;
		}
		const double slope = std::acos(std::clamp(normalized.y, 0.0, 1.0)) * 1000.0;
		uint16_t rounded_slope = 0;
		if (!vegetation_round_ties_even_u16(slope, rounded_slope))
		{
			return false;
		}

		out_normalized_world_normal = normalized;
		out_slope_milliradians = rounded_slope;
		return true;
	}

	VegetationSurfaceBatchResult sample_vegetation_surface_batch(
		const IVegetationSurfaceSnapshot& snapshot,
		const std::vector<VegetationSurfaceSampleRequest>& requests,
		VegetationOperationControl control)
	{
		if (!operation_is_active(control))
		{
			return failed_batch("Vegetation surface operation is cancelled or expired.");
		}
		if (requests.empty() || requests.size() > 4096u ||
			!std::all_of(requests.begin(), requests.end(), request_is_valid))
		{
			return failed_batch("Vegetation surface requests are invalid.");
		}

		try
		{
			if (!bounds_are_valid(snapshot.bounds()))
			{
				return failed_batch("Vegetation surface bounds are invalid.");
			}
		}
		catch (...)
		{
			return failed_batch("Vegetation surface bounds failed.");
		}
		if (!operation_is_active(control))
		{
			return failed_batch("Vegetation surface operation is cancelled or expired.");
		}

		VegetationSurfaceIdentity identity_before{};
		try
		{
			identity_before = snapshot.identity();
		}
		catch (...)
		{
			return failed_batch("Vegetation surface identity failed.");
		}
		if (!identity_is_valid(identity_before))
		{
			return failed_batch("Vegetation surface identity is invalid.");
		}

		VegetationSurfaceBatchResult result{};
		try
		{
			result = snapshot.sample_batch(requests, control);
		}
		catch (...)
		{
			return failed_batch("Vegetation surface sampling failed.");
		}

		VegetationSurfaceIdentity identity_after{};
		try
		{
			identity_after = snapshot.identity();
		}
		catch (...)
		{
			return failed_batch("Vegetation surface identity failed.");
		}
		if (!identities_match(identity_before, identity_after))
		{
			return failed_batch("Vegetation surface identity changed during sampling.");
		}
		if (!operation_is_active(control))
		{
			return failed_batch("Vegetation surface operation is cancelled or expired.");
		}
		if (result.samples.size() != requests.size())
		{
			return failed_batch("Vegetation surface batch shape is invalid.");
		}

		VegetationSurfaceStatus aggregate = VegetationSurfaceStatus::Ready;
		for (size_t index = 0; index < result.samples.size(); ++index)
		{
			VegetationSurfaceSample& sample = result.samples[index];
			if (sample.request_index != index)
			{
				return failed_batch("Vegetation surface sample order is invalid.");
			}

			switch (sample.status)
			{
			case VegetationSurfaceStatus::Ready:
			{
				if (!std::isfinite(sample.world_height_meters))
				{
					return failed_batch("Vegetation surface height is invalid.");
				}
				glm::dvec3 normalized{};
				uint16_t slope = 0;
				if (!evaluate_vegetation_surface_normal(sample.world_normal, normalized, slope))
				{
					return failed_batch("Vegetation surface normal is invalid.");
				}
				uint32_t weight_sum = 0;
				for (const uint8_t weight : sample.material_slot_weights)
				{
					weight_sum += weight;
				}
				if (weight_sum != 255u)
				{
					return failed_batch("Vegetation surface material weights are invalid.");
				}
				sample.world_normal = normalized;
				break;
			}
			case VegetationSurfaceStatus::Outside:
				if (!non_ready_payload_is_zero(sample))
				{
					return failed_batch("Vegetation surface non-ready payload is not empty.");
				}
				canonicalize_non_ready_payload(sample);
				break;
			case VegetationSurfaceStatus::Pending:
				if (!non_ready_payload_is_zero(sample))
				{
					return failed_batch("Vegetation surface non-ready payload is not empty.");
				}
				canonicalize_non_ready_payload(sample);
				if (aggregate == VegetationSurfaceStatus::Ready)
				{
					aggregate = VegetationSurfaceStatus::Pending;
				}
				break;
			case VegetationSurfaceStatus::Failed:
				if (!non_ready_payload_is_zero(sample))
				{
					return failed_batch("Vegetation surface non-ready payload is not empty.");
				}
				canonicalize_non_ready_payload(sample);
				aggregate = VegetationSurfaceStatus::Failed;
				break;
			default:
				return failed_batch("Vegetation surface sample status is invalid.");
			}
		}

		if (result.status != aggregate)
		{
			return failed_batch("Vegetation surface aggregate status is invalid.");
		}
		if (!operation_is_active(control))
		{
			return failed_batch("Vegetation surface operation is cancelled or expired.");
		}
		return result;
	}
}
