#pragma once
#include "hplatform.h"
namespace HASHEAENGINE
{
    struct Service {

        virtual auto init(void* configuration) -> HS_Result = 0;
        virtual auto shutdown() -> HS_Result = 0;
#ifdef HASHEA_DEBUG
        virtual auto on_gui() -> void {};
#endif
    }; // struct Service

#define HASHEA_DECLARE_SERVICE(Type)        static Type* instance();
};
