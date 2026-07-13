#pragma once

#include "Base/hcore.h"
#include <cstdint>

namespace AshEngine
{
	enum class ApplicationReadiness : uint8_t
	{
		Pending = 0,
		Ready,
		Failed
	};

	enum class ApplicationAutomationMode : uint8_t
	{
		Disabled = 0,
		Smoke,
		FrameDump
	};

	enum class ApplicationAutomationOutcome : uint8_t
	{
		Running = 0,
		Succeeded,
		Failed,
		TimedOut
	};

	struct ApplicationAutomationPreFrame
	{
		ApplicationReadiness application_readiness = ApplicationReadiness::Pending;
		uint64_t render_asset_epoch = 0;
		bool render_assets_pending = false;
		bool render_assets_failed = false;
		bool render_commands_pending = false;
	};

	struct ApplicationAutomationFrame : ApplicationAutomationPreFrame
	{
		uint64_t scene_submission_asset_epoch = 0;
		uint32_t scene_packets_attempted = 0;
		uint32_t scene_packets_succeeded = 0;
		uint32_t scene_packets_failed = 0;
		uint32_t scene_packets_capture_ready = 0;
		bool present_completed = false;
	};

	struct ApplicationAutomationDecision
	{
		bool request_exit = false;
		bool succeeded = false;
		bool invalidate_temporal_history = false;
		bool accept_capture = false;
		bool discard_capture = false;
	};

	class ASH_API ApplicationAutomationController
	{
	public:
		void configure(ApplicationAutomationMode mode, double timeout_seconds);
		bool should_request_capture(const ApplicationAutomationPreFrame& frame) const;
		ApplicationAutomationDecision observe_presented_frame(
			const ApplicationAutomationFrame& frame,
			bool capture_requested,
			bool capture_succeeded,
			double elapsed_seconds);
		ApplicationAutomationDecision complete_capture(bool write_succeeded, double elapsed_seconds);
		ApplicationAutomationOutcome outcome() const;

	private:
		bool is_ready_frame(const ApplicationAutomationFrame& frame) const;
		ApplicationAutomationDecision finish(ApplicationAutomationOutcome outcome, bool accept_capture = false);

	private:
		ApplicationAutomationMode m_mode = ApplicationAutomationMode::Disabled;
		ApplicationAutomationOutcome m_outcome = ApplicationAutomationOutcome::Running;
		double m_timeout_seconds = 0.0;
		uint64_t m_armed_render_asset_epoch = 0;
		bool m_capture_armed = false;
		bool m_capture_completion_pending = false;
	};
}
