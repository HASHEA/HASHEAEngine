#include "Function/Render/EnvironmentMapAsset.h"

#include "Base/IniConfig.h"
#include "Base/hlog.h"
#include "Function/Render/RenderFormatUtils.h"
#include "wyhash.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <json.hpp>
#include <limits>
#include <mutex>

namespace AshEngine
{
	namespace
	{
		using json = nlohmann::json;

		struct AshIBLHeader
		{
			char magic[8];
			uint32_t version = 0;
			uint32_t flags = 0;
			uint32_t payload_count = 0;
			uint32_t reserved0 = 0;
			uint64_t metadata_offset = 0;
			uint64_t metadata_size = 0;
			uint64_t payload_table_offset = 0;
			uint64_t payload_table_size = 0;
		};

		struct AshIBLPayloadDesc
		{
			AshIBLPayloadKind kind = AshIBLPayloadKind::RadianceCubemap;
			AshIBLCompression compression = AshIBLCompression::None;
			RenderTextureFormat format = RenderTextureFormat::RGBA16_SFLOAT;
			uint32_t width = 0;
			uint32_t height = 0;
			uint32_t face_count = 0;
			uint32_t mip_count = 0;
			uint64_t byte_offset = 0;
			uint64_t byte_size = 0;
			uint64_t uncompressed_size = 0;
		};

		static constexpr uint64_t k_ashibl_alignment = 16u;

		static auto align_up(uint64_t value, uint64_t alignment) -> uint64_t
		{
			return (value + alignment - 1u) & ~(alignment - 1u);
		}

		static auto make_error(std::string* out_error, std::string message) -> bool
		{
			if (out_error)
			{
				*out_error = std::move(message);
			}
			return false;
		}

