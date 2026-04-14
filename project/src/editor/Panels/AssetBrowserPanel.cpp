#include "Panels/AssetBrowserPanel.h"
#include "Base/hlog.h"
#include "Services/AssetDatabaseService.h"
#include "Services/SelectionService.h"
#include "imgui.h"

namespace AshEditor
{
	AssetBrowserPanel::AssetBrowserPanel()
		: EditorPanel("asset_browser", "Asset Browser")
	{
	}

	void AssetBrowserPanel::on_attach(EditorContext& context)
	{
		(void)context;
		HLogInfo("AssetBrowserPanel attached.");
	}

	void AssetBrowserPanel::on_gui(EditorContext& context)
	{
		if (!begin_panel_window())
		{
			end_panel_window();
			return;
		}

		if (!context.asset_database_service)
		{
			ImGui::TextUnformatted("Asset database unavailable.");
			end_panel_window();
			return;
		}

		const auto& items = context.asset_database_service->get_items();
		ImGui::Text("Root: %s", context.asset_database_service->get_asset_root().string().c_str());
		ImGui::Text("Items: %u", static_cast<uint32_t>(items.size()));
		const std::string last_error = context.asset_database_service->get_last_error();
		if (!last_error.empty())
		{
			ImGui::TextColored(ImVec4(0.90f, 0.40f, 0.40f, 1.0f), "Last Error: %s", last_error.c_str());
		}
		ImGui::Separator();
		ImGui::BeginChild("AssetBrowserItems");
		if (ImGui::BeginTable("AssetBrowserTable", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY))
		{
			ImGui::TableSetupColumn("Path");
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 110.0f);
			ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 90.0f);
			ImGui::TableHeadersRow();

			for (const AshEngine::AssetInfo& item : items)
			{
				const bool selected =
					context.selection_service &&
					context.selection_service->get_selection().kind == EditorSelectionKind::Asset &&
					context.selection_service->get_selection().id == item.id;
				const std::string path_label = item.relative_path.generic_string();

				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				if (ImGui::Selectable(path_label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
				{
					if (context.selection_service)
					{
						context.selection_service->select({ EditorSelectionKind::Asset, item.id, item.name, path_label });
					}
				}
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(AssetDatabaseService::get_type_label(item.type));
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(AssetDatabaseService::get_load_state_label(context.asset_database_service->get_load_state(item.id)));
			}

			ImGui::EndTable();
		}
		ImGui::EndChild();
		ImGui::Separator();
		ImGui::TextUnformatted("Importer jobs, GUID metadata and thumbnails are reserved for the next phase.");

		end_panel_window();
	}
}
