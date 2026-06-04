#include "App/EditorLogBridge.h"

#include "Base/hlog.h"
#include "Core/EditorEventBus.h"

#include "spdlog/details/log_msg.h"
#include "spdlog/logger.h"
#include "spdlog/sinks/base_sink.h"

#include <algorithm>
#include <utility>

namespace AshEditor
{
	namespace
	{
		EditorLogSeverity ToEditorLogSeverity(spdlog::level::level_enum eLevel)
		{
			switch (eLevel)
			{
			case spdlog::level::trace:
			case spdlog::level::debug:
				return EditorLogSeverity::Trace;
			case spdlog::level::info:
				return EditorLogSeverity::Info;
			case spdlog::level::warn:
				return EditorLogSeverity::Warning;
			case spdlog::level::err:
			case spdlog::level::critical:
			case spdlog::level::off:
			default:
				return EditorLogSeverity::Error;
			}
		}

		void AttachSinkToLogger(
			const std::shared_ptr<spdlog::logger>& spLogger,
			const spdlog::sink_ptr& spSink)
		{
			if (!spLogger || !spSink)
			{
				return;
			}

			std::vector<spdlog::sink_ptr>& vecSinks = spLogger->sinks();
			if (std::find(vecSinks.begin(), vecSinks.end(), spSink) == vecSinks.end())
			{
				vecSinks.push_back(spSink);
			}
		}

		void DetachSinkFromLogger(
			const std::shared_ptr<spdlog::logger>& spLogger,
			const spdlog::sink_ptr& spSink)
		{
			if (!spLogger || !spSink)
			{
				return;
			}

			std::vector<spdlog::sink_ptr>& vecSinks = spLogger->sinks();
			vecSinks.erase(
				std::remove(vecSinks.begin(), vecSinks.end(), spSink),
				vecSinks.end());
		}
	}

	class EditorLogBridge::Sink final : public spdlog::sinks::base_sink<std::mutex>
	{
	public:
		explicit Sink(EditorLogBridge& refOwner)
			: _refOwner(refOwner)
		{
		}

	protected:
		void sink_it_(const spdlog::details::log_msg& refMessage) override
		{
			EditorLogEvent event{};
			event.eSeverity = ToEditorLogSeverity(refMessage.level);
			event.strSource = refMessage.logger_name.size() == 0
				? std::string("Runtime")
				: std::string(refMessage.logger_name.data(), refMessage.logger_name.size());
			event.strMessage = std::string(refMessage.payload.data(), refMessage.payload.size());
			_refOwner.Enqueue(std::move(event));
		}

		void flush_() override
		{
		}

	private:
		EditorLogBridge& _refOwner;
	};

	EditorLogBridge::EditorLogBridge() = default;

	EditorLogBridge::~EditorLogBridge()
	{
		Detach();
	}

	void EditorLogBridge::Attach(EditorEventBus& refEventBus)
	{
		_pEventBus = &refEventBus;
		if (!_spSink)
		{
			_spSink = std::make_shared<Sink>(*this);
		}

		AttachSinkToLogger(AshEngine::LogService::instance()->get_engine_logger(), _spSink);
		AttachSinkToLogger(AshEngine::LogService::instance()->get_app_logger(), _spSink);
	}

	void EditorLogBridge::Detach()
	{
		if (_spSink)
		{
			DetachSinkFromLogger(AshEngine::LogService::instance()->get_engine_logger(), _spSink);
			DetachSinkFromLogger(AshEngine::LogService::instance()->get_app_logger(), _spSink);
		}

		_pEventBus = nullptr;
		std::scoped_lock<std::mutex> lock(_mutexPending);
		_vecPendingEvents.clear();
	}

	void EditorLogBridge::FlushPending()
	{
		if (!_pEventBus)
		{
			return;
		}

		std::vector<EditorLogEvent> vecPendingEvents{};
		{
			std::scoped_lock<std::mutex> lock(_mutexPending);
			vecPendingEvents.swap(_vecPendingEvents);
		}

		for (const EditorLogEvent& refEvent : vecPendingEvents)
		{
			_pEventBus->Publish(refEvent);
		}
	}

	void EditorLogBridge::Enqueue(EditorLogEvent event)
	{
		std::scoped_lock<std::mutex> lock(_mutexPending);
		_vecPendingEvents.push_back(std::move(event));
	}
}
