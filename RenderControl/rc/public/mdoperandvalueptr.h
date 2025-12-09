/*
    filename:       mdoperandvalueptr.h
    author:         Ming Dong
    date:           2016-APR-09
    description:    
*/
#pragma once

#include "mdoperand.h"

DOME_NAMESPACE_BEGIN

class RC_API MDOperandValuePtr : public MDOperand
{
public:
    MDOperandValuePtr(DSimpleTypedValue* i_pData);
    virtual ~MDOperandValuePtr();

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

    DResult                             set(DSimpleTypedValue* i_pData);

private:
    DSimpleTypedValue*                  m_pData;
};


DOME_NAMESPACE_END