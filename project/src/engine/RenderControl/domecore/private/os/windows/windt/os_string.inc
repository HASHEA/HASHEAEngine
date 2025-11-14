// define _WINSOCKAPI_ macro to avoid error when include winsock2.h header file later
// Don't include winsock.h
#pragma push_macro("_WINSOCKAPI_")
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#pragma pop_macro("_WINSOCKAPI_")

#include "stdio.h"
#include "stdarg.h"

#ifdef DOME_COMPATIBLE_WITH_VS2012
//#pragma warning(disable : 4996)
#endif

DOME_NAMESPACE_BEGIN

Int OS_String::StrVNSPrintf(U8Char* o_pString, Int i_BuffSize, const U8Char* i_Format, va_list i_Args)
{
    return vsnprintf(o_pString, i_BuffSize * sizeof(U8Char), i_Format, i_Args);
}

Int OS_String::StrVNSPrintf(U16Char* o_pString, Int i_BuffSize, const U16Char* i_Format, va_list i_Args)
{
    return vswprintf((wchar_t*)o_pString, i_BuffSize * sizeof(U16Char), (const wchar_t*)i_Format, i_Args);
}

Int OS_String::StrVNSPrintf(U32Char* o_pString, Int i_BuffSize, const U32Char* i_Format, va_list i_Args)
{
    DOME_ERROR2(0, "Error: Utf-32 string format function is not implemented still.");
    return -1;
}

#ifdef DOME_COMPATIBLE_WITH_VS2012
// in vs2012, vsscanf is not support, here is a simulate version which support 10 args
int vsscanf_for_vs2012(const char *s, const char *fmt, va_list ap)
{
  void *a[20];
  int i;
  for (i=0; i<sizeof(a)/sizeof(a[0]); i++) a[i] = va_arg(ap, void *);
  return sscanf(s, fmt, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], 
				a[10], a[11], a[12], a[13], a[14], a[15], a[16], a[17], a[18], a[19]);
}

// in vs2012, vswscanf is not support, here is a simulate version which support 10 args
int vswscanf_for_vs2012(const wchar_t *s, const wchar_t *fmt, va_list ap)
{
  void *a[20];
  int i;
  for (i=0; i<sizeof(a)/sizeof(a[0]); i++) a[i] = va_arg(ap, void *);
  return swscanf(s, fmt, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], 
				a[10], a[11], a[12], a[13], a[14], a[15], a[16], a[17], a[18], a[19]);
}
#endif

Int OS_String::StrVSScanf(const U8Char* i_pString, const U8Char* i_Format, va_list o_Args)
{
#ifdef DOME_COMPATIBLE_WITH_VS2012
    return vsscanf_for_vs2012(i_pString, i_Format, o_Args);
#else
    return vsscanf(i_pString, i_Format, o_Args);
#endif
}

Int OS_String::StrVSScanf(const U16Char* i_pString, const U16Char* i_Format, va_list o_Args)
{
#ifdef DOME_COMPATIBLE_WITH_VS2012
    return vswscanf_for_vs2012((const wchar_t*)i_pString, (const wchar_t*)i_Format, o_Args);
#else
    return vswscanf((const wchar_t*)i_pString, (const wchar_t*)i_Format, o_Args);
#endif
}

Int OS_String::StrVSScanf(const U32Char* i_pString, const U32Char* i_Format, va_list o_Args)
{
    return -1;
}


Int OS_String::StrFormat(U8Char* o_pString, Int i_BuffSize, const U8Char* i_Format, ...)
{
    Int l_Result = 0;
    va_list l_Args;
    va_start(l_Args, i_Format);
    l_Result = StrVNSPrintf(o_pString, i_BuffSize, i_Format, l_Args);
    va_end(l_Args);
    if (l_Result >= 0)
        ++l_Result;
    else
        l_Result = -1;
    return l_Result;
}

Int OS_String::StrFormat(U16Char* o_pString, Int i_BuffSize, const U16Char* i_Format, ...)
{
    Int l_Result = 0;
    va_list l_Args;
    va_start(l_Args, i_Format);
    l_Result = StrVNSPrintf(o_pString, i_BuffSize, i_Format, l_Args);
    va_end(l_Args);
    if (l_Result >= 0)
        ++l_Result;
    else
        l_Result = -1;
    return l_Result;
}

Int OS_String::StrFormat(U32Char* o_pString, Int i_BuffSize, const U32Char* i_Format, ...)
{
    Int l_Result = 0;
    va_list l_Args;
    va_start(l_Args, i_Format);
    l_Result = StrVNSPrintf(o_pString, i_BuffSize, i_Format, l_Args);
    va_end(l_Args);
    if (l_Result >= 0)
        ++l_Result;
    else
        l_Result = -1;
    return l_Result;
}

Int OS_String::StrScan(const U8Char* i_pString, const U8Char* i_Format, ...)
{
    Int l_Result = 0;
    va_list l_Args;
    va_start(l_Args, i_Format);
    l_Result = StrVSScanf(i_pString, i_Format, l_Args);
    va_end(l_Args);
    return l_Result;
}

Int OS_String::StrScan(const U16Char* i_pString, const U16Char* i_Format, ...)
{
    Int l_Result = 0;
    va_list l_Args;
    va_start(l_Args, i_Format);
    l_Result = StrVSScanf(i_pString, i_Format, l_Args);
    va_end(l_Args);
    return l_Result;
}

Int OS_String::StrScan(const U32Char* i_pString, const U32Char* i_Format, ...)
{
    Int l_Result = 0;
    va_list l_Args;
    va_start(l_Args, i_Format);
    l_Result = StrVSScanf(i_pString, i_Format, l_Args);
    va_end(l_Args);
    return l_Result;
}

DOME_NAMESPACE_END
