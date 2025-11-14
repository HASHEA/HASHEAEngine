// define _WINSOCKAPI_ macro to avoid error when include winsock2.h header file later
// Don't include winsock.h
#pragma push_macro("_WINSOCKAPI_")
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#pragma pop_macro("_WINSOCKAPI_")
#include <Objbase.h>


DOME_NAMESPACE_BEGIN

DResult OS_Guid::GenerateGuid(DGUID& o_Guid)
{
    GUID l_Guid;
    HRESULT hr = CoCreateGuid(&l_Guid);
    if(hr != S_OK)
        return R_FAILED;
    o_Guid.m_Data1 = l_Guid.Data1;
    o_Guid.m_Data2 = l_Guid.Data2;
    o_Guid.m_Data3 = l_Guid.Data3;
    for(Int i = 0; i < 8; ++i)
        o_Guid.m_Data4[i] = l_Guid.Data4[i];

    return R_SUCCESS;
}

DOME_NAMESPACE_END