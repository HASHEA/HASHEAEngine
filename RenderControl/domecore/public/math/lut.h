/*
    filename:       lut.h
    author:         Ming Dong
    date:           2016-Feb-23
    description:    
*/
#pragma once

#include "bezier.h"
#include "../imemory.h"
#include "../container.h"

DOME_NAMESPACE_BEGIN

/*
    KEYTYPE can be F32 or F64
    VALUETYPE can be any type which support  c++ operator*(KEYTYPE T)
*/
template <class KEYTYPE, class VALUETYPE = KEYTYPE, class ALLOCATOR_T = IDefaultMemManager>
class TLut
{
public:
    static const Int k_DefaultSPU = 100;
    enum _CurveType
    {
        CT_JUMP,
        CT_LINEAR,
        CT_BEZIER2,
        CT_BEZIER3
    };
    struct _ControlPoint
    {
        _CurveType      m_CurveType;        // specify the curve type at the right side of this control point
        KEYTYPE         m_Key;
        KEYTYPE         m_KeyLeft;
        KEYTYPE         m_KeyRight;
        VALUETYPE       m_Value;
        VALUETYPE       m_ValueLeft;
        VALUETYPE       m_ValueRight;
    };
    typedef TArray<_ControlPoint, ALLOCATOR_T> _ControlPointArray;
    typedef TArray<VALUETYPE, ALLOCATOR_T> _BakedDataArray;

    TLut()
    {
        init();
    }

    TLut(Int i_NumCP, const _ControlPointArray* i_pCPs)
    {
        init();
        set(i_NumCp, i_pCPs);
    }

    TLut(const TLut& i_Other)
    {
        init();
        copyFrom(i_Other);
    }

    ~TLut()
    {
        reset();
    }

    TLut& operator=(const TLut& i_Other)
    {
        copyFrom(i_Other);
        return *this;
    }

    void setNumSamplePerUnit(KEYTYPE i_spu)
    {
        resetBakedData();
        m_NumSamplePerUnit = i_spu;
    }

    KEYTYPE getNumSamplePerUnit() const
    {
        return m_NumSamplePerUnit;
    }

    void reset()
    {
        resetBakedData();

        m_ControlPointArray.clear();
        m_NumSamplePerUnit = k_DefaultSPU;
    }

    void copyFrom(const TLut& i_Other)
    {
        reset();

        m_NumSamplePerUnit = i_Other.m_NumSamplePerUnit;
        m_ControlPointArray = i_Other.m_ControlPointArray;
    }

    void set(Int i_NumCP, const _ControlPointArray* i_pCPs)
    {
        resetBakedData();

        m_ControlPointArray.resize(i_NumCp);
        for (Int i = 0; i < i_NumCp; ++i)
        {
            m_ControlPointArray[i] = i_pCPs[i];
        }
    }

    void addControlPoint(_CurveType i_CurveType, KEYTYPE i_Key, KEYTYPE i_KeyLeft, KEYTYPE i_KeyRight,
        VALUETYPE i_Value, VALUETYPE i_ValueLeft, VALUETYPE i_ValueRight)
    {
        _ControlPoint l_Cp;
        l_Cp.m_CurveType = i_CurveType;
        l_Cp.m_Key = i_Key;
        l_Cp.m_KeyLeft = i_KeyLeft;
        l_Cp.m_KeyRight = i_KeyRight;
        l_Cp.m_Value = i_Value;
        l_Cp.m_ValueLeft = i_ValueLeft;
        l_Cp.m_ValueRight = i_ValueRight;
        addControlPoint(l_Cp);
    }

    void addControlPoint(const _ControlPoint& i_Cp)
    {
        DOME_ASSERT(i_Cp.m_KeyLeft <= i_Cp.m_Key);
        DOME_ASSERT(i_Cp.m_KeyRight >= i_Cp.m_Key);

        resetBakedData();

        if(m_ControlPointArray.size() == 0)
            m_ControlPointArray.push_back(i_Cp);
        else
        {
            Int l_LastIdx = m_ControlPointArray.size() - 1;
            const _ControlPoint& l_LastCp = m_ControlPointArray[l_LastIdx];
            DOME_ASSERT2(i_Cp.m_Key >= l_LastCp.m_Key, "The key of control point must increase only.");
            DOME_ASSERT(i_Cp.m_KeyLeft >= l_LastCp.m_Key);
            DOME_ASSERT(l_LastCp.m_KeyRight <= i_Cp.m_Key);

            m_ControlPointArray.push_back(i_Cp);
        }
    }

