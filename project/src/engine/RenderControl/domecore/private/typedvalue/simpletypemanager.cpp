/*
    filename:       typemanager.cpp
    author:         Ming Dong
    date:           2016-Feb-04
    description:    
*/
#include "../../public/typedvalue/simpletypemanager.h"
#include "../../public/container/database.h"

DOME_NAMESPACE_BEGIN

typedef const ISimpleType*                                  _TypePtr;
typedef TDataBase<DString, _TypePtr>                        _TypeDataBase;

class DSimpleType_Unknown : public ISimpleType
{
public:
    DSimpleType_Unknown(): m_TypeName("Unknown"), m_GroupName("Internal")
    {
    }

    virtual const DSimpleTypeName&      getTypeName() const {return m_TypeName;}
    virtual DSimpleTypeID               getTypeID() const {return DOME_SIMPLETYPEID_FROM_NAME(m_TypeName);}
    virtual const DSimpleTypeName&      getGroupName() const {return m_GroupName;}
    virtual DSimpleTypeID               getGroupID() const {return DOME_SIMPLETYPEID_FROM_NAME(m_GroupName); }

    virtual Bool                        isComparable() const {return DM_FALSE;}
    virtual Bool                        isSerializable() const {return DM_FALSE;}

    virtual DResult                     create(DSimpleValuePtr& o_Value) const {return R_SUCCESS;}
    virtual DResult                     create(DSimpleValuePtr& o_Value, const void* i_pData) const {return R_SUCCESS;}
    virtual DResult                     create(DSimpleValuePtr& o_Value, const DSimpleValuePtr& i_Value) const {return R_SUCCESS;}

    virtual DResult                     destroy(DSimpleValuePtr& o_Value) const {return R_SUCCESS;}

    virtual DResult                     copy(DSimpleValuePtr& o_Dest, const void* i_pData) const {return R_SUCCESS;}
    virtual DResult                     copy(DSimpleValuePtr& o_Dest, const DSimpleValuePtr& i_Source) const {return R_SUCCESS;}

    virtual DResult                     convertCopy(DSimpleValuePtr& o_Dest, const ISimpleType* i_pSrcType, const void* i_pData) const {return R_SUCCESS;}
    virtual DResult                     convertCopy(DSimpleValuePtr& o_Dest, const ISimpleType* i_pSrcType, const DSimpleValuePtr& i_SrcValue) const {return R_SUCCESS;}

    virtual const void*                 getValuePtr(const DSimpleValuePtr& i_Value) const {return DM_NULL;}
    virtual void*                       getValuePtr(DSimpleValuePtr& i_Value) const {return DM_NULL;}

    virtual Int                         compare(const DSimpleValuePtr& i_V0, const void* i_pData) const {return 0;}
    virtual Int                         compare(const DSimpleValuePtr& i_V0, const DSimpleValuePtr& i_V1) const {return 0;}

    virtual DResult                     serialize(const DSimpleValuePtr& i_Value, Int i_BuffSize, U8* o_pBuffer, Int& o_BufferWrite) const {return R_FAILED;}
    virtual DResult                     deserialize(DSimpleValuePtr& o_Value, Int i_BufferSize, const U8* i_pBuffer, Int& o_BufferRead) const {return R_FAILED;}
public:
    DSimpleTypeName                     m_TypeName;
    DSimpleTypeName                     m_GroupName;
};

struct DSimpleTypeManager::_PrivateData
{
    _TypeDataBase                       m_TypeDataBase;

    DSimpleType_Unknown                 m_SimpleType_Unknown;
};

