/*
    filename:       rcparametersys.cpp
    author:         Ming Dong
    date:           2016-MAY-14
    description:    
*/

#include "../public/rcparametersys.h"
#include "../public/rcglobal.h"

RC_NAMESPACE_BEGIN

class RCParameterSys::RCParameterSys_Impl
{
public:
    RCParameterSys_Impl()
    {

    }

    ~RCParameterSys_Impl()
    {
        clearAllParameters();
    }

    DResult                     clearAllParameters()
    {
        m_ParamDB.reset();
        return R_SUCCESS;
    }

    DResult                     registerParameter(const DString& i_ParamName, DSimpleTypeID i_TypeID)
    {
        DResult l_Result = m_ParamDB.add(i_ParamName);
        if(DM_FAIL(l_Result))
            return l_Result;

        DStringHash l_Hash(i_ParamName.c_str());
        _ValueInfo* l_pValue = m_ParamDB.get(l_Hash);
        DOME_ASSERT(l_pValue);

        return l_pValue->m_Value.initType(i_TypeID);
    }

    DResult                     registerParameter(const DString& i_ParamName, const DSimpleTypedValue& i_Default, const DString& i_MetaData)
    {
        DResult l_Result = m_ParamDB.add(i_ParamName);
        if(DM_FAIL(l_Result))
            return l_Result;

        DStringHash l_Hash(i_ParamName.c_str());
        _ValueInfo* l_pValue = m_ParamDB.get(l_Hash);
        DOME_ASSERT(l_pValue);
        l_pValue->m_Value = i_Default;
        l_pValue->m_MetaData = i_MetaData;

        return R_SUCCESS;
    }

    DSimpleTypeID               getParameterTypeID(DStringHash i_ParamKey) const
    {
        const _ValueInfo* l_pValue = m_ParamDB.get(i_ParamKey);
        if(!l_pValue)
            return DSimpleTypeManager::Instance().getTypeID_Unknown();

        return l_pValue->m_Value.getTypeID();
    }

    const DString&              getParameterMetaData(DStringHash i_ParamKey) const
    {
        const _ValueInfo* l_pValue = m_ParamDB.get(i_ParamKey);
        if(!l_pValue)
            return m_EmptyString;

        return l_pValue->m_MetaData;
    }

    DResult                     setParameter(DStringHash i_ParamKey, const DSimpleTypedValue& i_Value)
    {
        _ValueInfo* l_pValue = m_ParamDB.get(i_ParamKey);
        if(!l_pValue)
            return R_NOTFOUND;

        l_pValue->m_Value = i_Value;

        return R_SUCCESS;
    }

    DResult                     getParameter(DStringHash i_ParamKey, DSimpleTypedValue& o_Value) const
    {
        const _ValueInfo* l_pValue = m_ParamDB.get(i_ParamKey);
        if(!l_pValue)
            return R_NOTFOUND;

        o_Value = l_pValue->m_Value;
        return R_SUCCESS;
    }

    const DSimpleTypedValue*    getParameter(DStringHash i_ParamKey) const
    {
        const _ValueInfo* l_pValue = m_ParamDB.get(i_ParamKey);
        if(!l_pValue)
            return DM_NULL;

        return &l_pValue->m_Value;
    }

    DSimpleTypedValue*          getParameter(DStringHash i_ParamKey)
    {
        _ValueInfo* l_pValue = m_ParamDB.get(i_ParamKey);
        if(!l_pValue)
            return DM_NULL;

        return &l_pValue->m_Value;
    }

    Int                         getParameterCount() const
    {
        return m_ParamDB.getCount();
    }

