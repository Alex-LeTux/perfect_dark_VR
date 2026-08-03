#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <map>
#include <unordered_map>
#include <vector>

#include <SDL.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/gbi.h>
#include "glad/glad.h"
#include "gfx_cc.h"
#include "gfx_rendering_api.h"
#include "gfx_pc.h"
#include "../vr/vr_log.h"
#ifdef ANDROID
#include <android/log.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "PD-VR", __VA_ARGS__)
#else
#define LOGI(...) vr_log(__VA_ARGS__)
#endif



// ============================================================================
// GLOBAL STATE - OVR_multiview / Render Targets / others
// ============================================================================
bool use_multiview = false;
// Per eye: IPD translation, horizontal frustum centre, HUD parallax,
// vertical frustum centre.
float s_eye_offsets[8] = { 0.0f, 0.0f, 0.0f, 0.0f,
                           0.0f, 0.0f, 0.0f, 0.0f };
extern float g_eyeTanHalfFov[2];
extern "C" bool vr_dl_is_pause_or_menu;
extern float vr_world_scale;
extern bool is_meta_runtime;
bool copy_fbo_menu = false;
bool VrIsTitleLegal = true;
//bool capture_begin = false;
static bool hud_L_was_drawn = false;
static bool hud_R_was_drawn = false;
static bool hud_H_was_drawn = false;
// ============================================================================
// GLOBAL STATE - MSAA
// ============================================================================
typedef void (*PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC)(
        GLenum, GLenum, GLuint, GLint, GLint, GLsizei);
PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC
        glFramebufferTextureMultiviewOVR = nullptr;

typedef void (APIENTRY* PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC)(GLenum target, GLenum attachment, GLuint texture, GLint level, GLsizei samples, GLint baseViewIndex, GLsizei numViews);
PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC pfnFramebufferTextureMultisampleMultiviewOVR = nullptr;

// ============================================================================
// GLOBAL STATE - GLuint
// ============================================================================
extern GLuint g_currentMultiviewSwapchainTex;
extern "C" GLuint vr_get_current_multiview_swapchain_tex();

static GLuint s_mirror_prog  = 0;
static GLuint s_mirror_vao   = 0;
static GLint  s_mirror_uloc_tex   = -1;
static GLint  s_mirror_uloc_layer = -1;
static GLint  s_mirror_uloc_rect  = -1;

static GLuint mv_blit_prog = 0;
static GLint mv_blit_uTexLoc = -1;
static GLint mv_blit_uFlipYLoc = -1;
static GLint mv_blit_uRectLoc = -1;

// ============================================================================
// GLOBAL STATE - gVrMenuH Head XR layer
// ============================================================================
static bool gVrMenuLWasClearedThisFrame = false;
static int gVrMenuLCaptureDepth = 0;

static bool gVrMenuHWasClearedThisFrame = false;
static int gVrMenuHCaptureDepth = 0;

static GLint gCurEyeOffsetLeftLoc  = -1;
static GLint gCurEyeOffsetRightLoc = -1;

static int gMenuCaptureRefCount = 0;
bool gForceFlatShaderForMenu = false;

static inline void gfx_opengl_menu_capture_push(void) {
    gMenuCaptureRefCount++;
    gForceFlatShaderForMenu = true;
}

static inline void gfx_opengl_menu_capture_pop(void) {
    if (gMenuCaptureRefCount > 0) gMenuCaptureRefCount--;
    gForceFlatShaderForMenu = (gMenuCaptureRefCount > 0);
}
// ============================================================================
// GLOBAL STATE - Mirror Layers
// ============================================================================
enum VrMirrorMenuSource {
    VR_MIRROR_MENU_L = 0,
    VR_MIRROR_MENU_R = 1,
    VR_MIRROR_MENU_H = 2,
};

struct VrMirrorMenuEntry {
    VrMirrorMenuSource source;
    float mvp[16];
};

extern "C" int vrGetMenuMirrorMVPList(int eyeIndex, VrMirrorMenuEntry* outEntries);
extern GLuint gfx_opengl_get_vr_menu_texture(void);
extern GLuint gfx_opengl_get_vr_menu_texture_R(void);
extern GLuint gfx_opengl_get_vr_menu_texture_H(void);
//---

// Fullscreen tri, VS without attributes (uses gl_VertexID)
//#version 300 es if gles, otherwise 330 core
static const char* mv_blit_vs_src =
        "#version 300 es\n"
        "precision highp float;\n"
        "out vec2 vUV;\n"
        "const vec2 pos[3] = vec2[3](\n"
        "    vec2(-1.0,-1.0),\n"
        "    vec2( 3.0,-1.0),\n"
        "    vec2(-1.0, 3.0)\n"
        ");\n"
        "void main() {\n"
        "    vec2 p = pos[gl_VertexID];\n"
        "    gl_Position = vec4(p, 0.0, 1.0);\n"
        "    vUV = p * 0.5 + 0.5;\n"
        "}\n";


#ifdef ANDROID
static const char* mv_blit_fs_src =
        "#version 300 es\n"
        "precision highp float;\n"
        "in vec2 vUV;\n"
        "out vec4 outColor;\n"
        "uniform sampler2DArray uTex;\n"
        "uniform int uLayer;\n"
        "uniform int uFlipY;\n"
        "uniform vec4 uRect; // u0, v0, u1, v1\n"
        "void main() {\n"
        "    vec2 uv = vUV;\n"
        "    if (uFlipY != 0) uv.y = 1.0 - uv.y;\n"
        "    // Remap 0..1 to the source sub-rectangle\n"
        "    vec2 srcUV;\n"
        "    srcUV.x = mix(uRect.x, uRect.z, uv.x);\n"
        "    srcUV.y = mix(uRect.y, uRect.w, uv.y);\n"
        "    outColor = texture(uTex, vec3(srcUV, float(uLayer)));\n"
        "}\n";


#else
extern "C" int gfx_sdl_get_mirror_eye();
extern "C" bool gfx_sdl_is_mirror_enabled();
static GLint s_mirror_uloc_sbs = -1;
extern "C" bool gfx_sdl_is_mirror_sbs();

static GLuint s_logo_tex = 0;
int    logo_w   = 0;
int    logo_h   = 0;

extern "C" GLuint gfx_opengl_get_logo_tex()  { return s_logo_tex; }
extern "C" bool mirror_enabled;
extern "C" void mirror_apply_size(bool enabled);

static GLuint load_bmp_texture(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
//        vr_log("load_bmp_texture: cannot open %s", path);
        return 0;
    }

    uint8_t header[54];
    if (fread(header, 1, 54, f) != 54 || header[0] != 'B' || header[1] != 'M') {
//        vr_log("load_bmp_texture: not a valid BMP file");
        fclose(f);
        return 0;
    }

    int w      = *(int*)&header[18];
    int h      = *(int*)&header[22];
    int offset = *(int*)&header[10];
    int bpp    = *(uint16_t*)&header[28]; // bits per pixel

    fseek(f, offset, SEEK_SET);

    int bytes_per_pixel = bpp / 8; // 3 or 4
    int row_size = (w * bytes_per_pixel + 3) & ~3;

    std::vector<uint8_t> raw(row_size * abs(h));
    fread(raw.data(), 1, raw.size(), f);
    fclose(f);

    // Convert BGR(A) to RGB and flip vertically
    std::vector<uint8_t> pixels(w * abs(h) * 3);
    for (int y = 0; y < abs(h); y++) {
        int src_y = (h > 0) ? (abs(h) - 1 - y) : y;
        for (int x = 0; x < w; x++) {
            uint8_t* src = &raw[src_y * row_size + x * bytes_per_pixel];
            uint8_t* dst = &pixels[(y * w + x) * 3];
            dst[0] = src[2]; // R ← B
            dst[1] = src[1]; // G
            dst[2] = src[0]; // B ← R
        }
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, abs(h), 0,
                 GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

//    vr_log("load_bmp_texture: loaded %s (%dx%d, %dbpp)", path, w, abs(h), bpp);
    return tex;
}

extern "C" void gfx_opengl_load_mirror_logo(const char* path) {
    // Re-open to read dimensions from the BMP header
    FILE* f = fopen(path, "rb");
    if (!f) return;
    uint8_t header[54];
    if (fread(header, 1, 54, f) == 54 && header[0] == 'B' && header[1] == 'M') {
        logo_w = *(int*)&header[18];
        logo_h = abs(*(int*)&header[22]);
    }
    fclose(f);
    s_logo_tex = load_bmp_texture(path);
}


static const char* mv_blit_fs_src =
        "#version 300 es\n"
        "precision highp float;\n"
        "in vec2 vUV;\n"
        "out vec4 outColor;\n"
        "uniform sampler2DArray uTex;\n"
        "uniform int uLayer;\n"
        "uniform int uFlipY;\n"
        "uniform vec4 uRect;\n"
        "uniform int uSbs;\n"
        // sRGB encoding function
        "vec3 linear_to_srgb(vec3 c) {\n"
        "    return mix(c * 12.92,\n"
        "               1.055 * pow(clamp(c, 0.0, 1.0), vec3(1.0/2.2)) - 0.055,\n"
        "               step(0.0031308, c));\n"
        "}\n"
        "void main() {\n"
        "    vec2 uv = vUV;\n"
        "    if (uFlipY != 0) uv.y = 1.0 - uv.y;\n"
        "    vec4 col;\n"
        "    if (uSbs != 0) {\n"
        //      Left side (uv.x < 0.5) = eye 0, right side = eye 1
        "        int eye = (uv.x < 0.5) ? 0 : 1;\n"
        "        vec2 half_uv = vec2(\n"
        "            (eye == 0) ? uv.x * 2.0 : (uv.x - 0.5) * 2.0,\n"
        "            uv.y\n"
        "        );\n"
        //      Apply uRect in each half
        "        vec2 srcUV;\n"
        "        srcUV.x = mix(uRect.x, uRect.z, half_uv.x);\n"
        "        srcUV.y = mix(uRect.y, uRect.w, half_uv.y);\n"
        "        col = texture(uTex, vec3(srcUV, float(eye)));\n"
        "    } else {\n"
        //      Normal mode — single eye
        "        vec2 srcUV;\n"
        "        srcUV.x = mix(uRect.x, uRect.z, uv.x);\n"
        "        srcUV.y = mix(uRect.y, uRect.w, uv.y);\n"
        "        col = texture(uTex, vec3(srcUV, float(uLayer)));\n"
        "    }\n"
        "    outColor = vec4(linear_to_srgb(col.rgb), 1.0);\n"
        "}\n";

#endif

//---------------------------------------------------------

using namespace std;

struct ShaderProgram {
    GLuint opengl_program_id;
    uint8_t num_inputs;
    bool used_textures[SHADER_MAX_TEXTURES];
    uint8_t num_floats;
    GLint attrib_locations[16];
    uint8_t attrib_sizes[16];
    uint8_t num_attribs;
    GLint frame_count_location;
    GLint noise_scale_location;
    GLint three_point_filter_locations[2];


    GLint eyeOffsetLeftLocation;
    GLint eyeOffsetRightLocation;

    GLint isMenuLocation;
    GLint worldScaleLocation;

    GLint crosshairParallaxLoc;
    GLint crosshairParallaxLeftLoc;

    GLint IsTitleLegal;

    GLint TanHalfFovLeft;
    GLint TanHalfFovRight;

};



struct Framebuffer {
    uint32_t width, height;
    bool has_depth_buffer;
    uint32_t msaa_level;
    bool invert_y;

    bool is_multiview; // VR

    GLuint fbo, clrbuf, clrbuf_msaa, rbo;
};

static std::map<pair<uint64_t, uint32_t>, struct ShaderProgram> shader_program_pool;
static GLuint opengl_vbo;
static GLuint opengl_vao;
static bool current_depth_mask;

static uint32_t frame_count;

static std::vector<Framebuffer> framebuffers;
static size_t current_framebuffer;
static float current_noise_scale;
static FilteringMode current_filter_mode = FILTER_LINEAR;
static bool current_textures_linear_filter[2] = { false, false };

#ifdef ANDROID // VR
static int gl_glsl_version = 130;
static char gl_glsl_version_str[16] = "130";
#else
static int gl_glsl_version = 330;
static char gl_glsl_version_str[16] = "330";
#endif

static GLenum gl_mirror_clamp = GL_MIRROR_CLAMP_TO_EDGE;
static bool gl_es = false;
static bool gl_core_profile = false;




//----------------------------------------------VR

static bool gfx_opengl_is_multiview(void) {
    return use_multiview;
}


