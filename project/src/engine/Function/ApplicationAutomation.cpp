#include "Function/ApplicationAutomation.h"

namespace AshEngine
{
	void ApplicationAutomationController::configure(
		ApplicationAutomationMode mode,
		double timeout_seconds)
	{
		m_mode = mode;
		m_outcome = ApplicationAutomationOutcome::Running;
		m_timeout_seconds = timeout_seconds > 0.0 ? timeout_seconds : 0.0;
		m_armed_render_asset_epoch = 0;
		m_capture_armed = false;
		m_capture_completion_pending = false;
	}

	bool ApplicationAutomationController::should_request_capture(
		const ApplicationAutomationPreFrame& frame) const
	{
		return
			m_mode == ApplicationAutomationMode::FrameDump &&
			m_outcome == ApplicationAutomationOutcome::Running &&
			m_capture_armed &&
			frame.application_readiness == ApplicationReadiness::Ready &&
			!frame.render_assets_pending &&
			!frame.render_assets_failed &&
			!frame.render_commands_pending &&
			frame.render_asset_epoch == m_armed_render_asset_epoch;
	}

	ApplicationAutomationDecision ApplicationAutomationController::observe_presented_frame(
		const ApplicationAutomationFrame& frame,
		bool capture_requested,
		bool capture_succeeded,
		double elapsed_seconds)
	{
		ApplicationAutomationDecision decision{};
		if (m_mode == ApplicationAutomationMode::Disabled)
		{
			return decision;
		}
		if (m_outcome != ApplicationAutomationOutcome::Running)
		{
			decision.request_exit = true;
			decision.succeeded = m_outcome == ApplicationAutomationOutcome::Succeeded;
			return decision;
		}

		if (frame.application_readiness == ApplicationReadiness::Failed || frame.render_assets_failed)
		{
			return finish(ApplicationAutomationOutcome::Failed);
		}

		const bool ready_frame = is_ready_frame(frame);
		const bool capture_epoch_matches_arm =
			!capture_requested ||
			(m_capture_armed && frame.render_asset_epoch == m_armed_render_asset_epoch);
		if (capture_requested && (!ready_frame || !capture_epoch_matches_arm))
		{
			decision.discard_capture = true;
			m_capture_armed = false;
		}

		if (m_timeout_seconds > 0.0 && elapsed_seconds >= m_timeout_seconds)
		{
			ApplicationAutomationDecision timeout = finish(ApplicationAutomationOutcome::TimedOut);
			timeout.discard_capture = decision.discard_capture;
			return timeout;
		}

		const bool ready = ready_frame && capture_epoch_matches_arm;
		if (ready)
		{
			if (m_mode == ApplicationAutomationMode::Smoke)
			{
				return finish(ApplicationAutomationOutcome::Succeeded);
			}

			if (capture_requested)
			{
				if (capture_succeeded)
				{
					m_capture_armed = false;
					m_capture_completion_pending = true;
					decision.accept_capture = true;
					return decision;
				}
				return finish(ApplicationAutomationOutcome::Failed);
			}

			m_capture_armed = true;
			m_armed_render_asset_epoch = frame.render_asset_epoch;
			decision.invalidate_temporal_history = true;
		}

		return decision;
	}

	ApplicationAutomationDecision ApplicationAutomationController::complete_capture(
		bool write_succeeded,
		double elapsed_seconds)
	{
		if (!m_capture_completion_pending || m_outcome != ApplicationAutomationOutcome::Running)
		{
			return {};
		}
		m_capture_completion_pending = false;
		if (m_timeout_seconds > 0.0 && elapsed_seconds >= m_timeout_seconds)
		{
			return finish(ApplicationAutomationOutcome::TimedOut);
		}
		return finish(write_succeeded
			? ApplicationAutomationOutcome::Succeeded
			: ApplicationAutomationOutcome::Failed,
			write_succeeded);
	}

	ApplicationAutomationOutcome ApplicationAutomationController::outcome() const
	{
		return m_outcome;
	}

	bool ApplicationAutomationController::is_ready_frame(
		const ApplicationAutomationFrame& frame) const
	{
		const bool capture_content_ready =
			m_mode != ApplicationAutomationMode::FrameDump ||
			frame.scene_packets_capture_ready == frame.scene_packets_attempted;
		return
			frame.application_readiness == ApplicationReadiness::Ready &&
			!frame.render_assets_pending &&
			!frame.render_assets_failed &&
			!frame.render_commands_pending &&
			frame.present_completed &&
			frame.scene_packets_attempted > 0 &&
			frame.scene_packets_failed == 0 &&
			frame.scene_packets_succeeded == frame.scene_packets_attempted &&
			capture_content_ready &&
			frame.scene_submission_asset_epoch == frame.render_asset_epoch;
	}

	ApplicationAutomationDecision ApplicationAutomationController::finish(
		ApplicationAutomationOutcome outcome,
		bool accept_capture)
	{
		m_outcome = outcome;
		m_capture_armed = false;
		m_capture_completion_pending = false;
		ApplicationAutomationDecision decision{};
		decision.request_exit = true;
		decision.succeeded = outcome == ApplicationAutomationOutcome::Succeeded;
		decision.accept_capture = accept_capture;
		return decision;
	}
}
