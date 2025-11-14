/*
    filename:       simplemessage.cpp
    author:         Ming Dong
    date:           2016-Mar-28
    description:    
*/

#include "../../public/network/simplemessage.h"

DOME_NAMESPACE_BEGIN

class DSimpleMessage::DSimpleMessage_Impl
{
public:
    DSimpleMessage_Impl()
    {

    }

    DSimpleMessage_Impl(const DString& i_MessageName)
        : m_MessageName(i_MessageName)
    {

    }

    ~DSimpleMessage_Impl()
    {

    }

    DResult reset()
    {
        m_MessageName.clear();
        m_ParamMap.clear();
        m_ParamNameArray.clear();
        m_ParamArray.clear();

        return R_SUCCESS;
    }

    DResult copyFrom(const DSimpleMessage_Impl& i_Other)
    {
        reset();

        m_MessageName = i_Other.m_MessageName;

        for (Int i = 0; i < i_Other.m_ParamNameArray.size(); ++i)
        {
            const DHashString& l_ParamName = *i_Other.m_ParamNameArray[i];
            const DSimpleTypedValue& l_ParamValue = *i_Other.m_ParamArray[i];

            _ParamMap::iterator it = m_ParamMap.get(l_ParamName);
            it->second = l_ParamValue;
            m_ParamNameArray.push_back(&(it->first));
            m_ParamArray.push_back(&(it->second));
        }
        return R_SUCCESS;
    }

    const DString&              getMessageName() const
    {
        return m_MessageName;
    }

    Int         getParameterCount() const
    {
        return m_ParamArray.size();
    }

    DSimpleTypedValue*          getParameter(Int i_Index)
    {
        DOME_ASSERT(i_Index > 0 && i_Index < getParameterCount());
        return m_ParamArray[i_Index];
    }

    const DSimpleTypedValue*    getParameter(Int i_Index) const
    {
        DOME_ASSERT(i_Index > 0 && i_Index < getParameterCount());
        return m_ParamArray[i_Index];
    }

    DSimpleTypedValue*          getParameter(const DHashString& i_ParamName)
    {
        _ParamMap::iterator it = m_ParamMap.find(i_ParamName);
        if(it == m_ParamMap.end())
            return DM_NULL;
        else
            return &it->second;
    }

    const DSimpleTypedValue*    getParameter(const DHashString& i_ParamName) const
    {
        _ParamMap::const_iterator cit = m_ParamMap.find(i_ParamName);
        if(cit == m_ParamMap.end())
            return DM_NULL;
        else
            return &cit->second;
    }

    DResult     addParameter(const DHashString& i_ParamName, const DSimpleTypedValue& i_Value)
    {
        _ParamMap::const_iterator cit = m_ParamMap.find(i_ParamName);
        if(cit != m_ParamMap.end())
            return R_ALREADYADDED;

        _ParamMap::iterator it = m_ParamMap.get(i_ParamName);
        it->second = i_Value;
        m_ParamNameArray.push_back(&(it->first));
        m_ParamArray.push_back(&(it->second));
        return R_SUCCESS;
    }

    DResult     setParameter(const DHashString& i_ParamName, const DSimpleTypedValue& i_Value)
    {
        _ParamMap::const_iterator cit = m_ParamMap.find(i_ParamName);
        if(cit == m_ParamMap.end())
            return R_NOTFOUND;

        _ParamMap::iterator it = m_ParamMap.get(i_ParamName);
        it->second = i_Value;
        m_ParamNameArray.push_back(&(it->first));
        m_ParamArray.push_back(&(it->second));
        return R_SUCCESS;
    }

    DResult     removeParameter(const DHashString& i_ParamName)
    {
        _ParamMap::iterator it = m_ParamMap.find(i_ParamName);
        if(it == m_ParamMap.end())
            return R_NOTFOUND;

        DSimpleTypedValue* l_pValue = &it->second;
        for (Int i = 0; i < m_ParamArray.size(); ++i)
        {
            if (l_pValue == m_ParamArray[i])
            {
                m_ParamNameArray.remove(i);
                m_ParamArray.remove(i);
                m_ParamMap.erase(it);
                return R_SUCCESS;
            }
        }
        return R_FAILED;
    }

