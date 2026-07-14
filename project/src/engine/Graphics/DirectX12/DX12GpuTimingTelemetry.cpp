#include "DX12GpuTimingTelemetry.h"

#include "Graphics/CommandBuffer.h"

#include <algorithm>
#include <string>

namespace RHI
{
	namespace
	{
		constexpr uint64_t kReadbackSizeBytes =
			static_cast<uint64_t>(kGpuTimingQueryCapacity) * sizeof(uint64_t);

		std::string wide_to_utf8(const wchar_t* text)
		{
			if (!text || text[0] == L'\0')
			{
				return {};
			}

			const int required_size =
				WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
			if (required_size <= 1)
			{
				return {};
			}

			std::string result(static_cast<size_t>(required_size), '\0');
			WideCharToMultiByte(
				CP_UTF8,
				0,
				text,
				-1,
				result.data(),
				required_size,
				nullptr,
				nullptr);
			result.pop_back();
			return result;
		}

		std::string format_driver_version(const LARGE_INTEGER& version)
		{
			return std::to_string(HIWORD(version.HighPart)) + "." +
				std::to_string(LOWORD(version.HighPart)) + "." +
				std::to_string(HIWORD(version.LowPart)) + "." +
				std::to_string(LOWORD(version.LowPart));
		}
	}

	bool make_dx12_gpu_timing_resolve_range(
		uint32_t physical_slot,
		uint32_t query_count,
		DX12GpuTimingResolveRange& out_range)
	{
		if (physical_slot >= kGpuTimingFrameRingDepth ||
			query_count == 0u ||
			query_count > kGpuTimingQueriesPerFrame)
		{
			return false;
		}

		DX12GpuTimingResolveRange range{};
		range.start_query = physical_slot * kGpuTimingQueriesPerFrame;
		range.query_count = query_count;
		range.destination_offset_bytes =
			static_cast<uint64_t>(range.start_query) * sizeof(uint64_t);
		const uint64_t range_end = range.destination_offset_bytes +
			static_cast<uint64_t>(range.query_count) * sizeof(uint64_t);
		if (range_end > kReadbackSizeBytes)
		{
			return false;
		}

		out_range = range;
		return true;
	}

