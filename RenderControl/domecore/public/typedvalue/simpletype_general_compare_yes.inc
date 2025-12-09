    virtual Bool                isComparable() const
    {
        return DM_TRUE;
    }

    virtual Int                 compare(const DSimpleValuePtr& i_V0, const void* i_pData) const
    {
        const TYPE& l_v0 = *((TYPE*)getValuePtr(i_V0));
        const TYPE& l_v1 = *((TYPE*)i_pData);
        if(l_v0 < l_v1) return -1;
        else if(l_v0 > l_v1) return 1;
        else return 0;
    }

    virtual Int                 compare(const DSimpleValuePtr& i_V0, const DSimpleValuePtr& i_V1) const
    {
        const TYPE& l_v0 = *((TYPE*)getValuePtr(i_V0));
        const TYPE& l_v1 = *((TYPE*)getValuePtr(i_V1));
        if(l_v0 < l_v1) return -1;
        else if(l_v0 > l_v1) return 1;
        else return 0;
    }

