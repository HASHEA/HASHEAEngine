#include "Panels/ViewportPanelSupport.h"

#include "Core/AssetPresentationUtils.h"
#include "Core/EditorSelection.h"
#include "Core/EntityCommands.h"
#include "Core/IEditorCommandExecutor.h"
#include "Function/Gui/UIContext.h"
#include "Function/Render/ScenePresentationHandles.h"
#include "Function/Scene/SceneQuery.h"
#include "Panels/ViewportPanelSceneSupportInternal.h"
#include "Services/AssetDatabaseService.h"
#include "Services/DragDropTransferService.h"
#include "Services/EditorViewportCameraService.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace AshEditor
{
	namespace
	{
		bool TryBuildSceneInteractionRay(
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			const AshEngine::UIRect& rectContent,
			const AshEngine::UIVec2& vecMousePosition,
			AshEngine::SceneRay& outRay)
		{
			return
				refDeps.pViewportCameraService &&
				refDeps.pViewportCameraService->TryBuildViewportRay(
					strViewportId,
					rectContent,
					vecMousePosition,
					outRay);
		}

		bool TryQuerySceneInteraction(
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			const AshEngine::UIRect& rectContent,
			const AshEngine::UIVec2& vecMousePosition,
			AshEngine::SceneRay& outRay,
			std::vector<AshEngine::SceneRayHit>* pOutHits)
		{
			if (!refDeps.pSceneService || !refDeps.pAssetDatabaseService)
			{
				return false;
			}
			if (!TryBuildSceneInteractionRay(refDeps, strViewportId, rectContent, vecMousePosition, outRay))
			{
				return false;
			}

			if (!pOutHits)
			{
				return true;
			}

			AshEngine::AssetDatabase& refAssetDatabase = refDeps.pAssetDatabaseService->GetDatabase();
			*pOutHits = AshEngine::ray_cast_scene(
				refDeps.pSceneService->GetActiveScene(),
				refAssetDatabase,
				outRay);
			return true;
		}

		bool TryResolveScenePickPixel(
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			const AshEngine::UIRect& rectContent,
			const AshEngine::UIVec2& vecMousePosition,
			int32_t& outPixelX,
			int32_t& outPixelY)
		{
			if (!refDeps.pViewportService ||
				rectContent.width <= 1.0f ||
				rectContent.height <= 1.0f ||
				!ViewportPanelSupport::IsPointInRect(rectContent, vecMousePosition))
			{
				return false;
			}

			float fOutputWidth = rectContent.width;
			float fOutputHeight = rectContent.height;
			if (const EditorViewportRenderState* pRenderState = refDeps.pViewportService->GetRenderState(strViewportId))
			{
				if (pRenderState->uOutputWidth > 0u && pRenderState->uOutputHeight > 0u)
				{
					fOutputWidth = static_cast<float>(pRenderState->uOutputWidth);
					fOutputHeight = static_cast<float>(pRenderState->uOutputHeight);
				}
			}

			const float fLocalX = (vecMousePosition.x - rectContent.x) / rectContent.width * fOutputWidth;
			const float fLocalY = (vecMousePosition.y - rectContent.y) / rectContent.height * fOutputHeight;
			outPixelX = static_cast<int32_t>(std::clamp(std::floor(fLocalX), 0.0f, std::max(0.0f, fOutputWidth - 1.0f)));
			outPixelY = static_cast<int32_t>(std::clamp(std::floor(fLocalY), 0.0f, std::max(0.0f, fOutputHeight - 1.0f)));
			return true;
		}

		bool TryResolveSceneDropTransform(
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			const AshEngine::UIRect& rectContent,
			const AshEngine::UIVec2& vecMousePosition,
			AshEngine::TransformComponent& outTransform)
		{
			outTransform = {};
			outTransform.scale = { 1.0f, 1.0f, 1.0f };

			if (!refDeps.pSceneService || !refDeps.pAssetDatabaseService)
			{
				return false;
			}

			AshEngine::SceneRay ray{};
			if (!TryQuerySceneInteraction(
				refDeps,
				strViewportId,
				rectContent,
				vecMousePosition,
				ray,
				nullptr))
			{
				return false;
			}

			glm::vec3 vecDropPosition{};
			AshEngine::SceneDropPointDesc dropPointDesc{};
			dropPointDesc.camera_fallback_distance = 4.0f;
			if (AshEngine::find_scene_drop_point(
				refDeps.pSceneService->GetActiveScene(),
				refDeps.pAssetDatabaseService->GetDatabase(),
				ray,
				ray.origin,
				ray.direction,
				vecDropPosition,
				dropPointDesc))
			{
				outTransform.position = vecDropPosition;
				return true;
			}

			outTransform.position = ray.origin + ray.direction * 4.0f;
			return true;
		}

		EditorSelection BuildEntitySelection(const AshEngine::Entity& refEntity, uint64_t uFallbackEntityId)
		{
			EditorSelection selection{};
			selection.eKind = EditorSelectionKind::Entity;
			selection.uId = refEntity.is_valid() ? refEntity.get_id() : uFallbackEntityId;
			if (refEntity.is_valid())
			{
				selection.strLabel = refEntity.get_name();
			}
			return selection;
		}

		enum class SceneViewportBoxSelectionMode : uint8_t
		{
			Replace = 0,
			Toggle,
			Add,
			Remove
		};

		SceneViewportBoxSelectionMode ResolveBoxSelectionMode(AshEngine::UIModifierFlags uModifiers)
		{
			if ((uModifiers & AshEngine::UIModifierFlagBits::Ctrl) != 0)
			{
				return SceneViewportBoxSelectionMode::Toggle;
			}
			if ((uModifiers & AshEngine::UIModifierFlagBits::Shift) != 0)
			{
				return SceneViewportBoxSelectionMode::Add;
			}
			if ((uModifiers & AshEngine::UIModifierFlagBits::Alt) != 0)
			{
				return SceneViewportBoxSelectionMode::Remove;
			}
			return SceneViewportBoxSelectionMode::Replace;
		}

		void AppendSelectionIfMissing(std::vector<EditorSelection>& vecSelections, const EditorSelection& refSelection)
		{
			if (refSelection.IsEmpty())
			{
				return;
			}

			for (const EditorSelection& refExistingSelection : vecSelections)
			{
				if (refExistingSelection.eKind == refSelection.eKind && refExistingSelection.uId == refSelection.uId)
				{
					return;
				}
			}
			vecSelections.push_back(refSelection);
		}

		void RemoveSelectionByIdentity(
			std::vector<EditorSelection>& vecSelections,
			EditorSelectionKind eKind,
			uint64_t uId)
		{
			vecSelections.erase(
				std::remove_if(
					vecSelections.begin(),
					vecSelections.end(),
					[eKind, uId](const EditorSelection& refSelection)
					{
						return refSelection.eKind == eKind && refSelection.uId == uId;
					}),
				vecSelections.end());
		}

		bool ContainsSelectionIdentity(
			const std::vector<EditorSelection>& vecSelections,
			const EditorSelection& refSelection)
		{
			for (const EditorSelection& refExistingSelection : vecSelections)
			{
				if (refExistingSelection.eKind == refSelection.eKind && refExistingSelection.uId == refSelection.uId)
				{
					return true;
				}
			}
			return false;
		}

		std::vector<EditorSelection> BuildSelectionsAfterBoxSelection(
			const SelectionService& refSelectionService,
			const std::vector<EditorSelection>& vecBoxSelections,
			SceneViewportBoxSelectionMode eMode)
		{
			if (eMode == SceneViewportBoxSelectionMode::Replace)
			{
				return vecBoxSelections;
			}

			std::vector<EditorSelection> vecResult = refSelectionService.GetSelections();
			for (const EditorSelection& refSelection : vecBoxSelections)
			{
				switch (eMode)
				{
				case SceneViewportBoxSelectionMode::Toggle:
					if (ContainsSelectionIdentity(vecResult, refSelection))
					{
						RemoveSelectionByIdentity(vecResult, refSelection.eKind, refSelection.uId);
					}
					else
					{
						AppendSelectionIfMissing(vecResult, refSelection);
					}
					break;
				case SceneViewportBoxSelectionMode::Add:
					AppendSelectionIfMissing(vecResult, refSelection);
					break;
				case SceneViewportBoxSelectionMode::Remove:
					RemoveSelectionByIdentity(vecResult, refSelection.eKind, refSelection.uId);
					break;
				case SceneViewportBoxSelectionMode::Replace:
				default:
					break;
				}
			}
			return vecResult;
		}

		std::vector<EditorSelection> BuildSceneViewportBoxSelections(
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			const AshEngine::UIRect& rectContent,
			const AshEngine::UIRect& rectSelection)
		{
			std::vector<EditorSelection> vecSelections{};
			if (!refDeps.pSceneService || !refDeps.pAssetDatabaseService)
			{
				return vecSelections;
			}

			ViewportPanelSupport::Detail::SceneViewportProjectionContext projectionContext{};
			if (!ViewportPanelSupport::Detail::TryBuildSceneViewportProjectionContext(refDeps, strViewportId, rectContent, projectionContext))
			{
				return vecSelections;
			}

			const AshEngine::Scene& refScene = refDeps.pSceneService->GetActiveScene();
			AshEngine::AssetDatabase& refAssetDatabase = refDeps.pAssetDatabaseService->GetDatabase();
			const std::vector<AshEngine::Entity> vecEntities = refScene.get_entities();
			vecSelections.reserve(vecEntities.size());
			for (const AshEngine::Entity& refEntity : vecEntities)
			{
				if (!refEntity.is_valid())
				{
					continue;
				}

				bool bIntersectsSelection = false;
				if (refEntity.has_mesh_component())
				{
					AshEngine::SceneWorldBounds bounds{};
					AshEngine::UIRect rectEntityScreen{};
					bIntersectsSelection =
						AshEngine::get_entity_world_bounds(refScene, refAssetDatabase, refEntity.get_id(), bounds) &&
						ViewportPanelSupport::Detail::TryProjectWorldBoundsToViewportRect(projectionContext, bounds, rectEntityScreen) &&
						ViewportPanelSupport::RectsIntersect(rectSelection, rectEntityScreen);
				}
				else if (refEntity.has_camera_component() || refEntity.has_light_component())
				{
					AshEngine::UIVec2 vecScreenPosition{};
					bIntersectsSelection =
						ViewportPanelSupport::Detail::TryProjectEntityPointToViewport(refScene, projectionContext, refEntity, vecScreenPosition) &&
						ViewportPanelSupport::IsPointInRect(rectSelection, vecScreenPosition);
				}

				if (bIntersectsSelection)
				{
					AppendSelectionIfMissing(vecSelections, BuildEntitySelection(refEntity, refEntity.get_id()));
				}
			}
			return vecSelections;
		}
	}

	namespace ViewportPanelSupport
	{
		EditorGizmoService::InteractionResult UpdateSceneGizmoInteraction(
			const ViewportPanelDeps& refDeps,
			AshEngine::UIContext& refUi,
			const EditorViewportInputState& refInput,
			bool bViewportHovered,
			const AshEngine::UIRect& rectContent)
		{
			if (!refDeps.pGizmoService ||
				!refDeps.pSceneService ||
				!refDeps.pAssetDatabaseService ||
				!refDeps.pSelectionService ||
				!refDeps.pViewportCameraService ||
				!refDeps.pGizmoState ||
				!refDeps.pCommandExecutor)
			{
				return {};
			}

			EditorGizmoService::ViewportContext viewportContext{};
			if (!Detail::TryBuildSceneGizmoViewportContext(refDeps, rectContent, viewportContext))
			{
				return {};
			}

			return refDeps.pGizmoService->UpdateSceneViewportInteraction(
				refUi,
				refInput,
				bViewportHovered,
				*refDeps.pSceneService,
				*refDeps.pAssetDatabaseService,
				*refDeps.pSelectionService,
				*refDeps.pCommandExecutor,
				*refDeps.pGizmoState,
				viewportContext);
		}

		void ApplySceneViewportClickSelection(
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			const AshEngine::UIRect& rectContent,
			const AshEngine::UIVec2& vecMousePosition,
			AshEngine::UIModifierFlags uModifiers)
		{
			if (!refDeps.pSelectionService || !refDeps.pSceneService || !refDeps.pAssetDatabaseService)
			{
				return;
			}
			if (!IsPointInRect(rectContent, vecMousePosition))
			{
				return;
			}

			AshEngine::SceneRay ray{};
			std::vector<AshEngine::SceneRayHit> vecHits{};
			if (!TryQuerySceneInteraction(
				refDeps,
				strViewportId,
				rectContent,
				vecMousePosition,
				ray,
				&vecHits))
			{
				return;
			}

			if (!vecHits.empty())
			{
				const AshEngine::Entity entitySelected = refDeps.pSceneService->FindEntity(vecHits.front().entity_id);
				if ((uModifiers & AshEngine::UIModifierFlagBits::Ctrl) != 0)
				{
					refDeps.pSelectionService->Toggle(BuildEntitySelection(entitySelected, vecHits.front().entity_id));
				}
				else
				{
					refDeps.pSelectionService->SelectSingle(BuildEntitySelection(entitySelected, vecHits.front().entity_id));
				}
				return;
			}

			if ((uModifiers & AshEngine::UIModifierFlagBits::Ctrl) == 0)
			{
				refDeps.pSelectionService->Clear();
			}
		}

		bool RequestSceneViewportClickPick(
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			const AshEngine::UIRect& rectContent,
			const AshEngine::UIVec2& vecMousePosition,
			AshEngine::SceneViewBindingHandle& outBinding)
		{
			outBinding = {};
			if (!refDeps.pViewportService)
			{
				return false;
			}

			int32_t iPixelX = 0;
			int32_t iPixelY = 0;
			if (!TryResolveScenePickPixel(refDeps, strViewportId, rectContent, vecMousePosition, iPixelX, iPixelY))
			{
				return false;
			}

			const AshEngine::SceneViewBindingHandle binding =
				refDeps.pViewportService->GetSceneViewBindingHandle(strViewportId);
			if (!binding.is_valid())
			{
				return false;
			}

			if (!AshEngine::request_scene_entity_pick(binding, iPixelX, iPixelY))
			{
				return false;
			}

			outBinding = binding;
			return true;
		}

		bool PollSceneViewportClickPick(
			const AshEngine::SceneViewBindingHandle binding,
			AshEngine::ScenePickResult& outResult)
		{
			outResult = {};
			return binding.is_valid() && AshEngine::poll_scene_entity_pick_result(binding, outResult);
		}

		void ApplySceneViewportPickResultSelection(
			const ViewportPanelDeps& refDeps,
			const AshEngine::ScenePickResult& refPickResult,
			AshEngine::UIModifierFlags uModifiers)
		{
			if (!refDeps.pSelectionService || !refDeps.pSceneService)
			{
				return;
			}

			if (refPickResult.hit && refPickResult.entity_id != 0)
			{
				const AshEngine::Entity entitySelected = refDeps.pSceneService->FindEntity(refPickResult.entity_id);
				if ((uModifiers & AshEngine::UIModifierFlagBits::Ctrl) != 0)
				{
					refDeps.pSelectionService->Toggle(BuildEntitySelection(entitySelected, refPickResult.entity_id));
				}
				else
				{
					refDeps.pSelectionService->SelectSingle(BuildEntitySelection(entitySelected, refPickResult.entity_id));
				}
				return;
			}

			if ((uModifiers & AshEngine::UIModifierFlagBits::Ctrl) == 0)
			{
				refDeps.pSelectionService->Clear();
			}
		}

		void ApplySceneViewportBoxSelection(
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			const AshEngine::UIRect& rectContent,
			const AshEngine::UIRect& rectSelection,
			AshEngine::UIModifierFlags uModifiers)
		{
			if (!refDeps.pSelectionService)
			{
				return;
			}

			const std::vector<EditorSelection> vecBoxSelections =
				BuildSceneViewportBoxSelections(refDeps, strViewportId, rectContent, rectSelection);
			const SceneViewportBoxSelectionMode eMode = ResolveBoxSelectionMode(uModifiers);
			const std::vector<EditorSelection> vecResultSelections =
				BuildSelectionsAfterBoxSelection(*refDeps.pSelectionService, vecBoxSelections, eMode);
			refDeps.pSelectionService->SelectRange(vecResultSelections);
		}

		bool HandleAssetDropInstantiate(
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			const AshEngine::UIRect& rectContent,
			const AshEngine::UIVec2& vecMousePosition,
			const DragDropTransferData& refData)
		{
			if (!refDeps.pAssetDatabaseService || !refDeps.pCommandExecutor || !refDeps.pSceneService)
			{
				return false;
			}
			if (refData.vecEntityIds.empty())
			{
				return false;
			}

			const AshEngine::AssetInfo* pAsset = refDeps.pAssetDatabaseService->FindById(refData.vecEntityIds.front());
			if (!pAsset)
			{
				return false;
			}

			AshEngine::TransformComponent dropTransform{};
			const bool bUseWorldTransform = TryResolveSceneDropTransform(
				refDeps,
				strViewportId,
				rectContent,
				vecMousePosition,
				dropTransform);

			if (!IsSceneInstantiableAssetType(pAsset->type))
			{
				return false;
			}

			return refDeps.pCommandExecutor->ExecuteCommand(
				std::make_unique<InstantiateSceneAssetCommand>(
					refDeps.pAssetDatabaseService,
					pAsset->id,
					0,
					bUseWorldTransform,
					dropTransform,
					BuildSceneAssetEntityName(*pAsset)));
		}
	}
}