		static auto try_get_ini_uint32(const IniConfig& ini_config, const char* section, const char* key, uint32_t& out_value) -> bool
		{
			if (!ini_config.has_value(section, key))
			{
				return false;
			}

			const std::string text = trim_ini_string(ini_config.get_string(section, key, ""));
			if (text.empty())
			{
				return false;
			}

			char* parse_end = nullptr;
			const unsigned long parsed = std::strtoul(text.c_str(), &parse_end, 10);
			if (parse_end == text.c_str() || *parse_end != '\0')
			{
				return false;
			}

			out_value = static_cast<uint32_t>(parsed);
			return true;
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

		static auto runtime_environment_lighting_config_storage() -> EnvironmentLightingConfig&
		{
			static EnvironmentLightingConfig config = make_default_environment_lighting_config();
			return config;
		}

		static auto runtime_environment_lighting_config_mutex() -> std::mutex&
		{
			static std::mutex mutex;
			return mutex;
		}

		static auto payload_format_is_valid(RenderTextureFormat format, bool is_brdf_lut) -> bool
		{
			if (format == RenderTextureFormat::RGBA16_SFLOAT)
			{
				return true;
			}
			return is_brdf_lut && format == RenderTextureFormat::RG16_SFLOAT;
		}

		static auto validate_cube_payload(
			const TextureCubePayload& payload,
			const char* name,
			std::string* out_error) -> bool
		{
			if (payload.width == 0 || payload.height == 0)
			{
				return make_error(out_error, std::string(name) + " cubemap width/height must be non-zero.");
			}
			if (payload.mip_count == 0)
			{
				return make_error(out_error, std::string(name) + " cubemap mip_count must be non-zero.");
			}
			if (payload.subresources.size() != payload.mip_count * k_ashibl_face_count)
			{
				return make_error(out_error, std::string(name) + " cubemap subresource count mismatch.");
			}
			if (!payload_format_is_valid(payload.format, false))
			{
				return make_error(out_error, std::string(name) + " cubemap format is unsupported.");
			}

			const uint32_t tight_row_pitch = calculate_render_texture_tight_row_pitch(payload.format, payload.width);
			for (const TextureSubresourcePayload& subresource : payload.subresources)
			{
				const uint32_t mip_width = std::max(1u, payload.width >> subresource.mip_level);
				const uint32_t mip_height = std::max(1u, payload.height >> subresource.mip_level);
				const uint32_t mip_tight_row_pitch = calculate_render_texture_tight_row_pitch(payload.format, mip_width);
				if (subresource.width != mip_width || subresource.height != mip_height)
				{
					return make_error(out_error, std::string(name) + " cubemap subresource dimensions are invalid.");
				}
				if (subresource.array_layer >= k_ashibl_face_count)
				{
					return make_error(out_error, std::string(name) + " cubemap face index is out of range.");
				}
				if (subresource.row_pitch < mip_tight_row_pitch)
				{
					return make_error(out_error, std::string(name) + " cubemap row pitch is too small.");
				}
				if (subresource.pixel_data.size() < static_cast<size_t>(subresource.row_pitch) * mip_height)
				{
					return make_error(out_error, std::string(name) + " cubemap pixel payload is too small.");
				}
			}
			return true;
		}

		static auto build_metadata_json(const EnvironmentMapCookedData& data) -> json
		{
			json metadata = json{
				{ "asset_version", k_ashibl_version },
				{ "source_texture_path", data.build_desc.source_texture_path },
				{ "source_content_hash", data.source_content_hash },
				{ "build_settings", {
					{ "radiance_size", data.build_desc.radiance_size },
					{ "irradiance_size", data.build_desc.irradiance_size },
					{ "prefilter_size", data.build_desc.prefilter_size },
					{ "prefilter_mip_count", data.build_desc.prefilter_mip_count },
					{ "brdf_lut_size", data.build_desc.brdf_lut_size },
					{ "sample_count", data.build_desc.sample_count },
				}},
				{ "generated_timestamp", 0 },
				{ "baker_version", "cpu_reference_v1" },
				{ "radiance", {
					{ "width", data.radiance.width },
					{ "height", data.radiance.height },
					{ "mip_count", data.radiance.mip_count },
				}},
				{ "irradiance", {
					{ "width", data.irradiance.width },
					{ "height", data.irradiance.height },
					{ "mip_count", data.irradiance.mip_count },
				}},
				{ "prefilter", {
					{ "width", data.prefiltered_specular.width },
					{ "height", data.prefiltered_specular.height },
					{ "mip_count", data.prefiltered_specular.mip_count },
				}},
				{ "brdf_lut", {
					{ "width", data.brdf_lut.width },
					{ "height", data.brdf_lut.height },
				}},
			};
			if (data.dominant_light.valid)
			{
				metadata["dominant_light"] =
				{
					{ "direction", {
						data.dominant_light.direction.x,
						data.dominant_light.direction.y,
						data.dominant_light.direction.z,
					}},
					{ "azimuth_degrees", data.dominant_light.azimuth_degrees },
					{ "elevation_degrees", data.dominant_light.elevation_degrees },
					{ "luminance", data.dominant_light.luminance },
					{ "source", data.dominant_light.source },
				};
			}
			return metadata;
		}

		static auto populate_environment_map_metadata_from_json(
			const json& metadata,
			EnvironmentMapMetadata& out_metadata) -> void
		{
			out_metadata.build_desc.source_texture_path = metadata.value("source_texture_path", std::string{});
			out_metadata.source_content_hash = metadata.value("source_content_hash", 0ull);
			if (metadata.contains("build_settings"))
			{
				const json& settings = metadata["build_settings"];
				out_metadata.build_desc.radiance_size = settings.value("radiance_size", out_metadata.build_desc.radiance_size);
				out_metadata.build_desc.irradiance_size = settings.value("irradiance_size", out_metadata.build_desc.irradiance_size);
				out_metadata.build_desc.prefilter_size = settings.value("prefilter_size", out_metadata.build_desc.prefilter_size);
				out_metadata.build_desc.prefilter_mip_count = settings.value("prefilter_mip_count", out_metadata.build_desc.prefilter_mip_count);
				out_metadata.build_desc.brdf_lut_size = settings.value("brdf_lut_size", out_metadata.build_desc.brdf_lut_size);
				out_metadata.build_desc.sample_count = settings.value("sample_count", out_metadata.build_desc.sample_count);
			}
			if (metadata.contains("dominant_light") && metadata["dominant_light"].is_object())
			{
				const json& dominant_light = metadata["dominant_light"];
				const json direction = dominant_light.value("direction", json::array());
				if (direction.is_array() && direction.size() >= 3u)
				{
					out_metadata.dominant_light.valid = true;
					out_metadata.dominant_light.direction = glm::vec3(
						direction[0].get<float>(),
						direction[1].get<float>(),
						direction[2].get<float>());
					out_metadata.dominant_light.azimuth_degrees =
						dominant_light.value("azimuth_degrees", out_metadata.dominant_light.azimuth_degrees);
					out_metadata.dominant_light.elevation_degrees =
						dominant_light.value("elevation_degrees", out_metadata.dominant_light.elevation_degrees);
					out_metadata.dominant_light.luminance =
						dominant_light.value("luminance", out_metadata.dominant_light.luminance);
					out_metadata.dominant_light.source =
						dominant_light.value("source", std::string{});
				}
			}
		}

		static auto append_cube_payload_bytes(
			std::vector<uint8_t>& blob,
			const TextureCubePayload& payload,
			AshIBLPayloadKind kind,
			std::vector<AshIBLPayloadDesc>& table) -> bool
		{
			std::vector<TextureSubresourcePayload> ordered = payload.subresources;
			std::sort(ordered.begin(), ordered.end(), [](const TextureSubresourcePayload& a, const TextureSubresourcePayload& b)
			{
				if (a.mip_level != b.mip_level)
				{
					return a.mip_level < b.mip_level;
				}
				return a.array_layer < b.array_layer;
			});

			const uint64_t payload_offset = align_up(blob.size(), k_ashibl_alignment);
			if (payload_offset > blob.size())
			{
				blob.resize(static_cast<size_t>(payload_offset), 0u);
			}

			uint64_t byte_size = 0;
			for (const TextureSubresourcePayload& subresource : ordered)
			{
				const uint32_t mip_height = std::max(1u, payload.height >> subresource.mip_level);
				const uint32_t tight_row_pitch = calculate_render_texture_tight_row_pitch(payload.format, subresource.width);
				const uint32_t row_pitch = std::max(subresource.row_pitch, tight_row_pitch);
				for (uint32_t row = 0; row < mip_height; ++row)
				{
					const size_t src_offset = static_cast<size_t>(row) * subresource.row_pitch;
					if (src_offset + tight_row_pitch > subresource.pixel_data.size())
					{
						return false;
					}
					blob.insert(blob.end(), subresource.pixel_data.data() + src_offset, subresource.pixel_data.data() + src_offset + tight_row_pitch);
					byte_size += tight_row_pitch;
				}
			}

			AshIBLPayloadDesc desc{};
			desc.kind = kind;
			desc.compression = AshIBLCompression::None;
			desc.format = payload.format;
			desc.width = payload.width;
			desc.height = payload.height;
			desc.face_count = k_ashibl_face_count;
			desc.mip_count = payload.mip_count;
			desc.byte_offset = payload_offset;
			desc.byte_size = byte_size;
			desc.uncompressed_size = byte_size;
			table.push_back(desc);
			return true;
		}

		static auto append_brdf_payload_bytes(
			std::vector<uint8_t>& blob,
			const Texture2DPayload& payload,
			std::vector<AshIBLPayloadDesc>& table) -> bool
		{
			const uint64_t payload_offset = align_up(blob.size(), k_ashibl_alignment);
			if (payload_offset > blob.size())
			{
				blob.resize(static_cast<size_t>(payload_offset), 0u);
			}

			const uint32_t tight_row_pitch = calculate_render_texture_tight_row_pitch(payload.format, payload.width);
			const uint32_t row_pitch = std::max(payload.row_pitch, tight_row_pitch);
			uint64_t byte_size = 0;
			for (uint32_t row = 0; row < payload.height; ++row)
			{
				const size_t src_offset = static_cast<size_t>(row) * row_pitch;
				if (src_offset + tight_row_pitch > payload.pixel_data.size())
				{
					return false;
				}
				blob.insert(blob.end(), payload.pixel_data.data() + src_offset, payload.pixel_data.data() + src_offset + tight_row_pitch);
				byte_size += tight_row_pitch;
			}

			AshIBLPayloadDesc desc{};
			desc.kind = AshIBLPayloadKind::BRDFLut2D;
			desc.compression = AshIBLCompression::None;
			desc.format = payload.format;
			desc.width = payload.width;
			desc.height = payload.height;
			desc.face_count = 1;
			desc.mip_count = 1;
			desc.byte_offset = payload_offset;
			desc.byte_size = byte_size;
			desc.uncompressed_size = byte_size;
			table.push_back(desc);
			return true;
		}

		static auto read_payload_descs(
			const std::vector<uint8_t>& file_bytes,
			const AshIBLHeader& header,
			std::vector<AshIBLPayloadDesc>& out_descs,
			std::string* out_error) -> bool
		{
			if (header.payload_table_offset + header.payload_table_size > file_bytes.size())
			{
				return make_error(out_error, "AshIBL payload table is out of range.");
			}
			if (header.payload_table_size % sizeof(AshIBLPayloadDesc) != 0)
			{
				return make_error(out_error, "AshIBL payload table size is invalid.");
			}

			const size_t desc_count = static_cast<size_t>(header.payload_table_size / sizeof(AshIBLPayloadDesc));
			if (desc_count != header.payload_count)
			{
				return make_error(out_error, "AshIBL payload table count does not match the header.");
			}
			out_descs.resize(desc_count);
			std::memcpy(out_descs.data(), file_bytes.data() + header.payload_table_offset, header.payload_table_size);
			return true;
		}

		static auto load_cube_payload_from_desc(
			const std::vector<uint8_t>& file_bytes,
			const AshIBLPayloadDesc& desc,
			TextureCubePayload& out_payload,
			std::string* out_error) -> bool
		{
			if (desc.compression != AshIBLCompression::None)
			{
				return make_error(out_error, "AshIBL compressed payloads are not supported in v1.");
			}
			if (desc.face_count != k_ashibl_face_count)
			{
				return make_error(out_error, "AshIBL cubemap face_count must be 6.");
			}
			if (desc.byte_offset + desc.byte_size > file_bytes.size())
			{
				return make_error(out_error, "AshIBL cubemap payload is out of range.");
			}

			const uint64_t payload_end = desc.byte_offset + desc.byte_size;
			out_payload.format = desc.format;
			out_payload.width = desc.width;
			out_payload.height = desc.height;
			out_payload.mip_count = desc.mip_count;
			out_payload.subresources.clear();
			out_payload.subresources.reserve(static_cast<size_t>(desc.mip_count) * k_ashibl_face_count);

			uint64_t cursor = desc.byte_offset;
			for (uint32_t mip = 0; mip < desc.mip_count; ++mip)
			{
				const uint32_t mip_width = std::max(1u, desc.width >> mip);
				const uint32_t mip_height = std::max(1u, desc.height >> mip);
				const uint32_t tight_row_pitch = calculate_render_texture_tight_row_pitch(desc.format, mip_width);
				for (uint32_t face = 0; face < k_ashibl_face_count; ++face)
				{
					TextureSubresourcePayload subresource{};
					subresource.mip_level = mip;
					subresource.array_layer = face;
					subresource.width = mip_width;
					subresource.height = mip_height;
					subresource.row_pitch = tight_row_pitch;
					subresource.pixel_data.resize(static_cast<size_t>(tight_row_pitch) * mip_height);
					for (uint32_t row = 0; row < mip_height; ++row)
					{
						if (cursor + tight_row_pitch > payload_end)
						{
							return make_error(out_error, "AshIBL cubemap payload truncated.");
						}
						std::memcpy(
							subresource.pixel_data.data() + static_cast<size_t>(row) * tight_row_pitch,
							file_bytes.data() + cursor,
							tight_row_pitch);
						cursor += tight_row_pitch;
					}
					out_payload.subresources.push_back(std::move(subresource));
				}
			}
			return cursor == payload_end || make_error(out_error, "AshIBL cubemap payload size does not match its layout.");
		}

		static auto load_brdf_payload_from_desc(
			const std::vector<uint8_t>& file_bytes,
			const AshIBLPayloadDesc& desc,
			Texture2DPayload& out_payload,
			std::string* out_error) -> bool
		{
			if (desc.compression != AshIBLCompression::None)
			{
				return make_error(out_error, "AshIBL compressed payloads are not supported in v1.");
			}
			if (desc.face_count != 1)
			{
				return make_error(out_error, "AshIBL BRDF LUT face_count must be 1.");
			}
			if (desc.byte_offset + desc.byte_size > file_bytes.size())
			{
				return make_error(out_error, "AshIBL BRDF LUT payload is out of range.");
			}

			const uint64_t payload_end = desc.byte_offset + desc.byte_size;
			const uint32_t tight_row_pitch = calculate_render_texture_tight_row_pitch(desc.format, desc.width);
			out_payload.format = desc.format;
			out_payload.width = desc.width;
			out_payload.height = desc.height;
			out_payload.row_pitch = tight_row_pitch;
			out_payload.pixel_data.resize(static_cast<size_t>(tight_row_pitch) * desc.height);

			uint64_t cursor = desc.byte_offset;
			for (uint32_t row = 0; row < desc.height; ++row)
			{
				if (cursor + tight_row_pitch > payload_end)
				{
					return make_error(out_error, "AshIBL BRDF LUT payload truncated.");
				}
				std::memcpy(
					out_payload.pixel_data.data() + static_cast<size_t>(row) * tight_row_pitch,
					file_bytes.data() + cursor,
					tight_row_pitch);
				cursor += tight_row_pitch;
			}
			return cursor == payload_end || make_error(out_error, "AshIBL BRDF LUT payload size does not match its layout.");
		}
	}

