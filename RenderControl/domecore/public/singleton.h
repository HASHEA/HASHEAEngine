#pragma once
//#ifndef __SINGLETON_H__
//#define __SINGLETON_H__
#include "defines.h"
#include "error.h"

DOME_NAMESPACE_BEGIN

template<class TYPE>
class TSingleton
{
public:
    TSingleton()
    {
        DOME_ASSERT2(!s_pInstance, "Trying to create multiple instance for a singleton class");
        s_pInstance = static_cast<TYPE*>(this);
    }

    virtual ~TSingleton()
    {
        DOME_ASSERT2(s_pInstance, "Trying to destroy a singleton instance, but the instance pointer is NULL!");
        s_pInstance = DM_NULL;
    }

    static Bool IsInstanceCreated()
    {
        return s_pInstance != DM_NULL;
    }

    static TYPE& Instance()
    {
        DOME_ASSERT2(s_pInstance, "The instance of the singleton class is still not created!");
        return *s_pInstance;
    }

    static TYPE*   InstancePtr()
    {
        return s_pInstance;
    }

private:
    static TYPE*            s_pInstance;
};

template<class TYPE>
TYPE* TSingleton<TYPE>::s_pInstance = DM_NULL;



#define DOME_SINGLETON_DECLARATION(TYPE)                                                                                                        \
private:                                                                                                                                        \
    static TYPE*            s_pInstance;                                                                                                        \
public:                                                                                                                                         \
    static Bool IsInstanceCreated()                                                                                                             \
    {                                                                                                                                           \
        return s_pInstance != DM_NULL;                                                                                                          \
    }                                                                                                                                           \
                                                                                                                                                \
    static TYPE& Instance()                                                                                                                     \
    {                                                                                                                                           \
        DOME_ASSERT2(s_pInstance, "The instance of the singleton class is still not created!");                                                 \
        return *s_pInstance;                                                                                                                    \
    }                                                                                                                                           \
                                                                                                                                                \
    static TYPE*   InstancePtr()                                                                                                                \
    {                                                                                                                                           \
        return s_pInstance;                                                                                                                     \
    }                                                                                                                                           \
private:

#define DOME_SINGLETON_IMPLEMENTATION(TYPE)                                                                                                     \
TYPE* TYPE::s_pInstance = DM_NULL;

#define DOME_SINGLETON_CONSTRUCTOR_CODE(TYPE)                                                                                                   \
DOME_ASSERT2(!s_pInstance, "Trying to create multiple instance for a singleton class");                                                         \
s_pInstance = static_cast<TYPE*>(this);

#define DOME_SINGLETON_DESTRUCTOR_CODE(TYPE)                                                                                                    \
DOME_ASSERT2(s_pInstance, "Trying to destroy a singleton instance, but the instance pointer is NULL!");                                         \
s_pInstance = DM_NULL;




DOME_NAMESPACE_END

//#endif//__SINGLETON_H__