DSimpleTypeManager::DSimpleTypeManager()
{
    DOME_INTERNALTYPE_INIT(Unknown);
    DOME_INTERNALTYPE_INIT(Int);
    DOME_INTERNALTYPE_INIT(F32);
    DOME_INTERNALTYPE_INIT(F64);
    DOME_INTERNALTYPE_INIT(DStringHash);
    DOME_INTERNALTYPE_INIT(DVector2f);
    DOME_INTERNALTYPE_INIT(DVector3f);
    DOME_INTERNALTYPE_INIT(DVector4f);
    DOME_INTERNALTYPE_INIT(DMatrix2x2f);
    DOME_INTERNALTYPE_INIT(DMatrix3x3f);
    DOME_INTERNALTYPE_INIT(DMatrix4x4f);
    DOME_INTERNALTYPE_INIT(DQuaternionf);
    DOME_INTERNALTYPE_INIT(DLutf);
    DOME_INTERNALTYPE_INIT(DVectorLut1f);
    DOME_INTERNALTYPE_INIT(DVectorLut2f);
    DOME_INTERNALTYPE_INIT(DVectorLut3f);
    DOME_INTERNALTYPE_INIT(DVectorLut4f);
    DOME_INTERNALTYPE_INIT(DVector2d);
    DOME_INTERNALTYPE_INIT(DVector3d);
    DOME_INTERNALTYPE_INIT(DVector4d);
    DOME_INTERNALTYPE_INIT(DMatrix2x2d);
    DOME_INTERNALTYPE_INIT(DMatrix3x3d);
    DOME_INTERNALTYPE_INIT(DMatrix4x4d);
    DOME_INTERNALTYPE_INIT(DQuaterniond);
    DOME_INTERNALTYPE_INIT(DLutd);
    DOME_INTERNALTYPE_INIT(DVectorLut1d);
    DOME_INTERNALTYPE_INIT(DVectorLut2d);
    DOME_INTERNALTYPE_INIT(DVectorLut3d);
    DOME_INTERNALTYPE_INIT(DVectorLut4d);
    DOME_INTERNALTYPE_INIT(DVector2i);
    DOME_INTERNALTYPE_INIT(DVector3i);
    DOME_INTERNALTYPE_INIT(DVector4i);
    DOME_INTERNALTYPE_INIT(DMatrix2x2i);
    DOME_INTERNALTYPE_INIT(DMatrix3x3i);
    DOME_INTERNALTYPE_INIT(DMatrix4x4i);
    DOME_INTERNALTYPE_INIT(DString);
    DOME_INTERNALTYPE_INIT(DHashString);
    DOME_INTERNALTYPE_INIT(DWString);
    DOME_INTERNALTYPE_INIT(DWHashString);
    DOME_INTERNALTYPE_INIT(OSHandle);


    me = DOME_New(_PrivateData);

    registerType(&me->m_SimpleType_Unknown);
}

DSimpleTypeManager::~DSimpleTypeManager()
{
    unregisterType(&me->m_SimpleType_Unknown);

    DOME_Del(me);
}

DResult DSimpleTypeManager::registerType(const ISimpleType* i_pType)
{
    DResult hr;
    hr = me->m_TypeDataBase.add(i_pType->getTypeName().get());
    if (DM_FAIL(hr))
    {
        return hr;
    }

    return me->m_TypeDataBase.set(i_pType->getTypeID().get(), i_pType);
}

DResult DSimpleTypeManager::unregisterType(const DSimpleTypeName& i_TypeName)
{
    return unregisterType(DOME_SIMPLETYPEID_FROM_NAME(i_TypeName));
}

DResult DSimpleTypeManager::unregisterType(DSimpleTypeID i_TypeID)
{
    return me->m_TypeDataBase.set(i_TypeID.get(), DM_NULL);
}

DResult DSimpleTypeManager::unregisterType(const ISimpleType* i_pType)
{
    return unregisterType(i_pType->getTypeID());
}

DSimpleTypeID DSimpleTypeManager::getTypeIDByName(const DSimpleTypeName& i_TypeName) const
{
    return DOME_SIMPLETYPEID_FROM_NAME(i_TypeName);
}

DSimpleTypeName DSimpleTypeManager::getTypeNameByID(DSimpleTypeID i_TypeID) const
{
    DString l_TypeNameStr;
    DResult hr = me->m_TypeDataBase.getKeyFromHash(i_TypeID.get(), l_TypeNameStr);
    if(DM_FAIL(hr))
        l_TypeNameStr = "Unknown";
    return DSimpleTypeName(l_TypeNameStr);
}

Int DSimpleTypeManager::getTypeCount() const
{
    return me->m_TypeDataBase.getCount();
}

const ISimpleType* DSimpleTypeManager::getTypeByIndex(Int i_Index) const
{
    const _TypePtr* l_ppType = me->m_TypeDataBase.get(i_Index);
    if(l_ppType)
        return *l_ppType;
    else
        return DM_NULL;
}

const ISimpleType* DSimpleTypeManager::getTypeByName(const DSimpleTypeName& i_TypeName) const
{
    return getTypeByID(getTypeIDByName(i_TypeName));
}

const ISimpleType* DSimpleTypeManager::getTypeByID(DSimpleTypeID i_TypeID) const
{
    const _TypePtr* l_ppType = me->m_TypeDataBase.get(i_TypeID.get());
    if(l_ppType)
        return *l_ppType;
    else
        return DM_NULL;
}



DOME_NAMESPACE_END