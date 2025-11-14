#ifndef __OS_STRING_H__
#define __OS_STRING_H__
#include "../typedefs.h"
#include "../error.h"
#include "../unicode.h"

DOME_NAMESPACE_BEGIN

class DOME_CORE_API OS_String
{
public:
    /*
        Calculate the size(in destination type unit) of converted string, include the end 0
        DESTTYPE and SRCTYPE can only be Char or WChar type
        i_pSrcStr:  the NULL ended source string
        RETURN: if there is invlide code point in the string, return -1,
                otherwise, return how many destination 
    */
    template <class DESTTYPE, class SRCTYPE>
    static Int TGetConvertedStrSize(const SRCTYPE* i_pSrcStr)
    {
        unicode::CodePoint l_Cp;
        Int l_CpLen;
        Int l_Result = 0;
        const SRCTYPE* l_pSrc = i_pSrcStr;
        const Int k_MaxEncodeBuff = 4;
        DESTTYPE l_Buff[k_MaxEncodeBuff];
        while (DM_TRUE)
        {
            l_Cp = unicode::DecodeCodePoint(l_pSrc, &l_CpLen);
            if (l_Cp < 0)
                return -1;
            l_pSrc += l_CpLen;
            l_CpLen = unicode::EncodeCodePoint(l_Cp, l_Buff, k_MaxEncodeBuff);
            if (l_CpLen > k_MaxEncodeBuff)
                return -1;
            if (l_Cp == 0)
                return l_Result + 1;
            l_Result += l_CpLen;
        }
        return -1;
    }

    /*
        Convert from NULL terminated source string to destination string
        o_pDest:            destination buffer
        i_DestBuffSize:     destination buffer character size, if 0, ignore the limitation
        i_pSrc:             source buffer pointer, the string should be NULL terminated
        return:             if successfull, return 0, otherwise, return -1
    */
    template <class DESTTYPE, class SRCTYPE>
    static Int TConvertStr(DESTTYPE* o_pDest, Int i_DestBuffSize, const SRCTYPE* i_pSrc)
    {
        unicode::CodePoint l_Cp;
        Int l_CpLen;
        DESTTYPE* l_pDst = o_pDest;
        const SRCTYPE* l_pSrc = i_pSrc;
        Int l_DestBuffUsed = 0;
        if (i_DestBuffSize == 0) i_DestBuffSize = DM_INT_MAX;
        while (DM_TRUE)
        {
            l_Cp = unicode::DecodeCodePoint(l_pSrc, &l_CpLen);
            if (l_Cp < 0)
                return -1;
            l_pSrc += l_CpLen;
            l_CpLen = unicode::EncodeCodePoint(l_Cp, l_pDst, i_DestBuffSize - l_DestBuffUsed);
            if (l_CpLen > (i_DestBuffSize - l_DestBuffUsed))
                return -1;
            if (l_Cp == 0)
                return 0;
            l_pDst += l_CpLen;
            l_DestBuffUsed += l_CpLen;
        }
        return -1;
    }

    /*
        Get string buffer size, in bytes, including the end 0
        return -1 if there is invalid code point
    */
    template<class T>
    static Int TGetStrBufferSize(const T* i_pString)
    {
        Int l_Count = 0;
        if (!i_pString) return DM_NULL;
        while (i_pString[l_Count] != 0)
        {
            ++l_Count;
        }
        ++l_Count;
        return l_Count * sizeof(T);
    }

    /*
        Get string character count, without ending 0
        return -1 if there is invalid code point
    */
    template<class T>
    static Int TGetStrCharCount(const T* i_pString)
    {
        Int l_Count = 0;
        if (!i_pString) return 0;
        while (i_pString[l_Count] != 0)
        {
            ++l_Count;
        }
        return l_Count;
    }

    /*
        Get string unicode code point count, without ending 0
        return -1 if there is invalid code point
    */
    template<class T>
    static Int TGetStrCodePointCount(const T* i_pString)
    {
        const T* l_pCur = i_pString;
        Int l_Count = 0;
        while (DM_TRUE)
        {
            Int l_CpLen = 0;
            unicode::CodePoint l_Cp = unicode::DecodeCodePoint(l_pCur, &l_CpLen);
            if (l_Cp < 0) return -1;
            if (l_Cp == 0) return l_Count;
            ++l_Count;
            l_pCur += l_CpLen;
        }
        return -1;
    }

