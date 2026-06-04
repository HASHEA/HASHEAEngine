#pragma once

#include "Core/EditorViewportInputState.h"
#include "Core/EditorSceneTypes.h"
#include "Core/IEditorViewportBindingResolver.h"
#include "Function/Gui/UICommon.h"

#include <cstdint>
#include <glm/vec3.hpp>

#include <string>
#include <unordered_map>

namespace AshEditor
{
	class AssetDatabaseService;
	class SceneService;
}

namespace AshEngine
{
	struct SceneRay;
}

namespace AshEditor
{

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
		static constexpr float kMinMoveSpeed = 0.25f;
		static constexpr float kMaxMoveSpeed = 256.0f;

	public:
		void SetDefaultMoveSpeed(float fMoveSpeed);
		float GetMoveSpeed(const std::string& strViewportId) const;
		void SetMoveSpeed(const std::string& strViewportId, float fMoveSpeed);

		void SyncFromScene(
			const SceneService& refSceneService,
			const AssetDatabaseService& refAssetDatabaseService);

		void UpdateViewportInput(
			const SceneService& refSceneService,
			const AssetDatabaseService& refAssetDatabaseService,
			const EditorViewportInputState& refInput,
			double dTimeSeconds,
			const EditorViewportCameraInputContext& refContext);

		bool TryResolveViewportBinding(
			const std::string& strViewportId,
			EditorViewportBindingOverride& outOverride) const override;

		bool TryBuildViewportRay(
			const std::string& strViewportId,
			const AshEngine::UIRect& rectContent,
			const AshEngine::UIVec2& vecMousePosition,
			AshEngine::SceneRay& outRay) const;

		void Reset();

	private:
		enum class CameraDragMode : uint8_t
		{
			None = 0,
			Orbit,
			Pan,
			Dolly
		};

		struct ViewportCameraState
		{
			AshEngine::Scene* pScene = nullptr;
			std::string strSourceSceneName{};
			std::string strSourceScenePath{};
			glm::vec3 vecPosition{ 0.0f, 1.5f, -5.0f };
			glm::vec3 vecRotationEulerDegrees{ 12.0f, 0.0f, 0.0f };
			glm::vec3 vecOrbitTarget{ 0.0f, 0.9f, 0.0f };
			AshEngine::SceneViewCameraOverride cameraOverride{};
			uint32_t uViewportWidth = 1;
			uint32_t uViewportHeight = 1;
			float fFovYDegrees = 60.0f;
			float fNearPlane = 0.03f;
			float fFarPlane = 2000.0f;
			float fMoveSpeed = 8.0f;
			float fOrbitDistance = 6.0f;
			CameraDragMode eDragMode = CameraDragMode::None;
			bool bHasLastMousePosition = false;
			bool bInitialized = false;
			double dLastMouseX = 0.0;
			double dLastMouseY = 0.0;
		};

	private:
		ViewportCameraState& EnsureState(const std::string& strViewportId);
		const ViewportCameraState* FindState(const std::string& strViewportId) const;
		static float ClampMoveSpeed(float fMoveSpeed);
		static float ClampOrbitDistance(float fOrbitDistance);
		void SyncCameraState(
			const SceneService& refSceneService,
			const AssetDatabaseService& refAssetDatabaseService,
			const std::string& strViewportId,
			ViewportCameraState& refState);
		void SeedCameraFromSceneContent(
			const AshEngine::Scene& refScene,
			const AssetDatabaseService& refAssetDatabaseService,
			ViewportCameraState& refState) const;
		void RefreshCameraOverride(ViewportCameraState& refState) const;
		void UpdateViewportExtent(const AshEngine::UIRect& rectContent, ViewportCameraState& refState) const;
		void UpdatePositionFromOrbit(ViewportCameraState& refState) const;
		void FocusEntity(
			const SceneService& refSceneService,
			const AssetDatabaseService& refAssetDatabaseService,
			const std::string& strViewportId,
			ViewportCameraState& refState,
			SceneEntityId uEntityId);
		static bool IsSupportedSceneViewport(const std::string& strViewportId);

	private:
		float _fDefaultMoveSpeed = 8.0f;
		std::unordered_map<std::string, ViewportCameraState> _mapStates{};
	};
}
