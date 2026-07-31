// ============================================================================
// VR OpenXR - Perfect Dark VR By Alex_Le_Tux
// Unified: Android (OpenGL ES + OpenXR) / Windows (OpenGL + OpenXR)
// ============================================================================

#ifdef ANDROID
#  define XR_USE_PLATFORM_ANDROID 1
#  define XR_USE_GRAPHICS_API_OPENGL_ES 1
#else
#  ifndef XR_USE_PLATFORM_WIN32
#    define XR_USE_PLATFORM_WIN32
#  endif
#  ifndef XR_USE_GRAPHICS_API_OPENGL
#    define XR_USE_GRAPHICS_API_OPENGL
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#endif

// ============================================================================
// INCLUDES
// ============================================================================

// Standard Library
#include <cstring>
#include <cmath>
#include <vector>
#include <array>
#include <algorithm>

#ifdef ANDROID
// Android Platform
//#  include <android/log.h>
#  include <android/native_window.h>
#  include <jni.h>
// Graphics APIs
#  include <EGL/egl.h>
#  include <GLES3/gl3.h>
#else
// Windows API
#  include <windows.h>
#  include <wingdi.h>
#  include "../vr/vr_log.h"
// Graphics APIs
#  include "../fast3d/glad/glad.h"
#  include <GL/gl.h>
#  include <cstdio>
#include "vr_runtime_launcher.h"
#endif


#include "../port/fast3d/gfx_rendering_api.h"
#include "../port/fast3d/gfx_pc.h"

// OpenXR Runtime
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

// Project
#include "vr_openxr.h"
#include "vr_input.h"
#include "vr_log.h"


extern "C" struct GfxRenderingAPI* gfx_get_current_rendering_api(void);

// ============================================================================
// LOGGING
// ============================================================================

#define XR_CHECK(call) \
do { \
    XrResult r = (call); \
    if (XR_FAILED(r)) { \
        LOGE("%s failed: %d", #call, (int)r); \
        return; \
    } \
} while (0)

#define XR_CHECK_R(call, retval) \
do { \
    XrResult r = (call); \
    if (XR_FAILED(r)) { \
        LOGE("%s failed: %d", #call, (int)r); \
        return (retval); \
    } \
} while (0)

// ============================================================================
// GLOBAL STATE - Core VR State
// ============================================================================

VRState g_vrState = {};
static bool g_vrInitialized = false;
extern "C" void vr_shutdown();

// ============================================================================
// GLOBAL STATE - others
// ============================================================================
extern bool vr_dl_is_pause_or_menu;
extern bool copy_fbo_menu;
// ============================================================================
// GLOBAL STATE - Platform-specific context
// ============================================================================

#ifdef ANDROID
extern JavaVM*        g_vm;
extern jobject        g_activity;
extern ANativeWindow* g_window;

static EGLDisplay g_sessionEglDisplay = EGL_NO_DISPLAY;
static EGLContext g_sessionEglContext  = EGL_NO_CONTEXT;
static EGLConfig  g_eglConfigForWindow = (EGLConfig)0;
#else
static HGLRC g_xr_hglrc = nullptr;
static HDC   g_xr_hdc   = nullptr;
extern SDL_Window *wnd;
extern SDL_GLContext ctx;
#endif

// ============================================================================
// GLOBAL STATE - Render Targets
// ============================================================================

static float g_eyeProjMtx[2][16] = {};
static float g_eyeViewMtx[2][16] = {};
extern float s_eye_offsets[8];
float XrFov = 0.0f;
float XrAspect = 1.0f;
float g_eyeTanHalfFov[2];
float ipd_meters = 0.0f;;
float VrStereoCrosshair = 0.70f;

uint32_t VrRecommendedW = 0;
uint32_t VrRecommendedH = 0;
int VrSmallW = 0;
int VrSmallH = 0;
int32_t g_internalRenderWidth  = 0;
int32_t g_internalRenderHeight = 0;
float RENDER_SCALE = 1.0f;
extern "C" int vr_get_internal_render_width()  { return g_internalRenderWidth; }
extern "C" int vr_get_internal_render_height() { return g_internalRenderHeight; }
extern "C" s32 videoInitDisplayModes(void);

#ifdef ANDROID
extern "C"
JNIEXPORT jfloat JNICALL
Java_org_libsdl_app_SDLSurface_get_1RENDER_1SCALE(JNIEnv* env, jobject thiz) {
    return RENDER_SCALE;
}

extern "C"
JNIEXPORT jint JNICALL
Java_org_libsdl_app_SDLSurface_get_1targetW(JNIEnv* env, jobject thiz) {
    return VrRecommendedW;
}

extern "C"
JNIEXPORT jint JNICALL
Java_org_libsdl_app_SDLSurface_get_1targetH(JNIEnv* env, jobject thiz) {
    return VrRecommendedH;
}
#endif

// ============================================================================
// GLOBAL STATE - OVR_multiview
// ============================================================================
extern bool     use_multiview;
static uint32_t g_acquiredSwapchainImageIndex = 0;
// Tracks whether a swapchain image is currently checked out from the runtime, so a
// teardown that lands mid-frame can hand it back instead of destroying it underneath.
static bool     g_swapchainImageAcquired     = false;
GLuint          g_multiviewFBO               = 0;
static GLuint   g_multiviewDepthArray        = 0;
GLuint          g_currentMultiviewSwapchainTex = 0;
extern "C" void gfx_opengl_connect_multiview_fbo(GLuint fbo_id, uint32_t width, uint32_t height);

// ============================================================================
// GLOBAL STATE - Depth / scale
// ============================================================================
float g_camZNear = 10.0f;
float g_camZFar  = 10000.0f;
float vr_world_scale = 0.0f;

// ============================================================================
// GLOBAL STATE - MSAA
// ============================================================================
typedef void (*PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC)(GLenum, GLenum, GLuint, GLint, GLint, GLsizei);
extern PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC glFramebufferTextureMultiviewOVR;

typedef void (*PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC)(GLenum, GLenum, GLuint, GLint, GLsizei, GLint, GLsizei);
extern PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC pfnFramebufferTextureMultisampleMultiviewOVR;

extern uint32_t gfx_msaa_level;

// ============================================================================
// GLOBAL STATE - Rendering & Swapchains
// ============================================================================
#ifdef ANDROID
static bool g_swapchainImagesInit[1] = { false };
static std::vector<XrSwapchainImageOpenGLESKHR> g_swapchainImages[1];
#else
static bool g_swapchainImagesInit[1] = { false };
static std::vector<XrSwapchainImageOpenGLKHR> g_swapchainImages[1];
#endif

bool g_frameStarted = false;
static XrFrameState g_frameState   = { XR_TYPE_FRAME_STATE };
static std::array<XrView, 2> g_frameViews = { XrView{XR_TYPE_VIEW}, XrView{XR_TYPE_VIEW} };

static XrReferenceSpaceType gPlaySpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
static bool g_colorSpaceExtSupported = false;

// ============================================================================
// GLOBAL STATE - MENU Rendering & Swapchains
// ============================================================================

static uint32_t g_menuSwapchainWidth  = 0;
static uint32_t g_menuSwapchainHeight = 0;
extern GLuint gfx_opengl_get_vr_menu_texture(void);  // Left-hand HUD texture
extern GLuint gfx_opengl_get_vr_menu_texture_R(void);// Right-hand HUD texture
extern GLuint gfx_opengl_get_vr_menu_texture_H(void);// Head HUD texture
extern bool is_weapon_hud;
float VrHudDistance = 0.8f;

// GLOBAL STATE - MENU Rendering Swapchains
static XrSwapchain g_menuSwapchain  = XR_NULL_HANDLE; // Left-hand HUD
static XrSwapchain g_menuSwapchainR = XR_NULL_HANDLE; // Right-hand HUD
static XrSwapchain g_menuSwapchainH = XR_NULL_HANDLE; // Head-locked HUD

#ifdef ANDROID
static std::vector<XrSwapchainImageOpenGLESKHR> g_menuSwapchainImages;
static std::vector<XrSwapchainImageOpenGLESKHR> g_menuSwapchainImagesR;
static std::vector<XrSwapchainImageOpenGLESKHR> g_menuSwapchainImagesH;
#else
static std::vector<XrSwapchainImageOpenGLKHR> g_menuSwapchainImages;
static std::vector<XrSwapchainImageOpenGLKHR> g_menuSwapchainImagesR;
static std::vector<XrSwapchainImageOpenGLKHR> g_menuSwapchainImagesH;
#endif

// ============================================================================
// GLOBAL STATE - Miroir layers
// ============================================================================
static XrCompositionLayerQuad g_lastMenuLayerL{};
static XrCompositionLayerQuad g_lastMenuLayerR{};
static XrCompositionLayerQuad g_lastMenuLayerH{};
static bool g_lastSubmitMenuL = false;
static bool g_lastSubmitMenuR = false;
static bool g_lastSubmitMenuH = false;
static std::array<XrView, 2> g_lastViews{};
extern "C" int gfx_sdl_get_mirror_eye();

// ============================================================================
// HEAD TRACKING - State
// ============================================================================

XrQuaternionf vr_HMD_rot_Q = { 0, 0, 0, 1 };
XrVector3f gHeadPos = { 0, 0, 0 };
bool positionValid = false;
bool orientationValid = false;

XrQuaternionf gRawHeadQ = { 0, 0, 0, 1 };
float g_yawOffsetDegrees = 0.0f;
float gStandingHeadHeight = 0.0f;

// ============================================================
// SMOOTHING HMD — When zoom is enabled
// ============================================================
#define HMD_SMOOTH_ALPHA_POS  0.05f   // Position
#define HMD_SMOOTH_ALPHA_ROT  0.05f   // Rotation

static float          sSmoothedHeadPos[3]  = {0, 0, 0};
static XrQuaternionf  sSmoothedHeadQ       = {0, 0, 0, 1};
static bool           sSmoothedHeadInit    = false;
extern void QuatSlerp(const float a[4], const float b[4], float t, float out[4]);
extern int vr_button_R_grip;
extern int vr_button_L_grip;
extern bool WepCanZoom;
// ============================================================================
// vr_is_initialized
// ============================================================================

extern "C" bool vr_is_initialized() {
    return g_vrInitialized;
}

// ============================================================================
// OpenXR Empty Frame
// ============================================================================

static inline void vr_end_empty_frame(XrTime t)
{
    XrFrameEndInfo endInfo{ XR_TYPE_FRAME_END_INFO };
    endInfo.displayTime            = t;
    endInfo.environmentBlendMode   = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount             = 0;
    endInfo.layers                 = nullptr;
    xrEndFrame(g_vrState.session, &endInfo);
}

// ============================================================================
// PC : Detect Runtime Steam VR / Meta oculus
// ============================================================================

enum class XrRuntimeType {
    Unknown,
    MetaOculusQuest,  // "Oculus" runtimeName
    SteamVR,          // "SteamVR" runtimeName
    VirtualDesktopXR, // "VirtualDesktopXR" runtimeName
};

static XrRuntimeType gActiveRuntime = XrRuntimeType::Unknown;
bool is_meta_runtime = false;

static void vr_detect_runtime() {
    XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};
    if (XR_SUCCEEDED(xrGetInstanceProperties(g_vrState.instance, &props))) {
        LOGI("OpenXR Runtime: %s (ver %u.%u.%u)",
             props.runtimeName,
             XR_VERSION_MAJOR(props.runtimeVersion),
             XR_VERSION_MINOR(props.runtimeVersion),
             XR_VERSION_PATCH(props.runtimeVersion));

        if (strstr(props.runtimeName, "Oculus")) {
            gActiveRuntime = XrRuntimeType::MetaOculusQuest;
            is_meta_runtime = true;
            LOGI("Detected runtime: Meta/Oculus");
        } else if (strstr(props.runtimeName, "VirtualDesktopXR")){
            gActiveRuntime = XrRuntimeType::VirtualDesktopXR;
            is_meta_runtime = false;
            LOGI("Detected runtime: VirtualDesktopXR");
        } else if (strstr(props.runtimeName, "SteamVR")){
            gActiveRuntime = XrRuntimeType::SteamVR;
            is_meta_runtime = false;
            LOGI("Detected runtime: SteamVR");
        }else{
            gActiveRuntime = XrRuntimeType::SteamVR;
            is_meta_runtime = false;
            LOGI("Detected runtime: UNKNOWN, set to SteamVR by default");
        }
    }
}

// ============================================================================
// EXTENSIONS - OpenXR Instance Setup
// ============================================================================

static std::vector<const char*> vr_enumerate_extensions()
{
    LOGI("Querying available OpenXR extensions");

    uint32_t extensionCount = 0;
    xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);

    std::vector<XrExtensionProperties> extensionProperties(
            extensionCount, { XR_TYPE_EXTENSION_PROPERTIES }
    );
    xrEnumerateInstanceExtensionProperties(
            nullptr, extensionCount, &extensionCount, extensionProperties.data()
    );

    LOGI("Found %u OpenXR extensions", extensionCount);

    std::vector<const char*> enabledExts;

#ifdef ANDROID
    const char* requiredExts[] = {
            "XR_KHR_android_create_instance",
            "XR_KHR_opengl_es_enable"
    };
#else
    const char* requiredExts[] = {
        "XR_KHR_opengl_enable"
    };
#endif

    for (const auto& ext : extensionProperties) {
        for (const char* required : requiredExts) {
            if (std::strcmp(ext.extensionName, required) == 0) {
                enabledExts.push_back(required);
                LOGI("Extension enabled: %s", required);
            }
        }
        if (std::strcmp(ext.extensionName, "XR_EXT_local_floor") == 0) {
            enabledExts.push_back("XR_EXT_local_floor");
        }
        if (std::strcmp(ext.extensionName, "XR_FB_color_space") == 0) {
            enabledExts.push_back("XR_FB_color_space");
            g_colorSpaceExtSupported = true;
            LOGI("Extension enabled: XR_FB_color_space");
        }
    }

    return enabledExts;
}

