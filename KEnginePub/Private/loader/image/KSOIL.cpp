/*
    Jonathan Dummer
    2007-07-26-10.36

    Simple OpenGL Image Library

    Public Domain
    using Sean Barret's stb_image as a base

    Thanks to:
    * Sean Barret - for the awesome stb_image
    * Dan Venkitachalam - for finding some non-compliant DDS files, and patching some explicit casts
    * everybody at gamedev.net
*/
#define SOIL_CHECK_FOR_GL_ERRORS 0
/*
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <wingdi.h>
    #include <GL/gl.h>
#elif defined(__APPLE__) || defined(__APPLE_CC__)
    #include <OpenGL/gl.h>
    #include <Carbon/Carbon.h>
    #define APIENTRY
#else
    #include <GL/gl.h>
    #include <GL/glx.h>
#endif
*/
//#include <OpenGLES/EAGL.h>

//#ifdef __APPLE__
//#include <OpenGLES/ES2/gl.h>
//#else
//#ifdef COCOS2DX
//#include <GL/glew.h>
//#else
//#include <GLES2/gl2.h>
//#endif
//#endif
#include "../../stdafx.h"
#include "KSOIL.h"
#include "stb_image_aug.h"
#include "image_helper.h"
#include "image_DXT.h"
#include "Engine/KGLog.h"
#include "image_jpg.h"
#include "zlib-1.2.8/zlib.h"
//#include "../interface/ITypes.h"

#include <stdlib.h>
#include <string.h>
#include "FreeImage/png.h"

#include "../../stdafx.h"
#include <assert.h>
#include "../KTexturePool.h"
/*  error reporting */
const char* result_string_pointer = "SOIL initialized";

/*  for loading cube maps   */
enum {
    SOIL_CAPABILITY_UNKNOWN = -1,
    SOIL_CAPABILITY_NONE = 0,
    SOIL_CAPABILITY_PRESENT = 1
};
static int has_cubemap_capability = SOIL_CAPABILITY_UNKNOWN;
#define SOIL_TEXTURE_WRAP_R                 0x8072
#define SOIL_CLAMP_TO_EDGE                  0x812F
#define SOIL_NORMAL_MAP                     0x8511
#define SOIL_REFLECTION_MAP                 0x8512
#define SOIL_TEXTURE_CUBE_MAP               0x8513
#define SOIL_TEXTURE_BINDING_CUBE_MAP       0x8514
#define SOIL_TEXTURE_CUBE_MAP_POSITIVE_X    0x8515
#define SOIL_TEXTURE_CUBE_MAP_NEGATIVE_X    0x8516
#define SOIL_TEXTURE_CUBE_MAP_POSITIVE_Y    0x8517
#define SOIL_TEXTURE_CUBE_MAP_NEGATIVE_Y    0x8518
#define SOIL_TEXTURE_CUBE_MAP_POSITIVE_Z    0x8519
#define SOIL_TEXTURE_CUBE_MAP_NEGATIVE_Z    0x851A
#define SOIL_PROXY_TEXTURE_CUBE_MAP         0x851B
#define SOIL_MAX_CUBE_MAP_TEXTURE_SIZE      0x851C
/*  for non-power-of-two texture    */
static int has_NPOT_capability = SOIL_CAPABILITY_UNKNOWN;
/*  for texture rectangles  */
static int has_tex_rectangle_capability = SOIL_CAPABILITY_UNKNOWN;
#define SOIL_TEXTURE_RECTANGLE_ARB              0x84F5
#define SOIL_MAX_RECTANGLE_TEXTURE_SIZE_ARB     0x84F8
/*  for using DXT compression   */
static int has_DXT_capability = SOIL_CAPABILITY_UNKNOWN;
int query_DXT_capability(void);
#define SOIL_RGB_S3TC_DXT1      0x83F0
#define SOIL_RGBA_S3TC_DXT1     0x83F1
#define SOIL_RGBA_S3TC_DXT3     0x83F2
#define SOIL_RGBA_S3TC_DXT5     0x83F3




//////////////////////////////////////////////////////////////
//ParsedMipMapBuf::ParsedMipMapBuf()
//{
//  pMipbuf = 0;
//  internal_texture_format = 0;
//  original_texture_format = 0;
//  opengl_texture_type = 0;
//  opengl_texture_target = 0;
//  nWidth = 0;
//  nHeight = 0;
//  nMipMapLevel = 0;
//}
//
//ParsedMipMapBuf::~ParsedMipMapBuf()
//{
//  if(pMipbuf)
//  {
//      free(pMipbuf);
//      pMipbuf = 0;
//  }
//}

KTextureParsedBuf::KTextureParsedBuf()
{
    nWidth = 0;
    nHeight = 0;
    bRepeat = TRUE;
    pWholeBuf = 0;
    internal_texture_format = 0;
    original_texture_format = 0;
    opengl_texture_type = 0;
    opengl_texture_target = 0;
    bMipMap = 0;
    bLowTexureQuality = FALSE;
    szFilename = NULL;
    nBufLen = 0;

    bPKM = 0;
    bKTX = 0;
    bPvr = 0;
    bCube = 0;
    bHardwareDDS = 0;
    bDDSBGRA8 = false;
    bDDS = false;
    //memset(pMipMapBufArray, 0, sizeof(ParsedMipMapBuf *) * MAX_MIPAMP_BUF_ARRAY_COUNT);
}

