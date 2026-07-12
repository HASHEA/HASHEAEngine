#include "Core/SceneSnapshotComponentUtils.h"

#include "Core/SceneComponentSerialization.h"

namespace AshEditor
{
	namespace SceneSnapshotComponentUtils
	{
		namespace
		{
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
				snapshot.strSerializedValue =
					SceneComponentSerialization::SerializeComponentPayload(&refComponent, *pComponentDesc);
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

				Component component{};
				if (!SceneComponentSerialization::DeserializeComponentPayload(
					refSnapshot.strSerializedValue,
					*pComponentDesc,
					&component))
				{
					return false;
				}

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

				Component component{};
				if (!SceneComponentSerialization::DeserializeComponentPayload(
					refSnapshot.strSerializedValue,
					*pComponentDesc,
					&component))
				{
					return false;
				}

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
				case AshEngine::SceneComponentType::Environment:
					return !entity.has_environment_component() || entity.remove_environment_component();
				case AshEngine::SceneComponentType::Particle:
					return !entity.has_particle_component() || entity.remove_particle_component();
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
			case AshEngine::SceneComponentType::Environment:
				return refEntity.has_environment_component()
					? MakeComponentSnapshot(eType, refEntity.get_environment_component())
					: std::nullopt;
			case AshEngine::SceneComponentType::Particle:
				return refEntity.has_particle_component()
					? MakeComponentSnapshot(eType, refEntity.get_particle_component())
					: std::nullopt;
			default:
				return std::nullopt;
			}
		}

		std::vector<SceneComponentSnapshot> CaptureComponentSnapshots(const AshEngine::Entity& refEntity)
		{
			std::vector<SceneComponentSnapshot> vecSnapshots{};
			for (const AshEngine::SceneComponentType eType : refEntity.get_component_types())
			{
				std::optional<SceneComponentSnapshot> optSnapshot = CaptureComponentSnapshot(refEntity, eType);
				if (optSnapshot.has_value())
				{
					vecSnapshots.push_back(std::move(*optSnapshot));
				}
			}

			return vecSnapshots;
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
				AshEngine::SceneComponentType::Environment,
				AshEngine::SceneComponentType::Particle,
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
				case AshEngine::SceneComponentType::Environment:
					if (!ApplyOptionalComponent<AshEngine::EnvironmentComponent>(
						entity,
						refComponentSnapshot,
						&AshEngine::Entity::has_environment_component,
						&AshEngine::Entity::set_environment_component,
						&AshEngine::Entity::add_environment_component))
					{
						return false;
					}
					break;
				case AshEngine::SceneComponentType::Particle:
					if (!ApplyOptionalComponent<AshEngine::ParticleComponent>(
						entity,
						refComponentSnapshot,
						&AshEngine::Entity::has_particle_component,
						&AshEngine::Entity::set_particle_component,
						&AshEngine::Entity::add_particle_component))
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
	}
}
