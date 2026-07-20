// vr_android_jni.cpp

#ifdef ANDROID
#define XR_USE_PLATFORM_ANDROID 1
#define XR_USE_GRAPHICS_API_OPENGL_ES 1

#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "vr_openxr.h"

static const char* TAG = "PD-VR";

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)


// Globals
jobject g_activity = nullptr;
static jobject g_appContext = nullptr;
ANativeWindow* g_window = nullptr;
JavaVM* g_vm = nullptr;
static bool g_loaderInitialized = false;

// Declared elsewhere (called from your GL thread via vr_initialize())
extern "C" void openxr_initialize_vr(JavaVM* vm, jobject activity, ANativeWindow* window);

extern "C" JNIEXPORT void JNICALL
Java_com_perfectdark_port_MainActivity_nativeSetVrJavaContext(
        JNIEnv* env, jclass, jobject activity, jobject surface) {

    LOGI("nativeSetVrJavaContext called (activity=%p, surface=%p)", activity, surface);

// 1) JavaVM (only once)
    if (!g_vm) {
        if (env->GetJavaVM(&g_vm) != JNI_OK) {
            LOGE("Failed to get JavaVM");
            return;
        }
        LOGI("JavaVM acquired: %p", g_vm);
    }

// 2) Activity (avoid deleting/recreating it while OpenXR is running)
    if (!g_activity && activity) {
        g_activity = env->NewGlobalRef(activity);
        LOGI("Activity global ref created: %p", g_activity);

// 3) ApplicationContext (CRITICAL: do not DeleteGlobalRef / do not recreate)
        if (!g_appContext) {
            jclass activityClass = env->GetObjectClass(activity);
            if (!activityClass) {
                LOGE("Failed to get Activity class");
                return;
            }

            jmethodID getAppCtx = env->GetMethodID(activityClass,
                                                   "getApplicationContext",
                                                   "()Landroid/content/Context;");

            if (!getAppCtx) {
                LOGE("Failed to get getApplicationContext method");
                env->DeleteLocalRef(activityClass);
                return;
            }

            jobject appCtxLocal = env->CallObjectMethod(activity, getAppCtx);
            if (!appCtxLocal) {
                LOGE("getApplicationContext returned null");
                env->DeleteLocalRef(activityClass);
                return;
            }

            g_appContext = env->NewGlobalRef(appCtxLocal);
            env->DeleteLocalRef(appCtxLocal);
            env->DeleteLocalRef(activityClass);

            LOGI("ApplicationContext global ref created: %p", g_appContext);
        }
    }

// 4) Window - Free the old window if it exists
    if (g_window) {
        LOGI("Releasing previous ANativeWindow: %p", g_window);
        ANativeWindow_release(g_window);
        g_window = nullptr;
    }

// Create the new window ONLY if surface is non-null
    if (surface) {
        g_window = ANativeWindow_fromSurface(env, surface);

        if (g_window) {
            LOGI("ANativeWindow created from Surface: %p", g_window);
        } else {
            LOGE("ANativeWindow_fromSurface failed despite non-null surface");
        }
    } else {
        LOGE("Surface is null, no ANativeWindow created");
    }

    LOGI("Android context stored (vm=%p activity=%p appCtx=%p window=%p)",
         g_vm, g_activity, g_appContext, g_window);

// 5) Init loader only once, and keep the same g_appContext afterwards
    if (!g_loaderInitialized && g_vm && g_appContext) {
        g_loaderInitialized = true;

        PFN_xrInitializeLoaderKHR xrInitializeLoaderKHR = nullptr;
        XrResult r = xrGetInstanceProcAddr(
                XR_NULL_HANDLE,
                "xrInitializeLoaderKHR",
                (PFN_xrVoidFunction*)&xrInitializeLoaderKHR);

        if (XR_SUCCEEDED(r) && xrInitializeLoaderKHR) {
            XrLoaderInitInfoAndroidKHR loaderInfo{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
            loaderInfo.applicationVM = g_vm;
            loaderInfo.applicationContext = g_appContext;

            r = xrInitializeLoaderKHR((const XrLoaderInitInfoBaseHeaderKHR*)&loaderInfo);

            if (XR_SUCCEEDED(r)) {
                LOGI("xrInitializeLoaderKHR succeeded: %d", r);
            } else {
                LOGE("xrInitializeLoaderKHR failed: %d", r);
            }
        } else {
            LOGE("Failed to get xrInitializeLoaderKHR function pointer: %d", r);
        }
    }
}
#endif
