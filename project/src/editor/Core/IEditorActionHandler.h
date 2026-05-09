#pragma once

#include <string_view>

namespace AshEditor
{
	class IEditorActionHandler
	{
	public:
		virtual ~IEditorActionHandler() = default;

		virtual bool CanExecuteAction(std::string_view svActionId) const = 0;
		virtual void ExecuteAction(std::string_view svActionId) = 0;
	};
}