    Int         getMessageBufferSize() const
    {
        Int l_MessageSize = 0;

        // count message name size
        l_MessageSize += sizeof(S32);
        l_MessageSize += m_MessageName.getMemSize();

        for (Int i = 0; i < m_ParamArray.size(); ++i)
        {
            const DHashString& l_ParamName = *m_ParamNameArray[i];
            const DSimpleTypedValue& l_Value = *m_ParamArray[i];
            if(!l_Value.isSerializable())
                return -1;

            l_MessageSize += sizeof(S32);
            l_MessageSize += l_ParamName.getMemSize();

            l_MessageSize += sizeof(DSimpleTypeID);
            Int l_ParamSize = 0;
            DOME_ASSERT(l_Value.serialize(0, DM_NULL, l_ParamSize) == R_BUFFERSIZENOTENOUGH);
            l_MessageSize += l_ParamSize;
        }
        return l_MessageSize;
    }

    DResult     serialize(Int i_BufferSize, U8* o_pBuffer, Int& o_BufferWrite) const
    {
        Int l_BuffSize = getMessageBufferSize();
        if(l_BuffSize < 0)
            return R_FAILED;
        if ((l_BuffSize > i_BufferSize) || (!o_pBuffer))
        {
            o_BufferWrite = l_BuffSize;
            return R_BUFFERSIZENOTENOUGH;
        }

        o_BufferWrite = l_BuffSize;

        Int l_BuffWrite = 0;
        U8* l_ptr = o_pBuffer;

        // serialize message name
        DSimpleTypedValue l_MessageName(m_MessageName);
        l_MessageName.serialize(i_BufferSize, l_ptr, l_BuffWrite);
        l_ptr += l_BuffWrite;
        i_BufferSize -= l_BuffWrite;

        for (Int i = 0; i < m_ParamArray.size(); ++i)
        {
            // serialize parameter name
            DSimpleTypedValue l_ParamName(*m_ParamNameArray[i]);
            l_ParamName.serialize(i_BufferSize, l_ptr, l_BuffWrite);
            l_ptr += l_BuffWrite;
            i_BufferSize -= l_BuffWrite;

            // serialize parameter type
            const U8* l_psrc;
            DSimpleTypeID l_TypeID = m_ParamArray[i]->getTypeID();
            l_psrc = (const U8*)&l_TypeID;
            for(int n = 0; n < sizeof(DSimpleTypeID); ++n)
                l_ptr[n] = l_psrc[n];
            l_ptr += sizeof(DSimpleTypeID);
            i_BufferSize -= sizeof(DSimpleTypeID);

            m_ParamArray[i]->serialize(i_BufferSize, l_ptr, l_BuffWrite);
            i_BufferSize -= l_BuffWrite;
            l_ptr += l_BuffWrite;
        }

        return R_SUCCESS;
    }

