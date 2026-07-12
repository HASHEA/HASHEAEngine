#pragma once
#include "DX12Wrapper.h"
#include "DX12Helper.hpp"
#include "DX12Fence.h"
#include "DX12Queue.h"
#include "DX12CommandPool.h"
#include "DX12DescriptorHeap.h"
#include "DX12ResourceTracker.h"
#include "Base/hcore.h"
#include "Base/hstring.h"
#include "Base/hbit.hpp"
#include "Graphics/GraphicsContext.h"
#include "Graphics/Sampler.h"
#include "Base/ds/harray.hpp"
#include "Base/ds/hhash_map.hpp"
#include "Base/hcommandQueue.hpp"
#include <vector>
#include <memory>
#include <array>
#include <mutex>
#include <string>
#include <unordered_map>

struct D3D12MA_Allocator; // Forward declare from D3D12MA
namespace D3D12MA { class Allocator; }

using namespace AshEngine;
namespace RHI
{
	constexpr uint32_t k_dx12_max_frames = 3;

	class DX12CommandBuffer;
	class DX12StagingBufferPool;
	class DX12Swapchain;

	struct DX12FrameResources
	{
		DX12CommandPool* cmdAllocator = nullptr;
		DX12CommandPool* uploadCmdAllocator = nullptr;
		DX12CommandBuffer* uploadCmdBuffer = nullptr;
		DX12Fence* fence = nullptr;
		uint64_t fenceValue = 0;
		bool uploadCommandsPending = false;
	};

	class DX12Context : public GraphicsContext
	{
	public:
		auto init(void* config) -> bool override;
		auto shutdown() -> bool override;
		auto destroy() -> void override;
		auto get_render_memory_stats() const -> RenderMemoryStats override
		{
			return {};
		}
		DX12Context() { s_instance = this; }
		~DX12Context() {}

	public:
		inline static auto get() { return s_instance; }
		inline auto get_device() const { return m_device.Get(); }
		inline auto get_graphics_queue() -> DX12Queue& { return m_graphicsQueue; }
		inline auto get_compute_queue() -> DX12Queue& { return m_computeQueue; }
		inline auto get_copy_queue() -> DX12Queue& { return m_copyQueue; }
		inline auto get_descriptor_heaps() -> DX12DescriptorHeapManager& { return m_descriptorHeaps; }
		inline auto get_resource_tracker() -> DX12ResourceTracker& { return m_resourceTracker; }
		inline auto get_d3d12ma_allocator() -> D3D12MA::Allocator* { return m_d3d12maAllocator; }
		inline auto get_factory() const { return m_factory.Get(); }
		inline auto get_current_frame() const { return m_currentFrame; }
		inline auto get_absolute_frame_count() const { return m_absoluteFrame; }
		inline auto get_staging_buffer() -> DX12StagingBufferPool* { return m_stagingBuffer; }
		inline auto get_highest_shader_model() const { return m_highestShaderModel; }

		enum class IndirectSignatureKind : uint32_t
		{
			Draw = 0,
			DrawIndexed,
			Dispatch,
			Count
		};
		// Fixed argument-only signatures (no root constant changes), lazily created, cached per device.
		auto get_indirect_command_signature(IndirectSignatureKind kind) -> ID3D12CommandSignature*;

		inline auto get_current_frame_deletion_queue() -> DelayCommandQueue&
		{
			return m_delayedDeletionQueues[m_currentFrame];
		}

		auto queue_buffer_upload(const std::shared_ptr<Buffer>& buffer, uint32_t offset, uint32_t size, void* data) -> bool;
		auto queue_texture_upload(const std::shared_ptr<Texture>& texture, const void* data) -> bool;

	public:
		// RHI Device Interfaces
		auto create_shader(const ShaderCreation& ci) -> std::shared_ptr<Shader> override;
		auto create_buffer(const BufferCreation& ci) -> std::shared_ptr<Buffer> override;
		auto create_buffer_view(const BufferViewCreation& ci, std::shared_ptr<Buffer> parentBuffer) -> std::shared_ptr<BufferView> override;
		auto create_texture(const TextureCreation& ci) -> std::shared_ptr<Texture> override;
		auto create_texture_view(const TextureViewCreation& ci, std::shared_ptr<Texture> parentTexture) -> std::shared_ptr<TextureView> override;
		auto create_render_pass(const RenderPassCreation& ci) -> std::shared_ptr<RenderPass> override;
		auto create_framebuffer(const FramebufferCreation& ci) -> std::shared_ptr<Framebuffer> override;
		auto create_graphics_render_program(const GraphicProgramCreateDesc& desc) -> std::unique_ptr<IGraphicsRenderProgram> override;
		auto create_compute_render_program(const ComputeProgramCreateDesc& desc) -> std::unique_ptr<IComputeRenderProgram> override;
		auto create_sampler(const SamplerCreation& ci) -> std::shared_ptr<Sampler> override;
		auto get_sampler(const AshSamplerState& ss) -> std::shared_ptr<Sampler> override;
		auto wait_idle() -> void override;
		auto wait_for_frame_completion(uint64_t timeout_nanoseconds) -> bool override;
		auto begin_frame() -> void override;
		auto end_frame(bool has_acquired_swapchain_image = true) -> void override;
		auto get_command_buffer(uint32_t threadIndx) -> CommandBuffer* override;
		auto get_secondary_command_buffer(uint32_t threadIndx) -> CommandBuffer* override;
		auto submit(const SubmitInfo& info) -> void override;
		auto submit_immediately(const SubmitInfo& info) -> void override;

	private:
		friend class DX12Swapchain;

