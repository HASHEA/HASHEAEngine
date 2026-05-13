#pragma once

#include "Base/input/Input.h"
#include "Core/EditorSceneTypes.h"
#include "Core/IEditorViewportBindingResolver.h"
#include "Function/Gui/UICommon.h"

#include <glm/vec3.hpp>

#include <string>
#include <unordered_map>

namespace AshEditor
{
	class SceneService;

	struct EditorViewportCameraInputContext
	{
		std::string strViewportId{};
		AshEngine::UIRect rectContent{};
		bool bViewportFocused = false;
		bool bViewportHovered = false;
		bool bAcceptsInput = false;
		SceneEntityId uFocusEntityId = 0;
	};

	class EditorViewportCameraService final : public IEditorViewportBindingResolver
	{
	public:
		void SyncFromScene(const SceneService& refSceneService);

		void UpdateViewportInput(
			const SceneService& refSceneService,
			const AshEngine::InputState& refInput,
			double dTimeSeconds,
			const EditorViewportCameraInputContext& refContext);

		bool TryResolveViewportBinding(
			const std::string& strViewportId,
			EditorViewportBindingOverride& outOverride) const override;

		void Reset();

	private:
		struct ViewportCameraState
		{
			AshEngine::Scene previewScene{};
			AshEngine::EntityId uCameraEntityId = 0;
			uint64_t uSourceSceneChangeVersion = 0;
			std::string strSourceSceneName{};
			std::string strSourceScenePath{};
			glm::vec3 vecPosition{ 0.0f, 1.5f, -5.0f };
			glm::vec3 vecRotationEulerDegrees{ 12.0f, 0.0f, 0.0f };
			float fMoveSpeed = 8.0f;
			bool bMouseLookActive = false;
			bool bHasLastMousePosition = false;
			double dLastMouseX = 0.0;
			double dLastMouseY = 0.0;
			double dLastUpdateTimeSeconds = -1.0;
		};

	private:
		static constexpr AshEngine::EntityId kEditorCameraEntityId = 0xffff'ffff'ffff'fff0ull;

		ViewportCameraState& EnsureState(const std::string& strViewportId);
		const ViewportCameraState* FindState(const std::string& strViewportId) const;
		void SyncPreviewScene(
			const SceneService& refSceneService,
			const std::string& strViewportId,
			ViewportCameraState& refState);
		void SeedCameraFromSceneContent(const AshEngine::Scene& refScene, ViewportCameraState& refState) const;
		void ApplyCameraToPreview(ViewportCameraState& refState) const;
		void FocusEntity(
			const SceneService& refSceneService,
			const std::string& strViewportId,
			ViewportCameraState& refState,
			SceneEntityId uEntityId);
		static bool IsSupportedSceneViewport(const std::string& strViewportId);

	private:
		std::unordered_map<std::string, ViewportCameraState> _mapStates{};
	};
}
