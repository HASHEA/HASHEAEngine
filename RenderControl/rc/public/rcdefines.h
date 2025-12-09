/*
    filename:       rcdefines.h
    author:         Ming Dong
    date:           2016-MAR-22
    description:    
*/
#pragma once

#include <domecore/public/domecore.h>

#ifdef DOME_MODULE_AS_LIB
#define RC_API
#define RC_API
#else
#ifdef RC_EXPORTS
#define RC_API      DOME_EXPORT
#else
#define RC_API      DOME_IMPORT
#endif
#endif

#define RC_NAMESPACE_BEGIN      DOME_NAMESPACE_BEGIN
#define RC_NAMESPACE_END        DOME_NAMESPACE_END
#define RC_NS                   DOME_NS


#define RC_RELEASE(pInterface)          \
do                                      \
{                                       \
    if (pInterface)                     \
    {                                   \
        (pInterface)->Release();        \
        (pInterface) = NULL;            \
    }                                   \
} while(FALSE)
