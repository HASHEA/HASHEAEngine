#include "DX12Context.h"
#include "DX12CommandBuffer.h"
#include "DX12Buffer.h"
#include "DX12BufferView.h"
#include "DX12Texture.h"
#include "DX12TextureView.h"
#include "DX12Sampler.h"
#include "DX12Shader.h"
#include "DX12RenderPass.h"
#include "DX12Framebuffer.h"
#include "DX12RenderProgram.h"
#include "DX12StagingBufferPool.h"
#include "DX12GpuProfiler.h"
#include "Graphics/GpuProfilerRHI.h"
#include "Base/hlog.h"
#include "Base/hprofiler.h"
#include "Base/hassert.h"
#include "Base/hmemory.h"
#include "Graphics/TextureUploadUtils.h"
#include "Base/hthreading.h"
#include "D3D12MemAlloc.h"
#include <cstring>
#include <string>

namespace RHI
{
	DX12Context* DX12Context::s_instance = nullptr;

	namespace
	{
		constexpr bool should_log_d3d12_debug_message(D3D12_MESSAGE_SEVERITY severity)
		{
			return severity == D3D12_MESSAGE_SEVERITY_WARNING ||
				severity == D3D12_MESSAGE_SEVERITY_ERROR ||
				severity == D3D12_MESSAGE_SEVERITY_CORRUPTION;
		}

		constexpr const char* d3d12_message_severity_to_string(D3D12_MESSAGE_SEVERITY severity)
		{
			switch (severity)
			{
			case D3D12_MESSAGE_SEVERITY_CORRUPTION: return "CORRUPTION";
			case D3D12_MESSAGE_SEVERITY_ERROR: return "ERROR";
			case D3D12_MESSAGE_SEVERITY_WARNING: return "WARNING";
			case D3D12_MESSAGE_SEVERITY_INFO: return "INFO";
			case D3D12_MESSAGE_SEVERITY_MESSAGE: return "MESSAGE";
			default: return "UNKNOWN";
			}
		}

		constexpr const char* d3d12_message_category_to_string(D3D12_MESSAGE_CATEGORY category)
		{
			switch (category)
			{
			case D3D12_MESSAGE_CATEGORY_APPLICATION_DEFINED: return "APPLICATION_DEFINED";
			case D3D12_MESSAGE_CATEGORY_MISCELLANEOUS: return "MISCELLANEOUS";
			case D3D12_MESSAGE_CATEGORY_INITIALIZATION: return "INITIALIZATION";
			case D3D12_MESSAGE_CATEGORY_CLEANUP: return "CLEANUP";
			case D3D12_MESSAGE_CATEGORY_COMPILATION: return "COMPILATION";
			case D3D12_MESSAGE_CATEGORY_STATE_CREATION: return "STATE_CREATION";
			case D3D12_MESSAGE_CATEGORY_STATE_SETTING: return "STATE_SETTING";
			case D3D12_MESSAGE_CATEGORY_STATE_GETTING: return "STATE_GETTING";
			case D3D12_MESSAGE_CATEGORY_RESOURCE_MANIPULATION: return "RESOURCE_MANIPULATION";
			case D3D12_MESSAGE_CATEGORY_EXECUTION: return "EXECUTION";
			case D3D12_MESSAGE_CATEGORY_SHADER: return "SHADER";
			default: return "UNKNOWN";
			}
		}

