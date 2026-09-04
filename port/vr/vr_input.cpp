#ifndef XR_USE_GRAPHICS_API_OPENGL_ES
#define XR_USE_GRAPHICS_API_OPENGL_ES
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif


#include <openxr/openxr.h>

#include <cstring>
#include <vector>
#include <array>
#include <cstdio>
#include <cstdarg>
#include <cmath>

#include "vr_input.h"
#include "vr_log.h"
#include "vr_openxr.h"


#ifdef ANDROID
#include <openxr/openxr_platform.h>
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "PD-VR", __VA_ARGS__)
#else
#define LOGI(...) printf(__VA_ARGS__)
#endif

#include <constants.h>



extern int weaponnum;
extern bool VR_FUNC_SECONDARY;
extern int vr_button_R_grip;
extern int vr_button_L_grip;
extern bool VrMotionThrowing;
bool WepCanZoom = false;
bool VrWeaponRecoil = true;
extern "C" bool VrTwoHandsGun(int weaponnum);
bool gripPressed = false;
int VrLeftHandedMode = 0;

// ===== VR CODE EXTENSION WITH FULL CONTROLLER SUPPORT =====

XrSpaceVelocity gCachedVelocity[2] = {};
XrSpaceLocation spaceLocation;
float vr_ctrl_velocity[2][3]; // [ctrlIndex][x,y,z]
bool gIsValveIndex = false;

// --- Gesture frame, located against playSpace ---
// The pose above is located against viewSpace, which is what weapon placement
// wants. Its velocity is not usable for gestures: a velocity relative to a
// moving VIEW space needs the runtime to track that space's own velocity, and
// runtimes that don't just hand back the room-fixed tracking velocity instead.
// Asking for playSpace explicitly is unambiguous on every runtime, so all swing
// gestures use these and apply the play -> world transform themselves.
static XrPosef gCtrlPosePlay[2] = {};
static XrSpaceVelocity gCachedVelocityPlay[2] = {};
float vr_ctrl_quat_play[2][4];     // {w,x,y,z}, raw OpenXR basis, play space
float vr_ctrl_velocity_play[2][3]; // m/s,       raw OpenXR basis, play space

// ===== MANUAL RELOADING =========================

float gVrReloadPullLocalX = 0.0f;
float gVrReloadPullLocalY = 0.0f;
float gVrReloadPullLocalZ = 0.0f;

static bool  sReloadPrevCaptured = false;
static float sReloadRelPrevX = 0.0f;
static float sReloadRelPrevY = 0.0f;
static float sReloadRelPrevZ = 0.0f;

// Inverse of a quaternion (w,x,y,z) — for a unit quaternion, this is the conjugate
/*static void QuatInverse(const float q[4], float out[4]) {
    out[0] =  q[0];   // w
    out[1] = -q[1];   // -x
    out[2] = -q[2];   // -y
    out[3] = -q[3];   // -z
}*/

// ============================================================
// SMOOTHING POSITION + ROTATION — Both controllers
// ============================================================

#define CTRL_SMOOTH_ALPHA_POS       1.00f   // Position with grip 1.0f = none
#define CTRL_SMOOTH_ALPHA_ROT_GRIP  0.10f   // Rotation with grip (strong)
#define CTRL_SMOOTH_ALPHA_ROT_IDLE  0.50f   // Rotation without grip (very light)


static float  sSmoothedPos[2][3]  = {{0,0,0},{0,0,0}};
static float  sSmoothedQuat[2][4] = {{0,0,0,1},{0,0,0,1}};
static bool   sSmoothedInit[2]    = {false, false};
//==================================================

XrActionSet gActionSet = XR_NULL_HANDLE;

// === BASIC ACTIONS ===
XrAction gPoseAction = XR_NULL_HANDLE;           // Controller positions (existing)
XrAction gSelectAction = XR_NULL_HANDLE;         // Main button (trigger/select)
XrAction gGripAction = XR_NULL_HANDLE;           // Grip/Squeeze
XrAction gMenuAction = XR_NULL_HANDLE;           // Menu button
XrAction gButtonAAction = XR_NULL_HANDLE;        // A button (right hand)
XrAction gButtonBAction = XR_NULL_HANDLE;        // B button (right hand)
XrAction gButtonXAction = XR_NULL_HANDLE;        // X button (left hand)
XrAction gButtonYAction = XR_NULL_HANDLE;        // Y button (left hand)
XrAction gThumbstickAction = XR_NULL_HANDLE;     // 2D joystick
XrAction gThumbstickClickAction = XR_NULL_HANDLE;// Joystick click
XrAction gTrackpadAction = XR_NULL_HANDLE;       // 2D trackpad
XrAction gTrackpadClickAction = XR_NULL_HANDLE;  // Trackpad click
XrAction gTrackpadTouchAction = XR_NULL_HANDLE;  // Trackpad touch
XrAction gTriggerValueAction = XR_NULL_HANDLE;   // Analog trigger value
XrAction gGripValueAction = XR_NULL_HANDLE;      // Analog grip value
XrAction gHapticAction = XR_NULL_HANDLE;         // Haptic vibration

XrAction gGripForceAction = XR_NULL_HANDLE; // Physical force (Valve Index only)

XrSpace gControllerSpace[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE}; // Left/Right controller spaces

// Controller positions and orientations
float gCtrlPos[2][3] = { {0,0,0}, {0,0,0} };
float gCtrlQuat[2][4] = { {0,0,0,1}, {0,0,0,1} };
float gCtrlQuatRaw[2][4] = {{0,0,0,1}, {0,0,0,1}};

ControllerInputState gControllerStates[2]; // [0] = left, [1] = right
XrPath gHandPaths[2] = { XR_NULL_PATH, XR_NULL_PATH };

// === HELPER FUNCTIONS ===
XrResult vr_string_to_path(XrInstance instance, const char* path_string, XrPath* path) {
    return xrStringToPath(instance, path_string, path);
}