	DX12GpuTimingResolveResult resolve_dx12_gpu_timing_record(
		const GpuTimingFrameRecord& record,
		const std::array<uint64_t, kGpuTimingQueriesPerFrame>& timestamps,
		uint64_t timestamp_frequency_hz,
		GpuFrameTimingSample& out_sample)
	{
		if (timestamp_frequency_hz == 0u)
		{
			GpuFrameTimingSample unsupported_sample{};
			unsupported_sample.frame_id = record.frame_id;
			unsupported_sample.valid = false;
			unsupported_sample.invalid_reason = GpuTimingInvalidReason::BackendUnsupported;
			out_sample = unsupported_sample;
			return DX12GpuTimingResolveResult::Unsupported;
		}

		if (record.slot_index >= kGpuTimingFrameRingDepth ||
			record.query_count == 0u ||
			record.query_count > kGpuTimingQueriesPerFrame)
		{
			GpuFrameTimingSample invalid_sample{};
			invalid_sample.frame_id = record.frame_id;
			invalid_sample.valid = false;
			invalid_sample.invalid_reason = GpuTimingInvalidReason::FrameStateError;
			out_sample = invalid_sample;
			return DX12GpuTimingResolveResult::Ready;
		}

		GpuFrameTimingSample sample{};
		sample.frame_id = record.frame_id;
		sample.valid = record.valid;
		sample.invalid_reason = record.invalid_reason;
		if (!record.valid)
		{
			if (sample.invalid_reason == GpuTimingInvalidReason::None)
			{
				sample.invalid_reason = GpuTimingInvalidReason::FrameStateError;
			}
			out_sample = sample;
			return DX12GpuTimingResolveResult::Ready;
		}

		const uint32_t slot_begin = record.slot_index * kGpuTimingQueriesPerFrame;
		const GpuTimingQueryPair& frame_pair =
			record.query_pairs[static_cast<uint32_t>(GpuTimingMetric::Frame)];
		if (!frame_pair.begun || !frame_pair.ended)
		{
			sample.valid = false;
			sample.invalid_reason = GpuTimingInvalidReason::IncompleteScope;
			out_sample = sample;
			return DX12GpuTimingResolveResult::Ready;
		}
		if (frame_pair.begin_query < slot_begin ||
			frame_pair.end_query < slot_begin ||
			frame_pair.begin_query >= frame_pair.end_query)
		{
			sample.valid = false;
			sample.invalid_reason = GpuTimingInvalidReason::FrameStateError;
			out_sample = sample;
			return DX12GpuTimingResolveResult::Ready;
		}

		for (uint32_t metric_index = 0u; metric_index < kGpuTimingMetricCount; ++metric_index)
		{
			const GpuTimingQueryPair& pair = record.query_pairs[metric_index];
			if (!pair.begun && !pair.ended)
			{
				continue;
			}
			if (!pair.begun || !pair.ended)
			{
				sample.valid = false;
				sample.invalid_reason = GpuTimingInvalidReason::IncompleteScope;
				break;
			}
			if (pair.begin_query < slot_begin ||
				pair.end_query < slot_begin ||
				pair.begin_query >= pair.end_query)
			{
				sample.valid = false;
				sample.invalid_reason = GpuTimingInvalidReason::FrameStateError;
				break;
			}

			const uint32_t local_begin = pair.begin_query - slot_begin;
			const uint32_t local_end = pair.end_query - slot_begin;
			if (local_begin >= record.query_count || local_end >= record.query_count)
			{
				sample.valid = false;
				sample.invalid_reason = GpuTimingInvalidReason::FrameStateError;
				break;
			}

			uint64_t elapsed_ticks = 0u;
			double duration_ms = 0.0;
			if (!gpu_timing_delta_ticks(
					timestamps[local_begin],
					timestamps[local_end],
					64u,
					elapsed_ticks) ||
				!gpu_timing_ticks_to_milliseconds_from_frequency(
					elapsed_ticks,
					static_cast<double>(timestamp_frequency_hz),
					duration_ms))
			{
				sample.valid = false;
				sample.invalid_reason = GpuTimingInvalidReason::InvalidTimestampCalibration;
				break;
			}

			GpuTimingMetricSample& metric_sample = sample.metrics[metric_index];
			metric_sample.duration_ms = duration_ms;
			metric_sample.valid = true;
		}

		out_sample = sample;
		return DX12GpuTimingResolveResult::Ready;
	}

	DX12GpuTimingFencePollResult classify_dx12_gpu_timing_fence(
		uint64_t completed_value,
		uint64_t target_value)
	{
		if (completed_value == UINT64_MAX)
		{
			return DX12GpuTimingFencePollResult::DeviceRemoved;
		}
		if (target_value == 0u)
		{
			return DX12GpuTimingFencePollResult::InvalidTarget;
		}
		return completed_value >= target_value
			? DX12GpuTimingFencePollResult::Ready
			: DX12GpuTimingFencePollResult::Pending;
	}

	DX12GpuTimingSubmitResult classify_dx12_gpu_timing_submit(
		ID3D12CommandList* expected_command_list,
		ID3D12CommandList* const* executed_command_lists,
		uint32_t executed_command_list_count,
		const DX12FenceSignalResult& signal_result)
	{
		bool exact_command_list_executed = false;
		if (expected_command_list && executed_command_lists)
		{
			for (uint32_t index = 0u; index < executed_command_list_count; ++index)
			{
				if (executed_command_lists[index] == expected_command_list)
				{
					exact_command_list_executed = true;
					break;
				}
			}
		}
		if (!exact_command_list_executed)
		{
			return DX12GpuTimingSubmitResult::CommandListNotExecuted;
		}
		if (FAILED(signal_result.hresult))
		{
			return DX12GpuTimingSubmitResult::FenceSignalFailed;
		}
		if (signal_result.target_value == 0u)
		{
			return DX12GpuTimingSubmitResult::InvalidFenceTarget;
		}
		return DX12GpuTimingSubmitResult::Accepted;
	}

