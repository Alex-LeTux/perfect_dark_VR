#include <ultra64.h>

#include <math.h>

#include "constants.h"
#include "game/chraction.h"
#include "game/bondgun.h"
#include "game/cheats.h"
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
#include "../port/vr/vr_log.h"

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
extern s32 g_LookingAtPropHandMask;
s32 g_currentCrosshairHand = HAND_RIGHT;
extern bool show_laser_dot[2]; // detect if dotpos is valable



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

    return (videoGetAspect() / XrAspect);
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



// =============================================================================
// 3D Helper: Projection Data Structure
// =============================================================================
struct sightprojdata {
    struct coord right;
    struct coord up;
    f32 fx, fy;
    f32 aim_x, aim_y, aim_z;
    f32 dx, dy, dz, dist;
    f32 cx, cy, cz;
    f32 render_dist;
    f32 fov_scale;
    f32 zoom_scale;
    s32 viewleft, viewtop, viewwidth, viewheight, viewright, viewbottom;
    bool is_sky;
    bool hasprop;
};

// =============================================================================
// 3D Helper: Calculates all projection and VR math
// =============================================================================
static void sightCalculate3DProjection(f32 crossx, f32 crossy, struct sightprojdata *p)
{
    struct player *player = g_Vars.currentplayer;
    struct hand *rhand = &player->hands[g_currentCrosshairHand];
    const struct coord *dotpos = &rhand->dotpos;
    struct coord campos = player->cam_pos;

    p->fx = sightGetAdjustedX(crossx / g_ScaleX);
    p->fy = crossy;

    p->viewleft = viGetViewLeft() / g_ScaleX;
    p->viewtop = viGetViewTop();
    p->viewwidth = viGetViewWidth() / g_ScaleX;
    p->viewheight = viGetViewHeight();
    p->viewright = p->viewleft + p->viewwidth - 1;
    p->viewbottom = p->viewtop + p->viewheight - 1;

    Mtxf *viewmtx = camGetWorldToScreenMtxf();
    p->right.x = viewmtx->m[0][0]; p->right.y = viewmtx->m[1][0]; p->right.z = viewmtx->m[2][0];
    p->up.x    = viewmtx->m[0][1]; p->up.y    = viewmtx->m[1][1]; p->up.z    = viewmtx->m[2][1];

    f32 rl = sqrtf(p->right.x*p->right.x + p->right.y*p->right.y + p->right.z*p->right.z);
    if (rl > 0.0001f) { p->right.x /= rl; p->right.y /= rl; p->right.z /= rl; }
    f32 ul = sqrtf(p->up.x*p->up.x + p->up.y*p->up.y + p->up.z*p->up.z);
    if (ul > 0.0001f) { p->up.x /= ul; p->up.y /= ul; p->up.z /= ul; }

    f32 scx = p->viewleft + (p->viewwidth / 2.0f);
    f32 scy = p->viewtop + (p->viewheight / 2.0f);

    f32 current_fov = player->fovy;
    if (current_fov <= 0.0f) current_fov = 98.0f;

    f32 tan_fov = tanf(current_fov * 3.14159265f / 360.0f);
    p->fov_scale = tan_fov / (p->viewheight / 2.0f);

    f32 x_diff = (p->fx - scx) * p->fov_scale;
    f32 y_diff = -(p->fy - scy) * p->fov_scale;

    struct coord fwd;
    fwd.x = p->up.y * p->right.z - p->up.z * p->right.y;
    fwd.y = p->up.z * p->right.x - p->up.x * p->right.z;
    fwd.z = p->up.x * p->right.y - p->up.y * p->right.x;
    f32 fl = sqrtf(fwd.x*fwd.x + fwd.y*fwd.y + fwd.z*fwd.z);
    if (fl > 0.0001f) { fwd.x /= fl; fwd.y /= fl; fwd.z /= fl; }

    p->aim_x = fwd.x + p->right.x * x_diff + p->up.x * y_diff;
    p->aim_y = fwd.y + p->right.y * x_diff + p->up.y * y_diff;
    p->aim_z = fwd.z + p->right.z * x_diff + p->up.z * y_diff;
    f32 aim_l = sqrtf(p->aim_x*p->aim_x + p->aim_y*p->aim_y + p->aim_z*p->aim_z);
    if (aim_l > 0.0001f) { p->aim_x /= aim_l; p->aim_y /= aim_l; p->aim_z /= aim_l; }

    p->hasprop = player->lookingatprop.prop != NULL && (g_LookingAtPropHandMask & (1 << g_currentCrosshairHand)) != 0;
    p->is_sky = !show_laser_dot[g_currentCrosshairHand];

    p->dx = dotpos->x - campos.x;
    p->dy = dotpos->y - campos.y;
    p->dz = dotpos->z - campos.z;
    p->dist = sqrtf(p->dx*p->dx + p->dy*p->dy + p->dz*p->dz);

    p->render_dist = 1500.0f;

    if (p->hasprop && player->lookingatprop.prop) {
        struct coord *proppos = &player->lookingatprop.prop->pos;
        f32 px = proppos->x - campos.x;
        f32 py = proppos->y - campos.y;
        f32 pz = proppos->z - campos.z;
        p->render_dist = sqrtf(px*px + py*py + pz*pz);
    } else if (!p->is_sky) {
        p->render_dist = p->dist;
    }

    if (p->render_dist < 100.0f) p->render_dist = 100.0f;
    else if (p->render_dist > 1500.0f) p->render_dist = 1500.0f;

    p->cx = p->aim_x * p->render_dist * 5.0f;
    p->cy = p->aim_y * p->render_dist * 5.0f;
    p->cz = p->aim_z * p->render_dist * 5.0f;

    static f32 vr_base_fov = 0.0f;
    if (current_fov > vr_base_fov) vr_base_fov = current_fov;
    p->zoom_scale = 1.0f;
    if (vr_base_fov > 0.0f) {
        p->zoom_scale = tan_fov / tanf(vr_base_fov * 3.14159265f / 360.0f);
    }
}

