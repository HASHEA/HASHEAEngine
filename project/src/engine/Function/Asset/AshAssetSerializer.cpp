#include "Function/Asset/AssetData.h"

#include <json.hpp>

#include <exception>
#include <fstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace AshEngine
{
	namespace
	{
		using json = nlohmann::json;

		constexpr uint32_t k_ashasset_file_version = 2;

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

		static auto to_json_vec3(const glm::vec3& value) -> json
		{
			return json::array({ value.x, value.y, value.z });
		}

		static auto from_json_vec3(const json& value, const glm::vec3& fallback) -> glm::vec3
		{
			if (!value.is_array() || value.size() != 3)
			{
				return fallback;
			}

			glm::vec3 result = fallback;
			result.x = value[0].get<float>();
			result.y = value[1].get<float>();
			result.z = value[2].get<float>();
			return result;
		}

		static auto serialize_material_overrides(const std::vector<MeshMaterialOverride>& overrides) -> json
		{
			json result = json::array();
			for (const MeshMaterialOverride& override_desc : overrides)
			{
				if (override_desc.material_slot == k_invalid_material_slot || override_desc.material_path.empty())
				{
					continue;
				}

				result.push_back(
				{
					{ "material_slot", override_desc.material_slot },
					{ "material_path", override_desc.material_path },
				});
			}
			return result;
		}

		static auto deserialize_material_overrides(const json& value) -> std::vector<MeshMaterialOverride>
		{
			std::vector<MeshMaterialOverride> overrides{};
			if (!value.is_array())
			{
				return overrides;
			}

			overrides.reserve(value.size());
			for (const json& entry : value)
			{
				if (!entry.is_object())
				{
					continue;
				}

				MeshMaterialOverride override_desc{};
				override_desc.material_slot = entry.value("material_slot", k_invalid_material_slot);
				override_desc.material_path = entry.value("material_path", std::string{});
				if (override_desc.material_slot == k_invalid_material_slot || override_desc.material_path.empty())
				{
					continue;
				}

				overrides.push_back(std::move(override_desc));
			}

			return overrides;
		}

		static auto rebuild_ashasset_hierarchy(AshAsset& asset) -> void
		{
			asset.root_nodes.clear();
			for (AshAssetNode& node : asset.nodes)
			{
				node.children.clear();
			}

			for (uint32_t node_index = 0; node_index < asset.nodes.size(); ++node_index)
			{
				AshAssetNode& node = asset.nodes[node_index];
				if (node.parent_index >= 0 && static_cast<size_t>(node.parent_index) < asset.nodes.size() && static_cast<uint32_t>(node.parent_index) != node_index)
				{
					asset.nodes[static_cast<size_t>(node.parent_index)].children.push_back(node_index);
				}
				else
				{
					node.parent_index = -1;
					asset.root_nodes.push_back(node_index);
				}
			}
		}
	}

	bool load_ashasset_from_file(const std::filesystem::path& path, AshAsset& out_asset, std::string* out_error)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		std::ifstream input(path);
		if (!input.is_open())
		{
			bResult = make_error(out_error, "Failed to open ashasset file.");
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

		const uint32_t version = root.value("version", k_ashasset_file_version);
		if (version > k_ashasset_file_version)
		{
			bResult = make_error(out_error, "AshAsset file version is newer than this runtime supports.");
			break;
		}

		const json nodes_json = root.value("nodes", json::array());
		if (!nodes_json.is_array())
		{
			bResult = make_error(out_error, "AshAsset nodes entry is invalid.");
			break;
		}

		AshAsset asset{};
		asset.name = root.value("name", path.stem().string());
		asset.source_path = path;
		asset.nodes.reserve(nodes_json.size());

		for (const json& node_json : nodes_json)
		{
			if (!node_json.is_object())
			{
				continue;
			}

			AshAssetNode node{};
			node.name = node_json.value("name", std::string("Entity"));
			node.parent_index = node_json.value("parent", -1);

			if (node_json.contains("transform"))
			{
				const json& transform_json = node_json["transform"];
				node.transform.position = from_json_vec3(transform_json.value("position", json::array()), node.transform.position);
				node.transform.rotation_euler_degrees = from_json_vec3(transform_json.value("rotation_euler_degrees", json::array()), node.transform.rotation_euler_degrees);
				node.transform.scale = from_json_vec3(transform_json.value("scale", json::array({ 1.0f, 1.0f, 1.0f })), node.transform.scale);
			}

			if (node_json.contains("camera"))
			{
				const json& camera_json = node_json["camera"];
				CameraComponent camera{};
				camera.primary = camera_json.value("primary", camera.primary);
				camera.projection = static_cast<CameraProjectionType>(camera_json.value("projection", static_cast<int32_t>(camera.projection)));
				camera.fov_y_degrees = camera_json.value("fov_y_degrees", camera.fov_y_degrees);
				camera.near_plane = camera_json.value("near_plane", camera.near_plane);
				camera.far_plane = camera_json.value("far_plane", camera.far_plane);
				camera.orthographic_height = camera_json.value("orthographic_height", camera.orthographic_height);
				node.camera = camera;
			}

			if (node_json.contains("light"))
			{
				const json& light_json = node_json["light"];
				LightComponent light{};
				light.type = static_cast<LightType>(light_json.value("type", static_cast<int32_t>(light.type)));
				light.color = from_json_vec3(light_json.value("color", json::array()), light.color);
				light.intensity = light_json.value("intensity", light.intensity);
				light.range = light_json.value("range", light.range);
				light.inner_cone_angle_degrees = light_json.value("inner_cone_angle_degrees", light.inner_cone_angle_degrees);
				light.outer_cone_angle_degrees = light_json.value("outer_cone_angle_degrees", light.outer_cone_angle_degrees);
				node.light = light;
			}

			if (node_json.contains("mesh"))
			{
				const json& mesh_json = node_json["mesh"];
				MeshComponent mesh{};
				mesh.asset_path = mesh_json.value("asset_path", std::string{});
				mesh.mesh_index = mesh_json.value("mesh_index", 0u);
				mesh.material_overrides = deserialize_material_overrides(mesh_json.value("material_overrides", json::array()));
				mesh.visible = mesh_json.value("visible", true);
				mesh.mobility = static_cast<SceneMobility>(mesh_json.value("mobility", static_cast<uint32_t>(mesh.mobility)));
				mesh.layer_mask = mesh_json.value("layer_mask", mesh.layer_mask);
				node.mesh = mesh;
			}

			asset.nodes.push_back(std::move(node));
		}

		rebuild_ashasset_hierarchy(asset);
		if (!asset.is_valid())
		{
			bResult = make_error(out_error, "AshAsset file does not contain a valid node hierarchy.");
			break;
		}

		out_asset = std::move(asset);
		clear_error(out_error);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool save_ashasset_to_file(const AshAsset& asset, const std::filesystem::path& path, std::string* out_error)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		if (!asset.is_valid())
		{
			bResult = make_error(out_error, "AshAsset is invalid.");
			break;
		}

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
		root["version"] = k_ashasset_file_version;
		root["name"] = asset.name;
		root["nodes"] = json::array();

		for (const AshAssetNode& node : asset.nodes)
		{
			json node_json{};
			node_json["name"] = node.name;
			node_json["parent"] = node.parent_index;
			node_json["transform"] =
			{
				{ "position", to_json_vec3(node.transform.position) },
				{ "rotation_euler_degrees", to_json_vec3(node.transform.rotation_euler_degrees) },
				{ "scale", to_json_vec3(node.transform.scale) },
			};

			if (node.camera.has_value())
			{
				const CameraComponent& camera = node.camera.value();
				node_json["camera"] =
				{
					{ "primary", camera.primary },
					{ "projection", static_cast<int32_t>(camera.projection) },
					{ "fov_y_degrees", camera.fov_y_degrees },
					{ "near_plane", camera.near_plane },
					{ "far_plane", camera.far_plane },
					{ "orthographic_height", camera.orthographic_height },
				};
			}

			if (node.light.has_value())
			{
				const LightComponent& light = node.light.value();
				node_json["light"] =
				{
					{ "type", static_cast<int32_t>(light.type) },
					{ "color", to_json_vec3(light.color) },
					{ "intensity", light.intensity },
					{ "range", light.range },
					{ "inner_cone_angle_degrees", light.inner_cone_angle_degrees },
					{ "outer_cone_angle_degrees", light.outer_cone_angle_degrees },
				};
			}

			if (node.mesh.has_value())
			{
				const MeshComponent& mesh = node.mesh.value();
				node_json["mesh"] =
				{
					{ "asset_path", mesh.asset_path },
					{ "mesh_index", mesh.mesh_index },
					{ "visible", mesh.visible },
					{ "mobility", static_cast<uint32_t>(mesh.mobility) },
					{ "layer_mask", mesh.layer_mask },
				};
				if (!mesh.material_overrides.empty())
				{
					node_json["mesh"]["material_overrides"] = serialize_material_overrides(mesh.material_overrides);
				}
			}

			root["nodes"].push_back(std::move(node_json));
		}

		std::ofstream output(path);
		if (!output.is_open())
		{
			bResult = make_error(out_error, "Failed to open ashasset output file.");
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
}