// ============================================================================
// INSTANCE - OpenXR Instance Creation
// ============================================================================

#ifdef ANDROID
static bool vr_create_instance(JavaVM* vm, jobject activity, const std::vector<const char*>& extensions)
#else
static bool vr_create_instance(const std::vector<const char*>& extensions)
#endif
{
    LOGI("Creating OpenXR instance");

#ifdef ANDROID
    if (extensions.size() < 2) {
        LOGE("Missing required extensions (need at least 2)");
        return false;
    }
    XrInstanceCreateInfoAndroidKHR androidInfo{ XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR };
    androidInfo.applicationVM       = vm;
    androidInfo.applicationActivity = activity;
#else
    if (extensions.empty()) {
        LOGE("No required OpenXR extensions found");
        return false;
    }
#endif

    XrInstanceCreateInfo ci{ XR_TYPE_INSTANCE_CREATE_INFO };
#ifdef ANDROID
    ci.next = &androidInfo;
#endif
    std::strncpy(ci.applicationInfo.applicationName, "PerfectDark", XR_MAX_APPLICATION_NAME_SIZE - 1);
    ci.applicationInfo.apiVersion    = XR_API_VERSION_1_0;
    ci.enabledExtensionCount         = (uint32_t)extensions.size();
    ci.enabledExtensionNames         = extensions.data();


    LOGI("vrcreateinstance: gvrState.instance = %p", (void*)(uintptr_t)g_vrState.instance);

    if (g_vrState.instance != XR_NULL_HANDLE) {
        LOGE("vrcreateinstance: instance already exists! (%p) — BUG", (void*)g_vrState.instance);
        return false;
    }
    XrResult result = xrCreateInstance(&ci, &g_vrState.instance);
    if (XR_FAILED(result)) {
        LOGE("xrCreateInstance failed: %d", (int)result);
        return false;
    }

    LOGI("OpenXR instance created successfully");
#ifndef ANDROID
    vr_detect_runtime();
#endif

    return true;
}

// ============================================================================
// SYSTEM - HMD System Detection
// ============================================================================

static bool vr_get_system()
{
    LOGI("Querying HMD system");
    XrSystemGetInfo sysInfo{XR_TYPE_SYSTEM_GET_INFO};
    sysInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    XrResult result = xrGetSystem(g_vrState.instance, &sysInfo, &g_vrState.systemId);
    if (XR_FAILED(result)) {
        if (result == XR_ERROR_FORM_FACTOR_UNAVAILABLE)
            LOGE("HMD not connected or runtime not ready");
        LOGE("xrGetSystem failed: %d", (int)result);
        return false;
    }
    if (g_vrState.systemId == 0) {
        LOGE("No HMD system found!");
        return false;
    }
    LOGI("System found: %llu", (unsigned long long)g_vrState.systemId);

    // --- Retrieve HMD model ---
    XrSystemProperties sysProps{XR_TYPE_SYSTEM_PROPERTIES};
    XrResult propResult = xrGetSystemProperties(g_vrState.instance, g_vrState.systemId, &sysProps);
    if (XR_SUCCEEDED(propResult)) {
        LOGI("HMD model: %s (vendorId=%u)", sysProps.systemName, sysProps.vendorId);
        LOGI("Max swapchain: %u x %u, max layers: %u",
             sysProps.graphicsProperties.maxSwapchainImageWidth,
             sysProps.graphicsProperties.maxSwapchainImageHeight,
             sysProps.graphicsProperties.maxLayerCount);
    } else {
        LOGE("xrGetSystemProperties failed: %d", (int)propResult);
    }

    return true;
}




// ============================================================================
// RESOLUTION - View Configuration & Target Resolution
// ============================================================================

struct vimode {
    s32 fbwidth;
    s32 fbheight;
    s32 width;
    f32 yscale;
    s32 xscale;
    s32 fullheight;
    s32 fulltop;
    s32 wideheight;
    s32 widetop;
    s32 cinemaheight;
    s32 cinematop;
};

extern struct vimode g_ViModes[6];

extern "C" bool vr_configure_resolution() {
    uint32_t viewCount = 0;
    XrResult r = xrEnumerateViewConfigurationViews(
            g_vrState.instance,
            g_vrState.systemId,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
            0, &viewCount, nullptr);

    if (XR_FAILED(r) || viewCount == 0) {
        LOGE("xrEnumerateViewConfigurationViews (count) failed: %d", (int)r);
        return false;
    }

    std::vector<XrViewConfigurationView> views(
            viewCount, { XR_TYPE_VIEW_CONFIGURATION_VIEW });

    r = xrEnumerateViewConfigurationViews(
            g_vrState.instance,
            g_vrState.systemId,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
            viewCount, &viewCount, views.data());

    if (XR_FAILED(r)) {
        LOGE("xrEnumerateViewConfigurationViews (data) failed: %d", (int)r);
        return false;
    }

    // Both eyes normally have the same recommended resolution
    int VrRealRecommendedW = views[0].recommendedImageRectWidth;
    int VrRealRecommendedH = views[0].recommendedImageRectHeight;
    LOGI("HMD recommended resolution: %u x %u", VrRealRecommendedW, VrRealRecommendedH);

    // --- Calculate the actual HMD aspect ratio ---
    double aspectRatio = (double)VrRealRecommendedW / (double)VrRealRecommendedH;
    XrAspect = (float)aspectRatio;
    LOGI("HMD aspect ratio: %.4f", XrAspect);

    // --- Fixed width enforced, height derived from aspect ratio ---
    constexpr uint32_t VR_FIXED_WIDTH = 1832;

    VrRecommendedW = VR_FIXED_WIDTH & ~1u; // ensures an even width too, for consistency
    uint32_t derivedH = (uint32_t)std::lround((double)VrRecommendedW / XrAspect);
    VrRecommendedH = (derivedH + 1u) & ~1u; // rounds up to the nearest even number, instead of truncating down

    //Get correct aspect ratio and size for HUD VR
    VrSmallW = VrRecommendedW / 4.5f;
    VrSmallH = VrRecommendedH / 4.5f;

    LOGI("VR small resolution (fixed W, derived H): %u x %u", VrSmallW, VrSmallH);


    //Set correct aspect ratio and size for VR
    g_ViModes[0] = (struct vimode){(int32_t)VrSmallW, (int32_t)VrSmallH, (int32_t)VrSmallW, 1, 1, (int32_t)VrSmallH, 0, 360, 40, 272, 84  }; // default VR
    g_ViModes[1] = (struct vimode){(int32_t)VrSmallW, (int32_t)VrSmallH, (int32_t)VrSmallW, 1, 1, (int32_t)VrSmallH, 0, 360, 40, 272, 84  }; // hi-res VR

    // --- Internal render resolution: keep the same aspect ratio, fixed width ---
    g_internalRenderWidth = (uint32_t)(VR_FIXED_WIDTH * RENDER_SCALE);
    g_internalRenderHeight = (uint32_t)std::lround(g_internalRenderWidth / XrAspect);
    LOGI("HMD g_internalRenderWidth/Height resolution: %u x %u", g_internalRenderWidth, g_internalRenderHeight);

    // Set real VR resolution in Display list
    videoInitDisplayModes();

#ifdef ANDROID
    // Set android surface with/height
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    jclass clazz = env->GetObjectClass(activity);
    jmethodID method_id = env->GetStaticMethodID(clazz, "triggerResize", "()V");
    env->CallStaticVoidMethod(clazz, method_id);
    env->DeleteLocalRef(activity);
#else
    // Set windows with/height
    SDL_GL_MakeCurrent(wnd, ctx);
    SDL_SetWindowSize(wnd, g_internalRenderWidth, g_internalRenderHeight);
#endif

    return true;
}

// ============================================================================
// PLATFORM CONTEXT - Capture and Verify
// ============================================================================

#ifdef ANDROID
static bool vr_capture_egl_context()
{
    LOGI("Capturing EGL context");

    EGLDisplay display = eglGetCurrentDisplay();
    EGLContext context = eglGetCurrentContext();

    if (display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT) {
        LOGE("No current EGL context!");
        return false;
    }

    g_sessionEglDisplay = display;
    g_sessionEglContext = context;
    LOGI("EGL: display=%p context=%p", display, context);

    EGLint configId = 0;
    eglQueryContext(display, context, EGL_CONFIG_ID, &configId);

    EGLint configAttribs[] = { EGL_CONFIG_ID, configId, EGL_NONE };
    EGLConfig config = nullptr;
    EGLint numConfigs = 0;
    eglChooseConfig(display, configAttribs, &config, 1, &numConfigs);

    if (numConfigs <= 0 || config == nullptr) {
        LOGE("eglChooseConfig failed");
        return false;
    }

    g_eglConfigForWindow = config;
    LOGI("EGL config: %p", config);

    EGLint glVersion;
    eglQueryContext(display, context, EGL_CONTEXT_CLIENT_VERSION, &glVersion);
    LOGI("EGL context version: %d", glVersion);

    return true;
}
#endif // ANDROID

// ============================================================================
// GRAPHICS REQUIREMENTS - Verify Support
// ============================================================================

