#include <ultra64.h>

#include <math.h>

#include "constants.h"
#include "game/chraction.h"
#include "game/bondgun.h"
#include "game/game_0b0fd0.h"
#include "game/game_0b2150.h"
#include "game/tex.h"
#include "game/savebuffer.h"
#include "game/sight.h"
#include "game/game_1531a0.h"
#include "game/file.h"
#include "game/gfxmemory.h"
#include "game/lang.h"
#include "game/options.h"
#include "game/propobj.h"
#include "bss.h"
#include "lib/vi.h"
#include "lib/main.h"
#include "lib/snd.h"
#include "data.h"
#include "types.h"
#ifndef PLATFORM_N64

#include "video.h"

#include "../port/vr/vr_openxr.h"
#include <lib/mtx.h>
#include <game/camera.h>

#ifdef ANDROID
#include <android/log.h>


#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "PD-VR", __VA_ARGS__)
#else
#define LOGI(...) printf(__VA_ARGS__)
#endif

#define SIGHT_COLOUR ((PLAYER_EXTCFG().crosshairhealth >= CROSSHAIR_HEALTH_ON_GREEN) ? sightGetCrosshairHealthColor(g_Vars.currentplayer->bondhealth, g_Vars.currentplayer->prop->chr->cshield * 0.125f) : PLAYER_EXTCFG().crosshaircolour)
#define SIGHT_SCALE PLAYER_EXTCFG().crosshairsize

extern float  vr_LeftCrossX;
extern float  vr_LeftCrossY;
extern bool vr_LeftCrossValid;
extern int vr_button_R_grip;
extern int vr_button_L_grip;

extern void gfxSetCrosshairParallaxRight(float correction);
extern void gfxSetCrosshairParallaxLeft(float correction);
extern float vrComputeCrosshairParallax(float distanceGameUnits);




// =============================================================================
// HELPER : sightDrawLine3D
// =============================================================================
// Replaces gDPHudRectangle to draw a segment (x1,y1)→(x2,y2) as a
// 3D quad (2 triangles) with a controllable Z axis.
//
// Parameters:
//   x1,y1 → x2,y2 : screen-space coordinates (pixels, float)
//   z              : depth in vertex space ×10
//                    ex: -10 = -1.0f  (near HUD, default Skedar/Maian)
//                        -50 = -5.0f  (mid-distance)
//                       -200 = -20.0f (far)
//   colour         : RGBA u32 (same format as other sights)
//
// NOTE: the function assumes the RSP pipeline is already initialized
//       (func0f0d479c + gSPSetGeometryMode + gDPSetRenderMode etc.)
//       The caller is responsible for opening/closing this context.
// =============================================================================
static Gfx *sightDrawLine3D(Gfx *gdl, f32 x1, f32 y1, f32 x2, f32 y2,
                            s16 z, u32 colour)
{
    Vtx *verts = gfxAllocateVertices(4);
    Col *cols  = gfxAllocateColours(1);

    cols[0].word = PD_BE32(colour);

    // +1 always on x2 and y2 to be inclusive like gDPHudRectangle.
    // For a horizontal line (y1==y2): +1 on y gives a 1px thickness.
    // For a vertical line (x1==x2): +1 on x gives a 1px thickness.
    // In both cases, +1 on the other axis ensures the last pixel is included.
    const f32 rx2 = x2 + 1.0f;
    const f32 ry2 = y2 + 1.0f;

    verts[0].x = (s16)roundf(x1  * 10.0f) - 2;
    verts[0].y = (s16)roundf(y1  * 10.0f) + 2;
    verts[0].z = z;
    verts[0].colour = 0;

    verts[1].x = (s16)roundf(rx2 * 10.0f) - 2;
    verts[1].y = (s16)roundf(y1  * 10.0f) + 2;
    verts[1].z = z;
    verts[1].colour = 0;

    verts[2].x = (s16)roundf(rx2 * 10.0f) - 2;
    verts[2].y = (s16)roundf(ry2 * 10.0f) + 2;
    verts[2].z = z;
    verts[2].colour = 0;

    verts[3].x = (s16)roundf(x1  * 10.0f) - 2;
    verts[3].y = (s16)roundf(ry2 * 10.0f) + 2;
    verts[3].z = z;
    verts[3].colour = 0;

    gSPColor(gdl++, cols, 1);
    gSPVertex(gdl++, verts, 4, 0);
    gSPTri2(gdl++, 0, 1, 2, 0, 2, 3);

    return gdl;
}

// =============================================================================
// Utility macro: Z value to use for all 3D sights.
// =============================================================================
#define CROSSHAIR_Z_DEFAULT ((s16)(-10))   // -1.0f in shaders
#define CROSSHAIR_Z_DEFAULT_LEFT ((s16)(-8))  // abs(w-1) == -7.0 in shaders
static s16 g_currentCrosshairZ = CROSSHAIR_Z_DEFAULT;
s32 g_currentCrosshairHand = HAND_RIGHT;
extern bool show_laser_dot[2]; // for sightClassic


// =============================================================================
// Internal macro: opens the RSP geometry context for 3D sights.
// Call BEFORE the first sightDrawLine3D().
// =============================================================================
#define SIGHT3D_BEGIN(gdl)                                          \
    do {                                                            \
        gdl = func0f0d479c(gdl);                                    \
        gSPClearGeometryMode(gdl++, G_CULL_BOTH);                   \
        gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH);      \
        gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);           \
        gDPSetTextureFilter(gdl++, G_TF_BILERP);                    \
        gDPSetCycleType(gdl++, G_CYC_1CYCLE);                       \
        gDPSetRenderMode(gdl++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2); \
        gSPSetExtraGeometryModeEXT(gdl++, G_ASPECT_CENTER_EXT);     \
    } while(0)

// Close the RSP geometry context.
#define SIGHT3D_END(gdl)                                            \
    do {                                                            \
        gdl = func0f0d49c8(gdl);                                    \
        gSPClearExtraGeometryModeEXT(gdl++, G_ASPECT_CENTER_EXT);   \
    } while(0)





static u32 sightGetCrosshairHealthColor(float health, float shield)
{
    const float ratio = MAX(0.0f, MIN(health + shield, 2.0f));

    int red = 0;
    int green = 0;
    int blue = 0;
    if (ratio < 0.2f) {
        // Red (critical health level)
        red = 255;
        green = 0;
        blue = 0;
    } else if (ratio < 0.6f) {
        // Red-yellow
        red = 255;
        green = 255 * ((ratio - 0.2f) / 0.4f);
        blue = 0;
    } else if (ratio < 1.0f) {
        if (PLAYER_EXTCFG().crosshairhealth == CROSSHAIR_HEALTH_ON_GREEN) {
            // Yellow-green
            red = 255 * ((ratio - 0.6f) / 0.4f);
            green = 255;
            blue = 0;
        } else {
            // Yellow-white
            red = 255;
            green = 255;
            blue = 255 * ((ratio - 0.6f) / 0.4f);
        }
    } else {
        if (PLAYER_EXTCFG().crosshairhealth == CROSSHAIR_HEALTH_ON_GREEN) {
            // Green-cyan (overheal via shield)
            red = 0;
            green = 255;
            blue = 255 * (ratio - 1.0f);
        } else {
            // White-green (overheal via shield)
            red = 255 * (2.0f - ratio);
            green = 255;
            blue = 255 * (2.0f - ratio);
        }
    }

    return (red << 24) + (green << 16) + (blue << 8) + (PLAYER_EXTCFG().crosshaircolour & 0xff);
}

static inline f32 sightGetScaleX(void)
{

    return (videoGetAspect() / SCREEN_ASPECT);
}


static inline f32 sightGetAdjustedX(const f32 x) // VR
{
    const f32 cx = (x - (f32)(VrRecommendedW / 2)) * sightGetScaleX();
    return (f32)(VrRecommendedW / 2) + cx;
}

static inline void sightCalcSubpixel(
        f32 fx, f32 fy,
        s32 *out_x, s32 *out_y,
        f32 *out_subx, f32 *out_suby)
{
    const f32 x_adjusted = sightGetAdjustedX(fx);
    const f32 xi = floorf(x_adjusted);
    const f32 yi = floorf(fy);
    *out_x    = (s32)xi;
    *out_y    = (s32)yi;
    *out_subx = floorf((x_adjusted - xi) * 5.0f);
    *out_suby = floorf((fy - yi) * 5.0f);
}



#else

#define SIGHT_COLOUR 0x00ff0028
#define SIGHT_SCALE 2
#define sightGetScaleX() 1.f
#define sightGetAdjustedX(x) (x)

#endif

/**
 * Return true if the prop is considered friendly (blue sight).
 */
bool sightIsPropFriendly(struct prop *prop)
{
    if (prop == NULL) {
        prop = g_Vars.currentplayer->lookingatprop.prop;
    }

    if (prop == NULL) {
        return false;
    }

    if (prop->type != PROPTYPE_CHR && prop->type != PROPTYPE_PLAYER) {
        return false;
    }

    if (g_Vars.coopplayernum >= 0 && prop->type == PROPTYPE_PLAYER) {
        return true;
    }

    if (g_Vars.antiplayernum >= 0 && prop->type == PROPTYPE_PLAYER) {
        return false;
    }

    if (g_Vars.normmplayerisrunning == false
        && prop->chr
        && (prop->chr->hidden2 & CHRH2FLAG_BLUESIGHT)) {
        return true;
    }

    return chrCompareTeams(g_Vars.currentplayer->prop->chr, prop->chr, COMPARE_FRIENDS);
}

void sight0f0d715c(void)
{
    // empty
}

Gfx *sight0f0d7164(Gfx *gdl)
{
    return gdl;
}

/**
 * Return true if the given prop can be added to the target list.
 */
