#pragma once

#include <string>

namespace AshEditor
{
	class INotificationSink
	{
	public:
		virtual ~INotificationSink() = default;

		virtual void Notify(const std::string& strMessage, const char* pSource = "Editor") = 0;
	};
}
