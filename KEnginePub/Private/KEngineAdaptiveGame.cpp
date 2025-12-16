#if 0
#if defined(__ANDROID__)
#include "KG3DEngineOpenGL/KEnginePub/KEngineAdaptiveGame.h"
#include "KG3DEngineOpenGL/KEnginePub/KEngineAdaptiveGameFuncStep.h"
#include "Engine/KGLog.h"
#include <dlfcn.h>
#include <android/binder_auto_utils.h>

static bool s_bQapeBinderNeedReconnect = false;
static int s_nQapeBinderReconnectCount = 0;
static constexpr int MAX_QAPE_RECONNECT_NUM = 100;

void DeathNotificationCallback(void* cookie);
void PrintInternalError(int nRetCode);
QAPECpuHeadroomType CpuHeadroomTypeConvert(KCpuHeadroomType eType);

KAdaptiveGameClient::KAdaptiveGameClient()
{
    m_pEGAlib = dlopen("libQEGA.qti.so", RTLD_LAZY);        // libQEGA.qti.so is a library inside OEM device

    if (m_pEGAlib != nullptr)
    {
        m_pSet = reinterpret_cast<PFN_EGA_SET>(dlsym(m_pEGAlib, "ega_set"));
        m_pSetData = reinterpret_cast<PFN_EGA_SET_DATA>(dlsym(m_pEGAlib, "ega_set_data"));
        m_pGetData = reinterpret_cast<PFN_EGA_GET_DATA>(dlsym(m_pEGAlib, "ega_get_data"));

        m_bLoadSuccess = m_pSet != nullptr && m_pSetData != nullptr && m_pGetData != nullptr;
        KGLogPrintf(KGLOG_INFO, "libQEGA.qti.so: %s", m_bLoadSuccess ? "Loaded" : "Not loaded");
    }
    else
    {
        KGLogPrintf(KGLOG_ERR, "libQEGA.qti.so: Not loaded");
        KGLogPrintf(KGLOG_INFO, "switch to QAPE solution");
        //KEngineAdaptiveGameFuncStep::LoadServicesLib();
        m_pQapeBindObj.reset(_InitQapeBinderObj());
        if (m_pQapeBindObj != nullptr)
        {
            m_bQapeAvailable = true;
            m_bLoadSuccess = true;
            KGLogPrintf(KGLOG_INFO, "QapeService: Loaded");
        }
        else
        {
            KGLogPrintf(KGLOG_ERR, "QapeService: Not loaded");
        }
    }

    for (int idx = 0; idx < 7; idx++)
    {
        m_vFrameState.push_back({(KAdaptiveGameFrameStateID)idx, 0.0});
    }

    for (int idx = 0; idx < 3; idx++)
    {
        m_vServiceConfig.push_back({(KAdaptiveGameServiceConfigID)idx, 0.0});
    }
}

KAdaptiveGameClient::~KAdaptiveGameClient()
{
    if (m_pEGAlib != nullptr)
    {
        dlclose(m_pEGAlib);
    }

    if (m_pQapeBindObj != nullptr)
    {
        m_pQapeBindObj.reset();
    }
}

BOOL KAdaptiveGameClient::Init(int version, int targetFPS, uint8_t* metaData, int length, const char* pkgName)
{
    BOOL bResult = FALSE;
    BOOL bRetCode = FALSE;
    std::vector<double> vData;

    KGLOG_PROCESS_ERROR(m_bLoadSuccess && "ERROR_SERVICE_NA");
    KGLOG_PROCESS_ERROR(pkgName != nullptr);

    vData.resize(3 + (length >> 3) + 1);
    vData[0] = version;
    vData[1] = targetFPS;
    vData[2] = length;

    if (metaData != nullptr)
    {
        double* p = &vData[0] + 3;
        memcpy(p, metaData, length * sizeof(char));
    }

    bRetCode = _CommonSetData(Cmd_AppInit, vData);
    KGLOG_PROCESS_ERROR(bRetCode);

    m_sPackageName = pkgName;
    bResult = TRUE;
Exit0:
    return bResult;
}

void KAdaptiveGameClient::UnInit()
{
    _CommonSet(Cmd_AppEnd);
}