    /*
        copy string
        return how many characters were copied, without ending 0
    */
    template<class T>
    static Int TStrCopy(T* o_pDest, const T* i_pSrc)
    {
        DOME_ASSERT(o_pDest && i_pSrc);
        const T* l_pSrc = i_pSrc;
        while(*o_pDest++ = *l_pSrc++)
        {
        }
        return l_pSrc - i_pSrc - 1;
    }

    /*
        safe copy string
        return how many characters were copied, without ending 0
    */
    template<class T>
    static Int TSafeStrCopy(T* o_pDest, Int i_DestBuffSize, const T* i_pSrc)
    {
        DOME_ASSERT(o_pDest && i_pSrc);
        DOME_ASSERT(i_DestBuffSize > 0);
        Int l_MaxChar = i_DestBuffSize - 1;
        Int l_CharCopied = 0;
        while(true)
        {
            if(i_pSrc[l_CharCopied] == 0 || l_CharCopied >= l_MaxChar)
                break;
            o_pDest[l_CharCopied] = i_pSrc[l_CharCopied];
            ++ l_CharCopied;
        }
        o_pDest[l_CharCopied] = 0;
        return l_CharCopied;
    }

    /*
        Copy N characters
    */
    template<class T>
    static void TCopyChars(T* o_pDest, const T* i_pSrc, Int i_Count)
    {
        while(i_Count--)
        {
            *o_pDest++ = *i_pSrc++;
        }
    }


    /*
        concat string
        return 0 if successful
        return -1 if failed
    */
    template<class T>
    static Int TStrCat(T* io_pDest, Int i_DestBuffSize, const T*i_pSrc)
    {
        DOME_ASSERT(io_pDest);
        DOME_ASSERT(i_pSrc);
        Int l_DstLen = GetStrCharCount(io_pDest);
        Int l_SrcLen = GetStrCharCount(i_pSrc);
        if (i_DestBuffSize < (l_DstLen + l_SrcLen + 1))
            return -1;
        for (Int i = 0; i <= l_SrcLen; ++i)
        {
            io_pDest[l_DstLen + i] = i_pSrc[i];
        }
        return 0;
    }

    /*
        o_pDest:        the destination buffer
        i_pSrc:         the source buffer
        i_FirstChar:    the index of the first char
        i_NumChar:      this can be negative
        return 0 if successful
        return -1 if failed
    */
    template<class T>
    static Int TSubStr(T* o_pDest, const T* i_pSrc, Int i_FirstChar, Int i_NumChar)
    {
        Int l_StrLen = GetStrCharCount(i_pSrc);
        if (i_FirstChar < 0) return -1;
        if (i_FirstChar >= l_StrLen) return -1;
        if (i_NumChar <= 0) return -1;
        if ((i_FirstChar + i_NumChar) > l_StrLen) return -1;
        for (Int i = 0; i < i_NumChar; ++i)
        {
            o_pDest[i] = i_pSrc[i_FirstChar + i];
        }
        o_pDest[i_NumChar] = 0;
        return 0;
    }

    /*
        return 0 if same
        return -1 if i_pStr0 is less than i_pStr1
        return 1 if i_pStr1 is greater than i_pStr1
    */
    template<class T>
    static Int TCompareStr(const T* i_pString1, const T* i_pString2)
    {
        if(!i_pString1 && !i_pString2)
            return 0;
        if(!i_pString1)
            return -1;
        if(!i_pString2)
            return 1;
        while(*i_pString1 && *i_pString2)
        {
            if(*i_pString1 < *i_pString2)
                return -1;
            else if(*i_pString1 > *i_pString2)
                return 1;
            i_pString1 ++;
            i_pString2 ++;
        }
        if(*i_pString1 == 0 && *i_pString2 == 0)
            return 0;
        else if(*i_pString1 == 0)
            return -1;
        else
            return 1;
    }

