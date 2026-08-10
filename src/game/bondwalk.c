#include <ultra64.h>

#include <math.h>
#include <game/bondgrab.h>
#include "../../port/vr/vr_input.h"

#include "constants.h"
#include "game/bondmove.h"
#include "game/bondwalk.h"
#include "game/cheats.h"
#include "game/chraction.h"
#include "game/debug.h"
#include "game/footstep.h"
#include "game/game_006900.h"
#include "game/chr.h"
#include "game/prop.h"
#include "game/propsnd.h"
#include "game/objectives.h"
#include "game/bondgun.h"
#include "game/player.h"
#include "game/inv.h"
#include "game/bondhead.h"
#include "game/playermgr.h"
#include "game/propobj.h"

#include "game/camera.h"  //VR

#include "game/quaternion.h"

#include "bss.h"
#include "lib/model.h"
#include "lib/snd.h"
#include "lib/rng.h"
#include "lib/mtx.h"
#include "lib/anim.h"
#include "lib/collision.h"
#include "data.h"
#include "types.h"
#ifndef PLATFORM_N64
extern f32 fabsf(f32);
#endif


#include "../vr/vr_openxr.h"
#include "../vr/vr_log.h"



// Original vectors (initialized only once)
const struct coord original_look = {0.0f, 0.0f, 1.0f };
const struct coord original_up = {0.0f, -1.0f, 0.0f };
XrQuaternionf vr_joy_rot_Q = { 0, 0, 0, 1 };
float vr_joyAccum = 0.0f;
float VrYawRot = 0.0f;

extern bool vr_is_duel;
extern float vr_player_angle;
extern float VrSetWorldScale;
static f32 sVrEyeheightClamped = 150.0f; // default for starting game

// VR height settings. See vr_settings.h for what they mean; the menu writes
// them and pd-vr.ini persists them. The default is an average adult's standing
// eye height, which is a little under average stature.
float VrPlayerHeight = 160.0f;
bool VrMatchCharacterHeight = false;

// Highest head height we will believe, in cm. Only there to swallow tracking
// spikes; a real head never gets near it.
#define VR_MAX_HEAD_CM 250.0f

extern void vr_align_with_game_angle(float target_game_angle);
static bool vr_hoverbike_can_mount = false;

VrEyeheightMode sVrEyeheightMode = VR_EYEHEIGHT_STAND;

extern bool VrSeatedMode;

bool is_grabbing_mode = false;
bool is_hoverbike_mode = false;

int VrStickClickToCrouch = false;

void vr_rotate_vector_by_quaternion(struct coord* v, const XrQuaternionf* q) {
    float qx = q->x, qy = q->y, qz = q->z, qw = q->w;
    float vx = v->x, vy = v->y, vz = v->z;

    // q * v: quaternion * vector multiplication (as a pure quaternion)
    float t0 = qw * vx + qy * vz - qz * vy;
    float t1 = qw * vy + qz * vx - qx * vz;
    float t2 = qw * vz + qx * vy - qy * vx;
    float t3 = -qx * vx - qy * vy - qz * vz;

    // (q * v) * q^-1: multiplication by the conjugate
    v->x = t0 * qw - t3 * qx - t1 * qz + t2 * qy;
    v->y = t1 * qw - t3 * qy - t2 * qx + t0 * qz;
    v->z = t2 * qw - t3 * qz - t0 * qy + t1 * qx;
}


#define VR_JOY_TURN_SPEED   (120.0f / 360.0f / 60.0f) // continuous turn speed (deg/frame @60hz)
#define VR_SNAP_ANGLE_DEG   45.0f    // snap turn amplitude (30 or 45 degrees is common)
#define VR_SNAP_ACTIVATE    0.9f     // trigger threshold
#define VR_SNAP_DEACTIVATE  0.0001f     // re-arm threshold (hysteresis)
static bool vr_snapArmed = true;
int VrUseSnapTurn = false;   // true = snap turn, false = continuous turn


void joy_for_vr(void) {
    XrVector2f rightThumbstick;

    if (get_2d_input(1, "thumbstick", &rightThumbstick)) {

        if (VrUseSnapTurn && g_Vars.currentplayer->bondmovemode != MOVEMODE_GRAB) {
            // --- SNAP TURN MODE ---

            // Re-arm as soon as the stick returns close to center
            if (fabsf(rightThumbstick.x) < VR_SNAP_DEACTIVATE) {
                vr_snapArmed = true;
            }

            // Trigger a snap if armed and threshold exceeded
            if (vr_snapArmed && fabsf(rightThumbstick.x) > VR_SNAP_ACTIVATE) {
                float direction = (rightThumbstick.x > 0.0f) ? 1.0f : -1.0f;
                vr_joyAccum -= direction * (VR_SNAP_ANGLE_DEG / 360.0f);
                vr_snapArmed = false; // lock until stick returns to center
            }

        } else {
            // --- CONTINUOUS ROTATION MODE ---

            if (fabsf(rightThumbstick.x) > 0.1f) {
                vr_joyAccum -= rightThumbstick.x * VR_JOY_TURN_SPEED * g_Vars.lvupdate60freal;
            }

            // Keep snap re-armed to avoid a "ghost snap" if switching back later
            vr_snapArmed = true;
        }
    }

    float totalAngle = vr_joyAccum * 3.14159265f * 2.0f;
    vr_joy_rot_Q.x = 0.0f;
    vr_joy_rot_Q.y = sinf(totalAngle * 0.5f);
    vr_joy_rot_Q.z = 0.0f;
    vr_joy_rot_Q.w = cosf(totalAngle * 0.5f);
}



static struct coord lastVRHeadPos = { 0.0f, 0.0f, 0.0f };

// --- Roomscale: the head is allowed to leave the body ------------------------
//
// prop->pos is the BODY: the collision cylinder, what the AI shoots at, what
// owns the rooms. The camera is the HEAD. Physically leaning moves only the
// head; the body is dragged after it only once you have leaned far enough that
// you are plainly walking rather than leaning, and it is that drag -- never the
// lean -- that gets collision-tested against the body cylinder. This is what
// lets you put your head over a balcony rail: the rail stops the body, which
// was not going anywhere, and your head goes over it.
//
// Two offsets, because they are different questions. The PHYSICAL one is where
// your head actually is and never refuses anything -- if it did, motion spent
// pushing into a wall would be lost, and stepping back out would leave you
// permanently displaced from your own body. The SHOWN one is where the camera
// is allowed to be, the physical one clamped against solid geometry. Press into
// a wall and they diverge; step back and they converge again on their own.
//
// Both kept in PLAY space (the headset's own frame) so that turning, stick or
// snap, rotates them along with the world for free. Everything else wants game
// space, which is a rotation away and recomputed every frame.
static struct coord sVrHeadOffsetPlay = { 0.0f, 0.0f, 0.0f };   // physical
static struct coord sVrShownOffsetPlay = { 0.0f, 0.0f, 0.0f };  // clamped
static struct coord sVrHeadOffset = { 0.0f, 0.0f, 0.0f };       // clamped, game space

// How far the two have diverged, i.e. how far into something solid your real
// head has gone. Drives the comfort fade.
static f32 sVrHeadClipDist = 0.0f;

// The body chases the head every tick -- roomscale walking is the body walking,
// and it should be under you whenever it can be. Distance between the two opens
// up only where the body physically cannot follow (a rail across its waist, a
// wall at its shoulder) and closes again the moment the way is clear.
//
// Capped per tick so that catching up after a long lean stays a series of small
// collision-tested steps rather than one long jump, which could skip through
// thin geometry: cdExamCylMove02 only tests the destination. The cap is
// invisible in play -- the camera is body + offset, so moving the body and
// shrinking the offset by the same vector leaves the view exactly where it was.
#define VR_BODY_CATCHUP_MAX 10.0f

// How far your head gets past what the game will follow before the view is
// fully black. This is comfort feedback, not occlusion: the probe has already
// stopped the view at the wall, and this says so rather than letting you
// wonder why the world stopped moving with you.
#define VR_HEAD_FADE_START 2.0f
#define VR_HEAD_FADE_FULL  10.0f

// The probe that keeps your head out of solid geometry. A head, not a body: it
// is a short slab at eye level, so a waist-high rail passes underneath it and
// stops blocking the view, which is the entire point.
#define VR_HEAD_PROBE_RADIUS 12.0f
#define VR_HEAD_PROBE_YMAX   10.0f
#define VR_HEAD_PROBE_YMIN  -10.0f

// No single frame can legitimately hold this much head motion (25 cm at 90 Hz
// is 22 m/s). Anything bigger is a resync rather than a movement: a tracking
// glitch, or returning from a mode that does not run this code -- a bike ride
// leaves the last sample stale for as long as you were riding. Swallow it
// instead of teleporting the body.
#define VR_MAX_HEAD_STEP 25.0f

static void vr_rotate_vector_by_quaternion_inv(struct coord *v, const XrQuaternionf *q) {
    XrQuaternionf inv;
    inv.x = -q->x;
    inv.y = -q->y;
    inv.z = -q->z;
    inv.w = q->w;
    vr_rotate_vector_by_quaternion(v, &inv);
}

// Camera position: the body plus wherever your head has leaned to.
void vr_get_head_pos(struct coord *out) {
    out->x = g_Vars.currentplayer->prop->pos.x + sVrHeadOffset.x;
    out->y = g_Vars.currentplayer->prop->pos.y;
    out->z = g_Vars.currentplayer->prop->pos.z + sVrHeadOffset.z;
}

// 0 while the view is following your head, rising to 1 as your real head goes
// further into something the view will not follow you into.
f32 vr_get_head_clip_frac(void) {
    f32 frac;

    if (sVrHeadClipDist <= VR_HEAD_FADE_START) {
        return 0.0f;
    }

    frac = (sVrHeadClipDist - VR_HEAD_FADE_START) / (VR_HEAD_FADE_FULL - VR_HEAD_FADE_START);

    return frac > 1.0f ? 1.0f : frac;
}

// Can the head sit this far from the body without ending up inside something
// solid?
//
// Solid means "you cannot see through it". GEOFLAG_BLOCK_SIGHT is the level's
// own answer to that question -- it is what the AI's line-of-sight tests use,
// so every real wall carries it. The invisible barriers these levels put along
// balconies and rails to keep the player in do not, because you can plainly
// see through them, and blocking the head on those is what stopped leaning
// over a rail from working. Geometry you can see through, your head goes
// through; geometry you cannot, it does not.
static bool vr_head_offset_is_clear(f32 offx, f32 offz) {
    struct collision collisions[21];
    struct coord headPos;
    RoomNum rooms[8];
    s32 types, i;

    headPos.x = g_Vars.currentplayer->prop->pos.x + offx;
    headPos.y = g_Vars.currentplayer->prop->pos.y;
    headPos.z = g_Vars.currentplayer->prop->pos.z + offz;

    types = g_Vars.bondcollisions ? CDTYPE_ALL : CDTYPE_BG;

    func0f065e74(&g_Vars.currentplayer->prop->pos, g_Vars.currentplayer->prop->rooms, &headPos, rooms);
    bmoveFindEnteredRoomsByPos(g_Vars.currentplayer, &headPos, rooms);

    collisions[0].geo = NULL;

    propSetPerimEnabled(g_Vars.currentplayer->prop, false);

    cdCollectGeoForCylMove(&headPos, VR_HEAD_PROBE_RADIUS, rooms, types, GEOFLAG_WALL,
                           true, VR_HEAD_PROBE_YMAX, VR_HEAD_PROBE_YMIN, collisions, 20);

    propSetPerimEnabled(g_Vars.currentplayer->prop, true);

    for (i = 0; collisions[i].geo != NULL; i++) {
        if (collisions[i].geo->flags & GEOFLAG_BLOCK_SIGHT) {
            return false;
        }
    }

    // The test above only asks whether the head has ended up inside something,
    // which a wall thinner than a lean is wide enough to step straight over:
    // past its far side the destination is clear again and the view pops out
    // the other side. Asking for line of sight from the body catches the
    // crossing itself rather than the landing, and it is the same question --
    // sight -- so invisible barriers stay invisible to it too.
    if (!cdTestLos05(&g_Vars.currentplayer->prop->pos, g_Vars.currentplayer->prop->rooms,
                     &headPos, rooms, types, GEOFLAG_BLOCK_SIGHT)) {
        return false;
    }

    return true;
}

// Drag the body, full-body cylinder against the world as usual. Returns whether
// it actually moved, so the caller can leave what was blocked in the offset.
static bool vr_try_move_body(f32 dx, f32 dz) {
    struct coord newPos;
    RoomNum rooms[8];
    f32 radius, ymax, ymin;
    s32 types, result;

    if (dx == 0.0f && dz == 0.0f) {
        return false;
    }

    newPos.x = g_Vars.currentplayer->prop->pos.x + dx;
    newPos.y = g_Vars.currentplayer->prop->pos.y;
    newPos.z = g_Vars.currentplayer->prop->pos.z + dz;

    playerGetBbox(g_Vars.currentplayer->prop, &radius, &ymax, &ymin);
    types = g_Vars.bondcollisions ? CDTYPE_ALL : CDTYPE_BG;

    func0f065e74(&g_Vars.currentplayer->prop->pos, g_Vars.currentplayer->prop->rooms, &newPos, rooms);
    bmoveFindEnteredRoomsByPos(g_Vars.currentplayer, &newPos, rooms);

    propSetPerimEnabled(g_Vars.currentplayer->prop, false);

    result = cdExamCylMove02(&g_Vars.currentplayer->prop->pos, &newPos, radius, rooms, types, true,
                             ymax - g_Vars.currentplayer->prop->pos.y,
                             ymin - g_Vars.currentplayer->prop->pos.y);

    propSetPerimEnabled(g_Vars.currentplayer->prop, true);

    if (result != CDRESULT_NOCOLLISION) {
        return false;
    }

    g_Vars.currentplayer->prop->pos.x = newPos.x;
    g_Vars.currentplayer->prop->pos.z = newPos.z;

    return true;
}

