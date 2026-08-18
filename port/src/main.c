
#include <stdlib.h>
#include <stdio.h>
#include <string.h>  // test
#include <PR/ultratypes.h>
#include <PR/ultrasched.h>
#include <PR/os_message.h>

#include "lib/main.h"
#include "bss.h"
#include "data.h"

#include "video.h"
#include "audio.h"
#include "input.h"
#include "fs.h"
#include "romdata.h"
#include "config.h"
#include "mod.h"
#include "system.h"
#include "utils.h"
#include "ext_tex.h"

#include "../vr/vr_log.h"


#ifdef ANDROID
#include <jni.h>
#include <android/log.h>
#include <SDL.h>
#include <SDL_main.h>
#include <EGL/egl.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "PD-VR", __VA_ARGS__)
#else
#define LOGI(...) printf(__VA_ARGS__)


#endif


extern void vrShowWaitingWindow(const char *bmpPath);

#ifdef ANDROID
u32 g_OsMemSize = 0;
s32 g_OsMemSizeMb = 16;
u8 g_Is4Mb = 0;
s8 g_Resetting = false;
OSSched g_Sched;

OSMesgQueue g_MainMesgQueue;
OSMesg g_MainMesgBuf[32];

u8 *g_MempHeap = NULL;
u32 g_MempHeapSize = 0;

u32 g_VmNumTlbMisses = 0;
u32 g_VmNumPageMisses = 0;
u32 g_VmNumPageReplaces = 0;
u8 g_VmShowStats = 0;

s32 g_TickRateDiv = 0; // VR
s32 g_TickExtraSleep = true;

s32 g_SkipIntro = false;

s32 g_FileAutoSelect = -1;

extern s32 g_StageNum;

s32 bootGetMemSize(void)
{
    return (s32)g_OsMemSize;
}

void *bootAllocateStack(s32 threadid, s32 size)
{
    static u8 bruh[0x1000];
    return bruh;
}

void bootCreateSched(void)
{
    osCreateMesgQueue(&g_MainMesgQueue, g_MainMesgBuf, ARRAYCOUNT(g_MainMesgBuf));
    if (osTvType == OS_TV_MPAL) {
        osCreateScheduler(&g_Sched, NULL, OS_VI_MPAL_LAN1, 1);
    } else {
        osCreateScheduler(&g_Sched, NULL, OS_VI_NTSC_LAN1, 1);
    }
}




static void gameInit(void)
{

    osMemSize = g_OsMemSizeMb * 1024 * 1024;

    for (s32 i = 0; i < MAX_PLAYERS; ++i) {
        struct extplayerconfig *cfg = g_PlayerExtCfg + i;

        cfg->fovzoommult = cfg->fovzoom ? cfg->fovy / 60.0f : 1.0f;
    }

    if (g_HudCenter == HUDCENTER_NORMAL) {
        g_HudAlignModeL = G_ASPECT_CENTER_EXT;
        g_HudAlignModeR = G_ASPECT_CENTER_EXT;
    } else if (g_HudCenter == HUDCENTER_WIDE) {
        g_HudAlignModeL = G_ASPECT_LEFT_EXT | G_ASPECT_WIDE_EXT;
        g_HudAlignModeR = G_ASPECT_RIGHT_EXT | G_ASPECT_WIDE_EXT;
    }
}

static void cleanup(void)
{

    sysLogPrintf(LOG_NOTE, "shutdown");

    inputSaveBinds();
    configSave(CONFIG_PATH);
    videoShutdown();
    crashShutdown();



    // TODO: actually shut down all subsystems
}

#ifdef ANDROID
// Android-specific globals
static char g_data_path[512] = {0};
static int g_initialized = 0;

// JNI functions for Android
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL
Java_com_perfectdark_port_MainActivity_nativeInit(JNIEnv* env, jobject thiz, jstring dataPath) {
    if (g_initialized) return;

    const char* path = (*env)->GetStringUTFChars(env, dataPath, NULL);
    strncpy(g_data_path, path, sizeof(g_data_path) - 1);
    (*env)->ReleaseStringUTFChars(env, dataPath, path);

    g_initialized = 1;

    sysLogPrintf(LOG_NOTE, "Android native init complete, data path: %s", g_data_path);

    // Don't call pd_main here - let SDL2 handle it
}