// === CREATION OF ACTIONS AND INTERACTION PROFILES ===
XrResult create_vr_controllers_complete() {
    vr_log("[VR_CTRL] Complete initialization of VR controllers");

    // 1. Subaction paths (left/right)
    XrPath subactionPaths[2] = {XR_NULL_PATH, XR_NULL_PATH};
    XR_TRY(vr_string_to_path(g_vrState.instance, "/user/hand/left", &subactionPaths[0]));
    XR_TRY(vr_string_to_path(g_vrState.instance, "/user/hand/right", &subactionPaths[1]));

    gHandPaths[0] = subactionPaths[0];
    gHandPaths[1] = subactionPaths[1];

    // 2. Create the ActionSet
    XrActionSetCreateInfo actionSetInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::strncpy(actionSetInfo.actionSetName, "vr_game_actions", sizeof(actionSetInfo.actionSetName) - 1);
    std::strncpy(actionSetInfo.localizedActionSetName, "VR Game Actions", sizeof(actionSetInfo.localizedActionSetName) - 1);
    actionSetInfo.priority = 0;
    XR_TRY(xrCreateActionSet(g_vrState.instance, &actionSetInfo, &gActionSet));

    // === HELPER TO CREATE ACTIONS ===
    auto CreateAction = [&](XrAction& action, const char* name, const char* localized_name,
                            XrActionType type, bool use_subactions = true) -> XrResult {
        XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
        std::strncpy(actionInfo.actionName, name, sizeof(actionInfo.actionName) - 1);
        std::strncpy(actionInfo.localizedActionName, localized_name, sizeof(actionInfo.localizedActionName) - 1);
        actionInfo.actionType = type;

        if (use_subactions) {
            actionInfo.countSubactionPaths = 2;
            actionInfo.subactionPaths = subactionPaths;
        } else {
            actionInfo.countSubactionPaths = 0;
            actionInfo.subactionPaths = nullptr;
        }

        return xrCreateAction(gActionSet, &actionInfo, &action);
    };

    // 3. Create all actions
    XR_TRY(CreateAction(gPoseAction, "controller_pose", "Controller Pose", XR_ACTION_TYPE_POSE_INPUT));
    XR_TRY(CreateAction(gSelectAction, "select_button", "Select Button", XR_ACTION_TYPE_BOOLEAN_INPUT));
    XR_TRY(CreateAction(gGripAction, "grip_button", "Grip Button", XR_ACTION_TYPE_BOOLEAN_INPUT));
    XR_TRY(CreateAction(gMenuAction, "menu_button", "Menu Button", XR_ACTION_TYPE_BOOLEAN_INPUT));
    XR_TRY(CreateAction(gButtonAAction, "button_a", "Button A", XR_ACTION_TYPE_BOOLEAN_INPUT));
    XR_TRY(CreateAction(gButtonBAction, "button_b", "Button B", XR_ACTION_TYPE_BOOLEAN_INPUT));
    XR_TRY(CreateAction(gButtonXAction, "button_x", "Button X", XR_ACTION_TYPE_BOOLEAN_INPUT));
    XR_TRY(CreateAction(gButtonYAction, "button_y", "Button Y", XR_ACTION_TYPE_BOOLEAN_INPUT));
    XR_TRY(CreateAction(gThumbstickAction, "thumbstick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT));
    XR_TRY(CreateAction(gThumbstickClickAction, "thumbstick_click", "Thumbstick Click", XR_ACTION_TYPE_BOOLEAN_INPUT));
    XR_TRY(CreateAction(gTrackpadAction, "trackpad", "Trackpad", XR_ACTION_TYPE_VECTOR2F_INPUT));
    XR_TRY(CreateAction(gTrackpadClickAction, "trackpad_click", "Trackpad Click", XR_ACTION_TYPE_BOOLEAN_INPUT));
    XR_TRY(CreateAction(gTrackpadTouchAction, "trackpad_touch", "Trackpad Touch", XR_ACTION_TYPE_BOOLEAN_INPUT));
    XR_TRY(CreateAction(gTriggerValueAction, "trigger_value", "Trigger Value", XR_ACTION_TYPE_FLOAT_INPUT));
    XR_TRY(CreateAction(gGripValueAction, "grip_value", "Grip Value", XR_ACTION_TYPE_FLOAT_INPUT));
    XR_TRY(CreateAction(gHapticAction, "haptic_output", "Haptic Feedback", XR_ACTION_TYPE_VIBRATION_OUTPUT));

    XR_TRY(CreateAction(gGripForceAction, "grip_force", "Grip Force", XR_ACTION_TYPE_FLOAT_INPUT));

    // === MULTIPLE INTERACTION PROFILES ===

    // Helper to suggest bindings
    auto SuggestBindings = [&](const char* profile_path,
                               const std::vector<std::pair<XrAction, const char*>>& bindings) -> XrResult {
        XrPath profilePath = XR_NULL_PATH;
        XrResult r = vr_string_to_path(g_vrState.instance, profile_path, &profilePath);
        if (XR_FAILED(r)) {
            vr_log("[VR_CTRL] Profile %s path failed, skipping", profile_path);
            return XR_SUCCESS;
        }

        std::vector<XrActionSuggestedBinding> suggestedBindings;
        for (const auto& binding : bindings) {
            XrPath bindingPath = XR_NULL_PATH;
            r = vr_string_to_path(g_vrState.instance, binding.second, &bindingPath);
            if (XR_FAILED(r)) continue;
            suggestedBindings.push_back({binding.first, bindingPath});
        }

        XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        suggested.interactionProfile = profilePath;
        suggested.suggestedBindings = suggestedBindings.data();
        suggested.countSuggestedBindings = static_cast<uint32_t>(suggestedBindings.size());

        XrResult result = xrSuggestInteractionProfileBindings(g_vrState.instance, &suggested);
        vr_log("[VR_CTRL] Profile %s -> %s", profile_path, (result == XR_SUCCESS) ? "OK" : "FAILED");
        return XR_SUCCESS;
    };

    // 4. KHR SIMPLE CONTROLLER PROFILE (fallback)
    SuggestBindings("/interaction_profiles/khr/simple_controller", {
            {gPoseAction, "/user/hand/left/input/grip/pose"},
            {gPoseAction, "/user/hand/right/input/grip/pose"},
            {gSelectAction, "/user/hand/left/input/select/click"},
            {gSelectAction, "/user/hand/right/input/select/click"},
            {gMenuAction, "/user/hand/left/input/menu/click"},
            {gMenuAction, "/user/hand/right/input/menu/click"},
            {gHapticAction, "/user/hand/left/output/haptic"},
            {gHapticAction, "/user/hand/right/output/haptic"}
    });

    // 5. OCULUS TOUCH CONTROLLER PROFILE
    SuggestBindings("/interaction_profiles/oculus/touch_controller", {
            // Poses
            {gPoseAction, "/user/hand/left/input/grip/pose"},
            {gPoseAction, "/user/hand/right/input/grip/pose"},

            // Main buttons
            {gSelectAction, "/user/hand/left/input/trigger/value"},
            {gSelectAction, "/user/hand/right/input/trigger/value"},
            {gTriggerValueAction, "/user/hand/left/input/trigger/value"},
            {gTriggerValueAction, "/user/hand/right/input/trigger/value"},

            // Grip
            {gGripAction, "/user/hand/left/input/squeeze/value"},
            {gGripAction, "/user/hand/right/input/squeeze/value"},
            {gGripValueAction, "/user/hand/left/input/squeeze/value"},
            {gGripValueAction, "/user/hand/right/input/squeeze/value"},

            // A/B/X/Y buttons
            {gButtonXAction, "/user/hand/left/input/x/click"},
            {gButtonYAction, "/user/hand/left/input/y/click"},
            {gButtonAAction, "/user/hand/right/input/a/click"},
            {gButtonBAction, "/user/hand/right/input/b/click"},

            // Menu
            {gMenuAction, "/user/hand/left/input/menu/click"},

            // Thumbstick
            {gThumbstickAction, "/user/hand/left/input/thumbstick"},
            {gThumbstickAction, "/user/hand/right/input/thumbstick"},
            {gThumbstickClickAction, "/user/hand/left/input/thumbstick/click"},
            {gThumbstickClickAction, "/user/hand/right/input/thumbstick/click"},

            // Haptic
            {gHapticAction, "/user/hand/left/output/haptic"},
            {gHapticAction, "/user/hand/right/output/haptic"}
    });

    // 6. VALVE INDEX CONTROLLER PROFILE
    SuggestBindings("/interaction_profiles/valve/index_controller", {
            // Poses
            {gPoseAction, "/user/hand/left/input/grip/pose"},
            {gPoseAction, "/user/hand/right/input/grip/pose"},

            // Trigger / Grip
            {gSelectAction, "/user/hand/left/input/trigger/click"},
            {gSelectAction, "/user/hand/right/input/trigger/click"},
            {gTriggerValueAction, "/user/hand/left/input/trigger/value"},
            {gTriggerValueAction, "/user/hand/right/input/trigger/value"},
//            {gGripAction, "/user/hand/left/input/squeeze/value"}, // removed to use gGripValueAction
//            {gGripAction, "/user/hand/right/input/squeeze/value"}, // removed to use gGripValueAction
            {gGripValueAction, "/user/hand/left/input/squeeze/value"},
            {gGripValueAction, "/user/hand/right/input/squeeze/value"},

            // A/B buttons
            {gButtonAAction, "/user/hand/left/input/a/click"},
            {gButtonAAction, "/user/hand/right/input/a/click"},
            {gButtonBAction, "/user/hand/left/input/b/click"},
            {gButtonBAction, "/user/hand/right/input/b/click"},

            // Thumbstick
            {gThumbstickAction, "/user/hand/left/input/thumbstick"},
            {gThumbstickAction, "/user/hand/right/input/thumbstick"},
            {gThumbstickClickAction, "/user/hand/left/input/thumbstick/click"},
            {gThumbstickClickAction, "/user/hand/right/input/thumbstick/click"},

            // Trackpad
            {gTrackpadAction, "/user/hand/left/input/trackpad"},
            {gTrackpadAction, "/user/hand/right/input/trackpad"},
            {gTrackpadClickAction, "/user/hand/left/input/trackpad/click"},
            {gTrackpadClickAction, "/user/hand/right/input/trackpad/click"},
            {gTrackpadTouchAction, "/user/hand/left/input/trackpad/touch"},
            {gTrackpadTouchAction, "/user/hand/right/input/trackpad/touch"},

            // Haptic
            {gHapticAction, "/user/hand/left/output/haptic"},
            {gHapticAction, "/user/hand/right/output/haptic"},

            {gGripForceAction, "/user/hand/left/input/squeeze/force"},
            {gGripForceAction, "/user/hand/right/input/squeeze/force"}
    });

    // 7. HTC VIVE CONTROLLER PROFILE
    SuggestBindings("/interaction_profiles/htc/vive_controller", {
            // Poses
            {gPoseAction, "/user/hand/left/input/grip/pose"},
            {gPoseAction, "/user/hand/right/input/grip/pose"},

            // Buttons
            {gSelectAction, "/user/hand/left/input/trigger/click"},
            {gSelectAction, "/user/hand/right/input/trigger/click"},
            {gTriggerValueAction, "/user/hand/left/input/trigger/value"},
            {gTriggerValueAction, "/user/hand/right/input/trigger/value"},
            {gGripAction, "/user/hand/left/input/squeeze/click"},
            {gGripAction, "/user/hand/right/input/squeeze/click"},
            {gMenuAction, "/user/hand/left/input/menu/click"},
            {gMenuAction, "/user/hand/right/input/menu/click"},

            // Trackpad
            {gTrackpadAction, "/user/hand/left/input/trackpad"},
            {gTrackpadAction, "/user/hand/right/input/trackpad"},
            {gTrackpadClickAction, "/user/hand/left/input/trackpad/click"},
            {gTrackpadClickAction, "/user/hand/right/input/trackpad/click"},
            {gTrackpadTouchAction, "/user/hand/left/input/trackpad/touch"},
            {gTrackpadTouchAction, "/user/hand/right/input/trackpad/touch"},

            // Haptic
            {gHapticAction, "/user/hand/left/output/haptic"},
            {gHapticAction, "/user/hand/right/output/haptic"}
    });

    // 8. HP REVERB G2 / WMR PROFILE
    SuggestBindings("/interaction_profiles/hp/mixed_reality_controller", {
            // Poses
            {gPoseAction, "/user/hand/left/input/grip/pose"},
            {gPoseAction, "/user/hand/right/input/grip/pose"},

            // Triggers / grip
            {gTriggerValueAction, "/user/hand/left/input/trigger/value"},
            {gTriggerValueAction, "/user/hand/right/input/trigger/value"},
            {gGripValueAction, "/user/hand/left/input/squeeze/value"},
            {gGripValueAction, "/user/hand/right/input/squeeze/value"},

            // A/B/X/Y buttons
            {gButtonXAction, "/user/hand/left/input/x/click"},
            {gButtonYAction, "/user/hand/left/input/y/click"},
            {gButtonAAction, "/user/hand/right/input/a/click"},
            {gButtonBAction, "/user/hand/right/input/b/click"},

            // Menu
            {gMenuAction, "/user/hand/left/input/menu/click"},
            {gMenuAction, "/user/hand/right/input/menu/click"},

            // Thumbstick
            {gThumbstickAction, "/user/hand/left/input/thumbstick"},
            {gThumbstickAction, "/user/hand/right/input/thumbstick"},
            {gThumbstickClickAction, "/user/hand/left/input/thumbstick/click"},
            {gThumbstickClickAction, "/user/hand/right/input/thumbstick/click"},

            // Haptic
            {gHapticAction, "/user/hand/left/output/haptic"},
            {gHapticAction, "/user/hand/right/output/haptic"}
    });

    // 9. MICROSOFT MOTION CONTROLLER PROFILE (WMR)
    SuggestBindings("/interaction_profiles/microsoft/motion_controller", {
            // Poses
            {gPoseAction, "/user/hand/left/input/grip/pose"},
            {gPoseAction, "/user/hand/right/input/grip/pose"},

            // Buttons
            {gTriggerValueAction, "/user/hand/left/input/trigger/value"},
            {gTriggerValueAction, "/user/hand/right/input/trigger/value"},
            {gGripAction, "/user/hand/left/input/squeeze/click"},
            {gGripAction, "/user/hand/right/input/squeeze/click"},
            {gMenuAction, "/user/hand/left/input/menu/click"},
            {gMenuAction, "/user/hand/right/input/menu/click"},

            // Thumbstick
            {gThumbstickAction, "/user/hand/left/input/thumbstick"},
            {gThumbstickAction, "/user/hand/right/input/thumbstick"},
            {gThumbstickClickAction, "/user/hand/left/input/thumbstick/click"},
            {gThumbstickClickAction, "/user/hand/right/input/thumbstick/click"},

            // Trackpad
            {gTrackpadAction, "/user/hand/left/input/trackpad"},
            {gTrackpadAction, "/user/hand/right/input/trackpad"},
            {gTrackpadClickAction, "/user/hand/left/input/trackpad/click"},
            {gTrackpadClickAction, "/user/hand/right/input/trackpad/click"},
            {gTrackpadTouchAction, "/user/hand/left/input/trackpad/touch"},
            {gTrackpadTouchAction, "/user/hand/right/input/trackpad/touch"},

            // Haptic
            {gHapticAction, "/user/hand/left/output/haptic"},
            {gHapticAction, "/user/hand/right/output/haptic"}
    });


    SuggestBindings("/interaction_profiles/meta/touch_controller_plus", {
            {gPoseAction, "/user/hand/left/input/grip/pose"},
            {gPoseAction, "/user/hand/right/input/grip/pose"},
            {gTriggerValueAction, "/user/hand/left/input/trigger/value"},
            {gTriggerValueAction, "/user/hand/right/input/trigger/value"},
            {gGripValueAction, "/user/hand/left/input/squeeze/value"},
            {gGripValueAction, "/user/hand/right/input/squeeze/value"},
            {gButtonAAction, "/user/hand/right/input/a/click"},
            {gButtonBAction, "/user/hand/right/input/b/click"},
            {gButtonXAction, "/user/hand/left/input/x/click"},
            {gButtonYAction, "/user/hand/left/input/y/click"},
            {gThumbstickAction, "/user/hand/left/input/thumbstick"},
            {gThumbstickAction, "/user/hand/right/input/thumbstick"},
            {gThumbstickClickAction, "/user/hand/left/input/thumbstick/click"},
            {gThumbstickClickAction, "/user/hand/right/input/thumbstick/click"},
            {gHapticAction, "/user/hand/left/output/haptic"},
            {gHapticAction, "/user/hand/right/output/haptic"},
    });


    // 10. Attach the ActionSet to the session (REQUIRED before xrBeginSession)
    XrSessionActionSetsAttachInfo attachInfo{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attachInfo.countActionSets = 1;
    attachInfo.actionSets = &gActionSet;
    XR_TRY(xrAttachSessionActionSets(g_vrState.session, &attachInfo));

    // 11. Create ActionSpaces for controller poses
    XrActionSpaceCreateInfo spaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
    spaceInfo.poseInActionSpace.orientation = { 0, 0, 0, 1 };
    spaceInfo.poseInActionSpace.position = {0, 0, 0};
    spaceInfo.action = gPoseAction;

    spaceInfo.subactionPath = subactionPaths[0]; // left
    XR_TRY(xrCreateActionSpace(g_vrState.session, &spaceInfo, &gControllerSpace[0]));

    spaceInfo.subactionPath = subactionPaths[1]; // right
    XR_TRY(xrCreateActionSpace(g_vrState.session, &spaceInfo, &gControllerSpace[1]));

    // 12. Initialize state structures
    for (int i = 0; i < 2; i++) {
        auto& state = gControllerStates[i];
        state.pose = {XR_TYPE_ACTION_STATE_POSE};
        state.select = {XR_TYPE_ACTION_STATE_BOOLEAN};
        state.grip_value = {XR_TYPE_ACTION_STATE_FLOAT};
        state.menu = {XR_TYPE_ACTION_STATE_BOOLEAN};
        state.button_a = {XR_TYPE_ACTION_STATE_BOOLEAN};
        state.button_b = {XR_TYPE_ACTION_STATE_BOOLEAN};
        state.button_x = {XR_TYPE_ACTION_STATE_BOOLEAN};
        state.button_y = {XR_TYPE_ACTION_STATE_BOOLEAN};
        state.thumbstick = {XR_TYPE_ACTION_STATE_VECTOR2F};
        state.thumbstick_click = {XR_TYPE_ACTION_STATE_BOOLEAN};
        state.trackpad = {XR_TYPE_ACTION_STATE_VECTOR2F};
        state.trackpad_click = {XR_TYPE_ACTION_STATE_BOOLEAN};
        state.trackpad_touch = {XR_TYPE_ACTION_STATE_BOOLEAN};
        state.trigger_value = {XR_TYPE_ACTION_STATE_FLOAT};
        state.grip_click = {XR_TYPE_ACTION_STATE_BOOLEAN};

        if (gIsValveIndex){
            state.grip_force = {XR_TYPE_ACTION_STATE_FLOAT};
        }
        state.is_active = false;
    }

    vr_log("[VR_CTRL] Complete initialization completed successfully");
    return XR_SUCCESS;
}

// === INPUT READING FUNCTION (called every frame) ===
XrResult update_vr_controllers(XrTime predicted_time) {
    // 1. Sync the ActionSet
    XrActiveActionSet activeActionSet{};
    activeActionSet.actionSet = gActionSet;
    activeActionSet.subactionPath = XR_NULL_PATH;

    XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeActionSet;
    XR_TRY(xrSyncActions(g_vrState.session, &syncInfo));

    // 2. Helper to read action states
    auto GetActionState = [&](XrAction action, XrPath subactionPath, auto& state) -> XrResult {
        XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
        getInfo.action = action;
        getInfo.subactionPath = subactionPath;

        if constexpr (std::is_same_v<std::decay_t<decltype(state)>, XrActionStateBoolean>) {
            return xrGetActionStateBoolean(g_vrState.session, &getInfo, &state);
        } else if constexpr (std::is_same_v<std::decay_t<decltype(state)>, XrActionStateFloat>) {
            return xrGetActionStateFloat(g_vrState.session, &getInfo, &state);
        } else if constexpr (std::is_same_v<std::decay_t<decltype(state)>, XrActionStateVector2f>) {
            return xrGetActionStateVector2f(g_vrState.session, &getInfo, &state);
        } else if constexpr (std::is_same_v<std::decay_t<decltype(state)>, XrActionStatePose>) {
            return xrGetActionStatePose(g_vrState.session, &getInfo, &state);
        }
        return XR_ERROR_RUNTIME_FAILURE;
    };

    // 3. Read states for each controller
    for (int hand = 0; hand < 2; hand++) {
        auto& state = gControllerStates[hand];
        XrPath handPath = gHandPaths[hand];

        // Pose
        XR_TRY(GetActionState(gPoseAction, handPath, state.pose));
        {
            XrSpaceVelocity velocity = { XR_TYPE_SPACE_VELOCITY };
            XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
            loc.next = &velocity;

            XrResult res = xrLocateSpace(gControllerSpace[hand], g_vrState.viewSpace, predicted_time, &loc);

            if (XR_SUCCEEDED(res) &&
                (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
                (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
                state.controller_pose = loc.pose;
                gCachedVelocity[hand] = velocity;
                state.is_active = true;
                spaceLocation = loc;
            } else {
//                vr_log("[VR_DEBUG] hand=%d xrLocateSpace failed: res=%d flags=0x%X pose.isActive=%d",
//                       hand, (int)res, (unsigned)loc.locationFlags, (int)state.pose.isActive);
                state.is_active = false;
            }
        }

        // Same controller, located against playSpace: the gesture frame.
        {
            XrSpaceVelocity velocity = { XR_TYPE_SPACE_VELOCITY };
            XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
            loc.next = &velocity;

            XrResult res = xrLocateSpace(gControllerSpace[hand], g_vrState.playSpace, predicted_time, &loc);

            if (XR_SUCCEEDED(res) &&
                (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
                gCtrlPosePlay[hand] = loc.pose;
                gCachedVelocityPlay[hand] = velocity;
            }
        }

        // Actions bool
        XR_TRY(GetActionState(gSelectAction, handPath, state.select));
        XR_TRY(GetActionState(gMenuAction, handPath, state.menu));
        XR_TRY(GetActionState(gButtonAAction, handPath, state.button_a));
        XR_TRY(GetActionState(gButtonBAction, handPath, state.button_b));
        XR_TRY(GetActionState(gButtonXAction, handPath, state.button_x));
        XR_TRY(GetActionState(gButtonYAction, handPath, state.button_y));
        XR_TRY(GetActionState(gThumbstickClickAction, handPath, state.thumbstick_click));
        XR_TRY(GetActionState(gTrackpadClickAction, handPath, state.trackpad_click));
        XR_TRY(GetActionState(gTrackpadTouchAction, handPath, state.trackpad_touch));
        XR_TRY(GetActionState(gGripAction, handPath, state.grip_click));

        // Actions float
        XR_TRY(GetActionState(gTriggerValueAction, handPath, state.trigger_value));
        XR_TRY(GetActionState(gGripValueAction, handPath, state.grip_value));

        // Actions 2D
        XR_TRY(GetActionState(gThumbstickAction, handPath, state.thumbstick));
        XR_TRY(GetActionState(gTrackpadAction, handPath, state.trackpad));

        if (gIsValveIndex) {
            XR_TRY(GetActionState(gGripForceAction, handPath, state.grip_force));
        }
    }

    return XR_SUCCESS;
}



// Get a button state
extern "C" bool get_button_state(int hand_index, const char* button_name) {
    if (hand_index < 0 || hand_index > 1) return false;

    //vr_log("[VR_INPUT] get_button_state called: main=%d, button=%s", hand_index, button_name);

    if (hand_index < 0 || hand_index > 1) {
        vr_log("[VR_INPUT] Invalid hand index: %d", hand_index);
        return false;
    }

    if (VrLeftHandedMode){
        hand_index = 1 - hand_index;
    }

    const auto& state = gControllerStates[hand_index];

    // vr_log("[VR_INPUT] Controller state: %d: isActive=%d", hand_index, state.is_active);

    if (strcmp(button_name, "trigger") == 0) {
        // vr_log("[VR_INPUT] Trigger state: %d", state.select.currentState);
        return state.select.currentState;
    }

    if (strcmp(button_name, "trigger") == 0) return state.select.currentState;

    if (strcmp(button_name, "grip") == 0) {
        if (gIsValveIndex) {
            static bool gripLatched[2] = {false, false};
            float force = state.grip_force.isActive ? state.grip_force.currentState : state.grip_value.currentState;

            if (gripLatched[hand_index]) {
                if (force < 0.65f) gripLatched[hand_index] = false; // lower release threshold
            } else {
                if (force >= 0.85f) gripLatched[hand_index] = true; // activation threshold
            }
            return gripLatched[hand_index];
        }
        // Normal behavior for all other headsets
        return state.grip_click.currentState;
    }

    if (!VrLeftHandedMode) {
        if (strcmp(button_name, "menu") == 0) return state.menu.currentState;
        if (strcmp(button_name, "a") == 0) return state.button_a.currentState;
        if (strcmp(button_name, "b") == 0) return state.button_b.currentState;
        if (strcmp(button_name, "x") == 0) return state.button_x.currentState;
        if (strcmp(button_name, "y") == 0) return state.button_y.currentState;
        if (strcmp(button_name, "thumbstick_click") == 0) return state.thumbstick_click.currentState;
        if (strcmp(button_name, "trackpad_click") == 0) return state.trackpad_click.currentState;
        if (strcmp(button_name, "trackpad_touch") == 0) return state.trackpad_touch.currentState;
    }else{
        if (strcmp(button_name, "menu") == 0) return state.menu.currentState;
        // inverted... hacky fix
        if (strcmp(button_name, "a") == 0) return state.button_x.currentState;
        if (strcmp(button_name, "b") == 0) return state.button_y.currentState;
        if (strcmp(button_name, "x") == 0) return state.button_a.currentState;
        if (strcmp(button_name, "y") == 0) return state.button_b.currentState;
        if (strcmp(button_name, "thumbstick_click") == 0) return state.thumbstick_click.currentState;
        if (strcmp(button_name, "trackpad_click") == 0) return state.trackpad_click.currentState;
        if (strcmp(button_name, "trackpad_touch") == 0) return state.trackpad_touch.currentState;
    }
    return false;
}



/*float get_analog_value(int hand_index, const char* input_name) {
    if (hand_index < 0 || hand_index > 1) return 0.0f;

    if (VrLeftHandedMode){
        hand_index = 1 - hand_index;
    }

    const auto& state = gControllerStates[hand_index];

    if (strcmp(input_name, "trigger") == 0) return state.trigger_value.currentState;
    if (strcmp(input_name, "grip") == 0) {
        if (gIsValveIndex && state.grip_force.isActive)
            return state.grip_force.currentState;
        return state.grip_value.currentState;
    }
    return 0.0f;
}*/


extern "C" bool get_2d_input(int hand_index, const char* input_name, XrVector2f* value) {
    if (hand_index < 0 || hand_index > 1 || !value) return false;

    if (VrLeftHandedMode){
        hand_index = 1 - hand_index;
    }

    const auto& state = gControllerStates[hand_index];

    if (strcmp(input_name, "thumbstick") == 0 && state.thumbstick.isActive) {
        *value = state.thumbstick.currentState;
        return true;
    }
    if (strcmp(input_name, "trackpad") == 0 && state.trackpad.isActive) {
        *value = state.trackpad.currentState;
        return true;
    }

    return false;
}

// === HAPTIC VIBRATION ===
XrResult trigger_haptic_vibration(int hand_index, float amplitude, float duration, float frequency) {
    if (hand_index < 0 || hand_index > 1) return XR_ERROR_VALIDATION_FAILURE;

    if (VrLeftHandedMode){
        hand_index = 1 - hand_index;
    }

    XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
    vibration.amplitude = std::min(1.0f, std::max(0.0f, amplitude)); // Clamp 0-1
    vibration.duration = (duration < 0) ? XR_MIN_HAPTIC_DURATION : static_cast<XrDuration>(duration * 1000000000LL); // Convert to nanoseconds
    vibration.frequency = frequency;

    XrHapticActionInfo hapticInfo{XR_TYPE_HAPTIC_ACTION_INFO};
    hapticInfo.action = gHapticAction;
    hapticInfo.subactionPath = gHandPaths[hand_index];

    XrResult result = xrApplyHapticFeedback(g_vrState.session, &hapticInfo, (XrHapticBaseHeader*)&vibration);
    if (result != XR_SUCCESS) {
        vr_log("[VR_HAPTIC] Hand vibration error %d: %d", hand_index, result);
    }

    return result;
}

XrResult stop_haptic_vibration(int hand_index) {
    if (hand_index < 0 || hand_index > 1) return XR_ERROR_VALIDATION_FAILURE;

    XrHapticActionInfo hapticInfo{XR_TYPE_HAPTIC_ACTION_INFO};
    hapticInfo.action = gHapticAction;
    hapticInfo.subactionPath = gHandPaths[hand_index];

    return xrStopHapticFeedback(g_vrState.session, &hapticInfo);
}


extern "C" {
XrResult trigger_haptic_vibration_c(int hand_index, float amplitude, float duration) {
    return trigger_haptic_vibration(hand_index, amplitude, duration);
}

XrResult stop_haptic_vibration_c(int hand_index) {
    return stop_haptic_vibration(hand_index);
}
}


// === DEBUG AND MONITORING FUNCTIONS ===
/*
void log_controller_states() {
    for (int i = 0; i < 2; i++) {
        const auto& state = gControllerStates[i];
        const char* hand_name = (i == 0) ? "LEFT" : "RIGHT";

        if (!state.is_active) {
           // vr_log("[VR_DEBUG] %s controller: INACTIVE", hand_name);
            continue;
        }

        vr_log("[VR_DEBUG] %s controller:", hand_name);
        vr_log("  Position: %.3f, %.3f, %.3f",
               state.controller_pose.position.x,
               state.controller_pose.position.y,
               state.controller_pose.position.z);
        vr_log("  Trigger: %.3f | Grip: %.3f",
               state.trigger_value.currentState,
               state.grip_value.currentState);
        vr_log("  Thumbstick: %.3f, %.3f (click: %d)",
               state.thumbstick.currentState.x,
               state.thumbstick.currentState.y,
               state.thumbstick_click.currentState);
        vr_log("  Buttons: A=%d B=%d X=%d Y=%d Menu=%d",
               state.button_a.currentState,
               state.button_b.currentState,
               state.button_x.currentState,
               state.button_y.currentState,
               state.menu.currentState);
    }
}
*/


void detect_headset_profile() {
    char profileStr[256] = {};
    uint32_t outLen = 0;
    bool profileFound = false;

    for (int h = 0; h < 2; h++) {
        const char* handName = (h == 0) ? "LEFT" : "RIGHT";
        XrInteractionProfileState profileState{XR_TYPE_INTERACTION_PROFILE_STATE};
        XrResult res = xrGetCurrentInteractionProfile(
                g_vrState.session, gHandPaths[h], &profileState);

        if (XR_FAILED(res) || profileState.interactionProfile == XR_NULL_PATH) {
            vr_log("[VR_CTRL] %s hand: no profile yet (res=%d)", handName, (int)res);
            continue;
        }

        outLen = 0;
        xrPathToString(g_vrState.instance, profileState.interactionProfile,
                       sizeof(profileStr), &outLen, profileStr);
        vr_log("[VR_CTRL] %s hand profile selected is: %s", handName, profileStr);
        profileFound = true;
    }

    if (!profileFound) {
        vr_log("[VR_CTRL] No interaction profile active yet");
        gIsValveIndex = false;
        return;
    }

    gIsValveIndex = (strstr(profileStr, "valve/index_controller") != nullptr);
    vr_log("[VR_CTRL] IsValveIndex: %d", (int)gIsValveIndex);
}


//====== LEFT HAND OFFSETS AND ROTATIONS (Manual reloading) ===============================

// Helper : rotate a vector by a quaternion (w,x,y,z)
static void RotVecByQuat(float vx, float vy, float vz,
                         const float q[4],
                         float &ox, float &oy, float &oz)
{
    float qw = q[0], qx = q[1], qy = q[2], qz = q[3];
    float tx = 2.0f * (qy * vz - qz * vy);
    float ty = 2.0f * (qz * vx - qx * vz);
    float tz = 2.0f * (qx * vy - qy * vx);
    ox = vx + qw * tx + qy * tz - qz * ty;
    oy = vy + qw * ty + qz * tx - qx * tz;
    oz = vz + qw * tz + qx * ty - qy * tx;
}


// Helper: applies a pure axis rotation to the quaternion of controller i
// axis: 0=X, 1=Y, 2=Z
// angle: in radians (positive/negative for direction)
void applyCtrlRotation(int i, int axis, float angle) {
    float half = angle * 0.5f;
    float s = sinf(half);
    float c = cosf(half);

    float rotQuat[4] = { c, 0.0f, 0.0f, 0.0f };
    rotQuat[1 + axis] = s; // x=index1, y=index2, z=index3

    float qw = gCtrlQuat[i][0], qx = gCtrlQuat[i][1],
            qy = gCtrlQuat[i][2], qz = gCtrlQuat[i][3];
    float rw = rotQuat[0], rx = rotQuat[1],
            ry = rotQuat[2], rz = rotQuat[3];

    gCtrlQuat[i][0] = qw*rw - qx*rx - qy*ry - qz*rz;
    gCtrlQuat[i][1] = qw*rx + qx*rw + qy*rz - qz*ry;
    gCtrlQuat[i][2] = qw*ry - qx*rz + qy*rw + qz*rx;
    gCtrlQuat[i][3] = qw*rz + qx*ry - qy*rx + qz*rw;
}



// Update the local pull FOR MANUAL RELOADING
void vrUpdateReloadPull(void) {
    float rx = gCtrlPos[0][0] - gCtrlPos[1][0];
    float ry = gCtrlPos[0][1] - gCtrlPos[1][1];
    float rz = gCtrlPos[0][2] - gCtrlPos[1][2];

    if (!sReloadPrevCaptured) {
        sReloadRelPrevX = rx; sReloadRelPrevY = ry; sReloadRelPrevZ = rz;
        sReloadPrevCaptured = true;
        return;
    }

    float dx = rx - sReloadRelPrevX;
    float dy = ry - sReloadRelPrevY;
    float dz = rz - sReloadRelPrevZ;
    sReloadRelPrevX = rx; sReloadRelPrevY = ry; sReloadRelPrevZ = rz;

    const auto& rightPose = gControllerStates[1].controller_pose.orientation;
    float qInv[4] = { rightPose.w, -rightPose.x, -rightPose.y, -rightPose.z };

    float ldx, ldy, ldz;
    RotVecByQuat(dx, dy, dz, qInv, ldx, ldy, ldz);

    // Accumulate on the 3 local axes
    gVrReloadPullLocalX += ldx;
    gVrReloadPullLocalY -= ldz;
    gVrReloadPullLocalZ -= ldy;

}

// SLERP between two quaternions [w,x,y,z], t in [0,1]
void QuatSlerp(const float a[4], const float b[4], float t, float out[4])
{
    float dot = a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3];

    float bFlip[4] = {b[0], b[1], b[2], b[3]};
    if (dot < 0.0f) {
        bFlip[0] = -b[0]; bFlip[1] = -b[1];
        bFlip[2] = -b[2]; bFlip[3] = -b[3];
        dot = -dot;
    }

    if (dot > 0.9995f) {
        for (int i = 0; i < 4; i++)
            out[i] = a[i] + t * (bFlip[i] - a[i]);

        float len = sqrtf(out[0]*out[0] + out[1]*out[1] + out[2]*out[2] + out[3]*out[3]);
        if (len > 0.0f) { out[0]/=len; out[1]/=len; out[2]/=len; out[3]/=len; }
        return;
    }

    float theta0 = acosf(dot);
    float theta  = theta0 * t;
    float sinT0  = sinf(theta0);
    float sinT   = sinf(theta);

    float s0 = cosf(theta) - dot * sinT / sinT0;
    float s1 = sinT / sinT0;

    for (int i = 0; i < 4; i++)
        out[i] = s0 * a[i] + s1 * bFlip[i];
}



// ==========================================================
// WEAPON RECOIL PROFILES (100% on OpenXR side, no engine dependency)
// ==========================================================
struct WeaponRecoilProfile {
    float kickPitch;      // vertical amplitude (rad)
    float kickYaw;        // lateral amplitude (rad)
    float kickPush;       // translation recoil, backward (m)
    float springStiff;    // spring return
    float springDamp;     // damping
};


static WeaponRecoilProfile GetRecoilProfileForWeapon(int wnum)
{
    bool isTwoHandsGrip = VrTwoHandsGun(weaponnum) && get_button_state(0, "grip");

    switch (wnum) {
        // --- PISTOLS (one hand only, no two-handed grip) ---
        case WEAPON_FALCON2:
        case WEAPON_FALCON2_SILENCER:
        case WEAPON_FALCON2_SCOPE:
        case WEAPON_MAGSEC4:
        case WEAPON_PP9I:
        case WEAPON_CC13:
            return { 0.025f, 0.010f, -0.60f, 75.0f, 50.0f };

        case WEAPON_DY357MAGNUM:
        case WEAPON_DY357LX:
            return { 0.090f, 0.020f, -1.20f, 60.0f, 15.0f };

            // --- MAULER (no YAW, dry and very powerful translation recoil) ---
        case WEAPON_MAULER:
            if(VR_FUNC_SECONDARY){
                return { 0.020f, 0.000f, -2.20f, 160.0f, 12.0f };
            }else{
                return { 0.015f, 0.000f, -1.50f, 85.0f, 28.0f };
            }

            // --- CLASSIC LIGHT MACHINE GUNS (push significantly reinforced) ---
        case WEAPON_ZZT:
        case WEAPON_DMC:
        case WEAPON_RCP45:
        case WEAPON_KF7SPECIAL:
        case WEAPON_AR53:
                return { 0.003f, 0.004f, -0.500f, 130.0f, 18.0f };

            // --- CYCLONE / KL01313 (no lateral YAW recoil, reinforced push) ---
        case WEAPON_CYCLONE:
        case WEAPON_KL01313:
            if (isTwoHandsGrip) {
                return { 0.004f, 0.000f, -0.180f, 140.0f, 20.0f };
            } else {
                return { 0.012f, 0.000f, -0.500f, 110.0f, 15.0f };
            }

            // --- CMP150 (separated, reinforced push, no YAW) ---
        case WEAPON_CMP150:
                return { 0.006f, 0.000f, -0.400f, 120.0f, 16.0f };

        case WEAPON_RCP120:
            if(VR_FUNC_SECONDARY){
                return { 0.0f, 0.0f, 0.0f, 100.0f, 10.0f };
            }
            else if (isTwoHandsGrip) {
                return { 0.001f, 0.001f, -0.150f, 140.0f, 20.0f };
            } else {
                return { 0.030f, 0.015f, -1.000f, 95.0f, 13.0f };
            }

        case WEAPON_AR34:
        case WEAPON_K7AVENGER:
            if (isTwoHandsGrip) {
                return { 0.001f, 0.001f, -0.150f, 140.0f, 20.0f };
            } else {
                return { 0.030f, 0.015f, -1.000f, 95.0f, 13.0f };
            }

        case WEAPON_SUPERDRAGON:
            if(VR_FUNC_SECONDARY && !isTwoHandsGrip){
                return { 0.140f, 0.020f, -7.000f, 33.0f, 6.0f };
            }else if (isTwoHandsGrip) {
                return { 0.003f, 0.001f, -0.150f, 140.0f, 20.0f };
            } else {
                return { 0.030f, 0.015f, -1.000f, 95.0f, 13.0f };
            }

        case WEAPON_DRAGON:
        case WEAPON_LAPTOPGUN:
        case WEAPON_CALLISTO:
            if (isTwoHandsGrip) {
                return { 0.002f, 0.001f, -0.150f, 140.0f, 20.0f };
            } else {
                return { 0.030f, 0.015f, -1.000f, 95.0f, 13.0f };
            }

            // --- SHOTGUN (HUGE backward recoil) ---
        case WEAPON_SHOTGUN:
            if (isTwoHandsGrip) {
                return { 0.045f, 0.005f, -3.500f, 75.0f, 13.0f };
            } else {
                return { 0.140f, 0.020f, -7.000f, 33.0f, 6.0f };
            }

            // --- PRECISION WEAPONS (HUGE backward recoil) ---
        case WEAPON_SNIPERRIFLE:
        case WEAPON_FARSIGHT:
            if (isTwoHandsGrip) {
                return { 0.01f, 0.01f, -0.150f, 100.0f, 15.0f };
            } else {
                return { 0.140f, 0.020f, -7.000f, 33.0f, 6.0f };
            }

            // --- CROSSBOW (silent single shot, moderate push) ---
        case WEAPON_CROSSBOW:
            return { 0.015f, 0.005f, -0.15f, 90.0f, 25.0f };

            // --- DEVASTATOR / ROCKET LAUNCHER / SLAYER (HUGE backward recoil) ---
        case WEAPON_DEVASTATOR:
        case WEAPON_ROCKETLAUNCHER:
        case WEAPON_SLAYER:
            if (isTwoHandsGrip) {
                return { 0.050f, 0.0f, -5.000f, 35.0f, 5.5f };
            } else {
                return { 0.190f, 0.180f, -7.000f, 25.0f, 3.0f };
            }

            // --- REAPER (backward recoil heavily reinforced in both modes) ---
        case WEAPON_REAPER:
            if(VR_FUNC_SECONDARY && isTwoHandsGrip){
                return { 0.0040f, 0.050f, -0.0, 20.0f, 30.0f };
            }else if(VR_FUNC_SECONDARY && !isTwoHandsGrip){
                return { 0.010f, 0.180f, -0.0f, 20.0f, 20.0f };
            }else if (isTwoHandsGrip) {
                return { 0.0040f, 0.014f, -0.500f, 178.0f, 30.0f };
            } else {
                return { 0.010f, 0.060f, -0.900f, 150.0f, 20.0f };
            }

            // --- ALIEN / ENERGY WEAPONS ---
        case WEAPON_PHOENIX:
            if(VR_FUNC_SECONDARY){
                return { 0.040f, 0.050f, -7.000f, 33.0f, 6.0f };
            }else{
                return { 0.020f, 0.005f, -0.40f, 120.0f, 40.0f };
            }

            // --- SPECIAL WEAPONS AND TOOLS ---
        case WEAPON_TRANQUILIZER:
        case WEAPON_PSYCHOSISGUN:
            return { 0.010f, 0.005f, -0.15f, 100.0f, 60.0f };

        case WEAPON_LASER:
            return { 0.002f, 0.002f, -0.05f, 180.0f, 90.0f };

        case WEAPON_COMBATKNIFE:
        case WEAPON_UNARMED:
        case WEAPON_GRENADE:
        case WEAPON_TIMEDMINE:
        case WEAPON_PROXIMITYMINE:
        case WEAPON_REMOTEMINE:
        case WEAPON_NBOMB:
            return { 0.0f, 0.0f, 0.0f, 100.0f, 10.0f };

        default:
            return { 0.010f, 0.0f, -0.15f, 50.0f, 9.0f }; // generic fallback
    }



}


static float sRecoilQuat[2][4]  = { {1,0,0,0}, {1,0,0,0} };
static float sRecoilVelPitch[2] = { 0.0f, 0.0f };
static float sRecoilVelYaw[2]   = { 0.0f, 0.0f };
static float sRecoilPosOffset[2][3] = { {0,0,0}, {0,0,0} };
static float sRecoilVelPush[2]  = { 0.0f, 0.0f };

static float RecoilRandf()
{
    return (float)rand() / (float)RAND_MAX; // 0..1, purely local, no engine dependency
}

static void RecoilFireImpulse(int handIndex, const WeaponRecoilProfile& p)
{
    float randSide = (RecoilRandf() - 0.5f) * 2.0f; // -1..1

    sRecoilVelPitch[handIndex] += -p.kickPitch * p.springStiff;
    sRecoilVelYaw[handIndex]   +=  p.kickYaw   * randSide * p.springStiff;
    sRecoilVelPush[handIndex]  += -p.kickPush  * p.springStiff;
}

extern "C" void vrRecoilNotifyShotFired(int handnum)
{
    if (handnum != 0 && handnum != 1) return;

    // handnum comes from the game: HANDRIGHT=0, HANDLEFT=1
    // gControllerStates/gCtrlQuat follow the OpenXR convention: 0=left, 1=right
    // => the index must be inverted here
    int ctrlIndex = 1 - handnum;

    WeaponRecoilProfile profile = GetRecoilProfileForWeapon(weaponnum);
    RecoilFireImpulse(ctrlIndex, profile);
}

static void RecoilUpdate(int handIndex, float dt, const WeaponRecoilProfile& p)
{
    // --- Rotation (pitch/yaw) ---
    float curPitch = 2.0f * sRecoilQuat[handIndex][1];
    float curYaw   = 2.0f * sRecoilQuat[handIndex][2];

    float accPitch = -p.springStiff * curPitch - p.springDamp * sRecoilVelPitch[handIndex];
    float accYaw   = -p.springStiff * curYaw   - p.springDamp * sRecoilVelYaw[handIndex];

    sRecoilVelPitch[handIndex] += accPitch * dt;
    sRecoilVelYaw[handIndex]   += accYaw   * dt;

    float newPitch = curPitch + sRecoilVelPitch[handIndex] * dt;
    float newYaw   = curYaw   + sRecoilVelYaw[handIndex]   * dt;

    float hp = newPitch * 0.5f, hy = newYaw * 0.5f;
    float qX[4] = { cosf(hp), sinf(hp), 0.0f, 0.0f };
    float qY[4] = { cosf(hy), 0.0f, sinf(hy), 0.0f };

    float w1=qY[0], x1=qY[1], y1=qY[2], z1=qY[3];
    float w2=qX[0], x2=qX[1], y2=qX[2], z2=qX[3];

    sRecoilQuat[handIndex][0] = w1*w2 - x1*x2 - y1*y2 - z1*z2;
    sRecoilQuat[handIndex][1] = w1*x2 + x1*w2 + y1*z2 - z1*y2;
    sRecoilQuat[handIndex][2] = w1*y2 - x1*z2 + y1*w2 + z1*x2;
    sRecoilQuat[handIndex][3] = w1*z2 + x1*y2 - y1*x2 + z1*w2;

    // --- Translation (backward push, local Z axis) ---
    float curPush = sRecoilPosOffset[handIndex][2];
    float accPush = -p.springStiff * curPush - p.springDamp * sRecoilVelPush[handIndex];
    sRecoilVelPush[handIndex] += accPush * dt;
    sRecoilPosOffset[handIndex][2] = curPush + sRecoilVelPush[handIndex] * dt;
}


static void RecoilApplyToControllerPose(int handIndex, float outQuat[4], float outPos[3])
{
    float w1=outQuat[0], x1=outQuat[1], y1=outQuat[2], z1=outQuat[3];
    float w2=sRecoilQuat[handIndex][0], x2=sRecoilQuat[handIndex][1];
    float y2=sRecoilQuat[handIndex][2], z2=sRecoilQuat[handIndex][3];

    outQuat[0] = w1*w2 - x1*x2 - y1*y2 - z1*z2;
    outQuat[1] = w1*x2 + x1*w2 + y1*z2 - z1*y2;
    outQuat[2] = w1*y2 - x1*z2 + y1*w2 + z1*x2;
    outQuat[3] = w1*z2 + x1*y2 - y1*x2 + z1*w2;

    // Apply translation recoil in the controller's local space
    float lx = sRecoilPosOffset[handIndex][0];
    float ly = sRecoilPosOffset[handIndex][1];
    float lz = sRecoilPosOffset[handIndex][2];

    float rx, ry, rz;
    RotVecByQuat(lx, ly, lz, outQuat, rx, ry, rz); // already available in your file

    outPos[0] += rx;
    outPos[1] += ry;
    outPos[2] += rz;
}


extern XrQuaternionf YawToQuaternion(float angleDegrees);
extern XrVector3f RotateVectorY(XrVector3f v, float angleDegrees);
extern XrQuaternionf MultiplyQuaternions(XrQuaternionf q1, XrQuaternionf q2);
extern float g_yawOffsetDegrees;



void controller_pose() {

    static bool profileDetected = false;
    if (!profileDetected) {
        // Wait until both controllers are active before locking
        if (gControllerStates[0].is_active && gControllerStates[1].is_active) {
            detect_headset_profile();
            profileDetected = true;
        }
    }

    for (int i = 0; i < 2; i++) {
        // --- INVERSION LOGIC HERE ---
        // If left-handed mode is ON:
        // i=0 (logical Left Hand) will read sourceIndex=1 (physical Right Controller)
        // i=1 (logical Right Hand) will read sourceIndex=0 (physical Left Controller)
        int sourceIndex = VrLeftHandedMode ? (1 - i) : i;

        const auto& state = gControllerStates[sourceIndex]; // On utilise sourceIndex
        const char *handname = (i == 0) ? "LEFT" : "RIGHT";

        if (!state.is_active) {
            sSmoothedInit[i] = false;
            continue;
        }


        // --- Gesture frame: publish the play-space pose/velocity verbatim ---
        // Deliberately none of the conditioning applied below. Gesture maths
        // needs the rotation and the velocity to live in the same basis, and
        // the mirror/offset/recoil that the weapon pose needs would break that.
        vr_ctrl_quat_play[i][0] = gCtrlPosePlay[sourceIndex].orientation.w;
        vr_ctrl_quat_play[i][1] = gCtrlPosePlay[sourceIndex].orientation.x;
        vr_ctrl_quat_play[i][2] = gCtrlPosePlay[sourceIndex].orientation.y;
        vr_ctrl_quat_play[i][3] = gCtrlPosePlay[sourceIndex].orientation.z;

        if (gCachedVelocityPlay[sourceIndex].velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) {
            vr_ctrl_velocity_play[i][0] = gCachedVelocityPlay[sourceIndex].linearVelocity.x;
            vr_ctrl_velocity_play[i][1] = gCachedVelocityPlay[sourceIndex].linearVelocity.y;
            vr_ctrl_velocity_play[i][2] = gCachedVelocityPlay[sourceIndex].linearVelocity.z;
        }

        // --- Raw OpenXR position ---
        float rawX = state.controller_pose.position.x * 100.0f;
        float rawY = state.controller_pose.position.y * 100.0f;
        float rawZ = state.controller_pose.position.z * 100.0f;


        // --- Raw OpenXR quaternion (internal format: w, -x, y, -z) ---
        float rawQuat[4] = {
                state.controller_pose.orientation.w,
                -state.controller_pose.orientation.x,
                state.controller_pose.orientation.y,
                -state.controller_pose.orientation.z
        };

        // Save the raw version, before any SLERP, for uses
        // that must follow the controller with no delay (e.g. left-hand HUD layer)
        gCtrlQuatRaw[i][0] = rawQuat[0];
        gCtrlQuatRaw[i][1] = rawQuat[1];
        gCtrlQuatRaw[i][2] = rawQuat[2];
        gCtrlQuatRaw[i][3] = rawQuat[3];

        // --- Determine whether grip is pressed for THIS controller ---
        gripPressed = (sourceIndex == 1) ? (vr_button_R_grip != 0) : (vr_button_L_grip != 0);
        WepCanZoom = weaponnum == WEAPON_SNIPERRIFLE
                     || weaponnum == WEAPON_MAGSEC4
                     || weaponnum == WEAPON_LAPTOPGUN
                     || weaponnum == WEAPON_FARSIGHT
                     || weaponnum == WEAPON_DRAGON
                     || weaponnum == WEAPON_SUPERDRAGON
                     || weaponnum == WEAPON_K7AVENGER
                     || weaponnum == WEAPON_AR34
                     || weaponnum == WEAPON_FALCON2_SCOPE;


        if (gripPressed && WepCanZoom) {
            // --- Initialization on the first frame with grip ---
            if (!sSmoothedInit[i]) {
                sSmoothedPos[i][0]  = rawX;
                sSmoothedPos[i][1]  = rawY;
                sSmoothedPos[i][2]  = rawZ;
                sSmoothedQuat[i][0] = rawQuat[0];
                sSmoothedQuat[i][1] = rawQuat[1];
                sSmoothedQuat[i][2] = rawQuat[2];
                sSmoothedQuat[i][3] = rawQuat[3];
                sSmoothedInit[i]    = true;
            }

            // --- POSITION smoothing (EMA filter) ---
            sSmoothedPos[i][0] = CTRL_SMOOTH_ALPHA_POS * rawX + (1.0f - CTRL_SMOOTH_ALPHA_POS) * sSmoothedPos[i][0];
            sSmoothedPos[i][1] = CTRL_SMOOTH_ALPHA_POS * rawY + (1.0f - CTRL_SMOOTH_ALPHA_POS) * sSmoothedPos[i][1];
            sSmoothedPos[i][2] = CTRL_SMOOTH_ALPHA_POS * rawZ + (1.0f - CTRL_SMOOTH_ALPHA_POS) * sSmoothedPos[i][2];

            gCtrlPos[i][0] = sSmoothedPos[i][0];
            gCtrlPos[i][1] = sSmoothedPos[i][1];
            gCtrlPos[i][2] = sSmoothedPos[i][2];

            // --- ROTATION smoothing (SLERP) ---
            float slerpResult[4];
            QuatSlerp(sSmoothedQuat[i], rawQuat, CTRL_SMOOTH_ALPHA_ROT_GRIP, slerpResult);
            sSmoothedQuat[i][0] = slerpResult[0];
            sSmoothedQuat[i][1] = slerpResult[1];
            sSmoothedQuat[i][2] = slerpResult[2];
            sSmoothedQuat[i][3] = slerpResult[3];

            gCtrlQuat[i][0] = sSmoothedQuat[i][0];
            gCtrlQuat[i][1] = sSmoothedQuat[i][1];
            gCtrlQuat[i][2] = sSmoothedQuat[i][2];
            gCtrlQuat[i][3] = sSmoothedQuat[i][3];

        } else {
            // --- Grip released: raw position, but slightly smoothed rotation ---
            sSmoothedInit[i] = false;

            // Position: raw, no lag
            gCtrlPos[i][0] = rawX;
            gCtrlPos[i][1] = rawY;
            gCtrlPos[i][2] = rawZ;

            // Rotation: light permanent SLERP (high-frequency anti-jitter)
            // Initialize the quaternion buffer if necessary
            static bool sQuatIdleInit[2] = {false, false};
            if (!sQuatIdleInit[i]) {
                sSmoothedQuat[i][0] = rawQuat[0];
                sSmoothedQuat[i][1] = rawQuat[1];
                sSmoothedQuat[i][2] = rawQuat[2];
                sSmoothedQuat[i][3] = rawQuat[3];
                sQuatIdleInit[i] = true;
            }

            float slerpResult[4];
            QuatSlerp(sSmoothedQuat[i], rawQuat, CTRL_SMOOTH_ALPHA_ROT_IDLE, slerpResult);
            sSmoothedQuat[i][0] = slerpResult[0];
            sSmoothedQuat[i][1] = slerpResult[1];
            sSmoothedQuat[i][2] = slerpResult[2];
            sSmoothedQuat[i][3] = slerpResult[3];

            gCtrlQuat[i][0] = sSmoothedQuat[i][0];
            gCtrlQuat[i][1] = sSmoothedQuat[i][1];
            gCtrlQuat[i][2] = sSmoothedQuat[i][2];
            gCtrlQuat[i][3] = sSmoothedQuat[i][3];
        }

        // --- Rotation OFFSET (1.0 rad around X) --- identical to the original
        float offsetAngle = 1.5708f;
        float halfAngle   = offsetAngle * 0.5f;
        float sinHalf     = sinf(halfAngle);
        float cosHalf     = cosf(halfAngle);
        float offsetQuat[4] = {cosHalf, sinHalf, 0.0f, 0.0f};

        float w1=gCtrlQuat[i][0], x1=gCtrlQuat[i][1], y1=gCtrlQuat[i][2], z1=gCtrlQuat[i][3];
        float w2=offsetQuat[0],   x2=offsetQuat[1],   y2=offsetQuat[2],   z2=offsetQuat[3];
        gCtrlQuat[i][0] = w1*w2 - x1*x2 - y1*y2 - z1*z2;
        gCtrlQuat[i][1] = w1*x2 + x1*w2 + y1*z2 - z1*y2;
        gCtrlQuat[i][2] = w1*y2 - x1*z2 + y1*w2 + z1*x2;
        gCtrlQuat[i][3] = w1*z2 + x1*y2 - y1*x2 + z1*w2;


// Apply the same 90° X offset to the raw version
// to stay consistent with gCtrlQuat; otherwise the unsmoothed HUD
// ends up incorrectly oriented relative to the rest (weapon, recoil, etc.)
        float rw1 = gCtrlQuatRaw[i][0], rx1 = gCtrlQuatRaw[i][1], ry1 = gCtrlQuatRaw[i][2], rz1 = gCtrlQuatRaw[i][3];
        gCtrlQuatRaw[i][0] = rw1*w2 - rx1*x2 - ry1*y2 - rz1*z2;
        gCtrlQuatRaw[i][1] = rw1*x2 + rx1*w2 + ry1*z2 - rz1*y2;
        gCtrlQuatRaw[i][2] = rw1*y2 - rx1*z2 + ry1*w2 + rz1*x2;
        gCtrlQuatRaw[i][3] = rw1*z2 + rx1*y2 - ry1*x2 + rz1*w2;


        // VR Recoil
        if (VrWeaponRecoil) {
            static float sRecoilLastTime = 0.0f;
            float dt = 1.0f / 90.0f; // approx VR frametime, or retrieve via XrTime delta if available
            RecoilUpdate(i, dt, GetRecoilProfileForWeapon(weaponnum));
            RecoilApplyToControllerPose(i, gCtrlQuat[i], gCtrlPos[i]);
        }


        // --- Velocities ---
        if (gCachedVelocity[i].velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) {
            vr_ctrl_velocity[i][0] = gCachedVelocity[i].linearVelocity.x;
            vr_ctrl_velocity[i][1] = gCachedVelocity[i].linearVelocity.y;
            vr_ctrl_velocity[i][2] = gCachedVelocity[i].linearVelocity.z;
        }


    }

    vrUpdateReloadPull();
}


/*
// === USAGE EXAMPLE ===
void example_vr_input_usage() {

    XrPosef leftPose, rightPose;
    if (get_controller_pose(0, &leftPose)) {
        // Use the left controller pose
        vr_log("Left controller at: %.2f %.2f %.2f",
               leftPose.position.x, leftPose.position.y, leftPose.position.z);
    }

    // Detect button press
    if (get_button_state(1, "a")) {
        vr_log("Right-hand A button pressed!");
        trigger_haptic_vibration(1, 0.5f, 0.1f); // Light vibration
    }

    // Read analog values
    float rightTrigger = get_analog_value(1, "trigger");
    if (rightTrigger > 0.5f) {
        vr_log("Right trigger at %.2f%%", rightTrigger * 100);
    }

    // Read the joystick
    XrVector2f thumbstick;
    if (get_2d_input(0, "thumbstick", &thumbstick)) {
        if (fabsf(thumbstick.x) > 0.1f || fabsf(thumbstick.y) > 0.1f) {
            vr_log("Left joystick: %.2f, %.2f", thumbstick.x, thumbstick.y);
        }
    }
}
*/