bool sightCanTargetProp(struct prop *prop, s32 max)
{
    s32 i;

    for (i = 0; i < max; i++) {
        if (prop == g_Vars.currentplayer->trackedprops[i].prop) {
            return false;
        }
    }

    if (prop->type == PROPTYPE_CHR) {
        return true;
    }

    if (prop->type == PROPTYPE_PLAYER) {
        return true;
    }

    if ((prop->type == PROPTYPE_OBJ || prop->type == PROPTYPE_WEAPON || prop->type == PROPTYPE_DOOR)
        && prop->obj && (prop->obj->flags3 & OBJFLAG3_REACTTOSIGHT)) {
        return true;
    }

    if (bgunGetWeaponNum(HAND_RIGHT) == WEAPON_ROCKETLAUNCHER) {
        return true;
    }

    return false;
}

/**
 * Return true if the sight should change colour when aiming at the given prop.
 */
bool sightIsReactiveToProp(struct prop *prop)
{
    if (prop->obj == NULL) {
        return false;
    }

    if (prop->type == PROPTYPE_OBJ || prop->type == PROPTYPE_WEAPON || prop->type == PROPTYPE_DOOR) {
        struct defaultobj *obj = prop->obj;

        if (g_Vars.stagenum == STAGE_CITRAINING
            && (obj->modelnum == MODEL_COMHUB || obj->modelnum == MODEL_CIHUB || obj->modelnum == MODEL_TARGET)) {
            return true;
        }

        if (objGetDestroyedLevel(obj) > 0) {
            return false;
        }
    } else if (prop->type == PROPTYPE_CHR) {
        struct chrdata *chr = prop->chr;

        if (chr && chr->race == RACE_EYESPY) {
            struct eyespy *eyespy = chrToEyespy(chr);

            if (!eyespy || !eyespy->deployed) {
                return false;
            }
        }
    }

    return true;
}

s32 sightFindFreeTargetIndex(s32 max)
{
    s32 i;

    for (i = 0; i < max; i++) {
        if (g_Vars.currentplayer->trackedprops[i].prop == NULL) {
            return i;
        }
    }

    return -1;
}

void func0f0d7364(void)
{
    s32 i;

    for (i = 0; i < ARRAYCOUNT(g_Vars.currentplayer->trackedprops); i++) {
        g_Vars.currentplayer->trackedprops[i].prop = NULL;
    }
}

void sightTick(bool sighton)
{
    struct trackedprop *trackedprop;
    u8 newtracktype;
    s32 i;
    s32 index;
    struct invaimsettings *gunsettings = gsetGetAimSettings(&g_Vars.currentplayer->hands[0].gset);
    struct weaponfunc *func = weaponGetFunctionById(g_Vars.currentplayer->hands[0].gset.weaponnum,
                                                    g_Vars.currentplayer->hands[0].gset.weaponfunc);

    g_Vars.currentplayer->sighttimer240 += g_Vars.lvupdate240;

    for (i = 0; i < ARRAYCOUNT(g_Vars.currentplayer->targetset); i++) {
        if (g_Vars.currentplayer->targetset[i] > TICKS(512)) {
            if (g_Vars.currentplayer->targetset[i] < (VERSION >= VERSION_PAL_BETA ? TICKS(1020) : 1024) - g_Vars.lvupdate240) {
                g_Vars.currentplayer->targetset[i] += g_Vars.lvupdate240;
            } else {
                g_Vars.currentplayer->targetset[i] = TICKS(1020);
            }
        } else {
            if (g_Vars.currentplayer->targetset[i] < (VERSION >= VERSION_PAL_BETA ? TICKS(512) : 516) - g_Vars.lvupdate240) {
                g_Vars.currentplayer->targetset[i] += g_Vars.lvupdate240;
            } else {
                g_Vars.currentplayer->targetset[i] = TICKS(512);
            }
        }
    }

    newtracktype = gunsettings->tracktype;

    if (gsetHasFunctionFlags(&g_Vars.currentplayer->hands[0].gset, FUNCFLAG_THREATDETECTOR)) {
        newtracktype = SIGHTTRACKTYPE_THREATDETECTOR;
    }

    if (func && (func->type & 0xff) == INVENTORYFUNCTYPE_MELEE) {
        newtracktype = SIGHTTRACKTYPE_NONE;
    }

    if (newtracktype != g_Vars.currentplayer->sighttracktype) {
        if (newtracktype == SIGHTTRACKTYPE_THREATDETECTOR) {
            for (i = 0; i < ARRAYCOUNT(g_Vars.currentplayer->trackedprops); i++) {
                g_Vars.currentplayer->trackedprops[i].prop = NULL;
            }
        }

        g_Vars.currentplayer->sighttracktype = newtracktype;

        switch (newtracktype) {
            case SIGHTTRACKTYPE_NONE:
            case SIGHTTRACKTYPE_DEFAULT:
            case SIGHTTRACKTYPE_BETASCANNER:
            case SIGHTTRACKTYPE_ROCKETLAUNCHER:
            case SIGHTTRACKTYPE_FOLLOWLOCKON:
                break;
        }
    }

    if (sighton && g_Vars.currentplayer->lastsighton == false && newtracktype != SIGHTTRACKTYPE_THREATDETECTOR) {
        for (i = 0; i < ARRAYCOUNT(g_Vars.currentplayer->trackedprops); i++) {
            g_Vars.currentplayer->trackedprops[i].prop = NULL;
        }
    }

    for (i = 0; i < ARRAYCOUNT(g_Vars.currentplayer->trackedprops); i++) {
        trackedprop = &g_Vars.currentplayer->trackedprops[i];

        if (trackedprop->prop && !sightIsReactiveToProp(trackedprop->prop)) {
            trackedprop->prop = NULL;
        }
    }

    trackedprop = &g_Vars.currentplayer->lookingatprop;

    if (trackedprop->prop && !sightIsReactiveToProp(trackedprop->prop)) {
        trackedprop->prop = NULL;
    }

    switch (g_Vars.currentplayer->sighttracktype) {
        case SIGHTTRACKTYPE_DEFAULT:
        case SIGHTTRACKTYPE_BETASCANNER:
            // Conditionally copy lookingatprop to trackedprops[0], overwriting anything that's there
            if (sighton) {
                if (g_Vars.currentplayer->lookingatprop.prop) {
                    if (g_Vars.currentplayer->lookingatprop.prop != g_Vars.currentplayer->trackedprops[0].prop) {
                        struct sndstate *handle;

                        handle = snd00010718(NULL, 0, AL_VOL_FULL, AL_PAN_CENTER, SFX_0007, 1, 1, -1, true);

                        trackedprop = &g_Vars.currentplayer->trackedprops[0];

                        trackedprop->prop = g_Vars.currentplayer->lookingatprop.prop;
                        trackedprop->x1 = g_Vars.currentplayer->lookingatprop.x1;
                        trackedprop->y1 = g_Vars.currentplayer->lookingatprop.y1;
                        trackedprop->x2 = g_Vars.currentplayer->lookingatprop.x2;
                        trackedprop->y2 = g_Vars.currentplayer->lookingatprop.y2;

                        g_Vars.currentplayer->targetset[0] = 0;
                    }
                } else {
                    g_Vars.currentplayer->trackedprops[0].prop = NULL;
                }
            }
            break;
        case SIGHTTRACKTYPE_ROCKETLAUNCHER:
            // Conditionally copy lookingatprop to trackedprops[0], but only if that slot is empty
            if (sighton && g_Vars.currentplayer->lookingatprop.prop
                && sightCanTargetProp(g_Vars.currentplayer->lookingatprop.prop, 1)) {
                index = sightFindFreeTargetIndex(1);

                if (index >= 0) {
                    struct sndstate *handle;

                    handle = snd00010718(NULL, 0, AL_VOL_FULL, AL_PAN_CENTER, SFX_0007, 1, 1, -1, 1);

                    trackedprop = &g_Vars.currentplayer->trackedprops[index];

                    trackedprop->prop = g_Vars.currentplayer->lookingatprop.prop;
                    trackedprop->x1 = g_Vars.currentplayer->lookingatprop.x1;
                    trackedprop->y1 = g_Vars.currentplayer->lookingatprop.y1;
                    trackedprop->x2 = g_Vars.currentplayer->lookingatprop.x2;
                    trackedprop->y2 = g_Vars.currentplayer->lookingatprop.y2;

                    g_Vars.currentplayer->targetset[index] = 0;
                }
            }
            break;
        case SIGHTTRACKTYPE_FOLLOWLOCKON:
            // Conditionally copy lookingatprop to any trackedprops slot, but only if the slot is empty
            if (sighton && g_Vars.currentplayer->lookingatprop.prop
                && sightCanTargetProp(g_Vars.currentplayer->lookingatprop.prop, 4)) {
                index = sightFindFreeTargetIndex(4);

                if (index >= 0) {
                    struct sndstate *handle;

                    handle = snd00010718(NULL, 0, AL_VOL_FULL, AL_PAN_CENTER, SFX_0007, 1, 1, -1, 1);

                    trackedprop = &g_Vars.currentplayer->trackedprops[index];

                    trackedprop->prop = g_Vars.currentplayer->lookingatprop.prop;
                    trackedprop->x1 = g_Vars.currentplayer->lookingatprop.x1;
                    trackedprop->y1 = g_Vars.currentplayer->lookingatprop.y1;
                    trackedprop->x2 = g_Vars.currentplayer->lookingatprop.x2;
                    trackedprop->y2 = g_Vars.currentplayer->lookingatprop.y2;

                    g_Vars.currentplayer->targetset[index] = 0;
                }
            }
            break;
        case SIGHTTRACKTYPE_NONE:
        case SIGHTTRACKTYPE_THREATDETECTOR:
            break;
    }

    g_Vars.currentplayer->lastsighton = sighton;
}

/**
 * Calculate the position of one border of a target box.
 *
 * The arguments here are named for a left border,
 * but can be called for any of the four edges.
 */
s32 sightCalculateBoxBound(s32 targetx, s32 viewleft, s32 timeelapsed, s32 timeend)
{
    s32 value;

    if (timeelapsed > timeend) {
        timeelapsed = timeend;
    }

    value = (targetx - viewleft) * timeelapsed;

    return viewleft + value / timeend;
}

/**
 * Draw a red (or blue) box around the given trackedprop.
 *
 * textid can be:
 * 0 to have no label
 * 1 to label it as "0"
 * 2 to label it as "1"
 * ...
 * 6 to label it as "5"
 * 7 or above to treat textid as a proper language text ID.
 */
