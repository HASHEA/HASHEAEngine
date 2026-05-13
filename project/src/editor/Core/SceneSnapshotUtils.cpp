#include "Core/SceneSnapshotUtils.h"

#include <json.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <string_view>

namespace AshEditor::SceneSnapshotUtils
{
	namespace
	{
		using json = nlohmann::json;

		json ToJsonVec2(const glm::vec2& refValue)
		{
			return json::array({ refValue.x, refValue.y });
		}

		json ToJsonVec3(const glm::vec3& refValue)
		{
			return json::array({ refValue.x, refValue.y, refValue.z });
		}

		json ToJsonVec4(const glm::vec4& refValue)
		{
			return json::array({ refValue.x, refValue.y, refValue.z, refValue.w });
		}

		glm::vec2 FromJsonVec2(const json& refValue, const glm::vec2& refFallback)
		{
			if (!refValue.is_array() || refValue.size() != 2)
			{
				return refFallback;
			}

			glm::vec2 vec2Result = refFallback;
			vec2Result.x = refValue[0].get<float>();
			vec2Result.y = refValue[1].get<float>();
			return vec2Result;
		}

		glm::vec3 FromJsonVec3(const json& refValue, const glm::vec3& refFallback)
		{
			if (!refValue.is_array() || refValue.size() != 3)
			{
				return refFallback;
			}

			glm::vec3 vec3Result = refFallback;
			vec3Result.x = refValue[0].get<float>();
			vec3Result.y = refValue[1].get<float>();
			vec3Result.z = refValue[2].get<float>();
			return vec3Result;
		}

		glm::vec4 FromJsonVec4(const json& refValue, const glm::vec4& refFallback)
		{
			if (!refValue.is_array() || refValue.size() != 4)
			{
				return refFallback;
			}

			glm::vec4 vec4Result = refFallback;
			vec4Result.x = refValue[0].get<float>();
			vec4Result.y = refValue[1].get<float>();
			vec4Result.z = refValue[2].get<float>();
			vec4Result.w = refValue[3].get<float>();
			return vec4Result;
		}

		uint64_t ReadUnsignedIntegral(const uint8_t* pData, const uint32_t uSize)
		{
			switch (uSize)
			{
			case 1: return *reinterpret_cast<const uint8_t*>(pData);
			case 2: return *reinterpret_cast<const uint16_t*>(pData);
			case 4: return *reinterpret_cast<const uint32_t*>(pData);
			case 8: return *reinterpret_cast<const uint64_t*>(pData);
			default: return 0;
			}
		}

		int64_t ReadSignedIntegral(const uint8_t* pData, const uint32_t uSize)
		{
			switch (uSize)
			{
			case 1: return *reinterpret_cast<const int8_t*>(pData);
			case 2: return *reinterpret_cast<const int16_t*>(pData);
			case 4: return *reinterpret_cast<const int32_t*>(pData);
			case 8: return *reinterpret_cast<const int64_t*>(pData);
			default: return 0;
			}
		}

		void WriteUnsignedIntegral(uint8_t* pData, const uint32_t uSize, const uint64_t uValue)
		{
			switch (uSize)
			{
			case 1:
				*reinterpret_cast<uint8_t*>(pData) = static_cast<uint8_t>(uValue);
				return;
			case 2:
				*reinterpret_cast<uint16_t*>(pData) = static_cast<uint16_t>(uValue);
				return;
			case 4:
				*reinterpret_cast<uint32_t*>(pData) = static_cast<uint32_t>(uValue);
				return;
			case 8:
				*reinterpret_cast<uint64_t*>(pData) = static_cast<uint64_t>(uValue);
				return;
			default:
				return;
			}
		}