void vr_player_pos(void) {
    struct coord physical;
    struct coord shownPrev;
    struct coord delta;
    f32 dist;

    if (g_Vars.currentplayer->isdead
        || g_Vars.tickmode != TICKMODE_NORMAL
        || vr_is_duel
        || g_Vars.currentplayer->bondmovemode != MOVEMODE_WALK) {
        // Riding, grabbing, dead or in a cutscene: glue the head back onto the
        // body and just keep tracking, so the next live frame sees no jump.
        sVrHeadOffsetPlay.x = 0.0f;
        sVrHeadOffsetPlay.z = 0.0f;
        sVrShownOffsetPlay.x = 0.0f;
        sVrShownOffsetPlay.z = 0.0f;
        sVrHeadOffset.x = 0.0f;
        sVrHeadOffset.z = 0.0f;
        sVrHeadClipDist = 0.0f;

        lastVRHeadPos.x = gHeadPos.x;
        lastVRHeadPos.y = gHeadPos.y;
        lastVRHeadPos.z = gHeadPos.z;
        return;
    }

    delta.x = gHeadPos.x - lastVRHeadPos.x;
    delta.y = 0.0f;   // vertical is mapped absolutely by the eye height; it must not move the body
    delta.z = gHeadPos.z - lastVRHeadPos.z;

    if (fabsf(delta.x) > VR_MAX_HEAD_STEP || fabsf(delta.z) > VR_MAX_HEAD_STEP) {
        delta.x = 0.0f;
        delta.z = 0.0f;
    }

    lastVRHeadPos.x = gHeadPos.x;
    lastVRHeadPos.y = gHeadPos.y;
    lastVRHeadPos.z = gHeadPos.z;

    // The physical offset takes the motion unconditionally: it is a record of
    // where your head is, and nothing in the game may overrule that.
    sVrHeadOffsetPlay.x += delta.x;
    sVrHeadOffsetPlay.z += delta.z;

    // Both offsets into game space. Rotating them separately is the same as
    // rotating their sum, and both land in the world's current orientation.
    physical.x = sVrHeadOffsetPlay.x;
    physical.y = 0.0f;
    physical.z = sVrHeadOffsetPlay.z;
    vr_rotate_vector_by_quaternion(&physical, &vr_joy_rot_Q);

    shownPrev.x = sVrShownOffsetPlay.x;
    shownPrev.y = 0.0f;
    shownPrev.z = sVrShownOffsetPlay.z;
    vr_rotate_vector_by_quaternion(&shownPrev, &vr_joy_rot_Q);

    // 1. Lean. The body's cylinder gets no say in where your head goes; the
    //    only thing that may stop it is the head probe hitting something you
    //    cannot see through, and then only along the axis that hit.
    if (vr_head_offset_is_clear(physical.x, physical.z)) {
        sVrHeadOffset.x = physical.x;
        sVrHeadOffset.z = physical.z;
    } else {
        sVrHeadOffset.x = shownPrev.x;
        sVrHeadOffset.z = shownPrev.z;

        if (vr_head_offset_is_clear(physical.x, sVrHeadOffset.z)) {
            sVrHeadOffset.x = physical.x;
        }
        if (vr_head_offset_is_clear(sVrHeadOffset.x, physical.z)) {
            sVrHeadOffset.z = physical.z;
        }
    }

    // 2. Walk. The body chases the head all the way back to zero, every tick.
    //    It gets under you whenever it can; what it cannot do stays in the
    //    offset, so the gap only ever exists because something solid is in the
    //    way, and it closes by itself as soon as that stops being true.
    dist = sqrtf(physical.x * physical.x + physical.z * physical.z);

    if (dist > 0.0f) {
        f32 rate = (dist > VR_BODY_CATCHUP_MAX) ? (VR_BODY_CATCHUP_MAX / dist) : 1.0f;
        f32 dragx = physical.x * rate;
        f32 dragz = physical.z * rate;

        // The body moving under both offsets leaves the head where it is in the
        // world, so whatever it manages comes off both of them equally.
        if (vr_try_move_body(dragx, dragz)) {
            physical.x -= dragx;
            physical.z -= dragz;
            sVrHeadOffset.x -= dragx;
            sVrHeadOffset.z -= dragz;
        } else {
            // Blocked: slide along the surface instead of stopping dead, and
            // consume only the component that actually moved.
            if (vr_try_move_body(dragx, 0.0f)) {
                physical.x -= dragx;
                sVrHeadOffset.x -= dragx;
            }
            if (vr_try_move_body(0.0f, dragz)) {
                physical.z -= dragz;
                sVrHeadOffset.z -= dragz;
            }
        }
    }

    // What is left between where your head is and where the game will show it.
    {
        f32 clipx = physical.x - sVrHeadOffset.x;
        f32 clipz = physical.z - sVrHeadOffset.z;
        sVrHeadClipDist = sqrtf(clipx * clipx + clipz * clipz);
    }

    // Store both back in play space, where turning can rotate them for free.
    sVrHeadOffsetPlay.x = physical.x;
    sVrHeadOffsetPlay.y = 0.0f;
    sVrHeadOffsetPlay.z = physical.z;
    vr_rotate_vector_by_quaternion_inv(&sVrHeadOffsetPlay, &vr_joy_rot_Q);

    sVrShownOffsetPlay.x = sVrHeadOffset.x;
    sVrShownOffsetPlay.y = 0.0f;
    sVrShownOffsetPlay.z = sVrHeadOffset.z;
    vr_rotate_vector_by_quaternion_inv(&sVrShownOffsetPlay, &vr_joy_rot_Q);
}



void vr_player_rot(void) {


    if(g_Vars.currentplayer->isdead == false
       && g_Vars.tickmode == TICKMODE_NORMAL
       && g_Vars.currentplayer->bondmovemode != MOVEMODE_BIKE
       && g_Vars.currentplayer->bondmovemode != MOVEMODE_GRAB
       && !vr_is_duel
       && !is_grabbing_mode
       && !is_hoverbike_mode
            ) {

        joy_for_vr();

        struct coord look = original_look;
        struct coord up = original_up;

        vr_rotate_vector_by_quaternion(&look, &vr_HMD_rot_Q);
        vr_rotate_vector_by_quaternion(&up, &vr_HMD_rot_Q);

        vr_rotate_vector_by_quaternion(&look, &vr_joy_rot_Q);
        vr_rotate_vector_by_quaternion(&up, &vr_joy_rot_Q);

        g_Vars.currentplayer->bond2.unk1c.x = look.x;
        g_Vars.currentplayer->bond2.unk1c.y = -look.y;
        g_Vars.currentplayer->bond2.unk1c.z = look.z;

        g_Vars.currentplayer->bond2.unk28.x = up.x;
        g_Vars.currentplayer->bond2.unk28.y = -up.y;
        g_Vars.currentplayer->bond2.unk28.z = up.z;

        VrYawRot = atan2f(g_Vars.currentplayer->bond2.unk1c.x,
                          g_Vars.currentplayer->bond2.unk1c.z);


        g_Vars.currentplayer->vv_theta = -VrYawRot * 180.0f / M_PI;




// Not needed anymore ?
//    float horiz = sqrtf(g_Vars.currentplayer->bond2.unk1c.x * g_Vars.currentplayer->bond2.unk1c.x +
//    		g_Vars.currentplayer->bond2.unk1c.z * g_Vars.currentplayer->bond2.unk1c.z);
//    float pitchRad = atan2f(-g_Vars.currentplayer->bond2.unk1c.y, horiz);
//    g_Vars.currentplayer->vv_verta = pitchRad * 180.0f / M_PI;

    }
}

void vr_special_rot_mode(void)
{
    /* Hoverbike */
    is_hoverbike_mode = g_Vars.currentplayer->bondmovemode == MOVEMODE_BIKE;

    /* Flying crate / stretcher */
    is_grabbing_mode = g_Vars.currentplayer->bondmovemode == MOVEMODE_GRAB;

    if (!is_hoverbike_mode && !is_grabbing_mode) {
        return;
    }

    /*
     * On entering the hoverbike, realign the VR reference frame once.
     * Do not do this for MOVEMODE_GRAB.
     */
    if (is_hoverbike_mode && vr_hoverbike_can_mount) {
        vr_align_with_game_angle(0.0f);
        vr_hoverbike_can_mount = false;
    }

    /*
     * MOVEMODE_GRAB:
     *
     * bond2.unk00 must NOT be used as a yaw reference here.
     * The rotation of the crate/stretcher is computed by bondgrab.c
     * around g_Vars.currentplayer->vvtheta.
     *
     * We therefore fall back to the normal vr_player_rot() pipeline,
     * which keeps a stable HMD view + joystick rotation.
     */
    if (is_grabbing_mode) {
        struct coord look = original_look;
        struct coord up = original_up;
        float horiz;
        float pitchRad;

        joy_for_vr();

        vr_rotate_vector_by_quaternion(&look, &vr_HMD_rot_Q);
        vr_rotate_vector_by_quaternion(&up, &vr_HMD_rot_Q);

        vr_rotate_vector_by_quaternion(&look, &vr_joy_rot_Q);
        vr_rotate_vector_by_quaternion(&up, &vr_joy_rot_Q);

        g_Vars.currentplayer->bond2.unk1c.x = look.x;
        g_Vars.currentplayer->bond2.unk1c.y = -look.y;
        g_Vars.currentplayer->bond2.unk1c.z = look.z;

        g_Vars.currentplayer->bond2.unk28.x = up.x;
        g_Vars.currentplayer->bond2.unk28.y = -up.y;
        g_Vars.currentplayer->bond2.unk28.z = up.z;

        VrYawRot = atan2f(
                g_Vars.currentplayer->bond2.unk1c.x,
                g_Vars.currentplayer->bond2.unk1c.z);

        g_Vars.currentplayer->vv_theta = -VrYawRot * 180.0f / M_PI;

        horiz = sqrtf(
                g_Vars.currentplayer->bond2.unk1c.x
                * g_Vars.currentplayer->bond2.unk1c.x
                + g_Vars.currentplayer->bond2.unk1c.z
                  * g_Vars.currentplayer->bond2.unk1c.z);

        pitchRad = atan2f(-g_Vars.currentplayer->bond2.unk1c.y, horiz);

        g_Vars.currentplayer->vv_verta = pitchRad * 180.0f / M_PI;

        return;
    }

    /*
     * MOVEMODE_BIKE:
     * Preserve the special behavior based on the vehicle's orientation.
     */
    if (is_hoverbike_mode) {
        struct coord look = original_look;
        struct coord up = original_up;
        float forwardlen2;
        float vehyaw;
        XrQuaternionf vehQuat;

        joy_for_vr();

        /*
         * Avoids atan2f(0, 0) and an indeterminate orientation if the
         * hoverbike temporarily has no valid horizontal forward vector.
         */
        forwardlen2 =
                g_Vars.currentplayer->bond2.unk00.x
                * g_Vars.currentplayer->bond2.unk00.x
                + g_Vars.currentplayer->bond2.unk00.z
                  * g_Vars.currentplayer->bond2.unk00.z;

        if (forwardlen2 <= 0.0001f) {
            return;
        }

        vehyaw = atan2f(
                g_Vars.currentplayer->bond2.unk00.x,
                g_Vars.currentplayer->bond2.unk00.z);

        vehQuat.x = 0.0f;
        vehQuat.y = sinf(vehyaw * 0.5f);
        vehQuat.z = 0.0f;
        vehQuat.w = cosf(vehyaw * 0.5f);

        vr_rotate_vector_by_quaternion(&look, &vr_HMD_rot_Q);
        vr_rotate_vector_by_quaternion(&up, &vr_HMD_rot_Q);

        vr_rotate_vector_by_quaternion(&look, &vehQuat);
        vr_rotate_vector_by_quaternion(&up, &vehQuat);

        g_Vars.currentplayer->bond2.unk1c.x = look.x;
        g_Vars.currentplayer->bond2.unk1c.y = -look.y;
        g_Vars.currentplayer->bond2.unk1c.z = look.z;

        g_Vars.currentplayer->bond2.unk28.x = up.x;
        g_Vars.currentplayer->bond2.unk28.y = -up.y;
        g_Vars.currentplayer->bond2.unk28.z = up.z;
    }
    else {
        vr_hoverbike_can_mount = true;
    }
}
//------------------------------------------------------------------------------------------



