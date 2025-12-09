/*
    filename:       vectorlut.h
    author:         Ming Dong
    date:           2016-Mar-25
    description:    
*/
#pragma once

#include "lut.h"
#include "vector2.h"
#include "vector3.h"
#include "vector4.h"

DOME_NAMESPACE_BEGIN

template <class KEYTYPE, Int VECTORSIZE, class ALLOCATOR_T = IDefaultMemManager>
class TVectorLut
{
public:
    typedef TLut<KEYTYPE, KEYTYPE, ALLOCATOR_T>   LutType;

    TVectorLut()
    {
        init();
    }

    TVectorLut(const TVectorLut& i_Other)
    {
        init();
        copyFrom(i_Other);
    }

    void copyFrom(const TVectorLut& i_Other)
    {
        for (Int i = 0; i < VECTORSIZE; ++i)
        {
            m_Luts[i].copyFrom(i_Other.m_Luts[i]);
        }
    }

    void reset()
    {
        for (Int i = 0; i < VECTORSIZE; ++i)
        {
            m_Luts[i].reset();
        }
    }

    TVectorLut& operator= (const TVectorLut& i_Other)
    {
        copyFrom(i_Other);
        return *this;
    }

    void setNumSamplePerUnit(Int i_Index, KEYTYPE i_spu)
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < VECTORSIZE);
        m_Luts[i_Index].setNumSamplePerUnit(i_spu);
    }

    KEYTYPE getNumSamplePerUnit(Int i_Index) const
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < VECTORSIZE);
        return m_Luts[i_Index].getNumSamplePerUnit();
    }

    void setNumSamplePerUnit(KEYTYPE i_spu)
    {
        for (Int i = 0; i < VECTORSIZE; ++i)
        {
            setNumSamplePerUnit(i, i_spu);
        }
    }

    void setControlPoints(Int i_Index, Int i_NumCP, const typename LutType::_ControlPointArray* i_pCPs)
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < VECTORSIZE);
        m_Luts[i_Index].set(i_NumCP, i_pCPs);
    }

    void addControlPoint(Int i_Index, typename LutType::_CurveType i_CurveType, KEYTYPE i_Key, KEYTYPE i_KeyLeft, KEYTYPE i_KeyRight,
        KEYTYPE i_Value, KEYTYPE i_ValueLeft, KEYTYPE i_ValueRight)
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < VECTORSIZE);

        m_Luts[i_Index].addControlPoint(i_CurveType, i_Key, i_KeyLeft, i_KeyRight, i_Value, i_ValueLeft, i_ValueRight);
    }

    void addControlPoint(Int i_Index, const typename LutType::_ControlPoint& i_Cp)
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < VECTORSIZE);

        m_Luts[i_Index].addControlPoint(i_Cp);
    }

    Int getNumControlPoint(Int i_Index) const
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < VECTORSIZE);
        return m_Luts[i_Index].getNumControlPoint();
    }

    void getControlPoint(Int i_Index, Int i_CpIndex, typename LutType::_CurveType& o_CurveType, KEYTYPE& o_Key, KEYTYPE& o_KeyLeft, KEYTYPE& o_KeyRight,
        KEYTYPE& o_Value, KEYTYPE& o_ValueLeft, KEYTYPE& o_ValueRight) const
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < VECTORSIZE);
        m_Luts[i_Index].getControlPoint(i_CpIndex, o_CurveType, o_Key, o_KeyLeft, o_KeyRight, o_Value, o_ValueLeft, o_ValueRight);
    }

    void getControlPointRel(Int i_Index, Int i_CpIndex, typename LutType::_CurveType& o_CurveType, KEYTYPE& o_Key, KEYTYPE& o_KeyLeft, KEYTYPE& o_KeyRight,
        KEYTYPE& o_Value, KEYTYPE& o_ValueLeft, KEYTYPE& o_ValueRight) const
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < VECTORSIZE);
        m_Luts[i_Index].getControlPointRel(i_CpIndex, o_CurveType, o_Key, o_KeyLeft, o_KeyRight, o_Value, o_ValueLeft, o_ValueRight);
    }

    void setControlPoint(Int i_Index, Int i_CpIndex, typename LutType::_CurveType i_CurveType, KEYTYPE i_Key, KEYTYPE i_KeyLeft, KEYTYPE i_KeyRight,
        KEYTYPE i_Value, KEYTYPE i_ValueLeft, KEYTYPE i_ValueRight)
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < VECTORSIZE);
        m_Luts[i_Index].setControlPoint(i_CpIndex, i_CurveType, i_Key, i_KeyLeft, i_KeyRight, i_Value, i_ValueLeft, i_ValueRight);
    }

    void delControlPoint(Int i_Index, Int i_CpIndex)
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < VECTORSIZE);
        m_Luts[i_Index].delControlPoint(i_CpIndex);
    }

    void insertControlPoint(Int i_Index, Int i_CpIndex, typename LutType::_CurveType i_CurveType, KEYTYPE i_Key, KEYTYPE i_KeyLeft, KEYTYPE i_KeyRight,
        KEYTYPE i_Value, KEYTYPE i_ValueLeft, KEYTYPE i_ValueRight)
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < VECTORSIZE);
        m_Luts[i_Index].insertControlPoint(i_CpIndex, i_CurveType, i_Key, i_KeyLeft, i_KeyRight, i_Value, i_ValueLeft, i_ValueRight);
    }

    DResult addControlPointAdjust(Int i_Index, typename LutType::_CurveType i_CurveType, KEYTYPE i_Key, KEYTYPE i_KeyLeft, KEYTYPE i_KeyRight,
        KEYTYPE i_Value, KEYTYPE i_ValueLeft, KEYTYPE i_ValueRight)
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < VECTORSIZE);
        return m_Luts[i_Index].addControlPointAdjust(i_CurveType, i_Key, i_KeyLeft, i_KeyRight, i_Value, i_ValueLeft, i_ValueRight);
    }

    DResult addControlPointAdjustRel(Int i_Index, typename LutType::_CurveType i_CurveType, KEYTYPE i_Key, KEYTYPE i_KeyLeft, KEYTYPE i_KeyRight,
        KEYTYPE i_Value, KEYTYPE i_ValueLeft, KEYTYPE i_ValueRight)
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < VECTORSIZE);
        return m_Luts[i_Index].addControlPointAdjustRel(i_CurveType, i_Key, i_KeyLeft, i_KeyRight, i_Value, i_ValueLeft, i_ValueRight);
    }

    DResult insertControlPointAdjust(Int i_Index, Int i_CpIndex, typename LutType::_CurveType i_CurveType, KEYTYPE i_Key, KEYTYPE i_KeyLeft, KEYTYPE i_KeyRight,
        KEYTYPE i_Value, KEYTYPE i_ValueLeft, KEYTYPE i_ValueRight)
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < VECTORSIZE);
        return m_Luts[i_Index].insertControlPointAdjust(i_CpIndex, i_CurveType, i_Key, i_KeyLeft, i_KeyRight, i_Value, i_ValueLeft, i_ValueRight);
    }

    DResult insertControlPointAdjustRel(Int i_Index, Int i_CpIndex, typename LutType::_CurveType i_CurveType, KEYTYPE i_Key, KEYTYPE i_KeyLeft, KEYTYPE i_KeyRight,
        KEYTYPE i_Value, KEYTYPE i_ValueLeft, KEYTYPE i_ValueRight)
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < VECTORSIZE);
        return m_Luts[i_Index].insertControlPointAdjustRel(i_CpIndex, i_CurveType, i_Key, i_KeyLeft, i_KeyRight, i_Value, i_ValueLeft, i_ValueRight);
    }

    DResult setControlPointAdjust(Int i_Index, Int i_CpIndex, typename LutType::_CurveType i_CurveType, KEYTYPE i_Key, KEYTYPE i_KeyLeft, KEYTYPE i_KeyRight,
        KEYTYPE i_Value, KEYTYPE i_ValueLeft, KEYTYPE i_ValueRight)
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < VECTORSIZE);
        return m_Luts[i_Index].setControlPointAdjust(i_CpIndex, i_CurveType, i_Key, i_KeyLeft, i_KeyRight, i_Value, i_ValueLeft, i_ValueRight);
    }

    DResult setControlPointAdjustRel(Int i_Index, Int i_CpIndex, typename LutType::_CurveType i_CurveType, KEYTYPE i_Key, KEYTYPE i_KeyLeft, KEYTYPE i_KeyRight,
        KEYTYPE i_Value, KEYTYPE i_ValueLeft, KEYTYPE i_ValueRight)
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < VECTORSIZE);
        return m_Luts[i_Index].setControlPointAdjustRel(i_CpIndex, i_CurveType, i_Key, i_KeyLeft, i_KeyRight, i_Value, i_ValueLeft, i_ValueRight);
    }


    void bakeData()
    {
        for (Int i = 0; i < VECTORSIZE; ++i)
        {
            m_Luts[i].bakeData();
        }
    }

    void resetBakedData()
    {
        for (Int i = 0; i < VECTORSIZE; ++i)
        {
            m_Luts[i].resetBakedData();
        }
    }

    Bool isDataBaked() const
    {
        return m_Luts[0].isDataBaked();
    }

    KEYTYPE lookup(KEYTYPE i_Key) const
    {
        DOME_ASSERT(VECTORSIZE == 1);
        return m_Luts[0].lookup(i_Key);
    }

    KEYTYPE lookupFast(KEYTYPE i_Key) const
    {
        DOME_ASSERT(VECTORSIZE == 1);
        return m_Luts[0].lookupFast(i_Key);
    }

    TVector2<KEYTYPE> lookupVector2(KEYTYPE i_Key) const
    {
        DOME_ASSERT(VECTORSIZE == 2);
        return TVector2<KEYTYPE>(m_Luts[0].lookup(i_Key), m_Luts[1].lookup(i_Key));
    }

    TVector2<KEYTYPE> lookupFastVector2(KEYTYPE i_Key) const
    {
        DOME_ASSERT(VECTORSIZE == 2);
        return TVector2<KEYTYPE>(m_Luts[0].lookupFast(i_Key), m_Luts[1].lookupFast(i_Key));
    }

    TVector3<KEYTYPE> lookupVector3(KEYTYPE i_Key) const
    {
        DOME_ASSERT(VECTORSIZE == 3);
        return TVector3<KEYTYPE>(m_Luts[0].lookup(i_Key), m_Luts[1].lookup(i_Key), m_Luts[2].lookup(i_Key));
    }

    TVector3<KEYTYPE> lookupFastVector3(KEYTYPE i_Key) const
    {
        DOME_ASSERT(VECTORSIZE == 3);
        return TVector3<KEYTYPE>(m_Luts[0].lookupFast(i_Key), m_Luts[1].lookupFast(i_Key), m_Luts[2].lookupFast(i_Key));
    }

    TVector4<KEYTYPE> lookupVector4(KEYTYPE i_Key) const
    {
        DOME_ASSERT(VECTORSIZE == 4);
        return TVector4<KEYTYPE>(m_Luts[0].lookup(i_Key), m_Luts[1].lookup(i_Key), m_Luts[2].lookup(i_Key), m_Luts[3].lookup(i_Key));
    }

    TVector4<KEYTYPE> lookupFastVector4(KEYTYPE i_Key) const
    {
        DOME_ASSERT(VECTORSIZE == 4);
        return TVector4<KEYTYPE>(m_Luts[0].lookupFast(i_Key), m_Luts[1].lookupFast(i_Key), m_Luts[2].lookupFast(i_Key), m_Luts[3].lookupFast(i_Key));
    }


private:
    void init()
    {

    }

    LutType             m_Luts[VECTORSIZE];
};

typedef TVectorLut<F32, 1>      DVectorLut1f;
typedef TVectorLut<F32, 2>      DVectorLut2f;
typedef TVectorLut<F32, 3>      DVectorLut3f;
typedef TVectorLut<F32, 4>      DVectorLut4f;
typedef TVectorLut<F64, 1>      DVectorLut1d;
typedef TVectorLut<F64, 2>      DVectorLut2d;
typedef TVectorLut<F64, 3>      DVectorLut3d;
typedef TVectorLut<F64, 4>      DVectorLut4d;

DOME_NAMESPACE_END