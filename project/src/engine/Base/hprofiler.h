#pragma once
// AshEngine CPU profile facade (Tracy-backed).
//
// 用法（仅在 .cpp 中 include 本头，避免污染公共头）:
//   ASH_PROFILE_FRAME();                       // 主循环每帧 present 之后调用一次
//   ASH_PROFILE_THREAD("Render");              // 线程命名（每线程一次）
//   ASH_PROFILE_SCOPE();                       // 函数级 zone，名字取自 __FUNCTION__
//   ASH_PROFILE_SCOPE_N("BuildVisibleFrame");  // 显式名字
//   ASH_PROFILE_SCOPE_NC("BasePass", 0xFF8000) // 显式名字 + 颜色 (0xRRGGBB)
//   ASH_PROFILE_PLOT("DrawCalls", value);      // 时序数值（int64/float/double）
//   ASH_PROFILE_MESSAGE("loaded sponza");      // 文本事件
//   ASH_PROFILE_TAG("Backend", "Vulkan");      // frame tag
//
// 当未定义 TRACY_ENABLE 时所有宏都是 no-op。

#if defined(TRACY_ENABLE)
    #include <tracy/Tracy.hpp>

    #define ASH_PROFILE_FRAME()                FrameMark
    #define ASH_PROFILE_FRAME_N(name)          FrameMarkNamed(name)
    #define ASH_PROFILE_FRAME_START(name)      FrameMarkStart(name)
    #define ASH_PROFILE_FRAME_END(name)        FrameMarkEnd(name)

    #define ASH_PROFILE_THREAD(name)           tracy::SetThreadName(name)

    #define ASH_PROFILE_SCOPE()                ZoneScoped
    #define ASH_PROFILE_SCOPE_N(name)          ZoneScopedN(name)
    #define ASH_PROFILE_SCOPE_C(color)         ZoneScopedC(color)
    #define ASH_PROFILE_SCOPE_NC(name, color)  ZoneScopedNC(name, color)

    #define ASH_PROFILE_SCOPE_TEXT(text, size) ZoneText(text, size)

    #define ASH_PROFILE_PLOT(name, value)      TracyPlot(name, value)

    #define ASH_PROFILE_MESSAGE(msg)           TracyMessageL(msg)
    #define ASH_PROFILE_MESSAGE_C(msg, color)  TracyMessageLC(msg, color)

    #define ASH_PROFILE_TAG(key, value)        TracyMessageL(key ": " value)

    #define ASH_PROFILE_ALLOC(ptr, size)       TracyAlloc(ptr, size)
    #define ASH_PROFILE_FREE(ptr)              TracyFree(ptr)
#else
    #define ASH_PROFILE_FRAME()                ((void)0)
    #define ASH_PROFILE_FRAME_N(name)          ((void)0)
    #define ASH_PROFILE_FRAME_START(name)      ((void)0)
    #define ASH_PROFILE_FRAME_END(name)        ((void)0)

    #define ASH_PROFILE_THREAD(name)           ((void)0)

    #define ASH_PROFILE_SCOPE()                ((void)0)
    #define ASH_PROFILE_SCOPE_N(name)          ((void)0)
    #define ASH_PROFILE_SCOPE_C(color)         ((void)0)
    #define ASH_PROFILE_SCOPE_NC(name, color)  ((void)0)

    #define ASH_PROFILE_SCOPE_TEXT(text, size) ((void)0)

    #define ASH_PROFILE_PLOT(name, value)      ((void)0)

    #define ASH_PROFILE_MESSAGE(msg)           ((void)0)
    #define ASH_PROFILE_MESSAGE_C(msg, color)  ((void)0)

    #define ASH_PROFILE_TAG(key, value)        ((void)0)

    #define ASH_PROFILE_ALLOC(ptr, size)       ((void)0)
    #define ASH_PROFILE_FREE(ptr)              ((void)0)
#endif

// Common color palette for ASH_PROFILE_SCOPE_NC. Tracy uses 0xRRGGBB.
namespace AshEngine::Profile::Color
{
    static constexpr unsigned int Frame      = 0x55AA55;
    static constexpr unsigned int Tick       = 0x4E80FF;
    static constexpr unsigned int Render     = 0xFF8000;
    static constexpr unsigned int Scene      = 0xFFC040;
    static constexpr unsigned int Visibility = 0xFFE060;
    static constexpr unsigned int Submit     = 0xC04060;
    static constexpr unsigned int Present    = 0x803060;
    static constexpr unsigned int Upload     = 0x40A0C0;
    static constexpr unsigned int Barrier    = 0x808080;
    static constexpr unsigned int RHI        = 0xA0A0FF;
    static constexpr unsigned int Pipeline   = 0xC080FF;
    static constexpr unsigned int Descriptor = 0xFF60FF;
    static constexpr unsigned int Draw       = 0x60C0FF;
    static constexpr unsigned int UI         = 0xA0A040;
}
