#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <SDL.h>
#include <PR/ultratypes.h>
#include <PR/gbi.h>
#include "platform.h"
#include "config.h"
#include "system.h"
#include "video.h"

#include "../fast3d/gfx_api.h"
#include "../fast3d/gfx_sdl.h"
#include "../fast3d/gfx_opengl.h"

#include "../vr/vr_openxr.h"
#include "../vr/vr_log.h"


#ifdef ANDROID
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "PD-VR", __VA_ARGS__)
#else
#define LOGI(...) printf(__VA_ARGS__)
#endif

#define DEFAULT_VID_FULLSCREEN false
#define DEFAULT_VID_FULLSCREEN_EXCLUSIVE false

extern int32_t g_internalRenderWidth;
extern int32_t g_internalRenderHeight;

static struct GfxWindowManagerAPI *wmAPI;
static struct GfxRenderingAPI *renderingAPI;

static bool initDone = false;

// VR: size of the desktop mirror window, kept in step with the eye render size. Only the
// centering/fullscreen helpers below read these; the render resolution itself comes from
// RENDER_SCALE, so they are runtime state and are not persisted to the config.
s32 vidWidth = -1;
s32 vidHeight = -1;

static s32 vidFramebuffers = true;
static s32 vidFullscreen = DEFAULT_VID_FULLSCREEN;
static s32 vidFullscreenExclusive = DEFAULT_VID_FULLSCREEN_EXCLUSIVE;
static s32 vidMaximize = false;
static s32 vidCenter = false;
static s32 vidAllowHiDpi = false;
static s32 vidVsync = 1;
static s32 vidMSAA = 1;
static s32 vidFramerateLimit = 0;

static s32 vidDisplayFPS = 0;
static f32 vidDisplayFPSInterval = 1.f;
static f32 vidAvgFPS = 0;

static s32 vidNumModes = 1;
static displaymode vidModeDefault;
static displaymode *vidModes = &vidModeDefault;

static s32 texFilter = FILTER_LINEAR;
static s32 texFilter2D = true;
static s32 texDetail = false;

static u32 dlcount = 0;
static u32 frames = 0;
static f64 startTime, endTime;
static f64 accumDelta = 0.0;
static f64 fpsTime = 0.0;
static s32 fpsNumFrames = 0;

s32 videoInitDisplayModes(void);

extern float RENDER_SCALE;
static f32 *vidModeScales = NULL;
extern bool vr_restart_with_new_scale(float scale);
extern bool vr_configure_resolution();



s32 videoInit(void)
{
    wmAPI = &gfx_sdl;
    renderingAPI = &gfx_opengl_api;

    gfx_current_native_viewport.width = 320;
    gfx_current_native_viewport.height = 220;
    gfx_current_native_aspect = 320.f / 220.f;
    gfx_framebuffers_enabled = (bool)vidFramebuffers;
    gfx_detail_textures_enabled = (bool)texDetail;
    gfx_msaa_level = vidMSAA;

    struct GfxInitSettings set = {
            .wapi = wmAPI,
            .rapi = renderingAPI,
            .window_settings = {
                    .title = "Perfect Dark",
                    .width = VrRecommendedW,
                    .height = VrRecommendedH,
#ifdef ANDROID
                    .x = 0,
                    .y = 0,
#else
                    .x = 100,
			        .y = 100,
#endif
                    .fullscreen = vidFullscreen,
                    .fullscreen_is_exclusive = vidFullscreenExclusive,
                    .maximized = vidMaximize,
                    .centered = vidCenter,
                    .allow_hidpi = vidAllowHiDpi
            }
    };

    gfx_init(&set);

    videoInitDisplayModes();
    videoSetVsync(vidVsync);
    videoSetFramerateLimit(vidFramerateLimit);

    gfx_set_texture_filter((enum FilteringMode)texFilter);

    // Force fullscreen OFF VR
    videoSetFullscreen(false);

    initDone = true;
    return 0;
}


void videoStartFrame(void)
{
    if (initDone) {
        startTime = wmAPI->get_time();
        gfx_start_frame();
    }

    // Synchronize with their backend counterparts.
    vidFullscreen = videoGetFullscreen();
    vidMaximize = videoGetMaximizeWindow();
}

void videoSubmitCommands(Gfx *cmds)
{
    if (initDone) {
        gfx_run(cmds);
        ++dlcount;
    }
}

