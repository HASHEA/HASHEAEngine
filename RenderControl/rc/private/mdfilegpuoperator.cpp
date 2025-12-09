/*
    filename:       mdfilegpuoperator.h
    author:         Ming Dong
    date:           2016-06-18
    description:    
*/

#include "../public/mdfilegpuoperator.h"

RC_NAMESPACE_BEGIN

MDFileGpuOperator::MDFileGpuOperator(const DString& i_FileName)
{
    DOME_ASSERT(i_FileName.size() < 200);
    OS_String::TStrCopy(m_FileName, i_FileName.c_str());

    _load(i_FileName);
}

void MDFileGpuOperator::reload()
{
    reset();
    _load(m_FileName);
}


void MDFileGpuOperator::_load(const DString& i_FileName)
{
    DFile l_MdoFile(i_FileName, DOME_GetExternalFS());
    Char* l_pFileBuffer = DM_NULL;
    if (DM_SUCC(l_MdoFile.open(DM_FALSE)))
    {
        Int l_FileSize = l_MdoFile.getLength();
        l_pFileBuffer = (Char*)DOME_Alloc(l_FileSize + 1);

        l_MdoFile.read(l_pFileBuffer, l_FileSize);
        DOME_ASSERT(l_FileSize == l_MdoFile.getLength());
        l_MdoFile.close();

        l_pFileBuffer[l_FileSize] = 0;
    }
    else
    {
        DOME_ASSERT(0);
    }

    rapidxml::xml_document<> l_XmlDoc;
    rapidxml::xml_node<>* l_pXmlMDObject = DM_NULL;
    rapidxml::xml_node<>* l_pXmlChild = DM_NULL;
    rapidxml::xml_attribute<>* l_pXmlAttrib = DM_NULL;
    l_XmlDoc.parse<0>(l_pFileBuffer);
    l_pXmlMDObject = l_XmlDoc.first_node("MDObject");
    DOME_ASSERT(l_pXmlMDObject);

    l_pXmlAttrib = l_pXmlMDObject->first_attribute("name");
    DOME_ASSERT(l_pXmlAttrib);
    setOperatorName(l_pXmlAttrib->value());

    l_pXmlAttrib = l_pXmlMDObject->first_attribute("mustbemerged");
    if (l_pXmlAttrib)
    {
        if (DString("true") == l_pXmlAttrib->value())
            setMustBeMerged(DM_TRUE);
        else
            setMustBeMerged(DM_FALSE);
    }

    l_pXmlAttrib = l_pXmlMDObject->first_attribute("complexity");
    if(l_pXmlAttrib)
        setOperatorComplexity(DString(l_pXmlAttrib->value()).toInt());
    else
        setOperatorComplexity(5);

    for (l_pXmlChild = l_pXmlMDObject->first_node("Input"); l_pXmlChild != DM_NULL; l_pXmlChild = l_pXmlChild->next_sibling("Input"))
    {
        DHashString l_InputName;
        DHashString l_InputType;
        DString l_StrCanBeMerged;
        Bool l_bCanBeMerged;

        l_pXmlAttrib = l_pXmlChild->first_attribute("name");
        DOME_ASSERT(l_pXmlAttrib);
        l_InputName = l_pXmlAttrib->value();

        l_pXmlAttrib = l_pXmlChild->first_attribute("type");
        DOME_ASSERT(l_pXmlAttrib);
        l_InputType = l_pXmlAttrib->value();

        l_pXmlAttrib = l_pXmlChild->first_attribute("canbemerged");
        DOME_ASSERT(l_pXmlAttrib);
        l_StrCanBeMerged = l_pXmlAttrib->value();
        if(l_StrCanBeMerged == "false")
            l_bCanBeMerged = DM_FALSE;
        else
            l_bCanBeMerged = DM_TRUE;

        addInput(l_InputName, l_InputType, l_bCanBeMerged);
    }

    {
        // read and set the output texture format
        l_pXmlChild = l_pXmlMDObject->first_node("Output");
        DOME_ASSERT(l_pXmlChild);
        DString l_OutputFormat;
        RCGPUDATAFORMAT l_Format;
        l_pXmlAttrib = l_pXmlChild->first_attribute("format");
        DOME_ASSERT(l_pXmlAttrib);
        l_OutputFormat = l_pXmlAttrib->value();
        if(l_OutputFormat == "RGDF_RGBA8")
            l_Format = RGDF_RGBA8;
        else if(l_OutputFormat == "RGDF_RGBA16F")
            l_Format = RGDF_RGBA16F;
        else if (l_OutputFormat == "RGDF_RG32F")
            l_Format = RGDF_RG32F;
        else
            l_Format = RGDF_UNKNOWN;

        l_pXmlAttrib = l_pXmlChild->first_attribute("fmtdecider");
        Int l_FormatDecider = -1;
        if(l_pXmlAttrib)
            l_FormatDecider = DString(l_pXmlAttrib->value()).toInt();

        setOutputTextureFmt(l_Format, l_FormatDecider);

        // read and set the output texture size information
        l_pXmlAttrib = l_pXmlChild->first_attribute("sizedecider");
        Int l_SizeDecider = -1;
        if(l_pXmlAttrib)
            l_SizeDecider = DString(l_pXmlAttrib->value()).toInt();

        Int l_SizeMultiplierX = 1;
        Int l_SizeMultiplierY = 1;
        Int l_SizeDividerX = 1;
        Int l_SizeDividerY = 1;
        Int l_SizeAdderX = 0;
        Int l_SizeAdderY = 0;

        l_pXmlAttrib = l_pXmlChild->first_attribute("sizeinfo");
        if (l_pXmlAttrib)
        {
            OS_String::StrScan((const Char*)l_pXmlAttrib->value(), "%lld,%lld,%lld,%lld,%lld,%lld", &l_SizeMultiplierX, &l_SizeMultiplierY, &l_SizeDividerX, &l_SizeDividerY, &l_SizeAdderX, &l_SizeAdderY);
        }
        setResultSizeInfo(l_SizeDecider, DVector2i(l_SizeMultiplierX, l_SizeMultiplierY), DVector2i(l_SizeDividerX, l_SizeDividerY), DVector2i(l_SizeAdderX, l_SizeAdderY));
    }

    l_pXmlChild = l_pXmlMDObject->first_node("ShaderCode");
    DOME_ASSERT(l_pXmlChild);
    setOperatorShaderCode(l_pXmlChild->first_node()->value());

    DOME_ASSERT(l_pFileBuffer);
    DOME_Free(l_pFileBuffer);
}

RC_NAMESPACE_END