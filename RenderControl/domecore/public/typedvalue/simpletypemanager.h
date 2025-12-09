/*
    filename:       typemanager.h
    author:         Ming Dong
    date:           2016-Feb-04
    description:    
*/
#pragma once

#include "isimpletype.h"

DOME_NAMESPACE_BEGIN

#define DOME_INTERNALTYPE_DECL(TypeName)                                    \
private: mutable const ISimpleType*       m_pCache_##TypeName;              \
private: mutable DSimpleTypeID              m_Cache_ID_##TypeName;          \
public: inline const ISimpleType* getType_##TypeName() const                \
{                                                                           \
    if(m_pCache_##TypeName)                                                 \
        return m_pCache_##TypeName;                                         \
    m_pCache_##TypeName = getTypeByName(DSimpleTypeName(#TypeName));        \
    m_Cache_ID_##TypeName = getTypeIDByName(DSimpleTypeName(#TypeName));    \
    return m_pCache_##TypeName;                                             \
}                                                                           \
public: inline DSimpleTypeID getTypeID_##TypeName() const                   \
{                                                                           \
    if(m_pCache_##TypeName)                                                 \
        return m_Cache_ID_##TypeName;                                       \
    m_pCache_##TypeName = getTypeByName(DSimpleTypeName(#TypeName));        \
    m_Cache_ID_##TypeName = getTypeIDByName(DSimpleTypeName(#TypeName));    \
    return m_Cache_ID_##TypeName;                                           \
}

#define DOME_INTERNALTYPE_INIT(TypeName)                                    \
m_pCache_##TypeName = DM_NULL;                                              \
m_Cache_ID_##TypeName = 0;


class DOME_CORE_API DSimpleTypeManager : public TSingleton<DSimpleTypeManager>
{
public:
    DSimpleTypeManager();
    ~DSimpleTypeManager();

    DResult             registerType(const ISimpleType* i_pType);                 
    DResult             unregisterType(const DSimpleTypeName& i_TypeName);      // this is not remove the item from database (database doesn't support remove), just set the pointer to NULL
    DResult             unregisterType(DSimpleTypeID i_TypeID);                 // this is not remove the item from database (database doesn't support remove), just set the pointer to NULL
    DResult             unregisterType(const ISimpleType* i_pType);             // this is not remove the item from database (database doesn't support remove), just set the pointer to NULL

    DSimpleTypeID       getTypeIDByName(const DSimpleTypeName& i_TypeName) const;
    DSimpleTypeName     getTypeNameByID(DSimpleTypeID i_TypeID) const;

    Int                 getTypeCount() const;                                   // this is not exactly type count, see database class
    const ISimpleType*  getTypeByIndex(Int i_Index) const;                      // the type may be NULL if you unregister it, see database class
    const ISimpleType*  getTypeByName(const DSimpleTypeName& i_TypeName) const;
    const ISimpleType*  getTypeByID(DSimpleTypeID i_TypeID) const;

    // for internal types
    DOME_INTERNALTYPE_DECL(Unknown);
    DOME_INTERNALTYPE_DECL(Int);
    DOME_INTERNALTYPE_DECL(F32);
    DOME_INTERNALTYPE_DECL(F64);
    DOME_INTERNALTYPE_DECL(DStringHash)
    DOME_INTERNALTYPE_DECL(DVector2f);
    DOME_INTERNALTYPE_DECL(DVector3f);
    DOME_INTERNALTYPE_DECL(DVector4f);
    DOME_INTERNALTYPE_DECL(DMatrix2x2f);
    DOME_INTERNALTYPE_DECL(DMatrix3x3f);
    DOME_INTERNALTYPE_DECL(DMatrix4x4f);
    DOME_INTERNALTYPE_DECL(DQuaternionf);
    DOME_INTERNALTYPE_DECL(DLutf);
    DOME_INTERNALTYPE_DECL(DVectorLut1f);
    DOME_INTERNALTYPE_DECL(DVectorLut2f);
    DOME_INTERNALTYPE_DECL(DVectorLut3f);
    DOME_INTERNALTYPE_DECL(DVectorLut4f);
    DOME_INTERNALTYPE_DECL(DVector2d);
    DOME_INTERNALTYPE_DECL(DVector3d);
    DOME_INTERNALTYPE_DECL(DVector4d);
    DOME_INTERNALTYPE_DECL(DMatrix2x2d);
    DOME_INTERNALTYPE_DECL(DMatrix3x3d);
    DOME_INTERNALTYPE_DECL(DMatrix4x4d);
    DOME_INTERNALTYPE_DECL(DQuaterniond);
    DOME_INTERNALTYPE_DECL(DLutd);
    DOME_INTERNALTYPE_DECL(DVectorLut1d);
    DOME_INTERNALTYPE_DECL(DVectorLut2d);
    DOME_INTERNALTYPE_DECL(DVectorLut3d);
    DOME_INTERNALTYPE_DECL(DVectorLut4d);
    DOME_INTERNALTYPE_DECL(DVector2i);
    DOME_INTERNALTYPE_DECL(DVector3i);
    DOME_INTERNALTYPE_DECL(DVector4i);
    DOME_INTERNALTYPE_DECL(DMatrix2x2i);
    DOME_INTERNALTYPE_DECL(DMatrix3x3i);
    DOME_INTERNALTYPE_DECL(DMatrix4x4i);
    DOME_INTERNALTYPE_DECL(DString);
    DOME_INTERNALTYPE_DECL(DHashString);
    DOME_INTERNALTYPE_DECL(DWString);
    DOME_INTERNALTYPE_DECL(DWHashString);
    DOME_INTERNALTYPE_DECL(OSHandle);


private:
    struct _PrivateData;
    _PrivateData*       me;
};


DOME_NAMESPACE_END