static void gfx_opengl_set_eye_offsets(float left_ipd, float left_asym_x, float left_hud, float left_asym_y,
                                       float right_ipd, float right_asym_x, float right_hud, float right_asym_y) {
    // Left eye (gl_ViewID_OVR == 0)
    s_eye_offsets[0] = left_ipd;     // vec4.x: 3D IPD
    s_eye_offsets[1] = left_asym_x;  // vec4.y: horizontal lens asymmetry
    s_eye_offsets[2] = left_hud;     // vec4.z: 2D offset (HUD)
    s_eye_offsets[3] = left_asym_y;  // vec4.w: vertical lens asymmetry

    // Right eye (gl_ViewID_OVR == 1)
    s_eye_offsets[4] = right_ipd;
    s_eye_offsets[5] = right_asym_x;
    s_eye_offsets[6] = right_hud;
    s_eye_offsets[7] = right_asym_y;

}

static float g_crosshairParallaxRight = 0.0f;
static float g_crosshairParallaxLeft  = 0.0f;



extern "C" void gfxSetCrosshairParallaxRight(float correction) {
    g_crosshairParallaxRight = correction;
}

extern "C" void gfxSetCrosshairParallaxLeft(float correction) {
    g_crosshairParallaxLeft = correction;
}




extern "C" void gfx_opengl_connect_multiview_fbo(GLuint fbo_id, uint32_t width, uint32_t height) {
    if (framebuffers.empty()) framebuffers.resize(1);
    Framebuffer& fb = framebuffers[0];
    fb.fbo = fbo_id;   // points to gmultiviewFBO
    fb.width = width;
    fb.height = height;
    fb.has_depth_buffer = true;
    fb.is_multiview = true;
    fb.invert_y = false;
    fb.msaa_level = 1;
    fb.clrbuf = fb.clrbuf_msaa = fb.rbo = 0;
    use_multiview = true;
}

void gfx_opengl_init_multiview() { // VR
    bool found = false;
    if (gl_es) {
        GLint numExts = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &numExts);
        for (GLint i = 0; i < numExts; i++) {
            const char* e = (const char*)glGetStringi(GL_EXTENSIONS, i);
            if (e && (strcmp(e, "GL_OVR_multiview2") == 0 ||
                      strcmp(e, "GL_OVR_multiview") == 0)) {
                found = true;
                break;
            }
        }
    }
    else {
        const char* e = (const char*)glGetString(GL_EXTENSIONS);
        found = e && strstr(e, "GL_OVR_multiview");
    }

    if (!found) {
        return;
    }

    glFramebufferTextureMultiviewOVR =
            (PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC)
                    SDL_GL_GetProcAddress("glFramebufferTextureMultiviewOVR");
    if (!glFramebufferTextureMultiviewOVR)
        sysFatalError("Could not resolve glFramebufferTextureMultiviewOVR");

    // MSAA multiview
    pfnFramebufferTextureMultisampleMultiviewOVR =
            (PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC)SDL_GL_GetProcAddress("glFramebufferTextureMultisampleMultiviewOVR");

    if (!pfnFramebufferTextureMultisampleMultiviewOVR) {
        sysLogPrintf(LOG_WARNING, "GL: glFramebufferTextureMultisampleMultiviewOVR not available, multiview MSAA disabled");
    }

    use_multiview = true;
}

static void mv_blit_init() {
    if (mv_blit_prog != 0) return;

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &mv_blit_vs_src, NULL);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &mv_blit_fs_src, NULL);
    glCompileShader(fs);

    mv_blit_prog = glCreateProgram();
    glAttachShader(mv_blit_prog, vs);
    glAttachShader(mv_blit_prog, fs);
    glLinkProgram(mv_blit_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    mv_blit_uTexLoc = glGetUniformLocation(mv_blit_prog, "uTex");
    mv_blit_uFlipYLoc = glGetUniformLocation(mv_blit_prog, "uFlipY");
    mv_blit_uRectLoc = glGetUniformLocation(mv_blit_prog, "uRect");

}


// VR layers mirror
#ifndef ANDROID // if PC

static GLuint menuOverlayProg = 0;
static GLint  menuOverlayMvpLoc = -1;
static GLint  menuOverlayTexLoc = -1;
static GLuint menuOverlayVao = 0, menuOverlayVbo = 0;

static const char* menuOverlayVsSrc =
        "#version 330 core\n"
        "layout(location=0) in vec3 aPos;\n"
        "layout(location=1) in vec2 aUV;\n"
        "uniform mat4 uMVP;\n"
        "out vec2 vUV;\n"
        "void main() {\n"
        "    gl_Position = uMVP * vec4(aPos, 1.0);\n"
        "    vUV = aUV;\n"
        "}\n";

static const char* menuOverlayFsSrc =
        "#version 330 core\n"
        "in vec2 vUV;\n"
        "out vec4 outColor;\n"
        "uniform sampler2D uTex;\n"
        "void main() {\n"
        "    outColor = texture(uTex, vUV);\n"
        "}\n";

static void menuOverlayInit() {
    if (menuOverlayProg != 0) return;

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &menuOverlayVsSrc, NULL);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &menuOverlayFsSrc, NULL);
    glCompileShader(fs);

    menuOverlayProg = glCreateProgram();
    glAttachShader(menuOverlayProg, vs);
    glAttachShader(menuOverlayProg, fs);
    glLinkProgram(menuOverlayProg);
    glDeleteShader(vs);
    glDeleteShader(fs);

    menuOverlayMvpLoc = glGetUniformLocation(menuOverlayProg, "uMVP");
    menuOverlayTexLoc = glGetUniformLocation(menuOverlayProg, "uTex");

    float verts[] = {
            // pos              // uv
            -0.5f, -0.5f, 0.0f,  0.0f, 0.0f,
            0.5f, -0.5f, 0.0f,  1.0f, 0.0f,
            0.5f,  0.5f, 0.0f,  1.0f, 1.0f,
            -0.5f, -0.5f, 0.0f,  0.0f, 0.0f,
            0.5f,  0.5f, 0.0f,  1.0f, 1.0f,
            -0.5f,  0.5f, 0.0f,  0.0f, 1.0f,
    };

    glGenVertexArrays(1, &menuOverlayVao);
    glGenBuffers(1, &menuOverlayVbo);
    glBindVertexArray(menuOverlayVao);
    glBindBuffer(GL_ARRAY_BUFFER, menuOverlayVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);
}

extern "C" void gfx_opengl_draw_mirror_menu_overlay(GLuint menuTex, const float* mvp,
                                                    int vpX, int vpY, int vpW, int vpH) {
    if (menuTex == 0 || mvp == NULL) return;
    menuOverlayInit();

    GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glEnable(GL_SCISSOR_TEST);
    glViewport(vpX, vpY, vpW, vpH);
    glScissor(vpX, vpY, vpW, vpH);

    glUseProgram(menuOverlayProg);
    glUniformMatrix4fv(menuOverlayMvpLoc, 1, GL_FALSE, mvp);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, menuTex);
    glUniform1i(menuOverlayTexLoc, 0);

    glBindVertexArray(menuOverlayVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glDisable(GL_SCISSOR_TEST);
    if (depthWasEnabled) glEnable(GL_DEPTH_TEST);
    glUseProgram(0);
}
#endif
//---------------------------------------------------------------


static int gfx_opengl_get_max_texture_size() {
    GLint max_texture_size;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    return max_texture_size;
}

static const char* gfx_opengl_get_name() {
    return "OpenGL";
}

static struct GfxClipParameters gfx_opengl_get_clip_parameters(void) {
    return { false, framebuffers[current_framebuffer].invert_y };
}

static void gfx_opengl_vertex_array_set_attribs(struct ShaderProgram* prg) {
    size_t num_floats = prg->num_floats;
    size_t pos = 0;

    for (int i = 0; i < prg->num_attribs; i++) {
        if (prg->attrib_locations[i] >= 0) {
            glEnableVertexAttribArray(prg->attrib_locations[i]);
            glVertexAttribPointer(prg->attrib_locations[i], prg->attrib_sizes[i], GL_FLOAT, GL_FALSE,
                                  num_floats * sizeof(float), (void*)(pos * sizeof(float)));
        }
        pos += prg->attrib_sizes[i];
    }
}

static void gfx_opengl_set_uniforms(struct ShaderProgram* prg) {
    if (prg->frame_count_location >= 0) {
        glUniform1i(prg->frame_count_location, frame_count);
    }
    if (prg->noise_scale_location >= 0) {
        glUniform1f(prg->noise_scale_location, current_noise_scale);
    }
    if (prg->three_point_filter_locations[0] >= 0) {
        glUniform1i(prg->three_point_filter_locations[0], current_textures_linear_filter[0]);
    }
    if (prg->three_point_filter_locations[1] >= 0) {
        glUniform1i(prg->three_point_filter_locations[1], current_textures_linear_filter[1]);
    }


    if (use_multiview) { // VR

        if (prg->eyeOffsetLeftLocation >= 0)
            glUniform4f(prg->eyeOffsetLeftLocation,
                        s_eye_offsets[0], s_eye_offsets[1], s_eye_offsets[2], s_eye_offsets[3]);

        if (prg->eyeOffsetRightLocation >= 0)
            glUniform4f(prg->eyeOffsetRightLocation,
                        s_eye_offsets[4], s_eye_offsets[5], s_eye_offsets[6], s_eye_offsets[7]);

        if (prg->isMenuLocation >= 0)
            glUniform1i(prg->isMenuLocation, vr_dl_is_pause_or_menu ? 1 : 0);

        if (prg->worldScaleLocation >= 0)
            glUniform1f(prg->worldScaleLocation, vr_world_scale);

        if (prg->crosshairParallaxLoc >= 0)
            glUniform1f(prg->crosshairParallaxLoc, g_crosshairParallaxRight);

        if (prg->crosshairParallaxLeftLoc >= 0)
            glUniform1f(prg->crosshairParallaxLeftLoc, g_crosshairParallaxLeft);

        if (prg->IsTitleLegal >= 0)
            glUniform1i(prg->IsTitleLegal, VrIsTitleLegal ? 1 : 0);

        if (prg->TanHalfFovLeft >= 0)
            glUniform1f(prg->TanHalfFovLeft, g_eyeTanHalfFov[0]);

        if (prg->TanHalfFovRight >= 0)
            glUniform1f(prg->TanHalfFovRight, g_eyeTanHalfFov[1]);


    }

}

static void gfx_opengl_unload_shader(struct ShaderProgram* old_prg) {
    if (old_prg != NULL) {
        for (int i = 0; i < old_prg->num_attribs; i++) {
            if (old_prg->attrib_locations[i] >= 0) {
                glDisableVertexAttribArray(old_prg->attrib_locations[i]);
            }
        }
    }
}

static void gfx_opengl_load_shader(struct ShaderProgram* new_prg) {
    // if (!new_prg) return;
    glUseProgram(new_prg->opengl_program_id);
    gfx_opengl_vertex_array_set_attribs(new_prg);
    gfx_opengl_set_uniforms(new_prg);

    gCurEyeOffsetLeftLoc  = new_prg->eyeOffsetLeftLocation;
    gCurEyeOffsetRightLoc = new_prg->eyeOffsetRightLocation;
}

static void append_str(char* buf, size_t* len, const char* str) {
    while (*str != '\0') {
        buf[(*len)++] = *str++;
    }
}

static void append_line(char* buf, size_t* len, const char* str) {
    while (*str != '\0') {
        buf[(*len)++] = *str++;
    }
    buf[(*len)++] = '\n';
}

#define RAND_NOISE "((random(vec3(floor(gl_FragCoord.xy * noise_scale), float(frame_count))) + 1.0) / 2.0)"

