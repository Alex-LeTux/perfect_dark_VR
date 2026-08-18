// ============================================================================
// VR Hub - Simple pause-menu 3D environment (flat grid floor + gradient sky)
// Self-contained: its own shaders, its own VAOs, no dependency on the
// game's fast3d/display-list pipeline.
// ============================================================================

#include "vr_hub.h"
#include <cstring>
#include <cstdio>

#ifdef ANDROID
#include <GLES3/gl3.h>
#include <GLES3/gl32.h>

#include <SDL.h>
#include "external/stb_image.h"

#else
#include "../fast3d/glad/glad.h"
#endif

#ifdef ANDROID
#include <android/log.h>
#define HUB_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "PD-VR-Hub", __VA_ARGS__)
#else
#define HUB_LOGE(...) fprintf(stderr, __VA_ARGS__)
#endif

// ----------------------------------------------------------------------------
// Tunable look of the hub
// ----------------------------------------------------------------------------
static const float HUB_FLOOR_HALF_SIZE   = 100.0f;   // metres, floor is 200x200m
static const float HUB_GRID_CELL_METERS  = 1.0f;     // grid line every 1m
static const float HUB_FOG_START         = 15.0f;    // metres
static const float HUB_FOG_END           = 60.0f;    // metres

static const float HUB_SKY_COLOR_HORIZON[3]  = { 0.10f, 0.14f, 0.22f };
static const float HUB_FLOOR_BASE_COLOR[3]   = { 0.04f, 0.05f, 0.07f };
static const float HUB_FLOOR_LINE_COLOR[3]   = { 0.25f, 0.55f, 1.00f };

static const float HUB_LINE_FADE_START = 1.0f;
static const float HUB_LINE_FADE_END   = 25.0f;
static GLint sFloorLineFadeStartLoc = -1, sFloorLineFadeEndLoc = -1;

static GLuint sDecalTex = 0;
static GLint sFloorDecalLoc = -1, sFloorDecalHalfSizeLoc = -1, sFloorUseDecalLoc = -1;
static const float HUB_DECAL_HALF_WIDTH = 0.5f;
static const float HUB_DECAL_IMAGE_ASPECT = 262.0f / 480.0f;         // Source image height/width (480x262)
static const float HUB_DECAL_HALF_DEPTH  = HUB_DECAL_HALF_WIDTH * HUB_DECAL_IMAGE_ASPECT;
extern "C" unsigned int gfx_opengl_get_logo_tex();
// ----------------------------------------------------------------------------
// Shaders
// ----------------------------------------------------------------------------
#ifdef ANDROID
static const char* kVersionLine = "#version 300 es\n";
static const char* kPrecisionLine = "precision highp float;\n";
#else
static const char* kVersionLine = "#version 330 core\n";
static const char* kPrecisionLine = "";
#endif

// Shared vertex shader: multiview, one draw call renders both eyes.
static const char* kHubVsSrc =
        "layout(num_views = 2) in;\n"
        "layout(location = 0) in vec3 aPos;\n"
        "uniform mat4 uViewProj[2];\n"
        "out vec3 vWorldPos;\n"
        "void main() {\n"
        "    vWorldPos = aPos;\n"
        "    gl_Position = uViewProj[gl_ViewID_OVR] * vec4(aPos, 1.0);\n"
        "}\n";