Gfx *sightDrawTargetBox(Gfx *gdl, struct trackedprop *trackedprop, s32 textid, s32 time)
{
    s32 viewleft = viGetViewLeft() / g_ScaleX;
    s32 viewtop = viGetViewTop();
    s32 viewwidth = viGetViewWidth() / g_ScaleX;
    s32 viewheight = viGetViewHeight();
    s32 viewright = viewleft + viewwidth - 1;
    s32 viewbottom = viewtop + viewheight - 1;
    u32 colour;
    s32 boxleft;
    s32 boxright;
    s32 boxtop;
    s32 boxbottom;
    bool textonscreen = true;

    if (time > TICKS(512)) {
        time = TICKS(512);
    }

    boxleft = sightCalculateBoxBound(trackedprop->x1 / g_ScaleX, viewleft, time, TICKS(80));
    boxtop = sightCalculateBoxBound(trackedprop->y1, viewtop, time, TICKS(80));
    boxright = sightCalculateBoxBound(trackedprop->x2 / g_ScaleX, viewright, time, TICKS(80));
    boxbottom = sightCalculateBoxBound(trackedprop->y2, viewbottom, time, TICKS(80));

    if (trackedprop->prop) {
        colour = sightIsPropFriendly(trackedprop->prop) ? 0x000ff60 : 0xff000060;

        gdl = textSetPrimColour(gdl, colour);

        // Left
        if (boxleft >= viewleft && boxleft <= viewright && boxtop <= viewbottom && boxbottom >= viewtop) {
            gDPHudRectangle(gdl++,
                            boxleft, (boxtop > viewtop ? boxtop : viewtop),
                            boxleft, (boxbottom < viewbottom ? boxbottom : viewbottom));
        }

        // Right
        if (boxright >= viewleft && boxright <= viewright && boxtop <= viewbottom && boxbottom >= viewtop) {
            gDPHudRectangle(gdl++,
                            boxright, (boxtop > viewtop ? boxtop : viewtop),
                            boxright, (boxbottom < viewbottom ? boxbottom : viewbottom));
        } else {
            textonscreen = false;
        }

        // Top
        if (boxtop >= viewtop && boxtop <= viewbottom && boxleft <= viewright && boxright >= viewleft) {
            gDPHudRectangle(gdl++,
                            (boxleft > viewleft ? boxleft : viewleft), boxtop,
                            (boxright < viewright ? boxright : viewright), boxtop);
        } else {
            textonscreen = false;
        }

        // Bottom
        if (boxbottom >= viewtop && boxbottom <= viewbottom && boxleft <= viewright && boxright >= viewleft) {
            gDPHudRectangle(gdl++,
                            (boxleft > viewleft ? boxleft : viewleft), boxbottom,
                            (boxright < viewright ? boxright : viewright), boxbottom);
        }

        gdl = text0f153838(gdl);

        if (textid != 0 && textonscreen) {
            s32 x = boxright + 3;
            s32 y = boxtop + 3;

            if (textid < 7) {
                char label[] = {'1', '\n', '\0'};

                // textid 1 writes '0'
                label[0] = textid + 0x2f;

                gdl = textRender(gdl, &x, &y, label, g_CharsNumeric, g_FontNumeric, 0x00ff00a0, 0x000000a0, viGetWidth(), viGetHeight(), 0, 0);
            } else {
                char *text = langGet(textid);
#if VERSION >= VERSION_JPN_FINAL
                gdl = func0f1574d0jf(gdl, &x, &y, text, g_CharsHandelGothicXs, g_FontHandelGothicXs, 0x00ff00a0, 0x000000a0, viGetWidth(), viGetHeight(), 0, 0);
#else
                gdl = textRender(gdl, &x, &y, text, g_CharsHandelGothicXs, g_FontHandelGothicXs, 0x00ff00a0, 0x000000a0, viGetWidth(), viGetHeight(), 0, 0);
#endif
            }
        }
    }

    return gdl;
}

Gfx *sightDrawAimer(Gfx *gdl, f32 fx, f32 fy, s32 radius, s32 cornergap,
                    u32 colour)
{
    const s32 viewleft   = viGetViewLeft() / g_ScaleX;
    const s32 viewtop    = viGetViewTop();
    const s32 viewwidth  = viGetViewWidth() / g_ScaleX;
    const s32 viewheight = viGetViewHeight();
    const s32 viewright  = viewleft + viewwidth  - 1;
    const s32 viewbottom = viewtop  + viewheight - 1;

    const f32 x = sightGetAdjustedX(fx);
    const f32 y = fy;

    const s16 z = g_currentCrosshairZ;

    SIGHT3D_BEGIN(gdl);

    {
        Col *c = gfxAllocateColours(1);
        c[0].word = PD_BE32(SIGHT_COLOUR);
    }

    if (PLAYERCOUNT() == 1) {
        gdl = sightDrawLine3D(gdl, viewleft + 48, y, x - radius + 2, y,
                              z, SIGHT_COLOUR);
        gdl = sightDrawLine3D(gdl, x + radius - 2, y, viewright - 49, y,
                              z, SIGHT_COLOUR);
        gdl = sightDrawLine3D(gdl, x, viewtop + 10, x, y - radius + 2,
                              z, SIGHT_COLOUR);
        gdl = sightDrawLine3D(gdl, x, y + radius - 2, x, viewbottom - 10,
                              z, SIGHT_COLOUR);
    } else {
        gdl = sightDrawLine3D(gdl, viewleft, y, x - radius + 2, y,
                              z, SIGHT_COLOUR);
        gdl = sightDrawLine3D(gdl, x + radius - 2, y, viewright, y,
                              z, SIGHT_COLOUR);
        gdl = sightDrawLine3D(gdl, x, viewtop, x, y - radius + 2,
                              z, SIGHT_COLOUR);
        gdl = sightDrawLine3D(gdl, x, y + radius - 2, x, viewbottom,
                              z, SIGHT_COLOUR);
    }


    gdl = sightDrawLine3D(gdl, x - radius, y - radius, x - radius, y + radius,
                          z, colour);
    gdl = sightDrawLine3D(gdl, x + radius, y - radius, x + radius, y + radius,
                          z, colour);
    gdl = sightDrawLine3D(gdl, x - radius, y - radius, x + radius, y - radius,
                          z, colour);
    gdl = sightDrawLine3D(gdl, x - radius, y + radius, x + radius, y + radius,
                          z, colour);



    gdl = sightDrawLine3D(gdl, x - radius, y - radius, x - radius, y - cornergap,
                          z, colour);
    gdl = sightDrawLine3D(gdl, x - radius, y + cornergap, x - radius, y + radius,
                          z, colour);
    gdl = sightDrawLine3D(gdl, x + radius, y - radius, x + radius, y - cornergap,
                          z, colour);
    gdl = sightDrawLine3D(gdl, x + radius, y + cornergap, x + radius, y + radius,
                          z, colour);
    gdl = sightDrawLine3D(gdl, x - radius, y - radius, x - cornergap, y - radius,
                          z, colour);
    gdl = sightDrawLine3D(gdl, x + cornergap, y - radius, x + radius, y - radius,
                          z, colour);
    gdl = sightDrawLine3D(gdl, x - radius, y + radius, x - cornergap, y + radius,
                          z, colour);
    gdl = sightDrawLine3D(gdl, x + cornergap, y + radius, x + radius, y + radius,
                          z, colour);

    SIGHT3D_END(gdl);

    return gdl;
}


/**
 * The delayed aimer is an unused aimer box. It's twice as big as the normal one
 * and follows the gun's cursor with a very noticeable delay. The lines that
 * span the viewport are not used here, and a 3x3 box is filled in with green
 * at the live crosshair position.
 *
 * Because its position and speed properties are static variables, they only get
 * updated when the aimer is held. This means releasing and pressing R again
 * causes the box to appear where it was last.
 *
 * The default Y position is not quite centered, is not updated for PAL,
 * and is not reset for split screen play. There's also no viewport boundary
 * checks. It's likely that this feature was just a concept and was dropped
 * pretty early.
 */
