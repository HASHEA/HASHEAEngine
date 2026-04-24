#include "Function/Render/Material.h"

#include <json.hpp>
#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <string_view>

namespace AshEngine
{
	namespace
	{
		using json = nlohmann::json;

		constexpr uint32_t k_material_file_version = 1;

		static auto make_error(std::string* out_error, std::string_view message) -> bool
		{
			if (out_error)
			{
				*out_error = std::string(message);
			}
			return false;
		}

		static auto clear_error(std::string* out_error) -> void
		{
			if (out_error)
			{
				out_error->clear();
			}
		}

		struct ParsedMaterialTextureBinding
		{
			MaterialTextureBinding binding{};
			bool has_legacy_inline_sampler = false;
			RenderSamplerDesc legacy_inline_sampler{};
		};

		struct LegacyMaterialSamplerCacheEntry
		{
			RenderSamplerDesc desc{};
			std::string sampler_name{};
		};

		static auto to_lower_copy(std::string value) -> std::string
		{
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
			{
				return static_cast<char>(std::tolower(c));
			});
			return value;
		}

		static auto normalize_material_key(std::string_view path) -> std::string
		{
			return to_lower_copy(std::filesystem::path(path).lexically_normal().generic_string());
		}

		static auto canonical_builtin_material_path(std::string_view path) -> std::string
		{
			const std::string key = normalize_material_key(path);
			if (key == normalize_material_key(k_builtin_surface_pbr_material_path))
			{
				return k_builtin_surface_pbr_material_path;
			}
			if (key == normalize_material_key(k_builtin_default_surface_material_path))
			{
				return k_builtin_default_surface_material_path;
			}
			return {};
		}

		static auto mix_versions(uint64_t lhs, uint64_t rhs) -> uint64_t
		{
			uint64_t result = lhs;
			result ^= rhs + 0x9e3779b97f4a7c15ull + (result << 6) + (result >> 2);
			return result;
		}

		static auto to_json_vec4(const glm::vec4& value) -> json
		{
			return json::array({ value.x, value.y, value.z, value.w });
		}

		static auto from_json_vec4(const json& value, const glm::vec4& fallback) -> glm::vec4
		{
			if (!value.is_array() || (value.size() != 3 && value.size() != 4))
			{
				return fallback;
			}

			glm::vec4 result = fallback;
			result.x = value[0].get<float>();
			result.y = value[1].get<float>();
			result.z = value[2].get<float>();
			result.w = value.size() >= 4 ? value[3].get<float>() : fallback.w;
			return result;
		}

		static auto material_domain_to_string(MaterialDomain domain) -> const char*
		{
			switch (domain)
			{
			case MaterialDomain::Surface:
				return "Surface";
			case MaterialDomain::Decal:
				return "Decal";
			case MaterialDomain::PostProcess:
				return "PostProcess";
			case MaterialDomain::UI:
				return "UI";
			default:
				return "Surface";
			}
		}

		static auto material_blend_mode_to_string(MaterialBlendMode blend_mode) -> const char*
		{
			switch (blend_mode)
			{
			case MaterialBlendMode::Opaque:
				return "Opaque";
			case MaterialBlendMode::Masked:
				return "Masked";
			case MaterialBlendMode::Transparent:
				return "Transparent";
			default:
				return "Opaque";
			}
		}

		static auto material_shading_model_to_string(MaterialShadingModel shading_model) -> const char*
		{
			switch (shading_model)
			{
			case MaterialShadingModel::DefaultLit:
				return "DefaultLit";
			default:
				return "DefaultLit";
			}
		}

		static auto material_parameter_type_to_string(MaterialParameterType type) -> const char*
		{
			switch (type)
			{
			case MaterialParameterType::Scalar:
				return "Scalar";
			case MaterialParameterType::Vector4:
				return "Vector4";
			case MaterialParameterType::Texture:
				return "Texture";
			default:
				return "Scalar";
			}
		}

		static auto render_sampler_address_mode_to_string(RenderSamplerAddressMode mode) -> const char*
		{
			switch (mode)
			{
			case RenderSamplerAddressMode::Repeat:
				return "Repeat";
			case RenderSamplerAddressMode::MirroredRepeat:
				return "MirroredRepeat";
			case RenderSamplerAddressMode::ClampToEdge:
				return "ClampToEdge";
			case RenderSamplerAddressMode::ClampToBorder:
				return "ClampToBorder";
			case RenderSamplerAddressMode::MirrorClampToEdge:
				return "MirrorClampToEdge";
			default:
				return "Repeat";
			}
		}

		static auto render_sampler_filter_to_string(RenderSamplerFilter filter) -> const char*
		{
			switch (filter)
			{
			case RenderSamplerFilter::Nearest:
				return "Nearest";
			case RenderSamplerFilter::Linear:
			default:
				return "Linear";
			}
		}

		static auto normalize_material_token(std::string value) -> std::string
		{
			value = to_lower_copy(std::move(value));
			value.erase(
				std::remove_if(value.begin(), value.end(), [](unsigned char c)
				{
					return !std::isalnum(c);
				}),
				value.end());
			return value;
		}

		static auto try_parse_render_sampler_address_mode(
			const json& value,
			RenderSamplerAddressMode& out_mode) -> bool
		{
			if (!value.is_string())
			{
				return false;
			}

			const std::string lowered = normalize_material_token(value.get<std::string>());
			if (lowered == "repeat")
			{
				out_mode = RenderSamplerAddressMode::Repeat;
				return true;
			}
			if (lowered == "mirroredrepeat")
			{
				out_mode = RenderSamplerAddressMode::MirroredRepeat;
				return true;
			}
			if (lowered == "clamptoedge")
			{
				out_mode = RenderSamplerAddressMode::ClampToEdge;
				return true;
			}
			if (lowered == "clamptoborder")
			{
				out_mode = RenderSamplerAddressMode::ClampToBorder;
				return true;
			}
			if (lowered == "mirrorclamptoedge")
			{
				out_mode = RenderSamplerAddressMode::MirrorClampToEdge;
				return true;
			}
			return false;
		}

