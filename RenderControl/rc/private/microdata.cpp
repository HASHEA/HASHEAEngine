/*
    filename:       microdata.h
    author:         Ming Dong
    date:           2016-APR-07
    description:    
*/

#include "../public/microdata.h"

DOME_NAMESPACE_BEGIN

MicroData::MicroData(S32 i_Type)
    :m_Type(i_Type)
{

}

MicroData::~MicroData()
{

}

S32     MicroData::getType() const
{
    return m_Type;
}

Bool    MicroData::isOperand() const
{
    return getType() == MDT_OPERAND;
}

Bool    MicroData::isOperator() const
{
    return getType() == MDT_OPERATOR;
}

//Bool    MicroData::isMetaOP() const
//{
//    return getType() == MDT_METAOP;
//}

//Bool    MicroData::isMetaInfo() const
//{
//    return getType() == MDT_METAINFO;
//}



DOME_NAMESPACE_END