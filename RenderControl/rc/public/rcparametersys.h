/*
    filename:       rcparametersys.h
    author:         Ming Dong
    date:           2016-MAY-14
    description:    
*/
#pragma once

#include "rcconfigure.h"
#include "rcrenderer.h"

RC_NAMESPACE_BEGIN

#define DOME_PARAMSYS_SPECIALTYPE_DECL(TYPENAME)                                                                        \
DResult                     set##TYPENAME(DStringHash i_ParamKey, const TYPENAME& i_Value);                             \
DResult                     get##TYPENAME(DStringHash i_ParamKey, TYPENAME& o_Value) const;                             \
DResult                     set##TYPENAME(Int i_Index, const TYPENAME& i_Value);                                        \
DResult                     get##TYPENAME(Int i_Index, TYPENAME& o_Value) const;                             

#define DOME_PARAMSYS_SPECIALTYPE_DEF(TYPENAME)                                                                         \
DResult                     RCParameterSys::set##TYPENAME(DStringHash i_ParamKey, const TYPENAME& i_Value)              \
{                                                                                                                       \
    return me->set##TYPENAME(i_ParamKey, i_Value);                                                                      \
}                                                                                                                       \
DResult                     RCParameterSys::get##TYPENAME(DStringHash i_ParamKey, TYPENAME& o_Value) const              \
{                                                                                                                       \
    return me->get##TYPENAME(i_ParamKey, o_Value);                                                                      \
}                                                                                                                       \
DResult                     RCParameterSys::set##TYPENAME(Int i_Index, const TYPENAME& i_Value)                         \
{                                                                                                                       \
    return me->set##TYPENAME(i_Index, i_Value);                                                                         \
}                                                                                                                       \
DResult                     RCParameterSys::get##TYPENAME(Int i_Index, TYPENAME& o_Value) const                         \
{                                                                                                                       \
    return me->get##TYPENAME(i_Index, o_Value);                                                                         \
}

#define DOME_PARAMSYS_SPECIALTYPE_IMPL(TYPENAME)                                                                        \
DResult                     set##TYPENAME(DStringHash i_ParamKey, const TYPENAME& i_Value)                              \
{                                                                                                                       \
    DSimpleTypedValue* l_pValue = getParameter(i_ParamKey);                                                             \
    if (l_pValue)                                                                                                       \
    {                                                                                                                   \
        return l_pValue->set##TYPENAME(i_Value);                                                                        \
    }                                                                                                                   \
    else                                                                                                                \
        return R_NOTREGISTERED;                                                                                         \
}                                                                                                                       \
DResult                     get##TYPENAME(DStringHash i_ParamKey, TYPENAME& o_Value) const                              \
{                                                                                                                       \
    const DSimpleTypedValue* l_pValue = getParameter(i_ParamKey);                                                       \
    if (l_pValue && l_pValue->is##TYPENAME())                                                                           \
    {                                                                                                                   \
        o_Value = l_pValue->get##TYPENAME();                                                                            \
        return R_SUCCESS;                                                                                               \
    }                                                                                                                   \
    else                                                                                                                \
        return R_FAILED;                                                                                                \
}                                                                                                                       \
DResult                     set##TYPENAME(Int i_Index, const TYPENAME& i_Value)                                         \
{                                                                                                                       \
    DSimpleTypedValue* l_pValue = getParameter(i_Index);                                                                \
    if (l_pValue)                                                                                                       \
    {                                                                                                                   \
        return l_pValue->set##TYPENAME(i_Value);                                                                        \
    }                                                                                                                   \
    else                                                                                                                \
        return R_NOTREGISTERED;                                                                                         \
}                                                                                                                       \
DResult                     get##TYPENAME(Int i_Index, TYPENAME& o_Value) const                                         \
{                                                                                                                       \
    const DSimpleTypedValue* l_pValue = getParameter(i_Index);                                                          \
    if (l_pValue && l_pValue->is##TYPENAME())                                                                           \
    {                                                                                                                   \
        o_Value = l_pValue->get##TYPENAME();                                                                            \
        return R_SUCCESS;                                                                                               \
    }                                                                                                                   \
    else                                                                                                                \
        return R_FAILED;                                                                                                \
}


class RC_API RCParameterSys
{
public:
    RCParameterSys();
    ~RCParameterSys();

    DResult                     clearAllParameters();

    DResult                     registerParameter(const DString& i_ParamName, DSimpleTypeID i_TypeID);

    DResult                     registerParameter(const DString& i_ParamName, const DSimpleTypedValue& i_Default, const DString& i_MetaData);

    DSimpleTypeID               getParameterTypeID(DStringHash i_ParamKey) const;

    const DString&              getParameterMetaData(DStringHash i_ParamKey) const;

    DResult                     setParameter(DStringHash i_ParamKey, const DSimpleTypedValue& i_Value);

    DResult                     getParameter(DStringHash i_ParamKey, DSimpleTypedValue& o_Value) const;

    const DSimpleTypedValue*    getParameter(DStringHash i_ParamKey) const;

    DSimpleTypedValue*          getParameter(DStringHash i_ParamKey);

    Int                         getParameterCount() const;

    DSimpleTypeID               getParameterTypeID(Int i_Index) const;

    const DString&              getParameterMetaData(Int i_Index) const;

    DResult                     setParameter(Int i_Index, const DSimpleTypedValue& i_Value);

    DResult                     getParameter(Int i_Index, DSimpleTypedValue& o_Value) const;

    const DSimpleTypedValue*    getParameter(Int i_Index) const;

    DSimpleTypedValue*          getParameter(Int i_Index);


    // special types
    DOME_PARAMSYS_SPECIALTYPE_DECL(OSTexture2D)
    DOME_PARAMSYS_SPECIALTYPE_DECL(F32)
    DOME_PARAMSYS_SPECIALTYPE_DECL(DVector2f)
    DOME_PARAMSYS_SPECIALTYPE_DECL(DVector3f)
    DOME_PARAMSYS_SPECIALTYPE_DECL(DVector4f)
    DOME_PARAMSYS_SPECIALTYPE_DECL(DMatrix2x2f)
    DOME_PARAMSYS_SPECIALTYPE_DECL(DMatrix3x3f)
    DOME_PARAMSYS_SPECIALTYPE_DECL(DMatrix4x4f)
    DOME_PARAMSYS_SPECIALTYPE_DECL(Int)
    DOME_PARAMSYS_SPECIALTYPE_DECL(DVector2i)
    DOME_PARAMSYS_SPECIALTYPE_DECL(DVector3i)
    DOME_PARAMSYS_SPECIALTYPE_DECL(DVector4i)
    DOME_PARAMSYS_SPECIALTYPE_DECL(DMatrix2x2i)
    DOME_PARAMSYS_SPECIALTYPE_DECL(DMatrix3x3i)
    DOME_PARAMSYS_SPECIALTYPE_DECL(DMatrix4x4i)
    DOME_PARAMSYS_SPECIALTYPE_DECL(DVectorLut1f)
    DOME_PARAMSYS_SPECIALTYPE_DECL(DVectorLut2f)
    DOME_PARAMSYS_SPECIALTYPE_DECL(DVectorLut3f)
    DOME_PARAMSYS_SPECIALTYPE_DECL(DVectorLut4f)

private:
    class RCParameterSys_Impl;
    RCParameterSys_Impl*   me;
};


RC_NAMESPACE_END