    DSimpleTypeID               getParameterTypeID(Int i_Index) const
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < m_ParamDB.getCount());
        return m_ParamDB.get(i_Index)->m_Value.getTypeID();
    }

    const DString&              getParameterMetaData(Int i_Index) const
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < m_ParamDB.getCount());
        return m_ParamDB.get(i_Index)->m_MetaData;
    }

    DResult                     setParameter(Int i_Index, const DSimpleTypedValue& i_Value)
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < m_ParamDB.getCount());
        m_ParamDB.get(i_Index)->m_Value = i_Value;
        return R_SUCCESS;
    }

    DResult                     getParameter(Int i_Index, DSimpleTypedValue& o_Value) const
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < m_ParamDB.getCount());
        o_Value = m_ParamDB.get(i_Index)->m_Value;
        return R_SUCCESS;
    }

    const DSimpleTypedValue*    getParameter(Int i_Index) const
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < m_ParamDB.getCount());
        return &m_ParamDB.get(i_Index)->m_Value;
    }

    DSimpleTypedValue*          getParameter(Int i_Index)
    {
        DOME_ASSERT(i_Index >= 0 && i_Index < m_ParamDB.getCount());
        return &m_ParamDB.get(i_Index)->m_Value;
    }


    // special types
    DResult                     setOSTexture2D(DStringHash i_ParamKey, const OSTexture2D& i_Value)
    {
        DSimpleTypedValue* l_pValue = getParameter(i_ParamKey);
        if (l_pValue)
        {
            return l_pValue->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &i_Value);
        }
        else
            return R_NOTREGISTERED;
    }
    DResult                     getOSTexture2D(DStringHash i_ParamKey, OSTexture2D& o_Value) const
    {
        const DSimpleTypedValue* l_pValue = getParameter(i_ParamKey);
        if (l_pValue && (l_pValue->getTypeID() == RCGlobal::k_SimpleTypeID_OSTexture2D))
        {
            o_Value = *l_pValue->getValuePtr<OSTexture2D>();
            return R_SUCCESS;
        }
        else
            return R_FAILED;
    }

    DResult                     setOSTexture2D(Int i_Index, const OSTexture2D& i_Value)
    {
        DSimpleTypedValue* l_pValue = getParameter(i_Index);
        if (l_pValue)
        {
            return l_pValue->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &i_Value);
        }
        else
            return R_NOTREGISTERED;
    }
    DResult                     getOSTexture2D(Int i_Index, OSTexture2D& o_Value) const
    {
        const DSimpleTypedValue* l_pValue = getParameter(i_Index);
        if (l_pValue && (l_pValue->getTypeID() == RCGlobal::k_SimpleTypeID_OSTexture2D))
        {
            o_Value = *l_pValue->getValuePtr<OSTexture2D>();
            return R_SUCCESS;
        }
        else
            return R_FAILED;
    }

    DOME_PARAMSYS_SPECIALTYPE_IMPL(F32)
    DOME_PARAMSYS_SPECIALTYPE_IMPL(DVector2f)
    DOME_PARAMSYS_SPECIALTYPE_IMPL(DVector3f)
    DOME_PARAMSYS_SPECIALTYPE_IMPL(DVector4f)
    DOME_PARAMSYS_SPECIALTYPE_IMPL(DMatrix2x2f)
    DOME_PARAMSYS_SPECIALTYPE_IMPL(DMatrix3x3f)
    DOME_PARAMSYS_SPECIALTYPE_IMPL(DMatrix4x4f)
    DOME_PARAMSYS_SPECIALTYPE_IMPL(Int)
    DOME_PARAMSYS_SPECIALTYPE_IMPL(DVector2i)
    DOME_PARAMSYS_SPECIALTYPE_IMPL(DVector3i)
    DOME_PARAMSYS_SPECIALTYPE_IMPL(DVector4i)
    DOME_PARAMSYS_SPECIALTYPE_IMPL(DMatrix2x2i)
    DOME_PARAMSYS_SPECIALTYPE_IMPL(DMatrix3x3i)
    DOME_PARAMSYS_SPECIALTYPE_IMPL(DMatrix4x4i)
    DOME_PARAMSYS_SPECIALTYPE_IMPL(DVectorLut1f)
    DOME_PARAMSYS_SPECIALTYPE_IMPL(DVectorLut2f)
    DOME_PARAMSYS_SPECIALTYPE_IMPL(DVectorLut3f)
    DOME_PARAMSYS_SPECIALTYPE_IMPL(DVectorLut4f)


private:
    struct _ValueInfo
    {
        DSimpleTypedValue       m_Value;
        DString                 m_MetaData;
    };
    typedef TDataBase<DString, _ValueInfo>       _ParamDataBase;

    _ParamDataBase                  m_ParamDB;
    DString                         m_EmptyString;
};


RCParameterSys::RCParameterSys()
{
    me = DOME_New(RCParameterSys_Impl);
}

RCParameterSys::~RCParameterSys()
{
    DOME_Del(me);
}

