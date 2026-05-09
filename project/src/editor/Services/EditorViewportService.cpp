#include "Services/EditorViewportService.h"

#include "Base/hlog.h"
#include "Core/EditorEventBus.h"
#include "Core/EditorEvents.h"
#include "Core/EditorIds.h"
#include "Function/Render/ScenePresentationSubsystem.h"

#include <algorithm>
#include <limits>

namespace AshEditor
{
	namespace
	{
		uint32_t ClampViewportExtent(uint32_t uValue)
		{
			return std::max<uint32_t>(1u, std::min<uint32_t>(uValue, std::numeric_limits<uint16_t>::max()));
		}

		int GetViewportSortPriority(const AshEditor::EditorViewportInstance& refViewport)
		{
			if (refViewport.strId == EditorViewportIds::Scene)
			{
				return 0;
			}
			if (refViewport.strId == EditorViewportIds::Game)
			{
				return 1;
			}
			return 2;
		}

		template<typename ViewportPtr>
		void SortViewportsStably(std::vector<ViewportPtr>& refViewports)
		{
			std::sort(refViewports.begin(), refViewports.end(), [](const ViewportPtr pLhs, const ViewportPtr pRhs) {
				if (pLhs == pRhs)
				{
					return false;
				}
				if (!pLhs)
				{
					return false;
				}
				if (!pRhs)
				{
					return true;
				}

				const int iLhsPriority = GetViewportSortPriority(*pLhs);
				const int iRhsPriority = GetViewportSortPriority(*pRhs);
				if (iLhsPriority != iRhsPriority)
				{
					return iLhsPriority < iRhsPriority;
				}

				if (pLhs->strId != pRhs->strId)
				{
					return pLhs->strId < pRhs->strId;
				}

				return pLhs->strDisplayName < pRhs->strDisplayName;
			});
		}

		uint32_t GetSyncedOutputExtent(uint32_t uRequestedExtent, uint32_t uCurrentExtent)
		{
			if (uRequestedExtent > 0u)
			{
				return ClampViewportExtent(uRequestedExtent);
			}

			return uCurrentExtent > 0u ? ClampViewportExtent(uCurrentExtent) : 1u;
		}

		std::string MakeOutputDebugName(const EditorViewportInstance& refViewport)
		{
			return refViewport.strDisplayName.empty()
				? "EditorViewportOutput"
				: refViewport.strDisplayName + " Output";
		}

		std::string MakeBindingDebugName(const EditorViewportInstance& refViewport)
		{
			return refViewport.strDisplayName.empty()
				? "EditorViewportBinding"
				: refViewport.strDisplayName + " Binding";
		}
	}

	void EditorViewportService::SetEventBus(EditorEventBus* pEventBus)
	{
		_pEventBus = pEventBus;
	}

	EditorViewportPresentation EditorViewportService::BuildDefaultPresentation(const std::string& strViewportId)
	{
		EditorViewportPresentation presentation{};
		if (strViewportId == EditorViewportIds::Scene)
		{
			presentation.eKind = EditorViewportKind::Scene;
			presentation.bPreserveAspect = false;
			presentation.bAcceptsInput = true;
			presentation.bShowStats = true;
			presentation.bShowOverlays = true;
			return presentation;
		}

		if (strViewportId == EditorViewportIds::Game)
		{
			presentation.eKind = EditorViewportKind::Game;
			presentation.bPreserveAspect = true;
			presentation.bAcceptsInput = false;
			presentation.bShowStats = true;
			presentation.bShowOverlays = false;
			return presentation;
		}

		presentation.eKind = EditorViewportKind::Auxiliary;
		presentation.bPreserveAspect = false;
		presentation.bAcceptsInput = false;
		presentation.bShowStats = true;
		presentation.bShowOverlays = false;
		return presentation;
	}

	EditorViewportService::ViewportRecord* EditorViewportService::FindRecord(const std::string& strViewportId)
	{
		const std::unordered_map<std::string, std::unique_ptr<ViewportRecord>>::iterator itRecord = _mapViewports.find(strViewportId);
		return itRecord != _mapViewports.end() ? itRecord->second.get() : nullptr;
	}

	const EditorViewportService::ViewportRecord* EditorViewportService::FindRecord(const std::string& strViewportId) const
	{
		const std::unordered_map<std::string, std::unique_ptr<ViewportRecord>>::const_iterator itRecord = _mapViewports.find(strViewportId);
		return itRecord != _mapViewports.end() ? itRecord->second.get() : nullptr;
	}