BOOL KAdaptiveGameClient::FrameMove()
{
    BOOL bResult = FALSE;
    BOOL bRetCode = FALSE;

    KGLOG_PROCESS_ERROR(m_bLoadSuccess);

    bRetCode = _QueryFrameStates(m_vFrameState.data(), KAdaptiveGameFrameStateID::CPU_USAGE, KAdaptiveGameFrameStateID::THERMAL);
    KGLOG_PROCESS_ERROR(bRetCode);

    bRetCode = _QueryAllServiceConfigs(m_vServiceConfig.data());
    KGLOG_PROCESS_ERROR(bRetCode);
    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL KAdaptiveGameClient::GameStart()
{
    BOOL bResult = FALSE;
    BOOL bRetCode = FALSE;

    bRetCode = _CommonSet(Cmd_GameStart);
    KGLOG_PROCESS_ERROR(bRetCode);
    bResult = TRUE;
Exit0:
    return bResult;
}


BOOL KAdaptiveGameClient::GameStop()
{
    BOOL bResult = FALSE;
    BOOL bRetCode = FALSE;

    KGLOG_PROCESS_ERROR(m_bLoadSuccess && "ERROR_SERVICE_NA");

    bRetCode = _CommonSet(Cmd_GameStop);
    KGLOG_PROCESS_ERROR(bRetCode);

    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL KAdaptiveGameClient::RegisterThread(int tid, int tidGroup, int priority)
{
    BOOL bResult = FALSE;
    BOOL bRetCode = FALSE;
    std::vector<double> data;

    KGLOG_ASSERT_EXIT(m_bLoadSuccess);

    data.push_back(tid);
    data.push_back(tidGroup);
    data.push_back(priority);

    bRetCode = _CommonSetData(Cmd_RegisterThread, data);
    KGLOG_PROCESS_ERROR(bRetCode);

    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL KAdaptiveGameClient::UnRegisterThread(int tid)
{
    BOOL bResult = FALSE;
    BOOL bRetCode = FALSE;
    std::vector<double> data;

    KGLOG_ASSERT_EXIT(m_bLoadSuccess);

    bRetCode = _CommonSetData(Cmd_UnRegisterThread, data);
    KGLOG_PROCESS_ERROR(bRetCode);

    bResult = TRUE;
Exit0:
    return bResult;
}

std::string KAdaptiveGameClient::GetMsg()
{
    FrameMove();
    char buffer[256];
    sprintf(buffer, "CPU: %.2f, GPU: %.2f, Temp: %.1f, CURRENT(mA): %.1f, THERMAL: %.1f, MaxTemp: %.1f, MaxCurrent: %.1f, AvgCurrent: %.1f",
        m_vFrameState[1].Value, m_vFrameState[2].Value, m_vFrameState[3].Value, m_vFrameState[4].Value, m_vFrameState[5].Value, m_vServiceConfig[0].Value, m_vServiceConfig[1].Value, m_vServiceConfig[2].Value);
    std::string msg = buffer;
    return msg;
}

BOOL KAdaptiveGameClient::BoostCpu(int32_t nBoostValue)
{
    BOOL bResult = FALSE;
    BOOL bRetCode = FALSE;
    int  nRetCode = 0;
    QapeService::Status eStatus = QapeService::FAILURE;

    KGLOG_PROCESS_ERROR(m_bLoadSuccess && "ERROR_SERVICE_NA");

    bRetCode = _QapeSetPkg();
    KGLOG_PROCESS_ERROR(bRetCode);

//    eStatus = m_pQapeBindObj->boostCpu(nBoostValue, nRetCode);
//    KGLOG_PROCESS_ERROR(eStatus == QapeService::Status::SUCCESS);
//    KGLOG_PROCESS_ERROR(nRetCode == 0);

    bResult = TRUE;
Exit0:
    PrintInternalError(nRetCode);
    return bResult;
}

BOOL KAdaptiveGameClient::BoostGpu(int32_t nBoostValue)
{
    BOOL bResult = FALSE;
    BOOL bRetCode = FALSE;
    int nRetCode = 0;
    QapeService::Status eStatus = QapeService::FAILURE;

    KGLOG_PROCESS_ERROR(m_bLoadSuccess && "ERROR_SERVICE_NA");

    bRetCode = _QapeSetPkg();
    KGLOG_PROCESS_ERROR(bRetCode);

//    eStatus = m_pQapeBindObj->boostGpu(nBoostValue, nRetCode);
//    KGLOG_PROCESS_ERROR(eStatus == QapeService::Status::SUCCESS);
//    KGLOG_PROCESS_ERROR(nRetCode == 0);

    bResult = TRUE;
Exit0:
    PrintInternalError(nRetCode);
    return bResult;
}

BOOL KAdaptiveGameClient::HintLowLatency(int nTid)
{
    BOOL bResult = FALSE;
    BOOL bRetCode = FALSE;
    int nRetCode = 0;
    QapeService::Status eStatus = QapeService::FAILURE;

    KGLOG_PROCESS_ERROR(m_bLoadSuccess && "ERROR_SERVICE_NA");

    bRetCode = _QapeSetPkg();
    KGLOG_PROCESS_ERROR(bRetCode);

//    eStatus = m_pQapeBindObj->hintLowLatency(nTid, nRetCode);
//    KGLOG_PROCESS_ERROR(eStatus == QapeService::Status::SUCCESS);
//    KGLOG_PROCESS_ERROR(nRetCode == 0);

    bResult = TRUE;
Exit0:
    PrintInternalError(nRetCode);
    return bResult;
}

BOOL KAdaptiveGameClient::HintHighCpuUtil(int nTid)
{
    BOOL bResult = FALSE;
    BOOL bRetCode = FALSE;
    int nRetCode = 0;
    QapeService::Status eStatus = QapeService::FAILURE;

    KGLOG_PROCESS_ERROR(m_bLoadSuccess && "ERROR_SERVICE_NA");

    bRetCode = _QapeSetPkg();
    KGLOG_PROCESS_ERROR(bRetCode);

//    eStatus = m_pQapeBindObj->hintHighCpuutil(nTid, nRetCode);
//    KGLOG_PROCESS_ERROR(eStatus == QapeService::Status::SUCCESS);
//    KGLOG_PROCESS_ERROR(nRetCode == 0);

    bResult = TRUE;
Exit0:
    PrintInternalError(nRetCode);
    return bResult;
}

BOOL KAdaptiveGameClient::HintLowCpuUtil(int nTid)
{
    BOOL bResult = FALSE;
    BOOL bRetCode = FALSE;
    int nRetCode = 0;
    QapeService::Status eStatus = QapeService::FAILURE;

    KGLOG_PROCESS_ERROR(m_bLoadSuccess && "ERROR_SERVICE_NA");

    bRetCode = _QapeSetPkg();
    KGLOG_PROCESS_ERROR(bRetCode);

//    eStatus = m_pQapeBindObj->hintLowCpuutil(nTid, nRetCode);
//    KGLOG_PROCESS_ERROR(eStatus == QapeService::Status::SUCCESS);
//    KGLOG_PROCESS_ERROR(nRetCode == 0);

    bResult = TRUE;
Exit0:
    PrintInternalError(nRetCode);
    return bResult;
}

BOOL KAdaptiveGameClient::HintThreadPipeline(int* pTid, int nSize)
{
    BOOL bResult = FALSE;
    BOOL bRetCode = FALSE;
    int nRetCode = 0;
    QapeService::Status eStatus = QapeService::FAILURE;

    KGLOG_PROCESS_ERROR(m_bLoadSuccess && "ERROR_SERVICE_NA");

    bRetCode = _QapeSetPkg();
    KGLOG_PROCESS_ERROR(bRetCode);

//    eStatus = m_pQapeBindObj->hintThreadPipeline(pTid, nSize, nRetCode);
//    KGLOG_PROCESS_ERROR(eStatus == QapeService::Status::SUCCESS);
//    KGLOG_PROCESS_ERROR(nRetCode == 0);

    bResult = TRUE;
Exit0:
    PrintInternalError(nRetCode);
    return bResult;
}

void test(QAPEThermalZone zone)
{
    KGLogPrintf(KGLOG_INFO, "Thermal zone: %d", zone);
}

BOOL KAdaptiveGameClient::ReleaseThreadHint(KThreadHintCategory eCategory, int nTid)
{
    BOOL bResult = FALSE;
    BOOL bRetCode = FALSE;
    int nRetCode = 0;
    QapeService::Status eStatus = QapeService::FAILURE;
    QapeThermalZoneCallbackPtr funcPtr = &test;

    KGLOG_PROCESS_ERROR(m_bLoadSuccess && "ERROR_SERVICE_NA");

    bRetCode = _QapeSetPkg();
    KGLOG_PROCESS_ERROR(bRetCode);

//    eStatus = m_pQapeBindObj->releaseThreadHints((int32_t)eCategory, nTid, nRetCode);
//    KGLOG_PROCESS_ERROR(eStatus == QapeService::Status::SUCCESS);
//    KGLOG_PROCESS_ERROR(nRetCode == 0);


//    m_pQapeBindObj->registerThermalZoneCallback(funcPtr, nRetCode);

    bResult = TRUE;
Exit0:
    PrintInternalError(nRetCode);
    return bResult;
}

int32_t KAdaptiveGameClient::GetGpuHeadroom(int32_t nDuration, bool bAverage)
{
    BOOL bRetCode = FALSE;
    int32_t nResult = -1;
    QapeService::Status eStatus = QapeService::FAILURE;

    KGLOG_PROCESS_ERROR(m_bLoadSuccess && "ERROR_SERVICE_NA");

    bRetCode = _QapeSetPkg();
    KGLOG_PROCESS_ERROR(bRetCode);

//    eStatus = m_pQapeBindObj->getGpuHeadroom(nDuration, bAverage, nResult);
//    KGLOG_PROCESS_ERROR(eStatus == QapeService::Status::SUCCESS);
//    KGLOG_PROCESS_ERROR(nResult >= 0);

Exit0:
    if (nResult == -1)
    {
        KGLogPrintf(KGLOG_ERR, "Failed to request GPU headroom");
    }
    return nResult;
}

int32_t KAdaptiveGameClient::GetCpuHeadroom(KCpuHeadroomType eType, int32_t nNum, int32_t nDuration)
{
    BOOL bRetCode = FALSE;
    int32_t nResult = -1;
    QapeService::Status eStatus = QapeService::FAILURE;

    KGLOG_PROCESS_ERROR(m_bLoadSuccess && "ERROR_SERVICE_NA");

    bRetCode = _QapeSetPkg();
    KGLOG_PROCESS_ERROR(bRetCode);

//    eStatus = m_pQapeBindObj->getCpuHeadroom(CpuHeadroomTypeConvert(eType), nNum, nDuration, nResult);
//    KGLOG_PROCESS_ERROR(eStatus == QapeService::Status::SUCCESS);

Exit0:
    if (nResult == -1)
    {
        KGLogPrintf(KGLOG_ERR, "Failed to request CPU headroom");
    }
    return nResult;
}

KQAPEThermalHeadroomStatus KAdaptiveGameClient::QueryThermalHeadroomStatus()
{
    BOOL bRetCode = FALSE;
    int32_t nResult = 0;
    QapeService::Status eStatus = QapeService::FAILURE;

    KGLOG_PROCESS_ERROR(m_bLoadSuccess && "ERROR_SERVICE_NA");

    bRetCode = _QapeSetPkg();
    KGLOG_PROCESS_ERROR(bRetCode);

//    eStatus = m_pQapeBindObj->queryThermalHeadroomStatus(nResult);
//    KGLOG_PROCESS_ERROR(eStatus == QapeService::Status::SUCCESS);
//    KGLOG_PROCESS_ERROR(nResult >= 0);

Exit0:
    if (nResult == -1)
    {
        KGLogPrintf(KGLOG_ERR, "Failed to apply hint");
    }
    else if(nResult == -2)
    {
        KGLogPrintf(KGLOG_ERR, "Failed to establish session; check that setPkg has been called before");
    }
    return (KQAPEThermalHeadroomStatus)nResult;
}

int KAdaptiveGameClient::GetPipelineThreadMaxNumber()
{
    BOOL bRetCode = FALSE;
    int nResult = 0;

    KGLOG_PROCESS_ERROR(m_bLoadSuccess && "ERROR_SERVICE_NA");

    bRetCode = _QapeSetPkg();
    KGLOG_PROCESS_ERROR(bRetCode);

//    KGLOG_PROCESS_ERROR(m_pQapeBindObj->getPipelineThreadMaxNumber(nResult) == QapeService::Status::SUCCESS);
//    KGLOG_PROCESS_ERROR(nResult >= 0);

Exit0:
    if (nResult == -1)
    {
        KGLogPrintf(KGLOG_ERR, "Failed to get pipeline thread max number");
    }
    return nResult;
}

BOOL KAdaptiveGameClient::SetPipelineNumber(int nNumber)
{
    BOOL bResult = FALSE;
    BOOL bRetCode = FALSE;
    int nRetCode = 0;
    QapeService::Status eStatus = QapeService::FAILURE;

    KGLOG_PROCESS_ERROR(m_bLoadSuccess && "ERROR_SERVICE_NA");

    bRetCode = _QapeSetPkg();
    KGLOG_PROCESS_ERROR(bRetCode);

//    eStatus = m_pQapeBindObj->setPipelineNumber(nNumber, nRetCode);
//    KGLOG_PROCESS_ERROR(eStatus == QapeService::Status::SUCCESS);
//    KGLOG_PROCESS_ERROR(nRetCode == 0);

    bResult = TRUE;
Exit0:
    PrintInternalError(nRetCode);
    return bResult;
}

BOOL KAdaptiveGameClient::ResetPipelineNumber()
{
    BOOL bResult = FALSE;
    BOOL bRetCode = FALSE;
    int nRetCode = 0;
    QapeService::Status eStatus = QapeService::FAILURE;

    KGLOG_PROCESS_ERROR(m_bLoadSuccess && "ERROR_SERVICE_NA");

    bRetCode = _QapeSetPkg();
    KGLOG_PROCESS_ERROR(bRetCode);

//    eStatus = m_pQapeBindObj->resetPipelineNumber(nRetCode);
//    KGLOG_PROCESS_ERROR(eStatus == QapeService::Status::SUCCESS);
//    KGLOG_PROCESS_ERROR(nRetCode == 0);

    bResult = TRUE;
Exit0:
    PrintInternalError(nRetCode);
    return bResult;
}

BOOL KAdaptiveGameClient::SetDesiredContentRate(KQAPESupportedFrameRate eFrameRate)
{
    BOOL bResult = FALSE;
    BOOL bRetCode = FALSE;
    int nRetCode = 0;
    QapeService::Status eStatus = QapeService::FAILURE;

    KGLOG_PROCESS_ERROR(m_bLoadSuccess && "ERROR_SERVICE_NA");

    bRetCode = _QapeSetPkg();
    KGLOG_PROCESS_ERROR(bRetCode);

//    eStatus = m_pQapeBindObj->setDesiredContentRate((QAPESupportedFrameRate)eFrameRate, nRetCode);
//    KGLOG_PROCESS_ERROR(eStatus == QapeService::Status::SUCCESS);
//    KGLOG_PROCESS_ERROR(nRetCode == 0);

    bResult = TRUE;
Exit0:
    PrintInternalError(nRetCode);
    return bResult;
}

KFrameState KAdaptiveGameClient::GetFrameState()
{
    KFrameState frameState{};
    BOOL bRetCode = FALSE;

    KGLOG_PROCESS_ERROR(m_bLoadSuccess && "ERROR_SERVICE_NA");

    bRetCode = FrameMove();
    KGLOG_PROCESS_ERROR(bRetCode);

    frameState.CPU_USAGE = m_vFrameState[1].Value;
    frameState.GPU_USAGE = m_vFrameState[2].Value;
    frameState.TEMPERATURE = m_vFrameState[3].Value;
    frameState.CURRENT = m_vFrameState[4].Value;
    frameState.THERMAL = m_vFrameState[5].Value;
    frameState.MaxTemperature = m_vServiceConfig[0].Value;
    frameState.MaxCurrent = m_vServiceConfig[1].Value;
    frameState.AvgCurrent = m_vServiceConfig[2].Value;

Exit0:
    return frameState;
}

BOOL KAdaptiveGameClient::_CommonSet(int cmdId)
{
    BOOL bResult = FALSE;
    int nRetCode = -1;

    if (m_bQapeAvailable)
    {
        m_pQapeBindObj->appSet(m_uGameID, cmdId, nRetCode);
        KGLOG_PROCESS_ERROR(nRetCode == 0);
    }
    else if (m_pSet != nullptr)
    {
        nRetCode = m_pSet(m_uGameID, cmdId);
        KGLOG_PROCESS_ERROR(nRetCode == 0);
    }
    else
    {
        KGLOG_PROCESS_ERROR(FALSE);
    }

    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL KAdaptiveGameClient::_CommonSetData(int cmdId, std::vector<double>& data)
{
    BOOL bResult = FALSE;
    int nRetCode = -1;

    if (m_bQapeAvailable)
    {
        m_pQapeBindObj->appSetData(m_uGameID, cmdId, data.data(), data.size(), nRetCode);
        KGLOG_PROCESS_ERROR(nRetCode == 0);
    }
    else if(m_pSetData != nullptr)
    {
        nRetCode = m_pSetData(m_uGameID, cmdId, data);
        KGLOG_PROCESS_ERROR(nRetCode == 0);
    }
    else{
        KGLOG_PROCESS_ERROR(FALSE);
    }

    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL KAdaptiveGameClient::_CommonGetData(int cmdId, std::vector<double>& data)
{
    BOOL bResult = FALSE;
    int nRetCode = -1;

    if (m_bQapeAvailable)
    {
        m_pQapeBindObj->appGetData(m_uGameID, cmdId, data.data(), data.size(), nRetCode);
        KGLOG_PROCESS_ERROR(nRetCode == 0);
    }
    else if (m_pGetData != nullptr)
    {
        nRetCode = m_pGetData(m_uGameID, cmdId, data);
        KGLOG_PROCESS_ERROR(nRetCode == 0);
    }
    else
    {
        KGLOG_PROCESS_ERROR(FALSE);
    }

    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL KAdaptiveGameClient::_QueryFrameStates(KAdaptiveGameFrameState* frameState, KAdaptiveGameFrameStateID startState, KAdaptiveGameFrameStateID endState)
{
    BOOL bResult = FALSE;
    std::vector<double> data;
    data.reserve((int)endState - (int)startState + 1);
    for (int idx = (int)startState; idx < (int)endState; idx++)
    {
        data.push_back(idx);
    }

    int nRetCode = _CommonGetData(Cmd_QueryFrameState, data);
    KGLOG_PROCESS_ERROR(nRetCode);

    {
        int readIdx = 0;
        for (int idx = (int)startState; idx < (int)endState; idx++)
        {
            frameState[idx].Value = data[readIdx++];
        }
    }

    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL KAdaptiveGameClient::_QueryAllServiceConfigs(KAdaptiveGameServiceConfig* serviceConfig)
{
    BOOL bResult = FALSE;
    BOOL bRetCode = FALSE;
    std::vector<double> data;

    data.reserve((int)KAdaptiveGameServiceConfigID::DC_SIZE);
    for (int idx = 0; idx < (int)KAdaptiveGameServiceConfigID::DC_SIZE; idx++)
    {
        data.push_back(idx);
    }

    bRetCode = _CommonGetData(Cmd_QueryServiceConfig, data);
    KGLOG_PROCESS_ERROR(bRetCode);

    for (int idx = 0; idx < (int)KAdaptiveGameServiceConfigID::DC_SIZE; idx++)
    {
        serviceConfig[idx].Value = data[idx];
    }

    bResult = TRUE;
Exit0:
    return bResult;
}

QapeService* KAdaptiveGameClient::_InitQapeBinderObj()
{
    std::string qapeDescriptor = "vendor.qti.snapdragonServices.qape.IQape/default";
    // PFN_SERVICEADK_GET_SNAPDRAGON_SERVICE pService = reinterpret_cast<PFN_SERVICEADK_GET_SNAPDRAGON_SERVICE>(KEngineAdaptiveGameFuncStep::GetServicesAdkFunction("getSnapdragonService"));
    ::ndk::SpAIBinder* pQapeBinderObj = getSnapdragonService(qapeDescriptor.c_str());
//    ::ndk::SpAIBinder* pQapeBinderObj = pService(qapeDescriptor.c_str());
    if (pQapeBinderObj == nullptr)
    {
        KGLogPrintf(KGLOG_ERR, "QapeService: Not loaded");
        return nullptr;
    }

    auto* pQapeService = new QapeService();
    if (pQapeService->connectToQapeService(pQapeBinderObj) == QapeService::SUCCESS)
    {
        KGLogPrintf(KGLOG_INFO, "QapeService: Loaded");
    }
    else
    {
        delete pQapeService;
        pQapeService = nullptr;
        KGLogPrintf(KGLOG_ERR, "QapeService: Failed to connect to Qape Service");
        return nullptr;
    }

    if (pQapeService->linkToDeath(pQapeBinderObj, DeathNotificationCallback) == QapeService::SUCCESS)
    {
        KGLogPrintf(KGLOG_INFO, "QapeService: Link to death success");
    }
    else
    {
        delete pQapeService;
        pQapeService = nullptr;
        KGLogPrintf(KGLOG_ERR, "QapeService: Link to death failed");
        return nullptr;
    }
    return pQapeService;
}

BOOL KAdaptiveGameClient::_QapeSetPkg()
{
    BOOL bResult = FALSE;
    QapeService::Status eStatus = QapeService::FAILURE;

    KGLOG_PROCESS_ERROR(m_bQapeAvailable && "ERROR_SERVICE_NA");
    {
        int ret = 0;
//        eStatus = m_pQapeBindObj->setPkg(m_sPackageName.c_str(), ret);
//        KGLOG_PROCESS_ERROR(eStatus == QapeService::Status::SUCCESS);
//        KGLOG_PROCESS_ERROR(ret == 0);
    }

    bResult = TRUE;
Exit0:
    return bResult;
}

void DeathNotificationCallback(void* cookie)
{
    KGLogPrintf(KGLOG_INFO, "QAPE service death callback");
    s_bQapeBinderNeedReconnect = true;
    s_nQapeBinderReconnectCount = 0;
}

void PrintInternalError(int nRetCode)
{
    switch (nRetCode)
    {
        case -1:
            KGLogPrintf(KGLOG_ERR, "Unsupported param command");
            break;
        case -2:
            KGLogPrintf(KGLOG_ERR, "Generic error");
            break;
        case -3:
            KGLogPrintf(KGLOG_ERR, "Failed to request boost; exceeded the allowed boost count (3) in 10 seconds");
            break;
    }
}

QAPECpuHeadroomType CpuHeadroomTypeConvert(KCpuHeadroomType eType)
{
    switch (eType)
    {
        case KCpuHeadroomType::CPU_CORE:
            return QAPECpuHeadroomType::CORE;
        case KCpuHeadroomType::CPU_CLUSTER:
            return QAPECpuHeadroomType::CLUSTER;
        default:
            return QAPECpuHeadroomType::TOTAL;
    }
}

namespace NSKBase
{
    KAdaptiveGameClient* GetAdaptiveGameClient()
    {
        static KAdaptiveGameClient sAdaptiveGameClient;
        return &sAdaptiveGameClient;
    }

    void BoostCpu(int32_t nBoostValue)
    {
        GetAdaptiveGameClient()->BoostCpu(nBoostValue);
    }

    void BoostGpu(int32_t nBoostValue)
    {
        GetAdaptiveGameClient()->BoostGpu(nBoostValue);
    }

    void HintLowLatency(int nTid)
    {
        GetAdaptiveGameClient()->HintLowLatency(nTid);
    }

    void HintHighCpuUtil(int nTid)
    {
        GetAdaptiveGameClient()->HintHighCpuUtil(nTid);
    }

    void HintLowCpuUtil(int nTid)
    {
        GetAdaptiveGameClient()->HintLowCpuUtil(nTid);
    }

    void HintThreadPipeline(int* pTid, int nSize)
    {
        GetAdaptiveGameClient()->HintThreadPipeline(pTid, nSize);
    }

    void ReleaseThreadHint(KThreadHintCategory eCategory, int nTid)
    {
        GetAdaptiveGameClient()->ReleaseThreadHint(eCategory, nTid);
    }

    int32_t GetGpuHeadroom(int32_t nDuration, bool bAverage)
    {
        return GetAdaptiveGameClient()->GetGpuHeadroom(nDuration, bAverage);
    }

    int32_t GetCpuHeadroom(KCpuHeadroomType eType, int32_t nNum, int32_t nDuration)
    {
        return GetAdaptiveGameClient()->GetCpuHeadroom(eType, nNum, nDuration);
    }

    KQAPEThermalHeadroomStatus QueryThermalHeadroomStatus()
    {
        return GetAdaptiveGameClient()->QueryThermalHeadroomStatus();
    }

    int GetPipelineThreadMaxNumber()
    {
        return GetAdaptiveGameClient()->GetPipelineThreadMaxNumber();
    }

    void SetPipelineNumber(int nNumber)
    {
        GetAdaptiveGameClient()->SetPipelineNumber(nNumber);
    }

    void ResetPipelineNumber()
    {
        GetAdaptiveGameClient()->ResetPipelineNumber();
    }

    void SetDesiredContentRate(KQAPESupportedFrameRate eFrameRate)
    {
        GetAdaptiveGameClient()->SetDesiredContentRate(eFrameRate);
    }

    KFrameState GetFrameState()
    {
        return GetAdaptiveGameClient()->GetFrameState();
    }
}
#endif
#endif