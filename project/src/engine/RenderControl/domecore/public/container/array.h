//
//  array.h
//  engine
//
//  Created by Ming Dong on 12-03-06.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//
#pragma once
//#ifndef engine_array_h
//#define engine_array_h
#include "../configure.h"
#include "../defines.h"
#include "../typedefs.h"
#include "../error.h"
#include "../imemory.h"

DOME_NAMESPACE_BEGIN

template <class ARRAY_VALUE_T, class ARRAY_ALLOCATOR_T = IDefaultMemManager, Int RESERVESTEP = 8, Int FREERESERVEPAGE = 1>
class TArray
{
public:
    typedef ARRAY_VALUE_T value_type;

    class iterator
    {
        friend class const_iterator;
    public:

        iterator()
        {
            m_pObject = DM_NULL;
        }

        iterator(ARRAY_VALUE_T* i_pObject)
        {
            m_pObject = i_pObject;
        }

        iterator(const iterator& i_It)
        {
            m_pObject = i_It.m_pObject;
        }

        iterator& operator= (const iterator& i_It)
        {
            m_pObject = i_It.m_pObject;
            return *this;
        }

        Bool operator== (const iterator& i_It) const
        {
            return m_pObject == i_It.m_pObject;
        }

        Bool operator!= (const iterator& i_It) const
        {
            return m_pObject != i_It.m_pObject;
        }

        Bool operator> (const iterator& i_It) const
        {
            return m_pObject > i_It.m_pObject;
        }

        Bool operator>= (const iterator& i_It) const
        {
            return m_pObject >= i_It.m_pObject;
        }

        Bool operator< (const iterator& i_It) const
        {
            return m_pObject < i_It.m_pObject;
        }

        Bool operator<= (const iterator& i_It) const
        {
            return m_pObject <= i_It.m_pObject;
        }

        Bool isNull() const
        {
            return m_pObject == DM_NULL;
        }

        Int operator-(const iterator& i_It) const
        {
            return m_pObject - i_It.m_pObject;
        }

        iterator operator+(Int i_Count) const
        {
            return iterator(m_pObject + i_Count);
        }

        iterator operator-(Int i_Count) const
        {
            return iterator(m_pObject - i_Count);
        }

        iterator& operator++()
        {
            m_pObject ++;
            return *this;
        }

        iterator operator++(int)
        {
            iterator l_OldIt(*this);
            operator++();
            return l_OldIt;
        }

        iterator& operator--()
        {
            m_pObject --;
            return *this;
        }

        iterator operator--(int)
        {
            iterator l_OldIt(*this);
            operator--();
            return l_OldIt;
        }

        iterator& operator+=(Int i_Step)
        {
            m_pObject += i_Step;
            return *this;
        }

        iterator& operator-=(Int i_Step)
        {
            m_pObject -= i_Step;
            return *this;
        }

        ARRAY_VALUE_T& operator*()
        {
            DOME_ASSERT(m_pObject);
            return *m_pObject;
        }

        ARRAY_VALUE_T* operator->()
        {
            DOME_ASSERT(m_pObject);
            return m_pObject;
        }

// vs 2012 doesn't support explicit type conversion operator overload, so comment it for now
// use getPtr function instead
//        explicit operator ARRAY_VALUE_T* ()
//        {
//            return m_pObject;
//        }

        ARRAY_VALUE_T* getPtr()
        {
            return m_pObject;
        }

    protected:
        ARRAY_VALUE_T*     m_pObject;
    };

    class const_iterator
    {
    public:

        const_iterator()
        {
            m_pObject = DM_NULL;
        }

        const_iterator(const ARRAY_VALUE_T* i_pObject)
        {
            m_pObject = i_pObject;
        }

        const_iterator(const const_iterator& i_It)
        {
            m_pObject = i_It.m_pObject;
        }

        const_iterator& operator= (const const_iterator& i_It)
        {
            m_pObject = i_It.m_pObject;
            return *this;
        }