static const char* shader_item_to_str(uint32_t item, bool with_alpha, bool only_alpha, bool inputs_have_alpha,
                                      bool hint_single_element) {
    if (!only_alpha) {
        switch (item) {
            case SHADER_0:
                return with_alpha ? "vec4(0.0, 0.0, 0.0, 0.0)" : "vec3(0.0, 0.0, 0.0)";
            case SHADER_1:
                return with_alpha ? "vec4(1.0, 1.0, 1.0, 1.0)" : "vec3(1.0, 1.0, 1.0)";
            case SHADER_INPUT_1:
                return with_alpha || !inputs_have_alpha ? "vInput1" : "vInput1.rgb";
            case SHADER_INPUT_2:
                return with_alpha || !inputs_have_alpha ? "vInput2" : "vInput2.rgb";
            case SHADER_INPUT_3:
                return with_alpha || !inputs_have_alpha ? "vInput3" : "vInput3.rgb";
            case SHADER_INPUT_4:
                return with_alpha || !inputs_have_alpha ? "vInput4" : "vInput4.rgb";
            case SHADER_TEXEL0:
                return with_alpha ? "texVal0" : "texVal0.rgb";
            case SHADER_TEXEL0A:
                return hint_single_element ? "texVal0.a"
                                           : (with_alpha ? "vec4(texVal0.a, texVal0.a, texVal0.a, texVal0.a)"
                                                         : "vec3(texVal0.a, texVal0.a, texVal0.a)");
            case SHADER_TEXEL1A:
                return hint_single_element ? "texVal1.a"
                                           : (with_alpha ? "vec4(texVal1.a, texVal1.a, texVal1.a, texVal1.a)"
                                                         : "vec3(texVal1.a, texVal1.a, texVal1.a)");
            case SHADER_TEXEL1:
                return with_alpha ? "texVal1" : "texVal1.rgb";
            case SHADER_COMBINED:
                return with_alpha ? "texel" : "texel.rgb";
            case SHADER_NOISE:
                return with_alpha ? "vec4(" RAND_NOISE ", " RAND_NOISE ", " RAND_NOISE ", " RAND_NOISE ")"
                                  : "vec3(" RAND_NOISE ", " RAND_NOISE ", " RAND_NOISE ")";
        }
    }
    else {
        switch (item) {
            case SHADER_0:
                return "0.0";
            case SHADER_1:
                return "1.0";
            case SHADER_INPUT_1:
                return "vInput1.a";
            case SHADER_INPUT_2:
                return "vInput2.a";
            case SHADER_INPUT_3:
                return "vInput3.a";
            case SHADER_INPUT_4:
                return "vInput4.a";
            case SHADER_TEXEL0:
                return "texVal0.a";
            case SHADER_TEXEL0A:
                return "texVal0.a";
            case SHADER_TEXEL1A:
                return "texVal1.a";
            case SHADER_TEXEL1:
                return "texVal1.a";
            case SHADER_COMBINED:
                return "texel.a";
            case SHADER_NOISE:
                return RAND_NOISE;
        }
    }
    return "";
}

#undef RAND_NOISE

