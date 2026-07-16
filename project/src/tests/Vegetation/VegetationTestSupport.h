#pragma once

#include "Function/Asset/VegetationCodec.h"
#include "Function/Asset/VegetationSurface.h"
#include "Function/Scene/VegetationSurfaceProvider.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace VegetationTest
{
	inline std::string ToHex(const AshEngine::VegetationSha256& digest)
	{
		std::ostringstream stream;
		stream << std::hex << std::setfill('0');
		for (const uint8_t byte : digest)
		{
			stream << std::setw(2) << static_cast<uint32_t>(byte);
		}
		return stream.str();
	}

	inline AshEngine::VegetationSurfaceIdentity SurfaceIdentity(
		const uint8_t first_byte = 1,
		const uint64_t content_revision = 10,
		const uint64_t residency_revision = 20,
		const uint64_t transform_revision = 30)
	{
		AshEngine::VegetationSurfaceIdentity identity{};
		for (size_t index = 0; index < identity.surface_id.size(); ++index)
		{
			identity.surface_id[index] = static_cast<uint8_t>(first_byte + index);
		}
		identity.content_revision = content_revision;
		identity.residency_revision = residency_revision;
		identity.transform_revision = transform_revision;
		return identity;
	}

	inline AshEngine::VegetationSurfaceSample ReadySurfaceSample(
		const uint32_t request_index,
		const double world_height_meters,
		const glm::dvec3& world_normal,
		const std::array<uint8_t, 8>& material_slot_weights = { 255, 0, 0, 0, 0, 0, 0, 0 })
	{
		AshEngine::VegetationSurfaceSample sample{};
		sample.request_index = request_index;
		sample.status = AshEngine::VegetationSurfaceStatus::Ready;
		sample.world_height_meters = world_height_meters;
		sample.world_normal = world_normal;
		sample.material_slot_weights = material_slot_weights;
		return sample;
	}

	inline AshEngine::VegetationSurfaceSample NonReadySurfaceSample(
		const uint32_t request_index,
		const AshEngine::VegetationSurfaceStatus status)
	{
		AshEngine::VegetationSurfaceSample sample{};
		sample.request_index = request_index;
		sample.status = status;
		return sample;
	}

	inline AshEngine::VegetationSurfaceSampleRequest SurfaceRequest(
		const double world_x,
		const double world_z)
	{
		AshEngine::VegetationSurfaceSampleRequest request{};
		AshEngine::split_vegetation_world_xz(
			glm::dvec2(world_x, world_z), request.chunk, request.local_xz);
		return request;
	}

	inline AshEngine::VegetationSurfaceSampleRequest SurfaceRequest(
		const AshEngine::VegetationChunkCoord chunk,
		const glm::dvec2& local_xz)
	{
		AshEngine::VegetationSurfaceSampleRequest request{};
		request.chunk = chunk;
		request.local_xz = local_xz;
		return request;
	}

	inline AshEngine::VegetationOperationControl ActiveControl(
		const std::chrono::milliseconds remaining)
	{
		AshEngine::VegetationOperationControl control{};
		control.cancel_requested = std::make_shared<std::atomic_bool>(false);
		control.deadline = std::chrono::steady_clock::now() + remaining;
		return control;
	}

	class ScriptedSurfaceSnapshot final : public AshEngine::IVegetationSurfaceSnapshot
	{
	public:
		AshEngine::VegetationSurfaceIdentity identity_before = SurfaceIdentity();
		AshEngine::VegetationSurfaceIdentity identity_after = identity_before;
		AshEngine::VegetationSurfaceBounds surface_bounds{
			AshEngine::VegetationChunkCoord{ -1024, -1024 },
			AshEngine::VegetationChunkCoord{ 1024, 1024 }
		};
		AshEngine::VegetationSurfaceBatchResult result{};
		bool throw_on_bounds = false;
		bool throw_on_sample = false;
		size_t throw_on_identity_call = 0;
		std::function<void(const AshEngine::VegetationOperationControl&)> before_sample_return{};

		mutable size_t bounds_call_count = 0;
		mutable size_t identity_call_count = 0;
		mutable size_t sample_call_count = 0;
		mutable size_t last_request_count = 0;
		mutable std::shared_ptr<const std::atomic_bool> last_cancel_requested{};
		mutable std::chrono::steady_clock::time_point last_deadline{};

		AshEngine::VegetationSurfaceIdentity identity() const override
		{
			++identity_call_count;
			if (throw_on_identity_call == identity_call_count)
			{
				throw std::runtime_error("scripted identity failure");
			}
			return identity_call_count == 1 ? identity_before : identity_after;
		}

		AshEngine::VegetationSurfaceBounds bounds() const override
		{
			++bounds_call_count;
			if (throw_on_bounds)
			{
				throw std::runtime_error("scripted bounds failure");
			}
			return surface_bounds;
		}

		AshEngine::VegetationSurfaceBatchResult sample_batch(
			const std::vector<AshEngine::VegetationSurfaceSampleRequest>& requests,
			AshEngine::VegetationOperationControl control) const override
		{
			++sample_call_count;
			last_request_count = requests.size();
			last_cancel_requested = control.cancel_requested;
			last_deadline = control.deadline;
			if (throw_on_sample)
			{
				throw std::runtime_error("scripted sample failure");
			}
			if (before_sample_return)
			{
				before_sample_return(control);
			}
			return result;
		}
	};

	class ScriptedSurfaceProvider final : public AshEngine::IVegetationSurfaceProvider
	{
	public:
		AshEngine::VegetationSurfaceCaptureResult result{};
		bool throw_on_capture = false;
		mutable size_t capture_call_count = 0;
		mutable AshEngine::VegetationSurfaceBinding last_binding{};

		AshEngine::VegetationSurfaceCaptureResult capture(
			const AshEngine::VegetationSurfaceBinding binding) const override
		{
			++capture_call_count;
			last_binding = binding;
			if (throw_on_capture)
			{
				throw std::runtime_error("scripted capture failure");
			}
			return result;
		}
	};
}
