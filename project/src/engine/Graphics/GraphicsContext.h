#pragma once
#include "Base/hplatform.h"
#include "Base/hcore.h"
#include "Base/ds/harray.hpp"
#include "RHIBackend.h"
#include "RHIResource.h"
#include "Sampler.h"
#include <cstdint>
#include <memory>
namespace RHI {
    struct VulkanValidationConfig
    {
#ifdef ASH_DEBUG
        bool enableValidation = true;
        bool enableGpuAssisted = false;
        // Sync validation放大 CPU 帧时间 5-10x，默认关闭，按需 opt-in。
        bool enableSynchronizationValidation = false;
        bool breakOnValidationError = true;
#else
        bool enableValidation = false;
        bool enableGpuAssisted = false;
        bool enableSynchronizationValidation = false;
        bool breakOnValidationError = false;
#endif
    };

    struct DX12ValidationConfig
    {
#ifdef ASH_DEBUG
        bool enableDebugLayer = true;
        bool enableGpuValidation = true;
#else
        bool enableDebugLayer = false;
        bool enableGpuValidation = false;
#endif
    };

    struct BufferViewCreation;
    struct RenderPassCreation;
    struct FramebufferCreation;
    struct GraphicProgramCreateDesc;
    struct ComputeProgramCreateDesc;
    class RenderPass;
    class Framebuffer;
    class IGraphicsRenderProgram;
    class IComputeRenderProgram;
    class IGpuTimingTelemetry;
    struct GpuDescriptorPoolCreation {

        uint16_t                             samplers = 256;
        uint16_t                             combinedImageSamplers = 256;
        uint16_t                             sampledImage = 256;
        uint16_t                             storageImage = 256;
        uint16_t                             uniformTexelBuffers = 256;
        uint16_t                             storageTexelBuffers = 256;
        uint16_t                             uniformBuffer = 256;
        uint16_t                             storageBuffer = 256;
        uint16_t                             uniformBufferDynamic = 256;
        uint16_t                             storageBufferDynamic = 256;
        uint16_t                             inputAttachments = 256;

    };
    struct GraphicsContextInitConfig
    {
        GpuDescriptorPoolCreation descriptorPoolCreation{};
        void* window = nullptr;
        uint16_t                             width = 1;
        uint16_t                             height = 1;
        uint16_t                             num_threads = 1;
        uint16_t							 queryCount = 32;
        bool                                 enableGpuTimingTelemetry = false;
        Backend                              backend = Backend::Default;
        VulkanValidationConfig               vulkanValidation{};
        DX12ValidationConfig                 dx12Validation{};
        AshEngine::Array<const char*>        addtionalExtensions{};
    };

    struct SubmitInfo
    {
        CommandBuffer* cmds = nullptr;
        uint32_t cmdCount = 0;
    };

    struct RenderMemoryStats
    {
        bool supported = false;
        uint64_t gpu_allocator_current_bytes = 0;
        uint64_t gpu_allocator_peak_bytes = 0;
        uint64_t gpu_allocator_shutdown_live_bytes = 0;
    };

    class GraphicsContext
    {
    public:
        GraphicsContext() = default;
        virtual ~GraphicsContext() {}; 

        virtual auto init(void* config) -> bool = 0;
        virtual auto shutdown() -> bool = 0;
        virtual auto destroy() -> void = 0;
        virtual auto get_render_memory_stats() const -> RenderMemoryStats
        {
            return {};
        }
        // Context-owned, non-owning view. The pointer becomes invalid after shutdown()
        // or when this GraphicsContext is destroyed; disabled telemetry returns nullptr.
        virtual auto get_gpu_timing_telemetry() -> IGpuTimingTelemetry*
        {
            return nullptr;
        }
        static GraphicsContext* create(Backend backend);
    public:
        //RHI Device Interfaces
        virtual auto create_shader(const ShaderCreation& ci) -> std::shared_ptr<Shader> = 0;
        virtual auto create_buffer(const BufferCreation& ci) -> std::shared_ptr<Buffer> = 0;
		virtual auto create_buffer_view(const BufferViewCreation& ci, std::shared_ptr<Buffer> parentBuffer) -> std::shared_ptr<BufferView> = 0;
        virtual auto create_texture(const TextureCreation& ci) -> std::shared_ptr<Texture> = 0;
        virtual auto create_texture_view(const TextureViewCreation& ci, std::shared_ptr<Texture> parentTexture) -> std::shared_ptr<TextureView> = 0;
        virtual auto create_render_pass(const RenderPassCreation& ci) -> std::shared_ptr<RenderPass> = 0;
        virtual auto create_framebuffer(const FramebufferCreation& ci) -> std::shared_ptr<Framebuffer> = 0;
        virtual auto create_graphics_render_program(const GraphicProgramCreateDesc& desc) -> std::unique_ptr<IGraphicsRenderProgram> = 0;
        virtual auto create_compute_render_program(const ComputeProgramCreateDesc& desc) -> std::unique_ptr<IComputeRenderProgram> = 0;
        virtual auto create_sampler(const SamplerCreation& ci) -> std::shared_ptr<Sampler> = 0;
        virtual auto get_sampler(const AshSamplerState& ss)->std::shared_ptr<Sampler> = 0;
        virtual auto wait_idle() -> void = 0;
		virtual auto wait_for_frame_completion(uint64_t timeout_nanoseconds) -> bool = 0;
		virtual auto begin_frame() -> void = 0;
		virtual auto end_frame(bool has_acquired_swapchain_image = true) -> void = 0;
        virtual auto get_command_buffer(uint32_t threadIndx) -> CommandBuffer* = 0;
        virtual auto get_secondary_command_buffer(uint32_t threadIndx) -> CommandBuffer* = 0;
        virtual auto submit(const SubmitInfo& info) -> void = 0;
        virtual auto submit_immediately(const SubmitInfo& info) -> void = 0;
    };
};
