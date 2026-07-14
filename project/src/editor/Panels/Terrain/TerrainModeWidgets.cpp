#include "Panels/Terrain/TerrainModeWidgets.h"

#include "Function/Gui/UIContext.h"
#include "Panels/Terrain/TerrainModeState.h"
#include "Services/TerrainEditorService.h"

#include <algorithm>
#include <array>
#include <cmath>
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

		bool IsFileOperationInProgress(const TerrainModeView& refView)
		{
			if (!refView.p_file_operation_state)
			{
				return false;
			}
			return refView.p_file_operation_state->status ==
					TerrainFileOperationStatus::AwaitingPublication ||
				refView.p_file_operation_state->status == TerrainFileOperationStatus::Running ||
				refView.p_file_operation_state->status ==
					TerrainFileOperationStatus::PublishedAwaitingCatalog;
		}

		bool IsTerrainReadOnly(const TerrainModeView& refView)
		{
			return refView.p_external_change_state &&
				refView.p_external_change_state->read_only;
		}

		bool IsTerrainRecoveryState(const TerrainModeView& refView)
		{
			if (!refView.p_external_change_state)
			{
				return false;
			}
			return refView.p_external_change_state->status ==
					TerrainExternalChangeStatus::RecoveredReadOnly ||
				refView.p_external_change_state->status == TerrainExternalChangeStatus::Failed;
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
			case TerrainFileOperationKind::Create:
				return "Create Flat";
			case TerrainFileOperationKind::Import:
				return "Import Heightmap";
			case TerrainFileOperationKind::Export:
				return "Export Heightmap";
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
				refUi.text("%s: processing Terrain data...", pKind);
				break;
			case TerrainFileOperationStatus::PublishedAwaitingCatalog:
			{
				const std::string message =
					TerrainModeState::BuildPublishedAwaitingCatalogMessage(refState);
				refUi.text_wrapped("%s: %s", pKind, message.c_str());
				break;
			}
			case TerrainFileOperationStatus::Cancelled:
				refUi.text("%s cancelled.", pKind);
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
			for (const std::string& warning : refState.warnings)
			{
				refUi.text_wrapped("%s warning: %s", pKind, warning.c_str());
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
			const TerrainExternalChangeState* pExternal = refView.p_external_change_state;
			if (!refView.p_working_set)
			{
				refUi.text_wrapped("No Terrain asset is open for authoring. Select a Terrain asset before using this panel.");
				if (refView.blocking_operation)
				{
					refUi.text_unformatted("Loading Terrain asset...");
				}
				if (pExternal && pExternal->status == TerrainExternalChangeStatus::Failed)
				{
					refUi.text_wrapped(
						"Failed Terrain source is read only: %s",
						pExternal->diagnostic.empty() ? "Unknown load error." : pExternal->diagnostic.c_str());
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
			if (pExternal && pExternal->status == TerrainExternalChangeStatus::RecoveredReadOnly)
			{
				refUi.text_wrapped(
					"Recovered Terrain source is read only: %s",
					pExternal->diagnostic.empty() ? "The previous valid generation was recovered." :
						pExternal->diagnostic.c_str());
			}
			else if (pExternal && pExternal->status == TerrainExternalChangeStatus::Failed)
			{
				refUi.text_wrapped(
					"Failed Terrain source is read only: %s",
					pExternal->diagnostic.empty() ? "Unknown reload error." : pExternal->diagnostic.c_str());
			}
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

			const bool fileOperationInProgress = IsFileOperationInProgress(refView);
			const bool hasWorkingSet = refView.p_working_set != nullptr;
			const bool strokeActive = refView.preview.stroke_active;
			const bool readOnly = IsTerrainReadOnly(refView);
			const bool recoveryState = IsTerrainRecoveryState(refView);
			const bool canCancelFileOperation = refView.p_file_operation_state &&
				TerrainModeState::ShouldShowCancelFileOperation(
					refView.p_file_operation_state->status,
					refView.p_file_operation_state->kind);
			if (canCancelFileOperation)
			{
				if (refUi.button("Cancel File Operation"))
				{
					TerrainEditorIntent intent{};
					intent.kind = TerrainEditorIntent::Kind::CancelFileOperation;
					refResult.intents.push_back(std::move(intent));
				}
			}
			const bool canReplaceSession =
				!fileOperationInProgress && !strokeActive && !refView.blocking_operation &&
				!refView.dirty && !readOnly && !recoveryState;

			refUi.begin_disabled(fileOperationInProgress);
			refUi.input_text("Create asset path", refState.create_asset_path);
			refUi.input_float("Minimum height (m)", refState.create_height_min, 1.0f, 10.0f, "%.3f");
			refUi.input_float("Maximum height (m)", refState.create_height_max, 1.0f, 10.0f, "%.3f");
			refUi.input_float("Flat height (m)", refState.flat_height, 1.0f, 10.0f, "%.3f");
			refUi.end_disabled();
			const bool canCreate = canReplaceSession && !refState.create_asset_path.empty() &&
				refState.HasValidCreateParameters();
			refUi.begin_disabled(!canCreate);
			if (refUi.button("Create Flat"))
			{
				TerrainEditorIntent intent{};
				intent.kind = TerrainEditorIntent::Kind::Create;
				intent.asset_path = refState.create_asset_path;
				intent.create_desc = refState.BuildCreateDesc();
				refResult.intents.push_back(std::move(intent));
			}
			refUi.end_disabled();
			refUi.text_unformatted("Create uses the production 8193 x 8193, 1 m Terrain layout.");

			refUi.begin_disabled(fileOperationInProgress);
			refUi.input_text("Import asset path", refState.import_asset_path);
			refUi.input_text("Heightmap input", refState.import_heightmap_path);
			refUi.input_uint("Source width", refState.import_source_width);
			refUi.input_uint("Source height", refState.import_source_height);
			refUi.input_float("Height offset (m)", refState.import_height_offset, 1.0f, 10.0f, "%.3f");
			refUi.input_float("Height range (m)", refState.import_height_range, 1.0f, 10.0f, "%.3f");
			const std::vector<const char*> importFormats{ "PNG", "RAW", "EXR" };
			refUi.combo("Import format", refState.import_format_index, importFormats);
			const std::vector<const char*> rawFormats{ "R16", "R32F" };
			refUi.combo("Import RAW format", refState.import_raw_format_index, rawFormats);
			const std::vector<const char*> rawEndianness{ "Little endian", "Big endian" };
			refUi.combo("Import RAW endian", refState.import_raw_endian_index, rawEndianness);
			refUi.checkbox("Flip X", refState.import_flip_x);
			const std::vector<const char*> rawAxes{ "Rows are +Z", "Flip Z" };
			refUi.combo("Heightmap Z axis", refState.import_raw_axis_index, rawAxes);
			refUi.input_text("Import EXR channel", refState.import_exr_channel);
			const std::vector<const char*> resizePolicies{ "Reject mismatch", "Crop", "Catmull-Rom resample" };
			refUi.combo("Size mismatch", refState.import_resize_policy_index, resizePolicies);
			refUi.end_disabled();
			const AshEngine::TerrainHeightFileFormat importFormat =
				TerrainModeState::ResolveHeightFileFormat(
					refState.import_format_index, refState.import_raw_format_index);
			const bool validImportChannel =
				importFormat != AshEngine::TerrainHeightFileFormat::Exr ||
				!refState.import_exr_channel.empty();
			const bool canImport =
				canReplaceSession && !refState.import_asset_path.empty() &&
				!refState.import_heightmap_path.empty() &&
				refState.import_source_width > 0u && refState.import_source_height > 0u &&
				std::isfinite(refState.import_height_offset) &&
				std::isfinite(refState.import_height_range) &&
				refState.import_height_range > 0.0f && validImportChannel;
			refUi.begin_disabled(!canImport);
			if (refUi.button("Import Heightmap"))
			{
				TerrainEditorIntent intent{};
				intent.kind = TerrainEditorIntent::Kind::Import;
				intent.asset_path = refState.import_asset_path;
				intent.import_desc = refState.BuildImportDesc();
				refResult.intents.push_back(std::move(intent));
			}
			refUi.end_disabled();

			refUi.separator();
			refUi.begin_disabled(fileOperationInProgress);
			refUi.input_text("Heightmap output", refState.export_heightmap_path);
			const std::vector<const char*> exportFormats{ "PNG", "RAW", "EXR" };
			refUi.combo("Export format", refState.export_format_index, exportFormats);
			refUi.combo("Export RAW format", refState.export_raw_format_index, rawFormats);
			refUi.combo("Export RAW endian", refState.export_raw_endian_index, rawEndianness);
			const std::vector<const char*> exportSources{
				"Final composed", "Base import", "Selected height layer", "Material weight layer" };
			refUi.combo("Export source", refState.export_source_index, exportSources);
			refUi.input_uint("Export material layer", refState.export_material_layer_index);
			refUi.input_text("Export EXR channel", refState.export_exr_channel);
			const std::vector<const char*> exrPixelTypes{ "Half", "Float" };
			refUi.combo("EXR pixel type", refState.export_exr_pixel_type_index, exrPixelTypes);
			refUi.end_disabled();
			refUi.text_wrapped(
				"Export creates a new file and never overwrites an existing destination.");
			const AshEngine::TerrainExportSource exportSource =
				TerrainModeState::ResolveExportSource(refState.export_source_index);
			const AshEngine::TerrainHeightFileFormat exportFormat =
				TerrainModeState::ResolveHeightFileFormat(
					refState.export_format_index, refState.export_raw_format_index);
			const bool hasSelectedExportLayer =
				(exportSource != AshEngine::TerrainExportSource::HeightEditLayer &&
				 exportSource != AshEngine::TerrainExportSource::MaterialWeightLayer) ||
				refView.authoring_config.brush.layer_id.is_valid();
			const bool validMaterialExport =
				exportSource != AshEngine::TerrainExportSource::MaterialWeightLayer ||
				refState.export_material_layer_index < AshEngine::k_terrain_material_layer_count;
			const bool validExportChannel =
				exportFormat != AshEngine::TerrainHeightFileFormat::Exr ||
				!refState.export_exr_channel.empty();
			const bool canExport =
				hasWorkingSet && !fileOperationInProgress && !strokeActive &&
				!refState.export_heightmap_path.empty() &&
				hasSelectedExportLayer && validMaterialExport && validExportChannel;
			refUi.begin_disabled(!canExport);
			if (refUi.button("Export Heightmap"))
			{
				TerrainEditorIntent intent{};
				intent.kind = TerrainEditorIntent::Kind::Export;
				intent.export_desc = refState.BuildExportDesc(
					refView.authoring_config.brush.layer_id);
				refResult.intents.push_back(std::move(intent));
			}
			refUi.end_disabled();

			refUi.separator();
			const bool canSave =
				hasWorkingSet && refView.dirty && !fileOperationInProgress && !strokeActive && !readOnly;
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
				!fileOperationInProgress && !strokeActive &&
				(!readOnly || (recoveryState && refView.p_external_change_state->can_save_as));
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

			if (recoveryState)
			{
				const bool canRepair = refView.p_external_change_state->can_repair &&
					!fileOperationInProgress && !strokeActive;
				refUi.begin_disabled(!canRepair);
				if (refUi.button("Repair"))
				{
					TerrainEditorIntent intent{};
					intent.kind = TerrainEditorIntent::Kind::Repair;
					refResult.intents.push_back(std::move(intent));
				}
				refUi.end_disabled();
			}
			else
			{
				const bool canReload = hasWorkingSet && !refView.blocking_operation &&
					!fileOperationInProgress && !strokeActive && !readOnly;
				refUi.begin_disabled(!canReload);
				if (refUi.button("Reload"))
				{
					TerrainEditorIntent intent{};
					intent.kind = TerrainEditorIntent::Kind::Reload;
					refResult.intents.push_back(std::move(intent));
				}
				refUi.end_disabled();
			}

			const bool canOptimize =
				hasWorkingSet && !refView.dirty && !refView.pending_composition &&
				!refView.blocking_operation && !fileOperationInProgress && !strokeActive && !readOnly;
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
			const bool readOnly = IsTerrainReadOnly(refView);
			const bool canEditLayers =
				refView.p_working_set &&
				refView.preview.query_status == AshEngine::TerrainQueryStatus::Ready &&
				!refView.preview.stroke_active &&
				!refView.blocking_operation && !readOnly;
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
		const bool readOnly = IsTerrainReadOnly(refView);
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
			if (!readOnly && config.mode != TerrainEditorMode::Manage)
			{
				config.mode = TerrainEditorMode::Manage;
				configChanged = true;
			}
			DrawManageTab(refUi, refView, refState, result);
			refUi.end_tab_item();
		}
		if (refUi.begin_tab_item("Sculpt"))
		{
			if (!readOnly && config.mode != TerrainEditorMode::Sculpt)
			{
				config.mode = TerrainEditorMode::Sculpt;
				configChanged = true;
			}
			refUi.begin_disabled(refView.preview.stroke_active || readOnly);
			configChanged = DrawSculptTab(refUi, refView, config) || configChanged;
			refUi.end_disabled();
			refUi.end_tab_item();
		}
		if (refUi.begin_tab_item("Paint"))
		{
			if (!readOnly && config.mode != TerrainEditorMode::Paint)
			{
				config.mode = TerrainEditorMode::Paint;
				configChanged = true;
			}
			refUi.begin_disabled(refView.preview.stroke_active || readOnly);
			configChanged = DrawPaintTab(refUi, refView, config) || configChanged;
			refUi.end_disabled();
			refUi.end_tab_item();
		}
		if (refUi.begin_tab_item("Layers"))
		{
			if (!readOnly && config.mode != TerrainEditorMode::Layers)
			{
				config.mode = TerrainEditorMode::Layers;
				configChanged = true;
			}
			DrawLayersTab(refUi, refView, refState, result);
			refUi.end_tab_item();
		}
		refUi.end_tab_bar();
		refUi.end_disabled();

		if (configChanged && !lockTabs && !readOnly)
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