		void WriteSignedIntegral(uint8_t* pData, const uint32_t uSize, const int64_t iValue)
		{
			switch (uSize)
			{
			case 1:
				*reinterpret_cast<int8_t*>(pData) = static_cast<int8_t>(iValue);
				return;
			case 2:
				*reinterpret_cast<int16_t*>(pData) = static_cast<int16_t>(iValue);
				return;
			case 4:
				*reinterpret_cast<int32_t*>(pData) = static_cast<int32_t>(iValue);
				return;
			case 8:
				*reinterpret_cast<int64_t*>(pData) = static_cast<int64_t>(iValue);
				return;
			default:
				return;
			}
		}

		json SerializeComponentPayload(const void* pComponentData, const AshEngine::SceneComponentDesc& refComponentDesc)
		{
			json jsonComponent = json::object();
			const uint8_t* pBase = static_cast<const uint8_t*>(pComponentData);
			for (uint32_t uPropertyIndex = 0; uPropertyIndex < refComponentDesc.property_count; ++uPropertyIndex)
			{
				const AshEngine::ScenePropertyDesc& propertyDesc = refComponentDesc.properties[uPropertyIndex];
				const uint8_t* pPropertyData = pBase + propertyDesc.offset;
				switch (propertyDesc.type)
				{
				case AshEngine::ScenePropertyType::Bool:
					jsonComponent[propertyDesc.name] = *reinterpret_cast<const bool*>(pPropertyData);
					break;
				case AshEngine::ScenePropertyType::Int32:
					jsonComponent[propertyDesc.name] = ReadSignedIntegral(pPropertyData, propertyDesc.size);
					break;
				case AshEngine::ScenePropertyType::UInt32:
				case AshEngine::ScenePropertyType::Enum:
					jsonComponent[propertyDesc.name] = ReadUnsignedIntegral(pPropertyData, propertyDesc.size);
					break;
				case AshEngine::ScenePropertyType::Float:
					jsonComponent[propertyDesc.name] = *reinterpret_cast<const float*>(pPropertyData);
					break;
				case AshEngine::ScenePropertyType::Vec2:
					jsonComponent[propertyDesc.name] = ToJsonVec2(*reinterpret_cast<const glm::vec2*>(pPropertyData));
					break;
				case AshEngine::ScenePropertyType::Vec3:
					jsonComponent[propertyDesc.name] = ToJsonVec3(*reinterpret_cast<const glm::vec3*>(pPropertyData));
					break;
				case AshEngine::ScenePropertyType::Vec4:
					jsonComponent[propertyDesc.name] = ToJsonVec4(*reinterpret_cast<const glm::vec4*>(pPropertyData));
					break;
				case AshEngine::ScenePropertyType::String:
					jsonComponent[propertyDesc.name] = *reinterpret_cast<const std::string*>(pPropertyData);
					break;
				default:
					break;
				}
			}
			return jsonComponent;
		}

		void DeserializeComponentPayload(
			const json& refComponentJson,
			const AshEngine::SceneComponentDesc& refComponentDesc,
			void* pComponentData)
		{
			uint8_t* pBase = static_cast<uint8_t*>(pComponentData);
			for (uint32_t uPropertyIndex = 0; uPropertyIndex < refComponentDesc.property_count; ++uPropertyIndex)
			{
				const AshEngine::ScenePropertyDesc& propertyDesc = refComponentDesc.properties[uPropertyIndex];
				if (!refComponentJson.contains(propertyDesc.name))
				{
					continue;
				}

				const json& refPropertyJson = refComponentJson[propertyDesc.name];
				uint8_t* pPropertyData = pBase + propertyDesc.offset;
				switch (propertyDesc.type)
				{
				case AshEngine::ScenePropertyType::Bool:
					*reinterpret_cast<bool*>(pPropertyData) = refPropertyJson.get<bool>();
					break;
				case AshEngine::ScenePropertyType::Int32:
					WriteSignedIntegral(pPropertyData, propertyDesc.size, refPropertyJson.get<int64_t>());
					break;
				case AshEngine::ScenePropertyType::UInt32:
				case AshEngine::ScenePropertyType::Enum:
					WriteUnsignedIntegral(pPropertyData, propertyDesc.size, refPropertyJson.get<uint64_t>());
					break;
				case AshEngine::ScenePropertyType::Float:
					*reinterpret_cast<float*>(pPropertyData) = refPropertyJson.get<float>();
					break;
				case AshEngine::ScenePropertyType::Vec2:
					*reinterpret_cast<glm::vec2*>(pPropertyData) =
						FromJsonVec2(refPropertyJson, *reinterpret_cast<glm::vec2*>(pPropertyData));
					break;
				case AshEngine::ScenePropertyType::Vec3:
					*reinterpret_cast<glm::vec3*>(pPropertyData) =
						FromJsonVec3(refPropertyJson, *reinterpret_cast<glm::vec3*>(pPropertyData));
					break;
				case AshEngine::ScenePropertyType::Vec4:
					*reinterpret_cast<glm::vec4*>(pPropertyData) =
						FromJsonVec4(refPropertyJson, *reinterpret_cast<glm::vec4*>(pPropertyData));
					break;
				case AshEngine::ScenePropertyType::String:
					*reinterpret_cast<std::string*>(pPropertyData) = refPropertyJson.get<std::string>();
					break;
				default:
					break;
				}
			}
		}