    Int getNumControlPoint() const
    {
        return m_ControlPointArray.size();
    }

    void getControlPoint(Int i_CpIndex, _CurveType& o_CurveType, KEYTYPE& o_Key, KEYTYPE& o_KeyLeft, KEYTYPE& o_KeyRight,
        VALUETYPE& o_Value, VALUETYPE& o_ValueLeft, VALUETYPE& o_ValueRight) const
    {
        DOME_ASSERT(i_CpIndex >= 0 && i_CpIndex < m_ControlPointArray.size());
        const _ControlPoint& l_Cp = m_ControlPointArray[i_CpIndex];
        o_CurveType     = l_Cp.m_CurveType;
        o_Key           = l_Cp.m_Key;
        o_KeyLeft       = l_Cp.m_KeyLeft;
        o_KeyRight      = l_Cp.m_KeyRight;
        o_Value         = l_Cp.m_Value;
        o_ValueLeft     = l_Cp.m_ValueLeft;
        o_ValueRight    = l_Cp.m_ValueRight;
    }

    void getControlPointRel(Int i_CpIndex, _CurveType& o_CurveType, KEYTYPE& o_Key, KEYTYPE& o_KeyLeft, KEYTYPE& o_KeyRight,
        VALUETYPE& o_Value, VALUETYPE& o_ValueLeft, VALUETYPE& o_ValueRight) const
    {
        getControlPoint(i_CpIndex, o_CurveType, o_Key, o_KeyLeft, o_KeyRight, o_Value, o_ValueLeft, o_ValueRight);
        o_KeyLeft = o_KeyLeft - o_Key;
        o_KeyRight = o_KeyRight - o_Key;
    }

    void setControlPoint(Int i_CpIndex, _CurveType i_CurveType, KEYTYPE i_Key, KEYTYPE i_KeyLeft, KEYTYPE i_KeyRight,
        VALUETYPE i_Value, VALUETYPE i_ValueLeft, VALUETYPE i_ValueRight)
    {
        DOME_ASSERT(i_KeyLeft <= i_Key);
        DOME_ASSERT(i_KeyRight >= i_Key);
        DOME_ASSERT(i_CpIndex >= 0 && i_CpIndex < m_ControlPointArray.size());
        _ControlPoint& l_Cp = m_ControlPointArray[i_CpIndex];
        l_Cp.m_CurveType        = i_CurveType;
        l_Cp.m_Key              = i_Key;
        l_Cp.m_KeyLeft          = i_KeyLeft;
        l_Cp.m_KeyRight         = i_KeyRight;
        l_Cp.m_Value            = i_Value;
        l_Cp.m_ValueLeft        = i_ValueLeft;
        l_Cp.m_ValueRight       = i_ValueRight;
    }

    void delControlPoint(Int i_CpIndex)
    {
        DOME_ASSERT(i_CpIndex >= 0 && i_CpIndex < m_ControlPointArray.size());
        m_ControlPointArray.remove(i_CpIndex);
    }

    void insertControlPoint(Int i_CpIndex, _CurveType i_CurveType, KEYTYPE i_Key, KEYTYPE i_KeyLeft, KEYTYPE i_KeyRight,
        VALUETYPE i_Value, VALUETYPE i_ValueLeft, VALUETYPE i_ValueRight)
    {
        DOME_ASSERT(i_KeyLeft <= i_Key);
        DOME_ASSERT(i_KeyRight >= i_Key);
        DOME_ASSERT(i_CpIndex >= 0 && i_CpIndex <= m_ControlPointArray.size());
        _ControlPoint l_Cp;
        l_Cp.m_CurveType        = i_CurveType;
        l_Cp.m_Key              = i_Key;
        l_Cp.m_KeyLeft          = i_KeyLeft;
        l_Cp.m_KeyRight         = i_KeyRight;
        l_Cp.m_Value            = i_Value;
        l_Cp.m_ValueLeft        = i_ValueLeft;
        l_Cp.m_ValueRight       = i_ValueRight;

        m_ControlPointArray.insert(i_CpIndex, l_Cp);
    }

