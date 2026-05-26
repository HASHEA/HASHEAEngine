#include "Function/Render/EnvironmentMapBaker.h"

#include "Base/hcore.h"
#include "Base/hlog.h"
#include "Base/hmemory.h"
#include "Base/hprofiler.h"
#include "Function/Render/RenderFormatUtils.h"
#include "Function/Render/TextureAsset.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <glm/gtc/constants.hpp>
#include <iostream>
#include <vector>

namespace AshEngine
{
	namespace
	{
		struct Float4
		{
			float r = 0.0f;
			float g = 0.0f;
			float b = 0.0f;
			float a = 1.0f;
		};

		struct FloatCubemapFace
		{
			uint32_t width = 0;
			uint32_t height = 0;
			std::vector<glm::vec3> pixels{};
		};

		struct FloatCubemap
		{
			uint32_t size = 0;
			std::array<FloatCubemapFace, k_ashibl_face_count> faces{};
		};

		static auto set_bake_report(
			EnvironmentBakeReport* out_report,
			bool succeeded,
			const std::string& message,
			uint32_t radiance_faces = 0,
			uint32_t irradiance_faces = 0,
			uint32_t prefilter_mips = 0) -> void
		{
			if (!out_report)
			{
				return;
			}
			out_report->succeeded = succeeded;
			out_report->message = message;
			out_report->generated_radiance_faces = radiance_faces;
			out_report->generated_irradiance_faces = irradiance_faces;
			out_report->generated_prefilter_mips = prefilter_mips;
		}

		static auto clamp_power_of_two(uint32_t value, uint32_t minimum, uint32_t maximum, uint32_t fallback) -> uint32_t
		{
			if (value == 0u)
			{
				return std::clamp(fallback, minimum, maximum);
			}

			uint32_t power = 1u;
			while (power < value && power < maximum)
			{
				power <<= 1u;
			}
			if (power < minimum)
			{
				return minimum;
			}
			return std::min(power, maximum);
		}

		static auto max_mip_count_for_size(uint32_t size) -> uint32_t
		{
			uint32_t mip_count = 1u;
			while (size > 1u)
			{
				size >>= 1u;
				++mip_count;
			}
			return mip_count;
		}

		static auto apply_bake_overrides(
			EnvironmentLightingConfig& config,
			const EnvironmentBakeOverrides* overrides) -> void
		{
			if (!overrides)
			{
				return;
			}

			if (overrides->radiance_size > 0u)
			{
				config.default_radiance_size = clamp_power_of_two(
					overrides->radiance_size,
					1u,
					4096u,
					config.default_radiance_size);
			}
			if (overrides->irradiance_size > 0u)
			{
				config.default_irradiance_size = clamp_power_of_two(
					overrides->irradiance_size,
					1u,
					1024u,
					config.default_irradiance_size);
			}
			if (overrides->prefilter_size > 0u)
			{
				config.default_prefilter_size = clamp_power_of_two(
					overrides->prefilter_size,
					1u,
					2048u,
					config.default_prefilter_size);
			}
			if (overrides->brdf_lut_size > 0u)
			{
				config.default_brdf_lut_size = clamp_power_of_two(
					overrides->brdf_lut_size,
					1u,
					1024u,
					config.default_brdf_lut_size);
			}
			if (overrides->prefilter_mip_count > 0u)
			{
				config.default_prefilter_mip_count = std::clamp(
					overrides->prefilter_mip_count,
					1u,
					max_mip_count_for_size(config.default_prefilter_size));
			}
			else
			{
				config.default_prefilter_mip_count = std::min(
					config.default_prefilter_mip_count,
					max_mip_count_for_size(config.default_prefilter_size));
			}
			if (overrides->sample_count > 0u)
			{
				config.default_sample_count = std::clamp(overrides->sample_count, 1u, 4096u);
			}
		}