	DX12GpuTimingSafetyState advance_dx12_gpu_timing_safety_state(
		DX12GpuTimingSafetyState current_state,
		DX12GpuTimingSubmitResult submit_result)
	{
		if (current_state == DX12GpuTimingSafetyState::Quarantined)
		{
			return current_state;
		}

		switch (submit_result)
		{
		case DX12GpuTimingSubmitResult::Accepted:
		case DX12GpuTimingSubmitResult::CommandListNotExecuted:
			return DX12GpuTimingSafetyState::Operational;
		case DX12GpuTimingSubmitResult::FenceSignalFailed:
		case DX12GpuTimingSubmitResult::InvalidFenceTarget:
		default:
			return DX12GpuTimingSafetyState::Quarantined;
		}
	}

	uint32_t terminate_dx12_gpu_timing_tracking(
		GpuTimingFrameState& frame_state,
		std::array<uint64_t, kGpuTimingFrameRingDepth>& slot_frame_ids,
		std::array<uint64_t, kGpuTimingFrameRingDepth>& slot_fence_targets,
		std::array<bool, kGpuTimingFrameRingDepth>& slot_has_frame,
		GpuTimingInvalidReason reason,
		std::array<GpuFrameTimingSample, kGpuTimingFrameRingDepth>& out_samples)
	{
		out_samples = {};
		if (reason != GpuTimingInvalidReason::DeviceRemoved &&
			reason != GpuTimingInvalidReason::SubmissionFailed)
		{
			return 0u;
		}
		uint32_t sample_count = 0u;
		while (sample_count < kGpuTimingFrameRingDepth)
		{
			GpuTimingFrameRecord record{};
			GpuTimingPollResult poll_result = frame_state.poll_frame(record);
			if (poll_result == GpuTimingPollResult::Empty)
			{
				break;
			}
			if (poll_result == GpuTimingPollResult::Pending)
			{
				if (!frame_state.fail_committed_frame(
						record.frame_id,
						reason))
				{
					break;
				}
				poll_result = frame_state.poll_frame(record);
			}
			if (poll_result != GpuTimingPollResult::Ready)
			{
				break;
			}

			GpuFrameTimingSample sample{};
			sample.frame_id = record.frame_id;
			sample.valid = false;
			sample.invalid_reason = reason;
			out_samples[sample_count++] = sample;
			if (!frame_state.release_frame(record.frame_id))
			{
				--sample_count;
				break;
			}
		}
		// Terminal notification is authoritative: even malformed residual state
		// must not survive as a permanent Ready/Pending report.
		frame_state = {};
		slot_frame_ids = {};
		slot_fence_targets = {};
		slot_has_frame = {};
		return sample_count;
	}

	DX12GpuTimingTelemetry::~DX12GpuTimingTelemetry()
	{
		shutdown();
	}

