#include "KNativeImageReader.h"
#if defined(__ANDROID__)
#include "Engine/KGLog.h"

KNativeImageReader::KNativeImageReader()
                  : m_nCurIndex{0}, m_pReader{nullptr, AImageReader_delete}
{}

KNativeImageReader::~KNativeImageReader()
{}

BOOL KNativeImageReader::Init(uint32_t nWidth, uint32_t nHeight, uint32_t nFormat, uint64_t nUsage, uint32_t nMaxImages)
{
    BOOL bResult = FALSE;
    media_status_t eResult = AMEDIA_ERROR_BASE;

    KGLOG_PROCESS_ERROR(nMaxImages > 2 && "Max images must be at least 2.");

    for (uint32_t i = 0; i < nMaxImages; i++) {

        m_vImages.push_back({nullptr, AImage_delete});
    }
    m_vBuffers = std::vector<AHardwareBuffer*>(nMaxImages, nullptr);

    {
        auto pt = m_pReader.release();
        eResult = AImageReader_newWithUsage(nWidth, nHeight, nFormat, nUsage, m_vImages.size() + 2, &pt);
        m_pReader.reset(pt);
        KGLOG_PROCESS_ERROR(eResult == AMEDIA_OK);
    }

    {
        eResult = AImageReader_getWindow(m_pReader.get(), &m_pWindow);
        KGLOG_PROCESS_ERROR(eResult == AMEDIA_OK && m_pWindow != nullptr);
    }

    KGLogPrintf(KGLOG_INFO, "Image reader created.");

    bResult = TRUE;
Exit0:
    if (eResult != AMEDIA_OK)
    {
        KGLogPrintf(KGLOG_ERR, "(code: %s)", GetErrorString(eResult).c_str());
    }
    return bResult;
}

void KNativeImageReader::UnInit()
{
    m_pReader.reset();
    m_vImages.clear();
    m_vBuffers.clear();
    m_pWindow = nullptr;
    KGLogPrintf(KGLOG_INFO, "Destroying image reader...");
}

AHardwareBuffer* KNativeImageReader::GetLatestBuffer()
{
    AImage* pImage = nullptr;
    media_status_t eResult = AMEDIA_ERROR_BASE;
    eResult = AImageReader_acquireLatestImage(m_pReader.get(), &pImage);
    if (eResult != AMEDIA_OK || !pImage)
    {
        KGLogPrintf(KGLOG_ERR, "(code: %s)", GetErrorString(eResult).c_str());
    }
    else {

        AHardwareBuffer* buffer = nullptr;
        eResult = AImage_getHardwareBuffer(pImage, &buffer);
        if (eResult != AMEDIA_OK || !buffer)
        {
            KGLogPrintf(KGLOG_ERR, "(code: %s)", GetErrorString(eResult).c_str());
        }
        else
        {
            m_nCurIndex++;
            if (m_nCurIndex == m_vImages.size())
            {
                m_nCurIndex = 0;
            }
            m_vImages[m_nCurIndex].reset(pImage);
            m_vBuffers[m_nCurIndex] = buffer;
        }
    }

    return m_vBuffers[m_nCurIndex];
}

std::string KNativeImageReader::GetErrorString(media_status_t& status)
{
    switch (status)
    {
        case AMEDIACODEC_ERROR_INSUFFICIENT_RESOURCE:
            return "AMEDIACODEC_ERROR_INSUFFICIENT_RESOURCE (This indicates required resource was not able to be allocated.)";
        case AMEDIACODEC_ERROR_RECLAIMED:
            return "AMEDIACODEC_ERROR_RECLAIMED (This indicates the resource manager reclaimed the media resource used by the codec. With this error, the codec must be released, as it has moved to terminal state.)";
        case AMEDIA_ERROR_MALFORMED:
            return "AMEDIA_ERROR_MALFORMED (The input media data is corrupt or incomplete.)";
        case AMEDIA_ERROR_UNSUPPORTED:
            return "AMEDIA_ERROR_UNSUPPORTED (The required operation or media formats are not supported.)";
        case AMEDIA_ERROR_INVALID_OBJECT:
            return "AMEDIA_ERROR_INVALID_OBJECT (An invalid (or already closed) object is used in the function call.)";
        case AMEDIA_ERROR_INVALID_PARAMETER:
            return "AMEDIA_ERROR_INVALID_PARAMETER (At least one of the invalid parameters is used.)";
        case AMEDIA_ERROR_INVALID_OPERATION:
            return "AMEDIA_ERROR_INVALID_OPERATION (The media object is not in the right state for the required operation.)";
        case AMEDIA_ERROR_END_OF_STREAM:
            return "AMEDIA_ERROR_END_OF_STREAM (Media stream ends while processing the requested operation.)";
        case AMEDIA_ERROR_IO:
            return "AMEDIA_ERROR_IO (An Error occurred when the Media object is carrying IO operation.)";
        case AMEDIA_ERROR_WOULD_BLOCK:
            return "AMEDIA_ERROR_WOULD_BLOCK (The required operation would have to be blocked (on I/O or others), but blocking is not enabled.)";
        case AMEDIA_DRM_ERROR_BASE :
        case AMEDIA_DRM_NOT_PROVISIONED:
        case AMEDIA_DRM_RESOURCE_BUSY:
        case AMEDIA_DRM_DEVICE_REVOKED:
        case AMEDIA_DRM_SHORT_BUFFER:
        case AMEDIA_DRM_SESSION_NOT_OPENED:
        case AMEDIA_DRM_TAMPER_DETECTED:
        case AMEDIA_DRM_VERIFY_FAILED:
        case AMEDIA_DRM_NEED_KEY:
        case AMEDIA_DRM_LICENSE_EXPIRED:
            return "DRM error.";
        case AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE:
            return "AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE (There are no more image buffers to read/write image data.)";
        case AMEDIA_IMGREADER_MAX_IMAGES_ACQUIRED:
            return "AMEDIA_IMGREADER_MAX_IMAGES_ACQUIRED (The AImage object has used up the allowed maximum image buffers.)";
        case AMEDIA_IMGREADER_CANNOT_LOCK_IMAGE:
            return "AMEDIA_IMGREADER_CANNOT_LOCK_IMAGE (The required image buffer could not be locked to read.)";
        case AMEDIA_IMGREADER_CANNOT_UNLOCK_IMAGE:
            return "AMEDIA_IMGREADER_CANNOT_UNLOCK_IMAGE (The media data or buffer could not be unlocked.)";
        case AMEDIA_IMGREADER_IMAGE_NOT_LOCKED:
            return "AMEDIA_IMGREADER_IMAGE_NOT_LOCKED (The media/buffer needs to be locked to perform the required operation.)";
        default:
            return "Unknown error.";
    }
}
#endif