void videoEndFrame(void)
{
    if (!initDone) {
        return;
    }

    gfx_end_frame();

    ++frames;
    ++fpsNumFrames;

    const f64 flipTime = wmAPI->get_time();
    accumDelta += flipTime - endTime;
    endTime = flipTime;

    if (endTime >= fpsTime) {
        char tmp[128];
        vidAvgFPS = fpsNumFrames ? ((f64)fpsNumFrames / accumDelta) : 0.f;
        fpsNumFrames = 0;
        accumDelta = 0.0;
        fpsTime = endTime + vidDisplayFPSInterval;
    }
}



f32 videoGetAverageFPS(void)
{
    return vidAvgFPS;
}

void videoClearScreen(void)
{
    videoStartFrame();
    // TODO: clear
    videoEndFrame();
}

void *videoGetWindowHandle(void)
{
    if (initDone) {
        return wmAPI->get_window_handle();
    }
    return NULL;
}

void videoUpdateNativeResolution(s32 w, s32 h)
{
    gfx_current_native_viewport.width = w;
    gfx_current_native_viewport.height = h;
    gfx_current_native_aspect = (float)w / (float)h;
}

s32 videoGetNativeWidth(void)
{
    return gfx_current_native_viewport.width;
}

s32 videoGetNativeHeight(void)
{
    return gfx_current_native_viewport.height;
}

s32 videoGetWidth(void)
{
    return gfx_current_dimensions.width;
}

s32 videoGetHeight(void)
{
    return gfx_current_dimensions.height;
}

s32 videoGetFullscreen(void)
{
    vidFullscreen = wmAPI->get_fullscreen_state();
    return vidFullscreen;
}

s32 videoGetFullscreenMode(void)
{
    vidFullscreenExclusive = wmAPI->get_fullscreen_flag_mode();
    return vidFullscreenExclusive;
}

s32 videoGetMaximizeWindow(void)
{
    vidMaximize = wmAPI->get_maximized_state();
    return vidMaximize;
}

s32 videoGetCenterWindow(void)
{
    return vidCenter;
}

f32 videoGetAspect(void)
{
    return XrAspect;
}

s32 videoGetDisplayModeIndex(void)
{
    for (s32 i = 0; i < vidNumModes; ++i) {
        if (vidModes[i].width == gfx_current_dimensions.width &&
            vidModes[i].height == gfx_current_dimensions.height) {
            return i;
        }
//        LOGI("vid test vidModes W = %d, H = %d", vidModes[i].width, vidModes[i].height);
//        LOGI("vid test gfx_current_dimensions W = %d, H = %d", gfx_current_dimensions.width, gfx_current_dimensions.height);
    }

    // Current dimensions don't match any known mode, so return index 0, "Custom".
    return -1;

}

s32 videoGetMSAA(void)
{
    vidMSAA = (s32)gfx_msaa_level;
    return vidMSAA;
}

s32 videoGetVsync(void)
{
    vidVsync = wmAPI->get_swap_interval();
    return vidVsync;
}

s32 videoGetFramerateLimit(void)
{
    vidFramerateLimit = wmAPI->get_target_fps();
    return vidFramerateLimit;
}

s32 videoGetDisplayFPS(void)
{
    return vidDisplayFPS;
}