DResult                     RCParameterSys::clearAllParameters()
{
    return me->clearAllParameters();
}

DResult                     RCParameterSys::registerParameter(const DString& i_ParamName, DSimpleTypeID i_TypeID)
{
    return me->registerParameter(i_ParamName, i_TypeID);
}

DResult                     RCParameterSys::registerParameter(const DString& i_ParamName, const DSimpleTypedValue& i_TypeID, const DString& i_MetaData)
{
    return me->registerParameter(i_ParamName, i_TypeID, i_MetaData);
}

DSimpleTypeID               RCParameterSys::getParameterTypeID(DStringHash i_ParamKey) const
{
    return me->getParameterTypeID(i_ParamKey);
}

const DString&              RCParameterSys::getParameterMetaData(DStringHash i_ParamKey) const
{
    return me->getParameterMetaData(i_ParamKey);
}

DResult                     RCParameterSys::setParameter(DStringHash i_ParamKey, const DSimpleTypedValue& i_Value)
{
    return me->setParameter(i_ParamKey, i_Value);
}

DResult                     RCParameterSys::getParameter(DStringHash i_ParamKey, DSimpleTypedValue& o_Value) const
{
    return me->getParameter(i_ParamKey, o_Value);
}

const DSimpleTypedValue*    RCParameterSys::getParameter(DStringHash i_ParamKey) const
{
    return me->getParameter(i_ParamKey);
}

DSimpleTypedValue*          RCParameterSys::getParameter(DStringHash i_ParamKey)
{
    return me->getParameter(i_ParamKey);
}

Int                         RCParameterSys::getParameterCount() const
{
    return me->getParameterCount();
}

DSimpleTypeID               RCParameterSys::getParameterTypeID(Int i_Index) const
{
    return me->getParameterTypeID(i_Index);
}

const DString&              RCParameterSys::getParameterMetaData(Int i_Index) const
{
    return me->getParameterMetaData(i_Index);
}

DResult                     RCParameterSys::setParameter(Int i_Index, const DSimpleTypedValue& i_Value)
{
    return me->setParameter(i_Index, i_Value);
}

DResult                     RCParameterSys::getParameter(Int i_Index, DSimpleTypedValue& o_Value) const
{
    return me->getParameter(i_Index, o_Value);
}

const DSimpleTypedValue*    RCParameterSys::getParameter(Int i_Index) const
{
    return me->getParameter(i_Index);
}

DSimpleTypedValue*          RCParameterSys::getParameter(Int i_Index)
{
    return me->getParameter(i_Index);
}

// special types
DOME_PARAMSYS_SPECIALTYPE_DEF(OSTexture2D)
DOME_PARAMSYS_SPECIALTYPE_DEF(F32)
DOME_PARAMSYS_SPECIALTYPE_DEF(DVector2f)
DOME_PARAMSYS_SPECIALTYPE_DEF(DVector3f)
DOME_PARAMSYS_SPECIALTYPE_DEF(DVector4f)
DOME_PARAMSYS_SPECIALTYPE_DEF(DMatrix2x2f)
DOME_PARAMSYS_SPECIALTYPE_DEF(DMatrix3x3f)
DOME_PARAMSYS_SPECIALTYPE_DEF(DMatrix4x4f)
DOME_PARAMSYS_SPECIALTYPE_DEF(Int)
DOME_PARAMSYS_SPECIALTYPE_DEF(DVector2i)
DOME_PARAMSYS_SPECIALTYPE_DEF(DVector3i)
DOME_PARAMSYS_SPECIALTYPE_DEF(DVector4i)
DOME_PARAMSYS_SPECIALTYPE_DEF(DMatrix2x2i)
DOME_PARAMSYS_SPECIALTYPE_DEF(DMatrix3x3i)
DOME_PARAMSYS_SPECIALTYPE_DEF(DMatrix4x4i)
DOME_PARAMSYS_SPECIALTYPE_DEF(DVectorLut1f)
DOME_PARAMSYS_SPECIALTYPE_DEF(DVectorLut2f)
DOME_PARAMSYS_SPECIALTYPE_DEF(DVectorLut3f)
DOME_PARAMSYS_SPECIALTYPE_DEF(DVectorLut4f)



RC_NAMESPACE_END