/*
    filename:       operators.cpp
    author:         Ming Dong
    date:           2016-Jul-11
    description:    operators 
*/

#include "../../public/math/operators.h"

DOME_NAMESPACE_BEGIN
namespace Math
{
    // return value >= 0 means how many char copied, -1 means the buffer is not big enough
    Int ReadUntilCommaOrZero(const Char* i_pString, Char* o_Buff, Int i_BuffSize)
    {
        Int i;
        for (i = 0; i < i_BuffSize - 1; ++i)
        {
            if (i_pString[i] == ',' || i_pString[i] == '\0')
            {
                o_Buff[i] = 0;
                return i;
            }
            o_Buff[i] = i_pString[i];
        }
        return -1;
    }


    DOME_CORE_API DResult MathToDString(F32 i_Value, DString& o_Str)
    {
        Char l_StrBuff[128];
        OS_String::StrFormat(l_StrBuff, sizeof(l_StrBuff), "%f", i_Value);
        o_Str = l_StrBuff;
        return R_SUCCESS;
    }

    DOME_CORE_API DResult MathToDString(const DVector2f& i_Value, DString& o_Str)
    {
        Char l_StrBuff[128];
        OS_String::StrFormat(l_StrBuff, sizeof(l_StrBuff), "%f,%f", i_Value.x, i_Value.y);
        o_Str = l_StrBuff;
        return R_SUCCESS;
    }

    DOME_CORE_API DResult MathToDString(const DVector3f& i_Value, DString& o_Str)
    {
        Char l_StrBuff[128];
        OS_String::StrFormat(l_StrBuff, sizeof(l_StrBuff), "%f,%f,%f", i_Value.x, i_Value.y, i_Value.z);
        o_Str = l_StrBuff;
        return R_SUCCESS;
    }

    DOME_CORE_API DResult MathToDString(const DVector4f& i_Value, DString& o_Str)
    {
        Char l_StrBuff[128];
        OS_String::StrFormat(l_StrBuff, sizeof(l_StrBuff), "%f,%f,%f,%f", i_Value.x, i_Value.y, i_Value.z, i_Value.w);
        o_Str = l_StrBuff;
        return R_SUCCESS;
    }

    DOME_CORE_API DResult MathToDString(const DVectorLut1f& i_Value, DString& o_Str)
    {
        Char l_StrBuff[512];
        OS_String::StrFormat(l_StrBuff, sizeof(l_StrBuff), "%d", 1);
        o_Str = l_StrBuff;
        for (Int c = 0; c < 1; ++c)
        {
            OS_String::StrFormat(l_StrBuff, sizeof(l_StrBuff), ",%f", i_Value.getNumSamplePerUnit(c));
            o_Str += l_StrBuff;

            OS_String::StrFormat(l_StrBuff, sizeof(l_StrBuff), ",%d", i_Value.getNumControlPoint(c));
            o_Str += l_StrBuff;

            for (Int i = 0; i < i_Value.getNumControlPoint(c); ++i)
            {
                DVectorLut1f::LutType::_CurveType l_CurveType;
                DVector2f l_Pos, l_LPos, l_RPos;
                i_Value.getControlPoint(c, i, l_CurveType, l_Pos.x, l_LPos.x, l_RPos.x, l_Pos.y, l_LPos.y, l_RPos.y);
                OS_String::StrFormat(l_StrBuff, sizeof(l_StrBuff), ",%d,%f,%f,%f,%f,%f,%f", (S32)l_CurveType, l_Pos.x, l_Pos.y, l_LPos.x, l_LPos.y, l_RPos.x, l_RPos.y);
                o_Str += l_StrBuff;
            }
        }
        return R_SUCCESS;
    }