static bool vr_verify_graphics_requirements()
{
    LOGI("Verifying graphics requirements");

#ifdef ANDROID
    PFN_xrGetOpenGLESGraphicsRequirementsKHR xrGetGLReq = nullptr;
    XrResult pr = xrGetInstanceProcAddr(
            g_vrState.instance,
            "xrGetOpenGLESGraphicsRequirementsKHR",
            (PFN_xrVoidFunction*)&xrGetGLReq
    );
    if (XR_FAILED(pr) || !xrGetGLReq) {
        LOGE("xrGetInstanceProcAddr(xrGetOpenGLESGraphicsRequirementsKHR) failed: %d", (int)pr);
        return false;
    }
    XrGraphicsRequirementsOpenGLESKHR reqs{ XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR };
    XrResult reqR = xrGetGLReq(g_vrState.instance, g_vrState.systemId, &reqs);
    if (XR_FAILED(reqR)) {
        LOGE("xrGetOpenGLESGraphicsRequirementsKHR failed: %d", (int)reqR);
        return false;
    }
    LOGI("GLES requirements OK: minApi=0x%x maxApi=0x%x",
         (unsigned)reqs.minApiVersionSupported, (unsigned)reqs.maxApiVersionSupported);
#else
    PFN_xrGetOpenGLGraphicsRequirementsKHR xrGetGLReq = nullptr;
    XrResult pr = xrGetInstanceProcAddr(
        g_vrState.instance,
        "xrGetOpenGLGraphicsRequirementsKHR",
        (PFN_xrVoidFunction*)&xrGetGLReq
    );
    if (XR_FAILED(pr) || !xrGetGLReq) {
        LOGE("xrGetInstanceProcAddr(xrGetOpenGLGraphicsRequirementsKHR) failed: %d", (int)pr);
        return false;
    }
    XrGraphicsRequirementsOpenGLKHR reqs{ XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
    XrResult reqR = xrGetGLReq(g_vrState.instance, g_vrState.systemId, &reqs);
    if (XR_FAILED(reqR)) {
        LOGE("xrGetOpenGLGraphicsRequirementsKHR failed: %d", (int)reqR);
        return false;
    }
    LOGI("OpenGL requirements OK: minApi=0x%llx maxApi=0x%llx",
        (unsigned long long)reqs.minApiVersionSupported, (unsigned long long)reqs.maxApiVersionSupported);
#endif

    return true;
}

// ============================================================================
// SESSION - OpenXR Session Creation
// ============================================================================

static bool vr_create_session()
{
    LOGI("Creating OpenXR session");

#ifdef ANDROID
    XrGraphicsBindingOpenGLESAndroidKHR binding{ XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR };
    binding.display = g_sessionEglDisplay;
    binding.config  = g_eglConfigForWindow;
    binding.context = g_sessionEglContext;
#else
    HDC   hdc   = wglGetCurrentDC();
    HGLRC hglrc = wglGetCurrentContext();
    if (!hdc || !hglrc) {
        LOGE("No current GL context");
        return false;
    }
    g_xr_hdc   = hdc;
    g_xr_hglrc = hglrc;

    XrGraphicsBindingOpenGLWin32KHR binding{ XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR };
    binding.hDC   = hdc;
    binding.hGLRC = hglrc;
#endif

    XrSessionCreateInfo sessionInfo{ XR_TYPE_SESSION_CREATE_INFO };
    sessionInfo.next     = &binding;
    sessionInfo.systemId = g_vrState.systemId;

    XrResult result = xrCreateSession(g_vrState.instance, &sessionInfo, &g_vrState.session);
    if (XR_FAILED(result)) {
        LOGE("xrCreateSession failed: %d", (int)result);
        return false;
    }

    LOGI("Session created successfully: %p", (void*)g_vrState.session);
    return true;
}

// ============================================================================
// COLOR SPACE - Declare sRGB content to the OpenXR runtime
// ============================================================================

static void vr_setup_color_space()
{
    if (!g_colorSpaceExtSupported) {
        LOGI("XR_FB_color_space not supported, skipping");
        return;
    }

    PFN_xrSetColorSpaceFB pfnSetColorSpaceFB = nullptr;
    XrResult r = xrGetInstanceProcAddr(
            g_vrState.instance,
            "xrSetColorSpaceFB",
            (PFN_xrVoidFunction*)&pfnSetColorSpaceFB
    );
    if (XR_FAILED(r) || pfnSetColorSpaceFB == nullptr) {
        LOGE("xrGetInstanceProcAddr(xrSetColorSpaceFB) failed: %d", (int)r);
        return;
    }

    XrResult cr = pfnSetColorSpaceFB(g_vrState.session, XR_COLOR_SPACE_REC709_FB);
    if (XR_FAILED(cr)) {
        LOGE("xrSetColorSpaceFB failed: %d", (int)cr);
    } else {
        LOGI("Color space set to XR_COLOR_SPACE_REC709_FB (sRGB)");
    }
}

// ============================================================================
// SPACE - Reference Space Creation
// ============================================================================

static bool vr_create_play_space()
{
    LOGI("Creating play space");

    uint32_t count = 0;
    XR_CHECK_R(xrEnumerateReferenceSpaces(g_vrState.session, 0, &count, nullptr), false);

    std::vector<XrReferenceSpaceType> types(count);
    XR_CHECK_R(xrEnumerateReferenceSpaces(g_vrState.session, count, &count, types.data()), false);

    auto has = [&](XrReferenceSpaceType t) {
        return std::find(types.begin(), types.end(), t) != types.end();
    };

    if (has(XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT)) {
        gPlaySpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT;
        LOGI("Play space = LOCAL_FLOOR");
    } else if (has(XR_REFERENCE_SPACE_TYPE_STAGE)) {
        gPlaySpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
        LOGI("Play space = STAGE");
    } else {
        gPlaySpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        LOGI("Play space = LOCAL");
    }

    XrReferenceSpaceCreateInfo spaceInfo{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    spaceInfo.referenceSpaceType            = gPlaySpaceType;
    spaceInfo.poseInReferenceSpace.orientation = { 0, 0, 0, 1 };
    spaceInfo.poseInReferenceSpace.position    = { 0, 0, 0 };

    XrResult result = xrCreateReferenceSpace(g_vrState.session, &spaceInfo, &g_vrState.playSpace);
    if (XR_FAILED(result)) {
        LOGE("xrCreateReferenceSpace(playSpace) failed: %d", (int)result);
        return false;
    }

    LOGI("Play space type = %d", (int)gPlaySpaceType);
    return true;
}

static bool vr_create_view_space()
{
    LOGI("Creating view space (head)");

    XrReferenceSpaceCreateInfo spaceInfo{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    spaceInfo.referenceSpaceType               = XR_REFERENCE_SPACE_TYPE_VIEW;
    spaceInfo.poseInReferenceSpace.orientation = { 0, 0, 0, 1 };
    spaceInfo.poseInReferenceSpace.position    = { 0, 0, 0 };

    XrResult result = xrCreateReferenceSpace(g_vrState.session, &spaceInfo, &g_vrState.viewSpace);

    if (XR_FAILED(result)) {
        LOGE("xrCreateReferenceSpace(viewSpace) failed: %d", (int)result);
        return false;
    }

    LOGI("View space created: %p", (void*)g_vrState.viewSpace);
    return true;
}

// ============================================================================
// SWAPCHAINS - Format Selection
// ============================================================================

static bool vr_format_supported(const std::vector<int64_t>& formats, int64_t fmt) {
    for (int64_t f : formats) if (f == fmt) return true;
    return false;
}

static int64_t vr_pick_swapchain_format()
{
    uint32_t count = 0;
    XrResult r0 = xrEnumerateSwapchainFormats(g_vrState.session, 0, &count, nullptr);
    if (XR_FAILED(r0) || count == 0) {
        LOGE("xrEnumerateSwapchainFormats(count) failed r=%d count=%u", (int)r0, count);
        return (int64_t)GL_RGBA8;
    }

    std::vector<int64_t> formats(count);
    XrResult r1 = xrEnumerateSwapchainFormats(g_vrState.session, count, &count, formats.data());
    if (XR_FAILED(r1) || count == 0) {
        LOGE("xrEnumerateSwapchainFormats(data) failed r=%d count=%u", (int)r1, count);
        return (int64_t)GL_RGBA8;
    }

    for (uint32_t i = 0; i < count; i++) {
        LOGI("supported[%u] = 0x%llx (%lld)", i,
             (unsigned long long)formats[i], (long long)formats[i]);
    }


#ifdef ANDROID
    const int64_t preferred[] = {
            (int64_t)GL_RGBA8,
            (int64_t)GL_RGB10_A2,
            (int64_t)GL_RGBA16F,
            (int64_t)GL_SRGB8_ALPHA8,
    };

#else
    const int64_t preferred[] = {
            (int64_t)GL_SRGB8_ALPHA8,
            (int64_t)GL_RGBA8,
            (int64_t)GL_RGB10_A2,
            (int64_t)GL_RGBA16F,
    };
#endif


    for (int64_t fmt : preferred) {
        if (vr_format_supported(formats, fmt)) {
            LOGI("picked format=0x%llx (%lld)", (unsigned long long)fmt, (long long)fmt);
            return fmt;
        }
    }

    LOGI("no preferred format found, using first supported=0x%llx (%lld)",
         (unsigned long long)formats[0], (long long)formats[0]);
    return formats[0];
}

// ============================================================================
// SWAPCHAINS - Texture Creation
// ============================================================================

static bool vr_create_swapchains()
{
    const int64_t chosenFormat = vr_pick_swapchain_format();

    XrSwapchainCreateInfo swapInfo{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
    swapInfo.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    swapInfo.format      = chosenFormat;
    swapInfo.sampleCount = 1;
    swapInfo.width       = g_internalRenderWidth;
    swapInfo.height      = g_internalRenderHeight;
    swapInfo.faceCount   = 1;
    swapInfo.arraySize   = use_multiview ? 2 : 1;
    swapInfo.mipCount    = 1;

    // Under multiview both eyes are array layers of a single swapchain, so only [0] is ever
    // acquired, submitted or enumerated -- a second one was a full extra set of eye buffers
    // that nothing ever read. At a 3.0 render scale that is ~735 MiB of dead VRAM.
    const int swapchainCount = use_multiview ? 1 : 2;

    for (int eye = 0; eye < swapchainCount; eye++) {
        XrResult result = xrCreateSwapchain(g_vrState.session, &swapInfo, &g_vrState.swapchains[eye]);
        if (XR_FAILED(result)) {
            LOGE("xrCreateSwapchain eye=%d failed: %d (format=0x%llx)",
                 eye, (int)result, (unsigned long long)chosenFormat);
            return false;
        }

        uint32_t imageCount = 0;
        XrResult countResult = xrEnumerateSwapchainImages(g_vrState.swapchains[eye], 0, &imageCount, nullptr);
        if (XR_FAILED(countResult)) {
            LOGE("xrEnumerateSwapchainImages(count) eye=%d failed: %d", eye, (int)countResult);
            return false;
        }

        LOGI("Swapchain eye=%d created (format=0x%llx) images=%u",
             eye, (unsigned long long)chosenFormat, imageCount);
    }

    return true;
}


// ============================================================================
// SWAPCHAINS - MENU Texture Creation
// ============================================================================
static bool vr_create_menu_swapchain()
{
    g_menuSwapchainWidth  = (uint32_t)g_internalRenderWidth;
    g_menuSwapchainHeight = (uint32_t)g_internalRenderHeight;

    // Use the same format as the eye swapchains, selected dynamically
    // via xrEnumerateSwapchainFormats (instead of a hardcoded format)
    const int64_t chosenFormat = vr_pick_swapchain_format();

    XrSwapchainCreateInfo swapchainInfo = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    swapchainInfo.format     = chosenFormat;
    swapchainInfo.sampleCount = 1;
    swapchainInfo.width      = g_menuSwapchainWidth;
    swapchainInfo.height     = g_menuSwapchainHeight;
    swapchainInfo.faceCount  = 1;
    swapchainInfo.arraySize  = 1;
    swapchainInfo.mipCount   = 1;

    // Swapchain Left
    if (XR_FAILED(xrCreateSwapchain(g_vrState.session, &swapchainInfo, &g_menuSwapchain))) {
        LOGE("xrCreateSwapchain (menu L) failed (format=0x%llx)", (unsigned long long)chosenFormat);
        return false;
    }
    uint32_t imageCount = 0;
    xrEnumerateSwapchainImages(g_menuSwapchain, 0, &imageCount, nullptr);
#ifdef ANDROID
    g_menuSwapchainImages.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
#else
    g_menuSwapchainImages.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
#endif
    xrEnumerateSwapchainImages(g_menuSwapchain, imageCount, &imageCount,
                               reinterpret_cast<XrSwapchainImageBaseHeader*>(g_menuSwapchainImages.data()));

    // Swapchain Right
    if (XR_FAILED(xrCreateSwapchain(g_vrState.session, &swapchainInfo, &g_menuSwapchainR))) {
        LOGE("xrCreateSwapchain (menu R) failed (format=0x%llx)", (unsigned long long)chosenFormat);
        return false;
    }
    imageCount = 0;
    xrEnumerateSwapchainImages(g_menuSwapchainR, 0, &imageCount, nullptr);
#ifdef ANDROID
    g_menuSwapchainImagesR.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
#else
    g_menuSwapchainImagesR.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
#endif
    xrEnumerateSwapchainImages(g_menuSwapchainR, imageCount, &imageCount,
                               reinterpret_cast<XrSwapchainImageBaseHeader*>(g_menuSwapchainImagesR.data()));

    // Swapchain head-locked
    if (XR_FAILED(xrCreateSwapchain(g_vrState.session, &swapchainInfo, &g_menuSwapchainH))) {
        LOGE("xrCreateSwapchain (menu H) failed (format=0x%llx)", (unsigned long long)chosenFormat);
        return false;
    }
    imageCount = 0;
    xrEnumerateSwapchainImages(g_menuSwapchainH, 0, &imageCount, nullptr);
#ifdef ANDROID
    g_menuSwapchainImagesH.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
#else
    g_menuSwapchainImagesH.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
#endif
    xrEnumerateSwapchainImages(g_menuSwapchainH, imageCount, &imageCount,
                               reinterpret_cast<XrSwapchainImageBaseHeader*>(g_menuSwapchainImagesH.data()));

    LOGI("Menu swapchains created L/R/H: %u x %u (format=0x%llx)",
         g_menuSwapchainWidth, g_menuSwapchainHeight, (unsigned long long)chosenFormat);
    return true;
}





static void vr_update_menu_swapchain_L() {
    uint32_t imageIndex = 0;
    XrSwapchainImageAcquireInfo acquireInfo{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    xrAcquireSwapchainImage(g_menuSwapchain, &acquireInfo, &imageIndex);

    XrSwapchainImageWaitInfo waitInfo{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    waitInfo.timeout = XR_INFINITE_DURATION;
    xrWaitSwapchainImage(g_menuSwapchain, &waitInfo);

    GLuint srcTex = gfx_opengl_get_vr_menu_texture();
    GLuint dstTex = g_menuSwapchainImages[imageIndex].image;

    static GLuint srcFboTmp = 0, dstFboTmp = 0;
    if (srcFboTmp == 0) glGenFramebuffers(1, &srcFboTmp);
    if (dstFboTmp == 0) glGenFramebuffers(1, &dstFboTmp);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFboTmp);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, srcTex, 0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFboTmp);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dstTex, 0);

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(
            0, 0, g_menuSwapchainWidth, g_menuSwapchainHeight,
            0, 0, g_menuSwapchainWidth, g_menuSwapchainHeight,
            GL_COLOR_BUFFER_BIT, GL_LINEAR
    );
    glEnable(GL_SCISSOR_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    XrSwapchainImageReleaseInfo releaseInfo{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    xrReleaseSwapchainImage(g_menuSwapchain, &releaseInfo);
}

static void vr_update_menu_swapchain_R()
{
    uint32_t imageIndex = 0;
    XrSwapchainImageAcquireInfo acquireInfo = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    xrAcquireSwapchainImage(g_menuSwapchainR, &acquireInfo, &imageIndex);
    XrSwapchainImageWaitInfo waitInfo = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    xrWaitSwapchainImage(g_menuSwapchainR, &waitInfo);

    GLuint srcTex = gfx_opengl_get_vr_menu_texture_R();
    GLuint dstTex = g_menuSwapchainImagesR[imageIndex].image;

    static GLuint srcFboR = 0, dstFboR = 0;
    if (!srcFboR) glGenFramebuffers(1, &srcFboR);
    if (!dstFboR) glGenFramebuffers(1, &dstFboR);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFboR);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, srcTex, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFboR);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dstTex, 0);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(0, 0, g_menuSwapchainWidth, g_menuSwapchainHeight,
                      0, 0, g_menuSwapchainWidth, g_menuSwapchainHeight,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glEnable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    XrSwapchainImageReleaseInfo releaseInfo = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(g_menuSwapchainR, &releaseInfo);
}


static void vr_update_menu_swapchain_H() {
    uint32_t imageIndex = 0;
    XrSwapchainImageAcquireInfo acquireInfo = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    xrAcquireSwapchainImage(g_menuSwapchainH, &acquireInfo, &imageIndex);
    XrSwapchainImageWaitInfo waitInfo = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    xrWaitSwapchainImage(g_menuSwapchainH, &waitInfo);

    GLuint srcTex = gfx_opengl_get_vr_menu_texture_H();
    GLuint dstTex = g_menuSwapchainImagesH[imageIndex].image;

    static GLuint srcFboH = 0, dstFboH = 0;
    if (!srcFboH) glGenFramebuffers(1, &srcFboH);
    if (!dstFboH) glGenFramebuffers(1, &dstFboH);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFboH);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, srcTex, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFboH);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dstTex, 0);

    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(0, 0, g_menuSwapchainWidth, g_menuSwapchainHeight,
                      0, 0, g_menuSwapchainWidth, g_menuSwapchainHeight,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glEnable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    XrSwapchainImageReleaseInfo releaseInfo = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(g_menuSwapchainH, &releaseInfo);
}

// ============================================================================
// FBOs - Render Targets
// ============================================================================

static bool vr_create_eye_fbos()
{
    if (use_multiview) {
        glGenTextures(1, &g_multiviewDepthArray);
        glBindTexture(GL_TEXTURE_2D_ARRAY, g_multiviewDepthArray);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH24_STENCIL8,
                     g_internalRenderWidth, g_internalRenderHeight, 2,
                     0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

        glGenFramebuffers(1, &g_multiviewFBO);
        gfx_opengl_connect_multiview_fbo(g_multiviewFBO, g_internalRenderWidth, g_internalRenderHeight);
        return true;
    }
    else{
        return false;
    }

}

// ============================================================================
// CONTROLLERS - VR Controller Initialization
// ============================================================================

static void vr_init_controllers()
{
    LOGI("Initializing VR controllers");
    XrResult result = create_vr_controllers_complete();
    if (XR_FAILED(result)) {
        LOGE("Failed to initialize controllers");
    }
}

// ============================================================================
// HELPERS - Swapchain Image Management
// ============================================================================

static bool vr_ensure_swapchain_images()
{
    if (g_swapchainImagesInit[0]) return true;
    if (g_vrState.swapchains[0] == XR_NULL_HANDLE) return false;

    uint32_t count = 0;
    XrResult r0 = xrEnumerateSwapchainImages(g_vrState.swapchains[0], 0, &count, nullptr);
    if (XR_FAILED(r0) || count == 0) {
        LOGE("xrEnumerateSwapchainImages(count) failed: %d", (int)r0);
        return false;
    }

    g_swapchainImages[0].resize(count);
    for (auto& img : g_swapchainImages[0]) {
#ifdef ANDROID
        img.type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
#else
        img.type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
#endif
        img.next = nullptr;
    }

    XrResult r1 = xrEnumerateSwapchainImages(
            g_vrState.swapchains[0],
            count, &count,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(g_swapchainImages[0].data())
    );
    if (XR_FAILED(r1)) {
        LOGE("xrEnumerateSwapchainImages(data) failed: %d", (int)r1);
        g_swapchainImages[0].clear();
        return false;
    }

    g_swapchainImagesInit[0] = true;
    return true;
}

// ============================================================================
// HEAD TRACKING - Math Helpers
// ============================================================================

XrQuaternionf MultiplyQuaternions(XrQuaternionf q1, XrQuaternionf q2) {
    XrQuaternionf result;
    result.x =  q1.x * q2.w + q1.y * q2.z - q1.z * q2.y + q1.w * q2.x;
    result.y = -q1.x * q2.z + q1.y * q2.w + q1.z * q2.x + q1.w * q2.y;
    result.z =  q1.x * q2.y - q1.y * q2.x + q1.z * q2.w + q1.w * q2.z;
    result.w = -q1.x * q2.x - q1.y * q2.y - q1.z * q2.z + q1.w * q2.w;
    return result;
}

XrQuaternionf YawToQuaternion(float angleDegrees) {
    float rad = angleDegrees * (3.14159265f / 180.0f) * 0.5f;
    return { 0.0f, std::sinf(rad), 0.0f, std::cosf(rad) };
}

static float GetYawDegreesFromQuaternion(XrQuaternionf q) {
    float siny_cosp = 2.0f * (q.w * q.y + q.x * q.z);
    float cosy_cosp = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
    return std::atan2f(siny_cosp, cosy_cosp) * (180.0f / 3.14159265f);
}

extern "C" void vr_align_with_game_angle(float target_game_angle) {
    float current_physical_yaw = GetYawDegreesFromQuaternion(gRawHeadQ);
    g_yawOffsetDegrees = target_game_angle - current_physical_yaw;
    while (g_yawOffsetDegrees >  180.0f) g_yawOffsetDegrees -= 360.0f;
    while (g_yawOffsetDegrees < -180.0f) g_yawOffsetDegrees += 360.0f;
//    LOGI("Recenter: Target=%.2f, Physical=%.2f -> Offset=%.2f",
//         target_game_angle, current_physical_yaw, g_yawOffsetDegrees);
}

XrVector3f RotateVectorY(XrVector3f v, float angleDegrees) {
    float rad = angleDegrees * (3.14159265f / 180.0f);
    float s = std::sinf(rad), c = std::cosf(rad);
    return { v.x * c + v.z * s, v.y, -v.x * s + v.z * c };
}

// ============================================================================
// HEAD TRACKING - HMD Position & Rotation
// ============================================================================

static XrQuaternionf SlerpQuaternions(XrQuaternionf a, XrQuaternionf b, float t)
{
    const float qa[4] = {a.w, a.x, a.y, a.z};  // → [w,x,y,z]
    const float qb[4] = {b.w, b.x, b.y, b.z};
    float out[4];
    QuatSlerp(qa, qb, t, out);
    return {out[1], out[2], out[3], out[0]};    // → {x,y,z,w}
}

static void vr_update_head_tracking(XrTime predictedDisplayTime)
{
    if (!g_vrState.sessionRunning || g_vrState.session == XR_NULL_HANDLE) return;

    XrSpaceLocation headLocation = {XR_TYPE_SPACE_LOCATION};
    XrResult result = xrLocateSpace(
            g_vrState.viewSpace, g_vrState.playSpace,
            predictedDisplayTime, &headLocation);

    if (XR_FAILED(result)) {
        LOGE("xrLocateSpace failed %d", (int)result);
        positionValid = orientationValid = false;
        return;
    }

    positionValid    = (headLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)    != 0;
    orientationValid = (headLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;

    if (positionValid && orientationValid) {
        // Raw position and rotation (with yaw offset) — same calculation as the original
        XrVector3f rawPos = {
                -headLocation.pose.position.x * 100.0f,
                headLocation.pose.position.y * 100.0f,
                -headLocation.pose.position.z * 100.0f
        };
        gRawHeadQ = headLocation.pose.orientation;
        XrQuaternionf offsetQ = YawToQuaternion(g_yawOffsetDegrees);
        XrQuaternionf rawQ    = MultiplyQuaternions(offsetQ, gRawHeadQ);
        XrVector3f    rawHead = RotateVectorY(rawPos, g_yawOffsetDegrees);

        // --- Grip pressed on either controller ---
        bool gripAny = (vr_button_R_grip != 0) || (vr_button_L_grip != 0);

        if (gripAny && WepCanZoom) {
            // Initialize the buffer on the first frame with grip
            if (!sSmoothedHeadInit) {
                sSmoothedHeadPos[0] = rawHead.x;
                sSmoothedHeadPos[1] = rawHead.y;
                sSmoothedHeadPos[2] = rawHead.z;
                sSmoothedHeadQ      = rawQ;
                sSmoothedHeadInit   = true;
            }

            // POSITION smoothing (EMA)
            sSmoothedHeadPos[0] = HMD_SMOOTH_ALPHA_POS * rawHead.x + (1.0f - HMD_SMOOTH_ALPHA_POS) * sSmoothedHeadPos[0];
            sSmoothedHeadPos[1] = HMD_SMOOTH_ALPHA_POS * rawHead.y + (1.0f - HMD_SMOOTH_ALPHA_POS) * sSmoothedHeadPos[1];
            sSmoothedHeadPos[2] = HMD_SMOOTH_ALPHA_POS * rawHead.z + (1.0f - HMD_SMOOTH_ALPHA_POS) * sSmoothedHeadPos[2];

            gHeadPos = {sSmoothedHeadPos[0], sSmoothedHeadPos[1], sSmoothedHeadPos[2]};

            // ROTATION smoothing (SLERP)
            sSmoothedHeadQ = SlerpQuaternions(sSmoothedHeadQ, rawQ, HMD_SMOOTH_ALPHA_ROT);
            vr_HMD_rot_Q      = sSmoothedHeadQ;


        } else {
            // Grip released: raw values + reset initialization
            sSmoothedHeadInit = false;
            gHeadPos          = rawHead;
            vr_HMD_rot_Q         = rawQ;
        }

        if (gHeadPos.y > gStandingHeadHeight) {
            gStandingHeadHeight = gHeadPos.y;
        }

    }
}



// ============================================================================
// PC miroir - Layers
// ============================================================================
#ifndef ANDROID
static void QuatToMat4(const XrQuaternionf& q, float* m) {
    float x = q.x, y = q.y, z = q.z, w = q.w;

    float xx = x * x;
    float yy = y * y;
    float zz = z * z;
    float xy = x * y;
    float xz = x * z;
    float yz = y * z;
    float wx = w * x;
    float wy = w * y;
    float wz = w * z;

    m[0]  = 1.0f - 2.0f * (yy + zz);
    m[1]  = 2.0f * (xy + wz);
    m[2]  = 2.0f * (xz - wy);
    m[3]  = 0.0f;

    m[4]  = 2.0f * (xy - wz);
    m[5]  = 1.0f - 2.0f * (xx + zz);
    m[6]  = 2.0f * (yz + wx);
    m[7]  = 0.0f;

    m[8]  = 2.0f * (xz + wy);
    m[9]  = 2.0f * (yz - wx);
    m[10] = 1.0f - 2.0f * (xx + yy);
    m[11] = 0.0f;

    m[12] = 0.0f;
    m[13] = 0.0f;
    m[14] = 0.0f;
    m[15] = 1.0f;
}
static void Mat4Mul(const float* a, const float* b, float* out) {
    float r[16];
    for (int c = 0; c < 4; ++c) {
        for (int rIdx = 0; rIdx < 4; ++rIdx) {
            r[c * 4 + rIdx] =
                    a[0 * 4 + rIdx] * b[c * 4 + 0] +
                    a[1 * 4 + rIdx] * b[c * 4 + 1] +
                    a[2 * 4 + rIdx] * b[c * 4 + 2] +
                    a[3 * 4 + rIdx] * b[c * 4 + 3];
        }
    }
    memcpy(out, r, sizeof(r));
}


static void ProjectionFromFov(const XrFovf& fov, float nearZ, float farZ, float* m) {
    float l = tanf(fov.angleLeft);
    float r = tanf(fov.angleRight);
    float u = tanf(fov.angleUp);
    float d = tanf(fov.angleDown);

    float w = r - l;
    float h = u - d;

    memset(m, 0, 16 * sizeof(float));
    m[0]  = 2.0f / w;
    m[5]  = 2.0f / h;
    m[8]  = (r + l) / w;
    m[9]  = (u + d) / h;
    m[10] = -(farZ + nearZ) / (farZ - nearZ);
    m[11] = -1.0f;
    m[14] = -(farZ * (nearZ + nearZ)) / (farZ - nearZ);
}

// Inverse of a rigid pose matrix (rotation + translation only)
static void InvertRigidMat4(const float* m, float* out) {

    out[0] = m[0]; out[1] = m[4]; out[2] = m[8];
    out[4] = m[1]; out[5] = m[5]; out[6] = m[9];
    out[8] = m[2]; out[9] = m[6]; out[10] = m[10];
    out[3] = 0.0f; out[7] = 0.0f; out[11] = 0.0f; out[15] = 1.0f;

    float tx = m[12], ty = m[13], tz = m[14];
    out[12] = -(out[0] * tx + out[4] * ty + out[8]  * tz);
    out[13] = -(out[1] * tx + out[5] * ty + out[9]  * tz);
    out[14] = -(out[2] * tx + out[6] * ty + out[10] * tz);
}

enum VrMirrorMenuSource {
    VR_MIRROR_MENU_L = 0,
    VR_MIRROR_MENU_R = 1,
    VR_MIRROR_MENU_H = 2,
};

struct VrMirrorMenuEntry {
    VrMirrorMenuSource source;
    float mvp[16];
};

// Compute the MVP for a given quad (factored out from your existing code)
static void ComputeQuadMirrorMVP(const XrCompositionLayerQuad* quad,
                                 const XrView& view,
                                 float* outMvp) {
    // Model (rotation + translation + scale)
    float rot[16];
    QuatToMat4(quad->pose.orientation, rot);

    float model[16];
    memcpy(model, rot, sizeof(rot));
    model[12] = quad->pose.position.x;
    model[13] = quad->pose.position.y;
    model[14] = quad->pose.position.z;

    // Quad size in meters
    model[0] *= quad->size.width;
    model[1] *= quad->size.width;
    model[2] *= quad->size.width;

    model[4] *= quad->size.height;
    model[5] *= quad->size.height;
    model[6] *= quad->size.height;

    float proj[16];
    ProjectionFromFov(view.fov, 0.05f, 100.0f, proj);

    // If the quad is in viewSpace (head-locked), its pose is already in
    // camera space: no View transform needs to be applied.
    if (quad->space == g_vrState.viewSpace) {
        Mat4Mul(proj, model, outMvp);
        return;
    }

    // Quad in playSpace (hands): Proj * View * Model
    float eyeRot[16];
    QuatToMat4(view.pose.orientation, eyeRot);
    eyeRot[12] = view.pose.position.x;
    eyeRot[13] = view.pose.position.y;
    eyeRot[14] = view.pose.position.z;

    float viewMat[16];
    InvertRigidMat4(eyeRot, viewMat);

    float vm[16];
    Mat4Mul(viewMat, model, vm);
    Mat4Mul(proj, vm, outMvp);
}

// Returns the number of quads to draw in the mirror, and fills outEntries
// (outEntries must point to an array of at least 3 elements)
extern "C" int vrGetMenuMirrorMVPList(int eyeIndex, VrMirrorMenuEntry* outEntries) {
    int count = 0;
    const XrView& view = g_lastViews[eyeIndex];

    if (g_lastSubmitMenuL) {
        outEntries[count].source = VR_MIRROR_MENU_L;
        ComputeQuadMirrorMVP(&g_lastMenuLayerL, view, outEntries[count].mvp);
        count++;
    }
    if (g_lastSubmitMenuR) {
        outEntries[count].source = VR_MIRROR_MENU_R;
        ComputeQuadMirrorMVP(&g_lastMenuLayerR, view, outEntries[count].mvp);
        count++;
    }
    if (g_lastSubmitMenuH) {
        outEntries[count].source = VR_MIRROR_MENU_H;
        ComputeQuadMirrorMVP(&g_lastMenuLayerH, view, outEntries[count].mvp);
        count++;
    }

    return count;
}
#endif



// ============================================================================
// COMPOSITION - Layer Submission
// ============================================================================
static void rotvec(float *vx, float *vy, float *vz,
                   float qw, float qx, float qy, float qz)
{
    float tx = 2.0f*(qy*(*vz) - qz*(*vy));
    float ty = 2.0f*(qz*(*vx) - qx*(*vz));
    float tz = 2.0f*(qx*(*vy) - qy*(*vx));
    *vx += qw*tx + qy*tz - qz*ty;
    *vy += qw*ty + qz*tx - qx*tz;
    *vz += qw*tz + qx*ty - qy*tx;
}
static void quaternionMul_XR(const XrQuaternionf *a, const XrQuaternionf *b, XrQuaternionf *out)
{
    out->x = a->w * b->x + a->x * b->w + a->y * b->z - a->z * b->y;
    out->y = a->w * b->y - a->x * b->z + a->y * b->w + a->z * b->x;
    out->z = a->w * b->z + a->x * b->y - a->y * b->x + a->z * b->w;
    out->w = a->w * b->w - a->x * b->x - a->y * b->y - a->z * b->z;
}

extern bool gfx_vr_menu_L_dirty_and_clear(void);
extern bool gfx_vr_menu_R_dirty_and_clear(void);
extern bool gfx_vr_menu_H_dirty_and_clear(void);

static const float VR_MENU_FACING_THRESHOLD = 0.8f;

struct VrMenuResult {
    XrPosef pose;
    bool facingPlayer;
};

// Computes the pose (position + orientation) and visibility of a weapon HUD
// mirror=true for the left hand (inverted yaw)
static VrMenuResult vr_compute_weapon_menu(int ctrlIndex, bool mirror,
                                           float offsetX, float offsetY, float offsetZ) {

    XrQuaternionf qRaw;
    qRaw.x = -gCtrlQuatRaw[ctrlIndex][1];
    qRaw.y =  gCtrlQuatRaw[ctrlIndex][2];
    qRaw.z = -gCtrlQuatRaw[ctrlIndex][3];
    qRaw.w =  gCtrlQuatRaw[ctrlIndex][0];

    float yawAngle = mirror ? M_PI : -M_PI;
    XrQuaternionf qYaw = {0.0f, sinf(yawAngle * 0.25f), 0.0f, cosf(yawAngle * 0.25f)};
    XrQuaternionf qFinal;
    quaternionMul_XR(&qRaw, &qYaw, &qFinal);

    VrMenuResult r;
    r.pose.position = {
            gCtrlPos[ctrlIndex][0] / 100 + offsetX,
            gCtrlPos[ctrlIndex][1] / 100 + offsetY,
            gCtrlPos[ctrlIndex][2] / 100 + offsetZ
    };
    r.pose.orientation = qFinal;

    float fwdX = 0.0f, fwdY = 0.0f, fwdZ = 1.0f;
    rotvec(&fwdX, &fwdY, &fwdZ, qFinal.w, qFinal.x, qFinal.y, qFinal.z);

    float lx = r.pose.position.x, ly = r.pose.position.y, lz = r.pose.position.z;
    float len = sqrtf(lx*lx + ly*ly + lz*lz);
    if (len < 0.0001f) len = 0.0001f;

    float dot = (fwdX * -lx + fwdY * -ly + fwdZ * -lz) / len;
    r.facingPlayer = dot > VR_MENU_FACING_THRESHOLD;
    return r;
}

// Initialize the common fields of a menu quad (everything except pose/size/facing)
static XrCompositionLayerQuad vr_init_menu_quad(XrSwapchain swapchain) {
    XrCompositionLayerQuad q = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    q.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    q.space = g_vrState.viewSpace;
    q.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    q.subImage.swapchain = swapchain;
    q.subImage.imageArrayIndex = 0;
    q.subImage.imageRect.offset = {0, 0};
    q.subImage.imageRect.extent = {(int32_t)g_menuSwapchainWidth, (int32_t)g_menuSwapchainHeight};
    return q;
}

static void vr_submit_frame(XrFrameState& frameState, const std::array<XrView, 2>& views) {
    std::array<XrCompositionLayerProjectionView, 2> projViews;
    bool is_mv = gfx_get_current_rendering_api()->is_multiview();

    for (int eye = 0; eye < 2; ++eye) {
        projViews[eye] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        projViews[eye].pose = views[eye].pose;
        projViews[eye].fov = views[eye].fov;

        if (is_mv) {
            projViews[eye].subImage.swapchain = g_vrState.swapchains[0];
            projViews[eye].subImage.imageArrayIndex = eye;
        } else {
            projViews[eye].subImage.swapchain = g_vrState.swapchains[eye];
            projViews[eye].subImage.imageArrayIndex = 0;
        }

        projViews[eye].subImage.imageRect.offset = {0, 0};
        projViews[eye].subImage.imageRect.extent = {
                (int32_t) g_internalRenderWidth,
                (int32_t) g_internalRenderHeight
        };
    }

    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    layer.space = g_vrState.playSpace;
    layer.viewCount = 2;
    layer.views = projViews.data();

    const int ctrlL = 0;
    const int ctrlR = 1;

    // --- HUD Left Hand ---
    bool submitMenuL = (g_menuSwapchain != XR_NULL_HANDLE) && gfx_vr_menu_L_dirty_and_clear();

    if (submitMenuL) vr_update_menu_swapchain_L();

    XrCompositionLayerQuad menuLayerL = vr_init_menu_quad(g_menuSwapchain);

    if (is_weapon_hud) {
        VrMenuResult res = vr_compute_weapon_menu(ctrlL, /*mirror=*/true, 0.0f, 0.0f, 0.0f);
        menuLayerL.pose = res.pose;
        menuLayerL.size = {1.0f * XrAspect * 0.8f, 1.0f * 0.8f};
        submitMenuL = submitMenuL && res.facingPlayer;
    } else {
        XrQuaternionf qRaw;
        qRaw.x = -gCtrlQuatRaw[ctrlL][1];
        qRaw.y =  gCtrlQuatRaw[ctrlL][2];
        qRaw.z = -gCtrlQuatRaw[ctrlL][3];
        qRaw.w =  gCtrlQuatRaw[ctrlL][0];
        menuLayerL.pose.orientation = qRaw;
        menuLayerL.pose.position = {
                gCtrlPos[ctrlL][0] / 100 + 0.2f,
                gCtrlPos[ctrlL][1] / 100 + 0.2f,
                gCtrlPos[ctrlL][2] / 100 - 0.2f
        };
        menuLayerL.size = {1.0f * XrAspect, 1.0f};
    }

    // --- HUD Right hand ---
    bool submitMenuR = (g_menuSwapchainR != XR_NULL_HANDLE) && gfx_vr_menu_R_dirty_and_clear();

    if (submitMenuR) vr_update_menu_swapchain_R();

    XrCompositionLayerQuad menuLayerR = vr_init_menu_quad(g_menuSwapchainR);

    if (is_weapon_hud) {
        VrMenuResult res = vr_compute_weapon_menu(ctrlR, /*mirror=*/false, 0.0f, 0.0f, 0.0f);
        menuLayerR.pose = res.pose;
        menuLayerR.size = {1.0f * XrAspect * 0.8f, 1.0f * 0.8f};
        submitMenuR = submitMenuR && res.facingPlayer;
    } else {
        submitMenuR = false;
    }

    // --- HUD head-locked ---
    bool submitMenuH = (g_menuSwapchainH != XR_NULL_HANDLE) && gfx_vr_menu_H_dirty_and_clear();
    if (submitMenuH) vr_update_menu_swapchain_H();

    XrCompositionLayerQuad menuLayerH = vr_init_menu_quad(g_menuSwapchainH);

    menuLayerH.pose.orientation = {0.f, 0.f, 0.f, 1.f};
    menuLayerH.pose.position    = {0.f, 0.f, -VrHudDistance};

    // Size in meters — adjust as needed
    menuLayerH.size = {1.0f * XrAspect, 1.0f};

    // --- Layer submission ---
    int numLayers = 1;
    const XrCompositionLayerBaseHeader* layers[4];
    layers[0] = reinterpret_cast<const XrCompositionLayerBaseHeader *>(&layer);

    if (submitMenuL) layers[numLayers++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&menuLayerL);
    if (submitMenuR) layers[numLayers++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&menuLayerR);
    if (submitMenuH) layers[numLayers++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&menuLayerH);

#ifndef ANDROID // if PC
    // for miroir PC
    g_lastMenuLayerL   = menuLayerL;
    g_lastMenuLayerR   = menuLayerR;
    g_lastMenuLayerH   = menuLayerH;
    g_lastSubmitMenuL  = submitMenuL;
    g_lastSubmitMenuR  = submitMenuR;
    g_lastSubmitMenuH  = submitMenuH;
    g_lastViews        = views;
#endif

    XrFrameEndInfo endInfo = {XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime          = frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount           = numLayers;
    endInfo.layers               = layers;

    XrResult re = xrEndFrame(g_vrState.session, &endInfo);
    if (XR_FAILED(re))
        LOGE("xrEndFrame failed %d", (int)re);

}



// ============================================================================
// EVENTS - Session State Management
// ============================================================================

extern "C" void vr_poll_events(void)
{
    if (!g_vrState.instance) return;

    while (true) {
        XrEventDataBuffer event;
        std::memset(&event, 0, sizeof(event));
        event.type = XR_TYPE_EVENT_DATA_BUFFER;

        XrResult r = xrPollEvent(g_vrState.instance, &event);
        if (r == XR_EVENT_UNAVAILABLE) break;
        if (XR_FAILED(r)) {
            LOGE("xrPollEvent failed: %d", (int)r);
            break;
        }

        const XrEventDataBaseHeader* baseEvent = (const XrEventDataBaseHeader*)&event;

        switch (baseEvent->type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                const XrEventDataSessionStateChanged* ssEvent =
                        (const XrEventDataSessionStateChanged*)baseEvent;
                if (ssEvent->session != g_vrState.session) break;
                LOGI("Session state changed: %d", (int)ssEvent->state);

                switch (ssEvent->state) {
                    case XR_SESSION_STATE_READY:
                        if (!g_vrState.sessionRunning) {
                            XrSessionBeginInfo begin{ XR_TYPE_SESSION_BEGIN_INFO };
                            begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                            XrResult br = xrBeginSession(g_vrState.session, &begin);
                            if (br == XR_SUCCESS) {
                                g_vrState.sessionRunning = true;
                                LOGI("Session begun");
                            }
                        }
                        break;
                    case XR_SESSION_STATE_STOPPING:
                        if (g_vrState.sessionRunning) {
                            xrEndSession(g_vrState.session);
                            g_vrState.sessionRunning = false;
                            LOGI("Session ended");
                        }
                        break;
                    case XR_SESSION_STATE_EXITING:
                    case XR_SESSION_STATE_LOSS_PENDING:
                        g_vrState.sessionRunning = false;
                        LOGI("Session exiting/loss pending");
                        break;
                    case XR_SESSION_STATE_VISIBLE:
                    default: break;
                }
                break;
            }
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                g_vrState.sessionRunning = false;
                LOGI("Instance loss pending");
                break;
            case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
                if (g_vrState.playSpace != XR_NULL_HANDLE) {
                    xrDestroySpace(g_vrState.playSpace);
                    g_vrState.playSpace = XR_NULL_HANDLE;
                }
                // Runtimes can burst several of these (recentre, guardian/boundary reset,
                // system menu). If the recreate fails the handle stays null and every
                // xrLocateViews and xrEndFrame afterwards works off an invalid space, so
                // say so loudly rather than limping on in silence.
                if (!vr_create_play_space()) {
                    LOGE("REFERENCE_SPACE_CHANGE_PENDING: play space recreate FAILED");
                }
                break;
            default: break;
        }
    }
}

// ============================================================================
// INITIALIZATION - Complete VR Setup Pipeline
// ============================================================================

#ifdef ANDROID
extern "C" void openxr_initialize_vr(JavaVM* vm, jobject activity, ANativeWindow* window)
{
    LOGI("========== OPENXR INIT (Android) START ==========");
    g_vrState = VRState{};

    auto extensions = vr_enumerate_extensions();
    if (extensions.size() < 2) {
        LOGE("Missing required extensions");
        return;
    }

    if (!vr_create_instance(vm, activity, extensions)) return;
    if (!vr_get_system()) return;
    if (!vr_configure_resolution()) return;
    if (!vr_capture_egl_context()) return;
    if (!vr_verify_graphics_requirements()) return;
    if (!vr_create_session()) return;
    vr_setup_color_space();
    vr_init_controllers();
    if (!vr_create_play_space()) return;
    if (!vr_create_view_space()) return;
    if (!vr_create_swapchains()) return;
    if (!vr_create_eye_fbos()) return;

    // Swapchain dedicated to the menu quad panel
    if (!vr_create_menu_swapchain()) {
        vr_log("vr_create_menu_swapchain failed (non-fatal, menu quad disabled)");
    // non-fatal: continue without the panel
    }

    LOGI("========== OPENXR INIT (Android) COMPLETE ==========");
}
#else


extern "C" bool vrWaitForRuntime(int waitSeconds) {
    return vrEnsureDefaultRuntimeRunning();
}


static void vrDestroyInstanceIfNeeded(void) {
    if (g_vrState.instance != XR_NULL_HANDLE) {
        xrDestroyInstance(g_vrState.instance);
        g_vrState.instance = XR_NULL_HANDLE;
    }
}

static bool openxrInitializeVRwindowsInternal(void) {
    auto extensions = vr_enumerate_extensions();
    if (extensions.empty())              return false;
    if (!vr_create_instance(extensions)) return false;
    if (!vr_get_system())                  return false;
    if (!vr_configure_resolution())        return false;
    if (!vr_verify_graphics_requirements()) return false;
    if (!vr_create_session())              return false;
    vr_setup_color_space();
    vr_init_controllers();
    if (!vr_create_play_space())            return false;
    if (!vr_create_view_space())            return false;
    if (!vr_create_swapchains())           return false;
    if (!vr_create_eye_fbos())              return false;

    // Swapchain dedicated to the menu quad panel
    if (!vr_create_menu_swapchain()) {
        LOGE("vr_create_menu_swapchain failed (non-fatal, menu quad disabled)");
     // non-fatal: continue without the panel
    }

    return true;
}

extern "C" void openxr_initialize_vr_windows(void) {
    LOGI("========== OPENXR INIT (Windows) START ==========");
    g_vrState = VRState{};

    if (!openxrInitializeVRwindowsInternal()) {
        LOGE("OpenXR init failed, cleanup...");
        vrDestroyInstanceIfNeeded();
        return;
    }

    LOGI("========== OPENXR INIT (Windows) COMPLETE ==========");
}

#endif

// ============================================================================
// LIFECYCLE - Entry Point
// ============================================================================

extern "C" void vr_initialize()
{
#ifdef ANDROID
    if (g_vrInitialized || !g_activity || !g_window) return;

    EGLDisplay display = eglGetCurrentDisplay();
    EGLContext context  = eglGetCurrentContext();
    if (display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT) {
        LOGE("No OpenGL context available");
        return;
    }

    LOGI("Initializing from OpenGL thread (Android)");
    openxr_initialize_vr(g_vm, g_activity, g_window);
#else
    if (g_vrInitialized) return;

    LOGI("Initializing VR (Windows)");
    openxr_initialize_vr_windows();
#endif

    if (g_vrState.instance == XR_NULL_HANDLE || g_vrState.session == XR_NULL_HANDLE) {
        LOGE("vr_initialize: Failed to initialize OpenXR");
        return;
    }

    g_vrInitialized = true;
    LOGI("VR system ready");

}



// ----------------------------------------------------------------------
// STEREO: Actual horizontal lens offset ratio (NDC offset),
// derived from the asymmetric FOV reported by the OpenXR runtime for this eye.
// Replaces any fixed constant used to expand the scissor box.
// ----------------------------------------------------------------------
float vr_get_horizontal_fov_offset_ratio(int eye) {
    if (!g_vrInitialized || eye < 0 || eye > 1) return 0.0f;

    XrFovf& fov = g_frameViews[eye].fov;
    const float tanLeft  = std::tan(fov.angleLeft);
    const float tanRight = std::tan(fov.angleRight);
    const float denom = tanRight - tanLeft;
    if (std::fabs(denom) < 0.0001f) return 0.0f;

    // Fraction [-1, 1] indicating the optical center offset relative
    // to the geometric center of the image (0 = symmetric FOV)
    return (tanRight + tanLeft) / denom;
}

struct VrEyeFovTan {
    float tanLeft, tanRight, tanCenter, tanHalfWidth;
};

// Helper to compute the tangent of FOV angles for a given eye
VrEyeFovTan vr_get_eye_fov_tan(int eye) {
    XrFovf& fov = g_frameViews[eye].fov;
    VrEyeFovTan out;
    out.tanLeft  = tanf(fov.angleLeft);
    out.tanRight = tanf(fov.angleRight);
    out.tanCenter    = (out.tanLeft + out.tanRight) * 0.5f;
    out.tanHalfWidth = (out.tanRight - out.tanLeft) * 0.5f;
    return out;
}

extern float s_eye_offsets[8];

// -----------------------------------------------------------------------
// STEREO: Asymmetric projection from XrFovf angles
// -----------------------------------------------------------------------
int shaders_build_xr_projection(
        float tanLeft, float tanRight,
        float tanUp, float tanDown,
        float znear, float zfar,
        float outP[16])
{
    if (!outP) return 0;

    float rw = 1.0f / (tanRight - tanLeft);
    float rh = 1.0f / (tanUp - tanDown);
    float rz = 1.0f / (znear - zfar);

    // Reset to 0
    memset(outP, 0, 16 * sizeof(float));

    // DIRECT generation in N64 Row-Major format (transposed from OpenGL)
    outP[0 * 4 + 0] = 2.0f * rw;                    // P_n64[0][0]
    outP[1 * 4 + 1] = 2.0f * rh;                    // P_n64[1][1]

    outP[2 * 4 + 0] = (tanRight + tanLeft) * rw;    // P_n64[2][0] — offset X
    outP[2 * 4 + 1] = (tanUp + tanDown) * rh;       // P_n64[2][1] — offset Y
    outP[2 * 4 + 2] = (znear + zfar) * rz;          // P_n64[2][2] — scale Z
    outP[2 * 4 + 3] = -1.0f;                        // P_n64[2][3] — w_clip = -z

    outP[3 * 4 + 2] = 2.0f * znear * zfar * rz;     // P_n64[3][2] — offset Z

    g_camZNear = znear;
    g_camZFar = zfar;
    return 1;
}

// -----------------------------------------------------------------------
// STEREO: View matrix from XrPosef (position + quaternion)
// -----------------------------------------------------------------------
int shaders_build_xr_view(
        float px, float py, float pz,          // XrVector3f position
        float qx, float qy, float qz, float qw,// XrQuaternionf orientation
        float outV[16])
{
    if (!outV) return 0;

    // Convert the quaternion into a 3x3 rotation matrix
    float x2 = qx*qx, y2 = qy*qy, z2 = qz*qz;
    float xy = qx*qy, xz = qx*qz, yz = qy*qz;
    float wx = qw*qx, wy = qw*qy, wz = qw*qz;

    // Camera axes (row-major first, then transpose for the View matrix)
    float rx = 1 - 2*(y2+z2),  ux = 2*(xy-wz), fx = 2*(xz+wy);
    float ry = 2*(xy+wz),      uy = 1 - 2*(x2+z2), fy = 2*(yz-wx);
    float rz = 2*(xz-wy),      uz = 2*(yz+wx), fz = 1 - 2*(x2+y2);

    // Translations: -dot(axis, position)
    float tx = -(rx*px + ry*py + rz*pz);
    float ty = -(ux*px + uy*py + uz*pz);
    float tz = -(fx*px + fy*py + fz*pz);

    // Col-major for OpenGL
    outV[0]=rx; outV[1]=ry; outV[2]=rz; outV[3]=tx;
    outV[4]=ux; outV[5]=uy; outV[6]=uz; outV[7]=ty;
    outV[8]=fx; outV[9]=fy; outV[10]=fz;outV[11]=tz;
    outV[12]=0; outV[13]=0; outV[14]=0; outV[15]=1;
    return 1;
}

// ============================================================================
// FRAME LIFECYCLE - Begin Frame & Update Poses
// ============================================================================

// Sleep out the remainder of a nominal frame interval.
//
// xrWaitFrame below is the ONLY thing pacing the game's tick: the desktop mirror forces
// swap interval 0 every frame and Video.FramerateLimit defaults to 0, so nothing else
// throttles it. If the session dies and we just return early, mainTick free-runs -- it
// pegs a core and floods the GPU queue, which starves the desktop compositor and any
// streaming runtime alongside it. That is a whole-machine hang, not just a dead game.
static void vr_idle_pace()
{
    static uint64_t sLastTick = 0;

    const uint64_t freq = SDL_GetPerformanceFrequency();
    const uint64_t now  = SDL_GetPerformanceCounter();

    if (sLastTick != 0 && freq != 0) {
        const double elapsedMs = (double)(now - sLastTick) * 1000.0 / (double)freq;
        const double targetMs  = 1000.0 / 72.0;
        if (elapsedMs < targetMs) {
            SDL_Delay((Uint32)(targetMs - elapsedMs));
        }
    }

    sLastTick = SDL_GetPerformanceCounter();
}

extern "C" bool vr_begin_frame_and_update_poses()
{
    if (!g_vrState.sessionRunning || g_vrState.session == XR_NULL_HANDLE) {
        vr_idle_pace();
        return false;
    }

    XrFrameWaitInfo waitInfo{ XR_TYPE_FRAME_WAIT_INFO };
    g_frameState = { XR_TYPE_FRAME_STATE };
    XrResult r = xrWaitFrame(g_vrState.session, &waitInfo, &g_frameState);
    if (XR_FAILED(r)) {
        LOGE("xrWaitFrame failed: %d", (int)r);
        return false;
    }


    XrFrameBeginInfo beginInfo{ XR_TYPE_FRAME_BEGIN_INFO };
    r = xrBeginFrame(g_vrState.session, &beginInfo);
    if (XR_FAILED(r)) {
        LOGE("xrBeginFrame failed: %d", (int)r);
        return false;
    }

    g_frameStarted = true;

    if (!g_frameState.shouldRender) {
        vr_end_empty_frame(g_frameState.predictedDisplayTime);
        g_frameStarted = false;
        return false;
    }

    for (auto& v : g_frameViews) { v.type = XR_TYPE_VIEW; v.next = nullptr; }

    XrViewLocateInfo viewLocate{ XR_TYPE_VIEW_LOCATE_INFO };
    viewLocate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    viewLocate.displayTime           = g_frameState.predictedDisplayTime;
    viewLocate.space                 = g_vrState.playSpace;

    XrViewState viewState{ XR_TYPE_VIEW_STATE };
    uint32_t viewCount = 2;
    r = xrLocateViews(g_vrState.session, &viewLocate, &viewState, 2, &viewCount, g_frameViews.data());
    if (XR_FAILED(r) || viewCount < 2) {
        LOGE("xrLocateViews failed: %d", (int)r);
        vr_end_empty_frame(g_frameState.predictedDisplayTime);
        g_frameStarted = false;
        return false;
    }

    const XrViewStateFlags needed = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    if ((viewState.viewStateFlags & needed) != needed) {
        LOGE("viewStateFlags missing position/orientation");
        vr_end_empty_frame(g_frameState.predictedDisplayTime);
        g_frameStarted = false;
        return false;
    }

    // Build the symmetric projection used by the original game from the
    // average OpenXR tangent extents.  Using the angular span directly is
    // only correct for a perfectly symmetric frustum, and using the render
    // target dimensions is not guaranteed to match the optical frustum.
    // The per-eye centre offsets are applied later by the multiview shader.
    float tanHalfWidthSum = 0.0f;
    float tanHalfHeightSum = 0.0f;

    for (int eye = 0; eye < 2; eye++) {
        XrFovf& fov = g_frameViews[eye].fov;
        tanHalfWidthSum += (std::tanf(fov.angleRight) - std::tanf(fov.angleLeft)) * 0.5f;
        tanHalfHeightSum += (std::tanf(fov.angleUp) - std::tanf(fov.angleDown)) * 0.5f;
    }

    const float tanHalfWidth = tanHalfWidthSum * 0.5f;
    const float tanHalfHeight = tanHalfHeightSum * 0.5f;

    if (tanHalfWidth > 0.001f && tanHalfHeight > 0.001f) {
        XrFov = 2.0f * std::atanf(tanHalfHeight) * (180.0f / 3.14159265f);
        XrAspect = tanHalfWidth / tanHalfHeight;
    }

    static bool projectionLogged = false;
    if (!projectionLogged) {
        for (int eye = 0; eye < 2; eye++) {
            XrFovf& fov = g_frameViews[eye].fov;
            LOGI("OpenXR eye %d FOV radians: left=%.6f right=%.6f up=%.6f down=%.6f",
                 eye, fov.angleLeft, fov.angleRight, fov.angleUp, fov.angleDown);
        }
        LOGI("OpenXR game projection: vertical_fov=%.4f aspect=%.6f",
             XrFov, XrAspect);
        for (int eye = 0; eye < 2; eye++) {
            XrFovf& fov = g_frameViews[eye].fov;
            const float tanLeft = std::tanf(fov.angleLeft);
            const float tanRight = std::tanf(fov.angleRight);
            const float tanUp = std::tanf(fov.angleUp);
            const float tanDown = std::tanf(fov.angleDown);
            LOGI("OpenXR eye %d projection: scale_x=%.6f center_x=%.6f scale_y=%.6f center_y=%.6f",
                 eye,
                 2.0f / (tanRight - tanLeft),
                 (tanRight + tanLeft) / (tanRight - tanLeft),
                 2.0f / (tanUp - tanDown),
                 (tanUp + tanDown) / (tanUp - tanDown));
        }
        projectionLogged = true;
    }

    for (int eye = 0; eye < 2; eye++) {
        XrFovf&  fov  = g_frameViews[eye].fov;
        const XrPosef& pose = g_frameViews[eye].pose;

        float projMtx[16], viewMtx[16];

        shaders_build_xr_projection(
                std::tanf(fov.angleLeft),  std::tanf(fov.angleRight),
                std::tanf(fov.angleUp),    std::tanf(fov.angleDown),
                g_camZNear, g_camZFar, projMtx
        );
        shaders_build_xr_view(
                pose.position.x,    pose.position.y,    pose.position.z,
                pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w,
                viewMtx
        );

        std::memcpy(g_eyeProjMtx[eye], projMtx, 16 * sizeof(float));
        std::memcpy(g_eyeViewMtx[eye], viewMtx, 16 * sizeof(float));

        VrEyeFovTan fovTan = vr_get_eye_fov_tan(eye);
        float tanFovHalf = fovTan.tanHalfWidth;

        g_eyeTanHalfFov[eye] = tanFovHalf;

    }



    vr_update_head_tracking(g_frameState.predictedDisplayTime);
    update_vr_controllers(g_frameState.predictedDisplayTime);
    controller_pose();

    return true;
}


// ============================================================================
// STEREO API
// ============================================================================


float* vr_get_eye_proj_mtx(int eye) { return g_eyeProjMtx[eye]; }

extern "C" GLuint vr_get_current_multiview_swapchain_tex() {
    return g_currentMultiviewSwapchainTex;
}

// Begin OpenGL rendering for the current eye in multiview.
// Returns false when there is nothing valid to render into, in which case the caller must
// skip the display list rather than submit it against a dead swapchain.
bool vr_begin_eye_render()
{
    if (!gfx_get_current_rendering_api()->is_multiview()) return false;
    if (!vr_ensure_swapchain_images()) return false;


    XrSwapchainImageAcquireInfo acquireInfo = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    if (XR_FAILED(xrAcquireSwapchainImage(g_vrState.swapchains[0], &acquireInfo, &g_acquiredSwapchainImageIndex))) {
        return false;
    }
    g_swapchainImageAcquired = true;

    XrSwapchainImageWaitInfo waitInfo = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, nullptr, XR_INFINITE_DURATION };
    xrWaitSwapchainImage(g_vrState.swapchains[0], &waitInfo);


    GLuint swapchainTex = g_swapchainImages[0][g_acquiredSwapchainImageIndex].image;
    g_currentMultiviewSwapchainTex = swapchainTex;

    // For menu blur under the Oculus/Meta PC runtime
    if (is_meta_runtime && copy_fbo_menu) {
        gfx_copy_framebuffer(25, 0, 0, 0, true);
    }


    glBindFramebuffer(GL_FRAMEBUFFER, g_multiviewFBO);

    if (gfx_msaa_level > 1 && pfnFramebufferTextureMultisampleMultiviewOVR != nullptr) {
#ifdef ANDROID
        GLsizei safe_msaa = (gfx_msaa_level > 4) ? 4 : gfx_msaa_level; // limit to 4x
#else
        GLsizei safe_msaa = gfx_msaa_level; // no limit for PC

#endif
        pfnFramebufferTextureMultisampleMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,     swapchainTex,          0, safe_msaa, 0, 2);
        pfnFramebufferTextureMultisampleMultiviewOVR(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, g_multiviewDepthArray, 0, safe_msaa, 0, 2);
    } else if (glFramebufferTextureMultiviewOVR) {
        glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,        swapchainTex,          0, 0, 2);
        glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, g_multiviewDepthArray, 0, 0, 2);
    }

    glViewport(0, 0, g_internalRenderWidth, g_internalRenderHeight);
    glScissor(0, 0, g_internalRenderWidth, g_internalRenderHeight);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    return true;
}