		static std::string wide_to_utf8(const wchar_t* text)
		{
			if (!text || text[0] == L'\0')
			{
				return {};
			}

			const int required_size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
			if (required_size <= 1)
			{
				return {};
			}

			std::string result(static_cast<size_t>(required_size), '\0');
			WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), required_size, nullptr, nullptr);
			if (!result.empty() && result.back() == '\0')
			{
				result.pop_back();
			}
			return result;
		}

		constexpr bool should_log_dxgi_debug_message(DXGI_INFO_QUEUE_MESSAGE_SEVERITY severity)
		{
			return severity == DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING ||
				severity == DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR ||
				severity == DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION;
		}

		constexpr const char* dxgi_message_severity_to_string(DXGI_INFO_QUEUE_MESSAGE_SEVERITY severity)
		{
			switch (severity)
			{
			case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION: return "CORRUPTION";
			case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR: return "ERROR";
			case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING: return "WARNING";
			case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_INFO: return "INFO";
			case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_MESSAGE: return "MESSAGE";
			default: return "UNKNOWN";
			}
		}

		constexpr const char* dxgi_message_category_to_string(DXGI_INFO_QUEUE_MESSAGE_CATEGORY category)
		{
			switch (category)
			{
			case DXGI_INFO_QUEUE_MESSAGE_CATEGORY_MISCELLANEOUS: return "MISCELLANEOUS";
			case DXGI_INFO_QUEUE_MESSAGE_CATEGORY_INITIALIZATION: return "INITIALIZATION";
			case DXGI_INFO_QUEUE_MESSAGE_CATEGORY_CLEANUP: return "CLEANUP";
			case DXGI_INFO_QUEUE_MESSAGE_CATEGORY_COMPILATION: return "COMPILATION";
			case DXGI_INFO_QUEUE_MESSAGE_CATEGORY_STATE_CREATION: return "STATE_CREATION";
			case DXGI_INFO_QUEUE_MESSAGE_CATEGORY_STATE_SETTING: return "STATE_SETTING";
			case DXGI_INFO_QUEUE_MESSAGE_CATEGORY_STATE_GETTING: return "STATE_GETTING";
			case DXGI_INFO_QUEUE_MESSAGE_CATEGORY_RESOURCE_MANIPULATION: return "RESOURCE_MANIPULATION";
			case DXGI_INFO_QUEUE_MESSAGE_CATEGORY_EXECUTION: return "EXECUTION";
			case DXGI_INFO_QUEUE_MESSAGE_CATEGORY_SHADER: return "SHADER";
			default: return "UNKNOWN";
			}
		}

		const char* dxgi_producer_to_string(const DXGI_DEBUG_ID& producer)
		{
			if (IsEqualGUID(producer, DXGI_DEBUG_DXGI))
			{
				return "DXGI";
			}
			if (IsEqualGUID(producer, DXGI_DEBUG_DX))
			{
				return "DX";
			}
			if (IsEqualGUID(producer, DXGI_DEBUG_APP))
			{
				return "APP";
			}

			return "UNKNOWN";
		}

	}

	auto DX12Context::queue_buffer_upload(const std::shared_ptr<Buffer>& buffer, uint32_t offset, uint32_t size, void* data) -> bool
	{
		if (!buffer || !data || size == 0)
		{
			return false;
		}

		if (!AshEngine::is_in_render_thread() || !m_frameActive || m_frameResources.empty())
		{
			return _enqueue_pending_buffer_upload(buffer, offset, size, data);
		}

		return _record_buffer_upload(m_frameResources[m_currentFrame], buffer, offset, size, data);
	}

	auto DX12Context::queue_texture_upload(const std::shared_ptr<Texture>& texture, const void* data) -> bool
	{
		if (!texture || !data)
		{
			return false;
		}

		const TextureCreation& creation = texture->get_desciption();
		const AshDXGIFormatInfo& formatInfo = get_dxgi_format_info(creation.format);
		if (creation.eSampleCount != ASH_SAMPLE_COUNT_1_BIT ||
			DX12TextureFormat::is_depth_format(creation.format) ||
			formatInfo.dxgiFormat == DXGI_FORMAT_UNKNOWN ||
			formatInfo.uBytesPerBlock == 0 ||
			formatInfo.uWidthPerBlock == 0 ||
			formatInfo.uHeightPerBlock == 0)
		{
			return false;
		}

		if (!AshEngine::is_in_render_thread() || !m_frameActive || m_frameResources.empty())
		{
			return _enqueue_pending_texture_upload(texture, data);
		}

		return _record_texture_upload(m_frameResources[m_currentFrame], texture, data);
	}

	auto DX12Context::_enqueue_pending_buffer_upload(const std::shared_ptr<Buffer>& buffer, uint32_t offset, uint32_t size, const void* data) -> bool
	{
		if (!buffer || !data || size == 0)
		{
			return false;
		}

		PendingBufferUpload upload{};
		upload.buffer = buffer;
		upload.offset = offset;
		upload.data.resize(size);
		std::memcpy(upload.data.data(), data, size);
		{
			std::scoped_lock<std::mutex> lock(m_pendingUploadMutex);
			m_pendingBufferUploads.push_back(std::move(upload));
		}
		return true;
	}

	auto DX12Context::_enqueue_pending_texture_upload(const std::shared_ptr<Texture>& texture, const void* data) -> bool
	{
		if (!texture || !data)
		{
			return false;
		}

		const TextureCreation& creation = texture->get_desciption();
		const AshDXGIFormatInfo& formatInfo = get_dxgi_format_info(creation.format);
		TextureUploadFormatInfo uploadFormatInfo{};
		uploadFormatInfo.bytesPerBlock = formatInfo.uBytesPerBlock;
		uploadFormatInfo.widthPerBlock = formatInfo.uWidthPerBlock;
		uploadFormatInfo.heightPerBlock = formatInfo.uHeightPerBlock;

		std::vector<TextureUploadSubresource> subresources{};
		uint64_t totalBytes = 0;
		if (!build_tightly_packed_texture_upload_layout(creation, uploadFormatInfo, subresources, totalBytes) || totalBytes == 0 || totalBytes > UINT32_MAX)
		{
			return false;
		}

		PendingTextureUpload upload{};
		upload.texture = texture;
		upload.data.resize(static_cast<size_t>(totalBytes));
		std::memcpy(upload.data.data(), data, static_cast<size_t>(totalBytes));
		{
			std::scoped_lock<std::mutex> lock(m_pendingUploadMutex);
			m_pendingTextureUploads.push_back(std::move(upload));
		}
		return true;
	}

	auto DX12Context::_ensure_upload_command_buffer_recording(DX12FrameResources& frameResources) -> bool
	{
		if (!frameResources.uploadCmdAllocator || !frameResources.uploadCmdBuffer)
		{
			return false;
		}

		if (!frameResources.uploadCommandsPending)
		{
			frameResources.uploadCmdBuffer->set_allocator(frameResources.uploadCmdAllocator->get_allocator());
			frameResources.uploadCmdBuffer->begin_record();
			if (frameResources.uploadCmdBuffer->has_error())
			{
				HLogError(
					"DX12Context: failed to begin upload command buffer recording: {}",
					frameResources.uploadCmdBuffer->get_last_error().empty() ? "<unknown>" : frameResources.uploadCmdBuffer->get_last_error());
				return false;
			}
			frameResources.uploadCommandsPending = true;
		}

		return true;
	}

	auto DX12Context::_record_buffer_upload(DX12FrameResources& frameResources, const std::shared_ptr<Buffer>& buffer, uint32_t offset, uint32_t size, const void* data) -> bool
	{
		if (!buffer || !data || size == 0)
		{
			return false;
		}
		if (!_ensure_upload_command_buffer_recording(frameResources))
		{
			return false;
		}

		return frameResources.uploadCmdBuffer->cmd_update_sub_resource(buffer, offset, size, const_cast<void*>(data));
	}

	auto DX12Context::_record_texture_upload(DX12FrameResources& frameResources, const std::shared_ptr<Texture>& texture, const void* data) -> bool
	{
		if (!texture || !data)
		{
			return false;
		}
		if (!_ensure_upload_command_buffer_recording(frameResources))
		{
			return false;
		}

		return frameResources.uploadCmdBuffer->cmd_update_texture_sub_resource(texture, data);
	}

	auto DX12Context::_flush_pending_buffer_uploads(DX12FrameResources& frameResources) -> bool
	{
		std::vector<PendingBufferUpload> uploads{};
		{
			std::scoped_lock<std::mutex> lock(m_pendingUploadMutex);
			uploads.swap(m_pendingBufferUploads);
		}

		for (const PendingBufferUpload& upload : uploads)
		{
			if (!_record_buffer_upload(frameResources, upload.buffer, upload.offset, static_cast<uint32_t>(upload.data.size()), upload.data.data()))
			{
				return false;
			}
		}

		return true;
	}

	auto DX12Context::_flush_pending_texture_uploads(DX12FrameResources& frameResources) -> bool
	{
		std::vector<PendingTextureUpload> uploads{};
		{
			std::scoped_lock<std::mutex> lock(m_pendingUploadMutex);
			uploads.swap(m_pendingTextureUploads);
		}

		for (const PendingTextureUpload& upload : uploads)
		{
			if (!_record_texture_upload(frameResources, upload.texture, upload.data.data()))
			{
				return false;
			}
		}

		return true;
	}

	auto DX12Context::_finalize_upload_command_buffer(DX12FrameResources& frameResources) -> bool
	{
		if (!frameResources.uploadCommandsPending || !frameResources.uploadCmdBuffer)
		{
			return true;
		}
		if (frameResources.uploadCmdBuffer->get_state() == AshCommandBufferState::ASH_Recording)
		{
			frameResources.uploadCmdBuffer->end_record();
			if (frameResources.uploadCmdBuffer->has_error())
			{
				HLogError(
					"DX12Context: failed to end upload command buffer recording: {}",
					frameResources.uploadCmdBuffer->get_last_error().empty() ? "<unknown>" : frameResources.uploadCmdBuffer->get_last_error());
				return false;
			}
		}
		return !frameResources.uploadCmdBuffer->has_error();
	}

	auto DX12Context::init(void* config) -> bool
	{
		const auto& cfg = *reinterpret_cast<const GraphicsContextInitConfig*>(config);
		m_numThread = cfg.num_threads;
#if defined(ASH_DEBUG)
		m_enableDebugLayer = cfg.dx12Validation.enableDebugLayer;
		m_enableGpuValidation = cfg.dx12Validation.enableGpuValidation;
#else
		m_enableDebugLayer = false;
		m_enableGpuValidation = false;
#endif

		_enable_debug_layer();

		if (!_create_factory()) return false;
		if (!_select_adapter()) return false;
		if (!_create_device()) return false;
		_setup_debug_message_logging();
		if (!_create_command_queues()) return false;
		if (!_create_memory_allocator()) return false;
		if (!_create_descriptor_heaps()) return false;
		if (!_create_frame_resources(cfg.num_threads)) return false;

		// Create staging buffer
		m_stagingBuffer = Ash_New<DX12StagingBufferPool>();
		m_stagingBuffer->init(m_device.Get(), m_d3d12maAllocator);

		// 安装 Tracy GPU profiler。绑到主图形 queue。
		{
			auto* profiler = new DX12GpuProfiler(m_device.Get(), m_graphicsQueue.get_queue(), "DX12");
			gpu_profiler_install(profiler);
		}

		HLogInfo("DX12Context: Initialization complete.");
		return true;
	}

	auto DX12Context::shutdown() -> bool
	{
		wait_idle();

		// 卸载 Tracy GPU profiler，必须在 device/queue 销毁之前。
		if (auto* profiler = gpu_profiler_get())
		{
			gpu_profiler_install(nullptr);
			delete profiler;
		}

		// Process all deletion queues
		for (auto& q : m_delayedDeletionQueues)
			q.flush();

		if (m_stagingBuffer)
		{
			m_stagingBuffer->shutdown();
			Ash_Delete(nullptr, m_stagingBuffer);
			m_stagingBuffer = nullptr;
		}

		for (auto* cb : m_commandBuffers)
		{
			Ash_Delete(nullptr, cb);
		}
		m_commandBuffers.clear();

		_shutdown_frame_resources();
		m_descriptorHeaps.shutdown();

		if (m_d3d12maAllocator)
		{
			m_d3d12maAllocator->Release();
			m_d3d12maAllocator = nullptr;
		}

		m_graphicsQueue.shutdown();
		m_computeQueue.shutdown();
		m_copyQueue.shutdown();

		m_shaderPool.clear();
		m_samplerCache.clear();
		_drain_d3d12_debug_messages("shutdown");
		_drain_dxgi_debug_messages("shutdown");
		_flush_suppressed_debug_messages();
		_shutdown_debug_message_logging();

		m_device.Reset();
		m_adapter.Reset();
		m_factory.Reset();

		s_instance = nullptr;
		HLogInfo("DX12Context: Shutdown complete.");
		return true;
	}

	auto DX12Context::destroy() -> void
	{
		DX12Context* self = this;
		Ash_Delete(nullptr, self);
	}

	auto DX12Context::_enable_debug_layer() -> bool
	{
		if (!m_enableDebugLayer)
		{
			return true;
		}

		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();
			HLogInfo("DX12Context: Debug layer enabled.");

			if (m_enableGpuValidation)
			{
				ComPtr<ID3D12Debug1> debugController1;
				if (SUCCEEDED(debugController.As(&debugController1)))
				{
					debugController1->SetEnableGPUBasedValidation(TRUE);
					HLogInfo("DX12Context: GPU-based validation enabled.");
				}
			}
		}
		return true;
	}

	auto DX12Context::_setup_debug_message_logging() -> void
	{
#if defined(ASH_DEBUG)
		if (!m_enableDebugLayer)
		{
			return;
		}

		if (!m_d3d12InfoQueue && m_device)
		{
			if (FAILED(m_device.As(&m_d3d12InfoQueue)))
			{
				HLogWarning("DX12Context: Failed to query ID3D12InfoQueue for debug message routing.");
			}
		}

		if (m_d3d12InfoQueue)
		{
			// Route validation failures to logs instead of debugger breaks so smoke tests can shut down cleanly.
			m_d3d12InfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE);
			m_d3d12InfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
			_drain_d3d12_debug_messages("startup");
#if defined(__ID3D12InfoQueue1_INTERFACE_DEFINED__)
			if (!m_d3d12MessageCallbackRegistered && SUCCEEDED(m_d3d12InfoQueue.As(&m_d3d12InfoQueue1)) && m_d3d12InfoQueue1)
			{
				HRESULT hr = m_d3d12InfoQueue1->RegisterMessageCallback(
					&DX12Context::_d3d12_debug_message_callback,
					D3D12_MESSAGE_CALLBACK_FLAG_NONE,
					this,
					&m_d3d12MessageCallbackCookie);
				if (SUCCEEDED(hr))
				{
					m_d3d12MessageCallbackRegistered = true;
					HLogInfo("DX12Context: D3D12 debug-layer messages are routed to engine log.");
				}
				else
				{
					HLogWarning("DX12Context: Failed to register D3D12 debug callback. HRESULT: 0x{:08X}", static_cast<uint32_t>(hr));
				}
			}
#endif
		}

		if (!m_dxgiInfoQueue)
		{
			const HRESULT hr = DXGIGetDebugInterface1(0, IID_PPV_ARGS(&m_dxgiInfoQueue));
			if (FAILED(hr))
			{
				HLogWarning("DX12Context: Failed to query IDXGIInfoQueue for debug message routing. HRESULT: 0x{:08X}", static_cast<uint32_t>(hr));
			}
		}

		if (m_dxgiInfoQueue)
		{
			// Keep DXGI validation behavior aligned with D3D12: collect messages, do not interrupt execution.
			m_dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_DXGI, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, FALSE);
			m_dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_DXGI, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, FALSE);
			_drain_dxgi_debug_messages("startup");
		}
