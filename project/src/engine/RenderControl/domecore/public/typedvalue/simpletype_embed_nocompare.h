/*
    filename:       type_embed.h
    author:         Ming Dong
    date:           2016-MAR-16
    description:    
*/
#pragma once

#include "isimpletype.h"

DOME_NAMESPACE_BEGIN

template<class TYPE, class ALLOCATOR_T = IDefaultMemManager>
class TSimpleType_Embed_NoCompare : public ISimpleType
{
public:
    TSimpleType_Embed_NoCompare(const DSimpleTypeName& i_TypeName, const DSimpleTypeName& i_GroupName)
        : m_TypeName(i_TypeName)
        , m_GroupName(i_GroupName)
    {
    }

    virtual ~TSimpleType_Embed_NoCompare(){}

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
        DOME_NewPlacement(TYPE, ((TYPE*)&o_Value));
        return R_SUCCESS;
    }

    virtual DResult             create(DSimpleValuePtr& o_Value, const void* i_pData) const
    {
        DOME_NewPlacement(TYPE, ((TYPE*)&o_Value))(*((TYPE*)i_pData));
        return R_SUCCESS;
    }

    virtual DResult             create(DSimpleValuePtr& o_Value, const DSimpleValuePtr& i_Value) const
    {
        DOME_NewPlacement(TYPE, ((TYPE*)&o_Value))(*((const TYPE*)i_Value.get()));
        return R_SUCCESS;
    }

    virtual DResult             destroy(DSimpleValuePtr& o_Value) const
    {
        ((TYPE*)&o_Value)->~TYPE();
        return R_SUCCESS;
    }

    virtual DResult             copy(DSimpleValuePtr& o_Dest, const void* i_pData) const
    {
        *((TYPE*)&o_Dest) = *((TYPE*)i_pData);
        return R_SUCCESS;
    }

    virtual DResult             copy(DSimpleValuePtr& o_Dest, const DSimpleValuePtr& i_Source) const
    {
        o_Dest = i_Source;
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
        return &i_Value;
    }

    virtual void*               getValuePtr(DSimpleValuePtr& i_Value) const
    {
        return &i_Value;
    }

    virtual Int                 compare(const DSimpleValuePtr& i_V0, const void* i_pData) const
    {
        return 0;
    }

    virtual Int                 compare(const DSimpleValuePtr& i_V0, const DSimpleValuePtr& i_V1) const
    {
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