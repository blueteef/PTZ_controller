#pragma once

// =============================================================================
// PTZ Controller — Semantic Version
// =============================================================================

#define PTZ_VERSION_MAJOR  0
#define PTZ_VERSION_MINOR  1
#define PTZ_VERSION_PATCH  0

#define PTZ_BUILD_DATE  __DATE__
#define PTZ_BUILD_TIME  __TIME__

#ifndef PTZ_VERSION_EXTRA
#define PTZ_VERSION_EXTRA ""
#endif

// Helper macros for stringification
#define PTZ_VERSION_STRINGIFY(x)   #x
#define PTZ_VERSION_TOSTR(x)       PTZ_VERSION_STRINGIFY(x)

// Full version string: "0.1.0" (plus optional extra suffix from build flags)
#define PTZ_VERSION_STRING  \
    PTZ_VERSION_TOSTR(PTZ_VERSION_MAJOR) "." \
    PTZ_VERSION_TOSTR(PTZ_VERSION_MINOR) "." \
    PTZ_VERSION_TOSTR(PTZ_VERSION_PATCH) \
    PTZ_VERSION_EXTRA

#define PTZ_FIRMWARE_NAME  "PTZ Controller"