	EditorViewportInstance& EditorViewportService::EnsureViewport(std::string strViewportId, std::string strDisplayName)
	{
		if (strViewportId.empty())
		{
			strViewportId = "viewport";
		}

		std::unordered_map<std::string, std::unique_ptr<ViewportRecord>>::iterator itRecord = _mapViewports.find(strViewportId);
		if (itRecord == _mapViewports.end())
		{
			std::unique_ptr<ViewportRecord> upRecord = std::make_unique<ViewportRecord>();
			upRecord->viewportInstance.strId = strViewportId;
			upRecord->viewportInstance.strDisplayName = strDisplayName.empty() ? strViewportId : std::move(strDisplayName);
			upRecord->viewportPresentation = BuildDefaultPresentation(strViewportId);
			itRecord = _mapViewports.emplace(strViewportId, std::move(upRecord)).first;
		}
		else if (!strDisplayName.empty())
		{
			itRecord->second->viewportInstance.strDisplayName = std::move(strDisplayName);
		}

		if (_strPrimaryViewportId.empty())
		{
			_strPrimaryViewportId = strViewportId;
		}

		return itRecord->second->viewportInstance;
	}

	EditorViewportInstance* EditorViewportService::FindViewport(const std::string& strViewportId)
	{
		ViewportRecord* pRecord = FindRecord(strViewportId);
		return pRecord ? &pRecord->viewportInstance : nullptr;
	}

	const EditorViewportInstance* EditorViewportService::FindViewport(const std::string& strViewportId) const
	{
		const ViewportRecord* pRecord = FindRecord(strViewportId);
		return pRecord ? &pRecord->viewportInstance : nullptr;
	}

	std::vector<EditorViewportInstance*> EditorViewportService::GetViewports()
	{
		std::vector<EditorViewportInstance*> vecViewports{};
		vecViewports.reserve(_mapViewports.size());
		for (std::pair<const std::string, std::unique_ptr<ViewportRecord>>& entry : _mapViewports)
		{
			if (entry.second)
			{
				vecViewports.push_back(&entry.second->viewportInstance);
			}
		}
		SortViewportsStably(vecViewports);
		return vecViewports;
	}

	std::vector<const EditorViewportInstance*> EditorViewportService::GetViewports() const
	{
		std::vector<const EditorViewportInstance*> vecViewports{};
		vecViewports.reserve(_mapViewports.size());
		for (const std::pair<const std::string, std::unique_ptr<ViewportRecord>>& entry : _mapViewports)
		{
			if (entry.second)
			{
				vecViewports.push_back(&entry.second->viewportInstance);
			}
		}
		SortViewportsStably(vecViewports);
		return vecViewports;
	}

	EditorViewportInstance* EditorViewportService::GetPrimaryViewport()
	{
		return FindViewport(_strPrimaryViewportId);
	}

	const EditorViewportInstance* EditorViewportService::GetPrimaryViewport() const
	{
		return FindViewport(_strPrimaryViewportId);
	}

	const std::string& EditorViewportService::GetPrimaryViewportId() const
	{
		return _strPrimaryViewportId;
	}

	bool EditorViewportService::IsPrimaryViewport(const std::string& strViewportId) const
	{
		return !strViewportId.empty() && strViewportId == _strPrimaryViewportId;
	}

	void EditorViewportService::SetPrimaryViewport(const std::string& strViewportId)
	{
		if (!strViewportId.empty() && FindViewport(strViewportId))
		{
			if (_strPrimaryViewportId == strViewportId)
			{
				return;
			}

			const std::string strPreviousViewportId = _strPrimaryViewportId;
			_strPrimaryViewportId = strViewportId;
			NotifyPrimaryViewportChanged(strPreviousViewportId, _strPrimaryViewportId);
		}
	}

	EditorViewportPresentation* EditorViewportService::GetPresentation(const std::string& strViewportId)
	{
		ViewportRecord* pRecord = FindRecord(strViewportId);
		return pRecord ? &pRecord->viewportPresentation : nullptr;
	}

	const EditorViewportPresentation* EditorViewportService::GetPresentation(const std::string& strViewportId) const
	{
		const ViewportRecord* pRecord = FindRecord(strViewportId);
		return pRecord ? &pRecord->viewportPresentation : nullptr;
	}

	EditorViewportRenderState* EditorViewportService::GetRenderState(const std::string& strViewportId)
	{
		ViewportRecord* pRecord = FindRecord(strViewportId);
		return pRecord ? &pRecord->viewportRenderState : nullptr;
	}

	const EditorViewportRenderState* EditorViewportService::GetRenderState(const std::string& strViewportId) const
	{
		const ViewportRecord* pRecord = FindRecord(strViewportId);
		return pRecord ? &pRecord->viewportRenderState : nullptr;
	}

