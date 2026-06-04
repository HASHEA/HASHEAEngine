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
		virtual bool CanExecuteCreateFolder() const = 0;
		virtual void ExecuteCreateFolder() = 0;
		virtual bool CanExecuteInstantiateSelected() const = 0;
		virtual void ExecuteInstantiateSelected() = 0;
		virtual bool CanExecuteRenameSelected() const = 0;
		virtual void ExecuteRenameSelected() = 0;
		virtual bool CanExecuteDeleteSelected() const = 0;
		virtual void ExecuteDeleteSelected() = 0;
		virtual bool CanExecuteReimportSelected() const = 0;
		virtual void ExecuteReimportSelected() = 0;
	};
}