static void append_formula(char* buf, size_t* len, uint8_t c[2][4], bool do_single, bool do_multiply, bool do_mix,
                           bool with_alpha, bool only_alpha, bool opt_alpha) {
    if (do_single) {
        append_str(buf, len, shader_item_to_str(c[only_alpha][3], with_alpha, only_alpha, opt_alpha, false));
    }
    else if (do_multiply) {
        append_str(buf, len, shader_item_to_str(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, " * ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true));
    }
    else if (do_mix) {
        append_str(buf, len, "mix(");
        append_str(buf, len, shader_item_to_str(c[only_alpha][1], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, ", ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, ", ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true));
        append_str(buf, len, ")");
    }
    else {
        append_str(buf, len, "(");
        append_str(buf, len, shader_item_to_str(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, " - ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][1], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, ") * ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true));
        append_str(buf, len, " + ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][3], with_alpha, only_alpha, opt_alpha, false));
    }
}



// OpenXR supplies asymmetric projection centres.  The original renderer only
// accounted for the horizontal centre, so carry the vertical centre in the
// fourth eye-offset component and apply it to clip-space Y below.
const char* vr_shader = R"(
vec4 mvPos = aVtxPos;

bool vr_is_Menu_or_HUD = (abs(mvPos.w - 1.0) == 0.0);
bool vr_is_Menu_or_crosshair_right = (abs(mvPos.w - 1.0) == 9.0);
bool vr_is_crosshair_left = (abs(mvPos.w - 1.0) == 7.0);
bool vr_is_Menu_blur = (abs(mvPos.w - 1.0) == 8.0);

vec4 eyeOffset = (gl_ViewID_OVR == 0u) ? uEyeOffsetLeft : uEyeOffsetRight;

// --------------------
// MENU / HUD IN PAUSE MODE (uIsMenu == 1)
// --------------------
if (uIsMenu == 1 && vr_is_Menu_or_HUD) {
//    mvPos.x -= eyeOffset.z * mvPos.w;
}
else if (uIsMenu == 1 && vr_is_Menu_or_crosshair_right) {
//    mvPos.x -= eyeOffset.z * mvPos.w;
}
else if (uIsMenu == 1 && vr_is_Menu_blur) {
    // Menu background (fullscreen blur)
    mvPos.x -= eyeOffset.z * mvPos.w;
}
else if (uIsMenu == 1 && !vr_is_Menu_blur) {
    // Menu 3D (fake 3D): same parallax as HUD
    mvPos.x -= eyeOffset.z * mvPos.w;
}


// --------------------
// GAME (uIsMenu == 0): HUD + crosshair
// --------------------
// Right crosshair / reticle (parallax parameterized on C side via uCrosshairParallax*)
float crosshairParallaxLocFinal =
        (gl_ViewID_OVR == 0u) ? -uCrosshairParallaxLoc : uCrosshairParallaxLoc;

float crosshairParallaxLeftLocFinal =
        (gl_ViewID_OVR == 0u) ? -uCrosshairParallaxLeftLoc - 0.020f
                              :  uCrosshairParallaxLeftLoc + 0.020f;


if (uIsMenu == 0 && vr_is_Menu_or_HUD) {
    mvPos.x -= eyeOffset.z * mvPos.w;
    mvPos.y -= eyeOffset.w * mvPos.w;
}

else if (uIsMenu == 0 && vr_is_Menu_or_crosshair_right) {
    mvPos.x -= (eyeOffset.z + crosshairParallaxLocFinal) * mvPos.w;
    mvPos.x += uCrosshairParallaxLoc * 2.0f;
    mvPos.y -= uCrosshairParallaxLoc * 2.0f;
    mvPos.y -= eyeOffset.w * mvPos.w;
}

else if (uIsMenu == 0 && vr_is_crosshair_left) {
    mvPos.x -= (eyeOffset.z + crosshairParallaxLeftLocFinal) * mvPos.w;
    mvPos.x += uCrosshairParallaxLoc * 2.0f;
    mvPos.y -= uCrosshairParallaxLoc * 2.0f;
    mvPos.w += 2.0f; // distance correction for the left crosshair
    mvPos.y -= eyeOffset.w * mvPos.w;
}

else if (uIsMenu == 0) {
    mvPos.x -= eyeOffset.x + (eyeOffset.y * mvPos.w);
    mvPos.y -= eyeOffset.w * mvPos.w;
}

// --------------------
// "Legal" title splash
// --------------------
if (uIsTitleLegal == 1 && vr_is_Menu_or_HUD) {
    mvPos.w = 1.5f;
}


gl_Position = mvPos;
)";





static struct ShaderProgram* gfx_opengl_create_and_load_new_shader(uint64_t shader_id0, uint32_t shader_id1) {
    struct CCFeatures cc_features = { 0 };
    gfx_cc_get_features(shader_id0, shader_id1, &cc_features);

    // Shader variants used by menus can be substantially larger than gameplay
    // variants.  The VR prelude alone is over 3 KiB, so the former 4 KiB vertex
    // buffer could overflow while entering a menu and corrupt the native stack.
    char vs_buf[16384];
    char fs_buf[16384];
    size_t vs_len = 0;
    size_t fs_len = 0;
    size_t num_floats = 4;


    // Vertex shader

    vs_len += sprintf(vs_buf + vs_len, "#version %s\n", gl_glsl_version_str);


    if (use_multiview) { // VR
        append_line(vs_buf, &vs_len,
                    "#extension GL_OVR_multiview2 : require");
        append_line(vs_buf, &vs_len,
                    "layout(num_views = 2) in;");

        // --- New Menu uniforms ---
        append_line(vs_buf, &vs_len, "uniform int uIsMenu;");
        append_line(vs_buf, &vs_len, "uniform vec4 uEyeOffsetLeft;");
        append_line(vs_buf, &vs_len, "uniform vec4 uEyeOffsetRight;");
        append_line(vs_buf, &vs_len, "uniform float uWorldScale;");
        append_line(vs_buf, &vs_len, "uniform float uCrosshairParallaxLoc;");
        append_line(vs_buf, &vs_len, "uniform float uCrosshairParallaxLeftLoc;");

        append_line(vs_buf, &vs_len, "uniform int uIsTitleLegal;");
        append_line(vs_buf, &vs_len, "uniform float uTanHalfFovLeft;");
        append_line(vs_buf, &vs_len, "uniform float uTanHalfFovRight;");

    }


    if (gl_es) {
        append_line(vs_buf, &vs_len, "precision highp float;");
    }

    if (gl_glsl_version >= 130) {
        append_line(vs_buf, &vs_len, "#define INPUT in");
        append_line(vs_buf, &vs_len, "#define OUTPUT out");
    }
    else {
        append_line(vs_buf, &vs_len, "#define INPUT attribute");
        append_line(vs_buf, &vs_len, "#define OUTPUT varying");
    }

    append_line(vs_buf, &vs_len, "INPUT vec4 aVtxPos;");

    for (int i = 0; i < 2; i++) {
        if (cc_features.used_textures[i]) {
            vs_len += sprintf(vs_buf + vs_len, "INPUT vec2 aTexCoord%d;\n", i);
            vs_len += sprintf(vs_buf + vs_len, "OUTPUT vec2 vTexCoord%d;\n", i);
            num_floats += 2;
            for (int j = 0; j < 2; j++) {
                if (cc_features.clamp[i][j]) {
                    vs_len += sprintf(vs_buf + vs_len, "INPUT float aTexClamp%s%d;\n",
                                      j == 0 ? "S" : "T", i);
                    vs_len += sprintf(vs_buf + vs_len, "OUTPUT float vTexClamp%s%d;\n",
                                      j == 0 ? "S" : "T", i);
                    num_floats += 1;
                }
            }
        }
    }
    if (cc_features.opt_fog) {
        append_line(vs_buf, &vs_len, "INPUT vec4 aFog;");
        append_line(vs_buf, &vs_len, "OUTPUT vec4 vFog;");
        num_floats += 4;
    }

    if (cc_features.opt_grayscale) {
        append_line(vs_buf, &vs_len, "INPUT vec4 aGrayscaleColor;");
        append_line(vs_buf, &vs_len, "OUTPUT vec4 vGrayscaleColor;");
        num_floats += 4;
    }

    for (int i = 0; i < cc_features.num_inputs; i++) {
        vs_len += sprintf(vs_buf + vs_len, "INPUT vec%d aInput%d;\n", cc_features.opt_alpha ? 4 : 3,
                          i + 1);
        vs_len += sprintf(vs_buf + vs_len, "OUTPUT vec%d vInput%d;\n",
                          cc_features.opt_alpha ? 4 : 3, i + 1);
        num_floats += cc_features.opt_alpha ? 4 : 3;
    }

    append_line(vs_buf, &vs_len, "void main() {");
    for (int i = 0; i < 2; i++) {
        if (cc_features.used_textures[i]) {
            vs_len += sprintf(vs_buf + vs_len, "    vTexCoord%d = aTexCoord%d;\n", i, i);
            for (int j = 0; j < 2; j++) {
                if (cc_features.clamp[i][j]) {
                    vs_len += sprintf(vs_buf + vs_len, "    vTexClamp%s%d = aTexClamp%s%d;\n",
                                      j == 0 ? "S" : "T", i,
                                      j == 0 ? "S" : "T", i);
                }
            }
        }
    }
    if (cc_features.opt_fog) {
        append_line(vs_buf, &vs_len, "    vFog = aFog;");
    }
    if (cc_features.opt_grayscale) {
        append_line(vs_buf, &vs_len, "    vGrayscaleColor = aGrayscaleColor;");
    }
    for (int i = 0; i < cc_features.num_inputs; i++) {
        vs_len += sprintf(vs_buf + vs_len, "    vInput%d = aInput%d;\n", i + 1, i + 1);
    }


    // VR
    if (use_multiview) {
        append_line(vs_buf, &vs_len, vr_shader);

    }


    if (!GLAD_GL_ARB_depth_clamp) {
        // HACK: workaround when GL_DEPTH_CLAMP is unavailable
        append_line(vs_buf, &vs_len, "    gl_Position.z *= 0.3f;");
    }
    append_line(vs_buf, &vs_len, "}");

    // Fragment shader

    fs_len += sprintf(fs_buf + fs_len, "#version %s\n", gl_glsl_version_str);

    if (gl_es) {
        append_line(fs_buf, &fs_len, "precision highp float;");
    }

    if (gl_glsl_version >= 130) {
        append_line(fs_buf, &fs_len, "#define INPUT in");
        append_line(fs_buf, &fs_len, "#define OUTPUT_COLOR outColor");
        append_line(fs_buf, &fs_len, "#define SAMPLE_TEX(tex, uv) texture(tex, uv)");
    }
    else {
        append_line(fs_buf, &fs_len, "#define INPUT varying");
        append_line(fs_buf, &fs_len, "#define OUTPUT_COLOR gl_FragColor");
        append_line(fs_buf, &fs_len, "#define SAMPLE_TEX(tex, uv) texture2D(tex, uv)");
    }

    // Reference approach to color wrapping as in GLideN64
    // Return the wrapped value of x in the interval [low, high)
    append_line(fs_buf, &fs_len, "#define WRAP(x, low, high) mod((x)-(low), (high)-(low)) + (low)");

    append_line(fs_buf, &fs_len,
                "#define TEX_OFFSET(tex, uv, texSize, off) SAMPLE_TEX(tex, uv - (off)/texSize)");

    // append_line(fs_buf, &fs_len, "precision mediump float;");
    for (int i = 0; i < 2; i++) {
        if (cc_features.used_textures[i]) {
            fs_len += sprintf(fs_buf + fs_len, "INPUT vec2 vTexCoord%d;\n", i);
            for (int j = 0; j < 2; j++) {
                if (cc_features.clamp[i][j]) {
                    fs_len += sprintf(fs_buf + fs_len, "INPUT float vTexClamp%s%d;\n",
                                      j == 0 ? "S" : "T", i);
                }
            }
        }
    }
    if (cc_features.opt_fog) {
        append_line(fs_buf, &fs_len, "INPUT vec4 vFog;");
    }
    if (cc_features.opt_grayscale) {
        append_line(fs_buf, &fs_len, "INPUT vec4 vGrayscaleColor;");
    }
    for (int i = 0; i < cc_features.num_inputs; i++) {
        fs_len += sprintf(fs_buf + fs_len, "INPUT vec%d vInput%d;\n", cc_features.opt_alpha ? 4 : 3,
                          i + 1);
    }

    if (cc_features.used_textures[0]) {
        append_line(fs_buf, &fs_len, "uniform sampler2D uTex0;");
        if (current_filter_mode == FILTER_THREE_POINT)
            append_line(fs_buf, &fs_len, "uniform int three_point_filter0;");
    }
    if (cc_features.used_textures[1]) {
        append_line(fs_buf, &fs_len, "uniform sampler2D uTex1;");
        if (current_filter_mode == FILTER_THREE_POINT)
            append_line(fs_buf, &fs_len, "uniform int three_point_filter1;");
    }

    append_line(fs_buf, &fs_len, "uniform int frame_count;");
    append_line(fs_buf, &fs_len, "uniform float noise_scale;");

    append_line(fs_buf, &fs_len, "float random(in vec3 value) {");
    append_line(fs_buf, &fs_len,
                "    float random = dot(sin(value), vec3(12.9898, 78.233, 37.719));");
    append_line(fs_buf, &fs_len, "    return fract(sin(random) * 143758.5453);");
    append_line(fs_buf, &fs_len, "}");

    if (current_filter_mode == FILTER_THREE_POINT) {
        append_line(fs_buf, &fs_len,
                    "vec4 filter3point(in sampler2D tex, in vec2 texCoord, in vec2 texSize) {");
        append_line(fs_buf, &fs_len, "    vec2 offset = fract(texCoord*texSize - vec2(0.5));");
        append_line(fs_buf, &fs_len, "    offset -= step(1.0, offset.x + offset.y);");
        append_line(fs_buf, &fs_len, "    vec4 c0 = TEX_OFFSET(tex, texCoord, texSize, offset);");
        append_line(fs_buf, &fs_len,
                    "    vec4 c1 = TEX_OFFSET(tex, texCoord, texSize, vec2(offset.x - sign(offset.x), offset.y));");
        append_line(fs_buf, &fs_len,
                    "    vec4 c2 = TEX_OFFSET(tex, texCoord, texSize, vec2(offset.x, offset.y - sign(offset.y)));");
        append_line(fs_buf, &fs_len,
                    "    return c0 + abs(offset.x)*(c1-c0) + abs(offset.y)*(c2-c0);");
        append_line(fs_buf, &fs_len, "}");
    }

    if (cc_features.opt_blur) {
        // blur filter, used for menu backgrounds
        // used to be two for loops from 0 to 4, but apparently Intel drivers crashed when trying to unroll it
        // used to have a const weight array, but apparently drivers for the GT620 do not like const array initializers
        if (current_filter_mode == FILTER_THREE_POINT)
            append_line(fs_buf, &fs_len,
                        "lowp vec4 hookTexture2D(in sampler2D t, in vec2 uv, in vec2 texSize, in int three_point_filter) {");
        else
            append_line(fs_buf, &fs_len,
                        "lowp vec4 hookTexture2D(in sampler2D t, in vec2 uv, in vec2 texSize) {");

        append_line(fs_buf, &fs_len, "    lowp vec4 cw = vec4(0.0);");
        append_line(fs_buf, &fs_len, "    for (int i = 0; i < 16; ++i) {");
        append_line(fs_buf, &fs_len, "        vec2 xy = vec2(float(i & 3), float(i >> 2));");
        append_line(fs_buf, &fs_len, "        lowp float w = 0.009947 - length(xy) * 0.001;");
        append_line(fs_buf, &fs_len, "        vec2 scaled_uv = uv + (vec2(-1.5) + xy) / texSize;");

        if (current_filter_mode == FILTER_THREE_POINT)
            append_line(fs_buf, &fs_len,
                        "        lowp vec4 tex = mix(SAMPLE_TEX(t, scaled_uv), filter3point(t, scaled_uv, texSize), three_point_filter);");
        else
            append_line(fs_buf, &fs_len, "        lowp vec4 tex = SAMPLE_TEX(t, scaled_uv);");

        append_line(fs_buf, &fs_len, "        cw += vec4(tex.rgb * w, w);");
        append_line(fs_buf, &fs_len, "    }");
        append_line(fs_buf, &fs_len, "    return vec4(cw.rgb / cw.a, 1.0);");
        append_line(fs_buf, &fs_len, "}");
    }
    else {
        if (current_filter_mode == FILTER_THREE_POINT) {
            append_line(fs_buf, &fs_len,
                        "vec4 hookTexture2D(in sampler2D tex, in vec2 uv, in vec2 texSize, in int three_point_filter) {");
            append_line(fs_buf, &fs_len,
                        "    return mix(SAMPLE_TEX(tex, uv), filter3point(tex, uv, texSize), three_point_filter);");
            append_line(fs_buf, &fs_len, "}");
        }
        else {
            append_line(fs_buf, &fs_len,
                        "vec4 hookTexture2D(in sampler2D tex, in vec2 uv, in vec2 texSize) {");
            append_line(fs_buf, &fs_len, "    return SAMPLE_TEX(tex, uv);");
            append_line(fs_buf, &fs_len, "}");
        }
    }

    if (gl_glsl_version >= 130) {
        append_line(fs_buf, &fs_len, "out vec4 outColor;");
    }

    append_line(fs_buf, &fs_len, "void main() {");

    for (int i = 0; i < 2; i++) {
        if (cc_features.used_textures[i]) {
            bool s = cc_features.clamp[i][0], t = cc_features.clamp[i][1];

            fs_len += sprintf(fs_buf + fs_len,
                              "    vec2 texSize%d = vec2(textureSize(uTex%d, 0));\n", i, i);

            if (!s && !t) {
                fs_len += sprintf(fs_buf + fs_len, "    vec2 vTexCoordAdj%d = vTexCoord%d;\n", i,
                                  i);
            }
            else {
                if (s && t) {
                    fs_len += sprintf(fs_buf + fs_len,
                                      "    vec2 vTexCoordAdj%d = clamp(vTexCoord%d, 0.5 / texSize%d, "
                                      "vec2(vTexClampS%d, vTexClampT%d));\n",
                                      i, i, i, i, i);
                }
                else if (s) {
                    fs_len += sprintf(fs_buf + fs_len,
                                      "    vec2 vTexCoordAdj%d = vec2(clamp(vTexCoord%d.s, 0.5 / "
                                      "texSize%d.s, vTexClampS%d), vTexCoord%d.t);\n",
                                      i, i, i, i, i);
                }
                else {
                    fs_len += sprintf(fs_buf + fs_len,
                                      "    vec2 vTexCoordAdj%d = vec2(vTexCoord%d.s, clamp(vTexCoord%d.t, "
                                      "0.5 / texSize%d.t, vTexClampT%d));\n",
                                      i, i, i, i, i);
                }
            }

            if (current_filter_mode == FILTER_THREE_POINT)
                fs_len += sprintf(fs_buf + fs_len,
                                  "    vec4 texVal%d = hookTexture2D(uTex%d, vTexCoordAdj%d, texSize%d, three_point_filter%d);\n",
                                  i, i, i, i, i);
            else
                fs_len += sprintf(fs_buf + fs_len,
                                  "    vec4 texVal%d = hookTexture2D(uTex%d, vTexCoordAdj%d, texSize%d);\n",
                                  i, i, i, i);
        }
    }

    append_line(fs_buf, &fs_len, cc_features.opt_alpha ? "    vec4 texel;" : "    vec3 texel;");
    for (int c = 0; c < (cc_features.opt_2cyc ? 2 : 1); c++) {
        append_str(fs_buf, &fs_len, "    texel = ");
        if (!cc_features.color_alpha_same[c] && cc_features.opt_alpha) {
            append_str(fs_buf, &fs_len, "vec4(");
            append_formula(fs_buf, &fs_len, cc_features.c[c], cc_features.do_single[c][0],
                           cc_features.do_multiply[c][0], cc_features.do_mix[c][0], false, false,
                           true);
            append_str(fs_buf, &fs_len, ", ");
            append_formula(fs_buf, &fs_len, cc_features.c[c], cc_features.do_single[c][1],
                           cc_features.do_multiply[c][1], cc_features.do_mix[c][1], true, true,
                           true);
            append_str(fs_buf, &fs_len, ")");
        }
        else {
            append_formula(fs_buf, &fs_len, cc_features.c[c], cc_features.do_single[c][0],
                           cc_features.do_multiply[c][0], cc_features.do_mix[c][0],
                           cc_features.opt_alpha, false,
                           cc_features.opt_alpha);
        }
        append_line(fs_buf, &fs_len, ";");

        if (c == 0) {
            append_line(fs_buf, &fs_len, "    texel = WRAP(texel, -1.01, 1.01);");
        }
    }

    append_line(fs_buf, &fs_len, "    texel = WRAP(texel, -0.51, 1.51);");
    append_line(fs_buf, &fs_len, "    texel = clamp(texel, 0.0, 1.0);");
    // TODO discard if alpha is 0?
    if (cc_features.opt_fog) {
        if (cc_features.opt_alpha) {
            append_line(fs_buf, &fs_len,
                        "    texel = vec4(mix(texel.rgb, vFog.rgb, vFog.a), texel.a);");
        }
        else {
            append_line(fs_buf, &fs_len, "    texel = mix(texel, vFog.rgb, vFog.a);");
        }
    }

    if (cc_features.opt_texture_edge && cc_features.opt_alpha) {
        append_line(fs_buf, &fs_len, "    if (texel.a > 0.19) texel.a = 1.0; else discard;");
    }

    if (cc_features.opt_alpha && cc_features.opt_noise) {
        append_line(fs_buf, &fs_len,
                    "    texel.a *= floor(clamp(random(vec3(floor(gl_FragCoord.xy * noise_scale), float(frame_count))) + "
                    "texel.a, 0.0, 1.0));");
    }

    if (cc_features.opt_grayscale) {
        append_line(fs_buf, &fs_len, "    float intensity = (texel.r + texel.g + texel.b) / 3.0;");
        append_line(fs_buf, &fs_len, "    vec3 new_texel = vGrayscaleColor.rgb * intensity;");
        append_line(fs_buf, &fs_len,
                    "    texel.rgb = mix(texel.rgb, new_texel, vGrayscaleColor.a);");
    }


    if (cc_features.opt_alpha) {
        if (cc_features.opt_alpha_threshold) {
            append_line(fs_buf, &fs_len, "    if (texel.a < 8.0 / 256.0) discard;");
        }
        if (cc_features.opt_invisible) {
            append_line(fs_buf, &fs_len, "    texel.a = 0.0;");
        }

        append_line(fs_buf, &fs_len, "    OUTPUT_COLOR = texel;");
    }
    else {
        append_line(fs_buf, &fs_len, "    OUTPUT_COLOR = vec4(texel, 1.0);");
    }

    append_line(fs_buf, &fs_len, "}");

    vs_buf[vs_len] = '\0';
    fs_buf[fs_len] = '\0';

    const GLchar* sources[2] = { vs_buf, fs_buf };
    const GLint lengths[2] = { (GLint)vs_len, (GLint)fs_len };
    GLint success;

    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &sources[0], &lengths[0]);
    glCompileShader(vertex_shader);
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint max_length = 1024;
        glGetShaderiv(vertex_shader, GL_INFO_LOG_LENGTH, &max_length);
        char error_log[1024];
        glGetShaderInfoLog(vertex_shader, max_length, &max_length, &error_log[0]);
        sysLogPrintf(LOG_ERROR, "Failed to compile this vertex shader (ID %llx, %x):\n%s",
                     shader_id0, shader_id1, vs_buf);
        sysFatalError("Vertex shader compilation failed:\n%s", error_log);
    }

    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &sources[1], &lengths[1]);
    glCompileShader(fragment_shader);
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint max_length = 1024;
        glGetShaderiv(fragment_shader, GL_INFO_LOG_LENGTH, &max_length);
        char error_log[1024];
        glGetShaderInfoLog(fragment_shader, max_length, &max_length, &error_log[0]);
        sysLogPrintf(LOG_ERROR, "Failed to compile this fragment shader (ID %llx, %x):\n%s",
                     shader_id0, shader_id1, fs_buf);
        sysFatalError("Fragment shader compilation failed:\n%s", error_log);
    }

    GLuint shader_program = glCreateProgram();
    glAttachShader(shader_program, vertex_shader);
    glAttachShader(shader_program, fragment_shader);
    glLinkProgram(shader_program);

    glDetachShader(shader_program, vertex_shader);
    glDetachShader(shader_program, fragment_shader);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    size_t cnt = 0;

    struct ShaderProgram* prg = &shader_program_pool[make_pair(shader_id0, shader_id1)];
    prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, "aVtxPos");
    prg->attrib_sizes[cnt] = 4;
    ++cnt;

    for (int i = 0; i < 2; i++) {
        if (cc_features.used_textures[i]) {
            char name[32];
            sprintf(name, "aTexCoord%d", i);
            prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, name);
            prg->attrib_sizes[cnt] = 2;
            ++cnt;

            for (int j = 0; j < 2; j++) {
                if (cc_features.clamp[i][j]) {
                    sprintf(name, "aTexClamp%s%d", j == 0 ? "S" : "T", i);
                    prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, name);
                    prg->attrib_sizes[cnt] = 1;
                    ++cnt;
                }
            }
        }
    }

    if (cc_features.opt_fog) {
        prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, "aFog");
        prg->attrib_sizes[cnt] = 4;
        ++cnt;
    }

    if (cc_features.opt_grayscale) {
        prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, "aGrayscaleColor");
        prg->attrib_sizes[cnt] = 4;
        ++cnt;
    }

    for (int i = 0; i < cc_features.num_inputs; i++) {
        char name[16];
        sprintf(name, "aInput%d", i + 1);
        prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, name);
        prg->attrib_sizes[cnt] = cc_features.opt_alpha ? 4 : 3;
        ++cnt;
    }

    prg->opengl_program_id = shader_program;
    prg->num_inputs = cc_features.num_inputs;
    prg->used_textures[0] = cc_features.used_textures[0];
    prg->used_textures[1] = cc_features.used_textures[1];
    prg->num_floats = num_floats;
    prg->num_attribs = cnt;

    glUseProgram(shader_program);

    if (cc_features.used_textures[0]) {
        GLint sampler_location = glGetUniformLocation(shader_program, "uTex0");
        glUniform1i(sampler_location, 0);
    }
    if (cc_features.used_textures[1]) {
        GLint sampler_location = glGetUniformLocation(shader_program, "uTex1");
        glUniform1i(sampler_location, 1);
    }

    prg->frame_count_location = glGetUniformLocation(shader_program, "frame_count");
    prg->noise_scale_location = glGetUniformLocation(shader_program, "noise_scale");
    prg->three_point_filter_locations[0] = glGetUniformLocation(shader_program,
                                                                "three_point_filter0");
    prg->three_point_filter_locations[1] = glGetUniformLocation(shader_program,
                                                                "three_point_filter1");

    // VR...
    if (use_multiview) {
        prg->eyeOffsetLeftLocation = glGetUniformLocation(shader_program, "uEyeOffsetLeft");
        prg->eyeOffsetRightLocation = glGetUniformLocation(shader_program, "uEyeOffsetRight");
        prg->isMenuLocation = glGetUniformLocation(shader_program, "uIsMenu");
        prg->worldScaleLocation = glGetUniformLocation(shader_program, "uWorldScale");
        prg->crosshairParallaxLoc = glGetUniformLocation(shader_program, "uCrosshairParallaxLoc");
        prg->crosshairParallaxLeftLoc = glGetUniformLocation(shader_program, "uCrosshairParallaxLeftLoc");
        prg->IsTitleLegal = glGetUniformLocation(shader_program, "uIsTitleLegal");
        prg->TanHalfFovRight = glGetUniformLocation(shader_program, "uTanHalfFovRight");
        prg->TanHalfFovLeft = glGetUniformLocation(shader_program, "uTanHalfFovLeft");

    }

    gfx_opengl_load_shader(prg);

    return prg;
}

