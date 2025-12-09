/*
    filename:       database.h
    author:         Ming Dong
    date:           2016-MAR-17
    description:    
*/
#pragma once

#include "array.h"
#include "map.h"
#include "string.h"
#include "hashstring.h"
#include "ustring.h"
#include "uhashstring.h"

DOME_NAMESPACE_BEGIN

/*
    STRING_T must be a type derived from TCommonString class
    VALUE_T can be any type you want to store
    MAXSIZE is the max number of data this structure can store

    The data can't be removed, it is increase only, so if you want to remove
    some data, mark it out as removed.
*/
template <class STRING_T, class VALUE_T, Int PAGESIZE = 1024, Int PAGEGROWSTEP = 8, class ALLOCATOR_T = IDefaultMemManager>
class TDataBase
{
public:
    typedef ALLOCATOR_T                                         Allocator_t;
    typedef STRING_T                                            Key_t;
    typedef TStringHash<typename Key_t::Char_t>                 Hash_t;
    typedef VALUE_T                                             Value_t;
    typedef TArray<Value_t, Allocator_t, PAGESIZE, 0>           DataPage_t;
    typedef TArray<DataPage_t*, Allocator_t, PAGEGROWSTEP>      DataPageArray_t;
    typedef TMap<Hash_t, Value_t*, Math::TCompare<Hash_t, Hash_t>, Allocator_t>   HashDataDict_t;
    typedef TMap<Hash_t, Key_t, Math::TCompare<Hash_t, Hash_t>, Allocator_t>      HashKeyDict_t;

    TDataBase()
    {
        m_DataCount = 0;
    }

    ~TDataBase()
    {
        reset();
    }

    /*
        reset the database
    */
    DResult reset()
    {
        for (Int i = 0; i < m_DataPageArray.size(); ++i)
        {
            DOME_ASSERT(m_DataPageArray[i]);
            DOME_DelEx(m_DataPageArray[i], Allocator_t);
        }
        m_DataPageArray.clear();

        m_HashDataDict.clear();
        m_HashKeyDict.clear();

        m_DataCount = 0;

        return R_SUCCESS;
    }

    /*
        Get the total value count
    */
    Int getCount() const
    {
        return m_DataCount;
    }

    /*
        Add a key to the data base, after this, 
        you can set/get the value by key or index
        return R_ALREADYREGISTERED if this key is added before
        return R_STRINGHASHCONFLICT if two different string has same hash value
        return R_SUCCESS when success
    */
    DResult add(const Key_t& i_Key)
    {
        Hash_t l_Hash(i_Key.c_str());
        HashDataDict_t::iterator dit = m_HashDataDict.find(l_Hash);
        if (dit != m_HashDataDict.end())
        {
            HashKeyDict_t::iterator kit = m_HashKeyDict.find(l_Hash);
            DOME_ASSERT(kit != m_HashKeyDict.end());

            const Key_t& l_Key = kit->second;

            if(OS_String::TCompareStr(i_Key.c_str(), l_Key.c_str()) == 0)
                return R_ALREADYREGISTERED;
            else
            {
                DOME_ERROR2(0, "ERROR: there is string hash confliction, consider to change name.");
                return R_STRINGHASHCONFLICT;
            }
        }

        Value_t* l_pValue = DM_NULL;
        Int l_NewCount = m_DataCount + 1;
        Int l_PageIndex = (l_NewCount - 1) / PAGESIZE;
        if (l_PageIndex < m_DataPageArray.size())
        {
            DataPage_t* l_pDataPage = m_DataPageArray[l_PageIndex];
            DOME_ASSERT(l_pDataPage);
            l_pValue = (*l_pDataPage).add().getPtr();
        }
        else
        {
            DOME_ASSERT(l_PageIndex == m_DataPageArray.size());
            DOME_ASSERT((l_NewCount % PAGESIZE) == 1);

            DataPage_t* l_pDataPage = DOME_NewEx(DataPage_t, Allocator_t);
            m_DataPageArray.push_back(l_pDataPage);

            l_pValue = (*l_pDataPage).add().getPtr();
        }

        m_HashDataDict[l_Hash] = l_pValue;
        m_HashKeyDict[l_Hash] = i_Key;

        m_DataCount ++;
        return R_SUCCESS;
    }

    /*
        return if the key is registered or not
    */
    Bool isRegistered(const Key_t& i_Key)
    {
        Hash_t l_Hash(i_Key.c_str());
        HashDataDict_t::iterator dit = m_HashDataDict.find(l_Hash);
        if (dit != m_HashDataDict.end())
        {
            HashKeyDict_t::iterator kit = m_HashKeyDict.find(l_Hash);
            DOME_ASSERT(kit != m_HashKeyDict.end());

            const Key_t& l_Key = kit->second;

            if(OS_String::TCompareStr(i_Key.c_str(), l_Key.c_str()) == 0)
                return DM_TRUE;
            else
                return DM_FALSE;
        }
        return DM_FALSE;
    }

