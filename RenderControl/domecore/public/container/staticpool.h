/*
filename:       staticpool.h
author:         Ming Dong
date:           2017-Apr-28
description:
*/
#pragma once

#include "../configure.h"
#include "../defines.h"
#include "../typedefs.h"
#include "../error.h"
#include "../imemory.h"

DOME_NAMESPACE_BEGIN

template <class POOLTYPE, Int POOLSIZE = 256>
class TStaticPool
{
public:
    TStaticPool()
    {
        DOME_ASSERT(POOLSIZE >= 1);
        m_FreeCount = POOLSIZE;
        m_FreeHead = 0;
        for (Int i = 0; i < POOLSIZE; ++i)
        {
            m_Pool[i].m_bAlloced = DM_FALSE;
            m_Pool[i].m_NextFree = i + 1;
        }
        m_Pool[POOLSIZE - 1].m_NextFree = DM_INT_INVALID;
    }

    ~TStaticPool()
    {
        DOME_ASSERT(m_FreeCount == POOLSIZE);
    }

    Int             getFreeCount() const
    {
        return m_FreeCount;
    }

    Int             getAllocedCount() const
    {
        return POOLSIZE - m_FreeCount;
    }

    Bool            isFull() const
    {
        return getAllocedCount() == POOLSIZE;
    }

    Bool            isAlloced(Int i_Index) const
    {
        if (!DOME_INT_VALID(i_Index))
            return DM_FALSE;
        if (i_Index >= POOLSIZE)
            return DM_FALSE;
        if (!m_Pool[i_Index].m_bAlloced)
            return DM_FALSE;
        return DM_TRUE;
    }

    Bool            isFree(Int i_Index) const
    {
        return !isAlloced(i_Index);
    }

    Int             alloc()
    {
        if (isFull())
            return DM_INT_INVALID;

        DOME_ASSERT(DOME_INT_VALID(m_FreeHead));

        Int l_Result = m_FreeHead;
        m_FreeHead = m_Pool[m_FreeHead].m_NextFree;
        m_FreeCount--;
        m_Pool[l_Result].m_bAlloced = DM_TRUE;
        m_Pool[l_Result].m_NextFree = DM_INT_INVALID;
        return l_Result;
    }

    DResult         free(Int i_Index)
    {
        if (!DOME_INT_VALID(i_Index))
            return R_FAILED;
        if (i_Index >= POOLSIZE)
            return R_FAILED;
        if (!m_Pool[i_Index].m_bAlloced)
            return R_FAILED;
        m_Pool[i_Index].m_bAlloced = DM_FALSE;
        m_Pool[i_Index].m_NextFree = m_FreeHead;
        m_FreeHead = i_Index;
        m_FreeCount++;
        return R_SUCCESS;
    }

    POOLTYPE*       getPtr(Int i_Index)
    {
        if (!DOME_INT_VALID(i_Index))
            return DM_NULL;
        if (i_Index >= POOLSIZE)
            return DM_NULL;
        if (!m_Pool[i_Index].m_bAlloced)
            return DM_NULL;
        return &m_Pool[i_Index].m_Data;
    }

    const POOLTYPE* getPtr(Int i_Index) const
    {
        if (!DOME_INT_VALID(i_Index))
            return DM_NULL;
        if (i_Index >= POOLSIZE)
            return DM_NULL;
        if (!m_Pool[i_Index].m_bAlloced)
            return DM_NULL;
        return &m_Pool[i_Index].m_Data;
    }

private:
    struct DataHolder
    {
        Bool        m_bAlloced;
        Int         m_NextFree;
        POOLTYPE    m_Data;
    };

    Int             m_FreeHead;
    Int             m_FreeCount;
    DataHolder      m_Pool[POOLSIZE];
};


DOME_NAMESPACE_END