    /*
        return 0 if same
        return -1 if i_pStr0 is less than i_pStr1
        return 1 if i_pStr1 is greater than i_pStr1
    */
    template<class T>
    static Int TCompareChars(const T* i_pCharArray1, const T* i_pCharArray2, Int i_Count)
    {
        for(Int i = 0; i < i_Count; ++i)
        {
            if(i_pCharArray1[i] < i_pCharArray2[i])
                return -1;
            else if(i_pCharArray1[i] > i_pCharArray2[i])
                return 1;
        }
        return 0;
    }

    /*
        find i_pStr in i_pSrcStr, if found, return the index of the first char
        otherwise return -1.
    */
    template<class T>
    static Int TFindStr(const T* i_pPatern, Int i_PaternCount, const T* i_pString, Int i_StringLen)
    {
        if(i_PaternCount <= 0)
            i_PaternCount = TGetStrCharCount(i_pPatern);
        DOME_ASSERT(i_PaternCount > 0);
        if(i_StringLen <= 0)
            i_StringLen = TGetStrCharCount(i_pString);
        DOME_ASSERT(i_StringLen > 0);
        
        DOME_ASSERT(i_PaternCount <= i_StringLen);

        Int l_From = 0;
        Int l_To = i_StringLen - i_PaternCount;
        for(Int i = l_From; i <= l_To; ++i)
        {
            Int l_CompResult = TCompareChars(i_pPatern, i_pString + i, i_PaternCount);
            if(l_CompResult == 0)
                return i;
        }
        return -1;
    }

    /*
        find i_pStr in i_pSrcStr, if found, return the index of the first char
        otherwise return -1.
    */
    template<class T>
    static Int TFindStrFromBack(const T* i_pPatern, Int i_PaternCount, const T* i_pString, Int i_StringLen)
    {
        if(i_PaternCount <= 0)
            i_PaternCount = TGetStrCharCount(i_pPatern);
        DOME_ASSERT(i_PaternCount > 0);
        if(i_StringLen <= 0)
            i_StringLen = TGetStrCharCount(i_pString);
        DOME_ASSERT(i_StringLen > 0);
        
        DOME_ASSERT(i_PaternCount <= i_StringLen);

        Int l_From = 0;
        Int l_To = i_StringLen - i_PaternCount;
        for(Int i = l_To; i >= l_From; --i)
        {
            Int l_CompResult = TCompareChars(i_pPatern, i_pString + i, i_PaternCount);
            if(l_CompResult == 0)
                return i;
        }
        return -1;
    }

    /*
        Djb2 hash algorithm
        ref: http://programmers.stackexchange.com/questions/49550/which-hashing-algorithm-is-best-for-uniqueness-and-speed
    */
    template<class CHAR_T>
    static Uint TStrHashDjb2_Uint(const CHAR_T* i_pString)
    {
        if(!i_pString) return 0;
        Uint hash = 5381;
        Int c;
        while (c = *i_pString++)
            hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
        return hash;
    }

    /*
        Djb2a hash algorithm
    */
    template<class CHAR_T>
    static Uint TStrHashDjb2a_Uint(const CHAR_T* i_pString)
    {
        if(!i_pString) return 0;
        Uint hash = 5381;
        Int c;
        while (c = *i_pString++)
            hash = ((hash << 5) ^ hash) + c; /* hash * 33 + c */
        return hash;
    }

