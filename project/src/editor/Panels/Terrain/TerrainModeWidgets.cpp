#include "Panels/Terrain/TerrainModeWidgets.h"

#include "Function/Gui/UIContext.h"
#include "Panels/Terrain/TerrainModeState.h"
#include "Services/TerrainEditorService.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace AshEditor
{
	namespace
	{
		std::string TerrainLayerIdToString(const AshEngine::TerrainLayerId& refId)
		{
			std::ostringstream stream{};
			stream << std::hex << std::setfill('0');
			for (const uint8_t byte : refId.bytes)
			{
				stream << std::setw(2) << static_cast<uint32_t>(byte);
			}
			return stream.str();
		}

		const AshEngine::TerrainEditLayer* FindLayer(
			const TerrainModeView& refView,
			const AshEngine::TerrainLayerId& refLayerId)
		{
			if (!refView.p_working_set)
			{
				return nullptr;
			}
			const auto found = std::find_if(
				refView.p_working_set->edit_layers.begin(),
				refView.p_working_set->edit_layers.end(),
				[&refLayerId](const AshEngine::TerrainEditLayer& refLayer)
				{
					return refLayer.id == refLayerId;
				});
			return found == refView.p_working_set->edit_layers.end() ? nullptr : &*found;
		}

		void AppendLayerAction(
			TerrainModeDrawResult& refResult,
			const TerrainLayerActionKind eKind,
			const AshEngine::TerrainLayerId layerId,
			const std::string& strName = {},
			const uint32_t uDestinationIndex = 0u,
			const float fOpacity = 1.0f,
			const bool bFlagValue = false,
			const AshEngine::TerrainHeightBlendMode eBlendMode = AshEngine::TerrainHeightBlendMode::Additive)
		{
			TerrainEditorIntent intent{};
			intent.kind = TerrainEditorIntent::Kind::LayerAction;
			intent.layer_action.kind = eKind;
			intent.layer_action.layer_id = layerId;
			intent.layer_action.name = strName;
			intent.layer_action.destination_index = uDestinationIndex;
			intent.layer_action.opacity = fOpacity;
			intent.layer_action.flag_value = bFlagValue;
			intent.layer_action.blend_mode = eBlendMode;
			refResult.intents.push_back(std::move(intent));
		}

		void DrawUnavailableFileOperation(
			AshEngine::UIContext& refUi,
			const char* pLabel,
			const char* pAvailability)
		{
			refUi.begin_disabled(true);
			refUi.button(pLabel);
			refUi.end_disabled();
			refUi.same_line();
			refUi.text_unformatted(pAvailability);
		}

		bool IsFileOperationInProgress(const TerrainModeView& refView)
		{
			if (!refView.p_file_operation_state)
			{
				return false;
			}
			return refView.p_file_operation_state->status ==
					TerrainFileOperationStatus::AwaitingPublication ||
				refView.p_file_operation_state->status == TerrainFileOperationStatus::Running;
		}

		const char* QueryFileOperationKindLabel(const TerrainFileOperationKind eKind)
		{
			switch (eKind)
			{
			case TerrainFileOperationKind::Save:
				return "Save";
			case TerrainFileOperationKind::SaveAs:
				return "Save Copy As";
			case TerrainFileOperationKind::Optimize:
				return "Optimize";
			default:
				return "File operation";
			}
		}

		void DrawFileOperationStatus(AshEngine::UIContext& refUi, const TerrainModeView& refView)
		{
			if (!refView.p_file_operation_state)
			{
				return;
			}

			const TerrainFileOperationState& refState = *refView.p_file_operation_state;
			const char* pKind = QueryFileOperationKindLabel(refState.kind);
			switch (refState.status)
			{
			case TerrainFileOperationStatus::AwaitingPublication:
				refUi.text("%s: waiting for Terrain composition...", pKind);
				break;
			case TerrainFileOperationStatus::Running:
				refUi.text("%s: writing Terrain data...", pKind);
				break;
			case TerrainFileOperationStatus::Succeeded:
				refUi.text("%s completed.", pKind);
				break;
			case TerrainFileOperationStatus::Failed:
				refUi.text_wrapped(
					"%s failed: %s",
					pKind,
					refState.error.empty() ? "Unknown error." : refState.error.c_str());
				break;
			default:
				break;
			}
		}

		const char* QueryStatusLabel(const AshEngine::TerrainQueryStatus eStatus)
		{
			switch (eStatus)
			{
			case AshEngine::TerrainQueryStatus::Ready:
				return "Ready";
			case AshEngine::TerrainQueryStatus::Pending:
				return "Pending";
			case AshEngine::TerrainQueryStatus::Outside:
				return "Outside";
			case AshEngine::TerrainQueryStatus::Failed:
				return "Failed";
			default:
				return "Unknown";
			}
		}

		void DrawTerrainStatus(AshEngine::UIContext& refUi, const TerrainModeView& refView)
		{
			if (!refView.p_working_set)
			{
				refUi.text_wrapped("No Terrain asset is open for authoring. Select a Terrain asset before using this panel.");
				if (refView.blocking_operation)
				{
					refUi.text_unformatted("Loading Terrain asset...");
				}
				return;
			}

			refUi.text("Asset ID: %llu", static_cast<unsigned long long>(refView.asset_id));
			refUi.text("Source: %s", refView.p_working_set->source_path.generic_string().c_str());
			refUi.text(
				"Grid: %u x %u samples at %.3f m",
				refView.p_working_set->layout.sample_count_x,
				refView.p_working_set->layout.sample_count_z,
				refView.p_working_set->layout.sample_spacing_meters);
			refUi.text(
				"Content generation: %llu",
				static_cast<unsigned long long>(refView.p_working_set->content_generation));
			refUi.text(
				"Residency revision: %llu",
				static_cast<unsigned long long>(refView.p_working_set->residency_revision));
			refUi.text("Query status: %s", QueryStatusLabel(refView.preview.query_status));
			refUi.text("Unsaved changes: %s", refView.dirty ? "yes" : "no");
			if (refView.pending_composition)
			{
				refUi.text_unformatted("Composing edited Terrain components...");
			}
			else if (refView.blocking_operation)
			{
				refUi.text_unformatted("Terrain operation is in progress...");
			}
		}

		void DrawManageTab(
			AshEngine::UIContext& refUi,
			const TerrainModeView& refView,
			TerrainModeState& refState,
			TerrainModeDrawResult& refResult)
		{
			DrawTerrainStatus(refUi, refView);
			DrawFileOperationStatus(refUi, refView);
			refUi.separator();
			refUi.text_unformatted("File operations");

			refUi.begin_disabled(true);
			refUi.input_text("New Terrain asset", refState.create_asset_path);
			refUi.end_disabled();
			DrawUnavailableFileOperation(refUi, "Create Flat", "Available with the create/import job slice.");

			refUi.begin_disabled(true);
			refUi.input_text("Heightmap input", refState.import_heightmap_path);
			const std::vector<const char*> importFormats{ "PNG", "RAW", "EXR" };
			refUi.combo("Import format", refState.import_format_index, importFormats);
			const std::vector<const char*> rawFormats{ "R16", "R32F" };
			refUi.combo("RAW format", refState.raw_format_index, rawFormats);
			const std::vector<const char*> rawEndianness{ "Little endian", "Big endian" };
			refUi.combo("RAW endian", refState.raw_endian_index, rawEndianness);
			const std::vector<const char*> rawAxes{ "Rows are +Z", "Rows are -Z" };
			refUi.combo("RAW axis", refState.raw_axis_index, rawAxes);
			const std::vector<const char*> exrChannels{ "Y", "R", "G", "B", "A" };
			refUi.combo("EXR channel", refState.exr_channel_index, exrChannels);
			const std::vector<const char*> resizePolicies{ "Reject mismatch", "Crop", "Catmull-Rom resample" };
			refUi.combo("Size mismatch", refState.resize_policy_index, resizePolicies);
			refUi.end_disabled();
			DrawUnavailableFileOperation(refUi, "Import Heightmap", "Available with the create/import job slice.");

			refUi.begin_disabled(true);
			refUi.input_text("Heightmap output", refState.export_heightmap_path);
			const std::vector<const char*> exportSources{ "Final composed", "Base import", "Selected layer" };
			refUi.combo("Export source", refState.export_source_index, exportSources);
			refUi.end_disabled();
			DrawUnavailableFileOperation(refUi, "Export Heightmap", "Available with the export job slice.");

			const bool fileOperationInProgress = IsFileOperationInProgress(refView);
			const bool hasWorkingSet = refView.p_working_set != nullptr;
			const bool strokeActive = refView.preview.stroke_active;
			const bool canSave =
				hasWorkingSet && refView.dirty && !fileOperationInProgress && !strokeActive;
			refUi.begin_disabled(!canSave);
			if (refUi.button("Save"))
			{
				TerrainEditorIntent intent{};
				intent.kind = TerrainEditorIntent::Kind::Save;
				refResult.intents.push_back(std::move(intent));
			}
			refUi.end_disabled();

			refUi.begin_disabled(fileOperationInProgress);
			refUi.input_text("Save copy asset path", refState.save_as_asset_path);
			refUi.end_disabled();
			const bool canSaveAs =
				hasWorkingSet && !refState.save_as_asset_path.empty() &&
				!fileOperationInProgress && !strokeActive;
			refUi.begin_disabled(!canSaveAs);
			if (refUi.button("Save Copy As"))
			{
				TerrainEditorIntent intent{};
				intent.kind = TerrainEditorIntent::Kind::SaveAs;
				intent.asset_path = refState.save_as_asset_path;
				refResult.intents.push_back(std::move(intent));
			}
			refUi.end_disabled();
			refUi.text_wrapped("Save Copy As writes a new file without rebinding or clearing the current Terrain asset.");

			DrawUnavailableFileOperation(refUi, "Reload", "Available with conflict resolution.");

			const bool canOptimize =
				hasWorkingSet && !refView.dirty && !refView.pending_composition &&
				!refView.blocking_operation && !fileOperationInProgress && !strokeActive;
			refUi.begin_disabled(!canOptimize);
			if (refUi.button("Optimize"))
			{
				TerrainEditorIntent intent{};
				intent.kind = TerrainEditorIntent::Kind::Optimize;
				refResult.intents.push_back(std::move(intent));
			}
			refUi.end_disabled();
			if (refView.dirty)
			{
				refUi.text_unformatted("Save dirty Terrain changes before optimizing the container.");
			}
		}

		bool DrawCommonBrushControls(
			AshEngine::UIContext& refUi,
			TerrainAuthoringConfig& refConfig)
		{
			bool changed = false;
			changed = refUi.drag_float(
				"Radius (m)", refConfig.brush.radius_meters, 0.25f, 0.1f, 2048.0f, "%.2f") || changed;
			changed = refUi.slider_float("Strength", refConfig.brush.strength, 0.0f, 1.0f, "%.3f") || changed;
			changed = refUi.slider_float("Falloff", refConfig.brush.falloff, 0.0f, 1.0f, "%.3f") || changed;
			changed = refUi.drag_float(
				"Stroke spacing (m)", refConfig.brush.stroke_spacing_meters, 0.1f, 0.01f, 2048.0f, "%.2f") || changed;

			uint32_t seedLow = static_cast<uint32_t>(refConfig.brush.random_seed & 0xffffffffull);
			uint32_t seedHigh = static_cast<uint32_t>(refConfig.brush.random_seed >> 32u);
			const bool seedChanged =
				refUi.input_uint("Deterministic seed low", seedLow) |
				refUi.input_uint("Deterministic seed high", seedHigh);
			if (seedChanged)
			{
				refConfig.brush.random_seed =
					(static_cast<uint64_t>(seedHigh) << 32u) | static_cast<uint64_t>(seedLow);
				changed = true;
			}
			return changed;
		}

		bool DrawSculptTab(
			AshEngine::UIContext& refUi,
			const TerrainModeView& refView,
			TerrainAuthoringConfig& refConfig)
		{
			bool changed = false;
			const bool heightTool =
				refConfig.brush.tool >= AshEngine::TerrainBrushTool::Raise &&
				refConfig.brush.tool <= AshEngine::TerrainBrushTool::Noise;
			if (!heightTool)
			{
				refConfig.brush.tool = AshEngine::TerrainBrushTool::Raise;
				changed = true;
			}
			const AshEngine::TerrainEditLayer* pLayer = FindLayer(refView, refConfig.brush.layer_id);
			if (!pLayer)
			{
				refUi.text_wrapped("Add and select an edit layer before sculpting.");
				return false;
			}
			if (pLayer->locked)
			{
				refUi.text_wrapped("The selected edit layer is locked.");
			}

			int32_t toolIndex = 0;
			if (pLayer->height_blend_mode == AshEngine::TerrainHeightBlendMode::Additive)
			{
				const std::array<AshEngine::TerrainBrushTool, 3> tools{
					AshEngine::TerrainBrushTool::Raise,
					AshEngine::TerrainBrushTool::Lower,
					AshEngine::TerrainBrushTool::Noise
				};
				const std::vector<const char*> labels{ "Raise", "Lower", "Noise" };
				const auto found = std::find(tools.begin(), tools.end(), refConfig.brush.tool);
				if (found == tools.end())
				{
					refConfig.brush.tool = tools.front();
					changed = true;
				}
				else
				{
					toolIndex = static_cast<int32_t>(std::distance(tools.begin(), found));
				}
				if (refUi.combo("Sculpt tool", toolIndex, labels))
				{
					refConfig.brush.tool = tools[static_cast<size_t>(toolIndex)];
					changed = true;
				}
				refUi.text_unformatted("Additive layers support Raise, Lower, and Noise.");
			}
			else
			{
				const std::array<AshEngine::TerrainBrushTool, 2> tools{
					AshEngine::TerrainBrushTool::Smooth,
					AshEngine::TerrainBrushTool::Flatten
				};
				const std::vector<const char*> labels{ "Smooth", "Flatten" };
				const auto found = std::find(tools.begin(), tools.end(), refConfig.brush.tool);
				if (found == tools.end())
				{
					refConfig.brush.tool = tools.front();
					changed = true;
				}
				else
				{
					toolIndex = static_cast<int32_t>(std::distance(tools.begin(), found));
				}
				if (refUi.combo("Sculpt tool", toolIndex, labels))
				{
					refConfig.brush.tool = tools[static_cast<size_t>(toolIndex)];
					changed = true;
				}
				refUi.text_unformatted("Alpha layers support Smooth and Flatten.");
			}

			refUi.begin_disabled(
				pLayer->locked || refView.blocking_operation || refView.preview.stroke_active);
			changed = DrawCommonBrushControls(refUi, refConfig) || changed;
			refUi.end_disabled();
			return changed;
		}

		bool DrawPaintTab(
			AshEngine::UIContext& refUi,
			const TerrainModeView& refView,
			TerrainAuthoringConfig& refConfig)
		{
			bool changed = false;
			if (refConfig.brush.tool != AshEngine::TerrainBrushTool::Paint &&
				refConfig.brush.tool != AshEngine::TerrainBrushTool::Erase)
			{
				refConfig.brush.tool = AshEngine::TerrainBrushTool::Paint;
				changed = true;
			}
			const AshEngine::TerrainEditLayer* pLayer = FindLayer(refView, refConfig.brush.layer_id);
			if (!pLayer)
			{
				refUi.text_wrapped("Add and select an edit layer before painting.");
				return false;
			}

			const std::array<AshEngine::TerrainBrushTool, 2> tools{
				AshEngine::TerrainBrushTool::Paint,
				AshEngine::TerrainBrushTool::Erase
			};
			const std::vector<const char*> toolLabels{ "Paint", "Erase" };
			int32_t toolIndex = refConfig.brush.tool == AshEngine::TerrainBrushTool::Erase ? 1 : 0;
			if (refUi.combo("Paint tool", toolIndex, toolLabels))
			{
				refConfig.brush.tool = tools[static_cast<size_t>(toolIndex)];
				changed = true;
			}

			std::vector<std::string> materialLabels{};
			materialLabels.reserve(AshEngine::k_terrain_material_layer_count);
			for (uint32_t lane = 0u; lane < AshEngine::k_terrain_material_layer_count; ++lane)
			{
				const std::string& strName = refView.p_working_set->material_layers[lane].name;
				materialLabels.push_back(strName.empty()
					? "Material " + std::to_string(lane)
					: std::to_string(lane) + ": " + strName);
			}
			int32_t materialIndex = static_cast<int32_t>(refConfig.brush.material_layer_index);
			if (refUi.combo("Material layer", materialIndex, materialLabels))
			{
				refConfig.brush.material_layer_index = static_cast<uint32_t>(materialIndex);
				changed = true;
			}

			refUi.begin_disabled(
				pLayer->locked || refView.blocking_operation || refView.preview.stroke_active);
			changed = DrawCommonBrushControls(refUi, refConfig) || changed;
			refUi.end_disabled();
			return changed;
		}

		void DrawLayerList(
			AshEngine::UIContext& refUi,
			const TerrainModeView& refView,
			TerrainModeDrawResult& refResult)
		{
			if (!refView.p_working_set)
			{
				refUi.text_unformatted("No Terrain layer stack is open.");
				return;
			}

			for (const AshEngine::TerrainEditLayer& refLayer : refView.p_working_set->edit_layers)
			{
				const std::string strStableId = TerrainLayerIdToString(refLayer.id);
				refUi.push_id(strStableId.c_str());
				const bool selected = refLayer.id == refView.authoring_config.brush.layer_id;
				if (refUi.selectable(refLayer.name.c_str(), selected))
				{
					TerrainEditorIntent select{};
					select.kind = TerrainEditorIntent::Kind::SelectLayer;
					select.layer_id = refLayer.id;
					refResult.intents.push_back(std::move(select));
				}
				refUi.same_line();
				bool visible = refLayer.visible;
				if (refUi.checkbox("Visible", visible))
				{
					AppendLayerAction(
						refResult, TerrainLayerActionKind::SetVisible, refLayer.id, {}, 0u, 1.0f, visible);
				}
				refUi.same_line();
				bool locked = refLayer.locked;
				if (refUi.checkbox("Locked", locked))
				{
					AppendLayerAction(
						refResult, TerrainLayerActionKind::SetLocked, refLayer.id, {}, 0u, 1.0f, locked);
				}
				refUi.pop_id();
			}
		}

		void DrawSelectedLayerActions(
			AshEngine::UIContext& refUi,
			const TerrainModeView& refView,
			TerrainModeState& refState,
			TerrainModeDrawResult& refResult)
		{
			const AshEngine::TerrainLayerId selectedId = refView.authoring_config.brush.layer_id;
			const AshEngine::TerrainEditLayer* pSelected = FindLayer(refView, selectedId);
			if (!pSelected || !refView.p_working_set)
			{
				return;
			}

			refState.SyncLayerDrafts(refView.asset_id, *pSelected);

			refUi.separator();
			refUi.text_unformatted("Selected layer");
			if (refUi.input_text("Layer name", refState.rename_layer_name))
			{
				refState.rename_draft_dirty =
					refState.rename_layer_name != refState.rename_layer_source_name;
			}
			if (refUi.button("Rename") && !refState.rename_layer_name.empty())
			{
				AppendLayerAction(
					refResult, TerrainLayerActionKind::Rename, selectedId, refState.rename_layer_name);
				refState.rename_draft_dirty = false;
			}
			refUi.same_line();
			if (refUi.button("Duplicate"))
			{
				AppendLayerAction(refResult, TerrainLayerActionKind::Duplicate, selectedId);
			}
			refUi.same_line();
			if (refUi.button("Delete"))
			{
				AppendLayerAction(refResult, TerrainLayerActionKind::Delete, selectedId);
			}

			const auto found = std::find_if(
				refView.p_working_set->edit_layers.begin(),
				refView.p_working_set->edit_layers.end(),
				[&selectedId](const AshEngine::TerrainEditLayer& refLayer)
				{
					return refLayer.id == selectedId;
				});
			const uint32_t index = static_cast<uint32_t>(
				std::distance(refView.p_working_set->edit_layers.begin(), found));
			refUi.begin_disabled(index == 0u);
			if (refUi.button("Move Up"))
			{
				AppendLayerAction(refResult, TerrainLayerActionKind::Move, selectedId, {}, index - 1u);
			}
			refUi.end_disabled();
			refUi.same_line();
			refUi.begin_disabled(index + 1u >= refView.p_working_set->edit_layers.size());
			if (refUi.button("Move Down"))
			{
				AppendLayerAction(refResult, TerrainLayerActionKind::Move, selectedId, {}, index + 1u);
			}
			refUi.end_disabled();

			if (refUi.slider_float("Opacity", refState.opacity_draft, 0.0f, 1.0f, "%.3f"))
			{
				refState.opacity_draft_dirty = refState.opacity_draft != refState.opacity_source;
			}
			if (refUi.is_item_deactivated_after_edit())
			{
				AppendLayerAction(
					refResult, TerrainLayerActionKind::SetOpacity, selectedId, {}, 0u, refState.opacity_draft);
				refState.opacity_draft_dirty = false;
			}
		}

		void DrawLayersTab(
			AshEngine::UIContext& refUi,
			const TerrainModeView& refView,
			TerrainModeState& refState,
			TerrainModeDrawResult& refResult)
		{
			const bool canEditLayers =
				refView.p_working_set &&
				refView.preview.query_status == AshEngine::TerrainQueryStatus::Ready &&
				!refView.preview.stroke_active &&
				!refView.blocking_operation;
			refUi.begin_disabled(!canEditLayers);
			refUi.input_text("New layer name", refState.new_layer_name);
			const std::vector<const char*> blendModes{ "Additive", "Alpha" };
			refUi.combo("Height blend", refState.new_layer_blend_mode_index, blendModes);
			if (refUi.button("Add Layer") && !refState.new_layer_name.empty())
			{
				AppendLayerAction(
					refResult,
					TerrainLayerActionKind::Add,
					{},
					refState.new_layer_name,
					0u,
					1.0f,
					false,
					refState.new_layer_blend_mode_index == 0
						? AshEngine::TerrainHeightBlendMode::Additive
						: AshEngine::TerrainHeightBlendMode::Alpha);
			}
			refUi.end_disabled();

			refUi.separator();
			refUi.begin_disabled(!canEditLayers);
			DrawLayerList(refUi, refView, refResult);
			DrawSelectedLayerActions(refUi, refView, refState, refResult);
			refUi.end_disabled();
		}
	}

	TerrainModeDrawResult DrawTerrainModeTabs(
		AshEngine::UIContext& refUi,
		const TerrainModeView& refView,
		TerrainModeState& refState)
	{
		TerrainModeDrawResult result{};
		TerrainAuthoringConfig config = refView.authoring_config;
		bool configChanged = false;
		const bool lockTabs = refView.preview.stroke_active;
		if (!refView.last_error.empty())
		{
			refUi.text_wrapped("Last error: %s", refView.last_error.c_str());
			refUi.separator();
		}
		refUi.begin_disabled(lockTabs);
		if (!refUi.begin_tab_bar("TerrainModeTabs"))
		{
			refUi.end_disabled();
			return result;
		}

		if (refUi.begin_tab_item("Manage"))
		{
			if (config.mode != TerrainEditorMode::Manage)
			{
				config.mode = TerrainEditorMode::Manage;
				configChanged = true;
			}
			DrawManageTab(refUi, refView, refState, result);
			refUi.end_tab_item();
		}
		if (refUi.begin_tab_item("Sculpt"))
		{
			if (config.mode != TerrainEditorMode::Sculpt)
			{
				config.mode = TerrainEditorMode::Sculpt;
				configChanged = true;
			}
			refUi.begin_disabled(refView.preview.stroke_active);
			configChanged = DrawSculptTab(refUi, refView, config) || configChanged;
			refUi.end_disabled();
			refUi.end_tab_item();
		}
		if (refUi.begin_tab_item("Paint"))
		{
			if (config.mode != TerrainEditorMode::Paint)
			{
				config.mode = TerrainEditorMode::Paint;
				configChanged = true;
			}
			refUi.begin_disabled(refView.preview.stroke_active);
			configChanged = DrawPaintTab(refUi, refView, config) || configChanged;
			refUi.end_disabled();
			refUi.end_tab_item();
		}
		if (refUi.begin_tab_item("Layers"))
		{
			if (config.mode != TerrainEditorMode::Layers)
			{
				config.mode = TerrainEditorMode::Layers;
				configChanged = true;
			}
			DrawLayersTab(refUi, refView, refState, result);
			refUi.end_tab_item();
		}
		refUi.end_tab_bar();
		refUi.end_disabled();

		if (configChanged && !lockTabs)
		{
			TerrainEditorIntent configure{};
			configure.kind = TerrainEditorIntent::Kind::ConfigureAuthoring;
			configure.mode = config.mode;
			configure.brush = config.brush;
			result.intents.push_back(std::move(configure));
		}
		return result;
	}
}