    DOME_CORE_API DResult MathToDString(const DVectorLut2f& i_Value, DString& o_Str)
    {
        Char l_StrBuff[512];
        OS_String::StrFormat(l_StrBuff, sizeof(l_StrBuff), "%d", 2);
        o_Str = l_StrBuff;
        for (Int c = 0; c < 2; ++c)
        {
            OS_String::StrFormat(l_StrBuff, sizeof(l_StrBuff), ",%f", i_Value.getNumSamplePerUnit(c));
            o_Str += l_StrBuff;

            OS_String::StrFormat(l_StrBuff, sizeof(l_StrBuff), ",%d", i_Value.getNumControlPoint(c));
            o_Str += l_StrBuff;

            for (Int i = 0; i < i_Value.getNumControlPoint(c); ++i)
            {
                DVectorLut1f::LutType::_CurveType l_CurveType;
                DVector2f l_Pos, l_LPos, l_RPos;
                i_Value.getControlPoint(c, i, l_CurveType, l_Pos.x, l_LPos.x, l_RPos.x, l_Pos.y, l_LPos.y, l_RPos.y);
                OS_String::StrFormat(l_StrBuff, sizeof(l_StrBuff), ",%d,%f,%f,%f,%f,%f,%f", (S32)l_CurveType, l_Pos.x, l_Pos.y, l_LPos.x, l_LPos.y, l_RPos.x, l_RPos.y);
                o_Str += l_StrBuff;
            }
        }
        return R_SUCCESS;
    }

    DOME_CORE_API DResult MathToDString(const DVectorLut3f& i_Value, DString& o_Str)
    {
        Char l_StrBuff[512];
        OS_String::StrFormat(l_StrBuff, sizeof(l_StrBuff), "%d", 3);
        o_Str = l_StrBuff;
        for (Int c = 0; c < 3; ++c)
        {
            OS_String::StrFormat(l_StrBuff, sizeof(l_StrBuff), ",%f", i_Value.getNumSamplePerUnit(c));
            o_Str += l_StrBuff;

            OS_String::StrFormat(l_StrBuff, sizeof(l_StrBuff), ",%d", i_Value.getNumControlPoint(c));
            o_Str += l_StrBuff;

            for (Int i = 0; i < i_Value.getNumControlPoint(c); ++i)
            {
                DVectorLut1f::LutType::_CurveType l_CurveType;
                DVector2f l_Pos, l_LPos, l_RPos;
                i_Value.getControlPoint(c, i, l_CurveType, l_Pos.x, l_LPos.x, l_RPos.x, l_Pos.y, l_LPos.y, l_RPos.y);
                OS_String::StrFormat(l_StrBuff, sizeof(l_StrBuff), ",%d,%f,%f,%f,%f,%f,%f", (S32)l_CurveType, l_Pos.x, l_Pos.y, l_LPos.x, l_LPos.y, l_RPos.x, l_RPos.y);
                o_Str += l_StrBuff;
            }
        }
        return R_SUCCESS;
    }

    DOME_CORE_API DResult MathToDString(const DVectorLut4f& i_Value, DString& o_Str)
    {
        Char l_StrBuff[512];
        OS_String::StrFormat(l_StrBuff, sizeof(l_StrBuff), "%d", 4);
        o_Str = l_StrBuff;
        for (Int c = 0; c < 4; ++c)
        {
            OS_String::StrFormat(l_StrBuff, sizeof(l_StrBuff), ",%f", i_Value.getNumSamplePerUnit(c));
            o_Str += l_StrBuff;

            OS_String::StrFormat(l_StrBuff, sizeof(l_StrBuff), ",%d", i_Value.getNumControlPoint(c));
            o_Str += l_StrBuff;

            for (Int i = 0; i < i_Value.getNumControlPoint(c); ++i)
            {
                DVectorLut1f::LutType::_CurveType l_CurveType;
                DVector2f l_Pos, l_LPos, l_RPos;
                i_Value.getControlPoint(c, i, l_CurveType, l_Pos.x, l_LPos.x, l_RPos.x, l_Pos.y, l_LPos.y, l_RPos.y);
                OS_String::StrFormat(l_StrBuff, sizeof(l_StrBuff), ",%d,%f,%f,%f,%f,%f,%f", (S32)l_CurveType, l_Pos.x, l_Pos.y, l_LPos.x, l_LPos.y, l_RPos.x, l_RPos.y);
                o_Str += l_StrBuff;
            }
        }
        return R_SUCCESS;
    }


