//
// Created by Ant on 2024/8/15.
//
#include "KNativeCamera.h"

#if defined(__ANDROID__)
#include <sstream>

bool CompareResolutions(const KNativeCamera::KNativeCameraResolution& a, const KNativeCamera::KNativeCameraResolution& b, float targetAspectRatio);
void configureStabilizationMode(ACaptureRequest* captureRequest, ACameraMetadata* cameraMetadata);

void onCaptureCompleted(void* context, ACameraCaptureSession* session, ACaptureRequest* request, const ACameraMetadata* result)
{
    // 查询曝光时间
    ACameraMetadata_const_entry exposureEntry;
    if (ACameraMetadata_getConstEntry(result, ACAMERA_SENSOR_EXPOSURE_TIME, &exposureEntry) == ACAMERA_OK)
    {
        int64_t exposureTime = exposureEntry.data.i64[0];
        double exposureTimeMs = exposureTime / 1000000.0;
        KGLogPrintf(KGLOG_INFO, "Current capture exposure time: %.9f ms", exposureTimeMs);
    }

    // 查询自动曝光补偿
    ACameraMetadata_const_entry aeCompensationEntry;
    if (ACameraMetadata_getConstEntry(result, ACAMERA_CONTROL_AE_EXPOSURE_COMPENSATION, &aeCompensationEntry) == ACAMERA_OK)
    {
        int32_t aeCompensation = aeCompensationEntry.data.i32[0];
        KGLogPrintf(KGLOG_INFO, "Current capture AE compensation value: %d", aeCompensation);
    }

    // 查询光圈值
    ACameraMetadata_const_entry apertureEntry;
    if (ACameraMetadata_getConstEntry(result, ACAMERA_LENS_APERTURE, &apertureEntry) == ACAMERA_OK)
    {
        float aperture = apertureEntry.data.f[0];
        KGLogPrintf(KGLOG_INFO, "Current capture aperture: f/%.1f", aperture);
    }

    // 查询快门时间
    ACameraMetadata_const_entry shutterEntry;
    if (ACameraMetadata_getConstEntry(result, ACAMERA_SENSOR_EXPOSURE_TIME, &shutterEntry) == ACAMERA_OK)
    {
        int64_t shutterTime = shutterEntry.data.i64[0];
        double shutterTimeMs = shutterTime / 1000000.0;
        KGLogPrintf(KGLOG_INFO, "Current capture shutter time: %.9f ms", shutterTimeMs);
    }

    // 查询ISO值
    ACameraMetadata_const_entry isoEntry;
    if (ACameraMetadata_getConstEntry(result, ACAMERA_SENSOR_SENSITIVITY, &isoEntry) == ACAMERA_OK)
    {
        int32_t iso = isoEntry.data.i32[0];
        KGLogPrintf(KGLOG_INFO, "Current capture ISO: %d", iso);
    }
}

ACameraCaptureSession_captureCallbacks captureCallbacks = {
        .onCaptureCompleted = onCaptureCompleted,
};

KNativeCamera::KNativeCamera()
    : m_pManager{nullptr, ACameraManager_delete},
      m_pIds{nullptr, ACameraManager_deleteCameraIdList},
      m_pDevice{nullptr, ACameraDevice_close},
      m_pOutputs{nullptr, ACaptureSessionOutputContainer_free},
      m_pImgReaderOutput{nullptr, ACaptureSessionOutput_free},
      m_pSession{nullptr, ACameraCaptureSession_close},
      m_pCaptureReq{nullptr, ACaptureRequest_free},
      m_pTarget{nullptr, ACameraOutputTarget_free}
{}

KNativeCamera::~KNativeCamera()
{}

