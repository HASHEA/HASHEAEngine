//
//  unicode.h
//  domecore
//
//  Created by Ming Dong on 2015-Dec-07.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
#pragma once

#include "configure.h"
#include "defines.h"
#include "typedefs.h"

DOME_NAMESPACE_BEGIN
namespace unicode
{
/*
    https://en.wikipedia.org/wiki/Unicode
    According to Unicode encoding standard, the total code point range is [0x0000, 0x10FFFF]
    But the following code points are now valid code points
    [0xD800, 0xDBFF] high-surrogate code points
    [0xDC00, 0xDFFF] low-surrogate code points
    [0xFDD0, 0xFDEF] and any code point ending in the value FFFE or FFFF: noncharacter code points that 
                        are guaranteed never to be used for encoding characters, although applications 
                        may make use of these code points internally if they wish. 
*/

typedef S32             CodePoint;

/*
    Decode a unicode code point from a unicode string
    i_UString: point to the unicode string
    o_CodeLen: return how many characters this code point ocupied
    i_StrSize: Character count (include the end NULL) in the unicode string
    return: if there is a valid code point in the string buffer, return that code point, otherwise, return -1
*/
inline CodePoint DecodeCodePointUtf8(const U8Char* i_UString, Int* o_CodePointLen)
{
    CodePoint l_Ret = -1;
    U8Char l_FirstChar = i_UString[0];
    if ((l_FirstChar & 0x80) == 0)
    {
        *o_CodePointLen = 1;
        l_Ret = l_FirstChar;
    }
    else if ((l_FirstChar & 0xE0) == 0xC0)
    {
        if ((i_UString[1] & 0xC0) == 0x80)
        {
            *o_CodePointLen = 2;
            l_Ret = ((i_UString[0] & 0x1F) << 6) + ((i_UString[1] & 0x3F) << 0);
        }
    }
    else if ((l_FirstChar & 0xF0) == 0xE0)
    {
        if ((i_UString[1] & 0xC0) == 0x80 && (i_UString[2] & 0xC0) == 0x80)
        {
            *o_CodePointLen = 3;
            l_Ret = ((i_UString[0] & 0x0F) << 12) + ((i_UString[1] & 0x3F) << 6) + ((i_UString[2] & 0x3F) << 0);
        }
    }
    else if ((l_FirstChar & 0xF8) == 0xF0)
    {
        if ((i_UString[1] & 0xC0) == 0x80 && (i_UString[2] & 0xC0) == 0x80 && (i_UString[3] & 0xC0) == 0x80)
        {
            *o_CodePointLen = 4;
            l_Ret = ((i_UString[0] & 0x07) << 18) + ((i_UString[1] & 0x3F) << 12) + ((i_UString[2] & 0x3F) << 6) + ((i_UString[3] & 0x3F) << 0);
        }
    }
    return l_Ret;
}

inline CodePoint SafeDecodeCodePointUtf8(const U8Char* i_UString, Int i_StrSize, Int* o_CodePointLen)
{
    CodePoint l_Ret = -1;
    U8Char l_FirstChar = i_UString[0];
    if ((l_FirstChar & 0x80) == 0 && i_StrSize >= 1)
    {
        *o_CodePointLen = 1;
        l_Ret = l_FirstChar;
    }
    else if ((l_FirstChar & 0xE0) == 0xC0 && i_StrSize >= 2)
    {
        if ((i_UString[1] & 0xC0) == 0x80)
        {
            *o_CodePointLen = 2;
            l_Ret = ((i_UString[0] & 0x1F) << 6) + ((i_UString[1] & 0x3F) << 0);
        }
    }
    else if ((l_FirstChar & 0xF0) == 0xE0 && i_StrSize >= 3)
    {
        if ((i_UString[1] & 0xC0) == 0x80 && (i_UString[2] & 0xC0) == 0x80)
        {
            *o_CodePointLen = 3;
            l_Ret = ((i_UString[0] & 0x0F) << 12) + ((i_UString[1] & 0x3F) << 6) + ((i_UString[2] & 0x3F) << 0);
        }
    }
    else if ((l_FirstChar & 0xF8) == 0xF0 && i_StrSize >= 4)
    {
        if ((i_UString[1] & 0xC0) == 0x80 && (i_UString[2] & 0xC0) == 0x80 && (i_UString[3] & 0xC0) == 0x80)
        {
            *o_CodePointLen = 4;
            l_Ret = ((i_UString[0] & 0x07) << 18) + ((i_UString[1] & 0x3F) << 12) + ((i_UString[2] & 0x3F) << 6) + ((i_UString[3] & 0x3F) << 0);
        }
    }
    return l_Ret;
}

inline CodePoint DecodeCodePointUtf16(const U16Char* i_UString, Int* o_CodePointLen)
{
    U16Char l_FirstChar = i_UString[0];
    CodePoint l_Ret = -1;
    if (l_FirstChar >= 0 && l_FirstChar < 0xD800 || l_FirstChar >= 0xE000 && l_FirstChar < 1114112/*(17 * 65536)*/)
    {
        *o_CodePointLen = 1;
        l_Ret = l_FirstChar;
    }
    else if (l_FirstChar >= 0xD800 && l_FirstChar < 0xDC00)
    {
        if (i_UString[1] >= 0xDC00 && i_UString[1] < 0xE000)
        {
            *o_CodePointLen = 2;
            l_Ret = ((i_UString[0] - 0xD800) << 10) + (i_UString[1] - 0xDC00) + 0x10000;
        }
    }
    return l_Ret;
}

inline CodePoint SafeDecodeCodePointUtf16(const U16Char* i_UString, Int i_StrSize, Int* o_CodePointLen)
{
    U16Char l_FirstChar = i_UString[0];
    CodePoint l_Ret = -1;
    if (i_StrSize < 1) return l_Ret;
    if (l_FirstChar >= 0 && l_FirstChar < 0xD800 || l_FirstChar >= 0xE000 && l_FirstChar < 1114112/*(17 * 65536)*/)
    {
        *o_CodePointLen = 1;
        l_Ret = l_FirstChar;
    }
    else if (l_FirstChar >= 0xD800 && l_FirstChar < 0xDC00 && i_StrSize >= 2)
    {
        if (i_UString[1] >= 0xDC00 && i_UString[1] < 0xE000)
        {
            *o_CodePointLen = 2;
            l_Ret = ((i_UString[0] - 0xD800) << 10) + (i_UString[1] - 0xDC00) + 0x10000;
        }
    }
    return l_Ret;
}

inline CodePoint DecodeCodePointUtf32(const U32Char* i_UString, Int* o_CodePointLen)
{
    *o_CodePointLen = 1;
    return i_UString[0];
}

inline CodePoint SafeDecodeCodePointUtf32(const U32Char* i_UString, Int i_StrSize, Int* o_CodePointLen)
{
    if (i_StrSize < 1) return -1;
    *o_CodePointLen = 1;
    return i_UString[0];
}

/*
    function overload version
*/
inline CodePoint DecodeCodePoint(const U8Char* i_UString, Int* o_CodePointLen)
{
    return DecodeCodePointUtf8(i_UString, o_CodePointLen);
}

inline CodePoint SafeDecodeCodePoint(const U8Char* i_UString, Int i_StrSize, Int* o_CodePointLen)
{
    return SafeDecodeCodePointUtf8(i_UString, i_StrSize, o_CodePointLen);
}

inline CodePoint DecodeCodePoint(const U16Char* i_UString, Int* o_CodePointLen)
{
    return DecodeCodePointUtf16(i_UString, o_CodePointLen);
}

inline CodePoint SafeDecodeCodePoint(const U16Char* i_UString, Int i_StrSize, Int* o_CodePointLen)
{
    return SafeDecodeCodePointUtf16(i_UString, i_StrSize, o_CodePointLen);
}

inline CodePoint DecodeCodePoint(const U32Char* i_UString, Int* o_CodePointLen)
{
    return DecodeCodePointUtf32(i_UString, o_CodePointLen);
}

inline CodePoint SafeDecodeCodePoint(const U32Char* i_UString, Int i_StrSize, Int* o_CodePointLen)
{
    return SafeDecodeCodePointUtf32(i_UString, i_StrSize, o_CodePointLen);
}

/*
    Decode a NULL terminated unicode string to o_pCodePointBuff.
    i_UString:          the unicode string
    i_StrSize:          Character count (include the end NULL) in the unicode string
    o_pCodePointBuff:   the output code print buffer, can be NULL
    i_BuffSize:         the charater count (include the NULL end) which the output buffer can hold
    RETURN:             The function always return how many code points in the unicode string, 
                        except when there is invalid codepoint in the string
*/
inline Int DecodeStringUtf8(const U8Char* i_UString, CodePoint* o_pCodePointBuff, Int i_BuffSize)
{
    Int l_CPNum = 0;
    Int l_CPLen = 0;

    if (!o_pCodePointBuff) i_BuffSize = 0;

    CodePoint l_CP;
    while (DM_TRUE)
    {
        l_CP = DecodeCodePointUtf8(i_UString, &l_CPLen);
        if (l_CP < 0) return -1;
        if (l_CPNum < i_BuffSize)
        {
            o_pCodePointBuff[l_CPNum] = l_CP;
        }
        ++l_CPNum;
        if (l_CP == 0) break;
        i_UString += l_CPLen;
    }
    return l_CPNum;
}

inline Int SafeDecodeStringUtf8(const U8Char* i_UString, Int i_StrSize, CodePoint* o_pCodePointBuff, Int i_BuffSize)
{
    Int l_CPNum = 0;
    Int l_CPLen = 0;

    if (!o_pCodePointBuff) i_BuffSize = 0;

    CodePoint l_CP;
    while (DM_TRUE)
    {
        l_CP = SafeDecodeCodePointUtf8(i_UString, i_StrSize, &l_CPLen);
        if (l_CP < 0) return -1;
        if (l_CPNum < i_BuffSize)
        {
            o_pCodePointBuff[l_CPNum] = l_CP;
        }
        ++l_CPNum;
        if (l_CP == 0) break;
        i_UString += l_CPLen;
        i_StrSize -= l_CPLen;
        if (i_StrSize <= 0) break;
    }
    return l_CPNum;
}

inline Int DecodeStringUtf16(const U16Char* i_UString, CodePoint* o_pCodePointBuff, Int i_BuffSize)
{
    Int l_CPNum = 0;
    Int l_CPLen = 0;

    if (!o_pCodePointBuff) i_BuffSize = 0;

    CodePoint l_CP;
    while (DM_TRUE)
    {
        l_CP = DecodeCodePointUtf16(i_UString, &l_CPLen);
        if (l_CP < 0) return -1;
        if (l_CPNum < i_BuffSize)
        {
            o_pCodePointBuff[l_CPNum] = l_CP;
        }
        ++l_CPNum;
        if (l_CP == 0) break;
        i_UString += l_CPLen;
    }
    return l_CPNum;
}

inline Int SafeDecodeStringUtf16(const U16Char* i_UString, Int i_StrSize, CodePoint* o_pCodePointBuff, Int i_BuffSize)
{
    Int l_CPNum = 0;
    Int l_CPLen = 0;

    if (!o_pCodePointBuff) i_BuffSize = 0;

    CodePoint l_CP;
    while (DM_TRUE)
    {
        l_CP = SafeDecodeCodePointUtf16(i_UString, i_StrSize, &l_CPLen);
        if (l_CP < 0) return -1;
        if (l_CPNum < i_BuffSize)
        {
            o_pCodePointBuff[l_CPNum] = l_CP;
        }
        ++l_CPNum;
        if (l_CP == 0) break;
        i_UString += l_CPLen;
        i_StrSize -= l_CPLen;
        if (i_StrSize <= 0) break;
    }
    return l_CPNum;
}

inline Int DecodeStringUtf32(const U32Char* i_UString, CodePoint* o_pCodePointBuff, Int i_BuffSize)
{
    Int l_CPNum = 0;
    Int l_CPLen = 0;

    if (!o_pCodePointBuff) i_BuffSize = 0;

    CodePoint l_CP;
    while (DM_TRUE)
    {
        l_CP = DecodeCodePointUtf32(i_UString, &l_CPLen);
        if (l_CP < 0) return -1;
        if (l_CPNum < i_BuffSize)
        {
            o_pCodePointBuff[l_CPNum] = l_CP;
        }
        ++l_CPNum;
        if (l_CP == 0) break;
        i_UString += l_CPLen;
    }
    return l_CPNum;
}

inline Int SafeDecodeStringUtf32(const U32Char* i_UString, Int i_StrSize, CodePoint* o_pCodePointBuff, Int i_BuffSize)
{
    Int l_CPNum = 0;
    Int l_CPLen = 0;

    if (!o_pCodePointBuff) i_BuffSize = 0;

    CodePoint l_CP;
    while (DM_TRUE)
    {
        l_CP = SafeDecodeCodePointUtf32(i_UString, i_StrSize, &l_CPLen);
        if (l_CP < 0) return -1;
        if (l_CPNum < i_BuffSize)
        {
            o_pCodePointBuff[l_CPNum] = l_CP;
        }
        ++l_CPNum;
        if (l_CP == 0) break;
        i_UString += l_CPLen;
        i_StrSize -= l_CPLen;
        if (i_StrSize <= 0) break;
    }
    return l_CPNum;
}

/*
    function overload version
*/
inline Int DecodeString(const U8Char* i_pString, CodePoint* o_pCodePointBuff, Int i_BuffSize)
{
    return DecodeStringUtf8(i_pString, o_pCodePointBuff, i_BuffSize);
}

inline Int SafeDecodeString(const U8Char* i_pString, Int i_StrSize, CodePoint* o_pCodePointBuff, Int i_BuffSize)
{
    return SafeDecodeStringUtf8(i_pString, i_StrSize, o_pCodePointBuff, i_BuffSize);
}

inline Int DecodeString(const U16Char* i_pString, CodePoint* o_pCodePointBuff, Int i_BuffSize)
{
    return DecodeStringUtf16(i_pString, o_pCodePointBuff, i_BuffSize);
}

inline Int SafeDecodeString(const U16Char* i_pString, Int i_StrSize, CodePoint* o_pCodePointBuff, Int i_BuffSize)
{
    return SafeDecodeStringUtf16(i_pString, i_StrSize, o_pCodePointBuff, i_BuffSize);
}

inline Int DecodeString(const U32Char* i_pString, CodePoint* o_pCodePointBuff, Int i_BuffSize)
{
    return DecodeStringUtf32(i_pString, o_pCodePointBuff, i_BuffSize);
}

inline Int SafeDecodeString(const U32Char* i_pString, Int i_StrSize, CodePoint* o_pCodePointBuff, Int i_BuffSize)
{
    return SafeDecodeStringUtf32(i_pString, i_StrSize, o_pCodePointBuff, i_BuffSize);
}

/*
    Encode a unicode code point to unicode string
    i_CodePoint:    the unicode code point
    o_UString:      the unicode string buffer
    i_BuffSize:     the unicode string buffer size, in character unit
    The function try to encode the unicode code point into the string buffer
    if i_CodePoint is invalid, return -1,
    otherwise return how many unicode character needed to hold the encoded code point.
    if the return value is greater than i_BuffSize, the o_Utf8String will be untouched.
*/
inline Int EncodeCodePointUtf8(CodePoint i_CodePoint, U8Char* o_UString, Int i_BuffSize)
{
    if (i_CodePoint < 0 || i_CodePoint > 0x10ffff)
        return -1;
    if (i_CodePoint >= 0xD800 && i_CodePoint <= 0xDFFF)
        return -1;
    if (o_UString == DM_NULL)
        i_BuffSize = 0;

    if (i_CodePoint <= 0x7F)
    {
        if (i_BuffSize >= 1)
        {
            o_UString[0] = (U8Char)i_CodePoint;
        }
        return 1;
    }
    if (i_CodePoint <= 0x7FF)
    {
        if (i_BuffSize >= 2)
        {
            o_UString[0] = ((i_CodePoint >> 6) & 0x1F) | 0xC0;
            o_UString[1] = (i_CodePoint & 0x3F) | 0x80;
        }
        return 2;
    }
    if (i_CodePoint <= 0xFFFF)
    {
        if (i_BuffSize >= 3)
        {
            o_UString[0] = ((i_CodePoint >> 12) & 0x0f) | 0xE0;
            o_UString[1] = ((i_CodePoint >> 6) & 0x3F) | 0x80;
            o_UString[2] = (i_CodePoint & 0x3F) | 0x80;
        }
        return 3;
    }
    if (i_CodePoint <= 0x1FFFFF)
    {
        if (i_BuffSize >= 4)
        {
            o_UString[0] = ((i_CodePoint >> 18) & 0x07) | 0xF0;
            o_UString[1] = ((i_CodePoint >> 12) & 0x3F) | 0x80;
            o_UString[2] = ((i_CodePoint >> 6) & 0x3F) | 0x80;
            o_UString[3] = (i_CodePoint & 0x3F) | 0x80;
        }
        return 4;
    }
    return -1;
}

inline Int EncodeCodePointUtf16(CodePoint i_CodePoint, U16Char* o_UString, Int i_BuffSize)
{
    if (i_CodePoint < 0 || i_CodePoint > 0x10ffff)
        return -1;
    if (i_CodePoint >= 0xD800 && i_CodePoint <= 0xDFFF)
        return -1;
    if (o_UString == DM_NULL)
        i_BuffSize = 0;

    if (i_CodePoint <= 0xFFFF)
    {
        if (i_BuffSize >= 1)
        {
            o_UString[0] = (U16Char)i_CodePoint;
        }
        return 1;
    }
    if (i_CodePoint <= 0x10FFFF)
    {
        if (i_BuffSize >= 2)
        {
            o_UString[0] = (((i_CodePoint - 0x010000) >> 10) & 0x3FF) + 0xD800;
            o_UString[1] = (((i_CodePoint - 0x010000) >> 0) & 0x3FF) + 0xDC00;
        }
        return 2;
    }
    return -1;
}

inline Int EncodeCodePointUtf32(CodePoint i_CodePoint, U32Char* o_UString, Int i_BuffSize)
{
    if (i_CodePoint < 0 || i_CodePoint > 0x10ffff)
        return -1;
    if (i_CodePoint >= 0xD800 && i_CodePoint <= 0xDFFF)
        return -1;
    if (o_UString == DM_NULL)
        i_BuffSize = 0;

    if (i_BuffSize >= 1)
    {
        o_UString[0] = i_CodePoint;
    }
    return 1;
}

/*
    function overload version
*/
inline Int EncodeCodePoint(CodePoint i_CodePoint, U8Char* o_pString, Int i_BuffSize)
{
    return EncodeCodePointUtf8(i_CodePoint, o_pString, i_BuffSize);
}

inline Int EncodeCodePoint(CodePoint i_CodePoint, U16Char* o_pString, Int i_BuffSize)
{
    return EncodeCodePointUtf16(i_CodePoint, o_pString, i_BuffSize);
}

inline Int EncodeCodePoint(CodePoint i_CodePoint, U32Char* o_pString, Int i_BuffSize)
{
    return EncodeCodePointUtf32(i_CodePoint, o_pString, i_BuffSize);
}

/*
    Encode a NULL terminated unicode string from a unicode code point array.
    i_CodePointArray:   The unicode code point array
    o_UString:          The encoded unicode string, can be NULL
    i_BuffSize:         The unicode sring buffer size in character unit
    i_CodePointArraySize:   the code point array size

    RETURN: if there is invalid code point in the array, return -1, otherwise, return the unicode string buffer size (with the end 0) which
    is needed to hold the encoded string
*/
inline Int EncodeStringUtf8(const CodePoint* i_CodePointArray, U8Char* o_UString, Int i_BuffSize)
{
    const CodePoint* l_pCurCP = i_CodePointArray;
    U8Char* l_pDest = o_UString;
    Int l_CurBuffSize = o_UString ? i_BuffSize : 0;
    Int l_EncodedSize = 0;
    while (*l_pCurCP != 0)
    {
        Int l_Size = EncodeCodePointUtf8(*l_pCurCP, l_pDest, l_CurBuffSize);
        if (l_Size <= 0)
            return -1;
        l_pCurCP++;
        l_pDest += l_Size;
        l_CurBuffSize -= l_Size;
        l_EncodedSize += l_Size;
    }
    if (l_CurBuffSize > 0)
    {
        *l_pDest = 0;
    }
    l_EncodedSize += 1;
    return l_EncodedSize;
}

inline Int SafeEncodeStringUtf8(const CodePoint* i_CodePointArray, Int i_CodePointArraySize, U8Char* o_UString, Int i_BuffSize)
{
    const CodePoint* l_pCurCP = i_CodePointArray;
    U8Char* l_pDest = o_UString;
    Int l_CurBuffSize = o_UString ? i_BuffSize : 0;
    Int l_CurCPNum = i_CodePointArraySize;
    Int l_EncodedSize = 0;
    while (l_CurCPNum > 0 && *l_pCurCP != 0)
    {
        Int l_Size = EncodeCodePointUtf8(*l_pCurCP, l_pDest, l_CurBuffSize);
        if (l_Size <= 0)
            return -1;
        l_pCurCP++;
        l_CurCPNum--;
        l_pDest += l_Size;
        l_CurBuffSize -= l_Size;
        l_EncodedSize += l_Size;
    }
    if (l_CurCPNum > 0)
    {
        if (l_CurBuffSize > 0)
        {
            *l_pDest = 0;
        }
        l_EncodedSize += 1;
    }
    return l_EncodedSize;
}

inline Int EncodeStringUtf16(const CodePoint* i_CodePointArray, U16Char* o_UString, Int i_BuffSize)
{
    const CodePoint* l_pCurCP = i_CodePointArray;
    U16Char* l_pDest = o_UString;
    Int l_CurBuffSize = o_UString ? i_BuffSize : 0;
    Int l_EncodedSize = 0;
    while (*l_pCurCP != 0)
    {
        Int l_Size = EncodeCodePointUtf16(*l_pCurCP, l_pDest, l_CurBuffSize);
        if (l_Size <= 0)
            return -1;
        l_pCurCP++;
        l_pDest += l_Size;
        l_CurBuffSize -= l_Size;
        l_EncodedSize += l_Size;
    }
    if (l_CurBuffSize > 0)
    {
        *l_pDest = 0;
    }
    l_EncodedSize += 1;
    return l_EncodedSize;
}

inline Int SafeEncodeStringUtf16(const CodePoint* i_CodePointArray, Int i_CodePointArraySize, U16Char* o_UString, Int i_BuffSize)
{
    const CodePoint* l_pCurCP = i_CodePointArray;
    U16Char* l_pDest = o_UString;
    Int l_CurBuffSize = o_UString ? i_BuffSize : 0;
    Int l_CurCPNum = i_CodePointArraySize;
    Int l_EncodedSize = 0;
    while (l_CurCPNum > 0 && *l_pCurCP != 0)
    {
        Int l_Size = EncodeCodePointUtf16(*l_pCurCP, l_pDest, l_CurBuffSize);
        if (l_Size <= 0)
            return -1;
        l_pCurCP++;
        l_CurCPNum--;
        l_pDest += l_Size;
        l_CurBuffSize -= l_Size;
        l_EncodedSize += l_Size;
    }
    if (l_CurCPNum > 0)
    {
        if (l_CurBuffSize > 0)
        {
            *l_pDest = 0;
        }
        l_EncodedSize += 1;
    }
    return l_EncodedSize;
}

inline Int EncodeStringUtf32(const CodePoint* i_CodePointArray, U32Char* o_UString, Int i_BuffSize)
{
    const CodePoint* l_pCurCP = i_CodePointArray;
    U32Char* l_pDest = o_UString;
    Int l_CurBuffSize = o_UString ? i_BuffSize : 0;
    Int l_EncodedSize = 0;
    while (*l_pCurCP != 0)
    {
        Int l_Size = EncodeCodePointUtf32(*l_pCurCP, l_pDest, l_CurBuffSize);
        if (l_Size <= 0)
            return -1;
        l_pCurCP++;
        l_pDest += l_Size;
        l_CurBuffSize -= l_Size;
        l_EncodedSize += l_Size;
    }
    if (l_CurBuffSize > 0)
    {
        *l_pDest = 0;
    }
    l_EncodedSize += 1;
    return l_EncodedSize;
}

inline Int SafeEncodeStringUtf32(const CodePoint* i_CodePointArray, Int i_CodePointArraySize, U32Char* o_UString, Int i_BuffSize)
{
    const CodePoint* l_pCurCP = i_CodePointArray;
    U32Char* l_pDest = o_UString;
    Int l_CurBuffSize = o_UString ? i_BuffSize : 0;
    Int l_CurCPNum = i_CodePointArraySize;
    Int l_EncodedSize = 0;
    while (l_CurCPNum > 0 && *l_pCurCP != 0)
    {
        Int l_Size = EncodeCodePointUtf32(*l_pCurCP, l_pDest, l_CurBuffSize);
        if (l_Size <= 0)
            return -1;
        l_pCurCP++;
        l_CurCPNum--;
        l_pDest += l_Size;
        l_CurBuffSize -= l_Size;
        l_EncodedSize += l_Size;
    }
    if (l_CurCPNum > 0)
    {
        if (l_CurBuffSize > 0)
        {
            *l_pDest = 0;
        }
        l_EncodedSize += 1;
    }
    return l_EncodedSize;
}

/*
    function overload version
*/
inline Int EncodeString(const CodePoint* i_CodePointArray, U8Char* o_pString, Int i_BuffSize)
{
    return EncodeStringUtf8(i_CodePointArray, o_pString, i_BuffSize);
}

inline Int SafeEncodeString(const CodePoint* i_CodePointArray, Int i_CodePointArraySize, U8Char* o_pString, Int i_BuffSize)
{
    return SafeEncodeStringUtf8(i_CodePointArray, i_CodePointArraySize, o_pString, i_BuffSize);
}

inline Int EncodeString(const CodePoint* i_CodePointArray, U16Char* o_pString, Int i_BuffSize)
{
    return EncodeStringUtf16(i_CodePointArray, o_pString, i_BuffSize);
}

inline Int SafeEncodeString(const CodePoint* i_CodePointArray, Int i_CodePointArraySize, U16Char* o_pString, Int i_BuffSize)
{
    return SafeEncodeStringUtf16(i_CodePointArray, i_CodePointArraySize, o_pString, i_BuffSize);
}

inline Int EncodeString(const CodePoint* i_CodePointArray, U32Char* o_pString, Int i_BuffSize)
{
    return EncodeStringUtf32(i_CodePointArray, o_pString, i_BuffSize);
}

inline Int SafeEncodeString(const CodePoint* i_CodePointArray, Int i_CodePointArraySize, U32Char* o_pString, Int i_BuffSize)
{
    return SafeEncodeStringUtf32(i_CodePointArray, i_CodePointArraySize, o_pString, i_BuffSize);
}

}
DOME_NAMESPACE_END