		static auto try_parse_render_sampler_filter(
			const json& value,
			RenderSamplerFilter& out_filter) -> bool
		{
			if (!value.is_string())
			{
				return false;
			}

			const std::string lowered = normalize_material_token(value.get<std::string>());
			if (lowered == "nearest")
			{
				out_filter = RenderSamplerFilter::Nearest;
				return true;
			}
			if (lowered == "linear")
			{
				out_filter = RenderSamplerFilter::Linear;
				return true;
			}
			return false;
		}

		static auto render_sampler_desc_to_json(const RenderSamplerDesc& desc) -> json
		{
			return json
			{
				{ "address_u", render_sampler_address_mode_to_string(desc.address_u) },
				{ "address_v", render_sampler_address_mode_to_string(desc.address_v) },
				{ "address_w", render_sampler_address_mode_to_string(desc.address_w) },
				{ "min_filter", render_sampler_filter_to_string(desc.min_filter) },
				{ "mag_filter", render_sampler_filter_to_string(desc.mag_filter) },
				{ "mip_filter", render_sampler_filter_to_string(desc.mip_filter) },
			};
		}

		static auto parse_render_sampler_desc(
			const json& value,
			RenderSamplerDesc& out_desc,
			std::string* out_error) -> bool
		{
			if (!value.is_object())
			{
				return make_error(out_error, "Material texture sampler must be an object.");
			}

			out_desc = RenderSamplerDesc{};
			auto parse_address_mode = [&](const char* field_name, RenderSamplerAddressMode& field) -> bool
			{
				const auto it = value.find(field_name);
				if (it == value.end())
				{
					return true;
				}
				if (!try_parse_render_sampler_address_mode(*it, field))
				{
					return make_error(out_error, std::string("Material texture sampler field '") + field_name + "' uses an unsupported address mode.");
				}
				return true;
			};
			auto parse_filter = [&](const char* field_name, RenderSamplerFilter& field) -> bool
			{
				const auto it = value.find(field_name);
				if (it == value.end())
				{
					return true;
				}
				if (!try_parse_render_sampler_filter(*it, field))
				{
					return make_error(out_error, std::string("Material texture sampler field '") + field_name + "' uses an unsupported filter.");
				}
				return true;
			};

			if (!parse_address_mode("address_u", out_desc.address_u) ||
				!parse_address_mode("address_v", out_desc.address_v) ||
				!parse_address_mode("address_w", out_desc.address_w) ||
				!parse_filter("min_filter", out_desc.min_filter) ||
				!parse_filter("mag_filter", out_desc.mag_filter) ||
				!parse_filter("mip_filter", out_desc.mip_filter))
			{
				return false;
			}

			clear_error(out_error);
			return true;
		}

		static auto parse_material_texture_binding(
			const json& value,
			ParsedMaterialTextureBinding& out_binding,
			std::string* out_error) -> bool
		{
			out_binding = ParsedMaterialTextureBinding{};
			if (value.is_string())
			{
				out_binding.binding.texture_path = value.get<std::string>();
				clear_error(out_error);
				return true;
			}

			if (!value.is_object())
			{
				return make_error(out_error, "Texture material parameter value must be a string or object.");
			}

			if (const auto path_it = value.find("path"); path_it != value.end())
			{
				if (!path_it->is_string())
				{
					return make_error(out_error, "Material texture binding field 'path' must be a string.");
				}
				out_binding.binding.texture_path = path_it->get<std::string>();
			}

			if (const auto sampler_it = value.find("sampler"); sampler_it != value.end())
			{
				if (sampler_it->is_string())
				{
					out_binding.binding.sampler_name = sampler_it->get<std::string>();
				}
				else
				{
					out_binding.has_legacy_inline_sampler = true;
					if (!parse_render_sampler_desc(*sampler_it, out_binding.legacy_inline_sampler, out_error))
					{
						return false;
					}
				}
			}
			else if (const auto sampler_name_it = value.find("sampler_name"); sampler_name_it != value.end())
			{
				if (!sampler_name_it->is_string())
				{
					return make_error(out_error, "Material texture binding field 'sampler_name' must be a string.");
				}
				out_binding.binding.sampler_name = sampler_name_it->get<std::string>();
			}

			clear_error(out_error);
			return true;
		}

		static auto material_texture_binding_to_json(const MaterialTextureBinding& binding) -> json
		{
			json result = json::object();
			result["path"] = binding.texture_path;
			if (!binding.sampler_name.empty())
			{
				result["sampler"] = binding.sampler_name;
			}
			return result;
		}

		static auto is_valid_shader_identifier(std::string_view identifier) -> bool
		{
			if (identifier.empty())
			{
				return false;
			}

			const unsigned char first = static_cast<unsigned char>(identifier.front());
			if (!(std::isalpha(first) != 0 || identifier.front() == '_'))
			{
				return false;
			}

			for (const char ch : identifier)
			{
				const unsigned char value = static_cast<unsigned char>(ch);
				if (!(std::isalnum(value) != 0 || ch == '_'))
				{
					return false;
				}
			}

			return true;
		}

		static auto material_sampler_definition_to_json(const MaterialSamplerDefinition& definition) -> json
		{
			return json
			{
				{ "name", definition.name },
				{ "shader_sampler_name", definition.shader_sampler_name },
				{ "desc", render_sampler_desc_to_json(definition.desc) },
			};
		}

		static auto parse_material_sampler_definition(
			const json& value,
			MaterialSamplerDefinition& out_definition,
			std::string* out_error) -> bool
		{
			if (!value.is_object())
			{
				return make_error(out_error, "Material sampler definition must be an object.");
			}

			out_definition = MaterialSamplerDefinition{};
			out_definition.name = value.value("name", std::string{});
			if (out_definition.name.empty())
			{
				return make_error(out_error, "Material sampler definition is missing a name.");
			}

			out_definition.shader_sampler_name = value.value(
				"shader_sampler_name",
				value.value("shader_name", value.value("shader_sampler", std::string{})));
			if (out_definition.shader_sampler_name.empty())
			{
				return make_error(out_error, "Material sampler definition is missing 'shader_sampler_name'.");
			}
			if (!is_valid_shader_identifier(out_definition.shader_sampler_name))
			{
				return make_error(out_error, "Material sampler definition 'shader_sampler_name' must be a valid HLSL identifier.");
			}

			if (const auto desc_it = value.find("desc"); desc_it != value.end())
			{
				if (!parse_render_sampler_desc(*desc_it, out_definition.desc, out_error))
				{
					return false;
				}
			}
			else if (!parse_render_sampler_desc(value, out_definition.desc, out_error))
			{
				return false;
			}

			clear_error(out_error);
			return true;
		}

