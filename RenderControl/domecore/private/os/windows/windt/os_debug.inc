// define _WINSOCKAPI_ macro to avoid error when include winsock2.h header file later
// Don't include winsock.h
#pragma push_macro("_WINSOCKAPI_")
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#pragma pop_macro("_WINSOCKAPI_")

DOME_NAMESPACE_BEGIN

OS_Debug::MESSAGEBOXRET OS_Debug::ShowDebugDialog(OS_Debug::DEBUGDIALOGTYPE i_Type,const Char* i_pCaption,const Char* i_pInfo)
{
    Int l_Ret;
    switch(i_Type)
    {
    case DDT_WARNING:
        l_Ret = MessageBoxA(DM_NULL, i_pInfo, i_pCaption, MB_ABORTRETRYIGNORE|MB_ICONWARNING);
        switch(l_Ret)
        {
        case IDABORT:
            return MBR_ABORT;
        case IDRETRY:
            return MBR_CONTINUE;
        case IDIGNORE:
            return MBR_IGNORE;
        default:
            return MBR_UNKNOWN;
        }
        break;
    case DDT_ASSERT:
        l_Ret = MessageBoxA(DM_NULL, i_pInfo, i_pCaption, MB_ABORTRETRYIGNORE|MB_ICONSTOP);
        switch(l_Ret)
        {
        case IDABORT:
            return MBR_ABORT;
        case IDRETRY:
            return MBR_DEBUG;
        case IDIGNORE:
            return MBR_IGNORE;
        default:
            return MBR_UNKNOWN;
        }
        break;
    case DDT_ERROR:
        l_Ret = MessageBoxA(DM_NULL, i_pInfo, i_pCaption, MB_RETRYCANCEL|MB_ICONSTOP);
        switch(l_Ret)
        {
        case IDRETRY:
            return MBR_DEBUG;
        case IDCANCEL:
            return MBR_ABORT;
        default:
            return MBR_UNKNOWN;
        }
        break;
    }
    return MBR_UNKNOWN;
}

DOME_NAMESPACE_END