#endif
	}

	auto DX12Context::_shutdown_debug_message_logging() -> void
	{
#if defined(ASH_DEBUG)
#if defined(__ID3D12InfoQueue1_INTERFACE_DEFINED__)
		if (m_d3d12MessageCallbackRegistered && m_d3d12InfoQueue1)
		{
			const HRESULT hr = m_d3d12InfoQueue1->UnregisterMessageCallback(m_d3d12MessageCallbackCookie);
			if (FAILED(hr))
			{
				HLogWarning("DX12Context: Failed to unregister D3D12 debug callback. HRESULT: 0x{:08X}", static_cast<uint32_t>(hr));
			}
		}
		m_d3d12InfoQueue1.Reset();
		m_d3d12MessageCallbackCookie = 0;
#endif
		m_d3d12MessageCallbackRegistered = false;
		m_d3d12InfoQueue.Reset();
		m_dxgiInfoQueue.Reset();
#endif
	}

	auto DX12Context::_report_d3d12_debug_message(
		const char* phase,
		D3D12_MESSAGE_CATEGORY category,
		D3D12_MESSAGE_SEVERITY severity,
		D3D12_MESSAGE_ID id,
		LPCSTR description) -> void
	{
#if defined(ASH_DEBUG)
		if (!should_log_d3d12_debug_message(severity))
		{
			return;
		}

		const char* safePhase = phase ? phase : "runtime";
		const char* safeDescription = description ? description : "";
		const char* source = "D3D12";
		const char* severityText = d3d12_message_severity_to_string(severity);
		const char* categoryText = d3d12_message_category_to_string(category);
		const bool isError = severity != D3D12_MESSAGE_SEVERITY_WARNING;

		uint64_t occurrenceCount = 0;
		{
			std::lock_guard<std::mutex> lock(m_debugMessageMutex);
			std::string key = std::string(source) + "|" + severityText + "|" + categoryText + "|" + std::to_string(static_cast<int>(id)) + "|" + safeDescription;
			auto& entry = m_suppressedDebugMessages[key];
			if (entry.count == 0)
			{
				entry.source = source;
				entry.severity = severityText;
				entry.category = categoryText;
				entry.messageId = static_cast<int32_t>(id);
				entry.description = safeDescription;
				entry.error = isError;
			}

			++entry.count;
			occurrenceCount = entry.count;
		}

		if (occurrenceCount > 1)
		{
			return;
		}

		if (isError)
		{
			HLogError("[DX12 Validation][{}][{}] {} : Category={} MessageID={} Message={}",
				source,
				safePhase,
				severityText,
				categoryText,
				static_cast<int>(id),
				safeDescription);
			return;
		}

		HLogWarning("[DX12 Validation][{}][{}] {} : Category={} MessageID={} Message={}",
			source,
			safePhase,
			severityText,
			categoryText,
			static_cast<int>(id),
			safeDescription);
#else
		(void)phase;
		(void)category;
		(void)severity;
		(void)id;
		(void)description;
#endif
	}

	auto DX12Context::_report_dxgi_debug_message(const char* phase, const DXGI_INFO_QUEUE_MESSAGE& message) -> void
	{
#if defined(ASH_DEBUG)
		if (!should_log_dxgi_debug_message(message.Severity))
		{
			return;
		}

		const char* safePhase = phase ? phase : "runtime";
		const char* safeDescription = message.pDescription ? message.pDescription : "";
		const char* source = dxgi_producer_to_string(message.Producer);
		const char* severityText = dxgi_message_severity_to_string(message.Severity);
		const char* categoryText = dxgi_message_category_to_string(message.Category);
		const bool isError = message.Severity != DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING;

		uint64_t occurrenceCount = 0;
		{
			std::lock_guard<std::mutex> lock(m_debugMessageMutex);
			std::string key = std::string(source) + "|" + severityText + "|" + categoryText + "|" + std::to_string(static_cast<int>(message.ID)) + "|" + safeDescription;
			auto& entry = m_suppressedDebugMessages[key];
			if (entry.count == 0)
			{
				entry.source = source;
				entry.severity = severityText;
				entry.category = categoryText;
				entry.messageId = static_cast<int32_t>(message.ID);
				entry.description = safeDescription;
				entry.error = isError;
			}

			++entry.count;
			occurrenceCount = entry.count;
		}

		if (occurrenceCount > 1)
		{
			return;
		}

		if (isError)
		{
			HLogError("[DX12 Validation][{}][{}] {} : Category={} MessageID={} Message={}",
				source,
				safePhase,
				severityText,
				categoryText,
				static_cast<int>(message.ID),
				safeDescription);
			return;
		}

		HLogWarning("[DX12 Validation][{}][{}] {} : Category={} MessageID={} Message={}",
			source,
			safePhase,
			severityText,
			categoryText,
			static_cast<int>(message.ID),
			safeDescription);
#else
		(void)phase;
		(void)message;
#endif
	}

	auto DX12Context::_flush_suppressed_debug_messages() -> void
	{
#if defined(ASH_DEBUG)
		std::unordered_map<std::string, SuppressedDebugMessage> suppressedMessages{};
		{
			std::lock_guard<std::mutex> lock(m_debugMessageMutex);
			suppressedMessages.swap(m_suppressedDebugMessages);
		}

		for (const auto& [_, entry] : suppressedMessages)
		{
			if (entry.count <= 1)
			{
				continue;
			}

			const uint64_t suppressedCount = entry.count - 1;
			if (entry.error)
			{
				HLogError("[DX12 Validation][{}] Suppressed {} repeated messages : {} Category={} MessageID={} Message={}",
					entry.source,
					suppressedCount,
					entry.severity,
					entry.category,
					entry.messageId,
					entry.description);
				continue;
			}

			HLogWarning("[DX12 Validation][{}] Suppressed {} repeated messages : {} Category={} MessageID={} Message={}",
				entry.source,
				suppressedCount,
				entry.severity,
				entry.category,
				entry.messageId,
				entry.description);
		}
#endif
	}

	auto DX12Context::_drain_d3d12_debug_messages(const char* phase) -> void
	{
#if defined(ASH_DEBUG)
		if (!m_enableDebugLayer || !m_d3d12InfoQueue || m_d3d12MessageCallbackRegistered)
		{
			return;
		}

		const UINT64 messageCount = m_d3d12InfoQueue->GetNumStoredMessages();
		for (UINT64 messageIndex = 0; messageIndex < messageCount; ++messageIndex)
		{
			SIZE_T messageBytes = 0;
			if (FAILED(m_d3d12InfoQueue->GetMessage(messageIndex, nullptr, &messageBytes)) || messageBytes == 0)
			{
				continue;
			}

			std::vector<uint8_t> messageStorage(messageBytes);
			auto* message = reinterpret_cast<D3D12_MESSAGE*>(messageStorage.data());
			if (FAILED(m_d3d12InfoQueue->GetMessage(messageIndex, message, &messageBytes)))
			{
				continue;
			}

			_report_d3d12_debug_message(phase, message->Category, message->Severity, message->ID, message->pDescription);
		}

		if (messageCount > 0)
		{
			m_d3d12InfoQueue->ClearStoredMessages();
		}
#else
		(void)phase;
#endif
	}

	auto DX12Context::_drain_dxgi_debug_messages(const char* phase) -> void
	{
#if defined(ASH_DEBUG)
		if (!m_enableDebugLayer || !m_dxgiInfoQueue)
		{
			return;
		}

		const UINT64 messageCount = m_dxgiInfoQueue->GetNumStoredMessages(DXGI_DEBUG_DXGI);
		for (UINT64 messageIndex = 0; messageIndex < messageCount; ++messageIndex)
		{
			SIZE_T messageBytes = 0;
			if (FAILED(m_dxgiInfoQueue->GetMessage(DXGI_DEBUG_DXGI, messageIndex, nullptr, &messageBytes)) || messageBytes == 0)
			{
				continue;
			}

			std::vector<uint8_t> messageStorage(messageBytes);
			auto* message = reinterpret_cast<DXGI_INFO_QUEUE_MESSAGE*>(messageStorage.data());
			if (FAILED(m_dxgiInfoQueue->GetMessage(DXGI_DEBUG_DXGI, messageIndex, message, &messageBytes)))
			{
				continue;
			}

			_report_dxgi_debug_message(phase, *message);
		}

		if (messageCount > 0)
		{
			m_dxgiInfoQueue->ClearStoredMessages(DXGI_DEBUG_DXGI);
		}
#else
		(void)phase;
#endif
	}

	void __stdcall DX12Context::_d3d12_debug_message_callback(
		D3D12_MESSAGE_CATEGORY category,
		D3D12_MESSAGE_SEVERITY severity,
		D3D12_MESSAGE_ID id,
		LPCSTR description,
		void* context)
	{
		auto* dx12Context = static_cast<DX12Context*>(context);
		if (!dx12Context || !dx12Context->m_enableDebugLayer)
		{
			return;
		}

		dx12Context->_report_d3d12_debug_message("callback", category, severity, id, description);
	}

	auto DX12Context::_create_factory() -> bool
	{
		UINT dxgiFactoryFlags = 0;
		if (m_enableDebugLayer)
		{
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
		HRESULT hr = CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_factory));
		if (FAILED(hr))
		{
			HLogError("DX12Context: Failed to create DXGI factory. HRESULT: 0x{:08X}", (uint32_t)hr);
			return false;
		}
		return true;
	}

	auto DX12Context::_select_adapter() -> bool
	{
		ComPtr<IDXGIAdapter1> adapter1;
		// Try to get high performance adapter first
		for (UINT i = 0; m_factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter1)) != DXGI_ERROR_NOT_FOUND; ++i)
		{
			DXGI_ADAPTER_DESC1 desc;
			adapter1->GetDesc1(&desc);

			// Skip software adapters
			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
				continue;

			// Check for D3D12 support
			if (SUCCEEDED(D3D12CreateDevice(adapter1.Get(), D3D_FEATURE_LEVEL_12_1, __uuidof(ID3D12Device), nullptr)))
			{
				adapter1.As(&m_adapter);
				DXGI_ADAPTER_DESC1 adapterDesc;
				m_adapter->GetDesc1(&adapterDesc);
				HLogInfo("DX12Context: Selected adapter: {}", wide_to_utf8(adapterDesc.Description));
				return true;
			}
		}

		HLogError("DX12Context: No suitable D3D12 adapter found.");
		return false;
	}

	auto DX12Context::_create_device() -> bool
	{
		HRESULT hr = D3D12CreateDevice(m_adapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&m_device));
		if (FAILED(hr))
		{
			HLogError("DX12Context: Failed to create D3D12 device. HRESULT: 0x{:08X}", (uint32_t)hr);
			return false;
		}

		// Check ray tracing support
		D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
		if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))))
		{
			if (options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0)
			{
				HLogInfo("DX12Context: Ray tracing supported (Tier {}).", (int)options5.RaytracingTier);
			}
		}

		// Check shader model support
		D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = { D3D_SHADER_MODEL_6_6 };
		if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel))))
		{
			m_highestShaderModel = shaderModel.HighestShaderModel;
			HLogInfo("DX12Context: Shader Model 6.{} supported.", (int)(shaderModel.HighestShaderModel & 0xF));
		}

