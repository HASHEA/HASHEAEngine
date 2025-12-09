/*
    filename:       typemanager.cpp
    author:         Ming Dong
    date:           2016-Feb-04
    description:    
*/
#include "../../public/typedvalue/simpletypemanager.h"
#include "../../public/typedvalue/simpletypedvalue.h"

DOME_NAMESPACE_BEGIN

DSimpleTypedValue::DSimpleTypedValue()
{
    m_pType = DSimpleTypeManager::Instance().getType_Unknown();
    m_Value = DM_NULL;
}

DSimpleTypedValue::DSimpleTypedValue(const ISimpleType* i_pType)
{
    m_pType = i_pType;
    DOME_ASSERT(m_pType);
    m_pType->create(m_Value);
}

DSimpleTypedValue::DSimpleTypedValue(const ISimpleType* i_pType, const void* i_pData)
{
    m_pType = i_pType;
    DOME_ASSERT(m_pType);
    m_pType->create(m_Value, i_pData);
}

DSimpleTypedValue::DSimpleTypedValue(const DSimpleTypeName& i_TypeName)
{
    m_pType = DSimpleTypeManager::Instance().getTypeByName(i_TypeName);
    DOME_ASSERT(m_pType);
    m_pType->create(m_Value);
}

DSimpleTypedValue::DSimpleTypedValue(const DSimpleTypeName& i_TypeName, const void* i_pData)
{
    m_pType = DSimpleTypeManager::Instance().getTypeByName(i_TypeName);
    DOME_ASSERT(m_pType);
    m_pType->create(m_Value, i_pData);
}

DSimpleTypedValue::DSimpleTypedValue(DSimpleTypeID i_TypeID)
{
    m_pType = DSimpleTypeManager::Instance().getTypeByID(i_TypeID);
    DOME_ASSERT(m_pType);
    m_pType->create(m_Value);
}

DSimpleTypedValue::DSimpleTypedValue(DSimpleTypeID i_TypeID, const void* i_pData)
{
    m_pType = DSimpleTypeManager::Instance().getTypeByID(i_TypeID);
    DOME_ASSERT(m_pType);
    m_pType->create(m_Value, i_pData);
}

DSimpleTypedValue::~DSimpleTypedValue()
{
    DOME_ASSERT(m_pType);
    m_pType->destroy(m_Value);
}

DSimpleTypedValue::DSimpleTypedValue(const DSimpleTypedValue& i_Other)
{
    m_pType = i_Other.m_pType;
    m_pType->create(m_Value, i_Other.getValuePtr<const void*>());
}

DSimpleTypedValue& DSimpleTypedValue::operator=(const DSimpleTypedValue& i_Other)
{
    if (m_pType == DSimpleTypeManager::Instance().getType_Unknown())
    {
        m_pType = i_Other.m_pType;
        DOME_ASSERT(m_pType);
        m_pType->create(m_Value);
    }

    DOME_ASSERT(m_pType == i_Other.m_pType);

    m_pType->copy(m_Value, i_Other.m_Value);

    return *this;
}

const ISimpleType* DSimpleTypedValue::getType() const
{
    return m_pType;
}

const DSimpleTypeName& DSimpleTypedValue::getTypeName() const
{
    return m_pType->getTypeName();
}

DSimpleTypeID DSimpleTypedValue::getTypeID() const
{
    return m_pType->getTypeID();
}

DResult DSimpleTypedValue::initType(const ISimpleType* i_pType)
{
    if(m_pType == i_pType)
        return R_SUCCESS;
    if (m_pType == DSimpleTypeManager::Instance().getType_Unknown())
    {
        m_pType = i_pType;
        DOME_ASSERT(m_pType);
        m_pType->create(m_Value);
        return R_SUCCESS;
    }
    return R_FAILED;
}

DResult DSimpleTypedValue::initType(DSimpleTypeID i_TypeID)
{
    const ISimpleType* l_pType = DSimpleTypeManager::Instance().getTypeByID(i_TypeID);
    return initType(l_pType);
}

DResult DSimpleTypedValue::set(const ISimpleType* i_pType, const void* i_pData)
{
    DResult l_Result = initType(i_pType);
    if(DM_FAIL(l_Result))
        return l_Result;

    DOME_ASSERT(m_pType == i_pType);
    if(m_pType != i_pType)
        return R_FAILED;

    m_pType->copy(m_Value, i_pData);

    return R_SUCCESS;
}

DResult DSimpleTypedValue::set(const DSimpleTypeName& i_TypeName, const void* i_pData)
{
    const ISimpleType* l_pType = DSimpleTypeManager::Instance().getTypeByName(i_TypeName);
    return set(l_pType, i_pData);
}

DResult DSimpleTypedValue::set(DSimpleTypeID i_TypeID, const void* i_pData)
{
    const ISimpleType* l_pType = DSimpleTypeManager::Instance().getTypeByID(i_TypeID);
    return set(l_pType, i_pData);
}


// compare
Bool DSimpleTypedValue::isComparable() const
{
    return m_pType->isComparable();
}

Int  DSimpleTypedValue::compare(const DSimpleTypedValue& i_Other) const
{
    return m_pType->compare(m_Value, i_Other.m_Value);
}

// serialize
Bool DSimpleTypedValue::isSerializable() const
{
    return m_pType->isSerializable();
}

DResult DSimpleTypedValue::serialize(Int i_BuffSize, U8* o_pBuffer, Int& o_BufferWrite) const
{
    return m_pType->serialize(m_Value, i_BuffSize, o_pBuffer, o_BufferWrite);
}

DResult DSimpleTypedValue::deserialize(Int i_BufferSize, const U8* i_pBuffer, Int& o_BufferRead)
{
    return m_pType->deserialize(m_Value, i_BufferSize, i_pBuffer, o_BufferRead);
}



DOME_NAMESPACE_END