Gfx *sightDrawDelayedAimer(Gfx *gdl, s32 x, s32 y, s32 radius, s32 cornergap, u32 colour)
{
    s32 boxx;
    s32 boxy;
    s32 i;
    f32 dist;
    f32 accel;
    u32 stack;

    static f32 xpos = 160;
    static f32 ypos = 120;
    static f32 xspeed = 0;
    static f32 yspeed = 0;

#ifndef PLATFORM_N64
    x = sightGetAdjustedX(x);
    gSPSetExtraGeometryModeEXT(gdl++, G_ASPECT_CENTER_EXT);
#endif

    for (i = 0; i < g_Vars.lvupdate60; i++) {
        dist = x - xpos;

        if (dist > 0.5f || dist < -0.5f) {
            accel = dist * 0.05f;

            if (accel > PALUPF(2.0f)) {
                accel = PALUPF(2.0f);
            }

            if (accel < -PALUPF(2.0f)) {
                accel = -PALUPF(2.0f);
            }

            if (accel > xspeed) {
                accel = PALUPF(0.05f);
            } else if (accel < xspeed) {
                accel = -PALUPF(0.05f);
            } else {
                accel = 0.0f;
            }

            xspeed += accel;

            if (xspeed > PALUPF(2.0f)) {
                xspeed = PALUPF(2.0f);
            }

            if (xspeed < -PALUPF(2.0f)) {
                xspeed = -PALUPF(2.0f);
            }

            xpos += xspeed;
        } else {
            xpos = x;
            xspeed = 0.0f;
        }

        dist = y - ypos;

        if (dist > 0.5f || dist < -0.5f) {
            accel = dist * 0.05f;

            if (accel > PALUPF(2.0f)) {
                accel = PALUPF(2.0f);
            }
            if (accel < -PALUPF(2.0f)) {
                accel = -PALUPF(2.0f);
            }

            if (yspeed < accel) {
                accel = PALUPF(0.05f);
            } else if (accel < yspeed) {
                accel = -PALUPF(0.05f);
            } else {
                accel = 0.0f;
            }

            yspeed += accel;

            if (yspeed > PALUPF(2.0f)) {
                yspeed = PALUPF(2.0f);
            }

            if (yspeed < -PALUPF(2.0f)) {
                yspeed = -PALUPF(2.0f);
            }

            ypos += yspeed;
        } else {
            ypos = y;
            yspeed = 0.0f;
        }
    }

#ifndef PLATFORM_N64
    {
        f32 subxb, subyb;
        f32 xadj = sightGetAdjustedX(xpos);
        f32 xboxf = floorf(xadj);
        f32 yboxf = floorf(ypos);
        subxb = floorf((xadj - xboxf) * 5.0f);
        subyb = floorf((ypos - yboxf) * 5.0f);
        boxx  = (s32)xboxf;
        boxy  = (s32)yboxf;
        gDPSetSubpixelOffsetEXT(gdl++, subxb, subyb);
    }
#else
    boxx = (s32)xpos;
    boxy = (s32)ypos;
#endif

    gdl = textSetPrimColour(gdl, SIGHT_COLOUR);

    // Fill a 3x3 box at the live crosshair
    gDPHudRectangle(gdl++, x - 1, y - 1, x + 1, y - 1);
    gDPHudRectangle(gdl++, x - 1, y + 0, x + 1, y + 0);
    gDPHudRectangle(gdl++, x - 1, y + 1, x + 1, y + 1);

    gdl = text0f153838(gdl);

    gdl = textSetPrimColour(gdl, colour);

    // Draw the box
    gDPHudRectangle(gdl++, boxx - radius, boxy - radius, boxx - radius, boxy + radius);
    gDPHudRectangle(gdl++, boxx + radius, boxy - radius, boxx + radius, boxy + radius);
    gDPHudRectangle(gdl++, boxx - radius, boxy - radius, boxx + radius, boxy - radius);
    gDPHudRectangle(gdl++, boxx - radius, boxy + radius, boxx + radius, boxy + radius);

    // Go over the corners a second time
    gDPHudRectangle(gdl++, boxx - radius, boxy - radius, boxx - radius, boxy - cornergap);
    gDPHudRectangle(gdl++, boxx - radius, boxy + cornergap, boxx - radius, boxy + radius);
    gDPHudRectangle(gdl++, boxx + radius, boxy - radius, boxx + radius, boxy - cornergap);
    gDPHudRectangle(gdl++, boxx + radius, boxy + cornergap, boxx + radius, boxy + radius);
    gDPHudRectangle(gdl++, boxx - radius, boxy - radius, boxx - cornergap, boxy - radius);
    gDPHudRectangle(gdl++, boxx + cornergap, boxy - radius, boxx + radius, boxy - radius);
    gDPHudRectangle(gdl++, boxx - radius, boxy + radius, boxx - cornergap, boxy + radius);
    gDPHudRectangle(gdl++, boxx + cornergap, boxy + radius, boxx + radius, boxy + radius);

    gdl = text0f153838(gdl);

#ifndef PLATFORM_N64
    gSPClearExtraGeometryModeEXT(gdl++, G_ASPECT_CENTER_EXT);
#endif

    return gdl;
}

Gfx *sightDrawDefault(Gfx *gdl, bool sighton, f32 crossx, f32 crossy)
{
    s32 radius;
    s32 cornergap;
    u32 colour;

    f32 x = crossx / g_ScaleX;
    f32 y = crossy;
    struct trackedprop *trackedprop;
    s32 i;
    static s32 sight = 0;
    static s32 identifytimer = 0;

    gdl = text0f153628(gdl);

    if (1);

    switch (g_Vars.currentplayer->sighttracktype) {
        case SIGHTTRACKTYPE_NONE:
            if (sighton) {
                colour    = SIGHT_COLOUR;
                radius    = 8;
                cornergap = 5;
                gdl = sightDrawAimer(gdl, x, y, radius, cornergap, colour);
            }
            break;

        case SIGHTTRACKTYPE_DEFAULT:
            if (sighton) {
                if (g_Vars.currentplayer->lookingatprop.prop == NULL) {
                    colour    = SIGHT_COLOUR;
                    radius    = 8;
                    cornergap = 5;
                } else {
                    colour    = sightIsPropFriendly(NULL) ? 0x0000ff60 : 0xff000060;
                    radius    = 6;
                    cornergap = 3;
                }

                mainOverrideVariable("sight", &sight);

                switch (sight) {
                    case 0:
                        gdl = sightDrawAimer(gdl, x, y, radius, cornergap, colour);
                        break;
                    case 1:
                        gdl = sightDrawDelayedAimer(gdl, x, y, radius * 2,
                                                    cornergap * 2, colour);
                        break;
                }
            }
            break;

        case SIGHTTRACKTYPE_BETASCANNER:
            if (sighton) {
                s32 textx;
                s32 texty;

                if (g_Vars.currentplayer->lookingatprop.prop == NULL) {
                    colour    = SIGHT_COLOUR;
                    radius    = 8;
                    cornergap = 5;
                } else {
                    colour    = sightIsPropFriendly(NULL) ? 0x0000ff60 : 0xff000060;
                    radius    = 6;
                    cornergap = 3;
                }

                textx = 135;
                texty = 200;
                identifytimer += g_Vars.lvupdate240;

                if (identifytimer & 0x80) {
                    gdl = textRender(gdl, &textx, &texty, langGet(L_MISC_439),
                                     g_CharsHandelGothicXs, g_FontHandelGothicXs,
                                     0x00ff00a0, 0x000000a0,
                                     viGetWidth(), viGetHeight(), 0, 0);
                }

                gdl = sightDrawAimer(gdl, x, y, radius, cornergap, colour);

                if (g_Vars.currentplayer->lookingatprop.prop) {
                    gdl = sightDrawTargetBox(gdl,
                                             &g_Vars.currentplayer->lookingatprop,
                                             1,
                                             g_Vars.currentplayer->targetset[0]);
                }
            }
            break;

        case SIGHTTRACKTYPE_ROCKETLAUNCHER:
            for (i = 0; i < 1; i++) {
                trackedprop = &g_Vars.currentplayer->trackedprops[i];
                if (trackedprop->prop) {
                    gdl = sightDrawTargetBox(gdl, trackedprop, 0,
                                             g_Vars.currentplayer->targetset[i]);
                }
            }

            if (sighton) {
                if (g_Vars.currentplayer->lookingatprop.prop == NULL) {
                    colour    = SIGHT_COLOUR;
                    radius    = 8;
                    cornergap = 5;
                } else {
                    colour    = sightIsPropFriendly(NULL) ? 0x0000ff60 : 0xff000060;
                    radius    = 6;
                    cornergap = 3;
                }
                gdl = sightDrawAimer(gdl, x, y, radius, cornergap, colour);
            }
            break;

        case SIGHTTRACKTYPE_FOLLOWLOCKON:
        case SIGHTTRACKTYPE_THREATDETECTOR:
            for (i = 0; i < ARRAYCOUNT(g_Vars.currentplayer->trackedprops); i++) {
                trackedprop = &g_Vars.currentplayer->trackedprops[i];

                if (trackedprop->prop) {
                    if (g_Vars.currentplayer->sighttracktype
                        == SIGHTTRACKTYPE_THREATDETECTOR) {
                        struct defaultobj *obj = trackedprop->prop->obj;
                        struct weaponobj  *weapon;
                        u32 textid = 0;

                        if (obj && obj->type == OBJTYPE_AUTOGUN
                            && (obj->flags2 & (OBJFLAG2_AICANNOTUSE
                                               | OBJFLAG2_AUTOGUN_MALFUNCTIONING1))
                               == 0) {
                            textid = L_GUN_215;
                        }

                        weapon = trackedprop->prop->weapon;
                        if (weapon && weapon->base.type == OBJTYPE_WEAPON) {
                            switch (weapon->weaponnum) {
                                case WEAPON_GRENADE:
                                    textid = (weapon->gunfunc == FUNC_SECONDARY)
                                             ? L_GUN_212 : L_GUN_213;
                                    break;
                                case WEAPON_NBOMB:
                                    textid = (weapon->gunfunc == FUNC_SECONDARY)
                                             ? L_GUN_212 : L_GUN_216;
                                    break;
                                case WEAPON_TIMEDMINE:
                                    textid = L_GUN_213;
                                    break;
                                case WEAPON_PROXIMITYMINE:
                                    textid = L_GUN_212;
                                    break;
                                case WEAPON_REMOTEMINE:
                                    textid = L_GUN_214;
                                    break;
                                case WEAPON_DRAGON:
                                    if (weapon->gunfunc == FUNC_SECONDARY) {
                                        textid = L_GUN_212;
                                    }
                                    break;
                            }
                        }
                        gdl = sightDrawTargetBox(gdl, trackedprop, textid,
                                                 g_Vars.currentplayer->targetset[i]);
                    } else {
                        gdl = sightDrawTargetBox(gdl, trackedprop, i + 2,
                                                 g_Vars.currentplayer->targetset[i]);
                    }
                }
            }

            if (sighton) {
                if (g_Vars.currentplayer->lookingatprop.prop == NULL) {
                    colour    = SIGHT_COLOUR;
                    radius    = 8;
                    cornergap = 5;
                } else {
                    colour    = sightIsPropFriendly(NULL) ? 0x0000ff60 : 0xff000060;
                    radius    = 6;
                    cornergap = 3;
                }
                gdl = sightDrawAimer(gdl, x, y, radius, cornergap, colour);
            }
            break;
    }

    gdl = text0f153780(gdl);

    return gdl;
}

// For sightDrawClassic
static inline void crosshair3dSetVtx(Vtx *v, f32 cx, f32 cy, f32 cz,
                                     const struct coord *right,
                                     const struct coord *up,
                                     f32 rx, f32 ry, f32 half)
{
    v->x = (s16)roundf(cx + (right->x*rx + up->x*ry) * half);
    v->y = (s16)roundf(cy + (right->y*rx + up->y*ry) * half);
    v->z = (s16)roundf(cz + (right->z*rx + up->z*ry) * half);
}

