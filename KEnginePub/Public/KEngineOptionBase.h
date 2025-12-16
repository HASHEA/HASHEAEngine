#pragma once

#include "IKHeader.h"
#include "KBase/Public/thread/KThread.h"
#include "Engine/KUniqueString.h"
#include "Engine/KGLog.h"
#include "shared_mutex"


class DeviceConfigFile//以前的switchOption文件表
{
public:
    BOOL InitSwitchConfig(const char* pcszGpu); // 配置表只缓存本机型设备相关的，不相关的不加载
    int GetSwitchOpen(KUniqueStr pcszSwitchName);

private:
    typedef std::unordered_map<KUniqueStr, bool> SwitchBlockMap;
    SwitchBlockMap                               m_mapSwitch;
    std::string                                  m_strDeviceModel;
    std::string                                  m_strCpuDevice;
    std::string                                  m_strGpuDevice;
};

namespace NSEngine
{
    extern "C" BOOL InitDeviceConfigFile(const char* pcszGpu);
    extern "C" int GetDeviceConfigFileValue(KUniqueStr pcszSwitchName);
} // namespace NSEngine

//---------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------
namespace NSEngineOptionBase
{
    enum class emAccessType
    {
        NONE,
        LOGIC,
        RENDER,
        MUTEX,
        NO_LIMIT
    };

    template<typename T, bool bFileConfig, emAccessType emReadType, emAccessType emWriteType>
    class PropertyBase
    {
        static_assert(!bFileConfig || (std::is_same_v<T, BOOL> || std::is_same_v<T, bool>));
    protected:
        T m_value;
        KUniqueStr m_name;
    public:
        bool bTrace = false;

    protected://禁止基类被直接实例化
        PropertyBase() = delete;
        explicit PropertyBase(const T& initialValue, const char* pcszName = "") : m_value(initialValue), m_name(g_CacheOriginalString(pcszName)){}

    protected://子类中有需要再打开
        //函数式取值
        virtual T Get() const;
        //函数式赋值
        virtual T Set(const T& newValue);
        virtual std::shared_mutex* GetSharedMutex() const { return nullptr; }
        virtual bool LogicWriteAssert() const{ return IsLogicThread(); }
        virtual bool LogicReadAssert() const { return IsLogicThread(); }
        virtual bool RenderWriteAssert() const { return IsMainThread(); }
        virtual bool RenderReadAssert() const{ return IsMainThread(); }

    };

    template <typename T, bool bFileConfig, emAccessType emReadType, emAccessType emWriteType>
    T PropertyBase<T, bFileConfig, emReadType, emWriteType>::Get() const
    {
        if constexpr (emReadType == emAccessType::NONE)
        {
            KGLOG_ASSERT_EXIT(false && "Access forbidden");
        }
        if constexpr (emReadType == emAccessType::LOGIC)
        {
            KGLOG_ASSERT_EXIT(LogicReadAssert());
        }
        else if constexpr (emReadType == emAccessType::RENDER)
        {
            KGLOG_ASSERT_EXIT(RenderReadAssert());
        }


        if (bTrace)
        {

        }

        if constexpr (bFileConfig)
        {
            int nRet = NSEngine::GetDeviceConfigFileValue(m_name);
            if (nRet != -1)
            {
                return nRet;
            }
        }

        if constexpr (emReadType == emAccessType::MUTEX)
        {
            std::shared_mutex* mutex = GetSharedMutex();
            KGLOG_ASSERT_EXIT(mutex);
            std::shared_lock lock(*mutex);
            return m_value;
        }
        else
        {
            return m_value;
        }
        goto Exit0;//emReadType == emAccessType::NO_LIMIT时，标签会不被使用，warrning
    Exit0:
        return T();
    }

