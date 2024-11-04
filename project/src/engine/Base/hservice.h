#pragma once
#include "hplatform.h"
namespace HASHEAENGINE
{
    struct Service {

        virtual auto Init(void* configuration)->bool{}
        virtual auto Shutdown()->bool{}
#ifdef HASHEA_DEBUG
        virtual auto OnGUI() -> void {};
#endif
    }; // struct Service

#define HASHEA_DECLARE_SERVICE(Type)        static Type* instance();
};
