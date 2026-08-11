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
extern XrQuaternionf vr_recenter_rot_Q;
extern float XrAspect;

extern float gCtrlPos[2][3];
extern float gCtrlQuat[2][4];
extern float gCtrlQuatRaw[2][4];

// Gesture frame. Everything above is head-relative and carries the engine's
// mirrored basis; these are located against playSpace in raw OpenXR axes, with
// no mirror, no 90deg gun offset, no smoothing and no recoil. Swing gestures
// need one self-consistent basis, which is what this pair is for.
extern float vr_ctrl_quat_play[2][4];     // {w,x,y,z}
extern float vr_ctrl_velocity_play[2][3]; // m/s
extern float vr_head_velocity_play[3];    // m/s

// A plausible standing head (HMD) height in centimetres, used only until real
// tracking arrives. Everything that cares about the player's actual height uses
// the VrPlayerHeight setting.
#define VR_NOMINAL_HEAD_HEIGHT_CM 160.0f

// Head height above the physical floor, in centimetres. Under a floor-relative
// reference space this is exactly what the runtime reports; under LOCAL it is
// the reported Y plus a one-shot calibration offset.
extern float gVrHeadHeightCm;
extern bool gVrFloorRelativeSpace;

extern bool vr_init_done;
extern float vr_world_scale;

extern uint32_t VrRecommendedW;
extern uint32_t VrRecommendedH;


typedef enum {
    VR_EYEHEIGHT_STAND = 0,
    VR_EYEHEIGHT_DUCK,
    VR_EYEHEIGHT_SQUAT
} VrEyeheightMode;


#define VR_MENU_HUD_CAPTURE_BEGIN_L 0x56530000
#define VR_MENU_HUD_CAPTURE_END_L   0x56530001
#define VR_WEP_HUD_CAPTURE_BEGIN_L 0x56550000
#define VR_WEP_HUD_CAPTURE_END_L   0x56550001
extern void gfx_vr_hud_capture_begin_L(void);
extern void gfx_vr_hud_capture_end_L(void);

#define VR_WEP_HUD_CAPTURE_BEGIN_R 0x56560000
#define VR_WEP_HUD_CAPTURE_END_R   0x56560001
extern void gfx_vr_hud_capture_begin_R(void);
extern void gfx_vr_hud_capture_end_R(void);

#define VR_HUD_CAPTURE_BEGIN_H 0x56570000
#define VR_HUD_CAPTURE_END_H 0x56570001
extern void gfx_vr_hud_capture_begin_H(void);
extern void gfx_vr_hud_capture_end_H(void);

#ifdef __cplusplus
extern "C" {
#endif

// Restart the LOCAL-space height calibration (called at level start).
void vr_recalibrate_head_height(void);

#ifdef __cplusplus
}
#endif