    template <typename T, bool bFileConfig, emAccessType emReadType, emAccessType emWriteType>
    T PropertyBase<T, bFileConfig, emReadType, emWriteType>::Set(const T& newValue)
    {
        if constexpr (emWriteType == emAccessType::NONE)
        {
            KGLOG_ASSERT_EXIT(false && "Access forbidden");
        }
        if constexpr (emWriteType == emAccessType::LOGIC)
        {
            KGLOG_ASSERT_EXIT(LogicWriteAssert());
        } else if constexpr (emWriteType == emAccessType::RENDER)
        {
            KGLOG_ASSERT_EXIT(RenderWriteAssert());
        }

        if (bTrace)
        {

        }


        if constexpr (bFileConfig)
        {
            int nRet = NSEngine::GetDeviceConfigFileValue(m_name);
            if (nRet != -1)
            {
                return nRet;
            }
        }

        if constexpr (emReadType == emAccessType::MUTEX)
        {
            std::shared_mutex* mutex = GetSharedMutex();
            KGLOG_ASSERT_EXIT(mutex);
            std::unique_lock lock(*mutex);
            return m_value = newValue;
        }
        else
        {
            return m_value = newValue;
        }
        goto Exit0;//emReadType == emAccessType::NO_LIMIT时，标签会不被使用，warrning
    Exit0:
        return T();
    }

    //---------------------------------------------------------------------------------------------
    //---------------------------------------------------------------------------------------------

    //LogicRW
#define LogicRWTemplete T, bFileConfig,emAccessType::LOGIC,emAccessType::LOGIC
    template<typename T, bool bFileConfig>
    class Property_LogicRW :public PropertyBase<LogicRWTemplete>
    {
    public:
        Property_LogicRW() = delete;
        Property_LogicRW(const Property_LogicRW&) = delete;
        Property_LogicRW& operator=(const Property_LogicRW& rh)
        {
            Set(rh.Get());
            return *this;
        };
        Property_LogicRW& operator=(const Property_LogicRW&& rh)
        {
            Set(rh.Get());
            return *this;
        };
        Property_LogicRW(const T& initialValue, const char* pcszName = "") :PropertyBase<LogicRWTemplete>(initialValue, pcszName) {}

        //函数式取值
        using PropertyBase<LogicRWTemplete>::Get;
        //函数式赋值
        using PropertyBase<LogicRWTemplete>::Set;
        // 取值
        operator T() const { return Get(); }
        // 赋值
        T operator=(const T& newValue) { return Set(newValue); }
    };

    struct L2RSyncControler
    {
        bool bChanged = false;
        bool bSyncing = false;
    };

    //static L2RSyncControler g_NULLSyncControler;

    //LogicRW RenderRead
#define LogicRW_RenderRTemplete T, bFileConfig,emAccessType::NO_LIMIT,emAccessType::LOGIC
    template<typename T, bool bFileConfig>
    class Property_LogicRW_RenderR :public PropertyBase<LogicRW_RenderRTemplete>
    {
    private:
        L2RSyncControler& m_syncControler;
    public:
        Property_LogicRW_RenderR() = delete;
        Property_LogicRW_RenderR(const Property_LogicRW_RenderR& rh) :PropertyBase<LogicRW_RenderRTemplete>(rh.m_value, rh.m_name), m_syncControler(rh.m_syncControler)
        {//线程同步时需要先缓存一份逻辑线程的值，等待渲染线程去值，此时会有临时对象产生。
            //其他地方先禁止这个操作，有需要再开启
            KGLOG_ASSERT_EXIT(rh.m_syncControler.bSyncing && "operation forbidden! only use in sync");
        Exit0:
            return;
        }
        Property_LogicRW_RenderR(const Property_LogicRW_RenderR&& rh) :PropertyBase<LogicRW_RenderRTemplete>(rh.m_value, rh.m_name), m_syncControler(rh.m_syncControler)
        {//线程同步时lambda表达式会转发到命令队列，会发生移动构造
            //其他地方先禁止这个操作，有需要再开启
            KGLOG_ASSERT_EXIT(rh.m_syncControler.bSyncing && "operation forbidden! only use in sync");
        Exit0:
            return;
        }
        Property_LogicRW_RenderR& operator=(const Property_LogicRW_RenderR& rh)
        {
            Set(rh.Get());
            return *this;
        };
        Property_LogicRW_RenderR& operator=(const Property_LogicRW_RenderR&& rh)
        {
            Set(rh.Get());
            return *this;
        };
        Property_LogicRW_RenderR(const T& initialValue, L2RSyncControler& syncControler,const char* pcszName = "") :PropertyBase<LogicRW_RenderRTemplete>(initialValue, pcszName),m_syncControler(syncControler) {}