BOOL KNativeCamera::Init(uint32_t width, uint32_t height)
{
    BOOL bResult = FALSE;
    BOOL bRetCode = FALSE;
    camera_status_t eResult = ACAMERA_ERROR_BASE;

    // 初始化camera manager
    {
        m_pManager.reset(ACameraManager_create());
        KGLOG_PROCESS_ERROR(m_pManager.get() && "Cannot create camera manager.");
    }

    // 获取camera id list
    {
        auto pt = m_pIds.release();
        eResult = ACameraManager_getCameraIdList(m_pManager.get(), &pt);
        KGLOG_PROCESS_ERROR(eResult == ACAMERA_OK);
        m_pIds.reset(pt);
        KGLOG_PROCESS_ERROR(m_pIds->numCameras >= 1 && "No cameras found.");
    }

    // 选择camera
    {
        for (uint32_t i = 0; i < m_pIds->numCameras; i++)
        {
            auto cameraID = m_pIds->cameraIds[i];
            ACameraMetadata* metadata = nullptr;
            eResult = ACameraManager_getCameraCharacteristics(m_pManager.get(), cameraID, &metadata);
            if (eResult != ACAMERA_OK || !metadata)
            {
                continue;
            }

            ACameraMetadata_const_entry entry;
            if (ACameraMetadata_getConstEntry(metadata, ACAMERA_LENS_FACING, &entry) == ACAMERA_OK) {

                auto facing = static_cast<acamera_metadata_enum_android_lens_facing_t>(entry.data.u8[0]);

                // 选择使用哪一个摄像头，前置或者后置
                if (facing == ACAMERA_LENS_FACING_BACK)
                {
                    m_sRunningCameraId = std::string(cameraID);
                    ACameraMetadata_free(metadata);
                    metadata = nullptr;
                    break;
                }
            }
            ACameraMetadata_free(metadata);
            metadata = nullptr;
        }
        KGLOG_PROCESS_ERROR(!m_sRunningCameraId.empty() && "Cannot find appropriate device.");
    }

    // 打开camera
    {
        auto pt = m_pDevice.release();
        eResult = ACameraManager_openCamera(m_pManager.get(), m_sRunningCameraId.c_str(), &m_DevStateCbs, &pt);
        m_pDevice.reset(pt);
        KGLOG_PROCESS_ERROR(eResult == ACAMERA_OK || m_pDevice.get());
    }

    // 创建输出容器
    {
        auto pt = m_pOutputs.release();
        auto result = ACaptureSessionOutputContainer_create(&pt);
        m_pOutputs.reset(pt);
        KGLOG_PROCESS_ERROR(result == ACAMERA_OK);
    }

    // 根据当前FrameBuffer大小，计算宽高比最接近且支持的最高清的分辨率, 创建ImageReader
    {
        uint32_t uWidth = 0, uHeight = 0;
        float aspectRatio = width > height ? static_cast<float>(width) / height : static_cast<float>(height) / width;

        _CalculateSupportedResolutions(uWidth, uHeight, aspectRatio);

        if (m_pImageReader == nullptr)
        {
            m_pImageReader = std::make_unique<KNativeImageReader>();
            KGLOG_PROCESS_ERROR(m_pImageReader);
        }

        bRetCode = m_pImageReader->Init(uWidth, uHeight, AIMAGE_FORMAT_PRIVATE, AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE, 5);
        KGLOG_PROCESS_ERROR(bRetCode);
    }

    // 创建ImageReader输出
    {
        auto pt = m_pImgReaderOutput.release();
        eResult = ACaptureSessionOutput_create(m_pImageReader->GetWindow(), &pt);
        m_pImgReaderOutput.reset(pt);
        KGLOG_PROCESS_ERROR(eResult == ACAMERA_OK);
    }

    // 添加ImageReader输出到输出容器
    {
        eResult = ACaptureSessionOutputContainer_add(m_pOutputs.get(), m_pImgReaderOutput.get());
        KGLOG_PROCESS_ERROR(eResult == ACAMERA_OK);
    }

    // 创建capture session
    {
        auto pt = m_pSession.release();
        eResult = ACameraDevice_createCaptureSession(m_pDevice.get(), m_pOutputs.get(), &m_SessionStateCbs, &pt);
        m_pSession.reset(pt);
        KGLOG_PROCESS_ERROR(eResult == ACAMERA_OK);
    }

    // 创建capture request
    {
        auto pt = m_pCaptureReq.release();
        eResult = ACameraDevice_createCaptureRequest(m_pDevice.get(), TEMPLATE_PREVIEW, &pt);

        // 设置自动对焦模式
        uint8_t afMode = ACAMERA_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
        ACaptureRequest_setEntry_u8(pt, ACAMERA_CONTROL_AF_MODE, 1, &afMode);

        // 设置色差矫正
        int32_t aberrationMode = ACAMERA_COLOR_CORRECTION_ABERRATION_MODE_HIGH_QUALITY;
        ACaptureRequest_setEntry_i32(pt, ACAMERA_COLOR_CORRECTION_ABERRATION_MODE, 1, &aberrationMode);

        // 设置自动白平衡
        int32_t awbMode = ACAMERA_CONTROL_AWB_MODE_AUTO;
        ACaptureRequest_setEntry_i32(pt, ACAMERA_CONTROL_AWB_MODE, 1, &awbMode);

        // 设置自动曝光
        int32_t aeMode = ACAMERA_CONTROL_AE_MODE_ON;
        ACaptureRequest_setEntry_i32(pt, ACAMERA_CONTROL_AE_MODE, 1, &aeMode);

        // 设置降噪
        int32_t noiseReductionMode = ACAMERA_NOISE_REDUCTION_MODE_HIGH_QUALITY;
        ACaptureRequest_setEntry_i32(pt, ACAMERA_NOISE_REDUCTION_MODE, 1, &noiseReductionMode);

        // 设置边缘增强
        int32_t edgeMode = ACAMERA_EDGE_MODE_HIGH_QUALITY;
        ACaptureRequest_setEntry_i32(pt, ACAMERA_EDGE_MODE, 1, &edgeMode);

        // 设置镜头遮光校正
        int32_t shadingMode = ACAMERA_SHADING_MODE_HIGH_QUALITY;
        ACaptureRequest_setEntry_i32(pt, ACAMERA_SHADING_MODE, 1, &shadingMode);

        // 设置色彩校正
        int32_t colorCorrectionMode = ACAMERA_COLOR_CORRECTION_MODE_HIGH_QUALITY;
        ACaptureRequest_setEntry_i32(pt, ACAMERA_COLOR_CORRECTION_MODE, 1, &colorCorrectionMode);

        // 设置曝光补偿
//        float exposureCompensation = 0.0f;
//        ACaptureRequest_setEntry_float(pt, ACAMERA_CONTROL_AE_EXPOSURE_COMPENSATION, 1, &exposureCompensation);

        // 设置稳定模式
        ACameraMetadata *cameraMetadata = nullptr;
        ACameraManager_getCameraCharacteristics(m_pManager.get(), m_sRunningCameraId.c_str(), &cameraMetadata);
        configureStabilizationMode(pt, cameraMetadata);

        // 设置帧率范围
//        int32_t frameRateRange[2] = {5, 30};
//        ACaptureRequest_setEntry_i32(pt, ACAMERA_CONTROL_AE_TARGET_FPS_RANGE, 2, frameRateRange);

        // 尝试打开HDR模式
        bool isHdrSupported = false;
        ACameraMetadata_const_entry entry;
        if (ACameraMetadata_getConstEntry(cameraMetadata, ACAMERA_CONTROL_AVAILABLE_SCENE_MODES, &entry) == ACAMERA_OK)
        {
            for (size_t i = 0; i < entry.count; i++)
            {
                if (entry.data.u8[i] == ACAMERA_CONTROL_SCENE_MODE_HDR) {
                    isHdrSupported = true;
                    break;
                }
            }
        }
        if (isHdrSupported)
        {
            int32_t sceneMode = ACAMERA_CONTROL_SCENE_MODE_HDR;
            ACaptureRequest_setEntry_i32(pt, ACAMERA_CONTROL_SCENE_MODE, 1, &sceneMode);
        }


        ACameraMetadata_free(cameraMetadata);

        m_pCaptureReq.reset(pt);
        KGLOG_PROCESS_ERROR(eResult == ACAMERA_OK);
    }

    {
        auto pt = m_pTarget.release();
        eResult = ACameraOutputTarget_create(m_pImageReader->GetWindow(), &pt);
        m_pTarget.reset(pt);
        KGLOG_PROCESS_ERROR(eResult == ACAMERA_OK);
    }

    {
        eResult = ACaptureRequest_addTarget(m_pCaptureReq.get(), m_pTarget.get());
        KGLOG_PROCESS_ERROR(eResult == ACAMERA_OK);
    }

    KGLogPrintf(KGLOG_INFO, "Camera device created.");

    bResult = TRUE;
Exit0:
    if (eResult != ACAMERA_OK)
    {
        KGLogPrintf(KGLOG_ERR, "(code: %s)", GetCameraStatusString(eResult).c_str());
    }

    return bResult;
}