		template<typename Component>
		std::optional<SceneComponentSnapshot> MakeComponentSnapshot(
			const AshEngine::SceneComponentType eType,
			const Component& refComponent)
		{
			const AshEngine::SceneComponentDesc* pComponentDesc = AshEngine::get_scene_component_descriptor(eType);
			if (!pComponentDesc)
			{
				return std::nullopt;
			}

			SceneComponentSnapshot snapshot{};
			snapshot.eType = eType;
			snapshot.strSerializedValue = SerializeComponentPayload(&refComponent, *pComponentDesc).dump();
			return snapshot;
		}

		std::optional<SceneComponentSnapshot> CaptureComponentSnapshot(
			const AshEngine::Entity& refEntity,
			const AshEngine::SceneComponentType eType)
		{
			switch (eType)
			{
			case AshEngine::SceneComponentType::Name:
				return MakeComponentSnapshot(eType, refEntity.get_name_component());
			case AshEngine::SceneComponentType::Transform:
				return MakeComponentSnapshot(eType, refEntity.get_transform_component());
			case AshEngine::SceneComponentType::Camera:
				return refEntity.has_camera_component()
					? MakeComponentSnapshot(eType, refEntity.get_camera_component())
					: std::nullopt;
			case AshEngine::SceneComponentType::Light:
				return refEntity.has_light_component()
					? MakeComponentSnapshot(eType, refEntity.get_light_component())
					: std::nullopt;
			case AshEngine::SceneComponentType::Mesh:
				return refEntity.has_mesh_component()
					? MakeComponentSnapshot(eType, refEntity.get_mesh_component())
					: std::nullopt;
			default:
				return std::nullopt;
			}
		}

		SceneEntitySnapshot CaptureSnapshotRecursive(
			const AshEngine::Scene& refScene,
			const AshEngine::Entity& refEntity)
		{
			SceneEntitySnapshot snapshot{};
			snapshot.uEntityId = refEntity.get_id();
			snapshot.uSiblingIndex = refScene.get_entity_sibling_index(refEntity.get_id());

			for (const AshEngine::SceneComponentType eType : refEntity.get_component_types())
			{
				std::optional<SceneComponentSnapshot> optComponentSnapshot = CaptureComponentSnapshot(refEntity, eType);
				if (optComponentSnapshot.has_value())
				{
					snapshot.vecComponents.push_back(std::move(*optComponentSnapshot));
				}
			}

			for (const AshEngine::Entity& refChild : refEntity.get_children())
			{
				snapshot.vecChildren.push_back(CaptureSnapshotRecursive(refScene, refChild));
			}
			return snapshot;
		}

