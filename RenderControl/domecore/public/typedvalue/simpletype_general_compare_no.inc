    virtual Bool                isComparable() const
    {
        return DM_FALSE;
    }

    virtual Int                 compare(const DSimpleValuePtr& i_V0, const void* i_pData) const
    {
        DOME_ERROR(0);
        return 0;
    }

    virtual Int                 compare(const DSimpleValuePtr& i_V0, const DSimpleValuePtr& i_V1) const
    {
        DOME_ERROR(0);
        return 0;
    }

