#include "EtcPak.h"
#include "gli/gli.hpp"
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "Engine/KGLog.h"
#include "Bitmap.h"
#include "BlockData.h"
#include "DataProvider.h"
#include "Debug.h"
#include "Error.h"
#include "System.h"
#include "TaskDispatch.h"
#include "Timing.h"
#include "Engine/KGCRT.h"
#include "KBase/Public/io/KFile.h"
#include "KBase/Public/time/KTimer.h"
#include "KBase/Public/KBasePub.h"
#include "KEnginePub/Public/IKEngineOption.h"
#include "KEnginePub/Public/IKEnginePerformance.h"
//////////////////////////////////////////////////////////////////////////
#include "KBase/Public/KMemLeak.h"

using namespace NSKBase;

struct Pixel
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

BOOL ETC_PAK::LoadTga(const char* pTgaFile, uint32_t** ppBuffer, uint32_t& pixelCount, uint32_t& uWidth, uint32_t& uHeight, BOOL& bHasAlpha, BOOL inversY, BOOL bSwapRB)
{
    BOOL     bRet        = false;
    BOOL     bRetCode    = false;
    uint8_t  header0[12] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t  header1[6]  = {0};
    uint8_t  pixelPos    = sizeof(header0) + sizeof(header1);
    uint32_t uChannels   = 0;

    uint16_t width     = 0;
    uint16_t height    = 0;
    uint8_t  pixelbits = 0;
    uint32_t id;
    uint8_t* buf       = nullptr;
    Pixel*   p         = nullptr;
    uint32_t offset    = 0;
    uint32_t dataBytes = 0;
    uint32_t uiPixels  = 0;

    KGFile* fp = KGFOpen(pTgaFile, "rb");
    if (!fp)
    {
        goto Exit0;
    }

    KGFRead(header0, sizeof(header0), fp);
    KGFRead(header1, sizeof(header1), fp);

    memcpy(&width, &header1[0], sizeof(uint16_t));
    memcpy(&height, &header1[2], sizeof(uint16_t));
    pixelbits = header1[4];
    uChannels = pixelbits / 8;

    dataBytes = uChannels * width * height;

    uiPixels = width * height;
    p        = new Pixel[uiPixels];
    buf      = new uint8_t[dataBytes];
    KGFRead(buf, sizeof(uint8_t) * dataBytes, fp);

    // invers y and  bgra to rgba
    for (uint32_t i = 0; i < height; ++i)
    {
        for (uint32_t j = 0; j < width; ++j)
        {
            if (inversY)
            {
                id = (height - i - 1) * width * uChannels + j * uChannels;
            }
            else
            {
                id = i * width * uChannels + j * uChannels;
            }

            if (bSwapRB)
            {
                p[offset].r = buf[id + 2];
                p[offset].g = buf[id + 1];
                p[offset].b = buf[id];
            }
            else
            {
                p[offset].r = buf[id];
                p[offset].g = buf[id + 1];
                p[offset].b = buf[id + 2];
            }

            if (uChannels == 4)
            {
                p[offset].a = buf[id + 3];
            }
            else
            {
                p[offset].a = 0;
            }

            offset++;
        }
    }
    assert(offset == width * height);

    bRet = true;

Exit0:
    if (fp)
    {
        KGFClose(fp);
        fp = nullptr;
    }
    if (buf)
    {
        delete[] buf;
        buf = nullptr;
    }
    if (bRet)
    {
        *ppBuffer  = (uint32_t*)p;
        pixelCount = width * height;
        uWidth     = width;
        uHeight    = height;
        if (uChannels == 4)
        {
            bHasAlpha = true;
        }
        else
        {
            bHasAlpha = false;
        }
    }

    if (p)
    {
        delete[] p;
        p = nullptr;
    }
    return bRet;
}


