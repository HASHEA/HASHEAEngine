    virtual Bool                        isSerializable() const
    {
        return DM_TRUE;
    }

    virtual DResult                     serialize(const DSimpleValuePtr& i_Value, Int i_BuffSize, U8* o_pBuffer, Int& o_BufferWrite) const
    {
        const SIMPLETYPE_GENERAL_STRING_CLASS* l_pString = (const SIMPLETYPE_GENERAL_STRING_CLASS*)getValuePtr(i_Value);
        const SIMPLETYPE_GENERAL_STRING_CLASS& l_String = *l_pString;
        S32 l_StringLen = (S32)l_String.getMemSize();
        S32 l_TotalSize = (S32)l_StringLen + sizeof(S32);
        if ((l_TotalSize > i_BuffSize) || (!o_pBuffer))
        {
            o_BufferWrite = l_TotalSize;
            return R_BUFFERSIZENOTENOUGH;
        }
        U8* l_pData = (U8*)&l_TotalSize;
        for(Int i = 0; i < sizeof(S32); ++i)
            o_pBuffer[i] = l_pData[i];
        l_pData = (U8*)l_String.c_str();
        for(Int i = 0; i < l_StringLen; ++i)
            o_pBuffer[i + sizeof(S32)] = l_pData[i];
        o_BufferWrite = l_TotalSize;
        return R_SUCCESS;
    }

    virtual DResult                     deserialize(DSimpleValuePtr& o_Value, Int i_BufferSize, const U8* i_pBuffer, Int& o_BufferRead) const
    {
        SIMPLETYPE_GENERAL_STRING_CLASS* l_pString = (SIMPLETYPE_GENERAL_STRING_CLASS*)getValuePtr(o_Value);
        SIMPLETYPE_GENERAL_STRING_CLASS& l_String = *l_pString;

        if(i_BufferSize < sizeof(S32))
            return R_FAILED;

        S32 l_TotalSize;
        U8* l_pData = (U8*)&l_TotalSize;
        for(Int i = 0; i < sizeof(S32); ++i)
            l_pData[i] = i_pBuffer[i];

        if(l_TotalSize > i_BufferSize)
            return R_FAILED;

        if (l_TotalSize == sizeof(S32))
        {
            l_String.clear();
            o_BufferRead = sizeof(S32);
            return R_SUCCESS;
        }

        const SIMPLETYPE_GENERAL_STRING_CLASS::Char_t* l_pStrBuff = (const SIMPLETYPE_GENERAL_STRING_CLASS::Char_t*)(i_pBuffer + sizeof(S32));
        l_String.copyFrom(l_pStrBuff);
        o_BufferRead = l_TotalSize;
        return R_SUCCESS;
    }