Gfx *sightDrawClassic(Gfx *gdl, bool sighton, f32 crossx, f32 crossy)
{
    if (!sighton) {
        return gdl;
    }

    struct player *player = g_Vars.currentplayer;
    struct hand *rhand = &player->hands[g_currentCrosshairHand];

    // Screen mode: fixed dotpos, use vrdir via crossx/crossy
    if (!show_laser_dot[g_currentCrosshairHand]) {
        struct textureconfig *tconfig = &g_TexGeCrosshairConfigs[0];
        f32 spc4[2];
        f32 spbc[2];

        const f32 xi   = floorf(crossx);
        const f32 yi   = floorf(crossy + 1.0f);
        const s32 x    = (s32)xi;
        const s32 y    = (s32)yi;
        const f32 subx = floorf((crossx - xi)          * 5.0f);
        const f32 suby = floorf(((crossy + 1.0f) - yi) * 5.0f);


        static f32 vrdir_scale = 0.35f;

        const s32 halfw = roundf((f32)(tconfig->width >> 1)
                                 * (SCREEN_ASPECT / videoGetAspect())
                                 * vrdir_scale);

        gDPSetColorDither(gdl++,    G_CD_DISABLE);
        gDPSetTexturePersp(gdl++,   G_TP_NONE);
        gDPSetAlphaCompare(gdl++,   G_AC_NONE);
        gDPSetTextureLOD(gdl++,     G_TL_TILE);
        gDPSetTextureFilter(gdl++,  G_TF_POINT);
        gDPSetTextureConvert(gdl++, G_TC_FILT);
        gDPSetTextureLUT(gdl++,     G_TT_NONE);
        gDPPipeSync(gdl++);
        gDPSetSubpixelOffsetEXT(gdl++, subx, suby);
        gDPSetCycleType(gdl++,      G_CYC_1CYCLE);
        gDPSetRenderMode(gdl++,     G_RM_XLU_SURF, G_RM_XLU_SURF2);
        gDPSetCombineMode(gdl++,    G_CC_PRIMITIVE, G_CC_PRIMITIVE);
        gDPSetPrimColor(gdl++,      0, 0, 0x00, 0x00, 0x00, 0x00);

        const s32 x1 = x - halfw;
        const s32 y1 = y - (tconfig->height >> 1);
        const s32 x2 = x + halfw;
        const s32 y2 = y + (tconfig->height >> 1);
        gDPFillRectangle(gdl++, x1, y1, x2, y2);

        spc4[0] = x;
        spc4[1] = y;
        spbc[0] = halfw * (f32)g_ScaleX;
        spbc[1] = (tconfig->height >> 1) * vrdir_scale;

        texSelect(&gdl, tconfig, 2, 0, 0, 1, NULL);
        func0f0b278c(&gdl, spc4, spbc,
                     tconfig->width, tconfig->height,
                     0, 0, 1, 0xff, 0xff, 0xff, 0x7f,
                     tconfig->level > 0, 0);

        gDPPipeSync(gdl++);
        gDPSetSubpixelOffsetEXT(gdl++, 0, 0);

        gDPSetColorDither(gdl++,    G_CD_BAYER);
        gDPSetTexturePersp(gdl++,   G_TP_PERSP);
        gDPSetAlphaCompare(gdl++,   G_AC_NONE);
        gDPSetTextureLOD(gdl++,     G_TL_LOD);
        gDPSetTextureFilter(gdl++,  G_TF_BILERP);
        gDPSetTextureConvert(gdl++, G_TC_FILT);
        gDPSetTextureLUT(gdl++,     G_TT_NONE);

        return gdl;
    }

    // World 3D mode: real dotpos
    const struct coord *dotpos = &rhand->dotpos;

    if (dotpos->x == 0.0f && dotpos->y == 0.0f && dotpos->z == 0.0f) {
        return gdl;
    }

    struct coord right, up;

    {
        f32 nx = dotpos->x - rhand->muzzlepos.x;
        f32 ny = dotpos->y - rhand->muzzlepos.y;
        f32 nz = dotpos->z - rhand->muzzlepos.z;
        f32 rlen = sqrtf(nx*nx + ny*ny + nz*nz);

        if (rlen < 0.0001f) {
            return gdl;
        }

        nx /= rlen; ny /= rlen; nz /= rlen;

        struct coord up_world = {0.0f, 1.0f, 0.0f};
        if (fabsf(ny) > 0.99f) {
            up_world.x = 1.0f;
            up_world.y = 0.0f;
            up_world.z = 0.0f;
        }

        right.x = up_world.y*nz - up_world.z*ny;
        right.y = up_world.z*nx - up_world.x*nz;
        right.z = up_world.x*ny - up_world.y*nx;
        f32 rl = sqrtf(right.x*right.x + right.y*right.y + right.z*right.z);
        if (rl > 0.0001f) { right.x /= rl; right.y /= rl; right.z /= rl; }

        up.x = ny*right.z - nz*right.y;
        up.y = nz*right.x - nx*right.z;
        up.z = nx*right.y - ny*right.x;
    }

    struct coord campos = player->cam_pos;
    f32 dx = dotpos->x - campos.x;
    f32 dy = dotpos->y - campos.y;
    f32 dz = dotpos->z - campos.z;
    f32 dist = sqrtf(dx*dx + dy*dy + dz*dz);

    f32 half = 0.030f * dist;
    if (half < 8.0f) half = 8.0f;

    {
        static const f32 sizeScale[5] = { 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };
        s32 idx = g_PlayerExtCfg[0].crosshairsize;
        if (idx < 0) idx = 0;
        if (idx > 4) idx = 4;
        half *= sizeScale[idx];
    }

    Mtxf world_mtx;
    mtx4LoadIdentity(&world_mtx);
    mtx00015be0(camGetWorldToScreenMtxf(), &world_mtx);

    gDPSetColorDither(gdl++,    G_CD_DISABLE);
    gDPSetTexturePersp(gdl++,   G_TP_PERSP);
    gDPSetAlphaCompare(gdl++,   G_AC_NONE);
    gDPSetTextureLOD(gdl++,     G_TL_TILE);
    gDPSetTextureFilter(gdl++,  G_TF_POINT);
    gDPSetTextureConvert(gdl++, G_TC_FILT);
    gDPSetTextureLUT(gdl++,     G_TT_NONE);
    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++,      G_CYC_1CYCLE);
    gDPSetRenderMode(gdl++,     G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gSPClearGeometryMode(gdl++, G_CULL_BOTH);

    texSelect(&gdl, &g_TexGeCrosshairConfigs[0], 2, 0, 0, 1, NULL);

    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(gdl++, 0, 0, 0xff, 0xff, 0xff, 0x7f);

    Mtxf *mtx_tex = gfxAllocateMatrix();
    mtxF2L(&world_mtx, mtx_tex);
    gSPMatrix(gdl++, osVirtualToPhysical(mtx_tex),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    Vtx *vtx = gfxAllocateVertices(4);
    vtx[0].colour = vtx[1].colour = vtx[2].colour = vtx[3].colour = 0;

    vtx[0].s = 0;       vtx[0].t = 0;
    vtx[1].s = 32 << 5; vtx[1].t = 0;
    vtx[2].s = 32 << 5; vtx[2].t = 32 << 5;
    vtx[3].s = 0;       vtx[3].t = 32 << 5;

    crosshair3dSetVtx(&vtx[0], dotpos->x, dotpos->y, dotpos->z, &right, &up, -1.0f, -1.0f, half);
    crosshair3dSetVtx(&vtx[1], dotpos->x, dotpos->y, dotpos->z, &right, &up,  1.0f, -1.0f, half);
    crosshair3dSetVtx(&vtx[2], dotpos->x, dotpos->y, dotpos->z, &right, &up,  1.0f,  1.0f, half);
    crosshair3dSetVtx(&vtx[3], dotpos->x, dotpos->y, dotpos->z, &right, &up, -1.0f,  1.0f, half);

    gSPVertex(gdl++, osVirtualToPhysical(vtx), 4, 0);
    gSPTri2(gdl++, 0, 1, 2,  0, 2, 3);

    return gdl;
}


Gfx *sightDrawType2(Gfx *gdl, bool sighton, f32 crossx, f32 crossy)
{
    return sightDrawClassic(gdl, sighton, crossx, crossy);
}

#define COLOUR_LIGHTRED 0xff555564
#define COLOUR_DARKRED  0xff0000b2
#define COLOUR_GREEN    0x55ff5564
#define COLOUR_DARKBLUE 0x0000ff60

#define DIR_UP    0
#define DIR_DOWN  1
#define DIR_LEFT  2
#define DIR_RIGHT 3




Gfx *sightDrawSkedarTriangle(Gfx *gdl, f32 x, f32 y, s32 dir, u32 colour)
{
    f32 points[6];
    Vtx *vertices = gfxAllocateVertices(3);
    Col *colours = gfxAllocateColours(2);

    switch (dir) {
        case DIR_UP:
            points[0] = x;      points[1] = y;
            points[2] = x + 5;  points[3] = y + 7;
            points[4] = x - 5;  points[5] = y + 7;
            break;
        case DIR_DOWN:
            points[0] = x;      points[1] = y;
            points[2] = x + 5;  points[3] = y - 7;
            points[4] = x - 5;  points[5] = y - 7;
            break;
        case DIR_LEFT:
            points[0] = x;      points[1] = y;
            points[2] = x + 7;  points[3] = y - 5;
            points[4] = x + 7;  points[5] = y + 5;
            break;
        case DIR_RIGHT:
            points[0] = x;      points[1] = y;
            points[2] = x - 7;  points[3] = y - 5;
            points[4] = x - 7;  points[5] = y + 5;
            break;
        default:
            return gdl;
    }

    // Encode sub-pixel position directly into the vertices (1/10 pixel precision)
    vertices[0].x = (s16)roundf(points[0] * 10.0f);
    vertices[0].y = (s16)roundf(points[1] * 10.0f);
    vertices[0].z = -10;
    vertices[1].x = (s16)roundf(points[2] * 10.0f);
    vertices[1].y = (s16)roundf(points[3] * 10.0f);
    vertices[1].z = -10;
    vertices[2].x = (s16)roundf(points[4] * 10.0f);
    vertices[2].y = (s16)roundf(points[5] * 10.0f);
    vertices[2].z = -10;

#ifndef PLATFORM_N64
    for (int i = 0; i < 3; ++i) {
        vertices[i].x -= 2;
        vertices[i].y += 2;
    }
#endif

    if (colour == COLOUR_DARKRED && sightIsPropFriendly(NULL)) {
        colour = COLOUR_DARKBLUE;
    }

#define RGBA(r, g, b, a) (((r) & 0xff) << 24 | ((g) & 0xff) << 16 | ((b) & 0xff) << 8 | ((a) & 0xff))
    colours[0].word = PD_BE32(colour);
    colours[1].word = PD_BE32(RGBA((colour >> 24) & 0xff, (colour >> 16) & 0xff, (colour >> 8) & 0xff, 0x08));

    vertices[0].colour = 0;
    vertices[1].colour = 4;
    vertices[2].colour = 4;

    gSPColor(gdl++, colours, 2);
    gSPVertex(gdl++, vertices, 3, 0);
    gSPTri1(gdl++, 0, 1, 2);

    return gdl;
}


Gfx *sightDrawSkedar(Gfx *gdl, bool sighton, f32 crossx, f32 crossy)
{
    s32 viewleft   = viGetViewLeft() / g_ScaleX;
    s32 viewtop    = viGetViewTop();
    s32 viewwidth  = viGetViewWidth() / g_ScaleX;
    s32 viewheight = viGetViewHeight();
    s32 viewright  = viewleft + viewwidth - 1;
    s32 viewbottom = viewtop + viewheight - 1;
    s32 paddingy   = viewheight / 4;
    s32 paddingx   = viewwidth / 4;
    f32 trix1, trix2, triy1, triy2;
    u32 colour;
    u8 dir;
    bool hasprop = g_Vars.currentplayer->lookingatprop.prop != NULL;
    f32 frac;

    if (!sighton) {
        return gdl;
    }

    if (!hasprop) {
        g_Vars.currentplayer->sighttimer240 = 0;
    }

#ifndef PLATFORM_N64
    const f32 fx = sightGetAdjustedX(crossx / g_ScaleX);
    const f32 fy = crossy;
    const s32 x = (s32)floorf(fx);
    const s32 y = (s32)floorf(fy);
    gSPSetExtraGeometryModeEXT(gdl++, G_ASPECT_CENTER_EXT);

#else
    const s32 x = (s32)(crossx / g_ScaleX);
    const s32 y = (s32)crossy;
    const f32 fx = (f32)x;
    const f32 fy = (f32)y;
#endif

    gdl = func0f0d479c(gdl);
    gSPClearGeometryMode(gdl++, G_CULL_BOTH);
    gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH);
    gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
    gDPSetTextureFilter(gdl++, G_TF_BILERP);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetRenderMode(gdl++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);

    trix1 = fx;
    triy1 = fy;
    trix2 = fx;
    triy2 = fy;

    if (hasprop && g_Vars.currentplayer->sighttimer240 < TICKS(48)) {
        frac = g_Vars.currentplayer->sighttimer240 / TICKS(48.0f);
    }

    // Outer top triangle
    if (!hasprop) {
        colour = COLOUR_LIGHTRED;
        if (x < viewleft + paddingx) {
            dir = DIR_LEFT;
            trix1 = viewleft + paddingx;
        } else if (x > viewright - paddingx) {
            dir = DIR_RIGHT;
            trix1 = viewright - paddingx;
        } else {
            dir = DIR_DOWN;
            colour = COLOUR_GREEN;
        }
        triy1 = viewtop + paddingy;
    } else {
        if (g_Vars.currentplayer->sighttimer240 < TICKS(48)) {
            colour = COLOUR_LIGHTRED;
            dir = DIR_DOWN;
            triy1 = (fy - viewtop - paddingy - 2.0f) * frac + viewtop + paddingy;
        } else {
            colour = COLOUR_DARKRED;
            dir = DIR_DOWN;
            triy1 = fy - 2.0f;
        }
    }
    gdl = sightDrawSkedarTriangle(gdl, trix1, triy1, dir, colour);

    // Outer bottom triangle
    if (!hasprop) {
        colour = COLOUR_LIGHTRED;
        if (dir == DIR_DOWN) {
            colour = COLOUR_GREEN;
            dir = DIR_UP;
        }
        triy1 = viewbottom - paddingy;
    } else {
        if (g_Vars.currentplayer->sighttimer240 < TICKS(48)) {
            colour = COLOUR_LIGHTRED;
            dir = DIR_UP;
            triy1 = (fy - viewbottom + paddingy + 2.0f) * frac + viewbottom - paddingy;
        } else {
            colour = COLOUR_DARKRED;
            dir = DIR_UP;
            triy1 = fy + 2.0f;
        }
    }
    gdl = sightDrawSkedarTriangle(gdl, trix1, triy1, dir, colour);

    // Outer right triangle
    if (!hasprop) {
        colour = COLOUR_LIGHTRED;
        if (y < viewtop + paddingy) {
            dir = DIR_UP;
            triy2 = viewtop + paddingy;
        } else if (y > viewbottom - paddingy) {
            dir = DIR_DOWN;
            triy2 = viewbottom - paddingy;
        } else {
            dir = DIR_LEFT;
            colour = COLOUR_GREEN;
        }
        trix2 = viewright - paddingx;
    } else {
        if (g_Vars.currentplayer->sighttimer240 < TICKS(48)) {
            colour = COLOUR_LIGHTRED;
            dir = DIR_LEFT;
            trix2 = (fx - viewright + paddingx + 2.0f) * frac + viewright - paddingx;
        } else {
            colour = COLOUR_DARKRED;
            dir = DIR_LEFT;
            trix2 = fx + 2.0f;
        }
    }
    gdl = sightDrawSkedarTriangle(gdl, trix2, triy2, dir, colour);

    // Outer left triangle
    if (!hasprop) {
        colour = COLOUR_LIGHTRED;
        if (dir == DIR_LEFT) {
            colour = COLOUR_GREEN;
            dir = DIR_RIGHT;
        }
        trix2 = viewleft + paddingx;
    } else {
        if (g_Vars.currentplayer->sighttimer240 < TICKS(48)) {
            colour = COLOUR_LIGHTRED;
            dir = DIR_RIGHT;
            trix2 = (fx - viewleft - paddingx - 2.0f) * frac + viewleft + paddingx;
        } else {
            colour = COLOUR_DARKRED;
            dir = DIR_RIGHT;
            trix2 = fx - 2.0f;
        }
    }
    gdl = sightDrawSkedarTriangle(gdl, trix2, triy2, dir, colour);

    // Inner triangles — fx/fy
    if (!hasprop || g_Vars.currentplayer->sighttimer240 < TICKS(48)) {
        colour = hasprop ? COLOUR_LIGHTRED : COLOUR_GREEN;
        gdl = sightDrawSkedarTriangle(gdl, fx,        fy - 2.0f, DIR_DOWN,  colour);
        gdl = sightDrawSkedarTriangle(gdl, fx,        fy + 2.0f, DIR_UP,    colour);
        gdl = sightDrawSkedarTriangle(gdl, fx - 2.0f, fy,        DIR_RIGHT, colour);
        gdl = sightDrawSkedarTriangle(gdl, fx + 2.0f, fy,        DIR_LEFT,  colour);
    }

    gdl = func0f0d49c8(gdl);

#ifndef PLATFORM_N64
    gSPClearExtraGeometryModeEXT(gdl++, G_ASPECT_CENTER_EXT);
#endif

    return gdl;
}