BOOL ETC_PAK::BuildCompressdFileData(uint8_t** ppOutFileData, size_t& outFileSize, uint32_t* pPixelData, uint32_t width, uint32_t height, BOOL bAlpha, BOOL mipmap, TEX_TYPE texType)
{
    BOOL   bRet       = false;
    BOOL   bRetCode   = false;
    BOOL   etc2       = (texType == ETC2);
    BOOL   rgba       = true;
    BOOL   dxtc       = (texType == DXTC);
    BOOL   alpha      = bAlpha;
    bool   dither     = false;
    size_t pixelCount = width * height;
    {
        // unsigned int cpus = System::CPUCores();

        DataProvider dp(pPixelData, pixelCount, width, height, alpha, mipmap, false);
        auto         num = dp.NumberOfParts();

        ETC_PAK::Type type;
        if (etc2)
        {
            if (rgba && dp.Alpha())
            {
                type = Etc2_RGBA;
            }
            else
            {
                type = Etc2_RGB;
            }
        }
        else if (dxtc)
        {
            if (dp.Alpha())
            {
                type = Dxt5;
            }
            else
            {
                type = Dxt1;
            }
        }
        else
        {
            type = Etc1;
        }

        // TaskDispatch taskDispatch(cpus);
        TaskDispatchSingleton::Instance().DoNothing();

        auto bd = std::make_shared<BlockData>(dp.Size(), mipmap, type);
        // auto bd = std::make_shared<BlockData>("op.pvr", dp.Size(), mipmap, type);
        // BlockDataPtr bda;
        // if (alpha && dp.Alpha() && !rgba)
        //{
        //	bda = std::make_shared<BlockData>(alpha, dp.Size(), mipmap, type);
        // }
        for (uint32_t i = 0; i < num; i++)
        {
            auto part = dp.NextPart();

            if (type == ETC_PAK::Etc2_RGBA || type == ETC_PAK::Dxt5)
            {
                TaskDispatch::Queue([part, i, &bd, &dither]() { bd->ProcessRGBA(part.src, part.width / 4 * part.lines, part.offset, part.width); });
            }
            else
            {
                TaskDispatch::Queue([part, i, &bd, &dither]() { bd->Process(part.src, part.width / 4 * part.lines, part.offset, part.width, Channels::RGB, dither); });
                // if (bda)
                //{
                //	TaskDispatch::Queue([part, i, &bda]()
                //		{
                //			bda->Process(part.src, part.width / 4 * part.lines, part.offset, part.width, Channels::Alpha, false);
                //		});
                // }
            }
        }
        TaskDispatch::Sync();
        bd->FixData();
        *ppOutFileData = bd->GetBuffer();
        outFileSize    = bd->GetBufferLen();
    }

    bRet = true;
    // Exit0:
    return bRet;
}


int ETC_PAK::TestCompress()
{
    BOOL etc2   = false;
    BOOL rgba   = false;
    BOOL dxtc   = true;
    BOOL mipmap = false;
    BOOL alpha  = false;
    bool dither = false;


    uint32_t* buffer     = nullptr;
    uint32_t  pixelCount = 0;
    uint32_t  width      = 0;
    uint32_t  height     = 0;
    char      path[1024];

    LoadTga("enginedata/dda.tga", &buffer, pixelCount, width, height, alpha, false, false);

    uint8_t* pFileData = nullptr;
    size_t   fileSize  = 0;

    double t1 = KEnginePerformance::TimeGetTime();
    BuildCompressdFileData(&pFileData, fileSize, buffer, width, height, alpha, true, ETC2);
    double t2 = KEnginePerformance::TimeGetTime();

    KGLogPrintf(KGLOG_INFO, "compress time: %f", t2 - t1);

    if (pFileData && fileSize)
    {
        sprintf(path, "%s/dda.ktx", GetWorkPath());
        FILE* fp = fopen(path, "wb");
        if (fp)
        {
            fwrite(pFileData, sizeof(uint8_t), fileSize, fp);
            fclose(fp);
        }
    }


    if (buffer)
    {
        delete[] buffer;
    }

    if (pFileData)
    {
        delete[] pFileData;
    }
    return 0;
}


