#include "Graphics/GpuTimingTelemetryRHI.h"

#include <cmath>

namespace RHI
{
	GpuFrameTimingSample::GpuFrameTimingSample()
	{
		for (uint32_t metric_index = 0u; metric_index < kGpuTimingMetricCount; ++metric_index)
		{
			metrics[metric_index].metric = static_cast<GpuTimingMetric>(metric_index);
		}
	}

	const char* gpu_timing_metric_name(GpuTimingMetric metric)
	{
		switch (metric)
		{
		case GpuTimingMetric::Frame:
			return "GPU.Frame";
		case GpuTimingMetric::GBuffer:
			return "GPU.GBuffer";
		case GpuTimingMetric::AmbientOcclusion:
			return "GPU.AmbientOcclusion";
		case GpuTimingMetric::Shadows:
			return "GPU.Shadows";
		case GpuTimingMetric::DeferredLighting:
			return "GPU.DeferredLighting";
		case GpuTimingMetric::EnvironmentAndSky:
			return "GPU.EnvironmentAndSky";
		case GpuTimingMetric::Particles:
			return "GPU.Particles";
		case GpuTimingMetric::VolumetricLighting:
			return "GPU.VolumetricLighting";
		case GpuTimingMetric::Bloom:
			return "GPU.Bloom";
		case GpuTimingMetric::TemporalAA:
			return "GPU.TemporalAA";
		case GpuTimingMetric::ToneMapAndOverlays:
			return "GPU.ToneMapAndOverlays";
		case GpuTimingMetric::Count:
		case GpuTimingMetric::Invalid:
		default:
			return "GPU.Invalid";
		}
	}

	bool gpu_timing_delta_ticks(
		uint64_t begin_tick,
		uint64_t end_tick,
		uint32_t valid_bits,
		uint64_t& out_elapsed_ticks)
	{
		if (valid_bits == 0u || valid_bits > 64u)
		{
			return false;
		}

		if (valid_bits == 64u)
		{
			out_elapsed_ticks = end_tick - begin_tick;
			return true;
		}

		const uint64_t timestamp_mask = (uint64_t{ 1 } << valid_bits) - 1u;
		out_elapsed_ticks = (end_tick - begin_tick) & timestamp_mask;
		return true;
	}

	bool gpu_timing_ticks_to_milliseconds_from_frequency(
		uint64_t ticks,
		double frequency_hz,
		double& out_milliseconds)
	{
		if (!std::isfinite(frequency_hz) || frequency_hz <= 0.0)
		{
			return false;
		}

		const double milliseconds = (static_cast<double>(ticks) * 1000.0) / frequency_hz;
		if (!std::isfinite(milliseconds))
		{
			return false;
		}
		out_milliseconds = milliseconds;
		return true;
	}

	bool gpu_timing_ticks_to_milliseconds_from_period(
		uint64_t ticks,
		double period_ns,
		double& out_milliseconds)
	{
		if (!std::isfinite(period_ns) || period_ns <= 0.0)
		{
			return false;
		}

		const double milliseconds = static_cast<double>(ticks) * period_ns / 1000000.0;
		if (!std::isfinite(milliseconds))
		{
			return false;
		}
		out_milliseconds = milliseconds;
		return true;
	}

	bool GpuTimingFrameState::metric_index(GpuTimingMetric metric, uint32_t& out_index)
	{
		const uint32_t index = static_cast<uint32_t>(metric);
		if (index >= kGpuTimingMetricCount)
		{
			return false;
		}

		out_index = index;
		return true;
	}

	bool GpuTimingFrameState::reserve_query(Slot& slot, uint32_t& out_query_index)
	{
		if (slot.record.query_count >= kGpuTimingQueriesPerFrame)
		{
			set_invalid(slot, GpuTimingInvalidReason::QueryCapacityExceeded);
			return false;
		}

		out_query_index = slot.record.slot_index * kGpuTimingQueriesPerFrame + slot.record.query_count;
		++slot.record.query_count;
		return true;
	}