		auto _create_factory() -> bool;
		auto _select_adapter() -> bool;
		auto _create_device() -> bool;
		auto _create_command_queues() -> bool;
		auto _create_memory_allocator() -> bool;
		auto _create_descriptor_heaps() -> bool;
		auto _create_frame_resources(uint16_t numThread) -> bool;
		auto _enable_debug_layer() -> bool;
		auto _setup_debug_message_logging() -> void;
		auto _shutdown_debug_message_logging() -> void;
		auto _drain_d3d12_debug_messages(const char* phase) -> void;
		auto _drain_dxgi_debug_messages(const char* phase) -> void;
		auto _report_d3d12_debug_message(
			const char* phase,
			D3D12_MESSAGE_CATEGORY category,
			D3D12_MESSAGE_SEVERITY severity,
			D3D12_MESSAGE_ID id,
			LPCSTR description) -> void;
		auto _report_dxgi_debug_message(const char* phase, const DXGI_INFO_QUEUE_MESSAGE& message) -> void;
		auto _flush_suppressed_debug_messages() -> void;
		auto _shutdown_frame_resources() -> bool;
		auto _enqueue_pending_buffer_upload(const std::shared_ptr<Buffer>& buffer, uint32_t offset, uint32_t size, const void* data) -> bool;
		auto _enqueue_pending_texture_upload(const std::shared_ptr<Texture>& texture, const void* data) -> bool;
		auto _ensure_upload_command_buffer_recording(DX12FrameResources& frameResources) -> bool;
		auto _record_buffer_upload(DX12FrameResources& frameResources, const std::shared_ptr<Buffer>& buffer, uint32_t offset, uint32_t size, const void* data) -> bool;
		auto _record_texture_upload(DX12FrameResources& frameResources, const std::shared_ptr<Texture>& texture, const void* data) -> bool;
		auto _flush_pending_buffer_uploads(DX12FrameResources& frameResources) -> bool;
		auto _flush_pending_texture_uploads(DX12FrameResources& frameResources) -> bool;
		auto _finalize_upload_command_buffer(DX12FrameResources& frameResources) -> bool;
		auto create_sampler_uncached(const SamplerCreation& ci) -> std::shared_ptr<Sampler>;
		static void __stdcall _d3d12_debug_message_callback(
			D3D12_MESSAGE_CATEGORY category,
			D3D12_MESSAGE_SEVERITY severity,
			D3D12_MESSAGE_ID id,
			LPCSTR description,
			void* context);

	private:
		struct PendingBufferUpload
		{
			std::shared_ptr<Buffer> buffer = nullptr;
			uint32_t offset = 0;
			std::vector<uint8_t> data{};
		};

		struct PendingTextureUpload
		{
			std::shared_ptr<Texture> texture = nullptr;
			std::vector<uint8_t> data{};
		};

		bool m_enableDebugLayer = false;
		bool m_enableGpuValidation = false;
		ComPtr<IDXGIFactory6> m_factory;
		ComPtr<IDXGIAdapter4> m_adapter;
		ComPtr<ID3D12Device5> m_device;

		DX12Queue m_graphicsQueue;
		DX12Queue m_computeQueue;
		DX12Queue m_copyQueue;

		D3D12MA::Allocator* m_d3d12maAllocator = nullptr;

		DX12DescriptorHeapManager m_descriptorHeaps;
		DX12ResourceTracker m_resourceTracker;

		// Per-frame resources
		std::vector<DX12FrameResources> m_frameResources;
		DelayCommandQueue m_delayedDeletionQueues[k_dx12_max_frames];

		// Command buffers
		std::vector<DX12CommandBuffer*> m_commandBuffers;

		// Sampler cache / deduplication
		Array<std::shared_ptr<Sampler>> m_samplerCache;
		std::unordered_map<uint64_t, std::weak_ptr<Sampler>> m_samplerDedupPool{};
		mutable std::mutex m_samplerDedupMutex{};
		std::unordered_map<uint64_t, std::shared_ptr<Shader>> m_shaderPool;

		// Staging buffer
		DX12StagingBufferPool* m_stagingBuffer = nullptr;
		std::vector<PendingBufferUpload> m_pendingBufferUploads{};
		std::vector<PendingTextureUpload> m_pendingTextureUploads{};
		mutable std::mutex m_pendingUploadMutex{};

		// Indirect command signatures (lazy, fixed set)
		ComPtr<ID3D12CommandSignature> m_indirectSignatures[static_cast<uint32_t>(IndirectSignatureKind::Count)];
		std::mutex m_indirectSignatureMutex{};

		// Frame tracking
		uint32_t m_currentFrame = 0;
		uint32_t m_previousFrame = 0;
		uint64_t m_absoluteFrame = 0;
		uint16_t m_numThread = 0;
		D3D_SHADER_MODEL m_highestShaderModel = D3D_SHADER_MODEL_6_0;
		bool m_frameActive = false;

#if defined(ASH_DEBUG)
		struct SuppressedDebugMessage
		{
			std::string source{};
			std::string severity{};
			std::string category{};
			int32_t messageId = 0;
			std::string description{};
			bool error = false;
			uint64_t count = 0;
		};

		ComPtr<ID3D12InfoQueue> m_d3d12InfoQueue;
#if defined(__ID3D12InfoQueue1_INTERFACE_DEFINED__)
		ComPtr<ID3D12InfoQueue1> m_d3d12InfoQueue1;
		DWORD m_d3d12MessageCallbackCookie = 0;
#endif
		bool m_d3d12MessageCallbackRegistered = false;
		ComPtr<IDXGIInfoQueue> m_dxgiInfoQueue;
		std::mutex m_debugMessageMutex{};
		std::unordered_map<std::string, SuppressedDebugMessage> m_suppressedDebugMessages{};
#endif

	private:
		static DX12Context* s_instance;
	};
}