// End OpenGL rendering for the current eye and release swapchain image
void vr_end_eye_render()
{
    if (!gfx_get_current_rendering_api()->is_multiview()) return;
    if (!g_swapchainImageAcquired) return;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    XrSwapchainImageReleaseInfo releaseInfo = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    xrReleaseSwapchainImage(g_vrState.swapchains[0], &releaseInfo);
    g_swapchainImageAcquired = false;

}

void vr_get_eye_view_offset(int eye, float* out_tx, float* out_ty, float* out_tz, float* out_tx_HUD)
{
    if (!g_vrInitialized || eye < 0 || eye > 1) {
        if (out_tx)     *out_tx     = 0.0f;
        if (out_ty)     *out_ty     = 0.0f;
        if (out_tz)     *out_tz     = 0.0f;
        if (out_tx_HUD) *out_tx_HUD = 0.0f;
        return;
    }

    // Actual distance between the two eyes, measured by the OpenXR runtime
    const XrVector3f& posL = g_frameViews[0].pose.position;
    const XrVector3f& posR = g_frameViews[1].pose.position;

    float dx = posR.x - posL.x;
    float dy = posR.y - posL.y;
    float dz = posR.z - posL.z;

    ipd_meters = std::sqrt(dx * dx + dy * dy + dz * dz);

    if (ipd_meters < 0.01f || ipd_meters > 0.10f)
        ipd_meters = 0.064f;

    float localEyeX = (eye == 0 ? -ipd_meters * 0.5f : ipd_meters * 0.5f);

    if (out_tx)   *out_tx   = localEyeX * vr_world_scale;
    if (out_ty)   *out_ty   = 0.0f;
    if (out_tz)   *out_tz   = 0.0f;

    if (out_tx_HUD) {
        VrEyeFovTan fovTan = vr_get_eye_fov_tan(eye);

        // Use localEyeX (not affected by vr_world_scale) for HUD parallax calculation
        float parallaxOffset = localEyeX * (VrStereoCrosshair * fovTan.tanHalfWidth);

        // Canting managed separately via vr_get_horizontal_fov_offset_ratio
        float cantingOffset = vr_get_horizontal_fov_offset_ratio(eye);

        *out_tx_HUD = parallaxOffset + cantingOffset;
    }
}