	EnvironmentLightingConfig make_default_environment_lighting_config()
	{
		return EnvironmentLightingConfig{};
	}

	EnvironmentLightingConfig load_runtime_environment_lighting_config(const char* config_path)
	{
		EnvironmentLightingConfig config = make_default_environment_lighting_config();
		IniConfig ini_config{};
		if (!ini_config.load(config_path))
		{
			HLogInfo(
				"Environment lighting config file '{}' was not found. Using default environment lighting config.",
				resolve_runtime_config_path(config_path).string());
			return config;
		}

		bool bool_value = false;
		if (ini_config.try_get_bool("EnvironmentLighting", "RuntimeBakeCache", bool_value))
		{
			config.runtime_bake_cache = bool_value;
		}

		uint32_t uint_value = 0u;
		if (try_get_ini_uint32(ini_config, "EnvironmentLighting", "DefaultRadianceSize", uint_value))
		{
			config.default_radiance_size = clamp_power_of_two(uint_value, 1u, 4096u, config.default_radiance_size);
		}
		if (try_get_ini_uint32(ini_config, "EnvironmentLighting", "DefaultIrradianceSize", uint_value))
		{
			config.default_irradiance_size = clamp_power_of_two(uint_value, 1u, 1024u, config.default_irradiance_size);
		}
		if (try_get_ini_uint32(ini_config, "EnvironmentLighting", "DefaultPrefilterSize", uint_value))
		{
			config.default_prefilter_size = clamp_power_of_two(uint_value, 1u, 2048u, config.default_prefilter_size);
		}
		if (try_get_ini_uint32(ini_config, "EnvironmentLighting", "DefaultBRDFLUTSize", uint_value))
		{
			config.default_brdf_lut_size = clamp_power_of_two(uint_value, 1u, 1024u, config.default_brdf_lut_size);
		}
		if (try_get_ini_uint32(ini_config, "EnvironmentLighting", "DefaultPrefilterMipCount", uint_value))
		{
			config.default_prefilter_mip_count = std::clamp(
				uint_value,
				1u,
				max_mip_count_for_size(config.default_prefilter_size));
		}
		if (try_get_ini_uint32(ini_config, "EnvironmentLighting", "DefaultSampleCount", uint_value))
		{
			config.default_sample_count = std::clamp(uint_value, 1u, 4096u);
		}

		HLogInfo(
			"Runtime environment lighting config loaded. runtime_bake_cache={} radiance_size={} irradiance_size={} prefilter_size={} prefilter_mips={} brdf_lut_size={} sample_count={}.",
			config.runtime_bake_cache,
			config.default_radiance_size,
			config.default_irradiance_size,
			config.default_prefilter_size,
			config.default_prefilter_mip_count,
			config.default_brdf_lut_size,
			config.default_sample_count);
		return config;
	}