		static auto parse_material_sampler_definitions(
			const json& root,
			std::vector<MaterialSamplerDefinition>& out_definitions,
			std::string* out_error) -> bool
		{
			out_definitions.clear();
			if (!root.contains("samplers"))
			{
				clear_error(out_error);
				return true;
			}

			const json& samplers_json = root["samplers"];
			if (!samplers_json.is_array())
			{
				return make_error(out_error, "Material samplers must be an array.");
			}

			out_definitions.reserve(samplers_json.size());
			for (const json& sampler_json : samplers_json)
			{
				MaterialSamplerDefinition definition{};
				if (!parse_material_sampler_definition(sampler_json, definition, out_error))
				{
					return false;
				}

				for (const MaterialSamplerDefinition& existing : out_definitions)
				{
					if (existing.name == definition.name)
					{
						return make_error(out_error, "Material sampler definition names must be unique.");
					}
					if (existing.shader_sampler_name == definition.shader_sampler_name)
					{
						return make_error(out_error, "Material shader sampler names must be unique.");
					}
				}

				out_definitions.push_back(std::move(definition));
			}

			clear_error(out_error);
			return true;
		}

		static auto make_material_sampler_definitions_to_json(const std::vector<MaterialSamplerDefinition>& definitions) -> json
		{
			json samplers_json = json::array();
			for (const MaterialSamplerDefinition& definition : definitions)
			{
				samplers_json.push_back(material_sampler_definition_to_json(definition));
			}
			return samplers_json;
		}

		static auto make_unique_legacy_sampler_name(
			const std::vector<MaterialSamplerDefinition>& definitions,
			uint32_t start_index) -> std::string
		{
			uint32_t index = start_index;
			while (true)
			{
				const std::string candidate = "LegacySampler" + std::to_string(index);
				bool already_exists = false;
				for (const MaterialSamplerDefinition& definition : definitions)
				{
					if (definition.name == candidate || definition.shader_sampler_name == candidate)
					{
						already_exists = true;
						break;
					}
				}
				if (!already_exists)
				{
					return candidate;
				}
				++index;
			}
		}

		static auto resolve_legacy_sampler_binding(
			ParsedMaterialTextureBinding& binding,
			std::vector<MaterialSamplerDefinition>& inout_definitions,
			std::vector<LegacyMaterialSamplerCacheEntry>& legacy_cache) -> void
		{
			if (!binding.has_legacy_inline_sampler || !binding.binding.sampler_name.empty())
			{
				return;
			}

			for (const LegacyMaterialSamplerCacheEntry& cached : legacy_cache)
			{
				if (cached.desc == binding.legacy_inline_sampler)
				{
					binding.binding.sampler_name = cached.sampler_name;
					return;
				}
			}

			const std::string sampler_name = make_unique_legacy_sampler_name(
				inout_definitions,
				static_cast<uint32_t>(legacy_cache.size()));
			inout_definitions.push_back(MaterialSamplerDefinition{
				sampler_name,
				sampler_name,
				binding.legacy_inline_sampler
			});
			legacy_cache.push_back(LegacyMaterialSamplerCacheEntry{
				binding.legacy_inline_sampler,
				sampler_name
			});
			binding.binding.sampler_name = sampler_name;
		}

		static auto try_parse_material_domain(const json& root, MaterialDomain& out_domain) -> bool
		{
			const std::string value = to_lower_copy(root.value("domain", std::string(material_domain_to_string(out_domain))));
			if (value == "surface")
			{
				out_domain = MaterialDomain::Surface;
				return true;
			}
			if (value == "decal")
			{
				out_domain = MaterialDomain::Decal;
				return true;
			}
			if (value == "postprocess")
			{
				out_domain = MaterialDomain::PostProcess;
				return true;
			}
			if (value == "ui")
			{
				out_domain = MaterialDomain::UI;
				return true;
			}
			return false;
		}

		static auto try_parse_material_blend_mode(const json& root, MaterialBlendMode& out_blend_mode) -> bool
		{
			const std::string value = to_lower_copy(root.value("blend_mode", std::string(material_blend_mode_to_string(out_blend_mode))));
			if (value == "opaque")
			{
				out_blend_mode = MaterialBlendMode::Opaque;
				return true;
			}
			if (value == "masked")
			{
				out_blend_mode = MaterialBlendMode::Masked;
				return true;
			}
			if (value == "transparent")
			{
				out_blend_mode = MaterialBlendMode::Transparent;
				return true;
			}
			return false;
		}

		static auto try_parse_material_shading_model(const json& root, MaterialShadingModel& out_shading_model) -> bool
		{
			const std::string value = to_lower_copy(root.value("shading_model", std::string(material_shading_model_to_string(out_shading_model))));
			if (value == "defaultlit")
			{
				out_shading_model = MaterialShadingModel::DefaultLit;
				return true;
			}
			return false;
		}

		static auto try_parse_material_parameter_type(const std::string& value, MaterialParameterType& out_type) -> bool
		{
			const std::string lowered = to_lower_copy(value);
			if (lowered == "scalar")
			{
				out_type = MaterialParameterType::Scalar;
				return true;
			}
			if (lowered == "vector4")
			{
				out_type = MaterialParameterType::Vector4;
				return true;
			}
			if (lowered == "texture")
			{
				out_type = MaterialParameterType::Texture;
				return true;
			}
			return false;
		}

