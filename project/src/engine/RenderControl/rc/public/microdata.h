/*
    filename:       microdata.h
    author:         Ming Dong
    date:           2016-APR-07
    description:    
*/
#pragma once

#include "rctypedefs.h"

DOME_NAMESPACE_BEGIN


class RC_API MicroData
{
public:
    enum MDT : S32          // Micro Data Type
    {
        MDT_UNKNOWN,
        MDT_OPERAND,
        MDT_OPERATOR,
//        MDT_OPERATION,
//        MDT_METAOP,
//        MDT_METAINFO
    };

    enum IRP : S32          // Input Release Point
    {
        IRP_NEVER,
        IRP_AFTEREXECUTE,
        IRP_INFINISHCALLBACK
    };


    MicroData(S32 i_Type);
    virtual ~MicroData();

    S32     getType() const;
    Bool    isOperand() const;
    Bool    isOperator() const;


private:
    S32             m_Type;
};


DOME_NAMESPACE_END