	void set_runtime_environment_lighting_config(const EnvironmentLightingConfig& config)
	{
		std::lock_guard<std::mutex> lock(runtime_environment_lighting_config_mutex());
		runtime_environment_lighting_config_storage() = config;
	}

	EnvironmentLightingConfig get_runtime_environment_lighting_config()
	{
		std::lock_guard<std::mutex> lock(runtime_environment_lighting_config_mutex());
		return runtime_environment_lighting_config_storage();
	}

	EnvironmentMapBuildDesc make_environment_map_build_desc_from_runtime_config(const std::string& source_texture_path)
	{
		const EnvironmentLightingConfig config = get_runtime_environment_lighting_config();
		EnvironmentMapBuildDesc desc{};
		desc.source_texture_path = source_texture_path;
		desc.radiance_size = config.default_radiance_size;
		desc.irradiance_size = config.default_irradiance_size;
		desc.prefilter_size = config.default_prefilter_size;
		desc.prefilter_mip_count = std::min(
			config.default_prefilter_mip_count,
			max_mip_count_for_size(config.default_prefilter_size));
		desc.brdf_lut_size = config.default_brdf_lut_size;
		desc.sample_count = config.default_sample_count;
		return desc;
	}

	uint64_t hash_environment_source_file(const std::filesystem::path& path)
	{
		std::ifstream input_file(path, std::ios::binary | std::ios::ate);
		if (!input_file)
		{
			return 0ull;
		}

		const std::streamsize file_size = input_file.tellg();
		if (file_size <= 0)
		{
			return 0ull;
		}

		std::vector<uint8_t> file_bytes(static_cast<size_t>(file_size));
		input_file.seekg(0, std::ios::beg);
		input_file.read(reinterpret_cast<char*>(file_bytes.data()), file_size);
		if (!input_file)
		{
			return 0ull;
		}

		return wyhash(static_cast<const void*>(file_bytes.data()), file_bytes.size(), 0ull, _wyp);
	}

