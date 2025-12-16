#pragma once
#include "KEnginePub/Public/KEngineOptionBase.h"
using namespace NSEngineOptionBase;

struct EngineOption_LogicOnly
{//逻辑线程专用 可读可写
    //Property_LogicRW<类型, 是否被GpuSwitchOptionTab.tab表值控制> 变量名 = { 初始值, "名字（用于在GpuSwitchOptionTab.tab表找值 以及 trace调试功能 }
    Property_LogicRW<bool, false> example = { true };
};

//----------------------------------------------------------------------------
//-------------------from logic thread to logic thread------------------------

//需要从逻辑线程自动同步到渲染线程的结构体需要继承 L2RSyncControler
struct EOL2R_GlobalSwitch :public L2RSyncControler
{
    //Property_LogicRW_RenderR<类型，是否被GpuSwitchOptionTab.tab表值控制> 变量名 = { 初始值, 固定传一个*this用于自动同步, "名字（用于在GpuSwitchOptionTab.tab表找值 以及 trace调试功能 }
    Property_LogicRW_RenderR<bool, false> bRenderSkyBox{ true,*this, "bRenderSkyBox"};
    Property_LogicRW_RenderR<bool, false> bRenderWireFrame{ false,*this,"bRenderWireFrame"};
    Property_LogicRW_RenderR<bool, false> bRenderSceneBlockBox{ false,*this ,"bRenderSceneBlockBox"};
    Property_LogicRW_RenderR<bool, false> bEnableGSR{ false,*this, "bEnableGSR"};
    Property_LogicRW_RenderR<bool, false> bRenderModelBox{ false,*this, "bRenderModelBox"};
    Property_LogicRW_RenderR<bool, false> bRenderModelStBox{ false,*this, "bRenderModelStBox"};

    #if defined(_WIN32)
    Property_LogicRW_RenderR<bool, false> bUseSPDGenMips{ true,*this, "bUseSPDGenMips"};
    Property_LogicRW_RenderR<bool, false> bForceFSRInPS{ true ,*this, "bForceFSRInPS"};
    #else
    Property_LogicRW_RenderR<bool, false> bUseSPDGenMips{ false,*this, "bUseSPDGenMips"};
    Property_LogicRW_RenderR<bool, false> bForceFSRInPS{ true,*this, "bForceFSRInPS"};
    #endif
    //Property_LogicRW_RenderR<bool, false> bUseGSR2UltraPerf{ false ,*this, "bUseGSR2UltraPerf"};
    Property_LogicRW_RenderR<bool, false> bEnableDefferRender{ false ,*this, "bEnableDefferRender"};

    Property_LogicRW_RenderR<bool, false> bEnableRTDebuger{ false,*this, "bEnableRTDebuger"};
    Property_LogicRW_RenderR<bool, false> bShotPlayerWithAlpha{ false,*this, "bShotPlayerWithAlpha"};
    Property_LogicRW_RenderR<bool, false> bDebugBakeTerrain{ false,*this, "bDebugBakeTerrain"};
    Property_LogicRW_RenderR<bool, false> bEnablePipelineStat{ false,*this, "bEnablePipelineStat"};

    Property_LogicRW_RenderR<bool, false> bInfiniteReflect{ false,*this, "bInfiniteReflect"};
    #ifdef _WIN32
    Property_LogicRW_RenderR<bool, false> bReflectAntiFlicker{ true ,*this, "bReflectAntiFlicker"};
    #else
    Property_LogicRW_RenderR<bool, false> bReflectAntiFlicker{ false ,*this, "bReflectAntiFlicker"};
    #endif
    Property_LogicRW_RenderR<bool, false> bReflectUseMeanColor{ true ,*this, "bReflectUseMeanColor"};
};

struct EOL2R_Pssr: public L2RSyncControler
{
    Property_LogicRW_RenderR<float, false> fReflectDepthBias{ 0.002f,*this };
    Property_LogicRW_RenderR<float, false> fReflectMinDepth {200000.0f,*this };
    Property_LogicRW_RenderR<float, false> fReflectVelScale{ 2.0f,*this };
    Property_LogicRW_RenderR<float, false> fReflectDepthLerp{ 0.5f,*this };
    Property_LogicRW_RenderR<float, false> fReflecMinWeight{ 0.125f,*this };
    Property_LogicRW_RenderR<float, false> fReflectMaxWeight{ 1.0f,*this };
    Property_LogicRW_RenderR<int, false> nReflectionDepthLod{ 0,*this };
};

struct EngineOption_LogicWriteRenderRead
{//逻辑线程可读写，渲染线程只读
    EOL2R_GlobalSwitch globalSwitch;
    EOL2R_Pssr Pssr;

    //！！请将上面的成员变量添加到syncList中，将自动进行逻辑/渲染线程同步
    std::tuple<EOL2R_GlobalSwitch&, EOL2R_Pssr&> syncList = { globalSwitch,Pssr };
};
//----------------------------------------------------------------------------
//----------------------Render Thread Only------------------------------------

struct EngineOption_RenderOnly
{//渲染线程专用 可读可写
    //Property_RenderRW<类型, 是否被GpuSwitchOptionTab.tab表值控制> 变量名 = { 初始值, "名字（用于在GpuSwitchOptionTab.tab表找值 以及 trace调试功能 }
    Property_RenderRW<bool, false> m_bRenderSkyBox{ true };
};

//----------------------------------------------------------------------------
//----------------------------------------------------------------------------


struct KEngineOptions_Logic//逻辑线程的EngineOptions
{
    EngineOption_LogicOnly logic;//逻辑线程内部使用
    EngineOption_LogicWriteRenderRead toRender;//每帧同步到渲染线程
};

struct KEngineOptions_Render//渲染线程的EngineOptions
{
    EngineOption_RenderOnly render;//渲染线程内部使用，不连通imgui
    EngineOption_LogicWriteRenderRead fromLogic;//从逻辑线程同步过来，连通imgui
};

struct KEngineOptions_mutex//带锁，线程无关的公共库使用
{
    //Property_Mutex<类型, 是否被GpuSwitchOptionTab.tab表值控制> 变量名 = { 初始值, "名字（用于在GpuSwitchOptionTab.tab表找值 以及 trace调试功能 }
    Property_Mutex<bool, false> example = { true };
};

struct KEngineOptions_FileConfig //静态文件配置，只读
{
    //Property_ROnly<类型, 是否被GpuSwitchOptionTab.tab表值控制> 变量名 = { 初始值, "名字（用于在GpuSwitchOptionTab.tab表找值 以及 trace调试功能 }
    Property_ROnly<float, false> example = { 3.14f };
};

