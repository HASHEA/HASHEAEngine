////////////////////////////////////////////////////////////////////////////////
//
//  FileName    : KEngineAdaptiveGame.h
//  Creator     : Ant
//  Create Date : 2024
//
////////////////////////////////////////////////////////////////////////////////

#pragma once
#if 0
#if defined(__ANDROID__)
#include <android/binder_auto_utils.h>
#include "snapdragon_services_adk.h"
#include "snapdragon_services_qape.h"
#include "IKHeader.h"

#include <vector>
#include <memory>

enum class KAdaptiveGameFrameStateID
{
    FRAME_ID,       // 0 - FRAME_ID 当前的帧ID
    CPU_USAGE,      // 1 - CPU_USAGE 当前的CPU使用率
    GPU_USAGE,      // 2 - GPU_USAGE 当前的GPU使用率
    TEMPERATURE,    // 3 - TEMPERATURE 当前设备的表面温度，单位是毫度(1°C = 1000m°C)
    CURRENT,        // 4 - CURRENT 当前的电流 +充电 -放电 (mA)
    THERMAL,        // 5 - THERMAL 当前的热量 0 无预警 1 表示示警（可以多档示警）refer to https://developer.android.com/ndk/reference/group/thermal
                    // When ATHERMAL_STATUS_SEVERE = 3 happens, throttling triggers and can affect the CPU, GPU, and other subsystems!!!
    TARGET_FPS,     // 6 - TARGET_FPS 当前的目标帧率
    FS_SIZE = 7,
};

enum class KAdaptiveGameServiceConfigID
{
    MAXTEMPERATURE, // 0 - MAXTEMPERATURE 建议的最高温度
    MAXCURRENT,     // 1 - MAXCURRENT 建议的最高电流
    AVGCURRENT,     // 2 - AVGCURRENT 建议的平均电流

    DC_SIZE = 3,
};

enum KAdaptiveGameCmd_API
{
    Cmd_AppInit             = 1,
    Cmd_AppEnd              = 2,
    Cmd_InitGameConfig      = 3,
    Cmd_UpdateGameConfig    = 4,
    Cmd_GameStart           = 5,
    Cmd_GameStop            = 6,
    Cmd_RegisterThread      = 7,
    Cmd_UnRegisterThread    = 8,
    Cmd_SetForeground       = 9,
    Cmd_UpdateFrameState    = 10,
    Cmd_QueryFrameState     = 11,
    Cmd_QueryServiceConfig  = 13,

    //OEM feature up to OEM's implementation, ERROR_Not_Support would be return if not supported
    //Qualcomm Adaptive only pass the request to OEM's game service
    Cmd_CPUSetAffinity      = 101,
    Cmd_CPULockMaxFreq      = 102,  // if the frag value is set to -1, it means unlock
};

enum class KAdaptiveGameState
{
    STATE_OK                    = 0,    //No Error
    ERROR_UNKNOWN               = -1,   //Unknown Error
    ERROR_BAD_PARAM             = -2,   //Wrong input parameters
    ERROR_LACK_OF_DATA          = -5,   //Lack of data
    ERROR_SERVICE_NA            = -7,   //Service not available
    ERROR_NOT_SUPPORT           = -8,   //Request not supported
};

enum class KThreadHintCategory
{
    THREAD_HINT_LOW_LATENCY     = 0,    //Low latency
    THREAD_HINT_HIGH_CPU_UTIL   = 1,    //High CPU utilization
    THREAD_HINT_LOW_CPU_UTIL    = 2,    //Low CPU utilization
    THREAD_HINT_PIPELINE        = 3,    //Thread pipeline
};

enum class KQAPESupportedFrameRate : int32_t
{
    FRAME_30_FPS        = 0,
    FRAME_60_FPS        = 1,
    FRAME_90_FPS        = 2,
    FRAME_120_FPS       = 3,
    FRAME_144_FPS       = 4,
};

enum class KQAPEThermalHeadroomStatus : int32_t
{
    FAR_FROM_MITIGATION = 0,
    FROM_MITIGATION_75_PERCENT = 1,
    FROM_MITIGATION_50_PERCENT = 2,
    FROM_MITIGATION_30_PERCENT = 3,
    FROM_MITIGATION_10_PERCENT = 4,
    FROM_MITIGATION_5_PERCENT = 5,
    MITIGATION = 6,

    ERROR_APPLY_HINT = -1,
};

enum class KCpuHeadroomType : int32_t
{
    CPU_CORE = 0,
    CPU_CLUSTER = 1,
    CPU_TOTAL = 2,
};

typedef struct
{
    KAdaptiveGameFrameStateID Key;      // 需要查询的当前帧参数类型
    double Value;                       // 获取到的参数类型的值
} KAdaptiveGameFrameState;