	std::filesystem::path make_environment_map_source_cache_path(uint64_t source_content_hash)
	{
		if (source_content_hash == 0ull)
		{
			return {};
		}

		return std::filesystem::path("product/caches/EnvironmentCaches") /
			(std::to_string(source_content_hash) + ".ashibl");
	}

	bool validate_environment_map_cooked_data(const EnvironmentMapCookedData& data, std::string* out_error)
	{
		if (!validate_cube_payload(data.radiance, "radiance", out_error))
		{
			return false;
		}
		if (!validate_cube_payload(data.irradiance, "irradiance", out_error))
		{
			return false;
		}
		if (!validate_cube_payload(data.prefiltered_specular, "prefiltered_specular", out_error))
		{
			return false;
		}
		if (data.brdf_lut.width == 0 || data.brdf_lut.height == 0)
		{
			return make_error(out_error, "BRDF LUT width/height must be non-zero.");
		}
		if (!payload_format_is_valid(data.brdf_lut.format, true))
		{
			return make_error(out_error, "BRDF LUT format is unsupported.");
		}
		const uint32_t brdf_tight_row_pitch = calculate_render_texture_tight_row_pitch(data.brdf_lut.format, data.brdf_lut.width);
		if (data.brdf_lut.row_pitch < brdf_tight_row_pitch)
		{
			return make_error(out_error, "BRDF LUT row pitch is too small.");
		}
		if (data.brdf_lut.pixel_data.size() < static_cast<size_t>(brdf_tight_row_pitch) * data.brdf_lut.height)
		{
			return make_error(out_error, "BRDF LUT pixel payload is too small.");
		}
		return true;
	}

