/* Removed for VR
#ifdef ANDROID

#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include <android/log.h>
#include <time.h>

#include "gfx_window_manager_api.h"
#include "gfx_screen_config.h"

#define LOG_TAG "PerfectDark-GFX"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static EGLDisplay egl_display = EGL_NO_DISPLAY;
static EGLContext egl_context = EGL_NO_CONTEXT;
static EGLSurface egl_surface = EGL_NO_SURFACE;
static int screen_width = 1920;
static int screen_height = 1080;

static void gfx_android_init(const struct GfxWindowInitSettings *settings) {
    LOGI("Android GFX init: %dx%d", settings->width, settings->height);
    
    screen_width = settings->width;
    screen_height = settings->height;
    
    // EGL is already initialized by the Java side
    // We just need to get the current context
    egl_display = eglGetCurrentDisplay();
    egl_context = eglGetCurrentContext();
    egl_surface = eglGetCurrentSurface(EGL_DRAW);
    
    if (egl_display == EGL_NO_DISPLAY || egl_context == EGL_NO_CONTEXT || egl_surface == EGL_NO_SURFACE) {
        LOGE("Failed to get current EGL context");
        return;
    }
    
    LOGI("EGL context acquired successfully");
    
    // Enable depth testing
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    
    // Enable blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Set clear color
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
}

static void gfx_android_close(void) {
    LOGI("Android GFX close");
}

static int gfx_android_get_display_mode(int modenum, int *out_w, int *out_h) {
    if (modenum == 0) {
        *out_w = screen_width;
        *out_h = screen_height;
        return 1;
    }
    return 0;
}

static int gfx_android_get_current_display_mode(int *out_w, int *out_h) {
    *out_w = screen_width;
    *out_h = screen_height;
    return 1;
}

static int gfx_android_get_num_display_modes(void) {
    return 1;
}

static int32_t gfx_android_get_fullscreen_state(void) {
    return 1; // Always fullscreen on Android
}

static void gfx_android_set_fullscreen_changed_callback(void (*on_fullscreen_changed)(bool is_now_fullscreen)) {
    // Not applicable on Android - always fullscreen
}

static void gfx_android_set_fullscreen(bool enable) {
    // Not applicable on Android - always fullscreen
}

static void gfx_android_set_fullscreen_exclusive(bool exc) {
    // Not applicable on Android - always fullscreen
}

static void gfx_android_set_fullscreen_flag(int32_t mode) {
    // Not applicable on Android
}

static int32_t gfx_android_get_fullscreen_flag_mode(void) {
    return 1;
}

static int32_t gfx_android_get_maximized_state(void) {
    return 1; // Always maximized on Android
}

static void gfx_android_set_maximize(bool enable) {
    // Not applicable on Android
}

static void gfx_android_get_active_window_refresh_rate(uint32_t* refresh_rate) {
    *refresh_rate = 60; // Default to 60Hz
}

static void gfx_android_set_cursor_visibility(bool visible) {
    // Not applicable on Android
}

static void gfx_android_set_closest_resolution(int32_t width, int32_t height, bool should_center) {
    // Not applicable on Android
}

static void gfx_android_set_dimensions(uint32_t width, uint32_t height, int32_t posX, int32_t posY) {
    screen_width = width;
    screen_height = height;
}

static void gfx_android_get_dimensions(uint32_t *width, uint32_t *height, int32_t *posX, int32_t *posY) {
    *width = screen_width;
    *height = screen_height;
    *posX = 0;
    *posY = 0;
}

static void gfx_android_get_centered_positions(int32_t width, int32_t height, int32_t *posX, int32_t *posY) {
    *posX = 0;
    *posY = 0;
}

static void gfx_android_handle_events(void) {
    // Events are handled through JNI callbacks
}

static bool gfx_android_start_frame(void) {
    return true;
}

static void gfx_android_swap_buffers_begin(void) {
    // Buffer swapping is handled by the GLSurfaceView
}

static void gfx_android_swap_buffers_end(void) {
    // Buffer swapping is handled by the GLSurfaceView
}

static double gfx_android_get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static int32_t gfx_android_get_target_fps(void) {
    return 60;
}

static void gfx_android_set_target_fps(int fps) {
    // Not applicable on Android
}

static bool gfx_android_can_disable_vsync(void) {
    return false;
}

static void *gfx_android_get_window_handle(void) {
    return NULL;
}

static void gfx_android_set_window_title(const char *title) {
    // Not applicable on Android
}

static int gfx_android_get_swap_interval(void) {
    return 1;
}

static bool gfx_android_set_swap_interval(int interval) {
    return false;
}

struct GfxWindowManagerAPI gfx_android_api = {
    gfx_android_init,
    gfx_android_close,
    gfx_android_get_display_mode,
    gfx_android_get_current_display_mode,
    gfx_android_get_num_display_modes,
    gfx_android_get_fullscreen_state,
    gfx_android_set_fullscreen_changed_callback,
    gfx_android_set_fullscreen,
    gfx_android_set_fullscreen_exclusive,
    gfx_android_set_fullscreen_flag,
    gfx_android_get_fullscreen_flag_mode,
    gfx_android_get_maximized_state,
    gfx_android_set_maximize,
    gfx_android_get_active_window_refresh_rate,
    gfx_android_set_cursor_visibility,
    gfx_android_set_closest_resolution,
    gfx_android_set_dimensions,
    gfx_android_get_dimensions,
    gfx_android_get_centered_positions,
    gfx_android_handle_events,
    gfx_android_start_frame,
    gfx_android_swap_buffers_begin,
    gfx_android_swap_buffers_end,
    gfx_android_get_time,
    gfx_android_get_target_fps,
    gfx_android_set_target_fps,
    gfx_android_can_disable_vsync,
    gfx_android_get_window_handle,
    gfx_android_set_window_title,
    gfx_android_get_swap_interval,
    gfx_android_set_swap_interval,
};

#endif // ANDROID*/