void KNativeCamera::UnInit()
{
    m_pCaptureReq.reset();
    m_pTarget.reset();
    m_pSession.reset();
    m_pImgReaderOutput.reset();
    m_pOutputs.reset();
    m_pDevice.reset();
    m_pIds.reset();
    m_pManager.reset();
    m_pImageReader.reset();
    m_vSupportedResolutions.clear();

    KGLogPrintf(KGLOG_INFO, "Camera device destroyed.");
}

BOOL KNativeCamera::StartCapturing()
{
    BOOL bResult = FALSE;

    auto pt = m_pCaptureReq.release();
    auto result = ACameraCaptureSession_setRepeatingRequest(m_pSession.get(), &captureCallbacks, 1, &pt, nullptr);
    m_pCaptureReq.reset(pt);
    KGLOG_PROCESS_ERROR(result == ACAMERA_OK);

    bResult = TRUE;
Exit0:
    if (result != ACAMERA_OK)
    {
        m_bErrorCameraSession = true;
    }
    else
    {
        bResult = TRUE;
        m_bErrorCameraSession = false;
    }

    return bResult;
}

BOOL KNativeCamera::StopCapturing()
{
    BOOL bResult = FALSE;
    auto result = ACameraCaptureSession_stopRepeating(m_pSession.get());
    KGLOG_PROCESS_ERROR(result == ACAMERA_OK);

    bResult = TRUE;
Exit0:
    if (result != ACAMERA_OK)
    {
        KGLogPrintf(KGLOG_ERR, "(code: %s)", GetCameraStatusString(result).c_str());
    }

    return bResult;
}