JNIEXPORT void JNICALL
Java_com_perfectdark_port_MainActivity_nativeStartGame(JNIEnv* env, jobject thiz) {
    __android_log_print(ANDROID_LOG_INFO, "PerfectDark", "nativeStartGame called");
    if (g_initialized) {
        char* argv[] = {"pd", NULL};
        pd_main(1, (const char**)argv);
    }
}

JNIEXPORT void JNICALL
Java_com_perfectdark_port_MainActivity_nativeDestroy(JNIEnv* env, jobject thiz) {
    // Cleanup when activity is destroyed

    g_initialized = 0;
}

// Forward declaration
int pd_main(int argc, const char **argv);

// SDL2 will call this as the main function on Android
int SDL_main(int argc, char* argv[]) {
    // Add some basic logging to see if we get here
    __android_log_print(ANDROID_LOG_INFO, "PerfectDark", "SDL_main started on Android");

    // Wait for initialization if needed
    int timeout = 50; // 5 seconds
    while (!g_initialized && timeout > 0) {
        SDL_Delay(100);
        timeout--;
    }

    if (!g_initialized) {
        __android_log_print(ANDROID_LOG_ERROR, "PerfectDark", "Android not initialized after timeout");
        return -1;
    }

    // Log the data path for debugging
    __android_log_print(ANDROID_LOG_INFO, "PerfectDark", "Android data path: %s", g_data_path);

    // Change to the data directory so the game can find files
    if (chdir(g_data_path) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, "PerfectDark", "Failed to change to data directory: %s", g_data_path);
    } else {
        __android_log_print(ANDROID_LOG_INFO, "PerfectDark", "Changed working directory to: %s", g_data_path);
    }

    return pd_main(argc, (const char**)argv);
}

const char* sysGetDataPath(void) {
    if (g_data_path[0] == '\0') {
        // Default to SDL2's internal storage path for Android
        strcpy(g_data_path, "/data/data/com.perfectdark.port/files");
        sysLogPrintf(LOG_NOTE, "Using default Android data path: %s", g_data_path);
    }
    return g_data_path;
}


int pd_main(int argc, const char **argv)
#else
int main(int argc, const char **argv)
#endif
{


    sysInitArgs(argc, argv);

    __android_log_print(ANDROID_LOG_INFO, "PerfectDark", "Starting initialization sequence");

    if (!sysArgCheck("--no-crash-handler")) {
        crashInit();
    }

    __android_log_print(ANDROID_LOG_INFO, "PerfectDark", "sysInit starting");
    sysInit();
    __android_log_print(ANDROID_LOG_INFO, "PerfectDark", "fsInit starting");
    fsInit();
    __android_log_print(ANDROID_LOG_INFO, "PerfectDark", "configInit starting");
    configInit();
    __android_log_print(ANDROID_LOG_INFO, "PerfectDark", "SDL2 init starting");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, "PerfectDark", "SDL_Init failed: %s", SDL_GetError());
        return -1;
    }

    // Set OpenGL ES attributes for Android
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    __android_log_print(ANDROID_LOG_INFO, "PerfectDark", "videoInit starting");
    videoInit();
    __android_log_print(ANDROID_LOG_INFO, "PerfectDark", "inputInit starting");
    inputInit();
    __android_log_print(ANDROID_LOG_INFO, "PerfectDark", "audioInit starting");
    audioInit();
    __android_log_print(ANDROID_LOG_INFO, "PerfectDark", "romdataInit starting");
    romdataInit();
    __android_log_print(ANDROID_LOG_INFO, "PerfectDark", "romdataInit complete");
    extTexInit();
    __android_log_print(ANDROID_LOG_INFO, "PerfectDark", "extTexInit complete");

    g_ValidGbcRomFound = romdataCheckGbcRom();
    __android_log_print(ANDROID_LOG_INFO, "PerfectDark", "GBC ROM check complete");

    __android_log_print(ANDROID_LOG_INFO, "PerfectDark", "gameInit starting");
    gameInit();
    __android_log_print(ANDROID_LOG_INFO, "PerfectDark", "gameInit complete");

    if (fsGetModDir()) {
        modConfigLoad(MOD_CONFIG_FNAME);
    }

    atexit(cleanup);

    bootCreateSched();

    g_OsMemSize = osGetMemSize();

    g_MempHeapSize = g_OsMemSize;
    g_MempHeap = sysMemZeroAlloc(g_MempHeapSize);
    if (!g_MempHeap) {
        sysFatalError("Could not alloc %u bytes for memp heap.", g_MempHeapSize);
    }

    sysLogPrintf(LOG_NOTE, "memp heap at %p - %p", g_MempHeap, g_MempHeap + g_MempHeapSize);
    sysLogPrintf(LOG_NOTE, "rom  file at %p - %p", g_RomFile, g_RomFile + g_RomFileSize);

    g_SndDisabled = sysArgCheck("--no-sound");

    g_StageNum = sysArgGetInt("--boot-stage", STAGE_TITLE);

    // Debug
   // g_SkipIntro = true; // SKIP INTRO

    if (g_StageNum == STAGE_TITLE && (sysArgCheck("--skip-intro") || g_SkipIntro)) {
        // shorthand for --boot-stage 0x26
        g_StageNum = STAGE_CITRAINING;
    } else if (g_StageNum < 0x01 || g_StageNum > 0x5d) {
        // stage num out of range
        g_StageNum = STAGE_TITLE; // boot normal !!
    }

    if (g_StageNum != STAGE_TITLE) {
        sysLogPrintf(LOG_NOTE, "boot stage set to 0x%02x", g_StageNum);
    }

    g_FileAutoSelect = sysArgGetInt("--profile", -1);
    if (g_FileAutoSelect >= 0) {
        sysLogPrintf(LOG_NOTE, "player profile set to %d", g_FileAutoSelect);
    }







    mainProc();

    return 0;
}


