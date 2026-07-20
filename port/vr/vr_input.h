#ifndef XR_INPUT_H
#define XR_INPUT_H

#ifndef XR_USE_GRAPHICS_API_OPENGL_ES
#define XR_USE_GRAPHICS_API_OPENGL_ES
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif


#include <openxr/openxr.h>
#ifdef __cplusplus
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdarg>
#include <cmath>
#endif

#include <stdbool.h>




// === UTILITY MACROS ===
#ifndef XR_TRY
#define XR_TRY(expr) do { XrResult _res = (expr); if (XR_FAILED(_res)) return _res; } while(0)
#endif

// === GLOBAL VARIABLES FOR CONTROLLERS ===
extern XrActionSet gActionSet;

// === BASE ACTIONS ===
extern XrAction gPoseAction;
extern XrAction gSelectAction;
extern XrAction gGripAction;
extern XrAction gMenuAction;
extern XrAction gButtonAAction;
extern XrAction gButtonBAction;
extern XrAction gButtonXAction;
extern XrAction gButtonYAction;
extern XrAction gThumbstickAction;
extern XrAction gThumbstickClickAction;
extern XrAction gTrackpadAction;
extern XrAction gTrackpadClickAction;
extern XrAction gTrackpadTouchAction;
extern XrAction gTriggerValueAction;
extern XrAction gGripValueAction;
extern XrAction gHapticAction;

// === ACTION SPACES ===
extern XrSpace gControllerSpace[2]; // Left/Right controller spaces

// === CONTROLLER STATE STRUCTURE ===
struct ControllerInputState {
    XrActionStatePose pose;
    XrActionStateBoolean select;
    XrActionStateFloat grip_value;
    XrActionStateBoolean menu;
    XrActionStateBoolean button_a;
    XrActionStateBoolean button_b;
    XrActionStateBoolean button_x;
    XrActionStateBoolean button_y;
    XrActionStateVector2f thumbstick;
    XrActionStateBoolean thumbstick_click;
    XrActionStateVector2f trackpad;
    XrActionStateBoolean trackpad_click;
    XrActionStateBoolean trackpad_touch;
    XrActionStateFloat trigger_value;
    XrActionStateBoolean grip_click;
    XrActionStateFloat grip_force;

    // Calculated state
    XrPosef controller_pose;
    bool is_active;
};


#ifdef __cplusplus
extern ControllerInputState gControllerStates[2]; // [0] = left, [1] = right
#endif

// Paths for left/right hand
extern XrPath gHandPaths[2];

// === INITIALIZATION FUNCTIONS ===

/**
 * @brief Converts a string to XrPath
 * @param instance OpenXR instance
 * @param path_string Path string
 * @param path Pointer to the output XrPath
 * @return XrResult Operation result
 */
XrResult vr_string_to_path(XrInstance instance, const char* path_string, XrPath* path);

/**
 * @brief Fully initializes the VR controller system with all interaction profiles
 * @return XrResult Initialization result
 */
XrResult create_vr_controllers_complete();


// === UPDATE FUNCTIONS ===

/**
 * @brief Updates the states of all controllers (to be called every frame)
 * @param predicted_time Predicted time for the frame
 * @return XrResult Update result
 */
XrResult update_vr_controllers(XrTime predicted_time);

void controller_pose(); // Updates the positions and orientations of the controllers


#ifdef __cplusplus
extern "C" {
#endif

/**
* @brief Gets the pose of a controller
* @param hand_index Controller index (0 = left, 1 = right)
* @param pose Pointer to the output pose
* @return true if the pose is valid, false otherwise
*/
bool get_controller_pose(int hand_index, XrPosef* pose);


bool get_button_state(int hand_index, const char* button_name);

/**
 * @brief Gets the 2D value of an input (joystick, trackpad)
 * @param hand_index Controller index (0 = left, 1 = right)
 * @param input_name Input name ("thumbstick", "trackpad")
 * @param value Pointer to the output 2D value
 * @return true if the value is valid, false otherwise
 */
bool get_2d_input(int hand_index, const char* input_name, XrVector2f* value);

#ifdef __cplusplus
}
#endif


/**
 * @brief Gets the analog value of an input
 * @param hand_index Controller index (0 = left, 1 = right)
 * @param input_name Input name ("trigger", "grip")
 * @return Value between 0.0 and 1.0
 */
float get_analog_value(int hand_index, const char* input_name);



// === HAPTIC FUNCTIONS ===

/**
 * @brief Triggers a haptic vibration on a controller
 * @param hand_index Controller index (0 = left, 1 = right)
 * @param amplitude Vibration amplitude (0.0 to 1.0)
 * @param duration Duration in seconds (< 0 for minimum duration)
 * @param frequency Frequency in Hz (XR_FREQUENCY_UNSPECIFIED by default)
 * @return XrResult Operation result
 */
#ifdef __cplusplus
XrResult trigger_haptic_vibration(int hand_index, float amplitude, float duration, float frequency = XR_FREQUENCY_UNSPECIFIED);
#endif

#ifdef __cplusplus
extern "C" {
#endif
// Wrapper version for C (without default parameter)
XrResult trigger_haptic_vibration_c(int hand_index, float amplitude, float duration);
XrResult stop_haptic_vibration_c(int hand_index);
#ifdef __cplusplus
}
#endif
/**
 * @brief Stops the haptic vibration on a controller
 * @param hand_index Controller index (0 = left, 1 = right)
 * @return XrResult Operation result
 */
#ifdef __cplusplus
XrResult stop_haptic_vibration(int hand_index);
#endif

// === DEBUG FUNCTIONS ===

/**
 * @brief Displays the state of all controllers in the logs
 */
void log_controller_states();

/**
 * @brief Example of using controller functions
 */
void example_vr_input_usage();

#endif // XR_INPUT_H
