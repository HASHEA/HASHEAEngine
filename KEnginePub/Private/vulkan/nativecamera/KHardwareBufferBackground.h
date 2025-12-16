//
// Created by Ant on 2024/8/16.
//
#pragma once

#if defined(__ANDROID__)

#include "KNativeCameraUtility.h"
#include "KCameraHandler.h"
#include "KCameraBackground.h"


class KHardwareCameraBackground : public KCameraBackground
{
public:
    static constexpr int VERTICES = 4;
    struct Vertex
    {
        // x: x-axis position in the screen where the vertex of the image will
        // be rendered. Normalized between -1 (left) and 1 (right).
        float x;
        // pos_y: y-axis position in the screen where the vertex of the image will
        // be rendered. Normalized between -1 (top) and 1 (bottom).
        float y;
        // u: x-axis position of the texture to be renderer. Normalized between
        // 0 (left) and 1 (right).
        float u;
        // v: y-axis position of the texture to be renderer. Normalized between
        // 0 (top) and 1 (bottom).
        float v;
    };

    KHardwareCameraBackground() = default;
    virtual ~KHardwareCameraBackground() override = default;

public:
    virtual BOOL Init() override;
    virtual BOOL Render(const KCameraRenderParam& pParam) override;
    virtual void UnInit() override;
    virtual void Recreate() override;

private:
    BOOL _RenderBackground(gfx::KCommandBuffer* pPrimaryCmdBuffer, uint32_t uSwapChainIndex, float fExposure);
    BOOL _SetCurrentFrameVerticesAndIndices();
    BOOL _RenderFromHardwareBuffer(uint32_t uSwapChainIndex, float fExposure);
    BOOL _CreateVertexBuffer(size_t vertexSize, gfx::KGfxBuffer** ppVertexBuffer, const std::vector<Vertex>& vertices);
    BOOL _CreateIndexBuffer(size_t indexSize, gfx::KGfxBuffer** ppIndexBuffer);
    BOOL _CreateImage(KCameraImage& cameraImage, VkAndroidHardwareBufferFormatPropertiesANDROID& formatProperties);
    BOOL _GetHardwareBufferProperties(AHardwareBuffer* pHardwareBuffer, VkAndroidHardwareBufferFormatPropertiesANDROID& formatProperties, VkAndroidHardwareBufferPropertiesANDROID& properties);
    BOOL _BindImageToHardwareBuffer(VkImage& hImage, uint32_t uWidth, uint32_t uHeight, const VkAndroidHardwareBufferFormatPropertiesANDROID& formatProperties);
    BOOL _AllocateDeviceMemoryForImage(VkImage& hImage, VkDeviceMemory& hDeviceMemory, const VkAndroidHardwareBufferPropertiesANDROID& properties, AHardwareBuffer* pHardwareBuffer);
    BOOL _SetupYcbcrConversion(const VkAndroidHardwareBufferFormatPropertiesANDROID& formatProperties);
    BOOL _CreateImageView(VkImage& hImage, VkFormat format, VkImageView& hView);
    BOOL _CreateSampler();
    BOOL _CreateGraphicsPipeline();
    BOOL _TransitionImageLayout(VkImage& hImage, VkImageLayout oldLayout, VkImageLayout newLayout);
    BOOL _UpdateDescriptorSets(KCameraImage& cameraImage, uint32_t uSwapChainIndex);
    BOOL _UpdateViewportAndScissor(gfx::KCommandBuffer* pCommandBuffer);

    void _DestroyVertexBuffer();
    void _DestroyIndexBuffer();
    void _DestroyImage(KCameraImage& cameraImage);

private:
    std::vector<float> m_vTransformedUV{
            0.0f, 1.0f, // bottom left
            0.0f, 0.0f, // top left
            1.0f, 0.0f, // top right
            1.0f, 1.0f, // bottom right
    };

    std::vector<uint16_t> m_vIndices{
            0, 1, 2,
            2, 3, 0
    };

    gfx::KLayout* m_pLayout{nullptr};
    gfx::KVertexDescriptor* m_pVertexDescriptor{nullptr};
    gfx::KRenderPass* m_pRenderPass{nullptr};
    gfx::KRenderFrameBuffer* m_pFrameBuffer{nullptr};
    gfx::KCommandBuffer* m_pPrimaryCmdBuffer{nullptr};
    gfx::KDescriptorPoolContainer m_DescriptorPoolContainer;
    gfx::KSampler* m_pSampler{nullptr};
    gfx::KPipeline* m_pPipeline{nullptr};
    std::vector<gfx::KDescriptorSet*> m_vDescriptorSets;
    std::vector<gfx::KCommandBuffer*> m_vSecondaryCmdBuffers;
    gfx::KCommandPool* m_pCommandPool{nullptr};

    gfx::KGfxBuffer* m_pVertexBufferDefault{nullptr};
    gfx::KGfxBuffer* m_pVertexBufferPortrait{nullptr};
    gfx::KGfxBuffer* m_pVertexBufferReverse{nullptr};
    gfx::KGfxBuffer* m_pIndexBuffer{nullptr};

    std::vector<KCameraImage> m_vCameraImages;
    VkSamplerYcbcrConversion m_Conversion{VK_NULL_HANDLE};
    VkSamplerYcbcrConversionInfo m_ConversionInfo{};
    int32_t m_nWidth = 0;
    int32_t m_nHeight = 0;
    uint32_t m_uHardwareBufferWidth = 0;
    uint32_t m_uHardwareBufferHeight = 0;
};

#endif