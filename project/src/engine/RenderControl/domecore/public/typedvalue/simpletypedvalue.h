/*
    filename:       typedvalue.h
    author:         Ming Dong
    date:           2016-Mar-15
    description:    
*/
#pragma once

#include "isimpletype.h"
#include "simpletypemanager.h"

DOME_NAMESPACE_BEGIN



#define DOME_INTERNALTYPE(Type)                                                         \
DSimpleTypedValue(const Type& i_Value)                                                  \
{                                                                                       \
    m_pType = DSimpleTypeManager::Instance().getType_##Type();                          \
    DOME_ASSERT(m_pType);                                                               \
    m_pType->create(m_Value, (void*)(&i_Value));                                        \
}                                                                                       \
Bool is##Type() const                                                                   \
{                                                                                       \
    return DSimpleTypeManager::Instance().getTypeID_##Type() == m_pType->getTypeID();   \
}                                                                                       \
DResult set##Type(const Type& i_Value)                                                  \
{                                                                                       \
    if (is##Type())                                                                     \
    {                                                                                   \
        m_pType->copy(m_Value, (void*)(&i_Value));                                      \
        return R_SUCCESS;                                                               \
    }                                                                                   \
    else                                                                                \
        return R_TYPEMISMATCH;                                                          \
}                                                                                       \
Type& get##Type()                                                                       \
{                                                                                       \
    DOME_ASSERT(is##Type());                                                            \
    return *((Type*)m_pType->getValuePtr(m_Value));                                     \
}                                                                                       \
const Type& get##Type() const                                                           \
{                                                                                       \
    DOME_ASSERT(is##Type());                                                            \
    return *((const Type*)m_pType->getValuePtr(m_Value));                               \
}                                                                                       \
Type* get##Type##Ptr()                                                                 \
{                                                                                       \
    DOME_ASSERT(is##Type());                                                            \
    return (Type*)m_pType->getValuePtr(m_Value);                                        \
}                                                                                       \
const Type* get##Type##Ptr() const                                                     \
{                                                                                       \
    DOME_ASSERT(is##Type());                                                            \
    return (const Type*)m_pType->getValuePtr(m_Value);                                  \
}                                                                                       \
DSimpleTypedValue& operator=(const Type& i_Value)                                       \
{                                                                                       \
    DResult hr = set##Type(i_Value);                                                    \
    DOME_ASSERT(DM_SUCC(hr));                                                           \
    return *this;                                                                       \
}


class DOME_CORE_API DSimpleTypedValue
{
public:
    DSimpleTypedValue();
    DSimpleTypedValue(const ISimpleType* i_pType);
    DSimpleTypedValue(const ISimpleType* i_pType, const void* i_pData);
    DSimpleTypedValue(const DSimpleTypeName& i_TypeName);
    DSimpleTypedValue(const DSimpleTypeName& i_TypeName, const void* i_pData);
    DSimpleTypedValue(DSimpleTypeID i_TypeID);
    DSimpleTypedValue(DSimpleTypeID i_TypeID, const void* i_pData);

    ~DSimpleTypedValue();

    DSimpleTypedValue(const DSimpleTypedValue& i_Other);
    DSimpleTypedValue& operator=(const DSimpleTypedValue& i_Other);

    const ISimpleType* getType() const;
    const DSimpleTypeName& getTypeName() const;
    DSimpleTypeID getTypeID() const;

    // init the value type
    // these functions call only success when current type is unkonwn type
    DResult initType(const ISimpleType* i_pType);
    DResult initType(DSimpleTypeID i_TypeID);

    // set and get
    DResult set(const ISimpleType* i_pType, const void* i_pData);
    DResult set(const DSimpleTypeName& i_TypeName, const void* i_pData);
    DResult set(DSimpleTypeID i_TypeID, const void* i_pData);

    template <class TYPE>
    const TYPE* getValuePtr() const
    {
        return (const TYPE*)m_pType->getValuePtr(m_Value);
    }

    template <class TYPE>
    TYPE* getValuePtr()
    {
        return (TYPE*)m_pType->getValuePtr(m_Value);
    }

    template <class TYPE>
    const TYPE& getValue() const
    {
        return *((const TYPE*)m_pType->getValuePtr(m_Value));
    }

    template <class TYPE>
    TYPE& getValue()
    {
        return *((TYPE*)m_pType->getValuePtr(m_Value));
    }

    template <class TYPE>
    DResult setValue(const TYPE& i_Value)
    {
        m_pType->copy(m_Value, (void*)(&i_Value));
        return R_SUCCESS;
    }

    // compare
    Bool isComparable() const;
    Int  compare(const DSimpleTypedValue& i_Other) const;

    // serialize
    Bool isSerializable() const;
    DResult serialize(Int i_BuffSize, U8* o_pBuffer, Int& o_BufferWrite) const;
    DResult deserialize(Int i_BufferSize, const U8* i_pBuffer, Int& o_BufferRead);


    // INTERNAL TYPES, THESE TYPES WILL BE CACHED AND 
    // CAN BE ACCESSED IN A FAST WAY
    DOME_INTERNALTYPE(Int);
    DOME_INTERNALTYPE(F32);
    DOME_INTERNALTYPE(F64);
    DOME_INTERNALTYPE(DStringHash);
    DOME_INTERNALTYPE(DVector2f);
    DOME_INTERNALTYPE(DVector3f);
    DOME_INTERNALTYPE(DVector4f);
    DOME_INTERNALTYPE(DMatrix2x2f);
    DOME_INTERNALTYPE(DMatrix3x3f);
    DOME_INTERNALTYPE(DMatrix4x4f);
    DOME_INTERNALTYPE(DQuaternionf);
    DOME_INTERNALTYPE(DLutf);
    DOME_INTERNALTYPE(DVectorLut1f);
    DOME_INTERNALTYPE(DVectorLut2f);
    DOME_INTERNALTYPE(DVectorLut3f);
    DOME_INTERNALTYPE(DVectorLut4f);
    DOME_INTERNALTYPE(DVector2d);
    DOME_INTERNALTYPE(DVector3d);
    DOME_INTERNALTYPE(DVector4d);
    DOME_INTERNALTYPE(DMatrix2x2d);
    DOME_INTERNALTYPE(DMatrix3x3d);
    DOME_INTERNALTYPE(DMatrix4x4d);
    DOME_INTERNALTYPE(DQuaterniond);
    DOME_INTERNALTYPE(DLutd);
    DOME_INTERNALTYPE(DVectorLut1d);
    DOME_INTERNALTYPE(DVectorLut2d);
    DOME_INTERNALTYPE(DVectorLut3d);
    DOME_INTERNALTYPE(DVectorLut4d);
    DOME_INTERNALTYPE(DVector2i);
    DOME_INTERNALTYPE(DVector3i);
    DOME_INTERNALTYPE(DVector4i);
    DOME_INTERNALTYPE(DMatrix2x2i);
    DOME_INTERNALTYPE(DMatrix3x3i);
    DOME_INTERNALTYPE(DMatrix4x4i);
    DOME_INTERNALTYPE(DString);
    DOME_INTERNALTYPE(DHashString);
    DOME_INTERNALTYPE(DWString);
    DOME_INTERNALTYPE(DWHashString);
    DOME_INTERNALTYPE(OSHandle);


private:
    const ISimpleType*  m_pType;
#if DOME_IS_32BIT
    U32                 m_Pad1;
#endif
    DSimpleValuePtr     m_Value;            // the size is max(8, sizeof(void*)), so that it can fit Int, F32 and F64 types
#if DOME_IS_32BIT
    U32                 m_Pad2;
#endif
};


DOME_NAMESPACE_END