    DOME_CORE_API DResult MathFromDString(const DString& i_Str, F32& o_Value)
    {
        const Char* l_ptr = i_Str.c_str();
        Char l_Buff[1024];
        Int l_nRead;

        l_nRead = ReadUntilCommaOrZero(i_Str.c_str(), l_Buff, sizeof(l_Buff));
        l_ptr += l_nRead;
        if(l_nRead <= 0 || *l_ptr != 0) return R_FAILED;

        o_Value = OS_String::TStrToFloat<Char, F32>(l_Buff);
        return R_SUCCESS;
    }

    DOME_CORE_API DResult MathFromDString(const DString& i_Str, DVector2f& o_Value)
    {
        const Char* l_ptr = i_Str.c_str();
        Char l_Buff[1024];
        Int l_nRead;

        l_nRead = ReadUntilCommaOrZero(l_ptr, l_Buff, sizeof(l_Buff));
        l_ptr += l_nRead;
        if(l_nRead <= 0 || *l_ptr != ',') return R_FAILED;
        l_ptr++;
        o_Value.x = OS_String::TStrToFloat<Char, F32>(l_Buff);

        l_nRead = ReadUntilCommaOrZero(l_ptr, l_Buff, sizeof(l_Buff));
        l_ptr += l_nRead;
        if(l_nRead <= 0 || *l_ptr != 0) return R_FAILED;
        o_Value.y = OS_String::TStrToFloat<Char, F32>(l_Buff);

        return R_SUCCESS;
    }

    DOME_CORE_API DResult MathFromDString(const DString& i_Str, DVector3f& o_Value)
    {
        const Char* l_ptr = i_Str.c_str();
        Char l_Buff[1024];
        Int l_nRead;

        l_nRead = ReadUntilCommaOrZero(l_ptr, l_Buff, sizeof(l_Buff));
        l_ptr += l_nRead;
        if(l_nRead <= 0 || *l_ptr != ',') return R_FAILED;
        l_ptr++;
        o_Value.x = OS_String::TStrToFloat<Char, F32>(l_Buff);

        l_nRead = ReadUntilCommaOrZero(l_ptr, l_Buff, sizeof(l_Buff));
        l_ptr += l_nRead;
        if(l_nRead <= 0 || *l_ptr != ',') return R_FAILED;
        l_ptr++;
        o_Value.y = OS_String::TStrToFloat<Char, F32>(l_Buff);

        l_nRead = ReadUntilCommaOrZero(l_ptr, l_Buff, sizeof(l_Buff));
        l_ptr += l_nRead;
        if(l_nRead <= 0 || *l_ptr != 0) return R_FAILED;
        o_Value.z = OS_String::TStrToFloat<Char, F32>(l_Buff);

        return R_SUCCESS;
    }

    DOME_CORE_API DResult MathFromDString(const DString& i_Str, DVector4f& o_Value)
    {
        const Char* l_ptr = i_Str.c_str();
        Char l_Buff[1024];
        Int l_nRead;

        l_nRead = ReadUntilCommaOrZero(l_ptr, l_Buff, sizeof(l_Buff));
        l_ptr += l_nRead;
        if(l_nRead <= 0 || *l_ptr != ',') return R_FAILED;
        l_ptr++;
        o_Value.x = OS_String::TStrToFloat<Char, F32>(l_Buff);

        l_nRead = ReadUntilCommaOrZero(l_ptr, l_Buff, sizeof(l_Buff));
        l_ptr += l_nRead;
        if(l_nRead <= 0 || *l_ptr != ',') return R_FAILED;
        l_ptr++;
        o_Value.y = OS_String::TStrToFloat<Char, F32>(l_Buff);

        l_nRead = ReadUntilCommaOrZero(l_ptr, l_Buff, sizeof(l_Buff));
        l_ptr += l_nRead;
        if(l_nRead <= 0 || *l_ptr != ',') return R_FAILED;
        l_ptr++;
        o_Value.z = OS_String::TStrToFloat<Char, F32>(l_Buff);

        l_nRead = ReadUntilCommaOrZero(l_ptr, l_Buff, sizeof(l_Buff));
        l_ptr += l_nRead;
        if(l_nRead <= 0 || *l_ptr != 0) return R_FAILED;
        o_Value.w = OS_String::TStrToFloat<Char, F32>(l_Buff);

        return R_SUCCESS;

    }

