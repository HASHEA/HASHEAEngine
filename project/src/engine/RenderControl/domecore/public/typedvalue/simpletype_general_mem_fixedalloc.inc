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

#else


#endif