		template<typename Component>
		bool ApplyRequiredComponent(AshEngine::Entity entity, const SceneComponentSnapshot& refSnapshot)
		{
			const AshEngine::SceneComponentDesc* pComponentDesc =
				AshEngine::get_scene_component_descriptor(refSnapshot.eType);
			if (!pComponentDesc)
			{
				return false;
			}

			const json payloadJson = json::parse(refSnapshot.strSerializedValue, nullptr, false);
			if (payloadJson.is_discarded())
			{
				return false;
			}

			Component component{};
			DeserializeComponentPayload(payloadJson, *pComponentDesc, &component);
			return entity.write_component(refSnapshot.eType, &component, sizeof(Component));
		}

		template<typename Component>
		bool ApplyOptionalComponent(
			AshEngine::Entity entity,
			const SceneComponentSnapshot& refSnapshot,
			bool (AshEngine::Entity::*pfnHas)() const,
			bool (AshEngine::Entity::*pfnSet)(const Component&),
			bool (AshEngine::Entity::*pfnAdd)(const Component&))
		{
			const AshEngine::SceneComponentDesc* pComponentDesc =
				AshEngine::get_scene_component_descriptor(refSnapshot.eType);
			if (!pComponentDesc)
			{
				return false;
			}

			const json payloadJson = json::parse(refSnapshot.strSerializedValue, nullptr, false);
			if (payloadJson.is_discarded())
			{
				return false;
			}

			Component component{};
			DeserializeComponentPayload(payloadJson, *pComponentDesc, &component);
			return (entity.*pfnHas)()
				? (entity.*pfnSet)(component)
				: (entity.*pfnAdd)(component);
		}

		bool RemoveOptionalComponent(AshEngine::Entity entity, const AshEngine::SceneComponentType eType)
		{
			switch (eType)
			{
			case AshEngine::SceneComponentType::Camera:
				return !entity.has_camera_component() || entity.remove_camera_component();
			case AshEngine::SceneComponentType::Light:
				return !entity.has_light_component() || entity.remove_light_component();
			case AshEngine::SceneComponentType::Mesh:
				return !entity.has_mesh_component() || entity.remove_mesh_component();
			default:
				return true;
			}
		}

		bool SnapshotHasComponent(const SceneEntitySnapshot& refSnapshot, const AshEngine::SceneComponentType eType)
		{
			for (const SceneComponentSnapshot& refComponentSnapshot : refSnapshot.vecComponents)
			{
				if (refComponentSnapshot.eType == eType)
				{
					return true;
				}
			}
			return false;
		}

		bool ApplyEntitySnapshot(AshEngine::Entity entity, const SceneEntitySnapshot& refSnapshot)
		{
			if (!entity.is_valid())
			{
				return false;
			}

			const AshEngine::SceneComponentType arrOptionalTypes[] =
			{
				AshEngine::SceneComponentType::Camera,
				AshEngine::SceneComponentType::Light,
				AshEngine::SceneComponentType::Mesh,
			};
			for (const AshEngine::SceneComponentType eType : arrOptionalTypes)
			{
				if (!SnapshotHasComponent(refSnapshot, eType) && !RemoveOptionalComponent(entity, eType))
				{
					return false;
				}
			}

			for (const SceneComponentSnapshot& refComponentSnapshot : refSnapshot.vecComponents)
			{
				switch (refComponentSnapshot.eType)
				{
				case AshEngine::SceneComponentType::Name:
					if (!ApplyRequiredComponent<AshEngine::NameComponent>(entity, refComponentSnapshot))
					{
						return false;
					}
					break;
				case AshEngine::SceneComponentType::Transform:
					if (!ApplyRequiredComponent<AshEngine::TransformComponent>(entity, refComponentSnapshot))
					{
						return false;
					}
					break;
				case AshEngine::SceneComponentType::Camera:
					if (!ApplyOptionalComponent<AshEngine::CameraComponent>(
						entity,
						refComponentSnapshot,
						&AshEngine::Entity::has_camera_component,
						&AshEngine::Entity::set_camera_component,
						&AshEngine::Entity::add_camera_component))
					{
						return false;
					}
					break;
				case AshEngine::SceneComponentType::Light:
					if (!ApplyOptionalComponent<AshEngine::LightComponent>(
						entity,
						refComponentSnapshot,
						&AshEngine::Entity::has_light_component,
						&AshEngine::Entity::set_light_component,
						&AshEngine::Entity::add_light_component))
					{
						return false;
					}
					break;
				case AshEngine::SceneComponentType::Mesh:
					if (!ApplyOptionalComponent<AshEngine::MeshComponent>(
						entity,
						refComponentSnapshot,
						&AshEngine::Entity::has_mesh_component,
						&AshEngine::Entity::set_mesh_component,
						&AshEngine::Entity::add_mesh_component))
					{
						return false;
					}
					break;
				default:
					return false;
				}
			}

			return true;
		}