    /*
        FNV-1 and FNV-1a string hash algorithm

        FNV-1 pseudocode
        hash = FNV_offset_basis
        for each byte_of_data to be hashed
            hash = hash ¡Á FNV_prime
            hash = hash XOR byte_of_data
        return hash

        FNV-1a pseudocode
        hash = FNV_offset_basis
        for each byte_of_data to be hashed
            hash = hash XOR byte_of_data
            hash = hash ¡Á FNV_prime
        return hash

        Where the constants FNV_offset_basis and FNV_prime depend on the return hash size you want:
        Hash Size    Prime                       Offset
        ===========  =========================== =================================
        32-bit       16777619                    2166136261
        64-bit       1099511628211               14695981039346656037
        128-bit      309485009821345068724781371 144066263297769815596495629667062367629
        256-bit
            prime: 2^168 + 2^8 + 0x63 = 374144419156711147060143317175368453031918731002211
            offset: 100029257958052580907070968620625704837092796014241193945225284501741471925557
        512-bit
            prime: 2^344 + 2^8 + 0x57 = 35835915874844867368919076489095108449946327955754392558399825615420669938882575126094039892345713852759
            offset: 9659303129496669498009435400716310466090418745672637896108374329434462657994582932197716438449813051892206539805784495328239340083876191928701583869517785
        1024-bit
            prime: 2^680 + 2^8 + 0x8d = 5016456510113118655434598811035278955030765345404790744303017523831112055108147451509157692220295382716162651878526895249385292291816524375083746691371804094271873160484737966720260389217684476157468082573
            offset: 1419779506494762106872207064140321832088062279544193396087847491461758272325229673230371772250864096521202355549365628174669108571814760471015076148029755969804077320157692458563003215304957150157403644460363550505412711285966361610267868082893823963790439336411086884584107735010676915
    */
    template <class CHAR_T>
    static Uint TStrHashFnv1_Uint(const CHAR_T* i_pString)
    {
        if(!i_pString) return 0;
#if DOME_IS_32BIT
        Uint hash = 2166136261;
        Int c;
        while (c = *i_pString++)
        {
            hash = hash * 16777619;
            hash = hash ^ c;
        }
        return hash;
#elif DOME_IS_64BIT
        Uint hash = 14695981039346656037;
        Int c;
        while (c = *i_pString++)
        {
            hash = hash * 1099511628211;
            hash = hash ^ c;
        }
        return hash;
#else
#error This hardware architecture is not supported
#endif
    }

    template <class CHAR_T>
    static Uint TStrHashFnv1a_Uint(const CHAR_T* i_pString)
    {
        if(!i_pString) return 0;
#if DOME_IS_32BIT
        Uint hash = 2166136261;
        Int c;
        while (c = *i_pString++)
        {
            hash = hash ^ c;
            hash = hash * 16777619;
        }
        return hash;
#elif DOME_IS_64BIT
        Uint hash = 14695981039346656037;
        Int c;
        while (c = *i_pString++)
        {
            hash = hash ^ c;
            hash = hash * 1099511628211;
        }
        return hash;
#else
#error This hardware architecture is not supported
#endif
    }

    /*
        convert string to Integer
    */
    template<class CHAR_T, class INTEGER_T>
    static INTEGER_T TStrToInteger(const CHAR_T* i_pString, INTEGER_T i_Default = 0, Bool* o_bSucc = DM_NULL, Int* o_Sign = DM_NULL)
    {
        Bool l_bSucc = DM_TRUE;
        INTEGER_T l_Value = 0;
        Int l_Sign = 1;
        Int l_CpLen = 0;
        unicode::CodePoint l_CP;
        const CHAR_T* l_pCur = i_pString;
        Int l_CurrState = 0;    //0:skip white space  1:read number

        if (!l_pCur)
        {
            if(o_bSucc) *o_bSucc = DM_FALSE;
            if(o_Sign)  *o_Sign = 1;
            return i_Default;
        }

        while (DM_TRUE)
        {
            l_CP = unicode::DecodeCodePoint(l_pCur, &l_CpLen);
            if(l_CP == -1)
            {
                l_bSucc = DM_FALSE;
                break;
            }
            if(l_CP == 0)
                break;

            if(l_CurrState == 0)
            {
                if(l_CP == ' ' || l_CP == '\t')
                {
                    l_pCur += l_CpLen;
                }
                else if(l_CP == '+')
                {
                    l_pCur += l_CpLen;
                    l_Sign = 1;
                    l_CurrState = 1;
                }
                else if(l_CP == '-')
                {
                    l_pCur += l_CpLen;
                    l_Sign = -1;
                    l_CurrState = 1;
                }
                else if(l_CP >= '0' && l_CP <= '9')
                {
                    l_CurrState = 1;
                }
                else
                {
                    l_bSucc = DM_FALSE;
                    break;
                }
            }
            else if (l_CurrState == 1)
            {
                if(l_CP >= '0' && l_CP <= '9')
                {
                    l_Value *= 10;
                    l_Value += l_CP - '0';
                    l_pCur += l_CpLen;
                }
                else if(l_CP == ' ' || l_CP == '\t')
                {
                    break;
                }
                else
                {
                    l_bSucc = DM_FALSE;
                    break;
                }
            }
        }

        l_Value *= (INTEGER_T)l_Sign;

        if(!l_bSucc)
        {
            l_Value = i_Default;
        }

        if(o_bSucc)
            *o_bSucc = l_bSucc;

        if(o_Sign)
            *o_Sign = l_Sign;

        return l_Value;
    }

