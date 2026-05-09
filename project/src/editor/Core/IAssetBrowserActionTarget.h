#pragma once

namespace AshEditor
{
	class IAssetBrowserActionTarget
	{
	public:
		virtual ~IAssetBrowserActionTarget() = default;

		virtual bool CanExecuteOpenSelected() const = 0;
		virtual void ExecuteOpenSelected() = 0;
		virtual bool CanExecuteNavigateUp() const = 0;
		virtual void ExecuteNavigateUp() = 0;
	};
}
