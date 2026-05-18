#include "App/ViewportLayoutPersistence.h"

#include "Core/EditorIds.h"
#include "Services/EditorSettingsService.h"
#include "Services/EditorViewportService.h"

#include <fstream>
#include <json.hpp>

namespace AshEditor
{
	namespace
	{
		constexpr const char* kViewportLayoutStateFile = "product/config/editor/ViewportLayout.json";
		using json = nlohmann::json;

		std::string MakeViewportKindName(EditorViewportKind eKind)
		{
			switch (eKind)
			{
			case EditorViewportKind::Scene: return EditorViewportIds::Scene;
			case EditorViewportKind::Game: return EditorViewportIds::Game;
			default: return "auxiliary";
			}
		}

		EditorViewportKind ParseViewportKindName(const std::string& strKind)
		{
			if (strKind == EditorViewportIds::Scene)
			{
				return EditorViewportKind::Scene;
			}
			if (strKind == EditorViewportIds::Game)
			{
				return EditorViewportKind::Game;
			}
			return EditorViewportKind::Auxiliary;
		}
	}

	std::filesystem::path ViewportLayoutPersistence::GetStatePath(const EditorSettingsService& refSettingsService)
	{
		return refSettingsService.GetWorkspaceRoot() / kViewportLayoutStateFile;
	}

	void ViewportLayoutPersistence::Load(
		const EditorSettingsService& refSettingsService,
		EditorViewportService& refViewportService,
		INotificationSink* pNotificationSink)
	{
		const std::filesystem::path pathState = GetStatePath(refSettingsService);
		if (pathState.empty() || !std::filesystem::exists(pathState))
		{
			return;
		}

		std::ifstream input(pathState);
		if (!input.is_open())
		{
			if (pNotificationSink)
			{
				pNotificationSink->Notify("Failed to open viewport layout state file.");
			}
			return;
		}

		json root{};
		try
		{
			input >> root;
		}
		catch (const json::exception& refException)
		{
			if (pNotificationSink)
			{
				pNotificationSink->Notify(
					"Viewport layout state is invalid at '" +
					pathState.generic_string() +
					"'. Falling back to the default layout. (" +
					refException.what() +
					")");
			}
			return;
		}
		catch (const std::exception& refException)
		{
			if (pNotificationSink)
			{
				pNotificationSink->Notify(
					"Failed to read viewport layout state at '" +
					pathState.generic_string() +
					"'. Falling back to the default layout. (" +
					refException.what() +
					")");
			}
			return;
		}

		if (!root.is_object())
		{
			if (pNotificationSink)
			{
				pNotificationSink->Notify(
					"Viewport layout state is not a JSON object at '" +
					pathState.generic_string() +
					"'. Falling back to the default layout.");
			}
			return;
		}

		std::vector<EditorViewportPersistenceState> vecStates{};
		try
		{
			if (root.contains("viewports") && root["viewports"].is_array())
			{
				for (const json& entry : root["viewports"])
				{
					EditorViewportPersistenceState state{};
					state.strId = entry.value("id", std::string{});
					if (state.strId.empty())
					{
						continue;
					}

					state.bPanelOpen = entry.value("panelOpen", true);
					state.bShowToolbar = entry.value("showToolbar", true);
					state.bPreserveAspect = entry.value("preserveAspect", false);
					state.bAcceptsInput = entry.value("acceptsInput", false);
					state.bShowStats = entry.value("showStats", true);
					state.bShowOverlays = entry.value("showOverlays", false);
					state.bShowReferenceGrid = entry.value("showReferenceGrid", true);
					state.bShowReferenceOrigin = entry.value("showReferenceOrigin", true);
					state.bShowSelectionHelpers = entry.value("showSelectionHelpers", true);
					state.bShowCameraHelpers = entry.value("showCameraHelpers", true);
					state.bShowLightHelpers = entry.value("showLightHelpers", true);
					state.bShowSelectionPivot = entry.value("showSelectionPivot", true);
					vecStates.push_back(state);

					if (EditorViewportPresentation* pPresentation = refViewportService.GetPresentation(state.strId))
					{
						pPresentation->eKind = ParseViewportKindName(entry.value("kind", std::string{}));
					}
				}
			}

			refViewportService.ApplyPersistenceState(
				vecStates,
				root.value("primaryViewportId", std::string{ EditorViewportIds::Scene }));
		}
		catch (const json::exception& refException)
		{
			if (pNotificationSink)
			{
				pNotificationSink->Notify(
					"Viewport layout state contains invalid values at '" +
					pathState.generic_string() +
					"'. Falling back to the default layout. (" +
					refException.what() +
					")");
			}
		}
	}

	void ViewportLayoutPersistence::Save(
		const EditorSettingsService& refSettingsService,
		const EditorViewportService& refViewportService)
	{
		const std::filesystem::path pathState = GetStatePath(refSettingsService);
		if (pathState.empty())
		{
			return;
		}

		std::filesystem::create_directories(pathState.parent_path());

		json root{};
		if (const EditorViewportInstance* pPrimaryViewport = refViewportService.GetPrimaryViewport())
		{
			root["primaryViewportId"] = pPrimaryViewport->strId;
		}

		root["viewports"] = json::array();
		for (const EditorViewportPersistenceState& refState : refViewportService.CapturePersistenceState())
		{
			json entry{};
			entry["id"] = refState.strId;
			entry["panelOpen"] = refState.bPanelOpen;
			entry["showToolbar"] = refState.bShowToolbar;
			entry["preserveAspect"] = refState.bPreserveAspect;
			entry["acceptsInput"] = refState.bAcceptsInput;
			entry["showStats"] = refState.bShowStats;
			entry["showOverlays"] = refState.bShowOverlays;
			entry["showReferenceGrid"] = refState.bShowReferenceGrid;
			entry["showReferenceOrigin"] = refState.bShowReferenceOrigin;
			entry["showSelectionHelpers"] = refState.bShowSelectionHelpers;
			entry["showCameraHelpers"] = refState.bShowCameraHelpers;
			entry["showLightHelpers"] = refState.bShowLightHelpers;
			entry["showSelectionPivot"] = refState.bShowSelectionPivot;
			if (const EditorViewportPresentation* pPresentation = refViewportService.GetPresentation(refState.strId))
			{
				entry["kind"] = MakeViewportKindName(pPresentation->eKind);
			}
			root["viewports"].push_back(std::move(entry));
		}

		std::ofstream output(pathState, std::ios::out | std::ios::trunc);
		if (!output.is_open())
		{
			return;
		}

		output << root.dump(2);
	}
}