		static auto parameter_desc_to_json(const MaterialParameterDesc& parameter_desc) -> json
		{
			json result{};
			result["name"] = parameter_desc.name;
			result["type"] = material_parameter_type_to_string(parameter_desc.type);
			if (!parameter_desc.group.empty())
			{
				result["group"] = parameter_desc.group;
			}
			if (!parameter_desc.semantic.empty())
			{
				result["semantic"] = parameter_desc.semantic;
			}
			if (!parameter_desc.texture_usage.empty())
			{
				result["texture_usage"] = parameter_desc.texture_usage;
			}

			switch (parameter_desc.type)
			{
			case MaterialParameterType::Scalar:
				result["default"] = parameter_desc.default_scalar;
				break;
			case MaterialParameterType::Vector4:
				result["default"] = to_json_vec4(parameter_desc.default_vector);
				break;
			case MaterialParameterType::Texture:
				if (!parameter_desc.default_texture.texture_path.empty() || !parameter_desc.default_texture.sampler_name.empty())
				{
					result["default"] = material_texture_binding_to_json(parameter_desc.default_texture);
				}
				break;
			default:
				break;
			}

			return result;
		}

		static auto fixed_pbr_surface_inputs_to_json(const MaterialFixedPBRSurfaceInputs& inputs) -> json
		{
			return json
			{
				{ "base_color_factor", inputs.base_color_factor },
				{ "base_color_texture", inputs.base_color_texture },
				{ "normal_texture", inputs.normal_texture },
				{ "metallic_factor", inputs.metallic_factor },
				{ "roughness_factor", inputs.roughness_factor },
				{ "metallic_roughness_texture", inputs.metallic_roughness_texture },
				{ "emissive_factor", inputs.emissive_factor },
				{ "emissive_texture", inputs.emissive_texture },
				{ "opacity_mask", inputs.opacity_mask },
			};
		}

		static auto parse_material_parameter_desc(
			const json& root,
			MaterialParameterDesc& out_desc,
			std::vector<MaterialSamplerDefinition>& inout_sampler_definitions,
			std::vector<LegacyMaterialSamplerCacheEntry>& legacy_sampler_cache,
			std::string* out_error) -> bool
		{
			if (!root.is_object())
			{
				return make_error(out_error, "Material parameter entry must be an object.");
			}

			out_desc = MaterialParameterDesc{};
			out_desc.name = root.value("name", std::string{});
			if (out_desc.name.empty())
			{
				return make_error(out_error, "Material parameter is missing a name.");
			}

			if (!try_parse_material_parameter_type(root.value("type", std::string("Scalar")), out_desc.type))
			{
				return make_error(out_error, "Material parameter uses an unsupported type.");
			}

			out_desc.group = root.value("group", std::string{});
			out_desc.semantic = root.value("semantic", std::string{});
			out_desc.texture_usage = root.value("texture_usage", std::string{});

			if (root.contains("default"))
			{
				const json& default_value = root["default"];
				switch (out_desc.type)
				{
				case MaterialParameterType::Scalar:
					if (!default_value.is_number())
					{
						return make_error(out_error, "Scalar material parameter default must be numeric.");
					}
					out_desc.default_scalar = default_value.get<float>();
					break;
				case MaterialParameterType::Vector4:
					out_desc.default_vector = from_json_vec4(default_value, out_desc.default_vector);
					break;
				case MaterialParameterType::Texture:
				{
					ParsedMaterialTextureBinding parsed_binding{};
					if (!parse_material_texture_binding(default_value, parsed_binding, out_error))
					{
						return false;
					}
					resolve_legacy_sampler_binding(parsed_binding, inout_sampler_definitions, legacy_sampler_cache);
					out_desc.default_texture = std::move(parsed_binding.binding);
					break;
				}
				default:
					break;
				}
			}

			clear_error(out_error);
			return true;
		}

		static auto parse_fixed_pbr_surface_inputs(const json& root, MaterialFixedPBRSurfaceInputs& out_inputs) -> bool
		{
			if (!root.is_object())
			{
				return false;
			}

			out_inputs.base_color_factor = root.value("base_color_factor", out_inputs.base_color_factor);
			out_inputs.base_color_texture = root.value("base_color_texture", out_inputs.base_color_texture);
			out_inputs.normal_texture = root.value("normal_texture", out_inputs.normal_texture);
			out_inputs.metallic_factor = root.value("metallic_factor", out_inputs.metallic_factor);
			out_inputs.roughness_factor = root.value("roughness_factor", out_inputs.roughness_factor);
			out_inputs.metallic_roughness_texture = root.value("metallic_roughness_texture", out_inputs.metallic_roughness_texture);
			out_inputs.emissive_factor = root.value("emissive_factor", out_inputs.emissive_factor);
			out_inputs.emissive_texture = root.value("emissive_texture", out_inputs.emissive_texture);
			out_inputs.opacity_mask = root.value("opacity_mask", out_inputs.opacity_mask);
			return true;
		}

