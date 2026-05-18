#pragma once

#include "Function/Scene/SceneComponents.h"

#include <string>
#include <string_view>

namespace AshEditor::SceneComponentSerialization
{
	// Serializes only descriptor-backed fields and preserves the existing snapshot JSON shape.
	std::string SerializeComponentPayload(
		const void* pComponentData,
		const AshEngine::SceneComponentDesc& refComponentDesc);

	// Applies descriptor-backed fields from a serialized payload string.
	// Returns false only when the payload itself is not valid JSON.
	bool DeserializeComponentPayload(
		std::string_view svPayloadJson,
		const AshEngine::SceneComponentDesc& refComponentDesc,
		void* pComponentData);
}