AHardwareBuffer* KNativeCamera::GetLatestBuffer()
{
    return m_pImageReader->GetLatestBuffer();
}

std::string KNativeCamera::GetCameraStatusString(camera_status_t& hStatus)
{
    switch (hStatus) {
        case ACAMERA_ERROR_INVALID_PARAMETER:
            return "ACAMERA_ERROR_INVALID_PARAMETER (Camera operation has failed due to an invalid parameter being passed to the method.)";
        case ACAMERA_ERROR_CAMERA_DISCONNECTED:
            return "ACAMERA_ERROR_CAMERA_DISCONNECTED (Camera operation has failed because the camera device has been closed, possibly because a higher-priority client has taken ownership of the camera device.)";
        case ACAMERA_ERROR_NOT_ENOUGH_MEMORY:
            return "ACAMERA_ERROR_NOT_ENOUGH_MEMORY (Camera operation has failed due to insufficient memory.)";
        case ACAMERA_ERROR_METADATA_NOT_FOUND:
            return "ACAMERA_ERROR_METADATA_NOT_FOUND (Camera operation has failed due to the requested metadata tag cannot be found in input {@link ACameraMetadata} or {@link ACaptureRequest})";
        case ACAMERA_ERROR_CAMERA_DEVICE:
            return "ACAMERA_ERROR_CAMERA_DEVICE (Camera operation has failed and the camera device has encountered a fatal error and needs to be re-opened before it can be used again.)";
        case ACAMERA_ERROR_CAMERA_SERVICE:
            return "ACAMERA_ERROR_CAMERA_SERVICE (Camera operation has failed and the camera service has encountered a fatal error. The Android device may need to be shut down and restarted to restore camera function, or there may be a persistent hardware problem. An attempt at recovery may be possible by closing the ACameraDevice and the ACameraManager, and trying to acquire all resources again from scratch.)";
        case ACAMERA_ERROR_SESSION_CLOSED:
           return "ACAMERA_ERROR_SESSION_CLOSED (The {@link ACameraCaptureSession} has been closed and cannnot perform any operation other than {@link ACameraCaptureSession_close}.)";
        case ACAMERA_ERROR_INVALID_OPERATION:
            return "ACAMERA_ERROR_INVALID_OPERATION (Camera operation has failed due to an invalid internal operation. Usually this is due to a low-level problem that may resolve itself on retry.)";
        case ACAMERA_ERROR_STREAM_CONFIGURE_FAIL:
            return "ACAMERA_ERROR_STREAM_CONFIGURE_FAIL (Camera device does not support the stream configuration provided by application in {@link ACameraDevice_createCaptureSession} or {@link ACameraDevice_isSessionConfigurationSupported}.)";
        case ACAMERA_ERROR_CAMERA_IN_USE:
            return "ACAMERA_ERROR_CAMERA_IN_USE (Camera device is being used by another higher priority camera API client.)";
        case ACAMERA_ERROR_MAX_CAMERA_IN_USE:
            return "ACAMERA_ERROR_MAX_CAMERA_IN_USE (The system-wide limit for number of open cameras or camera resources has been reached, and more camera devices cannot be opened until previous instances are closed.)";
        case ACAMERA_ERROR_CAMERA_DISABLED:
            return "ACAMERA_ERROR_CAMERA_DISABLED (The camera is disabled due to a device policy, and cannot be opened.)";
        case ACAMERA_ERROR_PERMISSION_DENIED:
            return "ACAMERA_ERROR_PERMISSION_DENIED (The application does not have permission to open camera.)";
        case ACAMERA_ERROR_UNSUPPORTED_OPERATION:
            return "ACAMERA_ERROR_UNSUPPORTED_OPERATION (The operation is not supported by the camera device.)";
        default:
            return "Unknown error code.";
    }
}

