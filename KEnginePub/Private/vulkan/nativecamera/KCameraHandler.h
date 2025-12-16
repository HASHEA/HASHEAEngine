//
// Created by Ant on 2024/8/16.
//
#pragma once

#include "KBase/Public/System/ISystem.h"

#if defined(__ANDROID__)
#include "android/hardware_buffer_jni.h"
#include "KHardwareBufferBackground.h"
#include "KNativeCamera.h"
#include "KNativeImageReader.h"
#include "KCameraBackground.h"


#include "../../IGFX_Private.h"
#include "KBase/camera_capture/KCameraCaptureTransfer.h"
#include "KBase/Public/System/ISystem.h"

enum class KNativeCameraState
{
    Capturing,
    Stopped,
    Failed,
};

class KCameraHandler
{
public:
    static KCameraHandler& GetInstance()
    {
        static KCameraHandler instance;
        return instance;
    }

public:
    inline uint32_t GetSwapChainImageCount() const { return m_uSwapChainImageCount; }

    AHardwareBuffer* GetHardwareBuffer() const { return m_pNativeHardwareBuffer; }

    BOOL IsStopped() { return m_eNativeCameraState == KNativeCameraState::Stopped; }
    BOOL IsCapturing() { return m_eNativeCameraState == KNativeCameraState::Capturing; }

    void UnInit();

    BOOL Render(const KCameraRenderParam& pParam);
    BOOL FetchHardwareBuffer();


    BOOL StartCapture(uint32_t uWidth, uint32_t uHeight);
    BOOL StopCapture();

    inline KNativeCameraState GetCameraState() const { return m_eNativeCameraState; }

    void Recreate(uint32_t uWidth, uint32_t uHeight);

    bool IsPortrait() const { return m_bIsPortrait; }
    void SetPortrait(bool bIsPortrait)
    {
        m_bIsPortrait = bIsPortrait;
        m_pNativeCamera->SetPortrait(m_bIsPortrait);
    }

    NSKBase::EDeviceOrientation GetDeviceOrientation() const { return m_eDeviceOrientation; }
    void                        SetDeviceOrientation(NSKBase::EDeviceOrientation eDeviceOrientation) { m_eDeviceOrientation = eDeviceOrientation; }

private:
    BOOL _InitNativeCamera(uint32_t uWidth, uint32_t uHeight);

private:
    KCameraHandler();
    KCameraHandler(const KCameraHandler&)            = default;
    KCameraHandler& operator=(const KCameraHandler&) = default;
    ~KCameraHandler()                                = default;

private:
    uint32_t              m_uSwapChainImageCount{0};
    BackgroundRenderState m_eBackgroundRenderState{BackgroundRenderState::RENDER_WITH_HARDWARE_BUFFER};
    KNativeCameraState    m_eNativeCameraState{KNativeCameraState::Stopped};

    std::unique_ptr<KCameraBackground> m_pRenderBackground{nullptr};
    std::unique_ptr<KNativeCamera>     m_pNativeCamera = nullptr;
    AHardwareBuffer*                   m_pNativeHardwareBuffer{nullptr};

    int32_t m_nWidth  = 0;
    int32_t m_nHeight = 0;

    BOOL m_bDoNativeCameraInit{FALSE};
    BOOL m_bNativeCameraInit{FALSE};

    bool                        m_bIsPortrait{false};
    NSKBase::EDeviceOrientation m_eDeviceOrientation{NSKBase::EDeviceOrientation::LandscapeLeft};
};

KCameraHandler& g_GetCameraHandler();
#endif