#ifdef ASH_DEBUG
		if (SUCCEEDED(m_device.As(&m_d3d12InfoQueue)) && m_d3d12InfoQueue)
		{
			// Apply before full message routing is installed; validation failures are reported through the log path.
			m_d3d12InfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE);
			m_d3d12InfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
		}
#endif

		return true;
	}

	auto DX12Context::_create_command_queues() -> bool
	{
		if (!m_graphicsQueue.init(m_device.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT)) return false;
		if (!m_computeQueue.init(m_device.Get(), D3D12_COMMAND_LIST_TYPE_COMPUTE)) return false;
		if (!m_copyQueue.init(m_device.Get(), D3D12_COMMAND_LIST_TYPE_COPY)) return false;
		return true;
	}

	auto DX12Context::_create_memory_allocator() -> bool
	{
		D3D12MA::ALLOCATOR_DESC allocatorDesc = {};
		allocatorDesc.pDevice = m_device.Get();
		allocatorDesc.pAdapter = m_adapter.Get();
		allocatorDesc.Flags = D3D12MA::ALLOCATOR_FLAG_NONE;

		HRESULT hr = D3D12MA::CreateAllocator(&allocatorDesc, &m_d3d12maAllocator);
		if (FAILED(hr))
		{
			HLogError("DX12Context: Failed to create D3D12MA allocator. HRESULT: 0x{:08X}", (uint32_t)hr);
			return false;
		}
		return true;
	}

	auto DX12Context::_create_descriptor_heaps() -> bool
	{
		return m_descriptorHeaps.init(m_device.Get());
	}

	auto DX12Context::_create_frame_resources(uint16_t numThread) -> bool
	{
		m_frameResources.resize(k_dx12_max_frames);
		for (uint32_t i = 0; i < k_dx12_max_frames; ++i)
		{
			auto& fr = m_frameResources[i];
			fr.cmdAllocator = Ash_New<DX12CommandPool>();
			fr.cmdAllocator->init(m_device.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT);
			fr.uploadCmdAllocator = Ash_New<DX12CommandPool>();
			fr.uploadCmdAllocator->init(m_device.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT);
			fr.uploadCmdBuffer = Ash_New<DX12CommandBuffer>();
			fr.uploadCmdBuffer->init(m_device.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT);

			fr.fence = Ash_New<DX12Fence>();
			fr.fence->init(m_device.Get(), 0);
			fr.fenceValue = 0;
			fr.uploadCommandsPending = false;
		}

		// Create command buffers (one per thread)
		uint32_t numCmdBuffers = static_cast<uint32_t>(numThread) * k_dx12_max_frames;
		m_commandBuffers.resize(numCmdBuffers);
		for (uint32_t i = 0; i < numCmdBuffers; ++i)
		{
			m_commandBuffers[i] = Ash_New<DX12CommandBuffer>();
			m_commandBuffers[i]->init(m_device.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT);
		}

		m_currentFrame = 0;
		m_previousFrame = 0;
		m_absoluteFrame = 0;

		return true;
	}

	auto DX12Context::_shutdown_frame_resources() -> bool
	{
		for (auto& fr : m_frameResources)
		{
			if (fr.uploadCmdBuffer)
			{
				fr.uploadCmdBuffer->shutdown();
				Ash_Delete(nullptr, fr.uploadCmdBuffer);
				fr.uploadCmdBuffer = nullptr;
			}
			if (fr.uploadCmdAllocator)
			{
				fr.uploadCmdAllocator->shutdown();
				Ash_Delete(nullptr, fr.uploadCmdAllocator);
				fr.uploadCmdAllocator = nullptr;
			}
			if (fr.cmdAllocator)
			{
				fr.cmdAllocator->shutdown();
				Ash_Delete(nullptr, fr.cmdAllocator);
				fr.cmdAllocator = nullptr;
			}
			if (fr.fence)
			{
				fr.fence->shutdown();
				Ash_Delete(nullptr, fr.fence);
				fr.fence = nullptr;
			}
		}
		m_frameResources.clear();
		return true;
	}

	// ──────────────────────────────────────────────────────────────
	// Frame Management
	// ──────────────────────────────────────────────────────────────
	auto DX12Context::begin_frame() -> void
	{
		ASH_PROFILE_SCOPE_NC("DX12Context::begin_frame", AshEngine::Profile::Color::RHI);
		m_currentFrame = static_cast<uint32_t>(m_absoluteFrame % k_dx12_max_frames);

		auto& fr = m_frameResources[m_currentFrame];

		// Wait for this frame's previous work to complete
		{
			ASH_PROFILE_SCOPE_NC("DX12Context::WaitFrameFence", AshEngine::Profile::Color::RHI);
			fr.fence->wait();
		}

		// Reset command allocator for this frame
		fr.cmdAllocator->reset();
		fr.uploadCmdAllocator->reset();
		if (fr.uploadCmdBuffer)
		{
			fr.uploadCmdBuffer->set_state(ASH_Idle);
		}
		fr.uploadCommandsPending = false;

		// Reset shader-visible descriptor heaps
		m_descriptorHeaps.begin_frame();

		// Process delayed deletions
		m_delayedDeletionQueues[m_currentFrame].flush();

		if (m_stagingBuffer)
		{
			m_stagingBuffer->begin_frame(m_absoluteFrame);
		}

		m_frameActive = true;
		if (!_flush_pending_buffer_uploads(fr))
		{
			HLogError("DX12Context: Failed to flush pending buffer uploads for frame {}.", m_currentFrame);
		}
		if (!_flush_pending_texture_uploads(fr))
		{
			HLogError("DX12Context: Failed to flush pending texture uploads for frame {}.", m_currentFrame);
		}
	}

	auto DX12Context::end_frame() -> void
	{
		ASH_PROFILE_SCOPE_NC("DX12Context::end_frame", AshEngine::Profile::Color::RHI);
		auto& fr = m_frameResources[m_currentFrame];
		if (fr.uploadCommandsPending)
		{
			std::vector<ID3D12CommandList*> uploadCmdLists{};
			if (_finalize_upload_command_buffer(fr) && fr.uploadCmdBuffer && !fr.uploadCmdBuffer->has_error())
			{
				uploadCmdLists.push_back(fr.uploadCmdBuffer->get_command_list());
				{
					ASH_PROFILE_SCOPE_NC("DX12Context::ExecuteUploadCommandLists", AshEngine::Profile::Color::RHI);
					ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(uploadCmdLists.size()));
					m_graphicsQueue.execute_command_lists(static_cast<UINT>(uploadCmdLists.size()), uploadCmdLists.data());
				}
				fr.uploadCmdBuffer->set_state(ASH_Submitted);
				fr.fence->signal(m_graphicsQueue.get_queue());
			}
			else
			{
				HLogError("DX12Context: skipping errored upload command buffer submit in end_frame.");
			}
			fr.uploadCommandsPending = false;
		}
		_drain_d3d12_debug_messages("frame-end");

		// Tracy GPU collect。DX12 实现内部用 fence + readback 拿 timestamps，
		// 不需要正在录制的 cmd list，每帧调用一次即可。
		if (auto* profiler = gpu_profiler_get())
		{
			profiler->collect(nullptr);
		}

		m_frameActive = false;
		m_previousFrame = m_currentFrame;
		m_absoluteFrame++;
	}

	auto DX12Context::wait_idle() -> void
	{
		// Signal and wait on all queues
		for (auto& fr : m_frameResources)
		{
			fr.fence->signal(m_graphicsQueue.get_queue());
			fr.fence->wait();
		}
	}

	auto DX12Context::get_command_buffer(uint32_t threadIndx) -> CommandBuffer*
	{
		uint32_t index = m_currentFrame * m_numThread + threadIndx;
		H_ASSERT(index < m_commandBuffers.size());
		auto* cb = m_commandBuffers[index];
		cb->set_allocator(m_frameResources[m_currentFrame].cmdAllocator->get_allocator());
		return cb;
	}

	auto DX12Context::get_secondary_command_buffer(uint32_t threadIndx) -> CommandBuffer*
	{
		// DX12 uses bundles for secondary command buffers
		// For now, return a regular command buffer
		return get_command_buffer(threadIndx);
	}

	auto DX12Context::submit(const SubmitInfo& info) -> void
	{
		std::vector<ID3D12CommandList*> cmdLists;
		auto& fr = m_frameResources[m_currentFrame];
		if (fr.uploadCommandsPending)
		{
			if (_finalize_upload_command_buffer(fr) && fr.uploadCmdBuffer && !fr.uploadCmdBuffer->has_error())
			{
				cmdLists.push_back(fr.uploadCmdBuffer->get_command_list());
				fr.uploadCmdBuffer->set_state(ASH_Submitted);
			}
			else
			{
				HLogError("DX12Context: skipping errored upload command buffer submit.");
			}
			fr.uploadCommandsPending = false;
		}

		uint32_t submitCount = info.cmdCount;
		if (!info.cmds && submitCount > 0)
		{
			HLogError("DX12Context: submit received a null command buffer array with count {}.", submitCount);
			submitCount = 0;
		}

		for (uint32_t i = 0; i < submitCount; ++i)
		{
			auto* dx12Cb = static_cast<DX12CommandBuffer*>(&info.cmds[i]);
			if (!dx12Cb || dx12Cb->has_error() || dx12Cb->get_state() != AshCommandBufferState::ASH_Ended)
			{
				HLogError(
					"DX12Context: skipping invalid command buffer submit at index {} (state={}, error={}).",
					i,
					dx12Cb ? static_cast<uint32_t>(dx12Cb->get_state()) : UINT32_MAX,
					(dx12Cb && dx12Cb->has_error()) ? dx12Cb->get_last_error() : "<none>");
				continue;
			}
			cmdLists.push_back(dx12Cb->get_command_list());
			dx12Cb->set_state(ASH_Submitted);
		}

		if (!cmdLists.empty())
		{
			m_graphicsQueue.execute_command_lists(static_cast<UINT>(cmdLists.size()), cmdLists.data());
		}

		// Signal fence for this frame
		fr.fence->signal(m_graphicsQueue.get_queue());
		_drain_d3d12_debug_messages("submit");
	}

	auto DX12Context::submit_immediately(const SubmitInfo& info) -> void
	{
		submit(info);
		// Immediately wait
		auto& fr = m_frameResources[m_currentFrame];
		fr.fence->wait();
	}

	// ──────────────────────────────────────────────────────────────
	// Resource Creation (forward to DX12 implementations)
	// ──────────────────────────────────────────────────────────────
	auto DX12Context::create_shader(const ShaderCreation& ci) -> std::shared_ptr<Shader>
	{
		uint64_t hash = get_shader_hash(ci);
		auto it = m_shaderPool.find(hash);
		if (it != m_shaderPool.end())
			return it->second;

		auto shader = std::make_shared<DX12Shader>();
		if (!shader->init(ci))
			return nullptr;

		m_shaderPool.emplace(hash, std::static_pointer_cast<Shader>(shader));
		return shader;
	}

	auto DX12Context::create_buffer(const BufferCreation& ci) -> std::shared_ptr<Buffer>
	{
		auto buffer = std::make_shared<DX12Buffer>();
		if (!buffer->init(ci, m_device.Get(), m_d3d12maAllocator, &m_descriptorHeaps))
			return nullptr;

		if (ci.initial_data && ci.access_type == AshResourceAccessType::ASH_RESOURCE_ACCESS_GPU_ONLY)
		{
			if (!queue_buffer_upload(buffer, 0, ci.size, ci.initial_data))
			{
				HLogError("DX12Context: Failed to enqueue initial data upload for buffer '{}'.", ci.name ? ci.name : "UnnamedBuffer");
				return nullptr;
			}
		}

		return buffer;
	}

	auto DX12Context::create_buffer_view(const BufferViewCreation& ci, std::shared_ptr<Buffer> parentBuffer) -> std::shared_ptr<BufferView>
	{
		auto view = std::make_shared<DX12BufferView>();
		if (!view->init(ci, std::static_pointer_cast<DX12Buffer>(parentBuffer), m_device.Get(), &m_descriptorHeaps))
			return nullptr;
		return view;
	}

	auto DX12Context::create_texture(const TextureCreation& ci) -> std::shared_ptr<Texture>
	{
		auto texture = std::make_shared<DX12Texture>();
		if (!texture->init(ci, m_device.Get(), m_d3d12maAllocator, &m_descriptorHeaps))
			return nullptr;
		if (ci.initial_data)
		{
			if (!queue_texture_upload(texture, ci.initial_data))
			{
				HLogError("DX12Context: Failed to enqueue initial data upload for texture '{}'.", ci.name ? ci.name : "UnnamedTexture");
				return nullptr;
			}
		}
		return texture;
	}

	auto DX12Context::create_texture_view(const TextureViewCreation& ci, std::shared_ptr<Texture> parentTexture) -> std::shared_ptr<TextureView>
	{
		auto view = std::make_shared<DX12TextureView>();
		if (!view->init(ci, std::static_pointer_cast<DX12Texture>(parentTexture), m_device.Get(), &m_descriptorHeaps))
			return nullptr;
		return view;
	}

	auto DX12Context::create_render_pass(const RenderPassCreation& ci) -> std::shared_ptr<RenderPass>
	{
		auto rp = std::make_shared<DX12RenderPass>();
		if (!rp->init(ci))
			return nullptr;
		return rp;
	}

	auto DX12Context::create_framebuffer(const FramebufferCreation& ci) -> std::shared_ptr<Framebuffer>
	{
		auto fb = std::make_shared<DX12Framebuffer>();
		if (!fb->init(ci, m_device.Get(), &m_descriptorHeaps))
			return nullptr;
		return fb;
	}

	auto DX12Context::create_graphics_render_program(const GraphicProgramCreateDesc& desc) -> std::unique_ptr<IGraphicsRenderProgram>
	{
		auto program = std::make_unique<DX12GraphicsRenderProgram>();
		if (!program->create(desc))
			return nullptr;
		return program;
	}

	auto DX12Context::create_compute_render_program(const ComputeProgramCreateDesc& desc) -> std::unique_ptr<IComputeRenderProgram>
	{
		auto program = std::make_unique<DX12ComputeRenderProgram>();
		if (!program->create(desc))
			return nullptr;
		return program;
	}

	auto DX12Context::create_sampler(const SamplerCreation& ci) -> std::shared_ptr<Sampler>
	{
		auto sampler = std::make_shared<DX12Sampler>();
		if (!sampler->init(ci, m_device.Get(), &m_descriptorHeaps))
			return nullptr;
		return sampler;
	}

	auto DX12Context::get_sampler(const AshSamplerState& ss) -> std::shared_ptr<Sampler>
	{
		const uint32_t sampler_index = static_cast<uint32_t>(ss);
		if (sampler_index < m_samplerCache.size() && m_samplerCache[sampler_index] != nullptr)
			return m_samplerCache[sampler_index];

		SamplerCreation sc{};
		// Default sampler for ASH_SAMPLER_STATE_DEFAULT
		sc.minFilter = ASH_FILTER_LINEAR;
		sc.magFilter = ASH_FILTER_LINEAR;
		sc.mipFilter = ASH_FILTER_LINEAR;
		sc.address_mode_u = ASH_SAMPLER_ADDRESS_MODE_REPEAT;
		sc.address_mode_v = ASH_SAMPLER_ADDRESS_MODE_REPEAT;
		sc.address_mode_w = ASH_SAMPLER_ADDRESS_MODE_REPEAT;
		sc.enable_anisotropy = true;
		sc.max_anisotropy = 16.0f;
		sc.max_lod = 16.0f;
		sc.name = "default sampler";

		std::shared_ptr<Sampler> sampler = create_sampler(sc);
		if (!sampler)
			return nullptr;

		if (sampler_index >= m_samplerCache.size())
			m_samplerCache.set_size(sampler_index + 1);
		m_samplerCache[sampler_index] = sampler;
		return sampler;
	}
}
