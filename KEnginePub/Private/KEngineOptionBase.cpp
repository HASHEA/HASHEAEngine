#include "KEnginePub/Public/KEngineOptionBase.h"
#include "Engine/KGLog.h"
#include "Engine/FileTypeBase.h"
#include "KEnginePub/Public/IKEngineOption.h"
#include "KBase/Public/system/ISystem.h"
#include "KBase/Public/str/KStrHelper.h"
#include "KBase/Public/thread/KThread.h"
#include "KEnginePub/Public/KEsDrv.h"


#define GPU_SWITCH_OPTION_TAB_PATH "enginedata/GpuSwitchOptionTab.tab"
#define X3D_ENGINE_SWITCH_OPTION_TAB_COL_PLATFORM        1
#define X3D_ENGINE_SWITCH_OPTION_TAB_COL_GPU             2
#define X3D_ENGINE_SWITCH_OPTION_TAB_COL_CPU             3
#define X3D_ENGINE_SWITCH_OPTION_TAB_COL_MODEL           4
#define X3D_ENGINE_SWITCH_OPTION_TAB_COL_THREAD_AFFINITY 5

namespace
{
    bool IsMatchSwitchValue(const std::string strCurVal, char szColValue[256])
    {
        bool                     bMatch = false;
        std::vector<std::string> vecSplitValue;

        if (szColValue[0] == '*')
        {
            bMatch = true;
            goto Exit0;
        }

        KSTR_HELPER::toLower(szColValue);
        KSTR_HELPER::StrSplit(szColValue, "|", vecSplitValue);
        for (auto strSplitVal : vecSplitValue)
        {
            if (strCurVal.find(strSplitVal) != std::string::npos)
            {
                // 找到了需要block
                bMatch = true;
                break;
            }
        }

    Exit0:
        return bMatch;
    }

    static DeviceConfigFile g_sDeviceConfigFile;

} // namespace

BOOL NSEngine::InitDeviceConfigFile(const char* pcszGpu)
{
    return g_sDeviceConfigFile.InitSwitchConfig(pcszGpu);
}

int NSEngine::GetDeviceConfigFileValue(KUniqueStr pcszSwitchName)
{
    return g_sDeviceConfigFile.GetSwitchOpen(pcszSwitchName);
}