void bwalkInit(void)
{
    u32 prevmode = g_Vars.currentplayer->bondmovemode;
    s32 i;

    g_Vars.currentplayer->bondmovemode = MOVEMODE_WALK;
    g_Vars.currentplayer->bondonground = 0;
    g_Vars.currentplayer->tank = NULL;
    g_Vars.currentplayer->unk1af0 = NULL;
    g_Vars.currentplayer->bondonturret = false;

    g_Vars.currentplayer->swaypos = 0;
    g_Vars.currentplayer->swayoffset = 0;
    g_Vars.currentplayer->swaytarget = 0;
    g_Vars.currentplayer->swayoffset0 = 0;
    g_Vars.currentplayer->swayoffset2 = 0;

    g_Vars.currentplayer->bdeltapos.x = 0;
    g_Vars.currentplayer->bdeltapos.y = -0.0001f;
    g_Vars.currentplayer->bdeltapos.z = 0;

    g_Vars.currentplayer->isfalling = false;
    g_Vars.currentplayer->fallstart = 0;

    g_Vars.currentplayer->gunextraaimx = 0;
    g_Vars.currentplayer->gunextraaimy = 0;

    g_Vars.currentplayer->bondforcespeed.x = 0;
    g_Vars.currentplayer->bondforcespeed.y = 0;
    g_Vars.currentplayer->bondforcespeed.z = 0;

    if (prevmode != MOVEMODE_WALK && prevmode != MOVEMODE_CUTSCENE) {
        g_Vars.currentplayer->sumcrouch = 0;
        g_Vars.currentplayer->crouchheight = 0;
        g_Vars.currentplayer->crouchtime240 = 0;
        g_Vars.currentplayer->crouchfall = 0;
        g_Vars.currentplayer->crouchpos = CROUCHPOS_STAND;
        g_Vars.currentplayer->autocrouchpos = CROUCHPOS_STAND;
        g_Vars.currentplayer->crouchspeed = 0;
        g_Vars.currentplayer->crouchoffset = 0;

#if VERSION < VERSION_NTSC_1_0
        bwalkUpdateCrouchOffsetReal();
#endif

        g_Vars.currentplayer->guncloseroffset = 0;
    }

#if VERSION >= VERSION_NTSC_1_0
    bwalkUpdateCrouchOffsetReal();
#endif

    if (prevmode != MOVEMODE_GRAB && prevmode != MOVEMODE_WALK) {
        for (i = 0; i != 3; i++) {
            g_Vars.currentplayer->bondshotspeed.f[i] = 0;
        }

        g_Vars.currentplayer->speedsideways = 0;
        g_Vars.currentplayer->speedstrafe = 0;
        g_Vars.currentplayer->speedgo = 0;
        g_Vars.currentplayer->speedboost = 1;
        g_Vars.currentplayer->speedmaxtime60 = 0;
        g_Vars.currentplayer->speedforwards = 0;
        g_Vars.currentplayer->speedtheta = 0;
        g_Vars.currentplayer->speedthetacontrol = 0;
    }


    if (g_Vars.currentplayer->walkinitmove) {
        struct coord delta;
        mtx00016b58(&g_Vars.currentplayer->walkinitmtx,
                    0, 0, 0,
                    -g_Vars.currentplayer->bond2.unk1c.x, -g_Vars.currentplayer->bond2.unk1c.y, -g_Vars.currentplayer->bond2.unk1c.z,
                    g_Vars.currentplayer->bond2.unk28.x, g_Vars.currentplayer->bond2.unk28.y, g_Vars.currentplayer->bond2.unk28.z);
        g_Vars.currentplayer->walkinitt = 0;
        g_Vars.currentplayer->walkinitt2 = 0;
        g_Vars.currentplayer->walkinitstart.x = g_Vars.currentplayer->prop->pos.x;
        g_Vars.currentplayer->walkinitstart.y = g_Vars.currentplayer->prop->pos.y;
        g_Vars.currentplayer->walkinitstart.z = g_Vars.currentplayer->prop->pos.z;

        delta.x = g_Vars.currentplayer->walkinitpos.x - g_Vars.currentplayer->prop->pos.x;
        delta.y = 0;
        delta.z = g_Vars.currentplayer->walkinitpos.z - g_Vars.currentplayer->prop->pos.z;

        propSetPerimEnabled(g_Vars.currentplayer->hoverbike, false);
        bwalkCalculateNewPositionWithPush(&delta, 0, true, 0, CDTYPE_ALL);
        propSetPerimEnabled(g_Vars.currentplayer->hoverbike, true);
    }
    else if (prevmode != MOVEMODE_GRAB && prevmode != MOVEMODE_WALK) {
        g_Vars.currentplayer->moveinitspeed.x = 0;
        g_Vars.currentplayer->moveinitspeed.y = 0;
        g_Vars.currentplayer->moveinitspeed.z = 0;
    }
}

void bwalkSetSwayTargetf(f32 value) {
    g_Vars.currentplayer->swaytarget = value * 75.f;
}

void bwalkSetSwayTarget(s32 value)
{
    g_Vars.currentplayer->swaytarget = value * 75.0f;
}

void bwalkAdjustCrouchPos(s32 value)
{
    g_Vars.currentplayer->crouchpos += value;

    if (g_Vars.currentplayer->crouchpos < CROUCHPOS_SQUAT) {
        g_Vars.currentplayer->crouchpos = CROUCHPOS_SQUAT;
    }
    else if (g_Vars.currentplayer->crouchpos > CROUCHPOS_STAND) {
        g_Vars.currentplayer->crouchpos = CROUCHPOS_STAND;
    }
}

void bwalk0f0c3b38(struct coord* reltarget, struct defaultobj* obj)
{
    struct coord posunk;
    struct coord vector;
    struct coord tween;
    struct coord globalthinga;
    struct coord globalthingb;
    struct coord abstarget;

    abstarget.x = reltarget->x + g_Vars.currentplayer->prop->pos.x;
    abstarget.y = g_Vars.currentplayer->prop->pos.y;
    abstarget.z = reltarget->z + g_Vars.currentplayer->prop->pos.z;

#if VERSION >= VERSION_NTSC_1_0
    cdGetEdge(&globalthinga, &globalthingb, 223, "bondwalk.c");
#else
    cdGetEdge(&globalthinga, &globalthingb, 221, "bondwalk.c");
#endif

    vector.x = globalthingb.z - globalthinga.z;
    vector.y = 0;
    vector.z = globalthinga.x - globalthingb.x;

    if (vector.f[0] != 0 || vector.f[2] != 0) {
        guNormalize(&vector.x, &vector.y, &vector.z);
    }
    else {
        vector.z = 1;
    }

    func0f02e3dc(&globalthinga, &globalthingb, &abstarget, &vector, &posunk);

    tween.x = (abstarget.x - g_Vars.currentplayer->prop->pos.x) / g_Vars.lvupdate60freal;
    tween.y = 0;
    tween.z = (abstarget.z - g_Vars.currentplayer->prop->pos.z) / g_Vars.lvupdate60freal;

    func0f082e84(obj, &posunk, &vector, &tween, false);
}

/**
 * Attempt to move the current player up vertically by the given amount.
 *
 * Collision checks are done for the new location, and if successful the
 * player's positional values are updated.
 *
 * The function is called with amount = 0 when attempting to stand up from a
 * crouch, after increasing the player's bbox to the standing size.
 */
s32 bwalkTryMoveUpwards(f32 amount)
{
    bool result;
    struct coord newpos;
    RoomNum rooms[8];
    u32 stack;
    u32 types;
    f32 ymax;
    f32 ymin;
    f32 radius;

    if (g_Vars.currentplayer->floorflags & GEOFLAG_SLOPE) {
        g_Vars.enableslopes = false;
    }
    else {
        g_Vars.enableslopes = true;
    }

    newpos.x = g_Vars.currentplayer->prop->pos.x;
    newpos.y = g_Vars.currentplayer->prop->pos.y + amount;
    newpos.z = g_Vars.currentplayer->prop->pos.z;


    types = g_Vars.bondcollisions ? CDTYPE_ALL : CDTYPE_BG;

    playerGetBbox(g_Vars.currentplayer->prop, &radius, &ymax, &ymin);
    func0f065e74(&g_Vars.currentplayer->prop->pos, g_Vars.currentplayer->prop->rooms, &newpos, rooms);
    bmoveFindEnteredRoomsByPos(g_Vars.currentplayer, &newpos, rooms);
    propSetPerimEnabled(g_Vars.currentplayer->prop, false);

    ymin -= 0.1f;

    result = cdTestVolume(&newpos, radius, rooms, types, CHECKVERTICAL_YES,
                          ymax - g_Vars.currentplayer->prop->pos.y,
                          ymin - g_Vars.currentplayer->prop->pos.y);

    propSetPerimEnabled(g_Vars.currentplayer->prop, true);

    if (result == CDRESULT_NOCOLLISION) {

        g_Vars.currentplayer->prop->pos.y = newpos.y;
        propDeregisterRooms(g_Vars.currentplayer->prop);
        roomsCopy(rooms, g_Vars.currentplayer->prop->rooms);
    }


    g_Vars.enableslopes = true;

    return result;
}

bool bwalkCanMoveUpwards(f32 amount)
{
    bool result;
    struct coord newpos;
    RoomNum rooms[8];
    u32 stack;
    u32 types;
    f32 ymax;
    f32 ymin;
    f32 radius;

    if (g_Vars.currentplayer->floorflags & GEOFLAG_SLOPE) {
        g_Vars.enableslopes = false;
    }
    else {
        g_Vars.enableslopes = true;
    }

    newpos.x = g_Vars.currentplayer->prop->pos.x;
    newpos.y = g_Vars.currentplayer->prop->pos.y + amount;
    newpos.z = g_Vars.currentplayer->prop->pos.z;


    types = g_Vars.bondcollisions ? CDTYPE_ALL : CDTYPE_BG;


    playerGetBbox(g_Vars.currentplayer->prop, &radius, &ymax, &ymin);
    func0f065e74(&g_Vars.currentplayer->prop->pos, g_Vars.currentplayer->prop->rooms, &newpos, rooms);
    bmoveFindEnteredRoomsByPos(g_Vars.currentplayer, &newpos, rooms);
    propSetPerimEnabled(g_Vars.currentplayer->prop, false);

    ymin -= 0.1f;

    result = cdTestVolume(&newpos, radius, rooms, types, CHECKVERTICAL_YES,
                          ymax - g_Vars.currentplayer->prop->pos.y,
                          ymin - g_Vars.currentplayer->prop->pos.y);

    propSetPerimEnabled(g_Vars.currentplayer->prop, true);

    g_Vars.enableslopes = true;

    return (result == CDRESULT_NOCOLLISION);
}

bool bwalkCalculateNewPosition(struct coord* vel, f32 rotateamount, bool apply, f32 extrawidth, s32 checktypes)
{
    s32 result = CDRESULT_NOCOLLISION;
    f32 halfradius;
    struct coord dstpos;
    RoomNum dstrooms[8];
    bool copyrooms = false;
    RoomNum sp64[22];
    s32 types;
    f32 ymax;
    f32 ymin;
    f32 radius;
    f32 xdiff;
    f32 zdiff;
    s32 i;


    if (g_Vars.currentplayer->floorflags & GEOFLAG_SLOPE) {
        g_Vars.enableslopes = false;
    }
    else {
        g_Vars.enableslopes = true;
    }

    dstpos.x = g_Vars.currentplayer->prop->pos.x;
    dstpos.y = g_Vars.currentplayer->prop->pos.y;
    dstpos.z = g_Vars.currentplayer->prop->pos.z;


    if (vel->x || vel->y || vel->z) {
        if (g_Vars.currentplayer->tank) {
            propSetPerimEnabled(g_Vars.currentplayer->tank, false);
        }

        propSetPerimEnabled(g_Vars.currentplayer->prop, false);

        dstpos.x += vel->x;
        dstpos.y += vel->y;
        dstpos.z += vel->z;

        types = g_Vars.bondcollisions ? checktypes : CDTYPE_BG;

        playerGetBbox(g_Vars.currentplayer->prop, &radius, &ymax, &ymin);
        radius += extrawidth;

        func0f065dfc(&g_Vars.currentplayer->prop->pos, g_Vars.currentplayer->prop->rooms,
                     &dstpos, dstrooms, sp64, 20);

#if VERSION < VERSION_NTSC_1_0
        for (i = 0; dstrooms[i] != -1; i++) {
						if (dstrooms[i] == g_Vars.currentplayer->floorroom) {
								dstrooms[0] = g_Vars.currentplayer->floorroom;
								dstrooms[1] = -1;
								break;
						}
				}
#endif

        bmoveFindEnteredRoomsByPos(g_Vars.currentplayer, &dstpos, dstrooms);

        copyrooms = true;

        // Check if the player is moving at least half their radius along the
        // X or Z axis in a single frame. If less, only do a collision check for
        // the dst position. If more, do a halfway check too?
        xdiff = dstpos.x - g_Vars.currentplayer->prop->pos.x;
        zdiff = dstpos.z - g_Vars.currentplayer->prop->pos.z;
        halfradius = radius * 0.5f;


        if (xdiff > halfradius || zdiff > halfradius || xdiff < -halfradius || zdiff < -halfradius) {
            result = cdExamCylMove06(&g_Vars.currentplayer->prop->pos,
                                     g_Vars.currentplayer->prop->rooms,
                                     &dstpos, dstrooms, radius, types, 1,
                                     ymax - g_Vars.currentplayer->prop->pos.y,
                                     ymin - g_Vars.currentplayer->prop->pos.y);

            if (result == CDRESULT_NOCOLLISION) {
                result = cdExamCylMove02(&g_Vars.currentplayer->prop->pos,
                                         &dstpos, radius, dstrooms, types, true,
                                         ymax - g_Vars.currentplayer->prop->pos.y,
                                         ymin - g_Vars.currentplayer->prop->pos.y);

            }
        }
        else {
            result = cdExamCylMove02(&g_Vars.currentplayer->prop->pos,
                                     &dstpos, radius, sp64, types, true,
                                     ymax - g_Vars.currentplayer->prop->pos.y,
                                     ymin - g_Vars.currentplayer->prop->pos.y);
        }

        propSetPerimEnabled(g_Vars.currentplayer->prop, true);

        if (g_Vars.currentplayer->tank) {
            propSetPerimEnabled(g_Vars.currentplayer->tank, true);
        }
    }

    vr_player_rot(); // VR

    if (result == CDRESULT_NOCOLLISION && apply) {
        f32 angle = g_Vars.currentplayer->vv_theta + (rotateamount * 360) / M_BADTAU;

        while (angle < 0) {
            angle += 360;
        }

        while (angle >= 360) {
            angle -= 360;
        }

        g_Vars.currentplayer->vv_theta = angle;

        g_Vars.currentplayer->prop->pos.x = dstpos.x;
        g_Vars.currentplayer->prop->pos.y = dstpos.y;
        g_Vars.currentplayer->prop->pos.z = dstpos.z;


        if (copyrooms) {
            propDeregisterRooms(g_Vars.currentplayer->prop);
            roomsCopy(dstrooms, g_Vars.currentplayer->prop->rooms);
        }
    }

    g_Vars.enableslopes = true;

    return result;
}