        //函数式取值
        using PropertyBase<LogicRW_RenderRTemplete>::Get;
        //函数式赋值
        virtual T Set(const T& newValue) override
        {
            if (newValue != this->m_value)
            {
                m_syncControler.bChanged = true;//这里暂时先不管set没set成功，值不一样就算changed。要不然要先复制一份m_value出来作对比，担心T是个大结构，有性能问题
            }
            return PropertyBase<LogicRW_RenderRTemplete>::Set(newValue);
        }
        // 取值
        operator T() const { return Get(); }
        // 赋值
        T operator=(const T& newValue) { return Set(newValue); }
    protected:
        virtual bool LogicWriteAssert() const override
        {
            return IsLogicThread() || m_syncControler.bSyncing;
        }
    };

    //RenderRW
#define RenderRWTemplete T, bFileConfig,emAccessType::RENDER,emAccessType::RENDER
    template<typename T, bool bFileConfig>
    class Property_RenderRW :public PropertyBase<RenderRWTemplete>
    {
    public:
        Property_RenderRW() = delete;
        Property_RenderRW(const Property_RenderRW&) = delete;
        Property_RenderRW& operator=(const Property_RenderRW& rh)
        {
            Set(rh.Get());
            return *this;
        };
        Property_RenderRW& operator=(const Property_RenderRW&& rh)
        {
            Set(rh.Get());
            return *this;
        };
        Property_RenderRW(const T& initialValue, const char* pcszName = "") :PropertyBase<RenderRWTemplete>(initialValue, pcszName) {}

        //函数式取值
        using PropertyBase<RenderRWTemplete>::Get;
        //函数式赋值
        using PropertyBase<RenderRWTemplete>::Set;
        // 取值
        operator T() const { return Get(); }
        // 赋值
        T operator=(const T& newValue) { return Set(newValue); }
    };


    //mutex
#define mutexTemplete T, bFileConfig,emAccessType::MUTEX,emAccessType::MUTEX
    template<typename T, bool bFileConfig>
    class Property_Mutex :public PropertyBase<mutexTemplete>
    {
    public:
        Property_Mutex() = delete;
        Property_Mutex(const Property_Mutex&) = delete;
        Property_Mutex& operator=(const Property_Mutex& rh)
        {
            Set(rh.Get());
            return *this;
        };
        Property_Mutex& operator=(const Property_Mutex&& rh)
        {
            Set(rh.Get());
            return *this;
        };
        Property_Mutex(const T& initialValue, const char* pcszName = "") :PropertyBase<mutexTemplete>(initialValue, pcszName) {}

        //函数式取值
        using PropertyBase<mutexTemplete>::Get;
        //函数式赋值
        using PropertyBase<mutexTemplete>::Set;
        // 取值
        operator T() const { return Get(); }
        // 赋值
        T operator=(const T& newValue) { return Set(newValue); }
        virtual std::shared_mutex* GetSharedMutex() const override
        {
            return &mShareMutex;
        }
    private:
        mutable std::shared_mutex mShareMutex;
    };


    //read only
#define ReadOnlyTemplete T, bFileConfig,emAccessType::NO_LIMIT,emAccessType::NONE
    template<typename T, bool bFileConfig>
    class Property_ROnly :public PropertyBase<ReadOnlyTemplete>
    {
    public:
        Property_ROnly() = delete;
        Property_ROnly(const Property_ROnly&) = delete;
        Property_ROnly& operator=(const Property_ROnly&) = delete;
        Property_ROnly& operator=(const Property_ROnly&&) = delete;
        Property_ROnly(const T& initialValue, const char* pcszName = "") :PropertyBase<ReadOnlyTemplete>(initialValue, pcszName) {}

        //函数式取值
        using PropertyBase<ReadOnlyTemplete>::Get;
        // 取值
        operator T() const { return Get(); }
    };
}
