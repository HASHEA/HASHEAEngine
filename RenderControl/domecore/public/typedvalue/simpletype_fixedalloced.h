/*
    filename:       type_fixedalloced.h
    author:         Ming Dong
    date:           2016-MAR-16
    description:    
*/
#pragma once

#include "isimpletype.h"

DOME_NAMESPACE_BEGIN

template<class TYPE, class ALLOCATOR_T = IDefaultMemManager>
class TSimpleType_FixedAlloced : public ISimpleType
{
public:
    TSimpleType_FixedAlloced(const DSimpleTypeName& i_TypeName, const DSimpleTypeName& i_GroupName)
        : m_TypeName(i_TypeName)
        , m_GroupName(i_GroupName)
    {
    }

    virtual ~TSimpleType_FixedAlloced(){}

    virtual const DSimpleTypeName&    getTypeName() const
    {
        return m_TypeName;
    }

    virtual DSimpleTypeID             getTypeID() const
    {
        return DOME_SIMPLETYPEID_FROM_NAME(m_TypeName);
    }

    virtual const DSimpleTypeName&    getGroupName() const
    {
        return m_GroupName;
    }

    virtual DSimpleTypeID             getGroupID() const
    {
        return DOME_SIMPLETYPEID_FROM_NAME(m_GroupName);
    }

    virtual Bool                isComparable() const
    {
        return DM_FALSE;
    }

    virtual Bool                        isSerializable() const
    {
        return DM_FALSE;
    }

    virtual DResult             create(DSimpleValuePtr& o_Value) const
    {
        TYPE* l_ptr = (TYPE*)DOME_AllocFixEx(sizeof(TYPE), ALLOCATOR_T);
        DOME_NewPlacement(TYPE, l_ptr);
        o_Value.get() = (PtrInt)l_ptr;
        return R_SUCCESS;
    }

    virtual DResult             create(DSimpleValuePtr& o_Value, const void* i_pData) const
    {
        TYPE* l_ptr = (TYPE*)DOME_AllocFixEx(sizeof(TYPE), ALLOCATOR_T);
        DOME_NewPlacement(TYPE, l_ptr)(*((TYPE*)i_pData));
        o_Value.get() = (PtrInt)l_ptr;
        return R_SUCCESS;
    }

    virtual DResult             create(DSimpleValuePtr& o_Value, const DSimpleValuePtr& i_Value) const
    {
        TYPE* l_ptr = (TYPE*)DOME_AllocFixEx(sizeof(TYPE), ALLOCATOR_T);
        DOME_NewPlacement(TYPE, l_ptr)(*((const TYPE*)i_Value.get()));
        o_Value.get() = (PtrInt)l_ptr;
        return R_SUCCESS;
    }

    virtual DResult             destroy(DSimpleValuePtr& o_Value) const
    {
        DOME_ASSERT(o_Value.get());
        TYPE* l_ptr = (TYPE*)o_Value.get();
        l_ptr->~TYPE();

        DOME_FreeFixEx(l_ptr, sizeof(TYPE), ALLOCATOR_T);
        return R_SUCCESS;
    }

    virtual DResult             copy(DSimpleValuePtr& o_Dest, const void* i_pData) const
    {
        *((TYPE*)o_Dest.get()) = *((TYPE*)i_pData);
        return R_SUCCESS;
    }

    virtual DResult             copy(DSimpleValuePtr& o_Dest, const DSimpleValuePtr& i_Source) const
    {
        *((TYPE*)o_Dest.get()) = *((TYPE*)i_Source.get());
        return R_SUCCESS;
    }

    virtual DResult             convertCopy(DSimpleValuePtr& o_Dest, const ISimpleType* i_pSrcType, const void* i_pData) const
    {
        return R_FAILED;
    }

    virtual DResult             convertCopy(DSimpleValuePtr& o_Dest, const ISimpleType* i_pSrcType, const DSimpleValuePtr& i_SrcValue) const
    {
        return R_FAILED;
    }

    virtual const void*         getValuePtr(const DSimpleValuePtr& i_Value) const
    {
        return (const void*)i_Value.get();
    }

    virtual void*               getValuePtr(DSimpleValuePtr& i_Value) const
    {
        return (void*)i_Value.get();
    }

    virtual Int                 compare(const DSimpleValuePtr& i_V0, const void* i_pData) const
    {
        DOME_ERROR(0);
        return 0;
    }

    virtual Int                 compare(const DSimpleValuePtr& i_V0, const DSimpleValuePtr& i_V1) const
    {
        DOME_ERROR(0);
        return 0;
    }

    virtual DResult                     serialize(const DSimpleValuePtr& i_Value, Int i_BuffSize, U8* o_pBuffer, Int& o_BufferWrite) const
    {
        DOME_ERROR(0);
        return R_FAILED;
    }

    virtual DResult                     deserialize(DSimpleValuePtr& o_Value, Int i_BufferSize, const U8* i_pBuffer, Int& o_BufferRead) const
    {
        DOME_ERROR(0);
        return R_FAILED;
    }

private:
    DSimpleTypeName       m_TypeName;
    DSimpleTypeName       m_GroupName;
};



DOME_NAMESPACE_END