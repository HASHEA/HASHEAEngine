#include "Scene/SceneSerializer.h"
#include "Scene/Scene.h"
#include <fstream>
#include <json.hpp>

namespace AshEditor
{
	namespace
	{
		using json = nlohmann::json;

		static auto serialize_vec3(const std::array<float, 3>& value) -> json
		{
			return json::array({ value[0], value[1], value[2] });
		}

		static auto deserialize_vec3(const json& value, const std::array<float, 3>& fallback) -> std::array<float, 3>
		{
			std::array<float, 3> result = fallback;
			if (!value.is_array() || value.size() != 3)
			{
				return result;
			}

			for (size_t index = 0; index < 3; ++index)
			{
				result[index] = value[index].get<float>();
			}
			return result;
		}
	}

	bool SceneSerializer::save(const Scene& scene, const std::filesystem::path& path) const
	{
		std::filesystem::create_directories(path.parent_path());

		json root{};
		root["name"] = scene.get_name();
		root["nextEntityId"] = scene.get_next_entity_id();
		root["entities"] = json::array();

		for (const SceneEntityData& entity : scene.get_entities())
		{
			json entity_json{};
			entity_json["id"] = entity.id;
			entity_json["parentId"] = entity.parent_id;
			entity_json["name"] = entity.name.name;
			entity_json["transform"] = {
				{ "position", serialize_vec3(entity.transform.position) },
				{ "rotation", serialize_vec3(entity.transform.rotation) },
				{ "scale", serialize_vec3(entity.transform.scale) }
			};
			root["entities"].push_back(std::move(entity_json));
		}

		std::ofstream output(path, std::ios::out | std::ios::trunc);
		if (!output.is_open())
		{
			return false;
		}

		output << root.dump(2);
		return output.good();
	}

	bool SceneSerializer::load(const std::filesystem::path& path, Scene& scene) const
	{
		std::ifstream input(path);
		if (!input.is_open())
		{
			return false;
		}

		json root{};
		input >> root;

		scene.reset(root.value("name", "Untitled Scene"));
		scene.get_entities().clear();
		scene.set_next_entity_id(root.value("nextEntityId", 1ull));

		if (root.contains("entities") && root["entities"].is_array())
		{
			for (const json& entity_json : root["entities"])
			{
				SceneEntityData entity{};
				entity.id = entity_json.value("id", 0ull);
				entity.parent_id = entity_json.value("parentId", 0ull);
				entity.name.name = entity_json.value("name", "Entity");

				const json transform_json = entity_json.value("transform", json::object());
				entity.transform.position = deserialize_vec3(transform_json.value("position", json::array()), entity.transform.position);
				entity.transform.rotation = deserialize_vec3(transform_json.value("rotation", json::array()), entity.transform.rotation);
				entity.transform.scale = deserialize_vec3(transform_json.value("scale", json::array()), entity.transform.scale);

				scene.get_entities().push_back(std::move(entity));
			}
		}

		scene.mark_dirty(false);
		return true;
	}
}
