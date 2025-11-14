/*
    filename:       mdoperandarray.h
    author:         Ming Dong
    date:           2016-SEP-02
    description:    
*/
#pragma once

#include "mdoperand.h"

DOME_NAMESPACE_BEGIN

struct MDOperandArray_Data;

class RC_API MDOperandArray : public MDOperand
{
public:
	MDOperandArray();
    virtual ~MDOperandArray();

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

    void                                addOperand(MDOperand* i_pOperand);
    void                                clearOperand();

private:
    MDOperandArray_Data*                me;
};


DOME_NAMESPACE_END