static struct ShaderProgram* gfx_opengl_lookup_shader(uint64_t shader_id0, uint32_t shader_id1) {
    auto it = shader_program_pool.find(make_pair(shader_id0, shader_id1));
    return it == shader_program_pool.end() ? nullptr : &it->second;
}

static void gfx_opengl_shader_get_info(struct ShaderProgram* prg, uint8_t* num_inputs, bool used_textures[2]) {
    *num_inputs = prg->num_inputs;
    used_textures[0] = prg->used_textures[0];
    used_textures[1] = prg->used_textures[1];
}

static void gfx_opengl_clear_shaders(void) {
    glUseProgram(0);
    for (auto& pair : shader_program_pool) {
        glDeleteProgram(pair.second.opengl_program_id);
    }
    shader_program_pool.clear();

}

static GLuint gfx_opengl_new_texture(void) {
    GLuint ret;
    glGenTextures(1, &ret);
    return ret;
}

static void gfx_opengl_delete_texture(uint32_t texID) {
    glDeleteTextures(1, &texID);
}

static void gfx_opengl_select_texture(int tile, GLuint texture_id, bool linear_filter) {

    glActiveTexture(GL_TEXTURE0 + tile);
    glBindTexture(GL_TEXTURE_2D, texture_id);

    current_textures_linear_filter[tile] = linear_filter;
}

static void gfx_opengl_upload_texture(const uint8_t* rgba32_buf, uint32_t width, uint32_t height) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba32_buf);
}

static uint32_t gfx_cm_to_opengl(uint32_t val) {
    switch (val) {
        case G_TX_NOMIRROR | G_TX_CLAMP:
            return GL_CLAMP_TO_EDGE;
        case G_TX_MIRROR | G_TX_WRAP:
            return GL_MIRRORED_REPEAT;
        case G_TX_MIRROR | G_TX_CLAMP:
            return gl_mirror_clamp;
        case G_TX_NOMIRROR | G_TX_WRAP:
            return GL_REPEAT;
    }
    return 0;
}

static void gfx_opengl_set_sampler_parameters(int tile, bool linear_filter, uint32_t cms, uint32_t cmt) {
    const GLint filter = linear_filter && (current_filter_mode == FILTER_LINEAR) ? GL_LINEAR : GL_NEAREST;
    glActiveTexture(GL_TEXTURE0 + tile);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gfx_cm_to_opengl(cms));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gfx_cm_to_opengl(cmt));
}

static void gfx_opengl_set_depth_mode(bool depth_test, bool depth_update, bool depth_compare, bool depth_source_prim, uint16_t zmode) {
    if (depth_test) {
        glEnable(GL_DEPTH_TEST);
        glDepthMask(depth_update ? GL_TRUE : GL_FALSE);
        current_depth_mask = depth_update;

        if (depth_compare) {
            switch (zmode) {
                case ZMODE_INTER:
                    glDepthFunc(GL_LEQUAL);
                    glDisable(GL_POLYGON_OFFSET_FILL);
                    glPolygonOffset(0, 0);
                    break;

                case ZMODE_OPA:
                case ZMODE_XLU:
                    if (depth_source_prim) {
                        glDepthFunc(GL_LEQUAL);
                    }
                    else {
                        glDepthFunc(GL_LESS);
                    }
                    glDisable(GL_POLYGON_OFFSET_FILL);
                    glPolygonOffset(0, 0);
                    break;

                case ZMODE_DEC:
                    glDepthFunc(GL_LEQUAL);
                    glEnable(GL_POLYGON_OFFSET_FILL);
                    glPolygonOffset(-2, -2);
                    break;
            }
        }
        else {
            glDepthFunc(GL_ALWAYS);
            glDisable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(0, 0);
        }
    }
    else {
        glDisable(GL_DEPTH_TEST);
    }
}

static void gfx_opengl_set_depth_range(float znear, float zfar) {
    if (glDepthRangef) {
        glDepthRangef(znear, zfar);
    }
    else {
        glDepthRange(znear, zfar);
    }
}

static void gfx_opengl_set_viewport(int x, int y, int width, int height) {
    glViewport(x, y, width, height);
}

static void gfx_opengl_set_scissor(int x, int y, int width, int height) {
    glScissor(x, y, width, height);
}

static void gfx_opengl_set_use_alpha(bool use_alpha, bool modulate) { // VR
    if (use_alpha) glEnable(GL_BLEND);
    else glDisable(GL_BLEND);

    if (modulate) {
        glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_DST_COLOR, GL_ZERO);
    } else {
        // RGB: standard straight-alpha blending
        // Alpha: correct destination alpha accumulation (no src_alpha squared)
        glBlendFuncSeparate(
                GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,   // RGB
                GL_ONE,       GL_ONE_MINUS_SRC_ALPHA);  // Alpha
    }
}


static void gfx_opengl_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {

    // printf("flushing %d tris\n", buf_vbo_num_tris);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * buf_vbo_len, buf_vbo, GL_STREAM_DRAW);

    if (gForceFlatShaderForMenu) {
        if (gCurEyeOffsetLeftLoc  >= 0) glUniform4f(gCurEyeOffsetLeftLoc,  0.0f, 0.0f, 0.0f, 0.0f);
        if (gCurEyeOffsetRightLoc >= 0) glUniform4f(gCurEyeOffsetRightLoc, 0.0f, 0.0f, 0.0f, 1000.0f); // hide
    }

    glDrawArrays(GL_TRIANGLES, 0, 3 * buf_vbo_num_tris);

}

typedef void (APIENTRY* DEBUGPROC)(GLenum source,
                                   GLenum type,
                                   GLuint id,
                                   GLenum severity,
                                   GLsizei length,
                                   const GLchar* message,
                                   const void* userParam);

static void APIENTRY gl_debug(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* msg, const void* p) {
    sysLogPrintf(LOG_WARNING, "GL: (%05x) %s", id, msg);
}

static void gfx_opengl_enable_debug(void) {
    if (GLAD_GL_KHR_debug) {
        glEnable(GL_DEBUG_OUTPUT);
    }
    if (glDebugMessageControl != NULL) {
        // enable everything except some specific spam messages
        const GLuint disable[] = {
                0x20061, /* "Framebuffer detailed info" */
                0x20071  /* "Buffer detailed info" */
        };
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, GL_TRUE);
        glDebugMessageControl(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_OTHER, GL_DONT_CARE, 2, disable, GL_FALSE);
    }
    if (glDebugMessageCallback != NULL) {
        glDebugMessageCallback(gl_debug, NULL);
    }
}

static bool gfx_opengl_supports_framebuffers(void) {
    if (GLVersion.major > 2) {
        // GL3.0+ supports everything we need, but we'll still check it for sanity
        return (glad_glFramebufferRenderbuffer && glad_glBlitFramebuffer && glad_glRenderbufferStorageMultisample);
    }
    if (GLAD_GL_ARB_framebuffer_object) {
        // some implementations might be missing these functions
        return (glad_glBlitFramebuffer && glad_glRenderbufferStorageMultisample);
    }
    if (GLAD_GL_EXT_framebuffer_object && GLAD_GL_EXT_framebuffer_blit && GLAD_GL_EXT_framebuffer_multisample) {
        // sanity check
        return (glad_glFramebufferRenderbuffer && glad_glBlitFramebuffer && glad_glRenderbufferStorageMultisample);
    }
    // nothing supported
    return false;
}

static bool gfx_opengl_supports_shaders(void) {
    if (GLVersion.major > 2) {
        // should support GLSL 1.30 or higher
        return true;
    }

    // check supported GLSL version
    const char* ver = (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);
    if (ver) {
        int maj = 0, min = 0;
        sscanf(ver, "%d.%d", &maj, &min);
        if (maj > 1 || (maj == 1 && min > 20)) {
            // above 1.20, it should be fine
            return true;
        }
    }

    // check for an extension that adds textureSize
    return GLAD_GL_EXT_gpu_shader4;
}