Gfx *sightDrawZoom(Gfx *gdl, bool sighton, f32 crossx, f32 crossy)
{
    s32 viewleft = viGetViewLeft() / g_ScaleX;
    s32 viewtop = viGetViewTop();
    s32 viewhalfwidth = (viGetViewWidth() / g_ScaleX) >> 1;
    s32 viewhalfheight = viGetViewHeight() >> 1;
    s32 viewright = viewleft + viewhalfwidth * 2 - 1;
    s32 viewbottom = viewtop + viewhalfheight * 2 - 1;
    f32 maxfovy;
    s32 availableabove;
    s32 availablebelow;
    s32 availableleft;
    s32 availableright;
    f32 zoominfovy;
    f32 frac;
    f32 marginright;
    f32 margintop;
    f32 marginbottom;
    f32 marginleft;
    s32 cornerwidth;
    s32 cornerheight;
    s32 weaponnum;
    u8 showzoomrange;

    // The 48, 49 and 10 numbers are padding values. When zoomed in, the left
    // corner will be 48px from the viewport's left edge. The available values
    // are the zoomable range from the padding to the middle of the viewport.
    availableleft = viewhalfwidth - 48;
    availableright = viewhalfwidth - 49;
    availableabove = viewhalfheight - 10;
    availablebelow = viewhalfheight - 10;
    frac = 1.0f;
    weaponnum = g_Vars.currentplayer->hands[HAND_RIGHT].gset.weaponnum;
    cornerwidth = (viewhalfwidth >> 1) - 60;
    cornerheight = (viewhalfheight >> 1) - 22;

    showzoomrange = optionsGetShowZoomRange(g_Vars.currentplayerstats->mpindex)
                    && optionsGetSightOnScreen(g_Vars.currentplayerstats->mpindex);

    maxfovy = currentPlayerGetGunZoomFov();
    zoominfovy = g_Vars.currentplayer->zoominfovy;

    if (maxfovy == 0.0f || maxfovy == 60.0f) {
        if (weaponnum != WEAPON_SNIPERRIFLE) {
            showzoomrange = false;
        }
    } else {
        frac = maxfovy / zoominfovy;
    }

    if (showzoomrange) {
        gdl = text0f153628(gdl);
        gdl = textSetPrimColour(gdl, SIGHT_COLOUR);

        if (frac < 0.2f) {
            cornerwidth *= 0.2f;
            cornerheight *= 0.2f;
        } else {
            cornerwidth *= frac;
            cornerheight *= frac;
        }

        if (PLAYERCOUNT() >= 2) {
            cornerheight *= 2;
        }

        if (cornerwidth < 5) {
            cornerwidth = 5;
        }

        if (cornerheight < 5) {
            cornerheight = 5;
        }

        // Margin is the gap from the viewport edge to the zoom box
        marginleft = viewhalfwidth - availableleft * frac;
        marginright = viewhalfwidth - availableright * frac;
        marginbottom = viewhalfheight - availablebelow * frac;
        margintop = viewhalfheight - availableabove * frac;

#ifndef PLATFORM_N64
        // Center-align the zoom range
        if (frac != 1.0f) {
            viewleft += 1;
            viewright += 1;
            viewbottom += 1;
            viewtop += 1;
        }
        // Align box correctly for widescreen
        const f32 leftx = sightGetAdjustedX(viewleft + marginleft);
        const f32 rightx = sightGetAdjustedX(viewright - marginright);
#define BOXLEFT   leftx
#define BOXRIGHT  rightx
#else
        #define BOXLEFT   (viewleft + marginleft)
		#define BOXRIGHT  (viewright - marginright)
#endif
#define BOXBOTTOM (viewbottom - marginbottom)
#define BOXTOP    (viewtop + margintop)

        if (cornerwidth > BOXRIGHT - BOXLEFT) {
            cornerwidth = BOXRIGHT - BOXLEFT;
        }

        if (cornerheight > BOXBOTTOM - BOXTOP) {
            cornerheight = BOXBOTTOM - BOXTOP;
        }

#ifndef PLATFORM_N64
        gSPSetExtraGeometryModeEXT(gdl++, G_ASPECT_CENTER_EXT);
        gDPSetSubpixelOffsetEXT(gdl++, -2, -2);
#endif

        // Top left
        gDPHudRectangle(gdl++, BOXLEFT + 1, BOXTOP, BOXLEFT + cornerwidth - 1, BOXTOP);
        gDPHudRectangle(gdl++, BOXLEFT, BOXTOP, BOXLEFT, BOXTOP + cornerheight - 1);

        // Top right
        gDPHudRectangle(gdl++, BOXRIGHT - cornerwidth + 2, BOXTOP, BOXRIGHT - 1, BOXTOP);
        gDPHudRectangle(gdl++, BOXRIGHT, BOXTOP, BOXRIGHT, BOXTOP + cornerheight - 1);

        // Bottom left
        gDPHudRectangle(gdl++, BOXLEFT + 1, BOXBOTTOM, BOXLEFT + cornerwidth - 1, BOXBOTTOM);
        gDPHudRectangle(gdl++, BOXLEFT, BOXBOTTOM - cornerheight + 1, BOXLEFT, BOXBOTTOM);

        // Bottom right
        gDPHudRectangle(gdl++, BOXRIGHT - cornerwidth + 2, BOXBOTTOM, BOXRIGHT - 1, BOXBOTTOM);
        gDPHudRectangle(gdl++, BOXRIGHT, BOXBOTTOM - cornerheight + 1, BOXRIGHT, BOXBOTTOM);

        // Draw over the corners again, but only half as wide/high
        cornerwidth >>= 1;
        cornerheight >>= 1;

        // Top left
        gDPHudRectangle(gdl++, BOXLEFT, BOXTOP, BOXLEFT + cornerwidth, BOXTOP);
        gDPHudRectangle(gdl++, BOXLEFT, BOXTOP, BOXLEFT, BOXTOP + cornerheight);

        // Top right
        gDPHudRectangle(gdl++, BOXRIGHT - cornerwidth, BOXTOP, BOXRIGHT, BOXTOP);
        gDPHudRectangle(gdl++, BOXRIGHT, BOXTOP, BOXRIGHT, BOXTOP + cornerheight);

        // Bottom left
        gDPHudRectangle(gdl++, BOXLEFT, BOXBOTTOM, BOXLEFT + cornerwidth, BOXBOTTOM);
        gDPHudRectangle(gdl++, BOXLEFT, BOXBOTTOM - cornerheight, BOXLEFT, BOXBOTTOM);

        // Bottom right
        gDPHudRectangle(gdl++, BOXRIGHT - cornerwidth, BOXBOTTOM, BOXRIGHT, BOXBOTTOM);
        gDPHudRectangle(gdl++, BOXRIGHT, BOXBOTTOM - cornerheight, BOXRIGHT, BOXBOTTOM);


#ifndef PLATFORM_N64
        gSPClearExtraGeometryModeEXT(gdl++, G_ASPECT_CENTER_EXT);
        gDPSetSubpixelOffsetEXT(gdl++, 0, 0);
#endif

        gdl = text0f153838(gdl);
        gdl = text0f153780(gdl);
    }

    gdl = sightDrawDefault(gdl, sighton, crossx, crossy);

    return gdl;
}