        Bool operator== (const const_iterator& i_It) const
        {
            return m_pObject == i_It.m_pObject;
        }

        Bool operator!= (const const_iterator& i_It) const
        {
            return m_pObject != i_It.m_pObject;
        }

        Bool operator> (const const_iterator& i_It) const
        {
            return m_pObject > i_It.m_pObject;
        }

        Bool operator>= (const const_iterator& i_It) const
        {
            return m_pObject >= i_It.m_pObject;
        }

        Bool operator< (const const_iterator& i_It) const
        {
            return m_pObject < i_It.m_pObject;
        }

        Bool operator<= (const const_iterator& i_It) const
        {
            return m_pObject <= i_It.m_pObject;
        }

        Bool isNull() const
        {
            return m_pObject == DM_NULL;
        }

        Int operator-(const const_iterator& i_It) const
        {
            return m_pObject - i_It.m_pObject;
        }

        const_iterator operator+(Int i_Count) const
        {
            return const_iterator(m_pObject + i_Count);
        }

        const_iterator operator-(Int i_Count) const
        {
            return const_iterator(m_pObject - i_Count);
        }

        const_iterator& operator++()
        {
            m_pObject ++;
            return *this;
        }

        const_iterator operator++(int)
        {
            const_iterator l_OldIt(*this);
            operator++();
            return l_OldIt;
        }

        const_iterator& operator--()
        {
            m_pObject --;
            return *this;
        }

        const_iterator operator--(int)
        {
            const_iterator l_OldIt(*this);
            operator--();
            return l_OldIt;
        }

        const_iterator& operator+=(Int i_Step)
        {
            m_pObject += i_Step;
            return *this;
        }

        const_iterator& operator-=(Int i_Step)
        {
            m_pObject -= i_Step;
            return *this;
        }

        const_iterator(iterator i_It)
        {
            m_pObject = i_It.m_pObject;
        }

        const_iterator& operator=(iterator i_It)
        {
            m_pObject = i_It.m_pObject;
            return *this;
        }

        const ARRAY_VALUE_T& operator*() const
        {
            DOME_ASSERT(m_pObject);
            return *m_pObject;
        }

        const ARRAY_VALUE_T* operator->() const
        {
            DOME_ASSERT(m_pObject);
            return m_pObject;
        }

// vs 2012 doesn't support explicit type conversion operator overload, so comment it for now
// use getPtr function instead
//        explicit operator const ARRAY_VALUE_T* () const
//        {
//            return m_pObject;
//        }

        const ARRAY_VALUE_T* getPtr() const
        {
            return m_pObject;
        }

    protected:
        const ARRAY_VALUE_T*     m_pObject;
    };

public:
    TArray()
    {
        init();
    }
    
    TArray(const TArray& i_Array)
    {
        init();
        copyFrom(i_Array);
    }
    
    TArray& operator= (const TArray& i_Array)
    {
        copyFrom(i_Array);
        return *this;
    }
    
    ~TArray()
    {
        reset();
    }
    
    Bool empty() const
    {
        return m_Count == 0;
    }
    
    void clear()
    {
        reset();
    }

    Int size() const
    {
        return m_Count;
    }
    
    void resize(Int i_NewCount, const ARRAY_VALUE_T& i_Val = ARRAY_VALUE_T())
    {
        if(i_NewCount > m_Count)
        {
            resizeBuffer(i_NewCount);
            
            for(Int i = m_Count; i < i_NewCount; i ++)
            {
                copyConstruct(&m_pArray[i], i_Val);
            }
            m_Count = i_NewCount;
        }
        else if(i_NewCount < m_Count)
        {
            for(Int i = i_NewCount; i < m_Count; i ++)
            {
                destruct(&m_pArray[i]);
            }
            m_Count = i_NewCount;
            
            resizeBuffer(m_Count);
        }
    }
    
