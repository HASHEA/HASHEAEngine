#include "Function/Asset/VegetationBrush.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

namespace AshEngine
{
	namespace
	{
		constexpr int64_t k_millimeters_per_chunk = 256000;
		constexpr int64_t k_max_safe_axis_delta_mm = 1000000000;
		constexpr uint32_t k_min_brush_radius_mm = 250;
		constexpr uint32_t k_max_brush_radius_mm = 1024000;
		constexpr uint32_t k_max_brush_spacing_mm = 2048000;

		bool checked_add_int64(const int64_t lhs, const int64_t rhs, int64_t& out)
		{
			if ((rhs > 0 && lhs > std::numeric_limits<int64_t>::max() - rhs) ||
				(rhs < 0 && lhs < std::numeric_limits<int64_t>::min() - rhs))
			{
				return false;
			}
			out = lhs + rhs;
			return true;
		}

		bool checked_subtract_int64(const int64_t lhs, const int64_t rhs, int64_t& out)
		{
			if ((rhs > 0 && lhs < std::numeric_limits<int64_t>::min() + rhs) ||
				(rhs < 0 && lhs > std::numeric_limits<int64_t>::max() + rhs))
			{
				return false;
			}
			out = lhs - rhs;
			return true;
		}

		bool checked_multiply_int64(const int64_t lhs, const int64_t rhs, int64_t& out)
		{
			if (lhs == 0 || rhs == 0)
			{
				out = 0;
				return true;
			}
			if (lhs == -1)
			{
				if (rhs == std::numeric_limits<int64_t>::min())
				{
					return false;
				}
				out = -rhs;
				return true;
			}
			if (rhs == -1)
			{
				if (lhs == std::numeric_limits<int64_t>::min())
				{
					return false;
				}
				out = -lhs;
				return true;
			}

			if (lhs > 0)
			{
				if ((rhs > 0 && lhs > std::numeric_limits<int64_t>::max() / rhs) ||
					(rhs < 0 && rhs < std::numeric_limits<int64_t>::min() / lhs))
				{
					return false;
				}
			}
			else if ((rhs > 0 && lhs < std::numeric_limits<int64_t>::min() / rhs) ||
				(rhs < 0 && lhs < std::numeric_limits<int64_t>::max() / rhs))
			{
				return false;
			}

			out = lhs * rhs;
			return true;
		}

		uint64_t absolute_int64(const int64_t value)
		{
			return value >= 0
				? static_cast<uint64_t>(value)
				: static_cast<uint64_t>(-(value + 1)) + 1;
		}

		bool round_double_ties_even_to_int64(const double value, int64_t& out)
		{
			constexpr double k_int64_minimum = -9223372036854775808.0;
			constexpr double k_int64_maximum_exclusive = 9223372036854775808.0;
			if (!std::isfinite(value))
			{
				return false;
			}

			const double lower = std::floor(value);
			const double fraction = value - lower;
			double rounded = lower;
			if (fraction > 0.5 ||
				(fraction == 0.5 && std::fmod(lower, 2.0) != 0.0))
			{
				rounded += 1.0;
			}
			if (!std::isfinite(rounded) || rounded < k_int64_minimum ||
				rounded >= k_int64_maximum_exclusive)
			{
				return false;
			}
			out = static_cast<int64_t>(rounded);
			return true;
		}

		uint64_t round_unsigned_ratio_ties_even(
			const uint64_t numerator,
			const uint64_t denominator)
		{
			const uint64_t quotient = numerator / denominator;
			const uint64_t remainder = numerator % denominator;
			const bool round_up = remainder > denominator - remainder ||
				(remainder == denominator - remainder && (quotient & 1u) != 0);
			return quotient + static_cast<uint64_t>(round_up);
		}

