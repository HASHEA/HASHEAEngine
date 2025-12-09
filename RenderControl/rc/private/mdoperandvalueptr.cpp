/*
    filename:       mdoperandvalueptr.cpp
    author:         Ming Dong
    date:           2016-APR-09
    description:    
*/

#include "../public/mdoperandvalueptr.h"
#include "../public/rcglobal.h"

DOME_NAMESPACE_BEGIN

MDOperandValuePtr::MDOperandValuePtr(DSimpleTypedValue* i_pData)
{
    m_pData = i_pData;
}

MDOperandValuePtr::~MDOperandValuePtr()
{

}

Bool                        MDOperandValuePtr::isOperation() const
{
    return DM_FALSE;
}

Int                         MDOperandValuePtr::getDataCount() const
{
    return 1;
}

DSimpleTypeID               MDOperandValuePtr::getDataType(Int i_Index) const
{
    DOME_ASSERT(i_Index == 0);
    return m_pData->getTypeID();
}

const MDOperand*            MDOperandValuePtr::getSubOperand(Int i_Index) const
{
    return DM_NULL;
}

MDOperand*                  MDOperandValuePtr::getSubOperand(Int i_Index)
{
    return DM_NULL;
}

const DSimpleTypedValue*    MDOperandValuePtr::getDataPtr() const
{
    return m_pData;
}

DSimpleTypedValue*          MDOperandValuePtr::getDataPtr()
{
    return m_pData;
}

RCGPUDATAFORMAT             MDOperandValuePtr::getTexDataFmt(Int i_Index) const
{
    if (!m_pData) return RGDF_UNKNOWN;

    if (m_pData->getTypeID() == RCGlobal::k_SimpleTypeID_OSTexture2D)
    {
        OSTexture2D l_Tex = *(m_pData->getValuePtr<OSTexture2D>());
        if (l_Tex.isValid())
        {
            RCRenderer* l_pRenderer = l_Tex.getRenderer();
            return l_pRenderer->getTexture2DFormat(l_Tex);
        }
    }
    return RGDF_UNKNOWN;
}

DVector2i                   MDOperandValuePtr::getTexDataSize(Int i_Index) const
{
    if (m_pData->getTypeID() == RCGlobal::k_SimpleTypeID_OSTexture2D)
    {
        OSTexture2D l_Tex = *(m_pData->getValuePtr<OSTexture2D>());
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

DResult                     MDOperandValuePtr::set(DSimpleTypedValue* i_pData)
{
    m_pData = i_pData;
    return R_SUCCESS;
}

DOME_NAMESPACE_END