static void gfx_opengl_log_info(void) {
    const char* version = (const char*)glGetString(GL_VERSION);
    const char* vendor = (const char*)glGetString(GL_VENDOR);
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    const char* glsl_version = (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);
    sysLogPrintf(LOG_NOTE, "GL: version: %s", version ? version : "unknown");
    sysLogPrintf(LOG_NOTE, "GL: vendor: %s", vendor ? vendor : "unknown");
    sysLogPrintf(LOG_NOTE, "GL: renderer: %s", renderer ? renderer : "unknown");
    sysLogPrintf(LOG_NOTE, "GL: GLSL version: %s", glsl_version ? glsl_version : "unknown");
    sysLogPrintf(LOG_NOTE, "GL: ARB_framebuffer_object: %s", gfx_opengl_supports_framebuffers() ? "yes" : "no");
    sysLogPrintf(LOG_NOTE, "GL: ARB_depth_clamp: %s", GLAD_GL_ARB_depth_clamp ? "yes" : "no");
    sysLogPrintf(LOG_NOTE, "GL: ARB_texture_mirror_clamp_to_edge: %s", GLAD_GL_ARB_texture_mirror_clamp_to_edge ? "yes" : "no");
}

static void* gl_load_proc(const char* name) {
    void* ret = SDL_GL_GetProcAddress(name);
    if (ret) {
        return ret;
    }

    // try with postfixes
    static const char* post[] = { "ARB", "EXT" };
    char tmp[256] = { 0 };
    for (size_t i = 0; i < sizeof(post) / sizeof(*post); ++i) {
        snprintf(tmp, sizeof(tmp), "%s%s", name, post[i]);
        ret = SDL_GL_GetProcAddress(tmp);
        if (ret) {
            return ret;
        }
    }

    sysLogPrintf(LOG_ERROR, "GL: could not find function: %s", name);

    return NULL;
}

#ifndef __ANDROID__ // if PC
// VR try to fix camspy bug on AMD graphic card
typedef void (APIENTRY *PFNGLTEXTUREBARRIERPROC)(void);
static PFNGLTEXTUREBARRIERPROC glTextureBarrier_ptr = nullptr;
static bool gl_has_texture_barrier = false;

static void gfx_opengl_init_texture_barrier(void) {
    glTextureBarrier_ptr = (PFNGLTEXTUREBARRIERPROC)SDL_GL_GetProcAddress("glTextureBarrier");
    if (!glTextureBarrier_ptr) {
        glTextureBarrier_ptr = (PFNGLTEXTUREBARRIERPROC)SDL_GL_GetProcAddress("glTextureBarrierNV");
    }
    gl_has_texture_barrier = (glTextureBarrier_ptr != nullptr);

    sysLogPrintf(LOG_NOTE, "GL texture_barrier: %s", gl_has_texture_barrier ? "yes" : "no");
}

static void gl_texture_barrier_safe(void) {
    if (gl_has_texture_barrier && glTextureBarrier_ptr) {
        glTextureBarrier_ptr();
    }

    else {
        glFinish();
    }
}
#endif
//---

static void gfx_opengl_init_extensions(void) {
    // patch some extension values and pointers
    if (!GLAD_GL_ARB_depth_clamp) {
        if (GLAD_GL_EXT_depth_clamp || GLAD_GL_NV_depth_clamp) {
            GLAD_GL_ARB_depth_clamp = 1;
        }
        else if (!gl_es && GLVersion.major >= 3) {
            // GL3.2+ should have depth_clamp as part of the spec, but some devices don't report it for some reason
            GLAD_GL_ARB_depth_clamp = (GLVersion.major > 3 || (GLVersion.major == 3 && GLVersion.minor >= 2));
        }
    }

    if (!GLAD_GL_ARB_texture_mirror_clamp_to_edge) {
        GLAD_GL_ARB_texture_mirror_clamp_to_edge = GLAD_GL_EXT_texture_mirror_clamp_to_edge;
    }

    if (GLVersion.major < 3 && !GLAD_GL_ARB_framebuffer_object) {
        if (GLAD_GL_EXT_framebuffer_object && GLAD_GL_EXT_framebuffer_blit && GLAD_GL_EXT_framebuffer_multisample) {
            // because of the way glad works we'll have to copy the pointers over
            glad_glGenFramebuffers = glad_glGenFramebuffersEXT;
            glad_glGenRenderbuffers = glad_glGenRenderbuffersEXT;
            glad_glDeleteFramebuffers = glad_glDeleteFramebuffersEXT;
            glad_glDeleteRenderbuffers = glad_glDeleteRenderbuffersEXT;
            glad_glBindFramebuffer = glad_glBindFramebufferEXT;
            glad_glBindRenderbuffer = glad_glBindRenderbufferEXT;
            glad_glFramebufferRenderbuffer = glad_glFramebufferRenderbufferEXT;
            glad_glFramebufferTexture2D = glad_glFramebufferTexture2DEXT;
            glad_glRenderbufferStorage = glad_glRenderbufferStorageEXT;
            glad_glRenderbufferStorageMultisample = glad_glRenderbufferStorageMultisampleEXT;
            glad_glBlitFramebuffer = glad_glBlitFramebufferEXT;
        }
    }


}


static void gfx_opengl_init(void) {
    if (!gladLoadGLLoader(gl_load_proc) || glGetString == NULL || glEnable == NULL) {
        sysFatalError("Could not load OpenGL.\nReported SDL error: %s", SDL_GetError());
    }

    // check if we're using ES or core, which have more limited feature sets
    int val = 0;
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &val);
    gl_core_profile = (val == SDL_GL_CONTEXT_PROFILE_CORE);
    gl_es = (val == SDL_GL_CONTEXT_PROFILE_ES);

    gfx_opengl_init_extensions();

#ifndef ANDROID // if PC
    gfx_opengl_init_texture_barrier(); // VR AMD fix
#endif

    if (sysArgCheck("--debug-gl")) {
        gfx_opengl_enable_debug();
        // dump version information as early as possible
        gfx_opengl_log_info();
    }

    if (GLVersion.major < 2 || (GLVersion.major == 2 && GLVersion.minor < 1)) {
        const char* ver = (const char*)glGetString(GL_VERSION);
        sysFatalError("Could not load OpenGL 2.1.\nReported version: %d.%d (%s)",
                      GLVersion.major, GLVersion.minor, ver ? ver : "unknown");
    }

    if (!gfx_opengl_supports_shaders()) {
        sysLogPrintf(LOG_WARNING, "GL: GLSL 1.30 may be unsupported");
        // maybe replace this with sysFatalError, although the GLSL compiler will cause that later anyway
    }

    if (!gfx_framebuffers_enabled) {
        sysLogPrintf(LOG_WARNING, "GL: framebuffer effects disabled by user");
    }
    else if (!gfx_opengl_supports_framebuffers()) {
        sysLogPrintf(LOG_WARNING, "GL: GL_ARB_framebuffer_object unsupported, framebuffer effects disabled");
        gfx_framebuffers_enabled = false;
    }

    if ((GLVersion.major < 4 || GLVersion.minor < 4) && !GLAD_GL_ARB_texture_mirror_clamp_to_edge) {
        // GL_MIRROR_CLAMP_TO_EDGE unsupported
        gl_mirror_clamp = GL_MIRRORED_REPEAT;
    }

    // determine GLSL version
    if (gl_es) {
        // ES has its own numbering scheme, but it should support 300 even on 3.1 and 3.2
        gl_glsl_version = 300;
        snprintf(gl_glsl_version_str, sizeof(gl_glsl_version_str), "%d es", gl_glsl_version);
    }
    else if (!gl_core_profile) {
        // in compatibility profile we can just request the lowest possible version
#ifdef ANDROID // VR
        gl_glsl_version = 130;
#else
        gl_glsl_version = 330;
#endif
        snprintf(gl_glsl_version_str, sizeof(gl_glsl_version_str), "%d", gl_glsl_version);
    }
    else {
        // otherwise we have to pick a specific version
        if (GLVersion.major == 3 && GLVersion.minor == 2) {
            // 3.2 core is the earliest core version and it follows the old numbering scheme
            gl_glsl_version = 150;
        }
        else {
            // 3.3 and above follow the new numbering scheme
            gl_glsl_version = GLVersion.major * 100 + GLVersion.minor * 10;
        }
        snprintf(gl_glsl_version_str, sizeof(gl_glsl_version_str), "%d core", gl_glsl_version);
    }

    // VR
    gfx_opengl_init_multiview();

    glGenBuffers(1, &opengl_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, opengl_vbo);

    if (gl_core_profile || gl_es) {
        // warn the user that odd behavior can occur
        sysLogPrintf(LOG_WARNING, "GL: using core profile or ES, watch out for errors");
        // core/ES will fail badly if we don't use a VAO for our VBO
        glGenVertexArrays(1, &opengl_vao);
        glBindVertexArray(opengl_vao);
    }

    if (GLAD_GL_ARB_depth_clamp) {
        glEnable(GL_DEPTH_CLAMP);
    }
    glDepthFunc(GL_LEQUAL);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

#ifndef ANDROID // if PC
    mirror_apply_size(mirror_enabled); // VR
#endif
    framebuffers.resize(1); // for the default screen buffer

}


static void gfx_opengl_on_resize(void) {
}

static void gfx_opengl_start_frame(void) {
    frame_count++;

    gVrMenuLWasClearedThisFrame = false;
    gVrMenuLCaptureDepth = 0;

    gVrMenuHWasClearedThisFrame = false;
    gVrMenuHCaptureDepth = 0;

}

static void gfx_opengl_end_frame(void) {
    glFlush();

}

static void gfx_opengl_finish_render(void) {

}

static int gfx_opengl_create_framebuffer() {
    size_t i = framebuffers.size();
    framebuffers.resize(i + 1);

    GLuint clrbuf;
    glGenTextures(1, &clrbuf);
    glBindTexture(GL_TEXTURE_2D, clrbuf);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);


    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    framebuffers[i].clrbuf = clrbuf;

    if (!gfx_framebuffers_enabled) {
        return i;
    }

    GLuint clrbuf_msaa;
    glGenRenderbuffers(1, &clrbuf_msaa);
    framebuffers[i].clrbuf_msaa = clrbuf_msaa;

    GLuint rbo;
    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, 1, 1);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    framebuffers[i].rbo = rbo;

    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    framebuffers[i].fbo = fbo;

    return i;
}

static void gfx_opengl_update_framebuffer_parameters(int fb_id, uint32_t width, uint32_t height, uint32_t msaa_level,
                                                     bool opengl_invert_y, bool render_target, bool has_depth_buffer,
                                                     bool can_extract_depth) {

    if (fb_id < 0 || fb_id >= (int)framebuffers.size()) {
        return;
    }


    Framebuffer& fb = framebuffers[fb_id];
    // Multiview FBO 0 is managed by vr_openxr.cpp, just update the size
    if (fb_id == 0 && !framebuffers.empty() && framebuffers[0].is_multiview) {
        framebuffers[0].width = width;
        framebuffers[0].height = height;
        return;
    }

    width = max(width, 1U);
    height = max(height, 1U);

    if (gfx_framebuffers_enabled) {
        glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);

        if (fb_id != 0) {
            if (fb.width != width || fb.height != height || fb.msaa_level != msaa_level) {
                if (msaa_level <= 1) {
                    glBindTexture(GL_TEXTURE_2D, fb.clrbuf);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

                    glBindTexture(GL_TEXTURE_2D, 0);
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb.clrbuf, 0);
                }
                else {
                    glBindRenderbuffer(GL_RENDERBUFFER, fb.clrbuf_msaa);
                    glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaa_level, GL_RGBA8, width, height);
                    glBindRenderbuffer(GL_RENDERBUFFER, 0);
                    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, fb.clrbuf_msaa);
                }
            }

            if (has_depth_buffer &&
                (fb.width != width || fb.height != height || fb.msaa_level != msaa_level || !fb.has_depth_buffer)) {
                glBindRenderbuffer(GL_RENDERBUFFER, fb.rbo);
                if (msaa_level <= 1) {
                    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
                }
                else {
                    glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaa_level, GL_DEPTH24_STENCIL8, width, height);
                }
                glBindRenderbuffer(GL_RENDERBUFFER, 0);
            }

            if (!fb.has_depth_buffer && has_depth_buffer) {
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fb.rbo);
            }
            else if (fb.has_depth_buffer && !has_depth_buffer) {
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
            }
        }
    }
    else {
        has_depth_buffer = false;
    }

    fb.width = width;
    fb.height = height;
    fb.has_depth_buffer = has_depth_buffer;
    fb.msaa_level = msaa_level;
    fb.invert_y = opengl_invert_y;
}

bool gfx_opengl_start_draw_to_framebuffer(int fbid, float noisescale) {
    if (gfx_framebuffers_enabled && fbid < (int)framebuffers.size()) {
        Framebuffer& fb = framebuffers[fbid];

        if (noisescale != 0.0f) {
            current_noise_scale = 1.0f / noisescale;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);
        current_framebuffer = fbid;

        //        LOGI("GL start_draw_to_fb: fbid=%d w=%u h=%u is_mv=%d",
        //                     fbid, fb.width, fb.height, fb.is_multiview);

        return true;
    }
    return false;
}



