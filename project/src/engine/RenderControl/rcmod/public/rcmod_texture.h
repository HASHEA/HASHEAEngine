/*
    filename:       rcmod_texture.h
    author:         Ming Dong
    date:           2016-SEP-06
    description:    
*/
#pragma once

#include "rcmod_def.h"

struct RCMOD_Texture_Data;
class RCMOD_API RCMOD_Texture
{
public:
    RCMOD_Texture();
    RCMOD_Texture(const RCMOD_Texture& i_Value);
    ~RCMOD_Texture();

    const RCMOD_Texture& operator=(const RCMOD_Texture& i_Value);

    const void* getPtr() const;
    void* getPtr();

private:
    RCMOD_Texture_Data*      me;
};