BOOL KNativeCamera::_CalculateSupportedResolutions(uint32_t& uWidth, uint32_t& uHeight, float fAspectRatio)
{
    BOOL bResult = FALSE;
    ACameraMetadata* pMetadata = nullptr;
    camera_status_t eResult = ACAMERA_ERROR_BASE;
    ACameraMetadata_const_entry pEntry;

    eResult = ACameraManager_getCameraCharacteristics(m_pManager.get(), m_sRunningCameraId.c_str(), &pMetadata);
    KGLOG_PROCESS_ERROR(eResult == ACAMERA_OK);

    eResult = ACameraMetadata_getConstEntry(pMetadata, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &pEntry);
    KGLOG_PROCESS_ERROR(eResult == ACAMERA_OK);

    for (int32_t i = 0; i < (int32_t)pEntry.count; i += 4)
    {
        auto width = pEntry.data.i32[i + 1];
        auto height = pEntry.data.i32[i + 2];
        auto input = pEntry.data.i32[i + 3];

        if (input == 0)
        {
            m_vSupportedResolutions.emplace_back(KNativeCameraResolution(width, height));
        }
    }

    std::sort(m_vSupportedResolutions.begin(), m_vSupportedResolutions.end(), [fAspectRatio](const KNativeCameraResolution& a, const KNativeCameraResolution& b) {
        return CompareResolutions(a, b, fAspectRatio);
    });

    uWidth = (uint32_t)m_vSupportedResolutions[0].uWidth;
    uHeight = (uint32_t)m_vSupportedResolutions[0].uHeight;

    bResult = TRUE;
Exit0:
    if (pMetadata != nullptr)
    {
        ACameraMetadata_free(pMetadata);
    }

    if (eResult != ACAMERA_OK)
    {
        KGLogPrintf(KGLOG_ERR, "(code: %s)", GetCameraStatusString(eResult).c_str());
    }

    return bResult;
}

bool CompareResolutions(const KNativeCamera::KNativeCameraResolution& a, const KNativeCamera::KNativeCameraResolution& b, float targetAspectRatio)
{
    float aspectA = a.GetAspectRation();
    float aspectB = b.GetAspectRation();

    float diffA = std::abs(aspectA - targetAspectRatio);
    float diffB = std::abs(aspectB - targetAspectRatio);

    if (diffA != diffB)
    {
        return diffA < diffB;
    }
    else
    {
        return (a.uWidth * a.uHeight) > (b.uWidth * b.uHeight);
    }
}

void configureStabilizationMode(ACaptureRequest* captureRequest, ACameraMetadata* cameraMetadata)
{
    ACameraMetadata_const_entry entry;
    if (ACameraMetadata_getConstEntry(cameraMetadata, ACAMERA_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES, &entry) == ACAMERA_OK) {

        bool supported = false;
        for (size_t i = 0; i < entry.count; i++) {

            if (entry.data.u8[i] == ACAMERA_CONTROL_VIDEO_STABILIZATION_MODE_OFF) {

                supported = true;
                break;
            }
        }

        if (supported) {

            int32_t stabilizationMode = ACAMERA_CONTROL_VIDEO_STABILIZATION_MODE_ON;
            ACaptureRequest_setEntry_i32(captureRequest, ACAMERA_CONTROL_VIDEO_STABILIZATION_MODE, 1, &stabilizationMode);
        }
    }
}
#endif