PD_CONSTRUCTOR static void gameConfigInit(void)
{
    configRegisterInt("Game.MemorySize", &g_OsMemSizeMb, 4, 2048);
    configRegisterInt("Game.CenterHUD", &g_HudCenter, 0, 2);
    configRegisterInt("Game.MenuMouseControl", &g_MenuMouseControl, 0, 1);
    configRegisterFloat("Game.ScreenShakeIntensity", &g_ViShakeIntensityMult, 0.f, 10.f);
    configRegisterInt("Game.TickRateDivisor", &g_TickRateDiv, 0, 10);
    configRegisterInt("Game.ExtraSleep", &g_TickExtraSleep, 0, 1);
    configRegisterInt("Game.SkipIntro", &g_SkipIntro, 0, 1);
    configRegisterInt("Game.DisableMpDeathMusic", &g_MusicDisableMpDeath, 0, 1);
    configRegisterInt("Game.GEMuzzleFlashes", &g_BgunGeMuzzleFlashes, 0, 1);
    configRegisterInt("Game.MaxExplosions", &g_MaxExplosions, 6, 96);
    for (s32 j = 0; j < MAX_PLAYERS; ++j) {
        const s32 i = j + 1;
        configRegisterFloat(strFmt("Game.Player%d.FovY", i), &g_PlayerExtCfg[j].fovy, 5.f, 175.f);
        configRegisterInt(strFmt("Game.Player%d.FovAffectsZoom", i), &g_PlayerExtCfg[j].fovzoom, 0, 1);
        configRegisterInt(strFmt("Game.Player%d.MouseAimMode", i), &g_PlayerExtCfg[j].mouseaimmode, 0, 1);
        configRegisterFloat(strFmt("Game.Player%d.MouseAimSpeedX", i), &g_PlayerExtCfg[j].mouseaimspeedx, 0.f, 10.f);
        configRegisterFloat(strFmt("Game.Player%d.MouseAimSpeedY", i), &g_PlayerExtCfg[j].mouseaimspeedy, 0.f, 10.f);
        configRegisterFloat(strFmt("Game.Player%d.RadialMenuSpeed", i), &g_PlayerExtCfg[j].radialmenuspeed, 0.f, 10.f);
        configRegisterFloat(strFmt("Game.Player%d.CrosshairSway", i), &g_PlayerExtCfg[j].crosshairsway, 0.f, 10.f);
        configRegisterInt(strFmt("Game.Player%d.CrouchMode", i), &g_PlayerExtCfg[j].crouchmode, 0, CROUCHMODE_TOGGLE_ANALOG);
        configRegisterInt(strFmt("Game.Player%d.ExtendedControls", i), &g_PlayerExtCfg[j].extcontrols, 0, 1);
        configRegisterUInt(strFmt("Game.Player%d.CrosshairColour", i), &g_PlayerExtCfg[j].crosshaircolour, 0, 0xFFFFFFFF);
        configRegisterUInt(strFmt("Game.Player%d.CrosshairSize", i), &g_PlayerExtCfg[j].crosshairsize, 0, 4);
        configRegisterInt(strFmt("Game.Player%d.CrosshairHealth", i), &g_PlayerExtCfg[j].crosshairhealth, 0, CROSSHAIR_HEALTH_ON_WHITE);
        configRegisterInt(strFmt("Game.Player%d.UseKeyReloads", i), &g_PlayerExtCfg[j].usereloads, 0, false);
    }
}