    /*
        if there is another different key, but has same hash with this key, return DM_TRUE
        otherwise, return DM_FALSE
    */
    Bool isKeyConfilict(const Key_t& i_Key)
    {
        Hash_t l_Hash(i_Key.c_str());
        HashDataDict_t::iterator dit = m_HashDataDict.find(l_Hash);
        if (dit != m_HashDataDict.end())
        {
            HashKeyDict_t::iterator kit = m_HashKeyDict.find(l_Hash);
            DOME_ASSERT(kit != m_HashKeyDict.end());

            const Key_t& l_Key = kit->second;

            if(OS_String::TCompareStr(i_Key.c_str(), l_Key.c_str()) == 0)
                return DM_FALSE;
            else
                return DM_TRUE;
        }
        return DM_FALSE;
    }

    /*
        Get key from hash
    */
    const DResult getKeyFromHash(Hash_t i_Hash, Key_t& o_Key) const
    {
        HashKeyDict_t::const_iterator cit = m_HashKeyDict.find(i_Hash);
        if (cit == m_HashKeyDict.end())
        {
            return R_NOTREGISTERED;
        }
        o_Key = cit->second;
        return R_SUCCESS;
    }

    /*
        Get hash from key
    */
    Hash_t getHashFromKey(const Key_t& i_key) const
    {
        return Hash_t(i_Key.c_str());
    }

    /*
        Set value by index
        return R_OUTOFRANGE if the index is out of range
        return R_SUCCESS when success
    */
    DResult set(Int i_Index, const Value_t& i_Value)
    {
        if(i_Index < 0 || i_Index >= m_DataCount)
            return R_OUTOFRANGE;

        Int l_PageIndex = i_Index / PAGESIZE;
        Int l_SubIndex = i_Index % PAGESIZE;

        DOME_ASSERT(l_PageIndex < m_DataPageArray.size());
        DOME_ASSERT(m_DataPageArray[l_PageIndex]);

        (*m_DataPageArray[l_PageIndex])[l_SubIndex] = i_Value;
        return R_SUCCESS;
    }

    /*
        Set value by hash code
        return R_NOTREGISTERED if the key is not registered
        return R_SUCCESS when success
    */
    DResult set(Hash_t i_Hash, const Value_t& i_Value)
    {
        HashDataDict_t::iterator it = m_HashDataDict.find(i_Hash);
        if(it == m_HashDataDict.end())
            return R_NOTREGISTERED;
        *(it->second) = i_Value;
        return R_SUCCESS;
    }

    /*
        Set value by key value
        return R_NOTREGISTERED if the key is not registered
        return R_SUCCESS when success
    */
    DResult set(const Key_t& i_Key, const Value_t& i_Value)
    {
        Hash_t l_Hash(i_Key.c_str());
        return set(l_Hash, i_Value);
    }

    /*
        Get value by index
        return DM_NULL if the index is out of range
        return value's address when success
    */
    Value_t* get(Int i_Index)
    {
        if(i_Index < 0 || i_Index >= m_DataCount)
            return DM_NULL;

        Int l_PageIndex = i_Index / PAGESIZE;
        Int l_SubIndex = i_Index % PAGESIZE;

        DOME_ASSERT(l_PageIndex < m_DataPageArray.size());
        DOME_ASSERT(m_DataPageArray[l_PageIndex]);

        return &(*m_DataPageArray[l_PageIndex])[l_SubIndex];
    }

    const Value_t* get(Int i_Index) const
    {
        if(i_Index < 0 || i_Index >= m_DataCount)
            return DM_NULL;

        Int l_PageIndex = i_Index / PAGESIZE;
        Int l_SubIndex = i_Index % PAGESIZE;

        DOME_ASSERT(l_PageIndex < m_DataPageArray.size());
        DOME_ASSERT(m_DataPageArray[l_PageIndex]);

        return &(*m_DataPageArray[l_PageIndex])[l_SubIndex];
    }

    /*
        Get value by hash code
        return DM_NULL if the key is not registered
        return value's address when success
    */
    Value_t* get(Hash_t i_Hash)
    {
        HashDataDict_t::iterator it = m_HashDataDict.find(i_Hash);
        if(it == m_HashDataDict.end())
            return DM_NULL;
        return it->second;
    }

    const Value_t* get(Hash_t i_Hash) const
    {
        HashDataDict_t::const_iterator cit = m_HashDataDict.find(i_Hash);
        if(cit == m_HashDataDict.end())
            return DM_NULL;
        return cit->second;
    }

    /*
        Get value by key value
        return DM_NULL if the key is not registered
        return value's when success
    */
    Value_t* get(const Key_t& i_Key)
    {
        Hash_t l_Hash(i_Key.c_str());
        return get(l_Hash);
    }

    const Value_t* get(const Key_t& i_Key) const
    {
        Hash_t l_Hash(i_Key.c_str());
        return get(l_Hash);
    }

private:
    Int             m_DataCount;
    DataPageArray_t m_DataPageArray;
    HashDataDict_t  m_HashDataDict;
    HashKeyDict_t   m_HashKeyDict;
};


DOME_NAMESPACE_END