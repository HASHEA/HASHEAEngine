#pragma once
#include "hplatform.h"
namespace HASHEAENGINE
{
    struct Service {

        virtual auto Init(void* configuration) -> HS_Result { return HS_OK; }
        virtual auto Shutdown() -> HS_Result { return HS_OK; }
#ifdef HASHEA_DEBUG
        virtual auto OnGUI() -> void {};
#endif
    }; // struct Service

#define HASHEA_DECLARE_SERVICE(Type)        static Type* instance();
};
