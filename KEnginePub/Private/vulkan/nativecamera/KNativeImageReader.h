//
// Created by Ant on 2024/8/15.
//
#pragma once
#if defined(__ANDROID__)
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>

#include <memory>
#include <vector>
#include "KNativeCameraUtility.h"

class KNativeImageReader
{
public:
    using ImagePtr = std::unique_ptr<AImage, decltype(&AImage_delete)>;
    using ImageReaderPtr = std::unique_ptr<AImageReader, decltype(&AImageReader_delete)>;

    KNativeImageReader();
    ~KNativeImageReader();

    BOOL Init(uint32_t nWidth, uint32_t nHeight, uint32_t nFormat, uint64_t nUsage, uint32_t nMaxImages);
    void UnInit();

    AHardwareBuffer* GetLatestBuffer();
    inline ANativeWindow* GetWindow() const { return m_pWindow; };

    static std::string GetErrorString(media_status_t& status);

private:
    uint32_t m_nCurIndex;
    ANativeWindow* m_pWindow = nullptr;
    std::vector<AHardwareBuffer*> m_vBuffers;

    ImageReaderPtr m_pReader;
    std::vector<ImagePtr> m_vImages;
};
#endif