#else
u32 g_OsMemSize = 0;
s32 g_OsMemSizeMb = 16;
u8 g_Is4Mb = 0;
s8 g_Resetting = false;
OSSched g_Sched;

OSMesgQueue g_MainMesgQueue;
OSMesg g_MainMesgBuf[32];

u8* g_MempHeap = NULL;
u32 g_MempHeapSize = 0;

u32 g_VmNumTlbMisses = 0;
u32 g_VmNumPageMisses = 0;
u32 g_VmNumPageReplaces = 0;
u8 g_VmShowStats = 0;

s32 g_TickRateDiv = 0; // VR
s32 g_TickExtraSleep = true;

s32 g_SkipIntro = false;

s32 g_FileAutoSelect = -1;

extern s32 g_StageNum;

s32 bootGetMemSize(void)
{
		return (s32)g_OsMemSize;
}

void* bootAllocateStack(s32 threadid, s32 size)
{
		static u8 bruh[0x1000];
		return bruh;
}

void bootCreateSched(void)
{
		osCreateMesgQueue(&g_MainMesgQueue, g_MainMesgBuf, ARRAYCOUNT(g_MainMesgBuf));
		if (osTvType == OS_TV_MPAL) {
				osCreateScheduler(&g_Sched, NULL, OS_VI_MPAL_LAN1, 1);
		}
		else {
				osCreateScheduler(&g_Sched, NULL, OS_VI_NTSC_LAN1, 1);
		}
}

static void gameInit(void)
{
		osMemSize = g_OsMemSizeMb * 1024 * 1024;

		for (s32 i = 0; i < MAX_PLAYERS; ++i) {
				struct extplayerconfig* cfg = g_PlayerExtCfg + i;

				cfg->fovzoommult = cfg->fovzoom ? cfg->fovy / 60.0f : 1.0f;
		}

		if (g_HudCenter == HUDCENTER_NORMAL) {
				g_HudAlignModeL = G_ASPECT_CENTER_EXT;
				g_HudAlignModeR = G_ASPECT_CENTER_EXT;
		}
		else if (g_HudCenter == HUDCENTER_WIDE) {
				g_HudAlignModeL = G_ASPECT_LEFT_EXT | G_ASPECT_WIDE_EXT;
				g_HudAlignModeR = G_ASPECT_RIGHT_EXT | G_ASPECT_WIDE_EXT;
		}
}

static void cleanup(void)
{
		sysLogPrintf(LOG_NOTE, "shutdown");
		inputSaveBinds();
		configSave(CONFIG_PATH);
		videoShutdown();
		crashShutdown();
		// TODO: actually shut down all subsystems
}

int main(int argc, const char** argv)
{
        vrShowWaitingWindow("logo.bmp"); // VR
		sysInitArgs(argc, argv);
		if (!sysArgCheck("--no-crash-handler")) {
				crashInit();
		}
		sysInit();
		fsInit();
		configInit();
		videoInit();
		inputInit();
		audioInit();
		romdataInit();
        extTexInit();

		g_ValidGbcRomFound = romdataCheckGbcRom();

		gameInit();


		if (fsGetModDir()) {
				modConfigLoad(MOD_CONFIG_FNAME);
		}

		atexit(cleanup);

		bootCreateSched();

		g_OsMemSize = osGetMemSize();

		g_MempHeapSize = g_OsMemSize;
		g_MempHeap = sysMemZeroAlloc(g_MempHeapSize);
		if (!g_MempHeap) {
				sysFatalError("Could not alloc %u bytes for memp heap.", g_MempHeapSize);
		}

		sysLogPrintf(LOG_NOTE, "memp heap at %p - %p", g_MempHeap, g_MempHeap + g_MempHeapSize);
		sysLogPrintf(LOG_NOTE, "rom  file at %p - %p", g_RomFile, g_RomFile + g_RomFileSize);

		g_SndDisabled = sysArgCheck("--no-sound");

		g_StageNum = sysArgGetInt("--boot-stage", STAGE_TITLE);

		if (g_StageNum == STAGE_TITLE && (sysArgCheck("--skip-intro") || g_SkipIntro)) {
				// shorthand for --boot-stage 0x26
				g_StageNum = STAGE_CITRAINING;
		}
		else if (g_StageNum < 0x01 || g_StageNum > 0x5d) {
				// stage num out of range
				g_StageNum = STAGE_TITLE;
		}

		if (g_StageNum != STAGE_TITLE) {
				sysLogPrintf(LOG_NOTE, "boot stage set to 0x%02x", g_StageNum);
		}

		g_FileAutoSelect = sysArgGetInt("--profile", -1);
		if (g_FileAutoSelect >= 0) {
				sysLogPrintf(LOG_NOTE, "player profile set to %d", g_FileAutoSelect);
		}

		mainProc();

		return 0;
}