    DResult     deserialize(Int i_BufferSize, const U8* i_pBuffer, Int& o_BufferRead)
    {
        U8* l_ptr;
        const U8* l_psrc = i_pBuffer;
        Int l_BuffRead = 0;

        reset();

        // deserialize message name
        DSimpleTypedValue l_MessageName(DSimpleTypeID(DStringHash("DString")));
        if (DM_FAIL(l_MessageName.deserialize(i_BufferSize, l_psrc, l_BuffRead)))
        {
            return R_FAILED;
        }
        m_MessageName = l_MessageName.getDString();
        l_psrc += l_BuffRead;
        i_BufferSize -= l_BuffRead;
        o_BufferRead += l_BuffRead;

        if (i_BufferSize == 0)
        {
            return R_SUCCESS;
        }

        while (DM_TRUE)
        {
            // deserialize parameter name
            DSimpleTypedValue l_ParamName(DSimpleTypeID(DStringHash("DHashString")));
            if (DM_FAIL(l_ParamName.deserialize(i_BufferSize, l_psrc, l_BuffRead)))
            {
                return R_FAILED;
            }
            l_psrc += l_BuffRead;
            i_BufferSize -= l_BuffRead;
            o_BufferRead += l_BuffRead;

            // deserialize parameter type
            if(i_BufferSize < sizeof(DSimpleTypeID))
                return R_FAILED;
            DSimpleTypeID l_TypeID;
            l_ptr = (U8*)&l_TypeID;
            for (Int i = 0; i < sizeof(DSimpleTypeID); ++i)
            {
                l_ptr[i] = l_psrc[i];
            }
            l_psrc += sizeof(DSimpleTypeID);;
            i_BufferSize -= sizeof(DSimpleTypeID);
            o_BufferRead += sizeof(DSimpleTypeID);

            // deserialize parameter value
            DSimpleTypedValue l_ParamValue(l_TypeID);
            if (DM_FAIL(l_ParamValue.deserialize(i_BufferSize, l_psrc, l_BuffRead)))
            {
                return R_FAILED;
            }
            l_psrc += l_BuffRead;
            i_BufferSize -= l_BuffRead;
            o_BufferRead += l_BuffRead;

            addParameter(l_ParamName.getDHashString(), l_ParamValue);

            if (i_BufferSize == 0)
            {
                break;
            }
        }

        return R_SUCCESS;
    }


private:
    typedef TMap<DHashString, DSimpleTypedValue>        _ParamMap;
    typedef TArray<const DHashString*>                  _ParamNameArray;
    typedef TArray<DSimpleTypedValue*>                  _ParamArray;
    DString                                             m_MessageName;
    _ParamMap                                           m_ParamMap;
    _ParamNameArray                                     m_ParamNameArray;
    _ParamArray                                         m_ParamArray;
};


DSimpleMessage::DSimpleMessage()
{
    me = DOME_New(DSimpleMessage_Impl);
}

DSimpleMessage::DSimpleMessage(const DString& i_MessageName)
{
    me = DOME_New(DSimpleMessage_Impl)(i_MessageName);
}

DSimpleMessage::DSimpleMessage(const DSimpleMessage& i_Message)
{
    me = DOME_New(DSimpleMessage_Impl);
    me->copyFrom(*i_Message.me);
}

DSimpleMessage::~DSimpleMessage()
{
    DOME_Del(me);
}

DSimpleMessage& DSimpleMessage::operator= (const DSimpleMessage& i_Message)
{
    me->copyFrom(*i_Message.me);
    return *this;
}

DResult     DSimpleMessage::reset()
{
    return me->reset();
}

const DString&              DSimpleMessage::getMessageName() const
{
    return me->getMessageName();
}

Int                         DSimpleMessage::getParameterCount() const
{
    return me->getParameterCount();
}

DSimpleTypedValue*          DSimpleMessage::getParameter(Int i_Index)
{
    return me->getParameter(i_Index);
}

const DSimpleTypedValue*    DSimpleMessage::getParameter(Int i_Index) const
{
    return me->getParameter(i_Index);
}

DSimpleTypedValue*          DSimpleMessage::getParameter(const DHashString& i_ParamName)
{
    return me->getParameter(i_ParamName);
}

const DSimpleTypedValue*    DSimpleMessage::getParameter(const DHashString& i_ParamName) const
{
    return me->getParameter(i_ParamName);
}

DResult     DSimpleMessage::addParameter(const DHashString& i_ParamName, const DSimpleTypedValue& i_Value)
{
    return me->addParameter(i_ParamName, i_Value);
}

DResult     DSimpleMessage::setParameter(const DHashString& i_ParamName, const DSimpleTypedValue& i_Value)
{
    return me->setParameter(i_ParamName, i_Value);
}

DResult     DSimpleMessage::removeParameter(const DHashString& i_ParamName)
{
    return me->removeParameter(i_ParamName);
}

Int         DSimpleMessage::getMessageBufferSize() const
{
    return me->getMessageBufferSize();
}

DResult     DSimpleMessage::serialize(Int i_BufferSize, U8* o_pBuffer, Int& o_BufferWrite) const
{
    return me->serialize(i_BufferSize, o_pBuffer, o_BufferWrite);
}

DResult     DSimpleMessage::deserialize(Int i_BufferSize, const U8* i_pBuffer, Int& o_BufferRead)
{
    return me->deserialize(i_BufferSize, i_pBuffer, o_BufferRead);
}



DOME_NAMESPACE_END