// Floor: procedural grid + distance fog, no texture needed.
static const char* kFloorFsSrc =
        "in vec3 vWorldPos;\n"
        "out vec4 fragColor;\n"
        "uniform vec3 uBaseColor;\n"
        "uniform vec3 uLineColor;\n"
        "uniform vec3 uFogColor;\n"
        "uniform float uCellSize;\n"
        "uniform float uFogStart;\n"
        "uniform float uFogEnd;\n"
        "uniform float uLineFadeStart;\n"
        "uniform float uLineFadeEnd;\n"
        "uniform sampler2D uDecalTex;\n"
        "uniform vec2 uDecalHalfSize;\n"
        "uniform int uUseDecal;\n"
        "void main() {\n"
        "    vec2 coord = vWorldPos.xz / uCellSize;\n"
        "    vec2 gridDeriv = fwidth(coord);\n"
        "    vec2 gridAlias = abs(fract(coord - 0.5) - 0.5) / max(gridDeriv, vec2(0.0001));\n"
        "    float line = 1.0 - clamp(min(gridAlias.x, gridAlias.y), 0.0, 1.0);\n"
        "    float dist = length(vWorldPos.xz);\n"
        "    float lineFade = 1.0 - clamp((dist - uLineFadeStart) / max(uLineFadeEnd - uLineFadeStart, 0.001), 0.0, 1.0);\n"
        "    line *= lineFade;\n"
        "    vec3 color = mix(uBaseColor, uLineColor, line);\n"
        "    if (uUseDecal == 1) {\n"
        "        vec2 decalUV = vWorldPos.xz / (2.0 * uDecalHalfSize) + 0.5;\n"
        "        if (decalUV.x >= 0.0 && decalUV.x <= 1.0 && decalUV.y >= 0.0 && decalUV.y <= 1.0) {\n"
        "            vec4 decal = texture(uDecalTex, decalUV);\n"
        "            color = mix(color, decal.rgb, decal.a);\n"
        "        }\n"
        "    }\n"
        "    float fog = clamp((dist - uFogStart) / max(uFogEnd - uFogStart, 0.001), 0.0, 1.0);\n"
        "    color = mix(color, uFogColor, fog);\n"
        "    fragColor = vec4(color, 1.0);\n"
        "}\n";

// Sky: simple vertical gradient based on direction from the origin.
static const char* kSkyFsSrc =
        "in vec2 vScreenPos;\n"
        "out vec4 fragColor;\n"
        "uniform float uTime;\n"
        "float hash11(float p) {\n"
        "  p = fract(p * 0.1031); p *= p + 33.33; p *= p + p;\n"
        "  return fract(p);\n"
        "}\n"
        "void main() {\n"
        "  float band = 1.0 - smoothstep(0.0, 0.4, abs(vScreenPos.y));\n"
        "  float sectorRand = hash11(floor((vScreenPos.x * 0.5 + 0.5) * 24.0));\n"
        "  float cyclePhase = fract(uTime * 0.05 + sectorRand * 5.0);\n"
        "  float pulse = smoothstep(0.0, 0.3, cyclePhase) * smoothstep(1.0, 0.5, cyclePhase);\n"
        "  float sectorActive = step(0.6, sectorRand);\n"
        "  vec3 glowColor = mix(vec3(0.55, 0.05, 0.35), vec3(0.85, 0.15, 0.10), sectorRand);\n"
        "  vec3 color = glowColor * band * pulse * sectorActive * 1.5;\n"
        "  fragColor = vec4(color, band * pulse * sectorActive);\n"
        "}\n";


// ----------------------------------------------------------------------------
// ANDROID Image loading
// ----------------------------------------------------------------------------
#ifdef ANDROID
static GLuint sAndroidLogoTex = 0;

GLuint android_load_logo_texture(const char* assetPath) {
    if (sAndroidLogoTex != 0) return sAndroidLogoTex;

    SDL_RWops* rw = SDL_RWFromFile(assetPath, "rb");
    if (!rw) {
        HUB_LOGE("[vr_hub] SDL_RWFromFile a échoué pour %s: %s", assetPath, SDL_GetError());
        return 0;
    }

    Sint64 size = SDL_RWsize(rw);
    if (size <= 0) {
        SDL_RWclose(rw);
        return 0;
    }

    unsigned char* buffer = (unsigned char*)malloc((size_t)size);
    SDL_RWread(rw, buffer, 1, (size_t)size);
    SDL_RWclose(rw);

    int w, h, channels;
    unsigned char* pixels = stbi_load_from_memory(buffer, (int)size, &w, &h, &channels, 4);
    free(buffer);

    if (!pixels) {
        HUB_LOGE("[vr_hub] stbi_load_from_memory a échoué pour %s", assetPath);
        return 0;
    }

    glGenTextures(1, &sAndroidLogoTex);
    glBindTexture(GL_TEXTURE_2D, sAndroidLogoTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_image_free(pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return sAndroidLogoTex;
}
#endif

// ----------------------------------------------------------------------------
// GL objects
// ----------------------------------------------------------------------------
static GLuint sFloorProg = 0, sSkyProg = 0;
static GLuint sFloorVao = 0, sFloorVbo = 0;
static GLuint sSkyVbo = 0;

static GLint sFloorVpLoc = -1, sFloorBaseLoc = -1, sFloorLineLoc = -1,
        sFloorFogColorLoc = -1, sFloorCellLoc = -1, sFloorFogStartLoc = -1, sFloorFogEndLoc = -1;

static bool sInitDone = false;

static GLuint CompileShader(GLenum type, const char* versionLine, const char* extensionLine,
                            const char* precisionLine, const char* body) {
    char buf[8192];
    snprintf(buf, sizeof(buf), "%s%s%s%s", versionLine, extensionLine, precisionLine, body);
    const char* src = buf;

    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);

    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        HUB_LOGE("[vr_hub] shader compile error: %s", log);
    }
    return sh;
}