		AshEngine::Entity CreateSceneEntityWithId(
			AshEngine::Scene& refScene,
			const SceneEntityId uEntityId,
			const std::string_view svName,
			const SceneEntityId uParentId,
			const uint32_t uSiblingIndex)
		{
			if (uEntityId == 0)
			{
				return {};
			}

			const AshEngine::Entity parent = uParentId != 0 ? refScene.find_entity(uParentId) : AshEngine::Entity{};
			return parent.is_valid()
				? refScene.create_entity_with_id(uEntityId, svName, parent, uSiblingIndex)
				: refScene.create_entity_with_id(uEntityId, svName, {}, uSiblingIndex);
		}

		AshEngine::Entity RestoreSnapshotRecursive(
			AshEngine::Scene& refScene,
			const SceneEntitySnapshot& refSnapshot,
			const SceneEntityId uParentId)
		{
			AshEngine::Entity entity =
				CreateSceneEntityWithId(refScene, refSnapshot.uEntityId, "Entity", uParentId, refSnapshot.uSiblingIndex);
			if (!entity.is_valid() || !ApplyEntitySnapshot(entity, refSnapshot))
			{
				if (entity.is_valid())
				{
					refScene.destroy_entity(entity.get_id());
				}
				return {};
			}

			for (const SceneEntitySnapshot& refChildSnapshot : refSnapshot.vecChildren)
			{
				if (!RestoreSnapshotRecursive(refScene, refChildSnapshot, entity.get_id()).is_valid())
				{
					refScene.destroy_entity(entity.get_id());
					return {};
				}
			}
			return entity;
		}
	}

	std::optional<SceneEntitySnapshot> CaptureEntitySnapshot(
		const AshEngine::Scene& refScene,
		const SceneEntityId uEntityId)
	{
		const AshEngine::Entity entity = refScene.find_entity(uEntityId);
		if (!entity.is_valid())
		{
			return std::nullopt;
		}

		return CaptureSnapshotRecursive(refScene, entity);
	}

	AshEngine::Entity RestoreEntitySnapshot(
		AshEngine::Scene& refScene,
		const SceneEntitySnapshot& refSnapshot,
		const SceneEntityId uParentId)
	{
		return RestoreSnapshotRecursive(refScene, refSnapshot, uParentId);
	}

	AshEngine::Scene CloneScene(const AshEngine::Scene& refSourceScene)
	{
		if (!refSourceScene.is_valid())
		{
			return {};
		}

		AshEngine::Scene clonedScene = AshEngine::Scene::create(refSourceScene.get_name());
		if (!clonedScene.is_valid())
		{
			return {};
		}

		for (const AshEngine::Entity& refRootEntity : refSourceScene.get_root_entities())
		{
			const std::optional<SceneEntitySnapshot> optSnapshot =
				CaptureEntitySnapshot(refSourceScene, refRootEntity.get_id());
			if (!optSnapshot.has_value())
			{
				return {};
			}

			if (!RestoreEntitySnapshot(clonedScene, *optSnapshot, 0).is_valid())
			{
				return {};
			}
		}

		clonedScene.mark_clean();
		return clonedScene;
	}
}