// =============================================================================
// 3D Helper: Initializes the N64 rendering state (Pipeline Matrix & Blend)
// =============================================================================
static Gfx *sightSetup3DRenderState(Gfx *gdl, f32 mtx_scale)
{
    gdl = text0f153780(gdl);
    gDPPipeSync(gdl++);
    gDPSetTexturePersp(gdl++, G_TP_PERSP);
    gDPSetColorDither(gdl++, G_CD_DISABLE);
    gDPSetRenderMode(gdl++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
    gDPSetAlphaCompare(gdl++, G_AC_NONE);
    gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
    gSPClearGeometryMode(gdl++, G_CULL_BOTH);
    gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH);

    Mtxf sp1b0; mtx4LoadIdentity(&sp1b0);
    mtx00015be0(camGetWorldToScreenMtxf(), &sp1b0);
    sp1b0.m[3][0] = 0.0f; sp1b0.m[3][1] = 0.0f; sp1b0.m[3][2] = 0.0f;
    mtx00015f88(mtx_scale, &sp1b0);

    Mtxf *mtx = gfxAllocateMatrix(); mtxF2L(&sp1b0, mtx);
    gSPMatrix(gdl++, osVirtualToPhysical(mtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    return gdl;
}


// =============================================================================
// 3D Helper: Draws a line segment in the 3D world (with square caps)
// =============================================================================
static Gfx *sightDrawLineWorld3D(Gfx *gdl, f32 cx, f32 cy, f32 cz,
                                 const struct coord *right, const struct coord *up,
                                 f32 x1, f32 y1, f32 x2, f32 y2,
                                 f32 thickness, u32 colour)
{
    Vtx *verts = gfxAllocateVertices(4);
    Col *cols  = gfxAllocateColours(1);
    cols[0].word = PD_BE32(colour);

    f32 dx = x2 - x1;
    f32 dy = y2 - y1;
    f32 len = sqrtf(dx * dx + dy * dy);

    f32 nx = 0.0f; // Normal X (thickness)
    f32 ny = 0.0f; // Normal Y (thickness)
    f32 ex = 0.0f; // Extension X (to fill the corners)
    f32 ey = 0.0f; // Extension Y (to fill the corners)

    if (len > 0.0001f) {
        // The perpendicular vector (defines the thickness)
        nx = -(dy / len) * thickness;
        ny =  (dx / len) * thickness;

        // The parallel vector (extends the line at its ends)
        ex = (dx / len) * thickness;
        ey = (dy / len) * thickness;
    }

    f32 px[4], py[4];

    // Move the starting point back by the thickness amount (-ex, -ey)
    px[0] = (x1 - ex) + nx; py[0] = (y1 - ey) + ny;
    px[3] = (x1 - ex) - nx; py[3] = (y1 - ey) - ny;

    // Move the end point forward by the thickness amount (+ex, +ey)
    px[1] = (x2 + ex) + nx; py[1] = (y2 + ey) + ny;
    px[2] = (x2 + ex) - nx; py[2] = (y2 + ey) - ny;

    for (int i = 0; i < 4; i++) {
        verts[i].x = (s16)roundf(cx + right->x * px[i] + up->x * py[i]);
        verts[i].y = (s16)roundf(cy + right->y * px[i] + up->y * py[i]);
        verts[i].z = (s16)roundf(cz + right->z * px[i] + up->z * py[i]);
        verts[i].colour = 0;
    }

    gSPColor(gdl++, cols, 1);
    gSPVertex(gdl++, verts, 4, 0);
    gSPTri2(gdl++, 0, 1, 2, 0, 2, 3);

    return gdl;
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

// =============================================================================
// Target Lock-On Box (3D Hologram)
// =============================================================================
Gfx *sightDrawTargetBox(Gfx *gdl, struct trackedprop *trackedprop, s32 textid, s32 time)
{
    s32 viewleft = viGetViewLeft() / g_ScaleX;
    s32 viewtop = viGetViewTop();
    s32 viewwidth = viGetViewWidth() / g_ScaleX;
    s32 viewheight = viGetViewHeight();
    s32 viewright = viewleft + viewwidth - 1;
    s32 viewbottom = viewtop + viewheight - 1;

    if (time > TICKS(512)) time = TICKS(512);

    s32 boxleft = sightCalculateBoxBound(trackedprop->x1 / g_ScaleX, viewleft, time, TICKS(80));
    s32 boxtop = sightCalculateBoxBound(trackedprop->y1, viewtop, time, TICKS(80));
    s32 boxright = sightCalculateBoxBound(trackedprop->x2 / g_ScaleX, viewright, time, TICKS(80));
    s32 boxbottom = sightCalculateBoxBound(trackedprop->y2, viewbottom, time, TICKS(80));

    if (trackedprop->prop) {
        u32 colour = sightIsPropFriendly(trackedprop->prop) ? 0x000ff60 : 0xff000060;

        struct player *player = g_Vars.currentplayer;
        struct coord campos = player->cam_pos;

        f32 cx = trackedprop->prop->pos.x - campos.x;
        f32 cy = trackedprop->prop->pos.y - campos.y;
        f32 cz = trackedprop->prop->pos.z - campos.z;

        if (trackedprop->prop->type == PROPTYPE_CHR || trackedprop->prop->type == PROPTYPE_PLAYER) {
            cy -= 15.0f;
        }

        f32 dist = sqrtf(cx*cx + cy*cy + cz*cz);

        if (dist > 0.0001f) {
            Mtxf *viewmtx = camGetWorldToScreenMtxf();
            struct coord right = {viewmtx->m[0][0], viewmtx->m[1][0], viewmtx->m[2][0]};
            struct coord up    = {viewmtx->m[0][1], viewmtx->m[1][1], viewmtx->m[2][1]};

            f32 rl = sqrtf(right.x*right.x + right.y*right.y + right.z*right.z);
            if (rl > 0.0001f) { right.x /= rl; right.y /= rl; right.z /= rl; }
            f32 ul = sqrtf(up.x*up.x + up.y*up.y + up.z*up.z);
            if (ul > 0.0001f) { up.x /= ul; up.y /= ul; up.z /= ul; }

            f32 nx = cx / dist; f32 ny = cy / dist; f32 nz = cz / dist;

            f32 render_cx = cx - (nx * 10.0f);
            f32 render_cy = cy - (ny * 10.0f);
            f32 render_cz = cz - (nz * 10.0f);

            f32 pixel_scale = dist * 0.0035f;
            f32 half_w = ((f32)(boxright - boxleft) * pixel_scale) / 2.0f;
            f32 half_h = ((f32)(boxbottom - boxtop) * pixel_scale) / 2.0f;

            if (trackedprop->prop->type == PROPTYPE_CHR || trackedprop->prop->type == PROPTYPE_PLAYER) {
                half_h *= 1.5f;
            }

            gdl = sightSetup3DRenderState(gdl, 1.0f);

            gdl = sightDrawLineWorld3D(gdl, render_cx, render_cy, render_cz, &right, &up, -half_w, half_h, half_w, half_h, 1.0f, colour);
            gdl = sightDrawLineWorld3D(gdl, render_cx, render_cy, render_cz, &right, &up, -half_w, -half_h, half_w, -half_h, 1.0f, colour);
            gdl = sightDrawLineWorld3D(gdl, render_cx, render_cy, render_cz, &right, &up, -half_w, -half_h, -half_w, half_h, 1.0f, colour);
            gdl = sightDrawLineWorld3D(gdl, render_cx, render_cy, render_cz, &right, &up, half_w, -half_h, half_w, half_h, 1.0f, colour);

            gdl = text0f153628(gdl);
        }

        if (textid != 0) {
            bool textonscreen = !(boxright < viewleft || boxright > viewright || boxtop > viewbottom || boxbottom < viewtop);
            gdl = text0f153838(gdl);

            if (textonscreen) {
                s32 x = boxright + 3; s32 y = boxtop + 3;
                if (textid < 7) {
                    char label[] = {'1', '\n', '\0'}; label[0] = textid + 0x2f;
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
    }
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
// =============================================================================
// Sight with Inertia (sightDrawDelayedAimer)
// =============================================================================
Gfx *sightDrawDelayedAimer(Gfx *gdl, s32 x, s32 y, s32 radius, s32 cornergap, u32 colour)
{

    s32 boxx; s32 boxy; s32 i; f32 dist_2d; f32 accel;

    static f32 xpos = 160; static f32 ypos = 120;
    static f32 xspeed = 0; static f32 yspeed = 0;

    for (i = 0; i < g_Vars.lvupdate60; i++) {
        dist_2d = x - xpos;
        if (dist_2d > 0.5f || dist_2d < -0.5f) {
            accel = dist_2d * 0.05f;
            if (accel > PALUPF(2.0f)) accel = PALUPF(2.0f);
            if (accel < -PALUPF(2.0f)) accel = -PALUPF(2.0f);
            if (accel > xspeed) accel = PALUPF(0.05f);
            else if (accel < xspeed) accel = -PALUPF(0.05f);
            else accel = 0.0f;
            xspeed += accel;
            if (xspeed > PALUPF(2.0f)) xspeed = PALUPF(2.0f);
            if (xspeed < -PALUPF(2.0f)) xspeed = -PALUPF(2.0f);
            xpos += xspeed;
        } else { xpos = x; xspeed = 0.0f; }

        dist_2d = y - ypos;
        if (dist_2d > 0.5f || dist_2d < -0.5f) {
            accel = dist_2d * 0.05f;
            if (accel > PALUPF(2.0f)) accel = PALUPF(2.0f);
            if (accel < -PALUPF(2.0f)) accel = -PALUPF(2.0f);
            if (yspeed < accel) accel = PALUPF(0.05f);
            else if (accel < yspeed) accel = -PALUPF(0.05f);
            else accel = 0.0f;
            yspeed += accel;
            if (yspeed > PALUPF(2.0f)) yspeed = PALUPF(2.0f);
            if (yspeed < -PALUPF(2.0f)) yspeed = -PALUPF(2.0f);
            ypos += yspeed;
        } else { ypos = y; yspeed = 0.0f; }
    }

    boxx = (s32)xpos; boxy = (s32)ypos;

    struct player *player = g_Vars.currentplayer;
    struct hand *rhand = &player->hands[g_currentCrosshairHand];
    const struct coord *dotpos = &rhand->dotpos;
    struct coord campos = player->cam_pos;

    if (dotpos->x == 0.0f && dotpos->y == 0.0f && dotpos->z == 0.0f) return gdl;

    f32 dx = dotpos->x - campos.x;
    f32 dy = dotpos->y - campos.y;
    f32 dz = dotpos->z - campos.z;
    f32 dist_3d = sqrtf(dx*dx + dy*dy + dz*dz);
    if (dist_3d < 0.0001f) return gdl;

    f32 nx = dx / dist_3d; f32 ny = dy / dist_3d; f32 nz = dz / dist_3d;
    Mtxf *viewmtx = camGetWorldToScreenMtxf();
    struct coord right = {viewmtx->m[0][0], viewmtx->m[1][0], viewmtx->m[2][0]};
    struct coord up    = {viewmtx->m[0][1], viewmtx->m[1][1], viewmtx->m[2][1]};
    f32 rl = sqrtf(right.x*right.x + right.y*right.y + right.z*right.z);
    if (rl > 0.0001f) { right.x /= rl; right.y /= rl; right.z /= rl; }
    f32 ul = sqrtf(up.x*up.x + up.y*up.y + up.z*up.z);
    if (ul > 0.0001f) { up.x /= ul; up.y /= ul; up.z /= ul; }

    f32 render_dist = dist_3d;
    if (render_dist < 100.0f) render_dist = 100.0f;

    f32 cx = nx * render_dist;
    f32 cy = ny * render_dist;
    f32 cz = nz * render_dist;

    f32 base_scale = render_dist * 0.005f;
    static const f32 sizeScale[5] = { 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };
    s32 idx = g_PlayerExtCfg[0].crosshairsize;
    if (idx < 0) idx = 0; if (idx > 4) idx = 4;

    f32 current_fov = g_Vars.currentplayer->fovy;
    if (current_fov <= 0.0f) current_fov = 98.0f;

    static f32 vr_base_fov = 0.0f;
    if (current_fov > vr_base_fov) vr_base_fov = current_fov;
    f32 zoom_scale = 1.0f;
    if (vr_base_fov > 0.0f) {
        zoom_scale = tanf(current_fov * 3.14159265f / 360.0f) / tanf(vr_base_fov * 3.14159265f / 360.0f);
    }

    f32 final_scale = base_scale * sizeScale[idx] * zoom_scale;

    f32 scaled_r = (f32)radius * final_scale;
    f32 scaled_c = (f32)cornergap * final_scale;
    f32 scaled_t = 0.4f * final_scale;

    f32 x_diff = (boxx - x) * final_scale;
    f32 y_diff = -(boxy - y) * final_scale;

    f32 cx_d = cx + right.x * x_diff + up.x * y_diff;
    f32 cy_d = cy + right.y * x_diff + up.y * y_diff;
    f32 cz_d = cz + right.z * x_diff + up.z * y_diff;

    gdl = sightSetup3DRenderState(gdl, 1.0f);

    f32 dot_r = 1.5f * final_scale;
    gdl = sightDrawLineWorld3D(gdl, cx, cy, cz, &right, &up, -dot_r, 0.0f, dot_r, 0.0f, dot_r * 2.0f, SIGHT_COLOUR);
    gdl = sightDrawLineWorld3D(gdl, cx_d, cy_d, cz_d, &right, &up, -scaled_r, -scaled_r, -scaled_r,  scaled_r, scaled_t, colour);
    gdl = sightDrawLineWorld3D(gdl, cx_d, cy_d, cz_d, &right, &up,  scaled_r, -scaled_r,  scaled_r,  scaled_r, scaled_t, colour);
    gdl = sightDrawLineWorld3D(gdl, cx_d, cy_d, cz_d, &right, &up, -scaled_r, -scaled_r,  scaled_r, -scaled_r, scaled_t, colour);
    gdl = sightDrawLineWorld3D(gdl, cx_d, cy_d, cz_d, &right, &up, -scaled_r,  scaled_r,  scaled_r,  scaled_r, scaled_t, colour);
    gdl = sightDrawLineWorld3D(gdl, cx_d, cy_d, cz_d, &right, &up, -scaled_r, -scaled_r, -scaled_r, -scaled_c, scaled_t, colour);
    gdl = sightDrawLineWorld3D(gdl, cx_d, cy_d, cz_d, &right, &up, -scaled_r,  scaled_c, -scaled_r,  scaled_r, scaled_t, colour);
    gdl = sightDrawLineWorld3D(gdl, cx_d, cy_d, cz_d, &right, &up,  scaled_r, -scaled_r,  scaled_r, -scaled_c, scaled_t, colour);
    gdl = sightDrawLineWorld3D(gdl, cx_d, cy_d, cz_d, &right, &up,  scaled_r,  scaled_c,  scaled_r,  scaled_r, scaled_t, colour);
    gdl = sightDrawLineWorld3D(gdl, cx_d, cy_d, cz_d, &right, &up, -scaled_r, -scaled_r, -scaled_c, -scaled_r, scaled_t, colour);
    gdl = sightDrawLineWorld3D(gdl, cx_d, cy_d, cz_d, &right, &up,  scaled_c, -scaled_r,  scaled_r, -scaled_r, scaled_t, colour);
    gdl = sightDrawLineWorld3D(gdl, cx_d, cy_d, cz_d, &right, &up, -scaled_r,  scaled_r, -scaled_c,  scaled_r, scaled_t, colour);
    gdl = sightDrawLineWorld3D(gdl, cx_d, cy_d, cz_d, &right, &up,  scaled_c,  scaled_r,  scaled_r,  scaled_r, scaled_t, colour);

    gdl = text0f153628(gdl);
    return gdl;
}





// =============================================================================
// 3D Helper: Draws the "Default" sight (uses the Helpers)
// =============================================================================
static Gfx *sightDrawAimerWorld3D(Gfx *gdl, f32 crossx, f32 crossy, s32 radius, s32 cornergap, u32 colour)
{
    struct sightprojdata p;
    sightCalculate3DProjection(crossx, crossy, &p);

    f32 base_scale = p.render_dist * 0.005f;
    static const f32 sizeScale[5] = { 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };
    s32 idx = g_PlayerExtCfg[0].crosshairsize;
    if (idx < 0) idx = 0; if (idx > 4) idx = 4;

    f32 final_scale = base_scale * sizeScale[idx] * 5.0f * p.zoom_scale;

    f32 scaled_r = (f32)radius * final_scale;
    f32 scaled_c = (f32)cornergap * final_scale;
    f32 scaled_t = 0.4f * final_scale;
    f32 scaled_out = 150.0f * final_scale;

    gdl = sightSetup3DRenderState(gdl, 0.2f); // Utilisation du Helper !

    u32 outer_colour = SIGHT_COLOUR;
    gdl = sightDrawLineWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up, -scaled_out, 0.0f, -scaled_r, 0.0f, scaled_t, outer_colour);
    gdl = sightDrawLineWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up,  scaled_r, 0.0f,  scaled_out, 0.0f, scaled_t, outer_colour);
    gdl = sightDrawLineWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up, 0.0f, -scaled_out, 0.0f, -scaled_r, scaled_t, outer_colour);
    gdl = sightDrawLineWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up, 0.0f,  scaled_r, 0.0f,  scaled_out, scaled_t, outer_colour);

    gdl = sightDrawLineWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up, -scaled_r, -scaled_r, -scaled_r,  scaled_r, scaled_t, colour);
    gdl = sightDrawLineWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up,  scaled_r, -scaled_r,  scaled_r,  scaled_r, scaled_t, colour);
    gdl = sightDrawLineWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up, -scaled_r, -scaled_r,  scaled_r, -scaled_r, scaled_t, colour);
    gdl = sightDrawLineWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up, -scaled_r,  scaled_r,  scaled_r,  scaled_r, scaled_t, colour);

    gdl = sightDrawLineWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up, -scaled_r, -scaled_r, -scaled_r, -scaled_c, scaled_t, colour);
    gdl = sightDrawLineWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up, -scaled_r,  scaled_c, -scaled_r,  scaled_r, scaled_t, colour);
    gdl = sightDrawLineWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up,  scaled_r, -scaled_r,  scaled_r, -scaled_c, scaled_t, colour);
    gdl = sightDrawLineWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up,  scaled_r,  scaled_c,  scaled_r,  scaled_r, scaled_t, colour);
    gdl = sightDrawLineWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up, -scaled_r, -scaled_r, -scaled_c, -scaled_r, scaled_t, colour);
    gdl = sightDrawLineWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up,  scaled_c, -scaled_r,  scaled_r, -scaled_r, scaled_t, colour);
    gdl = sightDrawLineWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up, -scaled_r,  scaled_r, -scaled_c,  scaled_r, scaled_t, colour);
    gdl = sightDrawLineWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up,  scaled_c,  scaled_r,  scaled_r,  scaled_r, scaled_t, colour);

    gdl = text0f153628(gdl);
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
                gdl = sightDrawAimerWorld3D(gdl, crossx, crossy, radius, cornergap, colour);
            }
            break;

        case SIGHTTRACKTYPE_DEFAULT:
            if (sighton) {
                if (g_Vars.currentplayer->lookingatprop.prop == NULL
                    || (g_LookingAtPropHandMask & (1 << g_currentCrosshairHand)) == 0) {
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
                        gdl = sightDrawAimerWorld3D(gdl, crossx, crossy, radius, cornergap, colour);
                        break;
                    case 1:
                        gdl = sightDrawDelayedAimer(gdl, x, y, radius * 2, cornergap * 2, colour);
                        break;
                }
            }
            break;

        case SIGHTTRACKTYPE_BETASCANNER:
            if (sighton) {
                s32 textx;
                s32 texty;

                if (g_Vars.currentplayer->lookingatprop.prop == NULL
                    || (g_LookingAtPropHandMask & (1 << g_currentCrosshairHand)) == 0) {
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

                gdl = sightDrawAimerWorld3D(gdl, crossx, crossy, radius, cornergap, colour);

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
                if (g_Vars.currentplayer->lookingatprop.prop == NULL
                    || (g_LookingAtPropHandMask & (1 << g_currentCrosshairHand)) == 0) {
                    colour    = SIGHT_COLOUR;
                    radius    = 8;
                    cornergap = 5;
                } else {
                    colour    = sightIsPropFriendly(NULL) ? 0x0000ff60 : 0xff000060;
                    radius    = 6;
                    cornergap = 3;
                }
                gdl = sightDrawAimerWorld3D(gdl, crossx, crossy, radius, cornergap, colour);
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
                if (g_Vars.currentplayer->lookingatprop.prop == NULL
                    || (g_LookingAtPropHandMask & (1 << g_currentCrosshairHand)) == 0) {
                    colour    = SIGHT_COLOUR;
                    radius    = 8;
                    cornergap = 5;
                } else {
                    colour    = sightIsPropFriendly(NULL) ? 0x0000ff60 : 0xff000060;
                    radius    = 6;
                    cornergap = 3;
                }
                gdl = sightDrawAimerWorld3D(gdl, crossx, crossy, radius, cornergap, colour);
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


// =============================================================================
// CLASSIC Sight (sightDrawClassic) - Uses the Helpers
// =============================================================================
Gfx *sightDrawClassic(Gfx *gdl, bool sighton, f32 crossx, f32 crossy)
{
    if (!sighton) return gdl;

    struct player *player = g_Vars.currentplayer;
    struct hand *rhand = &player->hands[g_currentCrosshairHand];

    struct sightprojdata p;
    sightCalculate3DProjection(crossx, crossy, &p);

    // Replace the physical "Classic" coordinates (does not always point to the center of the screen)
    if (!p.is_sky) {
        if (p.dist > 0.0001f) {
            p.cx = (p.dx / p.dist) * p.render_dist * 5.0f;
            p.cy = (p.dy / p.dist) * p.render_dist * 5.0f;
            p.cz = (p.dz / p.dist) * p.render_dist * 5.0f;
        } else {
            p.cx = 0.0f; p.cy = 0.0f; p.cz = 0.0f;
        }
    }

    f32 dir_x, dir_y, dir_z;
    if (p.is_sky) {
        dir_x = p.aim_x; dir_y = p.aim_y; dir_z = p.aim_z;
    } else {
        dir_x = rhand->dotpos.x - rhand->muzzlepos.x;
        dir_y = rhand->dotpos.y - rhand->muzzlepos.y;
        dir_z = rhand->dotpos.z - rhand->muzzlepos.z;
    }
    f32 rlen = sqrtf(dir_x*dir_x + dir_y*dir_y + dir_z*dir_z);
    if (rlen > 0.0001f) {
        dir_x /= rlen; dir_y /= rlen; dir_z /= rlen;
    } else {
        // Fallback to the front of the camera
        struct coord fwd;
        fwd.x = p.up.y * p.right.z - p.up.z * p.right.y;
        fwd.y = p.up.z * p.right.x - p.up.x * p.right.z;
        fwd.z = p.up.x * p.right.y - p.up.y * p.right.x;
        dir_x = fwd.x; dir_y = fwd.y; dir_z = fwd.z;
    }

    struct coord up_world = {0.0f, 1.0f, 0.0f};
    if (fabsf(dir_y) > 0.99f) { up_world.x = 1.0f; up_world.y = 0.0f; up_world.z = 0.0f; }

    struct coord quad_right, quad_up;
    quad_right.x = up_world.y*dir_z - up_world.z*dir_y;
    quad_right.y = up_world.z*dir_x - up_world.x*dir_z;
    quad_right.z = up_world.x*dir_y - up_world.y*dir_x;
    f32 qrl = sqrtf(quad_right.x*quad_right.x + quad_right.y*quad_right.y + quad_right.z*quad_right.z);
    if (qrl > 0.0001f) { quad_right.x /= qrl; quad_right.y /= qrl; quad_right.z /= qrl; }

    quad_up.x = dir_y*quad_right.z - dir_z*quad_right.y;
    quad_up.y = dir_z*quad_right.x - dir_x*quad_right.z;
    quad_up.z = dir_x*quad_right.y - dir_y*quad_right.x;

    f32 half = 0.030f * p.render_dist;
    if (half < 8.0f) half = 8.0f;

    static const f32 sizeScale[5] = { 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };
    s32 idx = g_PlayerExtCfg[0].crosshairsize;
    if (idx < 0) idx = 0; if (idx > 4) idx = 4;

    half = half * sizeScale[idx] * 5.0f * p.zoom_scale;

    // "Classic" requires its own manual initialization due to the texture's transparency (gDPSetTextureLOD...)
    Mtxf world_mtx; mtx4LoadIdentity(&world_mtx);
    mtx00015be0(camGetWorldToScreenMtxf(), &world_mtx);
    world_mtx.m[3][0] = 0.0f; world_mtx.m[3][1] = 0.0f; world_mtx.m[3][2] = 0.0f;
    mtx00015f88(0.2f, &world_mtx);

    gDPSetColorDither(gdl++,    G_CD_DISABLE);
    gDPSetTexturePersp(gdl++,   G_TP_PERSP);
    gDPSetAlphaCompare(gdl++,   G_AC_NONE);
    gDPSetTextureLOD(gdl++,     G_TL_TILE);
    gDPSetTextureFilter(gdl++,  G_TF_POINT);
    gDPSetTextureConvert(gdl++, G_TC_FILT);
    gDPSetTextureLUT(gdl++,     G_TT_NONE);
    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++,      G_CYC_1CYCLE);
    gDPSetRenderMode(gdl++,     G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
    gSPClearGeometryMode(gdl++, G_CULL_BOTH);

    texSelect(&gdl, &g_TexGeCrosshairConfigs[0], 2, 0, 0, 1, NULL);

    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetCombineMode(gdl++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(gdl++, 0, 0, 0xff, 0xff, 0xff, 0x7f);

    Mtxf *mtx_tex = gfxAllocateMatrix(); mtxF2L(&world_mtx, mtx_tex);
    gSPMatrix(gdl++, osVirtualToPhysical(mtx_tex), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    Vtx *vtx = gfxAllocateVertices(4);
    vtx[0].colour = vtx[1].colour = vtx[2].colour = vtx[3].colour = 0;
    vtx[0].s = 0;       vtx[0].t = 0;
    vtx[1].s = 32 << 5; vtx[1].t = 0;
    vtx[2].s = 32 << 5; vtx[2].t = 32 << 5;
    vtx[3].s = 0;       vtx[3].t = 32 << 5;

    crosshair3dSetVtx(&vtx[0], p.cx, p.cy, p.cz, &quad_right, &quad_up, -1.0f, -1.0f, half);
    crosshair3dSetVtx(&vtx[1], p.cx, p.cy, p.cz, &quad_right, &quad_up,  1.0f, -1.0f, half);
    crosshair3dSetVtx(&vtx[2], p.cx, p.cy, p.cz, &quad_right, &quad_up,  1.0f,  1.0f, half);
    crosshair3dSetVtx(&vtx[3], p.cx, p.cy, p.cz, &quad_right, &quad_up, -1.0f,  1.0f, half);

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


// =============================================================================
// 3D Helper: Draws a Skedar triangle aligned with the screen (Flat Billboarding)
// =============================================================================
static Gfx *sightDrawSkedarTriangleWorld3D(Gfx *gdl, f32 cx, f32 cy, f32 cz,
                                           const struct coord *right, const struct coord *up,
                                           f32 offset_x, f32 offset_y, s32 dir, f32 scale, u32 colour)
{
    f32 pts_x[3];
    f32 pts_y[3];
    Vtx *vertices = gfxAllocateVertices(3);
    Col *colours = gfxAllocateColours(2);

    // Local definition of the triangle (same as the original game)
    switch (dir) {
        case DIR_UP:
            pts_x[0] = offset_x;      pts_y[0] = offset_y;
            pts_x[1] = offset_x + 5;  pts_y[1] = offset_y + 7;
            pts_x[2] = offset_x - 5;  pts_y[2] = offset_y + 7;
            break;
        case DIR_DOWN:
            pts_x[0] = offset_x;      pts_y[0] = offset_y;
            pts_x[1] = offset_x + 5;  pts_y[1] = offset_y - 7;
            pts_x[2] = offset_x - 5;  pts_y[2] = offset_y - 7;
            break;
        case DIR_LEFT:
            pts_x[0] = offset_x;      pts_y[0] = offset_y;
            pts_x[1] = offset_x + 7;  pts_y[1] = offset_y - 5;
            pts_x[2] = offset_x + 7;  pts_y[2] = offset_y + 5;
            break;
        case DIR_RIGHT:
            pts_x[0] = offset_x;      pts_y[0] = offset_y;
            pts_x[1] = offset_x - 7;  pts_y[1] = offset_y - 5;
            pts_x[2] = offset_x - 7;  pts_y[2] = offset_y + 5;
            break;
        default:
            return gdl;
    }

    if (colour == COLOUR_DARKRED && sightIsPropFriendly(NULL)) {
        colour = COLOUR_DARKBLUE;
    }

    // Skedar triangle transparency gradient
#define RGBA(r, g, b, a) (((r) & 0xff) << 24 | ((g) & 0xff) << 16 | ((b) & 0xff) << 8 | ((a) & 0xff))
    colours[0].word = PD_BE32(colour);
    colours[1].word = PD_BE32(RGBA((colour >> 24) & 0xff, (colour >> 16) & 0xff, (colour >> 8) & 0xff, 0x08));

    for (int i = 0; i < 3; i++) {
        vertices[i].x = (s16)roundf(cx + right->x * (pts_x[i] * scale) + up->x * (-pts_y[i] * scale));
        vertices[i].y = (s16)roundf(cy + right->y * (pts_x[i] * scale) + up->y * (-pts_y[i] * scale));
        vertices[i].z = (s16)roundf(cz + right->z * (pts_x[i] * scale) + up->z * (-pts_y[i] * scale));

        vertices[i].colour = (i == 0) ? 0 : 4;
    }

    gSPColor(gdl++, colours, 2);
    gSPVertex(gdl++, vertices, 3, 0);
    gSPTri1(gdl++, 0, 1, 2);

    return gdl;
}


// =============================================================================
// SKEDAR Sight (sightDrawSkedar) - Uses the Helpers
// =============================================================================
Gfx *sightDrawSkedar(Gfx *gdl, bool sighton, f32 crossx, f32 crossy)
{
    if (!sighton) return gdl;

    struct sightprojdata p;
    sightCalculate3DProjection(crossx, crossy, &p);

    s32 paddingy = p.viewheight / 4;
    s32 paddingx = p.viewwidth / 4;

    if (!p.hasprop) g_Vars.currentplayer->sighttimer240 = 0;

    f32 world_pixel_scale = (p.render_dist * 5.0f) * p.fov_scale;
    f32 base_scale = p.render_dist * 0.005f;
    static const f32 sizeScale[5] = { 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };
    s32 idx = g_PlayerExtCfg[0].crosshairsize;
    if (idx < 0) idx = 0; if (idx > 4) idx = 4;

    f32 final_scale = base_scale * sizeScale[idx] * 5.0f * p.zoom_scale;

    gdl = sightSetup3DRenderState(gdl, 0.2f);

    f32 frac = 1.0f;
    if (p.hasprop && g_Vars.currentplayer->sighttimer240 < TICKS(48)) {
        frac = g_Vars.currentplayer->sighttimer240 / TICKS(48.0f);
    }

    f32 trix1 = p.fx, trix2 = p.fx, triy1 = p.fy, triy2 = p.fy;
    u32 colour; u8 dir;

    if (!p.hasprop) {
        colour = COLOUR_LIGHTRED;
        if (p.fx < p.viewleft + paddingx) { dir = DIR_LEFT; trix1 = p.viewleft + paddingx; }
        else if (p.fx > p.viewright - paddingx) { dir = DIR_RIGHT; trix1 = p.viewright - paddingx; }
        else { dir = DIR_DOWN; colour = COLOUR_GREEN; }
        triy1 = p.viewtop + paddingy;
    } else {
        if (g_Vars.currentplayer->sighttimer240 < TICKS(48)) {
            colour = COLOUR_LIGHTRED; dir = DIR_DOWN;
            triy1 = (p.fy - p.viewtop - paddingy - 2.0f) * frac + p.viewtop + paddingy;
        } else {
            colour = COLOUR_DARKRED; dir = DIR_DOWN; triy1 = p.fy - 2.0f;
        }
    }
    gdl = sightDrawSkedarTriangleWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up, trix1 - p.fx, triy1 - p.fy, dir, world_pixel_scale, colour);

    if (!p.hasprop) {
        colour = COLOUR_LIGHTRED;
        if (dir == DIR_DOWN) { colour = COLOUR_GREEN; dir = DIR_UP; }
        triy1 = p.viewbottom - paddingy;
    } else {
        if (g_Vars.currentplayer->sighttimer240 < TICKS(48)) {
            colour = COLOUR_LIGHTRED; dir = DIR_UP;
            triy1 = (p.fy - p.viewbottom + paddingy + 2.0f) * frac + p.viewbottom - paddingy;
        } else {
            colour = COLOUR_DARKRED; dir = DIR_UP; triy1 = p.fy + 2.0f;
        }
    }
    gdl = sightDrawSkedarTriangleWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up, trix1 - p.fx, triy1 - p.fy, dir, world_pixel_scale, colour);

    if (!p.hasprop) {
        colour = COLOUR_LIGHTRED;
        if (p.fy < p.viewtop + paddingy) { dir = DIR_UP; triy2 = p.viewtop + paddingy; }
        else if (p.fy > p.viewbottom - paddingy) { dir = DIR_DOWN; triy2 = p.viewbottom - paddingy; }
        else { dir = DIR_LEFT; colour = COLOUR_GREEN; }
        trix2 = p.viewright - paddingx;
    } else {
        if (g_Vars.currentplayer->sighttimer240 < TICKS(48)) {
            colour = COLOUR_LIGHTRED; dir = DIR_LEFT;
            trix2 = (p.fx - p.viewright + paddingx + 2.0f) * frac + p.viewright - paddingx;
        } else {
            colour = COLOUR_DARKRED; dir = DIR_LEFT; trix2 = p.fx + 2.0f;
        }
    }
    gdl = sightDrawSkedarTriangleWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up, trix2 - p.fx, triy2 - p.fy, dir, world_pixel_scale, colour);

    if (!p.hasprop) {
        colour = COLOUR_LIGHTRED;
        if (dir == DIR_LEFT) { colour = COLOUR_GREEN; dir = DIR_RIGHT; }
        trix2 = p.viewleft + paddingx;
    } else {
        if (g_Vars.currentplayer->sighttimer240 < TICKS(48)) {
            colour = COLOUR_LIGHTRED; dir = DIR_RIGHT;
            trix2 = (p.fx - p.viewleft - paddingx - 2.0f) * frac + p.viewleft + paddingx;
        } else {
            colour = COLOUR_DARKRED; dir = DIR_RIGHT; trix2 = p.fx - 2.0f;
        }
    }
    gdl = sightDrawSkedarTriangleWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up, trix2 - p.fx, triy2 - p.fy, dir, world_pixel_scale, colour);

    if (!p.hasprop || g_Vars.currentplayer->sighttimer240 < TICKS(48)) {
        colour = p.hasprop ? COLOUR_LIGHTRED : COLOUR_GREEN;
        gdl = sightDrawSkedarTriangleWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up,  0.0f, -2.0f, DIR_DOWN,  final_scale, colour);
        gdl = sightDrawSkedarTriangleWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up,  0.0f,  2.0f, DIR_UP,    final_scale, colour);
        gdl = sightDrawSkedarTriangleWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up, -2.0f,  0.0f, DIR_RIGHT, final_scale, colour);
        gdl = sightDrawSkedarTriangleWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up,  2.0f,  0.0f, DIR_LEFT,  final_scale, colour);
    }

    gdl = text0f153628(gdl);
    return gdl;
}


