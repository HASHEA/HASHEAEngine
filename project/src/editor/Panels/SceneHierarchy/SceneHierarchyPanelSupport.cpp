#include "Panels/SceneHierarchy/SceneHierarchyPanelSupport.h"

#include "Base/hlog.h"
#include "Core/EditorIds.h"
#include "Core/EntityCommands.h"
#include "Core/IEditorCommandExecutor.h"
#include "Function/Gui/UIContext.h"
#include "Function/Scene/Scene.h"
#include "Services/CommandService.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"
#include "Widgets/EditorActionWidgets.h"
#include "Widgets/EditorThemeColors.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace AshEditor
{
	namespace
	{
		constexpr const char* kSceneEntityContextPopupId = "SceneHierarchyEntityContextMenu";
		constexpr const char* kSceneContentContextPopupId = "SceneHierarchyContentContextMenu";

		constexpr const char* GetCreatePresetMenuLabel(const SceneEntityCreatePreset ePreset)
		{
			switch (ePreset)
			{
			case SceneEntityCreatePreset::Mesh:
				return "New Mesh Entity";
			case SceneEntityCreatePreset::DirectionalLight:
				return "Directional Light";
			case SceneEntityCreatePreset::PointLight:
				return "Point Light";
			case SceneEntityCreatePreset::SpotLight:
				return "Spot Light";
			case SceneEntityCreatePreset::Camera:
				return "New Camera";
			case SceneEntityCreatePreset::Empty:
			default:
				return "New Empty Entity";
			}
		}

		std::string BuildCreatedEntityName(const AshEngine::Scene& refScene, const SceneEntityCreatePreset ePreset)
		{
			const uint32_t uNextIndex = refScene.get_entity_count() + 1;
			switch (ePreset)
			{
			case SceneEntityCreatePreset::Mesh:
				return "Mesh Entity " + std::to_string(uNextIndex);
			case SceneEntityCreatePreset::DirectionalLight:
				return "Directional Light " + std::to_string(uNextIndex);
			case SceneEntityCreatePreset::PointLight:
				return "Point Light " + std::to_string(uNextIndex);
			case SceneEntityCreatePreset::SpotLight:
				return "Spot Light " + std::to_string(uNextIndex);
			case SceneEntityCreatePreset::Camera:
				return "Camera " + std::to_string(uNextIndex);
			case SceneEntityCreatePreset::Empty:
			default:
				return "Entity " + std::to_string(uNextIndex);
			}
		}

		bool IsLightCreatePreset(const SceneEntityCreatePreset ePreset)
		{
			switch (ePreset)
			{
			case SceneEntityCreatePreset::DirectionalLight:
			case SceneEntityCreatePreset::PointLight:
			case SceneEntityCreatePreset::SpotLight:
				return true;
			default:
				return false;
			}
		}

		AshEngine::LightType GetCreatePresetLightType(const SceneEntityCreatePreset ePreset)
		{
			switch (ePreset)
			{
			case SceneEntityCreatePreset::PointLight:
				return AshEngine::LightType::Point;
			case SceneEntityCreatePreset::SpotLight:
				return AshEngine::LightType::Spot;
			case SceneEntityCreatePreset::DirectionalLight:
			default:
				return AshEngine::LightType::Directional;
			}
		}

		bool MatchesAllEntities(const AshEngine::Entity&)
		{
			return true;
		}

		bool MatchesCameraEntities(const AshEngine::Entity& refEntity)
		{
			return refEntity.has_camera_component();
		}

		bool MatchesLightEntities(const AshEngine::Entity& refEntity)
		{
			return refEntity.has_light_component();
		}

		bool MatchesMeshEntities(const AshEngine::Entity& refEntity)
		{
			return refEntity.has_mesh_component();
		}

		constexpr std::array<SceneHierarchyEntityFilterOption, 4> kSceneHierarchyEntityFilters{ {
			{ "All", &MatchesAllEntities },
			{ "Camera", &MatchesCameraEntities },
			{ "Light", &MatchesLightEntities },
			{ "Mesh", &MatchesMeshEntities },
		} };

		void AppendVisibleEntityIds(const AshEngine::Entity& refEntity, std::vector<SceneEntityId>& vecOutEntityIds)
		{
			if (!refEntity.is_valid())
			{
				return;
			}

			vecOutEntityIds.push_back(refEntity.get_id());
			for (const AshEngine::Entity& refChild : refEntity.get_children())
			{
				AppendVisibleEntityIds(refChild, vecOutEntityIds);
			}
		}

		void AppendReparentCandidates(
			const SceneService& refSceneService,
			const SceneEntityId uSceneEntityId,
			const AshEngine::Entity& refEntity,
			std::vector<SceneEntityId>& vecOutIds,
			std::vector<std::string>& vecOutLabels,
			const uint32_t uDepth)
		{
			if (refEntity.get_id() != uSceneEntityId &&
				refSceneService.CanReparentEntity(uSceneEntityId, refEntity.get_id()))
			{
				vecOutIds.push_back(refEntity.get_id());
				vecOutLabels.push_back(std::string(uDepth * 2, ' ') + refEntity.get_name());
			}

			for (const AshEngine::Entity& refChild : refEntity.get_children())
			{
				AppendReparentCandidates(
					refSceneService,
					uSceneEntityId,
					refChild,
					vecOutIds,
					vecOutLabels,
					uDepth + 1);
			}
		}

		void SelectEntityRange(
			const SceneHierarchyPanelDeps& refDeps,
			SceneHierarchyPanelState& refState,
			const SceneEntityId uTargetEntityId,
			const std::vector<SceneEntityId>& vecVisibleEntityIds)
		{
			if (!refDeps.pSceneService || !refDeps.pSelectionService || uTargetEntityId == 0)
			{
				return;
			}

			std::vector<SceneEntityId>::const_iterator itAnchor =
				std::find(vecVisibleEntityIds.begin(), vecVisibleEntityIds.end(), refState.uRangeSelectionAnchorEntityId);
			std::vector<SceneEntityId>::const_iterator itTarget =
				std::find(vecVisibleEntityIds.begin(), vecVisibleEntityIds.end(), uTargetEntityId);
			if (itAnchor == vecVisibleEntityIds.end() || itTarget == vecVisibleEntityIds.end())
			{
				const AshEngine::Entity entityTarget = refDeps.pSceneService->FindEntity(uTargetEntityId);
				if (entityTarget.is_valid())
				{
					refDeps.pSelectionService->SelectSingle({
						EditorSelectionKind::Entity,
						uTargetEntityId,
						entityTarget.get_name(),
						{}
					});
					refState.uRangeSelectionAnchorEntityId = uTargetEntityId;
				}
				return;
			}

			if (itTarget < itAnchor)
			{
				std::swap(itAnchor, itTarget);
			}

			std::vector<EditorSelection> vecSelections{};
			vecSelections.reserve(static_cast<size_t>(std::distance(itAnchor, itTarget)) + 1u);
			for (std::vector<SceneEntityId>::const_iterator itEntityId = itAnchor; itEntityId <= itTarget; ++itEntityId)
			{
				const AshEngine::Entity entity = refDeps.pSceneService->FindEntity(*itEntityId);
				if (!entity.is_valid())
				{
					continue;
				}

				vecSelections.push_back({
					EditorSelectionKind::Entity,
					entity.get_id(),
					entity.get_name(),
					{}
				});
			}

			refDeps.pSelectionService->SelectRange(vecSelections);
		}

		void DestroyEntity(const SceneHierarchyPanelDeps& refDeps, const SceneEntityId uSceneEntityId)
		{
			if (!refDeps.pSceneService || !refDeps.pSelectionService || !refDeps.pCommandExecutor)
			{
				HLogWarning(
					"SceneHierarchy delete entity skipped (scene_service={}, selection_service={}, command_executor={}).",
					refDeps.pSceneService != nullptr,
					refDeps.pSelectionService != nullptr,
					refDeps.pCommandExecutor != nullptr);
				return;
			}

			if (uSceneEntityId == 0)
			{
				HLogInfo("SceneHierarchy delete entity skipped because entityId is 0.");
				return;
			}

			if (!refDeps.pCommandExecutor->ExecuteCommand(std::make_unique<DeleteEntityCommand>(uSceneEntityId)))
			{
				HLogWarning("SceneHierarchy failed to delete entity {}.", static_cast<unsigned long long>(uSceneEntityId));
			}
		}
	}

	const std::array<SceneHierarchyEntityFilterOption, 4>& GetSceneHierarchyEntityFilters()
	{
		return kSceneHierarchyEntityFilters;
	}

	SceneEntityId GetSelectedSceneEntityId(const SceneHierarchyPanelDeps& refDeps)
	{
		if (!refDeps.pSelectionService || refDeps.pSelectionService->HasMultipleSelections())
		{
			return 0;
		}

		const EditorSelection& refSelection = refDeps.pSelectionService->GetSelection();
		return refSelection.eKind == EditorSelectionKind::Entity ? refSelection.uId : 0;
	}

	SceneEntityId GetEntityParentId(const AshEngine::Entity& refEntity)
	{
		const AshEngine::Entity entityParent = refEntity.get_parent();
		return entityParent.is_valid() ? entityParent.get_id() : 0;
	}

	uint32_t GetEntitySiblingIndex(const AshEngine::Entity& refEntity, const SceneService& refSceneService)
	{
		return refEntity.is_valid() ? refSceneService.GetEntitySiblingIndex(refEntity.get_id()) : 0u;
	}

	uint32_t GetParentChildCount(const SceneService& refSceneService, const SceneEntityId uParentId)
	{
		if (uParentId == 0)
		{
			return static_cast<uint32_t>(refSceneService.GetActiveScene().get_root_entities().size());
		}

		const AshEngine::Entity entityParent = refSceneService.FindEntity(uParentId);
		return entityParent.is_valid() ? static_cast<uint32_t>(entityParent.get_children().size()) : 0u;
	}

	std::vector<SceneEntityId> BuildVisibleEntityIds(const AshEngine::Scene& refScene)
	{
		std::vector<SceneEntityId> vecEntityIds{};
		for (const AshEngine::Entity& refRoot : refScene.get_root_entities())
		{
			AppendVisibleEntityIds(refRoot, vecEntityIds);
		}
		return vecEntityIds;
	}

	std::string BuildEntityPathLabel(const AshEngine::Entity& refEntity)
	{
		if (!refEntity.is_valid())
		{
			return {};
		}

		std::vector<std::string> vecParts{};
		AshEngine::Entity current = refEntity;
		for (uint32_t uGuard = 0; uGuard < 64 && current.is_valid(); ++uGuard)
		{
			vecParts.push_back(current.get_name());
			current = current.get_parent();
		}

		std::string strPath{};
		for (std::vector<std::string>::reverse_iterator it = vecParts.rbegin(); it != vecParts.rend(); ++it)
		{
			if (!strPath.empty())
			{
				strPath += "/";
			}
			strPath += *it;
		}
		return strPath;
	}

	void BuildReparentCandidateLists(
		const SceneService& refSceneService,
		const SceneEntityId uSceneEntityId,
		std::vector<SceneEntityId>& vecOutIds,
		std::vector<std::string>& vecOutLabels)
	{
		vecOutIds.clear();
		vecOutLabels.clear();
		vecOutIds.push_back(0);
		vecOutLabels.push_back("<Root>");

		for (const AshEngine::Entity& refRoot : refSceneService.GetActiveScene().get_root_entities())
		{
			AppendReparentCandidates(refSceneService, uSceneEntityId, refRoot, vecOutIds, vecOutLabels, 0);
		}
	}

	EditorTreeWidgetStyle MakeSceneTreeStyle(AshEngine::UIContext& refUi)
	{
		EditorTreeWidgetStyle style{};
		style.fRowHeight = 24.0f;
		style.fIndentSpacing = 12.0f;
		style.fIconSize = 16.0f;
		style.fIconTextSpacing = 4.0f;
		style.fRowPaddingY = 5.0f;
		style.fRowSpacingY = 3.0f;
		style.fConnectorHorizontalPadding = 3.0f;
		style.fDropZoneRatio = 0.34f;
		style.fGuideLinePaddingY = 0.0f;
		style.fAutoExpandHoverDelaySeconds = 0.45f;
		style.colorGuideLine = GetEditorGuideLineColor(refUi);
		style.colorDropAccent = GetEditorDropAccentColor(refUi);
		style.colorRowHoverFill = GetEditorRowHoverFillColor(refUi);
		style.colorRowHoverOutline = GetEditorRowHoverOutlineColor(refUi);
		style.colorRowSelectedFill = GetEditorRowSelectedFillColor(refUi);
		style.colorRowSelectedOutline = GetEditorRowSelectedOutlineColor(refUi);
		return style;
	}

	bool IsSceneEntityDragActive(const AshEngine::UIContext* pUiContext)
	{
		return pUiContext && pUiContext->is_drag_drop_payload_active(EditorDragPayloadTypes::SceneEntity);
	}

	void ExecuteCreateRoot(SceneHierarchyPanelState& refState, const SceneHierarchyPanelDeps& refDeps)
	{
		refState.uCreateChildAnchorParentId = 0;
		refState.bAwaitingCreateChildSelection = false;
		CreateEntity(refDeps, 0, SceneEntityCreatePreset::Empty);
	}

	void ExecuteCreateChildFromSelection(SceneHierarchyPanelState& refState, const SceneHierarchyPanelDeps& refDeps)
	{
		const SceneEntityId uSelectedSceneEntityId = GetSelectedSceneEntityId(refDeps);
		if (uSelectedSceneEntityId == 0)
		{
			refState.uCreateChildAnchorParentId = 0;
			refState.bAwaitingCreateChildSelection = false;
			return;
		}

		if (refState.uCreateChildAnchorParentId == 0)
		{
			refState.uCreateChildAnchorParentId = uSelectedSceneEntityId;
		}

		refState.bAwaitingCreateChildSelection = true;
		if (!CreateEntity(refDeps, refState.uCreateChildAnchorParentId, SceneEntityCreatePreset::Empty))
		{
			refState.uCreateChildAnchorParentId = 0;
			refState.bAwaitingCreateChildSelection = false;
		}
	}

	void ExecuteCopySelection(SceneHierarchyPanelState& refState, const SceneHierarchyPanelDeps& refDeps)
	{
		if (!refDeps.pSceneService || !refDeps.pSelectionService)
		{
			HLogWarning(
				"SceneHierarchy copy skipped (scene_service={}, selection_service={}).",
				refDeps.pSceneService != nullptr,
				refDeps.pSelectionService != nullptr);
			return;
		}

		const std::vector<SceneEntityId> vecSourceEntityIds =
			refDeps.pSceneService->BuildTopLevelEntityIds(
				refDeps.pSelectionService->GetSelectedIds(EditorSelectionKind::Entity));
		if (vecSourceEntityIds.empty())
		{
			HLogInfo("SceneHierarchy copy skipped because no valid entity is selected.");
			return;
		}

		std::vector<SceneEntitySnapshot> vecSnapshots{};
		std::vector<SceneEntityId> vecPreferredParentEntityIds{};
		vecSnapshots.reserve(vecSourceEntityIds.size());
		vecPreferredParentEntityIds.reserve(vecSourceEntityIds.size());
		for (const SceneEntityId uSourceEntityId : vecSourceEntityIds)
		{
			const AshEngine::Entity entity = refDeps.pSceneService->FindEntity(uSourceEntityId);
			const std::optional<SceneEntitySnapshot> optSnapshot =
				refDeps.pSceneService->CaptureEntitySnapshot(uSourceEntityId);
			if (!entity.is_valid() || !optSnapshot.has_value())
			{
				HLogWarning(
					"SceneHierarchy failed to copy entity {}.",
					static_cast<unsigned long long>(uSourceEntityId));
				return;
			}

			vecSnapshots.push_back(*optSnapshot);
			vecPreferredParentEntityIds.push_back(GetEntityParentId(entity));
		}

		refState.vecClipboardEntitySnapshots = std::move(vecSnapshots);
		refState.vecClipboardPreferredParentEntityIds = std::move(vecPreferredParentEntityIds);
	}

	void ExecutePasteSelection(const SceneHierarchyPanelState& refState, const SceneHierarchyPanelDeps& refDeps)
	{
		if (!refDeps.pCommandExecutor)
		{
			HLogWarning("SceneHierarchy paste skipped because command executor is unavailable.");
			return;
		}
		if (refState.vecClipboardEntitySnapshots.empty())
		{
			HLogInfo("SceneHierarchy paste skipped because clipboard is empty.");
			return;
		}

		if (!refDeps.pCommandExecutor->ExecuteCommand(
			std::make_unique<PasteEntitySnapshotsCommand>(
				refState.vecClipboardEntitySnapshots,
				refState.vecClipboardPreferredParentEntityIds)))
		{
			HLogWarning(
				"SceneHierarchy failed to paste {} entities.",
				static_cast<unsigned long long>(refState.vecClipboardEntitySnapshots.size()));
		}
	}

	void ExecuteDuplicateSelection(const SceneHierarchyPanelDeps& refDeps)
	{
		if (!refDeps.pSceneService || !refDeps.pSelectionService || !refDeps.pCommandExecutor)
		{
			HLogWarning(
				"SceneHierarchy duplicate skipped (scene_service={}, selection_service={}, command_executor={}).",
				refDeps.pSceneService != nullptr,
				refDeps.pSelectionService != nullptr,
				refDeps.pCommandExecutor != nullptr);
			return;
		}

		const std::vector<SceneEntityId> vecSourceEntityIds =
			refDeps.pSceneService->BuildTopLevelEntityIds(
				refDeps.pSelectionService->GetSelectedIds(EditorSelectionKind::Entity));
		if (vecSourceEntityIds.empty())
		{
			HLogInfo("SceneHierarchy duplicate skipped because no valid entity is selected.");
			return;
		}

		if (!refDeps.pCommandExecutor->ExecuteCommand(std::make_unique<DuplicateEntitiesCommand>(vecSourceEntityIds)))
		{
			HLogWarning(
				"SceneHierarchy failed to duplicate {} entities.",
				static_cast<unsigned long long>(vecSourceEntityIds.size()));
		}
	}

	bool CanPasteSelection(const SceneHierarchyPanelState& refState)
	{
		return !refState.vecClipboardEntitySnapshots.empty();
	}

	bool CreateEntity(
		const SceneHierarchyPanelDeps& refDeps,
		const SceneEntityId uParentId,
		const SceneEntityCreatePreset ePreset)
	{
		if (!refDeps.pSceneService || !refDeps.pCommandExecutor)
		{
			HLogWarning(
				"SceneHierarchy create entity skipped (scene_service={}, command_executor={}).",
				refDeps.pSceneService != nullptr,
				refDeps.pCommandExecutor != nullptr);
			return false;
		}

		AshEngine::Scene& refScene = refDeps.pSceneService->GetActiveScene();
		const std::string strEntityName = BuildCreatedEntityName(refScene, ePreset);

		if (ePreset == SceneEntityCreatePreset::Empty)
		{
			const bool bExecuted =
				refDeps.pCommandExecutor->ExecuteCommand(std::make_unique<CreateEntityCommand>(strEntityName, uParentId));
			if (!bExecuted)
			{
				HLogWarning("SceneHierarchy failed to create entity '{}'.", strEntityName);
			}
			return bExecuted;
		}

		if (!refDeps.pSelectionService)
		{
			HLogWarning(
				"SceneHierarchy failed to create preset entity '{}' because selection_service is unavailable.",
				strEntityName);
			return false;
		}

		// Keep preset creation atomic so one undo step always maps to one visible entity lifecycle change.
		if (!refDeps.pCommandExecutor->BeginCommandTransaction("Create Entity"))
		{
			HLogWarning(
				"SceneHierarchy failed to start create-entity transaction for preset '{}' (parent={}).",
				GetCreatePresetMenuLabel(ePreset),
				static_cast<unsigned long long>(uParentId));
			return false;
		}

		if (!refDeps.pCommandExecutor->ExecuteCommand(std::make_unique<CreateEntityCommand>(strEntityName, uParentId)))
		{
			refDeps.pCommandExecutor->CancelCommandTransaction();
			HLogWarning("SceneHierarchy failed to create entity '{}'.", strEntityName);
			return false;
		}

		const SceneEntityId uCreatedSceneEntityId = GetSelectedSceneEntityId(refDeps);
		if (uCreatedSceneEntityId == 0)
		{
			refDeps.pCommandExecutor->CancelCommandTransaction();
			HLogWarning(
				"SceneHierarchy created '{}', but could not resolve the new entity selection for preset component setup.",
				strEntityName);
			return false;
		}

		bool bComponentApplied = true;
		switch (ePreset)
		{
		case SceneEntityCreatePreset::Mesh:
		{
			AshEngine::MeshComponent meshComponent{};
			// A hierarchy-created mesh preset starts as a placeholder until the user assigns a real mesh asset.
			// Keeping it hidden avoids poisoning scene render extraction with an empty asset path.
			meshComponent.visible = false;
			bComponentApplied = refDeps.pCommandExecutor->ExecuteCommand(
				std::make_unique<SetMeshComponentCommand>(
					uCreatedSceneEntityId,
					std::optional<AshEngine::MeshComponent>{},
					std::optional<AshEngine::MeshComponent>{ meshComponent }));
			break;
		}
		case SceneEntityCreatePreset::DirectionalLight:
		case SceneEntityCreatePreset::PointLight:
		case SceneEntityCreatePreset::SpotLight:
		{
			AshEngine::LightComponent lightComponent{};
			lightComponent.type = GetCreatePresetLightType(ePreset);
			bComponentApplied = refDeps.pCommandExecutor->ExecuteCommand(
				std::make_unique<SetLightComponentCommand>(
					uCreatedSceneEntityId,
					std::optional<AshEngine::LightComponent>{},
					std::optional<AshEngine::LightComponent>{ lightComponent }));
			break;
		}
		case SceneEntityCreatePreset::Camera:
		{
			AshEngine::CameraComponent cameraComponent{};
			cameraComponent.primary = false;
			bComponentApplied = refDeps.pCommandExecutor->ExecuteCommand(
				std::make_unique<SetCameraComponentCommand>(
					uCreatedSceneEntityId,
					std::optional<AshEngine::CameraComponent>{},
					std::optional<AshEngine::CameraComponent>{ cameraComponent }));
			break;
		}
		case SceneEntityCreatePreset::Empty:
		default:
			break;
		}

		if (!bComponentApplied)
		{
			refDeps.pCommandExecutor->CancelCommandTransaction();
			HLogWarning(
				"SceneHierarchy failed to apply preset component(s) for entity '{}' (id={}).",
				strEntityName,
				static_cast<unsigned long long>(uCreatedSceneEntityId));
			return false;
		}

		if (!refDeps.pCommandExecutor->CommitCommandTransaction())
		{
			refDeps.pCommandExecutor->CancelCommandTransaction();
			HLogWarning(
				"SceneHierarchy failed to commit create-entity transaction for '{}' (id={}).",
				strEntityName,
				static_cast<unsigned long long>(uCreatedSceneEntityId));
			return false;
		}

		return true;
	}

	bool DrawCreateEntityMenuItems(
		AshEngine::UIContext& refUi,
		const SceneHierarchyPanelDeps& refDeps,
		const SceneEntityId uParentId)
	{
		bool bTriggered = false;
		const std::array<SceneEntityCreatePreset, 6> arrPresets{
			SceneEntityCreatePreset::Empty,
			SceneEntityCreatePreset::Mesh,
			SceneEntityCreatePreset::DirectionalLight,
			SceneEntityCreatePreset::PointLight,
			SceneEntityCreatePreset::SpotLight,
			SceneEntityCreatePreset::Camera
		};

		for (const SceneEntityCreatePreset ePreset : arrPresets)
		{
			if (IsLightCreatePreset(ePreset))
			{
				continue;
			}

			if (refUi.menu_item(GetCreatePresetMenuLabel(ePreset)) && CreateEntity(refDeps, uParentId, ePreset))
			{
				bTriggered = true;
			}
		}

		if (refUi.begin_menu("New Light"))
		{
			for (const SceneEntityCreatePreset ePreset : arrPresets)
			{
				if (!IsLightCreatePreset(ePreset))
				{
					continue;
				}

				if (refUi.menu_item(GetCreatePresetMenuLabel(ePreset)) &&
					CreateEntity(refDeps, uParentId, ePreset))
				{
					bTriggered = true;
				}
			}

			refUi.end_menu();
		}

		return bTriggered;
	}

	void DestroySelectedEntities(
		const SceneHierarchyPanelDeps& refDeps,
		const std::vector<SceneEntityId>& vecPendingDeleteEntityIds)
	{
		if (!refDeps.pSceneService || !refDeps.pSelectionService || !refDeps.pCommandExecutor)
		{
			HLogWarning(
				"SceneHierarchy batch delete skipped (scene_service={}, selection_service={}, command_executor={}).",
				refDeps.pSceneService != nullptr,
				refDeps.pSelectionService != nullptr,
				refDeps.pCommandExecutor != nullptr);
			return;
		}

		std::vector<SceneEntityId> vecDeleteEntityIds = vecPendingDeleteEntityIds;
		if (vecDeleteEntityIds.empty())
		{
			vecDeleteEntityIds = refDeps.pSceneService->BuildTopLevelEntityIds(
				refDeps.pSelectionService->GetSelectedIds(EditorSelectionKind::Entity));
		}

		if (vecDeleteEntityIds.empty())
		{
			HLogInfo("SceneHierarchy batch delete skipped because no valid entity is selected.");
			return;
		}

		if (vecDeleteEntityIds.size() == 1u)
		{
			DestroyEntity(refDeps, vecDeleteEntityIds.front());
			return;
		}

		if (!refDeps.pCommandExecutor->ExecuteCommand(std::make_unique<DeleteEntitiesCommand>(vecDeleteEntityIds)))
		{
			HLogWarning(
				"SceneHierarchy failed to batch delete {} entities.",
				static_cast<unsigned long long>(vecDeleteEntityIds.size()));
		}
	}

	void SelectEntityFromHierarchy(
		const EditorFrameContext& refFrameContext,
		const SceneHierarchyPanelDeps& refDeps,
		SceneHierarchyPanelState& refState,
		const AshEngine::Entity& refEntity,
		const std::vector<SceneEntityId>& vecVisibleEntityIds)
	{
		if (!refDeps.pSelectionService || !refEntity.is_valid())
		{
			return;
		}

		const SceneEntityId uSceneEntityId = refEntity.get_id();
		const EditorSelection selection{
			EditorSelectionKind::Entity,
			uSceneEntityId,
			refEntity.get_name(),
			{}
		};
		const AshEngine::UIModifierFlags uModifiers = refFrameContext.pUiContext
			? refFrameContext.pUiContext->get_key_modifiers()
			: AshEngine::UIModifierFlagBits::None;
		const bool bRangeSelect =
			(uModifiers & AshEngine::UIModifierFlagBits::Shift) != 0 &&
			refState.uRangeSelectionAnchorEntityId != 0;
		const bool bToggleSelect = (uModifiers & AshEngine::UIModifierFlagBits::Ctrl) != 0;

		if (bRangeSelect)
		{
			SelectEntityRange(refDeps, refState, uSceneEntityId, vecVisibleEntityIds);
			return;
		}

		if (bToggleSelect)
		{
			refDeps.pSelectionService->Toggle(selection);
			refState.uRangeSelectionAnchorEntityId = uSceneEntityId;
			return;
		}

		refDeps.pSelectionService->SelectSingle(selection);
		refState.uRangeSelectionAnchorEntityId = uSceneEntityId;
	}

	void DrawEntityContextMenu(
		const EditorFrameContext& refFrameContext,
		const SceneHierarchyPanelDeps& refDeps,
		SceneHierarchyPanelState& refState,
		const AshEngine::Entity& refEntity)
	{
		if (!refFrameContext.pUiContext)
		{
			return;
		}

		AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
		if (!refUi.begin_popup_context_item(kSceneEntityContextPopupId))
		{
			return;
		}

		if (refDeps.pSelectionService &&
			!refDeps.pSelectionService->IsSelected(EditorSelectionKind::Entity, refEntity.get_id()))
		{
			refDeps.pSelectionService->SelectSingle({
				EditorSelectionKind::Entity,
				refEntity.get_id(),
				refEntity.get_name(),
				{}
			});
			refState.uRangeSelectionAnchorEntityId = refEntity.get_id();
		}

		if (refDeps.pCommandService && DrawEditorActionMenuItem(
			refUi,
			*refDeps.pCommandService,
			EditorActionIds::SceneCreateChild,
			"scene_hierarchy.entity_context"))
		{
			refUi.close_current_popup();
		}
		refUi.separator();
		if (refDeps.pCommandService && DrawEditorActionMenuItem(
			refUi,
			*refDeps.pCommandService,
			EditorActionIds::EditCopy,
			"scene_hierarchy.entity_context"))
		{
			refUi.close_current_popup();
		}
		if (refDeps.pCommandService && DrawEditorActionMenuItem(
			refUi,
			*refDeps.pCommandService,
			EditorActionIds::EditPaste,
			"scene_hierarchy.entity_context"))
		{
			refUi.close_current_popup();
		}
		refUi.separator();
		if (DrawCreateEntityMenuItems(refUi, refDeps, refEntity.get_id()))
		{
			refUi.close_current_popup();
		}
		refUi.separator();
		if (refDeps.pCommandService && DrawEditorActionMenuItem(
			refUi,
			*refDeps.pCommandService,
			EditorActionIds::SelectionRename,
			"scene_hierarchy.entity_context"))
		{
			refUi.close_current_popup();
		}
		if (refDeps.pCommandService && DrawEditorActionMenuItem(
			refUi,
			*refDeps.pCommandService,
			EditorActionIds::SelectionReparent,
			"scene_hierarchy.entity_context"))
		{
			refUi.close_current_popup();
		}
		if (refDeps.pCommandService && DrawEditorActionMenuItem(
			refUi,
			*refDeps.pCommandService,
			EditorActionIds::SelectionDuplicate,
			"scene_hierarchy.entity_context"))
		{
			refUi.close_current_popup();
		}
		refUi.separator();
		if (refDeps.pCommandService && DrawEditorActionMenuItem(
			refUi,
			*refDeps.pCommandService,
			EditorActionIds::SelectionDelete,
			"scene_hierarchy.entity_context"))
		{
			refUi.close_current_popup();
		}

		refUi.end_popup();
	}

	void DrawContentContextMenu(
		const EditorFrameContext& refFrameContext,
		const SceneHierarchyPanelDeps& refDeps,
		const SceneEntityId uSelectedSceneEntityId)
	{
		if (!refFrameContext.pUiContext)
		{
			return;
		}

		AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
		if (!refUi.begin_popup(kSceneContentContextPopupId))
		{
			return;
		}

		if (refDeps.pCommandService && DrawEditorActionMenuItem(
			refUi,
			*refDeps.pCommandService,
			EditorActionIds::SceneCreateRoot,
			"scene_hierarchy.content_context"))
		{
			refUi.close_current_popup();
		}

		refUi.separator();
		if (DrawCreateEntityMenuItems(refUi, refDeps, 0))
		{
			refUi.close_current_popup();
		}

		refUi.separator();
		if (refDeps.pCommandService && DrawEditorActionMenuItem(
			refUi,
			*refDeps.pCommandService,
			EditorActionIds::EditPaste,
			"scene_hierarchy.content_context"))
		{
			refUi.close_current_popup();
		}
		refUi.separator();
		const bool bCanCreateChild = uSelectedSceneEntityId != 0;
		if (refDeps.pCommandService && DrawEditorActionMenuItem(
			refUi,
			*refDeps.pCommandService,
			EditorActionIds::SceneCreateChild,
			"scene_hierarchy.content_context",
			bCanCreateChild))
		{
			refUi.close_current_popup();
		}

		refUi.end_popup();
	}
}