KTextureParsedBuf::~KTextureParsedBuf()
{
    if (pWholeBuf)
    {
        free(pWholeBuf);
        pWholeBuf = 0;
    }

}

///////////////////////////////////////////////////////////////

unsigned char*
SOIL_load_image
(
    const char* filename,
    int* width, int* height, int* channels,
    int force_channels
)
{
    unsigned char* result = stbi_load(filename,
        width, height, channels, force_channels);
    if (result == NULL)
    {
        result_string_pointer = stbi_failure_reason();
    }
    else
    {
        result_string_pointer = "Image loaded";
    }
    return result;
}


unsigned char* SOIL_load_image_forOGL(const char* filename, int* width, int* height, int* channels, int force_channels, bool bInvertY)
{
    //force rgba
    unsigned char* result = SOIL_load_image(filename, width, height, channels, SOIL_LOAD_RGBA);
    if (bInvertY && result)
    {
        int i, j;
        for (j = 0; j * 2 < (*height); ++j)
        {
            int index1 = j * (*width) * 4;
            int index2 = ((*height) - 1 - j) * (*width) * 4;
            for (i = (*width) * 4; i > 0; --i)
            {
                unsigned char temp = result[index1];
                result[index1] = result[index2];
                result[index2] = temp;
                ++index1;
                ++index2;
            }
        }
    }
    return result;
}

//void save_image_as_png(FILE *fp, const unsigned char *image, int width, int height)
BOOL  save_image_as_png(char const* filename, int width, int height, const unsigned char* const  image)
{
    //const unsigned char *rows[height];
    png_bytep* rows = new png_bytep[height];
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    int row = 0;
    BOOL bRet = FALSE;
    FILE* fp = fopen(filename, "wb");
    if (fp)
    {
        png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
        if (!png_ptr)
            goto out_free_write_struct;

        info_ptr = png_create_info_struct(png_ptr);
        if (!info_ptr)
            goto out_free_write_struct;

        png_init_io(png_ptr, fp);
        //png_set_compression_level(png_ptr, Z_BEST_COMPRESSION);
        png_set_IHDR(png_ptr, info_ptr, width, height,
            8, PNG_COLOR_TYPE_RGB_ALPHA, PNG_INTERLACE_NONE,
            PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

        for (row = 0; row < height; row++)
            rows[row] = (png_bytep)&image[row * width * 4];

        png_set_rows(png_ptr, info_ptr, (png_byte**)rows);
        png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);
        png_write_end(png_ptr, info_ptr);
        png_destroy_info_struct(png_ptr, &info_ptr);
        fclose(fp);
        bRet = TRUE;
    }
out_free_write_struct:
    png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
    delete[] rows;
    return bRet;
}




unsigned char*
SOIL_load_image_from_memory
(
    const unsigned char* const buffer,
    int buffer_length,
    int* width, int* height, int* channels,
    int force_channels
)
{
    unsigned char* result = stbi_load_from_memory(
        buffer, buffer_length,
        width, height, channels,
        force_channels);
    if (result == NULL)
    {
        result_string_pointer = stbi_failure_reason();
    }
    else
    {
        result_string_pointer = "Image loaded from memory";
    }
    return result;
}

int
SOIL_save_image
(
    const char* filename,
    int image_type,
    int width, int height, int channels,
    const unsigned char* const data
)
{
    int save_result;

    /*  error check */
    if ((width < 1) || (height < 1) ||
        (channels < 1) || (channels > 4) ||
        (data == NULL) ||
        (filename == NULL))
    {
        return 0;
    }
    if (image_type == SOIL_SAVE_TYPE_BMP)
    {
        save_result = stbi_write_bmp(filename,
            width, height, channels, (void*)data);
    }
    else
        if (image_type == SOIL_SAVE_TYPE_TGA)
        {
            save_result = stbi_write_tga(filename,
                width, height, channels, (void*)data);
        }
        else
            if (image_type == SOIL_SAVE_TYPE_DDS)
            {
                save_result = save_image_as_DDS(filename,
                    width, height, channels, (const unsigned char* const)data);
            }
            else
                if (image_type == SOIL_SAVE_TYPE_JPG)
                {
                    save_result = SaveJPGFile(filename, width, height, data);
                }
                else
                    if (image_type == SOIL_SAVE_TYPE_PNG)
                    {
                        save_result = save_image_as_png(filename, width, height, data);
                    }
                    else
                    {
                        save_result = 0;
                    }
    if (save_result == 0)
    {
        result_string_pointer = "Saving the image failed";
    }
    else
    {
        result_string_pointer = "Image saved";
    }
    return save_result;
}

void
SOIL_free_image_data
(
    unsigned char* img_data
)
{
    free((void*)img_data);
}

const char*
SOIL_last_result
(
    void
)
{
    return result_string_pointer;
}