	bool write_ashibl_file(const std::filesystem::path& path, const EnvironmentMapCookedData& data, std::string* out_error)
	{
		if (!validate_environment_map_cooked_data(data, out_error))
		{
			return false;
		}

		std::vector<AshIBLPayloadDesc> payload_table{};
		std::vector<uint8_t> payload_blob{};
		if (!append_cube_payload_bytes(payload_blob, data.radiance, AshIBLPayloadKind::RadianceCubemap, payload_table) ||
			!append_cube_payload_bytes(payload_blob, data.irradiance, AshIBLPayloadKind::IrradianceCubemap, payload_table) ||
			!append_cube_payload_bytes(payload_blob, data.prefiltered_specular, AshIBLPayloadKind::PrefilteredSpecularCubemap, payload_table) ||
			!append_brdf_payload_bytes(payload_blob, data.brdf_lut, payload_table))
		{
			return make_error(out_error, "Failed to serialize AshIBL payloads.");
		}

		const json metadata = build_metadata_json(data);
		const std::string metadata_text = metadata.dump();
		const uint64_t metadata_offset = align_up(sizeof(AshIBLHeader), k_ashibl_alignment);
		const uint64_t metadata_size = metadata_text.size();
		const uint64_t payload_table_offset = align_up(metadata_offset + metadata_size, k_ashibl_alignment);
		const uint64_t payload_table_size = payload_table.size() * sizeof(AshIBLPayloadDesc);
		const uint64_t payload_blob_offset = align_up(payload_table_offset + payload_table_size, k_ashibl_alignment);

		for (AshIBLPayloadDesc& desc : payload_table)
		{
			desc.byte_offset += payload_blob_offset;
		}

		std::vector<uint8_t> file_bytes(static_cast<size_t>(payload_blob_offset), 0u);
		file_bytes.insert(file_bytes.end(), payload_blob.begin(), payload_blob.end());

		AshIBLHeader header{};
		std::memcpy(header.magic, k_ashibl_magic, sizeof(k_ashibl_magic));
		header.version = k_ashibl_version;
		header.payload_count = static_cast<uint32_t>(payload_table.size());
		header.metadata_offset = metadata_offset;
		header.metadata_size = metadata_size;
		header.payload_table_offset = payload_table_offset;
		header.payload_table_size = payload_table_size;

		std::memcpy(file_bytes.data(), &header, sizeof(header));
		std::memcpy(file_bytes.data() + metadata_offset, metadata_text.data(), metadata_text.size());
		std::memcpy(file_bytes.data() + payload_table_offset, payload_table.data(), payload_table_size);

		std::error_code create_error{};
		if (!path.parent_path().empty())
		{
			std::filesystem::create_directories(path.parent_path(), create_error);
		}

		std::ofstream output(path, std::ios::binary);
		if (!output.is_open())
		{
			return make_error(out_error, "Failed to open AshIBL output file.");
		}
		output.write(reinterpret_cast<const char*>(file_bytes.data()), static_cast<std::streamsize>(file_bytes.size()));
		return output.good();
	}