	bool EditorViewportService::UpdateRequestedSize(const std::string& strViewportId, uint32_t uWidth, uint32_t uHeight)
	{
		ViewportRecord* pRecord = FindRecord(strViewportId);
		if (!pRecord)
		{
			return false;
		}

		// Clamp sizes to keep presentation outputs within a safe range. A 0 requested extent means "no valid size this frame".
		const uint32_t uClampedWidth = uWidth > 0u ? ClampViewportExtent(uWidth) : 0u;
		const uint32_t uClampedHeight = uHeight > 0u ? ClampViewportExtent(uHeight) : 0u;
		if (pRecord->viewportInstance.state.uRequestedWidth == uClampedWidth &&
			pRecord->viewportInstance.state.uRequestedHeight == uClampedHeight)
		{
			return false;
		}

		pRecord->viewportInstance.state.uRequestedWidth = uClampedWidth;
		pRecord->viewportInstance.state.uRequestedHeight = uClampedHeight;
		pRecord->viewportRenderState.bPendingSync = true;
		return true;
	}

	void EditorViewportService::SetPanelOpen(const std::string& strId, bool bOpen)
	{
		ViewportRecord* pRecord = FindRecord(strId);
		if (pRecord)
		{
			if (pRecord->viewportPresentation.bPanelOpen != bOpen)
			{
				pRecord->viewportRenderState.bPendingSync = true;
				pRecord->viewportPresentation.bPanelOpen = bOpen;
				NotifyPresentationChanged(strId, pRecord->viewportPresentation);
				return;
			}
		}
	}

	bool EditorViewportService::SyncScenePresentations(
		AshEngine::ScenePresentationSubsystem& refScenePresentation,
		AshEngine::Scene& refScene)
	{
		// This call owns the editor-side lifecycle for per-viewport outputs + view bindings:
		// - Create/update offscreen outputs sized from the current requested extents.
		// - Create/update view bindings when presentation state requires a resync.
		// - Expose UI surfaces back to panels via EditorViewportInstance::surface.
		bool bAllSynced = true;
		for (EditorViewportInstance* pViewport : GetViewports())
		{
			if (!pViewport)
			{
				continue;
			}

			ViewportRecord* pRecord = FindRecord(pViewport->strId);
			if (!pRecord)
			{
				continue;
			}

			const bool bHasRequestedSize =
				pRecord->viewportInstance.state.uRequestedWidth > 0u &&
				pRecord->viewportInstance.state.uRequestedHeight > 0u;
			const uint32_t uOutputWidth = GetSyncedOutputExtent(
				pRecord->viewportInstance.state.uRequestedWidth,
				pRecord->viewportRenderState.uOutputWidth);
			const uint32_t uOutputHeight = GetSyncedOutputExtent(
				pRecord->viewportInstance.state.uRequestedHeight,
				pRecord->viewportRenderState.uOutputHeight);

			const std::string strOutputDebugName = MakeOutputDebugName(pRecord->viewportInstance);
			bool bOutputSynced = true;
			if (!pRecord->sceneOutput.is_valid())
			{
				AshEngine::SceneOutputDesc outputDesc{};
				outputDesc.debug_name = strOutputDebugName.c_str();
				outputDesc.kind = AshEngine::SceneOutputKind::Offscreen;
				outputDesc.width = uOutputWidth;
				outputDesc.height = uOutputHeight;
				pRecord->sceneOutput = refScenePresentation.create_output(outputDesc);
				pRecord->viewportRenderState.bPendingSync = true;
				bOutputSynced = pRecord->sceneOutput.is_valid();
			}
			else if (
				pRecord->viewportRenderState.uOutputWidth != uOutputWidth ||
				pRecord->viewportRenderState.uOutputHeight != uOutputHeight)
			{
				AshEngine::SceneOutputDesc outputDesc{};
				outputDesc.debug_name = strOutputDebugName.c_str();
				outputDesc.kind = AshEngine::SceneOutputKind::Offscreen;
				outputDesc.width = uOutputWidth;
				outputDesc.height = uOutputHeight;
				bOutputSynced = refScenePresentation.update_output(pRecord->sceneOutput, outputDesc);
			}

			if (!bOutputSynced)
			{
				HLogError("Editor viewport '{}' failed to sync output presentation state.", pRecord->viewportInstance.strId);
				pRecord->viewportRenderState.bPendingSync = true;
				pRecord->viewportInstance.surface = {};
				pRecord->viewportInstance.state.uWidth = 0u;
				pRecord->viewportInstance.state.uHeight = 0u;
				bAllSynced = false;
				continue;
			}

			pRecord->viewportInstance.surface = refScenePresentation.get_ui_surface(pRecord->sceneOutput);

			const std::string strBindingDebugName = MakeBindingDebugName(pRecord->viewportInstance);
			AshEngine::SceneViewBindingDesc bindingDesc{};
			bindingDesc.debug_name = strBindingDebugName.c_str();
			bindingDesc.scene = &refScene;
			bindingDesc.camera.source = AshEngine::SceneCameraSource::PrimaryCamera;
			bindingDesc.output = pRecord->sceneOutput;
			bindingDesc.enabled = pRecord->viewportPresentation.bPanelOpen && bHasRequestedSize;
			bindingDesc.sort_order = GetViewportSortPriority(pRecord->viewportInstance);

			bool bBindingSynced = true;
			if (!pRecord->sceneViewBinding.is_valid())
			{
				pRecord->sceneViewBinding = refScenePresentation.create_view_binding(bindingDesc);
				bBindingSynced = pRecord->sceneViewBinding.is_valid();
			}
			else if (pRecord->viewportRenderState.bPendingSync)
			{
				bBindingSynced = refScenePresentation.update_view_binding(pRecord->sceneViewBinding, bindingDesc);
			}

			if (!bBindingSynced)
			{
				HLogError("Editor viewport '{}' failed to sync scene binding state.", pRecord->viewportInstance.strId);
				pRecord->viewportRenderState.bPendingSync = true;
				pRecord->viewportInstance.state.uWidth = 0u;
				pRecord->viewportInstance.state.uHeight = 0u;
				bAllSynced = false;
				continue;
			}

			pRecord->viewportRenderState.uOutputWidth = uOutputWidth;
			pRecord->viewportRenderState.uOutputHeight = uOutputHeight;
			pRecord->viewportRenderState.bPendingSync = false;
			pRecord->viewportInstance.state.uWidth = bHasRequestedSize ? uOutputWidth : 0u;
			pRecord->viewportInstance.state.uHeight = bHasRequestedSize ? uOutputHeight : 0u;
		}

		return bAllSynced;
	}

