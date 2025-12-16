//
// Created by Ant on 2024/8/16.
//
#if defined(__ANDROID__)
#include "KCameraHandler.h"
#include "KNativeCameraUtility.h"

KCameraHandler& g_GetCameraHandler()
{
    return KCameraHandler::GetInstance();
}

KCameraHandler::KCameraHandler()
{
    m_pNativeCamera = std::make_unique<KNativeCamera>();
    m_pRenderBackground = std::make_unique<KHardwareCameraBackground>();
};

BOOL KCameraHandler::_InitNativeCamera(uint32_t uWidth, uint32_t uHeight)
{
    BOOL bResult = FALSE;
    BOOL bRetCode = FALSE;

    KGLOG_ASSERT_EXIT(NSKBase::g_sCameraCaptureTransfer.IsCapturing());
    KGLOG_ASSERT_EXIT(!m_bNativeCameraInit);

    bRetCode = DrvOption::bEnableAndroidCamera;
    KGLOG_PROCESS_ERROR(bRetCode && "Hardware Buffer Not Supported.");

    bRetCode = KNativeCameraUtility::GetSwapChainCount(m_uSwapChainImageCount);
    KGLOG_PROCESS_ERROR(bRetCode && m_uSwapChainImageCount > 0);

    KGLOG_PROCESS_ERROR(m_pNativeCamera);
    bRetCode = m_pNativeCamera->Init(uWidth, uHeight);
    KGLOG_PROCESS_ERROR(bRetCode);

    KGLOG_PROCESS_ERROR(m_pRenderBackground);
    bRetCode = m_pRenderBackground->Init();
    KGLOG_PROCESS_ERROR(bRetCode);

    bResult = TRUE;
Exit0:
    if (!bResult) 
    {
        KGLogPrintf(KGLOG_ERR, "Camera Handler Init Failed.");
    }
    return bResult;
}

void KCameraHandler::UnInit()
{
    if (IsCapturing())
    {
        StopCapture();
    }

    if (m_pNativeCamera)
    {
        m_pNativeCamera->UnInit();
    }

    if (m_pRenderBackground)
    {
        m_pRenderBackground->UnInit();
    }

    m_eNativeCameraState = KNativeCameraState::Stopped;
    m_bNativeCameraInit = FALSE;
    m_bDoNativeCameraInit = FALSE;
}

void KCameraHandler::Recreate(uint32_t uWidth, uint32_t uHeight)
{
    if (IsCapturing())
    {
        StopCapture();
    }

    if (m_pNativeCamera)
    {
        m_pNativeCamera->UnInit();
    }

    if (m_pRenderBackground)
    {
       m_pRenderBackground->Recreate();
    }

    m_eNativeCameraState = KNativeCameraState::Stopped;
    m_bNativeCameraInit = FALSE;
    m_bDoNativeCameraInit = FALSE;

    StartCapture(uWidth, uHeight);
}

BOOL KCameraHandler::Render(const KCameraRenderParam& pParam)
{
    BOOL bResult = FALSE;
    BOOL bRetCode = FALSE;

    KGLOG_ASSERT_EXIT(NSKBase::g_sCameraCaptureTransfer.IsCapturing());
    KGLOG_PROCESS_ERROR(m_eNativeCameraState == KNativeCameraState::Capturing);

    bRetCode = DrvOption::bEnableAndroidCamera;
    KGLOG_PROCESS_ERROR(bRetCode && "Hardware Buffer Not Supported.");

    if (m_pNativeCamera->IsCameraSessionError())
    {
        m_pNativeCamera->UnInit();
        m_bNativeCameraInit = FALSE;
        m_bDoNativeCameraInit = FALSE;
        StartCapture(pParam.nWidth, pParam.nHeight);
    }

    bRetCode = FetchHardwareBuffer();
    KGLOG_PROCESS_ERROR(bRetCode && "Failed to fetch hardware buffer.");

    bRetCode = m_pRenderBackground->Render(pParam);
    KGLOG_PROCESS_ERROR(bRetCode && "Failed to render background.");

    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL KCameraHandler::FetchHardwareBuffer()
{
    BOOL bResult = FALSE;

    m_pNativeHardwareBuffer = m_pNativeCamera->GetLatestBuffer();
    KGLOG_PROCESS_ERROR(m_pNativeHardwareBuffer && "Failed to get latest hardware buffer.");

    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL KCameraHandler::StartCapture(uint32_t uWidth, uint32_t uHeight)
{
    BOOL bResult = FALSE;
    BOOL bRetCode = FALSE;

    KGLOG_ASSERT_EXIT(NSKBase::g_sCameraCaptureTransfer.IsCapturing() && "Camera Capture Transfer is not started.");
    KGLOG_PROCESS_ERROR(m_eNativeCameraState != KNativeCameraState::Capturing && "Camera is already capturing.");

    bRetCode = DrvOption::bEnableAndroidCamera;
    KGLOG_PROCESS_ERROR(bRetCode && "Hardware Buffer Not Supported.");

    if (!m_bDoNativeCameraInit)
    {
        m_bDoNativeCameraInit = TRUE;
        m_nWidth = uWidth;
        m_nHeight = uHeight;
        bRetCode = _InitNativeCamera(m_nWidth, m_nHeight);
        KGLOG_ASSERT_EXIT(bRetCode);
        m_bNativeCameraInit = TRUE;
    }
    KGLOG_ASSERT_EXIT(m_bNativeCameraInit && "Native Camera Init Failed.");

    KGLOG_ASSERT_EXIT(m_eNativeCameraState != KNativeCameraState::Capturing);
    bRetCode = m_pNativeCamera->StartCapturing();
    KGLOG_PROCESS_ERROR(bRetCode && "Failed to start capturing.");
    m_eNativeCameraState = KNativeCameraState::Capturing;

    bResult = TRUE;
Exit0:
    if (!bResult)
    {
        m_eNativeCameraState = KNativeCameraState::Failed;
    }
    return bResult;
}

BOOL KCameraHandler::StopCapture()
{
    BOOL bResult = FALSE;

    if (m_eNativeCameraState == KNativeCameraState::Capturing)
    {
        bResult = m_pNativeCamera->StopCapturing();
        KGLOG_PROCESS_ERROR(bResult && "Failed to stop capturing.");

        m_eNativeCameraState = KNativeCameraState::Stopped;
    }

    bResult = TRUE;
Exit0:
    return bResult;
}

#endif