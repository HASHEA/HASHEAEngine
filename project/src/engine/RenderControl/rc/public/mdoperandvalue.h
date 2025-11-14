/*
    filename:       mdoperandvalue.h
    author:         Ming Dong
    date:           2016-APR-09
    description:    
*/
#pragma once

#include "mdoperand.h"

DOME_NAMESPACE_BEGIN

class RC_API MDOperandValue : public MDOperand
{
public:
    MDOperandValue(const DSimpleTypedValue& i_Data);
    MDOperandValue(DSimpleTypeID i_TypeID);
    MDOperandValue(const ISimpleType* i_pType);
    virtual ~MDOperandValue();

    // functions need to be implemented in child class
    virtual Bool                        isOperation() const DOME_OVERRIDE;
    virtual Int                         getDataCount() const DOME_OVERRIDE;
    virtual DSimpleTypeID               getDataType(Int i_Index) const DOME_OVERRIDE;
    // if getDataCount() > 1, call the following functions to get data
    virtual const MDOperand*            getSubOperand(Int i_Index) const DOME_OVERRIDE;
    virtual MDOperand*                  getSubOperand(Int i_Index) DOME_OVERRIDE;
    // if getDataCount() == 1, call the following functions to get data
    virtual const DSimpleTypedValue*    getDataPtr() const DOME_OVERRIDE;
    virtual DSimpleTypedValue*          getDataPtr() DOME_OVERRIDE;
    // special functions for texture data
    virtual RCGPUDATAFORMAT             getTexDataFmt(Int i_Index) const DOME_OVERRIDE;
    virtual DVector2i                   getTexDataSize(Int i_Index) const DOME_OVERRIDE;

private:
    DSimpleTypedValue                   m_Data;
};


DOME_NAMESPACE_END