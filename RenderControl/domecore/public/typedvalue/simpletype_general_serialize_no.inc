    virtual Bool                        isSerializable() const
    {
        return DM_FALSE;
    }

    virtual DResult                     serialize(const DSimpleValuePtr& i_Value, Int i_BuffSize, U8* o_pBuffer, Int& o_BufferWrite) const
    {
        DOME_ERROR(0);
        return R_FAILED;
    }

    virtual DResult                     deserialize(DSimpleValuePtr& o_Value, Int i_BufferSize, const U8* i_pBuffer, Int& o_BufferRead) const
    {
        DOME_ERROR(0);
        return R_FAILED;
    }