		static auto parse_material_root(const json& root, Material& out_material, std::string* out_error) -> bool
		{
			if (!root.is_object())
			{
				return make_error(out_error, "Material file root must be a JSON object.");
			}

			const uint32_t version = root.value("version", 0u);
			if (version != k_material_file_version)
			{
				return make_error(out_error, "Unsupported material file version.");
			}

			const std::string class_name = root.value("class", std::string{});
			if (class_name != "Material")
			{
				return make_error(out_error, "Material file class must be 'Material'.");
			}

			Material material{};
			material.set_name(root.value("name", std::string("Material")));

			MaterialDomain domain = MaterialDomain::Surface;
			if (!try_parse_material_domain(root, domain))
			{
				return make_error(out_error, "Material uses an unsupported domain.");
			}
			material.set_domain(domain);

			MaterialBlendMode blend_mode = MaterialBlendMode::Opaque;
			if (!try_parse_material_blend_mode(root, blend_mode))
			{
				return make_error(out_error, "Material uses an unsupported blend mode.");
			}
			material.set_blend_mode(blend_mode);

			MaterialShadingModel shading_model = MaterialShadingModel::DefaultLit;
			if (!try_parse_material_shading_model(root, shading_model))
			{
				return make_error(out_error, "Material uses an unsupported shading model.");
			}
			material.set_shading_model(shading_model);

			material.set_two_sided(root.value("two_sided", false));
			material.set_alpha_cutoff(root.value("alpha_cutoff", 0.5f));

			std::vector<MaterialSamplerDefinition> sampler_definitions{};
			if (!parse_material_sampler_definitions(root, sampler_definitions, out_error))
			{
				return false;
			}
			std::vector<LegacyMaterialSamplerCacheEntry> legacy_sampler_cache{};

			std::vector<MaterialParameterDesc> parameter_descs{};
			if (root.contains("parameters"))
			{
				const json& parameters_json = root["parameters"];
				if (!parameters_json.is_array())
				{
					return make_error(out_error, "Material parameters must be an array.");
				}

				parameter_descs.reserve(parameters_json.size());
				for (const json& parameter_json : parameters_json)
				{
					MaterialParameterDesc parameter_desc{};
					if (!parse_material_parameter_desc(
						parameter_json,
						parameter_desc,
						sampler_definitions,
						legacy_sampler_cache,
						out_error))
					{
						return false;
					}
					parameter_descs.push_back(std::move(parameter_desc));
				}
			}
			material.set_sampler_definitions(std::move(sampler_definitions));
			material.set_parameter_descs(std::move(parameter_descs));

			MaterialFixedPBRSurfaceInputs pbr_inputs{};
			if (root.contains("fixed_pbr_surface") && !parse_fixed_pbr_surface_inputs(root["fixed_pbr_surface"], pbr_inputs))
			{
				return make_error(out_error, "Material fixed_pbr_surface must be an object.");
			}
			material.set_fixed_pbr_surface_inputs(std::move(pbr_inputs));
			material.reset_change_version();

			out_material = std::move(material);
			clear_error(out_error);
			return true;
		}

		static auto parse_material_instance_root(const json& root, MaterialInstance& out_material_instance, std::string* out_error) -> bool
		{
			if (!root.is_object())
			{
				return make_error(out_error, "Material instance file root must be a JSON object.");
			}

			const uint32_t version = root.value("version", 0u);
			if (version != k_material_file_version)
			{
				return make_error(out_error, "Unsupported material file version.");
			}

			const std::string class_name = root.value("class", std::string{});
			if (class_name != "MaterialInstance")
			{
				return make_error(out_error, "Material file class must be 'MaterialInstance'.");
			}

			MaterialInstance material_instance{};
			material_instance.set_name(root.value("name", std::string("MaterialInstance")));
			material_instance.set_parent_asset_path(root.value("parent", std::string{}));

			std::vector<MaterialSamplerDefinition> sampler_definitions{};
			if (!parse_material_sampler_definitions(root, sampler_definitions, out_error))
			{
				return false;
			}
			std::vector<LegacyMaterialSamplerCacheEntry> legacy_sampler_cache{};

			if (root.contains("overrides"))
			{
				const json& overrides_json = root["overrides"];
				if (!overrides_json.is_object())
				{
					return make_error(out_error, "MaterialInstance overrides must be a JSON object.");
				}

				for (auto it = overrides_json.begin(); it != overrides_json.end(); ++it)
				{
					if (it.value().is_number())
					{
						material_instance.set_scalar_override(it.key(), it.value().get<float>());
						continue;
					}
					if (it.value().is_array() && (it.value().size() == 3 || it.value().size() == 4))
					{
						material_instance.set_vector_override(it.key(), from_json_vec4(it.value(), glm::vec4(0.0f)));
						continue;
					}

					ParsedMaterialTextureBinding texture_override{};
					if (!parse_material_texture_binding(it.value(), texture_override, out_error))
					{
						return false;
					}
					resolve_legacy_sampler_binding(texture_override, sampler_definitions, legacy_sampler_cache);
					material_instance.set_texture_override(it.key(), std::move(texture_override.binding));
				}
			}
			material_instance.set_sampler_definitions(std::move(sampler_definitions));

			material_instance.reset_change_version();
			out_material_instance = std::move(material_instance);
			clear_error(out_error);
			return true;
		}

		static auto build_surface_pbr_material() -> std::shared_ptr<MaterialInterface>
		{
			auto material = std::make_shared<Material>();
			material->set_name("M_SurfacePBR");
			material->set_asset_path(k_builtin_surface_pbr_material_path);
			material->set_domain(MaterialDomain::Surface);
			material->set_blend_mode(MaterialBlendMode::Opaque);
			material->set_shading_model(MaterialShadingModel::DefaultLit);
			material->set_two_sided(false);
			material->set_alpha_cutoff(0.5f);
			material->set_parameter_descs({
				MaterialParameterDesc{ "BaseColorFactor", MaterialParameterType::Vector4, "Surface", "BaseColorFactor", {}, glm::vec4(1.0f), 0.0f, {} },
				MaterialParameterDesc{ "BaseColorTexture", MaterialParameterType::Texture, "Surface", "BaseColorTexture", "Color", {}, 0.0f, MaterialTextureBinding{} },
				MaterialParameterDesc{ "NormalTexture", MaterialParameterType::Texture, "Surface", "NormalTexture", "Normal", {}, 0.0f, MaterialTextureBinding{} },
				MaterialParameterDesc{ "Metallic", MaterialParameterType::Scalar, "Surface", "Metallic", {}, {}, 0.0f, {} },
				MaterialParameterDesc{ "Roughness", MaterialParameterType::Scalar, "Surface", "Roughness", {}, {}, 1.0f, {} },
				MaterialParameterDesc{ "MetallicRoughnessTexture", MaterialParameterType::Texture, "Surface", "MetallicRoughnessTexture", "Linear", {}, 0.0f, MaterialTextureBinding{} },
				MaterialParameterDesc{ "EmissiveColor", MaterialParameterType::Vector4, "Surface", "EmissiveColor", {}, glm::vec4(0.0f), 0.0f, {} },
				MaterialParameterDesc{ "EmissiveTexture", MaterialParameterType::Texture, "Surface", "EmissiveTexture", "Color", {}, 0.0f, MaterialTextureBinding{} },
				MaterialParameterDesc{ "OpacityMask", MaterialParameterType::Scalar, "Surface", "OpacityMask", {}, {}, 1.0f, {} },
			});
			material->set_fixed_pbr_surface_inputs(MaterialFixedPBRSurfaceInputs{});
			material->reset_change_version();
			return material;
		}