BOOL DeviceConfigFile::InitSwitchConfig(const char* pcszGpu)
{
    BOOL bResult = FALSE;
    BOOL bRetCode = FALSE;

    ITabFile* piTableFile = nullptr;
    int                      nRowCount = 0;
    int                      nColumnCount = 0;
    int                      eThreadAffinityType = static_cast<int>(NSKBase::EThreadAffinityType::ONE_BIG_CORE);
    bool                     bSwitchOpen = true;
    const char* pcszSwitchName = nullptr;
    char                     szTmpValue[256] = "";
    int                      nValue = 0;
    std::vector<std::string> vecSplitValue;

#ifdef _WIN32
    const std::string strPlatform = "win";
#elif defined(__OHOS__)
    const std::string strPlatform = "harmonyOS";
#elif defined(__ANDROID__)
    const std::string strPlatform = "android";
#elif defined(__MACOS__)
    const std::string strPlatform = "macos";
#elif defined(__APPLE__)
    const std::string strPlatform = "ios";
#else
    ASSERT(FALSE);
#endif

    KGLOG_PROCESS_ERROR(pcszGpu);

    // 获取gpu的名字
    g_StrCpyLen(szTmpValue, pcszGpu, countof(szTmpValue));
    KSTR_HELPER::toLower(szTmpValue);
    m_strGpuDevice = szTmpValue;
    KGLogPrintf(KGLOG_INFO, "[EngineSwitchOption] current gpu value: %s", m_strGpuDevice.c_str());

    // 获取cpu的名字
    {
        int bGetRet = NSKBase::g_X3DSystem->GetCPUInfo(szTmpValue, countof(szTmpValue));
        if (bGetRet)
        {
            KSTR_HELPER::toLower(szTmpValue);
            m_strCpuDevice = szTmpValue;
        }
        KGLogPrintf(KGLOG_INFO, "[EngineSwitchOption] current cpu value: %s", m_strCpuDevice.c_str());
    }

    // 获取device的名字
    {
        int bGetRet = NSKBase::g_X3DSystem->GetDeviceModel(szTmpValue, countof(szTmpValue));
        if (bGetRet)
        {
            KSTR_HELPER::toLower(szTmpValue);
            m_strDeviceModel = szTmpValue;
        }
    }
    KGLogPrintf(KGLOG_INFO, "[EngineSwitchOption] current device value: %s", m_strDeviceModel.c_str());

    // 加载配置表
    piTableFile = g_OpenTabFile(GPU_SWITCH_OPTION_TAB_PATH);
    KG_PROCESS_ERROR(piTableFile);

    piTableFile->SetErrorLog(false);

    // 对应的devices有限制，获取每一个模块，cache
    nRowCount = piTableFile->GetHeight();
    nColumnCount = piTableFile->GetWidth();
    KG_PROCESS_ERROR(nRowCount >= 4);
    KG_PROCESS_ERROR(nColumnCount > 0);

    // 匹配对应的device
    for (int nLine = 4; nLine <= nRowCount; ++nLine)
    {
        // 匹配项目
        for (int nCol = 1; nCol <= nColumnCount; ++nCol)
        {
            // 第一列是平台，第二列GPU型号，第三列CPU型号，第四列为机型，第五列为主线程CPU亲合度，其余都是bool 模块是否开启
            if (nCol == X3D_ENGINE_SWITCH_OPTION_TAB_COL_PLATFORM)
            {
                bRetCode = piTableFile->GetString(nLine, nCol, "", szTmpValue, sizeof(szTmpValue));
                if (bRetCode == 1) // 读取到数据，对特定型号判断，如果没有数据，GPU限制
                {
                    bool bMatch = IsMatchSwitchValue(strPlatform, szTmpValue);
                    if (!bMatch)   // 匹配不成功，就不限制了
                    {
                        break;
                    }
                }
            }
            else if (nCol == X3D_ENGINE_SWITCH_OPTION_TAB_COL_GPU)
            {
                bRetCode = piTableFile->GetString(nLine, nCol, "", szTmpValue, sizeof(szTmpValue));
                if (bRetCode == 1)
                {
                    bool bMatch = IsMatchSwitchValue(m_strGpuDevice, szTmpValue);
                    if (!bMatch) // 匹配不成功，就不限制了
                    {
                        break;
                    }
                }
            }
            else if (nCol == X3D_ENGINE_SWITCH_OPTION_TAB_COL_CPU)
            {
                bRetCode = piTableFile->GetString(nLine, nCol, "", szTmpValue, sizeof(szTmpValue));
                if (bRetCode == 1)
                {
                    bool bMatch = IsMatchSwitchValue(m_strCpuDevice, szTmpValue);
                    if (!bMatch) // 匹配不成功，就不限制了
                    {
                        break;
                    }
                }
            }
            else if (nCol == X3D_ENGINE_SWITCH_OPTION_TAB_COL_MODEL)
            {
                bRetCode = piTableFile->GetString(nLine, nCol, "", szTmpValue, sizeof(szTmpValue));
                if (bRetCode == 1) // 读取到数据，对特定型号判断，如果没有数据，GPU限制
                {
                    bool bMatch = IsMatchSwitchValue(m_strDeviceModel, szTmpValue);
                    if (!bMatch)   // 匹配不成功，就不限制了
                    {
                        break;
                    }
                }
            }
            else if (nCol == X3D_ENGINE_SWITCH_OPTION_TAB_COL_THREAD_AFFINITY)
            {
                bRetCode = piTableFile->GetInteger(nLine, nCol, 0, &nValue);
                if (bRetCode == 1)
                {
                    eThreadAffinityType = nValue;
                }
            }
            else
            {
                pcszSwitchName = piTableFile->GetColName(nCol);
                bRetCode = piTableFile->GetBoolean(nLine, nCol, true, &bSwitchOpen);
                if (bRetCode == 1 && pcszSwitchName && pcszSwitchName[0])
                {
                    m_mapSwitch.emplace(g_CacheOriginalString(pcszSwitchName), bSwitchOpen);

                    KGLogPrintf(KGLOG_INFO, "[EngineSwitchOption] match switch %s, %d", pcszSwitchName, bSwitchOpen ? 1 : 0);
                }
            }
        }
    }

    bResult = TRUE;
Exit0:
    KG_COM_RELEASE(piTableFile);
    NSKBase::Android_SetThreadProcessorAffinityType(static_cast<NSKBase::EThreadAffinityType>(eThreadAffinityType));
    KGLogPrintf(KGLOG_INFO, "gpu value: %s", m_strGpuDevice.c_str());
    KGLogPrintf(KGLOG_INFO, "cpu value: %s", m_strCpuDevice.c_str());
    KGLogPrintf(KGLOG_INFO, "szModel value %s", m_strDeviceModel.c_str());
    KGLogPrintf(KGLOG_INFO, "platform value %s", strPlatform.c_str());
    KGLogPrintf(KGLOG_INFO, "affinity type %d", eThreadAffinityType);
    return bResult;
}

int DeviceConfigFile::GetSwitchOpen(KUniqueStr ustrSwitchName)
{
    int             bRet = -1;
    KEngineOptions* pOptions = NSEngine::GetEngineOptions();

    auto iter = m_mapSwitch.find(ustrSwitchName);
    if (iter != m_mapSwitch.end())
    {
        return iter->second;
    }

    return bRet;
}