bool bwalkCalculateNewPositionWithPush(struct coord* delta, f32 rotateamount, bool apply, f32 extrawidth, s32 types)
{
    s32 result = bwalkCalculateNewPosition(delta, rotateamount, apply, extrawidth, types);

    if (result != CDRESULT_NOCOLLISION) {
        struct prop* obstacle = cdGetObstacleProp();
        if (obstacle && g_Vars.lvupdate240 > 0) {
            if (obstacle->type == PROPTYPE_DOOR) {
                struct doorobj* door = obstacle->door;
                struct coord sp90;
                struct coord sp84;
                struct coord sp78;

                if (door->doorflags & DOORFLAG_DAMAGEONCONTACT) {
                    if (!g_Vars.currentplayer->isdead) {
#if VERSION >= VERSION_NTSC_1_0
                        cdGetEdge(&sp84, &sp78, 465, "bondwalk.c");
#else
                        cdGetEdge(&sp84, &sp78, 460, "bondwalk.c");
#endif

                        sp90.x = sp78.f[2] - sp84.f[2];
                        sp90.y = 0;
                        sp90.z = sp84.f[0] - sp78.f[0];

                        if (sp90.f[0] || sp90.f[2]) {
                            guNormalize(&sp90.x, &sp90.y, &sp90.z);
                        }
                        else {
                            sp90.z = 1;
                        }

                        chrDamageByLaser(g_Vars.currentplayer->prop->chr, 0.4f, &sp90, 0, g_Vars.currentplayer->prop);

                        // Laser zap sound
                        sndStart(var80095200, SFX_PICKUP_LASER, 0, -1, -1, -1, -1, -1);
                    }
                }
            }
            else if (obstacle->type == PROPTYPE_CHR) {
                struct chrdata* chr = obstacle->chr;
                struct coord newpos;
                RoomNum newrooms[8];
                f32 movingdist;
                f32 xdist;
                f32 zdist;
                f32 disttochr;
                bool canpush = false;

                if (g_Vars.normmplayerisrunning) {
                    if (chrCompareTeams(g_Vars.currentplayer->prop->chr, chr, COMPARE_FRIENDS)) {
                        // AI bot on same team
                        canpush = true;
                    }
                }
                else if (chr->chrflags & CHRCFLAG_PUSHABLE) {
                    if (g_Vars.antiplayernum < 0
                        || g_Vars.currentplayer != g_Vars.anti
                        || (chr->hidden & CHRHFLAG_ANTINONINTERACTABLE) == 0) {
                        canpush = true;
                    }
                }

                if (canpush) {
                    movingdist = sqrtf(delta->f[0] * delta->f[0] + delta->f[2] * delta->f[2]) / LVUPDATE60FREAL();

                    xdist = obstacle->pos.x - g_Vars.currentplayer->prop->pos.x;
                    zdist = obstacle->pos.z - g_Vars.currentplayer->prop->pos.z;

                    if (xdist || zdist) {
                        disttochr = sqrtf(xdist * xdist + zdist * zdist);

                        if (disttochr > 0) {
                            disttochr = movingdist / disttochr;

                            xdist *= disttochr;
                            zdist *= disttochr;

                            chr->pushspeed[0] = 0.5f * xdist;
                            chr->pushspeed[1] = 0.5f * zdist;

                            newpos.x = obstacle->pos.x + chr->pushspeed[0] * LVUPDATE60FREAL();
                            newpos.y = obstacle->pos.y;
                            newpos.z = obstacle->pos.z + chr->pushspeed[1] * LVUPDATE60FREAL();

                            chrCalculatePushPos(chr, &newpos, newrooms, false);

                            obstacle->pos.x = newpos.x;
                            obstacle->pos.y = newpos.y;
                            obstacle->pos.z = newpos.z;

                            propDeregisterRooms(obstacle);
                            roomsCopy(newrooms, obstacle->rooms);
                            chr0f0220ac(chr);
                            modelSetRootPosition(chr->model, &newpos);

                            result = bwalkCalculateNewPosition(delta, rotateamount, apply, extrawidth, types);
                        }
                    }
                }

            }
            else if (obstacle->type == PROPTYPE_PLAYER) {
                // empty
            }
            else if (obstacle->type == PROPTYPE_OBJ) {
                struct defaultobj* obj = obstacle->obj;
                bool dothething;

                if ((obj->hidden & OBJHFLAG_MOUNTED) == 0 && (obj->hidden & OBJHFLAG_GRABBED) == 0) {
                    if (g_Vars.currentplayer->unk1af0 == 0 && obj->type == OBJTYPE_TANK) {
                        g_Vars.currentplayer->tank = obstacle;
                    }
                    else if (obj->flags3 & OBJFLAG3_PUSHABLE) {
                        g_Vars.currentplayer->speedmaxtime60 = 0;
                        dothething = true;

                        if ((obj->hidden & OBJHFLAG_PROJECTILE) &&
                            (obj->projectile->flags & PROJECTILEFLAG_00001000)) {
                            dothething = false;
                        }

                        if (dothething) {
                            bwalk0f0c3b38(delta, obj);

                            if (obj->hidden & OBJHFLAG_PROJECTILE && (obj->projectile->flags & PROJECTILEFLAG_SLIDING)) {
                                bool somevalue;
                                bool embedded = false;
                                somevalue = projectileTick(obj, &embedded);

                                if (obj->hidden & OBJHFLAG_PROJECTILE) {
                                    obj->projectile->flags |= PROJECTILEFLAG_00001000;

                                    if (somevalue) {
                                        obj->projectile->flags |= PROJECTILEFLAG_00002000;
                                    }
                                    else {
                                        obj->projectile->flags &= ~PROJECTILEFLAG_00002000;
                                    }
                                }

                                if (somevalue) {
                                    result = bwalkCalculateNewPosition(delta, rotateamount, apply, extrawidth, types);
                                }
                            }
                        }
                    }
                }
            }
        }
    }



    return result;
}

s32 bwalk0f0c4764(struct coord* delta, struct coord* arg1, struct coord* arg2, s32 types)
{
    s32 result = bwalkCalculateNewPositionWithPush(delta, 0, true, 0, types);

    if (result == CDRESULT_COLLISION) {
#if VERSION >= VERSION_NTSC_1_0
        cdGetEdge(arg1, arg2, 607, "bondwalk.c");
#else
        cdGetEdge(arg1, arg2, 602, "bondwalk.c");
#endif
    }
    else {
    }

    return result;
}

s32 bwalk0f0c47d0(struct coord* a, struct coord* b, struct coord* c,
                  struct coord* d, struct coord* e, s32 types)
{
    struct coord quarter;
    bool result;

    if (cd00024ea4()) {
        f32 mult = cd00024e98();
        quarter.x = a->x * mult * 0.25f;
        quarter.y = a->y * mult * 0.25f;
        quarter.z = a->z * mult * 0.25f;
        result = bwalkCalculateNewPositionWithPush(&quarter, 0, true, 0, types);

        if (result == CDRESULT_NOCOLLISION) {
            return CDRESULT_NOCOLLISION;
        }

        if (result == CDRESULT_COLLISION) {
#if VERSION >= VERSION_NTSC_1_0
            cdGetEdge(d, e, 635, "bondwalk.c");
#else
            cdGetEdge(d, e, 630, "bondwalk.c");
#endif

            if (b->x != d->x
                || b->y != d->y
                || b->z != d->z
                || c->x != e->x
                || c->y != e->y
                || c->z != e->z) {
                return CDRESULT_COLLISION;
            }
        }
    }

    return CDRESULT_ERROR;
}

s32 bwalk0f0c494c(struct coord* a, struct coord* b, struct coord* c, s32 types)
{
    if (b->f[0] != c->f[0] || b->f[2] != c->f[2]) {
        f32 tmp;
        struct coord sp38;
        struct coord sp2c;

        sp38.x = c->x - b->x;
        sp38.y = 0;
        sp38.z = c->z - b->z;

        tmp = sqrtf(sp38.f[0] * sp38.f[0] + sp38.f[2] * sp38.f[2]);

        sp38.x *= 1.0f / tmp;
        sp38.z *= 1.0f / tmp;

        tmp = a->f[0] * sp38.f[0] + a->f[2] * sp38.f[2];

        sp2c.x = sp38.x * tmp;
        sp2c.y = 0;
        sp2c.z = sp38.z * tmp;

        return bwalkCalculateNewPositionWithPush(&sp2c, 0, true, 0, types);
    }

    return -1;
}

s32 bwalk0f0c4a5c(struct coord* arg0, struct coord* arg1, struct coord* arg2, s32 types)
{
    struct coord sp34;
    struct coord sp28;
    f32 ymax;
    f32 ymin;
    f32 tmp;
    f32 radius;

    playerGetBbox(g_Vars.currentplayer->prop, &radius, &ymax, &ymin);

    sp34.x = arg1->x - (g_Vars.currentplayer->prop->pos.x + arg0->f[0]);
    sp34.z = arg1->z - (g_Vars.currentplayer->prop->pos.z + arg0->f[2]);


    if (sp34.f[0] * sp34.f[0] + sp34.f[2] * sp34.f[2] <= radius * radius) {
        if (arg1->f[0] != g_Vars.currentplayer->prop->pos.f[0] || arg1->f[2] != g_Vars.currentplayer->prop->pos.f[2]) {
            sp34.x = -(arg1->z - g_Vars.currentplayer->prop->pos.z);
            sp34.y = 0;
            sp34.z = arg1->x - g_Vars.currentplayer->prop->pos.x;

            tmp = sqrtf(sp34.f[0] * sp34.f[0] + sp34.f[2] * sp34.f[2]);

            sp34.x = sp34.f[0] * (1.0f / tmp);
            sp34.z = sp34.f[2] * (1.0f / tmp);

            tmp = arg0->f[0] * sp34.f[0] + arg0->f[2] * sp34.f[2];

            sp34.x = sp34.x * tmp;
            sp34.z = sp34.z * tmp;

            sp28.x = sp34.x;
            sp28.y = 0;
            sp28.z = sp34.z;

            if (bwalkCalculateNewPositionWithPush(&sp28, 0, true, 0, types) == CDRESULT_NOCOLLISION) {
                return true;
            }
        }
    }
    else {
        sp34.x = arg2->x - (g_Vars.currentplayer->prop->pos.x + arg0->f[0]);
        sp34.z = arg2->z - (g_Vars.currentplayer->prop->pos.z + arg0->f[2]);


        if (sp34.f[0] * sp34.f[0] + sp34.f[2] * sp34.f[2] <= radius * radius) {
            if (arg2->f[0] != g_Vars.currentplayer->prop->pos.f[0] || arg2->f[2] != g_Vars.currentplayer->prop->pos.f[2]) {
                sp34.x = -(arg2->z - g_Vars.currentplayer->prop->pos.z);
                sp34.y = 0;
                sp34.z = arg2->x - g_Vars.currentplayer->prop->pos.x;

                tmp = sqrtf(sp34.f[0] * sp34.f[0] + sp34.f[2] * sp34.f[2]);

                sp34.x = sp34.f[0] * (1.0f / tmp);
                sp34.z = sp34.f[2] * (1.0f / tmp);

                tmp = arg0->f[0] * sp34.f[0] + arg0->f[2] * sp34.f[2];

                sp34.x = sp34.x * tmp;
                sp34.z = sp34.z * tmp;

                sp28.x = sp34.x;
                sp28.y = 0;
                sp28.z = sp34.z;

                if (bwalkCalculateNewPositionWithPush(&sp28, 0, true, 0, types) == CDRESULT_NOCOLLISION) {
                    return true;
                }
            }
        }
    }

    return false;
}

void bwalk0f0c4d98(void)
{
    // empty
}

void bwalkUpdateSpeedSideways(f32 targetspeed, f32 accelspeed, s32 mult)
{
    if (g_Vars.normmplayerisrunning) {
        targetspeed = (g_PlayerConfigsArray[g_Vars.currentplayerstats->mpindex].base.unk1c + 25.0f) / 100 * targetspeed;
    }

    if (g_Vars.currentplayer->speedstrafe > targetspeed) {
        g_Vars.currentplayer->speedstrafe -= PALUPF(accelspeed * mult);

        if (g_Vars.currentplayer->speedstrafe < targetspeed) {
            g_Vars.currentplayer->speedstrafe = targetspeed;
        }
    }
    else if (g_Vars.currentplayer->speedstrafe < targetspeed) {
        g_Vars.currentplayer->speedstrafe += PALUPF(accelspeed * mult);

        if (g_Vars.currentplayer->speedstrafe > targetspeed) {
            g_Vars.currentplayer->speedstrafe = targetspeed;
        }
    }

    g_Vars.currentplayer->speedsideways = g_Vars.currentplayer->speedstrafe;
}

void bwalkUpdateSpeedForwards(f32 targetspeed, f32 accelspeed)
{
    if (g_Vars.normmplayerisrunning) {
        targetspeed = (g_PlayerConfigsArray[g_Vars.currentplayerstats->mpindex].base.unk1c + 25.0f) / 100 * targetspeed;
    }

    if (g_Vars.currentplayer->speedgo < targetspeed) {
        g_Vars.currentplayer->speedgo += accelspeed * g_Vars.lvupdate60freal;

        if (g_Vars.currentplayer->speedgo > targetspeed) {
            g_Vars.currentplayer->speedgo = targetspeed;
        }
    }
    else if (g_Vars.currentplayer->speedgo > targetspeed) {
        g_Vars.currentplayer->speedgo -= accelspeed * g_Vars.lvupdate60freal;

        if (g_Vars.currentplayer->speedgo < targetspeed) {
            g_Vars.currentplayer->speedgo = targetspeed;
        }
    }

    g_Vars.currentplayer->speedforwards = g_Vars.currentplayer->speedgo;
}

