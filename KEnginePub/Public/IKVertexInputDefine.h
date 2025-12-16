#pragma once
#include "KBase/Public/math/KVector.h"
#include "KBase/Public/math/KVector4.h"
#include "KBase/Public/math/KVectorIntX.h"
#include "KBase/Public/math/KMatrix.h"

namespace gfx
{


    enum enumVertexDecl : uint8_t
    {
        P_VERT_DECL,
        P4_VERT_DECL,
        POINT_CLOUD_DECL,
        PT_VERT_DECL,
        TERRAIN_VERT_DECL,
        LANDSCAPE_VERT_DECL,
        LANDSCAPE_INSTANCE0_VERT_DECL,
        T1_VERT_DECL,
        T2_VERT_DECL,
        T3_VERT_DECL,
        T2_UVW_VERT_DECL,
        P2T_VERT_DECL,
        P2TC_VERT_DECL,
        PT3_VERT_DECL,
        PNT_VERT_DECL,
        PN2T_VERT_DECL,
        PC_VERT_DECL,
        PNCGT_VERT_DECL,
        PNCG2T_VERT_DECL,
        MAT44_INSTANCE_VERT_DECL,
        MAT34_INSTANCE_VERT_DECL,
        BONE_WEIGHT_DECL,
        PCT_VERT_DECL,
        SPEEDTREE_HD_VERT_DECL,
        SPEEDTREE_HD_BILLBOARD_VERT_DECL,
        SPEEDTREE_HD_INSANCE_VERT_DECL,
        VEC4_INSTANCE_VERT_DECL,
        PSS_VERT_DECL,
        PSSMESH_VERT_DECL,
        AABBBOX_OCCLUSION_CULL_INSTANCE_VERT_DECL,
        PSS_INSTANCE_VERT_DECL,
        SFX_BILLBOARD_VERT_DECL,
        SFX_PARTICLE_DECL,
        WATER_DECL,
        WATER_INSTANCE_DECL,
        DYNAMIC_VERT_DECL,              //动态顶点绑定，自定义添加任何格式
        VERT_DECL_COUNT
    };

    struct KAttribUsage
    {
        /// Corresponds to vertex shader attribute.
        enum Enum : uint8_t
        {
            VERT_POS_INDX = 0,
            VERT_NORMAL_INDX = 1,
            VERT_COLOR_INDX = 2,
            VERT_TANGENT_INDX = 3,
            VERT_TEX0_INDX = 4,
            VERT_TEX1_INDX = 5,
            VERT_TEX2_INDX = 6,
            VERT_TEX3_INDX = 7,
            VERT_TEX4_INDX = 8,
            VERT_TEX5_INDX = 9,

            ////// 这些枚举是instance数据使用的，非instance数据不可使用
            VERT_MATRIX_ROW1_INDX_INSTANCE = 10,
            VERT_MATRIX_ROW2_INDX_INSTANCE = 11,
            VERT_MATRIX_ROW3_INDX_INSTANCE = 12,
            VERT_MATRIX_ROW4_INDX_INSTANCE = 13,
            VERT_MATRIX_ROW5_INDX_INSTANCE = 14,
            VERT_POINT_LIGHT_INDX_INSTANCE = 15,
            ///////////////////////

            VERT_TEX6_INDX = 16,
            VERT_TEX7_INDX = 17,
            VERT_TEX8_INDX = 18,
            VERT_TEX9_INDX = 19,
            VERT_TEX10_INDX = 20,
            VERT_TEX11_INDX = 21,
            COUNT,

            /// 这些都是别名
            VERT_BLEND_INDX0 = VERT_TEX4_INDX,
            VERT_BLEND_INDX1 = VERT_TEX5_INDX,
            VERT_INSTANCE_MAX_BOUND = VERT_POINT_LIGHT_INDX_INSTANCE + 1,
        };

        enum BINDINGS : uint8_t
        {
            BIND_UBO_UNIMATRIXPARAM_VS,
            BIND_UBO_UNIMATRIXPARAM_FS,
            BIND_UBO_FOGPARAM,
            BIND_UBO_SUNLIGHTPARAM,
            BIND_UBO_COMMONPARAM,
            BIND_TEX_0,
            BIND_TEX_1,
            BIND_TEX_2,
            BIND_TEX_ENVPROBE,
            BIND_TEX_ENVPROBESH,
            BIND_TEX_ENVPREINTEGRATEDGF,
            BIND_COUNT
        };
    };

    struct KBaseAttribType
    {
        /// Attribute types:
        enum Enum : uint8_t
        {
            Uint8,  //!< Uint8
            Uint16, //!< Uint16
            Uint32,
            Half,   //!< Half, availability depends on: `CAPS_VERTEX_ATTRIB_HALF`.
            Float,  //!< Float
            Count
        };
    };

    enum KVertType
    {
        COMM_VERT,
        SKIN_WEIGHT_VERT,
        INSTANCE_VERT
    };

