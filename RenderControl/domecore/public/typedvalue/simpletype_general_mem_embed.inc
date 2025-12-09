#if SIMPLETYPE_GENERAL_INSIDECLASS

    SIMPLETYPE_GENERAL_CLASS_NAME(const DSimpleTypeName& i_TypeName, const DSimpleTypeName& i_GroupName)
        : m_TypeName(i_TypeName)
        , m_GroupName(i_GroupName)
    {
    }

    virtual ~SIMPLETYPE_GENERAL_CLASS_NAME(){}

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


#else

template<>
DResult SIMPLETYPE_GENERAL_CLASS_NAME<Int>::convertCopy(DSimpleValuePtr& o_Dest, const ISimpleType* i_pSrcType, const void* i_pData) const
{
    DResult hr = R_FAILED;
    DSimpleTypeID l_TypeID = i_pSrcType->getTypeID();
    if (DSimpleTypeManager::Instance().getTypeID_Int() == l_TypeID)
    {    
        Int l_Value = (Int)(*((Int*)i_pData));
        copy(o_Dest, &l_Value);
        hr = R_SUCCESS;
    }
    else if (DSimpleTypeManager::Instance().getTypeID_F32() == l_TypeID)
    {
        Int l_Value = (Int)(*((F32*)i_pData));
        copy(o_Dest, &l_Value);
        hr = R_SUCCESS;
    }
    else if (DSimpleTypeManager::Instance().getTypeID_F64() == l_TypeID)
    {
        Int l_Value = (Int)(*((F64*)i_pData));
        copy(o_Dest, &l_Value);
        hr = R_SUCCESS;
    }
    return hr;
}

template<>
DResult SIMPLETYPE_GENERAL_CLASS_NAME<F32>::convertCopy(DSimpleValuePtr& o_Dest, const ISimpleType* i_pSrcType, const void* i_pData) const
{
    DResult hr = R_FAILED;
    DSimpleTypeID l_TypeID = i_pSrcType->getTypeID();
    if (DSimpleTypeManager::Instance().getTypeID_Int() == l_TypeID)
    {    
        F32 l_Value = (F32)(*((Int*)i_pData));
        copy(o_Dest, &l_Value);
        hr = R_SUCCESS;
    }
    else if (DSimpleTypeManager::Instance().getTypeID_F32() == l_TypeID)
    {
        F32 l_Value = (F32)(*((F32*)i_pData));
        copy(o_Dest, &l_Value);
        hr = R_SUCCESS;
    }
    else if (DSimpleTypeManager::Instance().getTypeID_F64() == l_TypeID)
    {
        F32 l_Value = (F32)(*((F64*)i_pData));
        copy(o_Dest, &l_Value);
        hr = R_SUCCESS;
    }
    return hr;
}

template<>
DResult SIMPLETYPE_GENERAL_CLASS_NAME<F64>::convertCopy(DSimpleValuePtr& o_Dest, const ISimpleType* i_pSrcType, const void* i_pData) const
{
    DResult hr = R_FAILED;
    DSimpleTypeID l_TypeID = i_pSrcType->getTypeID();
    if (DSimpleTypeManager::Instance().getTypeID_Int() == l_TypeID)
    {    
        F64 l_Value = (F64)(*((Int*)i_pData));
        copy(o_Dest, &l_Value);
        hr = R_SUCCESS;
    }
    else if (DSimpleTypeManager::Instance().getTypeID_F32() == l_TypeID)
    {
        F64 l_Value = (F64)(*((F32*)i_pData));
        copy(o_Dest, &l_Value);
        hr = R_SUCCESS;
    }
    else if (DSimpleTypeManager::Instance().getTypeID_F64() == l_TypeID)
    {
        F64 l_Value = (F64)(*((F64*)i_pData));
        copy(o_Dest, &l_Value);
        hr = R_SUCCESS;
    }
    return hr;
}

template<>
DResult SIMPLETYPE_GENERAL_CLASS_NAME<Int>::convertCopy(DSimpleValuePtr& o_Dest, const ISimpleType* i_pSrcType, const DSimpleValuePtr& i_SrcValue) const
{
    DResult hr = R_FAILED;
    DSimpleTypeID l_TypeID = i_pSrcType->getTypeID();
    if (DSimpleTypeManager::Instance().getTypeID_Int() == l_TypeID)
    {    
        Int l_Value = (Int)(*((Int*)&i_SrcValue));
        copy(o_Dest, &l_Value);
        hr = R_SUCCESS;
    }
    else if (DSimpleTypeManager::Instance().getTypeID_F32() == l_TypeID)
    {
        Int l_Value = (Int)(*((F32*)&i_SrcValue));
        copy(o_Dest, &l_Value);
        hr = R_SUCCESS;
    }
    else if (DSimpleTypeManager::Instance().getTypeID_F64() == l_TypeID)
    {
        Int l_Value = (Int)(*((F64*)&i_SrcValue));
        copy(o_Dest, &l_Value);
        hr = R_SUCCESS;
    }
    return hr;
}

template<>
DResult SIMPLETYPE_GENERAL_CLASS_NAME<F32>::convertCopy(DSimpleValuePtr& o_Dest, const ISimpleType* i_pSrcType, const DSimpleValuePtr& i_SrcValue) const
{
    DResult hr = R_FAILED;
    DSimpleTypeID l_TypeID = i_pSrcType->getTypeID();
    if (DSimpleTypeManager::Instance().getTypeID_Int() == l_TypeID)
    {    
        F32 l_Value = (F32)(*((Int*)&i_SrcValue));
        copy(o_Dest, &l_Value);
        hr = R_SUCCESS;
    }
    else if (DSimpleTypeManager::Instance().getTypeID_F32() == l_TypeID)
    {
        F32 l_Value = (F32)(*((F32*)&i_SrcValue));
        copy(o_Dest, &l_Value);
        hr = R_SUCCESS;
    }
    else if (DSimpleTypeManager::Instance().getTypeID_F64() == l_TypeID)
    {
        F32 l_Value = (F32)(*((F64*)&i_SrcValue));
        copy(o_Dest, &l_Value);
        hr = R_SUCCESS;
    }
    return hr;
}

template<>
DResult SIMPLETYPE_GENERAL_CLASS_NAME<F64>::convertCopy(DSimpleValuePtr& o_Dest, const ISimpleType* i_pSrcType, const DSimpleValuePtr& i_SrcValue) const
{
    DResult hr = R_FAILED;
    DSimpleTypeID l_TypeID = i_pSrcType->getTypeID();
    if (DSimpleTypeManager::Instance().getTypeID_Int() == l_TypeID)
    {    
        F64 l_Value = (F64)(*((Int*)&i_SrcValue));
        copy(o_Dest, &l_Value);
        hr = R_SUCCESS;
    }
    else if (DSimpleTypeManager::Instance().getTypeID_F32() == l_TypeID)
    {
        F64 l_Value = (F64)(*((F32*)&i_SrcValue));
        copy(o_Dest, &l_Value);
        hr = R_SUCCESS;
    }
    else if (DSimpleTypeManager::Instance().getTypeID_F64() == l_TypeID)
    {
        F64 l_Value = (F64)(*((F64*)&i_SrcValue));
        copy(o_Dest, &l_Value);
        hr = R_SUCCESS;
    }
    return hr;
}

#endif