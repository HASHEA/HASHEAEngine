#pragma once

#include "Base/hcore.h"
#include "Graphics/RHIBackend.h"

#include <array>
#include <cstdint>
#include <limits>
#include <string>

namespace RHI
{
	class CommandBuffer;

	constexpr uint32_t kGpuTimingFrameRingDepth = 3u;
	constexpr uint32_t kGpuTimingMetricCount = 11u;
	constexpr uint32_t kGpuTimingQueriesPerFrame = kGpuTimingMetricCount * 2u;
	constexpr uint32_t kGpuTimingQueryCapacity = kGpuTimingFrameRingDepth * kGpuTimingQueriesPerFrame;
	constexpr uint32_t kGpuTimingInvalidQueryIndex = std::numeric_limits<uint32_t>::max();

	enum class GpuTimingMetric : uint8_t
	{
		Frame = 0,
		GBuffer,
		AmbientOcclusion,
		Shadows,
		DeferredLighting,
		EnvironmentAndSky,
		Particles,
		VolumetricLighting,
		Bloom,
		TemporalAA,
		ToneMapAndOverlays,
		Count,
		Invalid = std::numeric_limits<uint8_t>::max(),
	};

	enum class GpuTimingPollResult : uint8_t
	{
		Ready,
		Pending,
		Empty,
	};

	enum class GpuTimingInvalidReason : uint8_t
	{
		None,
		Aborted,
		SubmissionFailed,
		BackendUnsupported,
		DuplicateScope,
		OverlappingScope,
		IncompleteScope,
		InvalidMetric,
		QueryCapacityExceeded,
		FrameStateError,
		InvalidTimestampCalibration,
	};

	struct GpuTimingMetricSample
	{
		GpuTimingMetric metric = GpuTimingMetric::Invalid;
		double duration_ms = 0.0;
		bool valid = false;
	};

	struct ASH_API GpuFrameTimingSample
	{
		GpuFrameTimingSample();

		uint64_t frame_id = 0u;
		bool valid = false;
		GpuTimingInvalidReason invalid_reason = GpuTimingInvalidReason::None;
		std::array<GpuTimingMetricSample, kGpuTimingMetricCount> metrics{};
	};

	struct ASH_API GpuTimingTelemetryInfo
	{
		Backend backend = Backend::Default;
		std::string adapter_name{};
		std::string driver_version{};
		double timestamp_frequency_hz = 0.0;
		double timestamp_period_ns = 0.0;
		uint32_t timestamp_valid_bits = 0u;
		uint32_t query_capacity = kGpuTimingQueryCapacity;
		std::string timing_scope = "main_command_buffer";
	};

	class IGpuTimingTelemetry
	{
	public:
		virtual ~IGpuTimingTelemetry() = default;
		virtual bool begin_frame(CommandBuffer* cmd, uint64_t frame_id) = 0;
		virtual bool begin_scope(CommandBuffer* cmd, GpuTimingMetric metric) = 0;
		virtual void end_scope(CommandBuffer* cmd, GpuTimingMetric metric) = 0;
		virtual void end_frame(CommandBuffer* cmd, uint64_t frame_id) = 0;
		virtual void commit_frame(uint64_t frame_id) = 0;
		virtual void abort_frame(uint64_t frame_id, GpuTimingInvalidReason reason) = 0;
		virtual GpuTimingPollResult poll_completed_frame(GpuFrameTimingSample& out_sample) = 0;
		virtual GpuTimingTelemetryInfo get_info() const = 0;
	};

	struct GpuTimingQueryPair
	{
		uint32_t begin_query = kGpuTimingInvalidQueryIndex;
		uint32_t end_query = kGpuTimingInvalidQueryIndex;
		bool begun = false;
		bool ended = false;
	};

	struct GpuTimingFrameRecord
	{
		uint64_t frame_id = 0u;
		uint32_t slot_index = 0u;
		uint32_t query_count = 0u;
		bool valid = false;
		GpuTimingInvalidReason invalid_reason = GpuTimingInvalidReason::None;
		std::array<GpuTimingQueryPair, kGpuTimingMetricCount> query_pairs{};
	};

	// Fixed-capacity state shared by both backends. Query indices are absolute within
	// a ring-wide query allocation of kGpuTimingQueryCapacity entries.
	class ASH_API GpuTimingFrameState
	{
	public:
		bool begin_frame(uint64_t frame_id, uint32_t& out_query_index);
		bool begin_scope(GpuTimingMetric metric, uint32_t& out_query_index);
		bool end_scope(GpuTimingMetric metric, uint32_t& out_query_index);
		bool end_frame(uint64_t frame_id, uint32_t& out_query_index);
		bool commit_frame(uint64_t frame_id);
		bool abort_frame(uint64_t frame_id, GpuTimingInvalidReason reason);

		// Backends mark a committed frame ready only after observing their completion
		// primitive. Pending and Empty never block and never consume a ring slot.
		bool mark_frame_ready(uint64_t frame_id);
		GpuTimingPollResult poll_frame(GpuTimingFrameRecord& out_record) const;
		bool release_frame(uint64_t frame_id);

	private:
		enum class SlotStatus : uint8_t
		{
			Empty,
			Recording,
			Recorded,
			Pending,
			Ready,
		};

		struct Slot
		{
			SlotStatus status = SlotStatus::Empty;
			uint64_t commit_sequence = 0u;
			GpuTimingFrameRecord record{};
		};

		static bool metric_index(GpuTimingMetric metric, uint32_t& out_index);
		bool reserve_query(Slot& slot, uint32_t& out_query_index);
		void set_invalid(Slot& slot, GpuTimingInvalidReason reason);
		Slot* recording_slot();
		const Slot* oldest_committed_slot() const;
		Slot* find_slot(uint64_t frame_id);

		std::array<Slot, kGpuTimingFrameRingDepth> m_slots{};
		uint32_t m_recording_slot = kGpuTimingFrameRingDepth;
		GpuTimingMetric m_active_scope = GpuTimingMetric::Invalid;
		uint64_t m_next_commit_sequence = 1u;
	};

	ASH_API const char* gpu_timing_metric_name(GpuTimingMetric metric);
	ASH_API bool gpu_timing_delta_ticks(
		uint64_t begin_tick,
		uint64_t end_tick,
		uint32_t valid_bits,
		uint64_t& out_elapsed_ticks);
	ASH_API bool gpu_timing_ticks_to_milliseconds_from_frequency(
		uint64_t ticks,
		double frequency_hz,
		double& out_milliseconds);
	ASH_API bool gpu_timing_ticks_to_milliseconds_from_period(
		uint64_t ticks,
		double period_ns,
		double& out_milliseconds);
}
