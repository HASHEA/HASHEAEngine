#pragma once

namespace AshEditor
{
	class IActionInvoker
	{
	public:
		virtual ~IActionInvoker() = default;

		virtual bool InvokeAction(const char* pActionId, const char* pSource) = 0;
	};
}
