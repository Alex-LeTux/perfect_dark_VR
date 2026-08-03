#include <stdio.h>
#include <SDL.h>
#include <unistd.h>
#include <time.h>


#include "platform.h"
#include "system.h"

#include "gfx_window_manager_api.h"
#include "gfx_screen_config.h"


#include "../vr/vr_log.h"


#ifdef ANDROID

#include <EGL/egl.h>
#include <android/log.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "PD-VR", __VA_ARGS__)
#endif

#ifndef ANDROID // if PC
#define LOGI(...) printf(__VA_ARGS__)
#include <GL/glew.h>
#include "../vr/imgui/imgui.h"
#include "../vr/imgui/imgui_impl_sdl2.h"
#include "../vr/imgui/imgui_impl_opengl3.h"

extern "C" bool vrWaitForRuntime(int waitSeconds);
bool wait4vr = false;

// --- VR additions for the mirror window ---
SDL_Window*    mirror_wnd  = nullptr;
SDL_GLContext  mirror_ctx  = nullptr;
int mirror_width  = 916;
int mirror_height = 960;

static int  mirror_eye_index   = 0;
static bool mirror_sbs = false;  // false = one eye, true = side by side
static bool imgui_initialized  = false;
static bool mirror_is_43  = false;  // false = 1:1 VR, true = 4:3
static float toolbar_alpha     = 1.0f;   // current opacity
static uint32_t last_mouse_move_ms = 0;  // timestamp of the last mouse movement
#define TOOLBAR_HIDE_DELAY_MS 3000       // 3 seconds without movement → disappears

bool mirror_enabled     = false;
static int  mirror_saved_w     = 916;
static int  mirror_saved_h     = 960;

extern "C" void  gfx_opengl_load_mirror_logo(const char* path);
extern "C" unsigned int gfx_opengl_get_logo_tex();
extern int logo_w;
extern int logo_h;

#endif

extern "C" bool vr_configure_resolution();
extern uint32_t VrRecommendedW;
extern uint32_t VrRecommendedH;
extern uint32_t g_internalRenderWidth;
extern uint32_t g_internalRenderHeight;

SDL_Window *wnd;
SDL_GLContext ctx;
static SDL_Renderer *renderer;
static int sdl_to_lus_table[512];
static bool vsync_enabled = true;
// OTRTODO: These are redundant. Info can be queried from SDL.
//static int window_width = DESIRED_SCREEN_WIDTH;
//static int window_height = DESIRED_SCREEN_HEIGHT;

static int window_width = -1;
static int window_height = -1;

static uint32_t fullscreen_flag = SDL_WINDOW_FULLSCREEN_DESKTOP;
static bool fullscreen_state;
static bool maximized_state;
static bool is_running = true;

static void (*on_fullscreen_changed_callback)(bool is_now_fullscreen);

static int target_fps = 120; // above 60 since vsync is enabled by default
static uint64_t previous_time;
static uint64_t qpc_freq;

#define FRAME_INTERVAL_US_NUMERATOR 1000000
#define FRAME_INTERVAL_US_DENOMINATOR (target_fps)

static int32_t gfx_sdl_get_maximized_state(void) {
    return (int32_t) maximized_state;
}

static int32_t gfx_sdl_get_fullscreen_state(void) {
    return (int32_t) fullscreen_state;
}

static int32_t gfx_sdl_get_fullscreen_flag_mode(void) {
    return fullscreen_flag == SDL_WINDOW_FULLSCREEN_DESKTOP ? 0 : 1;
}

static void gfx_sdl_set_fullscreen_flag(int32_t mode) {
    switch (mode) {
        case 0: {
            fullscreen_flag = SDL_WINDOW_FULLSCREEN_DESKTOP;
        }
            break;
        case 1: {
            fullscreen_flag = SDL_WINDOW_FULLSCREEN;
        }
            break;
    }
}

static void set_fullscreen(bool on, bool call_callback) {
    fullscreen_state = on;
    SDL_SetWindowFullscreen(wnd, on ? fullscreen_flag : 0);
    if (call_callback && on_fullscreen_changed_callback) {
        on_fullscreen_changed_callback(on);
    }
}