		bool round_scaled_delta_ties_even(
			const int64_t delta,
			const uint64_t numerator,
			const uint64_t denominator,
			int64_t& out)
		{
			if (denominator == 0)
			{
				return false;
			}

			const uint64_t absolute_delta = absolute_int64(delta);
			if (numerator != 0 &&
				absolute_delta > std::numeric_limits<uint64_t>::max() / numerator)
			{
				return false;
			}
			const uint64_t rounded = round_unsigned_ratio_ties_even(
				absolute_delta * numerator, denominator);
			if (delta >= 0)
			{
				if (rounded > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
				{
					return false;
				}
				out = static_cast<int64_t>(rounded);
				return true;
			}

			constexpr uint64_t k_int64_minimum_magnitude = uint64_t{ 1 } << 63;
			if (rounded > k_int64_minimum_magnitude)
			{
				return false;
			}
			out = rounded == k_int64_minimum_magnitude
				? std::numeric_limits<int64_t>::min()
				: -static_cast<int64_t>(rounded);
			return true;
		}

		uint64_t floor_integer_square_root(uint64_t value)
		{
			uint64_t result = 0;
			uint64_t bit = uint64_t{ 1 } << 62;
			while (bit > value)
			{
				bit >>= 2;
			}
			while (bit != 0)
			{
				if (value >= result + bit)
				{
					value -= result + bit;
					result = (result >> 1) + bit;
				}
				else
				{
					result >>= 1;
				}
				bit >>= 2;
			}
			return result;
		}

		bool append_canonical_run(
			std::vector<VegetationSafeStrokeSegment>& segments,
			const VegetationWorldMillimeterPoint& run_begin,
			const VegetationWorldMillimeterPoint& run_end,
			const VegetationWorldMillimeterPoint& primitive_direction,
			uint64_t remaining_steps)
		{
			uint64_t maximum_steps = std::numeric_limits<uint64_t>::max();
			const uint64_t absolute_x = absolute_int64(primitive_direction.x);
			const uint64_t absolute_z = absolute_int64(primitive_direction.z);
			if (absolute_x != 0)
			{
				maximum_steps = std::min(maximum_steps,
					static_cast<uint64_t>(k_max_safe_axis_delta_mm) / absolute_x);
			}
			if (absolute_z != 0)
			{
				maximum_steps = std::min(maximum_steps,
					static_cast<uint64_t>(k_max_safe_axis_delta_mm) / absolute_z);
			}
			if (maximum_steps == 0 || remaining_steps == 0)
			{
				return false;
			}

			VegetationWorldMillimeterPoint current = run_begin;
			while (remaining_steps != 0)
			{
				const uint64_t step_count = std::min(remaining_steps, maximum_steps);
				if (step_count > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
				{
					return false;
				}

				int64_t delta_x = 0;
				int64_t delta_z = 0;
				VegetationWorldMillimeterPoint next{};
				if (!checked_multiply_int64(primitive_direction.x,
						static_cast<int64_t>(step_count), delta_x) ||
					!checked_multiply_int64(primitive_direction.z,
						static_cast<int64_t>(step_count), delta_z) ||
					!checked_add_int64(current.x, delta_x, next.x) ||
					!checked_add_int64(current.z, delta_z, next.z))
				{
					return false;
				}

				segments.push_back({ current, next });
				current = next;
				remaining_steps -= step_count;
			}
			return current == run_end;
		}

		bool interpolate_segment_point(
			const VegetationSafeStrokeSegment& segment,
			const uint64_t numerator,
			const uint64_t denominator,
			VegetationWorldMillimeterPoint& out)
		{
			int64_t delta_x = 0;
			int64_t delta_z = 0;
			int64_t scaled_x = 0;
			int64_t scaled_z = 0;
			return checked_subtract_int64(segment.end.x, segment.begin.x, delta_x) &&
				checked_subtract_int64(segment.end.z, segment.begin.z, delta_z) &&
				round_scaled_delta_ties_even(delta_x, numerator, denominator, scaled_x) &&
				round_scaled_delta_ties_even(delta_z, numerator, denominator, scaled_z) &&
				checked_add_int64(segment.begin.x, scaled_x, out.x) &&
				checked_add_int64(segment.begin.z, scaled_z, out.z);
		}
	}

	bool vegetation_surface_request_to_world_millimeter(
		const VegetationSurfaceSampleRequest& request,
		VegetationWorldMillimeterPoint& out_point)
	{
		if (!std::isfinite(request.local_xz.x) || !std::isfinite(request.local_xz.y) ||
			request.local_xz.x < 0.0 || request.local_xz.x >= 256.0 ||
			request.local_xz.y < 0.0 || request.local_xz.y >= 256.0)
		{
			return false;
		}

		int64_t chunk_x_mm = 0;
		int64_t chunk_z_mm = 0;
		int64_t local_x_mm = 0;
		int64_t local_z_mm = 0;
		VegetationWorldMillimeterPoint converted{};
		if (!checked_multiply_int64(request.chunk.x, k_millimeters_per_chunk, chunk_x_mm) ||
			!checked_multiply_int64(request.chunk.z, k_millimeters_per_chunk, chunk_z_mm) ||
			!round_double_ties_even_to_int64(request.local_xz.x * 1000.0, local_x_mm) ||
			!round_double_ties_even_to_int64(request.local_xz.y * 1000.0, local_z_mm) ||
			!checked_add_int64(chunk_x_mm, local_x_mm, converted.x) ||
			!checked_add_int64(chunk_z_mm, local_z_mm, converted.z))
		{
			return false;
		}

		out_point = converted;
		return true;
	}

	VegetationStrokeCanonicalizationResult canonicalize_vegetation_stroke(
		const std::vector<VegetationWorldMillimeterPoint>& raw_points)
	{
		VegetationStrokeCanonicalizationResult result{};
		if (raw_points.empty())
		{
			result.error = "stroke path is empty";
			return result;
		}

		std::vector<VegetationSafeStrokeSegment> segments{};
		VegetationWorldMillimeterPoint previous = raw_points.front();
		VegetationWorldMillimeterPoint run_begin{};
		VegetationWorldMillimeterPoint run_direction{};
		uint64_t run_steps = 0;
		bool has_run = false;

		for (std::size_t index = 1; index < raw_points.size(); ++index)
		{
			const VegetationWorldMillimeterPoint& point = raw_points[index];
			int64_t delta_x = 0;
			int64_t delta_z = 0;
			if (!checked_subtract_int64(point.x, previous.x, delta_x) ||
				!checked_subtract_int64(point.z, previous.z, delta_z) ||
				absolute_int64(delta_x) > static_cast<uint64_t>(k_max_safe_axis_delta_mm) ||
				absolute_int64(delta_z) > static_cast<uint64_t>(k_max_safe_axis_delta_mm))
			{
				result.error = "stroke event delta exceeds the v1 safe axis bound";
				return result;
			}
			if (delta_x == 0 && delta_z == 0)
			{
				continue;
			}

			const uint64_t step_count = std::gcd(
				absolute_int64(delta_x), absolute_int64(delta_z));
			const VegetationWorldMillimeterPoint direction{
				delta_x / static_cast<int64_t>(step_count),
				delta_z / static_cast<int64_t>(step_count)
			};
			if (!has_run || direction != run_direction)
			{
				if (has_run && !append_canonical_run(
					segments, run_begin, previous, run_direction, run_steps))
				{
					result.error = "stroke run overflowed during canonical splitting";
					return result;
				}
				has_run = true;
				run_begin = previous;
				run_direction = direction;
				run_steps = step_count;
			}
			else
			{
				if (run_steps > std::numeric_limits<uint64_t>::max() - step_count)
				{
					result.error = "stroke primitive step count overflowed";
					return result;
				}
				run_steps += step_count;
			}
			previous = point;
		}

		if (has_run && !append_canonical_run(
			segments, run_begin, previous, run_direction, run_steps))
		{
			result.error = "stroke run overflowed during canonical splitting";
			return result;
		}

		result.succeeded = true;
		result.safe_segments = std::move(segments);
		return result;
	}

	VegetationStrokeResampleResult resample_vegetation_stroke(
		const std::vector<VegetationWorldMillimeterPoint>& raw_points,
		const uint32_t spacing_mm)
	{
		VegetationStrokeResampleResult result{};
		if (spacing_mm == 0 || spacing_mm > k_max_brush_spacing_mm)
		{
			result.error = "stroke spacing is outside the v1 range";
			return result;
		}

		const VegetationStrokeCanonicalizationResult canonical =
			canonicalize_vegetation_stroke(raw_points);
		if (!canonical.succeeded)
		{
			result.error = canonical.error;
			return result;
		}

		std::vector<VegetationWorldMillimeterPoint> dabs{};
		dabs.push_back(raw_points.front());
		uint64_t distance_since_dab = 0;
		uint64_t total_length = 0;
		for (const VegetationSafeStrokeSegment& segment : canonical.safe_segments)
		{
			int64_t delta_x = 0;
			int64_t delta_z = 0;
			if (!checked_subtract_int64(segment.end.x, segment.begin.x, delta_x) ||
				!checked_subtract_int64(segment.end.z, segment.begin.z, delta_z))
			{
				result.error = "canonical segment delta overflowed";
				return result;
			}
			const uint64_t absolute_x = absolute_int64(delta_x);
			const uint64_t absolute_z = absolute_int64(delta_z);
			const uint64_t square_sum = absolute_x * absolute_x + absolute_z * absolute_z;
			const uint64_t segment_length = floor_integer_square_root(square_sum);
			if (segment_length == 0 ||
				total_length > std::numeric_limits<uint64_t>::max() - segment_length)
			{
				result.error = "canonical stroke length overflowed";
				return result;
			}
			total_length += segment_length;

			uint64_t offset = distance_since_dab == 0
				? static_cast<uint64_t>(spacing_mm)
				: static_cast<uint64_t>(spacing_mm) - distance_since_dab;
			while (offset <= segment_length)
			{
				VegetationWorldMillimeterPoint dab{};
				if (!interpolate_segment_point(segment, offset, segment_length, dab))
				{
					result.error = "stroke interpolation overflowed";
					return result;
				}
				if (dab != dabs.back())
				{
					dabs.push_back(dab);
				}
				if (segment_length - offset < spacing_mm)
				{
					break;
				}
				offset += spacing_mm;
			}

			distance_since_dab = (distance_since_dab +
				(segment_length % spacing_mm)) % spacing_mm;
		}

		if (dabs.back() != raw_points.back())
		{
			dabs.push_back(raw_points.back());
		}
		result.succeeded = true;
		result.dabs = std::move(dabs);
		return result;
	}

	uint8_t vegetation_brush_amount(
		const uint64_t distance_mm,
		const uint32_t radius_mm,
		const uint8_t strength,
		const uint8_t falloff)
	{
		if (radius_mm < k_min_brush_radius_mm || radius_mm > k_max_brush_radius_mm ||
			strength == 0 || distance_mm >= radius_mm)
		{
			return 0;
		}

		const uint64_t inner = static_cast<uint64_t>(radius_mm) *
			(255u - static_cast<uint32_t>(falloff)) / 255u;
		uint64_t coverage = 65535;
		if (falloff != 0 && distance_mm > inner)
		{
			coverage = round_unsigned_ratio_ties_even(
				(static_cast<uint64_t>(radius_mm) - distance_mm) * 65535u,
				static_cast<uint64_t>(radius_mm) - inner);
		}
		const uint64_t amount = round_unsigned_ratio_ties_even(
			static_cast<uint64_t>(strength) * coverage, 65535u);
		return static_cast<uint8_t>(amount);
	}
}
