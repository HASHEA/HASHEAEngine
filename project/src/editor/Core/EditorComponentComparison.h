#pragma once

#include "Function/Scene/SceneComponents.h"

#include <optional>

namespace AshEditor
{
	bool TransformComponentsEqual(
		const AshEngine::TransformComponent& refLeft,
		const AshEngine::TransformComponent& refRight);

	bool CameraComponentsEqual(
		const AshEngine::CameraComponent& refLeft,
		const AshEngine::CameraComponent& refRight);

	bool LightComponentsEqual(
		const AshEngine::LightComponent& refLeft,
		const AshEngine::LightComponent& refRight);

	bool MeshComponentsEqual(
		const AshEngine::MeshComponent& refLeft,
		const AshEngine::MeshComponent& refRight);

	bool EnvironmentComponentsEqual(
		const AshEngine::EnvironmentComponent& refLeft,
		const AshEngine::EnvironmentComponent& refRight);

	template<typename ComponentType>
	bool OptionalComponentsEqual(
		const std::optional<ComponentType>& optLeft,
		const std::optional<ComponentType>& optRight,
		bool (*pEqualsFn)(const ComponentType&, const ComponentType&))
	{
		if (optLeft.has_value() != optRight.has_value())
		{
			return false;
		}

		if (!optLeft.has_value())
		{
			return true;
		}

		return pEqualsFn(*optLeft, *optRight);
	}
}
