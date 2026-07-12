#include "Widgets/PropertyEditor/PropertyEditorWidget.h"

#include "Function/Gui/UICommon.h"
#include "Function/Gui/UIContext.h"
#include "Widgets/PropertyEditor/IPropertyTypeEditor.h"
#include "Widgets/PropertyEditor/PropertyEditorRegistry.h"
#include "Widgets/PropertyEditor/PropertyEditorTypes.h"

#include <cstdint>

namespace AshEditor
{
	PropertyEditorWidget::PropertyEditorWidget(PropertyEditorRegistry& refRegistry)
		: _pRegistry(&refRegistry)
	{
	}

	bool PropertyEditorWidget::DrawField(const PropertyDesc& refDesc, const PropertyEditorContext& refCtx)
	{
		if (!_pRegistry || !refCtx.pUi)
		{
			return false;
		}

		IPropertyTypeEditor* pEditor = _pRegistry->Find(refDesc.strTypeId);
		if (!pEditor)
		{
			refCtx.pUi->text_colored(
				AshEngine::UIColor{ 1.0f, 0.35f, 0.35f, 1.0f },
				"<unknown type: %s>",
				refDesc.strTypeId.c_str());
			return false;
		}

		return pEditor->DrawEditor(refDesc, refCtx);
	}

	bool PropertyEditorWidget::DrawFields(const std::vector<PropertyDesc>& refDescs, const PropertyEditorContext& refCtx)
	{
		if (!refCtx.pUi || refDescs.empty())
		{
			return false;
		}
		AshEngine::UIContext& refUi = *refCtx.pUi;

		const AshEngine::UITableFlags flags =
			AshEngine::UITableFlagBits::RowBg |
			AshEngine::UITableFlagBits::BordersInner |
			AshEngine::UITableFlagBits::Resizable |
			AshEngine::UITableFlagBits::SizingStretchProp;

		bool bAnyChanged = false;
		if (refUi.begin_table("##PropertyEditor", 2, flags))
		{
			refUi.table_setup_column("Name", AshEngine::UITableColumnFlagBits::WidthStretch, 0.4f);
			refUi.table_setup_column("Value", AshEngine::UITableColumnFlagBits::WidthStretch, 0.6f);

			for (size_t uIndex = 0; uIndex < refDescs.size(); ++uIndex)
			{
				const PropertyDesc& refDesc = refDescs[uIndex];
				refUi.table_next_row();
				refUi.push_id(static_cast<int32_t>(uIndex));

				refUi.table_next_column();
				refUi.text("%s", refDesc.strLabel.c_str());

				refUi.table_next_column();
				refUi.set_next_item_width(-1.0f);
				bAnyChanged = DrawField(refDesc, refCtx) || bAnyChanged;

				refUi.pop_id();
			}

			refUi.end_table();
		}

		return bAnyChanged;
	}
}