Gfx *sightDrawMaian(Gfx *gdl, bool sighton, f32 crossx, f32 crossy)
{
    s32 viewleft   = viGetViewLeft() / g_ScaleX;
    s32 viewtop    = viGetViewTop();
    s32 viewwidth  = viGetViewWidth() / g_ScaleX;
    s32 viewheight = viGetViewHeight();
    s32 viewright  = viewleft + viewwidth - 1;
    s32 viewbottom = viewtop + viewheight - 1;

    Vtx *vertices;
    Col *colours;
    bool hasprop = g_Vars.currentplayer->lookingatprop.prop != NULL;
    u32 colour = 0xff000060;

    if (!sighton) {
        return gdl;
    }

    if (sightIsPropFriendly(NULL)) {
        colour = 0x0000ff60;
    }

    const f32 fx = sightGetAdjustedX(crossx / g_ScaleX);
    const f32 fy = crossy;

    const s32 x  = (s32)floorf(fx);
    const s32 y  = (s32)floorf(fy);

    gSPSetExtraGeometryModeEXT(gdl++, G_ASPECT_CENTER_EXT);

    vertices = gfxAllocateVertices(8);
    colours  = gfxAllocateColours(2);

    gdl = func0f0d479c(gdl);
    gSPClearGeometryMode(gdl++, G_CULL_BOTH);
    gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH);
    gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
    gDPSetTextureFilter(gdl++, G_TF_BILERP);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetRenderMode(gdl++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);

    // Vertices 0-3: viewport edges (integers, unchanged)
    vertices[0].x = (viewleft + (viewwidth  >> 1)) * 10;
    vertices[0].y = (viewtop  + 10)                * 10;
    vertices[0].z = g_currentCrosshairZ;
    vertices[1].x = (viewleft + (viewwidth  >> 1)) * 10;
    vertices[1].y = (viewbottom - 10)              * 10;
    vertices[1].z = g_currentCrosshairZ;
    vertices[2].x = (viewleft  + 48)               * 10;
    vertices[2].y = (viewtop   + (viewheight >> 1)) * 10;
    vertices[2].z = g_currentCrosshairZ;
    vertices[3].x = (viewright - 49)               * 10;
    vertices[3].y = (viewtop   + (viewheight >> 1)) * 10;
    vertices[3].z = g_currentCrosshairZ;

    // Vertices 4-7: inner square in float sub-pixel (fx/fy, not x/y)
    vertices[4].x = (s16)roundf((fx - 4.0f) * 10.0f);
    vertices[4].y = (s16)roundf((fy - 4.0f) * 10.0f);
    vertices[4].z = g_currentCrosshairZ;
    vertices[5].x = (s16)roundf((fx + 4.0f) * 10.0f);
    vertices[5].y = (s16)roundf((fy - 4.0f) * 10.0f);
    vertices[5].z = g_currentCrosshairZ;
    vertices[6].x = (s16)roundf((fx + 4.0f) * 10.0f);
    vertices[6].y = (s16)roundf((fy + 4.0f) * 10.0f);
    vertices[6].z = g_currentCrosshairZ;
    vertices[7].x = (s16)roundf((fx - 4.0f) * 10.0f);
    vertices[7].y = (s16)roundf((fy + 4.0f) * 10.0f);
    vertices[7].z = g_currentCrosshairZ;

    // Center-align VR
    for (int i = 0; i < 8; ++i) {
        vertices[i].x -= 2;
        vertices[i].y += 2;
    }

    colours[0].word = PD_BE32(0x00ff000f);
    colours[1].word = PD_BE32(hasprop ? colour : 0x00ff0044);

    vertices[0].colour = 0;
    vertices[1].colour = 0;
    vertices[2].colour = 0;
    vertices[3].colour = 0;
    vertices[4].colour = 4;
    vertices[5].colour = 4;
    vertices[6].colour = 4;
    vertices[7].colour = 4;

    gSPColor(gdl++, colours, 2);
    gSPVertex(gdl++, vertices, 8, 0);
    gSPTri4(gdl++, 0, 4, 5, 5, 3, 6, 7, 6, 1, 4, 7, 2);

    // ---- Inner square borders ----
    // Drawn HERE, in the same RSP context, using fx/fy floats
    // → sub-pixel preserved, smooth movement, same Z as the triangles
    // Only change the color via a new gSPColor
    {
        Col *bcols = gfxAllocateColours(1);
        bcols[0].word = PD_BE32(SIGHT_COLOUR);
        gSPColor(gdl++, bcols, 1);
    }

    gdl = sightDrawLine3D(gdl, fx - 4.0f, fy - 4.0f, fx - 4.0f, fy + 4.0f,
                          g_currentCrosshairZ, SIGHT_COLOUR); // left
    gdl = sightDrawLine3D(gdl, fx + 4.0f, fy - 4.0f, fx + 4.0f, fy + 4.0f,
                          g_currentCrosshairZ, SIGHT_COLOUR); // right
    gdl = sightDrawLine3D(gdl, fx - 4.0f, fy - 4.0f, fx + 4.0f, fy - 4.0f,
                          g_currentCrosshairZ, SIGHT_COLOUR); // top
    gdl = sightDrawLine3D(gdl, fx - 4.0f, fy + 4.0f, fx + 4.0f, fy + 4.0f,
                          g_currentCrosshairZ, SIGHT_COLOUR); // bottom

    gdl = func0f0d49c8(gdl);

    gSPClearExtraGeometryModeEXT(gdl++, G_ASPECT_CENTER_EXT);

    return gdl;
}