    class KVertexDecl
    {
    public:
        KVertexDecl(enumVertexDecl declType);
        KVertexDecl& Add(KAttribUsage::Enum attribUsage, unsigned baseAttribCount, KBaseAttribType::Enum baseAttribType, KVertType eVertType, BOOL bAsInt = false);
        // 直接指定偏移量，通过baseAttribType 和 baseAttribCount 计算
        KVertexDecl& Add(KAttribUsage::Enum attribUsage, unsigned offset, unsigned baseAttribCount, KBaseAttribType::Enum baseAttribType, KVertType eVertType, BOOL bAsInt = false);

        struct _ATT
        {
            KBaseAttribType::Enum m_baseAttribType;
            uint8_t               m_baseAttribCount;
            uint16_t              m_offset;
            BOOL                  m_bAsInt;
            const char* m_szLocationName;
            _ATT()
            {
                m_bAsInt = false;
                m_baseAttribType = KBaseAttribType::Count;
                m_baseAttribCount = 0;
                m_offset = 0;
                m_szLocationName = nullptr;
            }
        } m_Attr[KAttribUsage::COUNT];

        enumVertexDecl     m_declType;
        uint16_t           m_stride;
        uint16_t           m_nItem;
        KAttribUsage::Enum m_ItemTable[KAttribUsage::COUNT];
    };

    struct P_VERT
    {
        NSKMath::KVec3     pos;
        static KVertexDecl decl;
    };

    struct P4_VERT
    {
        NSKMath::KVec4     pos;
        static KVertexDecl decl;
    };

    struct POINT_CLOUD_VERT
    {
        uint16_t           x, y, z, w;
        static KVertexDecl decl;
    };

    struct PT_VERT
    {
        NSKMath::KVec3     pos;
        NSKMath::KVec2     tex;
        static KVertexDecl decl;
    };

    struct TERRAIN_VERT
    {
#if TERRAIN_POS_DEBUG
        NSKMath::KVec3 pos;
#endif
        uint32_t           normal;
        float              hight;
        static KVertexDecl decl;
    };

    struct LANDSCAPE_VERT
    {
        NSKMath::KVec2       pos;
        NSKMath::KVectorInt4 NodeOffset;
        NSKMath::KVec4       NeighborLOD;
        static KVertexDecl   decl;
    };

    struct LANDSCAPE_INSTANCE0_VERT
    {
        NSKMath::KVectorInt4 NodeOffset;
        NSKMath::KVec4       NeighborLOD;
        static KVertexDecl   decl;
    };

    struct T1_VERT
    {
        NSKMath::KVec2     tex;
        static KVertexDecl decl;
    };

    struct T2_VERT
    {
        NSKMath::KVec2     tex;
        static KVertexDecl decl;
    };

    struct T3_VERT
    {
        NSKMath::KVec2     tex;
        static KVertexDecl decl;
    };

    struct T2_UVW_VERT
    {
        NSKMath::KVec3     tex;
        static KVertexDecl decl;
    };

    struct P2T_VERT
    {
        NSKMath::KVec3     pos;
        NSKMath::KVec2     tex0;
        NSKMath::KVec2     tex1;
        static KVertexDecl decl;
    };

    struct P2TC_VERT
    {
        NSKMath::KVec3     pos;
        NSKMath::KVec2     tex0;
        NSKMath::KVec2     tex1;
        uint8_t            r;
        uint8_t            g;
        uint8_t            b;
        uint8_t            a;
        static KVertexDecl decl;
    };

    struct PT3_VERT
    {
        NSKMath::KVec3     pos;
        NSKMath::KVec3     tex;
        static KVertexDecl decl;
    };

    struct PNT_VERT
    {
        NSKMath::KVec3     pos;
        NSKMath::KVec3     normal;
        NSKMath::KVec2     tex;
        static KVertexDecl decl;
    };

    struct PN2T_VERT
    {
        NSKMath::KVec3     pos;
        NSKMath::KVec3     normal;
        NSKMath::KVec2     tex0;
        NSKMath::KVec2     tex1;
        static KVertexDecl decl;
    };

    struct PC_VERT
    {
        NSKMath::KVec3     pos;
        uint8_t            r;
        uint8_t            g;
        uint8_t            b;
        uint8_t            a;
        static KVertexDecl decl;
    };

    struct PNCGT_VERT
    {
        NSKMath::KVec3 pos;
        NSKMath::KVec3 normal;
        uint8_t        r;
        uint8_t        g;
        uint8_t        b;
        uint8_t        a;

        NSKMath::KVec4     tangent;
        NSKMath::KVec2     tex0;
        static KVertexDecl decl;
    };

    struct PNCG2T_VERT
    {
        NSKMath::KVec3 pos;
        NSKMath::KVec3 normal;

        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t a;

        NSKMath::KVec4     tangent;
        NSKMath::KVec2     tex0;
        NSKMath::KVec2     tex1;
        static KVertexDecl decl;
    };

    //struct PSS_VERT
    //{
    //    NSKMath::KVec3 pos;