// =============================================================================
// Sniper Zoom Overlay (sightDrawZoom) - Corrected Thickness and Length
// =============================================================================
Gfx *sightDrawZoom(Gfx *gdl, bool sighton, f32 crossx, f32 crossy)
{
    struct sightprojdata p;
    sightCalculate3DProjection(crossx, crossy, &p);

    s32 viewhalfwidth = p.viewwidth >> 1;
    s32 viewhalfheight = p.viewheight >> 1;

    f32 maxfovy = currentPlayerGetGunZoomFov();
    f32 zoominfovy = g_Vars.currentplayer->zoominfovy;
    f32 frac = 1.0f;

    s32 weaponnum = g_Vars.currentplayer->hands[HAND_RIGHT].gset.weaponnum;
    u8 showzoomrange = optionsGetShowZoomRange(g_Vars.currentplayerstats->mpindex)
                       && optionsGetSightOnScreen(g_Vars.currentplayerstats->mpindex);

    f32 vr_aspect_squish = 0.65f;

    s32 availableleft = viewhalfwidth - 48;
    s32 availableright = viewhalfwidth - 49;
    s32 availableabove = (viewhalfheight - 10) * vr_aspect_squish;
    s32 availablebelow = (viewhalfheight - 10) * vr_aspect_squish;

    s32 cornerwidth = (viewhalfwidth >> 1) - 60;
    s32 cornerheight = (viewhalfheight >> 1) - 22;

    if (maxfovy == 0.0f || maxfovy == 60.0f) {
        if (weaponnum != WEAPON_SNIPERRIFLE) showzoomrange = false;
    } else {
        frac = maxfovy / zoominfovy;
    }

    if (showzoomrange) {
        if (frac < 0.2f) { cornerwidth *= 0.2f; cornerheight *= 0.2f; }
        else { cornerwidth *= frac; cornerheight *= frac; }

        if (PLAYERCOUNT() >= 2) cornerheight *= 2;
        if (cornerwidth < 5) cornerwidth = 5;
        if (cornerheight < 5) cornerheight = 5;

        f32 marginleft = viewhalfwidth - availableleft * frac;
        f32 marginright = viewhalfwidth - availableright * frac;
        f32 marginbottom = viewhalfheight - availablebelow * frac;
        f32 margintop = viewhalfheight - availableabove * frac;

        f32 boxleft = sightGetAdjustedX(p.viewleft + marginleft);
        f32 boxright = sightGetAdjustedX(p.viewright - marginright);
        f32 boxtop = p.viewtop + margintop;
        f32 boxbottom = p.viewbottom - marginbottom;

        if (cornerwidth > boxright - boxleft) cornerwidth = boxright - boxleft;
        if (cornerheight > boxbottom - boxtop) cornerheight = boxbottom - boxtop;

        struct coord fwd;
        fwd.x = p.up.y * p.right.z - p.up.z * p.right.y;
        fwd.y = p.up.z * p.right.x - p.up.x * p.right.z;
        fwd.z = p.up.x * p.right.y - p.up.y * p.right.x;
        f32 fl = sqrtf(fwd.x*fwd.x + fwd.y*fwd.y + fwd.z*fwd.z);
        if (fl > 0.0001f) { fwd.x /= fl; fwd.y /= fl; fwd.z /= fl; }

        f32 render_dist = 1500.0f;

        f32 cx = fwd.x * render_dist * 5.0f;
        f32 cy = fwd.y * render_dist * 5.0f;
        f32 cz = fwd.z * render_dist * 5.0f;

        f32 scx = sightGetAdjustedX(p.viewleft + viewhalfwidth);
        f32 scy = p.viewtop + viewhalfheight;

        f32 world_pixel_scale = (render_dist * 5.0f) * p.fov_scale;

        f32 l = (boxleft - scx) * world_pixel_scale;
        f32 r = (boxright - scx) * world_pixel_scale;
        f32 t = -(boxtop - scy) * world_pixel_scale;
        f32 b = -(boxbottom - scy) * world_pixel_scale;


        f32 cw = (cornerwidth * 0.5f) * world_pixel_scale;
        f32 ch = (cornerheight * 0.5f) * world_pixel_scale;
        f32 scaled_t = 0.4f * world_pixel_scale;

        gdl = sightSetup3DRenderState(gdl, 0.2f);

        u32 colour = SIGHT_COLOUR;

        gdl = sightDrawLineWorld3D(gdl, cx, cy, cz, &p.right, &p.up, l, t, l + cw, t, scaled_t, colour);
        gdl = sightDrawLineWorld3D(gdl, cx, cy, cz, &p.right, &p.up, l, t, l, t - ch, scaled_t, colour);
        gdl = sightDrawLineWorld3D(gdl, cx, cy, cz, &p.right, &p.up, r - cw, t, r, t, scaled_t, colour);
        gdl = sightDrawLineWorld3D(gdl, cx, cy, cz, &p.right, &p.up, r, t, r, t - ch, scaled_t, colour);
        gdl = sightDrawLineWorld3D(gdl, cx, cy, cz, &p.right, &p.up, l, b, l + cw, b, scaled_t, colour);
        gdl = sightDrawLineWorld3D(gdl, cx, cy, cz, &p.right, &p.up, l, b + ch, l, b, scaled_t, colour);
        gdl = sightDrawLineWorld3D(gdl, cx, cy, cz, &p.right, &p.up, r - cw, b, r, b, scaled_t, colour);
        gdl = sightDrawLineWorld3D(gdl, cx, cy, cz, &p.right, &p.up, r, b + ch, r, b, scaled_t, colour);

        gdl = text0f153628(gdl);
    }

    gdl = sightDrawDefault(gdl, sighton, crossx, crossy);
    return gdl;
}