static void set_maximize_window(bool on) {
    maximized_state = on;
    if (on) {
        SDL_MaximizeWindow(wnd);
    } else {
        SDL_RestoreWindow(wnd);
    }
}

static void gfx_sdl_get_active_window_refresh_rate(uint32_t *refresh_rate) {
    int display_in_use = SDL_GetWindowDisplayIndex(wnd);

    SDL_DisplayMode mode;
    SDL_GetCurrentDisplayMode(display_in_use, &mode);
    *refresh_rate = mode.refresh_rate;
}




#ifndef ANDROID // if PC
extern "C" void vrShowWaitingWindow(const char *bmpPath) {
    // Initialize SDL if not already
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0)
        SDL_Init(SDL_INIT_VIDEO);

    // Créer la fenêtre et le contexte GL
    SDL_Window *waitWnd = SDL_CreateWindow("Perfect Dark VR",
                                           SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                           220, 40, SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS);
    SDL_GLContext waitCtx = SDL_GL_CreateContext(waitWnd);
    SDL_GL_MakeCurrent(waitWnd, waitCtx);



    // Init ImGui
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(waitWnd, waitCtx);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Font
    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->Clear();
    ImFontConfig config;
    config.SizePixels = 16.0f;
    io.Fonts->AddFontDefault(&config);

    // load image logo
    GLuint waitLogoTex = 0;
    int waitLogoW = 0, waitLogoH = 0;
    SDL_Surface *surf = SDL_LoadBMP(bmpPath);
    if (surf) {
        waitLogoW = surf->w;
        waitLogoH = surf->h;
        SDL_SetWindowSize(waitWnd, waitLogoW, waitLogoH + 40);

        SDL_SetWindowPosition(waitWnd, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

        glGenTextures(1, &waitLogoTex);
        glBindTexture(GL_TEXTURE_2D, waitLogoTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, waitLogoW, waitLogoH, 0, GL_BGR, GL_UNSIGNED_BYTE, surf->pixels);
        SDL_FreeSurface(surf);
    }


    // loop until runtime is ready
    bool runtimeReady = false;
    while (!runtimeReady) {
        Uint32 frameStart = SDL_GetTicks();
        while (SDL_GetTicks() - frameStart < 1000) {
            SDL_Event event;
            while (SDL_PollEvent(&event))
                if (event.type == SDL_QUIT) exit(0);

            float alpha = (sinf(SDL_GetTicks() / 500.0f) + 1.0f) * 0.5f;

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(waitLogoW, waitLogoH + 40));
            ImGui::SetNextWindowBgAlpha(1.0f);
            ImGui::Begin("##w", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
            if (waitLogoTex)
                ImGui::Image((ImTextureID)(uintptr_t)waitLogoTex, ImVec2(waitLogoW, waitLogoH));

// X button in the top-right corner — absolute position
            ImGui::SetCursorPos(ImVec2(waitLogoW - 26, 2));
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
            if (ImGui::Button("X##close", ImVec2(24, 24)))
                exit(0);
            ImGui::PopStyleColor(2);

// Waiting for VR connection text
            ImGui::SetCursorPos(ImVec2(0, waitLogoH + 16));
            const char *text = "Waiting for VR connection...";
            float textWidth = ImGui::CalcTextSize(text).x;
            ImGui::SetCursorPosX((waitLogoW - textWidth) * 0.5f);
            ImGui::TextColored(ImVec4(1, 1, 1, alpha), text);

            ImGui::End();
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            SDL_GL_SwapWindow(waitWnd);
            SDL_Delay(16);

            while (SDL_PollEvent(&event)) {
                ImGui_ImplSDL2_ProcessEvent(&event);
                if (event.type == SDL_QUIT) exit(0);
            }
        }
        runtimeReady = vrWaitForRuntime(1);
    }

    // Clean
    if (waitLogoTex)
        glDeleteTextures(1, &waitLogoTex);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(waitCtx);
    SDL_DestroyWindow(waitWnd);
}
#endif