    //    uint8_t r;
    //    uint8_t g;
    //    uint8_t b;
    //    uint8_t a;

    //    NSKMath::KVec2 tex0;
    //    NSKMath::KVec3 normal;
    //    NSKMath::KVec4 tangent;

    //    static KVertexDecl decl;
    //};

    struct PSS_VERT
    {
        NSKMath::KVec3 Position;
        NSKMath::KVec2 TexCoord;
        uint8_t            r;
        uint8_t            g;
        uint8_t            b;
        uint8_t            a;
        NSKMath::KVec3 normal;
        static KVertexDecl decl;
    };

    struct PSSMESH_VERT
    {
        NSKMath::KVec3     pos;
        NSKMath::KVec3     normal;
        uint8_t            r;
        uint8_t            g;
        uint8_t            b;
        uint8_t            a;
        NSKMath::KVec2     tex0;
        NSKMath::KVec4     tangent;
        static KVertexDecl decl;
    };

    struct SFXBILLBOARD_VERT
    {
        NSKMath::KVec3 pos;
        uint8_t        r;
        uint8_t        g;
        uint8_t        b;
        uint8_t        a;
        NSKMath::KVec2 tex0;
        NSKMath::KVec2 tex1;

        static KVertexDecl decl;
    };

    struct SFXPARTICLE_VERT
    {
        NSKMath::KVec3 pos;
        uint8_t        r;
        uint8_t        g;
        uint8_t        b;
        uint8_t        a;
        NSKMath::KVec2 tex0;

        static KVertexDecl decl;
    };

    struct PSS_INSTANCE_VERT
    {
        NSKMath::KVec4 color;

        NSKMath::KVec4 tex0;
        NSKMath::KVec4 vworldmat0;
        NSKMath::KVec4 vworldmat1;
        NSKMath::KVec4 vworldmat2;

        static KVertexDecl decl;
    };

    struct MAT44_INSTNCE_VERT
    {
        float              mat[16];
        static KVertexDecl decl;
    };

    struct MAT34_INSTNCE_VERT
    {
        float              mat[12];
        static KVertexDecl decl;
    };

    struct VEC4_INSTANCE_VERT
    {
        NSKMath::KVec4     vOffset;
        static KVertexDecl decl;
    };

    struct BONE_WEIGHT_VERT
    {
        NSKMath::KVec4     weight01; // x id0, y id1, z fweight0 w fweight1
        NSKMath::KVec4     weight23; // x id2, y id3, z fweight2 w fweight3
        static KVertexDecl decl;
    };

    struct AABBBOX_OCCLUSION_CULL_INSTANCE_VERT
    {
        NSKMath::KVec4     vMin;
        NSKMath::KVec4     vMax;
        static KVertexDecl decl;
    };

    struct PCT_VERT
    {
        NSKMath::KVec3 pos;

        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t a;

        NSKMath::KVec2     tex;
        static KVertexDecl decl;
    };

    struct SPEEDTREE_HD_VERT
    {
        NSKMath::KVec3 vPosition;
        NSKMath::KVec3 vNormal;
        NSKMath::KVec3 vTangent;
        NSKMath::KVec4 vTexCoord0;
        NSKMath::KVec4 vTexCoord1;
        NSKMath::KVec4 vTexCoord2;
        NSKMath::KVec4 vTexCoord3;
        NSKMath::KVec3 vFacingLeafInfo;
        float          fStartChangeTime;
        float          fAmbientOcclusion;

        static KVertexDecl decl;
    };

    struct SPEEDTREE_HD_BILLBOARD_VERT
    {
        NSKMath::KVec3 vPosition;
        NSKMath::KVec3 vNormal;
        NSKMath::KVec3 vTangent;
        NSKMath::KVec2 vTexCoord0;
        NSKMath::KVec2 inTimeAO; // x = fStartChangeTime; y = null //名字是shader里来的

        static KVertexDecl decl;
    };

    struct SPEEDTREE_HD_INSTANCE_VERT
    {
        float              mat[12];
        // NSKMath::KVec4 inLodInfo;
        static KVertexDecl decl;
    };

    struct WATER_VERT
    {
        NSKMath::KVec4     PosL;
        static KVertexDecl decl;
    };

    struct WATER_INSTANCE_VERT
    {
        NSKMath::KVec4     OffsetScaleXZ;
        NSKMath::KVec4     MaskIDTess;
        static KVertexDecl decl;
    };

    struct XRLocalViewDataLH
    {
        NSKMath::KMatrix projectMat[2];

        // local view space data
        NSKMath::KMatrix local_To_ViewMat[2];
        NSKMath::KVec3   local_EyePos[2];

        NSKMath::KVec3 local_EyeCenterPos;
        NSKMath::KVec3 local_EyeCenterLookAt;

        NSKMath::KVec3   local_hand_pos[2];
        NSKMath::KVec3   local_hand_handRayDir[2];
        NSKMath::KMatrix local_hand_Matrix[2];

        float farSee = 0.0f;
        float nearSee = 0.0f;
    };

}
