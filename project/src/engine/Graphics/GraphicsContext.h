#pragma once
#include "Base/hplatform.h"
#include "Base/hcore.h"
#include "Base/ds/harray.hpp"
#include "RHIResource.h"
namespace RHI {
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
        AshEngine::Array<const char*>        addtionalExtensions{};
    };

    class GraphicsContext
    {
    public:
        GraphicsContext() = default;
        virtual ~GraphicsContext() {}; 

        virtual auto init(void* config) -> HS_Result = 0;
        virtual auto shutdown() -> HS_Result = 0;
        static GraphicsContext* create();
    public:
        //RHI Interfaces
        virtual auto map_buffer(const MapBufferParameters& params) -> void* = 0;
        virtual auto unmap_buffer(const MapBufferParameters& params) -> void = 0;
        virtual auto update_buffer_data(const MapBufferParameters& params, void* data) -> void = 0;
        virtual auto create_buffer(const BufferCreation& ci) -> std::shared_ptr<Buffer> = 0;
        virtual auto create_texture(const TextureCreation& ci) -> std::shared_ptr<Texture> = 0;
        virtual auto create_view(const TextureViewCreation& ci, std::shared_ptr<Texture> parentTexture) -> std::shared_ptr<TextureView> = 0;
        virtual auto create_sampler(const SamplerCreation& ci)->std::shared_ptr<Sampler> = 0;
  /*      virtual auto destroy_rhi_resource_Immediately(std::shared_ptr<RHIResource> resource) -> void = 0;
        virtual auto destroy_rhi_resource_Immediately(std::shared_ptr<RHIResource> resource) -> void = 0;
        virtual auto destroy_rhi_resource_Immediately(std::shared_ptr<RHIResource> resource) -> void = 0;*/
        virtual auto wait_idle() -> void = 0;
		virtual auto begin_frame() -> void = 0;
		virtual auto end_frame() -> void = 0;
    protected:
      
    };
};