static void gfx_sdl_init(const struct GfxWindowInitSettings *set) {

    window_width = VrRecommendedW;
    window_height = VrRecommendedH;

    vr_log("SDL: window size: %dx%d", window_width, window_height);

#ifdef SDL_HINT_VIDEO_HIGHDPI_DISABLED
    if (!set->allow_hidpi) {
        // HiDPI control, if available
        SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "1");
#if defined(PLATFORM_WIN32) && defined(SDL_HINT_WINDOWS_DPI_AWARENESS)
        // if HiDPI is disabled, declare ourselves DPI aware to get 1:1 window size on Windows
        SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitor");
#endif
    }
#endif

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        sysFatalError("Could not init SDL:\n%s", SDL_GetError());
    }

    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);


    if (sysArgCheck("--debug-gl")) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
    }


    int posX = set->x;
    int posY = set->y;
    int display_in_use = SDL_GetWindowDisplayIndex(wnd);
    if (display_in_use < 0) { // Fallback to default if out of bounds
        posX = SDL_WINDOWPOS_UNDEFINED;
        posY = SDL_WINDOWPOS_UNDEFINED;
    }

    if (set->centered) {
        SDL_DisplayMode mode = {};
        SDL_GetCurrentDisplayMode(0, &mode);
        posX = mode.w / 2 - window_width / 2;
        posY = mode.h / 2 - window_height / 2;
    }

    if (set->fullscreen_is_exclusive) {
        fullscreen_flag = SDL_WINDOW_FULLSCREEN;
    }

    // we will unhide the window once the GL context is successfully created
    Uint32 flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL;

    // if fullscreen was requested, start the window in fullscreen right away
    if (set->fullscreen) {
        flags |= fullscreen_flag;
        fullscreen_state = true;
    }


    if (set->maximized) {
        flags |= SDL_WINDOW_MAXIMIZED;
        maximized_state = true;
    }

#ifdef SDL_WINDOW_ALLOW_HIGHDPI
    if (set->allow_hidpi) {
        flags |= SDL_WINDOW_ALLOW_HIGHDPI;
    }
#endif

    // ideally we need 3.0 compat
    // if that doesn't work, try 3.2 core in case we're on mac, 2.1 compat as a last resort
    static u32 glver[][3] = {
            {0, 0, 0}, // for command line override
            {3, 0, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY}, // 3.0: default, has all the features required
            {4, 1, SDL_GL_CONTEXT_PROFILE_CORE}, // 4.1core: macs only have core profile and this is the latest
            {3, 2, SDL_GL_CONTEXT_PROFILE_CORE}, // 3.2core: older macs will only have this at best
            {3, 0, SDL_GL_CONTEXT_PROFILE_ES}, // es3: don't really support ES properly, but we can try
            {2, 1, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY}, // 2.1: absolute last resort, will still require GLSL130 as an extension
    };

    u32 verstart = 1;
    const u32 verend = sizeof(glver) / sizeof(*glver);
    const char *verstr = sysArgGetString("--gl-version");
    if (verstr && *verstr) {
        // user override
        glver[0][2] = strstr(verstr, "core") ? SDL_GL_CONTEXT_PROFILE_CORE :
                      (strstr(verstr, "es") ? SDL_GL_CONTEXT_PROFILE_ES :
                       SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        sscanf(verstr, "%d.%d", &glver[0][0], &glver[0][1]);
        if (glver[0][0] >= 1 && glver[0][0] <= 4 && glver[0][1] < 9) {
            verstart = 0;
        }
    }

    ctx = NULL;
    u32 vmin = 0, vmaj = 0, vprof = SDL_GL_CONTEXT_PROFILE_COMPATIBILITY;
    const char *vprofstr = "";
    for (u32 i = verstart; i < verend && !ctx; ++i) {
        vmaj = glver[i][0];
        vmin = glver[i][1];
        vprof = glver[i][2];
        vprofstr = (vprof == SDL_GL_CONTEXT_PROFILE_CORE ? "core" :
                    (vprof == SDL_GL_CONTEXT_PROFILE_ES ? "es" : ""));

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, vmaj);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, vmin);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, vprof);

        wnd = SDL_CreateWindow(set->title, posX, posY, window_width, window_height, flags);
        if (!wnd) {
            sysLogPrintf(LOG_WARNING, "SDL: could not open SDL window for GL%d.%d%s:\n%s", vmaj,
                         vmin, vprofstr, SDL_GetError());
            continue;
        }

        ctx = SDL_GL_CreateContext(wnd);
        if (!ctx) {
            sysLogPrintf(LOG_WARNING, "SDL: could not create GL%d.%d%s context: %s", vmaj, vmin,
                         vprofstr, SDL_GetError());
            SDL_DestroyWindow(wnd);
            wnd = nullptr;
        }


    }

    if (!wnd || !ctx) {
        sysFatalError(
                "Could not open SDL window with an OpenGL context of any supported version:\n%s",
                SDL_GetError());
    } else {
        sysLogPrintf(LOG_NOTE, "SDL: created GL%d.%d%s context", vmaj, vmin, vprofstr);
    }