    template <class VLTYPE>
    DResult TVectorLutFromString(const Char* i_pString, Int i_nCurve, VLTYPE& o_Value)
    {
        Char l_Buff[1024];
        const Char* l_ptr = i_pString;
        Int l_nRead;

        o_Value.reset();
        for (Int i = 0; i < i_nCurve; ++i)
        {
            F32 l_Spu;
            Int l_NumCp;

            if(*l_ptr != ',') return R_FAILED;
            l_ptr ++;
            l_nRead = ReadUntilCommaOrZero(l_ptr, l_Buff, sizeof(l_Buff));
            l_ptr += l_nRead;
            if(l_nRead <= 0) return R_FAILED;
            l_Spu = OS_String::TStrToFloat<Char, F32>(l_Buff);

            o_Value.setNumSamplePerUnit(i, l_Spu);

            if(*l_ptr != ',') return R_FAILED;
            l_ptr ++;
            l_nRead = ReadUntilCommaOrZero(l_ptr, l_Buff, sizeof(l_Buff));
            l_ptr += l_nRead;
            if(l_nRead <= 0) return R_FAILED;
            l_NumCp = OS_String::TStrToInteger<Char, Int>(l_Buff);

            for (Int cp = 0; cp < l_NumCp; ++cp)
            {
                Int l_CurveType;
                DVector2f l_Pos, l_LPos, l_RPos;

                if(*l_ptr != ',') return R_FAILED;
                l_ptr ++;
                l_nRead = ReadUntilCommaOrZero(l_ptr, l_Buff, sizeof(l_Buff));
                l_ptr += l_nRead;
                if(l_nRead <= 0) return R_FAILED;
                l_CurveType = OS_String::TStrToInteger<Char, Int>(l_Buff);

                if(*l_ptr != ',') return R_FAILED;
                l_ptr ++;
                l_nRead = ReadUntilCommaOrZero(l_ptr, l_Buff, sizeof(l_Buff));
                l_ptr += l_nRead;
                if(l_nRead <= 0) return R_FAILED;
                l_Pos.x = OS_String::TStrToFloat<Char, F32>(l_Buff);

                if(*l_ptr != ',') return R_FAILED;
                l_ptr ++;
                l_nRead = ReadUntilCommaOrZero(l_ptr, l_Buff, sizeof(l_Buff));
                l_ptr += l_nRead;
                if(l_nRead <= 0) return R_FAILED;
                l_Pos.y = OS_String::TStrToFloat<Char, F32>(l_Buff);

                if(*l_ptr != ',') return R_FAILED;
                l_ptr ++;
                l_nRead = ReadUntilCommaOrZero(l_ptr, l_Buff, sizeof(l_Buff));
                l_ptr += l_nRead;
                if(l_nRead <= 0) return R_FAILED;
                l_LPos.x = OS_String::TStrToFloat<Char, F32>(l_Buff);

                if(*l_ptr != ',') return R_FAILED;
                l_ptr ++;
                l_nRead = ReadUntilCommaOrZero(l_ptr, l_Buff, sizeof(l_Buff));
                l_ptr += l_nRead;
                if(l_nRead <= 0) return R_FAILED;
                l_LPos.y = OS_String::TStrToFloat<Char, F32>(l_Buff);

                if(*l_ptr != ',') return R_FAILED;
                l_ptr ++;
                l_nRead = ReadUntilCommaOrZero(l_ptr, l_Buff, sizeof(l_Buff));
                l_ptr += l_nRead;
                if(l_nRead <= 0) return R_FAILED;
                l_RPos.x = OS_String::TStrToFloat<Char, F32>(l_Buff);

                if(*l_ptr != ',') return R_FAILED;
                l_ptr ++;
                l_nRead = ReadUntilCommaOrZero(l_ptr, l_Buff, sizeof(l_Buff));
                l_ptr += l_nRead;
                if(l_nRead <= 0) return R_FAILED;
                l_RPos.y = OS_String::TStrToFloat<Char, F32>(l_Buff);

                o_Value.addControlPoint(i, (typename VLTYPE::LutType::_CurveType)l_CurveType, 
                    l_Pos.x, l_LPos.x, l_RPos.x,
                    l_Pos.y, l_LPos.y, l_RPos.y);
            }
        }
        return R_SUCCESS;
    }