typedef struct
{
    KAdaptiveGameServiceConfigID Key;   // 需要查询的系统参数类型
    double Value;                       // 获取到的系统参数类型的值
} KAdaptiveGameServiceConfig;

typedef uint32_t(*PFN_EGA_SET)(const uint32_t gameID, int cmdType);
typedef uint32_t(*PFN_EGA_SET_DATA)(const uint32_t gameID, int cmdType, std::vector<double>& data);
typedef uint32_t(*PFN_EGA_GET_DATA)(const uint32_t gameID, int cmdType, std::vector<double>& data);
typedef ::ndk::SpAIBinder* (*PFN_SERVICEADK_GET_SNAPDRAGON_SERVICE)(const char *name);


struct KFrameState
{
    double CPU_USAGE;           // CPU_USAGE 当前的CPU使用率
    double GPU_USAGE;           // GPU_USAGE 当前的GPU使用率
    double TEMPERATURE;         // TEMPERATURE 当前设备的表面温度，单位是毫度(1°C = 1000m°C)
    double CURRENT;             // CURRENT 当前的电流 +充电 -放电 (mA)
    double THERMAL;             // THERMAL 当前的热量 0 无预警 1 表示示警（可以多档示警）refer to https://developer.android.com/ndk/reference/group/thermal
                                // When ATHERMAL_STATUS_SEVERE = 3 happens, throttling triggers and can affect the CPU, GPU, and other subsystems!!!

    double MaxTemperature;      // MaxTemperature 建议的最高温度
    double MaxCurrent;          // MaxCurrent 建议的最高电流
    double AvgCurrent;          // AvgCurrent 建议的平均电流
};

class KAdaptiveGameClient
{
public:
    KAdaptiveGameClient();
    ~KAdaptiveGameClient();

    BOOL Init(int version, int targetFPS, uint8_t* metaData, int length, const char* pkgName);
    BOOL FrameMove();
    void UnInit();

    BOOL GameStart();
    BOOL GameStop();

    BOOL RegisterThread(int tid, int tidGroup, int priority);
    BOOL UnRegisterThread(int tid);

    std::string GetMsg();

public:
    BOOL BoostCpu(int32_t nBoostValue);
    BOOL BoostGpu(int32_t nBoostValue);
    BOOL HintLowLatency(int nTid);
    BOOL HintHighCpuUtil(int nTid);
    BOOL HintLowCpuUtil(int nTid);
    BOOL HintThreadPipeline(int* pTid, int nSize);
    BOOL ReleaseThreadHint(KThreadHintCategory eCategory, int nTid);
    int32_t GetGpuHeadroom(int32_t nDuration, bool bAverage);
    int32_t GetCpuHeadroom(KCpuHeadroomType eType, int32_t nNum, int32_t nDuration);
    KQAPEThermalHeadroomStatus QueryThermalHeadroomStatus();
    int GetPipelineThreadMaxNumber();
    BOOL SetPipelineNumber(int nNumber);
    BOOL ResetPipelineNumber();
    BOOL SetDesiredContentRate(KQAPESupportedFrameRate eFrameRate);

    KFrameState GetFrameState();
private:
    BOOL _CommonSet(int cmdId);
    BOOL _CommonSetData(int cmdId, std::vector<double>& data);
    BOOL _CommonGetData(int cmdId, std::vector<double>& data);

    BOOL _QueryFrameStates(KAdaptiveGameFrameState* frameState, KAdaptiveGameFrameStateID startState, KAdaptiveGameFrameStateID endState);
    BOOL _QueryAllServiceConfigs(KAdaptiveGameServiceConfig* serviceConfig);

    QapeService* _InitQapeBinderObj();
    QapeService* _GetBinderObj();

    BOOL _QapeSetPkg();
private:
    std::vector<KAdaptiveGameServiceConfig> m_vServiceConfig;
    std::vector<KAdaptiveGameFrameState> m_vFrameState;

    //used for quick caller ID reference for OEM/QCOM
    uint32_t m_uGameID;     // Game Developer should get this unique ID from Qualcomm, 0 is the default unregistered game
    uint64_t m_uFrameID;

    std::unique_ptr<QapeService> m_pQapeBindObj;

    void* m_pEGAlib = nullptr;
    PFN_EGA_SET m_pSet = nullptr;
    PFN_EGA_SET_DATA m_pSetData = nullptr;
    PFN_EGA_GET_DATA m_pGetData = nullptr;

    bool m_bLoadSuccess = false;
    bool m_bQapeAvailable = false;

    std::string m_sPackageName;
};

