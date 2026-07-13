#pragma once

#include "Core/EditorPanel.h"
#include "Widgets/PropertyEditor/PropertyEditorRegistry.h"
#include "Widgets/PropertyEditor/PropertyEditorWidget.h"

#include <cstdint>
#include <string>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace AshEditor
{
	// Development-only showcase that exercises every builtin property type editor, plus a
	// custom enum registered purely through EnumTypeRegistry (proving extension needs no core edit).
	class PropertyEditorDemoPanel final : public EditorPanel
	{
	public:
		PropertyEditorDemoPanel();

	public:
		void OnGui(const EditorFrameContext& refFrameContext) override;

	private:
		PropertyEditorRegistry _registry{};
		PropertyEditorWidget _widget;

		bool _bSample = true;
		int32_t _iSample = 5;
		uint32_t _uSample = 12u;
		float _fSample = 0.5f;
		glm::vec2 _v2Sample{ 1.0f, 2.0f };
		glm::vec3 _v3Sample{ 1.0f, 0.0f, 0.0f };
		glm::vec4 _v4Sample{ 0.0f, 1.0f, 0.0f, 1.0f };
		glm::vec3 _color3Sample{ 0.8f, 0.4f, 0.2f };
		glm::vec4 _color4Sample{ 0.2f, 0.6f, 0.9f, 1.0f };
		std::string _strSample{ "hello" };
		std::string _strAssetPath{ "assets/textures/default.png" };
		int32_t _iEngineEnum = 0;
		int32_t _iCustomEnum = 0;
	};
}