	void EditorViewportService::DestroyScenePresentations(AshEngine::ScenePresentationSubsystem* pScenePresentation)
	{
		for (std::pair<const std::string, std::unique_ptr<ViewportRecord>>& entry : _mapViewports)
		{
			if (!entry.second)
			{
				continue;
			}

			if (pScenePresentation)
			{
				if (entry.second->sceneViewBinding.is_valid())
				{
					pScenePresentation->destroy_view_binding(entry.second->sceneViewBinding);
				}
				if (entry.second->sceneOutput.is_valid())
				{
					pScenePresentation->destroy_output(entry.second->sceneOutput);
				}
			}

			entry.second->sceneViewBinding = {};
			entry.second->sceneOutput = {};
			entry.second->viewportInstance.surface = {};
			entry.second->viewportInstance.state.uWidth = 0u;
			entry.second->viewportInstance.state.uHeight = 0u;
			entry.second->viewportRenderState.uOutputWidth = 0u;
			entry.second->viewportRenderState.uOutputHeight = 0u;
			entry.second->viewportRenderState.bPendingSync = true;
		}
	}

	void EditorViewportService::ResetPresentations()
	{
		bool bPrimaryChanged = false;
		const std::string strPreviousPrimaryViewportId = _strPrimaryViewportId;
		for (std::pair<const std::string, std::unique_ptr<ViewportRecord>>& entry : _mapViewports)
		{
			if (!entry.second)
			{
				continue;
			}

			EditorViewportPresentation defaultPresentation = BuildDefaultPresentation(entry.second->viewportInstance.strId);
			if (entry.second->viewportPresentation.bShowToolbar != defaultPresentation.bShowToolbar ||
				entry.second->viewportPresentation.bPreserveAspect != defaultPresentation.bPreserveAspect ||
				entry.second->viewportPresentation.bAcceptsInput != defaultPresentation.bAcceptsInput ||
				entry.second->viewportPresentation.bShowStats != defaultPresentation.bShowStats ||
				entry.second->viewportPresentation.bShowOverlays != defaultPresentation.bShowOverlays ||
				entry.second->viewportPresentation.bPanelOpen != defaultPresentation.bPanelOpen)
			{
				entry.second->viewportPresentation = defaultPresentation;
				NotifyPresentationChanged(entry.second->viewportInstance.strId, entry.second->viewportPresentation);
			}
			entry.second->viewportRenderState.bPendingSync = true;
		}

		if (FindViewport(EditorViewportIds::Scene))
		{
			_strPrimaryViewportId = EditorViewportIds::Scene;
			bPrimaryChanged = strPreviousPrimaryViewportId != _strPrimaryViewportId;
		}

		if (bPrimaryChanged)
		{
			NotifyPrimaryViewportChanged(strPreviousPrimaryViewportId, _strPrimaryViewportId);
		}
	}

