#pragma once
#include "Base/hplatform.h"
#include <memory>
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
    class VulkanContext;
    class GraphicsContext
    {
    public:
        GraphicsContext() {};
        ~GraphicsContext() {}; 
    protected:
      
    };





    GraphicsContext* g_pGraphicsContext = nullptr;

#ifdef HASHEA_VULKAN
    VulkanContext* g_pVulkanContext = nullptr;
#endif // HASHEA_VULKAN

};