    DResult addControlPointAdjust(_CurveType i_CurveType, KEYTYPE i_Key, KEYTYPE i_KeyLeft, KEYTYPE i_KeyRight,
        VALUETYPE i_Value, VALUETYPE i_ValueLeft, VALUETYPE i_ValueRight)
    {
        if(i_KeyLeft > i_Key || i_KeyRight < i_Key)
            return R_FAILED;

        Int l_NumCp = m_ControlPointArray.size();
        if (l_NumCp == 0 || i_Key >= m_ControlPointArray[l_NumCp - 1].m_Key)
        {
            addControlPoint(i_CurveType, i_Key, i_KeyLeft, i_KeyRight, i_Value, i_ValueLeft, i_ValueRight);
            return R_SUCCESS;
        }
        else
        {
            i_Key = m_ControlPointArray[l_NumCp - 1].m_Key + (KEYTYPE)1;
            i_KeyLeft = i_KeyRight = i_Key;
            addControlPoint(i_CurveType, i_Key, i_KeyLeft, i_KeyRight, i_Value, i_ValueLeft, i_ValueRight);
            return R_OPERATIONADJUSTED;
        }
    }

    DResult addControlPointAdjustRel(_CurveType i_CurveType, KEYTYPE i_Key, KEYTYPE i_KeyLeft, KEYTYPE i_KeyRight,
        VALUETYPE i_Value, VALUETYPE i_ValueLeft, VALUETYPE i_ValueRight)
    {
        return addControlPointAdjust(i_CurveType, i_Key, i_Key + i_KeyLeft, i_Key + i_KeyRight, i_Value, i_ValueLeft, i_ValueRight);
    }

    DResult insertControlPointAdjust(Int i_CpIndex, _CurveType i_CurveType, KEYTYPE i_Key, KEYTYPE i_KeyLeft, KEYTYPE i_KeyRight,
        VALUETYPE i_Value, VALUETYPE i_ValueLeft, VALUETYPE i_ValueRight)
    {
        if(i_KeyLeft > i_Key || i_KeyRight < i_Key)
            return R_FAILED;

        Int l_NumCp = m_ControlPointArray.size();
        if (l_NumCp == 0 || i_CpIndex >= l_NumCp)
        {
            addControlPoint(i_CurveType, i_Key, i_KeyLeft, i_KeyRight, i_Value, i_ValueLeft, i_ValueRight);
            return R_SUCCESS;
        }

        if (i_CpIndex == 0)
        {
            if (i_Key <= m_ControlPointArray[0].m_Key)
            {
                insertControlPoint(0, i_CurveType, i_Key, i_KeyLeft, i_KeyRight, i_Value, i_ValueLeft, i_ValueRight);
                return R_SUCCESS;
            }
            else
            {
                i_Key = m_ControlPointArray[0].m_Key - (KEYTYPE)1;
                i_KeyLeft = i_KeyRight = i_Key;
                insertControlPoint(0, i_CurveType, i_Key, i_KeyLeft, i_KeyRight, i_Value, i_ValueLeft, i_ValueRight);
                return R_OPERATIONADJUSTED;
            }
        }

        if (i_Key >= m_ControlPointArray[i_CpIndex - 1].m_Key && i_Key <= m_ControlPointArray[i_CpIndex].m_Key)
        {
            insertControlPoint(i_CpIndex, i_CurveType, i_Key, i_KeyLeft, i_KeyRight, i_Value, i_ValueLeft, i_ValueRight);
            return R_SUCCESS;
        }
        else
        {
            i_Key = (m_ControlPointArray[i_CpIndex - 1].m_Key + m_ControlPointArray[i_CpIndex].m_Key) / (KEYTYPE)2;
            i_KeyLeft = i_KeyRight = i_Key;
            insertControlPoint(i_CpIndex, i_CurveType, i_Key, i_KeyLeft, i_KeyRight, i_Value, i_ValueLeft, i_ValueRight);
            return R_OPERATIONADJUSTED;
        }
    }