	void GpuTimingFrameState::set_invalid(Slot& slot, GpuTimingInvalidReason reason)
	{
		slot.record.valid = false;
		if (slot.record.invalid_reason == GpuTimingInvalidReason::None)
		{
			slot.record.invalid_reason = reason;
		}
	}

	GpuTimingFrameState::Slot* GpuTimingFrameState::recording_slot()
	{
		if (m_recording_slot >= kGpuTimingFrameRingDepth)
		{
			return nullptr;
		}
		return &m_slots[m_recording_slot];
	}

	const GpuTimingFrameState::Slot* GpuTimingFrameState::oldest_committed_slot() const
	{
		const Slot* oldest = nullptr;
		for (const Slot& slot : m_slots)
		{
			if (slot.status != SlotStatus::Pending && slot.status != SlotStatus::Ready)
			{
				continue;
			}
			if (!oldest || slot.commit_sequence < oldest->commit_sequence)
			{
				oldest = &slot;
			}
		}
		return oldest;
	}

	GpuTimingFrameState::Slot* GpuTimingFrameState::find_slot(uint64_t frame_id)
	{
		for (Slot& slot : m_slots)
		{
			if (slot.status != SlotStatus::Empty && slot.record.frame_id == frame_id)
			{
				return &slot;
			}
		}
		return nullptr;
	}

	bool GpuTimingFrameState::begin_frame(
		uint64_t frame_id,
		uint32_t physical_slot,
		uint32_t& out_query_index)
	{
		if (physical_slot >= kGpuTimingFrameRingDepth || recording_slot() || find_slot(frame_id))
		{
			return false;
		}

		Slot& slot = m_slots[physical_slot];
		if (slot.status != SlotStatus::Empty)
		{
			return false;
		}

		slot = {};
		slot.status = SlotStatus::Recording;
		slot.record.frame_id = frame_id;
		slot.record.slot_index = physical_slot;
		slot.record.valid = true;
		m_recording_slot = physical_slot;
		m_active_scope = GpuTimingMetric::Invalid;

		GpuTimingQueryPair& frame_pair = slot.record.query_pairs[static_cast<uint32_t>(GpuTimingMetric::Frame)];
		if (!reserve_query(slot, out_query_index))
		{
			return false;
		}
		frame_pair.begin_query = out_query_index;
		frame_pair.begun = true;
		return true;
	}

	bool GpuTimingFrameState::begin_scope(GpuTimingMetric metric, uint32_t& out_query_index)
	{
		Slot* slot = recording_slot();
		if (!slot || slot->status != SlotStatus::Recording)
		{
			return false;
		}

		uint32_t index = 0u;
		if (!metric_index(metric, index))
		{
			set_invalid(*slot, GpuTimingInvalidReason::InvalidMetric);
			return false;
		}
		GpuTimingQueryPair& pair = slot->record.query_pairs[index];
		if (pair.begun || pair.ended)
		{
			set_invalid(*slot, GpuTimingInvalidReason::DuplicateScope);
			return false;
		}
		if (m_active_scope != GpuTimingMetric::Invalid)
		{
			set_invalid(*slot, GpuTimingInvalidReason::OverlappingScope);
			return false;
		}
		if (!reserve_query(*slot, out_query_index))
		{
			return false;
		}

		pair.begin_query = out_query_index;
		pair.begun = true;
		m_active_scope = metric;
		return true;
	}

	bool GpuTimingFrameState::end_scope(GpuTimingMetric metric, uint32_t& out_query_index)
	{
		Slot* slot = recording_slot();
		if (!slot || slot->status != SlotStatus::Recording)
		{
			return false;
		}

		uint32_t index = 0u;
		if (!metric_index(metric, index))
		{
			set_invalid(*slot, GpuTimingInvalidReason::InvalidMetric);
			return false;
		}
		GpuTimingQueryPair& pair = slot->record.query_pairs[index];
		if (!pair.begun || pair.ended || m_active_scope != metric)
		{
			set_invalid(*slot, GpuTimingInvalidReason::IncompleteScope);
			return false;
		}
		if (!reserve_query(*slot, out_query_index))
		{
			return false;
		}

		pair.end_query = out_query_index;
		pair.ended = true;
		m_active_scope = GpuTimingMetric::Invalid;
		return true;
	}