static GLuint LinkProgram(GLuint vs, GLuint fs) {
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        fprintf(stderr, "[vr_hub] program link error: %s\n", log);
    }
    return prog;
}

static void BuildFloorMesh() {
    float s = HUB_FLOOR_HALF_SIZE;
    float verts[] = {
            -s, 0.0f, -s,
            s, 0.0f, -s,
            s, 0.0f,  s,
            -s, 0.0f, -s,
            s, 0.0f,  s,
            -s, 0.0f,  s,
    };

    glGenVertexArrays(1, &sFloorVao);
    glGenBuffers(1, &sFloorVbo);
    glBindVertexArray(sFloorVao);
    glBindBuffer(GL_ARRAY_BUFFER, sFloorVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);
}

static void BuildSkyMesh() {
    float v[] = {
            -1.0f, -1.0f,
            1.0f, -1.0f,
            1.0f,  1.0f,
            -1.0f, -1.0f,
            1.0f,  1.0f,
            -1.0f,  1.0f,
    };


    glGenBuffers(1, &sSkyVbo);
    glBindBuffer(GL_ARRAY_BUFFER, sSkyVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);
}

void vr_hub_init(void) {
    if (sInitDone) return;

    static const char* kExtensionLine = "#extension GL_OVR_multiview2 : require\n";

    GLuint vs = CompileShader(GL_VERTEX_SHADER, kVersionLine, kExtensionLine, kPrecisionLine, kHubVsSrc);

    GLuint floorFs = CompileShader(GL_FRAGMENT_SHADER, kVersionLine, "", kPrecisionLine, kFloorFsSrc);
    sFloorProg = LinkProgram(vs, floorFs);
    glDeleteShader(floorFs);

    GLuint skyFs   = CompileShader(GL_FRAGMENT_SHADER, kVersionLine, "", kPrecisionLine, kSkyFsSrc);
    sSkyProg = LinkProgram(vs, skyFs);

    glDeleteShader(skyFs);
    glDeleteShader(vs);

    sFloorVpLoc       = glGetUniformLocation(sFloorProg, "uViewProj");
    sFloorBaseLoc     = glGetUniformLocation(sFloorProg, "uBaseColor");
    sFloorLineLoc     = glGetUniformLocation(sFloorProg, "uLineColor");
    sFloorFogColorLoc = glGetUniformLocation(sFloorProg, "uFogColor");
    sFloorCellLoc     = glGetUniformLocation(sFloorProg, "uCellSize");
    sFloorFogStartLoc = glGetUniformLocation(sFloorProg, "uFogStart");
    sFloorFogEndLoc   = glGetUniformLocation(sFloorProg, "uFogEnd");
    sFloorLineFadeStartLoc = glGetUniformLocation(sFloorProg, "uLineFadeStart");
    sFloorLineFadeEndLoc   = glGetUniformLocation(sFloorProg, "uLineFadeEnd");

    sFloorDecalLoc         = glGetUniformLocation(sFloorProg, "uDecalTex");
    sFloorDecalHalfSizeLoc = glGetUniformLocation(sFloorProg, "uDecalHalfSize");
    sFloorUseDecalLoc      = glGetUniformLocation(sFloorProg, "uUseDecal");


#ifndef ANDROID // if PC
    extern GLuint gfx_opengl_get_logo_tex();
    sDecalTex = gfx_opengl_get_logo_tex();
#else
    sDecalTex = android_load_logo_texture("logo.jpg");
#endif

    BuildFloorMesh();
    BuildSkyMesh();

    sInitDone = true;
}