    DResult insertControlPointAdjustRel(Int i_CpIndex, _CurveType i_CurveType, KEYTYPE i_Key, KEYTYPE i_KeyLeft, KEYTYPE i_KeyRight,
        VALUETYPE i_Value, VALUETYPE i_ValueLeft, VALUETYPE i_ValueRight)
    {
        return insertControlPointAdjust(i_CpIndex, i_CurveType, i_Key, i_Key + i_KeyLeft, i_Key + i_KeyRight, i_Value, i_ValueLeft, i_ValueRight);
    }


    DResult setControlPointAdjust(Int i_CpIndex, _CurveType i_CurveType, KEYTYPE i_Key, KEYTYPE i_KeyLeft, KEYTYPE i_KeyRight,
        VALUETYPE i_Value, VALUETYPE i_ValueLeft, VALUETYPE i_ValueRight)
    {
        if(i_KeyLeft > i_Key || i_KeyRight < i_Key)
            return R_FAILED;

        if(i_CpIndex < 0 && i_CpIndex >= m_ControlPointArray.size())
            return R_FAILED;

        Bool l_bAdjusted = DM_FALSE;
        if ((i_CpIndex - 1) >= 0)
        {
            if (i_Key < m_ControlPointArray[i_CpIndex - 1].m_Key)
            {
                i_Key = m_ControlPointArray[i_CpIndex - 1].m_Key;
                l_bAdjusted = DM_TRUE;
            }
        }
        if ((i_CpIndex + 1) < m_ControlPointArray.size())
        {
            if (i_Key > m_ControlPointArray[i_CpIndex + 1].m_Key)
            {
                i_Key = m_ControlPointArray[i_CpIndex + 1].m_Key;
                l_bAdjusted = DM_TRUE;
            }
        }

        if (l_bAdjusted)
        {
            i_KeyLeft = i_KeyRight = i_Key;
            setControlPoint(i_CpIndex, i_CurveType, i_Key, i_KeyLeft, i_KeyRight, i_Value, i_ValueLeft, i_ValueRight);
            return R_OPERATIONADJUSTED;
        }
        else
        {
            setControlPoint(i_CpIndex, i_CurveType, i_Key, i_KeyLeft, i_KeyRight, i_Value, i_ValueLeft, i_ValueRight);
            return R_SUCCESS;
        }
    }

    DResult setControlPointAdjustRel(Int i_CpIndex, _CurveType i_CurveType, KEYTYPE i_Key, KEYTYPE i_KeyLeft, KEYTYPE i_KeyRight,
        VALUETYPE i_Value, VALUETYPE i_ValueLeft, VALUETYPE i_ValueRight)
    {
        return setControlPointAdjust(i_CpIndex, i_CurveType, i_Key, i_Key + i_KeyLeft, i_Key + i_KeyRight, i_Value, i_ValueLeft, i_ValueRight);
    }



