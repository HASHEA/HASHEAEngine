/*
    filename:       mdoperandarray.cpp
    author:         Ming Dong
    date:           2016-SEP-02
    description:    
*/

#include "../public/mdoperandarray.h"
#include "../public/rcglobal.h"

DOME_NAMESPACE_BEGIN

struct MDOperandArray_Data
{
    TArray<MDOperand*>      m_OperandArray;
};

MDOperandArray::MDOperandArray()
{
    me = DOME_New(MDOperandArray_Data);
}

MDOperandArray::~MDOperandArray()
{
    DOME_Del(me);
}

Bool                        MDOperandArray::isOperation() const
{
    return DM_FALSE;
}

Int                         MDOperandArray::getDataCount() const
{
    return me->m_OperandArray.size();
}

DSimpleTypeID               MDOperandArray::getDataType(Int i_Index) const
{
    DOME_ASSERT(i_Index >= 0 && i_Index < me->m_OperandArray.size());
    if (me->m_OperandArray[i_Index]->getDataCount() == 1)
        return me->m_OperandArray[i_Index]->getDataType(0);
    else
        return RCGlobal::k_SimpleTypeID_Unknown;
}

const MDOperand*            MDOperandArray::getSubOperand(Int i_Index) const
{
    DOME_ASSERT(i_Index >= 0 && i_Index < me->m_OperandArray.size());
    return me->m_OperandArray[i_Index];
}

MDOperand*                  MDOperandArray::getSubOperand(Int i_Index)
{
    DOME_ASSERT(i_Index >= 0 && i_Index < me->m_OperandArray.size());
    return me->m_OperandArray[i_Index];
}

const DSimpleTypedValue*    MDOperandArray::getDataPtr() const
{
    if (getDataCount() == 1)
        return me->m_OperandArray[0]->getDataPtr();
    else
        return DM_NULL;
}

DSimpleTypedValue*          MDOperandArray::getDataPtr()
{
    if (getDataCount() == 1)
        return me->m_OperandArray[0]->getDataPtr();
    else
        return DM_NULL;
}

RCGPUDATAFORMAT             MDOperandArray::getTexDataFmt(Int i_Index) const
{
    if (getDataCount() == 1)
        return me->m_OperandArray[i_Index]->getTexDataFmt(0);
    else
        return RGDF_UNKNOWN;
}

DVector2i                   MDOperandArray::getTexDataSize(Int i_Index) const
{
    if (getDataCount() == 1)
        return me->m_OperandArray[i_Index]->getTexDataSize(0);
    else
        return DVector2i(0);
}

void                        MDOperandArray::addOperand(MDOperand* i_pOperand)
{
    me->m_OperandArray.push_back(i_pOperand);
}

void                        MDOperandArray::clearOperand()
{
    me->m_OperandArray.clear();
}

DOME_NAMESPACE_END