// ============================================================================
// VR Layers HUD/MENU LEFT Hand
// ============================================================================

static int gVrMenuFb = -1;
static int gVrMenuFbPrevious = -1;
extern "C" int  vr_get_internal_render_width(void);
extern "C" int  vr_get_internal_render_height(void);
static GLint gVrMenuPrevViewport[4] = {0, 0, 0, 0 };
static GLint gVrMenuCaptureViewport[4] = {0, 0, 0, 0 };
extern void gfx_flush(void);

static void gfx_opengl_vr_menu_fb_init(void) {
    int w = vr_get_internal_render_width();
    int h = vr_get_internal_render_height();

    if (gVrMenuFb < 0) {
        gVrMenuFb = gfx_opengl_create_framebuffer();
    }

    // Recreate/resize if the VR resolution has changed (e.g. on the first frame)
    gfx_opengl_update_framebuffer_parameters(
            gVrMenuFb,
            (uint32_t)w, (uint32_t)h,
            /*msaa_level=*/1,
            /*opengl_invert_y=*/false,
            /*render_target=*/true,
            /*has_depth_buffer=*/true,
            /*can_extract_depth=*/true
    );
}

bool gfx_vr_menu_L_dirty_and_clear(void) {
    bool v = hud_L_was_drawn;
    hud_L_was_drawn = false;
    return v;
}

void gfx_vr_hud_capture_begin_L(void)
{
    gfx_flush();
    gfx_opengl_vr_menu_fb_init();

    if (gVrMenuLCaptureDepth++ > 0) {
        return;
    }
    gfx_opengl_menu_capture_push();

    glGetIntegerv(GL_VIEWPORT, gVrMenuPrevViewport);
    gVrMenuFbPrevious = (int)current_framebuffer;

    gfx_opengl_start_draw_to_framebuffer(gVrMenuFb, 0.0f);

    int w = vr_get_internal_render_width();
    int h = vr_get_internal_render_height();

    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);

    if (!gVrMenuLWasClearedThisFrame) {
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        gVrMenuLWasClearedThisFrame = true;
    }

    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, w, h);

    gVrMenuCaptureViewport[0] = 0;
    gVrMenuCaptureViewport[1] = 0;
    gVrMenuCaptureViewport[2] = w;
    gVrMenuCaptureViewport[3] = h;
}


void gfx_vr_hud_capture_end_L(void)
{

    gfx_flush();

    if (gVrMenuLCaptureDepth <= 0) {
        gVrMenuLCaptureDepth = 0;
        return;
    }

    if (--gVrMenuLCaptureDepth > 0) {
        return;
    }

    gfx_opengl_menu_capture_pop();
    hud_L_was_drawn = true;


    if (gVrMenuFbPrevious >= 0) {
        gfx_opengl_start_draw_to_framebuffer(gVrMenuFbPrevious, 0.0f);
    }

    gVrMenuFbPrevious = -1;

    glViewport(
            gVrMenuPrevViewport[0],
            gVrMenuPrevViewport[1],
            gVrMenuPrevViewport[2],
            gVrMenuPrevViewport[3]);

    glScissor(
            gVrMenuPrevViewport[0],
            gVrMenuPrevViewport[1],
            gVrMenuPrevViewport[2],
            gVrMenuPrevViewport[3]);
}

GLuint gfx_opengl_get_vr_menu_texture(void) {
    if (gVrMenuFb < 0) return 0;
    return framebuffers[gVrMenuFb].clrbuf;
}
//---


// ============================================================================
// VR - Second menu/HUD quad: RIGHT hand
// ============================================================================
static int  gVrMenuRFb          = -1;
static int  gVrMenuRFbPrevious  = -1;
static GLint gVrMenuRPrevViewport[4]    = {0, 0, 0, 0};
static GLint gVrMenuRCaptureViewport[4] = {0, 0, 0, 0};


static void gfx_opengl_vr_menu_R_fb_init(void)
{
    int w = vr_get_internal_render_width();
    int h = vr_get_internal_render_height();
    if (gVrMenuRFb < 0)
        gVrMenuRFb = gfx_opengl_create_framebuffer();
    gfx_opengl_update_framebuffer_parameters(
            gVrMenuRFb, (uint32_t)w, (uint32_t)h,
            /*msaalevel*/1, /*inverty*/false,
            /*rendertarget*/true, /*hasdepth*/true, /*canextractdepth*/true);
}

void gfx_vr_hud_capture_begin_R(void)
{
    gfx_flush();
    gfx_opengl_vr_menu_R_fb_init();
    gfx_opengl_menu_capture_push();

    glGetIntegerv(GL_VIEWPORT, gVrMenuRPrevViewport);
    gVrMenuRFbPrevious = (int)current_framebuffer;
    gfx_opengl_start_draw_to_framebuffer(gVrMenuRFb, 0.0f);
    int w = vr_get_internal_render_width();
    int h = vr_get_internal_render_height();
    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, w, h);
    gVrMenuRCaptureViewport[0] = 0;
    gVrMenuRCaptureViewport[1] = 0;
    gVrMenuRCaptureViewport[2] = w;
    gVrMenuRCaptureViewport[3] = h;
}

void gfx_vr_hud_capture_end_R(void)
{
    hud_R_was_drawn = true;
    gfx_flush();
    gfx_opengl_menu_capture_pop();

    if (gVrMenuRFbPrevious >= 0)
        gfx_opengl_start_draw_to_framebuffer(gVrMenuRFbPrevious, 0.0f);
    gVrMenuRFbPrevious = -1;
    glViewport(gVrMenuRPrevViewport[0], gVrMenuRPrevViewport[1],
               gVrMenuRPrevViewport[2], gVrMenuRPrevViewport[3]);
    glScissor (gVrMenuRPrevViewport[0], gVrMenuRPrevViewport[1],
               gVrMenuRPrevViewport[2], gVrMenuRPrevViewport[3]);


}

GLuint gfx_opengl_get_vr_menu_texture_R(void)
{
    if (gVrMenuRFb < 0) return 0;
    return framebuffers[gVrMenuRFb].clrbuf;
}

bool gfx_vr_menu_R_dirty_and_clear(void) {
    bool v = hud_R_was_drawn;
    hud_R_was_drawn = false;
    return v;
}

// ============================================================================
// --- VR - Third menu/HUD quad HEAD-LOCKED
// ============================================================================
static int gVrMenuHFb = -1;
static int gVrMenuHFbPrevious = -1;
static GLint gVrMenuHPrevViewport[4] = {0,0,0,0};
static GLint gVrMenuHCaptureViewport[4] = {0,0,0,0};


static void gfx_opengl_vr_menu_H_fb_init(void) {
    int w = vr_get_internal_render_width();
    int h = vr_get_internal_render_height();
    if (gVrMenuHFb <= 0)
        gVrMenuHFb = gfx_opengl_create_framebuffer();
    gfx_opengl_update_framebuffer_parameters(
            gVrMenuHFb, (uint32_t)w, (uint32_t)h,
            /*msaalevel=*/1, /*inverty=*/false,
            /*rendertarget=*/true, /*hasdepth=*/true, /*canextractdepth=*/true);
}

void gfx_vr_hud_capture_begin_H(void)
{
    gfx_flush();
    gfx_opengl_vr_menu_H_fb_init();

    if (gVrMenuHCaptureDepth++ > 0) {
        return;
    }
    gfx_opengl_menu_capture_push();

    glGetIntegerv(GL_VIEWPORT, gVrMenuHPrevViewport);
    gVrMenuHFbPrevious = (int)current_framebuffer;

    gfx_opengl_start_draw_to_framebuffer(gVrMenuHFb, 0.0f);

    int w = vr_get_internal_render_width();
    int h = vr_get_internal_render_height();

    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);

    if (!gVrMenuHWasClearedThisFrame) {
        glClearColor(0.f, 0.f, 0.f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT);
        gVrMenuHWasClearedThisFrame = true;
    }

    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, w, h);

    gVrMenuHCaptureViewport[0] = 0;
    gVrMenuHCaptureViewport[1] = 0;
    gVrMenuHCaptureViewport[2] = w;
    gVrMenuHCaptureViewport[3] = h;
}


void gfx_vr_hud_capture_end_H(void)
{
    gfx_flush();

    if (gVrMenuHCaptureDepth <= 0) {
        gVrMenuHCaptureDepth = 0;
        return;
    }

    if (--gVrMenuHCaptureDepth > 0) {
        return;
    }

    gfx_opengl_menu_capture_pop();
    hud_H_was_drawn = true;

    if (gVrMenuHFbPrevious >= 0) {
        gfx_opengl_start_draw_to_framebuffer(gVrMenuHFbPrevious, 0.0f);
    }

    gVrMenuHFbPrevious = -1;

    glViewport(
            gVrMenuHPrevViewport[0],
            gVrMenuHPrevViewport[1],
            gVrMenuHPrevViewport[2],
            gVrMenuHPrevViewport[3]);

    glScissor(
            gVrMenuHPrevViewport[0],
            gVrMenuHPrevViewport[1],
            gVrMenuHPrevViewport[2],
            gVrMenuHPrevViewport[3]);
}

bool gfx_vr_menu_H_dirty_and_clear(void) {
    bool v = hud_H_was_drawn;
    hud_H_was_drawn = false;
    return v;
}

GLuint gfx_opengl_get_vr_menu_texture_H(void) {
    if (gVrMenuHFb <= 0) return 0;
    return framebuffers[gVrMenuHFb].clrbuf;
}
//---






void gfx_opengl_clear_framebuffer(bool clear_color, bool clear_depth) {
    glDisable(GL_SCISSOR_TEST);

    GLbitfield mask = 0;
    if (clear_color) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        mask |= GL_COLOR_BUFFER_BIT;
    }
    if (clear_depth) {
        glDepthMask(GL_TRUE);
        mask |= GL_DEPTH_BUFFER_BIT;
    }
    glClear(mask);
    if (clear_depth) {
        glDepthMask(current_depth_mask ? GL_TRUE : GL_FALSE);
    }

    glEnable(GL_SCISSOR_TEST);
}

void gfx_opengl_resolve_msaa_color_buffer(int fb_id_target, int fb_id_source) {
    if (!gfx_framebuffers_enabled) {
        return;
    }

    Framebuffer& fb_dst = framebuffers[fb_id_target];
    Framebuffer& fb_src = framebuffers[fb_id_source];
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb_dst.fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fb_src.fbo);
    glBlitFramebuffer(0, 0, fb_src.width, fb_src.height, 0, 0, fb_dst.width, fb_dst.height, GL_COLOR_BUFFER_BIT,
                      GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, current_framebuffer);
    glEnable(GL_SCISSOR_TEST);
}


void* gfx_opengl_get_framebuffer_texture_id(int fbid) {
    const Framebuffer& fb = framebuffers[fbid];

    void* tex = nullptr;
    if (fb.is_multiview) {
        tex = (void*)(uintptr_t)vr_get_current_multiview_swapchain_tex();
    }
    else {
        tex = (void*)(uintptr_t)fb.clrbuf;
    }

    return tex;
}



void gfx_opengl_select_texture_fb(int fb_id) {
    //    LOGI( "GL select_texture_fb: fbid=%d clrbuf=%u is_mv=%d",
    //                 fb_id, framebuffers[fb_id].clrbuf, framebuffers[fb_id].is_multiview);
    // glDisable(GL_DEPTH_TEST);
    glActiveTexture(GL_TEXTURE0 + 0);
    glBindTexture(GL_TEXTURE_2D, framebuffers[fb_id].clrbuf);

    current_textures_linear_filter[0] = true;
}