PD_CONSTRUCTOR static void gameConfigInit(void)
{
	configRegisterInt("Game.MemorySize", &g_OsMemSizeMb, 4, 2048);
	configRegisterInt("Game.CenterHUD", &g_HudCenter, 0, 2);
	configRegisterInt("Game.MenuMouseControl", &g_MenuMouseControl, 0, 1);
	configRegisterFloat("Game.ScreenShakeIntensity", &g_ViShakeIntensityMult, 0.f, 10.f);
	configRegisterInt("Game.TickRateDivisor", &g_TickRateDiv, 0, 10);
	configRegisterInt("Game.ExtraSleep", &g_TickExtraSleep, 0, 1);
	configRegisterInt("Game.SkipIntro", &g_SkipIntro, 0, 1);
	configRegisterInt("Game.DisableMpDeathMusic", &g_MusicDisableMpDeath, 0, 1);
	configRegisterInt("Game.GEMuzzleFlashes", &g_BgunGeMuzzleFlashes, 0, 1);
	configRegisterInt("Game.MaxExplosions", &g_MaxExplosions, 6, 96);
	for (s32 j = 0; j < MAX_PLAYERS; ++j) {
		const s32 i = j + 1;
		configRegisterFloat(strFmt("Game.Player%d.FovY", i), &g_PlayerExtCfg[j].fovy, 5.f, 175.f);
		configRegisterInt(strFmt("Game.Player%d.FovAffectsZoom", i), &g_PlayerExtCfg[j].fovzoom, 0, 1);
		configRegisterInt(strFmt("Game.Player%d.MouseAimMode", i), &g_PlayerExtCfg[j].mouseaimmode, 0, 1);
		configRegisterFloat(strFmt("Game.Player%d.MouseAimSpeedX", i), &g_PlayerExtCfg[j].mouseaimspeedx, 0.f, 10.f);
		configRegisterFloat(strFmt("Game.Player%d.MouseAimSpeedY", i), &g_PlayerExtCfg[j].mouseaimspeedy, 0.f, 10.f);
		configRegisterFloat(strFmt("Game.Player%d.RadialMenuSpeed", i), &g_PlayerExtCfg[j].radialmenuspeed, 0.f, 10.f);
		configRegisterFloat(strFmt("Game.Player%d.CrosshairSway", i), &g_PlayerExtCfg[j].crosshairsway, 0.f, 10.f);
		//configRegisterFloat(strFmt("Game.Player%d.CrosshairEdgeBoundary", i), &g_PlayerExtCfg[j].crosshairedgeboundary, 0.0f, 1.0f);
		configRegisterInt(strFmt("Game.Player%d.CrouchMode", i), &g_PlayerExtCfg[j].crouchmode, 0, CROUCHMODE_TOGGLE_ANALOG);
		configRegisterInt(strFmt("Game.Player%d.ExtendedControls", i), &g_PlayerExtCfg[j].extcontrols, 0, 1);
		configRegisterUInt(strFmt("Game.Player%d.CrosshairColour", i), &g_PlayerExtCfg[j].crosshaircolour, 0, 0xFFFFFFFF);
		configRegisterUInt(strFmt("Game.Player%d.CrosshairSize", i), &g_PlayerExtCfg[j].crosshairsize, 0, 4);
		configRegisterInt(strFmt("Game.Player%d.CrosshairHealth", i), &g_PlayerExtCfg[j].crosshairhealth, 0, CROSSHAIR_HEALTH_ON_WHITE);
		configRegisterInt(strFmt("Game.Player%d.UseKeyReloads", i), &g_PlayerExtCfg[j].usereloads, 0, false);
	}
}
#endif
