    virtual Bool                        isSerializable() const
    {
        return DM_TRUE;
    }

    virtual DResult                     serialize(const DSimpleValuePtr& i_Value, Int i_BuffSize, U8* o_pBuffer, Int& o_BufferWrite) const
    {
        Int l_DataSize = sizeof(Type_t);
        if ((l_DataSize > i_BuffSize) || (!o_pBuffer))
        {
            o_BufferWrite = l_DataSize;
            return R_BUFFERSIZENOTENOUGH;
        }
        const U8* l_pData = (const U8*)getValuePtr(i_Value);
        for(Int i = 0; i < l_DataSize; ++i)
            o_pBuffer[i] = l_pData[i];
        o_BufferWrite = l_DataSize;
        return R_SUCCESS;
    }

    virtual DResult                     deserialize(DSimpleValuePtr& o_Value, Int i_BufferSize, const U8* i_pBuffer, Int& o_BufferRead) const
    {
        Int l_DataSize = sizeof(Type_t);
        if(l_DataSize > i_BufferSize)
            return R_FAILED;
        U8* l_pData = (U8*)getValuePtr(o_Value);
        for(Int i = 0; i < l_DataSize; ++i)
            l_pData[i] = i_pBuffer[i];
        o_BufferRead = l_DataSize;
        return R_SUCCESS;

    }