// Compute crosshair parallax correction based on target distance
extern "C" float vrComputeCrosshairParallax(float distanceGameUnits) {
    if (!g_vrInitialized) return 0.0f;

    const float kMinDistMeters = 1.75f;
    float distMeters = distanceGameUnits / 100.0f;
    if (distMeters < kMinDistMeters) distMeters = kMinDistMeters;

    float ipdMeters = ipd_meters;
    if (ipdMeters < 0.01f || ipdMeters > 0.10f) ipdMeters = 0.064f;

    float eyeHalfIpd = ipdMeters * 0.5f;

    VrEyeFovTan fovTan = vr_get_eye_fov_tan(0);
    float tanFovHalf = fovTan.tanHalfWidth;

    if (tanFovHalf < 0.001f) return 0.0f;

    float parallaxNominal = (eyeHalfIpd / kMinDistMeters) / tanFovHalf;
    float parallaxTarget  = (eyeHalfIpd / distMeters)     / tanFovHalf;

    float correction = parallaxTarget - parallaxNominal;
    if (correction > 0.02f) correction = 0.02f;
    if (correction < -0.02f) correction = -0.02f;

    return correction;
}


// ============================================================================
// FRAME LIFECYCLE - Submit
// ============================================================================

extern "C" bool vr_end_frame_and_submit()
{
    if (!g_vrState.sessionRunning || g_vrState.session == XR_NULL_HANDLE)
        return false;
    if (!g_frameStarted)
        return false;

    bool submitted = false;
    if (gfx_get_current_rendering_api()->is_multiview()) {
        vr_submit_frame(g_frameState, g_frameViews);
        submitted = true;
    }

    // xrEndFrame has run (or there was nothing to submit), so the frame is closed. This
    // used to be left set on the success path, which meant g_frameStarted was stuck true
    // for the rest of the process: the "called during open frame" guard in vr_shutdown
    // fired unconditionally and could not distinguish a genuine mid-frame teardown.
    g_frameStarted = false;

    return submitted;
}