    DOME_CORE_API DResult MathFromDString(const DString& i_Str, DVectorLut1f& o_Value)
    {
        Char l_Buff[1024];
        const Char* l_ptr = i_Str.c_str();
        Int l_nRead, l_nCurve;

        l_nRead = ReadUntilCommaOrZero(l_ptr, l_Buff, sizeof(l_Buff));
        l_ptr += l_nRead;
        if(l_nRead <= 0) return R_FAILED;
        l_nCurve = OS_String::TStrToInteger<Char, Int>(l_Buff);
        DOME_ASSERT(l_nCurve == 1);
        
        return TVectorLutFromString<DVectorLut1f>(l_ptr, l_nCurve, o_Value);
    }

    DOME_CORE_API DResult MathFromDString(const DString& i_Str, DVectorLut2f& o_Value)
    {
        Char l_Buff[1024];
        const Char* l_ptr = i_Str.c_str();
        Int l_nRead, l_nCurve;

        l_nRead = ReadUntilCommaOrZero(l_ptr, l_Buff, sizeof(l_Buff));
        l_ptr += l_nRead;
        if(l_nRead <= 0) return R_FAILED;
        l_nCurve = OS_String::TStrToInteger<Char, Int>(l_Buff);
        DOME_ASSERT(l_nCurve == 2);
        
        return TVectorLutFromString<DVectorLut2f>(l_ptr, l_nCurve, o_Value);
    }

    DOME_CORE_API DResult MathFromDString(const DString& i_Str, DVectorLut3f& o_Value)
    {
        Char l_Buff[1024];
        const Char* l_ptr = i_Str.c_str();
        Int l_nRead, l_nCurve;

        l_nRead = ReadUntilCommaOrZero(l_ptr, l_Buff, sizeof(l_Buff));
        l_ptr += l_nRead;
        if(l_nRead <= 0) return R_FAILED;
        l_nCurve = OS_String::TStrToInteger<Char, Int>(l_Buff);
        DOME_ASSERT(l_nCurve == 3);
        
        return TVectorLutFromString<DVectorLut3f>(l_ptr, l_nCurve, o_Value);
    }

    DOME_CORE_API DResult MathFromDString(const DString& i_Str, DVectorLut4f& o_Value)
    {
        Char l_Buff[1024];
        const Char* l_ptr = i_Str.c_str();
        Int l_nRead, l_nCurve;

        l_nRead = ReadUntilCommaOrZero(l_ptr, l_Buff, sizeof(l_Buff));
        l_ptr += l_nRead;
        if(l_nRead <= 0) return R_FAILED;
        l_nCurve = OS_String::TStrToInteger<Char, Int>(l_Buff);
        DOME_ASSERT(l_nCurve == 4);
        
        return TVectorLutFromString<DVectorLut4f>(l_ptr, l_nCurve, o_Value);
    }
}
DOME_NAMESPACE_END