		static auto build_default_surface_material() -> std::shared_ptr<MaterialInterface>
		{
			auto material_instance = std::make_shared<MaterialInstance>();
			material_instance->set_name("M_DefaultSurface");
			material_instance->set_asset_path(k_builtin_default_surface_material_path);
			material_instance->set_parent_asset_path(k_builtin_surface_pbr_material_path);
			material_instance->set_parent(make_builtin_material(k_builtin_surface_pbr_material_path));
			material_instance->set_vector_override("BaseColorFactor", glm::vec4(1.0f));
			material_instance->set_scalar_override("Metallic", 0.0f);
			material_instance->set_scalar_override("Roughness", 1.0f);
			material_instance->set_vector_override("EmissiveColor", glm::vec4(0.0f));
			material_instance->set_scalar_override("OpacityMask", 1.0f);
			material_instance->reset_change_version();
			return material_instance;
		}
	}

	bool Material::is_material_instance() const
	{
		return false;
	}

	const std::string& Material::get_name() const
	{
		return m_name;
	}

	const std::filesystem::path& Material::get_asset_path() const
	{
		return m_asset_path;
	}

	MaterialDomain Material::get_domain() const
	{
		return m_domain;
	}

	MaterialBlendMode Material::get_blend_mode() const
	{
		return m_blend_mode;
	}

	MaterialShadingModel Material::get_shading_model() const
	{
		return m_shading_model;
	}

	bool Material::is_two_sided() const
	{
		return m_two_sided;
	}

	float Material::get_alpha_cutoff() const
	{
		return m_alpha_cutoff;
	}

	uint64_t Material::get_change_version() const
	{
		return m_change_version;
	}

	const Material* Material::resolve_base_material() const
	{
		return this;
	}

	const std::vector<MaterialParameterDesc>& Material::get_parameter_descs() const
	{
		return m_parameter_descs;
	}

	const std::vector<MaterialSamplerDefinition>& Material::get_sampler_definitions() const
	{
		return m_sampler_definitions;
	}

	const MaterialFixedPBRSurfaceInputs* Material::get_fixed_pbr_surface_inputs() const
	{
		return &m_fixed_pbr_surface_inputs;
	}

	bool Material::try_get_scalar_parameter(const std::string& name, float& out_value) const
	{
		const MaterialParameterDesc* parameter_desc = find_parameter_desc(name);
		if (!parameter_desc || parameter_desc->type != MaterialParameterType::Scalar)
		{
			return false;
		}
		out_value = parameter_desc->default_scalar;
		return true;
	}

	bool Material::try_get_vector_parameter(const std::string& name, glm::vec4& out_value) const
	{
		const MaterialParameterDesc* parameter_desc = find_parameter_desc(name);
		if (!parameter_desc || parameter_desc->type != MaterialParameterType::Vector4)
		{
			return false;
		}
		out_value = parameter_desc->default_vector;
		return true;
	}

	bool Material::try_get_texture_parameter(const std::string& name, MaterialTextureBinding& out_binding) const
	{
		const MaterialParameterDesc* parameter_desc = find_parameter_desc(name);
		if (!parameter_desc || parameter_desc->type != MaterialParameterType::Texture)
		{
			return false;
		}
		out_binding = parameter_desc->default_texture;
		return true;
	}

	void Material::set_name(std::string name)
	{
		m_name = std::move(name);
		mark_changed();
	}

	void Material::set_asset_path(std::filesystem::path asset_path)
	{
		m_asset_path = std::move(asset_path);
		mark_changed();
	}

	void Material::set_domain(MaterialDomain domain)
	{
		m_domain = domain;
		mark_changed();
	}

	void Material::set_blend_mode(MaterialBlendMode blend_mode)
	{
		m_blend_mode = blend_mode;
		mark_changed();
	}

	void Material::set_shading_model(MaterialShadingModel shading_model)
	{
		m_shading_model = shading_model;
		mark_changed();
	}

	void Material::set_two_sided(bool two_sided)
	{
		m_two_sided = two_sided;
		mark_changed();
	}

	void Material::set_alpha_cutoff(float alpha_cutoff)
	{
		m_alpha_cutoff = alpha_cutoff;
		mark_changed();
	}

	void Material::set_parameter_descs(std::vector<MaterialParameterDesc> parameter_descs)
	{
		m_parameter_descs = std::move(parameter_descs);
		mark_changed();
	}

	void Material::add_parameter_desc(MaterialParameterDesc parameter_desc)
	{
		m_parameter_descs.push_back(std::move(parameter_desc));
		mark_changed();
	}

	void Material::set_sampler_definitions(std::vector<MaterialSamplerDefinition> sampler_definitions)
	{
		m_sampler_definitions = std::move(sampler_definitions);
		mark_changed();
	}

	void Material::add_sampler_definition(MaterialSamplerDefinition sampler_definition)
	{
		m_sampler_definitions.push_back(std::move(sampler_definition));
		mark_changed();
	}

	void Material::set_fixed_pbr_surface_inputs(MaterialFixedPBRSurfaceInputs inputs)
	{
		m_fixed_pbr_surface_inputs = std::move(inputs);
		mark_changed();
	}

	void Material::reset_change_version(uint64_t version)
	{
		m_change_version = version == 0 ? 1 : version;
	}

	void Material::mark_changed()
	{
		++m_change_version;
	}

	const MaterialParameterDesc* Material::find_parameter_desc(const std::string& name) const
	{
		for (const MaterialParameterDesc& parameter_desc : m_parameter_descs)
		{
			if (parameter_desc.name == name)
			{
				return &parameter_desc;
			}
		}
		return nullptr;
	}

	const MaterialSamplerDefinition* Material::find_sampler_definition(const std::string& name) const
	{
		for (const MaterialSamplerDefinition& sampler_definition : m_sampler_definitions)
		{
			if (sampler_definition.name == name)
			{
				return &sampler_definition;
			}
		}
		return nullptr;
	}

	bool MaterialInstance::is_material_instance() const
	{
		return true;
	}

	const std::string& MaterialInstance::get_name() const
	{
		return m_name;
	}

	const std::filesystem::path& MaterialInstance::get_asset_path() const
	{
		return m_asset_path;
	}

	MaterialDomain MaterialInstance::get_domain() const
	{
		return m_parent ? m_parent->get_domain() : MaterialDomain::Surface;
	}

	MaterialBlendMode MaterialInstance::get_blend_mode() const
	{
		return m_parent ? m_parent->get_blend_mode() : MaterialBlendMode::Opaque;
	}

	MaterialShadingModel MaterialInstance::get_shading_model() const
	{
		return m_parent ? m_parent->get_shading_model() : MaterialShadingModel::DefaultLit;
	}

	bool MaterialInstance::is_two_sided() const
	{
		return m_parent ? m_parent->is_two_sided() : false;
	}

	float MaterialInstance::get_alpha_cutoff() const
	{
		return m_parent ? m_parent->get_alpha_cutoff() : 0.5f;
	}

	uint64_t MaterialInstance::get_change_version() const
	{
		return mix_versions(m_change_version, m_parent ? m_parent->get_change_version() : 0ull);
	}

	const Material* MaterialInstance::resolve_base_material() const
	{
		return m_parent ? m_parent->resolve_base_material() : nullptr;
	}

	const std::vector<MaterialParameterDesc>& MaterialInstance::get_parameter_descs() const
	{
		static const std::vector<MaterialParameterDesc> k_empty_descs{};
		return m_parent ? m_parent->get_parameter_descs() : k_empty_descs;
	}

	const std::vector<MaterialSamplerDefinition>& MaterialInstance::get_sampler_definitions() const
	{
		if (!m_sampler_definitions.empty())
		{
			return m_sampler_definitions;
		}

		static const std::vector<MaterialSamplerDefinition> k_empty_definitions{};
		return m_parent ? m_parent->get_sampler_definitions() : k_empty_definitions;
	}

	const MaterialFixedPBRSurfaceInputs* MaterialInstance::get_fixed_pbr_surface_inputs() const
	{
		return m_parent ? m_parent->get_fixed_pbr_surface_inputs() : nullptr;
	}

	bool MaterialInstance::try_get_scalar_parameter(const std::string& name, float& out_value) const
	{
		const auto found = m_scalar_overrides.find(name);
		if (found != m_scalar_overrides.end())
		{
			out_value = found->second;
			return true;
		}
		return m_parent ? m_parent->try_get_scalar_parameter(name, out_value) : false;
	}

	bool MaterialInstance::try_get_vector_parameter(const std::string& name, glm::vec4& out_value) const
	{
		const auto found = m_vector_overrides.find(name);
		if (found != m_vector_overrides.end())
		{
			out_value = found->second;
			return true;
		}
		return m_parent ? m_parent->try_get_vector_parameter(name, out_value) : false;
	}

	bool MaterialInstance::try_get_texture_parameter(const std::string& name, MaterialTextureBinding& out_binding) const
	{
		const auto found = m_texture_overrides.find(name);
		if (found != m_texture_overrides.end())
		{
			out_binding = found->second;
			return true;
		}
		return m_parent ? m_parent->try_get_texture_parameter(name, out_binding) : false;
	}

	void MaterialInstance::set_name(std::string name)
	{
		m_name = std::move(name);
		mark_changed();
	}

	void MaterialInstance::set_asset_path(std::filesystem::path asset_path)
	{
		m_asset_path = std::move(asset_path);
		mark_changed();
	}

	void MaterialInstance::set_parent(std::shared_ptr<const MaterialInterface> parent)
	{
		m_parent = std::move(parent);
		mark_changed();
	}

	void MaterialInstance::clear_parent()
	{
		m_parent.reset();
		mark_changed();
	}

	const std::shared_ptr<const MaterialInterface>& MaterialInstance::get_parent() const
	{
		return m_parent;
	}

	void MaterialInstance::set_parent_asset_path(std::filesystem::path parent_asset_path)
	{
		m_parent_asset_path = std::move(parent_asset_path);
		mark_changed();
	}

	const std::filesystem::path& MaterialInstance::get_parent_asset_path() const
	{
		return m_parent_asset_path;
	}

	void MaterialInstance::clear_overrides()
	{
		m_scalar_overrides.clear();
		m_vector_overrides.clear();
		m_texture_overrides.clear();
		mark_changed();
	}

	void MaterialInstance::set_sampler_definitions(std::vector<MaterialSamplerDefinition> sampler_definitions)
	{
		m_sampler_definitions = std::move(sampler_definitions);
		mark_changed();
	}

	void MaterialInstance::add_sampler_definition(MaterialSamplerDefinition sampler_definition)
	{
		m_sampler_definitions.push_back(std::move(sampler_definition));
		mark_changed();
	}

	void MaterialInstance::clear_sampler_definitions()
	{
		m_sampler_definitions.clear();
		mark_changed();
	}

	const std::vector<MaterialSamplerDefinition>& MaterialInstance::get_local_sampler_definitions() const
	{
		return m_sampler_definitions;
	}

	void MaterialInstance::set_scalar_override(const std::string& name, float value)
	{
		m_scalar_overrides[name] = value;
		mark_changed();
	}

	void MaterialInstance::set_vector_override(const std::string& name, const glm::vec4& value)
	{
		m_vector_overrides[name] = value;
		mark_changed();
	}

	void MaterialInstance::set_texture_override(const std::string& name, MaterialTextureBinding value)
	{
		m_texture_overrides[name] = std::move(value);
		mark_changed();
	}

	void MaterialInstance::remove_override(const std::string& name)
	{
		m_scalar_overrides.erase(name);
		m_vector_overrides.erase(name);
		m_texture_overrides.erase(name);
		mark_changed();
	}

	const std::unordered_map<std::string, float>& MaterialInstance::get_scalar_overrides() const
	{
		return m_scalar_overrides;
	}

	const std::unordered_map<std::string, glm::vec4>& MaterialInstance::get_vector_overrides() const
	{
		return m_vector_overrides;
	}

	const std::unordered_map<std::string, MaterialTextureBinding>& MaterialInstance::get_texture_overrides() const
	{
		return m_texture_overrides;
	}

	void MaterialInstance::reset_change_version(uint64_t version)
	{
		m_change_version = version == 0 ? 1 : version;
	}

	void MaterialInstance::mark_changed()
	{
		++m_change_version;
	}

	const MaterialParameterDesc* find_material_parameter_desc(const MaterialInterface& material, const std::string& name)
	{
		for (const MaterialParameterDesc& parameter_desc : material.get_parameter_descs())
		{
			if (parameter_desc.name == name)
			{
				return &parameter_desc;
			}
		}
		return nullptr;
	}

	const MaterialSamplerDefinition* find_material_sampler_definition(const MaterialInterface& material, const std::string& name)
	{
		for (const MaterialSamplerDefinition& sampler_definition : material.get_sampler_definitions())
		{
			if (sampler_definition.name == name)
			{
				return &sampler_definition;
			}
		}
		return nullptr;
	}

	bool load_material_from_file(const std::filesystem::path& path, std::shared_ptr<MaterialInterface>& out_material, std::string* out_error)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);

		std::ifstream input(path, std::ios::binary);
		if (!input.is_open())
		{
			bResult = make_error(out_error, "Failed to open material file.");
			break;
		}

		json root{};
		try
		{
			input >> root;
		}
		catch (const std::exception& exception)
		{
			bResult = make_error(out_error, exception.what());
			break;
		}

		const std::string class_name = root.value("class", std::string{});
		if (class_name == "Material")
		{
			auto material = std::make_shared<Material>();
			if (!parse_material_root(root, *material, out_error))
			{
				bResult = false;
				break;
			}
			material->set_asset_path(path.lexically_normal());
			material->reset_change_version();
			out_material = std::move(material);
		}
		else if (class_name == "MaterialInstance")
		{
			auto material_instance = std::make_shared<MaterialInstance>();
			if (!parse_material_instance_root(root, *material_instance, out_error))
			{
				bResult = false;
				break;
			}
			material_instance->set_asset_path(path.lexically_normal());
			material_instance->reset_change_version();
			out_material = std::move(material_instance);
		}
		else
		{
			bResult = make_error(out_error, "Material file class is missing or unsupported.");
			break;
		}

		clear_error(out_error);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool save_material_to_file(const MaterialInterface& material, const std::filesystem::path& path, std::string* out_error)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);

		std::error_code create_error{};
		if (!path.parent_path().empty())
		{
			std::filesystem::create_directories(path.parent_path(), create_error);
			if (create_error)
			{
				bResult = make_error(out_error, create_error.message());
				break;
			}
		}

		json root{};
		root["version"] = k_material_file_version;
		root["name"] = material.get_name();

		if (material.is_material_instance())
		{
			const MaterialInstance& material_instance = static_cast<const MaterialInstance&>(material);
			root["class"] = "MaterialInstance";
			if (!material_instance.get_parent_asset_path().empty())
			{
				root["parent"] = material_instance.get_parent_asset_path().generic_string();
			}
			else if (material_instance.get_parent() && !material_instance.get_parent()->get_asset_path().empty())
			{
				root["parent"] = material_instance.get_parent()->get_asset_path().generic_string();
			}
			if (!material_instance.get_local_sampler_definitions().empty())
			{
				root["samplers"] = make_material_sampler_definitions_to_json(material_instance.get_local_sampler_definitions());
			}

			json overrides_json = json::object();
			for (const auto& [name, value] : material_instance.get_scalar_overrides())
			{
				overrides_json[name] = value;
			}
			for (const auto& [name, value] : material_instance.get_vector_overrides())
			{
				overrides_json[name] = to_json_vec4(value);
			}
			for (const auto& [name, value] : material_instance.get_texture_overrides())
			{
				overrides_json[name] = material_texture_binding_to_json(value);
			}
			root["overrides"] = std::move(overrides_json);
		}
		else
		{
			const Material& base_material = static_cast<const Material&>(material);
			root["class"] = "Material";
			root["domain"] = material_domain_to_string(base_material.get_domain());
			root["blend_mode"] = material_blend_mode_to_string(base_material.get_blend_mode());
			root["shading_model"] = material_shading_model_to_string(base_material.get_shading_model());
			root["two_sided"] = base_material.is_two_sided();
			root["alpha_cutoff"] = base_material.get_alpha_cutoff();
			if (!base_material.get_sampler_definitions().empty())
			{
				root["samplers"] = make_material_sampler_definitions_to_json(base_material.get_sampler_definitions());
			}
			root["parameters"] = json::array();
			for (const MaterialParameterDesc& parameter_desc : base_material.get_parameter_descs())
			{
				root["parameters"].push_back(parameter_desc_to_json(parameter_desc));
			}

			const MaterialFixedPBRSurfaceInputs* pbr_inputs = base_material.get_fixed_pbr_surface_inputs();
			if (pbr_inputs)
			{
				root["fixed_pbr_surface"] = fixed_pbr_surface_inputs_to_json(*pbr_inputs);
			}
		}

		std::ofstream output(path, std::ios::binary);
		if (!output.is_open())
		{
			bResult = make_error(out_error, "Failed to open material output file.");
			break;
		}

		try
		{
			output << root.dump(2);
		}
		catch (const std::exception& exception)
		{
			bResult = make_error(out_error, exception.what());
			break;
		}

		clear_error(out_error);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	std::shared_ptr<MaterialInterface> make_builtin_material(std::string_view virtual_path)
	{
		const std::string canonical_path = canonical_builtin_material_path(virtual_path);
		if (canonical_path.empty())
		{
			return nullptr;
		}

		if (canonical_path == k_builtin_surface_pbr_material_path)
		{
			static std::shared_ptr<MaterialInterface> surface_material = build_surface_pbr_material();
			return surface_material;
		}

		if (canonical_path == k_builtin_default_surface_material_path)
		{
			static std::shared_ptr<MaterialInterface> default_surface_material = build_default_surface_material();
			return default_surface_material;
		}

		return nullptr;
	}
}
