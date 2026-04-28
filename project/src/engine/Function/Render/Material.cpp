#include "Function/Render/Material.h"

#include "Function/Render/RenderDevice.h"
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

		constexpr uint32_t k_material_file_version_v2 = 2;

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

		enum class MaterialFileKind : uint8_t
		{
			Unknown = 0,
			Material,
			MaterialInstance
		};

		static auto classify_material_file_path(const std::filesystem::path& path) -> MaterialFileKind
		{
			const std::string extension = to_lower_copy(path.extension().string());
			if (extension == ".ashmat")
			{
				return MaterialFileKind::Material;
			}
			if (extension == ".ashmatins")
			{
				return MaterialFileKind::MaterialInstance;
			}
			return MaterialFileKind::Unknown;
		}

		static auto validate_material_file_kind(
			const std::filesystem::path& path,
			std::string_view class_name,
			std::string* out_error) -> bool
		{
			const MaterialFileKind file_kind = classify_material_file_path(path);
			if (file_kind == MaterialFileKind::Material && class_name == "Material")
			{
				clear_error(out_error);
				return true;
			}

			if (file_kind == MaterialFileKind::MaterialInstance && class_name == "MaterialInstance")
			{
				clear_error(out_error);
				return true;
			}

			return make_error(out_error, "Material file suffix does not match JSON class.");
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

		static auto material_parameter_type_to_v2_string(MaterialParameterType type) -> const char*
		{
			switch (type)
			{
			case MaterialParameterType::Scalar:
				return "float";
			case MaterialParameterType::Vector4:
				return "float4";
			default:
				return "float";
			}
		}

		static auto material_resource_type_to_string(MaterialResourceType type) -> const char*
		{
			switch (type)
			{
			case MaterialResourceType::Texture2D:
			default:
				return "Texture2D";
			}
		}

		static auto material_compare_op_to_string(MaterialCompareOp compare_op) -> const char*
		{
			switch (compare_op)
			{
			case MaterialCompareOp::LessEqual:
				return "LessEqual";
			case MaterialCompareOp::Always:
				return "Always";
			default:
				return "LessEqual";
			}
		}

		static auto material_resource_color_space_to_string(MaterialResourceColorSpace color_space) -> const char*
		{
			switch (color_space)
			{
			case MaterialResourceColorSpace::Linear:
				return "Linear";
			case MaterialResourceColorSpace::SRGB:
				return "sRGB";
			default:
				return "Linear";
			}
		}

		static auto render_cull_mode_to_string(RenderCullMode cull_mode) -> const char*
		{
			switch (cull_mode)
			{
			case RenderCullMode::None:
				return "None";
			case RenderCullMode::Front:
				return "Front";
			case RenderCullMode::Back:
				return "Back";
			default:
				return "Back";
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

		static auto try_parse_render_cull_mode(
			const json& value,
			RenderCullMode& out_cull_mode) -> bool
		{
			if (!value.is_string())
			{
				return false;
			}

			const std::string lowered = normalize_material_token(value.get<std::string>());
			if (lowered == "none")
			{
				out_cull_mode = RenderCullMode::None;
				return true;
			}
			if (lowered == "front")
			{
				out_cull_mode = RenderCullMode::Front;
				return true;
			}
			if (lowered == "back")
			{
				out_cull_mode = RenderCullMode::Back;
				return true;
			}
			return false;
		}

		static auto try_parse_material_compare_op(
			const json& value,
			MaterialCompareOp& out_compare_op) -> bool
		{
			if (!value.is_string())
			{
				return false;
			}

			const std::string lowered = normalize_material_token(value.get<std::string>());
			if (lowered == "lessequal")
			{
				out_compare_op = MaterialCompareOp::LessEqual;
				return true;
			}
			if (lowered == "always")
			{
				out_compare_op = MaterialCompareOp::Always;
				return true;
			}
			return false;
		}

		static auto try_parse_material_resource_type(
			const json& value,
			MaterialResourceType& out_type) -> bool
		{
			if (!value.is_string())
			{
				return false;
			}

			const std::string lowered = normalize_material_token(value.get<std::string>());
			if (lowered == "texture2d")
			{
				out_type = MaterialResourceType::Texture2D;
				return true;
			}
			return false;
		}

		static auto try_parse_material_resource_color_space(
			const json& value,
			MaterialResourceColorSpace& out_color_space) -> bool
		{
			if (!value.is_string())
			{
				return false;
			}

			const std::string lowered = normalize_material_token(value.get<std::string>());
			if (lowered == "linear")
			{
				out_color_space = MaterialResourceColorSpace::Linear;
				return true;
			}
			if (lowered == "srgb")
			{
				out_color_space = MaterialResourceColorSpace::SRGB;
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
				if (!sampler_it->is_string())
				{
					return make_error(out_error, "Material texture binding field 'sampler' must be a string.");
				}
				out_binding.binding.sampler_name = sampler_it->get<std::string>();
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
			const auto append_definition = [&](MaterialSamplerDefinition definition) -> bool
			{
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
				return true;
			};

			if (samplers_json.is_array())
			{
				out_definitions.reserve(samplers_json.size());
				for (const json& sampler_json : samplers_json)
				{
					MaterialSamplerDefinition definition{};
					if (!parse_material_sampler_definition(sampler_json, definition, out_error) ||
						!append_definition(std::move(definition)))
					{
						return false;
					}
				}
			}
			else if (samplers_json.is_object())
			{
				out_definitions.reserve(samplers_json.size());
				for (auto it = samplers_json.begin(); it != samplers_json.end(); ++it)
				{
					MaterialSamplerDefinition definition{};
					definition.name = it.key();
					definition.shader_sampler_name = it.key();
					if (!is_valid_shader_identifier(definition.shader_sampler_name))
					{
						return make_error(out_error, "Material sampler name must be a valid HLSL identifier.");
					}
					if (!parse_render_sampler_desc(it.value(), definition.desc, out_error) ||
						!append_definition(std::move(definition)))
					{
						return false;
					}
				}
			}
			else
			{
				return make_error(out_error, "Material samplers must be an array or object.");
			}

			clear_error(out_error);
			return true;
		}

		static auto make_material_sampler_definitions_to_json_v2(const std::vector<MaterialSamplerDefinition>& definitions) -> json
		{
			json samplers_json = json::object();
			for (const MaterialSamplerDefinition& definition : definitions)
			{
				samplers_json[definition.name] = render_sampler_desc_to_json(definition.desc);
			}
			return samplers_json;
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
			const std::string lowered = normalize_material_token(value);
			if (lowered == "scalar" || lowered == "float" || lowered == "float1")
			{
				out_type = MaterialParameterType::Scalar;
				return true;
			}
			if (lowered == "vector4" || lowered == "float4")
			{
				out_type = MaterialParameterType::Vector4;
				return true;
			}
			return false;
		}

		static auto parameter_desc_to_json_v2(const MaterialParameterDesc& parameter_desc) -> json
		{
			json result{};
			result["type"] = material_parameter_type_to_v2_string(parameter_desc.type);
			switch (parameter_desc.type)
			{
			case MaterialParameterType::Scalar:
				result["value"] = parameter_desc.default_scalar;
				break;
			case MaterialParameterType::Vector4:
				result["value"] = to_json_vec4(parameter_desc.default_vector);
				break;
			default:
				break;
			}
			return result;
		}

		static auto material_resource_desc_to_json_v2(const MaterialResourceDesc& resource_desc) -> json
		{
			json result = json::object();
			result["type"] = material_resource_type_to_string(resource_desc.type);
			if (!resource_desc.default_path.empty())
			{
				result["path"] = resource_desc.default_path;
			}
			if (!resource_desc.sampler.empty())
			{
				result["sampler"] = resource_desc.sampler;
			}
			result["colorSpace"] = material_resource_color_space_to_string(resource_desc.color_space);
			return result;
		}

		static auto material_static_render_state_to_json_v2(const MaterialStaticRenderStateDesc& render_state) -> json
		{
			return json
			{
				{ "blendMode", material_blend_mode_to_string(render_state.blend_mode) },
				{ "twoSided", render_state.two_sided },
				{ "cullMode", render_cull_mode_to_string(render_state.cull_mode) },
				{ "depthWrite", render_state.depth_write },
				{ "depthTest", material_compare_op_to_string(render_state.depth_test) },
				{ "alphaCutoff", render_state.alpha_cutoff },
			};
		}

		static auto parse_v2_required_capabilities(
			const json& root,
			std::vector<std::string>& out_required_capabilities,
			std::string* out_error) -> bool
		{
			out_required_capabilities.clear();
			const auto it = root.find("requiredCapabilities");
			if (it == root.end())
			{
				clear_error(out_error);
				return true;
			}
			if (!it->is_array())
			{
				return make_error(out_error, "Material requiredCapabilities must be an array.");
			}

			out_required_capabilities.reserve(it->size());
			for (const json& capability_json : *it)
			{
				if (!capability_json.is_string())
				{
					return make_error(out_error, "Material requiredCapabilities entries must be strings.");
				}
				out_required_capabilities.push_back(to_lower_copy(capability_json.get<std::string>()));
			}

			std::sort(out_required_capabilities.begin(), out_required_capabilities.end());
			out_required_capabilities.erase(
				std::unique(out_required_capabilities.begin(), out_required_capabilities.end()),
				out_required_capabilities.end());
			clear_error(out_error);
			return true;
		}

		static auto parse_v2_static_switches(
			const json& root,
			std::vector<MaterialStaticSwitchDesc>& out_static_switches,
			std::string* out_error) -> bool
		{
			out_static_switches.clear();
			const auto it = root.find("staticSwitches");
			if (it == root.end())
			{
				clear_error(out_error);
				return true;
			}
			if (!it->is_object())
			{
				return make_error(out_error, "Material staticSwitches must be an object.");
			}

			out_static_switches.reserve(it->size());
			for (auto switch_it = it->begin(); switch_it != it->end(); ++switch_it)
			{
				if (!switch_it.value().is_boolean())
				{
					return make_error(out_error, "Material staticSwitches values must be boolean.");
				}
				out_static_switches.push_back({
					switch_it.key(),
					switch_it.value().get<bool>()
				});
			}

			clear_error(out_error);
			return true;
		}

		static auto parse_v2_render_state(
			const json& root,
			MaterialStaticRenderStateDesc& out_render_state,
			std::string* out_error) -> bool
		{
			out_render_state = MaterialStaticRenderStateDesc{};
			const auto it = root.find("renderState");
			if (it == root.end())
			{
				clear_error(out_error);
				return true;
			}
			if (!it->is_object())
			{
				return make_error(out_error, "Material renderState must be an object.");
			}

			const json& render_state_json = *it;
			if (const auto blend_it = render_state_json.find("blendMode"); blend_it != render_state_json.end())
			{
				const std::string lowered = to_lower_copy(blend_it->get<std::string>());
				if (lowered == "opaque")
				{
					out_render_state.blend_mode = MaterialBlendMode::Opaque;
				}
				else if (lowered == "masked")
				{
					out_render_state.blend_mode = MaterialBlendMode::Masked;
				}
				else if (lowered == "transparent")
				{
					out_render_state.blend_mode = MaterialBlendMode::Transparent;
				}
				else
				{
					return make_error(out_error, "Material renderState blendMode is unsupported.");
				}
			}
			if (const auto two_sided_it = render_state_json.find("twoSided"); two_sided_it != render_state_json.end())
			{
				if (!two_sided_it->is_boolean())
				{
					return make_error(out_error, "Material renderState twoSided must be boolean.");
				}
				out_render_state.two_sided = two_sided_it->get<bool>();
			}
			if (const auto cull_it = render_state_json.find("cullMode"); cull_it != render_state_json.end())
			{
				if (!try_parse_render_cull_mode(*cull_it, out_render_state.cull_mode))
				{
					return make_error(out_error, "Material renderState cullMode is unsupported.");
				}
			}
			if (const auto depth_write_it = render_state_json.find("depthWrite"); depth_write_it != render_state_json.end())
			{
				if (!depth_write_it->is_boolean())
				{
					return make_error(out_error, "Material renderState depthWrite must be boolean.");
				}
				out_render_state.depth_write = depth_write_it->get<bool>();
			}
			if (const auto depth_test_it = render_state_json.find("depthTest"); depth_test_it != render_state_json.end())
			{
				if (!try_parse_material_compare_op(*depth_test_it, out_render_state.depth_test))
				{
					return make_error(out_error, "Material renderState depthTest is unsupported.");
				}
			}
			if (const auto alpha_cutoff_it = render_state_json.find("alphaCutoff"); alpha_cutoff_it != render_state_json.end())
			{
				if (!alpha_cutoff_it->is_number())
				{
					return make_error(out_error, "Material renderState alphaCutoff must be numeric.");
				}
				out_render_state.alpha_cutoff = alpha_cutoff_it->get<float>();
			}

			clear_error(out_error);
			return true;
		}

		static auto parse_v2_material_parameter_descs(
			const json& root,
			std::vector<MaterialParameterDesc>& out_parameter_descs,
			std::string* out_error) -> bool
		{
			out_parameter_descs.clear();
			const auto it = root.find("parameters");
			if (it == root.end())
			{
				clear_error(out_error);
				return true;
			}
			if (!it->is_object())
			{
				return make_error(out_error, "Material parameters must be an object.");
			}

			out_parameter_descs.reserve(it->size());
			for (auto parameter_it = it->begin(); parameter_it != it->end(); ++parameter_it)
			{
				if (!parameter_it.value().is_object())
				{
					return make_error(out_error, "Material parameter definition must be an object.");
				}

				MaterialParameterDesc parameter_desc{};
				parameter_desc.name = parameter_it.key();
				const std::string parameter_type = parameter_it.value().value("type", std::string("float"));
				if (!try_parse_material_parameter_type(parameter_type, parameter_desc.type))
				{
					return make_error(
						out_error,
						"V2 material parameters only support 'float' and 'float4'; declare textures under resources.");
				}

				const auto value_it = parameter_it.value().find("value");
				if (value_it != parameter_it.value().end())
				{
					switch (parameter_desc.type)
					{
					case MaterialParameterType::Scalar:
						if (!value_it->is_number())
						{
							return make_error(out_error, "Scalar material parameter value must be numeric.");
						}
						parameter_desc.default_scalar = value_it->get<float>();
						break;
					case MaterialParameterType::Vector4:
						parameter_desc.default_vector = from_json_vec4(*value_it, parameter_desc.default_vector);
						break;
					default:
						break;
					}
				}

				out_parameter_descs.push_back(std::move(parameter_desc));
			}

			clear_error(out_error);
			return true;
		}

		static auto parse_v2_material_resource_descs(
			const json& root,
			std::vector<MaterialResourceDesc>& out_resource_descs,
			std::string* out_error) -> bool
		{
			out_resource_descs.clear();
			const auto it = root.find("resources");
			if (it == root.end())
			{
				clear_error(out_error);
				return true;
			}
			if (!it->is_object())
			{
				return make_error(out_error, "Material resources must be an object.");
			}

			out_resource_descs.reserve(it->size());
			for (auto resource_it = it->begin(); resource_it != it->end(); ++resource_it)
			{
				if (!resource_it.value().is_object())
				{
					return make_error(out_error, "Material resource definition must be an object.");
				}

				MaterialResourceDesc resource_desc{};
				resource_desc.name = resource_it.key();
				if (!try_parse_material_resource_type(
					resource_it.value().value("type", std::string("Texture2D")),
					resource_desc.type))
				{
					return make_error(out_error, "Material resource uses an unsupported type.");
				}

				if (const auto path_it = resource_it.value().find("path"); path_it != resource_it.value().end())
				{
					if (!path_it->is_string())
					{
						return make_error(out_error, "Material resource path must be a string.");
					}
					resource_desc.default_path = path_it->get<std::string>();
				}
				if (const auto sampler_it = resource_it.value().find("sampler"); sampler_it != resource_it.value().end())
				{
					if (!sampler_it->is_string())
					{
						return make_error(out_error, "Material resource sampler must be a string.");
					}
					resource_desc.sampler = sampler_it->get<std::string>();
				}
				if (const auto color_space_it = resource_it.value().find("colorSpace"); color_space_it != resource_it.value().end())
				{
					if (!try_parse_material_resource_color_space(*color_space_it, resource_desc.color_space))
					{
						return make_error(out_error, "Material resource colorSpace is unsupported.");
					}
				}

				out_resource_descs.push_back(std::move(resource_desc));
			}

			clear_error(out_error);
			return true;
		}

		static auto parse_v2_material_instance_overrides(
			const json& root,
			MaterialInstance& out_material_instance,
			std::string* out_error) -> bool
		{
			const auto overrides_it = root.find("overrides");
			if (overrides_it == root.end())
			{
				clear_error(out_error);
				return true;
			}
			if (!overrides_it->is_object())
			{
				return make_error(out_error, "MaterialInstance overrides must be a JSON object.");
			}

			if (const auto parameters_it = overrides_it->find("parameters"); parameters_it != overrides_it->end())
			{
				if (!parameters_it->is_object())
				{
					return make_error(out_error, "MaterialInstance overrides.parameters must be an object.");
				}
				for (auto parameter_it = parameters_it->begin(); parameter_it != parameters_it->end(); ++parameter_it)
				{
					if (parameter_it.value().is_number())
					{
						out_material_instance.set_scalar_override(parameter_it.key(), parameter_it.value().get<float>());
					}
					else if (parameter_it.value().is_array() &&
						(parameter_it.value().size() == 3 || parameter_it.value().size() == 4))
					{
						out_material_instance.set_vector_override(
							parameter_it.key(),
							from_json_vec4(parameter_it.value(), glm::vec4(0.0f)));
					}
					else
					{
						return make_error(out_error, "MaterialInstance parameter overrides must be scalar or float4 values.");
					}
				}
			}

			if (const auto resources_it = overrides_it->find("resources"); resources_it != overrides_it->end())
			{
				if (!resources_it->is_object())
				{
					return make_error(out_error, "MaterialInstance overrides.resources must be an object.");
				}
				for (auto resource_it = resources_it->begin(); resource_it != resources_it->end(); ++resource_it)
				{
					ParsedMaterialTextureBinding parsed_binding{};
					if (!parse_material_texture_binding(resource_it.value(), parsed_binding, out_error))
					{
						return false;
					}
					out_material_instance.set_resource_override(resource_it.key(), std::move(parsed_binding.binding));
				}
			}

			clear_error(out_error);
			return true;
		}

		static auto build_material_compile_hash(const Material& material) -> uint64_t
		{
			uint64_t hash_value = 0;
			ASH_HASH::hash_combine(hash_value, material.get_domain());
			ASH_HASH::hash_combine(hash_value, material.get_material_shader_path().data(), ASH_HASH::CStringHash{});

			const MaterialStaticRenderStateDesc& render_state = material.get_static_render_state();
			ASH_HASH::hash_combine(hash_value, render_state.blend_mode);
			ASH_HASH::hash_combine(hash_value, render_state.two_sided);
			ASH_HASH::hash_combine(hash_value, render_state.cull_mode);
			ASH_HASH::hash_combine(hash_value, render_state.depth_write);
			ASH_HASH::hash_combine(hash_value, render_state.depth_test);
			ASH_HASH::hash_combine(hash_value, render_state.alpha_cutoff);

			std::vector<std::string> required_capabilities = material.get_required_capabilities();
			std::sort(required_capabilities.begin(), required_capabilities.end());
			for (const std::string& capability : required_capabilities)
			{
				ASH_HASH::hash_combine(hash_value, capability, std::hash<std::string>{});
			}

			std::vector<MaterialStaticSwitchDesc> static_switches = material.get_static_switches();
			std::sort(static_switches.begin(), static_switches.end(), [](const MaterialStaticSwitchDesc& lhs, const MaterialStaticSwitchDesc& rhs)
			{
				return lhs.name < rhs.name;
			});
			for (const MaterialStaticSwitchDesc& static_switch : static_switches)
			{
				ASH_HASH::hash_combine(hash_value, static_switch.name, std::hash<std::string>{});
				ASH_HASH::hash_combine(hash_value, static_switch.value);
			}

			std::vector<MaterialSamplerDefinition> sampler_definitions = material.get_sampler_definitions();
			std::sort(sampler_definitions.begin(), sampler_definitions.end(), [](const MaterialSamplerDefinition& lhs, const MaterialSamplerDefinition& rhs)
			{
				return lhs.name < rhs.name;
			});
			for (const MaterialSamplerDefinition& sampler_definition : sampler_definitions)
			{
				ASH_HASH::hash_combine(hash_value, sampler_definition.name, std::hash<std::string>{});
				ASH_HASH::hash_combine(hash_value, sampler_definition.shader_sampler_name, std::hash<std::string>{});
			}

			std::vector<MaterialParameterDesc> parameter_descs = material.get_parameter_descs();
			std::sort(parameter_descs.begin(), parameter_descs.end(), [](const MaterialParameterDesc& lhs, const MaterialParameterDesc& rhs)
			{
				return lhs.name < rhs.name;
			});
			for (const MaterialParameterDesc& parameter_desc : parameter_descs)
			{
				ASH_HASH::hash_combine(hash_value, parameter_desc.name, std::hash<std::string>{});
				ASH_HASH::hash_combine(hash_value, parameter_desc.type);
			}

			std::vector<MaterialResourceDesc> resource_descs = material.get_resource_descs();
			std::sort(resource_descs.begin(), resource_descs.end(), [](const MaterialResourceDesc& lhs, const MaterialResourceDesc& rhs)
			{
				return lhs.name < rhs.name;
			});
			for (const MaterialResourceDesc& resource_desc : resource_descs)
			{
				ASH_HASH::hash_combine(hash_value, resource_desc.name, std::hash<std::string>{});
				ASH_HASH::hash_combine(hash_value, resource_desc.type);
				ASH_HASH::hash_combine(hash_value, resource_desc.sampler, std::hash<std::string>{});
				ASH_HASH::hash_combine(hash_value, resource_desc.color_space);
			}

			return hash_value;
		}

		static auto parse_material_root_v2(const json& root, Material& out_material, std::string* out_error) -> bool
		{
			if (!root.is_object())
			{
				return make_error(out_error, "Material file root must be a JSON object.");
			}

			const uint32_t version = root.value("version", 0u);
			if (version != k_material_file_version_v2)
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

			material.set_material_shader_path(root.value("materialShader", std::string{}));

			MaterialStaticRenderStateDesc render_state{};
			if (!parse_v2_render_state(root, render_state, out_error))
			{
				return false;
			}
			material.set_static_render_state(render_state);

			std::vector<std::string> required_capabilities{};
			if (!parse_v2_required_capabilities(root, required_capabilities, out_error))
			{
				return false;
			}
			material.set_required_capabilities(std::move(required_capabilities));

			std::vector<MaterialStaticSwitchDesc> static_switches{};
			if (!parse_v2_static_switches(root, static_switches, out_error))
			{
				return false;
			}
			material.set_static_switches(std::move(static_switches));

			std::vector<MaterialSamplerDefinition> sampler_definitions{};
			if (!parse_material_sampler_definitions(root, sampler_definitions, out_error))
			{
				return false;
			}
			material.set_sampler_definitions(std::move(sampler_definitions));

			std::vector<MaterialParameterDesc> parameter_descs{};
			if (!parse_v2_material_parameter_descs(root, parameter_descs, out_error))
			{
				return false;
			}
			material.set_parameter_descs(std::move(parameter_descs));

			std::vector<MaterialResourceDesc> resource_descs{};
			if (!parse_v2_material_resource_descs(root, resource_descs, out_error))
			{
				return false;
			}
			material.set_resource_descs(std::move(resource_descs));
			material.reset_change_version();

			out_material = std::move(material);
			clear_error(out_error);
			return true;
		}

		static auto parse_material_instance_root_v2(const json& root, MaterialInstance& out_material_instance, std::string* out_error) -> bool
		{
			if (!root.is_object())
			{
				return make_error(out_error, "Material instance file root must be a JSON object.");
			}

			const uint32_t version = root.value("version", 0u);
			if (version != k_material_file_version_v2)
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
			material_instance.set_sampler_definitions(std::move(sampler_definitions));

			if (!parse_v2_material_instance_overrides(root, material_instance, out_error))
			{
				return false;
			}

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
			material->set_material_shader_path("project/src/engine/Shaders/MaterialV2/Materials/M_SurfacePBR.hlsl");
			material->set_static_render_state(MaterialStaticRenderStateDesc{});
			material->set_parameter_descs({
				MaterialParameterDesc{ "BaseColorFactor", MaterialParameterType::Vector4, {}, {}, {}, glm::vec4(1.0f), 0.0f },
				MaterialParameterDesc{ "Metallic", MaterialParameterType::Scalar, {}, {}, {}, {}, 0.0f },
				MaterialParameterDesc{ "Roughness", MaterialParameterType::Scalar, {}, {}, {}, {}, 1.0f },
				MaterialParameterDesc{ "EmissiveColor", MaterialParameterType::Vector4, {}, {}, {}, glm::vec4(0.0f), 0.0f },
			});
			material->set_sampler_definitions({
				MaterialSamplerDefinition{ "WrapLinear", "ASH_SurfacePBRSampler", RenderSamplerDesc{} },
			});
			material->set_resource_descs({
				MaterialResourceDesc{ "BaseColorTexture", MaterialResourceType::Texture2D, {}, "WrapLinear", MaterialResourceColorSpace::SRGB },
				MaterialResourceDesc{ "NormalTexture", MaterialResourceType::Texture2D, {}, "WrapLinear", MaterialResourceColorSpace::Linear },
				MaterialResourceDesc{ "MetallicRoughnessTexture", MaterialResourceType::Texture2D, {}, "WrapLinear", MaterialResourceColorSpace::Linear },
				MaterialResourceDesc{ "EmissiveTexture", MaterialResourceType::Texture2D, {}, "WrapLinear", MaterialResourceColorSpace::SRGB },
			});
			material->reset_change_version();
			return material;
		}

		static auto build_default_surface_material() -> std::shared_ptr<MaterialInterface>
		{
			auto material_instance = std::make_shared<MaterialInstance>();
			material_instance->set_name("MI_DefaultSurface");
			material_instance->set_asset_path(k_builtin_default_surface_material_path);
			material_instance->set_parent_asset_path(k_builtin_surface_pbr_material_path);
			material_instance->set_parent(make_builtin_material(k_builtin_surface_pbr_material_path));
			material_instance->reset_change_version();
			return material_instance;
		}
	}

	MaterialStaticRenderStateDesc::MaterialStaticRenderStateDesc()
		: cull_mode(RenderCullMode::Back)
	{
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

	std::string_view Material::get_material_shader_path() const
	{
		return m_material_shader_path;
	}

	const MaterialStaticRenderStateDesc& Material::get_static_render_state() const
	{
		return m_static_render_state;
	}

	const std::vector<std::string>& Material::get_required_capabilities() const
	{
		return m_required_capabilities;
	}

	const std::vector<MaterialStaticSwitchDesc>& Material::get_static_switches() const
	{
		return m_static_switches;
	}

	const std::vector<MaterialResourceDesc>& Material::get_resource_descs() const
	{
		return m_resource_descs;
	}

	uint64_t Material::get_compile_hash() const
	{
		return m_compile_hash;
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

	bool Material::try_get_resource_binding(const std::string& name, MaterialTextureBinding& out_binding) const
	{
		const MaterialResourceDesc* resource_desc = find_resource_desc(name);
		if (!resource_desc)
		{
			return false;
		}

		out_binding.texture_path = resource_desc->default_path;
		out_binding.sampler_name = resource_desc->sampler;
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
		m_static_render_state.blend_mode = blend_mode;
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
		m_static_render_state.two_sided = two_sided;
		mark_changed();
	}

	void Material::set_alpha_cutoff(float alpha_cutoff)
	{
		m_alpha_cutoff = alpha_cutoff;
		m_static_render_state.alpha_cutoff = alpha_cutoff;
		mark_changed();
	}

	void Material::set_material_shader_path(std::string material_shader_path)
	{
		m_material_shader_path = std::move(material_shader_path);
		mark_changed();
	}

	void Material::set_static_render_state(const MaterialStaticRenderStateDesc& static_render_state)
	{
		m_static_render_state = static_render_state;
		m_blend_mode = static_render_state.blend_mode;
		m_two_sided = static_render_state.two_sided;
		m_alpha_cutoff = static_render_state.alpha_cutoff;
		mark_changed();
	}

	void Material::set_required_capabilities(std::vector<std::string> required_capabilities)
	{
		m_required_capabilities = std::move(required_capabilities);
		mark_changed();
	}

	void Material::add_required_capability(std::string capability)
	{
		m_required_capabilities.push_back(std::move(capability));
		mark_changed();
	}

	void Material::set_static_switches(std::vector<MaterialStaticSwitchDesc> static_switches)
	{
		m_static_switches = std::move(static_switches);
		mark_changed();
	}

	void Material::add_static_switch(MaterialStaticSwitchDesc static_switch)
	{
		m_static_switches.push_back(std::move(static_switch));
		mark_changed();
	}

	void Material::set_resource_descs(std::vector<MaterialResourceDesc> resource_descs)
	{
		m_resource_descs = std::move(resource_descs);
		mark_changed();
	}

	void Material::add_resource_desc(MaterialResourceDesc resource_desc)
	{
		m_resource_descs.push_back(std::move(resource_desc));
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

	void Material::reset_change_version(uint64_t version)
	{
		m_compile_hash = build_material_compile_hash(*this);
		m_change_version = version == 0 ? 1 : version;
	}

	void Material::mark_changed()
	{
		m_compile_hash = build_material_compile_hash(*this);
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

	const MaterialResourceDesc* Material::find_resource_desc(const std::string& name) const
	{
		for (const MaterialResourceDesc& resource_desc : m_resource_descs)
		{
			if (resource_desc.name == name)
			{
				return &resource_desc;
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

	std::string_view MaterialInstance::get_material_shader_path() const
	{
		return m_parent ? m_parent->get_material_shader_path() : std::string_view{};
	}

	const MaterialStaticRenderStateDesc& MaterialInstance::get_static_render_state() const
	{
		static const MaterialStaticRenderStateDesc k_default_render_state{};
		return m_parent ? m_parent->get_static_render_state() : k_default_render_state;
	}

	const std::vector<std::string>& MaterialInstance::get_required_capabilities() const
	{
		static const std::vector<std::string> k_empty_capabilities{};
		return m_parent ? m_parent->get_required_capabilities() : k_empty_capabilities;
	}

	const std::vector<MaterialStaticSwitchDesc>& MaterialInstance::get_static_switches() const
	{
		static const std::vector<MaterialStaticSwitchDesc> k_empty_static_switches{};
		return m_parent ? m_parent->get_static_switches() : k_empty_static_switches;
	}

	const std::vector<MaterialResourceDesc>& MaterialInstance::get_resource_descs() const
	{
		static const std::vector<MaterialResourceDesc> k_empty_resource_descs{};
		return m_parent ? m_parent->get_resource_descs() : k_empty_resource_descs;
	}

	uint64_t MaterialInstance::get_compile_hash() const
	{
		return m_parent ? m_parent->get_compile_hash() : 0;
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
		if (m_sampler_definitions.empty())
		{
			static const std::vector<MaterialSamplerDefinition> k_empty_definitions{};
			return m_parent ? m_parent->get_sampler_definitions() : k_empty_definitions;
		}

		m_cached_merged_sampler_definitions.clear();
		if (m_parent)
		{
			m_cached_merged_sampler_definitions = m_parent->get_sampler_definitions();
		}

		for (const MaterialSamplerDefinition& local_definition : m_sampler_definitions)
		{
			const auto found = std::find_if(
				m_cached_merged_sampler_definitions.begin(),
				m_cached_merged_sampler_definitions.end(),
				[&local_definition](const MaterialSamplerDefinition& existing)
				{
					return existing.name == local_definition.name;
				});
			if (found != m_cached_merged_sampler_definitions.end())
			{
				*found = local_definition;
			}
			else
			{
				m_cached_merged_sampler_definitions.push_back(local_definition);
			}
		}

		return m_cached_merged_sampler_definitions;
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

	bool MaterialInstance::try_get_resource_binding(const std::string& name, MaterialTextureBinding& out_binding) const
	{
		const auto resource_override = m_resource_overrides.find(name);
		if (resource_override != m_resource_overrides.end())
		{
			out_binding = resource_override->second;
			return true;
		}
		return m_parent ? m_parent->try_get_resource_binding(name, out_binding) : false;
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
		m_cached_merged_sampler_definitions.clear();
		mark_changed();
	}

	void MaterialInstance::clear_parent()
	{
		m_parent.reset();
		m_cached_merged_sampler_definitions.clear();
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
		m_resource_overrides.clear();
		mark_changed();
	}

	void MaterialInstance::set_sampler_definitions(std::vector<MaterialSamplerDefinition> sampler_definitions)
	{
		m_sampler_definitions = std::move(sampler_definitions);
		m_cached_merged_sampler_definitions.clear();
		mark_changed();
	}

	void MaterialInstance::add_sampler_definition(MaterialSamplerDefinition sampler_definition)
	{
		m_sampler_definitions.push_back(std::move(sampler_definition));
		m_cached_merged_sampler_definitions.clear();
		mark_changed();
	}

	void MaterialInstance::clear_sampler_definitions()
	{
		m_sampler_definitions.clear();
		m_cached_merged_sampler_definitions.clear();
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

	void MaterialInstance::set_resource_override(const std::string& name, MaterialTextureBinding value)
	{
		m_resource_overrides[name] = std::move(value);
		mark_changed();
	}

	void MaterialInstance::remove_override(const std::string& name)
	{
		m_scalar_overrides.erase(name);
		m_vector_overrides.erase(name);
		m_resource_overrides.erase(name);
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

	const std::unordered_map<std::string, MaterialTextureBinding>& MaterialInstance::get_resource_overrides() const
	{
		return m_resource_overrides;
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

	const MaterialResourceDesc* find_material_resource_desc(const MaterialInterface& material, const std::string& name)
	{
		for (const MaterialResourceDesc& resource_desc : material.get_resource_descs())
		{
			if (resource_desc.name == name)
			{
				return &resource_desc;
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

		const uint32_t version = root.value("version", 0u);
		const std::string class_name = root.value("class", std::string{});
		ASH_PROCESS_ERROR(validate_material_file_kind(path, class_name, out_error));
		if (version != k_material_file_version_v2)
		{
			bResult = make_error(out_error, "Unsupported material file version.");
			break;
		}
		if (class_name == "Material")
		{
			auto material = std::make_shared<Material>();
			const bool parse_result = parse_material_root_v2(root, *material, out_error);
			if (!parse_result)
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
			const bool parse_result = parse_material_instance_root_v2(root, *material_instance, out_error);
			if (!parse_result)
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
		const MaterialFileKind file_kind = classify_material_file_path(path);
		if (!material.is_material_instance() && file_kind != MaterialFileKind::Material)
		{
			bResult = make_error(out_error, "Base material files must use .AshMat.");
			break;
		}
		if (material.is_material_instance() && file_kind != MaterialFileKind::MaterialInstance)
		{
			bResult = make_error(out_error, "MaterialInstance files must use .AshMatIns.");
			break;
		}

		root["name"] = material.get_name();
		root["version"] = k_material_file_version_v2;

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
				root["samplers"] = make_material_sampler_definitions_to_json_v2(material_instance.get_local_sampler_definitions());
			}

			json overrides_json = json::object();
			json parameter_overrides = json::object();
			for (const auto& [name, value] : material_instance.get_scalar_overrides())
			{
				parameter_overrides[name] = value;
			}
			for (const auto& [name, value] : material_instance.get_vector_overrides())
			{
				parameter_overrides[name] = to_json_vec4(value);
			}
			if (!parameter_overrides.empty())
			{
				overrides_json["parameters"] = std::move(parameter_overrides);
			}

			json resource_overrides = json::object();
			for (const auto& [name, value] : material_instance.get_resource_overrides())
			{
				resource_overrides[name] = material_texture_binding_to_json(value);
			}
			if (!resource_overrides.empty())
			{
				overrides_json["resources"] = std::move(resource_overrides);
			}
			root["overrides"] = std::move(overrides_json);
		}
		else
		{
			const Material& base_material = static_cast<const Material&>(material);
			root["class"] = "Material";
			root["domain"] = material_domain_to_string(base_material.get_domain());

			root["materialShader"] = std::string(base_material.get_material_shader_path());
			root["requiredCapabilities"] = base_material.get_required_capabilities();

			json static_switches = json::object();
			for (const MaterialStaticSwitchDesc& static_switch : base_material.get_static_switches())
			{
				static_switches[static_switch.name] = static_switch.value;
			}
			root["staticSwitches"] = std::move(static_switches);
			root["renderState"] = material_static_render_state_to_json_v2(base_material.get_static_render_state());

			if (!base_material.get_sampler_definitions().empty())
			{
				root["samplers"] = make_material_sampler_definitions_to_json_v2(base_material.get_sampler_definitions());
			}

			json parameters_json = json::object();
			for (const MaterialParameterDesc& parameter_desc : base_material.get_parameter_descs())
			{
				parameters_json[parameter_desc.name] = parameter_desc_to_json_v2(parameter_desc);
			}
			root["parameters"] = std::move(parameters_json);

			json resources_json = json::object();
			for (const MaterialResourceDesc& resource_desc : base_material.get_resource_descs())
			{
				resources_json[resource_desc.name] = material_resource_desc_to_json_v2(resource_desc);
			}
			root["resources"] = std::move(resources_json);
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