		static auto float_to_half_bits(float value) -> uint16_t
		{
			uint32_t bits = 0;
			std::memcpy(&bits, &value, sizeof(bits));

			const uint32_t sign = (bits >> 16u) & 0x8000u;
			int32_t exponent = static_cast<int32_t>((bits >> 23u) & 0xFFu) - 127 + 15;
			uint32_t mantissa = bits & 0x007FFFFFu;

			if (exponent <= 0)
			{
				if (exponent < -10)
				{
					return static_cast<uint16_t>(sign);
				}
				mantissa = (mantissa | 0x00800000u) >> static_cast<uint32_t>(1 - exponent);
				return static_cast<uint16_t>(sign | (mantissa >> 13u));
			}
			if (exponent >= 31)
			{
				return static_cast<uint16_t>(sign | 0x7C00u | (mantissa != 0u ? 0x0200u : 0u));
			}
			return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10u) | (mantissa >> 13u));
		}

		static auto write_rgba16_pixel(
			std::vector<uint8_t>& pixel_data,
			uint32_t row_pitch,
			uint32_t x,
			uint32_t y,
			const glm::vec3& rgb,
			float alpha = 1.0f) -> void
		{
			const size_t byte_offset = static_cast<size_t>(y) * row_pitch + static_cast<size_t>(x) * 8u;
			uint16_t* half_pixels = reinterpret_cast<uint16_t*>(pixel_data.data() + byte_offset);
			half_pixels[0] = float_to_half_bits(rgb.r);
			half_pixels[1] = float_to_half_bits(rgb.g);
			half_pixels[2] = float_to_half_bits(rgb.b);
			half_pixels[3] = float_to_half_bits(alpha);
		}

		static auto write_rg16_pixel(
			std::vector<uint8_t>& pixel_data,
			uint32_t row_pitch,
			uint32_t x,
			uint32_t y,
			const glm::vec2& value) -> void
		{
			const size_t byte_offset = static_cast<size_t>(y) * row_pitch + static_cast<size_t>(x) * 4u;
			uint16_t* half_pixels = reinterpret_cast<uint16_t*>(pixel_data.data() + byte_offset);
			half_pixels[0] = float_to_half_bits(value.x);
			half_pixels[1] = float_to_half_bits(value.y);
		}

		static auto radical_inverse_vdc(uint32_t bits) -> float
		{
			bits = (bits << 16u) | (bits >> 16u);
			bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
			bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
			bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
			bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
			return static_cast<float>(bits) * 2.3283064365386963e-10f;
		}

		static auto hammersley(uint32_t index, uint32_t count) -> glm::vec2
		{
			return glm::vec2(
				static_cast<float>(index) / static_cast<float>(count),
				radical_inverse_vdc(index));
		}

		static auto cube_direction(uint32_t face, float u, float v) -> glm::vec3
		{
			const float uc = 2.0f * u - 1.0f;
			const float vc = 2.0f * v - 1.0f;
			switch (face)
			{
			case 0: return glm::normalize(glm::vec3(1.0f, -vc, -uc));
			case 1: return glm::normalize(glm::vec3(-1.0f, -vc, uc));
			case 2: return glm::normalize(glm::vec3(uc, 1.0f, vc));
			case 3: return glm::normalize(glm::vec3(uc, -1.0f, -vc));
			case 4: return glm::normalize(glm::vec3(uc, -vc, 1.0f));
			case 5: return glm::normalize(glm::vec3(-uc, -vc, -1.0f));
			default: return glm::vec3(0.0f, 1.0f, 0.0f);
			}
		}

		static auto rgb_luminance(const glm::vec3& rgb) -> float
		{
			return glm::dot(rgb, glm::vec3(0.2126f, 0.7152f, 0.0722f));
		}

		static auto extract_dominant_light_metadata(const FloatCubemap& cubemap) -> EnvironmentDominantLightMetadata
		{
			EnvironmentDominantLightMetadata result{};
			result.source = "radiance_peak_cluster_v1";

			float max_luminance = 0.0f;
			glm::vec3 max_direction{ 0.0f, 1.0f, 0.0f };
			for (uint32_t face = 0; face < k_ashibl_face_count; ++face)
			{
				const FloatCubemapFace& face_data = cubemap.faces[face];
				if (face_data.width == 0u || face_data.height == 0u || face_data.pixels.empty())
				{
					continue;
				}

				for (uint32_t y = 0; y < face_data.height; ++y)
				{
					for (uint32_t x = 0; x < face_data.width; ++x)
					{
						const size_t pixel_index = static_cast<size_t>(y) * face_data.width + x;
						if (pixel_index >= face_data.pixels.size())
						{
							continue;
						}

						const float luminance = rgb_luminance(face_data.pixels[pixel_index]);
						if (luminance > max_luminance)
						{
							const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(face_data.width);
							const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(face_data.height);
							max_luminance = luminance;
							max_direction = cube_direction(face, u, v);
						}
					}
				}
			}

			if (max_luminance <= 0.0f)
			{
				return result;
			}

			const float threshold = max_luminance * 0.5f;
			glm::vec3 weighted_direction{ 0.0f };
			float total_weight = 0.0f;
			for (uint32_t face = 0; face < k_ashibl_face_count; ++face)
			{
				const FloatCubemapFace& face_data = cubemap.faces[face];
				if (face_data.width == 0u || face_data.height == 0u || face_data.pixels.empty())
				{
					continue;
				}

				for (uint32_t y = 0; y < face_data.height; ++y)
				{
					for (uint32_t x = 0; x < face_data.width; ++x)
					{
						const size_t pixel_index = static_cast<size_t>(y) * face_data.width + x;
						if (pixel_index >= face_data.pixels.size())
						{
							continue;
						}

						const float luminance = rgb_luminance(face_data.pixels[pixel_index]);
						if (luminance < threshold)
						{
							continue;
						}

						const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(face_data.width);
						const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(face_data.height);
						weighted_direction += cube_direction(face, u, v) * luminance;
						total_weight += luminance;
					}
				}
			}

			const glm::vec3 direction =
				total_weight > 0.0f && glm::length(weighted_direction) > 0.0001f ?
				glm::normalize(weighted_direction) :
				max_direction;
			result.valid = true;
			result.direction = direction;
			result.azimuth_degrees = std::atan2(direction.z, direction.x) * 180.0f / glm::pi<float>();
			result.elevation_degrees = std::asin(glm::clamp(direction.y, -1.0f, 1.0f)) * 180.0f / glm::pi<float>();
			result.luminance = max_luminance;
			return result;
		}

		static auto direction_to_equirect_uv(const glm::vec3& direction) -> glm::vec2
		{
			const glm::vec3 normal = glm::normalize(direction);
			const float phi = std::acos(glm::clamp(normal.y, -1.0f, 1.0f));
			const float theta = std::atan2(normal.z, normal.x);
			return glm::vec2(
				theta * (0.5f / glm::pi<float>()) + 0.5f,
				phi * (1.0f / glm::pi<float>()));
		}

		static auto direction_to_cube_face_uv(const glm::vec3& direction, uint32_t& out_face, glm::vec2& out_uv) -> bool
		{
			const glm::vec3 dir = glm::normalize(direction);
			const glm::vec3 abs_dir = glm::abs(dir);
			const float max_axis = glm::max(glm::max(abs_dir.x, abs_dir.y), abs_dir.z);
			if (max_axis <= 0.0f)
			{
				return false;
			}

			float uc = 0.0f;
			float vc = 0.0f;
			if (abs_dir.x >= abs_dir.y && abs_dir.x >= abs_dir.z)
			{
				out_face = dir.x > 0.0f ? 0u : 1u;
				const float inv = 1.0f / abs_dir.x;
				uc = dir.x > 0.0f ? -dir.z * inv : dir.z * inv;
				vc = -dir.y * inv;
			}
			else if (abs_dir.y >= abs_dir.x && abs_dir.y >= abs_dir.z)
			{
				out_face = dir.y > 0.0f ? 2u : 3u;
				const float inv = 1.0f / abs_dir.y;
				uc = dir.x * inv;
				vc = dir.y > 0.0f ? dir.z * inv : -dir.z * inv;
			}
			else
			{
				out_face = dir.z > 0.0f ? 4u : 5u;
				const float inv = 1.0f / abs_dir.z;
				uc = dir.z > 0.0f ? dir.x * inv : -dir.x * inv;
				vc = -dir.y * inv;
			}

			out_uv = glm::vec2((uc + 1.0f) * 0.5f, (vc + 1.0f) * 0.5f);
			return true;
		}

		static auto sample_equirect_bilinear(const TextureSourceData& source, const glm::vec2& uv) -> Float4
		{
			Float4 result{};
			if (source.width == 0 || source.height == 0 || source.pixel_data.empty())
			{
				return result;
			}

			const float u = glm::clamp(uv.x, 0.0f, 1.0f) * static_cast<float>(source.width - 1u);
			const float v = glm::clamp(uv.y, 0.0f, 1.0f) * static_cast<float>(source.height - 1u);
			const int x0 = static_cast<int>(std::floor(u));
			const int y0 = static_cast<int>(std::floor(v));
			const int x1 = std::min(x0 + 1, static_cast<int>(source.width) - 1);
			const int y1 = std::min(y0 + 1, static_cast<int>(source.height) - 1);
			const float fx = u - static_cast<float>(x0);
			const float fy = v - static_cast<float>(y0);

			const auto sample_texel = [&](int x, int y) -> Float4
			{
				const size_t offset = (static_cast<size_t>(y) * static_cast<size_t>(source.row_pitch)) +
					(static_cast<size_t>(x) * 4u * sizeof(float));
				if (offset + (4u * sizeof(float)) > source.pixel_data.size())
				{
					return {};
				}
				const float* rgba = reinterpret_cast<const float*>(source.pixel_data.data() + offset);
				return Float4{ rgba[0], rgba[1], rgba[2], rgba[3] };
			};

			const Float4 c00 = sample_texel(x0, y0);
			const Float4 c10 = sample_texel(x1, y0);
			const Float4 c01 = sample_texel(x0, y1);
			const Float4 c11 = sample_texel(x1, y1);

			const auto lerp4 = [](const Float4& a, const Float4& b, float t) -> Float4
			{
				return Float4{
					a.r + (b.r - a.r) * t,
					a.g + (b.g - a.g) * t,
					a.b + (b.b - a.b) * t,
					a.a + (b.a - a.a) * t
				};
			};

			const Float4 c0 = lerp4(c00, c10, fx);
			const Float4 c1 = lerp4(c01, c11, fx);
			return lerp4(c0, c1, fy);
		}

		static auto sample_equirect_rgb(const TextureSourceData& source, const glm::vec3& direction) -> glm::vec3
		{
			const Float4 color = sample_equirect_bilinear(source, direction_to_equirect_uv(direction));
			return glm::vec3(color.r, color.g, color.b);
		}

		static auto sample_float_cubemap_bilinear(
			const FloatCubemap& cubemap,
			const glm::vec3& direction) -> glm::vec3
		{
			uint32_t face = 0;
			glm::vec2 uv{};
			if (!direction_to_cube_face_uv(direction, face, uv))
			{
				return glm::vec3(0.0f);
			}

			const FloatCubemapFace& face_data = cubemap.faces[face];
			if (face_data.width == 0 || face_data.height == 0 || face_data.pixels.empty())
			{
				return glm::vec3(0.0f);
			}

			const float u = glm::clamp(uv.x, 0.0f, 1.0f) * static_cast<float>(face_data.width - 1u);
			const float v = glm::clamp(uv.y, 0.0f, 1.0f) * static_cast<float>(face_data.height - 1u);
			const int x0 = static_cast<int>(std::floor(u));
			const int y0 = static_cast<int>(std::floor(v));
			const int x1 = std::min(x0 + 1, static_cast<int>(face_data.width) - 1);
			const int y1 = std::min(y0 + 1, static_cast<int>(face_data.height) - 1);
			const float fx = u - static_cast<float>(x0);
			const float fy = v - static_cast<float>(y0);

			const auto sample_texel = [&](int x, int y) -> glm::vec3
			{
				return face_data.pixels[static_cast<size_t>(y) * face_data.width + static_cast<size_t>(x)];
			};

			const glm::vec3 c00 = sample_texel(x0, y0);
			const glm::vec3 c10 = sample_texel(x1, y0);
			const glm::vec3 c01 = sample_texel(x0, y1);
			const glm::vec3 c11 = sample_texel(x1, y1);
			const glm::vec3 c0 = glm::mix(c00, c10, fx);
			const glm::vec3 c1 = glm::mix(c01, c11, fx);
			return glm::mix(c0, c1, fy);
		}

		static auto build_orthonormal_basis(const glm::vec3& normal, glm::vec3& out_tangent, glm::vec3& out_bitangent) -> void
		{
			const glm::vec3 up = std::abs(normal.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
			out_tangent = glm::normalize(glm::cross(up, normal));
			out_bitangent = glm::cross(normal, out_tangent);
		}

		static auto tangent_to_world(const glm::vec3& local, const glm::vec3& normal) -> glm::vec3
		{
			glm::vec3 tangent{};
			glm::vec3 bitangent{};
			build_orthonormal_basis(normal, tangent, bitangent);
			return tangent * local.x + bitangent * local.y + normal * local.z;
		}

		static auto cosine_sample_hemisphere(const glm::vec2& xi) -> glm::vec3
		{
			const float radius = std::sqrt(xi.x);
			const float theta = glm::two_pi<float>() * xi.y;
			return glm::vec3(radius * std::cos(theta), radius * std::sin(theta), std::sqrt(glm::max(0.0f, 1.0f - xi.x)));
		}

		static auto distribution_ggx(float n_dot_h, float roughness) -> float
		{
			const float alpha = roughness * roughness;
			const float alpha2 = alpha * alpha;
			const float denom = n_dot_h * n_dot_h * (alpha2 - 1.0f) + 1.0f;
			return alpha2 / (glm::pi<float>() * glm::max(denom * denom, 1.0e-8f));
		}

		static auto geometry_schlick_ggx(float n_dot_v, float roughness) -> float
		{
			const float k = (roughness * roughness) * 0.5f;
			return n_dot_v / (n_dot_v * (1.0f - k) + k);
		}

		static auto geometry_smith(float n_dot_v, float n_dot_l, float roughness) -> float
		{
			return geometry_schlick_ggx(n_dot_v, roughness) * geometry_schlick_ggx(n_dot_l, roughness);
		}

		static auto importance_sample_ggx(const glm::vec2& xi, const glm::vec3& normal, float roughness) -> glm::vec3
		{
			const float alpha = roughness * roughness;
			const float phi = glm::two_pi<float>() * xi.x;
			const float cos_theta = std::sqrt((1.0f - xi.y) / (1.0f + (alpha * alpha - 1.0f) * xi.y));
			const float sin_theta = std::sqrt(glm::max(0.0f, 1.0f - cos_theta * cos_theta));
			const glm::vec3 local_h{
				std::cos(phi) * sin_theta,
				std::sin(phi) * sin_theta,
				cos_theta
			};

			glm::vec3 tangent{};
			glm::vec3 bitangent{};
			build_orthonormal_basis(normal, tangent, bitangent);
			return glm::normalize(tangent * local_h.x + bitangent * local_h.y + normal * local_h.z);
		}

		static auto allocate_cube_payload(
			TextureCubePayload& payload,
			uint32_t size,
			uint32_t mip_count,
			RenderTextureFormat format) -> void
		{
			payload.format = format;
			payload.width = size;
			payload.height = size;
			payload.mip_count = mip_count;
			payload.subresources.clear();
			payload.subresources.reserve(mip_count * k_ashibl_face_count);

			for (uint32_t mip = 0; mip < mip_count; ++mip)
			{
				const uint32_t mip_width = std::max(1u, size >> mip);
				const uint32_t mip_height = std::max(1u, size >> mip);
				const uint32_t row_pitch = calculate_render_texture_tight_row_pitch(format, mip_width);
				for (uint32_t face = 0; face < k_ashibl_face_count; ++face)
				{
					TextureSubresourcePayload subresource{};
					subresource.mip_level = mip;
					subresource.array_layer = face;
					subresource.width = mip_width;
					subresource.height = mip_height;
					subresource.row_pitch = row_pitch;
					subresource.pixel_data.assign(static_cast<size_t>(row_pitch) * mip_height, 0u);
					payload.subresources.push_back(std::move(subresource));
				}
			}
		}

		static auto find_cube_subresource(
			TextureCubePayload& payload,
			uint32_t mip,
			uint32_t face) -> TextureSubresourcePayload*
		{
			for (TextureSubresourcePayload& subresource : payload.subresources)
			{
				if (subresource.mip_level == mip && subresource.array_layer == face)
				{
					return &subresource;
				}
			}
			return nullptr;
		}

		static auto bake_equirect_to_radiance_cubemap(
			const TextureSourceData& source,
			uint32_t radiance_size,
			FloatCubemap& out_float_cubemap,
			TextureCubePayload& out_radiance) -> bool
		{
			ASH_PROFILE_SCOPE_N("EnvironmentMapBaker::EquirectToCube");

			out_float_cubemap.size = radiance_size;
			out_radiance = {};
			allocate_cube_payload(out_radiance, radiance_size, 1u, RenderTextureFormat::RGBA16_SFLOAT);

			for (uint32_t face = 0; face < k_ashibl_face_count; ++face)
			{
				FloatCubemapFace& float_face = out_float_cubemap.faces[face];
				float_face.width = radiance_size;
				float_face.height = radiance_size;
				float_face.pixels.resize(static_cast<size_t>(radiance_size) * radiance_size);

				TextureSubresourcePayload* subresource = find_cube_subresource(out_radiance, 0u, face);
				if (!subresource)
				{
					return false;
				}

				for (uint32_t y = 0; y < radiance_size; ++y)
				{
					for (uint32_t x = 0; x < radiance_size; ++x)
					{
						const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(radiance_size);
						const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(radiance_size);
						const glm::vec3 direction = cube_direction(face, u, v);
						const glm::vec3 rgb = sample_equirect_rgb(source, direction);
						float_face.pixels[static_cast<size_t>(y) * radiance_size + x] = rgb;
						write_rgba16_pixel(subresource->pixel_data, subresource->row_pitch, x, y, rgb);
					}
				}
			}

			return true;
		}

		static auto bake_irradiance_cubemap(
			const TextureSourceData& source,
			uint32_t irradiance_size,
			uint32_t sample_count,
			TextureCubePayload& out_irradiance) -> bool
		{
			ASH_PROFILE_SCOPE_N("EnvironmentMapBaker::IrradianceConvolution");

			out_irradiance = {};
			allocate_cube_payload(out_irradiance, irradiance_size, 1u, RenderTextureFormat::RGBA16_SFLOAT);

			for (uint32_t face = 0; face < k_ashibl_face_count; ++face)
			{
				TextureSubresourcePayload* subresource = find_cube_subresource(out_irradiance, 0u, face);
				if (!subresource)
				{
					return false;
				}

				for (uint32_t y = 0; y < irradiance_size; ++y)
				{
					for (uint32_t x = 0; x < irradiance_size; ++x)
					{
						const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(irradiance_size);
						const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(irradiance_size);
						const glm::vec3 normal = cube_direction(face, u, v);

						glm::vec3 irradiance(0.0f);
						for (uint32_t sample_index = 0; sample_index < sample_count; ++sample_index)
						{
							const glm::vec2 xi = hammersley(sample_index, sample_count);
							const glm::vec3 sample_dir = tangent_to_world(cosine_sample_hemisphere(xi), normal);
							irradiance += sample_equirect_rgb(source, sample_dir);
						}

						const glm::vec3 result = irradiance * (glm::pi<float>() / static_cast<float>(sample_count));
						write_rgba16_pixel(subresource->pixel_data, subresource->row_pitch, x, y, result);
					}
				}
			}

			return true;
		}

		static auto bake_prefiltered_specular_cubemap(
			const FloatCubemap& radiance_cubemap,
			uint32_t prefilter_size,
			uint32_t prefilter_mip_count,
			uint32_t sample_count,
			TextureCubePayload& out_prefilter) -> bool
		{
			ASH_PROFILE_SCOPE_N("EnvironmentMapBaker::SpecularPrefilter");

			out_prefilter = {};
			allocate_cube_payload(out_prefilter, prefilter_size, prefilter_mip_count, RenderTextureFormat::RGBA16_SFLOAT);

			for (uint32_t mip = 0; mip < prefilter_mip_count; ++mip)
			{
				const uint32_t mip_width = std::max(1u, prefilter_size >> mip);
				const uint32_t mip_height = std::max(1u, prefilter_size >> mip);
				const float roughness = prefilter_mip_count <= 1 ?
					0.0f :
					static_cast<float>(mip) / static_cast<float>(prefilter_mip_count - 1u);
				HLogInfo(
					"EnvironmentMapBaker: prefilter mip {}/{} ({}x{}, roughness={}).",
					mip + 1u,
					prefilter_mip_count,
					mip_width,
					mip_height,
					roughness);

				for (uint32_t face = 0; face < k_ashibl_face_count; ++face)
				{
					TextureSubresourcePayload* subresource = find_cube_subresource(out_prefilter, mip, face);
					if (!subresource)
					{
						return false;
					}

					for (uint32_t y = 0; y < mip_height; ++y)
					{
						for (uint32_t x = 0; x < mip_width; ++x)
						{
							const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(mip_width);
							const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(mip_height);
							const glm::vec3 normal = cube_direction(face, u, v);
							const glm::vec3 view = normal;
							const glm::vec3 reflection = view;
							if (roughness <= 0.0001f)
							{
								write_rgba16_pixel(
									subresource->pixel_data,
									subresource->row_pitch,
									x,
									y,
									sample_float_cubemap_bilinear(radiance_cubemap, reflection));
								continue;
							}

							glm::vec3 prefiltered(0.0f);
							float weight_sum = 0.0f;
							for (uint32_t sample_index = 0; sample_index < sample_count; ++sample_index)
							{
								const glm::vec2 xi = hammersley(sample_index, sample_count);
								const glm::vec3 half_vector = importance_sample_ggx(xi, normal, roughness);
								const glm::vec3 sample_dir = glm::normalize(2.0f * glm::dot(view, half_vector) * half_vector - view);
								const float n_dot_l = glm::max(0.0f, glm::dot(normal, sample_dir));
								if (n_dot_l > 0.0f)
								{
									const float d = distribution_ggx(glm::max(0.0f, glm::dot(normal, half_vector)), roughness);
									const float n_dot_h = glm::max(0.0f, glm::dot(normal, half_vector));
									const float h_dot_v = glm::max(0.0f, glm::dot(half_vector, view));
									const float pdf = (d * n_dot_h / (4.0f * h_dot_v)) + 0.0001f;
									const float sample_weight = n_dot_l / pdf;
									prefiltered += sample_float_cubemap_bilinear(radiance_cubemap, sample_dir) * sample_weight;
									weight_sum += sample_weight;
								}
							}

							const glm::vec3 result = weight_sum > 0.0f ?
								prefiltered / weight_sum :
								sample_float_cubemap_bilinear(radiance_cubemap, reflection);
							write_rgba16_pixel(subresource->pixel_data, subresource->row_pitch, x, y, result);
						}
					}
				}
			}

			return true;
		}

		static auto bake_brdf_lut(uint32_t lut_size, uint32_t sample_count, Texture2DPayload& out_brdf_lut) -> bool
		{
			ASH_PROFILE_SCOPE_N("EnvironmentMapBaker::BRDFLUT");

			out_brdf_lut = {};
			out_brdf_lut.format = RenderTextureFormat::RG16_SFLOAT;
			out_brdf_lut.width = lut_size;
			out_brdf_lut.height = lut_size;
			out_brdf_lut.row_pitch = calculate_render_texture_tight_row_pitch(out_brdf_lut.format, lut_size);
			out_brdf_lut.pixel_data.assign(static_cast<size_t>(out_brdf_lut.row_pitch) * lut_size, 0u);

			for (uint32_t y = 0; y < lut_size; ++y)
			{
				const float roughness = (static_cast<float>(y) + 0.5f) / static_cast<float>(lut_size);
				for (uint32_t x = 0; x < lut_size; ++x)
				{
					const float n_dot_v = (static_cast<float>(x) + 0.5f) / static_cast<float>(lut_size);
					const glm::vec3 view{
						std::sqrt(glm::max(0.0f, 1.0f - n_dot_v * n_dot_v)),
						0.0f,
						n_dot_v
					};
					const glm::vec3 normal(0.0f, 0.0f, 1.0f);

					float scale = 0.0f;
					float bias = 0.0f;
					for (uint32_t sample_index = 0; sample_index < sample_count; ++sample_index)
					{
						const glm::vec2 xi = hammersley(sample_index, sample_count);
						const glm::vec3 half_vector = importance_sample_ggx(xi, normal, roughness);
						const glm::vec3 light_dir = glm::normalize(2.0f * glm::dot(view, half_vector) * half_vector - view);

						const float n_dot_l = glm::max(0.0f, light_dir.z);
						const float n_dot_h = glm::max(0.0f, half_vector.z);
						const float v_dot_h = glm::max(0.0f, glm::dot(view, half_vector));
						if (n_dot_l > 0.0f)
						{
							const float geometry = geometry_smith(n_dot_v, n_dot_l, roughness);
							const float geometry_visibility = (geometry * v_dot_h) / (n_dot_h * n_dot_v + 0.0001f);
							const float fresnel = std::pow(1.0f - v_dot_h, 5.0f);
							scale += (1.0f - fresnel) * geometry_visibility;
							bias += fresnel * geometry_visibility;
						}
					}

					const float inv_sample_count = 1.0f / static_cast<float>(sample_count);
					write_rg16_pixel(
						out_brdf_lut.pixel_data,
						out_brdf_lut.row_pitch,
						x,
						y,
						glm::vec2(scale * inv_sample_count, bias * inv_sample_count));
				}
			}

			return true;
		}
	}

	bool EnvironmentMapBaker::bake_to_cooked_data(
		const EnvironmentMapBuildDesc& desc,
		EnvironmentMapCookedData& out_data,
		EnvironmentBakeReport* out_report)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);

		out_data = {};
		out_data.build_desc = desc;

		if (desc.source_texture_path.empty())
		{
			set_bake_report(out_report, false, "Environment bake source texture path is empty.");
			ASH_PROCESS_ERROR(false);
		}

		const std::filesystem::path source_path = desc.source_texture_path;
		out_data.source_content_hash = hash_environment_source_file(source_path);

		TextureSourceData source{};
		std::string decode_error{};
		if (!decode_texture_source_from_file(source_path, TextureColorSpace::Linear, source, &decode_error))
		{
			set_bake_report(out_report, false, decode_error.empty() ? "Failed to decode source texture." : decode_error);
			ASH_PROCESS_ERROR(false);
		}
		if (!source.is_hdr)
		{
			set_bake_report(out_report, false, "Environment bake source must be an HDR equirectangular texture.");
			ASH_PROCESS_ERROR(false);
		}
		HLogInfo(
			"EnvironmentMapBaker: decoded HDR source '{}' ({}x{}, row_pitch={}, bytes={}).",
			desc.source_texture_path,
			source.width,
			source.height,
			source.row_pitch,
			source.pixel_data.size());

		const uint32_t radiance_size = std::max(1u, desc.radiance_size);
		const uint32_t irradiance_size = std::max(1u, desc.irradiance_size);
		const uint32_t prefilter_size = std::max(1u, desc.prefilter_size);
		const uint32_t prefilter_mip_count = std::max(1u, desc.prefilter_mip_count);
		const uint32_t brdf_lut_size = std::max(1u, desc.brdf_lut_size);
		const uint32_t sample_count = std::max(1u, desc.sample_count);

		HLogInfo("EnvironmentMapBaker: generating radiance cubemap (size={}).", radiance_size);
		FloatCubemap radiance_float_cubemap{};
		if (!bake_equirect_to_radiance_cubemap(source, radiance_size, radiance_float_cubemap, out_data.radiance))
		{
			set_bake_report(out_report, false, "Failed to generate radiance cubemap.");
			ASH_PROCESS_ERROR(false);
		}
		out_data.dominant_light = extract_dominant_light_metadata(radiance_float_cubemap);
		if (out_data.dominant_light.valid)
		{
			HLogInfo(
				"EnvironmentMapBaker: dominant light direction=({}, {}, {}) azimuth={} elevation={} luminance={}.",
				out_data.dominant_light.direction.x,
				out_data.dominant_light.direction.y,
				out_data.dominant_light.direction.z,
				out_data.dominant_light.azimuth_degrees,
				out_data.dominant_light.elevation_degrees,
				out_data.dominant_light.luminance);
		}

		HLogInfo("EnvironmentMapBaker: generating irradiance cubemap (size={}, samples={}).", irradiance_size, sample_count);
		if (!bake_irradiance_cubemap(source, irradiance_size, sample_count, out_data.irradiance))
		{
			set_bake_report(out_report, false, "Failed to generate irradiance cubemap.");
			ASH_PROCESS_ERROR(false);
		}

		HLogInfo("EnvironmentMapBaker: generating prefiltered specular cubemap (size={}, mips={}, samples={}).", prefilter_size, prefilter_mip_count, sample_count);
		if (!bake_prefiltered_specular_cubemap(
			radiance_float_cubemap,
			prefilter_size,
			prefilter_mip_count,
			sample_count,
			out_data.prefiltered_specular))
		{
			set_bake_report(out_report, false, "Failed to generate prefiltered specular cubemap.");
			ASH_PROCESS_ERROR(false);
		}

		HLogInfo("EnvironmentMapBaker: generating BRDF LUT (size={}, samples={}).", brdf_lut_size, sample_count);
		if (!bake_brdf_lut(brdf_lut_size, sample_count, out_data.brdf_lut))
		{
			set_bake_report(out_report, false, "Failed to generate BRDF LUT.");
			ASH_PROCESS_ERROR(false);
		}

		std::string validation_error{};
		if (!validate_environment_map_cooked_data(out_data, &validation_error))
		{
			set_bake_report(out_report, false, validation_error.empty() ? "Baked environment data failed validation." : validation_error);
			ASH_PROCESS_ERROR(false);
		}

		set_bake_report(
			out_report,
			true,
			{},
			k_ashibl_face_count,
			k_ashibl_face_count,
			prefilter_mip_count);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool EnvironmentMapBaker::write_ashibl(
		const EnvironmentMapCookedData& data,
		const std::filesystem::path& output_path,
		EnvironmentBakeReport* out_report)
	{
		std::string error{};
		const bool succeeded = write_ashibl_file(output_path, data, &error);
		if (out_report)
		{
			out_report->succeeded = succeeded;
			out_report->message = succeeded ? std::string{} : error;
		}
		return succeeded;
	}

	bool EnvironmentMapBaker::read_ashibl(
		const std::filesystem::path& input_path,
		EnvironmentMapCookedData& out_data,
		EnvironmentBakeReport* out_report)
	{
		std::string error{};
		const bool succeeded = read_ashibl_file(input_path, out_data, &error);
		if (out_report)
		{
			out_report->succeeded = succeeded;
			out_report->message = succeeded ? std::string{} : error;
		}
		return succeeded;
	}

	int32_t bake_ashibl_file_from_runtime_config(
		const char* source_texture_path,
		const char* output_path,
		const char* config_path,
		EnvironmentBakeReport* out_report)
	{
		return bake_ashibl_file_from_runtime_config(
			source_texture_path,
			output_path,
			config_path,
			static_cast<const EnvironmentBakeOverrides*>(nullptr),
			out_report);
	}

	int32_t bake_ashibl_file_from_runtime_config(
		const char* source_texture_path,
		const char* output_path,
		const char* config_path,
		const EnvironmentBakeOverrides* overrides,
		EnvironmentBakeReport* out_report)
	{
		if (!source_texture_path || !source_texture_path[0] || !output_path || !output_path[0])
		{
			set_bake_report(out_report, false, "Source HDR path and output AshIBL path are required.");
			return 1;
		}

		LogService::instance()->init(nullptr);
		MemoryService::instance()->init(nullptr);

		EnvironmentBakeReport report{};
		bool succeeded = false;
		try
		{
			do
			{
				EnvironmentLightingConfig config =
					load_runtime_environment_lighting_config(config_path ? config_path : "product/config/Engine.ini");
				apply_bake_overrides(config, overrides);
				set_runtime_environment_lighting_config(config);
				HLogInfo(
					"AshIBL bake config resolved. radiance_size={} irradiance_size={} prefilter_size={} prefilter_mips={} brdf_lut_size={} sample_count={}.",
					config.default_radiance_size,
					config.default_irradiance_size,
					config.default_prefilter_size,
					config.default_prefilter_mip_count,
					config.default_brdf_lut_size,
					config.default_sample_count);

				EnvironmentMapBuildDesc desc =
					make_environment_map_build_desc_from_runtime_config(source_texture_path);
				EnvironmentMapCookedData cooked{};
				if (!EnvironmentMapBaker::bake_to_cooked_data(desc, cooked, &report))
				{
					break;
				}

				const std::filesystem::path resolved_output_path = output_path;
				if (const std::filesystem::path parent_path = resolved_output_path.parent_path(); !parent_path.empty())
				{
					std::filesystem::create_directories(parent_path);
				}

				HLogInfo("EnvironmentMapBaker: writing AshIBL '{}'.", resolved_output_path.string());
				succeeded = EnvironmentMapBaker::write_ashibl(cooked, resolved_output_path, &report);
			} while (false);
		}
		catch (const std::exception& exception)
		{
			set_bake_report(&report, false, exception.what());
			HLogError("EnvironmentMapBaker: bake command failed with exception: {}", exception.what());
		}
		catch (...)
		{
			set_bake_report(&report, false, "Unknown exception during AshIBL bake.");
			HLogError("EnvironmentMapBaker: bake command failed with an unknown exception.");
		}

		if (out_report)
		{
			*out_report = report;
		}

		MemoryService::instance()->shutdown();
		LogService::instance()->shutdown();
		return succeeded ? 0 : 1;
	}
}
