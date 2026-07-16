#include "Function/Asset/VegetationSpecies.h"

#include "Function/Asset/VegetationAssetCodecInternal.h"

#include <json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace AshEngine
{
	namespace
	{
		using json = nlohmann::json;
		using ordered_json = nlohmann::ordered_json;
		using namespace VegetationAssetCodecInternal;

		class DuplicateKeySax final : public nlohmann::json_sax<json>
		{
		public:
			bool null() override { return true; }
			bool boolean(bool) override { return true; }
			bool number_integer(number_integer_t) override { return true; }
			bool number_unsigned(number_unsigned_t) override { return true; }
			bool number_float(number_float_t, const string_t&) override { return true; }
			bool string(string_t&) override { return true; }
			bool binary(binary_t&) override { return true; }
			bool start_object(std::size_t) override
			{
				m_contexts.push_back({ true, {} });
				return true;
			}
			bool key(string_t& value) override
			{
				if (m_contexts.empty() || !m_contexts.back().is_object ||
					!m_contexts.back().keys.insert(value).second)
				{
					m_duplicate = true;
				}
				return true;
			}
			bool end_object() override
			{
				if (m_contexts.empty()) return false;
				m_contexts.pop_back();
				return true;
			}
			bool start_array(std::size_t) override
			{
				m_contexts.push_back({ false, {} });
				return true;
			}
			bool end_array() override
			{
				if (m_contexts.empty()) return false;
				m_contexts.pop_back();
				return true;
			}
			bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) override
			{
				m_parse_error = true;
				return false;
			}
			bool valid() const { return !m_duplicate && !m_parse_error && m_contexts.empty(); }

		private:
			struct Context
			{
				bool is_object = false;
				std::set<std::string> keys{};
			};
			std::vector<Context> m_contexts{};
			bool m_duplicate = false;
			bool m_parse_error = false;
		};

		bool exact_keys(const json& value, const std::initializer_list<const char*> keys)
		{
			if (!value.is_object() || value.size() != keys.size()) return false;
			return std::all_of(keys.begin(), keys.end(), [&value](const char* key)
			{
				return value.contains(key);
			});
		}

		bool read_integer(const json& value, const int64_t minimum, const int64_t maximum, int64_t& result)
		{
			if (value.is_number_unsigned())
			{
				const uint64_t number = value.get<uint64_t>();
				if (minimum > 0 && number < static_cast<uint64_t>(minimum)) return false;
				if (number > static_cast<uint64_t>(maximum)) return false;
				result = static_cast<int64_t>(number);
				return true;
			}
			if (!value.is_number_integer()) return false;
			const int64_t number = value.get<int64_t>();
			if (number < minimum || number > maximum) return false;
			result = number;
			return true;
		}

		bool read_string(const json& value, std::string& result, const size_t minimum, const size_t maximum)
		{
			if (!value.is_string()) return false;
			result = value.get<std::string>();
			return result.size() >= minimum && result.size() <= maximum && valid_utf8(result);
		}

		bool inspect_string(
			const json& value,
			std::string_view& result,
			const size_t minimum,
			const size_t maximum)
		{
			if (!value.is_string()) return false;
			const std::string& text = value.get_ref<const std::string&>();
			result = text;
			return result.size() >= minimum && result.size() <= maximum && valid_utf8(result);
		}

		bool inspect_id(const json& value, VegetationId& id)
		{
			std::string_view text{};
			if (!inspect_string(value, text, 32, 32)) return false;
			id.fill(0);
			for (size_t index = 0; index < id.size(); ++index)
			{
				auto nibble = [](const char character, uint8_t& output)
				{
					if (character >= '0' && character <= '9') output = static_cast<uint8_t>(character - '0');
					else if (character >= 'a' && character <= 'f') output = static_cast<uint8_t>(character - 'a' + 10);
					else return false;
					return true;
				};
				uint8_t high = 0, low = 0;
				if (!nibble(text[index * 2u], high) || !nibble(text[index * 2u + 1u], low)) return false;
				id[index] = static_cast<uint8_t>((high << 4u) | low);
			}
			return !all_zero(id);
		}

		bool inspect_int_array(
			const json& value,
			const size_t count,
			const int64_t minimum,
			const int64_t maximum)
		{
			if (!value.is_array() || value.size() != count) return false;
			for (const json& element : value)
			{
				int64_t ignored = 0;
				if (!read_integer(element, minimum, maximum, ignored)) return false;
			}
			return true;
		}

		bool decode_id(const json& value, VegetationId& id)
		{
			std::string text{};
			if (!read_string(value, text, 32, 32)) return false;
			id.fill(0);
			for (size_t index = 0; index < id.size(); ++index)
			{
				const char high = text[index * 2];
				const char low = text[index * 2 + 1];
				auto nibble = [](const char character, uint8_t& output)
				{
					if (character >= '0' && character <= '9') output = static_cast<uint8_t>(character - '0');
					else if (character >= 'a' && character <= 'f') output = static_cast<uint8_t>(character - 'a' + 10);
					else return false;
					return true;
				};
				uint8_t high_value = 0;
				uint8_t low_value = 0;
				if (!nibble(high, high_value) || !nibble(low, low_value)) return false;
				id[index] = static_cast<uint8_t>((high_value << 4u) | low_value);
			}
			return !all_zero(id);
		}

		std::string encode_id(const VegetationId& id)
		{
			constexpr char digits[] = "0123456789abcdef";
			std::string text(32, '0');
			for (size_t index = 0; index < id.size(); ++index)
			{
				text[index * 2] = digits[id[index] >> 4u];
				text[index * 2 + 1] = digits[id[index] & 0x0fu];
			}
			return text;
		}

		bool read_int_array(
			const json& value,
			const size_t count,
			const int64_t minimum,
			const int64_t maximum,
			std::vector<int64_t>& output)
		{
			if (!value.is_array() || value.size() != count) return false;
			output.clear();
			output.reserve(count);
			for (const json& element : value)
			{
				int64_t number = 0;
				if (!read_integer(element, minimum, maximum, number)) return false;
				output.push_back(number);
			}
			return true;
		}

		bool parse_species(const json& root, VegetationSpecies& species, std::string* out_error)
		{
			if (!exact_keys(root, { "schema_version", "species_id", "name", "mesh_lods",
				"bounds_mm", "placement", "render" }))
				return fail(out_error, "Vegetation Species root keys are invalid.");
			int64_t schema = 0;
			if (!read_integer(root.at("schema_version"), 1, 1, schema) ||
				!decode_id(root.at("species_id"), species.species_id) ||
				!read_string(root.at("name"), species.name, 1, 256))
				return fail(out_error, "Vegetation Species identity is invalid.");

			const json& lods = root.at("mesh_lods");
			if (!lods.is_array() || lods.empty() || lods.size() > 16)
				return fail(out_error, "Vegetation Species LOD count is invalid.");
			std::set<std::string> mesh_paths{};
			uint32_t previous_error = 0;
			for (const json& lod_json : lods)
			{
				if (!exact_keys(lod_json,
					{ "mesh_asset_path", "material_asset_paths", "screen_error_milli" }))
					return fail(out_error, "Vegetation Species LOD keys are invalid.");
				VegetationMeshLod lod{};
				if (!read_string(lod_json.at("mesh_asset_path"), lod.mesh_asset_path, 1, 4096) ||
					!valid_asset_path(lod.mesh_asset_path, false) ||
					!mesh_paths.insert(lod.mesh_asset_path).second)
					return fail(out_error, "Vegetation Species mesh path is invalid.");
				const json& materials = lod_json.at("material_asset_paths");
				if (!materials.is_array() || materials.empty() || materials.size() > 64)
					return fail(out_error, "Vegetation Species material count is invalid.");
				for (const json& material_json : materials)
				{
					std::string material{};
					if (!read_string(material_json, material, 1, 4096) || !valid_asset_path(material, false))
						return fail(out_error, "Vegetation Species material path is invalid.");
					lod.material_asset_paths.push_back(std::move(material));
				}
				int64_t screen_error = 0;
				if (!read_integer(lod_json.at("screen_error_milli"), 1, 1000000, screen_error) ||
					static_cast<uint32_t>(screen_error) <= previous_error)
					return fail(out_error, "Vegetation Species LOD error is invalid.");
				lod.screen_error_milli = static_cast<uint32_t>(screen_error);
				previous_error = lod.screen_error_milli;
				species.mesh_lods.push_back(std::move(lod));
			}

			const json& bounds = root.at("bounds_mm");
			if (!exact_keys(bounds, { "min", "max" }))
				return fail(out_error, "Vegetation Species bounds keys are invalid.");
			std::vector<int64_t> minimums{};
			std::vector<int64_t> maximums{};
			if (!read_int_array(bounds.at("min"), 3, std::numeric_limits<int32_t>::min(),
					std::numeric_limits<int32_t>::max(), minimums) ||
				!read_int_array(bounds.at("max"), 3, std::numeric_limits<int32_t>::min(),
					std::numeric_limits<int32_t>::max(), maximums))
				return fail(out_error, "Vegetation Species bounds are invalid.");
			for (size_t axis = 0; axis < 3; ++axis)
			{
				if (minimums[axis] >= maximums[axis])
					return fail(out_error, "Vegetation Species bounds are not ordered.");
				species.bounds_mm.min[axis] = static_cast<int32_t>(minimums[axis]);
				species.bounds_mm.max[axis] = static_cast<int32_t>(maximums[axis]);
			}

			const json& placement = root.at("placement");
			if (!exact_keys(placement, { "candidates_per_cell", "min_scale_q12", "max_scale_q12",
				"min_slope_milliradians", "max_slope_milliradians", "material_slot_min",
				"material_slot_max", "align_to_normal" }))
				return fail(out_error, "Vegetation Species placement keys are invalid.");
			int64_t candidates = 0, min_scale = 0, max_scale = 0, min_slope = 0, max_slope = 0;
			if (!read_integer(placement.at("candidates_per_cell"), 1, 256, candidates) ||
				!read_integer(placement.at("min_scale_q12"), 1, 65535, min_scale) ||
				!read_integer(placement.at("max_scale_q12"), 1, 65535, max_scale) || min_scale > max_scale ||
				!read_integer(placement.at("min_slope_milliradians"), 0, 1571, min_slope) ||
				!read_integer(placement.at("max_slope_milliradians"), 0, 1571, max_slope) || min_slope > max_slope ||
				!placement.at("align_to_normal").is_boolean())
				return fail(out_error, "Vegetation Species placement values are invalid.");
			std::vector<int64_t> slot_min{};
			std::vector<int64_t> slot_max{};
			if (!read_int_array(placement.at("material_slot_min"), 8, 0, 255, slot_min) ||
				!read_int_array(placement.at("material_slot_max"), 8, 0, 255, slot_max))
				return fail(out_error, "Vegetation Species material slots are invalid.");
			for (size_t slot = 0; slot < 8; ++slot)
			{
				if (slot_min[slot] > slot_max[slot])
					return fail(out_error, "Vegetation Species material slot range is invalid.");
				species.placement.material_slot_min[slot] = static_cast<uint8_t>(slot_min[slot]);
				species.placement.material_slot_max[slot] = static_cast<uint8_t>(slot_max[slot]);
			}
			species.placement.candidates_per_cell = static_cast<uint16_t>(candidates);
			species.placement.min_scale_q12 = static_cast<uint16_t>(min_scale);
			species.placement.max_scale_q12 = static_cast<uint16_t>(max_scale);
			species.placement.min_slope_milliradians = static_cast<uint16_t>(min_slope);
			species.placement.max_slope_milliradians = static_cast<uint16_t>(max_slope);
			species.placement.align_to_normal = placement.at("align_to_normal").get<bool>();

			const json& render = root.at("render");
			if (!exact_keys(render, { "casts_shadow", "two_sided", "deformation",
				"impostor_asset_path", "chunk_hlod_asset_path" }) ||
				!render.at("casts_shadow").is_boolean() || !render.at("two_sided").is_boolean())
				return fail(out_error, "Vegetation Species render keys are invalid.");
			std::string deformation{};
			if (!read_string(render.at("deformation"), deformation, 4, 5) ||
				!read_string(render.at("impostor_asset_path"), species.render.impostor_asset_path, 0, 4096) ||
				!read_string(render.at("chunk_hlod_asset_path"), species.render.chunk_hlod_asset_path, 0, 4096) ||
				!valid_asset_path(species.render.impostor_asset_path, true) ||
				!valid_asset_path(species.render.chunk_hlod_asset_path, true))
				return fail(out_error, "Vegetation Species render values are invalid.");
			if (deformation == "None") species.render.deformation = VegetationDeformation::None;
			else if (deformation == "Grass") species.render.deformation = VegetationDeformation::Grass;
			else if (deformation == "Tree") species.render.deformation = VegetationDeformation::Tree;
			else return fail(out_error, "Vegetation Species deformation is invalid.");
			species.render.casts_shadow = render.at("casts_shadow").get<bool>();
			species.render.two_sided = render.at("two_sided").get<bool>();
			return true;
		}

		bool preflight_species_root(
			const json& root,
			const uint64_t file_bytes,
			VegetationLoadCost& cost,
			std::string* out_error)
		{
			cost = {};
			cost.file_bytes = file_bytes;
			cost.payload_bytes = file_bytes;
			cost.decoded_bytes = 70;
			if (!exact_keys(root, { "schema_version", "species_id", "name", "mesh_lods",
				"bounds_mm", "placement", "render" }))
				return fail(out_error, "Vegetation Species root keys are invalid.");
			int64_t schema = 0;
			VegetationId species_id{};
			std::string_view name{};
			if (!read_integer(root.at("schema_version"), 1, 1, schema) ||
				!inspect_id(root.at("species_id"), species_id) ||
				!inspect_string(root.at("name"), name, 1, 256) ||
				!checked_add(cost.decoded_bytes, name.size(), cost.decoded_bytes))
				return fail(out_error, "Vegetation Species identity is invalid.");

			const json& lods = root.at("mesh_lods");
			if (!lods.is_array() || lods.empty() || lods.size() > 16)
				return fail(out_error, "Vegetation Species LOD count is invalid.");
			uint64_t lod_charge = 0;
			if (!checked_mul(lods.size(), 4, lod_charge) ||
				!checked_add(cost.decoded_bytes, lod_charge, cost.decoded_bytes))
				return fail(out_error, "Vegetation Species decoded cost overflowed.");
			uint32_t previous_error = 0;
			for (size_t lod_index = 0; lod_index < lods.size(); ++lod_index)
			{
				const json& lod = lods[lod_index];
				if (!exact_keys(lod,
					{ "mesh_asset_path", "material_asset_paths", "screen_error_milli" }))
					return fail(out_error, "Vegetation Species LOD keys are invalid.");
				std::string_view mesh_path{};
				if (!inspect_string(lod.at("mesh_asset_path"), mesh_path, 1, 4096) ||
					!valid_asset_path(mesh_path, false))
					return fail(out_error, "Vegetation Species mesh path is invalid.");
				for (size_t previous = 0; previous < lod_index; ++previous)
				{
					std::string_view previous_path{};
					if (!inspect_string(lods[previous].at("mesh_asset_path"), previous_path, 1, 4096) ||
						previous_path == mesh_path)
						return fail(out_error, "Vegetation Species mesh path is duplicated.");
				}
				if (!checked_add(cost.decoded_bytes, mesh_path.size(), cost.decoded_bytes))
					return fail(out_error, "Vegetation Species decoded cost overflowed.");
				const json& materials = lod.at("material_asset_paths");
				if (!materials.is_array() || materials.empty() || materials.size() > 64)
					return fail(out_error, "Vegetation Species material count is invalid.");
				for (const json& material : materials)
				{
					std::string_view path{};
					if (!inspect_string(material, path, 1, 4096) || !valid_asset_path(path, false) ||
						!checked_add(cost.decoded_bytes, path.size(), cost.decoded_bytes))
						return fail(out_error, "Vegetation Species material path is invalid.");
				}
				int64_t screen_error = 0;
				if (!read_integer(lod.at("screen_error_milli"), 1, 1000000, screen_error) ||
					static_cast<uint32_t>(screen_error) <= previous_error)
					return fail(out_error, "Vegetation Species LOD error is invalid.");
				previous_error = static_cast<uint32_t>(screen_error);
			}

			const json& bounds = root.at("bounds_mm");
			if (!exact_keys(bounds, { "min", "max" }) ||
				!inspect_int_array(bounds.at("min"), 3,
					std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()) ||
				!inspect_int_array(bounds.at("max"), 3,
					std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()))
				return fail(out_error, "Vegetation Species bounds are invalid.");
			for (size_t axis = 0; axis < 3; ++axis)
			{
				int64_t minimum = 0, maximum = 0;
				if (!read_integer(bounds.at("min")[axis], std::numeric_limits<int32_t>::min(),
						std::numeric_limits<int32_t>::max(), minimum) ||
					!read_integer(bounds.at("max")[axis], std::numeric_limits<int32_t>::min(),
						std::numeric_limits<int32_t>::max(), maximum) || minimum >= maximum)
					return fail(out_error, "Vegetation Species bounds are not ordered.");
			}

			const json& placement = root.at("placement");
			if (!exact_keys(placement, { "candidates_per_cell", "min_scale_q12", "max_scale_q12",
				"min_slope_milliradians", "max_slope_milliradians", "material_slot_min",
				"material_slot_max", "align_to_normal" }))
				return fail(out_error, "Vegetation Species placement keys are invalid.");
			int64_t candidates = 0, min_scale = 0, max_scale = 0, min_slope = 0, max_slope = 0;
			if (!read_integer(placement.at("candidates_per_cell"), 1, 256, candidates) ||
				!read_integer(placement.at("min_scale_q12"), 1, 65535, min_scale) ||
				!read_integer(placement.at("max_scale_q12"), 1, 65535, max_scale) || min_scale > max_scale ||
				!read_integer(placement.at("min_slope_milliradians"), 0, 1571, min_slope) ||
				!read_integer(placement.at("max_slope_milliradians"), 0, 1571, max_slope) || min_slope > max_slope ||
				!placement.at("align_to_normal").is_boolean() ||
				!inspect_int_array(placement.at("material_slot_min"), 8, 0, 255) ||
				!inspect_int_array(placement.at("material_slot_max"), 8, 0, 255))
				return fail(out_error, "Vegetation Species placement values are invalid.");
			for (size_t slot = 0; slot < 8; ++slot)
			{
				int64_t minimum = 0, maximum = 0;
				if (!read_integer(placement.at("material_slot_min")[slot], 0, 255, minimum) ||
					!read_integer(placement.at("material_slot_max")[slot], 0, 255, maximum) ||
					minimum > maximum)
					return fail(out_error, "Vegetation Species material slot range is invalid.");
			}

			const json& render = root.at("render");
			if (!exact_keys(render, { "casts_shadow", "two_sided", "deformation",
				"impostor_asset_path", "chunk_hlod_asset_path" }) ||
				!render.at("casts_shadow").is_boolean() || !render.at("two_sided").is_boolean())
				return fail(out_error, "Vegetation Species render keys are invalid.");
			std::string_view deformation{}, impostor{}, hlod{};
			if (!inspect_string(render.at("deformation"), deformation, 4, 5) ||
				(deformation != "None" && deformation != "Grass" && deformation != "Tree") ||
				!inspect_string(render.at("impostor_asset_path"), impostor, 0, 4096) ||
				!inspect_string(render.at("chunk_hlod_asset_path"), hlod, 0, 4096) ||
				!valid_asset_path(impostor, true) || !valid_asset_path(hlod, true) ||
				!checked_add(cost.decoded_bytes, impostor.size(), cost.decoded_bytes) ||
				!checked_add(cost.decoded_bytes, hlod.size(), cost.decoded_bytes))
				return fail(out_error, "Vegetation Species render values are invalid.");
			return true;
		}

		enum class SpeciesPreflightStatus : uint8_t
		{
			Invalid,
			BudgetRejected,
			OwnershipAdmitted
		};

		SpeciesPreflightStatus preflight_species_document(
			const std::vector<uint8_t>& bytes,
			const VegetationLoadBudget& budget,
			VegetationLoadCost& cost,
			json* out_root,
			std::string* out_error)
		{
			cost = {};
			if (bytes.size() > budget.max_file_bytes || bytes.size() > budget.max_payload_bytes)
			{
				cost.file_bytes = bytes.size();
				cost.payload_bytes = bytes.size();
				fail(out_error, "Vegetation Species file size exceeds its budget.");
				return SpeciesPreflightStatus::BudgetRejected;
			}
			if (bytes.empty() ||
				(bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf))
			{
				fail(out_error, "Vegetation Species encoding is invalid.");
				return SpeciesPreflightStatus::Invalid;
			}
			// Duplicate-key and DOM storage are bounded parser scratch admitted by file/payload bytes.
			// No DTO-owned string/vector is constructed until the exact logical cost is admitted below.
			DuplicateKeySax duplicate_checker{};
			if (!json::sax_parse(bytes.begin(), bytes.end(), &duplicate_checker) || !duplicate_checker.valid())
			{
				fail(out_error, "Vegetation Species JSON is invalid or has duplicate keys.");
				return SpeciesPreflightStatus::Invalid;
			}
			json root = json::parse(bytes.begin(), bytes.end(), nullptr, false);
			if (root.is_discarded() || !preflight_species_root(root, bytes.size(), cost, out_error))
				return SpeciesPreflightStatus::Invalid;
			if (decide_vegetation_ownership(cost, budget) ==
				VegetationOwnershipDecision::BudgetRejected)
			{
				fail(out_error, "Vegetation Species decoded budget exceeded before DTO ownership.");
				return SpeciesPreflightStatus::BudgetRejected;
			}
			if (out_root != nullptr) *out_root = std::move(root);
			clear_error(out_error);
			return SpeciesPreflightStatus::OwnershipAdmitted;
		}

		bool validate_species_dto(
			const VegetationSpecies& species,
			std::string* out_error)
		{
			if (all_zero(species.species_id) || species.name.empty() ||
				species.name.size() > 256 || !valid_utf8(species.name) ||
				species.mesh_lods.empty() || species.mesh_lods.size() > 16)
			{
				return fail(out_error, "Vegetation Species identity or LOD count is invalid.");
			}

			uint32_t previous_error = 0;
			for (size_t lod_index = 0; lod_index < species.mesh_lods.size(); ++lod_index)
			{
				const VegetationMeshLod& lod = species.mesh_lods[lod_index];
				if (!valid_asset_path(lod.mesh_asset_path, false) ||
					lod.material_asset_paths.empty() || lod.material_asset_paths.size() > 64 ||
					lod.screen_error_milli == 0 || lod.screen_error_milli > 1000000 ||
					lod.screen_error_milli <= previous_error)
				{
					return fail(out_error, "Vegetation Species LOD is invalid.");
				}
				for (size_t previous = 0; previous < lod_index; ++previous)
				{
					if (species.mesh_lods[previous].mesh_asset_path == lod.mesh_asset_path)
						return fail(out_error, "Vegetation Species mesh path is duplicated.");
				}
				for (const std::string& material_path : lod.material_asset_paths)
				{
					if (!valid_asset_path(material_path, false))
						return fail(out_error, "Vegetation Species material path is invalid.");
				}
				previous_error = lod.screen_error_milli;
			}

			for (size_t axis = 0; axis < species.bounds_mm.min.size(); ++axis)
			{
				if (species.bounds_mm.min[axis] >= species.bounds_mm.max[axis])
					return fail(out_error, "Vegetation Species bounds are not ordered.");
			}

			const VegetationPlacement& placement = species.placement;
			if (placement.candidates_per_cell == 0 || placement.candidates_per_cell > 256 ||
				placement.min_scale_q12 == 0 || placement.max_scale_q12 == 0 ||
				placement.min_scale_q12 > placement.max_scale_q12 ||
				placement.min_slope_milliradians > 1571 ||
				placement.max_slope_milliradians > 1571 ||
				placement.min_slope_milliradians > placement.max_slope_milliradians)
			{
				return fail(out_error, "Vegetation Species placement is invalid.");
			}
			for (size_t slot = 0; slot < placement.material_slot_min.size(); ++slot)
			{
				if (placement.material_slot_min[slot] > placement.material_slot_max[slot])
					return fail(out_error, "Vegetation Species material slot range is invalid.");
			}

			if (species.render.deformation != VegetationDeformation::None &&
				species.render.deformation != VegetationDeformation::Grass &&
				species.render.deformation != VegetationDeformation::Tree)
			{
				return fail(out_error, "Vegetation Species deformation is invalid.");
			}
			if (!valid_asset_path(species.render.impostor_asset_path, true) ||
				!valid_asset_path(species.render.chunk_hlod_asset_path, true))
			{
				return fail(out_error, "Vegetation Species render path is invalid.");
			}
			return true;
		}

		ordered_json species_to_json(const VegetationSpecies& species)
		{
			ordered_json root = ordered_json::object();
			root["schema_version"] = 1;
			root["species_id"] = encode_id(species.species_id);
			root["name"] = species.name;
			root["mesh_lods"] = ordered_json::array();
			for (const VegetationMeshLod& lod : species.mesh_lods)
			{
				ordered_json lod_json = ordered_json::object();
				lod_json["mesh_asset_path"] = lod.mesh_asset_path;
				lod_json["material_asset_paths"] = lod.material_asset_paths;
				lod_json["screen_error_milli"] = lod.screen_error_milli;
				root["mesh_lods"].push_back(std::move(lod_json));
			}
			ordered_json bounds = ordered_json::object();
			bounds["min"] = species.bounds_mm.min;
			bounds["max"] = species.bounds_mm.max;
			root["bounds_mm"] = std::move(bounds);
			ordered_json placement = ordered_json::object();
			placement["candidates_per_cell"] = species.placement.candidates_per_cell;
			placement["min_scale_q12"] = species.placement.min_scale_q12;
			placement["max_scale_q12"] = species.placement.max_scale_q12;
			placement["min_slope_milliradians"] = species.placement.min_slope_milliradians;
			placement["max_slope_milliradians"] = species.placement.max_slope_milliradians;
			placement["material_slot_min"] = species.placement.material_slot_min;
			placement["material_slot_max"] = species.placement.material_slot_max;
			placement["align_to_normal"] = species.placement.align_to_normal;
			root["placement"] = std::move(placement);
			ordered_json render = ordered_json::object();
			render["casts_shadow"] = species.render.casts_shadow;
			render["two_sided"] = species.render.two_sided;
			switch (species.render.deformation)
			{
			case VegetationDeformation::None: render["deformation"] = "None"; break;
			case VegetationDeformation::Grass: render["deformation"] = "Grass"; break;
			case VegetationDeformation::Tree: render["deformation"] = "Tree"; break;
			default: render["deformation"] = ""; break;
			}
			render["impostor_asset_path"] = species.render.impostor_asset_path;
			render["chunk_hlod_asset_path"] = species.render.chunk_hlod_asset_path;
			root["render"] = std::move(render);
			return root;
		}

		bool species_cost(const VegetationSpecies& species, const uint64_t file_bytes,
			VegetationLoadCost& cost)
		{
			cost = {};
			cost.file_bytes = file_bytes;
			cost.payload_bytes = file_bytes;
			uint64_t decoded = 70;
			uint64_t lod_charge = 0;
			if (!checked_mul(species.mesh_lods.size(), 4, lod_charge) ||
				!checked_add(decoded, lod_charge, decoded) ||
				!checked_add(decoded, species.name.size(), decoded)) return false;
			for (const VegetationMeshLod& lod : species.mesh_lods)
			{
				if (!checked_add(decoded, lod.mesh_asset_path.size(), decoded)) return false;
				for (const std::string& material : lod.material_asset_paths)
					if (!checked_add(decoded, material.size(), decoded)) return false;
			}
			if (!checked_add(decoded, species.render.impostor_asset_path.size(), decoded) ||
				!checked_add(decoded, species.render.chunk_hlod_asset_path.size(), decoded)) return false;
			cost.decoded_bytes = decoded;
			return true;
		}
	}

	bool decode_vegetation_species(
		const std::vector<uint8_t>& bytes,
		const VegetationLoadBudget& budget,
		VegetationSpecies& out_species,
		std::string* out_error,
		VegetationLoadCost* out_cost)
	{
		out_species = {};
		if (out_cost != nullptr) *out_cost = {};
		clear_error(out_error);
		json root{};
		VegetationLoadCost cost{};
		if (preflight_species_document(bytes, budget, cost, &root, out_error) !=
			SpeciesPreflightStatus::OwnershipAdmitted)
			return false;
		VegetationSpecies species{};
		if (!parse_species(root, species, out_error)) return false;
		VegetationLoadCost published_cost{};
		if (!species_cost(species, bytes.size(), published_cost) ||
			!same_load_cost(cost, published_cost))
			return fail(out_error, "Vegetation Species preflight and DTO costs diverged.");
		out_species = std::move(species);
		if (out_cost != nullptr) *out_cost = cost;
		clear_error(out_error);
		return true;
	}

	bool encode_vegetation_species(
		const VegetationSpecies& species,
		std::vector<uint8_t>& out_bytes,
		std::string* out_error)
	{
		out_bytes.clear();
		clear_error(out_error);
		if (!validate_species_dto(species, out_error)) return false;
		const ordered_json root = species_to_json(species);
		const std::string encoded = root.dump();
		const std::vector<uint8_t> candidate(encoded.begin(), encoded.end());
		VegetationSpecies validated{};
		VegetationLoadBudget validation_budget{
			std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max(),
			std::numeric_limits<uint64_t>::max(), 0, 0, 0
		};
		std::vector<uint8_t> canonical = candidate;
		canonical.push_back('\n');
		if (!decode_vegetation_species(canonical, validation_budget, validated, out_error)) return false;
		out_bytes = std::move(canonical);
		clear_error(out_error);
		return true;
	}
}
