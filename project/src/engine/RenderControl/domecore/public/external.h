/*
    filename:       external.h
    author:         Ming Dong
    date:           2016-MAR-05
    description:    
*/
#pragma once

#include "configure.h"
#include "imemory.h"

#ifdef DOME_EXTERNAL_ASIO
#define ASIO_STANDALONE
#include <asio.hpp>
#endif

#ifdef DOME_EXTERNAL_RAPIDJSON
#define RAPIDJSON_ASSERT(x)     DOME_ASSERT2(x, "Rapidjson asserted.")
#define RAPIDJSON_NEW(x)        DOME_NewTag(x, "Rapidjson new.")
#define RAPIDJSON_DELETE(ptr)   DOME_Del(ptr)
#include <rapidjson/rapidjson.h>
#endif

#ifdef DOME_EXTERNAL_RAPIDXML
#include <rapidxml/rapidxml.hpp>
#include <rapidxml/rapidxml_print.hpp>
#endif