void gfx_opengl_copy_framebuffer(int fb_dst, int fb_src, int left, int top, bool flip_y, bool use_back) {
    if (!gfx_framebuffers_enabled || fb_dst >= (int)framebuffers.size() || fb_src >= (int)framebuffers.size()) {
        return;
    }

    Framebuffer& src = framebuffers[fb_src];
    Framebuffer& dst = framebuffers[fb_dst];

//    LOGI("Framebuffer src %d", fb_src);
//    LOGI("Framebuffer dst %d", fb_dst);

    int srcX0, srcY0, srcX1, srcY1;
    int dstX0, dstY0, dstX1, dstY1;

    dstX0 = dstY0 = 0;
    dstX1 = dst.width;
    dstY1 = dst.height;

    if (left >= 0 && top >= 0) {
        // unscaled rectangle copy
        srcX0 = left;
        srcY0 = top;
        srcX1 = left + dst.width;
        srcY1 = top + dst.height;
    }
    else {
        // scaled full copy
        srcX0 = 0;
        srcY0 = 0;
        srcX1 = src.width;
        srcY1 = src.height;
    }



    // For PC Oculus Meta runtime
    // Special menu case (fb_dst == 25): direct blit from layer 0 of the swapchain
    if ( is_meta_runtime && fb_dst == 25) {
        copy_fbo_menu = true;
        GLuint texArray = vr_get_current_multiview_swapchain_tex();

        // Temporary FBO pointing to layer 0 of the swapchain
        GLuint tmpFbo = 0;
        glGenFramebuffers(1, &tmpFbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, tmpFbo);
        glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texArray, 0, 0);

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst.fbo);
        glDisable(GL_SCISSOR_TEST);

        // Blit with scaling (swapchain 1832x1920 → FBO 25 698x524)
        glBlitFramebuffer(
                0, framebuffers[0].height, framebuffers[0].width, 0,  // inverted src Y
                0, 0, dst.width, dst.height,
                GL_COLOR_BUFFER_BIT, GL_LINEAR
        );

        glEnable(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &tmpFbo);
        return;
    }


    // Fix for background menu, camspy, cloaking effect on enemies or bots (G5 building / challenges)
    if (fb_dst != 4 && framebuffers[0].is_multiview) { // fb_dst 4 = cutescene

        mv_blit_init();

        // Minimal GL state backup
        GLint prevFbo = 0, prevProg = 0;
        GLint viewport[4];
        GLboolean prevDepthTest = GL_FALSE;
        GLboolean prevDepthMask = GL_FALSE;
        GLboolean prevBlend = GL_FALSE;

        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
        glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetBooleanv(GL_DEPTH_TEST, &prevDepthTest);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
        glGetBooleanv(GL_BLEND, &prevBlend);


        // Calculation of normalized UVs corresponding to the source rectangle
        float u0, v0, u1, v1;

        if (left >= 0 && top >= 0) {
            // Sub-rectangle (invisible cloak, FB 5-19 effects)
            u0 = (float)srcX0 / (float)src.width;
            v0 = (float)srcY0 / (float)src.height;
            u1 = (float)srcX1 / (float)src.width;
            v1 = (float)srcY1 / (float)src.height;
        }
        else {
            // Full-screen copy (camera, menus, etc.) → full UV
            u0 = 0.0f;
            v0 = 0.0f;
            u1 = 1.0f;
            v1 = 1.0f;
        }


        // We leave the flip_y handling to the shader (as before), so no swapping of v0/v1 here
        // The shader will use uRect and uFlipY together.

        glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
        glViewport(0, 0, dst.width, dst.height);

        glDisable(GL_SCISSOR_TEST);

        // IMPORTANT: copy the color without taking the destination FBO depth into account
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);

        glUseProgram(mv_blit_prog);

        GLuint texArray = vr_get_current_multiview_swapchain_tex();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, texArray);


#ifndef __ANDROID__ // if PC
        // Fix AMD
        gfx_flush();
        gl_texture_barrier_safe(); // force visibility of the previous write
#endif

        glUniform1i(mv_blit_uTexLoc, 0);
        glUniform1i(mv_blit_uFlipYLoc, flip_y ? 1 : 0);

        // NEW: source rectangle in UV
        glUniform4f(mv_blit_uRectLoc, u0, v0, u1, v1);

        glBindVertexArray(opengl_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // Restore GL state
        glBindVertexArray(0);
        glUseProgram(prevProg);
        glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

        if (prevDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        glDepthMask(prevDepthMask);
        if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        glEnable(GL_SCISSOR_TEST);


        return;
    }

    // Unchanged non-multiview path (classic glBlitFramebuffer)
    glDisable(GL_SCISSOR_TEST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, src.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst.fbo);

    if (flip_y) {
        std::swap(dstY0, dstY1);
    }

    if (fb_src == 0) {
        if (framebuffers[0].is_multiview) {
            glReadBuffer(GL_COLOR_ATTACHMENT0);
        }
        else {
            glReadBuffer(use_back && !gl_es ? GL_BACK : GL_FRONT);
        }
    }
    else {
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    }

    glBlitFramebuffer(
            srcX0, srcY0, srcX1, srcY1,
            dstX0, dstY0, dstX1, dstY1,
            GL_COLOR_BUFFER_BIT, GL_NEAREST
    );

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[current_framebuffer].fbo);
    glReadBuffer(GL_BACK);
    glEnable(GL_SCISSOR_TEST);
}




void gfx_opengl_set_texture_filter(FilteringMode mode) {
    current_filter_mode = mode;
}

FilteringMode gfx_opengl_get_texture_filter(void) {
    return current_filter_mode;
}




static void gfx_opengl_init_mirror_shader() {
#ifndef ANDROID // if PC
    if (s_mirror_prog) return;

    auto compile = [](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        return s;
    };

    GLuint vs = compile(GL_VERTEX_SHADER,   mv_blit_vs_src);
    GLuint fs = compile(GL_FRAGMENT_SHADER, mv_blit_fs_src);
    s_mirror_prog = glCreateProgram();
    glAttachShader(s_mirror_prog, vs);
    glAttachShader(s_mirror_prog, fs);
    glLinkProgram(s_mirror_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    s_mirror_uloc_tex   = glGetUniformLocation(s_mirror_prog, "uTex");
    s_mirror_uloc_layer = glGetUniformLocation(s_mirror_prog, "uLayer");
    s_mirror_uloc_rect  = glGetUniformLocation(s_mirror_prog, "uRect");
    s_mirror_uloc_sbs = glGetUniformLocation(s_mirror_prog, "uSbs");

    // Empty VAO required for the fullscreen triangle trick
    glGenVertexArrays(1, &s_mirror_vao);
#endif
}


static void gfx_opengl_mirror_to_desktop(
        uint32_t src_w, uint32_t src_h,   // VR resolution
        uint32_t dst_w, uint32_t dst_h)   // real size of mirror_wnd
{
#ifndef ANDROID // if PC

    if (!gfx_sdl_is_mirror_enabled()) return;

    GLuint eye_array_tex = vr_get_current_multiview_swapchain_tex();
    if (!eye_array_tex) return;

    extern SDL_Window*   mirror_wnd;
    extern SDL_GLContext mirror_ctx;
    extern SDL_Window*   wnd;
    extern SDL_GLContext ctx;
    if (!mirror_wnd || !mirror_ctx) return;

    gfx_opengl_init_mirror_shader();

    // Letterbox calculation: rendering area in UV coordinates [0..1]
    float src_ratio = (float)src_w / (float)src_h;
    float dst_ratio = (float)dst_w / (float)dst_h;
    float rect_x = 0.0f, rect_y = 0.0f, rect_w = 1.0f, rect_h = 1.0f;


    // ── Switch to mirror_wnd ──
    SDL_GL_MakeCurrent(mirror_wnd, mirror_ctx);

    // Disable auto sRGB conversion on the desktop backbuffer
    glDisable(GL_FRAMEBUFFER_SRGB);

    glViewport(0, 0, (GLsizei)dst_w, (GLsizei)dst_h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glUseProgram(s_mirror_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, eye_array_tex);
    glUniform1i(s_mirror_uloc_tex,   0);

    bool sbs = gfx_sdl_is_mirror_sbs();
    glUniform1i(s_mirror_uloc_sbs, gfx_sdl_is_mirror_sbs() ? 1 : 0);

    if (sbs) {
        glUniform4f(s_mirror_uloc_rect, 0.0f, 0.0f, 1.0f, 1.0f);
    } else {
        glUniform4f(s_mirror_uloc_rect, rect_x, rect_y, rect_x + rect_w, rect_y + rect_h);
    }
    glUniform1i(s_mirror_uloc_layer, gfx_sdl_get_mirror_eye());

    glBindVertexArray(s_mirror_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);  // fullscreen triangle

    // --- NEW BLOCK 2: HUD DISPLAY ---
    // ── Switch to mirror_wnd ──
    SDL_GL_MakeCurrent(mirror_wnd, mirror_ctx);

    glDisable(GL_FRAMEBUFFER_SRGB);
    glViewport(0, 0, (GLsizei)dst_w, (GLsizei)dst_h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glUseProgram(s_mirror_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, eye_array_tex);
    glUniform1i(s_mirror_uloc_tex, 0);
    // ... (uniforms sbs/layer/rect unchanged) ...

    glBindVertexArray(s_mirror_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3); // fullscreen triangle

// --- XR Layers: quad menu panel, same as in the headset ---
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    if (sbs) {
        // Left half = left eye, right half = right eye
        int halfW = dst_w / 2;

        VrMirrorMenuEntry entriesL[3];
        int nL = vrGetMenuMirrorMVPList(0, entriesL);
        for (int i = 0; i < nL; i++) {
            GLuint tex = 0;
            switch (entriesL[i].source) {
                case VR_MIRROR_MENU_L: tex = gfx_opengl_get_vr_menu_texture();   break;
                case VR_MIRROR_MENU_R: tex = gfx_opengl_get_vr_menu_texture_R(); break;
                case VR_MIRROR_MENU_H: tex = gfx_opengl_get_vr_menu_texture_H(); break;
            }
            gfx_opengl_draw_mirror_menu_overlay(tex, entriesL[i].mvp, 0, 0, halfW, dst_h);
        }

        VrMirrorMenuEntry entriesR[3];
        int nR = vrGetMenuMirrorMVPList(1, entriesR);
        for (int i = 0; i < nR; i++) {
            GLuint tex = 0;
            switch (entriesR[i].source) {
                case VR_MIRROR_MENU_L: tex = gfx_opengl_get_vr_menu_texture();   break;
                case VR_MIRROR_MENU_R: tex = gfx_opengl_get_vr_menu_texture_R(); break;
                case VR_MIRROR_MENU_H: tex = gfx_opengl_get_vr_menu_texture_H(); break;
            }
            gfx_opengl_draw_mirror_menu_overlay(tex, entriesR[i].mvp, halfW, 0, dst_w - halfW, dst_h);
        }

        // Restore the full-screen viewport for the rest
        glViewport(0, 0, dst_w, dst_h);
    } else {
        int mirrorEye = gfx_sdl_get_mirror_eye();
        VrMirrorMenuEntry entries[3];
        int n = vrGetMenuMirrorMVPList(mirrorEye, entries);
        for (int i = 0; i < n; i++) {
            GLuint tex = 0;
            switch (entries[i].source) {
                case VR_MIRROR_MENU_L: tex = gfx_opengl_get_vr_menu_texture();   break;
                case VR_MIRROR_MENU_R: tex = gfx_opengl_get_vr_menu_texture_R(); break;
                case VR_MIRROR_MENU_H: tex = gfx_opengl_get_vr_menu_texture_H(); break;
            }
            gfx_opengl_draw_mirror_menu_overlay(tex, entries[i].mvp, 0, 0, dst_w, dst_h);
        }
    }

// Cleanup
glBindVertexArray(0);
glBindTexture(GL_TEXTURE_2D, 0);
glUseProgram(0);
glDisable(GL_BLEND);
glFlush();

SDL_GL_MakeCurrent(wnd, ctx);
#endif
}


struct GfxRenderingAPI gfx_opengl_api = {
        gfx_opengl_get_name,
        gfx_opengl_get_max_texture_size,
        gfx_opengl_get_clip_parameters,
        gfx_opengl_unload_shader,
        gfx_opengl_load_shader,
        gfx_opengl_create_and_load_new_shader,
        gfx_opengl_lookup_shader,
        gfx_opengl_shader_get_info,
        gfx_opengl_clear_shaders,
        gfx_opengl_new_texture,
        gfx_opengl_select_texture,
        gfx_opengl_upload_texture,
        gfx_opengl_set_sampler_parameters,
        gfx_opengl_set_depth_mode,
        gfx_opengl_set_depth_range,
        gfx_opengl_set_viewport,
        gfx_opengl_set_scissor,
        gfx_opengl_set_use_alpha,
        gfx_opengl_draw_triangles,
        gfx_opengl_init,
        gfx_opengl_on_resize,
        gfx_opengl_start_frame,
        gfx_opengl_end_frame,
        gfx_opengl_finish_render,
        gfx_opengl_create_framebuffer,
        gfx_opengl_update_framebuffer_parameters,
        gfx_opengl_start_draw_to_framebuffer,
        gfx_opengl_copy_framebuffer,
        gfx_opengl_clear_framebuffer,
        gfx_opengl_resolve_msaa_color_buffer,
        gfx_opengl_get_framebuffer_texture_id,
        gfx_opengl_select_texture_fb,
        gfx_opengl_delete_texture,
        gfx_opengl_set_texture_filter,
        gfx_opengl_get_texture_filter,
        gfx_opengl_set_eye_offsets,
        gfx_opengl_is_multiview,
        gfx_opengl_mirror_to_desktop
};