    VALUETYPE lookup(KEYTYPE i_Key) const
    {
        Int l_NumCps = m_ControlPointArray.size();
        if(l_NumCps == 0)
            return VALUETYPE();
        else if(l_NumCps == 1 || i_Key <= m_ControlPointArray[0].m_Key)
            return m_ControlPointArray[0].m_Value;
        else if(i_Key >= m_ControlPointArray[l_NumCps - 1].m_Key)
            return m_ControlPointArray[l_NumCps - 1].m_Value;

        for (Int i = 0; i < (l_NumCps - 1); ++i)
        {
            if (i_Key >= m_ControlPointArray[i].m_Key && i_Key <= m_ControlPointArray[i + 1].m_Key)
            {
                KEYTYPE l_Tolerence = (KEYTYPE)1 / m_NumSamplePerUnit;
                KEYTYPE l_KeyDiff = m_ControlPointArray[i + 1].m_Key - m_ControlPointArray[i].m_Key;

                if(l_KeyDiff < l_Tolerence)
                    return m_ControlPointArray[i].m_Value;

                switch (m_ControlPointArray[i].m_CurveType)
                {
                case CT_JUMP:
                {
                    return m_ControlPointArray[i].m_Value;
                }
                case CT_LINEAR:
                {
                    KEYTYPE t = (i_Key - m_ControlPointArray[i].m_Key) / l_KeyDiff;
                    return m_ControlPointArray[i].m_Value * ((KEYTYPE)1 - t) + 
                        m_ControlPointArray[i + 1].m_Value * t;
                }
                case CT_BEZIER3:
                {
                    KEYTYPE t = TGetBezier3Time(
                        m_ControlPointArray[i].m_Key,
                        m_ControlPointArray[i].m_KeyRight,
                        m_ControlPointArray[i + 1].m_KeyLeft,
                        m_ControlPointArray[i + 1].m_Key,
                        i_Key, 
                        l_Tolerence
                        );
                    return TBezier3(
                        m_ControlPointArray[i].m_Value,
                        m_ControlPointArray[i].m_ValueRight,
                        m_ControlPointArray[i + 1].m_ValueLeft,
                        m_ControlPointArray[i + 1].m_Value,
                        t
                        );
                }
                default:
                    DOME_ERROR2(0, "this curve is not supported still.");
                }
            }
        }

        DOME_ASSERT(0); // shouldn't be here
        return VALUETYPE();
    }

    void bakeData()
    {
        resetBakedData();

        if (m_ControlPointArray.size() == 0)
        {
            m_FirstSampleKey = (KEYTYPE)0;
            m_BakedDataArray.push_back(VALUETYPE());
        }
        else if (m_ControlPointArray.size() == 1)
        {
            m_FirstSampleKey = m_ControlPointArray[0].m_Key;
            m_BakedDataArray.push_back(m_ControlPointArray[0].m_Value);
        }
        else
        {
            KEYTYPE l_FirstKey = m_ControlPointArray[0].m_Key;
            KEYTYPE l_LastKey = m_ControlPointArray[m_ControlPointArray.size() - 1].m_Key;
            KEYTYPE l_KeyStep = (KEYTYPE)1 / m_NumSamplePerUnit;
            m_FirstSampleKey = l_Firstkey;
            for (KEYTYPE key = l_FirstKey; key < l_LastKey; key += l_KeyStep)
            {
                m_BakedDataArray.push_back(lookup(key));
            }
            m_BakedDataArray.push_back(lookup(l_LastKey));
        }
        m_bBakedData = DM_TRUE;
    }

    void resetBakedData()
    {
        m_bBakedData = DM_FALSE;
        m_FirstSampleKey = (KEYTYPE)0;
        m_BakedDataArray.clear();
    }

    Bool isDataBaked() const
    {
        return m_bBakedData;
    }

    VALUETYPE lookupFast(KEYTYPE i_Key) const
    {
        if (isDataBaked() && m_BakedDataArray.size() > 0)
        {
            if(i_Key < m_FirstSampleKey)
                return m_BakedDataArray[0];
            else
            {
                KEYTYPE l_KeyStep = (KEYTYPE)1 / m_NumSamplePerUnit;
                KEYTYPE l_Key = (i_Key - m_FirstSampleKey) / l_KeyStep;
                Int l_iKey = (Int)l_Key;
                DOME_ASSERT(l_iKey >= 0);
                if(l_iKey >= m_BakedDataArray.size())
                    return m_BakedDataArray[m_BakedDataArray.size() - 1];
                else
                    return m_BakedDataArray[l_iKey];
            }
        }
        else
            return lookup(i_Key);
    }

private:
    void init()
    {
        m_NumSamplePerUnit = k_DefaultSPU;
        m_bBakedData = DM_FALSE;
        m_FirstSampleKey = (KEYTYPE)0;
    }

    KEYTYPE             m_NumSamplePerUnit;
    _ControlPointArray  m_ControlPointArray;

    // baked data 
    Bool                m_bBakedData;
    KEYTYPE             m_FirstSampleKey;
    _BakedDataArray     m_BakedDataArray;
};


typedef TLut<F32>    DLutf;
typedef TLut<F64>    DLutd;
typedef TLut<Int>    DLuti;


DOME_NAMESPACE_END