//    SDL_GL_MakeCurrent(wnd, ctx); // removed for VR
//    SDL_GL_SetSwapInterval(1); // removed for VR
//    SDL_ShowWindow(wnd);// removed for VR

#ifndef ANDROID
    SDL_GL_MakeCurrent(wnd, ctx);
    SDL_GL_SetSwapInterval(0); // vsync OFF

// Hide the main window — it is only used for the GL/OpenXR
    SDL_HideWindow(wnd);

// --- Create the mirror window by sharing the same GL context ---
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);

    mirror_wnd = SDL_CreateWindow(
            "Perfect Dark VR - Mirror",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            mirror_width, mirror_height,           // same as logo image size + toolbar height
            SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN
    );


    if (mirror_wnd) {
        mirror_ctx = SDL_GL_CreateContext(mirror_wnd);
        if (!mirror_ctx) {
            vr_log("SDL: could not create mirror GL context: %s", SDL_GetError());
            SDL_DestroyWindow(mirror_wnd);
            mirror_wnd = nullptr;
        } else {
            // Switch back to the main context for VR rendering
            SDL_GL_MakeCurrent(wnd, ctx);
            SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
            vr_log( "SDL: mirror window created (%dx%d)", mirror_width, mirror_height);
        }
    }


    if (mirror_wnd && mirror_ctx) {
        SDL_GL_MakeCurrent(mirror_wnd, mirror_ctx);
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::StyleColorsDark();
        ImGui_ImplSDL2_InitForOpenGL(mirror_wnd, mirror_ctx);
        ImGui_ImplOpenGL3_Init("#version 330");
        imgui_initialized = true;

        SDL_GL_MakeCurrent(wnd, ctx); // Switch back to the main context

    }

#endif

    qpc_freq = SDL_GetPerformanceFrequency();

}

static void gfx_sdl_close(void) {
    is_running = false;
#ifndef ANDROID
    if (imgui_initialized) {
        SDL_GL_MakeCurrent(mirror_wnd, mirror_ctx);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        SDL_GL_MakeCurrent(wnd, ctx);
    }
#endif
}

static void
gfx_sdl_set_fullscreen_changed_callback(void (*on_fullscreen_changed)(bool is_now_fullscreen)) {
    on_fullscreen_changed_callback = on_fullscreen_changed;
}

static void gfx_sdl_set_fullscreen(bool enable) {
    set_fullscreen(enable, true);
}

static void gfx_sdl_set_fullscreen_exclusive(bool enable) {
    const uint32_t newflag = enable ? SDL_WINDOW_FULLSCREEN : SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (fullscreen_flag != newflag) {
        fullscreen_flag = newflag;
        // reset fullscreen to take new value into account if it already is in fullscreen
        if (fullscreen_state) {
            fullscreen_state = false;
            set_fullscreen(enable, true);
        }
    }
}

static void gfx_sdl_set_maximize_window(bool enable) {
    set_maximize_window(enable);
}

