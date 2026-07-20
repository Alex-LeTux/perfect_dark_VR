#pragma once

#include <openxr/openxr.h>
#include <SDL.h>

#ifdef ANDROID
#include <openxr/openxr_platform.h>
#include <android/log.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "PD-VR", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "PD-VR", __VA_ARGS__)
#else
#define LOGI(...) vr_log(__VA_ARGS__)
#define LOGE(...) vr_log(__VA_ARGS__)
#endif


#ifdef __cplusplus
#ifdef ANDROID
// Global Variable Android (vr_android_jni.cpp)
extern jobject g_activity;        // ApplicationContext
extern ANativeWindow* g_window;   // Surface native
extern JavaVM* g_vm;              // Java VM
#endif

struct VRState {
    XrInstance instance;
    XrSystemId systemId;
    XrSession session;
    XrSpace playSpace;
    XrSpace viewSpace;
    XrSwapchain swapchains[2];
    bool sessionRunning;
};

extern VRState g_vrState;

int shaders_build_xr_view(
        float px, float py, float pz,
        float qx, float qy, float qz, float qw,
        float outV[16]);

int shaders_build_xr_projection(
        float tanLeft, float tanRight,
        float tanUp,   float tanDown,
        float znear,   float zfar,
        float outP[16]);


#endif


extern XrVector3f gHeadPos;
extern XrQuaternionf vr_HMD_rot_Q;
extern XrQuaternionf gRawHeadQ;

extern float gCtrlPos[2][3];
extern float gCtrlQuat[2][4];

extern float gStandingHeadHeight;
extern bool vr_init_done;
extern float vr_world_scale;

extern uint32_t VrRecommendedW;
extern uint32_t VrRecommendedH;


typedef enum {
    VR_EYEHEIGHT_STAND = 0,
    VR_EYEHEIGHT_DUCK,
    VR_EYEHEIGHT_SQUAT
} VrEyeheightMode;