    iterator begin()
    {
        return iterator(m_pArray);
    }

    const_iterator begin() const
    {
        return const_iterator(m_pArray);
    }

    const_iterator cbegin() const
    {
        return const_iterator(m_pArray);
    }

    iterator end()
    {
        return iterator(m_pArray + m_Count);
    }

    const_iterator end() const
    {
        return const_iterator(m_pArray + m_Count);
    }

    const_iterator cend() const
    {
        return const_iterator(m_pArray + m_Count);
    }

    const_iterator get(Int i_Index) const
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < m_Count);
        return const_iterator(&m_pArray[i_Index]);
    }
    
    iterator get(Int i_Index)
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < m_Count);
        return iterator(&m_pArray[i_Index]);
    }

    void set(Int i_Index, const ARRAY_VALUE_T& i_Val)
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < m_Count);
        m_pArray[i_Index] = i_Val;
    }

    iterator add()
    {
        resizeBuffer(m_Count + 1);
        construct(&m_pArray[m_Count]);
        m_Count ++;
        return iterator(&m_pArray[m_Count - 1]);
    }
    
    iterator add(const ARRAY_VALUE_T& i_Value)
    {
        resizeBuffer(m_Count + 1);
        copyConstruct(&m_pArray[m_Count], i_Value);
        m_Count ++;
        return iterator(&m_pArray[m_Count - 1]);
    }

    void push_back(const ARRAY_VALUE_T& i_Value)
    {
        add(i_Value);
    }
    
    iterator insert(Int i_Index, const ARRAY_VALUE_T& i_Value = ARRAY_VALUE_T())
    {
        return insert(i_Index, 1, i_Value);
    }

    iterator insert(iterator i_It, const ARRAY_VALUE_T& i_Value = ARRAY_VALUE_T())
    {
        return insert(i_It - begin(), i_Value);
    }

    iterator insert(Int i_Index, Int i_Count, const ARRAY_VALUE_T& i_Value = ARRAY_VALUE_T())
    {
        DOME_ASSERT(i_Index >= 0 && i_Index <= m_Count);
        DOME_ASSERT(i_Count >= 0);

        if(i_Index < 0)
            i_Index = 0;
        if(i_Index > m_Count)
            i_Index = m_Count;
        if(i_Count == 0)
            return iterator(&m_pArray[i_Index]);

        resizeBuffer(m_Count + i_Count);

        for(Int i = m_Count - 1; i >= i_Index; --i)
        {
            moveElement(&m_pArray[i+i_Count], &m_pArray[i]);
        }

        for(Int i = i_Index; i < (i_Index + i_Count); ++i)
        {
            copyConstruct(&m_pArray[i], i_Value);
        }
        m_Count += i_Count;

        return iterator(&m_pArray[i_Index]);
    }

    iterator insert(iterator i_It, Int i_Count, const ARRAY_VALUE_T& i_Value = ARRAY_VALUE_T())
    {
        return insert(i_It - begin(), i_Count, i_Value);
    }

    iterator insert(Int i_Index, const_iterator i_Begin, const_iterator i_End)
    {
        DOME_ASSERT(i_Index >= 0 && i_Index <= m_Count);
        Int i_Count = i_End - i_Begin;
        DOME_ASSERT(i_Count >= 0);

        if(i_Index < 0)
            i_Index = 0;
        if(i_Index > m_Count)
            i_Index = m_Count;
        if(i_Count == 0)
            return iterator(&m_pArray[i_Index]);

        resizeBuffer(m_Count + i_Count);

        for(Int i = m_Count - 1; i >= i_Index; --i)
        {
            moveElement(&m_pArray[i+i_Count], &m_pArray[i]);
        }

        const_iterator it = i_Begin;
        for(Int i = i_Index; i < (i_Index + i_Count); ++i)
        {
            m_pArray[i] = *it;
            ++it;
        }
        m_Count += i_Count;

        return iterator(&m_pArray[i_Index]);
    }

    iterator insert(iterator i_It, const_iterator i_Begin, const_iterator i_End)
    {
        return insert(i_It - begin(), i_Begin, i_End);
    }
    
    Bool removeLast()
    {
        if(m_Count == 0)
            return DM_FALSE;
        
        destruct(&m_pArray[m_Count - 1]);
        m_Count --;
        
        resizeBuffer(m_Count);
        return DM_TRUE;
    }

    void pop_back()
    {
        removeLast();
    }
    
    Bool remove(Int i_Index)
    {
        return remove(i_Index, 1);
    }

    Bool remove(Int i_Index, Int i_Count)
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < m_Count);
        DOME_ASSERT(i_Count >= 0);
        DOME_WARNING((i_Index + i_Count) <= m_Count);

        if(i_Index < 0 || i_Index >= m_Count)
            return DM_FALSE;
        if((i_Index + i_Count) > m_Count)
            i_Count = m_Count - i_Index;

        for(Int i = i_Index; i < (i_Index + i_Count); ++i)
        {
            destruct(&m_pArray[i]);
        }

        for(Int i = i_Index + i_Count; i < m_Count; ++i)
        {
            moveElement(&m_pArray[i - i_Count], &m_pArray[i]);
        }

        m_Count -= i_Count;

        resizeBuffer(m_Count);

        return DM_TRUE;
    }

    iterator erase(iterator i_It)
    {
        return erase(i_It, i_It + 1);
    }

    iterator erase(iterator i_BeginIt, iterator i_EndIt)
    {
        DOME_ASSERT(i_BeginIt >= begin());
        DOME_ASSERT(i_BeginIt < end());
        DOME_ASSERT(i_EndIt >= i_BeginIt);
        DOME_ASSERT(i_EndIt <= end());

        Int l_BeginIndex = i_BeginIt - begin();
        Int l_EraseCount = i_EndIt - i_BeginIt;

        for(iterator it = i_BeginIt; it < i_EndIt; ++it)
        {
            destruct(it.getPtr());
        }

        for(iterator dstit = i_BeginIt, srcit = i_EndIt; srcit < end(); ++dstit, ++srcit)
        {
            moveElement(dstit.getPtr(), srcit.getPtr());
        }

        m_Count -= l_EraseCount;

        resizeBuffer(m_Count);

        return iterator(&m_pArray[l_BeginIndex]);
    }
    
    const ARRAY_VALUE_T& operator [] (Int i_Index) const
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < m_Count);
        return m_pArray[i_Index];
    }
    
    ARRAY_VALUE_T& operator[] (Int i_Index)
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < m_Count);
        return m_pArray[i_Index];
    }