static void gfx_sdl_set_cursor_visibility(bool visible) {
    if (visible) {
        SDL_ShowCursor(SDL_ENABLE);
    } else {
        SDL_ShowCursor(SDL_DISABLE);
    }
}

static void
get_centered_positions_native(int32_t width, int32_t height, int32_t *posX, int32_t *posY) {
    const int disp_idx = SDL_GetWindowDisplayIndex(wnd);
    SDL_DisplayMode mode = {};
    SDL_GetDesktopDisplayMode(disp_idx, &mode);
    *posX = mode.w / 2 - width / 2;
    *posY = mode.h / 2 - height / 2;
}

static void
gfx_sdl_get_centered_positions(int32_t width, int32_t height, int32_t *posX, int32_t *posY) {
    const int disp_idx = SDL_GetWindowDisplayIndex(wnd);
    SDL_DisplayMode mode = {};
    SDL_GetCurrentDisplayMode(disp_idx, &mode);
    *posX = mode.w / 2 - width / 2;
    *posY = mode.h / 2 - height / 2;
}

static void gfx_sdl_set_closest_resolution(int32_t width, int32_t height, bool should_center) {
    const SDL_DisplayMode mode = {.w = width, .h = height};
    const int disp_idx = SDL_GetWindowDisplayIndex(wnd);
    SDL_DisplayMode closest = {};
    if (SDL_GetClosestDisplayMode(disp_idx, &mode, &closest)) {
        SDL_SetWindowDisplayMode(wnd, &closest);
        SDL_SetWindowSize(wnd, closest.w, closest.h);
        if (should_center) {
            int32_t posX = 0;
            int32_t posY = 0;
            get_centered_positions_native(closest.w, closest.h, &posX, &posY);
            SDL_SetWindowPosition(wnd, posX, posY);
        }
    }
}


static void gfx_sdl_set_dimensions(uint32_t width, uint32_t height, int32_t posX, int32_t posY) {
    SDL_SetWindowSize(wnd, width, height);
    SDL_SetWindowPosition(wnd, posX, posY);
}

static void
gfx_sdl_get_dimensions(uint32_t *width, uint32_t *height, int32_t *posX, int32_t *posY) {
    SDL_GL_GetDrawableSize(wnd, static_cast<int *>((void *) width),
                           static_cast<int *>((void *) height));
    SDL_GetWindowPosition(wnd, static_cast<int *>(posX), static_cast<int *>(posY));
}

static void gfx_sdl_handle_events(void) {
    SDL_Event event;
#ifndef ANDROID // if PC
    while (SDL_PollEvent(&event)) {

        if (imgui_initialized)
            ImGui_ImplSDL2_ProcessEvent(&event);

        switch (event.type) {
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_RETURN && (event.key.keysym.mod & KMOD_ALT)) {
                    set_fullscreen(!fullscreen_state, true);
                }
                break;
            case SDL_WINDOWEVENT:
                if (mirror_wnd && mirror_enabled &&
                    event.window.windowID == SDL_GetWindowID(mirror_wnd) &&
                    event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {

                    if (!mirror_enabled) break;

                    int new_w = event.window.data1;
                    int new_h = event.window.data2;

                    // Ratio
                    float target_ratio;
                    if (mirror_sbs) {
                        target_ratio = 2.0f;        // Two eyes 1:1 side by side = 2:1 ~ 16/9
                    } else if (mirror_is_43) {
                        target_ratio = 4.0f / 3.0f;
                    } else {
                        target_ratio = 1.0f;        // 1:1 VR
                    }

                    float current_ratio = (float) new_w / (float) new_h;
                    int corrected_w = new_w;
                    int corrected_h = new_h;

                    if (current_ratio > target_ratio) {
                        corrected_w = (int) (new_h * target_ratio);
                    } else {
                        corrected_h = (int) (new_w / target_ratio);
                    }
                    if (corrected_w != new_w || corrected_h != new_h) {
                        SDL_SetWindowSize(mirror_wnd, corrected_w, corrected_h);
                    }

                    SDL_GL_GetDrawableSize(mirror_wnd, &mirror_width, &mirror_height);
                }
                    // Closing the mirror window → q
                else if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                    exit(0);
                }
                // Main window: ignore SDL_WINDOWEVENT_SIZE_CHANGED
                // because it is hidden and should not change size
                break;
            case SDL_MOUSEMOTION:
                last_mouse_move_ms = SDL_GetTicks();
                break;
            case SDL_QUIT:
                exit(0);
                break;
        }
    }