void bwalkUpdateVertical(void)
{
    s32 i;
    f32 newfallspeed;
    f32 radius;
    f32 ymax;
    f32 ymin;
    f32 ground;
    bool onladder;
    bool onladder2 = false;
    RoomNum rooms[8];
    struct coord testpos;
    struct coord newpos;
    RoomNum newrooms[8];
    s32 newinlift;
    struct prop* lift = NULL;
    f32 sumground;
    f32 moveamount;
#if VERSION >= VERSION_NTSC_1_0
    f32 limit;
    f32 amount;
    struct prop* prop;
#endif
    f32 newmanground;
    f32 fallspeed;
    f32 eyeheight;
    f32 multiplier;
#if VERSION >= VERSION_NTSC_1_0
    struct defaultobj* obj;
#endif

    playerGetBbox(g_Vars.currentplayer->prop, &radius, &ymax, &ymin);

#if VERSION >= VERSION_NTSC_1_0
    // Maybe reset counter-op's radius - not sure why
    // Maybe it gets set to 0 when they die?
    if (g_Vars.antiplayernum >= 0
        && g_Vars.currentplayer == g_Vars.anti
        && g_Vars.currentplayer->bond2.radius != 30
        && cdTestVolume(&g_Vars.currentplayer->prop->pos, 30, g_Vars.currentplayer->prop->rooms, CDTYPE_ALL, CHECKVERTICAL_YES, ymax - g_Vars.currentplayer->prop->pos.y, ymin - g_Vars.currentplayer->prop->pos.y)) {
        g_Vars.currentplayer->prop->chr->radius = 30;
        g_Vars.currentplayer->bond2.radius = 30;
        radius = 30;
    }
#endif

    // Determine if player is on a ladder
    // If this comes up false, a second check is done... maybe checking if the
    // player is touching a ladder from a room which shares the same coordinate
    // space?
    onladder = cdFindLadder(&g_Vars.currentplayer->prop->pos,
                            radius * 1.2f, ymax - g_Vars.currentplayer->prop->pos.y,
                            g_Vars.currentplayer->vv_manground - g_Vars.currentplayer->prop->pos.y + 1,
                            g_Vars.currentplayer->prop->rooms, GEOFLAG_LADDER | GEOFLAG_LADDER_PLAYERONLY,
                            &g_Vars.currentplayer->laddernormal);

    if (!onladder) {
        testpos.x = g_Vars.currentplayer->prop->pos.x;
        testpos.y = g_Vars.currentplayer->prop->pos.y - 10;
        testpos.z = g_Vars.currentplayer->prop->pos.z;
        roomsCopy(g_Vars.currentplayer->prop->rooms, rooms);
        bmoveFindEnteredRoomsByPos(g_Vars.currentplayer, &testpos, rooms);
        onladder2 = cdFindLadder(&g_Vars.currentplayer->prop->pos,
                                 radius * 1.1f, ymax - g_Vars.currentplayer->prop->pos.y,
                                 g_Vars.currentplayer->vv_manground - g_Vars.currentplayer->prop->pos.y - 10,
                                 rooms, GEOFLAG_LADDER | GEOFLAG_LADDER_PLAYERONLY, &g_Vars.currentplayer->laddernormal);
    }


    testpos.x = g_Vars.currentplayer->prop->pos.x;
    testpos.y = g_Vars.currentplayer->prop->pos.y;
    testpos.z = g_Vars.currentplayer->prop->pos.z;

    if (g_Vars.currentplayer->inlift) {
        testpos.y -= g_Vars.currentplayer->crouchheight + g_Vars.currentplayer->crouchoffsetrealsmall;
    }

    roomsCopy(g_Vars.currentplayer->prop->rooms, rooms);
    bmoveFindEnteredRoomsByPos(g_Vars.currentplayer, &testpos, rooms);
    ground = cdFindGroundInfoAtCyl(&testpos, g_Vars.currentplayer->bond2.radius, rooms,
                                   &g_Vars.currentplayer->floorcol, &g_Vars.currentplayer->floortype,
                                   &g_Vars.currentplayer->floorflags, &g_Vars.currentplayer->floorroom,
                                   &newinlift, &lift);
    ground += g_Vars.currentplayer->bondonground;

    if (ground < -30000) {
        ground = -30000;
    }

#if PIRACYCHECKS
    if (g_Vars.currentplayer->inlift && newinlift == false) {
				// Exiting a lift
				piracyRestore();
		}
#endif

    if (g_Vars.currentplayer->inlift && newinlift && g_Vars.currentplayer->onladder == false) {
        // Remaining in a lift
        moveamount = ground - g_Vars.currentplayer->vv_ground;

#if VERSION >= VERSION_NTSC_1_0
        if (moveamount != 0)
#endif
        {
            // The lift is moving
            if (g_Vars.currentplayer->isfalling == false && lift == g_Vars.currentplayer->lift) {
                if (g_Vars.currentplayer->liftground - g_Vars.currentplayer->vv_manground < 1.0f
                    && g_Vars.currentplayer->liftground - g_Vars.currentplayer->vv_manground > -1.0f) {
                    // It's actually moving, and not a floating point precision issue
                    g_Vars.currentplayer->vv_ground += moveamount;

                    if (moveamount > 0
                        || lift == NULL
                        || lift->obj == NULL
                        || (lift->obj->flags & OBJFLAG_CHOPPER_INACTIVE) == 0
                        || bwalkTryMoveUpwards(moveamount) == CDRESULT_NOCOLLISION) {
                        // Going up
                        g_Vars.currentplayer->vv_manground += moveamount;
                        g_Vars.currentplayer->sumground = g_Vars.currentplayer->vv_manground / (PAL ? 0.054400026798248f : 0.045499980449677f);
                    }
                }
            }

            if (g_Vars.currentplayer->walkinitmove) {
                g_Vars.currentplayer->walkinitstart.y += moveamount;
            }
        }
    }
    else {
        lift = NULL;
    }

    g_Vars.currentplayer->inlift = newinlift;

    if (newinlift) {
        g_Vars.currentplayer->liftground = ground;
    }

    g_Vars.currentplayer->lift = lift;

    // Ladders
    if (g_Vars.currentplayer->onladder) {
        if (g_Vars.currentplayer->ladderupdown >= 0 ||
            (ground <= g_Vars.currentplayer->vv_manground &&
             ground <= g_Vars.currentplayer->vv_manground + g_Vars.currentplayer->ladderupdown)) {
            // Still on ladder
            if (bwalkTryMoveUpwards(g_Vars.currentplayer->ladderupdown) == CDRESULT_NOCOLLISION) {
                g_Vars.currentplayer->vv_manground += g_Vars.currentplayer->ladderupdown;
            }
        }
        else {
            if (bwalkTryMoveUpwards(ground - g_Vars.currentplayer->vv_manground) == CDRESULT_NOCOLLISION) {
                g_Vars.currentplayer->vv_manground = ground;
                onladder = false;
            }
        }
    }

    g_Vars.currentplayer->onladder = onladder;

    if (g_Vars.currentplayer->onladder) {
        g_Vars.currentplayer->vv_ground = g_Vars.currentplayer->vv_manground;
    }
    else if (onladder2 == false) {
        g_Vars.currentplayer->vv_ground = ground;
    }

    // Standing on flat ground, or going up stairs, ledges or ramps
    // In other words, not falling
    if (g_Vars.currentplayer->bdeltapos.y >= 0.0f
        || g_Vars.currentplayer->vv_ground > g_Vars.currentplayer->vv_manground) {
        g_Vars.currentplayer->sumground = g_Vars.currentplayer->vv_manground / (PAL ? 0.054400026798248f : 0.045499980449677f);

        for (i = 0; i < g_Vars.lvupdate240; i++) {
            g_Vars.currentplayer->sumground =
                    g_Vars.currentplayer->sumground * (PAL ? 0.94559997320175f : 0.9545f) + g_Vars.currentplayer->vv_ground;
        }

        if (g_Vars.currentplayer->vv_manground < g_Vars.currentplayer->vv_ground) {
            // Feet are lower than the ground
            sumground = g_Vars.currentplayer->sumground * (PAL ? 0.054400026798248f : 0.045499980449677f);

            if (sumground < g_Vars.currentplayer->vv_ground - 50) {
                sumground = g_Vars.currentplayer->vv_ground - 50;
            }

            if (bwalkTryMoveUpwards(sumground - g_Vars.currentplayer->vv_manground) == CDRESULT_NOCOLLISION) {
                g_Vars.currentplayer->vv_manground = sumground;
            }
#if VERSION >= VERSION_NTSC_1_0
            else {
                // Not enough room above. If on a hoverbike, blow it up
                prop = cdGetObstacleProp();

                if (prop
                    && g_Vars.currentplayer->prop->pos.y < prop->pos.y
                    && prop->type == PROPTYPE_OBJ) {
                    obj = prop->obj;

                    if (obj->modelnum == MODEL_HOVBIKE) {
                        amount = (obj->maxdamage - obj->damage + 1) / 250.0f;
                        obj->flags &= ~OBJFLAG_INVINCIBLE;
                        objDamage(obj, amount, &obj->prop->pos, WEAPON_REMOTEMINE, -1);
                    }
                }
            }
#endif
        }

        // Kill player if standing on tile with GEOFLAG_DIE
        if ((g_Vars.currentplayer->floorflags & GEOFLAG_DIE)
            && g_Vars.currentplayer->vv_manground - 20.0f < g_Vars.currentplayer->vv_ground
            && g_Vars.currentplayer->onladder == false
            && onladder2 == false) {
            playerDie(true);
        }
    }

    if (g_Vars.currentplayer->vv_manground > g_Vars.currentplayer->vv_ground) {
        // Not standing on ground - probably falling, or on an object of some sort
        fallspeed = g_Vars.currentplayer->bdeltapos.y;
        newmanground = g_Vars.currentplayer->vv_manground;

        if (debugIsTurboModeEnabled()
            && g_Vars.currentplayer->bondforcespeed.x == 0
            && g_Vars.currentplayer->bondforcespeed.z == 0) {
            multiplier = 0.277777777f * 5;
        }
        else {
            multiplier = 0.277777777f;
        }

        newfallspeed = fallspeed - g_Vars.lvupdate60freal * multiplier;
        newmanground += g_Vars.lvupdate60freal * (fallspeed + newfallspeed) * 0.5f;
        fallspeed = newfallspeed;

        if (newmanground < g_Vars.currentplayer->vv_ground) {
            newfallspeed = g_Vars.currentplayer->vv_manground - g_Vars.currentplayer->vv_ground;
            newmanground = g_Vars.currentplayer->vv_ground;

            fallspeed = sqrtf(g_Vars.currentplayer->bdeltapos.y *
                              g_Vars.currentplayer->bdeltapos.y +
                              (((newfallspeed + newfallspeed) * 0.277777777f) / 60.0f) * 60.0f);
            fallspeed = -fallspeed;
        }

        if (bwalkTryMoveUpwards(newmanground - g_Vars.currentplayer->vv_manground) == CDRESULT_NOCOLLISION) {
            // Falling
            g_Vars.currentplayer->vv_manground = newmanground;
            g_Vars.currentplayer->bdeltapos.y = fallspeed;

            if (g_Vars.currentplayer->isfalling == false) {
                // Just started falling
                g_Vars.currentplayer->isfalling = true;
                g_Vars.currentplayer->fallstart = g_Vars.lvframe60;
            }
            else {
                if (g_Vars.lvframe60 - g_Vars.currentplayer->fallstart > TICKS(240)) {
                    // Have been falling for 4 seconds
                    playerDie(true);
                }
            }
        }
        else {
            // Not falling
#if VERSION >= VERSION_NTSC_1_0
            if (g_Vars.normmplayerisrunning == false
                && g_Vars.currentplayer->vv_ground < g_Vars.currentplayer->vv_manground - 30) {
                // Not falling - but still at least 30 units off the ground.
                // Must be something in the way...
                prop = cdGetObstacleProp();

                if (prop) {
                    if (prop->type == PROPTYPE_CHR) {
                        // Landed on top of a chr
                        if (prop->chr->inlift) {
                            chrYeetFromPos(prop->chr, &g_Vars.currentplayer->prop->pos, 0);
                        }
                    }
                    else if (prop->type == PROPTYPE_PLAYER) {
                        // Landed on top of a player
                        u32 prevplayernum = g_Vars.currentplayernum;
                        setCurrentPlayerNum(playermgrGetPlayerNumByProp(prop));

                        if (g_Vars.currentplayer->inlift) {
                            playerDieByShooter(prevplayernum, true);
                        }

                        setCurrentPlayerNum(prevplayernum);
                    }
                }
            }
#endif

            g_Vars.currentplayer->bdeltapos.y = VERSION >= VERSION_NTSC_1_0 ? 0.0f : 0;

            if (g_Vars.currentplayer->isfalling) {
                g_Vars.currentplayer->isfalling = false;
            }

            if (g_Vars.currentplayer->vv_manground <= -30000) {
                playerDie(true);
            }
        }
    }
    else {
        // Not falling
        if (g_Vars.currentplayer->isfalling) {
            g_Vars.currentplayer->isfalling = false;
        }

        if (g_Vars.currentplayer->vv_manground <= -30000) {
            playerDie(true);
        }
    }

    if (g_Vars.currentplayer->bdeltapos.y < 0 &&
        g_Vars.currentplayer->vv_manground <= g_Vars.currentplayer->vv_ground) {
        // Landing after a fall
        if (g_Vars.currentplayer->isfalling) {
            g_Vars.currentplayer->isfalling = false;
        }

        // I suspect these crouch fields are related to the recovery during
        // landing. Eg. The faster the fall speed, the longer Jo will take to
        // stand back to full height again.
        if (g_Vars.currentplayer->bdeltapos.y < -13.333333f) {
            g_Vars.currentplayer->crouchtime240 = TICKS(60);
            g_Vars.currentplayer->crouchfall = -90;
        }
        else if (g_Vars.currentplayer->bdeltapos.y < -5.0f) {
            g_Vars.currentplayer->crouchtime240 = TICKS(60);
            g_Vars.currentplayer->crouchfall =
                    (-5.0f - g_Vars.currentplayer->bdeltapos.y) * -90.0f / 8.333333f;
        }

        if (g_Vars.currentplayer->bdeltapos.y < -6.0f) {
            // Play footstep sounds
            s32 sound;
            struct chrdata* chr = g_Vars.currentplayer->prop->chr;
            chr->floortype = g_Vars.currentplayer->floortype;
            chr->footstep = 1;

            sound = footstepChooseSound(chr, true);

            if (sound != -1) {
                if (sound != -1) {
                    psCreate(NULL, g_Vars.currentplayer->prop, sound,
                             -1, -1, PSFLAG_0400 | PSFLAG_IGNOREROOMS, 0, PSTYPE_NONE, 0, -1, NULL, -1, -1, -1, -1);
                }

                chr->footstep = 2;
                sound = footstepChooseSound(chr, true);

                if (sound != -1) {
                    psCreate(NULL, g_Vars.currentplayer->prop, sound,
                             -1, -1, PSFLAG_0400 | PSFLAG_IGNOREROOMS, 0, PSTYPE_NONE, 0, -1, NULL, -1, -1, -1, -1);
                }
            }

            if (g_Vars.mplayerisrunning == false
                && (chr->headnum == HEAD_DARK_COMBAT || chr->headnum == HEAD_DARK_FROCK)
                && g_Vars.lvframe60 - g_Vars.currentplayer->fallstart > TICKS(40)) {
                // Play Jo landing grunt
                s32 sounds[] = {
                        SFX_JO_LANDING_046F,
                        SFX_JO_LANDING_05B6,
                        SFX_JO_LANDING_05B7
                };

                psCreate(NULL, g_Vars.currentplayer->prop, sounds[rngRandom() % 3],
                         -1, -1, PSFLAG_0400 | PSFLAG_IGNOREROOMS, 0, PSTYPE_NONE, 0, -1, NULL, -1, -1, -1, -1);
            }
        }

        g_Vars.currentplayer->bdeltapos.y = 0;
    }

    // Decrease crouchtime240 for this tick.
    // If reached 0 and crouchfall is negative, start increasing
    // crouchfall over the next several ticks until it reaches 0.
    for (i = 0; i < g_Vars.lvupdate240; i++) {
        if (g_Vars.currentplayer->crouchtime240 > 0) {
            g_Vars.currentplayer->sumcrouch =
                    g_Vars.currentplayer->sumcrouch * (PAL ? 0.93540000915527f : 0.9456f) + g_Vars.currentplayer->crouchfall;
            g_Vars.currentplayer->crouchtime240--;
        }
        else {
            if (g_Vars.currentplayer->crouchfall < 0) {
                g_Vars.currentplayer->crouchfall -= (PAL ? -1.3636363744736f : -1.125f);

                if (g_Vars.currentplayer->crouchfall >= 0) {
                    g_Vars.currentplayer->crouchfall = 0;
                }
            }

            g_Vars.currentplayer->sumcrouch =
                    g_Vars.currentplayer->sumcrouch * (PAL ? 0.93540000915527f : 0.9456f) + g_Vars.currentplayer->crouchfall;
        }
    }


    g_Vars.currentplayer->crouchheight = g_Vars.currentplayer->sumcrouch * (PAL ? 0.064599990844727f : 0.054400026798248f);
    g_Vars.currentplayer->vv_height =
            (g_Vars.currentplayer->headpos.y / g_Vars.currentplayer->standheight)
            * g_Vars.currentplayer->vv_eyeheight;


    if(!VrSeatedMode) {
// VR height: map the real head height above the physical floor into game units.
// Nothing is remembered between frames, so a physical jump can no longer latch
// a reference and leave you shorter for the session.
//
//   real height mode      one real cm is one game unit -- the scale the game
//                         itself is modelled at, Joanna's eye sitting at 159
//                         units for 1.59 m. The body you are wearing does not
//                         come into it, so you are your own height in every
//                         level. World scale applies, as it does to the IPD.
//   character height mode your standing height maps onto the character's own
//                         standing eye height (vv_eyeheight -- 159 for Joanna,
//                         106 for Elvis, 175 for Mr Blonde), so standing up
//                         puts your eye exactly where theirs is. World scale is
//                         deliberately left out: the promise here is the
//                         character's height relative to the level, and scaling
//                         it would break that.
        float unitsPerCm = VrMatchCharacterHeight
                           ? (g_Vars.currentplayer->vv_eyeheight / VrPlayerHeight)
                           : VrSetWorldScale;

        eyeheight = gVrHeadHeightCm * unitsPerCm;

// Sanity clamp against tracking glitches, not a height cap -- VR_MAX_HEAD_CM is
// above any real tracked head. The ceiling check below is what keeps the view
// out of level geometry.
        if (eyeheight > VR_MAX_HEAD_CM * unitsPerCm) eyeheight = VR_MAX_HEAD_CM * unitsPerCm;
        if (eyeheight < 0.0f) eyeheight = 0.0f;

// --- VR EYEHEIGHT DIVIDER TOGGLE (thumbstick click) ---
        {

            static bool sPrevThumbstickClick = false;

            bool curThumbstickClick = VrStickClickToCrouch && get_button_state(0, "thumbstick_click");

            // Rising edge detection: only switch when the button is pressed
            if (curThumbstickClick && !sPrevThumbstickClick) {
                switch (sVrEyeheightMode) {
                    case VR_EYEHEIGHT_STAND:
                        sVrEyeheightMode = VR_EYEHEIGHT_DUCK;
                        break;
                    case VR_EYEHEIGHT_DUCK:
                        sVrEyeheightMode = VR_EYEHEIGHT_SQUAT;
                        break;
                    case VR_EYEHEIGHT_SQUAT:
                    default:
                        sVrEyeheightMode = VR_EYEHEIGHT_STAND;
                        break;
                }
            }
            sPrevThumbstickClick = curThumbstickClick;


            switch (sVrEyeheightMode) {
                case VR_EYEHEIGHT_DUCK:
                    eyeheight = eyeheight / 1.3f;
                    break;
                case VR_EYEHEIGHT_SQUAT:
                    eyeheight = eyeheight / 1.6f;
                    break;
                case VR_EYEHEIGHT_STAND:
                default:
                    break;
            }
        }

// --- VR CEILING CHECK ---
        {
            f32 targetCors = eyeheight
                             - g_Vars.currentplayer->vv_height
                             - g_Vars.currentplayer->crouchheight
                               * g_Vars.currentplayer->vv_eyeheight * 0.0062893079593778f;

            f32 prevCrouchOffsetReal = g_Vars.currentplayer->crouchoffsetreal;
            f32 prevCrouchOffsetSmall = g_Vars.currentplayer->crouchoffsetsmall;
            f32 prevCrouchOffsetRealSmall = g_Vars.currentplayer->crouchoffsetrealsmall;

            g_Vars.currentplayer->crouchoffsetreal = targetCors;
            g_Vars.currentplayer->crouchoffsetsmall = targetCors;
            g_Vars.currentplayer->crouchoffsetrealsmall = targetCors;

            bool canStand = bwalkCanMoveUpwards(0);

            g_Vars.currentplayer->crouchoffsetreal = prevCrouchOffsetReal;
            g_Vars.currentplayer->crouchoffsetsmall = prevCrouchOffsetSmall;
            g_Vars.currentplayer->crouchoffsetrealsmall = prevCrouchOffsetRealSmall;

            if (canStand) {
                sVrEyeheightClamped = eyeheight;
            } else {
                eyeheight = sVrEyeheightClamped;
            }
        }
        // --- END VR CEILING CHECK ---
    }
    else{
        eyeheight = g_Vars.currentplayer->vv_height +
                    g_Vars.currentplayer->crouchoffsetrealsmall +
                    g_Vars.currentplayer->crouchheight *
                    g_Vars.currentplayer->vv_eyeheight * 0.0062893079593778f;


        static bool sPrevThumbstickClick = false;

        bool curThumbstickClick = get_button_state(0, "thumbstick_click");

        // Rising edge detection: only switch when the button is pressed
        if (curThumbstickClick && !sPrevThumbstickClick) {
            switch (sVrEyeheightMode) {
                case VR_EYEHEIGHT_STAND:
                    sVrEyeheightMode = VR_EYEHEIGHT_DUCK;
                    break;
                case VR_EYEHEIGHT_DUCK:
                    sVrEyeheightMode = VR_EYEHEIGHT_SQUAT;
                    break;
                case VR_EYEHEIGHT_SQUAT:
                default:
                    sVrEyeheightMode = VR_EYEHEIGHT_STAND;
                    break;
            }
        }
        sPrevThumbstickClick = curThumbstickClick;

        if (eyeheight < 30) {
            eyeheight = 30;
        }
    }


    newpos.x = g_Vars.currentplayer->prop->pos.x;
    newpos.y = g_Vars.currentplayer->vv_manground + eyeheight;
    newpos.z = g_Vars.currentplayer->prop->pos.z;




#if VERSION >= VERSION_NTSC_1_0
    if (newpos.y < g_Vars.currentplayer->vv_ground + 10) {
        newpos.y = g_Vars.currentplayer->vv_ground + 10;
    }
#endif

    if (newpos.x != g_Vars.currentplayer->prop->pos.x
        || newpos.y != g_Vars.currentplayer->prop->pos.y
        || newpos.z != g_Vars.currentplayer->prop->pos.z) {
        func0f065e74(&g_Vars.currentplayer->prop->pos, g_Vars.currentplayer->prop->rooms, &newpos, newrooms);

        g_Vars.currentplayer->prop->pos.x = newpos.x;
        g_Vars.currentplayer->prop->pos.y = newpos.y;
        g_Vars.currentplayer->prop->pos.z = newpos.z;

        propDeregisterRooms(g_Vars.currentplayer->prop);
        roomsCopy(newrooms, g_Vars.currentplayer->prop->rooms);
    }
}