protected:
    // construct an object without alloc memory
    inline void construct(ARRAY_VALUE_T* i_pObject)
    {
        DOME_NewPlacement(ARRAY_VALUE_T, i_pObject);
    }
    
    // copy construct an object without alloc memory
    inline void copyConstruct(ARRAY_VALUE_T* i_pObject, const ARRAY_VALUE_T& i_Value)
    {
        DOME_NewPlacement(ARRAY_VALUE_T, i_pObject) (i_Value);
    }
    
    // destruct an object without free memory
    inline void destruct(ARRAY_VALUE_T* i_pObject)
    {
        i_pObject->~ARRAY_VALUE_T();
    }
    
    // Copy object memory from source to destination, and clear source object's memory to ZERO.
    inline void moveElement(ARRAY_VALUE_T* i_pObjDst, ARRAY_VALUE_T* i_pObjSrc)
    {
        memcpy(i_pObjDst, i_pObjSrc, sizeof(ARRAY_VALUE_T));
        memset(i_pObjSrc, 0, sizeof(ARRAY_VALUE_T));
    }
    
    // init an object, without consider release object's resource
    inline void init()
    {
        m_Count = 0;
        m_pArray = DM_NULL;
    }
    
    // reset an object, if there is resource allocated in this object, release it
    inline void reset()
    {
        if (m_pArray != DM_NULL) 
        {
            for(Int i = 0; i < m_Count; i ++)
            {
                destruct(&m_pArray[i]);
            }
            DOME_FreeEx(m_pArray, ARRAY_ALLOCATOR_T);
        }
        m_pArray = DM_NULL;
        m_Count = 0;
    }

    inline Int getBufferSize() const
    {
        return ARRAY_ALLOCATOR_T::Instance().getSize(m_pArray);
    }

    inline Int getReserveCount() const
    {
        Int l_BufferSize = getBufferSize();
        DOME_ASSERT((l_BufferSize / sizeof(ARRAY_VALUE_T) * sizeof(ARRAY_VALUE_T)) == l_BufferSize);
        return l_BufferSize / sizeof(ARRAY_VALUE_T);
    }
    
    // Important: before call this function, the object must have been initialized
    inline void copyFrom(const TArray& i_Array)
    {
        reset();

        m_Count = i_Array.m_Count;
        if(m_Count > 0)
        {
            m_pArray = (ARRAY_VALUE_T*)DOME_AllocEx(i_Array.getReserveCount() * sizeof(ARRAY_VALUE_T), ARRAY_ALLOCATOR_T);
            for(Int i = 0; i < m_Count; i ++)
            {
                copyConstruct(&m_pArray[i], i_Array.m_pArray[i]);
            }
        }
    }
    
    /*
        This function try to adjust the buffer with the new desired array size.
        This function will never change current size of array.
     */
    inline void resizeBuffer(Int i_NewCount)
    {
        Int l_NewCapacityMin = 0;
        Int l_NewCapacityMax = 0;
        Int l_NewCapacity = 0;
        Int l_CurCapacity = 0;
        
        DOME_WARNING(i_NewCount >= 0);

        // never delete array element, make sure new count >= existing count
        if(i_NewCount < m_Count)
            i_NewCount = m_Count;

        if(i_NewCount <= 0)
        {
            // since new count is always greater or eaqual then existing count, 
            // new count is 0, meaning the current array has elements any more,
            // so I just reset the array object here.
            reset();
            return ;
        }
        
        // calculate the good min/max capacity based on the desired new count
        if(RESERVESTEP > 0)
        {
            l_NewCapacityMin = ((i_NewCount + RESERVESTEP - 1) / RESERVESTEP) * RESERVESTEP;
            l_NewCapacityMax = l_NewCapacityMin + RESERVESTEP * FREERESERVEPAGE;
        }
        else
        {
            // Auto calculate capacity
            l_NewCapacityMin = 1;
            while(l_NewCapacityMin < i_NewCount)
                l_NewCapacityMin *= 2;
            l_NewCapacityMax = l_NewCapacityMin * 2;
        }

        // get the current capacity of the array object
        if(m_pArray)
        {
            l_CurCapacity = getReserveCount();
        }
        
        // the current capacity is in good range, no need to adjust the buffer size
        if(l_CurCapacity >= l_NewCapacityMin && l_CurCapacity <= l_NewCapacityMax)
            return ;
        
        l_NewCapacity = l_NewCapacityMin;
        
        ARRAY_VALUE_T* l_pNewArray = (ARRAY_VALUE_T*)DOME_AllocEx(l_NewCapacity * sizeof(ARRAY_VALUE_T), ARRAY_ALLOCATOR_T);
        for(Int i = 0; i < m_Count; ++i)
        {
            moveElement(l_pNewArray + i, m_pArray + i);
        }
        if(m_pArray)
            DOME_FreeEx(m_pArray, ARRAY_ALLOCATOR_T);
        m_pArray = l_pNewArray;
    }
    
private:
    ARRAY_VALUE_T*      m_pArray;
    Int                 m_Count;
};
    
DOME_NAMESPACE_END

//#endif
