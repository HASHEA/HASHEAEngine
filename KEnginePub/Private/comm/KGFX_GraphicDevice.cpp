#include "KGFX_GraphicDevice.h"
#include "KGFX_ShaderHelper.h"
#include "Engine/KGLog.h"
#include "KEnginePub/Public/IGFX_Public.h"
#include "KGFX_StaticConstBuffer.h"
#include "KEnginePub/Private/loader/KGFX_MemTexture.h"
#include "KEnginePub/Private/loader/KGFX_FileTexture.h"

///////////////////////////////////////////////////////////////////////////////
#include "KBase/Public/KMemLeak.h"
#include "optick.h"

namespace gfx
{
    void KGFX_GraphicDevice::UnInit()
    {
        m_DelayRelase_Objects.Uninit("ReleasAll_DelayReleaseObject");
        m_DelayRelase_Program.Uninit("ReleasAll_KGFXProgram");

        KGFX_DestroyShaderFilePool();
    }

    KGFX_GraphicDevice::KGFX_GraphicDevice()
    {
        KGFX_CreateShaderFilePool();
        KGFX_CreateDXCComplier();
    }

    KGFX_GraphicDevice::~KGFX_GraphicDevice()
    {
        KGFX_DestroyShaderFilePool();
        gfx::KGFX_DestroyDXCComplier();
    }

    void KGFX_GraphicDevice::FrameMove(BOOL bFrameRendered)
    {
        m_DelayRelase_Objects.FrameMoveRelease("ReleasAll_DelayReleaseObject");
        m_DelayRelase_Program.FrameMoveRelease("DelayRelase_Program");
    }

    BOOL KGFX_GraphicDevice::DestroyProgram(IKGFX_Program*& pProgram)
    {
        return m_DelayRelase_Program.DestroyResource("KGFX_Program", pProgram);
    }

    BOOL KGFX_GraphicDevice::GC_DelayReleaseObject(KGFX_DelayReleaseObject* pDeviceObj, const std::function<void()>& pfunSyncReleaseCall)
    {
        return m_DelayRelase_Objects.DestroyResource("DestroyDelayReleaseObject", pDeviceObj, pfunSyncReleaseCall);
    }

    void KGFX_GraphicDevice::ReleaseAllDeviceObjects()
    {
        m_DelayRelase_Objects.ReleaseAll("ReleasAll_DelayReleaseObject");
    }

    void KGFX_GraphicDevice::ReleaseAllProgram()
    {
        m_DelayRelase_Program.ReleaseAll("ReleasAll_KGFXProgram");
    }


    bool KGFX_GraphicDevice::CreateStaticConstBuf(IKGFX_ConstBuffer** ppRetConstBuffer, uint32_t size, const char* pDebugName)
    {
        BOOL bResult  = false;
        BOOL bRetCode = false;

        IKGFX_ConstBuffer* pBuffer = new gfx::KGFX_MemoryConstBuffer;
        bRetCode                   = pBuffer->Init(size);
        KGLOG_PROCESS_ERROR(bRetCode);
    Exit0:
        if (!bRetCode)
        {
            SAFE_RELEASE(pBuffer);
        }
        *ppRetConstBuffer = pBuffer;
        return bRetCode;
    }
} // namespace gfx