void bwalkApplyCrouchSpeed(void)
{
    if (bmoveGetCrouchPos() == CROUCHPOS_DUCK) {
        g_Vars.currentplayer->speedforwards *= 0.5f;
        g_Vars.currentplayer->speedsideways *= 0.5f;
    }
    else if (bmoveGetCrouchPos() == CROUCHPOS_SQUAT) {
        g_Vars.currentplayer->speedforwards *= 0.35f;
        g_Vars.currentplayer->speedsideways *= 0.35f;
    }
}

void bwalkUpdateCrouchOffsetReal(void)
{
    if (g_Vars.currentplayer->vv_eyeheight + -90.0f * g_Vars.currentplayer->vv_eyeheight * (1.0f / 159.0f) < 69.0f) {
        g_Vars.currentplayer->crouchoffsetreal = g_Vars.currentplayer->crouchoffset * ((69.0f - g_Vars.currentplayer->vv_eyeheight) / -90.0f);
    }
    else {
        g_Vars.currentplayer->crouchoffsetreal = g_Vars.currentplayer->crouchoffset * g_Vars.currentplayer->vv_eyeheight * (1.0f / 159.0f);
    }

    if (cheatIsActive(CHEAT_SMALLJO)) {
        g_Vars.currentplayer->crouchoffsetsmall = 69.0f - g_Vars.currentplayer->vv_eyeheight;
        g_Vars.currentplayer->crouchoffsetrealsmall = 69.0f - g_Vars.currentplayer->vv_eyeheight;
    }
    else {
        g_Vars.currentplayer->crouchoffsetsmall = g_Vars.currentplayer->crouchoffset;
        g_Vars.currentplayer->crouchoffsetrealsmall = g_Vars.currentplayer->crouchoffsetreal;
    }
}

bool bwalkCanUncrouch(void)
{
    f32 targetoffset = 0;

    if (g_Vars.currentplayer->crouchpos == CROUCHPOS_SQUAT) {
        targetoffset = -90;
    }
    else if (g_Vars.currentplayer->crouchpos == CROUCHPOS_DUCK) {
        targetoffset = -45;
    }

    if (targetoffset != g_Vars.currentplayer->crouchoffset) {
        f32 prevcrouchoffset = g_Vars.currentplayer->crouchoffset;
        f32 prevcrouchoffsetreal = g_Vars.currentplayer->crouchoffsetreal;
        f32 prevcrouchoffsetsmall = g_Vars.currentplayer->crouchoffsetsmall;
        f32 prevcrouchoffsetrealsmall = g_Vars.currentplayer->crouchoffsetrealsmall;
        f32 prevcrouchspeed = g_Vars.currentplayer->crouchspeed;

        g_Vars.currentplayer->crouchoffset = targetoffset;

        bwalkUpdateCrouchOffsetReal();

        const bool result = bwalkCanMoveUpwards(0);

        g_Vars.currentplayer->crouchoffset = prevcrouchoffset;
        g_Vars.currentplayer->crouchoffsetreal = prevcrouchoffsetreal;
        g_Vars.currentplayer->crouchoffsetsmall = prevcrouchoffsetsmall;
        g_Vars.currentplayer->crouchoffsetrealsmall = prevcrouchoffsetrealsmall;
        g_Vars.currentplayer->crouchspeed = prevcrouchspeed;

        return result;
    }

    return true;
}


