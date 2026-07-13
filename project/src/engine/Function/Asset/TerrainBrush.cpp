#include "Function/Asset/TerrainBrush.h"

#include <cmath>

namespace AshEngine
{
	namespace
	{
		auto fail(std::string* out_error, const char* message) -> bool
		{
			if (out_error != nullptr)
			{
				*out_error = message;
			}
			return false;
		}

		auto metric_distance_squared(
			const TerrainStrokeSample& lhs,
			const TerrainStrokeSample& rhs,
			const TerrainBrushMetric& metric) -> double
		{
			const double delta_x =
				(static_cast<double>(rhs.terrain_local_xz.x) - lhs.terrain_local_xz.x) *
				metric.world_meters_per_terrain_meter.x;
			const double delta_z =
				(static_cast<double>(rhs.terrain_local_xz.y) - lhs.terrain_local_xz.y) *
				metric.world_meters_per_terrain_meter.y;
			return delta_x * delta_x + delta_z * delta_z;
		}
	}

	bool resample_terrain_stroke(
		const std::vector<TerrainStrokeSample>& input,
		const TerrainBrushMetric& metric,
		float spacing_meters,
		std::vector<TerrainStrokeSample>& out_samples,
		std::string* out_error)
	{
		if (out_error != nullptr)
		{
			out_error->clear();
		}

		const glm::vec2 metric_axes = metric.world_meters_per_terrain_meter;
		if (!std::isfinite(spacing_meters) || spacing_meters <= 0.0f ||
			!std::isfinite(metric_axes.x) || metric_axes.x <= 0.0f ||
			!std::isfinite(metric_axes.y) || metric_axes.y <= 0.0f)
		{
			return fail(out_error, "Terrain stroke spacing and metric axes must be finite and positive.");
		}

		for (const TerrainStrokeSample& sample : input)
		{
			if (!std::isfinite(sample.terrain_local_xz.x) ||
				!std::isfinite(sample.terrain_local_xz.y) ||
				!std::isfinite(sample.pressure) ||
				sample.pressure < 0.0f || sample.pressure > 1.0f)
			{
				return fail(out_error, "Terrain stroke samples must be finite with pressure in [0,1].");
			}
		}

		try
		{
			constexpr double duplicate_distance_squared = 1.0e-12;
			std::vector<TerrainStrokeSample> samples{};
			samples.reserve(input.size());
			for (const TerrainStrokeSample& sample : input)
			{
				if (!samples.empty() &&
					metric_distance_squared(samples.back(), sample, metric) <= duplicate_distance_squared)
				{
					samples.back() = sample;
				}
				else
				{
					samples.push_back(sample);
				}
			}

			if (samples.size() <= 1u)
			{
				out_samples.swap(samples);
				return true;
			}

			std::vector<TerrainStrokeSample> resampled{};
			resampled.push_back(samples.front());
			const double spacing = static_cast<double>(spacing_meters);
			double accumulated_distance = 0.0;
			double next_emission_distance = spacing;

			for (size_t index = 1u; index < samples.size(); ++index)
			{
				const TerrainStrokeSample& segment_start = samples[index - 1u];
				const TerrainStrokeSample& segment_end = samples[index];
				const double segment_length = std::sqrt(
					metric_distance_squared(segment_start, segment_end, metric));
				const double segment_end_distance = accumulated_distance + segment_length;

				while (next_emission_distance <= segment_end_distance)
				{
					const double interpolation =
						(next_emission_distance - accumulated_distance) / segment_length;
					if (interpolation >= 1.0)
					{
						resampled.push_back(segment_end);
					}
					else
					{
						const float t = static_cast<float>(interpolation);
						TerrainStrokeSample emitted{};
						emitted.terrain_local_xz = segment_start.terrain_local_xz +
							(segment_end.terrain_local_xz - segment_start.terrain_local_xz) * t;
						emitted.pressure = segment_start.pressure +
							(segment_end.pressure - segment_start.pressure) * t;
						resampled.push_back(emitted);
					}
					next_emission_distance += spacing;
				}
				accumulated_distance = segment_end_distance;
			}

			const TerrainStrokeSample& final_sample = samples.back();
			if (metric_distance_squared(resampled.back(), final_sample, metric) <=
				duplicate_distance_squared)
			{
				resampled.back() = final_sample;
			}
			else
			{
				resampled.push_back(final_sample);
			}

			out_samples.swap(resampled);
			return true;
		}
		catch (...)
		{
			return fail(out_error, "Terrain stroke resampling could not allocate output storage.");
		}
	}
}
