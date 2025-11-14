/*
    filename:       mdoperandvalue.cpp
    author:         Ming Dong
    date:           2016-APR-09
    description:    
*/

#include "../public/mdoperandvalue.h"
#include "../public/rcglobal.h"

DOME_NAMESPACE_BEGIN

MDOperandValue::MDOperandValue(const DSimpleTypedValue& i_Data)
    :m_Data(i_Data)
{

}

MDOperandValue::MDOperandValue(DSimpleTypeID i_TypeID)
    : m_Data(i_TypeID)
{

}

MDOperandValue::MDOperandValue(const ISimpleType* i_pType)
    : m_Data(i_pType)
{

}

MDOperandValue::~MDOperandValue()
{

}

Bool                        MDOperandValue::isOperation() const
{
    return DM_FALSE;
}

Int                         MDOperandValue::getDataCount() const
{
    return 1;
}

DSimpleTypeID               MDOperandValue::getDataType(Int i_Index) const
{
    DOME_ASSERT(i_Index == 0);
    return m_Data.getTypeID();
}

const MDOperand*            MDOperandValue::getSubOperand(Int i_Index) const
{
    return DM_NULL;
}

MDOperand*                  MDOperandValue::getSubOperand(Int i_Index)
{
    return DM_NULL;
}

const DSimpleTypedValue*    MDOperandValue::getDataPtr() const
{
    return &m_Data;
}

DSimpleTypedValue*          MDOperandValue::getDataPtr()
{
    return &m_Data;
}

RCGPUDATAFORMAT             MDOperandValue::getTexDataFmt(Int i_Index) const
{
    if (m_Data.getTypeID() == RCGlobal::k_SimpleTypeID_OSTexture2D)
    {
        OSTexture2D l_Tex = *m_Data.getValuePtr<OSTexture2D>();
        if (l_Tex.isValid())
        {
            RCRenderer* l_pRenderer = l_Tex.getRenderer();
            return l_pRenderer->getTexture2DFormat(l_Tex);
        }
    }
    return RGDF_UNKNOWN;
}

DVector2i                   MDOperandValue::getTexDataSize(Int i_Index) const
{
    if (m_Data.getTypeID() == RCGlobal::k_SimpleTypeID_OSTexture2D)
    {
        OSTexture2D l_Tex = *m_Data.getValuePtr<OSTexture2D>();
        if (l_Tex.isValid())
        {
            RCRenderer* l_pRenderer = l_Tex.getRenderer();
            DVector2i l_Result;
            l_pRenderer->getTexture2DSize(l_Tex, l_Result.x, l_Result.y);
            return l_Result;
        }
    }
    return DVector2i(0);
}


DOME_NAMESPACE_END