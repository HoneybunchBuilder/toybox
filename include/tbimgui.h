#pragma once

#ifdef __clang__
#pragma clang diagnostic push
#if __has_warning("-Wreserved-identifier")
#pragma clang diagnostic ignored "-Wreserved-identifier"
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#endif
#endif

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#ifdef __clang__
#pragma clang diagnostic pop
#endif