void bwalkUpdateCrouchOffset(void)
{
    f32 targetoffset = 0;

    if (bmoveGetCrouchPos() == CROUCHPOS_SQUAT) {
        targetoffset = -90;
    } else if (bmoveGetCrouchPos() == CROUCHPOS_DUCK) {
        targetoffset = -45;
    } else if (bmoveGetCrouchPos() == CROUCHPOS_STAND) {
        // empty
    }



    if (targetoffset != g_Vars.currentplayer->crouchoffset) {
        f32 prevcrouchoffset = g_Vars.currentplayer->crouchoffset;
        f32 prevcrouchoffsetreal = g_Vars.currentplayer->crouchoffsetreal;
        f32 prevcrouchoffsetsmall = g_Vars.currentplayer->crouchoffsetsmall;
        f32 prevcrouchoffsetrealsmall = g_Vars.currentplayer->crouchoffsetrealsmall;

        // f32 *frac, f32 maxfrac, f32 *fracspeed, f32 accel, f32 decel, f32 maxspeed
        applySpeed(&g_Vars.currentplayer->crouchoffset, targetoffset,
                   &g_Vars.currentplayer->crouchspeed, PALUPF(0.5f), PALUPF(0.5f), PALUPF(5.0f));

        bwalkUpdateCrouchOffsetReal();

        if (bwalkTryMoveUpwards(0) == CDRESULT_COLLISION) {
            // Crouch adjustment is blocked by ceiling
            g_Vars.currentplayer->crouchoffset = prevcrouchoffset;
            g_Vars.currentplayer->crouchoffsetreal = prevcrouchoffsetreal;
            g_Vars.currentplayer->crouchoffsetsmall = prevcrouchoffsetsmall;
            g_Vars.currentplayer->crouchoffsetrealsmall = prevcrouchoffsetrealsmall;
            g_Vars.currentplayer->crouchspeed = 0;
            bwalkAdjustCrouchPos(-1);
        }
    }

    if (targetoffset == g_Vars.currentplayer->crouchoffset) {
        g_Vars.currentplayer->crouchspeed = 0;
    }

    g_Vars.currentplayer->guncloseroffset = g_Vars.currentplayer->crouchoffset / -90;
}


void bwalkUpdateTheta(void)
{
    f32 mult;
    f32 rotateamount;
    struct coord delta = { 0, 0, 0 };

#ifdef PLATFORM_N64
    // Turn speed is calculated from the chr's height
		mult = 159.0f / g_Vars.currentplayer->vv_eyeheight;
#else
    // Same turn speed for all heights
    mult = 1.f;
#endif
    rotateamount = g_Vars.currentplayer->speedtheta * mult
                   * g_Vars.lvupdate60freal * 0.0174505133f * 3.5f;


    bwalkCalculateNewPositionWithPush(&delta, rotateamount, true, 0, CDTYPE_ALL);

}

void bwalk0f0c63bc(struct coord* arg0, u32 arg1, s32 types)
{
    struct coord sp100;
    struct coord sp88;

    g_Vars.currentplayer->bondonturret = false;
    g_Vars.currentplayer->autocrouchpos = CROUCHPOS_STAND;

    bwalk0f0c4d98();

    if (bwalk0f0c4764(arg0, &sp100, &sp88, types) == CDRESULT_COLLISION) {
        struct coord sp76;
        struct coord sp64;

        s32 result = bwalk0f0c47d0(arg0, &sp100, &sp88, &sp76, &sp64, types);

        if (result >= CDRESULT_NOCOLLISION || result <= CDRESULT_ERROR) {
            if (result >= CDRESULT_NOCOLLISION) {

                bwalk0f0c4d98();
            }

            if (arg1
                && bwalk0f0c494c(arg0, &sp100, &sp88, types) <= CDRESULT_COLLISION
                && bwalk0f0c4a5c(arg0, &sp100, &sp88, types) <= CDRESULT_COLLISION) {
                // empty
            }
        }
        else if (result == CDRESULT_COLLISION) {
            struct coord sp48;
            struct coord sp36;


            if (bwalk0f0c47d0(arg0, &sp76, &sp64, &sp48, &sp36, types) >= CDRESULT_NOCOLLISION) {
                bwalk0f0c4d98();
            }

            if (arg1
                && bwalk0f0c494c(arg0, &sp76, &sp64, types) <= CDRESULT_COLLISION
                && bwalk0f0c494c(arg0, &sp100, &sp88, types) <= CDRESULT_COLLISION
                && bwalk0f0c4a5c(arg0, &sp76, &sp64, types) <= CDRESULT_COLLISION) {
                bwalk0f0c4a5c(arg0, &sp100, &sp88, types);
            }
        }
    }

    bwalk0f0c4d98();
}

void bwalkUpdatePrevPos(void)
{
    g_Vars.currentplayer->bondprevpos.x = g_Vars.currentplayer->prop->pos.x;
    g_Vars.currentplayer->bondprevpos.y = g_Vars.currentplayer->prop->pos.y;
    g_Vars.currentplayer->bondprevpos.z = g_Vars.currentplayer->prop->pos.z;


    roomsCopy(g_Vars.currentplayer->prop->rooms, g_Vars.currentplayer->bondprevrooms);

}

void bwalkHandleActivate(void)
{

    if (g_Vars.currentplayer->walkinitmove) {
        g_Vars.currentplayer->bondactivateorreload = 0;
    }

}

void bwalkApplyMoveData(struct movedata* data)
{
    if (g_Vars.currentplayer->walkinitmove == false) {
        // Sideways
        if (data->digitalstepleft) {
            bwalkUpdateSpeedSideways(-1, 0.2f, data->digitalstepleft);
        }
        else if (data->digitalstepright) {
            bwalkUpdateSpeedSideways(1, 0.2f, data->digitalstepright);
        }
        else if (data->unk14 == false) {
            bwalkUpdateSpeedSideways(0, 0.2f, g_Vars.lvupdate60);
        }
        else if (data->unk14) {
            bwalkUpdateSpeedSideways(data->analogstrafe * 0.014285714365542f, 0.2f, g_Vars.lvupdate60);
        }

        // Forward/back
        if (data->digitalstepforward) {
            bwalkUpdateSpeedForwards(1, 1);
            g_Vars.currentplayer->speedmaxtime60 += g_Vars.lvupdate60;
        }
        else if (data->digitalstepback) {
            bwalkUpdateSpeedForwards(-1, 1);
        }
        else if (data->canlookahead == false) {
            bwalkUpdateSpeedForwards(0, 1);
        }
        else {
            bwalkUpdateSpeedForwards(data->analogwalk * 0.014285714365542f, 1);
        }


        if (data->canlookahead) {
            if (data->analogwalk > 60) {
                g_Vars.currentplayer->speedmaxtime60 += g_Vars.lvupdate60;
            }
            else {
                g_Vars.currentplayer->speedmaxtime60 = 0;
            }
        }

        // Force speeds to range -1 to 1
        if (g_Vars.currentplayer->speedforwards > 1) {
            g_Vars.currentplayer->speedforwards = 1;
        }

        if (g_Vars.currentplayer->speedforwards < -1) {
            g_Vars.currentplayer->speedforwards = -1;
        }

        if (g_Vars.currentplayer->speedsideways > 1) {
            g_Vars.currentplayer->speedsideways = 1;
        }

        if (g_Vars.currentplayer->speedsideways < -1) {
            g_Vars.currentplayer->speedsideways = -1;
        }

        g_Vars.currentplayer->speedforwards *= 1.08f;
        g_Vars.currentplayer->speedforwards *= g_Vars.currentplayer->speedboost;

        if ((data->canlookahead == false && data->digitalstepforward == false) ||
            bmoveGetCrouchPos() != CROUCHPOS_STAND) {
            g_Vars.currentplayer->speedmaxtime60 = 0;
        }

#ifndef PLATFORM_N64

/*        if (data->rleanleft) { // Removed for VR
            bwalkSetSwayTarget(-1);
        }
        else if (data->rleanright) {
            bwalkSetSwayTarget(1);
        }
        else if (fabsf(data->analoglean)) {
            bwalkSetSwayTargetf(data->analoglean);
        }
        else {
            bwalkSetSwayTarget(0);
        }*/


        if (fabsf(data->analoglean)) {
            bwalkSetSwayTargetf(data->analoglean);
        }
        else {
            bwalkSetSwayTarget(0);
        }
#else
        if (data->rleanleft) {
						bwalkSetSwayTarget(-1);
				}
				else if (data->rleanright) {
						bwalkSetSwayTarget(1);
				}
				else {
						bwalkSetSwayTarget(0);
				}
#endif

        while (data->crouchdown-- > 0) {
            bwalkAdjustCrouchPos(-1);
        }

        while (data->crouchup-- > 0) {
            bwalkAdjustCrouchPos(1);
        }

        g_Vars.currentplayer->eyesshut = data->eyesshut;
    }
}

void bwalkUpdateSpeedTheta(void)
{
#ifdef PLATFORM_N64
    if (bmoveGetCrouchPos() == CROUCHPOS_SQUAT) {
				g_Vars.currentplayer->speedtheta *= 0.5f;
		}
		else if (bmoveGetCrouchPos() == CROUCHPOS_DUCK) {
				g_Vars.currentplayer->speedtheta *= 0.75f;
		}
#endif
}