// =============================================================================
// MAIAN Sight (sightDrawMaian) - Uses the Helpers
// =============================================================================
Gfx *sightDrawMaian(Gfx *gdl, bool sighton, f32 crossx, f32 crossy)
{
    if (!sighton) return gdl;

    u32 colour = sightIsPropFriendly(NULL) ? 0x0000ff60 : 0xff000060;

    struct sightprojdata p;
    sightCalculate3DProjection(crossx, crossy, &p);

    f32 world_pixel_scale = (p.render_dist * 5.0f) * p.fov_scale;
    f32 base_scale = p.render_dist * 0.005f;
    static const f32 sizeScale[5] = { 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };
    s32 idx = g_PlayerExtCfg[0].crosshairsize;
    if (idx < 0) idx = 0; if (idx > 4) idx = 4;

    f32 final_scale = base_scale * sizeScale[idx] * 5.0f * p.zoom_scale;

    gdl = sightSetup3DRenderState(gdl, 0.2f);

    f32 offset_x[4], offset_y[4];
    offset_x[0] = (p.viewleft + (p.viewwidth >> 1)) - p.fx;
    offset_y[0] = (p.viewtop + 10) - p.fy;
    offset_x[1] = (p.viewleft + (p.viewwidth >> 1)) - p.fx;
    offset_y[1] = (p.viewbottom - 10) - p.fy;
    offset_x[2] = (p.viewleft + 48) - p.fx;
    offset_y[2] = (p.viewtop + (p.viewheight >> 1)) - p.fy;
    offset_x[3] = (p.viewright - 49) - p.fx;
    offset_y[3] = (p.viewtop + (p.viewheight >> 1)) - p.fy;

    Vtx *vertices = gfxAllocateVertices(8);
    Col *colours  = gfxAllocateColours(2);
    colours[0].word = PD_BE32(0x00ff000f);
    colours[1].word = PD_BE32(p.hasprop ? colour : 0x00ff0044);

    for (int i = 0; i < 4; ++i) {
        vertices[i].x = (s16)roundf(p.cx + p.right.x * (offset_x[i] * world_pixel_scale) + p.up.x * (-offset_y[i] * world_pixel_scale));
        vertices[i].y = (s16)roundf(p.cy + p.right.y * (offset_x[i] * world_pixel_scale) + p.up.y * (-offset_y[i] * world_pixel_scale));
        vertices[i].z = (s16)roundf(p.cz + p.right.z * (offset_x[i] * world_pixel_scale) + p.up.z * (-offset_y[i] * world_pixel_scale));
        vertices[i].colour = 0;
    }

    f32 center_ox[4] = { -4.0f, 4.0f, 4.0f, -4.0f };
    f32 center_oy[4] = { -4.0f, -4.0f, 4.0f, 4.0f };
    for (int i = 0; i < 4; ++i) {
        vertices[i+4].x = (s16)roundf(p.cx + p.right.x * (center_ox[i] * final_scale) + p.up.x * (-center_oy[i] * final_scale));
        vertices[i+4].y = (s16)roundf(p.cy + p.right.y * (center_ox[i] * final_scale) + p.up.y * (-center_oy[i] * final_scale));
        vertices[i+4].z = (s16)roundf(p.cz + p.right.z * (center_ox[i] * final_scale) + p.up.z * (-center_oy[i] * final_scale));
        vertices[i+4].colour = 4;
    }

    gSPColor(gdl++, colours, 2);
    gSPVertex(gdl++, vertices, 8, 0);
    gSPTri4(gdl++, 0, 4, 5, 5, 3, 6, 7, 6, 1, 4, 7, 2);

    f32 s4 = 4.0f * final_scale;
    f32 thickness = 0.4f * final_scale;

    gdl = sightDrawLineWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up, -s4, -s4, -s4,  s4, thickness, SIGHT_COLOUR);
    gdl = sightDrawLineWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up,  s4, -s4,  s4,  s4, thickness, SIGHT_COLOUR);
    gdl = sightDrawLineWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up, -s4, -s4,  s4, -s4, thickness, SIGHT_COLOUR);
    gdl = sightDrawLineWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up, -s4,  s4,  s4,  s4, thickness, SIGHT_COLOUR);

    gdl = text0f153628(gdl);
    return gdl;
}


