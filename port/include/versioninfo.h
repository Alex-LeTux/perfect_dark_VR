#ifndef _IN_VERSIONINFO_H
#define _IN_VERSIONINFO_H

#ifdef ANDROID
#define VERSION_TARGET "arm64-v8a Standalone"
#define VERSION_ARCH "arm64-v8a"
#else
#define VERSION_TARGET "x86_64-windows"
#define VERSION_ARCH "x86_64"
#endif

#define VERSION_ROMID "ntsc-final"
#define VERSION_BUILD "RelWithDebInfo"
#define VERSION_HASH "VR"
#define VERSION_BRANCH "port"

#endif