s32 videoInitDisplayModes(void)
{
    if (!wmAPI->get_current_display_mode(&vidModeDefault.width, &vidModeDefault.height)) {
        vidModeDefault.width = VrRecommendedW;
        vidModeDefault.height = VrRecommendedH;
        return false;
    }

    const s32 numBaseModes = wmAPI->get_num_display_modes();
    if (!numBaseModes) {
        return false;
    }

    const float customScales[] = { 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f };
    const s32 numCustomModes = 1 + (s32)(sizeof(customScales) / sizeof(customScales[0]));

    displaymode *modeList = sysMemZeroAlloc((numBaseModes + numCustomModes) * sizeof(displaymode));
    if (!modeList) return false;

    f32 *scaleList = sysMemZeroAlloc((numBaseModes + numCustomModes) * sizeof(f32));
    if (!scaleList) { sysMemFree(modeList); return false; }

    s32 numModes = 0;

    // Custom modes scaled from the internal render resolution
    for (s32 i = 0; i < (s32)(sizeof(customScales) / sizeof(customScales[0])); ++i) {
        modeList[numModes].width  = (s32)(VrRecommendedW * customScales[i]) & ~1;
        modeList[numModes].height = (s32)(VrRecommendedH * customScales[i]) & ~1;
        scaleList[numModes] = customScales[i];
        ++numModes;
    }

    // SDL modes — skip those that duplicate a custom mode
    s32 w = -1, h = w, neww = w, newh = w;
    for (s32 i = 0; i < numBaseModes; ++i) {
        wmAPI->get_display_mode(i, &neww, &newh);
        if (neww == w && newh == h) continue;
        w = neww;
        h = newh;

        s32 duplicate = false;
        for (s32 j = 0; j < numModes; ++j) {
            if (modeList[j].width == w && modeList[j].height == h) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;

        modeList[numModes].width  = w;
        modeList[numModes].height = h;
        scaleList[numModes] = 0.f;
        ++numModes;
    }

    modeList  = sysMemRealloc(modeList,  numModes * sizeof(displaymode));
    scaleList = sysMemRealloc(scaleList, numModes * sizeof(f32));
    if (!modeList || !scaleList) return false;

    if (vidModeScales) sysMemFree(vidModeScales);
    vidModes      = modeList;
    vidModeScales = scaleList;
    vidNumModes   = numModes;

    // VR calls this again once it knows the eye render size; adopt it as the mirror-window
    // size so the helpers below never run on a placeholder.
    if (g_internalRenderWidth > 0 && g_internalRenderHeight > 0) {
        vidWidth  = g_internalRenderWidth;
        vidHeight = g_internalRenderHeight;
    }

    return true;
}

s32 videoGetDisplayMode(displaymode *out, const s32 index)
{
    if (index >= 0 && index < vidNumModes) {
        *out = vidModes[index];
        return true;
    }
    return false;
}

s32 videoGetNumDisplayModes(void)
{
    return vidNumModes;
}

void videoSetDisplayMode(const s32 index)
{

    const displaymode dm = vidModes[index];
    vidWidth  = dm.width;
    vidHeight = dm.height;

    // Update RENDER_SCALE
    if (vidModeScales && vidModeScales[index] > 0.f) {
        RENDER_SCALE = vidModeScales[index];
    } else {
        RENDER_SCALE = 1.0f;
    }

    vr_log("videoSetDisplayMode: index=%d, %dx%d, scale=%.2f",
           index, vidWidth, vidHeight, RENDER_SCALE);


    s32 posX = 100;
    s32 posY = 100;
    if (vidCenter) {
        wmAPI->get_centered_positions(vidWidth, vidHeight, &posX, &posY);
    }

    if (vidFullscreen) {
        wmAPI->set_closest_resolution(vidWidth, vidHeight, vidCenter);
    } else {
        if (vidMaximize) {
            videoSetMaximizeWindow(false);
        } else {
            wmAPI->set_dimensions(vidWidth, vidHeight, posX, posY);
        }
    }

    // Update RENDER_SCALE and restart
    if (vidModeScales && vidModeScales[index] > 0.f) {
        vr_restart_with_new_scale(vidModeScales[index]);
    }

}

s32 videoGetTextureFilter2D(void)
{
    return texFilter2D;
}

u32 videoGetTextureFilter(void)
{
    return texFilter;
}

s32 videoGetDetailTextures(void)
{
    return texDetail;
}

void videoSetWindowOffset(s32 x, s32 y)
{
    gfx_current_game_window_viewport.x = x;
    gfx_current_game_window_viewport.y = y;
}

void videoSetFullscreen(s32 fs)
{
    if (fs != vidFullscreen) {
        vidFullscreen = !!fs;
        wmAPI->set_closest_resolution(vidWidth, vidHeight, vidCenter);
        wmAPI->set_fullscreen(vidFullscreen);
        if (!vidFullscreen && vidMaximize) {
            wmAPI->set_maximize(false);
            wmAPI->set_maximize(true);
        }
    }
}

void videoSetFullscreenMode(s32 mode)
{
    vidFullscreenExclusive = mode;
    wmAPI->set_fullscreen_flag(mode);
    if (vidFullscreen) {
        wmAPI->set_fullscreen(false);
        wmAPI->set_fullscreen(true);
    }
}

void videoSetMaximizeWindow(s32 fs)
{
    if (fs != vidMaximize) {
        vidMaximize = !!fs;
        wmAPI->set_maximize(vidMaximize);
        if (vidCenter && !vidMaximize) {
            s32 posX = 0;
            s32 posY = 0;
            wmAPI->get_centered_positions(vidWidth, vidHeight, &posX, &posY);
            wmAPI->set_dimensions(vidWidth, vidHeight, posX, posY);
        }
    }
}

void videoSetCenterWindow(s32 center)
{
    vidCenter = center;
    if (vidCenter && !vidMaximize) {
        s32 posX = 0;
        s32 posY = 0;
        wmAPI->get_centered_positions(vidWidth, vidHeight, &posX, &posY);
        wmAPI->set_dimensions(vidWidth, vidHeight, posX, posY);
    }
}

void videoSetTextureFilter(u32 filter)
{
    if (filter > FILTER_THREE_POINT) filter = FILTER_THREE_POINT;
    if (texFilter == filter) return;
    texFilter = filter;
    gfx_set_texture_filter((enum FilteringMode)filter);
}

void videoSetTextureFilter2D(s32 filter)
{
    texFilter2D = !!filter;
}

void videoSetDetailTextures(s32 detail)
{
    texDetail = !!detail;
    gfx_detail_textures_enabled = (bool)texDetail;
}

s32 videoCreateFramebuffer(u32 w, u32 h, s32 upscale, s32 autoresize)
{
    return gfx_create_framebuffer(w, h, upscale, autoresize);
}

void videoSetMSAA(const s32 msaa)
{
    vidMSAA = msaa;
    gfx_msaa_level = (u32)vidMSAA;
}

void videoSetVsync(const s32 vsync)
{
    vidVsync = wmAPI->set_swap_interval(vsync) ? vsync : 0;

    if (vidVsync == 0 && vidFramerateLimit == 0) {
        // cap FPS if there's no vsync to prevent the game from exploding
        videoSetFramerateLimit(VIDEO_MAX_FPS);
    }
}

void videoSetFramerateLimit(const s32 limit)
{
    vidFramerateLimit = (vidVsync == 0 && limit == 0) ? VIDEO_MAX_FPS : limit;
    wmAPI->set_target_fps(vidFramerateLimit);
}

void videoSetDisplayFPS(const s32 displayfps)
{
    vidDisplayFPS = displayfps;
}

void videoSetFramebuffer(s32 target)
{
    return gfx_set_framebuffer(target, 1.f);
}

void videoResetFramebuffer(void)
{
    return gfx_reset_framebuffer();
}

s32 videoFramebuffersSupported(void)
{
    return gfx_framebuffers_enabled;
}

void videoResizeFramebuffer(s32 target, u32 w, u32 h, s32 upscale, s32 autoresize)
{
    gfx_resize_framebuffer(target, w, h, upscale, autoresize);
}

void videoCopyFramebuffer(s32 dst, s32 src, s32 left, s32 top)
{
    // assume immediate copies always read the front buffer
    gfx_copy_framebuffer(dst, src, left, top, false);
}

void videoResetTextureCache(void)
{
    gfx_texture_cache_clear();
}

void videoFreeCachedTexture(const void *texptr)
{
    gfx_texture_cache_delete(texptr);
}

void videoShutdown(void)
{
    free(vidModes);
}

PD_CONSTRUCTOR static void videoConfigInit(void)
{
    configRegisterInt("Video.DefaultFullscreen", &vidFullscreen, 0, 1);
    configRegisterInt("Video.DefaultMaximize", &vidMaximize, 0, 1);
    configRegisterInt("Video.ExclusiveFullscreen", &vidFullscreenExclusive, 0, 1);
    configRegisterInt("Video.CenterWindow", &vidCenter, 0, 1);
    configRegisterInt("Video.AllowHiDpi", &vidAllowHiDpi, 0, 1);
    configRegisterInt("Video.VSync", &vidVsync, -1, 10);
    configRegisterInt("Video.FramebufferEffects", &vidFramebuffers, 0, 1);
    configRegisterInt("Video.FramerateLimit", &vidFramerateLimit, 0, VIDEO_MAX_FPS);
    configRegisterInt("Video.DisplayFPS", &vidDisplayFPS, 0, 1);
    configRegisterFloat("Video.DisplayFPSInterval", &vidDisplayFPSInterval, 0.01f, 32.f);
    configRegisterInt("Video.MSAA", &vidMSAA, 1, 16);
    configRegisterInt("Video.TextureFilter", &texFilter, 0, 2);
    configRegisterInt("Video.TextureFilter2D", &texFilter2D, 0, 1);
    configRegisterInt("Video.DetailTextures", &texDetail, 0, 1);
    // VR: the eye render resolution is derived from RENDER_SCALE, so this is the key that
    // makes the Extended menu's Resolution choice survive a restart. configInit() runs long
    // before vr_initialize(), so the saved scale is already in place when the swapchains are
    // first sized -- no restart and no resolution pop on startup.
    configRegisterFloat("Video.VRRenderScale", &RENDER_SCALE, 0.5f, 4.f);
}