	bool DX12GpuTimingTelemetry::init(
		ID3D12Device* device,
		ID3D12CommandQueue* graphics_queue,
		IDXGIAdapter1* adapter)
	{
		shutdown();
		if (!device || !graphics_queue || !adapter)
		{
			return false;
		}

		uint64_t timestamp_frequency_hz = 0u;
		if (FAILED(graphics_queue->GetTimestampFrequency(&timestamp_frequency_hz)) ||
			timestamp_frequency_hz == 0u)
		{
			return false;
		}

		D3D12_QUERY_HEAP_DESC query_heap_desc{};
		query_heap_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
		query_heap_desc.Count = kGpuTimingQueryCapacity;
		if (FAILED(device->CreateQueryHeap(
				&query_heap_desc,
				IID_PPV_ARGS(&m_query_heap))))
		{
			shutdown();
			return false;
		}
		dx12_set_debug_name(m_query_heap.Get(), "DX12 GPU Timing Query Heap");

		D3D12_HEAP_PROPERTIES heap_properties{};
		heap_properties.Type = D3D12_HEAP_TYPE_READBACK;
		heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heap_properties.CreationNodeMask = 1u;
		heap_properties.VisibleNodeMask = 1u;

		D3D12_RESOURCE_DESC readback_desc{};
		readback_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		readback_desc.Width = kReadbackSizeBytes;
		readback_desc.Height = 1u;
		readback_desc.DepthOrArraySize = 1u;
		readback_desc.MipLevels = 1u;
		readback_desc.Format = DXGI_FORMAT_UNKNOWN;
		readback_desc.SampleDesc.Count = 1u;
		readback_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		if (FAILED(device->CreateCommittedResource(
				&heap_properties,
				D3D12_HEAP_FLAG_NONE,
				&readback_desc,
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(&m_readback_resource))))
		{
			shutdown();
			return false;
		}
		dx12_set_debug_name(m_readback_resource.Get(), "DX12 GPU Timing Readback");

		D3D12_RANGE read_range{ 0u, static_cast<SIZE_T>(kReadbackSizeBytes) };
		void* mapped = nullptr;
		if (FAILED(m_readback_resource->Map(0u, &read_range, &mapped)) || !mapped)
		{
			shutdown();
			return false;
		}
		m_mapped_timestamps = static_cast<uint64_t*>(mapped);
		m_timestamp_frequency_hz = timestamp_frequency_hz;

		m_info = {};
		m_info.backend = Backend::DirectX12;
		m_info.timestamp_frequency_hz = static_cast<double>(timestamp_frequency_hz);
		m_info.timestamp_period_ns = 1000000000.0 / m_info.timestamp_frequency_hz;
		m_info.timestamp_valid_bits = 64u;
		m_info.query_capacity = kGpuTimingQueryCapacity;
		m_info.timing_scope = "main_command_buffer";

		DXGI_ADAPTER_DESC1 adapter_desc{};
		if (SUCCEEDED(adapter->GetDesc1(&adapter_desc)))
		{
			m_info.adapter_name = wide_to_utf8(adapter_desc.Description);
		}
		LARGE_INTEGER driver_version{};
		if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver_version)))
		{
			m_info.driver_version = format_driver_version(driver_version);
		}

		m_supported = true;
		return true;
	}

	void DX12GpuTimingTelemetry::shutdown()
	{
		if (m_mapped_timestamps && m_readback_resource)
		{
			D3D12_RANGE written_range{ 0u, 0u };
			m_readback_resource->Unmap(0u, &written_range);
		}
		m_mapped_timestamps = nullptr;
		m_timestamp_frequency_hz = 0u;
		m_readback_resource.Reset();
		m_query_heap.Reset();
		m_info = {};
		m_frame_state = {};
		m_completed_samples = {};
		m_slot_frame_ids = {};
		m_slot_fence_targets = {};
		m_slot_has_frame = {};
		m_completed_head = 0u;
		m_completed_count = 0u;
		m_available_slot = kGpuTimingFrameRingDepth;
		clear_active_frame();
		m_supported = false;
		m_device_removed = false;
		m_safety_state = DX12GpuTimingSafetyState::Operational;
	}

	bool DX12GpuTimingTelemetry::enqueue_completed_sample(const GpuFrameTimingSample& sample)
	{
		if (m_completed_count >= kGpuTimingFrameRingDepth)
		{
			return false;
		}
		const uint32_t tail =
			(m_completed_head + m_completed_count) % kGpuTimingFrameRingDepth;
		m_completed_samples[tail] = sample;
		++m_completed_count;
		return true;
	}

	bool DX12GpuTimingTelemetry::dequeue_completed_sample(GpuFrameTimingSample& out_sample)
	{
		if (m_completed_count == 0u)
		{
			return false;
		}
		out_sample = m_completed_samples[m_completed_head];
		m_completed_samples[m_completed_head] = {};
		m_completed_head = (m_completed_head + 1u) % kGpuTimingFrameRingDepth;
		--m_completed_count;
		return true;
	}

	void DX12GpuTimingTelemetry::notify_terminal_completion(
		GpuTimingInvalidReason reason)
	{
		if (reason != GpuTimingInvalidReason::DeviceRemoved &&
			reason != GpuTimingInvalidReason::SubmissionFailed)
		{
			return;
		}
		if (m_active_slot < kGpuTimingFrameRingDepth)
		{
			m_frame_state.abort_frame(
				m_active_frame_id,
				reason);
			clear_active_frame();
		}

		std::array<GpuFrameTimingSample, kGpuTimingFrameRingDepth> invalid_samples{};
		const uint32_t invalid_sample_count =
			terminate_dx12_gpu_timing_tracking(
				m_frame_state,
				m_slot_frame_ids,
				m_slot_fence_targets,
				m_slot_has_frame,
				reason,
				invalid_samples);
		for (uint32_t index = 0u; index < invalid_sample_count; ++index)
		{
			// A full CPU queue explicitly drops the newly terminal sample. The
			// shared frame state is still released, so poll cannot remain Pending.
			enqueue_completed_sample(invalid_samples[index]);
		}
		m_safety_state = DX12GpuTimingSafetyState::Quarantined;
		if (reason == GpuTimingInvalidReason::DeviceRemoved)
		{
			m_device_removed = true;
		}
		m_available_slot = kGpuTimingFrameRingDepth;
	}

	void DX12GpuTimingTelemetry::clear_active_frame()
	{
		m_active_slot = kGpuTimingFrameRingDepth;
		m_active_query_count = 0u;
		m_active_frame_id = 0u;
		m_active_fence_target = 0u;
		m_active_command_buffer = nullptr;
		m_active_native_command_list = nullptr;
		m_frame_ended = false;
		m_submission_bound = false;
	}

	bool DX12GpuTimingTelemetry::release_recycled_slot(
		uint32_t physical_slot,
		const GpuTimingFrameRecord& record,
		const GpuFrameTimingSample* completed_sample)
	{
		GpuTimingFrameRecord queued_record{};
		const GpuTimingPollResult poll_result = m_frame_state.poll_frame(queued_record);
		if ((poll_result != GpuTimingPollResult::Pending &&
				poll_result != GpuTimingPollResult::Ready) ||
			queued_record.frame_id != record.frame_id)
		{
			return false;
		}
		if (poll_result == GpuTimingPollResult::Pending &&
			!m_frame_state.mark_frame_ready(record.frame_id))
		{
			return false;
		}
		if (!m_frame_state.release_frame(record.frame_id))
		{
			return false;
		}

		bool queued = true;
		if (completed_sample)
		{
			queued = enqueue_completed_sample(*completed_sample);
		}
		m_slot_has_frame[physical_slot] = false;
		m_slot_frame_ids[physical_slot] = 0u;
		m_slot_fence_targets[physical_slot] = 0u;
		if (!m_device_removed &&
			m_safety_state == DX12GpuTimingSafetyState::Operational)
		{
			m_available_slot = physical_slot;
		}
		return queued;
	}

	bool DX12GpuTimingTelemetry::resolve_recycled_slot(
		uint32_t physical_slot,
		uint64_t completed_fence_value)
	{
		m_available_slot = kGpuTimingFrameRingDepth;
		if (!m_supported || physical_slot >= kGpuTimingFrameRingDepth)
		{
			return false;
		}
		if (completed_fence_value == UINT64_MAX)
		{
			notify_terminal_completion(GpuTimingInvalidReason::DeviceRemoved);
			return false;
		}
		if (m_device_removed ||
			m_safety_state == DX12GpuTimingSafetyState::Quarantined ||
			m_active_slot == physical_slot)
		{
			return false;
		}

		if (!m_slot_has_frame[physical_slot])
		{
			m_available_slot = physical_slot;
			return true;
		}

		GpuTimingFrameRecord record{};
		if (m_frame_state.poll_frame(record) == GpuTimingPollResult::Empty ||
			record.frame_id != m_slot_frame_ids[physical_slot] ||
			record.slot_index != physical_slot)
		{
			return false;
		}

		const DX12GpuTimingFencePollResult fence_result = classify_dx12_gpu_timing_fence(
			completed_fence_value,
			m_slot_fence_targets[physical_slot]);
		if (fence_result == DX12GpuTimingFencePollResult::Pending)
		{
			return false;
		}
		if (fence_result == DX12GpuTimingFencePollResult::DeviceRemoved)
		{
			notify_terminal_completion(GpuTimingInvalidReason::DeviceRemoved);
			return false;
		}
		if (fence_result == DX12GpuTimingFencePollResult::InvalidTarget)
		{
			GpuFrameTimingSample sample{};
			sample.frame_id = record.frame_id;
			sample.valid = false;
			sample.invalid_reason = GpuTimingInvalidReason::FrameStateError;
			if (!m_frame_state.fail_committed_frame(
					record.frame_id,
					GpuTimingInvalidReason::FrameStateError))
			{
				return false;
			}
			const GpuFrameTimingSample* queued_sample =
				m_completed_count < kGpuTimingFrameRingDepth ? &sample : nullptr;
			return release_recycled_slot(physical_slot, record, queued_sample);
		}

		if (m_completed_count >= kGpuTimingFrameRingDepth)
		{
			return release_recycled_slot(physical_slot, record, nullptr);
		}
		if (!m_mapped_timestamps || record.query_count == 0u ||
			record.query_count > kGpuTimingQueriesPerFrame)
		{
			GpuFrameTimingSample sample{};
			sample.frame_id = record.frame_id;
			sample.valid = false;
			sample.invalid_reason = GpuTimingInvalidReason::FrameStateError;
			if (!m_frame_state.fail_committed_frame(
					record.frame_id,
					GpuTimingInvalidReason::FrameStateError))
			{
				return false;
			}
			return release_recycled_slot(physical_slot, record, &sample);
		}

		std::array<uint64_t, kGpuTimingQueriesPerFrame> timestamps{};
		const uint64_t* slot_timestamps =
			m_mapped_timestamps + physical_slot * kGpuTimingQueriesPerFrame;
		std::copy_n(slot_timestamps, record.query_count, timestamps.data());

		GpuFrameTimingSample sample{};
		resolve_dx12_gpu_timing_record(
			record,
			timestamps,
			m_timestamp_frequency_hz,
			sample);
		return release_recycled_slot(physical_slot, record, &sample);
	}

	void DX12GpuTimingTelemetry::observe_submission(
		ID3D12CommandList* const* executed_command_lists,
		uint32_t executed_command_list_count,
		const DX12FenceSignalResult& signal_result)
	{
		if (!m_supported || m_device_removed ||
			m_safety_state == DX12GpuTimingSafetyState::Quarantined ||
			!m_frame_ended ||
			m_active_slot >= kGpuTimingFrameRingDepth || m_submission_bound)
		{
			return;
		}

		const DX12GpuTimingSubmitResult submit_result = classify_dx12_gpu_timing_submit(
			m_active_native_command_list,
			executed_command_lists,
			executed_command_list_count,
			signal_result);
		m_safety_state = advance_dx12_gpu_timing_safety_state(
			m_safety_state,
			submit_result);
		if (submit_result != DX12GpuTimingSubmitResult::Accepted)
		{
			m_frame_state.abort_frame(
				m_active_frame_id,
				GpuTimingInvalidReason::SubmissionFailed);
			clear_active_frame();
			if (m_safety_state == DX12GpuTimingSafetyState::Quarantined)
			{
				m_available_slot = kGpuTimingFrameRingDepth;
			}
			return;
		}

		m_active_fence_target = signal_result.target_value;
		m_submission_bound = true;
	}

	bool DX12GpuTimingTelemetry::begin_frame(CommandBuffer* cmd, uint64_t frame_id)
	{
		if (!m_supported || m_device_removed ||
			m_safety_state == DX12GpuTimingSafetyState::Quarantined ||
			!cmd || !cmd->get_native_handle() ||
			m_available_slot >= kGpuTimingFrameRingDepth ||
			m_active_slot < kGpuTimingFrameRingDepth ||
			m_slot_has_frame[m_available_slot])
		{
			return false;
		}

		uint32_t query_index = kGpuTimingInvalidQueryIndex;
		if (!m_frame_state.begin_frame(frame_id, m_available_slot, query_index))
		{
			return false;
		}

		auto* command_list =
			static_cast<ID3D12GraphicsCommandList*>(cmd->get_native_handle());
		command_list->EndQuery(m_query_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query_index);

		m_active_slot = m_available_slot;
		m_available_slot = kGpuTimingFrameRingDepth;
		m_active_query_count = 1u;
		m_active_frame_id = frame_id;
		m_active_command_buffer = cmd;
		m_active_native_command_list = command_list;
		m_frame_ended = false;
		m_submission_bound = false;
		return true;
	}

	bool DX12GpuTimingTelemetry::begin_scope(CommandBuffer* cmd, GpuTimingMetric metric)
	{
		if (!m_supported || m_device_removed ||
			m_safety_state == DX12GpuTimingSafetyState::Quarantined || !cmd ||
			cmd != m_active_command_buffer || m_frame_ended ||
			m_active_slot >= kGpuTimingFrameRingDepth)
		{
			return false;
		}

		uint32_t query_index = kGpuTimingInvalidQueryIndex;
		if (!m_frame_state.begin_scope(metric, query_index))
		{
			return false;
		}
		m_active_native_command_list->EndQuery(
			m_query_heap.Get(),
			D3D12_QUERY_TYPE_TIMESTAMP,
			query_index);
		m_active_query_count =
			query_index - m_active_slot * kGpuTimingQueriesPerFrame + 1u;
		return true;
	}

	void DX12GpuTimingTelemetry::end_scope(CommandBuffer* cmd, GpuTimingMetric metric)
	{
		if (!m_supported || m_device_removed ||
			m_safety_state == DX12GpuTimingSafetyState::Quarantined || !cmd ||
			cmd != m_active_command_buffer || m_frame_ended ||
			m_active_slot >= kGpuTimingFrameRingDepth)
		{
			return;
		}

		uint32_t query_index = kGpuTimingInvalidQueryIndex;
		if (!m_frame_state.end_scope(metric, query_index))
		{
			return;
		}
		m_active_native_command_list->EndQuery(
			m_query_heap.Get(),
			D3D12_QUERY_TYPE_TIMESTAMP,
			query_index);
		m_active_query_count =
			query_index - m_active_slot * kGpuTimingQueriesPerFrame + 1u;
	}

	void DX12GpuTimingTelemetry::end_frame(CommandBuffer* cmd, uint64_t frame_id)
	{
		if (!m_supported || m_device_removed ||
			m_safety_state == DX12GpuTimingSafetyState::Quarantined || !cmd ||
			cmd != m_active_command_buffer || m_frame_ended ||
			m_active_slot >= kGpuTimingFrameRingDepth ||
			m_active_frame_id != frame_id)
		{
			return;
		}

		uint32_t query_index = kGpuTimingInvalidQueryIndex;
		m_frame_state.end_frame(frame_id, query_index);
		if (query_index != kGpuTimingInvalidQueryIndex)
		{
			m_active_native_command_list->EndQuery(
				m_query_heap.Get(),
				D3D12_QUERY_TYPE_TIMESTAMP,
				query_index);
			m_active_query_count =
				query_index - m_active_slot * kGpuTimingQueriesPerFrame + 1u;

			DX12GpuTimingResolveRange range{};
			if (make_dx12_gpu_timing_resolve_range(
					m_active_slot,
					m_active_query_count,
					range))
			{
				m_active_native_command_list->ResolveQueryData(
					m_query_heap.Get(),
					D3D12_QUERY_TYPE_TIMESTAMP,
					range.start_query,
					range.query_count,
					m_readback_resource.Get(),
					range.destination_offset_bytes);
			}
		}

		m_active_command_buffer = nullptr;
		m_frame_ended = true;
	}

	void DX12GpuTimingTelemetry::commit_frame(uint64_t frame_id)
	{
		if (!m_supported || m_device_removed ||
			m_safety_state == DX12GpuTimingSafetyState::Quarantined ||
			!m_frame_ended ||
			m_active_slot >= kGpuTimingFrameRingDepth ||
			m_active_frame_id != frame_id)
		{
			return;
		}

		if (!m_submission_bound || m_active_fence_target == 0u ||
			!m_frame_state.commit_frame(frame_id))
		{
			m_frame_state.abort_frame(frame_id, GpuTimingInvalidReason::SubmissionFailed);
			clear_active_frame();
			return;
		}

		m_slot_frame_ids[m_active_slot] = frame_id;
		m_slot_fence_targets[m_active_slot] = m_active_fence_target;
		m_slot_has_frame[m_active_slot] = true;
		clear_active_frame();
	}

	void DX12GpuTimingTelemetry::abort_frame(
		uint64_t frame_id,
		GpuTimingInvalidReason reason)
	{
		if (!m_supported || m_active_slot >= kGpuTimingFrameRingDepth ||
			m_active_frame_id != frame_id)
		{
			return;
		}
		m_frame_state.abort_frame(frame_id, reason);
		clear_active_frame();
	}

	GpuTimingPollResult DX12GpuTimingTelemetry::poll_completed_frame(
		GpuFrameTimingSample& out_sample)
	{
		if (dequeue_completed_sample(out_sample))
		{
			return GpuTimingPollResult::Ready;
		}
		for (bool slot_has_frame : m_slot_has_frame)
		{
			if (slot_has_frame)
			{
				return GpuTimingPollResult::Pending;
			}
		}
		return GpuTimingPollResult::Empty;
	}

	GpuTimingTelemetryInfo DX12GpuTimingTelemetry::get_info() const
	{
		return m_info;
	}
}