// ============================================================================
// VR Shutdown
// ============================================================================
extern "C" void vr_shutdown()
{
    LOGI("========== VR SHUTDOWN START ==========");

    // 1. Ensure no frame is currently in progress
    // (if we are between begin/end, we cannot destroy cleanly)
    if (g_frameStarted) {
        LOGI("vr_shutdown: called during open frame! Forcing end.");
        // Submit an empty frame to release the runtime
        vr_end_empty_frame(g_frameState.predictedDisplayTime);
        g_frameStarted = false;
    }
    // 2. FBOs OpenGL
    if (g_multiviewFBO) {
        glDeleteFramebuffers(1, &g_multiviewFBO);
        g_multiviewFBO = 0;
    }
    if (g_multiviewDepthArray) {
        glDeleteTextures(1, &g_multiviewDepthArray);
        g_multiviewDepthArray = 0;
    }
    g_currentMultiviewSwapchainTex = 0;

    // 3. Swapchains. Hand back any image still checked out before destroying anything --
    // tearing down a swapchain while the runtime thinks we hold one of its images is how a
    // teardown that lands mid-frame leaves an out-of-process runtime in a bad state.
    if (g_swapchainImageAcquired && g_vrState.swapchains[0] != XR_NULL_HANDLE) {
        XrSwapchainImageReleaseInfo releaseInfo = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
        xrReleaseSwapchainImage(g_vrState.swapchains[0], &releaseInfo);
    }
    g_swapchainImageAcquired = false;

    // Destroy both handles: [1] only exists when multiview is off, and is XR_NULL_HANDLE
    // otherwise. Only [0] is ever enumerated, so only its image cache needs clearing.
    for (int eye = 0; eye < 2; eye++) {
        if (g_vrState.swapchains[eye] != XR_NULL_HANDLE) {
            xrDestroySwapchain(g_vrState.swapchains[eye]);
            g_vrState.swapchains[eye] = XR_NULL_HANDLE;
        }
    }
    g_swapchainImagesInit[0] = false;
    g_swapchainImages[0].clear();

    if (g_menuSwapchain != XR_NULL_HANDLE) {
        xrDestroySwapchain(g_menuSwapchain);
        g_menuSwapchain = XR_NULL_HANDLE;
        g_menuSwapchainImages.clear();
    }
    if (g_menuSwapchainR != XR_NULL_HANDLE) {
        xrDestroySwapchain(g_menuSwapchainR);
        g_menuSwapchainR = XR_NULL_HANDLE;
        g_menuSwapchainImagesR.clear();
    }
    if (g_menuSwapchainH != XR_NULL_HANDLE) {
        xrDestroySwapchain(g_menuSwapchainH);
        g_menuSwapchainH = XR_NULL_HANDLE;
        g_menuSwapchainImagesH.clear();
    }

    // 4. Reference spaces
    if (g_vrState.viewSpace != XR_NULL_HANDLE) {
        xrDestroySpace(g_vrState.viewSpace);
        g_vrState.viewSpace = XR_NULL_HANDLE;
    }
    if (g_vrState.playSpace != XR_NULL_HANDLE) {
        xrDestroySpace(g_vrState.playSpace);
        g_vrState.playSpace = XR_NULL_HANDLE;
    }
    // 5. Session
    if (g_vrState.session != XR_NULL_HANDLE) {
        if (g_vrState.sessionRunning) {
            xrEndSession(g_vrState.session);
            g_vrState.sessionRunning = false;
        }
        xrDestroySession(g_vrState.session);
        g_vrState.session = XR_NULL_HANDLE;
    }
    // 6. Instance
    if (g_vrState.instance != XR_NULL_HANDLE) {
        xrDestroyInstance(g_vrState.instance);
        g_vrState.instance = XR_NULL_HANDLE;
    }
    // 7. Reset globals
    g_vrInitialized = false;
    g_internalRenderWidth  = 0;
    g_internalRenderHeight = 0;
    g_frameStarted = false;

    LOGI("========== VR SHUTDOWN COMPLETE ==========");
}