    /*
        convert hex string to Integer
    */
    template<class CHAR_T, class INTEGER_T>
    static INTEGER_T THexStrToInteger(const CHAR_T* i_pString, INTEGER_T i_Default = 0, Bool* o_bSucc = DM_NULL, Int* o_Sign = DM_NULL)
    {
        Bool l_bSucc = DM_TRUE;
        INTEGER_T l_Value = 0;
        Int l_Sign = 1;
        Int l_CpLen = 0;
        unicode::CodePoint l_CP;
        const CHAR_T* l_pCur = i_pString;
        Int l_CurrState = 0;    //0:skip white space  1:read 0x or 0X  2: read number

        if (!l_pCur)
        {
            if(o_bSucc) *o_bSucc = DM_FALSE;
            if(o_Sign)  *o_Sign = 1;
            return i_Default;
        }

        while (DM_TRUE)
        {
            l_CP = unicode::DecodeCodePoint(l_pCur, &l_CpLen);
            if(l_CP == -1)
            {
                l_bSucc = DM_FALSE;
                break;
            }
            if(l_CP == 0)
                break;

            if(l_CurrState == 0)
            {
                if(l_CP == ' ' || l_CP == '\t')
                {
                    l_pCur += l_CpLen;
                }
                else if(l_CP == '+')
                {
                    l_pCur += l_CpLen;
                    l_Sign = 1;
                    l_CurrState = 1;
                }
                else if(l_CP == '-')
                {
                    l_pCur += l_CpLen;
                    l_Sign = -1;
                    l_CurrState = 1;
                }
                else if(l_CP == '0')
                {
                    l_pCur += l_CpLen;
                    l_CurrState = 1;
                }
                else
                {
                    l_bSucc = DM_FALSE;
                    break;
                }
            }
            else if (l_CurrState == 1)
            {
                if (l_CP == 'x' || l_CP == 'X')
                {
                    l_pCur += l_CpLen;
                    l_CurrState = 2;
                }
                else
                {
                    l_bSucc = DM_FALSE;
                    break;
                }
            }
            else if (l_CurrState == 2)
            {
                if(l_CP >= '0' && l_CP <= '9')
                {
                    l_Value *= 16;
                    l_Value += l_CP - '0';
                    l_pCur += l_CpLen;
                }
                else if (l_CP >= 'a' && l_CP <= 'f')
                {
                    l_Value *= 16;
                    l_Value += 10 + l_CP - 'a';
                    l_pCur += l_CpLen;
                }
                else if (l_CP >= 'A' && l_CP <= 'F')
                {
                    l_Value *= 16;
                    l_Value += 10 + l_CP - 'A';
                    l_pCur += l_CpLen;
                }
                else if(l_CP == ' ' || l_CP == '\t')
                {
                    break;
                }
                else
                {
                    l_bSucc = DM_FALSE;
                    break;
                }
            }
        }

        l_Value *= (INTEGER_T)l_Sign;

        if(!l_bSucc)
        {
            l_Value = i_Default;
        }

        if(o_bSucc)
            *o_bSucc = l_bSucc;

        if(o_Sign)
            *o_Sign = l_Sign;

        return l_Value;
    }