	std::vector<EditorViewportPersistenceState> EditorViewportService::CapturePersistenceState() const
	{
		std::vector<EditorViewportPersistenceState> vecStates{};
		const std::vector<const EditorViewportInstance*> vecViewports = GetViewports();
		vecStates.reserve(vecViewports.size());
		for (const EditorViewportInstance* pViewport : vecViewports)
		{
			if (!pViewport)
			{
				continue;
			}

			const ViewportRecord* pRecord = FindRecord(pViewport->strId);
			if (!pRecord)
			{
				continue;
			}
			vecStates.push_back({
				pRecord->viewportInstance.strId,
				pRecord->viewportPresentation.bPanelOpen,
				pRecord->viewportPresentation.bShowToolbar,
				pRecord->viewportPresentation.bPreserveAspect,
				pRecord->viewportPresentation.bAcceptsInput,
				pRecord->viewportPresentation.bShowStats,
				pRecord->viewportPresentation.bShowOverlays
			});
		}
		return vecStates;
	}

	bool EditorViewportService::ApplyPresentationState(ViewportRecord& refRecord, const EditorViewportPersistenceState& refState)
	{
		const bool bChanged =
			refRecord.viewportPresentation.bPanelOpen != refState.bPanelOpen ||
			refRecord.viewportPresentation.bShowToolbar != refState.bShowToolbar ||
			refRecord.viewportPresentation.bPreserveAspect != refState.bPreserveAspect ||
			refRecord.viewportPresentation.bAcceptsInput != refState.bAcceptsInput ||
			refRecord.viewportPresentation.bShowStats != refState.bShowStats ||
			refRecord.viewportPresentation.bShowOverlays != refState.bShowOverlays;

		refRecord.viewportPresentation.bPanelOpen = refState.bPanelOpen;
		refRecord.viewportPresentation.bShowToolbar = refState.bShowToolbar;
		refRecord.viewportPresentation.bPreserveAspect = refState.bPreserveAspect;
		refRecord.viewportPresentation.bAcceptsInput = refState.bAcceptsInput;
		refRecord.viewportPresentation.bShowStats = refState.bShowStats;
		refRecord.viewportPresentation.bShowOverlays = refState.bShowOverlays;
		refRecord.viewportRenderState.bPendingSync = true;
		return bChanged;
	}

	void EditorViewportService::ApplyPersistenceState(
		const std::vector<EditorViewportPersistenceState>& vecStates,
		const std::string& strPrimaryViewportId)
	{
		for (const EditorViewportPersistenceState& state : vecStates)
		{
			ViewportRecord* pRecord = FindRecord(state.strId);
			if (!pRecord)
			{
				continue;
			}

			if (ApplyPresentationState(*pRecord, state))
			{
				NotifyPresentationChanged(state.strId, pRecord->viewportPresentation);
			}
		}

		SetPrimaryViewport(strPrimaryViewportId);
	}

	void EditorViewportService::Clear()
	{
		_mapViewports.clear();
		_strPrimaryViewportId.clear();
	}

	void EditorViewportService::NotifyPresentationChanged(
		const std::string& strViewportId,
		const EditorViewportPresentation& presentation) const
	{
		if (!_pEventBus)
		{
			return;
		}

		EditorViewportPresentationChangedEvent event{};
		event.strViewportId = strViewportId;
		event.bShowToolbar = presentation.bShowToolbar;
		event.bPreserveAspect = presentation.bPreserveAspect;
		event.bAcceptsInput = presentation.bAcceptsInput;
		event.bShowStats = presentation.bShowStats;
		event.bShowOverlays = presentation.bShowOverlays;
		event.bPanelOpen = presentation.bPanelOpen;
		_pEventBus->Publish(event);
	}

	void EditorViewportService::NotifyPrimaryViewportChanged(
		const std::string& strPreviousViewportId,
		const std::string& strCurrentViewportId) const
	{
		if (!_pEventBus || strPreviousViewportId == strCurrentViewportId)
		{
			return;
		}

		EditorPrimaryViewportChangedEvent event{};
		event.strPreviousViewportId = strPreviousViewportId;
		event.strCurrentViewportId = strCurrentViewportId;
		_pEventBus->Publish(event);
	}
}