#endif
}


static bool gfx_sdl_start_frame(void) {
    return true;
}

static uint64_t qpc_to_100ns(uint64_t qpc) {
    return qpc / qpc_freq * 10000000 + qpc % qpc_freq * 10000000 / qpc_freq;
}

static inline void sync_framerate_with_timer(void) {
    uint64_t t;
    t = qpc_to_100ns(SDL_GetPerformanceCounter());

    const int64_t next =
            previous_time + 10 * FRAME_INTERVAL_US_NUMERATOR / FRAME_INTERVAL_US_DENOMINATOR;
    int64_t left = next - t;
    // We want to exit a bit early, so we can busy-wait the rest to never miss the deadline
    left -= 15000UL;
    if (left > 0) {
        sysSleep(left);
    }

    do {
        sysCpuRelax();
        t = qpc_to_100ns(SDL_GetPerformanceCounter());
    } while ((int64_t) t < next);

    t = qpc_to_100ns(SDL_GetPerformanceCounter());
    if (left > 0 && t - next < 10000) {
        // In case it takes some time for the application to wake up after sleep,
        // or inaccurate timer,
        // don't let that slow down the framerate.
        t = next;
    }
    previous_time = t;
}


#ifndef ANDROID // if PC

extern "C" void gfx_sdl_get_mirror_dimensions(int* w, int* h) { // VR
    if (mirror_wnd) {
        SDL_GL_GetDrawableSize(mirror_wnd, w, h);
    } else {
        *w = 0;
        *h = 0;
    }
}

extern "C" int gfx_sdl_get_mirror_eye() { // VR
    return mirror_eye_index;
}

extern "C" bool gfx_sdl_is_mirror_enabled() {
    return mirror_enabled;
}

extern "C" bool gfx_sdl_is_mirror_sbs() {
    return mirror_sbs;
}

extern "C" void mirror_apply_size(bool enabled) {
    if (enabled) {
  // Restore the saved size
        SDL_SetWindowSize(mirror_wnd, mirror_saved_w, mirror_saved_h);
        SDL_SetWindowResizable(mirror_wnd, SDL_TRUE);
    } else {
// Save the current size then collapse
        mirror_saved_w = mirror_width;
        mirror_saved_h = mirror_height;
        SDL_SetWindowResizable(mirror_wnd, SDL_FALSE); // Prevent resizing in collapsed mode
        gfx_opengl_load_mirror_logo("logo.bmp");
        SDL_SetWindowSize(mirror_wnd, (float)logo_w, (float)logo_h + 36); // 36 is toolbar height
        SDL_ShowWindow(mirror_wnd);
    }
}

#endif

