/*
    filename:       os_guid.h
    author:         Ming Dong
    date:           2016-Jul-13
    description:    
*/
#pragma once

#include "../typedefs.h"

DOME_NAMESPACE_BEGIN

class DOME_CORE_API OS_Guid
{
public:
    struct DGUID
    {
        U32         m_Data1;
        U16         m_Data2;
        U16         m_Data3;
        U8          m_Data4[8];
    };

    static DResult GenerateGuid(DGUID& o_Guid);
};


DOME_NAMESPACE_END