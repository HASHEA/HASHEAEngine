#pragma once
#include "Base/hplatform.h"
#include "Base/hcore.h"
#include "Base/ds/harray.hpp"
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
    class VulkanContext;
    class GraphicsContext
    {
    public:
        GraphicsContext() = default;
        virtual ~GraphicsContext() {}; 

        virtual auto init(void* config) -> HS_Result = 0;
        virtual auto shutdown() -> HS_Result = 0;
        static GraphicsContext* create();
    protected:
      
    };
};