#pragma once
#include "hplatform.h"
namespace AshEngine
{
    struct Service {

        virtual auto init(void* configuration) -> bool = 0;
        virtual auto shutdown() -> bool = 0;
#ifdef ASH_DEBUG
        virtual auto on_gui() -> void {};
#endif
    }; // struct Service

#define ASH_DECLARE_SERVICE(Type)        static Type* instance();
};
