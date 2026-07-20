#pragma once

#include <array>
#include <cstdint>
#include <limits>

namespace AshEngine
{
	struct GpuDrivenPrototypeId
	{
		uint32_t value = 0u;

		constexpr bool is_valid() const { return value != 0u; }
	};

	constexpr bool operator==(GpuDrivenPrototypeId lhs, GpuDrivenPrototypeId rhs)
	{
		return lhs.value == rhs.value;
	}

	constexpr bool operator!=(GpuDrivenPrototypeId lhs, GpuDrivenPrototypeId rhs)
	{
		return !(lhs == rhs);
	}

	struct GpuDrivenPageHandle
	{
		uint32_t slot = std::numeric_limits<uint32_t>::max();
		uint32_t generation = 0u;

		constexpr bool is_valid() const
		{
			return slot != std::numeric_limits<uint32_t>::max() && generation != 0u;
		}
	};

	constexpr bool operator==(GpuDrivenPageHandle lhs, GpuDrivenPageHandle rhs)
	{
		return lhs.slot == rhs.slot && lhs.generation == rhs.generation;
	}

	constexpr bool operator!=(GpuDrivenPageHandle lhs, GpuDrivenPageHandle rhs)
	{
		return !(lhs == rhs);
	}

	enum class GpuDrivenTransformEncoding : uint8_t
	{
		Unknown = 0,
		CompressedTRS,
		Affine3x4F32
	};

	inline constexpr uint32_t kGpuDrivenInstancePageVersion = 1u;
	inline constexpr uint32_t kGpuDrivenCompressedTRSStride = 32u;
	inline constexpr uint32_t kGpuDrivenAffine3x4F32Stride = 48u;

	constexpr uint32_t gpu_driven_instance_stride(GpuDrivenTransformEncoding encoding)
	{
		switch (encoding)
		{
		case GpuDrivenTransformEncoding::CompressedTRS:
			return kGpuDrivenCompressedTRSStride;
		case GpuDrivenTransformEncoding::Affine3x4F32:
			return kGpuDrivenAffine3x4F32Stride;
		default:
			return 0u;
		}
	}

	struct GpuDrivenInstancePageDesc
	{
		uint32_t version = kGpuDrivenInstancePageVersion;
		std::array<float, 3> origin{};
		std::array<float, 3> bounds_min{};
		std::array<float, 3> bounds_max{};
		GpuDrivenTransformEncoding encoding = GpuDrivenTransformEncoding::Unknown;
		uint32_t instance_stride = 0u;
		uint32_t capacity = 0u;
		uint32_t count = 0u;
	};

	enum class GpuDrivenInstancePageValidationError : uint8_t
	{
		None = 0,
		UnsupportedVersion,
		UnknownEncoding,
		InvalidStride,
		ZeroCapacity,
		CountExceedsCapacity,
		PayloadByteSizeOverflow
	};

	struct GpuDrivenInstancePageValidationResult
	{
		bool valid = false;
		GpuDrivenInstancePageValidationError error =
			GpuDrivenInstancePageValidationError::UnsupportedVersion;
		uint32_t payload_byte_size = 0u;
	};

	constexpr GpuDrivenInstancePageValidationResult validate_gpu_driven_instance_page_desc(
		const GpuDrivenInstancePageDesc& desc)
	{
		if (desc.version != kGpuDrivenInstancePageVersion)
		{
			return { false, GpuDrivenInstancePageValidationError::UnsupportedVersion, 0u };
		}

		const uint32_t expected_stride = gpu_driven_instance_stride(desc.encoding);
		if (expected_stride == 0u)
		{
			return { false, GpuDrivenInstancePageValidationError::UnknownEncoding, 0u };
		}
		if (desc.instance_stride != expected_stride)
		{
			return { false, GpuDrivenInstancePageValidationError::InvalidStride, 0u };
		}
		if (desc.capacity == 0u)
		{
			return { false, GpuDrivenInstancePageValidationError::ZeroCapacity, 0u };
		}
		if (desc.count > desc.capacity)
		{
			return { false, GpuDrivenInstancePageValidationError::CountExceedsCapacity, 0u };
		}

		const uint64_t payload_byte_size =
			static_cast<uint64_t>(desc.instance_stride) * static_cast<uint64_t>(desc.capacity);
		if (payload_byte_size > std::numeric_limits<uint32_t>::max())
		{
			return { false, GpuDrivenInstancePageValidationError::PayloadByteSizeOverflow, 0u };
		}

		return {
			true,
			GpuDrivenInstancePageValidationError::None,
			static_cast<uint32_t>(payload_byte_size) };
	}

	struct GpuDrivenViewDesc
	{
		std::array<float, 16> camera_relative_view{};
		std::array<float, 16> camera_relative_projection{};
		std::array<float, 24> frustum_planes{};
		uint32_t viewport_width = 0u;
		uint32_t viewport_height = 0u;
		bool reverse_z = true;
	};

	struct GpuDrivenDrawGroupDesc
	{
		GpuDrivenPrototypeId prototype{};
		uint32_t lod = 0u;
		uint32_t section = 0u;
		uint32_t visible_list_base = 0u;
		uint32_t visible_list_capacity = 0u;
		uint32_t indirect_args_offset = 0u;
	};
}
