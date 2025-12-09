/*
    filename:       rcconfigure.h
    author:         Ming Dong
    date:           2016-MAR-22
    description:    
*/
#pragma once

#include "rcdefines.h"

// max texture resource can be used in one render pass
#define RC_RENDER_VS_MAXTEXTURE                     8
// max const parameters can be used in one render pass, in float4
#define RC_RENDER_VS_MAXCONSTBUFFER                 256

// max texture resource can be used in one render pass
#define RC_RENDER_PS_MAXTEXTURE                     8
// max const parameters can be used in one render pass, in float4
#define RC_RENDER_PS_MAXCONSTBUFFER                 256
// max render targets can be used in one render pass, in float4
#define RC_RENDER_MAXRENDERTARGET                   4

/*
    max OS Render Resource(OSRR) that can be created
*/
#define RC_OSRR_MAXTEXTURE                          1024
#define RC_OSRR_MAXVERTEXSHADER                     1024
#define RC_OSRR_MAXPIXELSHADER                      1024
#define RC_OSRR_MAXVERTEXBUFFER                     1024
#define RC_OSRR_MAXINDEXBUFFER                      1024
#define RC_OSRR_VERTEXLAYOUT                        1024
#define RC_OSRR_MAXRENDEROPERATION                  1024


#define RC_MAXEFFECTMANAGER                         64