// ============================================================================
// Restart VR with new scale
// ============================================================================

extern "C" bool vr_restart_with_new_scale(float new_scale) {
    LOGI("vr_restart_with_new_scale: %.2f -> %.2f", RENDER_SCALE, new_scale);
    RENDER_SCALE = new_scale;
    vr_shutdown();

    vr_initialize();
    if (!g_vrInitialized) {
        LOGE("vr_restart_with_new_scale: re-init FAILED at scale %.2f", new_scale);
    }
    return g_vrInitialized;
}

// A resolution change arrives from the options menu, which the game ticks with an OpenXR
// frame open. Rebuilding the instance there is what wedges the runtime, so the request is
// parked here and acted on from vr_apply_pending_scale() once the frame has been submitted.
static float g_pendingRenderScale = 0.0f;

extern "C" void vr_request_scale(float new_scale) {
    if (new_scale > 0.0f) {
        g_pendingRenderScale = new_scale;
    }
}

// Called from mainTick after vr_end_frame_and_submit(), where no frame is open.
extern "C" bool vr_apply_pending_scale(void) {
    const float scale = g_pendingRenderScale;
    if (scale <= 0.0f) {
        return true;
    }
    g_pendingRenderScale = 0.0f;

    if (scale == RENDER_SCALE) {
        return true;
    }

    return vr_restart_with_new_scale(scale);
}