Gfx *sightDrawTarget(Gfx *gdl, f32 crossx, f32 crossy)
{
    static u32 var80070f9c = 0x00ff00ff;
    static u32 var80070fa0 = 0x00ff0011;

    mainOverrideVariable("sout", &var80070f9c);
    mainOverrideVariable("sin",  &var80070fa0);

    const f32 x = sightGetAdjustedX(crossx / g_ScaleX);
    const f32 y = crossy;

    const s16  z      = g_currentCrosshairZ;
    const u32  colour = SIGHT_COLOUR;
    const f32  sc     = (f32)SIGHT_SCALE;

    SIGHT3D_BEGIN(gdl);

    if (SIGHT_SCALE == 0) {
        gdl = sightDrawLine3D(gdl, x, y, x, y, z, colour);
    } else {
        gdl = sightDrawLine3D(gdl, x + 1*sc, y,      x + 3*sc, y,      z, colour);
        gdl = sightDrawLine3D(gdl, x + 1*sc, y,      x + 2*sc, y,      z, colour);
        gdl = sightDrawLine3D(gdl, x - 3*sc, y,      x - 1*sc, y,      z, colour);
        gdl = sightDrawLine3D(gdl, x - 2*sc, y,      x - 1*sc, y,      z, colour);
        gdl = sightDrawLine3D(gdl, x,        y + 1*sc, x,      y + 3*sc, z, colour);
        gdl = sightDrawLine3D(gdl, x,        y + 1*sc, x,      y + 2*sc, z, colour);
        gdl = sightDrawLine3D(gdl, x,        y - 3*sc, x,      y - 1*sc, z, colour);
        gdl = sightDrawLine3D(gdl, x,        y - 2*sc, x,      y - 1*sc, z, colour);
    }

    SIGHT3D_END(gdl);

    return gdl;
}

bool sightHasTargetWhileAiming(s32 sight)
{
    if (sight == SIGHT_DEFAULT || sight == SIGHT_ZOOM) {
        return true;
    }

    return false;
}





static Gfx *sightDrawLeftHand(Gfx *gdl, s32 L_sight, bool sighton)
{
    const f32  crossx  = vr_LeftCrossX;
    const f32  crossy  = vr_LeftCrossY;

    g_currentCrosshairHand = HAND_LEFT;
    struct hand *lhand = &g_Vars.currentplayer->hands[HAND_LEFT];
    g_currentCrosshairZ = CROSSHAIR_Z_DEFAULT_LEFT;
    float targetDist = 50000.0f; // default: far away (zero parallax)

    if (lhand != NULL) {
        float dx = lhand->dotpos.x - g_Vars.currentplayer->prop->pos.x;
        float dy = lhand->dotpos.y - g_Vars.currentplayer->prop->pos.y;
        float dz = lhand->dotpos.z - g_Vars.currentplayer->prop->pos.z;
        float dist = sqrtf(dx*dx + dy*dy + dz*dz);
        if (dist > 10.0f) targetDist = dist; // ignore if too close (0 = no impact)
    }
    gfxSetCrosshairParallaxLeft(vrComputeCrosshairParallax(targetDist));

    if(vr_button_L_grip) {
        switch (L_sight) {
            case SIGHT_DEFAULT:
                gdl = sightDrawDefault(gdl, sighton && optionsGetSightOnScreen(
                        g_Vars.currentplayerstats->mpindex), crossx, crossy);
                break;
            case SIGHT_CLASSIC:
                gdl = sightDrawClassic(gdl, sighton && optionsGetSightOnScreen(
                        g_Vars.currentplayerstats->mpindex), crossx, crossy);
                break;
            case SIGHT_2:
                gdl = sightDrawType2(gdl, sighton && optionsGetSightOnScreen(
                        g_Vars.currentplayerstats->mpindex), crossx, crossy);
                break;
            case SIGHT_3:
                gdl = sightDrawDefault(gdl, sighton && optionsGetSightOnScreen(
                        g_Vars.currentplayerstats->mpindex), crossx, crossy);
                break;
            case SIGHT_SKEDAR:
                gdl = sightDrawSkedar(gdl, sighton && optionsGetSightOnScreen(
                        g_Vars.currentplayerstats->mpindex), crossx, crossy);
                break;
            case SIGHT_ZOOM:
                gdl = sightDrawZoom(gdl, sighton && optionsGetSightOnScreen(
                        g_Vars.currentplayerstats->mpindex), crossx, crossy);
                break;
            case SIGHT_MAIAN:
                gdl = sightDrawMaian(gdl, sighton && optionsGetSightOnScreen(
                        g_Vars.currentplayerstats->mpindex), crossx, crossy);
                break;
            default:
                gdl = sightDrawDefault(gdl, sighton && optionsGetSightOnScreen(
                        g_Vars.currentplayerstats->mpindex), crossx, crossy);
                break;
            case SIGHT_NONE:
                break;
        }
    }

    if (L_sight != SIGHT_NONE && optionsGetSightOnScreen(g_Vars.currentplayerstats->mpindex)) {
        if ((optionsGetAlwaysShowTarget(g_Vars.currentplayerstats->mpindex) && !sighton)
            || (sighton && sightHasTargetWhileAiming(L_sight))) {
            gdl = sightDrawTarget(gdl, crossx, crossy);
        }
    }
    g_ScaleX = 1;

    return gdl;
}



/**
 * sighton is true if the player is using the aimer (ie. holding R).
 */




Gfx *sightDraw(Gfx *gdl, bool sighton, s32 sight)
{
    if (sight);

    if (g_Vars.currentplayer->activemenumode != AMMODE_CLOSED) {
        return gdl;
    }

    if (g_Vars.currentplayer->gunctrl.passivemode) {
        return gdl;
    }


    g_currentCrosshairHand = HAND_RIGHT;
    struct hand *rhand = &g_Vars.currentplayer->hands[HAND_RIGHT];
    float targetDist = 50000.0f; // default: far away (zero parallax)
    g_currentCrosshairZ = CROSSHAIR_Z_DEFAULT;

    if (rhand != NULL) {
        float dx = rhand->dotpos.x - g_Vars.currentplayer->prop->pos.x;
        float dy = rhand->dotpos.y - g_Vars.currentplayer->prop->pos.y;
        float dz = rhand->dotpos.z - g_Vars.currentplayer->prop->pos.z;
        float dist = sqrtf(dx*dx + dy*dy + dz*dz);
        if (dist > 10.0f) targetDist = dist; // ignore if too close (0 = no impact)
    }

    gfxSetCrosshairParallaxRight(vrComputeCrosshairParallax(targetDist));

#ifndef PLATFORM_N64
    // Rounding the crosshair positions allow them to more accurately follow the
    // gun's vector. Without this, the mantissa isn't factored in at all (cast
    // to integer), which leads to some awkward behavior, such as the crosshair
    // taking a long time to return to the center of the screen when coming from
    // an up and/or left direction.

    const f32 crossx = g_Vars.currentplayer->crosspos[0]; // VR
    const f32 crossy = g_Vars.currentplayer->crosspos[1];
#else
    const f32 crossx = g_Vars.currentplayer->crosspos[0];
	const f32 crossy = g_Vars.currentplayer->crosspos[1];
#endif

#if PAL
    g_ScaleX = 1;
#else
    if (g_ViRes == VIRES_HI) {
        g_ScaleX = 2;
    } else {
        g_ScaleX = 1;
    }
#endif

    if (PLAYERCOUNT() >= 2 && g_Vars.coopplayernum < 0 && g_Vars.antiplayernum < 0) {
        sight = SIGHT_DEFAULT;
    }

#ifndef PLATFORM_N64
    if (g_Vars.currentplayer->bondhealth <= 0.0f) {
        // Hide crosshair during death animation
        sight = SIGHT_NONE;
    }
#endif

    sightTick(sighton);

    if(vr_button_R_grip) {
        switch (sight) {
            case SIGHT_DEFAULT:
                gdl = sightDrawDefault(gdl, sighton && optionsGetSightOnScreen(
                        g_Vars.currentplayerstats->mpindex), crossx, crossy);
                break;
            case SIGHT_CLASSIC:
                gdl = sightDrawClassic(gdl, sighton && optionsGetSightOnScreen(
                        g_Vars.currentplayerstats->mpindex), crossx, crossy);
                break;
            case SIGHT_2:
                gdl = sightDrawType2(gdl, sighton && optionsGetSightOnScreen(
                        g_Vars.currentplayerstats->mpindex), crossx, crossy);
                break;
            case SIGHT_3:
                gdl = sightDrawDefault(gdl, sighton && optionsGetSightOnScreen(
                        g_Vars.currentplayerstats->mpindex), crossx, crossy);
                break;
            case SIGHT_SKEDAR:
                gdl = sightDrawSkedar(gdl, sighton && optionsGetSightOnScreen(
                        g_Vars.currentplayerstats->mpindex), crossx, crossy);
                break;
            case SIGHT_ZOOM:
                gdl = sightDrawZoom(gdl, sighton && optionsGetSightOnScreen(
                        g_Vars.currentplayerstats->mpindex), crossx, crossy);
                break;
            case SIGHT_MAIAN:
                gdl = sightDrawMaian(gdl, sighton && optionsGetSightOnScreen(
                        g_Vars.currentplayerstats->mpindex), crossx, crossy);
                break;
            default:
                gdl = sightDrawDefault(gdl, sighton && optionsGetSightOnScreen(
                        g_Vars.currentplayerstats->mpindex), crossx, crossy);
                break;
            case SIGHT_NONE:
                break;
        }

    }

    if (sight != SIGHT_NONE && optionsGetSightOnScreen(g_Vars.currentplayerstats->mpindex)) {
        if ((optionsGetAlwaysShowTarget(g_Vars.currentplayerstats->mpindex) && !sighton)
            || (sighton && sightHasTargetWhileAiming(sight))) {
            gdl = sightDrawTarget(gdl, crossx, crossy);
        }
    }

    // ===== VR Left-hand crosshair =====
    if (vr_LeftCrossValid && sight != SIGHT_NONE) {
        gdl = sightDrawLeftHand(gdl, sight, sighton);
    }

    return gdl;
}