BOOL ETC_PAK::IsETC(const char* pFileName)
{
    BOOL  bRet   = false;
    char* buffer = nullptr;
    long  len    = 0;
    FILE* fp     = fopen(pFileName, "rb");
    BOOL  bEtc   = false;
    KG_PROCESS_ERROR(fp);

    {
        fseek(fp, 0, SEEK_END);
        len = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        buffer = new char[len];
        fread(buffer, sizeof(char) * len, 1, fp);

        gli::texture t = gli::loadbyType(buffer, len, gli::KTX_TYPE);

        KG_PROCESS_ERROR(!t.empty());

        gli::format fmt = t.format();

        if (fmt >= gli::FORMAT_RGB_ETC2_UNORM_BLOCK8 && fmt <= gli::FORMAT_RG_EAC_SNORM_BLOCK16)
        {
            bEtc = true;
        }
    }

Exit0:
    if (fp)
    {
        fclose(fp);
    }
    // if (!bEtc)
    //{
    //	printf("%s \r\n", pFileName);
    // }
    SAFE_DELETE_ARRAY(buffer);
    return bEtc;
}

#ifdef _WIN32

void GetKTXFiles(std::string folder_path, std::vector<std::string>& files)
{
    // 文件句柄
    intptr_t           hFile = 0; // Win10
    // 文件信息
    struct _finddata_t fileinfo;
    std::string        p;
    try
    {
        if ((hFile = _findfirst(p.assign(folder_path).append("/*").c_str(), &fileinfo)) != -1)
        {
            do
            {
                // 如果是目录,迭代之
                // 如果不是,加入列表
                if ((fileinfo.attrib & _A_SUBDIR))
                {
                    if (strcmp(fileinfo.name, ".") != 0 && strcmp(fileinfo.name, "..") != 0)
                        GetKTXFiles(p.assign(folder_path).append("/").append(fileinfo.name), files);
                }
                else
                {
                    if (strstr(fileinfo.name, ".ktx"))
                    {
                        files.push_back(p.assign(folder_path).append("/").append(fileinfo.name));
                    }
                }
            } while (_findnext(hFile, &fileinfo) == 0);

            _findclose(hFile);
        }
    }
    catch (std::exception e)
    {
        printf("something error");
    }
}

void ETC_PAK::CheckEtcDirListForWin32(const char* szDirName)
{
    std::vector<std::string> filenames;
    GetKTXFiles(szDirName, filenames);
    FILE* fp = fopen("error1.log", "wb");
    if (fp)
    {
        uint32_t n               = 0;
        uint32_t uCount          = (uint32_t)filenames.size();
        uint32_t uFindErrorCount = 0;
        for (auto it : filenames)
        {
            std::string& filename = it;
            if (!IsETC(filename.c_str()) && strstr(filename.c_str(), "mb/landscape/procedural") == nullptr)
            {
                fprintf(fp, "%s\r\n", filename.c_str());
                fflush(fp);
                uFindErrorCount++;
            }
            if (n && n % 10000 == 0)
            {
                printf("扫描...%d:%d, (%.3f%%) 累计发现 %d 个问题贴图 \r\n", n, uCount, ((float)n / (float)uCount) * 100.0f, uFindErrorCount);
            }
            ++n;
        }
        printf("扫描...%d:%d, (%.3f%%) 累计发现 %d 个问题贴图 \r\n", uCount, uCount, ((float)uCount / (float)uCount) * 100.0f, uFindErrorCount);
        fclose(fp);
    }
    printf("完成扫描");
}

#endif

ETC_PAK::TaskDispatchSingleton::TaskDispatchSingleton()
{
    ASSERT(!m_pTaskDispatch);
    unsigned int cpus = System::CPUCores();
    m_pTaskDispatch   = new TaskDispatch(cpus);
}

ETC_PAK::TaskDispatchSingleton::~TaskDispatchSingleton()
{
    SAFE_DELETE(m_pTaskDispatch);
}
