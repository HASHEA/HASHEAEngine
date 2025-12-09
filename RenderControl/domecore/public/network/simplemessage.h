/*
    filename:       simplemessage.h
    author:         Ming Dong
    date:           2016-Mar-28
    description:    
*/
#pragma once

#include "../typedefs.h"
#include "../typedvalue/simpletypedvalue.h"

DOME_NAMESPACE_BEGIN

class DOME_CORE_API DSimpleMessage
{
public:
    DSimpleMessage();
    DSimpleMessage(const DString& i_MessageName);
    DSimpleMessage(const DSimpleMessage& i_Message);
    ~DSimpleMessage();

    DSimpleMessage& operator= (const DSimpleMessage& i_Message);

    DResult     reset();

    const DString&              getMessageName() const;

    Int                         getParameterCount() const;
    DSimpleTypedValue*          getParameter(Int i_Index);
    const DSimpleTypedValue*    getParameter(Int i_Index) const;
    DSimpleTypedValue*          getParameter(const DHashString& i_ParamName);
    const DSimpleTypedValue*    getParameter(const DHashString& i_ParamName) const;

    DResult     addParameter(const DHashString& i_ParamName, const DSimpleTypedValue& i_Value);
    DResult     setParameter(const DHashString& i_ParamName, const DSimpleTypedValue& i_Value);
    DResult     removeParameter(const DHashString& i_ParamName);

    Int         getMessageBufferSize() const;
    DResult     serialize(Int i_BufferSize, U8* o_pBuffer, Int& o_BufferWrite) const;
    DResult     deserialize(Int i_BufferSize, const U8* i_pBuffer, Int& o_BufferRead);

private:
    class DSimpleMessage_Impl;
    DSimpleMessage_Impl*        me;
};


DOME_NAMESPACE_END