void bwalk0f0c69b8(void)
{
    s32 i;
    f32 spe0;
    f32 spdc;
    f32 spd8;
    struct coord spcc = { 0, 0, 0 };
    f32 spc8;
    f32 spc4;
    f32 spc0;
    f32 tmp1;
    f32 tmp2;
    f32 spb4;
    f32 spb0;
    f32 dist;
    f32 spa8;
    f32 mult;
    f32 f0;
    f32 lvupdate60f;
    s32 lvupdate240;
    s32 cdresult;
    struct escalatorobj* esc;
    f32 sp8c;
    f32 sp88;
    f32 speedforwards;
    f32 speedsideways;
    f32 speedtheta;
    f32 maxspeed;
    f32 sp74;
    f32 radius;
    f32 ymax;
    f32 ymin;
    f32 xdiff;
    f32 zdiff;
    f32 xdelta;
    f32 zdelta;
    f32 sp54;
    f32 sp50;
    f32 sp4c;
    f32 sp48;
    f32 sp44;
    f32 sp40;
    f32 sp3c;
    f32 breathing;

    spc0 = g_Vars.currentplayer->vv_eyeheight - 159;

    if (invHasBriefcase() && ((g_MpSetup.scenario == MPSCENARIO_HOLDTHEBRIEFCASE || g_MpSetup.scenario == MPSCENARIO_CAPTURETHECASE))) {
        spc0 = -63.600006f;
    }

    spc0 = spc0 / 353.33331298828f + 1.0f;

    if (g_Vars.normmplayerisrunning && (g_MpSetup.options & MPOPTION_FASTMOVEMENT)) {
        spc0 *= 1.25f;
    }

#if VERSION >= VERSION_NTSC_1_0
    if (cheatIsActive(CHEAT_SMALLJO)) {
        spc0 *= 0.4f;
    }
#endif

    if (g_Vars.currentplayer->walkinitmove) {
        g_Vars.currentplayer->walkinitt += g_Vars.lvupdate60freal * (1.0f / 60.0f);

        if (g_Vars.currentplayer->walkinitt >= 1.0f) {
            g_Vars.currentplayer->walkinitt = 1.0f;
            g_Vars.currentplayer->walkinitmove = false;
        }

        g_Vars.currentplayer->walkinitt2 = 1.0f - (cosf(g_Vars.currentplayer->walkinitt * M_BADPI) + 1.0f) * 0.5f;

        bmoveUpdateHead(0.0f, 0.0f, 0.0f, &g_Vars.currentplayer->walkinitmtx, 1.0f - g_Vars.currentplayer->walkinitt2);

        g_Vars.currentplayer->gunspeed = 0.0f;

        bmoveUpdateMoveInitSpeed(&spcc);
        bwalkCalculateNewPositionWithPush(&spcc, 0.0f, true, 0.0f, CDTYPE_ALL);
    }
    else {
        bwalkApplyCrouchSpeed();
        bwalkUpdateCrouchOffset();

        bmove0f0cba88(&spc8, &spc4,
                      &g_Vars.currentplayer->bondshotspeed,
                      g_Vars.currentplayer->vv_sintheta, g_Vars.currentplayer->vv_costheta);

        tmp1 = -g_Vars.currentplayer->swaytarget * g_Vars.currentplayer->bond2.unk00.f[2];
        tmp2 = g_Vars.currentplayer->swaytarget * g_Vars.currentplayer->bond2.unk00.f[0];
        tmp1 *= spc0;
        tmp2 *= spc0;
        spa8 = 0.0f;

        if (g_Vars.currentplayer->crouchoffset < -45.0f) {
            tmp1 *= 0.35f;
            tmp2 *= 0.35f;
        }
        else if (g_Vars.currentplayer->crouchoffset < 0.0f) {
            tmp1 *= 0.5f;
            tmp2 *= 0.5f;
        }

        spb4 = tmp1 - g_Vars.currentplayer->swayoffset0;
        spb0 = tmp2 - g_Vars.currentplayer->swayoffset2;

        dist = sqrtf(spb4 * spb4 + spb0 * spb0);

        if (g_Vars.lvupdate60freal > PALUPF(4)) {
            lvupdate60f = PALUPF(4);
            lvupdate240 = 4;
        }
        else {
            lvupdate60f = g_Vars.lvupdate60freal;
            lvupdate240 = g_Vars.lvupdate60;
        }

        for (i = 0; i < lvupdate240; i++) {
            spa8 += (dist - spa8) * PALUPF(0.1f);
        }

        spa8 += 3.75f * lvupdate60f;

        if (g_Vars.currentplayer->crouchoffset < -45.0f) {
            spa8 *= 0.35f;
        }
        else if (g_Vars.currentplayer->crouchoffset < 0.0f) {
            spa8 *= 0.5f;
        }

        if (spa8 < dist) {
            spa8 /= dist;
            spb4 *= spa8;
            spb0 *= spa8;
        }

        speedsideways = (g_Vars.currentplayer->speedsideways + spc4) * 0.8f;
        speedforwards = g_Vars.currentplayer->speedforwards + spc8;
        speedtheta = g_Vars.currentplayer->speedtheta * 0.8f;

        if (speedsideways < 0.0f) {
            speedsideways = -speedsideways;
        }

        if (speedforwards < 0.0f) {
            speedforwards = -speedforwards;
        }

        if (speedtheta < 0.0f) {
            speedtheta = -speedtheta;
        }

        maxspeed = speedforwards;

        if (speedsideways > maxspeed) {
            maxspeed = speedsideways;
        }

        if (speedtheta > maxspeed) {
            maxspeed = speedtheta;
        }

        if (dist >= 0.1f && maxspeed < 0.8f) {
            maxspeed = 0.8f;
        }

        if (maxspeed >= 0.75f) {
            g_Vars.currentplayer->bondbreathing += (maxspeed - 0.75f) * g_Vars.lvupdate60freal / 900;
        }
        else {
            g_Vars.currentplayer->bondbreathing -= (0.75f - maxspeed) * g_Vars.lvupdate60freal / 2700;
        }

        if (g_Vars.currentplayer->bondbreathing < 0.0f) {
            g_Vars.currentplayer->bondbreathing = 0.0f;
        }
        else if (g_Vars.currentplayer->bondbreathing > 1.0f) {
            g_Vars.currentplayer->bondbreathing = 1.0f;
        }

        mult = g_HeadAnims[HEADANIM_MOVING].translateperframe * 0.5f * g_Vars.lvupdate60freal;
        spe0 = (g_Vars.currentplayer->speedsideways * spc0 + spc4) * mult;

#if VERSION >= VERSION_NTSC_1_0
        if (cheatIsActive(CHEAT_SMALLJO)) {
            spe0 /= 0.4f;
        }
#endif

        bmove0f0cc654(maxspeed, g_Vars.currentplayer->speedforwards * spc0 + spc8, spe0);

        g_Vars.currentplayer->gunspeed = maxspeed;

        spdc = g_Vars.currentplayer->headpos.x;
        spd8 = g_Vars.currentplayer->headpos.z;

#if VERSION >= VERSION_NTSC_1_0
        if (cheatIsActive(CHEAT_SMALLJO)) {
            spdc *= 0.4f;
        }
#endif

        spcc.f[0] += (spd8 * g_Vars.currentplayer->bond2.unk00.f[0] - spdc * g_Vars.currentplayer->bond2.unk00.f[2]) * g_Vars.lvupdate60freal;
        spcc.f[2] += (spd8 * g_Vars.currentplayer->bond2.unk00.f[2] + spdc * g_Vars.currentplayer->bond2.unk00.f[0]) * g_Vars.lvupdate60freal;
        spcc.f[0] += spb4;
        spcc.f[2] += spb0;

        bmoveUpdateMoveInitSpeed(&spcc);

        if (debugIsTurboModeEnabled()) {
            spcc.f[0] += (g_Vars.currentplayer->bond2.unk00.f[0] * g_Vars.currentplayer->speedforwards - g_Vars.currentplayer->bond2.unk00.f[2] * g_Vars.currentplayer->speedsideways) * g_Vars.lvupdate60freal * 10.0f;
            spcc.f[2] += (g_Vars.currentplayer->bond2.unk00.f[2] * g_Vars.currentplayer->speedforwards + g_Vars.currentplayer->bond2.unk00.f[0] * g_Vars.currentplayer->speedsideways) * g_Vars.lvupdate60freal * 10.0f;
        }

        if (g_Vars.currentplayer->bondforcespeed.f[0] != 0.0f || g_Vars.currentplayer->bondforcespeed.f[2] != 0.0f) {
            spcc.f[0] += g_Vars.currentplayer->bondforcespeed.f[0] * g_Vars.lvupdate60freal;
            spcc.f[2] += g_Vars.currentplayer->bondforcespeed.f[2] * g_Vars.lvupdate60freal;
        }

        if (g_Vars.currentplayer->onladder) {
            guNormalize(&g_Vars.currentplayer->laddernormal.x, &g_Vars.currentplayer->laddernormal.y, &g_Vars.currentplayer->laddernormal.z);

            sp74 = -(spcc.f[0] * g_Vars.currentplayer->laddernormal.f[0] + spcc.f[2] * g_Vars.currentplayer->laddernormal.f[2]);

            if (-4.0f * g_Vars.lvupdate60freal < sp74) {
                if (sp74 < 0.0f) {
                    spcc.f[0] += sp74 * g_Vars.currentplayer->laddernormal.f[0];
                    spcc.f[2] += sp74 * g_Vars.currentplayer->laddernormal.f[2];
                    g_Vars.currentplayer->ladderupdown = sp74 * 0.3f;
                }
                else {
                    playerGetBbox(g_Vars.currentplayer->prop, &radius, &ymax, &ymin);

                    if (!cd0002a13c(&g_Vars.currentplayer->prop->pos,
                                    radius * 1.1f, ymax - g_Vars.currentplayer->prop->pos.y,
                                    (g_Vars.currentplayer->vv_manground - g_Vars.currentplayer->prop->pos.y) + 1.0f,
                                    g_Vars.currentplayer->prop->rooms, GEOFLAG_LADDER | GEOFLAG_LADDER_PLAYERONLY)) {
                        g_Vars.currentplayer->ladderupdown = 0.0f;
                    }
                    else {
                        spcc.f[0] += sp74 * g_Vars.currentplayer->laddernormal.f[0];
                        spcc.f[2] += sp74 * g_Vars.currentplayer->laddernormal.f[2];
                        g_Vars.currentplayer->ladderupdown = sp74 * 0.3f;
                    }
                }

                spcc.x *= 0.3f;
                spcc.z *= 0.3f;
            }
            else {
                g_Vars.currentplayer->ladderupdown = 0.0f;
            }
        }

        if (g_Vars.currentplayer->lift) {
            esc = (struct escalatorobj*)g_Vars.currentplayer->lift->obj;

            if (esc->base.type == OBJTYPE_ESCASTEP) {
                spcc.x += esc->base.prop->pos.x - esc->prevpos.x;
                spcc.z += esc->base.prop->pos.z - esc->prevpos.z;
            }
        }

        sp8c = g_Vars.currentplayer->prop->pos.x;
        sp88 = g_Vars.currentplayer->prop->pos.z;


        bwalk0f0c63bc(&spcc, g_Vars.currentplayer->swaytarget == 0.0f, CDTYPE_ALL);

        xdelta = g_Vars.currentplayer->prop->pos.x - g_Vars.currentplayer->bondprevpos.x;
        zdelta = g_Vars.currentplayer->prop->pos.z - g_Vars.currentplayer->bondprevpos.z;

        sp54 = -xdelta * g_Vars.currentplayer->bond2.unk00.f[2] + zdelta * g_Vars.currentplayer->bond2.unk00.f[0];
        sp50 = xdelta * g_Vars.currentplayer->bond2.unk00.f[0] + zdelta * g_Vars.currentplayer->bond2.unk00.f[2];

        sp4c = -spcc.f[0] * g_Vars.currentplayer->bond2.unk00.f[2] + spcc.f[2] * g_Vars.currentplayer->bond2.unk00.f[0];
        sp48 = spcc.f[0] * g_Vars.currentplayer->bond2.unk00.f[0] + spcc.f[2] * g_Vars.currentplayer->bond2.unk00.f[2];

        if (xdelta >= 0.0f) {
            if (g_Vars.currentplayer->bondshotspeed.f[0] > 0.0f) {
                if (spcc.f[0] >= 0.0f && xdelta < spcc.f[0]) {
                    g_Vars.currentplayer->bondshotspeed.f[0] *= xdelta / spcc.f[0];
                }
            }
            else {
                if (spcc.f[0] < 0.0f) {
                    g_Vars.currentplayer->bondshotspeed.f[0] = 0.0f;
                }
            }
        }
        else {
            if (g_Vars.currentplayer->bondshotspeed.f[0] < 0.0f) {
                if (spcc.f[0] <= 0.0f && spcc.f[0] < xdelta) {
                    g_Vars.currentplayer->bondshotspeed.f[0] *= xdelta / spcc.f[0];
                }
            }
            else {
                if (spcc.f[0] > 0.0f) {
                    g_Vars.currentplayer->bondshotspeed.f[0] = 0.0f;
                }
            }
        }

        if (zdelta >= 0.0f) {
            if (g_Vars.currentplayer->bondshotspeed.f[2] > 0.0f) {
                if (spcc.f[2] >= 0.0f && zdelta < spcc.f[2]) {
                    g_Vars.currentplayer->bondshotspeed.f[2] *= zdelta / spcc.f[2];
                }
            }
            else {
                if (spcc.f[2] < 0.0f) {
                    g_Vars.currentplayer->bondshotspeed.f[2] = 0.0f;
                }
            }
        }
        else {
            if (g_Vars.currentplayer->bondshotspeed.f[2] < 0.0f) {
                if (spcc.f[2] <= 0.0f && spcc.f[2] < zdelta) {
                    g_Vars.currentplayer->bondshotspeed.f[2] *= zdelta / spcc.f[2];
                }
            }
            else {
                if (spcc.f[2] > 0.0f) {
                    g_Vars.currentplayer->bondshotspeed.f[2] = 0.0f;
                }
            }
        }

        if (sp4c != 0.0f && g_Vars.currentplayer->speedstrafe * sp4c > 0.0f) {
            sp54 /= sp4c;

            if (sp54 <= 0.0f) {
                g_Vars.currentplayer->speedstrafe = 0.0f;
            }
            else if (sp54 < 1.0f) {
                g_Vars.currentplayer->speedstrafe *= sp54;
            }
        }

        if (sp48 != 0.0f) {
            if (g_Vars.currentplayer->speedgo * sp48 > 0.0f) {
                sp50 /= sp48;

                if (sp50 <= 0.0f) {
                    g_Vars.currentplayer->speedgo = 0.0f;
                }
                else if (sp50 < 1.0f) {
                    g_Vars.currentplayer->speedgo *= sp50;
                }
            }
        }

        xdiff = g_Vars.currentplayer->prop->pos.x - sp8c;
        zdiff = g_Vars.currentplayer->prop->pos.z - sp88;
        f0 = spcc.f[0] * spcc.f[0] + spcc.f[2] * spcc.f[2];


        if (f0 != 0.0f) {
            f0 = (xdiff * xdiff + zdiff * zdiff) / f0;
        }

        f0 = sqrtf(f0);
        g_Vars.currentplayer->swayoffset0 += f0 * spb4;
        g_Vars.currentplayer->swayoffset2 += f0 * spb0;
    }

    sp44 = g_Vars.currentplayer->speedtheta;
    sp40 = g_Vars.currentplayer->speedverta / 0.7f + g_Vars.currentplayer->crouchspeed / PALUPF(5.0f);
    sp3c = g_Vars.currentplayer->gunspeed;

    breathing = bheadGetBreathingValue();

    if (sp40 > 1.0f) {
        sp40 = 1.0f;
    }
    else if (sp40 < -1.0f) {
        sp40 = -1.0f;
    }

    if (g_Vars.currentplayer->headanim == HEADANIM_MOVING) {
        breathing *= 1.2f;
    }

    bgun0f09d8dc(breathing, sp3c, sp40, sp44, 0.0f);

    bgunSetAdjustPos(g_Vars.currentplayer->vv_verta360 * 0.017450513318181f);
}


void bwalkTick(void)
{


    bwalkUpdatePrevPos();
    bwalkUpdateTheta();
    bmoveUpdateVerta();
    bwalk0f0c69b8();

    bwalkUpdateVertical();

    vr_player_pos();




#if VERSION >= VERSION_NTSC_1_0
    {
        s32 i;

        for (i = 0; g_Vars.currentplayer->prop->rooms[i] != -1; i++) {
            if (g_Vars.currentplayer->floorroom == g_Vars.currentplayer->prop->rooms[i]) {
                propDeregisterRooms(g_Vars.currentplayer->prop);
                g_Vars.currentplayer->prop->rooms[0] = g_Vars.currentplayer->floorroom;
                g_Vars.currentplayer->prop->rooms[1] = -1;
                break;
            }
        }
    }
#endif

    bmoveUpdateRooms(g_Vars.currentplayer);
    objectiveCheckRoomEntered(g_Vars.currentplayer->prop->rooms[0]);

    if (g_Vars.currentplayer->walkinitmove) {
        struct coord coord;
        coord.x = (g_Vars.currentplayer->walkinitstart.x - g_Vars.currentplayer->walkinitpos.x)
                  * (1.0f - g_Vars.currentplayer->walkinitt2) + g_Vars.currentplayer->prop->pos.x;


        coord.y = (g_Vars.currentplayer->walkinitstart.y - g_Vars.currentplayer->prop->pos.y)
                  * (1.0f - g_Vars.currentplayer->walkinitt2) + g_Vars.currentplayer->prop->pos.y;

        coord.z = (g_Vars.currentplayer->walkinitstart.z - g_Vars.currentplayer->walkinitpos.z)
                  * (1.0f - g_Vars.currentplayer->walkinitt2) + g_Vars.currentplayer->prop->pos.z;

        bmove0f0cc19c(&coord);
    }
    else {
        // VR: the camera sits at the head, which is the body plus however far
        // you have physically leaned away from it.
        struct coord headpos;
        vr_get_head_pos(&headpos);
        bmove0f0cc19c(&headpos);
    }

    playerUpdatePerimInfo();
    doorsCheckAutomatic();
}
