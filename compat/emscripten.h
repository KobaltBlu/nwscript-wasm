#pragma once

#ifdef __EMSCRIPTEN__
#include <strings.h>

#ifndef stricmp
#define stricmp strcasecmp
#endif

#ifndef strnicmp
#define strnicmp strncasecmp
#endif
#endif