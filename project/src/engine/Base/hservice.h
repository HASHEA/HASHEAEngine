#pragma once
#include "hplatform.h"
namespace HASHEAENGINE
{
    struct Service {

        virtual auto Init(void* configuration)->bool{}
        virtual auto Shutdown()->bool{}

    }; // struct Service

#define HASHEA_DECLARE_SERVICE(Type)        static Type* instance();
};
