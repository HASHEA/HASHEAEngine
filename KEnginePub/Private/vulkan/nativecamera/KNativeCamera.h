//
// Created by Ant on 2024/8/15.
//
#pragma once
#if defined(__ANDROID__)
#include <camera/NdkCaptureRequest.h>
#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraError.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadataTags.h>
#include <memory>

#include "KNativeImageReader.h"
#include "KNativeCameraUtility.h"
#include "Engine/KGLog.h"

class KNativeCamera
{
public:
    struct KNativeCameraResolution
    {
        int32_t uWidth;
        int32_t uHeight;

        KNativeCameraResolution(int32_t width, int32_t height) : uWidth(width), uHeight(height) {}

        float GetAspectRation() const { return static_cast<float>(uWidth) / uHeight; }
    };

    using ACameraManagerPtr = std::unique_ptr<ACameraManager, decltype(&ACameraManager_delete)>;
    using ACameraIdListPtr = std::unique_ptr<ACameraIdList, decltype(&ACameraManager_deleteCameraIdList)>;
    using ACameraDevicePtr = std::unique_ptr<ACameraDevice, decltype(&ACameraDevice_close)>;
    using ACaptureSessionOutputContainerPtr =
        std::unique_ptr<ACaptureSessionOutputContainer, decltype(&ACaptureSessionOutputContainer_free)>;
    using ACaptureSessionOutputPtr =
        std::unique_ptr<ACaptureSessionOutput, decltype(&ACaptureSessionOutput_free)>;
    using ACameraCaptureSessionPtr =
        std::unique_ptr<ACameraCaptureSession, decltype(&ACameraCaptureSession_close)>;
    using ACaptureRequestPtr = std::unique_ptr<ACaptureRequest, decltype(&ACaptureRequest_free)>;
    using ACameraOutputTargetPtr =
        std::unique_ptr<ACameraOutputTarget, decltype(&ACameraOutputTarget_free)>;

    KNativeCamera();
    ~KNativeCamera();

    BOOL Init(uint32_t width, uint32_t height);
    void UnInit();

    BOOL StartCapturing();
    BOOL StopCapturing();

    AHardwareBuffer* GetLatestBuffer();

    bool IsCameraSessionError() const { return m_bErrorCameraSession; }

    void SetPortrait(bool bPortrait) { m_bPortrait = bPortrait; }

    static void OnDeviceDisconnected(void* a_obj, ACameraDevice* a_device) { KGLogPrintf(KGLOG_INFO, "Camera device disconnected."); }

    static void OnDeviceError(void* a_obj, ACameraDevice* a_device, int a_err_code) { KGLogPrintf(KGLOG_INFO, "Camera device error: %d", a_err_code); }

    static void OnSessionClosed(void* a_obj, ACameraCaptureSession* a_session) { KGLogPrintf(KGLOG_INFO, "Camera session closed."); }

    static void OnSessionReady(void* a_obj, ACameraCaptureSession* a_session) { KGLogPrintf(KGLOG_INFO, "Camera session ready."); }

    static void OnSessionActive(void* a_obj, ACameraCaptureSession* a_session) { KGLogPrintf(KGLOG_INFO, "Camera session active."); }

private:
    static std::string GetCameraStatusString(camera_status_t& hStatus);
    BOOL _CalculateSupportedResolutions(uint32_t& uWidth, uint32_t& uHeight, float fAspectRatio);

private:

    ACameraDevice_StateCallbacks m_DevStateCbs
    {
        this,
        OnDeviceDisconnected,
        OnDeviceError
    };

    ACameraCaptureSession_stateCallbacks m_SessionStateCbs
    {
        this,
        OnSessionClosed,
        OnSessionReady,
        OnSessionActive
    };

    ACameraManagerPtr m_pManager;
    ACameraIdListPtr m_pIds;
    ACameraDevicePtr m_pDevice;
    ACaptureSessionOutputContainerPtr m_pOutputs;
    ACaptureSessionOutputPtr m_pImgReaderOutput;
    ACameraCaptureSessionPtr m_pSession;
    ACaptureRequestPtr m_pCaptureReq;
    ACameraOutputTargetPtr m_pTarget;
    std::string m_sRunningCameraId;
    std::vector<KNativeCameraResolution> m_vSupportedResolutions;
    std::unique_ptr<KNativeImageReader> m_pImageReader;
    bool m_bPortrait{false};

    bool m_bErrorCameraSession{false};
};
#endif