// =============================================================================
// SIMPLE CROSSHAIR (sightDrawTarget) - Uses the Helpers
// =============================================================================
Gfx *sightDrawTarget(Gfx *gdl, f32 crossx, f32 crossy)
{
    static u32 var80070f9c = 0x00ff00ff;
    static u32 var80070fa0 = 0x00ff0011;

    mainOverrideVariable("sout", &var80070f9c);
    mainOverrideVariable("sin",  &var80070fa0);

    struct sightprojdata p;
    sightCalculate3DProjection(crossx, crossy, &p);

    f32 base_scale = p.render_dist * 0.005f;
    s32 idx = SIGHT_SCALE;
    if (idx < 0) idx = 0; if (idx > 4) idx = 4;
    static const f32 sizeScale[5] = { 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };

    f32 final_scale = base_scale * sizeScale[idx] * 5.0f * p.zoom_scale;
    f32 thickness = 0.4f * final_scale;
    f32 sc = 2.5f * final_scale;
    f32 p1 = 1.0f * sc; f32 p2 = 2.0f * sc; f32 p3 = 3.0f * sc;

    u32 colour = SIGHT_COLOUR;

    gdl = sightSetup3DRenderState(gdl, 0.2f);

    if (SIGHT_SCALE == 0) {
        gdl = sightDrawLineWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up, -thickness, 0.0f, thickness, 0.0f, thickness, colour);
    } else {
        gdl = sightDrawLineWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up,  p1, 0.0f,  p3, 0.0f, thickness, colour);
        gdl = sightDrawLineWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up, -p3, 0.0f, -p1, 0.0f, thickness, colour);
        gdl = sightDrawLineWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up, 0.0f,  p1, 0.0f,  p3, thickness, colour);
        gdl = sightDrawLineWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up, 0.0f, -p3, 0.0f, -p1, thickness, colour);
        gdl = sightDrawLineWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up,  p1, 0.0f,  p2, 0.0f, thickness, colour);
        gdl = sightDrawLineWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up, -p2, 0.0f, -p1, 0.0f, thickness, colour);
        gdl = sightDrawLineWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up, 0.0f,  p1, 0.0f,  p2, thickness, colour);
        gdl = sightDrawLineWorld3D(gdl, p.cx, p.cy, p.cz, &p.right, &p.up, 0.0f, -p2, 0.0f, -p1, thickness, colour);
    }

    gdl = text0f153628(gdl);
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

    bool is_aiming = vr_button_L_grip && sighton;

    if ((PLAYER_EXTCFG().crosshairhideunlessaiming) && is_aiming) {

            gdl = sightDrawTarget(gdl, crossx, crossy);
            g_ScaleX = 1;
            return gdl;
    }

        switch (L_sight) {
            case SIGHT_DEFAULT:
                gdl = sightDrawDefault(gdl, is_aiming && optionsGetSightOnScreen(
                        g_Vars.currentplayerstats->mpindex), crossx, crossy);
                break;
            case SIGHT_CLASSIC:
                gdl = sightDrawClassic(gdl, is_aiming && optionsGetSightOnScreen(
                        g_Vars.currentplayerstats->mpindex), crossx, crossy);
                break;
            case SIGHT_2:
                gdl = sightDrawType2(gdl, is_aiming && optionsGetSightOnScreen(
                        g_Vars.currentplayerstats->mpindex), crossx, crossy);
                break;
            case SIGHT_3:
                gdl = sightDrawDefault(gdl, is_aiming && optionsGetSightOnScreen(
                        g_Vars.currentplayerstats->mpindex), crossx, crossy);
                break;
            case SIGHT_SKEDAR:
                gdl = sightDrawSkedar(gdl, is_aiming && optionsGetSightOnScreen(
                        g_Vars.currentplayerstats->mpindex), crossx, crossy);
                break;
            case SIGHT_ZOOM:
                if ((PLAYER_EXTCFG().hidesightzoom) || (PLAYER_EXTCFG().crosshairhideunlessaiming)){
                    break;
                }
                gdl = sightDrawZoom(gdl, is_aiming && optionsGetSightOnScreen(
                        g_Vars.currentplayerstats->mpindex), crossx, crossy);
                break;
            case SIGHT_MAIAN:
                gdl = sightDrawMaian(gdl, is_aiming && optionsGetSightOnScreen(
                        g_Vars.currentplayerstats->mpindex), crossx, crossy);
                break;
            default:
                gdl = sightDrawDefault(gdl, is_aiming && optionsGetSightOnScreen(
                        g_Vars.currentplayerstats->mpindex), crossx, crossy);
                break;
            case SIGHT_NONE:
                break;
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

    bool is_aiming = vr_button_R_grip && sighton;

    if ((PLAYER_EXTCFG().crosshairhideunlessaiming) && is_aiming) {
        gdl = sightDrawTarget(gdl, crossx, crossy);
        g_ScaleX = 1;

        // ===== VR Left-hand crosshair =====
        if (vr_LeftCrossValid && sight != SIGHT_NONE) {
            gdl = sightDrawLeftHand(gdl, sight, sighton);
        }

        return gdl;
    }


    switch (sight) {
        case SIGHT_DEFAULT:
            gdl = sightDrawDefault(gdl, is_aiming && optionsGetSightOnScreen(
                    g_Vars.currentplayerstats->mpindex), crossx, crossy);
            break;
        case SIGHT_CLASSIC:
            gdl = sightDrawClassic(gdl, is_aiming && optionsGetSightOnScreen(
                    g_Vars.currentplayerstats->mpindex), crossx, crossy);
            break;
        case SIGHT_2:
            gdl = sightDrawType2(gdl, is_aiming && optionsGetSightOnScreen(
                    g_Vars.currentplayerstats->mpindex), crossx, crossy);
            break;
        case SIGHT_3:
            gdl = sightDrawDefault(gdl, is_aiming && optionsGetSightOnScreen(
                    g_Vars.currentplayerstats->mpindex), crossx, crossy);
            break;
        case SIGHT_SKEDAR:
            gdl = sightDrawSkedar(gdl, is_aiming && optionsGetSightOnScreen(
                    g_Vars.currentplayerstats->mpindex), crossx, crossy);
            break;
        case SIGHT_ZOOM:
            if ((PLAYER_EXTCFG().hidesightzoom) || (PLAYER_EXTCFG().crosshairhideunlessaiming)){
                break;
            }
            gdl = sightDrawZoom(gdl, is_aiming && optionsGetSightOnScreen(
                    g_Vars.currentplayerstats->mpindex), crossx, crossy);
            break;
        case SIGHT_MAIAN:
            gdl = sightDrawMaian(gdl, is_aiming && optionsGetSightOnScreen(
                    g_Vars.currentplayerstats->mpindex), crossx, crossy);
            break;
        default:
            gdl = sightDrawDefault(gdl, is_aiming && optionsGetSightOnScreen(
                    g_Vars.currentplayerstats->mpindex), crossx, crossy);
            break;
        case SIGHT_NONE:
            break;
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