	bool read_ashibl_file(const std::filesystem::path& path, EnvironmentMapCookedData& out_data, std::string* out_error)
	{
		out_data = {};
		std::ifstream input(path, std::ios::binary | std::ios::ate);
		if (!input.is_open())
		{
			return make_error(out_error, "Failed to open AshIBL input file.");
		}

		const std::streamsize file_size = input.tellg();
		if (file_size < static_cast<std::streamsize>(sizeof(AshIBLHeader)))
		{
			return make_error(out_error, "AshIBL file is too small.");
		}
		input.seekg(0, std::ios::beg);

		std::vector<uint8_t> file_bytes(static_cast<size_t>(file_size));
		input.read(reinterpret_cast<char*>(file_bytes.data()), file_size);
		if (!input.good())
		{
			return make_error(out_error, "Failed to read AshIBL file.");
		}

		AshIBLHeader header{};
		std::memcpy(&header, file_bytes.data(), sizeof(header));
		if (std::memcmp(header.magic, k_ashibl_magic, sizeof(k_ashibl_magic)) != 0)
		{
			return make_error(out_error, "AshIBL magic is invalid.");
		}
		if (header.version != k_ashibl_version)
		{
			return make_error(out_error, "AshIBL version is unsupported.");
		}
		if (header.payload_count < 4)
		{
			return make_error(out_error, "AshIBL payload table is missing required payloads.");
		}
		if (header.metadata_offset + header.metadata_size > file_bytes.size() ||
			header.payload_table_offset + header.payload_table_size > file_bytes.size())
		{
			return make_error(out_error, "AshIBL header offsets are out of range.");
		}

		std::vector<AshIBLPayloadDesc> payload_descs{};
		if (!read_payload_descs(file_bytes, header, payload_descs, out_error))
		{
			return false;
		}

		bool has_radiance = false;
		bool has_irradiance = false;
		bool has_prefilter = false;
		bool has_brdf = false;
		for (const AshIBLPayloadDesc& desc : payload_descs)
		{
			if (desc.compression != AshIBLCompression::None)
			{
				return make_error(out_error, "AshIBL compressed payloads are not supported in v1.");
			}
			if (desc.byte_offset + desc.byte_size > file_bytes.size())
			{
				return make_error(out_error, "AshIBL payload offset/size is out of range.");
			}

			switch (desc.kind)
			{
			case AshIBLPayloadKind::RadianceCubemap:
				if (!load_cube_payload_from_desc(file_bytes, desc, out_data.radiance, out_error))
				{
					return false;
				}
				has_radiance = true;
				break;
			case AshIBLPayloadKind::IrradianceCubemap:
				if (!load_cube_payload_from_desc(file_bytes, desc, out_data.irradiance, out_error))
				{
					return false;
				}
				has_irradiance = true;
				break;
			case AshIBLPayloadKind::PrefilteredSpecularCubemap:
				if (!load_cube_payload_from_desc(file_bytes, desc, out_data.prefiltered_specular, out_error))
				{
					return false;
				}
				has_prefilter = true;
				break;
			case AshIBLPayloadKind::BRDFLut2D:
				if (!load_brdf_payload_from_desc(file_bytes, desc, out_data.brdf_lut, out_error))
				{
					return false;
				}
				has_brdf = true;
				break;
			default:
				break;
			}
		}

		if (!has_radiance || !has_irradiance || !has_prefilter || !has_brdf)
		{
			return make_error(out_error, "AshIBL file is missing one or more required payloads.");
		}

		try
		{
			const json metadata = json::parse(
				std::string(reinterpret_cast<const char*>(file_bytes.data() + header.metadata_offset), header.metadata_size));
			EnvironmentMapMetadata parsed_metadata{};
			populate_environment_map_metadata_from_json(metadata, parsed_metadata);
			out_data.build_desc = parsed_metadata.build_desc;
			out_data.source_content_hash = parsed_metadata.source_content_hash;
			out_data.dominant_light = parsed_metadata.dominant_light;
		}
		catch (const std::exception& exception)
		{
			return make_error(out_error, exception.what());
		}

		return validate_environment_map_cooked_data(out_data, out_error);
	}