    /*
        convert string to Integer
    */
    template<class CHAR_T, class FLOAT_T>
    static FLOAT_T TStrToFloat(const CHAR_T* i_pString, FLOAT_T i_Default = 0.0f, Bool* o_bSucc = DM_NULL, Int* o_Sign = DM_NULL)
    {
        Bool l_bSucc = DM_TRUE;
        FLOAT_T l_Value = 0;
        Int l_Sign = 1;
        Int l_CpLen = 0;
        unicode::CodePoint l_CP;
        const CHAR_T* l_pCur = i_pString;
        Int l_CurrState = 0;    //0:skip white space  1:read integer part  2:read fraction part
        Int l_DivideFactor = 10;

        if (!l_pCur)
        {
            if(o_bSucc) *o_bSucc = DM_FALSE;
            if(o_Sign)  *o_Sign = 1;
            return i_Default;
        }

        while (DM_TRUE)
        {
            l_CP = unicode::DecodeCodePoint(l_pCur, &l_CpLen);
            if(l_CP == -1)
            {
                l_bSucc = DM_FALSE;
                break;
            }
            if(l_CP == 0)
                break;

            if(l_CurrState == 0)
            {
                if(l_CP == ' ' || l_CP == '\t')
                {
                    l_pCur += l_CpLen;
                }
                else if(l_CP == '+')
                {
                    l_pCur += l_CpLen;
                    l_Sign = 1;
                    l_CurrState = 1;
                }
                else if(l_CP == '-')
                {
                    l_pCur += l_CpLen;
                    l_Sign = -1;
                    l_CurrState = 1;
                }
                else if(l_CP >= '0' && l_CP <= '9')
                {
                    l_CurrState = 1;
                }
                else
                {
                    l_bSucc = DM_FALSE;
                    break;
                }
            }
            else if (l_CurrState == 1)
            {
                if(l_CP >= '0' && l_CP <= '9')
                {
                    l_Value *= 10;
                    l_Value += l_CP - '0';
                    l_pCur += l_CpLen;
                }
                else if (l_CP == '.')
                {
                    l_CurrState = 2;
                    l_pCur += l_CpLen;
                    l_DivideFactor = 10;
                }
                else if(l_CP == ' ' || l_CP == '\t')
                {
                    break;
                }
                else
                {
                    l_bSucc = DM_FALSE;
                    break;
                }
            }
            else if (l_CurrState == 2)
            {
                if(l_CP >= '0' && l_CP <= '9')
                {
                    l_Value += (l_CP - '0') / (FLOAT_T)l_DivideFactor;
                    l_DivideFactor *= 10;
                    l_pCur += l_CpLen;
                }
                else if(l_CP == ' ' || l_CP == '\t' || l_CP == 'f' || l_CP == 'F')
                {
                    break;
                }
                else
                {
                    l_bSucc = DM_FALSE;
                    break;
                }
            }
        }

        l_Value *= (FLOAT_T)l_Sign;

        if(!l_bSucc)
        {
            l_Value = i_Default;
        }

        if(o_bSucc)
            *o_bSucc = l_bSucc;

        if(o_Sign)
            *o_Sign = l_Sign;

        return l_Value;
    }

    /*
        return the number of character the result string has, include the end 0
    */
    static Int StrVNSPrintf(U8Char* o_pString, Int i_BuffSize, const U8Char* i_Format, va_list i_Args);
    static Int StrVNSPrintf(U16Char* o_pString, Int i_BuffSize, const U16Char* i_Format, va_list i_Args);
    static Int StrVNSPrintf(U32Char* o_pString, Int i_BuffSize, const U32Char* i_Format, va_list i_Args);

    static Int StrVSScanf(const U8Char* i_pString, const U8Char* i_Format, va_list o_Args);
    static Int StrVSScanf(const U16Char* i_pString, const U16Char* i_Format, va_list o_Args);
    static Int StrVSScanf(const U32Char* i_pString, const U32Char* i_Format, va_list o_Args);

    static Int StrFormat(U8Char* o_pString, Int i_BuffSize, const U8Char* i_Format, ...);
    static Int StrFormat(U16Char* o_pString, Int i_BuffSize, const U16Char* i_Format, ...);
    static Int StrFormat(U32Char* o_pString, Int i_BuffSize, const U32Char* i_Format, ...);

    static Int StrScan(const U8Char* i_pString, const U8Char* i_Format, ...);
    static Int StrScan(const U16Char* i_pString, const U16Char* i_Format, ...);
    static Int StrScan(const U32Char* i_pString, const U32Char* i_Format, ...);
};

DOME_NAMESPACE_END

#endif//__OS_STRING_H__