	bool GpuTimingFrameState::end_frame(uint64_t frame_id, uint32_t& out_query_index)
	{
		Slot* slot = recording_slot();
		if (!slot || slot->status != SlotStatus::Recording || slot->record.frame_id != frame_id)
		{
			if (slot)
			{
				set_invalid(*slot, GpuTimingInvalidReason::FrameStateError);
			}
			return false;
		}

		bool complete = true;
		if (m_active_scope != GpuTimingMetric::Invalid)
		{
			set_invalid(*slot, GpuTimingInvalidReason::IncompleteScope);
			complete = false;
		}

		GpuTimingQueryPair& frame_pair =
			slot->record.query_pairs[static_cast<uint32_t>(GpuTimingMetric::Frame)];
		if (!reserve_query(*slot, out_query_index))
		{
			complete = false;
		}
		else
		{
			frame_pair.end_query = out_query_index;
			frame_pair.ended = true;
		}

		slot->status = SlotStatus::Recorded;
		m_recording_slot = kGpuTimingFrameRingDepth;
		m_active_scope = GpuTimingMetric::Invalid;
		return complete;
	}

	bool GpuTimingFrameState::commit_frame(uint64_t frame_id)
	{
		Slot* slot = find_slot(frame_id);
		if (!slot || slot->status != SlotStatus::Recorded)
		{
			return false;
		}

		slot->status = SlotStatus::Pending;
		slot->commit_sequence = m_next_commit_sequence++;
		return true;
	}

	bool GpuTimingFrameState::abort_frame(uint64_t frame_id, GpuTimingInvalidReason)
	{
		Slot* slot = find_slot(frame_id);
		if (!slot || (slot->status != SlotStatus::Recording && slot->status != SlotStatus::Recorded))
		{
			return false;
		}

		if (slot->status == SlotStatus::Recording)
		{
			m_recording_slot = kGpuTimingFrameRingDepth;
			m_active_scope = GpuTimingMetric::Invalid;
		}
		*slot = {};
		return true;
	}

	bool GpuTimingFrameState::fail_committed_frame(
		uint64_t frame_id,
		GpuTimingInvalidReason reason)
	{
		if (reason == GpuTimingInvalidReason::None)
		{
			return false;
		}

		Slot* slot = find_slot(frame_id);
		if (!slot || slot->status != SlotStatus::Pending)
		{
			return false;
		}

		slot->record.valid = false;
		slot->record.invalid_reason = reason;
		slot->status = SlotStatus::Ready;
		return true;
	}

	bool GpuTimingFrameState::mark_frame_ready(uint64_t frame_id)
	{
		Slot* slot = find_slot(frame_id);
		if (!slot || slot->status != SlotStatus::Pending)
		{
			return false;
		}
		slot->status = SlotStatus::Ready;
		return true;
	}

	GpuTimingPollResult GpuTimingFrameState::poll_frame(GpuTimingFrameRecord& out_record) const
	{
		const Slot* slot = oldest_committed_slot();
		if (!slot)
		{
			return GpuTimingPollResult::Empty;
		}

		out_record = slot->record;
		return slot->status == SlotStatus::Ready
			? GpuTimingPollResult::Ready
			: GpuTimingPollResult::Pending;
	}

	bool GpuTimingFrameState::release_frame(uint64_t frame_id)
	{
		Slot* slot = find_slot(frame_id);
		if (!slot || slot->status != SlotStatus::Ready || oldest_committed_slot() != slot)
		{
			return false;
		}
		*slot = {};
		return true;
	}
}