void vr_hub_render(const float eyeViewProj[2][16]) {
    if (!sInitDone) vr_hub_init();

    GLint prevDepthFunc = GL_LESS;
    glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);
    glDepthFunc(GL_LESS);

    GLboolean prevPolyOffsetFill = glIsEnabled(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_POLYGON_OFFSET_FILL);

    GLboolean prevStencilTest = glIsEnabled(GL_STENCIL_TEST);
    glDisable(GL_STENCIL_TEST);

    for (int i = 0; i < 4; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);

    GLint prevVao = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);

    GLint prevProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);

    GLint prevBlendSrcRGB, prevBlendDstRGB, prevBlendSrcAlpha, prevBlendDstAlpha;
    glGetIntegerv(GL_BLEND_SRC_RGB,   &prevBlendSrcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB,   &prevBlendDstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDstAlpha);
    GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);

    GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);

    GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE);

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glClearColor(HUB_SKY_COLOR_HORIZON[0], HUB_SKY_COLOR_HORIZON[1], HUB_SKY_COLOR_HORIZON[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);


// --- Floor ---
    glDepthMask(GL_TRUE);
    glUseProgram(sFloorProg);
    glUniformMatrix4fv(sFloorVpLoc, 2, GL_FALSE, &eyeViewProj[0][0]);
    glUniform3fv(sFloorBaseLoc, 1, HUB_FLOOR_BASE_COLOR);
    glUniform3fv(sFloorLineLoc, 1, HUB_FLOOR_LINE_COLOR);
    glUniform3fv(sFloorFogColorLoc, 1, HUB_SKY_COLOR_HORIZON);
    glUniform1f(sFloorCellLoc, HUB_GRID_CELL_METERS);
    glUniform1f(sFloorFogStartLoc, HUB_FOG_START);
    glUniform1f(sFloorFogEndLoc, HUB_FOG_END);
    glUniform1f(sFloorLineFadeStartLoc, HUB_LINE_FADE_START);
    glUniform1f(sFloorLineFadeEndLoc, HUB_LINE_FADE_END);

    GLint prevTex0 = 0;
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex0);

    if (sDecalTex != 0) {
        glBindTexture(GL_TEXTURE_2D, sDecalTex);
        glUniform1i(sFloorDecalLoc, 0);
        glUniform2f(sFloorDecalHalfSizeLoc, HUB_DECAL_HALF_WIDTH, HUB_DECAL_HALF_DEPTH);
        glUniform1i(sFloorUseDecalLoc, 1);
    } else {
        glUniform1i(sFloorUseDecalLoc, 0);
    }

    glBindVertexArray(sFloorVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindTexture(GL_TEXTURE_2D, prevTex0);

    glBindVertexArray(prevVao);
    glUseProgram(prevProgram);

    if (blendWasEnabled) {
        glEnable(GL_BLEND);
        glBlendFuncSeparate(prevBlendSrcRGB, prevBlendDstRGB, prevBlendSrcAlpha, prevBlendDstAlpha);
    } else {
        glDisable(GL_BLEND);
    }

    if (!depthWasEnabled) glDisable(GL_DEPTH_TEST);
    if (cullWasEnabled) glEnable(GL_CULL_FACE);
    if (scissorWasEnabled) glEnable(GL_SCISSOR_TEST);

    glDepthFunc(prevDepthFunc);
    if (prevPolyOffsetFill) glEnable(GL_POLYGON_OFFSET_FILL);
    if (prevStencilTest) glEnable(GL_STENCIL_TEST);
}

