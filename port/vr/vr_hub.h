#pragma once
// ============================================================================
// VR Hub - Simple pause-menu 3D environment (flat grid floor + gradient sky)
// Renders directly into the currently-bound multiview eye FBO, using
// GL_OVR_multiview2 exactly like the game's own shaders do.
// ============================================================================

#ifdef __cplusplus
extern "C" {
#endif

// Call once, after the GL context exists and use_multiview/g_multiviewFBO
// have been created (i.e. after vr_create_eye_fbos() succeeds).
void vr_hub_init(void);

// Call every frame INSTEAD of the game's normal 3D draw calls, while
// vr_dl_is_pause_or_menu is true. Assumes g_multiviewFBO is already bound
// (same FBO the game normally draws its world into).
//
// eyeViewProj[0] = left eye (gl_ViewID_OVR == 0), combined Proj * View,
// eyeViewProj[1] = right eye (gl_ViewID_OVR == 1), combined Proj * View.
// Both are column-major 4x4 matrices (16 floats), same convention as the
// rest of vr_openxr.cpp (see ProjectionFromFov / Mat4Mul).
void vr_hub_render(const float eyeViewProj[2][16]);

void vr_hub_shutdown(void);

#ifdef __cplusplus
}
#endif