static void gfx_sdl_swap_buffers_begin(void) {
    if (target_fps) {
        sync_framerate_with_timer();
    }



#ifndef ANDROID

    if (mirror_wnd && mirror_ctx) {
    SDL_GL_MakeCurrent(mirror_wnd, mirror_ctx);

if (imgui_initialized) {
    uint32_t now = SDL_GetTicks();
    uint32_t idle_ms = now - last_mouse_move_ms;

    if (idle_ms < TOOLBAR_HIDE_DELAY_MS) {
        float fade_start = (float)(TOOLBAR_HIDE_DELAY_MS - 500);
        float t = ((float)idle_ms - fade_start) / 500.0f;
        toolbar_alpha = 1.0f - fmaxf(0.0f, fminf(1.0f, t));
    } else {
        toolbar_alpha = 0.0f;
    }

    // Toolbar always visible when mirror is OFF (no fade)
    if (!mirror_enabled) toolbar_alpha = 1.0f;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    if (toolbar_alpha > 0.0f) {
        ImGui::SetNextWindowPos({0, 0});
        // Full window height when mirror is OFF, otherwise only the toolbar
        ImGui::SetNextWindowSize({(float)mirror_width, (float)(mirror_enabled ? 36 : mirror_height)});
        ImGui::SetNextWindowBgAlpha(mirror_enabled ? 0.75f * toolbar_alpha : 0.95f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, toolbar_alpha);

        ImGui::Begin("##toolbar", nullptr,
            ImGuiWindowFlags_NoTitleBar      |
            ImGuiWindowFlags_NoResize        |
            ImGuiWindowFlags_NoMove          |
            ImGuiWindowFlags_NoScrollbar     |
            ImGuiWindowFlags_NoSavedSettings |
            (toolbar_alpha < 0.1f ? ImGuiWindowFlags_NoInputs : 0));

        // ── 1) Mirror ON/OFF button (first) ──
        bool was_enabled = mirror_enabled;
        ImGui::PushStyleColor(ImGuiCol_Button,
            mirror_enabled
                ? ImVec4(0.2f, 0.6f, 0.2f, 1.0f)   // Color = ON
                : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));  // Color = OFF
        if (ImGui::Button(mirror_enabled ? "Mirror ON" : "Mirror OFF")) {
            mirror_enabled = !mirror_enabled;
            mirror_apply_size(mirror_enabled);
        }
        ImGui::PopStyleColor();


        unsigned int logo_tex = gfx_opengl_get_logo_tex();
        if (logo_tex && !mirror_enabled) {
            // ── FPS whene mirror OFF ──
            ImGui::SameLine(0, 20);
            ImGui::Text("%.0f fps", ImGui::GetIO().Framerate);
            //---------------------------------------------

             // logo image
            ImVec2 win_pos = ImGui::GetWindowPos();
            // Offset by 36px downward (toolbar height)
            ImVec2 img_min = ImVec2(win_pos.x, win_pos.y + 36.0f);
            ImVec2 img_max = ImVec2(win_pos.x + (float)logo_w, win_pos.y + 36.0f + (float)logo_h);

            ImGui::GetWindowDrawList()->AddImage(
                (ImTextureID)(uintptr_t)logo_tex,
                img_min,
                img_max,
                ImVec2(0, 0), ImVec2(1, 1)
            );

            ImGui::Dummy(ImVec2((float)logo_w, (float)logo_h + 36.0f));
        }


        /// Other buttons only when mirror is ON
        if (mirror_enabled) {
            ImGui::SameLine(0, 20);

        // ── 2) Toggle eyes / SbS ──
        ImGui::SameLine(0, 20);
        ImGui::Text("Eye:");
        ImGui::SameLine();

        int eye_mode = mirror_sbs ? 2 : mirror_eye_index;

        if (ImGui::RadioButton("L", eye_mode == 0)) {
            if (mirror_sbs) SDL_SetWindowSize(mirror_wnd, mirror_saved_w, mirror_saved_h);
            mirror_eye_index = 0;
            mirror_sbs = false;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("R", eye_mode == 1)) {
            if (mirror_sbs) SDL_SetWindowSize(mirror_wnd, mirror_saved_w, mirror_saved_h);
            mirror_eye_index = 1;
            mirror_sbs = false;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("SbS", eye_mode == 2)) {
            mirror_sbs = true;
            mirror_saved_w = mirror_width;
            mirror_saved_h = mirror_height;
            SDL_SetWindowSize(mirror_wnd, mirror_height * 2, mirror_height);
        }

        // ── 3) Toggle format
        ImGui::SameLine(0, 20);
        if (mirror_sbs) ImGui::BeginDisabled();

            const char* format_label = mirror_sbs
                ? "Format: SbS 16/9"
                : (mirror_is_43 ? "Format: 4:3" : "Format: 1:1 VR");

            if (ImGui::Button(format_label)) {
                mirror_is_43 = !mirror_is_43;
                int h = mirror_height;
                int new_w = mirror_is_43 ? (int)(h * 4.0f / 3.0f) : h;
                SDL_SetWindowSize(mirror_wnd, new_w, h);
            }

            if (mirror_sbs) ImGui::EndDisabled();

            ImGui::SameLine(0, 20);

            // ── 4) FPS ──
            ImGui::Text("%.0f fps", ImGui::GetIO().Framerate);
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    }

    SDL_GL_SetSwapInterval(0);
    SDL_GL_SwapWindow(mirror_wnd);
    SDL_GL_MakeCurrent(wnd, ctx);
}



#endif
}


static void gfx_sdl_swap_buffers_end(void) {

}

static double gfx_sdl_get_time(void) {
    return SDL_GetPerformanceCounter() / (double) qpc_freq;
}

static int32_t gfx_sdl_get_target_fps(void) {
    return target_fps;
}

static void gfx_sdl_set_target_fps(int fps) {
    target_fps = fps;
}

static bool gfx_sdl_can_disable_vsync(void) {
    return true;
}

static void *gfx_sdl_get_window_handle(void) {
    return (void *) wnd;
}

static void gfx_sdl_set_window_title(const char *title) {
    SDL_SetWindowTitle(wnd, title);
}

static int gfx_sdl_get_swap_interval(void) {
    return SDL_GL_GetSwapInterval();
}

static bool gfx_sdl_set_swap_interval(int interval) {
    const bool success = SDL_GL_SetSwapInterval(interval) >= 0;
    vsync_enabled = success && (interval != 0);
    if (!success) {
        sysLogPrintf(LOG_WARNING, "SDL: failed to set vsync %d: %s", interval, SDL_GetError());
    }
    return success;
}

int gfx_sdl_get_display_mode(int modenum, int *out_w, int *out_h) {
    const int display_in_use = SDL_GetWindowDisplayIndex(wnd);
    SDL_DisplayMode sdlmode;
    if (SDL_GetDisplayMode(display_in_use, modenum, &sdlmode) == 0) {
        *out_w = g_internalRenderWidth ;
        *out_h = g_internalRenderHeight;

        return 1;
    }
    return 0;
}

int gfx_sdl_get_current_display_mode(int *out_w, int *out_h) {
    const int display_in_use = SDL_GetWindowDisplayIndex(wnd);
    SDL_DisplayMode sdlmode;
    if (SDL_GetCurrentDisplayMode(display_in_use, &sdlmode) == 0) {
        *out_w = g_internalRenderWidth ; // VR
        *out_h = g_internalRenderHeight;
        return 1;
    }
    return 0;
}

int gfx_sdl_get_num_display_modes(void) {
    const int display_in_use = SDL_GetWindowDisplayIndex(wnd);
    return SDL_GetNumDisplayModes(display_in_use);
}


struct GfxWindowManagerAPI gfx_sdl = {
        gfx_sdl_init,
        gfx_sdl_close,
        gfx_sdl_get_display_mode,
        gfx_sdl_get_current_display_mode,
        gfx_sdl_get_num_display_modes,
        gfx_sdl_get_fullscreen_state,
        gfx_sdl_set_fullscreen_changed_callback,
        gfx_sdl_set_fullscreen,
        gfx_sdl_set_fullscreen_exclusive,
        gfx_sdl_set_fullscreen_flag,
        gfx_sdl_get_fullscreen_flag_mode,
        gfx_sdl_get_maximized_state,
        gfx_sdl_set_maximize_window,
        gfx_sdl_get_active_window_refresh_rate,
        gfx_sdl_set_cursor_visibility,
        gfx_sdl_set_closest_resolution,
        gfx_sdl_set_dimensions,
        gfx_sdl_get_dimensions,
        gfx_sdl_get_centered_positions,
        gfx_sdl_handle_events,
        gfx_sdl_start_frame,
        gfx_sdl_swap_buffers_begin,
        gfx_sdl_swap_buffers_end,
        gfx_sdl_get_time,
        gfx_sdl_get_target_fps,
        gfx_sdl_set_target_fps,
        gfx_sdl_can_disable_vsync,
        gfx_sdl_get_window_handle,
        gfx_sdl_set_window_title,
        gfx_sdl_get_swap_interval,
        gfx_sdl_set_swap_interval,
};
