#pragma once

#define RCRENDERERSYS_DX11          1
#define RCRENDERERSYS               RCRENDERERSYS_DX11

#ifdef RCMOD_AS_LIB
#define RCMOD_API
#define RCMOD_API
#else
#ifdef RCMOD_EXPORTS
#define RCMOD_API      __declspec(dllexport)
#else
#define RCMOD_API      __declspec(dllimport)
#endif
#endif