	bool read_ashibl_metadata_file(const std::filesystem::path& path, EnvironmentMapMetadata& out_metadata, std::string* out_error)
	{
		out_metadata = {};
		std::ifstream input(path, std::ios::binary | std::ios::ate);
		if (!input.is_open())
		{
			return make_error(out_error, "Failed to open AshIBL input file.");
		}

		const std::streamsize file_size_stream = input.tellg();
		if (file_size_stream < static_cast<std::streamsize>(sizeof(AshIBLHeader)))
		{
			return make_error(out_error, "AshIBL file is too small.");
		}

		const uint64_t file_size = static_cast<uint64_t>(file_size_stream);
		input.seekg(0, std::ios::beg);

		AshIBLHeader header{};
		input.read(reinterpret_cast<char*>(&header), static_cast<std::streamsize>(sizeof(header)));
		if (!input.good())
		{
			return make_error(out_error, "Failed to read AshIBL header.");
		}
		if (std::memcmp(header.magic, k_ashibl_magic, sizeof(k_ashibl_magic)) != 0)
		{
			return make_error(out_error, "AshIBL magic is invalid.");
		}
		if (header.version != k_ashibl_version)
		{
			return make_error(out_error, "AshIBL version is unsupported.");
		}
		if (header.metadata_size == 0)
		{
			return make_error(out_error, "AshIBL metadata is empty.");
		}
		if (header.metadata_offset > file_size || header.metadata_size > file_size - header.metadata_offset)
		{
			return make_error(out_error, "AshIBL metadata offset/size is out of range.");
		}
		if (header.metadata_size > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
		{
			return make_error(out_error, "AshIBL metadata is too large.");
		}

		std::string metadata_text(static_cast<size_t>(header.metadata_size), '\0');
		input.seekg(static_cast<std::streamoff>(header.metadata_offset), std::ios::beg);
		input.read(metadata_text.data(), static_cast<std::streamsize>(metadata_text.size()));
		if (!input.good())
		{
			return make_error(out_error, "Failed to read AshIBL metadata.");
		}

		try
		{
			const json metadata = json::parse(metadata_text);
			populate_environment_map_metadata_from_json(metadata, out_metadata);
		}
		catch (const std::exception& exception)
		{
			return make_error(out_error, exception.what());
		}
		return true;
	}

	void fill_environment_map_test_pattern(EnvironmentMapCookedData& data)
	{
		auto fill_cube = [](TextureCubePayload& payload, uint32_t size, uint32_t mip_count, uint8_t base_value)
		{
			payload.format = RenderTextureFormat::RGBA16_SFLOAT;
			payload.width = size;
			payload.height = size;
			payload.mip_count = mip_count;
			payload.subresources.clear();
			for (uint32_t mip = 0; mip < mip_count; ++mip)
			{
				const uint32_t mip_width = std::max(1u, size >> mip);
				const uint32_t mip_height = std::max(1u, size >> mip);
				const uint32_t row_pitch = calculate_render_texture_tight_row_pitch(payload.format, mip_width);
				for (uint32_t face = 0; face < k_ashibl_face_count; ++face)
				{
					TextureSubresourcePayload subresource{};
					subresource.mip_level = mip;
					subresource.array_layer = face;
					subresource.width = mip_width;
					subresource.height = mip_height;
					subresource.row_pitch = row_pitch;
					subresource.pixel_data.assign(static_cast<size_t>(row_pitch) * mip_height, static_cast<uint8_t>(base_value + face + mip));
					payload.subresources.push_back(std::move(subresource));
				}
			}
		};

		fill_cube(data.radiance, data.build_desc.radiance_size, 1u, 10u);
		fill_cube(data.irradiance, data.build_desc.irradiance_size, 1u, 20u);
		fill_cube(data.prefiltered_specular, data.build_desc.prefilter_size, data.build_desc.prefilter_mip_count, 30u);

		data.brdf_lut.format = RenderTextureFormat::RG16_SFLOAT;
		data.brdf_lut.width = data.build_desc.brdf_lut_size;
		data.brdf_lut.height = data.build_desc.brdf_lut_size;
		data.brdf_lut.row_pitch = calculate_render_texture_tight_row_pitch(data.brdf_lut.format, data.brdf_lut.width);
		data.brdf_lut.pixel_data.assign(static_cast<size_t>(data.brdf_lut.row_pitch) * data.brdf_lut.height, 40u);
	}

	std::string make_environment_map_asset_key(const std::string& ibl_asset_path, const std::string& source_texture_path)
	{
		if (!ibl_asset_path.empty())
		{
			return "ashibl:" + ibl_asset_path;
		}
		if (!source_texture_path.empty())
		{
			return "source:" + source_texture_path;
		}
		return "fallback:";
	}
}
