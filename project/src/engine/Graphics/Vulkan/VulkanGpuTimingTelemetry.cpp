#include "VulkanGpuTimingTelemetry.h"

#include "Graphics/CommandBuffer.h"

#include <cmath>
#include <string>
#include <vector>

namespace RHI
{
	VulkanGpuTimingResolveResult resolve_vulkan_gpu_timing_record(
		const GpuTimingFrameRecord& record,
		const std::array<VulkanGpuTimingRawQueryResult, kGpuTimingQueriesPerFrame>& raw_results,
		uint32_t timestamp_valid_bits,
		double timestamp_period_ns,
		GpuFrameTimingSample& out_sample)
	{
		if (timestamp_valid_bits == 0u || timestamp_valid_bits > 64u)
		{
			GpuFrameTimingSample unsupported_sample{};
			unsupported_sample.frame_id = record.frame_id;
			unsupported_sample.valid = false;
			unsupported_sample.invalid_reason = GpuTimingInvalidReason::BackendUnsupported;
			out_sample = unsupported_sample;
			return VulkanGpuTimingResolveResult::Unsupported;
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
			return VulkanGpuTimingResolveResult::Ready;
		}

		for (uint32_t query_index = 0u; query_index < record.query_count; ++query_index)
		{
			if (raw_results[query_index].availability == 0u)
			{
				return VulkanGpuTimingResolveResult::Pending;
			}
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
			return VulkanGpuTimingResolveResult::Ready;
		}

		const uint32_t slot_begin = record.slot_index * kGpuTimingQueriesPerFrame;
		const GpuTimingQueryPair& frame_pair =
			record.query_pairs[static_cast<uint32_t>(GpuTimingMetric::Frame)];
		if (!frame_pair.begun || !frame_pair.ended)
		{
			sample.valid = false;
			sample.invalid_reason = GpuTimingInvalidReason::IncompleteScope;
			out_sample = sample;
			return VulkanGpuTimingResolveResult::Ready;
		}
		if (frame_pair.begin_query < slot_begin ||
			frame_pair.end_query < slot_begin ||
			frame_pair.begin_query >= frame_pair.end_query)
		{
			sample.valid = false;
			sample.invalid_reason = GpuTimingInvalidReason::FrameStateError;
			out_sample = sample;
			return VulkanGpuTimingResolveResult::Ready;
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
			if (pair.begin_query < slot_begin || pair.end_query < slot_begin ||
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
					raw_results[local_begin].timestamp,
					raw_results[local_end].timestamp,
					timestamp_valid_bits,
					elapsed_ticks) ||
				!gpu_timing_ticks_to_milliseconds_from_period(
					elapsed_ticks,
					timestamp_period_ns,
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
		return VulkanGpuTimingResolveResult::Ready;
	}

	VulkanGpuTimingTelemetry::~VulkanGpuTimingTelemetry()
	{
		shutdown();
	}

	bool VulkanGpuTimingTelemetry::init(
		VkPhysicalDevice physical_device,
		VkDevice device,
		uint32_t graphics_queue_family,
		VkAllocationCallbacks* allocation_callbacks)
	{
		shutdown();
		if (physical_device == VK_NULL_HANDLE || device == VK_NULL_HANDLE)
		{
			return false;
		}

		uint32_t queue_family_count = 0u;
		vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);
		if (graphics_queue_family >= queue_family_count)
		{
			return false;
		}
		std::vector<VkQueueFamilyProperties> queue_family_properties(queue_family_count);
		vkGetPhysicalDeviceQueueFamilyProperties(
			physical_device,
			&queue_family_count,
			queue_family_properties.data());

		VkPhysicalDeviceDriverProperties driver_properties{
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
		VkPhysicalDeviceProperties2 physical_device_properties{
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
		physical_device_properties.pNext = &driver_properties;
		vkGetPhysicalDeviceProperties2(physical_device, &physical_device_properties);

		const uint32_t timestamp_valid_bits =
			queue_family_properties[graphics_queue_family].timestampValidBits;
		const double timestamp_period_ns =
			static_cast<double>(physical_device_properties.properties.limits.timestampPeriod);

		m_info = {};
		m_info.backend = Backend::Vulkan;
		m_info.adapter_name = physical_device_properties.properties.deviceName;
		m_info.timestamp_period_ns = timestamp_period_ns;
		m_info.timestamp_valid_bits = timestamp_valid_bits;
		m_info.query_capacity = kGpuTimingQueryCapacity;
		m_info.timing_scope = "main_command_buffer";
		if (std::isfinite(timestamp_period_ns) && timestamp_period_ns > 0.0)
		{
			m_info.timestamp_frequency_hz = 1000000000.0 / timestamp_period_ns;
		}

		m_info.driver_version = driver_properties.driverName;
		if (driver_properties.driverInfo[0] != '\0')
		{
			if (!m_info.driver_version.empty())
			{
				m_info.driver_version += " ";
			}
			m_info.driver_version += driver_properties.driverInfo;
		}
		if (m_info.driver_version.empty())
		{
			m_info.driver_version = std::to_string(physical_device_properties.properties.driverVersion);
		}

		if (timestamp_valid_bits == 0u || timestamp_valid_bits > 64u ||
			!std::isfinite(timestamp_period_ns) || timestamp_period_ns <= 0.0)
		{
			return false;
		}

		VkQueryPoolCreateInfo query_pool_info{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
		query_pool_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
		query_pool_info.queryCount = kGpuTimingQueryCapacity;
		VkQueryPool query_pool = VK_NULL_HANDLE;
		if (vkCreateQueryPool(device, &query_pool_info, allocation_callbacks, &query_pool) != VK_SUCCESS)
		{
			return false;
		}

		m_device = device;
		m_allocation_callbacks = allocation_callbacks;
		m_query_pool = query_pool;
		m_supported = true;
		return true;
	}

	void VulkanGpuTimingTelemetry::shutdown()
	{
		if (m_query_pool != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE)
		{
			vkDestroyQueryPool(m_device, m_query_pool, m_allocation_callbacks);
		}
		m_device = VK_NULL_HANDLE;
		m_allocation_callbacks = nullptr;
		m_query_pool = VK_NULL_HANDLE;
		m_frame_state = {};
		m_completed_samples = {};
		m_slot_frame_ids = {};
		m_slot_has_frame = {};
		m_completed_head = 0u;
		m_completed_count = 0u;
		m_available_slot = kGpuTimingFrameRingDepth;
		m_active_slot = kGpuTimingFrameRingDepth;
		m_active_frame_id = 0u;
		m_active_command_buffer = nullptr;
		m_supported = false;
	}

	bool VulkanGpuTimingTelemetry::enqueue_completed_sample(const GpuFrameTimingSample& sample)
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

	bool VulkanGpuTimingTelemetry::dequeue_completed_sample(GpuFrameTimingSample& out_sample)
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

	bool VulkanGpuTimingTelemetry::resolve_recycled_slot(
		uint32_t physical_slot,
		bool completion_observed)
	{
		m_available_slot = kGpuTimingFrameRingDepth;
		if (!m_supported || physical_slot >= kGpuTimingFrameRingDepth)
		{
			return false;
		}

		if (!m_slot_has_frame[physical_slot])
		{
			m_available_slot = physical_slot;
			return true;
		}
		if (!completion_observed)
		{
			return false;
		}

		GpuTimingFrameRecord record{};
		if (m_frame_state.poll_frame(record) != GpuTimingPollResult::Pending ||
			record.frame_id != m_slot_frame_ids[physical_slot] ||
			record.slot_index != physical_slot ||
			record.query_count == 0u ||
			record.query_count > kGpuTimingQueriesPerFrame)
		{
			return false;
		}
		if (m_completed_count >= kGpuTimingFrameRingDepth)
		{
			// The fixed CPU queue is deliberately bounded. Once the backend completion
			// primitive is observed, drop exactly this sample instead of pinning the
			// physical query slot for another ring cycle. PerfGate accounts for the
			// committed frame as unresolved, so coverage exposes sustained backpressure.
			if (!m_frame_state.mark_frame_ready(record.frame_id) ||
				!m_frame_state.release_frame(record.frame_id))
			{
				return false;
			}
			m_slot_has_frame[physical_slot] = false;
			m_slot_frame_ids[physical_slot] = 0u;
			m_available_slot = physical_slot;
			return true;
		}

		std::array<VulkanGpuTimingRawQueryResult, kGpuTimingQueriesPerFrame> raw_results{};
		const VkResult query_result = vkGetQueryPoolResults(
			m_device,
			m_query_pool,
			physical_slot * kGpuTimingQueriesPerFrame,
			record.query_count,
			record.query_count * sizeof(VulkanGpuTimingRawQueryResult),
			raw_results.data(),
			sizeof(VulkanGpuTimingRawQueryResult),
			VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

		GpuFrameTimingSample sample{};
		if (query_result == VK_SUCCESS || query_result == VK_NOT_READY)
		{
			const VulkanGpuTimingResolveResult resolve_result =
				resolve_vulkan_gpu_timing_record(
					record,
					raw_results,
					m_info.timestamp_valid_bits,
					m_info.timestamp_period_ns,
					sample);
			if (resolve_result == VulkanGpuTimingResolveResult::Pending)
			{
				return false;
			}
		}
		else
		{
			sample.frame_id = record.frame_id;
			sample.valid = false;
			sample.invalid_reason = query_result == VK_ERROR_DEVICE_LOST
				? GpuTimingInvalidReason::DeviceRemoved
				: GpuTimingInvalidReason::FrameStateError;
		}

		if (!m_frame_state.mark_frame_ready(record.frame_id) ||
			!m_frame_state.release_frame(record.frame_id))
		{
			return false;
		}
		if (!enqueue_completed_sample(sample))
		{
			return false;
		}

		m_slot_has_frame[physical_slot] = false;
		m_slot_frame_ids[physical_slot] = 0u;
		m_available_slot = physical_slot;
		return true;
	}

	bool VulkanGpuTimingTelemetry::begin_frame(CommandBuffer* cmd, uint64_t frame_id)
	{
		if (!m_supported || !cmd || !cmd->get_native_handle() ||
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

		VkCommandBuffer command_buffer =
			reinterpret_cast<VkCommandBuffer>(cmd->get_native_handle());
		vkCmdResetQueryPool(
			command_buffer,
			m_query_pool,
			m_available_slot * kGpuTimingQueriesPerFrame,
			kGpuTimingQueriesPerFrame);
		vkCmdWriteTimestamp(
			command_buffer,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			m_query_pool,
			query_index);

		m_active_slot = m_available_slot;
		m_available_slot = kGpuTimingFrameRingDepth;
		m_active_frame_id = frame_id;
		m_active_command_buffer = cmd;
		return true;
	}

	bool VulkanGpuTimingTelemetry::begin_scope(CommandBuffer* cmd, GpuTimingMetric metric)
	{
		if (!m_supported || !cmd || cmd != m_active_command_buffer ||
			m_active_slot >= kGpuTimingFrameRingDepth)
		{
			return false;
		}

		uint32_t query_index = kGpuTimingInvalidQueryIndex;
		if (!m_frame_state.begin_scope(metric, query_index))
		{
			return false;
		}
		vkCmdWriteTimestamp(
			reinterpret_cast<VkCommandBuffer>(cmd->get_native_handle()),
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			m_query_pool,
			query_index);
		return true;
	}

	void VulkanGpuTimingTelemetry::end_scope(CommandBuffer* cmd, GpuTimingMetric metric)
	{
		if (!m_supported || !cmd || cmd != m_active_command_buffer ||
			m_active_slot >= kGpuTimingFrameRingDepth)
		{
			return;
		}

		uint32_t query_index = kGpuTimingInvalidQueryIndex;
		if (!m_frame_state.end_scope(metric, query_index))
		{
			return;
		}
		vkCmdWriteTimestamp(
			reinterpret_cast<VkCommandBuffer>(cmd->get_native_handle()),
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			m_query_pool,
			query_index);
	}

	void VulkanGpuTimingTelemetry::end_frame(CommandBuffer* cmd, uint64_t frame_id)
	{
		if (!m_supported || !cmd || cmd != m_active_command_buffer ||
			m_active_slot >= kGpuTimingFrameRingDepth ||
			m_active_frame_id != frame_id)
		{
			return;
		}

		uint32_t query_index = kGpuTimingInvalidQueryIndex;
		m_frame_state.end_frame(frame_id, query_index);
		if (query_index != kGpuTimingInvalidQueryIndex)
		{
			vkCmdWriteTimestamp(
				reinterpret_cast<VkCommandBuffer>(cmd->get_native_handle()),
				VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
				m_query_pool,
				query_index);
		}
		m_active_command_buffer = nullptr;
	}

	void VulkanGpuTimingTelemetry::commit_frame(uint64_t frame_id)
	{
		if (!m_supported || m_active_command_buffer ||
			m_active_slot >= kGpuTimingFrameRingDepth ||
			m_active_frame_id != frame_id ||
			!m_frame_state.commit_frame(frame_id))
		{
			return;
		}

		m_slot_frame_ids[m_active_slot] = frame_id;
		m_slot_has_frame[m_active_slot] = true;
		m_active_slot = kGpuTimingFrameRingDepth;
		m_active_frame_id = 0u;
	}

	void VulkanGpuTimingTelemetry::abort_frame(
		uint64_t frame_id,
		GpuTimingInvalidReason reason)
	{
		if (!m_supported || m_active_slot >= kGpuTimingFrameRingDepth ||
			m_active_frame_id != frame_id)
		{
			return;
		}
		m_frame_state.abort_frame(frame_id, reason);
		m_active_slot = kGpuTimingFrameRingDepth;
		m_active_frame_id = 0u;
		m_active_command_buffer = nullptr;
	}

	GpuTimingPollResult VulkanGpuTimingTelemetry::poll_completed_frame(
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

	GpuTimingTelemetryInfo VulkanGpuTimingTelemetry::get_info() const
	{
		return m_info;
	}
}
