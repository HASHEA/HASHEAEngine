/*
    filename:       isimpletype.h
    author:         Ming Dong
    date:           2016-Mar-15
    description:    
*/
#pragma once

#include "../typedefs.h"
#include "../error.h"
#include "../container.h"
#include "../strongtypedef.h"
#include "../math/mathutils.h"
#include "../container/string.h"
#include "../math.h"

DOME_NAMESPACE_BEGIN

#if DOME_IDE == DOME_IDE_VISUALSTUDIO
#  pragma warning( push )
#  pragma warning( disable: 4251 )
#endif

DOME_STRONGTYPEDEF(DString, DSimpleTypeName, DOME_CORE_API);
DOME_STRONGTYPEDEF(DStringHash, DSimpleTypeID, DOME_CORE_API);
DOME_STRONGTYPEDEF(PtrInt, DSimpleValuePtr, DOME_CORE_API);

#if DOME_IDE == DOME_IDE_VISUALSTUDIO
#  pragma warning( pop )
#endif

#define DOME_SIMPLETYPEID_FROM_NAME(tn) DSimpleTypeID(DStringHash(tn.get().c_str()))


class DOME_CORE_API ISimpleType
{
public:

    virtual ~ISimpleType(){};

    virtual const DSimpleTypeName&      getTypeName() const = 0;
    virtual DSimpleTypeID               getTypeID() const = 0;
    virtual const DSimpleTypeName&      getGroupName() const = 0;
    virtual DSimpleTypeID               getGroupID() const = 0;

    virtual Bool                        isComparable() const = 0;
    virtual Bool                        isSerializable() const = 0;

    virtual DResult                     create(DSimpleValuePtr& o_Value) const = 0;
    virtual DResult                     create(DSimpleValuePtr& o_Value, const void* i_pData) const = 0;
    virtual DResult                     create(DSimpleValuePtr& o_Value, const DSimpleValuePtr& i_Value) const = 0;

    virtual DResult                     destroy(DSimpleValuePtr& o_Value) const = 0;

    virtual DResult                     copy(DSimpleValuePtr& o_Dest, const void* i_pData) const = 0;
    virtual DResult                     copy(DSimpleValuePtr& o_Dest, const DSimpleValuePtr& i_Source) const = 0;

    virtual DResult                     convertCopy(DSimpleValuePtr& o_Dest, const ISimpleType* i_pSrcType, const void* i_pData) const = 0;
    virtual DResult                     convertCopy(DSimpleValuePtr& o_Dest, const ISimpleType* i_pSrcType, const DSimpleValuePtr& i_SrcValue) const = 0;

    virtual const void*                 getValuePtr(const DSimpleValuePtr& i_Value) const = 0;
    virtual void*                       getValuePtr(DSimpleValuePtr& i_Value) const = 0;

    virtual Int                         compare(const DSimpleValuePtr& i_V0, const void* i_pData) const = 0;
    virtual Int                         compare(const DSimpleValuePtr& i_V0, const DSimpleValuePtr& i_V1) const = 0;

    virtual DResult                     serialize(const DSimpleValuePtr& i_Value, Int i_BuffSize, U8* o_pBuffer, Int& o_BufferWrite) const = 0;
    virtual DResult                     deserialize(DSimpleValuePtr& o_Value, Int i_BufferSize, const U8* i_pBuffer, Int& o_BufferRead) const = 0;
};



DOME_NAMESPACE_END