namespace NSKBase
{
    extern "C"
    {
        KAdaptiveGameClient* GetAdaptiveGameClient();

        /*
         * @brief: 为所有可用CPU按照输入的指定百分比增加虚拟利用率(用于CPU频率的计算), API 会持续调用 2s
         * @caution: 仅在游戏启动后调用, 该API在10s内只能被调用3次。
         * @usage: 适用于游戏启动后，需要提高CPU频率的场景
         * @param nBoostValue: 需要增加的CPU利用率百分比，取值范围[-50, 400]
         */
        void BoostCpu(int32_t nBoostValue);

        /*
         * @brief: 通过输入参数提升 GPU 负载的百分比, API 会持续调用 2s
         * @caution: 仅在游戏启动后调用, 该API在10s内只能被调用3次。
         * @usage: 适用于游戏启动后，需要提高GPU频率的场景. 例如, GPU 密集型游戏中的场景切换。
         * @param nBoostValue: 需要增加的GPU利用率百分比，取值范围[1, 100]
         */
        void BoostGpu(int32_t nBoostValue);

        /*
         * @brief: 标记指定线程为低延迟线程，该标记不会改变线程在性能核心和节能核心之间的位置，但会提高线程的优先级。
         * @caution: 若设置了许多的低延迟线程，会减弱低延迟效果，系统调度器不会在低延迟线程之中进一步区分优先级。线程标记在整个线程的
         * 生命周期内保持有效，直到调用 ReleaseThreadHint() 来释放线程标记或者当应用程序转入后台或者被终止时的自动释放机制释放。
         * @param nTid: 需要标记的线程ID
         */
        void HintLowLatency(int nTid);

        /*
         * @brief: 标记指定线程需要在SoC的高性能核心上运行。
         * @caution: 当线程被标记的时候，系统会优先使用高性能核心来运行线程。这可能会导致更高的能耗并影响电池寿命。
         * 任何线程标记都只在线程的生命周期内保持有效，直到调用 ReleaseThreadHint() 来释放线程标记或者当应用程序转入后台或者被终止时的自动释放机制释放。
         * @usage: 该标记对于运行重负载的关键线程非常有用，该标记可以帮助调度器确保这些线程在高性能核心上运行。不过由于标记后的线程是被尝试移到高性能核心上，
         * 若当前已有线程在此核心上运行，可能无法得到预期的性能提升，甚至可能会导致性能下降(线程间的资源竞争)。
         * @param nTid: 需要标记的线程ID
         */
        void HintHighCpuUtil(int nTid);

        /*
         * @brief: 标记指定线程不需要在高性能核心上运行。
         * @caution: 任何被标记的线程都不会在大核上运行，任何线程标记都只在线程的生命周期内保持有效，直到调用 ReleaseThreadHint()
         * 来释放线程标记或者当应用程序转入后台或者被终止时的自动释放机制释放。
         * @usage: HintLowCpuUtil 和 HintHighCpuUtil 与 thread affinity 不同，当使用 thread affinity 时，开发者
         * 需要识别线程可以运行的集群配置和核心。而 HintLowCpuUtil 和 HintHighCpuUtil 线程可以在任何核心上运行，但是当核心已有
         * 运行的线程，被标记的线程仍需要在队列中等待。
         * @param nTid: 需要标记的线程ID
         */
        void HintLowCpuUtil(int nTid);

        /*
         * @brief: 标记一组需要在绘制周期中需要运行的线程列表，以提示底层系统要以更高优先级来调度这些线程。在每个渲染周期的Vsync阶段的到SoC系统的保证。
         * @param nTid: 需要标记的线程ID列表
         * @param nSize: 线程ID列表的大小, 最大值取决当前平台支持的最大线程数。
         */
        void HintThreadPipeline(int* pTid, int nSize);

        /*
         * @brief: 释放线程标记，释放之前通过 HintLowLatency, HintHighCpuUtil, HintLowCpuUtil, HintThreadPipeline 设置的线程标记
         * 将线程恢复到默认状态。
         * @param eCategory: 需要释放的线程标记类型
         * THREAD_HINT_LOW_LATENCY = 0,   // 低延迟线程
         * THREAD_HINT_HIGH_CPU_UTIL = 1, // 高CPU利用率线程
         * THREAD_HINT_LOW_CPU_UTIL = 2,  // 低CPU利用率线程
         * THREAD_HINT_PIPELINE = 3,      // Pipeline线程组
         * 如果参数设置为 THREAD_HINT_PIPELINE, 则会移除所有通过 HintThreadPipeline 设置的线程标记，而 nTid 参数会被忽略。
         * @param nTid: 需要释放标记的线程ID
         */
        void ReleaseThreadHint(KThreadHintCategory eCategory, int nTid);

        /*
         * @brief: 获取过去 duration 秒内的 平均或者最小的 GPU 余量百分比。 API 的目的时提供GPU性能信息，来帮助判断工作负载
         * 是否在SoC上受到了GPU的限制。
         * @caution: 仅在游戏启动后调用, 当设备进入热保护时，会影响到 GPU 余量的运算。
         * @param nDuration: 需要查询的时间段，单位为秒，取值范围[1, 60]
         * @param bAverage: 是否返回平均值，true 返回平均值，false 返回最小值
         * @return: 返回 GPU 余量百分比, 小于 0 表示获取失败
         */
        int32_t GetGpuHeadroom(int32_t nDuration, bool bAverage);

        /*
         * @brief: 获取过去 duration 秒内 单核/集群 CPU 余量百分比。 API 提供运行时CPU的性能，来帮助判断是否进入CPU瓶颈
         * @caution：仅在游戏启动后调用
         * @param eType: 查询的CPU的类型, 可选值为 CPU_CORE, CPU_CLUSTER, CPU_TOTAL
         * @param nNum: 若eType参数设置为 CPU_CORE 或者 CPU_CLUSTER, 则该参数为CPU核心或者集群的编号, 当 eType 设置为
         * CPU_TOTAL 时，该参数无效.
         * @param nDuration: 需要查询的时间段，单位为秒，取值范围[1, 60]
         * @return: 返回 CPU 余量百分比, 小于 0 表示获取失败
         */
        int32_t GetCpuHeadroom(KCpuHeadroomType eType, int32_t nNum, int32_t nDuration);

        /*
         * @brief: 查询当前的热量状态, 帮助了解在当前的运行情况下，还有多少性能潜力可以被利用在不超出当前设备的热限制下.
         * @caution: 仅在游戏启动后调用
         * @return: 返回当前的热量状态, 可选值为
         * FAR_FROM_MITIGATION = 0,                // 远未达到热限制
         * FROM_MITIGATION_75_PERCENT = 1,         // 尚有 75% 的余量到达热限制
         * FROM_MITIGATION_50_PERCENT = 2,         // 尚有 50% 的余量到达热限制
         * FROM_MITIGATION_30_PERCENT = 3,         // 尚有 30% 的余量到达热限制
         * FROM_MITIGATION_10_PERCENT = 4,         // 尚有 10% 的余量到达热限制
         * FROM_MITIGATION_5_PERCENT = 5,          // 尚有 5% 的余量到达热限制
         * MITIGATION = 6,                         // 已经达到热限制
         * ERROR_APPLY_HINT = -1,                  // 获取失败
         */
        KQAPEThermalHeadroomStatus QueryThermalHeadroomStatus();

        /*
         * @brief: 获取当前设备支持的最大 Pipeline 线程数，结果取决于当前平台
         * @return: 返回当前设备支持的最大 Pipeline 线程数， -1 表示获取失败
         */
        int GetPipelineThreadMaxNumber();

        /*
         * @brief: 设置 Pipeline 的线程数，用来控制HintThreadPipeline的线程数，不得超过GetPipelineThreadMaxNumber返回的值
         * @param nNumber: 需要设置的 Pipeline 线程数，取值范围[1, GetPipelineThreadMaxNumber()]
         */
        void SetPipelineNumber(int nNumber);

        /*
         * @brief: 重置 Pipeline 的线程数至 0
         */
        void ResetPipelineNumber();

        /*
         * @brief: 主要影响的是scheduler计算负载的窗口，rate越高，计算出来的负载更激进(人话应该是通过期望帧率来调整计算系统负载时的时间窗口，
         * 更高的帧率意味着要在更短的时间处理更多的数据，系统也要更频繁的进行任务分配和资源管理。目前接口反应到应用程序上会延迟10-20s)
         * @param eFrameRate: 可选值为 FRAME_30_FPS, FRAME_60_FPS, FRAME_90_FPS, FRAME_120_FPS, FRAME_144_FPS
         */
        void SetDesiredContentRate(KQAPESupportedFrameRate eFrameRate);

        /*
         * @brief: 获取当前帧的状态信息
         * @return: 返回当前帧的状态信息
         * KFrameState :
         * CPU_USAGE 当前的CPU使用率
         * GPU_USAGE 当前的GPU使用率
         * TEMPERATURE 当前设备的表面温度，单位是毫度(1°C = 1000m°C)
         * CURRENT 当前的电流 +充电 -放电(mA)
         * THERMAL 当前的热量 0 无预警 1 表示示警（可以多档示警）refer to https://developer.android.com/ndk/reference/group/thermal
         * MaxTemperature 建议的最高温度
         * MaxCurrent 建议的最高电流
         * AvgCurrent 建议的平均电流
         */
        KFrameState GetFrameState();
    }
}
#endif
#endif