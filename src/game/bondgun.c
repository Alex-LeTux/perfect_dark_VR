#include <ultra64.h>

#include <math.h>
#include <stdio.h>
#include "../../port/vr/vr_input.h"

#include "constants.h"
#include "../lib/naudio/n_sndp.h"
#include "game/bondmove.h"
#include "game/cheats.h"
#include "game/chraction.h"
#include "game/inv.h"
#include "game/game_006900.h"
#include "game/chr.h"
#include "game/prop.h"
#include "game/propsnd.h"
#include "game/game_096360.h"
#include "game/acosfasinf.h"
#include "game/game_096b20.h"
#include "game/quaternion.h"
#include "game/game_097aa0.h"
#include "game/bondgun.h"
#include "game/vr_holster.h"
#include "game/vr_reload.h"
#include "game/vr_reload_data.h"
#include "game/gunfx.h"
#include "game/game_0b0fd0.h"
#include "game/modeldef.h"
#include "game/modelmgr.h"
#include "game/tex.h"
#include "game/camera.h"
#include "game/player.h"
#include "game/mtxf2lbulk.h"
#include "game/gfxmemory.h"
#include "game/sight.h"
#include "game/inv.h"
#include "game/playermgr.h"
#include "game/smoke.h"
#include "game/game_1531a0.h"
#include "game/file.h"
#include "game/lv.h"
#include "game/texdecompress.h"
#include "game/zbuf.h"
#include "game/training.h"
#include "game/lang.h"
#include "game/mplayer/mplayer.h"
#include "game/pak.h"
#include "game/options.h"
#include "game/propobj.h"
#include "game/objectives.h"
#include "bss.h"
#include "lib/collision.h"
#include "lib/vi.h"
#include "lib/joy.h"
#include "lib/main.h"
#include "lib/model.h"
#include "lib/snd.h"
#include "lib/rng.h"
#include "lib/mtx.h"
#include "lib/anim.h"
#include "lib/lib_317f0.h"
#include "data.h"
#include "types.h"
#ifndef PLATFORM_N64
#include "game/stagetable.h"
#include "video.h"
#include "platform.h"
#endif


#include "../../port/vr/vr_openxr.h"
#include "../../port/vr/vr_log.h"
#include "vr_scope.h"


#include <game/bg.h>
#include <stdlib.h>
#include <string.h>
#include <system.h>

#ifdef ANDROID
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "PD-VR", __VA_ARGS__)
#else
#define LOGI(...) printf(__VA_ARGS__)
#include <malloc.h>
#endif

// VR global -----------------------------------------
extern XrQuaternionf vr_joy_rot_Q;
extern XrQuaternionf vr_HMD_rot_Q;
extern XrQuaternionf vr_recenter_rot_Q;
extern float vr_ctrl_velocity[2][3]; // [ctrlIndex][x,y,z] en m/s
// Gesture frame: play space, raw OpenXR axes. See vr_openxr.h.
extern float vr_ctrl_quat_play[2][4];
extern float vr_ctrl_velocity_play[2][3];
extern float vr_head_velocity_play[3];
extern void vr_rotate_vector_by_quaternion(struct coord* v, const XrQuaternionf* q);
int vr_invert_hands = false;
bool vr_leftHasWeapon = false;
static bool leftTrig = false;
static bool vr_prevLeftTrig = false;
bool vr_set_motion_triggered = false;
extern int vr_button_R_grip;
extern int vr_button_L_grip;
int weaponnum = 0;
int handnum = 0;
// VR: the weapon whose file bgunTickGunLoad is loading, set only around its fileLoadToAddr, for the
// gun file preprocess (filemodel.c drops the fists model's left hand, and the laser's right hand, by
// weapon); -1 otherwise, when the preprocess reads the global weaponnum as before
s32 g_VrGunLoadWeaponnum = -1;
struct coord velocity = { 0, 0, 0 };
extern int VrLeftHandedMode;
int VrHideArms = 0;

// VR Left crosshair HUD -------------------------
float vr_LeftCrossX = 0.0f;
float vr_LeftCrossY = 0.0f;
bool vr_LeftCrossValid = false;
#define CLAMP(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))

// VR UNARMED ---------------------------------------
static struct guncmd vr_hand_grip_anim[2] = {
        { GUNCMD_PLAYANIMATION, 0, 1002, 20000 },
        { GUNCMD_END }
};
// The fist was open (grip released) last tick: the next grip press plays the clench
bool vr_is_R_fist = false;
bool vr_is_L_fist = false;
bool vr_grip_for_unarmed = false;

// VR COMBATKNIFE ----------------------------------
static struct guncmd vr_knife_sec_anim[2] = {
        { GUNCMD_PLAYANIMATION, 0, 1029, 20000 },
        { GUNCMD_END }
};
static struct guncmd vr_knife_sec_anim_revers[2] = {
        { GUNCMD_PLAYANIMATION, 0, 1029, -10000 },
        { GUNCMD_END }
};
static struct guncmd vr_knife_sec_anim_throw[2] = {
        { GUNCMD_PLAYANIMATION, 0, 1051, 10000 },
        { GUNCMD_END }
};

bool vr_R_func_secondary_knife = false;
bool vr_R_knife_sec_anim_run = false;
bool vr_R_trigger = false;

bool vr_L_func_secondary_knife = false;
bool vr_L_knife_sec_anim_run = false;
bool vr_L_trigger = false;

// VR knife (Task H3b): a slash started by a swing plays its animation from the stroke. Both of the
// slash's animations (ANIM_GUN_KNIFE_SLASH and the 50% ANIM_GUN_KNIFE_EQUIP, the script's random
// pick) wind the knife back over frames 0 to 20 and swing it through 20 to 27, and the script's hit
// (gunscript_waittime(24, 2)) comes at frame 24: from frame 0 the hit landed 24 frames (0.4 s)
// after the swing that started it. From frame 20 it lands 4 frames (67 ms) after, in the stroke.
// Set per engine hand by the swing's slash, taken by the animation's load (bgun0f0981e8).
#define VR_KNIFE_STROKE_FRAME 20.0f
static bool vr_knife_fromstroke[2];

// VR: the DY357s (Magnum and LX) fire from a cocked hammer (Task J10, the user: "the two magnums have
// an awkward delay between when the user pulls the trigger and when the gun fires"). Their fire
// animation (ANIM_GUN_DY357_SHOOT, 26 frames) is a double-action pull: the hammer goes back 58 degrees
// over frames 0 to 9 and falls over 10 and 11, and the script's shot (gunscript_waittime(12, 2)) comes
// at frame 12, 12 frames (0.2 s) after the pull; the Falcon 2's script has no such wait and fires on
// the pull. From frame 9 the hammer falls at once and the shot goes off 2 frames (33 ms) after the
// pull, with the hammer. Taken by the animation's load (bgun0f0981e8) in the attack state only: the
// equip animation is the same one.
#define VR_DY357_COCKED_FRAME 9.0f

// VR: the knife's throw wind-up runs in the idle state, so a switch can take the knife away mid
// wind-up: a flag left set would throw the next knife drawn on its first tick, trigger up
void bgunKnifeReset(void)
{
    vr_R_func_secondary_knife = false;
    vr_R_knife_sec_anim_run = false;
    vr_L_func_secondary_knife = false;
    vr_L_knife_sec_anim_run = false;
    vr_knife_fromstroke[HAND_RIGHT] = false;
    vr_knife_fromstroke[HAND_LEFT] = false;
}

// VR Trow Grenade / Mine etc -----------------------------------------
#define VR_THROW_HISTORY_FRAMES 15
#define VR_THROW_HISTORY_MAX    64

typedef struct {
    float vx, vy, vz;
    float vr_magnitude;
    XrQuaternionf orientQ;
    uint32_t frame60;
} vr_ThrowSample;

static vr_ThrowSample gThrowHistory[2][VR_THROW_HISTORY_MAX]; // [ctrlIdx][sample]
static int vr_ThrowHistoryIdx[2] = {0, 0};
static int vr_ThrowHistoryCount[2] = {0, 0};
bool vr_throw_cancelled = false;
bool VrMotionThrowing = true;

// VR: with holsters (manual reload on) the three bugs throw by motion as well (user decision): they
// have no fire animation, so the wind-up is the wait in HANDSTATEMINOR_ATTACK_THROW_1 while the
// trigger is held (as the ECM mine), and the release throws with the swing (vr_throw). The
// projectile is the same bug object as the aimed throw's, so the scripts that look for it on an
// object (if_weapon_thrown_on_object) still see it.
static bool vrBugMotionThrow(s32 weaponnum)
{
    return VrMotionThrowing && vrHolsterActive()
        && (weaponnum == WEAPON_COMMSRIDER
            || weaponnum == WEAPON_TRACERBUG
            || weaponnum == WEAPON_TARGETAMPLIFIER);
}

// VR: the weapons the motion throw releases with the swing (vr_throw at THROW_1) and spawns with
// that velocity (bgunCreateThrownProjectile). The one list for the release, the spawn, the swing
// samples and the trigger-held wait, so they cannot drift apart.
static bool vrMotionThrowWeapon(s32 weaponnum, s32 weaponfunc)
{
    switch (weaponnum) {
    case WEAPON_GRENADE:
    case WEAPON_NBOMB:
    case WEAPON_ECMMINE:
    case WEAPON_PROXIMITYMINE:
    case WEAPON_TIMEDMINE:
    case WEAPON_REMOTEMINE:
        return true;
    case WEAPON_LAPTOPGUN:
    case WEAPON_DRAGON:
    case WEAPON_COMBATKNIFE:
        return weaponfunc == FUNC_SECONDARY;
    }

    return vrBugMotionThrow(weaponnum);
}

// The grenade, the N-bomb and the timed, proximity and remote mines hold their wind-up in the fire
// animation (gunscript_waitforzreleased); the other motion throws wait in THROW_1 while the trigger
// is held
static bool vrThrowWindsUpInAnim(s32 weaponnum)
{
    return weaponnum == WEAPON_GRENADE
        || weaponnum == WEAPON_NBOMB
        || weaponnum == WEAPON_TIMEDMINE
        || weaponnum == WEAPON_PROXIMITYMINE
        || weaponnum == WEAPON_REMOTEMINE;
}

// VR Laser Dot ------------------------------------------------------
static f32 old_dotposX[2];
static f32 old_dotposY[2];
static f32 old_dotposZ[2];
static bool old_dotpos_init[2] = { false, false };
bool show_laser_dot[2]  = { false, false };
bool VrlaserDotForALL = false;

// VR manual reload: also turns on the body holsters (draw from the left hip or over the right
// shoulder). The two work together or not at all.
bool VrManualReloading = false;

// ===== VR gun / hand placement ==============================================================
// The geometry here was established by measurement rather than guesswork and is now baked in.
// Only the handful of genuinely per-player values below stay configurable, via pd-vr.ini
// (there is no menu UI for them -- see vrSettingsSave() for the documented key names).
float VrGunOffX = 2.35f;       // grip fit trim in the controller frame, game units
float VrGunOffY = -13.02f;
float VrGunOffZ = -4.39f;
float VrArmElbowTuck  = 1.00f; // 0..1, how much of the solved elbow the forearm takes (0: the animation's)
float VrArmBodyFollow = 0.02f; // how fast the torso yaw chases the head (spin comfort)
int   VrFistClench    = 1;     // close the off-hand while the left grip is squeezed

// --- Baked constants: measured/tuned, deliberately not user-facing --------------------------
#define VR_FIST_SIDE         -1.0f   // the mirrored fists hand's arm side for vr_wrist_rot
#define VR_FIST_ROLL         80.0f   // fists grip roll about the forearm axis (degrees)
#define VR_FIST_ROLL_CLENCH -80.0f   // extra roll blended in as the fist closes

// --- Runtime state (not settings) -----------------------------------------------------------
// Smoothed torso yaw. This is what makes the shoulders (vr_arm_solve) spin-safe: a quick glance leaves the
// torso behind so the anchor holds still, while a sustained physical turn drags it around with
// you. Counter-rotating by ABSOLUTE head yaw was the bug -- a 360 spin unwound the elbows.
static float VrBodyYaw = 0.0f;
f32 vrGetBodyYaw(void) { return VrBodyYaw; } // for vr_reload.c (shoulder clip point)
float VrFistClenchAmt = 0.0f;  // 0 = open, 1 = closed; eased, shared by the pose and render paths
//Vr Recoil -----------------------------------------------------------
void vrRecoilNotifyShotFired(int handnum);
extern bool VrWeaponRecoil;
bool VR_FUNC_SECONDARY = false; // For vr_input.cpp / recoil
//---
float zRollAngle = 0.0f;
// VR: the gun's turn about its model's x and y, with zRollAngle as the z (mtx4LoadRotation's angles),
// set per hand by vr_gun_pos_rot; only the Laser with holsters has them (Task H3a)
static float vrGunTurnX = 0.0f;
static float vrGunTurnY = 0.0f;

// VR: the per-weapon turn (vrGunTurnX, vrGunTurnY, zRollAngle) applied in the gun's model frame, after
// the controller's rotation
static void vrGunTurnApply(Mtxf *posrotmtx)
{
    if (zRollAngle != 0.0f || vrGunTurnX != 0.0f || vrGunTurnY != 0.0f) {
        struct coord rot = { vrGunTurnX, vrGunTurnY, zRollAngle };
        Mtxf turn;
        Mtxf tmp;
        mtx4LoadIdentity(&turn);
        mtx4LoadRotation(&rot, &turn);
        mtx4MultMtx4(posrotmtx, &turn, &tmp);
        *posrotmtx = tmp;
    }
}

// VR: the Laser's offset with the grip trim (VrGunOffX, Y, Z: in the controller's frame) turned back by
// the turn (vrGunTurnX, vrGunTurnY, zRollAngle), so that the placement, which turns the offset with the
// gun (posrotmtx after vrGunTurnApply, bgun0f0a5550), leaves the trim in the controller's frame as on
// the guns with no turn (Task H5b). The trim's x is the one the render keeps: it takes VrGunOffX off
// on the hand it takes for the mirrored copy (handToMirror there; the Laser is DUALFLIP)
static void vrGunTrimUnturn(s32 handnum, struct coord *off)
{
    struct coord rot = { vrGunTurnX, vrGunTurnY, zRollAngle };
    Mtxf turn;
    f32 t[3];
    s32 handToMirror = VrLeftHandedMode ? HAND_RIGHT : HAND_LEFT;

    t[0] = handnum == handToMirror ? 0.0f : VrGunOffX;
    t[1] = VrGunOffY;
    t[2] = VrGunOffZ;

    mtx4LoadIdentity(&turn);
    mtx4LoadRotation(&rot, &turn);

    // turn.m[i] is where the turn takes axis i, so the turn's inverse takes t to { turn.m[i] . t }
    off->x += turn.m[0][0] * t[0] + turn.m[0][1] * t[1] + turn.m[0][2] * t[2] - t[0];
    off->y += turn.m[1][0] * t[0] + turn.m[1][1] * t[1] + turn.m[1][2] * t[2] - t[1];
    off->z += turn.m[2][0] * t[0] + turn.m[2][1] * t[1] + turn.m[2][2] * t[2] - t[2];
}
//----

#define GUNLOADSTATE_FLUX     0
#define GUNLOADSTATE_MODEL    1
#define GUNLOADSTATE_TEXTURES 2
#define GUNLOADSTATE_DLS      3
#define GUNLOADSTATE_LOADED   4

#define MASTERLOADSTATE_FLUX   0
#define MASTERLOADSTATE_HANDS  1
#define MASTERLOADSTATE_GUN    2
#define MASTERLOADSTATE_CARTS  3
#define MASTERLOADSTATE_LOADED 4

// Max downwards pitch when changing guns or reloading a classic gun
#define MAX_PITCH 0.87252569198608f

#if VERSION >= VERSION_PAL_BETA
struct sndstate* g_CasingAudioHandles[2];
s32 var8009d0d8;
u32 fill2;
struct sndstate* g_BgunAudioHandles[MAX_PLAYERS];
s32 var8009d0dc;
u32 fill2_2;
s32 var8009d0f0[3];
u32 var8009d0fc;
u32 var8009d100;
u32 var8009d104;
u32 var8009d108;
u32 var8009d10c;
u32 var8009d110;
u32 var8009d114;
u32 var8009d118;
u32 var8009d11c;
u32 var8009d120;
u32 var8009d124;
u32 var8009d128;
u32 var8009d12c;
u32 var8009d130;
u32 var8009d134;
u32 var8009d138;
u32 var8009d13c;
f32 var8009d140;
struct hand* var8009d144;
s32 var8009d148;
u32 var8009d14c;
struct fireslot g_Fireslots[20];
#elif VERSION >= VERSION_NTSC_1_0
struct sndstate* g_CasingAudioHandles[2];
s32 var8009d0d8;
s32 var8009d0dc;
struct sndstate* g_BgunAudioHandles[MAX_PLAYERS];
s32 var8009d0f0[3];
u32 var8009d0fc;
u32 var8009d100;
u32 var8009d104;
u32 var8009d108;
u32 var8009d10c;
u32 var8009d110;
u32 var8009d114;
u32 var8009d118;
u32 var8009d11c;
u32 var8009d120;
u32 var8009d124;
u32 var8009d128;
u32 var8009d12c;
u32 var8009d130;
u32 var8009d134;
u32 var8009d138;
u32 var8009d13c;
f32 var8009d140;
struct hand* var8009d144;
s32 var8009d148;
u32 var8009d14c;
struct fireslot g_Fireslots[20];
#else
s32 var8009d0dc;
u32 var800a1800nb;
s32 var8009d0f0[3];
u32 var8009d0fc;
u32 var8009d100;
u32 var8009d104;
u32 var8009d108;
u32 var8009d10c;
u32 var8009d110;
u32 var8009d114;
u32 var8009d118;
u32 var8009d11c;
u32 var8009d120;
u32 var8009d124;
u32 var8009d128;
u32 var8009d12c;
u32 var8009d130;
u32 var8009d134;
u32 var8009d138;
u32 var8009d13c;
f32 var8009d140;
struct hand* var8009d144;
s32 var8009d148;
u32 var8009d14c;
struct sndstate* g_CasingAudioHandles[2];
s32 var8009d0d8;
struct sndstate* g_BgunAudioHandles[MAX_PLAYERS];
struct fireslot g_Fireslots[20];
u32 fill2[1];
#endif

Lights1 var80070090 = gdSPDefLights1(0x96, 0x96, 0x96, 0xff, 0xff, 0xff, 0xb2, 0x4d, 0x2e);

#ifdef PLATFORM_64BIT
u32 g_BgunGunMemBaseSizeDefault = 150 * 1024 * 2; // #TODO adjust these values properly
u32 g_BgunGunMemBaseSize4Mb2P = 120 * 1024 * 2;
#else
u32 g_BgunGunMemBaseSizeDefault = 150 * 1024;
u32 g_BgunGunMemBaseSize4Mb2P = 120 * 1024;
#endif

u16 g_CartFileNums[] = {
        FILE_GCARTRIDGE,
        FILE_GCARTRIFLE,
        FILE_GCARTBLUE,
        FILE_GCARTSHELL,
};

u32 var800700b8 = 0x00000000;

char var800700bc[][10] = {
        { 'i','d','l','e'                     }, // "idle"
        { 'p','r','e','p','a','r','e'         }, // "prepare"
        { 'c','a','n','t','u','s','e'         }, // "cantuse"
        { 'n','o','a','m','m','o'             }, // "noammo"
        { 'u','s','e','2'                     }, // "use2"
        { 'c','h','a','n','g','e'             }, // "change"
        { 'u','p','g','r','a','d','e'         }, // "upgrade"
        { 'c','h','a','n','g','e','f','n'     }, // "changefn"
        { 'i','d','l','e','s','t','u','c','k' }, // "idlestuck"
        { 'x','x','x'                         }, // "xxx"
};

#ifndef PLATFORM_N64
s32 g_BgunGeMuzzleFlashes = false;
#endif







// VR--------------------------

bool VrTwoHandsGun(s32 weaponnum) {
    switch (weaponnum) {
        case WEAPON_CALLISTO:
        case WEAPON_RCP120:
        case WEAPON_DRAGON:
        case WEAPON_K7AVENGER:
        case WEAPON_AR34:
        case WEAPON_SUPERDRAGON:
        case WEAPON_SHOTGUN:
        case WEAPON_SNIPERRIFLE:
        case WEAPON_FARSIGHT:
        case WEAPON_DEVASTATOR:
        case WEAPON_ROCKETLAUNCHER:
        case WEAPON_SLAYER:
        case WEAPON_REAPER:
        case WEAPON_LAPTOPGUN:
            return true;
        default:
            return false;
    }
}

// Two-handed AIM is a narrower set than the two-handed POSE. Steering by the line between the
// controllers needs the two hands to be meaningfully apart along some fixed axis of the weapon;
// where they are not, the hand line is noise and would corrupt the aim rather than refine it.
//
// Only the Slayer fails that. Its off-hand wraps the bottom of the same pistol grip, so the hands
// are nearly coincident, the separation sits in the fallback band anyway, and the direction between
// them is vertical -- nowhere near any axis of the gun. It keeps its two-handed pose, mesh handling
// and recoil, and aims from the trigger wrist exactly as it did before two-handed aiming existed.
//
// The Rocket Launcher and Reaper also hold the off-hand off-barrel, but consistently so, and are
// handled by giving them a grip axis instead (vrTwoHandAxisYaw) rather than by exclusion.
static bool vrTwoHandAimGun(s32 weaponnum)
{
    switch (weaponnum) {
        case WEAPON_SLAYER:
            return false;
        default:
            return VrTwoHandsGun(weaponnum);
    }
}

static bool vrLaserDotAllowed(s32 weaponnum)
{
    switch (weaponnum) {
        // Weapons without hands / gadgets / explosives = no laser dot
        case WEAPON_UNARMED:
        case WEAPON_NONE:
        case WEAPON_COMBATKNIFE:
        case WEAPON_GRENADE:
        case WEAPON_NBOMB:
        case WEAPON_TIMEDMINE:
        case WEAPON_PROXIMITYMINE:
        case WEAPON_REMOTEMINE:
        case WEAPON_ECMMINE:
        case WEAPON_HORIZONSCANNER:
        case WEAPON_DATAUPLINK:
        case WEAPON_RTRACKER:
        case WEAPON_PRESIDENTSCANNER:
        case WEAPON_DOORDECODER:
        case WEAPON_AUTOSURGEON:
        case WEAPON_COMMSRIDER:
        case WEAPON_TRACERBUG:
        case WEAPON_TARGETAMPLIFIER:
        case WEAPON_CLOAKINGDEVICE:
        case WEAPON_COMBATBOOST:
        case WEAPON_EXPLOSIVES:
        case WEAPON_SKEDARBOMB:
        case WEAPON_NIGHTVISION:
        case WEAPON_XRAYSCANNER:
        case WEAPON_IRSCANNER:
        case WEAPON_EYESPY:
        case WEAPON_DISGUISE40:
        case WEAPON_DISGUISE41:
            return false;
        default:
            return true;
    }
}

// VR: a device, disguise or door decoder held from the chest pouch is its own model at
// the controller, no hands. Each row: the model point that sits at the grip, in model units (the
// middle of its vertices), and its size against the guns' 0.1 (the prop's world scale over 0.1: the
// goggles' model is drawn at 0.75 in the world). The root position node's offset is left out: with
// an anim (a held gun model always has one) modelUpdatePositionNodeMtx never applies the root's
// pos, so taking back the goggles' 13 cm and the IR scanner's 1.3 m head-height offsets put them
// that far (65 cm at their scale) below the hand. Read from the ROM's models (PchrnightsightZ,
// PxrayspecsZ, GirscannerZ, PrussdarZ, PchrcloakerZ, CeyespyZ, PchrbriefcaseZ); tune here.
struct vrheldfit {
    s32 weaponnum;
    f32 scale;
    f32 x;
    f32 y;
    f32 z;
    f32 hold[3]; // where it sits from the grip, cm: inward (toward the other hand), up, forward
    f32 roll;    // degrees about the controller's forward, its top turned inward
};

// The user's marks (hand, then where the object should be; left hand, controller level), 2026-10-22:
// the CamSpy 4.1 in, 4.2 up, 8.5 ahead, which the other objects follow; the briefcase 5.4 in, 11.1 up,
// 1.2 ahead. The model's +z is the controller's forward (the gun's -z is its back). The briefcase,
// marked again with the controller's rotation (left hand): rolled 87 degrees with its top inward,
// then moved 14.1 cm outward by a third pair (sideways only): 2.6 out, 6.0 up, 3.3 back; its middle,
// not its handle, sits there.
#define VR_HELD_HOLD { 4.1f, 4.2f, 8.5f }

static const struct vrheldfit g_VrHeldFits[] = {
    { WEAPON_NIGHTVISION,    5.0f,   0.0f,    0.5f,    0.0f, VR_HELD_HOLD }, // 40 x 9 x 26 units: 20 cm wide
    { WEAPON_XRAYSCANNER,    5.0f,   0.0f,    0.5f,    0.0f, VR_HELD_HOLD }, // the same frames
    { WEAPON_IRSCANNER,      0.5f,   0.0f,    0.5f,    0.0f, VR_HELD_HOLD }, // a 406 unit visor: 20 cm wide
    { WEAPON_RTRACKER,       2.0f,   0.5f,    0.5f,    0.0f, VR_HELD_HOLD }, // 59 x 19 x 100 units, 0.2 in the world
    { WEAPON_CLOAKINGDEVICE, 1.0f,   0.0f,    0.0f,    0.0f, VR_HELD_HOLD }, // a 184 unit disc
    { WEAPON_EYESPY,         1.0f, -14.5f,   73.5f,   -0.5f, VR_HELD_HOLD }, // the drone, 217 x 147 x 83 units
    // 380 x 99 x 384 units: 38 cm
    { WEAPON_DISGUISE40,     1.0f, -11.0f,    5.5f,  -80.0f, { -2.6f, 6.0f, -3.3f }, 87.0f },
    { WEAPON_DISGUISE41,     1.0f, -11.0f,    5.5f,  -80.0f, { -2.6f, 6.0f, -3.3f }, 87.0f },
    { WEAPON_DOORDECODER,    1.0f,   0.0f,    0.0f,    0.0f, VR_HELD_HOLD }, // 102 x 26 x 134 units
    { WEAPON_COMMSRIDER,     1.0f,   0.0f,    0.0f,    0.0f, VR_HELD_HOLD }, // PchrbugZ, held from the chest pouch
    { WEAPON_EXPLOSIVES,     1.0f,   0.0f,    0.0f,    0.0f, VR_HELD_HOLD }, // PexplosivebrickZ
    // G5 Building's Detonator only (vrHolsterIsG5Detonator): the detonator's box middle in its own
    // matrix's frame (GremotemineZ part 0x29, matrices 34 to 36: 32 x 173 x 71 units), placed by
    // vrG5DetonatorPlace
    { WEAPON_REMOTEMINE,     1.0f,   2.0f,   28.4f,   15.5f, VR_HELD_HOLD },
};

// Centres and sizes a held device's model on mtx, the hand's model matrix after the guns' 0.1 scale
static void vrHeldFitApply(s32 weaponnum, Mtxf *mtx)
{
    s32 i, j;

    for (i = 0; i < ARRAYCOUNT(g_VrHeldFits); i++) {
        const struct vrheldfit *fit = &g_VrHeldFits[i];

        if (fit->weaponnum != weaponnum) {
            continue;
        }

        for (j = 0; j < 3; j++) {
            // The axes are 0.1 long here, so 10 per cm. With +y up and +z forward the model's +x
            // is the right hand's left, its inward, and the left hand's model is mirrored.
            mtx->m[3][j] += (fit->hold[0] * mtx->m[0][j] + fit->hold[1] * mtx->m[1][j]
                    + fit->hold[2] * mtx->m[2][j]) * 10.0f;
        }

        if (fit->roll != 0.0f) {
            // The top (+y) toward the inward side, which is the model's -x here (the headset
            // showed +x rolling it the wrong way)
            f32 c = cosf(fit->roll * (M_PI / 180.0f));
            f32 s = -sinf(fit->roll * (M_PI / 180.0f));

            for (j = 0; j < 3; j++) {
                f32 x = mtx->m[0][j];
                f32 y = mtx->m[1][j];

                mtx->m[0][j] = c * x - s * y;
                mtx->m[1][j] = c * y + s * x;
            }
        }

        for (j = 0; j < 3; j++) {
            mtx->m[3][j] -= (fit->x * mtx->m[0][j] + fit->y * mtx->m[1][j] + fit->z * mtx->m[2][j]) * fit->scale;
        }

        mtx00015f04(fit->scale, mtx);
        return;
    }
}

// VR: G5 Building's Detonator (vrHolsterIsG5Detonator) is the remote mine's model, whose detonator
// (part 0x29) is on matrices 34 to 36 under the left hand. bgun0f098030 hides the arms and the
// mine; here the detonator's matrices go on the grip as a held device's (root, the hand's matrix
// with its g_VrHeldFits row applied), the button (35) and the antenna (36) at their rest offsets
// from 34. The model's other matrices draw nothing then.
#define VR_DETONATOR_MTX 34

static void vrG5DetonatorPlace(Mtxf *matrices, s32 nummatrices, Mtxf *root)
{
    static const f32 offsets[3][3] = {
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 67.78f, -2.64f },
        { -228.25f, 56.85f, -12.94f },
    };
    s32 i, j;

    if (nummatrices < VR_DETONATOR_MTX + 3) {
        return;
    }

    for (i = 0; i < 3; i++) {
        Mtxf *mtx = &matrices[VR_DETONATOR_MTX + i];

        mtx4Copy(root, mtx);

        for (j = 0; j < 3; j++) {
            mtx->m[3][j] += offsets[i][0] * root->m[0][j] + offsets[i][1] * root->m[1][j]
                + offsets[i][2] * root->m[2][j];
        }
    }
}



#define VR_MAX_GUN_PARTS 64
static s32 s_gunMovableMtxIndices[VR_MAX_GUN_PARTS];
static s32 s_gunMovableCount = 0;
static bool s_vrPartHidden[VR_MAX_GUN_PARTS] = {false};


void vrHideGunParts(Mtxf *matrices, s32 handnum)
{
    for (s32 i = 0; i < s_gunMovableCount; i++) {
        if (!s_vrPartHidden[i]) continue;

        s32 mtxindex = s_gunMovableMtxIndices[i];

        // --- MIRROR MODE HANDLING (DUAL WIELD / UNARMED) ---
        if (handnum == 1 && weaponnum != WEAPON_REMOTEMINE) {
            // If we were asked to hide right-hand matrices (1 to 16),
            // ignore them to avoid accidentally amputating the right hand.
            if (mtxindex >= 1 && mtxindex <= 16) {
                continue;
            }
            // If we were asked to hide left-hand matrices (17 to 32),
            // redirect them to the recycled right hand (1 to 16).
            if (mtxindex >= 17 && mtxindex <= 32) {
                mtxindex -= 16; // 17 becomes 1, 18 becomes 2 ... 32 becomes 16
            }
        }

        // Send the secret signal so that gfx_pc.cpp can hide the matrix
        matrices[mtxindex].m[0][3] = 2.0f;
    }
}


s32 HideAll[] = { -1};

s32 LeftHandMtx[] = {
        17, 18,19,20,21,22,23,24,25,26,27,28,29,30,31,32, //Lhand
        -1 // End of the list
};


// The left hand and the clip it holds: the clip's matrices are the owned gun's row's heldmtx (42 for
// the Falcon 2, 39 for the MagSec 4, 38 for the Mauler and the Tranquilizer) and, for a part skinned
// to a run of them, up to heldmtxlast (the Phoenix's orb: 37 to 39), written into the list on each
// call. A run that is backwards or longer than VR_RELOAD_HELD_MTX_MAX keeps heldmtx alone
// (vrReloadResetAll warns).
static const s32 *vrLeftHandAndMagMtx(const struct vrreloaddef *def)
{
    // Lhand (17 to 32), the row's held matrices, the end of the list
    static s32 list[16 + VR_RELOAD_HELD_MTX_MAX + 1];
    s32 n = def->heldmtxlast - def->heldmtx + 1;
    s32 i;

    if (def->heldmtxlast < 0 || n < 1 || n > VR_RELOAD_HELD_MTX_MAX) {
        n = 1;
    }

    for (i = 0; i < 16; i++) {
        list[i] = 17 + i;
    }

    for (i = 0; i < n; i++) {
        list[16 + i] = def->heldmtx + i;
    }

    list[16 + n] = -1;
    return list;
}

s32 ArmsMtx[] = {
        1, 17,// Arms
        -1 // End of the list
};



void vrHideAllExcept(const s32 *keepList)
{
    for (s32 i = 0; i < s_gunMovableCount; i++) {
        s32 mtxindex = s_gunMovableMtxIndices[i];
        bool keep = false;
        for (s32 k = 0; keepList[k] != -1; k++) {
            if (mtxindex == keepList[k]) {
                keep = true;
                break;
            }
        }
        s_vrPartHidden[i] = !keep;
    }
}


void vrHideOnly(const s32 *hideList)
{
    for (s32 i = 0; i < s_gunMovableCount; i++) {
        s32 mtxindex = s_gunMovableMtxIndices[i];

        bool hide = false;
        for (s32 k = 0; hideList[k] != -1; k++) {
            if (mtxindex == hideList[k]) {
                hide = true;
                break;
            }
        }
        s_vrPartHidden[i] = hide;
    }
}

static s32 sHandMtxIndices[VR_MAX_GUN_PARTS];
static s32 sHandMtxCount = 0;


void vrBuildHandIndexList(struct modeldef *handmodeldef) {
    sHandMtxCount = 0;
    if (!handmodeldef) return;

    struct modelnode *node = handmodeldef->rootnode;
    while (node) {
        u32 type = node->type & 0xff;
        if (type == MODELNODETYPE_POSITION || type == MODELNODETYPE_POSITIONHELD) {
            s32 idx = modelFindNodeMtxIndex(node, 0);

            // SAFETY GUARD: never exceed VR_MAX_GUN_PARTS
            if (idx >= 0 && sHandMtxCount < VR_MAX_GUN_PARTS) {
                sHandMtxIndices[sHandMtxCount++] = idx;
            }
        }
        if (node->child) node = node->child;
        else { while (node) { if (node->next) { node = node->next; break; } node = node->parent; } }
    }
}

static bool isHandIndex(s32 mtxindex) {
    for (s32 i = 0; i < sHandMtxCount; i++)
        if (sHandMtxIndices[i] == mtxindex) return true;
    return false;
}

void vrBuildMtxPartsList(struct hand *hand, struct modeldef *modeldef, bool filterHands) {
    s_gunMovableCount = 0;
    // VR: reset the hidden-part state whenever the movable-part list is rebuilt.
    // Otherwise stale "hidden" flags from a previous weapon/reload/throw persist
    // into the new list, making the gun and hands disappear (issue #43).
    for (s32 i = 0; i < VR_MAX_GUN_PARTS; i++) s_vrPartHidden[i] = false;
    if (!modeldef) return;

    struct modelnode *node = modeldef->rootnode;
    while (node) {
        u32 type = node->type & 0xff;
        if (type == MODELNODETYPE_POSITION || type == MODELNODETYPE_POSITIONHELD) {
            s32 mtxindex = modelFindNodeMtxIndex(node, 0);
            if (mtxindex >= 0 && (!filterHands || !isHandIndex(mtxindex))) {
                bool dup = false;
                for (s32 i = 0; i < s_gunMovableCount; i++)
                    if (s_gunMovableMtxIndices[i] == mtxindex) { dup = true; break; }

                // SAFETY GUARD: never exceed VR_MAX_GUN_PARTS
                if (!dup && s_gunMovableCount < VR_MAX_GUN_PARTS) {
                    s_gunMovableMtxIndices[s_gunMovableCount++] = mtxindex;
                }
            }
        }
        if (node->child) node = node->child;
        else { while (node) { if (node->next) { node = node->next; break; } node = node->parent; } }
    }
}


Mtxf vr_sp234, vr_sp284, vr_sp2c4, vr_sp164, vr_sp124;


// Global VrCopyWep / VrCopyHand variables - The unarmed left hand
// It is a copy of the right hand, but we hide the right hand and display the left hand instead.
// What is defined as the right hand in the original game actually contains
// both the left and right hands, but the left hand is simply hidden. It appears during reloading.

struct model g_VrCopyWepModel;
struct modeldef *g_VrCopyWepModeldef = NULL;
bool g_VrCopyWepFilemodel = false;
bool g_VrCopyWepReadyToRender = false;
Mtxf g_VrCopyWepSp2c4;
struct model g_VrCopyHandModel;
struct modeldef *g_VrCopyHandModeldef = NULL;
static bool vrSwitchCopyGun = true;
// Own anim state for the clip-in-hand pose, so holding the reload frame never touches the right
// hand's real gun animation (hand->anim).
static struct anim g_VrClipHoldAnim;
static bool g_VrClipHoldAnimReady = false;
static bool vrSwitchGun = true;

// Persistent real pointer (sysMemAlloc), separate from g_VrCopyWepModel.matrices
// which is reassigned every frame to a gfxAllocate buffer for rendering.
static Mtxf *g_VrCopyWepMatricesAlloc = NULL;
static u32  *g_VrCopyWepRwdatasAlloc  = NULL;
static u32  *g_VrCopyHandRwdatasAlloc = NULL;
//----------------------------------------------------------

// FISTS OFF-HAND ------------------------------------------------------------------------
// The single-wield off-hand used to be the GUN model's left-hand submesh skinned onto the gun
// skeleton's foregrip bones -- bone-welded to a grip pose, which is why no rigid roll/offset could
// ever align it. Instead render the real UNARMED fists model. That asset is already resident while
// armed as gunctrl.handmodeldef (measured: numparts 2, nummatrices 33, i.e. bones 1-16 = right hand,
// 17-32 = left hand) -- it is the very same file WEAPON_UNARMED loads as its gunmodeldef. We keep our
// OWN instance rather than reusing g_VrCopyHandModel, which is NULL for some weapons because
// vrCopyWepLoad can early-return before initialising it.
struct model     g_VrFistModel;
struct anim      g_VrFistAnim;      // own anim state, so the clench can't disturb the real hands
struct modeldef *g_VrFistModeldef = NULL;
static u32      *g_VrFistRwdatasAlloc = NULL;
static s32       g_VrFistFilenum = -1;
static Mtxf      g_VrFistSp2c4;
static Mtxf      g_VrFistOpenSp2c4; // the same root with the open hand's roll, for vrReloadNoteFistWrist
// The open-pose pass's matrices: never drawn, so not from display-list memory. The fists model has
// 33 matrices (measured above); a larger one gets no capture.
#define VR_FIST_OPEN_MAX_MTX 40
static Mtxf      g_VrFistOpenMtx[VR_FIST_OPEN_MAX_MTX];
static bool      g_VrFistReady = false;

// The fists root from the free left-hand frame for a clench amount (0 open, 1 closed): the grip roll
// about the hand's own forward axis, to line the thumb up with the controller (the clenched anim-1002
// hand sits at a different orientation than the rest pose, so the roll follows the clench), then the
// mirror and the model scale. Safe at the root (unlike the legacy hand, whose origin sat at the
// elbow so a root roll swung the wrist): this is a first-class hand model and vr_wrist_rot re-aims
// the forearm afterwards, so the arm stays connected.
static void vrFistRoot(const Mtxf *base, f32 clench, Mtxf *out)
{
    float a = (VR_FIST_ROLL + clench * VR_FIST_ROLL_CLENCH) * (float) (M_PI / 180.0);
    float ca, sa;
    s32 c;

    *out = *base;

    if (VrLeftHandedMode) {
        a = -a;
    }

    ca = cosf(a);
    sa = sinf(a);

    for (c = 0; c < 3; c++) {          // rotate rows 0/1 about row 2 (local forward)
        float va = out->m[0][c];
        float vb = out->m[1][c];
        out->m[0][c] = va * ca - vb * sa;
        out->m[1][c] = va * sa + vb * ca;
    }

    // Render bones 1-16 (the RIGHT hand) mirrored, exactly how the unarmed left hand is built
    // (mtx00015e24 negates row 0).
    if (!VrLeftHandedMode) {
        mtx00015e24(-1, out);
    }

    mtx00015f04(0.10000001f, out);
}


static void quaternionMul_XR(const XrQuaternionf *a, const XrQuaternionf *b, XrQuaternionf *out)
{
    out->x = a->w * b->x + a->x * b->w + a->y * b->z - a->z * b->y;
    out->y = a->w * b->y - a->x * b->z + a->y * b->w + a->z * b->x;
    out->z = a->w * b->z + a->x * b->y - a->y * b->x + a->z * b->w;
    out->w = a->w * b->w - a->x * b->x - a->y * b->y - a->z * b->z;
}



static void rotvec(float *vx, float *vy, float *vz,
                   float qw, float qx, float qy, float qz)
{
    float tx = 2.0f*(qy*(*vz) - qz*(*vy));
    float ty = 2.0f*(qz*(*vx) - qx*(*vz));
    float tz = 2.0f*(qx*(*vy) - qy*(*vx));
    *vx += qw*tx + qy*tz - qz*ty;
    *vy += qw*ty + qz*tx - qx*tz;
    *vz += qw*tz + qx*ty - qy*tx;
}

// Turn the forearm bone about the wrist (handMtx pivot) toward a point (the solved elbow), by blend
// of the angle, preserving the forearm length. Rotating about the pivot keeps the wrist vertex fixed
// (joint stays connected -- unlike a raw position set, which compresses the bone and tears the wrist).
// Only the direction of (ax,ay,az) matters; distance is ignored because length is preserved.
static void tuck_elbow(Mtxf *armMtx, Mtxf *handMtx, float ax, float ay, float az, float blend) {
    float px = handMtx->m[3][0], py = handMtx->m[3][1], pz = handMtx->m[3][2];  // wrist pivot
    float dcx = armMtx->m[3][0] - px, dcy = armMtx->m[3][1] - py, dcz = armMtx->m[3][2] - pz; // wrist->elbow
    float L = sqrtf(dcx*dcx + dcy*dcy + dcz*dcz);
    if (L < 0.0001f) return;
    float ddx = ax - px, ddy = ay - py, ddz = az - pz;                          // wrist->anchor
    float dl = sqrtf(ddx*ddx + ddy*ddy + ddz*ddz);
    if (dl < 0.0001f) return;
    float ux = dcx/L, uy = dcy/L, uz = dcz/L;
    float vx = ddx/dl, vy = ddy/dl, vz = ddz/dl;
    float dot = ux*vx + uy*vy + uz*vz;
    if (dot > 1.0f) dot = 1.0f; if (dot < -1.0f) dot = -1.0f;
    if (dot > 0.9999f) return;
    float rax = uy*vz - uz*vy, ray = uz*vx - ux*vz, raz = ux*vy - uy*vx;        // rotation axis
    float al = sqrtf(rax*rax + ray*ray + raz*raz);
    if (al < 0.0001f) return;
    rax /= al; ray /= al; raz /= al;
    float ang = acosf(dot) * blend;
    float s = sinf(ang * 0.5f), qw = cosf(ang * 0.5f);
    float qx = rax*s, qy = ray*s, qz = raz*s;
    rotvec(&armMtx->m[0][0], &armMtx->m[0][1], &armMtx->m[0][2], qw, qx, qy, qz);
    rotvec(&armMtx->m[1][0], &armMtx->m[1][1], &armMtx->m[1][2], qw, qx, qy, qz);
    rotvec(&armMtx->m[2][0], &armMtx->m[2][1], &armMtx->m[2][2], qw, qx, qy, qz);
    rotvec(&dcx, &dcy, &dcz, qw, qx, qy, qz);   // length preserved
    armMtx->m[3][0] = px + dcx;
    armMtx->m[3][1] = py + dcy;
    armMtx->m[3][2] = pz + dcz;
}



// The arm: the hands model draws a forearm (matrix 1, its origin the elbow) and a hand (matrix 2,
// the wrist), 17 and 18 for the left arm, and no upper arm. The gun animation welds the forearm to
// the hand, so on its own it swings with the controller. vr_arm_solve finds the elbow from a
// shoulder placed from the body (an upper arm and the model's forearm, bent down, out, back and
// toward the hand's little finger side) and turns the forearm about the wrist onto it. The turn away from the
// animation's forearm stays under a real wrist's bend: past it the forearm follows the hand instead
// of the shoulder, which shows nowhere, as no upper arm is drawn. Measured on the ntsc-final hands
// (GcombathandslodZ, tools/vr_measure): the hand bone 329.9 model units (33.0 cm) along the
// forearm, and the knuckles along the hand bone's x, the little finger toward -x on the right hand
// (matrix 2) and +x on the left (18, its mirror).
#define VR_ARM_FOREARM_CM   32.99f  // elbow to wrist in the hands model
#define VR_ARM_UPPER_CM     27.0f   // shoulder to elbow: 60 cm of reach with the forearm
#define VR_ARM_SHOULDER_X   18.0f   // the shoulder joint in the body frame (vrBodyPoint: cm from
#define VR_ARM_SHOULDER_Y  -24.0f   // the head, x toward the arm's side, y up, z back)
#define VR_ARM_SHOULDER_Z    8.0f
#define VR_ARM_POLE_DOWN     1.0f   // the elbow bends down,
#define VR_ARM_POLE_OUT      0.4f   // out to the arm's side,
#define VR_ARM_POLE_BACK     0.5f   // back,
#define VR_ARM_POLE_FINGER   0.5f   // and toward the hand's little finger side (under 1: see below)
#define VR_ARM_MAX_BEND_DEG 60.0f   // the most the forearm turns from the animation's at the wrist

static f32 vr_arm_norm(f32 *v)
{
    f32 len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);

    if (len > 0.0001f) {
        v[0] /= len;
        v[1] /= len;
        v[2] /= len;
    }

    return len;
}

// v less its part along unit u, normalized; the length before normalizing
static f32 vr_arm_perp(f32 *v, const f32 *u)
{
    f32 dot = v[0] * u[0] + v[1] * u[1] + v[2] * u[2];

    v[0] -= dot * u[0];
    v[1] -= dot * u[1];
    v[2] -= dot * u[2];

    return vr_arm_norm(v);
}

// The forearm (armmtx) turned about the wrist (handmtx) onto the solved elbow. side: the arm's
// physical side, +1 right, -1 left. finger: the little finger's side along the hand bone's x, -1
// for a right hand's bone (2) and +1 for a left hand's (18). The matrices' rows carry the mirror a
// mirrored hand is drawn with, so the drawn little finger is that side of the stored row either way.
static void vr_arm_solve(Mtxf *matrices, s32 armmtx, s32 handmtx, f32 side, f32 finger)
{
    Mtxf *arm = &matrices[armmtx];
    Mtxf *hand = &matrices[handmtx];
    f32 body[3], shoulder[3], down[3], out[3], back[3];
    f32 anim[3], u[3], pole[3], fing[3], f[3];
    f32 lf, lu, k, d, along, r, dot, bend, turn;
    f32 maxbend = VR_ARM_MAX_BEND_DEG * (f32) (M_PI / 180.0);
    s32 i;

    for (i = 0; i < 3; i++) {
        anim[i] = arm->m[3][i] - hand->m[3][i];  // wrist to elbow, as animated
        fing[i] = finger * hand->m[0][i];
    }

    lf = vr_arm_norm(anim);

    if (lf < 0.0001f || vr_arm_norm(fing) < 0.0001f) {
        return;
    }

    // Matrix units per cm: the gun's matrices are camera space times bg_scale, the copy model's and
    // the fists' have no bg_scale, and the forearm is drawn the model's length in each
    k = lf / VR_ARM_FOREARM_CM;
    lu = VR_ARM_UPPER_CM * k;

    body[0] = VR_ARM_SHOULDER_X * side;
    body[1] = VR_ARM_SHOULDER_Y;
    body[2] = VR_ARM_SHOULDER_Z;
    vrBodyPointMirror(body, 0, shoulder);

    for (i = 0; i < 3; i++) {
        u[i] = shoulder[i] * k - hand->m[3][i];  // wrist to shoulder
    }

    d = vr_arm_norm(u);

    if (d < 0.0001f) {
        return;
    }

    // The elbow's circle about the wrist-shoulder line: a reach past the arm's length straightens
    // the arm toward the shoulder
    if (d > (lu + lf) * 0.999f) {
        d = (lu + lf) * 0.999f;
    }

    if (d < fabsf(lu - lf) + 0.001f * lf) {
        d = fabsf(lu - lf) + 0.001f * lf;
    }

    along = (lf * lf - lu * lu + d * d) / (2.0f * d);
    r = lf * lf - along * along;
    r = r > 0.0f ? sqrtf(r) : 0.0f;

    // The elbow's side on that circle: down, out and back from the body, normalized across the
    // wrist-shoulder line, plus the little finger's side at half that, so the hand's roll moves
    // the elbow but can never cancel the body's hints (the old fixed direction flipped where it
    // pointed against the forearm). Undefined only with the shoulder straight along the body's
    // hints from the wrist (the hand up and across the face), where it takes the side.
    body[0] = 0.0f;
    body[1] = -1.0f;
    body[2] = 0.0f;
    vrBodyPointMirror(body, 0, down);
    body[0] = side;
    body[1] = 0.0f;
    vrBodyPointMirror(body, 0, out);
    body[0] = 0.0f;
    body[2] = 1.0f;
    vrBodyPointMirror(body, 0, back);

    for (i = 0; i < 3; i++) {
        pole[i] = down[i] * VR_ARM_POLE_DOWN + out[i] * VR_ARM_POLE_OUT + back[i] * VR_ARM_POLE_BACK;
    }

    if (vr_arm_perp(pole, u) < 0.001f) {
        if (vr_arm_perp(out, u) < 0.001f) {
            return;
        }

        pole[0] = out[0];
        pole[1] = out[1];
        pole[2] = out[2];
    }

    // Not normalized across the line: a finger along it fades out rather than swinging the elbow
    dot = fing[0] * u[0] + fing[1] * u[1] + fing[2] * u[2];

    for (i = 0; i < 3; i++) {
        pole[i] += (fing[i] - dot * u[i]) * VR_ARM_POLE_FINGER;
    }

    vr_arm_perp(pole, u);

    for (i = 0; i < 3; i++) {
        f[i] = (u[i] * along + pole[i] * r) / lf;  // wrist to the solved elbow, unit
    }

    // The wrist's bend: a turn from the animation's forearm up to maxbend is taken whole; past it
    // the turn falls back to none at a forearm opposite the animation's, so the turn's direction,
    // undefined there, never flips the forearm (a hand turned against the arm keeps its forearm)
    dot = anim[0] * f[0] + anim[1] * f[1] + anim[2] * f[2];
    bend = acosf(dot > 1.0f ? 1.0f : dot < -1.0f ? -1.0f : dot);

    if (bend > maxbend) {
        turn = maxbend * ((f32) M_PI - bend) / ((f32) M_PI - maxbend);

        if (vr_arm_perp(f, anim) < 0.0001f) {
            return;
        }

        for (i = 0; i < 3; i++) {
            f[i] = anim[i] * cosf(turn) + f[i] * sinf(turn);
        }
    }

    tuck_elbow(arm, hand, hand->m[3][0] + f[0], hand->m[3][1] + f[1], hand->m[3][2] + f[2], VrArmElbowTuck);
}

// primarySide: +1 when matrices[1]/[2] is the gun-hand side's arm (the RIGHT hand's gun), -1 when
// it is the other side's (the dual-wield copy weapon, the fists); mirrored by the left-handed mode.
//
// The arms are matrices 1 and 2 (right) and 17 and 18 (left) of a model with hands: the hands model
// has 33 matrices (bones 1 to 32) and is drawn with its gun model's matrix array, which holds the gun
// model's nummatrices (bgun0f0a5550), so a gun drawn with hands has more than 32. A gun with fewer has
// no hands and is left alone: the classic KL01313, KF7 Special, DMC and RC-P45 models have 4 matrices,
// the ZZT 5 and the AR53 3 (ntsc-final ROM), so 17 and 18 are past the end of the array, and 1 and 2
// are the muzzle position (part 0x32) and its child (0x50), which the solve would move. The fists
// (unarmed) are loaded as the gun model, with no hands model, and have 33 matrices: they have arms.
#define VR_HAND_MTX_LAST 32

// VR: a model with hands: its matrix array holds the hands' bones 1 to 32 (see above)
static bool vrModelHasHands(const struct modeldef *modeldef)
{
    return modeldef != NULL && modeldef->nummatrices > VR_HAND_MTX_LAST;
}

void vr_wrist_rot(struct modeldef *modeldef, Mtxf *matrices, float primarySide) {
    f32 side = primarySide * (VrLeftHandedMode ? -1.0f : 1.0f);

    if (VrHideArms || VrArmElbowTuck <= 0.0f) return;

    if (matrices == NULL || !vrModelHasHands(modeldef)) return;

    vr_arm_solve(matrices, 1, 2, side, -1.0f);
    vr_arm_solve(matrices, 17, 18, -side, 1.0f);
}

// Manual reload: the copy model's left arm, holding a clip, placed as the free fists' arm
// (vrReloadPlaceHeldArm), is solved as the fists' arm: the fists' visible arm is their matrix 1,
// on the off-hand's side (VR_FIST_SIDE); vr_wrist_rot's 17 on the copy model would be the other
// side's. Task F9c.
static void vr_tuck_held_arm(Mtxf *matrices, s32 armmtx, s32 handmtx) {
    if (VrHideArms || VrArmElbowTuck <= 0.0f) return;

    vr_arm_solve(matrices, armmtx, handmtx, VR_FIST_SIDE * (VrLeftHandedMode ? -1.0f : 1.0f), 1.0f);
}



static Mtxf g_VrLeftHandFreeSp2c4; // Free left hand position: the clip-in-hand pose is drawn there

static bool  VrTwoHandGrip     = false;
// The free hand's grip has held a clip, slapped or racked, and is not yet let go: not the support hand
static bool  VrTwoHandReloadGrip = false;

// The gun hand's gun is aimed with both hands. The scopes and the zoom read only this: the lens
// shows (vrScopeBegin), the Sniper Rifle's and the Farsight's zoom work (bondmove.c) and the gun
// hand's rotation is smoothed (vr_input.cpp) only while it is true. The grip alone never counts:
// with holsters it is held the whole time a gun is in the hand. Today it is the rifle-only gate
// (VrTwoHandsGun and the free hand's grip, VrTwoHandGrip), so the pistols with a scope (the MagSec 4,
// the Falcon 2 Scope) never qualify (Stage D, which would have extended it, was dropped; the Falcon 2
// Scope's lens shows one-handed instead, vrScopeBegin). With the Farsight or the Rocket Launcher it
// also puts the player in sight mode (bondmove.c, Task J8a). A free
// hand holding a clip is reloading, not aiming. In VR always, holsters on or off.
// VrTwoHandGrip is written only while bgun0f0a5550 draws the gun, which it skips in cutscenes, the
// eyespy, menus and while the hand is hidden, but controller_pose and vr_update_head_tracking read
// this every frame: so the states where the gun is not drawn or used (vrScopeBegin's) are false here.
// The gun is the right hand's (bgunGetWeaponNum), as bondmove.c's zoom gates read it.
bool vrTwoHandAiming(void)
{
    struct player *player = g_Vars.currentplayer;

    if (!player
        || player->cameramode != CAMERAMODE_DEFAULT
        || player->activemenumode != AMMODE_CLOSED
        || player->gunctrl.passivemode
        || player->bondhealth <= 0.0f) {
        return false;
    }

    if (!VrTwoHandGrip || !VrTwoHandsGun(bgunGetWeaponNum(HAND_RIGHT))) {
        return false;
    }

    return !vrReloadHandHoldsClip(HAND_RIGHT);
}

// The free hand grips a two-handed gun as its support hand (VrTwoHandGrip): the grip held, and not a
// grip that holds or has held a clip, slapped or racked, not yet let go (VrTwoHandReloadGrip). For
// the support hand's effects that read the grip each shot (the halved spread, the two-hand recoil
// profile in vr_input.cpp, the support hand's fire buzz in input.c). As VrTwoHandGrip, it is written
// only while bgun0f0a5550 draws the gun.
bool vrTwoHandGripHeld(void)
{
    return VrTwoHandGrip;
}

// A new level or life: a reload grip latched before it (VrTwoHandReloadGrip) does not carry over
void bgunTwoHandReset(void)
{
    VrTwoHandReloadGrip = false;
}

// The game's own full screen zoom is on: the Farsight's, the one zoom gun without a lens (the lens
// guns keep the default view, playerUpdateZoom), in sight mode and aimed with two hands (bondmove.c
// gates its zoom on that). vr_openxr.cpp steadies the head only then.
bool vrZoomViewOn(void)
{
    struct player *player = g_Vars.currentplayer;

    return player && player->insightaimmode
        && bgunGetWeaponNum(HAND_RIGHT) == WEAPON_FARSIGHT && vrTwoHandAiming();
}

// The CMP150's Follow Lock-on, its secondary function, in the gun hand (Task J7, the user: "how are we
// supposed to use cmp's lock on aiming behavior if we cant two handed aim?"). The original locks
// targets only in sight mode (R held) and then swivels the crosshair onto the first locked one, and
// shots go to the crosshair. In VR no grip holding a gun sends R (vrHolsterGripAimBlocked), the
// CMP150 is not two-handed, and shots go where the controller points, so the lock never started and
// a lock would steer nothing. Sight mode gives the CMP150 nothing else (no zoom, the same spread), so
// with this function selected targets lock without it (sightTick) and the shots go to the target the
// original would follow (bgunCalculatePlayerShotSpread)
bool vrCmpFollowLockOn(void)
{
    struct player *player = g_Vars.currentplayer;

    return player != NULL && bgunGetWeaponNum(HAND_RIGHT) == WEAPON_CMP150
        && player->hands[HAND_RIGHT].gset.weaponfunc == FUNC_SECONDARY;
}

// ===== TWO-HANDED AIMING ====================================================================
// Two-handed weapons aim along the line between the two controllers, not just the primary
// wrist. Position is untouched -- the gun still hangs off the trigger hand exactly as before,
// it only rotates -- so this cannot disturb the grip alignment. Roll still comes from the
// primary wrist, since a two-point line cannot define it.

// Player preference, on the VR options menu. Off restores wrist-only aiming for every weapon,
// which some players will prefer -- bracing with the off-hand is more physical but asks more of
// them, and a seated or one-handed player cannot use it at all.
bool VrTwoHandAim = true;

// Hand separation over which aim fades from the wrist to the hand line. The lower bound is not a
// comfort setting: controllers pressed physically together still read ~9 apart (measured with bulky
// PSVR2 controllers; slimmer ones bottom out lower, which only widens the fallback). Aim
// sensitivity scales as 1/separation, so without this the gun turns violently as the hands close.
// The upper bound sits below any real bracing hold -- measured 20-27 -- so a natural grip always
// gets full authority.
#define VR_2H_SEP_MIN   9.0f
#define VR_2H_SEP_MAX  18.0f
#define VR_2H_EASE      0.15f   // per-tick engage/release easing, so the aim never snaps

// Where each weapon's support grip sits relative to its barrel, in degrees of yaw about the gun's
// up axis (+ = to the gun's LEFT). Zero -- straight down the barrel -- is correct for every
// conventional two-hander, so only the exceptions are listed. Both exceptions hold the off-hand
// square to the barrel rather than ahead of it: the Rocket Launcher on a horizontal handle out to
// the left, the Reaper on the second of two symmetrical grips. Dialled in-headset; deriving these
// from the models' own hand bones does not work, because a model's root is an arbitrary authoring
// origin rather than the trigger grip, so bone offsets are not comparable between weapons.
static f32 vrTwoHandAxisYaw(s32 weaponnum)
{
    switch (weaponnum) {
        case WEAPON_ROCKETLAUNCHER:
        case WEAPON_REAPER:
            return 90.0f;
        default:
            return 0.0f;
    }
}

static float sTwoHandAmt = 0.0f;
static u32   sTwoHandEaseFrame = 0xffffffff;

// Build the gun's orientation from a controller quaternion, applying the two-handed aim
// override on top. BOTH writers of hand->posrotmtx must go through here: vr_gun_pos_rot builds
// it normally, but bgun0f09a6f8 rebuilds it from scratch every recoil frame, and would
// otherwise silently stomp the override exactly while firing.
static void vrBuildGunRotation(s32 handnum, struct hand *hand, const f32 quat[4], Mtxf *out)
{
    quaternionToMtx((f32 *)quat, out);

    // Reset the engage amount while disabled, so switching the option back on mid-game starts from
    // one-handed rather than resuming a stale blend.
    if (!VrTwoHandAim) { sTwoHandAmt = 0.0f; return; }

    if (handnum != HAND_RIGHT) return;
    if (!vrTwoHandAimGun(g_Vars.currentplayer->gunctrl.weaponnum)) return;

    // Ease in/out so engaging or releasing the grip doesn't snap the aim. Guarded to advance once
    // per frame: while firing, bgun0f09a6f8 calls this in the same frame vr_gun_pos_rot already
    // did, which would otherwise make the grip engage twice as fast during a burst as at rest.
    if (sTwoHandEaseFrame != g_Vars.lvframe60) {
        f32 want = VrTwoHandGrip ? 1.0f : 0.0f;
        sTwoHandEaseFrame = g_Vars.lvframe60;
        sTwoHandAmt += (want - sTwoHandAmt) * VR_2H_EASE;
    }
    if (sTwoHandAmt <= 0.001f) return;

    s32 pri = (!vr_invert_hands) ? 1 : 0;   // primary/trigger controller
    s32 off = 1 - pri;                      // support hand

    // Direction between the physical controllers, in the RAW OpenXR position frame.
    f32 dx = gCtrlPos[off][0] - gCtrlPos[pri][0];
    f32 dy = gCtrlPos[off][1] - gCtrlPos[pri][1];
    f32 dz = gCtrlPos[off][2] - gCtrlPos[pri][2];
    f32 sep = sqrtf(dx*dx + dy*dy + dz*dz);
    if (sep < 1e-4f) return;
    dx /= sep; dy /= sep; dz /= sep;

    // Fade back to wrist aim as the hands close, where the line between them stops being a usable
    // pointing direction.
    f32 sepW = (sep - VR_2H_SEP_MIN) / (VR_2H_SEP_MAX - VR_2H_SEP_MIN);
    sepW = CLAMP(sepW, 0.0f, 1.0f);
    f32 blend = sTwoHandAmt * sepW;
    if (blend <= 0.001f) return;

    // FRAME: gCtrlPos is raw OpenXR, but this matrix is consumed in a different basis. Settled by
    // measurement rather than derivation (the two consumers disagree -- the shot path post-negates
    // Y while the visual path composes a 180deg Y): negating Y alone lines the gun up with the
    // physical hand line, which is consistent with it cancelling the shot path's own Y flip.
    dy = -dy;

    // GRIP AXIS: the direction from the trigger grip toward the support grip, in the GUN's own
    // frame. A conventional foregrip lies straight down the barrel at {0,0,-1}; the exceptions in
    // vrTwoHandAxisYaw swing it about the gun's up axis. Yaw alone covered both of them, so there
    // is deliberately no pitch term.
    f32 yrad = vrTwoHandAxisYaw(g_Vars.currentplayer->gunctrl.weaponnum) * 0.017453292f;
    f32 ax   = -sinf(yrad), ay = 0.0f, az = -cosf(yrad);

    if (VrLeftHandedMode) {
        ax = -ax;
    }

    // The grip axis in world. At yaw 0 this is identically the gun's forward, which is what makes
    // the whole block reduce to a plain "point the barrel down the hand line" for every
    // conventional weapon -- the behaviour those weapons were validated with.
    f32 vx = ax*out->m[0][0] + ay*out->m[1][0] + az*out->m[2][0];
    f32 vy = ax*out->m[0][1] + ay*out->m[1][1] + az*out->m[2][1];
    f32 vz = ax*out->m[0][2] + ay*out->m[1][2] + az*out->m[2][2];

    // Blend it toward the physical hand line. blend==0 leaves it untouched.
    f32 tx = vx + (dx - vx) * blend;
    f32 ty = vy + (dy - vy) * blend;
    f32 tz = vz + (dz - vz) * blend;
    f32 tl = sqrtf(tx*tx + ty*ty + tz*tz);
    if (tl < 1e-4f) return;
    tx /= tl; ty /= tl; tz /= tl;

    // Rotate the gun by the shortest arc carrying the grip axis onto that target, then read off
    // where the barrel ended up. A shortest-arc rotation introduces no twist about itself, so
    // primary-wrist roll survives exactly as it did when the barrel was aimed directly.
    f32 kx = vy*tz - vz*ty;
    f32 ky = vz*tx - vx*tz;
    f32 kz = vx*ty - vy*tx;
    f32 s  = sqrtf(kx*kx + ky*ky + kz*kz);
    if (s < 1e-6f) return;   // already aligned (or exactly opposed): nothing well-defined to do
    f32 c  = vx*tx + vy*ty + vz*tz;
    kx /= s; ky /= s; kz /= s;

    // Rodrigues, applied to the current barrel direction.
    f32 fx = -out->m[2][0], fy = -out->m[2][1], fz = -out->m[2][2];
    f32 kd = kx*fx + ky*fy + kz*fz;
    f32 bx = fx*c + (ky*fz - kz*fy)*s + kx*kd*(1.0f - c);
    f32 by = fy*c + (kz*fx - kx*fz)*s + ky*kd*(1.0f - c);
    f32 bz = fz*c + (kx*fy - ky*fx)*s + kz*kd*(1.0f - c);
    f32 bl = sqrtf(bx*bx + by*by + bz*bz);
    if (bl < 1e-4f) return;
    bx /= bl; by /= bl; bz /= bl;

    // mtx00016b58 negates `look` internally (row2 = -normalize(look)), so rotating the model's
    // {0,0,-1} forward by the result yields `look` itself. Up comes from row 1 of the wrist-derived
    // rotation: already in the right frame, and what keeps primary-wrist roll working for free.
    mtx00016b58(out, 0.0f, 0.0f, 0.0f, bx, by, bz,
                out->m[1][0], out->m[1][1], out->m[1][2]);
}

// ===== THE GUNS' KICK (Tasks G0d and H4) ==================================================
// User, 2026-10-19: "shotgun also has so much kick when fired that it needs to move more in space by
// the player or something, right now having just the hand jerk up seems odd". The engine's recoil is
// zero for the Shotgun (recoildist and recoilangle 0); the kick seen was its fire animation (the gun
// and the model's hands 8 cm back and 5 down at frames 7 to 10, then an 80 degree muzzle-up pump
// flourish with the trigger hand dropping 14 cm, back by frame 72: not played while manual reload
// pumps it, bgunTickIncAttackingShoot) and vr_input.cpp's spring on the gun controller's pose (up to 23
// degrees and 20 cm, its translation not turned into the gun's frame, discarded by the two-hand aim:
// off while this kick runs, GetRecoilProfileForWeapon). With manual reload on (vrGunKickDef) the drawn
// gun pose is kicked instead: back along the barrel and the muzzle up, with about a degree sideways
// at random, each shaped A (t/tp) e^(1-t/tp) with tp 40 ms (the peak at 40 ms, under 10% by 200 ms),
// timed in game time so it is the same at any frame rate. Applied to hand->posrotmtx after vrBuildGunRotation, by both of its writers
// (vr_gun_pos_rot and the recoil frames of bgun0f09aba4), so the two-hand aim keeps it and everything
// drawn from that matrix (the gun, its hands, the reload's grab areas captured from it) moves with
// it. gCtrlPos and gCtrlQuat are untouched: the aim's hand line, the holsters and the free hand's
// grabs stay on the real controllers. Never the camera. The gun turns about the gun controller (the
// grip offset's pivot in the render). The shot's own direction is read before the kick starts; a
// double blast's second shell, 13 ticks on, leaves within 0.7 degrees of it.
//
// The revolvers (Task H4), user, 2026-10-20: "revolver works but the kick on it feels so awkward in
// VR". The DY357's fire animation hardly moves the gun (0.75 degrees); the kick was the engine's recoil
// (bgun0f09aba4: the muzzle 35 degrees up over 8 ticks and back over 16, the LX's 24, in 60 Hz steps,
// on the gun controller's pose) plus vr_input.cpp's spring (12.7 degrees and 3 cm): about 48 degrees
// at 139 ms, a half-second drift and judder at 72 or 90 Hz. The DY357 and DY357-LX now kick as the
// Shotgun does, with their own values, whatever the reload setting (the user's complaint was the feel
// in VR, and their fire animation plays either way: the hammer, the cylinder and the finger). The
// engine's turn and push are zeroed for a gun with a kick (bgun0f09aba4; its timing, so the lockout
// between shots, is unchanged) and the spring is off (vrGunKickOwnsRecoil). The kick is kept per hand:
// a pair's left revolver kicks too and buzzes its own controller. The render mirrors a gun side's model
// by negating its x row after posrotmtx (bgun0f0a5550), so the kick's turns about the gun's own axes
// hold on either hand: the muzzle goes up, the side turn (random either way) is only mirrored.
struct vrgunkick {
    f32 peak60ths;      // tp
    f32 end60ths;       // the kick ends, about 1% of the peak left (t/tp 7.5)
    f32 back2hcm;       // back along the barrel, the support hand gripping ...
    f32 back1hcm;       // ... one-handed
    f32 rise2hdeg;      // the muzzle up
    f32 rise1hdeg;
    f32 sidedeg;        // sideways, at random either way up to this
    f32 buzz1amp;       // the shot: a hard buzz on the controller of the hand that fired, and on the
    f32 buzz1secs;      // support controller while it grips ...
    f32 buzz2amp;       // ... then a softer one
    f32 buzz2secs;
    f32 buzz2at60ths;
};

// The Shotgun (Task G0d): the peak at 40 ms, under 10% by 200 ms
static const struct vrgunkick sVrKickShotgun = {
    .peak60ths = 2.4f, .end60ths = 18.0f,
    .back2hcm = 5.0f, .back1hcm = 7.0f, .rise2hdeg = 6.0f, .rise1hdeg = 10.0f, .sidedeg = 1.0f,
    .buzz1amp = 1.0f, .buzz1secs = 0.07f, .buzz2amp = 0.4f, .buzz2secs = 0.12f, .buzz2at60ths = 4.2f,
};

// The DY357 and DY357-LX (Task H4): the peak at 50 ms, under 10% by 245 ms; one-handed guns, so the
// gripping values are the same. Over by 375 ms, before the next shot can go (the lockout: 44 ticks,
// the LX 62)
static const struct vrgunkick sVrKickRevolver = {
    .peak60ths = 3.0f, .end60ths = 22.5f,
    .back2hcm = 4.0f, .back1hcm = 4.0f, .rise2hdeg = 20.0f, .rise1hdeg = 20.0f, .sidedeg = 1.5f,
    .buzz1amp = 1.0f, .buzz1secs = 0.07f, .buzz2amp = 0.5f, .buzz2secs = 0.15f, .buzz2at60ths = 4.2f,
};

// Per hand (HAND_RIGHT, HAND_LEFT)
static f32  sVrKickT[2] = { -1.0f, -1.0f };     // 60ths of game time since the shot; -1: no kick
static s32  sVrKickFrame[2] = { -1, -1 };       // lvframenum of its last step (once per frame)
static const struct vrgunkick *sVrKickDef[2];   // the gun's kick that started it
static f32  sVrKickBack[2];                     // this shot's amplitudes: cm, degrees, degrees (the
static f32  sVrKickRise[2];                     // side signed)
static f32  sVrKickSide[2];
static bool sVrKickBuzz2[2];                    // the second buzz is still to come

// The kick of the hand's gun, or NULL. The Shotgun on the gun hand while the player pumps it (manual
// reload's pumpaction row), the same test that skips its fire animation (bgunTickIncAttackingShoot):
// with manual reload off it plays its fire animation, with its own kick, and keeps vr_input.cpp's
// spring and the engine's rumble. The revolvers in either hand (the left one of a pair while that hand
// is in use), whatever the reload setting
static const struct vrgunkick *vrGunKickDef(s32 handnum)
{
    struct player *player = g_Vars.currentplayer;
    struct vrreloaddef *def;
    s32 weaponnum;

    // VR is single-player: these statics never follow another player's gun
    if (player == NULL || g_Vars.currentplayernum != 0 || handnum < 0 || handnum >= 2) {
        return NULL;
    }

    if (handnum == HAND_LEFT && !player->hands[HAND_LEFT].inuse) {
        return NULL;
    }

    if (handnum == HAND_RIGHT) {
        def = vrReloadOwnedDef(HAND_RIGHT);

        if (def != NULL && def->pumpaction) {
            return &sVrKickShotgun;
        }
    }

    weaponnum = player->hands[handnum].gset.weaponnum;

    if (weaponnum == WEAPON_DY357MAGNUM || weaponnum == WEAPON_DY357LX) {
        return &sVrKickRevolver;
    }

    return NULL;
}

// For vr_input.cpp: the gun hand's recoil is this kick, so its spring stays off (a pair is one weapon,
// so the gun hand's answer holds for the left gun too)
bool vrGunKickOwnsRecoil(void)
{
    return vrGunKickDef(HAND_RIGHT) != NULL;
}

// The two buzzes: on the controller of the hand that fired (the left hand's on the off controller,
// trigger_haptic_vibration flipping both for the gun side), and on the support controller while it
// grips the gun hand's gun
static void vrGunKickBuzz(s32 handnum, f32 amp, f32 secs)
{
    if (handnum == HAND_LEFT) {
        vrReloadBuzz(VRR_CTRL_OFF, amp, secs);
        return;
    }

    vrReloadBuzz(VRR_CTRL_GUN, amp, secs);

    if (VrTwoHandsGun(g_Vars.currentplayer->gunctrl.weaponnum) && vrTwoHandGripHeld()) {
        vrReloadBuzz(VRR_CTRL_OFF, amp, secs);
    }
}

// A shot of a hand's gun (bgun0f09a6f8): the kick restarts, two-handed while the support hand grips
// the gun hand's gun, and the first buzz. The engine's rumble for the shot is not sent for such a gun:
// its rumble pak pulses (2 frames on, 4 off, inputRumble) stop the controllers' haptics between them
static void vrGunKickStart(s32 handnum, s32 weaponnum)
{
    const struct vrgunkick *kick = vrGunKickDef(handnum);
    bool twohand;

    if (kick == NULL) {
        return;
    }

    twohand = handnum == HAND_RIGHT && VrTwoHandsGun(weaponnum) && vrTwoHandGripHeld();

    sVrKickT[handnum] = 0.0f;
    sVrKickFrame[handnum] = g_Vars.lvframenum;
    sVrKickDef[handnum] = kick;
    sVrKickBack[handnum] = twohand ? kick->back2hcm : kick->back1hcm;
    sVrKickRise[handnum] = twohand ? kick->rise2hdeg : kick->rise1hdeg;
    sVrKickSide[handnum] = ((s32) (rngRandom() % 2001) - 1000) * (kick->sidedeg / 1000.0f);
    sVrKickBuzz2[handnum] = true;

    vrGunKickBuzz(handnum, kick->buzz1amp, kick->buzz1secs);
}

// The kick on a freshly built hand->posrotmtx (rows: the gun model's x, y and z, its forward, in the
// matrix's frame, the render drawing it turned by Ry180; row 3 the gun's place, cm, view space).
// Steps the kick's time once per frame (none passes while paused), plays the second buzz and ends the
// kick past its end or when the hand holds another gun (or none)
static void vrGunKickApply(s32 handnum, struct hand *hand)
{
    Mtxf *m = &hand->posrotmtx;
    const struct vrgunkick *kick;
    f32 u, k, back, a, b, ca, sa, cb, sb;
    s32 j;

    if (handnum < 0 || handnum >= 2 || sVrKickT[handnum] < 0.0f) {
        return;
    }

    kick = sVrKickDef[handnum];

    if (vrGunKickDef(handnum) != kick) {
        sVrKickT[handnum] = -1.0f;
        return;
    }

    if (sVrKickFrame[handnum] != g_Vars.lvframenum) {
        sVrKickFrame[handnum] = g_Vars.lvframenum;
        sVrKickT[handnum] += g_Vars.lvupdate60freal;

        if (sVrKickBuzz2[handnum] && sVrKickT[handnum] >= kick->buzz2at60ths) {
            sVrKickBuzz2[handnum] = false;
            vrGunKickBuzz(handnum, kick->buzz2amp, kick->buzz2secs);
        }

        if (sVrKickT[handnum] > kick->end60ths) {
            sVrKickT[handnum] = -1.0f;
            return;
        }
    }

    // The recoil setting turns the motion off; the buzz stays
    if (!VrWeaponRecoil) {
        return;
    }

    u = sVrKickT[handnum] / kick->peak60ths;
    k = u * expf(1.0f - u);
    back = sVrKickBack[handnum] * k;
    a = sVrKickRise[handnum] * k * (M_PI / 180.0f);
    b = sVrKickSide[handnum] * k * (M_PI / 180.0f);
    ca = cosf(a);
    sa = sinf(a);
    cb = cosf(b);
    sb = sinf(b);

    // Back along the barrel as it points before the turn: its view-space direction is the z row
    // turned by Ry180 (x and z negated), so back is the row with y negated
    m->m[3][0] += m->m[2][0] * back;
    m->m[3][1] -= m->m[2][1] * back;
    m->m[3][2] += m->m[2][2] * back;

    // The muzzle up, a turn about the gun's own x that tips its forward (z) toward its up (y); then
    // sideways about its own y. Local turns, so they hold whatever the aim and the gun side
    for (j = 0; j < 3; j++) {
        f32 y = m->m[1][j];
        f32 z = m->m[2][j];

        m->m[1][j] = ca * y - sa * z;
        m->m[2][j] = sa * y + ca * z;
    }

    for (j = 0; j < 3; j++) {
        f32 x = m->m[0][j];
        f32 z = m->m[2][j];

        m->m[0][j] = cb * x - sb * z;
        m->m[2][j] = sb * x + cb * z;
    }
}

// NOTE: adjust the size according to your enum (e.g. last weapon + 1)
#define NUM_WEAPONS 64

// Reload animation id per weapon and ammo slot, resolved when the copy-weapon model loads. Only
// guns with a manual reload row get one, per row (the SuperDragon: the rifle's and the grenade
// magazine's, Task F9e): the clip-in-hand pose holds a frame of it.
static int gVrResolvedAnimId[NUM_WEAPONS][2];
static bool gVrAnimIdsResolved[NUM_WEAPONS] = {false};

// Resolves the reload animId of a weapon's ammo slot (an ammoindex: the magazine's reload
// script) using bgunStartAnimation on a temporary hand
static int vrGetReloadAnimIdForWeapon(s32 wep, s32 ammoindex)
{
    if (wep < 0 || wep >= NUM_WEAPONS || ammoindex < 0 || ammoindex >= 2) return -1;

    struct weapon *weapon = g_Weapons[wep];
    if (!weapon || !weapon->ammos[ammoindex] || !weapon->ammos[ammoindex]->reload_animation) return -1;

    // The single-gun reload, also for a pair: the command list switches to the dual-wield reload
    // while the left hand is in use (bgun0f098884), and the hold frame is a frame of the single one
    // (in a pair the clip hand was drawn from the dual anim's frame, rotated and far off the hand)
    struct hand *rightHand = g_Vars.currentplayer ? &g_Vars.currentplayer->hands[HAND_RIGHT] : NULL;
    struct hand *leftHand = g_Vars.currentplayer ? &g_Vars.currentplayer->hands[HAND_LEFT] : NULL;
    s32 leftinuse = leftHand ? leftHand->inuse : false;
    struct hand tempHand;
    if (rightHand) {
        tempHand = *rightHand;
    } else {
        memset(&tempHand, 0, sizeof(tempHand));
    }
    tempHand.gset.weaponnum = wep;
    tempHand.animload = -1;
    if (leftHand) leftHand->inuse = false;
    bgunStartAnimation(weapon->ammos[ammoindex]->reload_animation, HAND_RIGHT, &tempHand);
    if (leftHand) leftHand->inuse = leftinuse;

    return (int)tempHand.animload;
}

void vrResolveReloadAnimIds(s32 wep)
{
    if (wep < 0 || wep >= NUM_WEAPONS) return;
    for (s32 ammo = 0; ammo < 2; ammo++) {
        gVrResolvedAnimId[wep][ammo] = vrReloadGetDef(wep, ammo) != NULL ? vrGetReloadAnimIdForWeapon(wep, ammo) : -1;
    }
    gVrAnimIdsResolved[wep] = true;
}

static int vrResolveReloadAnimId(s32 wep, s32 ammo)
{
    if (wep < 0 || wep >= NUM_WEAPONS || ammo < 0 || ammo >= 2) return -1;
    if (!gVrAnimIdsResolved[wep]) {
        vrResolveReloadAnimIds(wep);
    }
    return gVrResolvedAnimId[wep][ammo];
}

// Helper macro to read the resolved animId of a gun's row (its ammo slot, the row's ammoindex)
#define VR_ANIM_ID(wep, ammo) vrResolveReloadAnimId((wep), (ammo))



// Free the fists instance. Idempotent + NULL-safe, so it doubles as the "reload" step.
static void vrFistUnload(void)
{
    if (g_VrFistRwdatasAlloc) {
        sysMemFree(g_VrFistRwdatasAlloc);
        g_VrFistRwdatasAlloc = NULL;
    }
    g_VrFistModeldef = NULL;
    g_VrFistFilenum  = -1;
    g_VrFistReady    = false;
    memset(&g_VrFistModel, 0, sizeof(g_VrFistModel));
}

// Lazily (re)build the fists instance from gunctrl.handmodeldef. Self-healing: keyed on both the
// modeldef pointer AND handfilenum, because gun memory is a fixed arena -- a reloaded hands asset can
// land at the SAME address (e.g. after bgunEnterFlux(), which swaps gun mem without going through
// vrCopyWepLoad). Returns false when there is no usable hands model, in which case callers fall back
// to the legacy copy hand. Cheap: the hands modeldef has only 2 parts.
static bool vrFistEnsureLoaded(void)
{
    struct player *player = g_Vars.currentplayer;
    struct modeldef *md = player->gunctrl.handmodeldef;

    if (md != NULL && md == g_VrFistModeldef
        && (s32)player->gunctrl.handfilenum == g_VrFistFilenum
        && g_VrFistRwdatasAlloc != NULL) {
        return true;   // already current
    }

    vrFistUnload();

    if (md == NULL || md->rootnode == NULL || md->numparts <= 0 || md->nummatrices <= 0) {
        return false;
    }

    s32 rs = md->numparts * (s32)sizeof(union modelrwdata);
    u32 *rw = (u32 *)sysMemAlloc(rs);
    if (!rw) {
        return false;
    }
    memset(rw, 0, rs);

    modelInit(&g_VrFistModel, md, rw, false);
    // Own anim state. Left at animnum 0 it is inert -- modelSetMatricesWithAnim tests
    // (anim && anim->animnum) and falls through to the modeldef's rest pose, which IS the unarmed
    // idle look. The clench sets a fixed frame of anim 1002 on top of it.
    animInit(&g_VrFistAnim);
    g_VrFistModel.anim   = &g_VrFistAnim;
    g_VrFistRwdatasAlloc = rw;
    g_VrFistModeldef     = md;
    g_VrFistFilenum      = (s32)player->gunctrl.handfilenum;
    return true;
}

// Collapse a range of the fists' WORLD matrices so those bones draw nothing. Deliberately
// self-contained -- vrHideOnly/vrHideGunParts operate on an index list built from the GUN modeldef,
// which does not apply here. Same collapse trick as vrHideGunParts. Never touches index 0 (the root).
static void vrFistHideRange(Mtxf *m, s32 count, s32 first, s32 last)
{
    s32 k;
    if (first < 1) first = 1;
    if (last >= count) last = count - 1;

    for (k = first; k <= last; k++) {
        m[k].m[0][0] = m[k].m[0][1] = m[k].m[0][2] = 0.0f;
        m[k].m[1][0] = m[k].m[1][1] = m[k].m[1][2] = 0.0f;
        m[k].m[2][0] = m[k].m[2][1] = m[k].m[2][2] = 0.0f;
        m[k].m[3][0] = 99999.0f;
    }
}

void vrCopyWepUnload(void)
{

    if (g_VrCopyWepMatricesAlloc) {
        sysMemFree(g_VrCopyWepMatricesAlloc);
        g_VrCopyWepMatricesAlloc = NULL;
    }
    if (g_VrCopyWepRwdatasAlloc) {
        sysMemFree(g_VrCopyWepRwdatasAlloc);
        g_VrCopyWepRwdatasAlloc = NULL;
    }
    if (g_VrCopyHandRwdatasAlloc) {
        sysMemFree(g_VrCopyHandRwdatasAlloc);
        g_VrCopyHandRwdatasAlloc = NULL;
    }

    g_VrCopyWepModeldef  = NULL;
    g_VrCopyHandModeldef = NULL;
    g_VrCopyWepReadyToRender = false;

    memset(&g_VrCopyWepModel, 0, sizeof(g_VrCopyWepModel));
    memset(&g_VrCopyHandModel, 0, sizeof(g_VrCopyHandModel));
}


void vrCopyWepLoad(s32 handnum)
{

    if (g_VrCopyWepModeldef != NULL || g_VrCopyHandModeldef != NULL){
        vrCopyWepUnload();
    }

    vrSwitchCopyGun = true;

    struct player *player = g_Vars.currentplayer;
    struct modeldef *gunmodeldef = player->gunctrl.gunmodeldef;
    struct modeldef *handmodeldef = player->gunctrl.handmodeldef;

    if (!gunmodeldef) {
        return;
    }

    // SAFETY GUARD: weapon without a real 3D model (fists, knife using
    // only the hand model, etc.) -> do not create a VR copy
    if (gunmodeldef->rootnode == NULL
        || gunmodeldef->numparts <= 0
        || gunmodeldef->nummatrices <= 0) {
        g_VrCopyWepModeldef = NULL;
        return;
    }

    s32 gnp = gunmodeldef->numparts;
    s32 gnm = gunmodeldef->nummatrices;
    s32 grs = gnp * (s32)sizeof(union modelrwdata);

    u32 *wepRwdatas = (u32 *)sysMemAlloc(grs);
    Mtxf *wepMatrices = (Mtxf *)sysMemAlloc(gnm * sizeof(Mtxf));

    if (!wepRwdatas || !wepMatrices) {
        if (wepRwdatas) sysMemFree(wepRwdatas);
        if (wepMatrices) sysMemFree(wepMatrices);
        return;
    }
    memset(wepRwdatas, 0, grs);
    memset(wepMatrices, 0, gnm * sizeof(Mtxf));

    modelInit(&g_VrCopyWepModel, gunmodeldef, wepRwdatas, false);
    g_VrCopyWepModel.matrices = wepMatrices;
    g_VrCopyWepModeldef = gunmodeldef;

    g_VrCopyWepMatricesAlloc = wepMatrices;
    g_VrCopyWepRwdatasAlloc  = wepRwdatas;

    g_VrCopyHandModeldef = handmodeldef;
    if (handmodeldef && handmodeldef->rootnode != NULL && handmodeldef->numparts > 0) {
        s32 hnp = handmodeldef->numparts;
        s32 hrs = hnp * (s32)sizeof(union modelrwdata);

        u32 *handRwdatas = (u32 *)sysMemAlloc(hrs);
        if (handRwdatas) {
            memset(handRwdatas, 0, hrs);
            modelInit(&g_VrCopyHandModel, handmodeldef, handRwdatas, false);
            g_VrCopyHandModel.matrices = wepMatrices;
            g_VrCopyHandRwdatasAlloc = handRwdatas;
        } else {
            g_VrCopyHandModeldef = NULL;
        }
    } else {
        g_VrCopyHandModeldef = NULL;
    }

    s32 wep = player->hands[HAND_RIGHT].gset.weaponnum;
    vrResolveReloadAnimIds(wep);

}



//void vr_record_throw_sample(int ctrlIdx, float vx, float vy, float vz) // VR
//{
//    vr_ThrowSample *s = &gThrowHistory[ctrlIdx][vr_ThrowHistoryIdx[ctrlIdx]];
//    s->vx = vx;
//    s->vy = vy;
//    s->vz = vz;
//    s->vr_magnitude = sqrtf(vx*vx + vy*vy + vz*vz);
//    s->frame60 = g_Vars.lvframe60;
//
//    vr_ThrowHistoryIdx[ctrlIdx] = (vr_ThrowHistoryIdx[ctrlIdx] + 1) % VR_THROW_HISTORY_MAX;
//    if (vr_ThrowHistoryCount[ctrlIdx] < VR_THROW_HISTORY_MAX) {
//        vr_ThrowHistoryCount[ctrlIdx]++;
//    }
//}

/*
struct coord vr_throw(s32 handnum) {
    int ctrlIdx = (!vr_invert_hands) ? (handnum == HAND_RIGHT ? 1 : 0)
                                     : (handnum == HAND_RIGHT ? 0 : 1);

    uint32_t frameNow = g_Vars.lvframe60;
    float best_vx = vr_ctrl_velocity[ctrlIdx][0];
    float best_vy = vr_ctrl_velocity[ctrlIdx][1];
    float best_vz = vr_ctrl_velocity[ctrlIdx][2];
    float best_mag = sqrtf(best_vx*best_vx + best_vy*best_vy + best_vz*best_vz);

    int count = vr_ThrowHistoryCount[ctrlIdx];
    int cur   = vr_ThrowHistoryIdx[ctrlIdx];
    for (int i = 0; i < count; i++) {
        int idx = cur - 1 - i;
        if (idx < 0) idx += VR_THROW_HISTORY_MAX;

        vr_ThrowSample *s = &gThrowHistory[ctrlIdx][idx];

        if (frameNow - s->frame60 > VR_THROW_HISTORY_FRAMES)
            break;

        if (s->vr_magnitude > best_mag) {
            best_mag = s->vr_magnitude;
            best_vx  = s->vx;
            best_vy  = s->vy;
            best_vz  = s->vz;
        }
    }

    float vx = best_vx;
    float vy = best_vy;
    float vz = best_vz;
    float vr_magnitude = best_mag;
    struct coord throwdir;


    if (vr_magnitude > 0.5f) {
        XrQuaternionf headQMonde;
        headQMonde.x = vr_joy_rot_Q.w * vr_HMD_rot_Q.x + vr_joy_rot_Q.x * vr_HMD_rot_Q.w +
                       vr_joy_rot_Q.y * vr_HMD_rot_Q.z - vr_joy_rot_Q.z * vr_HMD_rot_Q.y;
        headQMonde.y = vr_joy_rot_Q.w * vr_HMD_rot_Q.y - vr_joy_rot_Q.x * vr_HMD_rot_Q.z +
                       vr_joy_rot_Q.y * vr_HMD_rot_Q.w + vr_joy_rot_Q.z * vr_HMD_rot_Q.x;
        headQMonde.z = vr_joy_rot_Q.w * vr_HMD_rot_Q.z + vr_joy_rot_Q.x * vr_HMD_rot_Q.y -
                       vr_joy_rot_Q.y * vr_HMD_rot_Q.x + vr_joy_rot_Q.z * vr_HMD_rot_Q.w;
        headQMonde.w = vr_joy_rot_Q.w * vr_HMD_rot_Q.w - vr_joy_rot_Q.x * vr_HMD_rot_Q.x -
                       vr_joy_rot_Q.y * vr_HMD_rot_Q.y - vr_joy_rot_Q.z * vr_HMD_rot_Q.z;

        throwdir.x = -vx / vr_magnitude;
        throwdir.y = -vy / vr_magnitude;
        throwdir.z = -vz / vr_magnitude;

        vr_rotate_vector_by_quaternion(&throwdir, &headQMonde);
        throwdir.y = -throwdir.y;

        float minSpeed   = 1.0f;
        float maxSpeed   = 1000.0f;
        float throwSpeed = vr_magnitude * 10.0f;
        if (throwSpeed < minSpeed) throwSpeed = minSpeed;
        if (throwSpeed > maxSpeed) throwSpeed = maxSpeed;

        velocity.x = throwdir.x * throwSpeed;
        velocity.y = throwdir.y * throwSpeed;
        velocity.z = throwdir.z * throwSpeed;

        vr_ThrowHistoryCount[ctrlIdx] = 0;
        vr_ThrowHistoryIdx[ctrlIdx] = 0;
        vr_throw_cancelled = false;
    }
    else {
        vr_ThrowHistoryCount[ctrlIdx] = 0;
        vr_ThrowHistoryIdx[ctrlIdx] = 0;
        vr_throw_cancelled = true;
    }

    return velocity;
}*/



// 1. Capture the orientation at the time of recording
//
// The samples are play-space velocities, so the play -> world rotation is the
// recenter followed by the stick/snap body yaw. NOT vr_HMD_rot_Q: that already
// contains the recenter times the raw head rotation, and folding the head yaw
// into a vector that is not head-relative applies the player's facing twice --
// which is what used to send every throw off toward the room's forward.
void vr_record_throw_sample(int ctrlIdx, float vx, float vy, float vz) {
    vr_ThrowSample *s = &gThrowHistory[ctrlIdx][vr_ThrowHistoryIdx[ctrlIdx]];
    s->vx = vx;
    s->vy = vy;
    s->vz = vz;
    s->vr_magnitude = sqrtf(vx*vx + vy*vy + vz*vz);

    // freeze the body orientation at THIS exact frame, so snap-turning during
    // the wind-up cannot swing a throw that has already been aimed
    quaternionMul_XR(&vr_joy_rot_Q, &vr_recenter_rot_Q, &s->orientQ);

    s->frame60 = g_Vars.lvframe60;
    vr_ThrowHistoryIdx[ctrlIdx] = (vr_ThrowHistoryIdx[ctrlIdx] + 1) % VR_THROW_HISTORY_MAX;
    if (vr_ThrowHistoryCount[ctrlIdx] < VR_THROW_HISTORY_MAX)
        vr_ThrowHistoryCount[ctrlIdx]++;
}

// 2. Use the frozen orientation from the BEST sample, not the current frame's
struct coord vr_throw(s32 handnum) {
    int ctrlIdx = !vr_invert_hands ? (handnum == HAND_RIGHT ? 1 : 0)
                                   : (handnum == HAND_RIGHT ? 0 : 1);
    uint32_t frameNow = g_Vars.lvframe60;

    float bestvx = vr_ctrl_velocity_play[ctrlIdx][0];
    float bestvy = vr_ctrl_velocity_play[ctrlIdx][1];
    float bestvz = vr_ctrl_velocity_play[ctrlIdx][2];
    float bestmag = sqrtf(bestvx*bestvx + bestvy*bestvy + bestvz*bestvz);

    // Default orientation = the current one, in case
    // the "live" (current) sample wins
    XrQuaternionf bestOrientQ;
    quaternionMul_XR(&vr_joy_rot_Q, &vr_recenter_rot_Q, &bestOrientQ);

    int count = vr_ThrowHistoryCount[ctrlIdx];
    int cur   = vr_ThrowHistoryIdx[ctrlIdx];
    for (int i = 0; i < count; i++) {
        int idx = cur - 1 - i;
        if (idx < 0) idx += VR_THROW_HISTORY_MAX;
        vr_ThrowSample *s = &gThrowHistory[ctrlIdx][idx];
        if (frameNow - s->frame60 > VR_THROW_HISTORY_FRAMES) break;
        if (s->vr_magnitude > bestmag) {
            bestmag    = s->vr_magnitude;
            bestvx     = s->vx;
            bestvy     = s->vy;
            bestvz     = s->vz;
            bestOrientQ = s->orientQ;
        }
    }

    float vx = bestvx, vy = bestvy, vz = bestvz;
    float vrmagnitude = bestmag;
    struct coord throwdir;

    if (vrmagnitude > 0.5f) {
        throwdir.x = -vx / vrmagnitude;
        throwdir.y = -vy / vrmagnitude;
        throwdir.z = -vz / vrmagnitude;

        vr_rotate_vector_by_quaternion(&throwdir, &bestOrientQ);
        // Negating all three then restoring Y is the 180deg-Y basis flip that
        // takes an OpenXR direction into game space -- the same convention as
        // the look vector in vr_player_rot() and the head position in
        // vr_update_head_tracking(). Leave it alone.
        throwdir.y = -throwdir.y;

        float minSpeed = 1.0f, maxSpeed = 1000.0f;
        float throwSpeed = vrmagnitude * 10.0f;
        if (throwSpeed < minSpeed) throwSpeed = minSpeed;
        if (throwSpeed > maxSpeed) throwSpeed = maxSpeed;

        velocity.x = throwdir.x * throwSpeed;
        velocity.y = throwdir.y * throwSpeed;
        velocity.z = throwdir.z * throwSpeed;

        vr_ThrowHistoryCount[ctrlIdx] = 0;
        vr_ThrowHistoryIdx[ctrlIdx]   = 0;
        vr_throw_cancelled = false;
    } else {
        vr_ThrowHistoryCount[ctrlIdx] = 0;
        vr_ThrowHistoryIdx[ctrlIdx]   = 0;
        vr_throw_cancelled = true;
    }
    return velocity;
}




#if !MATCHING || VERSION >= VERSION_NTSC_1_0
void bgunRumble(s32 handnum, s32 weaponnum)
{
#if VERSION >= VERSION_NTSC_1_0
    u32 stack;
    s32 contpadtouse1;
    s32 contpadtouse2;
    bool singlewield = false;
    s32 contpad1;
    s32 contpad2;
    s32 contpad1hasrumble;
    s32 contpad2hasrumble;

    joyGetContpadNumsForPlayer(g_Vars.currentplayernum, &contpad1, &contpad2);

    if (optionsGetControlMode(g_Vars.currentplayerstats->mpindex) >= CONTROLMODE_21
        && contpad1 >= 0 && contpad2 >= 0) {
        contpad1hasrumble = pakGetType(contpad1) == PAKTYPE_RUMBLE;
        contpad2hasrumble = pakGetType(contpad2) == PAKTYPE_RUMBLE;

        if (!weaponHasFlag(weaponnum, WEAPONFLAG_DUALWIELD)) {
            singlewield = true;
        }

        if (contpad1hasrumble && contpad2hasrumble) {
            if (singlewield) {
                pakRumble(contpad1, 0.2f, 2, 4);
                pakRumble(contpad2, 0.2f, 2, 4);
            }
            else {
                s32 contpadtouse1 = contpad1;

                if (handnum == HAND_LEFT) {
                    contpadtouse1 = contpad2;
                }

                pakRumble(contpadtouse1, 0.2f, 2, 4);
            }
        }
        else {
            s32 contpadtouse2 = contpad1;

            if (contpad2hasrumble) {
                contpadtouse2 = contpad2;
            }

            pakRumble(contpadtouse2, 0.2f, 2, 4);
        }
    }
    else {
        if (contpad1 >= 0) {
            pakRumble(contpad1, 0.2f, 2, 4);
        }
    }
#else
    s32 stack1;
		s32 stack2;
		s8 contpad1;
		s8 contpad2;
		bool contpad1hasrumble;
		bool contpad2hasrumble;
		s32 contpadtouse1;
		s32 contpadtouse2;
		s32 controlmode = optionsGetControlMode(g_Vars.currentplayerstats->mpindex);

		if (controlmode >= CONTROLMODE_21 && controlmode < CONTROLMODE_PC) {
				contpad1hasrumble = pakGetType(g_Vars.currentplayernum) == PAKTYPE_RUMBLE;
				contpad2hasrumble = pakGetType(g_Vars.currentplayernum + PLAYERCOUNT()) == PAKTYPE_RUMBLE;

				if (contpad1hasrumble && contpad2hasrumble) {
						contpadtouse1 = g_Vars.currentplayernum;

						if (handnum == HAND_LEFT) {
								contpadtouse1 += PLAYERCOUNT();
						}

						pakRumble(contpadtouse1, 0.2f, 2, 4);
				}
				else {
						contpadtouse2 = g_Vars.currentplayernum;

						if (contpad2hasrumble) {
								contpadtouse2 += PLAYERCOUNT();
						}

						pakRumble(contpadtouse2, 0.2f, 2, 4);
				}
		}
		else {
				pakRumble(g_Vars.currentplayernum, 0.2f, 2, 4);
		}
#endif
}
#else
GLOBAL_ASM(
		glabel bgunRumble
		/*  f095b30:	27bdffd0 */ 	addiu	$sp, $sp, -48
		/*  f095b34:	3c08800a */ 	lui	$t0, % hi(g_Vars)
		/*  f095b38:	2508e6c0 */ 	addiu	$t0, $t0, % lo(g_Vars)
		/*  f095b3c:	8d0e0288 */ 	lw	$t6, 0x288($t0)
		/*  f095b40:	afbf0014 */ 	sw	$ra, 0x14($sp)
		/*  f095b44:	afa40030 */ 	sw	$a0, 0x30($sp)
		/*  f095b48:	afa50034 */ 	sw	$a1, 0x34($sp)
		/*  f095b4c:	0fc53380 */ 	jal	optionsGetControlMode
		/*  f095b50:	8dc40070 */ 	lw	$a0, 0x70($t6)
		/*  f095b54:	3c08800a */ 	lui	$t0, % hi(g_Vars)
		/*  f095b58:	28410004 */ 	slti	$at, $v0, 0x4
		/*  f095b5c:	1420007c */ 	bnez	$at, .NB0f095d50
		/*  f095b60:	2508e6c0 */ 	addiu	$t0, $t0, % lo(g_Vars)
		/*  f095b64:	0fc44336 */ 	jal	pakGetType
		/*  f095b68:	8104028f */ 	lb	$a0, 0x28f($t0)
		/*  f095b6c:	3c08800a */ 	lui	$t0, % hi(g_Vars)
		/*  f095b70:	2508e6c0 */ 	addiu	$t0, $t0, % lo(g_Vars)
		/*  f095b74:	8d0f006c */ 	lw	$t7, 0x6c($t0)
		/*  f095b78:	24050001 */ 	addiu	$a1, $zero, 0x1
		/*  f095b7c:	00453026 */ xor $a2, $v0, $a1
		/*  f095b80:	11e00003 */ 	beqz	$t7, .NB0f095b90
		/*  f095b84:	2cc60001 */ 	sltiu	$a2, $a2, 0x1
		/*  f095b88:	10000002 */ 	beqz	$zero, .NB0f095b94
		/*  f095b8c:	00a05025 */ or $t2, $a1, $zero
		.NB0f095b90:
/*  f095b90:	00005025 */ or $t2, $zero, $zero
.NB0f095b94 :
		/*  f095b94:	8d180068 */ 	lw	$t8, 0x68($t0)
		/*  f095b98:	00004825 */ or $t1, $zero, $zero
		/*  f095b9c:	00001825 */ or $v1, $zero, $zero
		/*  f095ba0:	13000003 */ 	beqz	$t8, .NB0f095bb0
		/*  f095ba4:	00001025 */ or $v0, $zero, $zero
		/*  f095ba8:	10000001 */ 	beqz	$zero, .NB0f095bb0
		/*  f095bac:	00a04825 */ or $t1, $a1, $zero
		.NB0f095bb0:
/*  f095bb0:	8d190064 */ 	lw	$t9, 0x64($t0)
/*  f095bb4:	13200003 */ 	beqz	$t9, .NB0f095bc4
/*  f095bb8:	00000000 */ 	sll	$zero, $zero, 0x0
/*  f095bbc:	10000001 */ 	beqz	$zero, .NB0f095bc4
/*  f095bc0:	00a01825 */ or $v1, $a1, $zero
.NB0f095bc4:
/*  f095bc4:	8d0c0070 */ 	lw	$t4, 0x70($t0)
/*  f095bc8:	11800003 */ 	beqz	$t4, .NB0f095bd8
/*  f095bcc:	00000000 */ 	sll	$zero, $zero, 0x0
/*  f095bd0:	10000001 */ 	beqz	$zero, .NB0f095bd8
/*  f095bd4:	00a01025 */ or $v0, $a1, $zero
.NB0f095bd8:
/*  f095bd8:	8d18028c */ 	lw	$t8, 0x28c($t0)
/*  f095bdc:	00436821 */ 	addu	$t5, $v0, $v1
/*  f095be0:	01a97021 */ 	addu	$t6, $t5, $t1
/*  f095be4:	01ca7821 */ 	addu	$t7, $t6, $t2
/*  f095be8:	01f82021 */ 	addu	$a0, $t7, $t8
/*  f095bec:	0004ce00 */ 	sll	$t9, $a0, 0x18
/*  f095bf0:	00192603 */ 	sra	$a0, $t9, 0x18
/*  f095bf4:	0fc44336 */ 	jal	pakGetType
/*  f095bf8:	afa6001c */ 	sw	$a2, 0x1c($sp)
/*  f095bfc:	8fa6001c */ 	lw	$a2, 0x1c($sp)
/*  f095c00:	3c08800a */ 	lui	$t0, % hi(g_Vars)
/*  f095c04:	2508e6c0 */ 	addiu	$t0, $t0, % lo(g_Vars)
/*  f095c08:	10c0002a */ 	beqz	$a2, .NB0f095cb4
/*  f095c0c:	24050001 */ 	addiu	$a1, $zero, 0x1
/*  f095c10:	14450028 */ 	bne	$v0, $a1, .NB0f095cb4
/*  f095c14:	8fae0030 */ 	lw	$t6, 0x30($sp)
/*  f095c18:	15c5001c */ 	bne	$t6, $a1, .NB0f095c8c
/*  f095c1c:	8d0b028c */ 	lw	$t3, 0x28c($t0)
/*  f095c20:	8d0f0070 */ 	lw	$t7, 0x70($t0)
/*  f095c24:	00005025 */ or $t2, $zero, $zero
/*  f095c28:	00004825 */ or $t1, $zero, $zero
/*  f095c2c:	11e00003 */ 	beqz	$t7, .NB0f095c3c
/*  f095c30:	00001825 */ or $v1, $zero, $zero
/*  f095c34:	10000001 */ 	beqz	$zero, .NB0f095c3c
/*  f095c38:	240a0001 */ 	addiu	$t2, $zero, 0x1
.NB0f095c3c:
/*  f095c3c:	8d18006c */ 	lw	$t8, 0x6c($t0)
/*  f095c40:	00001025 */ or $v0, $zero, $zero
/*  f095c44:	13000003 */ 	beqz	$t8, .NB0f095c54
/*  f095c48:	00000000 */ 	sll	$zero, $zero, 0x0
/*  f095c4c:	10000001 */ 	beqz	$zero, .NB0f095c54
/*  f095c50:	24090001 */ 	addiu	$t1, $zero, 0x1
.NB0f095c54:
/*  f095c54:	8d190068 */ 	lw	$t9, 0x68($t0)
/*  f095c58:	13200003 */ 	beqz	$t9, .NB0f095c68
/*  f095c5c:	00000000 */ 	sll	$zero, $zero, 0x0
/*  f095c60:	10000001 */ 	beqz	$zero, .NB0f095c68
/*  f095c64:	24030001 */ 	addiu	$v1, $zero, 0x1
.NB0f095c68:
/*  f095c68:	8d0c0064 */ 	lw	$t4, 0x64($t0)
/*  f095c6c:	11800003 */ 	beqz	$t4, .NB0f095c7c
/*  f095c70:	00000000 */ 	sll	$zero, $zero, 0x0
/*  f095c74:	10000001 */ 	beqz	$zero, .NB0f095c7c
/*  f095c78:	24020001 */ 	addiu	$v0, $zero, 0x1
.NB0f095c7c:
/*  f095c7c:	01626821 */ 	addu	$t5, $t3, $v0
/*  f095c80:	01a37021 */ 	addu	$t6, $t5, $v1
/*  f095c84:	01c97821 */ 	addu	$t7, $t6, $t1
/*  f095c88:	01ea5821 */ 	addu	$t3, $t7, $t2
.NB0f095c8c :
		/*  f095c8c:	000b2600 */ 	sll	$a0, $t3, 0x18
		/*  f095c90:	0004c603 */ 	sra	$t8, $a0, 0x18
		/*  f095c94:	3c053e4c */ 	lui	$a1, 0x3e4c
		/*  f095c98:	34a5cccd */ 	ori	$a1, $a1, 0xcccd
		/*  f095c9c:	03002025 */ or $a0, $t8, $zero
		/*  f095ca0:	24060002 */ 	addiu	$a2, $zero, 0x2
		/*  f095ca4:	0fc45e2f */ 	jal	pakRumble
		/*  f095ca8:	24070004 */ 	addiu	$a3, $zero, 0x4
		/*  f095cac:	1000002f */ 	beqz	$zero, .NB0f095d6c
		/*  f095cb0:	8fbf0014 */ 	lw	$ra, 0x14($sp)
		.NB0f095cb4:
/*  f095cb4:	1445001c */ 	bne	$v0, $a1, .NB0f095d28
/*  f095cb8:	8d0b028c */ 	lw	$t3, 0x28c($t0)
/*  f095cbc:	8d0c0070 */ 	lw	$t4, 0x70($t0)
/*  f095cc0:	00005025 */ or $t2, $zero, $zero
/*  f095cc4:	00004825 */ or $t1, $zero, $zero
/*  f095cc8:	11800003 */ 	beqz	$t4, .NB0f095cd8
/*  f095ccc:	00001825 */ or $v1, $zero, $zero
/*  f095cd0:	10000001 */ 	beqz	$zero, .NB0f095cd8
/*  f095cd4:	240a0001 */ 	addiu	$t2, $zero, 0x1
.NB0f095cd8:
/*  f095cd8:	8d0d006c */ 	lw	$t5, 0x6c($t0)
/*  f095cdc:	00001025 */ or $v0, $zero, $zero
/*  f095ce0:	11a00003 */ 	beqz	$t5, .NB0f095cf0
/*  f095ce4:	00000000 */ 	sll	$zero, $zero, 0x0
/*  f095ce8:	10000001 */ 	beqz	$zero, .NB0f095cf0
/*  f095cec:	24090001 */ 	addiu	$t1, $zero, 0x1
.NB0f095cf0:
/*  f095cf0:	8d0e0068 */ 	lw	$t6, 0x68($t0)
/*  f095cf4:	11c00003 */ 	beqz	$t6, .NB0f095d04
/*  f095cf8:	00000000 */ 	sll	$zero, $zero, 0x0
/*  f095cfc:	10000001 */ 	beqz	$zero, .NB0f095d04
/*  f095d00:	24030001 */ 	addiu	$v1, $zero, 0x1
.NB0f095d04:
/*  f095d04:	8d0f0064 */ 	lw	$t7, 0x64($t0)
/*  f095d08:	11e00003 */ 	beqz	$t7, .NB0f095d18
/*  f095d0c:	00000000 */ 	sll	$zero, $zero, 0x0
/*  f095d10:	10000001 */ 	beqz	$zero, .NB0f095d18
/*  f095d14:	24020001 */ 	addiu	$v0, $zero, 0x1
.NB0f095d18:
/*  f095d18:	0162c021 */ 	addu	$t8, $t3, $v0
/*  f095d1c:	0303c821 */ 	addu	$t9, $t8, $v1
/*  f095d20:	03296021 */ 	addu	$t4, $t9, $t1
/*  f095d24:	018a5821 */ 	addu	$t3, $t4, $t2
.NB0f095d28 :
		/*  f095d28:	000b2600 */ 	sll	$a0, $t3, 0x18
		/*  f095d2c:	00046e03 */ 	sra	$t5, $a0, 0x18
		/*  f095d30:	3c053e4c */ 	lui	$a1, 0x3e4c
		/*  f095d34:	34a5cccd */ 	ori	$a1, $a1, 0xcccd
		/*  f095d38:	01a02025 */ or $a0, $t5, $zero
		/*  f095d3c:	24060002 */ 	addiu	$a2, $zero, 0x2
		/*  f095d40:	0fc45e2f */ 	jal	pakRumble
		/*  f095d44:	24070004 */ 	addiu	$a3, $zero, 0x4
		/*  f095d48:	10000008 */ 	beqz	$zero, .NB0f095d6c
		/*  f095d4c:	8fbf0014 */ 	lw	$ra, 0x14($sp)
		.NB0f095d50:
/*  f095d50:	3c053e4c */ 	lui	$a1, 0x3e4c
/*  f095d54:	34a5cccd */ 	ori	$a1, $a1, 0xcccd
/*  f095d58:	8104028f */ 	lb	$a0, 0x28f($t0)
/*  f095d5c:	24060002 */ 	addiu	$a2, $zero, 0x2
/*  f095d60:	0fc45e2f */ 	jal	pakRumble
/*  f095d64:	24070004 */ 	addiu	$a3, $zero, 0x4
/*  f095d68:	8fbf0014 */ 	lw	$ra, 0x14($sp)
.NB0f095d6c:
/*  f095d6c:	27bd0030 */ 	addiu	$sp, $sp, 0x30
/*  f095d70:	03e00008 */ 	jr	$ra
/*  f095d74:	00000000 */ 	sll	$zero, $zero, 0x0
);
#endif

s32 bgunGetUnequippedReloadIndex(s32 weaponnum)
{
    if (weaponnum == WEAPON_CROSSBOW) {
        return 0;
    }

    if (weaponnum == WEAPON_SHOTGUN) {
        return 1;
    }

    if (weaponnum == WEAPON_DY357MAGNUM) {
        return 2;
    }

    if (weaponnum == WEAPON_DY357LX) {
        return 3;
    }

    return -1;
}

/**
 * The magnums, shotgun and crossbow are special because that the game remembers
 * how much ammo is loaded in their clips when the weapon is not being used.
 * Their clips are gradually reloaded while the weapon is not in use, and that
 * gradual reloading is handled by this function.
 *
 * The gunroundsspent value is actually a countdown timer,
 * not the number of rounds as the name suggests.
 */
void bgunTickUnequippedReload(void)
{
    s32 i;
    s32 j;

    for (i = 0; i < 2; i++) {
        for (j = 0; j < ARRAYCOUNT(g_Vars.currentplayer->hands[i].gunroundsspent); j++) {
            u16 spent = g_Vars.currentplayer->hands[i].gunroundsspent[j];

            if (spent > g_Vars.lvupdate60) {
                spent -= g_Vars.lvupdate60;
            }
            else {
                spent = 0;
            }

            g_Vars.currentplayer->hands[i].gunroundsspent[j] = spent;
        }
    }
}

bool bgunTestGunVisCommand(struct gunviscmd* cmd, struct hand* hand)
{
    bool result = true;

    switch (cmd->type) {
        case GUNVISCMD_CHECKUPGRADE:
            if (((hand->gset.unk0639 >> cmd->param) & 1) == 0) {
                result = false;
            }
            break;
        case GUNVISCMD_CHECKINLEFTHAND:
            if (hand != &g_Vars.currentplayer->hands[HAND_LEFT]) {
                result = false;
            }
            break;
        case GUNVISCMD_CHECKINRIGHTHAND:
            if (hand != &g_Vars.currentplayer->hands[HAND_RIGHT]) {
                result = false;
            }
            break;
    }

    return result;
}

void bgunSetPartVisible(s16 partnum, bool visible, struct hand* hand, struct modeldef* modeldef)
{
    struct modelnode* node;

    if (partnum == MODELPART_HAND_LEFT || partnum == MODELPART_HAND_RIGHT) {
        if (g_Vars.currentplayer->gunctrl.handmodeldef) {
            node = modelGetPart(g_Vars.currentplayer->gunctrl.handmodeldef, partnum);

            if (node) {
                struct modelrodata_toggle* rodata = &node->rodata->toggle;
                u32* ptr = &hand->handsavedata[rodata->rwdataindex];
                *ptr = visible;
            }
        }
    }
    else {
        node = modelGetPart(modeldef, partnum);

        if (node) {
            struct modelrodata_toggle* rodata = &node->rodata->toggle;
            u32* ptr = &hand->unk0a6c[rodata->rwdataindex];
            *ptr = visible;
        }
    }
}

void bgunExecuteGunVisCommands(struct hand* hand, struct modeldef* modeldef, struct gunviscmd* commands)
{
    struct gunviscmd* cmd = commands;
    bool done = false;

    if (cmd == NULL) {
        return;
    }

    while (!done) {
        if (bgunTestGunVisCommand(cmd, hand)) {
            if (cmd->op == GUNVISOP_IFTRUE_SETVISIBLE) {
                bgunSetPartVisible(cmd->partnum, true, hand, modeldef);
            }

            if (cmd->op == GUNVISOP_IFTRUE_SETHIDDEN) {
                bgunSetPartVisible(cmd->partnum, false, hand, modeldef);
            }

            if (cmd->op == GUNVISOP_SETVISIBILITY) {
                bgunSetPartVisible(cmd->partnum, true, hand, modeldef);
            }
        }
        else {
            if (cmd->op == GUNVISOP_SETVISIBILITY) {
                bgunSetPartVisible(cmd->partnum, false, hand, modeldef);
            }
        }

        cmd++;

        if (cmd->type == GUNVISCMD_END) {
            done = true;
        }
    }
}

void bgun0f098030(struct hand* hand, struct modeldef* modeldef)
{
    struct weapon* weapon = weaponFindById(hand->gset.weaponnum);
    s32 i;
    s32 j;

    bgunExecuteGunVisCommands(hand, modeldef, weapon->gunviscmds);
    bgunSetPartVisible(MODELPART_0042, false, hand, modeldef);

    // VR: G5 Building's Detonator is the detonator alone at the hand, as a held device: no arms, no
    // mine (vrG5DetonatorPlace)
    if (vrHolsterIsG5Detonator(hand->gset.weaponnum)) {
        bgunSetPartVisible(MODELPART_HAND_LEFT, false, hand, modeldef);
        bgunSetPartVisible(MODELPART_HAND_RIGHT, false, hand, modeldef);
        bgunSetPartVisible(MODELPART_REMOTEMINE_MINE, false, hand, modeldef);
        bgunSetPartVisible(MODELPART_REMOTEMINE_DETONATOR, true, hand, modeldef);
    }

    for (i = 0; i < 2; i++) {
        if (weapon->ammos[i] && (weapon->ammos[i]->flags & AMMOFLAG_QTYAFFECTSPARTVIS)) {
            for (j = 0; j < hand->clipsizes[i]; j++) {
                if (j >= hand->loadedammo[i]) {
                    bgunSetPartVisible(j + 100, false, hand, modeldef);
                }
                else {
                    bgunSetPartVisible(j + 100, true, hand, modeldef);
                }
            }
        }
    }
}

f32 bgun0f09815c(struct hand* hand)
{
    if (hand->animmode == HANDANIMMODE_BUSY && hand->unk0ce8 != NULL) {
        if (hand->unk0ce8->unk04 < 0) {
            return modelGetNumAnimFrames(&hand->gunmodel) - modelGetCurAnimFrame(&hand->gunmodel);
        }

        return modelGetCurAnimFrame(&hand->gunmodel);
    }

    return 0;
}

void bgun0f0981e8(struct hand* hand, struct modeldef* modeldef)
{
#if VERSION >= VERSION_PAL_BETA
    f32 s4;
		f32 s2;
#else
    s32 s2;
    s32 s4;
#endif
    struct guncmd* cmd;
    f32 animspeed;
    bool done;
    f32 animspeedmult;
    s32 partnums[15];
    bool partsvisible[15];
    s32 partframes[15];
    s32 s0;
    s32 index;

    hand->unk0cc8_04 = false;

    if (hand->animmode == HANDANIMMODE_BUSY && bgun0f09815c(hand) >= modelGetNumAnimFrames(&hand->gunmodel) - 1) {
        hand->animmode = HANDANIMMODE_IDLE;
    }

    // This condition looks like a bug (using | instead of ||), but it happens
    // to make no difference anyway. Brackets added for clarity.
    if ((hand->animmode == (u32)HANDANIMMODE_BUSY) | (hand->animload >= 0)) {
        if (hand->gangstarot > 0.0f) {
            hand->animframeinc = 0;
#if VERSION >= VERSION_PAL_BETA
            hand->animframeincfreal = 0.0f;
#endif
        }

        if (hand->animload >= 0) {
            animspeedmult = 1.0f;
            animspeed = hand->unk0ce8->unk04 / 10000.0f;

            if (hand->unk0d0e_07 && g_Vars.currentplayer->hands[HAND_LEFT].inuse) {
                animspeedmult = RANDOMFRAC() * 0.77f + 0.7f;
            }

            if (hand->unk0ce8 && animspeed < 0.0f) {
                modelSetAnimation(&hand->gunmodel, hand->animload, false, 0.0f, animspeedmult * animspeed, 0.0f);
                modelSetAnimFrame(&hand->gunmodel, modelGetNumAnimFrames(&hand->gunmodel));
            }
            else {
                modelSetAnimation(&hand->gunmodel, hand->animload, false, 0.0f, animspeedmult * animspeed, 0.0f);
            }


            // VR knife (Task H3b): a swing's slash starts at the stroke (see VR_KNIFE_STROKE_FRAME)
            {
                s32 vrh = hand == &g_Vars.currentplayer->hands[HAND_LEFT] ? HAND_LEFT : HAND_RIGHT;

                if (vr_knife_fromstroke[vrh] && hand->gset.weaponnum == WEAPON_COMBATKNIFE
                        && hand->gset.weaponfunc == FUNC_PRIMARY && hand->state == HANDSTATE_ATTACK
                        && animspeed > 0.0f && modelGetNumAnimFrames(&hand->gunmodel) > VR_KNIFE_STROKE_FRAME) {
                    modelSetAnimFrame(&hand->gunmodel, VR_KNIFE_STROKE_FRAME);
                }

                vr_knife_fromstroke[vrh] = false;
            }

            // VR: the DY357s' shot from a cocked hammer (see VR_DY357_COCKED_FRAME)
            if ((hand->gset.weaponnum == WEAPON_DY357MAGNUM || hand->gset.weaponnum == WEAPON_DY357LX)
                    && hand->gset.weaponfunc == FUNC_PRIMARY && hand->state == HANDSTATE_ATTACK
                    && hand->animload == ANIM_GUN_DY357_SHOOT && animspeed > 0.0f
                    && modelGetNumAnimFrames(&hand->gunmodel) > VR_DY357_COCKED_FRAME) {
                modelSetAnimFrame(&hand->gunmodel, VR_DY357_COCKED_FRAME);
            }

            hand->animload = -1;
            hand->animmode = HANDANIMMODE_BUSY;
#if VERSION >= VERSION_PAL_BETA
            hand->animframeincfreal = modelGetAbsAnimSpeed(&hand->gunmodel) * PALUPF(hand->animframeinc);
#endif
        }

        if (hand->unk0cc8_02) {
            hand->animframeinc = 0;
#if VERSION >= VERSION_PAL_BETA
            hand->animframeincfreal = 0.0f;
#endif
        }

        s4 = bgun0f09815c(hand);

#if VERSION >= VERSION_PAL_BETA
        s2 = hand->animframeincfreal + s4;
#else
        s2 = hand->animframeinc + s4;
#endif

        if (s4 == 0 && s2 > 0) {
            s4--;
        }

        if (hand->unk0ce8) {
            done = false;
            cmd = hand->unk0ce8;

            if (cmd) {
                s0 = 0;

                do {
                    if (cmd->type == GUNCMD_END) {
                        done = true;
                    }
                    else if (cmd->type == GUNCMD_SHOWPART || cmd->type == GUNCMD_HIDEPART) {
                        if (s2 >= cmd->unk02) {
                            s32 i;
                            index = -1;

                            for (i = 0; i < s0; i++) {
                                if (cmd->unk04 == partnums[i]) {
                                    index = i;
                                }
                            }

                            if (index == -1) {
                                index = s0;
                                s0++;

                                if (1);

                                partnums[index] = cmd->unk04;
                                partframes[index] = -1;
                            }

                            if (cmd->unk02 > partframes[index]) {
                                partframes[index] = cmd->unk02;
                                partsvisible[index] = cmd->type == GUNCMD_SHOWPART ? true : false;
                            }
                        }
                    }
                    else {
                        switch (cmd->type) {
                            case GUNCMD_WAITFORZRELEASED:
                                if (hand->unk0cc8_01) {
                                    if (s2 >= cmd->unk02 && s4 < cmd->unk02 && s4 < s2) {
#if VERSION >= VERSION_PAL_BETA
                                        f32 tmp = cmd->unk02 - bgun0f09815c(hand);
																		tmp /= 2;

																		if (hand->animframeincfreal > tmp) {
#if PAL
																				hand->animframeinc = tmp * 0.83333333f / modelGetAbsAnimSpeed(&hand->gunmodel);
#else
																				hand->animframeinc = tmp / modelGetAbsAnimSpeed(&hand->gunmodel);
#endif
																				hand->animframeincfreal = modelGetAbsAnimSpeed(&hand->gunmodel) * PALUPF(hand->animframeinc);
																		}

																		s2 = hand->animframeincfreal + s4;
#else
                                        s32 tmp = cmd->unk02 - (s32)bgun0f09815c(hand);
                                        tmp /= 2;

                                        if (hand->animframeinc > tmp) {
                                            hand->animframeinc = tmp;
                                        }

                                        s2 = hand->animframeinc + s4;
#endif
                                    }
                                }
                                break;
                            case GUNCMD_REPEATUNTILFULL:
                                if (hand->incrementalreloading && s2 >= cmd->unk02 && s4 < cmd->unk02 && s4 < s2) {
#if VERSION >= VERSION_PAL_BETA
                                    f32 sp78 = s2;

																while (sp78 >= cmd->unk02) {
																		sp78 += cmd->unk04 - cmd->unk02;
																}

																s4 = sp78;
																hand->animframeinc = 0;
																hand->animframeincfreal = 0;
#else
                                    s32 sp78 = cmd->unk04 + (((s32)s2 - cmd->unk02) % ((cmd->unk02 - cmd->unk04) + 1));
                                    s4 = sp78;
                                    hand->animframeinc = 0;
#endif

                                    modelSetAnimFrame(&hand->gunmodel, sp78);
                                    hand->animloopcount++;
                                    s2 = sp78;
                                }
                                break;
                        }
                    }

                    cmd++;
                } while (!done);

                if (s0 > 0) {
                    s32 i;

                    for (i = 0; i < s0; i++) {
                        bgunSetPartVisible(partnums[i], partsvisible[i], hand, modeldef);
                    }
                }
            }
        }

#if VERSION >= VERSION_PAL_BETA
        modelSetAnimPlaySpeed(&hand->gunmodel, PALUPF(4.0f), 0);
				modelTickAnimQuarterSpeed(&hand->gunmodel, hand->animframeinc, true);
#else
        modelTickAnim(&hand->gunmodel, hand->animframeinc, true);
#endif

        s2 = bgun0f09815c(hand);

        if (hand->unk0ce8) {
            bool done = false;
            struct guncmd* cmd = hand->unk0ce8;
            f32 speed = 1.0f;
            bool hasspeed = false;
#if VERSION < VERSION_NTSC_1_0
            struct sndstate* audiohandle;
#endif

            if (cmd) {
                do {
                    if (cmd->type == GUNCMD_END) {
                        done = true;
                    }
                    else {
                        if (s2 >= cmd->unk02 && s4 < cmd->unk02 && s4 < s2) {
                            switch (cmd->type) {
                                case GUNCMD_PLAYSOUND:
#if VERSION >= VERSION_NTSC_1_0
                                    if (hasspeed) {
                                        snd00010718(0, 0, AL_VOL_FULL, AL_PAN_CENTER, cmd->unk04, speed, 1, -1, 1);
                                        hasspeed = false;
                                    }
                                    else {
                                        snd00010718(0, 0, AL_VOL_FULL, AL_PAN_CENTER, cmd->unk04, 1.0f, 1, -1, 1);
                                    }
#else
                                    audiohandle = sndStart(var80095200, cmd->unk04, NULL, -1, -1, -1, -1, -1);

																if (hasspeed && audiohandle) {
																		hasspeed = false;
																		audioPostEvent(audiohandle, AL_SNDP_PITCH_EVT, *(s32*)&speed);
																}
#endif
                                    break;
                                case GUNCMD_SETSOUNDSPEED:
                                    speed = cmd->unk04 / 1000.0f;
                                    hasspeed = true;
                                    break;
                                case GUNCMD_POPOUTSACKOFPILLS:
                                    hand->unk0cc8_04++;
                                    break;
                            }
                        }
                    }

                    cmd++;
                } while (!done);
            }
        }
    }
}

bool bgun0f098884(struct guncmd* cmd, struct gset* gset)
{
    s32 result = false;

    if (cmd->unk01 == 0) {
        return true;
    }

    if (cmd->unk01 == 1 && g_Vars.currentplayer->hands[HAND_LEFT].inuse == true) {
        result = true;
    }

    if (cmd->unk01 == 2 && gset->weaponfunc == FUNC_SECONDARY) {
        result = true;
    }

    return result;
}


void bgunStartAnimation(struct guncmd* cmd, s32 handnum, struct hand* hand)
{

    if (cmd->type != GUNCMD_PLAYANIMATION) {
        struct guncmd* loopcmd = cmd;
        s32 done = false;
        u32 rand = rngRandom() % 100;

        if (VrMotionThrowing && handnum == HAND_RIGHT && hand->gset.weaponnum == WEAPON_UNARMED) { // VR
            s32 targetAnim = vr_button_R_grip ? 1002 : 1055;
            struct guncmd *found = NULL;

            while (loopcmd->type != GUNCMD_END) {
                if (loopcmd->type == GUNCMD_RANDOM) {
                    struct guncmd *chosen = (struct guncmd *) loopcmd->unk04;
                    if (chosen->type == GUNCMD_PLAYANIMATION &&
                        chosen->unk02 == targetAnim) {
                        found = (struct guncmd *) loopcmd->unk04;
                        break;
                    }
                }
                loopcmd++;
            }

            if (found != NULL) {
                bgunStartAnimation(found, handnum, hand);
            }
        // VR: hand is this hand (hand[HAND_LEFT] read past the hands array)
        }else if (VrMotionThrowing && handnum == HAND_LEFT && hand->gset.weaponnum == WEAPON_UNARMED) { // VR
            s32 targetAnim = vr_button_L_grip ? 1002 : 1055;
            struct guncmd *found = NULL;

            while (loopcmd->type != GUNCMD_END) {
                if (loopcmd->type == GUNCMD_RANDOM) {
                    struct guncmd *chosen = (struct guncmd *) loopcmd->unk04;
                    if (chosen->type == GUNCMD_PLAYANIMATION &&
                        chosen->unk02 == targetAnim) {
                        found = (struct guncmd *) loopcmd->unk04;
                        break;
                    }
                }
                loopcmd++;
            }

            if (found != NULL) {
                bgunStartAnimation(found, handnum, hand);
            }
        }else {
            while (loopcmd->type != GUNCMD_END) {
                if (bgun0f098884(loopcmd, &hand->gset) && !done) {
                    if (loopcmd->type == GUNCMD_INCLUDE) {
                        done = true;
                        bgunStartAnimation((struct guncmd *)loopcmd->unk04, handnum, hand);
                    } else if (loopcmd->type == GUNCMD_RANDOM) {
                        if ((struct guncmd *)loopcmd->unk04 != hand->unk0d80 && loopcmd->unk02 > rand) {
                            done = true;
                            bgunStartAnimation((struct guncmd *)loopcmd->unk04, handnum, hand);
                        }
                    }
                }

                loopcmd++;
            }
        }
    }
    else {
        hand->animload = cmd->unk02;
        hand->animmode = HANDANIMMODE_IDLE;
        hand->unk0cc8_01 = 0;
        hand->incrementalreloading = false;
        hand->unk0ce8 = cmd;
        hand->animloopcount = 0;
        hand->unk0cc8_02 = 0;
        hand->unk0d0e_07 = false;
        hand->unk0d80 = cmd;

    }
}

bool bgun0f098a44(struct hand* hand, s32 time)
{
    struct guncmd* cmd = hand->unk0ce8;
    s32 waittimekeyframe = -1;
    s32 zreleasekeyframe = -1;

    if (hand->animmode == HANDANIMMODE_IDLE) {
        return (hand->animload == -1);
    }

    while (cmd->type != GUNCMD_END && waittimekeyframe == -1) {
        if (cmd->type == GUNCMD_WAITFORZRELEASED) {
            zreleasekeyframe = cmd->unk02;
        }

        if (cmd->type == GUNCMD_WAITTIME && time == cmd->unk04) {
            waittimekeyframe = cmd->unk02;
        }

        cmd++;
    }

    if (waittimekeyframe >= 0) {
#if VERSION >= VERSION_PAL_BETA
        if (hand->unk0cc8_01 && bgun0f09815c(hand) <= zreleasekeyframe) {
						return false;
				}

				return (bgun0f09815c(hand) + hand->animframeincfreal >= waittimekeyframe);
#else
        if (hand->unk0cc8_01 && (s32)bgun0f09815c(hand) <= zreleasekeyframe) {
            return false;
        }

        return (bgun0f09815c(hand) + hand->animframeinc >= waittimekeyframe);
#endif
    }

    return true;
}

s32 bgun0f098b80(struct hand* hand, s32 arg1)
{
    struct guncmd* cmd = hand->unk0ce8;
    s32 keyframe = -1;

    if (hand->animmode == HANDANIMMODE_IDLE) {
        return 0;
    }

    while (cmd->type != GUNCMD_END && keyframe == -1) {
        if (cmd->type == GUNCMD_WAITTIME) {
            if (cmd->unk04 == arg1) {
                keyframe = cmd->unk02;
            }
        }

        cmd++;
    }

    if (keyframe == -1) {
        keyframe = 0;
    }

    return keyframe;
}

bool bgunIsAnimBusy(struct hand* hand)
{
    return hand->animmode != HANDANIMMODE_IDLE;
}

void bgunResetAnim(struct hand* hand)
{
    hand->animload = -1;
    hand->animmode = HANDANIMMODE_IDLE;
    hand->unk0cc8_01 = false;
    hand->incrementalreloading = false;
    hand->unk0ce8 = NULL;
    hand->animloopcount = 0;
    hand->unk0cc8_02 = false;
    hand->unk0d0e_07 = false;
}

void bgunGetWeaponInfo(struct handweaponinfo* info, s32 handnum)
{
    s32 weaponnum = bgunGetWeaponNum2(handnum);

    info->weaponnum = weaponnum;
    info->definition = g_Weapons[weaponnum];
    info->gunctrl = &g_Vars.currentplayer->gunctrl;
}


/*
 * Return values:
 * -1 = gun function doesn't exist or ammo fully depleted
 * 0 = trigger reload
 * 1 = has ammo in clip and ammo in reserve
 * 2 = has ammo in clip but none in reserve
 * 3 = gun doesn't use ammo or clip is full
*/



s32 bgun0f098ca0(s32 funcnum, struct handweaponinfo* info, struct hand* hand)
{
    s32 result = 3;
    struct weaponfunc* func = weaponGetFunction(&hand->gset, funcnum);

    if (!func) {
        return -1;
    }

    if (func->ammoindex != -1) {
        s32 ammoindex = func->ammoindex;

        if (info->gunctrl->ammotypes[ammoindex] >= 0
            && hand->loadedammo[ammoindex] < hand->clipsizes[ammoindex]) {
            s32 minqty = 1;

            if (info->weaponnum == WEAPON_SHOTGUN && funcnum == FUNC_SECONDARY) {
                minqty = 2;
            }

            if (info->weaponnum == WEAPON_TRANQUILIZER && funcnum == FUNC_SECONDARY) {
                minqty = bgunGetMinClipQty(WEAPON_TRANQUILIZER, FUNC_SECONDARY);
            }

            result = 1;


            s32 vrhand = (hand == &g_Vars.currentplayer->hands[HAND_LEFT]) ? HAND_LEFT : HAND_RIGHT;

            // Manual reload owns the ammo slots with a row (vrReloadOwnsAmmo: the SuperDragon's rifle
            // clip and grenade magazine both, Task F9e): never auto reloaded. A function's ammo with
            // no row (none today) auto reloads as in the original game, except while the free hand is
            // busy with the row's clip or rack (vrReloadFreeHandBusy): it waits for it
            if (vrReloadOwnsAmmo(vrhand, ammoindex)) {
                if (hand->loadedammo[ammoindex] < minqty) {
                    result = 2; /* manual reload owns this ammo: never auto reload */
                } else if (g_Vars.currentplayer->ammoheldarr[info->gunctrl->ammotypes[ammoindex]] == 0) {
                    result = 2;
                }
            }
            else if (hand->loadedammo[ammoindex] < minqty) {
                result = 0;

                if (g_Vars.currentplayer->ammoheldarr[info->gunctrl->ammotypes[ammoindex]] == 0) {
                    result = -1;
                } else if (vrReloadFreeHandBusy(vrhand)) {
                    result = 2;
                }
            }
            else {
                if (g_Vars.currentplayer->ammoheldarr[info->gunctrl->ammotypes[ammoindex]] == 0) {
                    result = 2;
                }
            }

        }
    }

    return result;
}



void bgun0f098df8(s32 weaponfunc, struct handweaponinfo* info, struct hand* hand, u8 onebullet, u8 checkunequipped)
{
    struct weaponfunc* func = weaponGetFunction(&hand->gset, weaponfunc);

    if (func && func->ammoindex != -1) {
        s32 ammoindex = func->ammoindex;

        if (info->gunctrl->ammotypes[ammoindex] >= 0) {
            s32 amount = hand->clipsizes[ammoindex] - hand->loadedammo[ammoindex];

            s32 reloadindex = bgunGetUnequippedReloadIndex(info->weaponnum);

            if (g_FrIsValidWeapon) {
                reloadindex = -1;
            }

            if (checkunequipped && reloadindex >= 0) {
#if VERSION >= VERSION_PAL_BETA
                amount -= hand->gunroundsspent[reloadindex] / TICKS(256);
#else
                amount -= hand->gunroundsspent[reloadindex] >> 8;
#endif
            }

            if (onebullet) {
                amount = 1;
            }

            if (amount > g_Vars.currentplayer->ammoheldarr[info->gunctrl->ammotypes[ammoindex]]) {
                amount = g_Vars.currentplayer->ammoheldarr[info->gunctrl->ammotypes[ammoindex]];
            }

#if VERSION >= VERSION_JPN_FINAL
            // In most versions of the game, reloading the shotgun while going
						// through a teleport in Deep Sea will cause the shotgun to load
						// more ammo than its capacity. JPN Final fixes this here.
						if (amount > hand->clipsizes[ammoindex] - hand->loadedammo[ammoindex]) {
								amount = hand->clipsizes[ammoindex] - hand->loadedammo[ammoindex];
						}
#endif

            hand->loadedammo[ammoindex] += amount;
            g_Vars.currentplayer->ammoheldarr[info->gunctrl->ammotypes[ammoindex]] -= amount;

            if (info->definition->ammos[ammoindex]->flags & AMMOFLAG_NORESERVE) {
                g_Vars.currentplayer->ammoheldarr[info->gunctrl->ammotypes[ammoindex]] = 0;
            }

            if (func);
        }
    }
}

void bgun0f098f8c(struct handweaponinfo* info, struct hand* hand)
{
    s32 i;

    for (i = 0; i < 2; i++) {
        if (weaponGetFunction(&hand->gset, i)) {
            bgun0f098df8(i, info, hand, 0, 1);
        }
    }
}

bool bgun0f099008(s32 handnum)
{

    struct handweaponinfo info;

    bgunGetWeaponInfo(&info, handnum);

    if (bgun0f098ca0(0, &info, &g_Vars.currentplayer->hands[handnum]) > 0) {
        return true;
    }

    if (bgun0f098ca0(1, &info, &g_Vars.currentplayer->hands[handnum]) > 0) {
        return true;
    }

    return false;
}

bool bgun0f0990b0(struct weaponfunc* basefunc, struct weapon* weapon)
{
    if (!basefunc) {
        return true;
    }

    if (basefunc->type == INVENTORYFUNCTYPE_NONE) {
        return true;
    }

    if ((basefunc->type & 0xff) == INVENTORYFUNCTYPE_MELEE) {
        return true;
    }

    if ((basefunc->type & 0xff) == INVENTORYFUNCTYPE_SPECIAL) {
        struct weaponfunc_special* func = (struct weaponfunc_special*)basefunc;

        if (func->specialfunc != HANDATTACKTYPE_DETONATE
            && func->specialfunc != HANDATTACKTYPE_BOOST
            && func->specialfunc != HANDATTACKTYPE_REVERTBOOST) {
            return true;
        }
    }

    if ((basefunc->type & 0xff) == INVENTORYFUNCTYPE_THROW) {
        if (basefunc->ammoindex <= -1) {
            return true;
        }
    }

    if (basefunc->ammoindex >= 0
        && weapon->ammos[basefunc->ammoindex]
        && bgunGetAmmoCount(weapon->ammos[basefunc->ammoindex]->type) <= 0) {
        return true;
    }

    return false;
}

bool bgun0f099188(struct hand* hand, s32 gunfunc)
{
    struct weaponfunc* func = weaponGetFunction(&hand->gset, gunfunc);
    struct weapon* weapon = weaponFindById(hand->gset.weaponnum);

    if (bgunIsUsingSecondaryFunction() == gunfunc) {
        return false;
    }

    return bgun0f0990b0(func, weapon);
}

// VR: PD-VRR "dry:" lines, the empty trigger's path (the user: the Falcon 2 does not click when
// empty): the pull with what decides it, the engine's empty state, each click and the release
// VR: a row-dry pull just went to the empty state, which clicks on its first tick (Task H6a)
static bool VrDryClickOnPull[2] = { false, false };

// VR: the rounds a function needs loaded to fire: the Tranquilizer's Lethal Injection takes its
// minimum (bgunGetMinClipQty), every other function 1. Manual reload never auto reloads, so
// bgun0f098ca0 calls a clip under it loaded, and the stab fired with 1 to 3 darts and emptied the
// clip (Task J8, secondary functions audit); under it the trigger clicks dry as an empty clip does
static s32 vrDryMinQty(struct hand *hand)
{
    if (hand->gset.weaponnum == WEAPON_TRANQUILIZER && hand->gset.weaponfunc == FUNC_SECONDARY) {
        return bgunGetMinClipQty(WEAPON_TRANQUILIZER, FUNC_SECONDARY);
    }

    return 1;
}

static void vrDryLog(s32 handnum, struct hand *hand, const char *what)
{
    char buf[256];
    struct weaponfunc *func = gsetGetWeaponFunction(&hand->gset);
    s32 ammoindex = func != NULL ? func->ammoindex : -1;

    snprintf(buf, sizeof(buf), "dry: %s hand=%d weapon=%d func=%d ammoindex=%d loaded=%d state=%d"
            " stateframes=%d stateflags=%x animmode=%d animload=%d animframe=%.1f",
            what, handnum, hand->gset.weaponnum, hand->gset.weaponfunc, ammoindex,
            ammoindex >= 0 ? hand->loadedammo[ammoindex] : -1, hand->state, hand->stateframes,
            hand->stateflags, hand->animmode, hand->animload,
            hand->animmode == HANDANIMMODE_BUSY ? bgun0f09815c(hand) : -1.0f);
    vrHolsterDiagLine(buf);
}

s32 bgunTickIncIdle(struct handweaponinfo* info, s32 handnum, struct hand* hand, s32 lvupdate)
{
    bool usesec;
    s32 gunfunc = bgunIsUsingSecondaryFunction();
    s32 sp34;
    s32 sp30;
    bool changefunc;
    s32 next;
    struct hand* lhand;
    struct weaponfunc* func;

    hand->lastdirvalid = false;
    hand->burstbullets = 0;
#if VERSION < VERSION_PAL_BETA
    hand->animframeincfreal = hand->animframeinc;
#endif
    hand->shotremainder = 0;

    // If ready to change gun due to manual switch, just do that
    if (bgunIsReadyToSwitch(handnum) && bgunSetState(handnum, HANDSTATE_CHANGEGUN)) {
        return lvupdate;
    }

    if (gunfunc == hand->gset.weaponfunc) {
        hand->unk0cc8_07 = false;
    }

    hand->unk0cc8_08 = false;

    if (hand->inuse) {
        sp34 = bgun0f098ca0(hand->gset.weaponfunc, info, hand);

        // Handle changing gun function
        if (gunfunc != hand->gset.weaponfunc && hand->modenext != HANDMODE_RELOAD) {
            changefunc = true;

            if (hand->unk0cc8_07 && bgun0f098ca0(1 - hand->gset.weaponfunc, info, hand) < 0) {
                changefunc = false;
            }

            if (changefunc && info->weaponnum == WEAPON_COMBATKNIFE) {
                if (sp34 == 0) {
                    hand->count60 = 0;
                    hand->count = 0;
                    hand->gset.weaponfunc = gunfunc;

                    if (bgunSetState(handnum, HANDSTATE_RELOAD)) {
                        return lvupdate;
                    }
                }
                else {
                    if (sp34 < 0) {
                        changefunc = false;
                    }
                }
            }

            if (changefunc) {
                hand->unk0cc8_07 = false;

                if (bgunSetState(handnum, HANDSTATE_CHANGEFUNC)) {
                    return lvupdate;
                }
            }
        }

        if (sp34 < 0 && vrHolsterIsG5Detonator(info->weaponnum)) {
            // VR: G5 Building's Detonator has no rounds by design, as the original game's left hand
            // detonator: it stays idle, with no auto switch (which holds the hand lowered, pitched
            // 50 degrees off the controller) and no function change; its trigger detonates
            // (vrHolsterDeviceTrigger)
        } else if (sp34 < 0) {
            // Attempted to shoot with no ammo

            // Consider switching to another weapon
            if (weaponHasFlag(info->weaponnum, WEAPONFLAG_THROWABLE)
                && (info->weaponnum != WEAPON_REMOTEMINE || handnum != HAND_LEFT)
                && bgunSetState(handnum, HANDSTATE_AUTOSWITCH)) {
                return lvupdate;
            }

            // Consider switching to other gun function
            usesec = FUNCISSEC();

            if (usesec == gunfunc) {
                sp30 = bgun0f098ca0(1 - hand->gset.weaponfunc, info, hand);

                if (bgun0f099188(hand, 1 - hand->gset.weaponfunc)
                    && info->weaponnum != WEAPON_REAPER) {
                    if (info->gunctrl->wantammo) {
                        func = weaponGetFunction(&hand->gset, 1 - hand->gset.weaponfunc);

                        if ((func->type & 0xff) != INVENTORYFUNCTYPE_MELEE) {
                            sp30 = -1;
                        }
                    }
                    else {
                        sp30 = -1;
                    }
                }

                if (sp30 < 0) {
                    hand->unk0cc8_08 = true;
                }
                else {
                    if (!weaponHasFlag(info->weaponnum, WEAPONFLAG_04000000)
                        || hand->gset.weaponfunc == FUNC_SECONDARY) {
                        hand->unk0cc8_07 = true;

                        if (bgunSetState(handnum, HANDSTATE_CHANGEFUNC)) {
                            return lvupdate;
                        }
                    }
                }
            }
        }
        else if (sp34 == 0) {
            // Clip is empty
            if (hand->triggeron && info->weaponnum != WEAPON_NONE) {
                hand->unk0cc8_01 = false;

                if (bgunSetState(handnum, HANDSTATE_ATTACKEMPTY)) {
                    vrDryLog(handnum, hand, "empty state (engine clip empty)");
                    return lvupdate;
                }
            }
            else {
                hand->count60 = 0;
                hand->count = 0;

                if (bgunSetState(handnum, HANDSTATE_RELOAD)) {
                    hand->modenext = HANDMODE_NONE;
                    return lvupdate;
                }
            }
        }
        else {
            // VR: manual reload's ammo is never auto reloaded, so bgun0f098ca0 gives a gun with a row
            // 2 (as if loaded) when its clip is empty: a trigger the row allows on a gun with nothing
            // to fire (VRR_READY and dry: the DY357 fired out and shut, a pistol fired dry with its
            // clip in) went to the attack state, which fires nothing and never clicks (Task H6a). It
            // goes to the engine's empty state as the original game's empty clip does: a gun with a
            // finger animation (the DY357s: the hammer) plays it, the others don't; both click every 25
            // ticks while held. All click on the pull, as a refused pull does (vrDryTriggerTick, Task
            // F11d): the state's first 25 ticks are skipped, and a finger animation's first click is not
            // left for its keyframe, which a quick tap releases before (the Falcon 2's). The Reaper
            // is left out: out of ammo it goes on to its grinder on the pull, as in the original game.
            // So is the RC-P120's cloak while it is on: its trigger turns it off (Task J8)
            struct weaponfunc *vrdryfunc = gsetGetWeaponFunction(&hand->gset);

            if (hand->triggeron && info->weaponnum != WEAPON_NONE && info->weaponnum != WEAPON_REAPER
                    && vrdryfunc != NULL
                    && vrdryfunc->ammoindex >= 0 && info->gunctrl->ammotypes[vrdryfunc->ammoindex] >= 0
                    && hand->loadedammo[vrdryfunc->ammoindex] < vrDryMinQty(hand)
                    && vrReloadOwnsAmmo(handnum, vrdryfunc->ammoindex)
                    && !(info->weaponnum == WEAPON_RCP120 && hand->gset.weaponfunc == FUNC_SECONDARY
                        && (g_Vars.currentplayer->devicesactive & DEVICE_CLOAKRCP120))) {
                hand->unk0cc8_01 = false;

                if (bgunSetState(handnum, HANDSTATE_ATTACKEMPTY)) {
                    hand->stateframes = TICKS(25) + 1;
                    VrDryClickOnPull[handnum] = true;
                    vrDryLog(handnum, hand, "empty state (row dry)");
                    return lvupdate;
                }
            }

            // Clip has ammo
            if (hand->triggeron || (hand->activatesecondary && hand->gset.weaponfunc == FUNC_SECONDARY)) {
                if (info->weaponnum != WEAPON_NONE) {
                    g_Vars.currentplayer->doautoselect = false;

                    hand->mode = HANDMODE_ATTACK;
                    hand->count = 0;
                    hand->count60 = 0;
                    hand->triggerreleased = false;
                    hand->activatesecondary = false;

                    vrHolsterNoteShot(handnum); // VR: PD-VRR, the first attack after a holster switch

                    if (bgunSetState(handnum, HANDSTATE_ATTACK)) {
                        return lvupdate;
                    }
                }
            }

            // Not attacking, but the player may have attempted
            // to change guns or reload while firing
            if (hand->modenext != HANDMODE_NONE) {
                next = hand->modenext;

                hand->mode = hand->modenext;
                hand->count60 = 0;
                hand->count = 0;
                hand->modenext = HANDMODE_NONE;

                if (next == HANDMODE_RELOAD && sp34 < 2 && sp34 >= 0) {
                    if (bgunSetState(handnum, HANDSTATE_RELOAD)) {
                        if (handnum && handnum && handnum);
                        return lvupdate;
                    }
                }
            }
        }
    }

    if (handnum == HAND_RIGHT) {
        if (info->gunctrl->wantammo) {
            bgunAutoSwitchWeapon();
        }
        else {
            lhand = &g_Vars.currentplayer->hands[1] - handnum;

            if ((hand->unk0cc8_08 || !hand->inuse)
                && (lhand->unk0cc8_08 || !lhand->inuse)
                && (hand->triggeron || lhand->triggeron)) {
                bgunAutoSwitchWeapon();
            }

            hand->unk0cc8_08 = lhand->unk0cc8_08 = false;
        }
    }

    return 0;
}



void bgunSetArmPitch(struct hand* hand, f32 angle) // VR
{
    Mtxf base;
    Mtxf offset;
    mtx4Copy(&hand->posmtx, &base);

    // "Lower/raise" offset IN controller local space
    mtx4LoadXRotation(angle, &offset);

    // Local translation (adjust according to your VR units/scale)
    offset.m[3][0] = 0.0f;
    offset.m[3][1] = (1.0f - cosf(angle)) * -80.0f;
    offset.m[3][2] = sinf(angle) * 15.0f;

    // Compose: final = base * offset (offset applied in controller local space)
    mtx4MultMtx4(&base, &offset, &hand->posrotmtx);

    hand->useposrot = true;
    mtx4Copy(&hand->posrotmtx, &hand->posmtx);
}



// VR: a holster switch deletes the old third-person weapon (at the lower, or the auto switch's
// delete) and creates the new one (at the raise) in one tick, but weaponDeleteFromChr only marks the
// held object: its weapons_held slot is cleared when the next third-person tick frees it
// (playerTickThirdPerson in multiplayer), and playermgrCreateWeapon skips a filled slot. The raise
// then leaves the create pending, and bgunTickHand makes it once the slot is free, outside a switch
// (a switch's own raise creates). A new delete cancels it. Only the VR player (player 0) switches
// this way.
static bool g_VrWeaponCreatePending[2];

static void bgunDeleteChrWeapon(s32 handnum)
{
    if (vrHolsterFastSwitch()) {
        g_VrWeaponCreatePending[handnum] = false;
    }

    playermgrDeleteWeapon(handnum);
}

static void bgunCreateChrWeapon(s32 handnum)
{
    struct prop *held = g_Vars.currentplayer->prop->chr->weapons_held[handnum];

    if (vrHolsterFastSwitch() && held != NULL && (held->obj->hidden & OBJHFLAG_DELETING)) {
        g_VrWeaponCreatePending[handnum] = true;
        return;
    }

    playermgrCreateWeapon(handnum);
}

static void bgunTickPendingChrWeapon(s32 handnum)
{
    struct hand *hand;
    struct prop *held;

    if (g_Vars.currentplayernum != 0 || !g_VrWeaponCreatePending[handnum]) {
        return;
    }

    // A create left pending when a match ended must not reach the next (solo) level
    if (!(g_Vars.mplayerisrunning && (IS8MB() || PLAYERCOUNT() != 1))) {
        g_VrWeaponCreatePending[handnum] = false;
        return;
    }

    hand = &g_Vars.currentplayer->hands[handnum];

    if (hand->state == HANDSTATE_CHANGEGUN || hand->state == HANDSTATE_AUTOSWITCH) {
        return;
    }

    held = g_Vars.currentplayer->prop->chr->weapons_held[handnum];

    if (held == NULL) {
        g_VrWeaponCreatePending[handnum] = false;
        playermgrCreateWeapon(handnum);
    } else if ((held->obj->hidden & OBJHFLAG_DELETING) == 0) {
        g_VrWeaponCreatePending[handnum] = false;
    }
}

s32 bgunTickIncAutoSwitch(struct handweaponinfo* info, s32 handnum, struct hand* hand, s32 lvupdate)
{
    u32 stack;
    s32 someval;
    s32 gunfunc = bgunIsUsingSecondaryFunction();

    if (!hand->inuse && bgunSetState(handnum, HANDSTATE_IDLE)) {
        return lvupdate;
    }

    if (hand->stateminor == HANDSTATEMINOR_AUTOSWITCH_UNEQUIP) {
        s32 delay = TICKS(16);

        if (g_Vars.normmplayerisrunning) {
            delay = TICKS(12);
        }

        // VR: with holsters the lower before an auto switch (the last mine thrown) takes no ticks,
        // as a holster switch's does. The throw has already left the hand (HANDSTATE_ATTACK).
        if (vrHolsterFastSwitch()) {
            delay = 0;
        }

        if (hand->stateframes >= delay) {
            hand->stateminor++; // to HANDSTATEMINOR_AUTOSWITCH_DELETE
        }
        else {
            bgunSetArmPitch(hand, hand->stateframes * MAX_PITCH / delay); // nop
        }

    }

    if (hand->stateminor == HANDSTATEMINOR_AUTOSWITCH_DELETE) {
        hand->lastdirvalid = false;
#if VERSION < VERSION_PAL_BETA
        hand->animframeincfreal = hand->animframeinc;
#endif
        hand->shotremainder = 0;

        if (bgunIsReadyToSwitch(handnum) && bgunSetState(handnum, HANDSTATE_CHANGEGUN)) {
            if (g_Vars.mplayerisrunning && (IS8MB() || PLAYERCOUNT() != 1)) {
                bgunDeleteChrWeapon(handnum);
            }

            bgunFreeHeldRocket(handnum);

            // VR: this goes straight to the load, past the lower that unloads the weapon copy. The
            // new gun can load at the same gun-memory address, so the copy's modeldef check at the
            // raise would keep the old gun's copy (its rwdata and part lists sized for the old
            // model), drawn from memory the load overwrites meanwhile. Unload it here as the
            // lower does; the raise loads the new one.
            if (g_VrCopyWepModeldef != NULL) {
                vrCopyWepUnload();
            }

            hand->mode = HANDMODE_6;
            hand->stateminor = HANDSTATEMINOR_AUTOSWITCH_2;
            hand->count = 0;
            return 0;
        }

        if (hand->inuse) {
            someval = bgun0f098ca0(gunfunc, info, hand);

            if (info->weaponnum == WEAPON_TIMEDMINE || info->weaponnum == WEAPON_PROXIMITYMINE) {
                hand->gset.weaponfunc = gunfunc;
            }

            if (info->weaponnum == WEAPON_REMOTEMINE
                && gunfunc != hand->gset.weaponfunc
                && bgunSetState(handnum, HANDSTATE_CHANGEFUNC)) {
                return lvupdate;
            }

            if (g_Vars.currentplayer->doautoselect) {
                struct hand* otherhand = &g_Vars.currentplayer->hands[1 - handnum];
                struct handweaponinfo otherinfo;
                bool ready = true;

                bgunGetWeaponInfo(&otherinfo, 1 - handnum);

                if (otherhand->inuse) {
                    if (bgun0f098ca0(0, &otherinfo, otherhand) >= 0) {
                        ready = false;
                    }

                    if (bgun0f098ca0(1, &otherinfo, otherhand) >= 0) {
                        ready = false;
                    }

                    if (bgun0f099188(otherhand, otherhand->gset.weaponfunc)) {
                        ready = true;
                    }
                }

                if (otherhand->state != HANDSTATE_IDLE && otherhand->state != HANDSTATE_AUTOSWITCH) {
                    ready = false;
                }

                if (ready) {
                    bgunAutoSwitchWeapon();
                }
            }

            if (someval <= 1 && someval >= 0) {
                if (g_Vars.currentplayer->hands[1 - handnum].state != HANDSTATE_RELOAD) {
                    hand->count60 = 0;
                    hand->count = 0;

                    if (bgunSetState(handnum, HANDSTATE_RELOAD)) {
                        if (info->weaponnum == WEAPON_COMBATKNIFE) {
                            hand->mode = HANDMODE_11;
                            hand->pausetime60 = TICKS(17);
                            hand->count60 = 0;
                            hand->count = -1;
                            hand->stateminor = HANDSTATEMINOR_AUTOSWITCH_2;
                        }

                        return lvupdate;
                    }
                }
            }

            if (hand->modenext) {
                hand->mode = hand->modenext;
                hand->count60 = 0;
                hand->count = 0;
                hand->modenext = HANDMODE_NONE;
            }
        }

        bgunSetArmPitch(hand, MAX_PITCH); // nop
    }

    return 0;
}

bool bgunIsReloading(struct hand* hand)
{
    if (hand->state == HANDSTATE_RELOAD) {
        return true;
    }

    return false;
}

s32 bgunTickIncReload(struct handweaponinfo* info, s32 handnum, struct hand* hand, s32 lvupdate)
{
    u32 stack;
    struct weaponfunc* func = gsetGetWeaponFunction(&hand->gset);

    if (g_Vars.currentplayer->isdead) {
        hand->animmode = HANDANIMMODE_IDLE;
        hand->animload = -1;

        if (bgunSetState(handnum, HANDSTATE_IDLE)) {
            return lvupdate;
        }
    }

    if (hand->statecycles == 0) {
        struct hand* otherhand = &g_Vars.currentplayer->hands[1 - handnum];

        hand->gs_int1 = -1;
        hand->gs_int2 = 0;

        if (otherhand->state == HANDSTATE_RELOAD && otherhand->stateframes < TICKS(20)) {
            hand->stateminor = HANDSTATEMINOR_RELOAD_WAIT;
        }
    }

    if (hand->stateminor == HANDSTATEMINOR_RELOAD_WAIT) {
        struct hand* otherhand = &g_Vars.currentplayer->hands[1 - handnum];

        if (otherhand->state == HANDSTATE_RELOAD && otherhand->stateframes < TICKS(20)) {
            return 0;
        }

        hand->stateframes = 0;
        hand->statecycles = 0;
        hand->stateminor = HANDSTATEMINOR_RELOAD_MAIN;
        hand->statelastframe = 0;
    }

    if (hand->stateminor == HANDSTATEMINOR_RELOAD_MAIN) {
        if (hand->statecycles == 0) {
            if (func && (func->ammoindex == 0 || func->ammoindex == 1)) {
                if (info->definition->ammos[func->ammoindex]->reload_animation
                    && info->weaponnum != WEAPON_COMBATKNIFE) {

                    bgunStartAnimation(info->definition->ammos[func->ammoindex]->reload_animation, handnum, hand);

                    hand->unk0d0e_07 = true;

                    if (info->definition->ammos[func->ammoindex]->flags & AMMOFLAG_INCREMENTALRELOAD) {
                        hand->incrementalreloading = true;
                    }

                    if (info->weaponnum == WEAPON_GRENADE || info->weaponnum == WEAPON_NBOMB) {
                        hand->ejectstate = EJECTSTATE_INACTIVE;
                    }
                }
                else {
                    hand->stateminor++; // to HANDSTATEMINOR_RELOAD_LOWER
                }
            }
            else {
                if (bgunSetState(handnum, HANDSTATE_IDLE)) {
                    return lvupdate;
                }
            }
        }
        else {
            if (info->definition->ammos[func->ammoindex]->flags & AMMOFLAG_INCREMENTALRELOAD) {
                if (bgun0f098a44(hand, 1)) {
                    if ((hand->stateflags & HANDSTATEFLAG_00000010) == 0) {
                        s32 value;

                        bgun0f098df8(hand->gset.weaponfunc, info, hand, 1, 0);
                        hand->stateflags |= HANDSTATEFLAG_00000010;
                        value = bgun0f098ca0(hand->gset.weaponfunc, info, hand);

                        if (value >= 2) {
                            hand->incrementalreloading = false;
                        }

                        if (value == -1) {
                            hand->incrementalreloading = false;
                        }
                    }
                }
                else {
                    hand->stateflags = 0;
                }

                if (hand->triggeron) {
                    hand->incrementalreloading = false;
                }

#if VERSION >= VERSION_JPN_FINAL
                if (g_Vars.currentplayer->devicesactive & ~g_Vars.currentplayer->devicesinhibit & DEVICE_EYESPY) {
										hand->incrementalreloading = false;
								}
#endif
            }
            else {
                if ((hand->stateflags & HANDSTATEFLAG_00000010) == 0) {
                    if (bgun0f098a44(hand, 1)) {
                        bgun0f098df8(hand->gset.weaponfunc, info, hand, 0, 0);
                        hand->stateflags |= HANDSTATEFLAG_00000010;
                    }
                }
            }

            if (hand->animmode != HANDANIMMODE_BUSY) {
                if (bgunSetState(handnum, HANDSTATE_IDLE)) {
                    return lvupdate;
                }
            }

            if (1);
        }
    }

    if (hand->stateminor == HANDSTATEMINOR_RELOAD_LOWER) {
        if (hand->count60 > TICKS(15) || !hand->visible) {
            hand->mode = HANDMODE_11;
            hand->stateminor++; // to HANDSTATEMINOR_RELOAD_SOUND
            hand->pausetime60 = TICKS(17);
            hand->count60 = 0;
            hand->count = 0;
        }
        else {
            bgunSetArmPitch(hand, hand->count60 * MAX_PITCH / TICKS(16));
        }
    }

    if (hand->stateminor == HANDSTATEMINOR_RELOAD_SOUND) {
        if (hand->count == 0) {
            if (info->weaponnum == WEAPON_COMBATKNIFE
                && func->ammoindex >= 0
                && info->definition->ammos[func->ammoindex]->reload_animation) {
                bgunStartAnimation(info->definition->ammos[func->ammoindex]->reload_animation, handnum, hand);
                hand->unk0cc8_02 = true;
            }

            if ((hand->stateflags & HANDSTATEFLAG_00000010) == 0) {
                bgun0f098df8(hand->gset.weaponfunc, info, hand, 0, 0);
            }

            if (g_Vars.lvupdate240 > 0
                && g_Vars.currentplayer->cameramode != CAMERAMODE_THIRDPERSON
                && bgunIsLoaded()
                && !g_PlayerInvincible
                && !g_Vars.currentplayer->isdead) {
                switch (info->weaponnum) {
                    case WEAPON_NONE:
                    case WEAPON_UNARMED:
                    case WEAPON_COMBATKNIFE:
                    case WEAPON_LASER:
                    case WEAPON_GRENADE:
                    case WEAPON_TIMEDMINE:
                    case WEAPON_PROXIMITYMINE:
                    case WEAPON_REMOTEMINE:
                    case WEAPON_ECMMINE:
                    case WEAPON_COMMSRIDER:
                    case WEAPON_TRACERBUG:
                    case WEAPON_TARGETAMPLIFIER:
                    case WEAPON_BRIEFCASE2:
                        // No reload sound
                        break;
                    default:
                        sndStart(var80095200, SFX_RELOAD_DEFAULT, 0, -1, -1, -1, -1, -1);
                        break;
                }
            }
        }

        if (hand->count60 >= hand->pausetime60 && hand->count >= 2) {
            hand->mode = HANDMODE_12;
            hand->stateminor++; // to HANDSTATEMINOR_RELOAD_RAISE
            hand->count60 = 0;
            hand->count = 0;
        }
        else {
            bgunSetArmPitch(hand, MAX_PITCH);
        }
    }

    if (hand->stateminor == HANDSTATEMINOR_RELOAD_RAISE) {
        if (info->weaponnum == WEAPON_COMBATKNIFE) {
            hand->animmode = HANDANIMMODE_IDLE;
        }

        if (hand->count == 0) {
            g_Vars.currentplayer->doautoselect = false;
        }

        if (hand->count60 >= TICKS(23)
            || !weaponGetFileNum2(info->weaponnum)
            || !weaponHasFlag(info->weaponnum, WEAPONFLAG_00000040)
            || weaponHasFlag(info->weaponnum, WEAPONFLAG_00000080)) {
            hand->mode = HANDMODE_NONE;
            hand->count60 = 0;
            hand->count = 0;

            if (bgunSetState(handnum, HANDSTATE_IDLE)) {
                return lvupdate;
            }
        }
        else {
            bgunSetArmPitch(hand, (TICKS(23) - hand->count60) * MAX_PITCH / TICKS(23));
        }
    }

    return 0;
}

s32 bgunTickIncChangeFunc(struct handweaponinfo* info, s32 handnum, struct hand* hand, s32 lvupdate)
{
    struct guncmd* cmd;
    bool more = false;

    if (hand->statecycles == 0) {
        if (hand->gset.weaponfunc == FUNC_PRIMARY) {
            cmd = gsetGetPriToSecAnim(&hand->gset);
            hand->gset.weaponfunc = FUNC_SECONDARY;
        }
        else {
            cmd = gsetGetSecToPriAnim(&hand->gset);
            hand->gset.weaponfunc = FUNC_PRIMARY;
        }

        more = false;

        if (cmd != NULL) {
            bgunStartAnimation(cmd, handnum, hand);
            more = true;
            g_Vars.currentplayer->hands[HAND_RIGHT].unk0dd4 = -1;
        }
    }
    else {
        if (hand->animmode == HANDANIMMODE_BUSY) {
            more = true;
        }
    }

    if (!more && bgunSetState(handnum, HANDSTATE_IDLE)) {
        return lvupdate;
    }

    return 0;
}

s32 bgun0f09a3f8(struct hand* hand, struct weaponfunc* func)
{
    bool burst = false;
    bool smallburst = false;
    struct gunctrl* ctrl = &g_Vars.currentplayer->gunctrl;

    if ((func->flags & FUNCFLAG_BURST3) && hand->burstbullets < 3) {
        // Make automatics do single shot when holding aim
        if (!g_Vars.currentplayer->insightaimmode || (func->type & 0xff00) != 0x100) {
            // Not aiming and not an automatic weapon
            smallburst = true;
        }
    }

    if ((func->flags & FUNCFLAG_BURST2) && hand->burstbullets < 2) {
        smallburst = true;
    }

    if ((func->flags & FUNCFLAG_BURST5) && hand->burstbullets < 5) {
        smallburst = true;
    }

    if ((func->flags & FUNCFLAG_BURST50) && hand->burstbullets < 50) {
        burst = true;
    }

    if (smallburst) {
        burst = true;
    }

    if (hand->triggeron || (hand->stateflags & HANDSTATEFLAG_00000010) == 0 || burst) {
        if (func->ammoindex >= 0
            && hand->loadedammo[func->ammoindex] == 0
            && ctrl->ammotypes[func->ammoindex] >= 0) {
            // Clip is empty
            return -1;
        }

        if ((func->type & 0xff00) == 0x100) {
            struct weaponfunc_shootauto* autofunc = (struct weaponfunc_shootauto*)func;

            if (autofunc->turretaccel > 0) {
                if (hand->gs_float1 < 1) {
                    hand->gs_float1 += LVUPDATE60FREAL() / autofunc->turretaccel;

                    if (hand->gs_float1 > 1) {
                        hand->gs_float1 = 1;
                        return 1;
                    }
                }
            }
            else {
                hand->gs_float1 = 1;
            }

            return 1;
        }

        hand->gs_float1 = 1;

        if (smallburst) {
            if (hand->burstbullets > 0) {
                s32 delay = 3;

                if (hand->gset.weaponnum == WEAPON_SHOTGUN) {
                    delay = TICKS(13);
                }

                if (hand->stateframes < delay) {
                    return 0;
                }
            }

            hand->stateframes = 0;
        }

        if ((func->flags & FUNCFLAG_BURST3) && hand->burstbullets == 2) {
            smallburst = false;
        }

        if ((func->flags & FUNCFLAG_BURST2) && hand->burstbullets == 1) {
            smallburst = false;
        }

        if ((func->flags & FUNCFLAG_BURST5) && hand->burstbullets == 4) {
            smallburst = false;
        }

        if (smallburst) {
            return 1;
        }

        return 2;
    }

    if ((func->type & 0xff00) == (INVENTORYFUNCTYPE_SHOOT_AUTOMATIC & 0xff00)) {
        struct weaponfunc_shootauto* autofunc = (struct weaponfunc_shootauto*)func;

        if (autofunc->turretdecel > 0) {
            if (hand->gs_float1 > 0) {
                hand->gs_float1 -= LVUPDATE60FREAL() / autofunc->turretdecel;

                if (hand->gs_float1 < 0) {
                    hand->gs_float1 = 0;
                    return -1;
                }

                return 1;
            }
        }
        else {
            hand->gs_float1 = 0;
        }

        return -1;
    }

    return -1;
}

void bgun0f09a6f8(struct handweaponinfo* info, s32 handnum, struct hand* hand, struct weaponfunc* func)
{
    bool usesammo = true;

    static u32 rontime = 2;
    static u32 rofftime = 4;

    mainOverrideVariable("rontime", &rontime);
    mainOverrideVariable("rofftime", &rofftime);

    hand->firing = true;

    if ((func->type & 0xff00) == 0x100) {
        struct weaponfunc_shootauto* autofunc = (struct weaponfunc_shootauto*)func;
        f32 tmp;
        f32 tmp2;

        tmp = autofunc->initialrpm + (autofunc->maxrpm - autofunc->initialrpm) * hand->gs_float1;
        tmp2 = tmp / 60.0f * (LVUPDATE60FREAL() / 60.0f) + hand->shotremainder;

        hand->shotstotake = tmp2;
        hand->shotremainder = tmp2 - hand->shotstotake;

        if (hand->shotstotake <= 0) {
            if ((hand->stateflags & HANDSTATEFLAG_00000010) == 0) {
                hand->shotstotake++;
            }
            else {
                hand->firing = false;
            }
        }
    }
    else {
        hand->shotstotake = 1;

        if (hand->gset.weaponnum == WEAPON_LASER) {
            usesammo = false;
        }
    }

    hand->burstbullets += hand->shotstotake;

    if (func->flags & FUNCFLAG_NOMUZZLEFLASH) {
        hand->flashon = false;
    }
    else {
#ifdef PLATFORM_N64
        hand->flashon = true;
#else
        if (g_BgunGeMuzzleFlashes) {
            if (func->type == INVENTORYFUNCTYPE_SHOOT_SINGLE || (hand->shotstotake & 1)) {
                hand->flashon = true;
            }
        }
        else {
            hand->flashon = true;
        }
#endif
    }

    bgunStartSlide(handnum);

    hand->loadslide = 0;

    if (hand->firing) {
        hand->statevar1 = hand->stateframes;
        hand->stateflags |= HANDSTATEFLAG_00000020;
        hand->stateflags |= HANDSTATEFLAG_00000010;

        // VR: a shot of a gun with a kick (the Shotgun, the revolvers) is felt as its kick's two buzzes,
        // which the engine's rumble would stop
        if (vrGunKickDef(handnum) == NULL) {
            bgunRumble(handnum, info->weaponnum);
        }

        vrGunKickStart(handnum, info->weaponnum);

        if (usesammo && func->ammoindex >= 0) {
            hand->loadedammo[func->ammoindex] -= hand->shotstotake;

            if (hand->loadedammo[func->ammoindex] < 0) {
                // Note: loadedammo is negative
                hand->shotstotake += hand->loadedammo[func->ammoindex];
                hand->loadedammo[func->ammoindex] = 0;
            }
        }

        switch (func->type & 0xff00) {
            case 0:
            case 0x100:
                hand->attacktype = HANDATTACKTYPE_SHOOT;
                break;
            case 0x200:
                hand->attacktype = HANDATTACKTYPE_SHOOTPROJECTILE;
                break;
        }
    }

    if (hand->firing) {
        bool playsound = false;

        if (gsetGetFireslotDuration(&hand->gset) > 0) {
            if (g_Vars.lvframe60 != g_Vars.currentplayer->hands[1 - handnum].lastshootframe60
                && g_Vars.lvframe60 > hand->allowshootframe) {
                hand->allowshootframe = g_Vars.lvframe60 + gsetGetFireslotDuration(&hand->gset);
                playsound = true;
            }
        }
        else {
            if (hand->firing) {
                playsound = true;
            }
        }

        if (playsound) {
#if VERSION >= VERSION_NTSC_1_0
            OSPri prevpri = osGetThreadPri(0);
            osSetThreadPri(0, osGetThreadPri(&g_AudioManager.thread) + 1);
#endif

            if (hand->audiohandle2 && sndGetState(hand->audiohandle2) != AL_STOPPED) {
                audioStop(hand->audiohandle2);
            }

            if (hand->audiohandle3 && sndGetState(hand->audiohandle3) != AL_STOPPED) {
                audioStop(hand->audiohandle3);
            }

            if (gsetGetSingleShootSound(&hand->gset)) {
                struct sndstate* handle = NULL;

                if (hand->audiohandle2 == NULL) {
                    handle = sndStart(var80095200, gsetGetSingleShootSound(&hand->gset), &hand->audiohandle2, -1, -1, -1, -1, -1);
                }
                else if (hand->audiohandle3 == NULL) {
                    handle = sndStart(var80095200, gsetGetSingleShootSound(&hand->gset), &hand->audiohandle3, -1, -1, -1, -1, -1);
                }

                hand->lastshootframe60 = g_Vars.lvframe60;

                if (hand->gset.weaponnum == WEAPON_MAULER && handle) {
                    s32 matmot = hand->matmot1;
                    f32 tmp;
                    f32 frac = matmot / 3.0f;

                    if (frac > 1.0f) {
                        frac = 1.0f;
                    }

                    tmp = 1.0f - frac * 0.4f;

                    audioPostEvent(handle, AL_SNDP_PITCH_EVT, *(s32*)&tmp);
                }

            }

#if VERSION >= VERSION_NTSC_1_0
            osSetThreadPri(0, prevpri);
#endif
        }
    }
}


bool bgun0f09aba4(struct hand* hand, struct handweaponinfo* info, s32 handnum, struct weaponfunc_shoot* func)
{
    s32 unk24;
    s32 unk25;
    s32 sum;
    s32 unk26;
    s32 unk27;
    s32 recoverytime60;
    s32 frames;
    struct weapon* weapondef;
    f32 mult1;
    f32 recoildist;
    f32 recoilangle;
    f32 mult2;
    u32 stack;

#if PAL
    unk24 = func->unk24;
		unk25 = func->unk25;
		unk26 = func->unk26;
		unk27 = func->unk27;
		recoverytime60 = func->recoverytime60;
		weapondef = info->definition;

		if (unk24 >= 4) {
				unk24 = TICKS(unk24);
		}

		if (unk25 >= 4) {
				unk25 = TICKS(unk25);
		}

		if (unk26 >= 4) {
				unk26 = TICKS(unk26);
		}

		if (unk27 >= 4) {
				unk27 = TICKS(unk27);
		}

		if (recoverytime60 >= 4) {
				recoverytime60 = TICKS(recoverytime60);
		}

		sum = unk24 + unk25;
#elif VERSION >= VERSION_JPN_FINAL
    unk24 = func->unk24;
		unk25 = func->unk25;
		unk26 = func->unk26;
		unk27 = func->unk27;
		recoverytime60 = func->recoverytime60;
		weapondef = info->definition;
		sum = unk24 + unk25;
#else
    unk24 = func->unk24;
    unk25 = func->unk25;
    sum = unk24 + unk25;
    unk26 = func->unk26;
    unk27 = func->unk27;
    recoverytime60 = func->recoverytime60;
    weapondef = info->definition;
#endif

    frames = hand->stateframes - hand->statevar1;

    if (sum < 1) {
        sum = 0;
    }
    else {
        if (hand->triggerreleased
            && hand->triggeron
            && frames >= unk26
            && unk26 > 0
            && unk27 >= 0
            && (hand->stateflags & HANDSTATEFLAG_00000040) == 0
            && frames + unk27 < sum) {
            hand->stateflags |= HANDSTATEFLAG_00000040;
            hand->statevar1 = frames;

            hand->rotxstart = hand->rotxoffset;
            hand->rotxend = 0;

            hand->posend.x = 0;
            hand->posend.y = 0;
            hand->posend.z = 0;

            hand->posstart.x = hand->posoffset.x;
            hand->posstart.y = hand->posoffset.y;
            hand->posstart.z = hand->posoffset.z;
        }

        int ctrlIndex = (handnum == HAND_RIGHT ? 1 : 0);
        if (vrLaserOnOffHand(g_Vars.currentplayer->hands[HAND_RIGHT].gset.weaponnum)) {
            ctrlIndex = (handnum == HAND_RIGHT ? 0 : 1);
        }

        if (hand->stateflags & HANDSTATEFLAG_00000040) {
            if (unk27 > frames - hand->statevar1) {
                mult1 = cosf((f32)(unk27 - frames + hand->statevar1) * 1.5707963705063f / (f32)unk27) * 0.5f + 0.5f;

                float posAmpX = (recoildist / 100.0f);
                float angleAmpX = (recoilangle / 100.0f);

                float decay = 1.0f - mult1;


                hand->posoffset.x = hand->posoffset.x - posAmpX * decay;
                hand->posoffset.y = hand->posoffset.y;
                hand->posoffset.z = hand->posoffset.z;

                const float qw = gCtrlQuat[ctrlIndex][0];
                const float qx = gCtrlQuat[ctrlIndex][1];
                const float qy = gCtrlQuat[ctrlIndex][2];
                const float qz = gCtrlQuat[ctrlIndex][3];

                const float recoilRad = -(recoilangle)*decay * (M_BADTAU / 360.0f);

                const float h = recoilRad * 0.5f;
                const float rw = cosf(h);
                const float rx = sinf(h);
                const float ry = 0.0f;
                const float rz = 0.0f;

                float qTmp[4];
                qTmp[0] = qw * rw - qx * rx - qy * ry - qz * rz;
                qTmp[1] = qw * rx + qx * rw + qy * rz - qz * ry;
                qTmp[2] = qw * ry - qx * rz + qy * rw + qz * rx;
                qTmp[3] = qw * rz + qx * ry - qy * rx + qz * rw;

                {
                    const float invLen = 1.0f / sqrtf(qTmp[0] * qTmp[0] + qTmp[1] * qTmp[1] + qTmp[2] * qTmp[2] + qTmp[3] * qTmp[3]);
                    qTmp[0] *= invLen; qTmp[1] *= invLen; qTmp[2] *= invLen; qTmp[3] *= invLen;
                }



                vrBuildGunRotation(handnum, hand, qTmp, &hand->posrotmtx);

                // VR Apply per-weapon Z roll correction (and the Laser's turn)
                vrGunTurnApply(&hand->posrotmtx);

                hand->posrotmtx.m[3][0] = hand->posoffset.x;
                hand->posrotmtx.m[3][1] = hand->posoffset.y;
                hand->posrotmtx.m[3][2] = hand->posoffset.z;

                // The gun's kick (the Shotgun, the revolvers), after the two-hand aim (vr_gun_pos_rot's is
                // rebuilt here)
                vrGunKickApply(handnum, hand);

                hand->useposrot = true;
                hand->rotxoffset = 0.0f;

            }
            else {
                mtx4LoadIdentity(&hand->posrotmtx);
                hand->useposrot = false;
                return true;
            }
        }

        if (frames < sum && (hand->stateflags & HANDSTATEFLAG_00000040) == 0) {
            recoildist = func->recoildist;
            recoilangle = func->recoilangle;

            // VR: a gun with a kick (the revolvers, Task H4) takes none of the engine's turn or push, its
            // drawn pose kicked instead (vrGunKickApply); unk24, unk25 and recoverytime60, the lockout
            // between shots, are unchanged
            if (vrGunKickDef(handnum) != NULL) {
                recoildist = 0.0f;
                recoilangle = 0.0f;
            }

            if (frames < unk24) {
                mult2 = sinf(frames * 1.5707963705063f / (f32)unk24);
            }
            else {
                mult2 = cosf((f32)(frames - unk24) * M_PI / (f32)unk25) * 0.5f + 0.5f;
            }

            float posAmpX = (recoildist / 100.0f);
            float angleAmpX = (recoilangle / 100.0f);

            hand->posoffset.x = hand->posoffset.x - posAmpX * mult2;
            hand->posoffset.y = hand->posoffset.y;
            hand->posoffset.z = hand->posoffset.z;

            const float qw = gCtrlQuat[ctrlIndex][0];
            const float qx = gCtrlQuat[ctrlIndex][1];
            const float qy = gCtrlQuat[ctrlIndex][2];
            const float qz = gCtrlQuat[ctrlIndex][3];

            const float recoilRad = -(recoilangle)*mult2 * (M_BADTAU / 360.0f);

            const float h = recoilRad * 0.5f;
            const float rw = cosf(h);
            const float rx = sinf(h);
            const float ry = 0.0f;
            const float rz = 0.0f;

            float qTmp[4];
            qTmp[0] = qw * rw - qx * rx - qy * ry - qz * rz;
            qTmp[1] = qw * rx + qx * rw + qy * rz - qz * ry;
            qTmp[2] = qw * ry - qx * rz + qy * rw + qz * rx;
            qTmp[3] = qw * rz + qx * ry - qy * rx + qz * rw;

            {
                const float invLen = 1.0f / sqrtf(qTmp[0] * qTmp[0] + qTmp[1] * qTmp[1] + qTmp[2] * qTmp[2] + qTmp[3] * qTmp[3]);
                qTmp[0] *= invLen; qTmp[1] *= invLen; qTmp[2] *= invLen; qTmp[3] *= invLen;
            }

            vrBuildGunRotation(handnum, hand, qTmp, &hand->posrotmtx);

            // VR Apply per-weapon Z roll correction (and the Laser's turn)
            vrGunTurnApply(&hand->posrotmtx);

            hand->posrotmtx.m[3][0] = hand->posoffset.x;
            hand->posrotmtx.m[3][1] = hand->posoffset.y;
            hand->posrotmtx.m[3][2] = hand->posoffset.z;

            // The gun's kick (the Shotgun, the revolvers), after the two-hand aim (vr_gun_pos_rot's is
            // rebuilt here)
            vrGunKickApply(handnum, hand);

            mtx4Copy(&hand->posrotmtx, &hand->posmtx);

            hand->useposrot = true;
            mtx4Copy(&hand->posrotmtx, &hand->posmtx);

        }
    }

    if (sum <= frames) {
        if (unk27 >= 0 && hand->triggerreleased && hand->triggeron) {
            return true;
        }
        else if (sum + recoverytime60 <= frames) {
            return true;
        }
    }

    return false;
}







bool bgunTickIncAttackingShoot(struct handweaponinfo* info, s32 handnum, struct hand* hand)
{
    static u32 var80070128 = 99;

    struct weaponfunc* func = gsetGetWeaponFunction(&hand->gset);
    bool sp68;
    s32 sp64;
    s32 sp60;

    if (func == NULL) {
        return true;
    }

    if (hand->stateminor == HANDSTATEMINOR_ATTACK_SHOOT_0) {
        sp64 = 1;

        mainOverrideVariable("gkef", &var80070128);

        if (hand->statecycles == 0) {
            struct vrreloaddef *vrpump = vrReloadOwnedDef(handnum);

            hand->gs_float1 = 0;

            // VR: a gun the player pumps (manual reload's pumpaction row, the Shotgun: Task G0d) does
            // not play its fire animation: its kick moved the gun and both hands off the controllers
            // and its 80 degree flourish pumped the gun itself. The gun stays on the controller and is
            // kicked in its drawn pose (vrGunKickApply); its pump moves only with the player's hand
            // (vrReloadMoveRack). With no animation the shot also goes off at once, not at its frame 9
            if (func->fire_animation && !(vrpump != NULL && vrpump->pumpaction)) {
                bgunStartAnimation(func->fire_animation, handnum, hand);
                hand->unk0cc8_01 = true;
            }

            hand->burstbullets = 0;
        }

        if (!bgun0f098a44(hand, 2)) {
            sp64 = 0;
        }

        if (sp64) {
            hand->stateminor = HANDSTATEMINOR_ATTACK_SHOOT_1;
        }

        hand->matmot2 = hand->gs_float1;
    }

    if (hand->stateminor == HANDSTATEMINOR_ATTACK_SHOOT_1) {
        sp60 = bgun0f09a3f8(hand, func);

        if ((func->type & 0xff00) == 0x100) {
            struct weaponfunc_shootauto* autofunc = (struct weaponfunc_shootauto*)func;
            f32 floats[12];

            if (autofunc->vibrationstart != NULL && autofunc->vibrationmax != NULL) {
                func0f097b64(autofunc->vibrationstart, autofunc->vibrationmax, hand->gs_float1, floats);
                func0f097b40(hand->upgrademult, floats, hand->finalmult);
            }
        }

        if (sp60 > 0) {
            bgun0f09a6f8(info, handnum, hand, func);
        }

        if (sp60 < 0 || sp60 == 2) {
            hand->stateminor = HANDSTATEMINOR_ATTACK_SHOOT_2;
        }

        hand->matmot2 = hand->gs_float1;

        if (hand->triggeron && hand->matmot2 < 0.4f) {
            hand->matmot2 = 0.4f;
        }

        if (hand->triggerreleased) {
            hand->unk0cc8_01 = false;
        }

        return false;
    }

    if (hand->stateminor == HANDSTATEMINOR_ATTACK_SHOOT_2) {
        if (hand->stateflags & HANDSTATEFLAG_00000020) {
            sp68 = bgun0f09aba4(hand, info, handnum, (struct weaponfunc_shoot*)func);
        }
        else {
            sp68 = true;
        }

        if (hand->gset.weaponnum == WEAPON_SHOTGUN && hand->animmode == HANDANIMMODE_BUSY) {
            sp68 = false;
        }

        hand->matmot2 = hand->gs_float1;

        if (sp68 && !hand->triggeron) {
            hand->matmot2 = 0;
        }

        if (hand->gset.weaponnum == WEAPON_MAULER) {
            hand->matmot1 = 0;
        }

        return sp68;
    }

    return false;
}

bool bgunTickIncAttackingThrow(s32 handnum, struct hand* hand) {
    struct weaponfunc_throw *func = (struct weaponfunc_throw *) gsetGetWeaponFunction(&hand->gset);
    if (func == NULL) {
        return true;
    }



    if (hand->stateminor == HANDSTATEMINOR_ATTACK_THROW_0) {
        if ((hand->gset.weaponnum == WEAPON_LAPTOPGUN && hand->triggeron) ||
            (hand->gset.weaponnum == WEAPON_DRAGON && hand->triggeron)) { // VR
            hand->stateminor = HANDSTATEMINOR_ATTACK_THROW_1;
            /// Trigger still held down → wait
            return false;
        }

        if ((hand->gset.weaponnum == WEAPON_COMBATKNIFE &&
             hand->gset.weaponfunc == FUNC_SECONDARY)) {
            hand->stateminor = HANDSTATEMINOR_ATTACK_THROW_1;
            g_Vars.currentplayer->gunctrl.throwing = true;
            // Trigger still held down → wait
            return false;
        }


        if (hand->statecycles == 0) {
            if (func->base.flags & FUNCFLAG_DISCARDWEAPON) {
                invRemoveItemByNum(hand->gset.weaponnum);
                g_Vars.currentplayer->gunctrl.throwing = true;
#if VERSION >= VERSION_NTSC_1_0
                bgunSwitchToPrevious();
#else
                bgunAutoSwitchWeapon();
#endif
                hand->primetimer60 = 0;
                return true;
            }

            if (func->base.fire_animation) {
                bgunStartAnimation(func->base.fire_animation, handnum, hand);
                hand->unk0cc8_01 = true;
            }
        }

        if (func->base.fire_animation) {
            if (hand->triggerreleased) {
                hand->unk0cc8_01 = false;
            }

            if (bgun0f098a44(hand, 2)) {
                hand->stateminor = HANDSTATEMINOR_ATTACK_THROW_1;
                hand->unk0cc8_01 = false;
            }
        } else {
            hand->stateminor = HANDSTATEMINOR_ATTACK_THROW_1;
        }
    }


    if (hand->stateminor == HANDSTATEMINOR_ATTACK_THROW_1) {
        if (vrMotionThrowWeapon(hand->gset.weaponnum, hand->gset.weaponfunc)
            && !vrThrowWindsUpInAnim(hand->gset.weaponnum)
            && (hand->triggeron)) {
            // Trigger still held down → wait
            return false;
        }

        // 2) Laptop Gun / secondary Dragon: remove the weapon from the VR inventory
        if (hand->gset.weaponnum == WEAPON_LAPTOPGUN ||
            hand->gset.weaponnum == WEAPON_DRAGON) {
            invRemoveItemByNum(hand->gset.weaponnum);
        }

        // VR...
        if (vrMotionThrowWeapon(hand->gset.weaponnum, hand->gset.weaponfunc)){


            if (VrMotionThrowing) {
                // ===== MODE VR : lancer par vélocité du contrôleur =====
                velocity = vr_throw(handnum);
                if (vr_throw_cancelled) {
                    if(hand->gset.weaponnum == WEAPON_GRENADE
                       || hand->gset.weaponnum == WEAPON_NBOMB){
                        bgunSetState(handnum, HANDSTATE_RELOAD);
                    }else{
                        bgunSetState(handnum, HANDSTATE_IDLE);
                    }
                    return false;
                } else {
                    vr_throw_cancelled = false;
                    if((hand->gset.weaponnum == WEAPON_LAPTOPGUN && hand->gset.weaponfunc == FUNC_SECONDARY)
                       || (hand->gset.weaponnum == WEAPON_DRAGON && hand->gset.weaponfunc == FUNC_SECONDARY)){
                        g_Vars.currentplayer->gunctrl.throwing = true;
                        bgunSwitchToPrevious();
                        hand->primetimer60 = 0;
                    }
                }
            } else {
                // ===== MODE CLASSIQUE : lancer par bouton, pas de vélocité VR =====
                // La vélocité reste à {0,0,0} — bgunCreateThrownProjectile la calculera
                // depuis gundir exactement comme dans le jeu original (paste-2.txt)
                vr_throw_cancelled = false;
                if ((hand->gset.weaponnum == WEAPON_LAPTOPGUN && hand->gset.weaponfunc == FUNC_SECONDARY)
                    || (hand->gset.weaponnum == WEAPON_DRAGON && hand->gset.weaponfunc == FUNC_SECONDARY)) {
                    g_Vars.currentplayer->gunctrl.throwing = true;
                    bgunSwitchToPrevious();
                    hand->primetimer60 = 0;
                }
            }

        }

        hand->firing = true;
        hand->attacktype = HANDATTACKTYPE_THROWPROJECTILE;
        hand->loadedammo[func->base.ammoindex]--;
        hand->stateminor = HANDSTATEMINOR_ATTACK_THROW_2;

        return false;
    }

    if (hand->stateminor == HANDSTATEMINOR_ATTACK_THROW_2) {

        if (hand->stateframes > TICKS(func->recoverytime60) / 3) { // VR speedup reloading
            return true;
        }

        if (hand->gset.weaponnum == WEAPON_REMOTEMINE
            && bgunIsUsingSecondaryFunction() == true
            && hand->triggerreleased
            && hand->triggeron) {
            return true;
        }

        return false;
    }

    // This state is only used after having a grenade explode in the player's
    // hand. It waits 4 seconds before finishing, which means the player won't
    // pull out another grenade until the flames have cleared.
    if (hand->stateminor == HANDSTATEMINOR_ATTACK_THROW_GRENADEWAIT) {
        bgunResetAnim(hand);

        if (hand->stateframes > TICKS(func->activatetime60 + 240)) {
            return true;
        }

        return false;
    }

    hand->primetimer60 = hand->stateframes;

    // If held a grenade too long, force throw it and enter the wait state
    if (hand->gset.weaponnum == WEAPON_GRENADE
        && hand->gset.weaponfunc == FUNC_PRIMARY
        && hand->primetimer60 > TICKS(func->activatetime60)) {
        hand->firing = true;
        hand->attacktype = HANDATTACKTYPE_THROWPROJECTILE;
        hand->loadedammo[func->base.ammoindex]--;
        hand->stateminor = HANDSTATEMINOR_ATTACK_THROW_GRENADEWAIT;

        return false;
    }


    return false;
}

s32 bgunGetMinClipQty(s32 weaponnum, s32 funcnum)
{
    if (weaponnum == WEAPON_TRANQUILIZER && funcnum == FUNC_SECONDARY) {
        return 4;
    }

    return 1;
}

const char var7f1ab8ac[] = "changegunmem type %d CurrentPlayer->gunctrl.gunmemtype %d\n";
const char var7f1ab8e8[] = "LockTimer: %d\n";
const char var7f1ab8f8[] = "BriGun: Releasing gunmem - current gunmemtype %d gunmemnew %d\n";
const char var7f1ab938[] = "GiveMem: %d\n";

u32 var8007012c = 0x00000000;
u32 var80070130 = 0x00000000;

bool bgunTickIncAttackingMelee(s32 handnum, struct hand* hand)
{
    struct weaponfunc* func = gsetGetWeaponFunction(&hand->gset);

    if (func == NULL) {
        return true;
    }

    if (hand->gset.weaponnum == WEAPON_REAPER) {
        if (hand->statecycles == 0) {
            hand->matmot2 = 0.1f;
            hand->burstbullets = 0;
        }

        hand->firing = true;
        hand->attacktype = HANDATTACKTYPE_MELEE;
        hand->burstbullets++;

        if (hand->triggeron) {
            hand->matmot2 += 0.01f * LVUPDATE60FREAL();

            if (hand->matmot2 > 1) {
                hand->matmot2 = 1;
            }
        }
        else {
            hand->matmot2 = 0;
            return true;
        }

        return false;
    }

    if (hand->stateminor == HANDSTATEMINOR_ATTACK_MELEE_0) {
        if (hand->statecycles == 0) {
            hand->firing = true;
            hand->attacktype = HANDATTACKTYPE_MELEENOUNCLOAK;

            if (func->fire_animation) {

                bgunStartAnimation(func->fire_animation, handnum, hand);
                hand->unk0cc8_01 = true;
            }
        }

        if (func->fire_animation) {
            if (hand->triggerreleased) {
                hand->unk0cc8_01 = false;
            }

            if (bgun0f098a44(hand, 2)) {
                hand->stateminor = HANDSTATEMINOR_ATTACK_MELEE_1;
                hand->unk0cc8_01 = false;
            }
        }
        else {
            hand->stateminor = HANDSTATEMINOR_ATTACK_MELEE_1;
        }
    }

    if (hand->stateminor == HANDSTATEMINOR_ATTACK_MELEE_3 && bgun0f098a44(hand, 3)) {
        hand->stateminor = HANDSTATEMINOR_ATTACK_MELEE_1;
        hand->unk0cc8_01 = false;
    }

    if (hand->stateminor == HANDSTATEMINOR_ATTACK_MELEE_1) {
        hand->firing = true;
        hand->attacktype = HANDATTACKTYPE_MELEE;

        if (hand->gset.weaponnum == WEAPON_TRANQUILIZER && func->ammoindex >= 0) {
            if (hand->loadedammo[func->ammoindex] > bgunGetMinClipQty(WEAPON_TRANQUILIZER, FUNC_SECONDARY)) {
                hand->loadedammo[func->ammoindex] -= bgunGetMinClipQty(WEAPON_TRANQUILIZER, FUNC_SECONDARY);
            }
            else {
                hand->loadedammo[func->ammoindex] = 0;
            }
        }

        if (func->fire_animation) {
            if (func->fire_animation && !bgun0f098a44(hand, 3)) {
                hand->stateminor = HANDSTATEMINOR_ATTACK_MELEE_3;
            }
            else {
                hand->stateminor = HANDSTATEMINOR_ATTACK_MELEE_2;
            }
        }

        if (cheatIsActive(CHEAT_HURRICANEFISTS)) {
            hand->stateminor = HANDSTATEMINOR_ATTACK_MELEE_2;
        }

        return false;
    }

    if (hand->stateminor == HANDSTATEMINOR_ATTACK_MELEE_2) {
        if (!bgunIsAnimBusy(hand)) {
            return true;
        }

        if (cheatIsActive(CHEAT_HURRICANEFISTS) && hand->gset.weaponnum == WEAPON_UNARMED) {
            return true;
        }

        if (hand->stateframes > TICKS(60)) {
            return true;
        }

        return false;
    }

    return false;
}

bool bgunTickIncAttackingSpecial(struct hand* hand)
{
    struct weaponfunc_special* func = (struct weaponfunc_special*)gsetGetWeaponFunction(&hand->gset);

    if (!func) {
        return true;
    }

    if (hand->stateminor == HANDSTATEMINOR_ATTACK_SPECIAL_START) {
        hand->stateminor = HANDSTATEMINOR_ATTACK_SPECIAL_EXECUTE;
    }

    if (hand->stateminor == HANDSTATEMINOR_ATTACK_SPECIAL_EXECUTE) {
        hand->firing = true;
        hand->attacktype = func->specialfunc;

        if (func->base.ammoindex >= 0) {
            hand->loadedammo[func->base.ammoindex]--;
        }

        hand->stateminor = HANDSTATEMINOR_ATTACK_SPECIAL_RECOVER;
        return false;
    }

    if (hand->stateminor == HANDSTATEMINOR_ATTACK_SPECIAL_RECOVER) {
        if (hand->stateframes > TICKS(func->recoverytime60)) {
            return true;
        }

        return false;
    }

    return false;
}

// The engine's dry click for a trigger pulled with nothing to fire (bgunTickIncAttackEmpty). VR:
// apart so the trigger manual reload refuses (vrDryTriggerTick) clicks as the engine's empty gun does
static void bgunPlayEmptyClick(s32 weaponnum)
{
    switch (weaponnum) {
        case WEAPON_PHOENIX:
        case WEAPON_CALLISTO:
        case WEAPON_FARSIGHT:
        {
            // Maian weapons have a wet sounding click effect
            f32 speed = 2.07f;

#if VERSION >= VERSION_NTSC_1_0
            OSPri prevpri = osGetThreadPri(0);
            struct sndstate* handle;
            osSetThreadPri(0, osGetThreadPri(&g_AudioManager.thread) + 1);
#else
            struct sndstate* handle;
#endif

            handle = sndStart(var80095200, SFX_HIT_WATER, NULL, -1, -1, -1, -1, -1);

            if (handle) {
                audioPostEvent(handle, AL_SNDP_PITCH_EVT, *(s32*)&speed);
            }

#if VERSION >= VERSION_NTSC_1_0
            osSetThreadPri(0, prevpri);
#endif
        }
            // fall-through - unsure if intentional
        case WEAPON_TRANQUILIZER:
        case WEAPON_PSYCHOSISGUN:
        {
            // The tranquliser and psychosis gun use the standard click
            // effect but slightly faster.
            f32 speed = 1.5f;

#if VERSION >= VERSION_NTSC_1_0
            OSPri prevpri = osGetThreadPri(0);
            struct sndstate* handle;
            osSetThreadPri(0, osGetThreadPri(&g_AudioManager.thread) + 1);
#else
            struct sndstate* handle;
#endif

            handle = sndStart(var80095200, SFX_FIREEMPTY, NULL, -1, -1, -1, -1, -1);

            if (handle) {
                audioPostEvent(handle, AL_SNDP_PITCH_EVT, *(s32*)&speed);
            }

#if VERSION >= VERSION_NTSC_1_0
            osSetThreadPri(0, prevpri);
#endif
        }
            break;
        case WEAPON_UNARMED:
        case WEAPON_COMBATKNIFE:
        case WEAPON_GRENADE:
        case WEAPON_NBOMB:
        case WEAPON_TIMEDMINE:
        case WEAPON_PROXIMITYMINE:
        case WEAPON_REMOTEMINE:
        case WEAPON_COMBATBOOST:
            // No sound effect
            break;
        default:
            // Default click sound effect
            sndStart(var80095200, SFX_FIREEMPTY, NULL, -1, -1, -1, -1, -1);
            break;
    }
}

s32 bgunTickIncAttackEmpty(struct handweaponinfo* info, s32 handnum, struct hand* hand, s32 lvupdate)
{
    u32 stack;
    bool playsound = false;
    // VR: a gun with a row loaded while its trigger is held (the Crossbow's bolt, which no step of
    // the load refuses) has something to fire: it stops clicking and its finger animation is not
    // restarted, and the hand stays here until the release, so the shot takes a fresh pull (going idle
    // with the trigger held would fire it as the round goes in) (Task H6a)
    struct weaponfunc *vrfunc = gsetGetWeaponFunction(&hand->gset);
    bool vrloaded = vrfunc != NULL && vrfunc->ammoindex >= 0
        && info->gunctrl->ammotypes[vrfunc->ammoindex] >= 0
        && hand->loadedammo[vrfunc->ammoindex] >= vrDryMinQty(hand)
        && vrReloadOwnsAmmo(handnum, vrfunc->ammoindex);
    bool vrclickonpull = VrDryClickOnPull[handnum];
    // VR: the Tranquilizer's Lethal Injection short of its darts clicks with no stab (its fire
    // animation is the injection): the default case's click every 25 ticks (Task J8)
    s32 vrclickweapon = info->weaponnum == WEAPON_TRANQUILIZER && vrDryMinQty(hand) > 1
        ? WEAPON_NONE : info->weaponnum;

    VrDryClickOnPull[handnum] = false;

    if (vrloaded) {
        // Nothing: no click, no animation, no reset of the hand's animation
    } else switch (vrclickweapon) {
        case WEAPON_FALCON2:
        case WEAPON_FALCON2_SILENCER:
        case WEAPON_FALCON2_SCOPE:
        case WEAPON_MAGSEC4:
        case WEAPON_MAULER:
        case WEAPON_PHOENIX:
        case WEAPON_DY357MAGNUM:
        case WEAPON_DY357LX:
        case WEAPON_CMP150:
        case WEAPON_CYCLONE:
        case WEAPON_CALLISTO:
        case WEAPON_RCP120:
        case WEAPON_LAPTOPGUN:
        case WEAPON_REAPER:
        case WEAPON_TRANQUILIZER:
        case WEAPON_PP9I:
        case WEAPON_CC13:
            // These weapons are weapons with visible finger trigger animations
            if (hand->stateframes > TICKS(25)) {
                hand->stateframes -= TICKS(25);
                hand->stateflags = 0;

                bgunResetAnim(hand);
            }

            if (hand->animmode != HANDANIMMODE_BUSY) {
                bool restartedanim = false;

                if ((hand->stateflags & HANDSTATEFLAG_00000010) == 0) {
                    struct weaponfunc* func = NULL;

                    if (info->definition) {
                        func = gsetGetWeaponFunction(&hand->gset);
                    }

                    if (func && func->fire_animation) {
                        bgunStartAnimation(func->fire_animation, handnum, hand);
                        restartedanim = true;
                    }
                }

                if (!restartedanim && hand->stateframes > TICKS(25)) {
                    playsound = true;
                }
            }
            else if (bgun0f098a44(hand, 5)) {
                playsound = true;
            }

            // VR: the pull's click is played now, and the flag it sets skips the keyframe's
            if (vrclickonpull) {
                playsound = true;
            }
            break;
        default:
            // Weapons without visible trigger animations must
            // still play the click sound every 25 frames
            if (hand->stateframes > TICKS(25)) {
                playsound = true;

                hand->stateframes -= TICKS(25);
                hand->stateflags = 0;

                bgunResetAnim(hand);
            }
    }

    hand->mode = HANDMODE_13;
    hand->count60 = 0;
    hand->count = 0;

    if (playsound && (hand->stateflags & HANDSTATEFLAG_00000010) == 0) {
        hand->stateflags |= HANDSTATEFLAG_00000010;

        bgunPlayEmptyClick(info->weaponnum);
        vrDryLog(handnum, hand, "click (empty state)");
    }

    // Handle releasing trigger
    if (!hand->triggeron) {
        vrDryLog(handnum, hand, "release (empty state)");
        hand->mode = HANDMODE_NONE;
        hand->count60 = 0;
        hand->count = 0;

        if (bgunSetState(handnum, HANDSTATE_IDLE)) {
            return lvupdate;
        }

        bgunResetAnim(hand);
    }

    return 0;
}

s32 bgunTickIncAttack(struct handweaponinfo* info, s32 handnum, struct hand* hand, s32 lvupdate)
{
    u32 stack;
    struct weaponfunc* func = NULL;
    bool finished = true;
    u32 stack2;

    if (info->definition) {
        func = gsetGetWeaponFunction(&hand->gset);
    }

    if (func != NULL) {
        switch (func->type & 0xff) {
            case INVENTORYFUNCTYPE_SHOOT:
                finished = bgunTickIncAttackingShoot(info, handnum, hand);
                break;
            case INVENTORYFUNCTYPE_THROW:
                finished = bgunTickIncAttackingThrow(handnum, hand);
                break;
            case INVENTORYFUNCTYPE_MELEE:
                finished = bgunTickIncAttackingMelee(handnum, hand);
                break;
            case INVENTORYFUNCTYPE_SPECIAL:
                finished = bgunTickIncAttackingSpecial(hand);
                break;
        }
    }

    if (finished) {
        if (hand->gset.weaponnum == WEAPON_REAPER && hand->triggeron) {
            hand->gset.weaponfunc = FUNC_SECONDARY;
            finished = false;
        }

        if (finished && bgunSetState(handnum, HANDSTATE_IDLE)) {
            return lvupdate;
        }
    }

    if (1);
    if (1);

    return 0;
}

bool bgunIsReadyToSwitch(s32 handnum)
{
    struct player* player = g_Vars.currentplayer;

    // Dont switch if... something firing range related
    if (g_FrIsValidWeapon
        && frGetWeaponBySlot(frGetSlot()) == player->hands[HAND_RIGHT].gset.weaponnum
        && g_Vars.currentplayer->gunctrl.throwing == false) {
        return false;
    }

    // Don't switch right hand if left hand is about to auto switch
    if (handnum == HAND_RIGHT
        && player->hands[HAND_LEFT].inuse
        && player->hands[HAND_LEFT].state == HANDSTATE_AUTOSWITCH
        && player->hands[HAND_LEFT].stateminor == HANDSTATEMINOR_AUTOSWITCH_UNEQUIP) {
        return false;
    }

    if (player->gunctrl.switchtoweaponnum >= 0) {
        return true;
    }

    if (handnum == HAND_LEFT) {
        if (handnum == HAND_LEFT) {
            if (player->hands[HAND_RIGHT].state == HANDSTATE_RELOAD) {
                return false;
            }

            if (player->hands[HAND_RIGHT].state == HANDSTATE_CHANGEFUNC) {
                return false;
            }

            if (player->hands[HAND_RIGHT].state == HANDSTATE_ATTACK) {
                return false;
            }
        }

        if (player->hands[handnum].inuse && !player->gunctrl.dualwielding) {
            return true;
        }

        if (!player->hands[handnum].inuse && player->gunctrl.dualwielding) {
            return true;
        }
    }

    return false;
}

bool bgunCanFreeWeapon(s32 handnum)
{
    struct player* player = g_Vars.currentplayer;

    // VR: a holster switch frees the old gun in the tick its hands reach the load. The three ticks
    // kept the old gun's memory for the N64 frames still in flight; the port draws each frame's
    // display list before the next tick, and the weapon copy is unloaded before the load (at the
    // lower, or at the auto switch's delete, which skips the lower).
    if (player->hands[handnum].state == HANDSTATE_CHANGEGUN
        && player->hands[handnum].stateminor == HANDSTATEMINOR_CHANGEGUN_LOAD
        && (player->hands[handnum].count >= 3 || vrHolsterFastSwitch())
        && player->gunctrl.throwing == false) {
        return true;
    }

    return false;
}

bool bgun0f09bf44(s32 handnum)
{
    bool result = true;
    struct player* player = g_Vars.currentplayer;

    if (!bgunIsLoaded()) {
        result = false;
    }

    if (player->gunctrl.switchtoweaponnum != -1) {
        result = false;
    }

    if (handnum == HAND_LEFT && player->gunctrl.dualwielding != player->hands[handnum].inuse) {
        result = false;
    }

    if (player->gunctrl.gunmemnew >= 0) {
        result = false;
    }

    if (player->hands[1 - handnum].state == HANDSTATE_RELOAD) {
        result = false;
    }

    return result;
}

// VR: a holster switch skips the equip animation, but the gun file's rest pose (animnum 0) has the
// hand flat under the gun: the held pose is the frame a normal switch leaves the model on. That is
// the frame the equip animation ends on, except for WEAPONFLAG_00004000 guns, whose equip animation
// never plays: unk0cc8_02 zeroes its frame step and the switch goes idle with it set, so they hold
// the frame it starts on (frame 0, or the last frame for a reversed animation). Put the model
// straight on that frame without playing it, so none of the animation's sounds or keyframed part
// commands fire. bgunStartAnimation resolves the command list (includes, random picks, conditions)
// to its animation and speed; the hand's anim state is then cleared, so nothing ticks the model off
// that frame. Weapons without an equip animation (fists) start at rest. Either way an animation the old weapon queued while the hand was hidden (the fist
// clench when a draw flips the gun side, or a punch-speed draw) is dropped, and the idle matrix
// cache (unk0dd8, otherwise rebuilt only on a gun load or a function change) is rebuilt from the
// new pose instead of freezing a wrong one.
static void vrHolsterSetEquipPose(struct guncmd* equip, s32 handnum, struct hand* hand)
{
    bgunResetAnim(hand);

    if (equip) {
        bgunStartAnimation(equip, handnum, hand);
    }

    if (hand->animload >= 0 && hand->unk0ce8) {
        f32 speed = hand->unk0ce8->unk04 / 10000.0f;
        bool held = weaponHasFlag(hand->gset.weaponnum, WEAPONFLAG_00004000);
        f32 end;

        modelSetAnimation(&hand->gunmodel, hand->animload, false, 0.0f, speed, 0.0f);
        end = modelGetAnimEndFrame(&hand->gunmodel);

        // A reversed animation starts on its last frame (bgun0f0981e8) and ends on frame 0
        if (held) {
            modelSetAnimFrame(&hand->gunmodel, speed < 0.0f ? end : 0.0f);
        } else {
            modelSetAnimFrame(&hand->gunmodel, speed < 0.0f ? 0.0f : end);
        }
    } else {
        animInit(&hand->anim);
    }

    bgunResetAnim(hand);
    g_Vars.currentplayer->hands[HAND_RIGHT].unk0dd4 = -1;
}

s32 bgunTickIncChangeGun(struct handweaponinfo* info, s32 handnum, struct hand* hand, s32 lvupdate)
{
    u32 stack;
    struct weapon* weapon = info->definition;

    if (hand->statecycles == 0) {
        if (g_Vars.normmplayerisrunning == false) {
            hand->pausetime60 = 0;
        }
        else {
            hand->pausetime60 = 0;
        }

    }

    // Handle unequip animation. Wait in this state until the animation is
    // finished, or skip this state if there is no animation to play.
    if (hand->stateminor == HANDSTATEMINOR_CHANGEGUN_UNEQUIP) {
        bool skipanim = false;


        if (weaponHasFlag(info->weaponnum, WEAPONFLAG_THROWABLE)
            && !(info->weaponnum == WEAPON_REMOTEMINE && handnum == HAND_LEFT)
            && bgun0f098ca0(0, info, hand) < 0) {
            skipanim = true;
        }

        hand->count = 0;

        if (!skipanim) {
            // VR: a holster switch skips the unequip animation and so its sounds (the else branch
            // still waits out a thrown gun)
            if (weapon->unequip_animation
                && !vrHolsterFastSwitch()
                && hand->inuse == true
                && !(hand->ejectstate != EJECTSTATE_INACTIVE && hand->ejecttype == EJECTTYPE_GUN)) {
                if (hand->statecycles == 0) {
                    bgunStartAnimation(weapon->unequip_animation, handnum, hand);

                }
                else if (hand->animmode == HANDANIMMODE_IDLE) {
                    hand->stateminor++; // to HANDSTATEMINOR_CHANGEGUN_LOWER

                }
            }
            else {
                hand->stateflags |= HANDSTATEFLAG_00000001;

                if (hand->ejectstate == EJECTSTATE_INIT) {
                    return 0;
                }

                hand->stateminor++; // to HANDSTATEMINOR_CHANGEGUN_LOWER

            }
        }
        else {
            hand->stateminor++; // to HANDSTATEMINOR_CHANGEGUN_LOWER
        }

        if (hand->stateminor == HANDSTATEMINOR_CHANGEGUN_LOWER) {
            hand->stateframes = 0;
        }
    }

    // For classic guns, handle lowering it to offscreen.
    // Throw the gun if that's what the player is doing.
    if (hand->stateminor == HANDSTATEMINOR_CHANGEGUN_LOWER) {
        s32 delay = TICKS(16);
        bool throwing = false;
        u32 stack2;

        hand->count = 0;


        if (g_Vars.normmplayerisrunning) {
            delay = TICKS(12);
        }

        if (weapon->unequip_animation && (hand->stateflags & HANDSTATEFLAG_00000001) == 0) {
            delay = 1;
        }

        if (!hand->inuse) {
            delay = 1;
        }

        // VR: a holster switch lowers at once (in the call that reached the lower), so the gun is
        // never drawn pitched down. The pitch below is then only reached by a throw.
        if (vrHolsterFastSwitch()) {
            delay = 0;
        }


        // --- VR unload the weapon copy here
        // Only in a weapon change: the copy points at the gun's modeldef in gun memory, which the
        // new gun's load overwrites (see the auto switch's unload, bgunTickIncAutoSwitch). The left
        // hand also lowers alone, with no change pending, when dual wield ends or starts with the
        // same gun (bgunIsReadyToSwitch): the copy is then still the kept gun's and must stay, as
        // only a raise in use reloads it. Unloading it there left a pair let go of with no free
        // hand drawn (the copy and the fists both need it) until another switch.
        if (g_VrCopyWepModeldef != NULL && g_Vars.currentplayer->gunctrl.switchtoweaponnum >= 0) {
            s32 halfDelay = delay / 2.5;
            if (hand->stateframes >= halfDelay) {
                vrCopyWepUnload();
            }
        }

        if (hand->ejecttype == EJECTTYPE_GUN
            && (hand->ejectstate == EJECTSTATE_INIT || hand->ejectstate == EJECTSTATE_AIRBORNE)) {
            throwing = true;
        }

        if (g_Vars.currentplayer->gunctrl.throwing == true) {
            throwing = true;
        }

        if (hand->stateframes >= delay) {
            if (!throwing) {
                if (g_Vars.mplayerisrunning && (IS8MB() || PLAYERCOUNT() != 1)) {
                    bgunDeleteChrWeapon(handnum);
                }

                bgunFreeHeldRocket(handnum);

                hand->mode = HANDMODE_6;
                hand->stateminor++; // to HANDSTATEMINOR_CHANGEGUN_LOAD

            }
            else {
                bgunSetArmPitch(hand, MAX_PITCH);

                if (g_Vars.currentplayer->gunctrl.throwing == true && hand->inuse) {
                    hand->firing = true;
                    hand->attacktype = HANDATTACKTYPE_THROWPROJECTILE;
                    hand->gset.weaponfunc = FUNC_SECONDARY;
                }
            }



        }
        else {
            bgunSetArmPitch(hand, hand->stateframes * MAX_PITCH / delay);


        }

    }

    // Wait for the new gun to be loaded, then start its equip animation
    // (if any) and move on to the next state.
    if (hand->stateminor == HANDSTATEMINOR_CHANGEGUN_LOAD) {
        s32 prevmode = hand->mode;

        hand->animmode = HANDANIMMODE_IDLE;

        if (hand->pausechange == 0 || hand->pausetime60 <= hand->count60) {
            if (hand->mode == HANDMODE_6) {

                if (bgun0f09bf44(handnum)) {
                    hand->mode = HANDMODE_7;

                    if (!hand->inuse && bgunSetState(handnum, HANDSTATE_IDLE)) {
                        return lvupdate;
                    }
                }
            }

            // VR: a holster switch goes on to the raise in the call that found the gun ready
            // (vanilla waits a tick in HANDMODE_7)
            if (prevmode != HANDMODE_6 || (vrHolsterFastSwitch() && hand->mode == HANDMODE_7)) {
                if (bgunIsLoaded()) {
                    // VR: a holster switch puts the gun straight on the pose a normal switch
                    // leaves it on (vrHolsterSetEquipPose) so it can fire at once. The animation's part
                    // show/hide commands only apply while it plays (bgun0f0981e8): when idle the
                    // gunviscmds give the same parts as its end, so nothing is lost but its sounds.
                    if (vrHolsterFastSwitch()) {
                        vrHolsterSetEquipPose(info->definition->equip_animation, handnum, hand);
                    } else if (info->definition->equip_animation) {
                        bgunStartAnimation(info->definition->equip_animation, handnum, hand);
                        hand->unk0cc8_02 = true;

                    }

                    hand->mode = HANDMODE_EQUIP;
                    hand->stateminor++; // to HANDSTATEMINOR_CHANGEGUN_RAISE
                    hand->count60 = 0;
                    hand->count = 0;


                    // --- VR: resynchronize the weapon copy here ---
                    struct modeldef *currentGunModeldef = g_Vars.currentplayer->gunctrl.gunmodeldef;
                    if (g_VrCopyWepModeldef != currentGunModeldef) {
                        vrCopyWepLoad(handnum);                         // recopie le modèle courant
                        vrSwitchGun = true;
                        vrResolveReloadAnimIds(g_Vars.currentplayer->gunctrl.weaponnum);
                    }

                }
            }
        }

        if (hand->mode == HANDMODE_6 || hand->mode == HANDMODE_7) {
            bgunSetArmPitch(hand, MAX_PITCH);

        }

    }

    // Handle raising the new gun and playing the equipped sound effect.
    if (hand->stateminor == HANDSTATEMINOR_CHANGEGUN_RAISE) {
        s32 delay = TICKS(23);

        if (g_Vars.normmplayerisrunning) {
            delay = TICKS(12);
        }

        if (weaponHasFlag(hand->gset.weaponnum, WEAPONFLAG_00004000)) {
            hand->animmode = HANDANIMMODE_IDLE;
        }
        else if (weapon->equip_animation) {
            delay = 1;
        }

        // VR: a holster switch raises at once: the gun goes from its load straight to idle, never
        // drawn pitched down
        if (vrHolsterFastSwitch()) {
            delay = 0;
        }

        if (hand->count == 0) {
            if (g_Vars.mplayerisrunning && (IS8MB() || PLAYERCOUNT() != 1)) {
                bgunCreateChrWeapon(handnum);
            }


            // VR: a manual-reload gun put away by a switch comes back as it was, not refilled: each
            // magazine with a saved shape (the SuperDragon's rifle clip and grenade magazine each keep
            // their own, Task F9e). Its other ammo slots (a magazine with no saved shape, or a
            // function's ammo with no row) fill as the engine fills them
            s32 vrfilled = vrReloadDrawFill(handnum);

            if (!vrfilled) {
                bgun0f098f8c(info, hand);
            } else {
                for (s32 vrfn = 0; vrfn < 2; vrfn++) {
                    struct weaponfunc *vrfunc = weaponGetFunction(&hand->gset, vrfn);

                    if (vrfunc && vrfunc->ammoindex >= 0 && vrfunc->ammoindex < 2
                            && !(vrfilled & (1 << vrfunc->ammoindex))) {
                        bgun0f098df8(vrfn, info, hand, 0, 1);
                    }
                }
            }

            if (weaponHasFlag(info->weaponnum, WEAPONFLAG_THROWABLE)
                && (info->weaponnum != WEAPON_REMOTEMINE || handnum != HAND_LEFT)
                && !vrHolsterIsG5Detonator(info->weaponnum) // VR: no rounds by design (bgunTickIncIdle)
                && bgun0f098ca0(0, info, hand) < 0
                && bgunSetState(handnum, HANDSTATE_AUTOSWITCH)) {
                hand->stateminor = HANDSTATEMINOR_AUTOSWITCH_DELETE;
                return lvupdate;
            }

            g_Vars.currentplayer->doautoselect = false;

            if (g_Vars.lvupdate240 > 0
                && g_Vars.currentplayer->cameramode != CAMERAMODE_THIRDPERSON
                && bgunIsLoaded()
                && !g_PlayerInvincible
                && !g_Vars.currentplayer->isdead) {
#if VERSION >= VERSION_NTSC_1_0
                struct sndstate* handle1;
                f32 speed1;
                struct sndstate* handle2;
                OSPri prevpri1;
                f32 speed2;
                OSPri prevpri2;
                struct sndstate* handle3;
                f32 speed3;
                OSPri prevpri3;
#else
                struct sndstate* handle1;
								f32 speed1;
								struct sndstate* handle2;
								f32 speed2;
								struct sndstate* handle3;
								f32 speed3;
#endif

                bool vrdrawsound = true; // VR: PD-VRR, the frame the draw sound starts

                switch (info->weaponnum) {
                    case WEAPON_HORIZONSCANNER:
                        speed1 = 3.5f;

#if VERSION >= VERSION_NTSC_1_0
                        prevpri1 = osGetThreadPri(0);
                        osSetThreadPri(0, osGetThreadPri(&g_AudioManager.thread) + 1);
#endif
                        handle1 = sndStart(var80095200, SFX_EQUIP_HORIZONSCANNER, 0, -1, -1, -1, -1, -1);

                        if (handle1) {
                            audioPostEvent(handle1, AL_SNDP_PITCH_EVT, *(s32*)&speed1);
                        }

#if VERSION >= VERSION_NTSC_1_0
                        osSetThreadPri(0, prevpri1);
#endif
                        break;
                    case WEAPON_LASER:
                        sndStart(var80095200, SFX_PICKUP_LASER, 0, -1, -1, -1, -1, -1);
                        break;
                    case WEAPON_COMBATKNIFE:
                        sndStart(var80095200, SFX_PICKUP_KNIFE, 0, -1, -1, -1, -1, -1);
                        break;
                    case WEAPON_REMOTEMINE:
                        if (handnum == HAND_RIGHT) {
                            sndStart(var80095200, SFX_PICKUP_MINE, 0, -1, -1, -1, -1, -1);
                        } else {
                            vrdrawsound = false;
                        }
                        break;
                    case WEAPON_TIMEDMINE:
                    case WEAPON_PROXIMITYMINE:
                    case WEAPON_ECMMINE:
                    case WEAPON_DATAUPLINK:
                    case WEAPON_RTRACKER:
                    case WEAPON_PRESIDENTSCANNER:
                    case WEAPON_DOORDECODER:
                    case WEAPON_AUTOSURGEON:
                    case WEAPON_COMMSRIDER:
                    case WEAPON_TRACERBUG:
                    case WEAPON_TARGETAMPLIFIER:
                    case WEAPON_NIGHTVISION: // VR: the held devices take the gadget sound, not the gun cock
                    case WEAPON_XRAYSCANNER:
                    case WEAPON_IRSCANNER:
                    case WEAPON_EYESPY:
                        sndStart(var80095200, SFX_PICKUP_MINE, 0, -1, -1, -1, -1, -1);
                        break;
                    case WEAPON_TRANQUILIZER:
                    case WEAPON_PSYCHOSISGUN:
                        speed2 = 1.5f;

#if VERSION >= VERSION_NTSC_1_0
                        prevpri2 = osGetThreadPri(0);
                        osSetThreadPri(0, osGetThreadPri(&g_AudioManager.thread) + 1);
#endif

                        handle2 = sndStart(var80095200, SFX_PICKUP_GUN, 0, -1, -1, -1, -1, -1);

                        if (handle2) {
                            audioPostEvent(handle2, AL_SNDP_PITCH_EVT, *(s32*)&speed2);
                        }

#if VERSION >= VERSION_NTSC_1_0
                        osSetThreadPri(0, prevpri2);
#endif
                        break;
                    case WEAPON_REAPER:
                        speed3 = 0.85f;

#if VERSION >= VERSION_NTSC_1_0
                        prevpri3 = osGetThreadPri(0);
                        osSetThreadPri(0, osGetThreadPri(&g_AudioManager.thread) + 1);
#endif

                        handle3 = sndStart(var80095200, SFX_PICKUP_GUN, 0, -1, -1, -1, -1, -1);

                        if (handle3) {
                            audioPostEvent(handle3, AL_SNDP_PITCH_EVT, *(s32*)&speed3);
                        }

#if VERSION >= VERSION_NTSC_1_0
                        osSetThreadPri(0, prevpri3);
#endif
                        break;
                    case WEAPON_NONE:
                    case WEAPON_UNARMED:
                    case WEAPON_LAPTOPGUN:
                    case WEAPON_CROSSBOW:
                    case WEAPON_GRENADE:
                    case WEAPON_NBOMB:
                    case WEAPON_COMBATBOOST:
                    case WEAPON_CLOAKINGDEVICE:
                    case WEAPON_EXPLOSIVES:
                    case WEAPON_SKEDARBOMB:
                    case WEAPON_DISGUISE40:
                    case WEAPON_DISGUISE41:
                    case WEAPON_FLIGHTPLANS:
                    case WEAPON_RESEARCHTAPE:
                    case WEAPON_BACKUPDISK:
                    case WEAPON_KEYCARD45:
                    case WEAPON_KEYCARD46:
                    case WEAPON_KEYCARD47:
                    case WEAPON_KEYCARD48:
                    case WEAPON_KEYCARD49:
                    case WEAPON_KEYCARD4A:
                    case WEAPON_KEYCARD4B:
                    case WEAPON_KEYCARD4C:
                    case WEAPON_SUITCASE:
                    case WEAPON_BRIEFCASE:
                    case WEAPON_NECKLACE:
                    case WEAPON_BRIEFCASE2:
                        // No equip sound
                        vrdrawsound = false;
                        break;
                    default:
                        sndStart(var80095200, SFX_PICKUP_GUN, 0, -1, -1, -1, -1, -1);
                        break;
                }

                if (vrdrawsound) {
                    vrHolsterNoteDrawSound(handnum);
                }
            }
        }

        if (hand->count60 >= delay
            || !weaponGetFileNum2(info->weaponnum)
            || !weaponHasFlag(info->weaponnum, WEAPONFLAG_00000040)
            || weaponHasFlag(info->weaponnum, WEAPONFLAG_00000080)) {
            hand->mode = HANDMODE_NONE;
            hand->stateminor++; // to HANDSTATEMINOR_CHANGEGUN_EQUIP
            vrHolsterNoteRaised(handnum); // VR: the swing hold-back restarts at the raise

            if (weaponHasFlag(hand->gset.weaponnum, WEAPONFLAG_00004000) == 0) {
                hand->unk0cc8_02 = false;
            }

            hand->count60 = 0;
            hand->count = 0;
        }
        else {
            bgunSetArmPitch(hand, (delay - hand->count60) * MAX_PITCH / delay);


        }
    }

    // Wait for equip animation to finish then go to idle state
    if (hand->stateminor == HANDSTATEMINOR_CHANGEGUN_EQUIP) {
        if (info->definition->equip_animation && !weaponHasFlag(hand->gset.weaponnum, WEAPONFLAG_00004000)) {
            if (hand->animmode == HANDANIMMODE_IDLE) {
                if (bgunSetState(handnum, HANDSTATE_IDLE)) {
                    return lvupdate;
                }
            }
        }
        else {
            if (bgunSetState(handnum, HANDSTATE_IDLE)) {
                return lvupdate;
            }
        }
    }

    return 0;
}

/**
 * This function may have implemented an early beta feature where the gun could
 * be held at the side of the screen, pointed upwards. The feature was shown in
 * a demo video but doesn't exist in any public version of the game.
 */
s32 bgunTickIncState2(struct handweaponinfo* info, s32 handnum, struct hand* hand, s32 lvupdate)
{
    return 0;
}


// Controller basis in GAME-world axes (per hand), produced by vr_gun_pos_rot and consumed at
// render time by bgun0f0a5550 to world-lock the gun ORIENTATION via the live camera matrix.
struct coord gVrGunRight[2] = { {1,0,0}, {1,0,0} };
struct coord gVrGunUp[2]    = { {0,1,0}, {0,1,0} };
// The Laser is worn on the off hand, logical controller 0 (the gun hand's trigger and grips trade
// places with the off hand's, input.c, and its hands file keeps only the left arm, filemodel.c), so
// it always sat in the hand that did not hold it: with holsters, drawn by the right hand it showed
// on the left, and by the left on the right. With holsters (Task F11h, the user: "we need to see if
// we can get lasers and crossbows to swap hands as well") it is in the hand that holds it, the gun
// side, as any gun: on the gun hand's controller, its left arm mirrored onto the right hand as the
// other guns are mirrored onto the left (bgun0f0a5550), so it moves with the gun side. Holsters
// off, it is as before.
bool vrLaserOnOffHand(s32 weaponnum)
{
    return weaponnum == WEAPON_LASER && !vrHolsterActive();
}

struct coord gVrGunFwd[2]   = { {0,0,1}, {0,0,1} };
// Per-weapon grip offset, passed from vr_gun_pos_rot (tick) to bgun0f0a5550 (render) so it can be
// applied in the gun's STABLE world orientation instead of the head-relative view frame.
struct coord gVrGunOff[2]   = { {0,0,0}, {0,0,0} };

void vr_gun_pos_rot(int handnum, struct hand* hand) {

    int ctrlIndex = (handnum == HAND_RIGHT ? 1 : 0);
    if (vrLaserOnOffHand(g_Vars.currentplayer->hands[HAND_RIGHT].gset.weaponnum)) {
        ctrlIndex = (handnum == HAND_RIGHT ? 0 : 1);
    }

    // Per-weapon grip offset (delta from the tracked controller point to the gun model origin).
    //
    // X and Z are NEGATED relative to the values this mod originally shipped. Those were authored
    // against the old placement `gCtrlPos + posrotmtx*off`; the offset now lands in the gun MODEL's
    // frame (`gCtrlPos + Ry180*posrotmtx*off`, see the placement block in bgun0f0a5550), which is
    // a 180deg-Y basis change -- so every authored value needs the matching (x,y,z) -> (-x,y,-z).
    // Confirmed against a tuned Falcon2: total Z went +7.3 -> -8.4, i.e. sign flipped, magnitude kept.
    // Applying it here preserves every per-weapon relationship the original author dialled in, so a
    // single global trim serves all weapons and none of them need individual hand-tuning.
    struct coord off;
    switch (g_Vars.currentplayer->gunctrl.weaponnum) {
        case WEAPON_DRAGON:
        case WEAPON_SUPERDRAGON:
            // Y was 20 here; measured 4 units too high against the controller, so it matches the
            // 16 every other rifle-class weapon uses.
            if (!VrLeftHandedMode) {
                off.x = -4.0f; off.y = 16.0f; off.z = 4.0f;
            } else {
                off.x = 4.0f; off.y = 16.0f; off.z = 4.0f;
            }
            break;

        case WEAPON_RCP120:
        case WEAPON_AR34:
        case WEAPON_SHOTGUN:
        case WEAPON_SNIPERRIFLE:
        case WEAPON_FARSIGHT:
            if (!VrLeftHandedMode) {
                off.x = -4.0f; off.y = 16.0f; off.z = 8.0f;
            } else {
                off.x = 4.0f; off.y = 16.0f; off.z = 8.0f;
            }
            break;

        case WEAPON_CALLISTO:
        case WEAPON_ROCKETLAUNCHER:
            if (!VrLeftHandedMode) {
                off.x = -8.0f; off.y = 14.0f; off.z = -4.0f;
            } else {
                off.x = 8.0f; off.y = 14.0f; off.z = -4.0f;
            }
            break;

        case WEAPON_REAPER:
            if (!VrLeftHandedMode) {
                off.x = 8.0f;  off.y = 12.0f; off.z = -4.0f;
            } else {
                off.x = -8.0f;  off.y = 12.0f; off.z = -4.0f;
            }
            break;

        case WEAPON_DEVASTATOR:
            if (!VrLeftHandedMode) {
                off.x = -4.0f; off.y = 16.0f; off.z = 4.0f;
            } else {
                off.x = 4.0f; off.y = 16.0f; off.z = 4.0f;
            }
            break;

        case WEAPON_COMBATKNIFE:
        case WEAPON_CROSSBOW:
        case WEAPON_GRENADE:
        case WEAPON_NBOMB:
            if (!VrLeftHandedMode) {
                off.x = -2.0f; off.y = 12.0f; off.z = -12.0f;
            } else {
                off.x = 2.0f; off.y = 12.0f; off.z = -12.0f;
            }
            break;

        case WEAPON_LASER:
            // With holsters, on the gun hand (vrLaserOnOffHand false): the offset that, with the turn
            // below, puts the Laser's arm (its left arm, mirrored onto the right) where the Falcon's
            // right arm sits on the controller: the wrist on its wrist and the forearm along its
            // forearm (Task H3a, the user: "for laser the arm is not close to where the controller
            // should be"; the equip animations' last frames: tools/vr_measure/laserhand.py); mirrored
            // in left-handed mode. Task F12c's {13.1, 6.3, -8.0} with a quarter turn put the palm on
            // the Falcon's palm and left the wrist 5.1 cm off and the forearm 3.7 degrees across; this
            // leaves the palm 5.6 cm off, the Laser's hand being bent at the wrist unlike the Falcon's.
            // The fit leaves out the grip trim, which lands in the controller's frame as on the Falcon
            // (vrGunTrimUnturn below, Task H5b). Holsters off: the default, as before
            if (!vrLaserOnOffHand(WEAPON_LASER)) {
                off.x = VrLeftHandedMode ? -13.51f : 13.51f; off.y = -1.52f; off.z = -9.49f;
            } else {
                off.x = 0.0f;  off.y = 16.0f; off.z = -4.0f;
            }
            break;

        case WEAPON_FALCON2:
        case WEAPON_FALCON2_SILENCER:
        case WEAPON_FALCON2_SCOPE:
            // The Falcon 2's model sits 3.8 cm toward the gun hand's side of the CMP150's on the same
            // offset (the user: "falcon 2 model and hand appear to be more to the left than the cmp
            // and other weapons", the off controller hitting the gun hand's when reloading): on their
            // idle frames (the Falcon's equip end, the CMP150's shoot frame 0) the body's middle is at
            // model x -1.97 against the CMP150's 1.91 and the palm at -2.82 against 0.88 (the MagSec 4
            // and DY357 sit near 0, the Mauler with the CMP150). Model +x is the off hand's side; the
            // left-handed model is mirrored, so the shift is too
            off.x = VrLeftHandedMode ? -3.8f : 3.8f; off.y = 16.0f; off.z = -4.0f;
            break;

        default:
            off.x = 0.0f;  off.y = 16.0f; off.z = -4.0f;
            break;
    }

    // Z rotation correction
    vrGunTurnX = 0.0f;
    vrGunTurnY = 0.0f;

    switch (g_Vars.currentplayer->gunctrl.weaponnum) {
        case WEAPON_LASER:
            // Holsters off (vrLaserOnOffHand): -1.1 on the left controller, as before. With holsters,
            // on the gun hand: the Laser's hand is flat, palm down, so it turns about 102 degrees about
            // the barrel (z) and a little about x and y: the turn that lays its forearm along the
            // Falcon's with the palm facing in as near the Falcon's as the hand's bend allows (29
            // degrees off; Task H3a, tools/vr_measure/laserhand.py; Task F12c's quarter turn about z
            // alone left the forearm 3.7 degrees across), on the right hand with the model mirrored
            // (y and z negated below for the left hand, unmirrored)
            if (vrLaserOnOffHand(WEAPON_LASER)) {
                zRollAngle = ctrlIndex ? 0.0f : -1.1f;
            } else {
                vrGunTurnX = 0.0640f;
                vrGunTurnY = -0.0044f;
                zRollAngle = 1.7755f;
            }
            break;
        case WEAPON_CROSSBOW:
            zRollAngle = ctrlIndex ? 1.1f : -0.0f;
            break;
        case WEAPON_UNARMED:
            if (VrMotionThrowing) {
                if(!vr_button_R_grip && handnum == HAND_RIGHT){
                    zRollAngle = 1.0f;
                }
                else if(!vr_button_L_grip && handnum == HAND_LEFT) {
                    zRollAngle = -1.0f;
                }
                else{
                    zRollAngle = 0.0f;
                    off.x = 0.0f;  off.y = 16.0f; off.z = -4.0f;
                }
            }
            break;
        default:
            zRollAngle = 0.0f;
            break;
    }

    if (VrLeftHandedMode) {
        zRollAngle = -zRollAngle;
        vrGunTurnY = -vrGunTurnY; // the mirror (x negated) keeps a turn about x, negates y and z
    }


    // Per-player grip fit trim (pd-vr.ini), so the gun can be nudged onto the controller.
    off.x += VrGunOffX;
    off.y += VrGunOffY;
    off.z += VrGunOffZ;

    // The Laser's turn would turn the trim with the offset: about 20 cm off, the hand in from the
    // controller and above it on either hand, where H3a's fit (without the trim) put the wrist on the
    // Falcon's (Task H5b, the user's marks: "where the controller is and where the hand should be";
    // tools/vr_measure/laserhand.py)
    if (g_Vars.currentplayer->gunctrl.weaponnum == WEAPON_LASER && !vrLaserOnOffHand(WEAPON_LASER)) {
        vrGunTrimUnturn(handnum, &off);
    }

    // Two-handed weapons carry their own per-weapon base offsets above (e.g. AR34/Shotgun/Sniper use
    // (4,16,-8) vs the default (0,16,4)), and BOTH hands read the same one -- which is why they sit
    // off by an identical amount on each side. The single global trim can only satisfy one base, so
    // these get their own additive trim on top.
    // ORIENTATION: from the controller quat, plus the two-handed aim override for weapons that
    // support it (see vrBuildGunRotation).
    {
        // gCtrlQuat is stored [w,x,y,z]; quaternionToMtx wants the same order.
        f32 qf[4] = { gCtrlQuat[ctrlIndex][0], gCtrlQuat[ctrlIndex][1],
                      gCtrlQuat[ctrlIndex][2], gCtrlQuat[ctrlIndex][3] };
        vrBuildGunRotation(handnum, hand, qf, &hand->posrotmtx);
    }

    // Apply per-weapon Z roll correction (and the Laser's turn)
    vrGunTurnApply(&hand->posrotmtx);

    // The offset is applied properly at RENDER (bgun0f0a5550), which has the gun's world
    // orientation; here we just stash it and keep the plain view-space sum so the other tick-time
    // consumers (muzzle/recoil/laser) are unchanged.
    gVrGunOff[handnum] = off;
    hand->posoffset.x = gCtrlPos[ctrlIndex][0] + off.x;
    hand->posoffset.y = gCtrlPos[ctrlIndex][1] + off.y;
    hand->posoffset.z = gCtrlPos[ctrlIndex][2] + off.z;

    hand->posrotmtx.m[3][0] = hand->posoffset.x;
    hand->posrotmtx.m[3][1] = hand->posoffset.y;
    hand->posrotmtx.m[3][2] = hand->posoffset.z;

    // The gun's kick (the Shotgun, the revolvers), after the two-hand aim
    vrGunKickApply(handnum, hand);

    hand->useposrot = true;
    mtx4Copy(&hand->posrotmtx, &hand->posmtx);

}


s32 bgunTickInc(struct handweaponinfo* info, s32 handnum, s32 lvupdate)
{
    s32 result = 0;
    struct hand* hand = &g_Vars.currentplayer->hands[handnum];
    s32 prevstate = hand->state;


    hand->firing = false;
    hand->flashon = false;
    hand->stateframes += lvupdate;

    if (g_Vars.lvupdate240 > 0) {
        hand->count60 += g_Vars.lvupdate60;
        hand->count++;
    }

    hand->useposrot = false;

    vr_gun_pos_rot(handnum, hand); // VR

    if (handnum == HAND_RIGHT) {
        // The holsters may change the gun side: before the reload reads the controllers
        vrHolsterTick();
    }

    // Manual reload: the right gun is driven, the left gun of a pair (holsters on) only tracked
    vrReloadTick(handnum);

    switch (hand->state) {
        case HANDSTATE_IDLE:
            result = bgunTickIncIdle(info, handnum, hand, lvupdate);
            break;
        case HANDSTATE_RELOAD:
            result = bgunTickIncReload(info, handnum, hand, lvupdate);
            break;
        case HANDSTATE_ATTACK:
            result = bgunTickIncAttack(info, handnum, hand, lvupdate);
            break;
        case HANDSTATE_2:
            result = bgunTickIncState2(info, handnum, hand, lvupdate);
            break;
        case HANDSTATE_CHANGEGUN:
            result = bgunTickIncChangeGun(info, handnum, hand, lvupdate);
            break;
        case HANDSTATE_ATTACKEMPTY:
            result = bgunTickIncAttackEmpty(info, handnum, hand, lvupdate);
            break;
        case HANDSTATE_AUTOSWITCH:
            result = bgunTickIncAutoSwitch(info, handnum, hand, lvupdate);
            break;
        case HANDSTATE_CHANGEFUNC:
            result = bgunTickIncChangeFunc(info, handnum, hand, lvupdate);
            break;
    }

    hand->statelastframe = hand->stateframes;

    if (hand->state != prevstate) {
        hand->statelastframe = -result;
    }
    else {
        hand->stateframes -= result;
        hand->statecycles++;
    }

    return result;
}


bool bgunSetState(s32 handnum, s32 state)
{
    bool valid = true;
    struct hand* hand = &g_Vars.currentplayer->hands[handnum];

    // Sanity check - don't allow changing function if there is no other
    if (state == HANDSTATE_CHANGEFUNC && weaponGetFunction(&hand->gset, 1 - hand->gset.weaponfunc) == NULL) {
        valid = false;
    }

    if (valid) {
        hand->state = state;
        hand->stateframes = 0;
        hand->stateflags = 0;
        hand->statecycles = 0;
        hand->stateminor = 0;
        hand->statelastframe = 0;
    }

    return valid;
}


void bgunTickHand(s32 handnum)
{
    struct hand *hand = &g_Vars.currentplayer->hands[handnum];
    struct handweaponinfo info;
    s32 lvupdate;
    s32 i = 20;

    if (handnum); if (handnum); if (handnum); if (handnum);
#if VERSION >= VERSION_PAL_BETA
    if (handnum);
#endif

    bgunTickPendingChrWeapon(handnum);
    bgunGetWeaponInfo(&info, handnum);

    // VR: a gun parked in this tick (vrHolsterTick runs in HAND_RIGHT's tick, after
    // bgunTickGameplay set the triggers) does not fire
    if (vrHandParked(handnum)) {
        hand->triggeron = false;
        hand->triggerreleased = true;
    }

    lvupdate = g_Vars.lvupdate60;
    hand->animframeinc = g_Vars.lvupdate60;

#if VERSION >= VERSION_PAL_BETA
    hand->animframeincfreal = modelGetAbsAnimSpeed(&hand->gunmodel) * PALUPF(hand->animframeinc);
#else
    hand->animframeincfreal += PALUPF(g_Vars.lvupdate60);
#endif

    while (i >= 0) {
        s32 prevstate = hand->state;

        lvupdate = bgunTickInc(&info, handnum, lvupdate);
        i--;

        if (lvupdate <= 0) {
            // VR: a holster switch goes on into the change in the tick it starts, also in a tick
            // with no 60ths to pass on (lvupdate60 is 0 on some frames above 60 Hz)
            if (vrHolsterFastSwitch() && hand->state == HANDSTATE_CHANGEGUN
                && prevstate != HANDSTATE_CHANGEGUN) {
                continue;
            }

            break;
        }
    }
}


void bgunTickSwitch(void)
{
    bgunTickSwitch2();
}

void bgunInitHandAnims(void)
{
    struct hand* hand;
    s32 i;

    for (i = 0; i < 2; i++) {
        if (i == 0) {
            hand = &g_Vars.currentplayer->hands[1];
        }
        else {
            hand = &g_Vars.currentplayer->hands[0];
        }

        hand->gangstarot = 0;
        hand->state = HANDSTATE_IDLE;
        hand->animload = -1;
        hand->animmode = HANDANIMMODE_IDLE;

        animInit(&hand->anim);

        hand->gunmodel.anim = &hand->anim;
        hand->handmodel.anim = &hand->anim;
    }
}

f32 bgunGetNoiseRadius(s32 handnum)
{
    return g_Vars.currentplayer->hands[handnum].noiseradius;
}

void bgunDecreaseNoiseRadius(void)
{
    struct player* player = g_Vars.currentplayer;
    f32 consideramount;
    struct gset gsetleft;
    struct gset gsetright;
    struct noisesettings noisesettingsleft;
    struct noisesettings noisesettingsright;
    f32 subamount;

    gsetPopulateFromCurrentPlayer(HAND_LEFT, &gsetleft);
    gsetPopulateFromCurrentPlayer(HAND_RIGHT, &gsetright);

    gsetGetNoiseSettings(&gsetleft, &noisesettingsleft);
    gsetGetNoiseSettings(&gsetright, &noisesettingsright);

    // Right hand
    if (bgunIsFiring(HAND_RIGHT)) {
        player->hands[HAND_RIGHT].noiseradius += noisesettingsright.incradius;

        if (player->hands[HAND_RIGHT].noiseradius > noisesettingsright.maxradius) {
            player->hands[HAND_RIGHT].noiseradius = noisesettingsright.maxradius;
        }
    }

    subamount = g_Vars.lvupdate60freal * noisesettingsright.incradius / (noisesettingsright.decbasespeed * 60.0f);
    consideramount = (player->hands[HAND_RIGHT].noiseradius - noisesettingsright.minradius) * g_Vars.lvupdate60freal / (noisesettingsright.decremspeed * 60.0f);

    if (consideramount > subamount) {
        subamount = consideramount;
    }

    player->hands[HAND_RIGHT].noiseradius -= subamount;

    if (player->hands[HAND_RIGHT].noiseradius < noisesettingsright.minradius) {
        player->hands[HAND_RIGHT].noiseradius = noisesettingsright.minradius;
    }

    // Left hand
    if (bgunIsFiring(HAND_LEFT)) {
        player->hands[HAND_LEFT].noiseradius += noisesettingsleft.incradius;

        if (player->hands[HAND_LEFT].noiseradius > noisesettingsleft.maxradius) {
            player->hands[HAND_LEFT].noiseradius = noisesettingsleft.maxradius;
        }
    }

    subamount = g_Vars.lvupdate60freal * noisesettingsleft.incradius / (noisesettingsleft.decbasespeed * 60.0f);
    consideramount = (player->hands[HAND_LEFT].noiseradius - noisesettingsleft.minradius) * g_Vars.lvupdate60freal / (noisesettingsleft.decremspeed * 60.0f);

    if (consideramount > subamount) {
        subamount = consideramount;
    }

    player->hands[HAND_LEFT].noiseradius -= subamount;

    if (player->hands[HAND_LEFT].noiseradius < noisesettingsleft.minradius) {
        player->hands[HAND_LEFT].noiseradius = noisesettingsleft.minradius;
    }
}

void bgunCalculateBlend(s32 handnum)
{
    s32 sp60[2];
    s32 sp58[2];
    struct weapon* weapon = weaponFindById(bgunGetWeaponNum(handnum));
    f32 sway = weapon->sway;
    struct player* player = g_Vars.currentplayer;

    sp60[handnum] = (player->hands[handnum].curblendpos + 2) % 4;
    sp58[handnum] = (player->hands[handnum].curblendpos + 1) % 4;
    player->hands[handnum].curblendpos = sp58[handnum];

    player->hands[handnum].blendlook[sp60[handnum]].x = (RANDOMFRAC() - 0.5f) * 0.08f * sway;
    player->hands[handnum].blendlook[sp60[handnum]].y = (RANDOMFRAC() - 0.5f) * 0.1f * sway;
    player->hands[handnum].blendlook[sp60[handnum]].z = -1;

    player->hands[handnum].blendup[sp60[handnum]].x = (RANDOMFRAC() - 0.5f) * 0.1f * sway;
    player->hands[handnum].blendup[sp60[handnum]].y = 1;
    player->hands[handnum].blendup[sp60[handnum]].z = (RANDOMFRAC() - 0.5f) * 0.1f * sway;

    player->hands[handnum].blendpos[sp60[handnum]].x = (RANDOMFRAC() * 0.75f) + 1.5f;
    player->hands[handnum].blendpos[sp60[handnum]].y = (2 + RANDOMFRAC()) * player->hands[handnum].blendscale1;
    player->hands[handnum].blendpos[sp60[handnum]].z = (RANDOMFRAC() - 0.5f) * 2.5f;

    if (player->hands[handnum].sideflag < 0) {
        player->hands[handnum].blendpos[sp60[handnum]].x *= -1;

        if (player->hands[handnum].sideflag == -2) {
            player->hands[handnum].sideflag = 1;
        }
        else {
            player->hands[handnum].sideflag = -2;
        }
    }
    else {
        if (player->hands[handnum].sideflag == 2) {
            player->hands[handnum].sideflag = -1;
        }
        else {
            player->hands[handnum].sideflag = 2;
        }
    }

    player->hands[handnum].blendscale1 = -player->hands[handnum].blendscale1;
}

void bgunUpdateBlend(struct hand* hand, s32 handnum)
{
    u32 stack[3];
    s32 i;
    struct coord sp5c = { 0, 0, 0 };
    struct coord sp50 = { 0, 0, -1 };
    struct coord sp44 = { 0, 1, 0 };
    s32 pos = hand->curblendpos;
    struct player* player = g_Vars.currentplayer;

    func0f096b70(&hand->blendpos[(pos + 3) % 4], &hand->blendpos[pos], &hand->blendpos[(pos + 1) % 4], &hand->blendpos[(pos + 2) % 4], hand->dampt, &sp5c);
    func0f096b70(&hand->blendlook[(pos + 3) % 4], &hand->blendlook[pos], &hand->blendlook[(pos + 1) % 4], &hand->blendlook[(pos + 2) % 4], hand->dampt, &sp50);
    func0f096b70(&hand->blendup[(pos + 3) % 4], &hand->blendup[pos], &hand->blendup[(pos + 1) % 4], &hand->blendup[(pos + 2) % 4], hand->dampt, &sp44);

    sp5c.x *= player->gunposamplitude;
    sp5c.y *= player->gunposamplitude;
    sp5c.z *= player->gunposamplitude;

    sp5c.x += hand->adjustdamp.x;
    sp5c.y += hand->adjustdamp.y;

    sp5c.x += handGetXShift(handnum);

}

u32 var80070158 = 0x04e50764;
u32 var8007015c = 0x05360529;
u32 var80070160 = 0x0531052a;
u32 var80070164 = 0x052b052c;
u32 var80070168 = 0x052c052d;
u32 var8007016c = 0x052b052b;
u32 var80070170 = 0x052e052f;
u32 var80070174 = 0x052f0530;
u32 var80070178 = 0x05310532;
u32 var8007017c = 0x05320533;
u32 var80070180 = 0x05340535;
u32 var80070184 = 0x05360537;
u32 var80070188 = 0x05380530;
u32 var8007018c = 0x0539053a;
u32 var80070190 = 0x0532053b;
u32 var80070194 = 0x05310766;
u32 var80070198 = 0x07670768;
u32 var8007019c = 0x0769076a;
u32 var800701a0 = 0x076b076c;
u32 var800701a4 = 0x076d0000;
u32 var800701a8 = 0x0000ffff;

void bgun0f09d8dc(f32 breathing, f32 arg1, f32 arg2, f32 arg3, f32 arg4)
{
    /*                // Removed for VR
		f32 dampt[2];
		struct player* player = g_Vars.currentplayer;
		u32 stack;
		s32 i;
		f32 sp50 = arg2;
		f32 sp4c;
		u32 stack2;

		if (sp50 < 0.0f) {
				sp50 = -sp50;
		}

		if (arg1 > 0.8f) {
				player->gunposamplitude = 1.0f;
		}
		else {
				if (arg1 > 0.1f) {
						f32 tmp = 1.0f - cosf((arg1 - 0.1f) * M_BADTAU / 2.8f);
						player->gunposamplitude = 0.8f * tmp + 0.2f;
				}
				else {
						player->gunposamplitude = 0.1f;
				}
		}

		if (bmoveGetCrouchPos() != CROUCHPOS_SQUAT) {
				if (player->gunposamplitude < 0.3f * g_Vars.currentplayer->bondbreathing) {
						player->gunposamplitude = 0.3f * g_Vars.currentplayer->bondbreathing;
				}
		}

		if (player->gunposamplitude < 0.5f * sp50) {
				player->gunposamplitude = 0.5f * sp50;
		}

		for (i = 0; i < g_Vars.lvupdate240; i++) {
				player->gunampsum = (PAL ? 0.9847f : 0.9872f) * player->gunampsum + player->gunposamplitude;
		}

		player->gunposamplitude = (PAL ? 0.01529997587204f : 0.012799978256226f) * player->gunampsum;

		if (breathing < 0.016666667535901f * sp50) {
				breathing = 0.016666667535901f * sp50;
		}

		for (i = 0; i < g_Vars.lvupdate240; i++) {
				player->cyclesum = (PAL ? 0.9847f : 0.9872f) * player->cyclesum + breathing;
		}

		breathing = player->cyclesum * (PAL ? 0.01529997587204f : 0.012799978256226f);
		sp4c = breathing * g_Vars.lvupdate60freal;
		dampt[0] = player->hands[0].dampt + sp4c;

		while (dampt[0] >= 1.0f) {
				bgunCalculateBlend(HAND_RIGHT);
				dampt[0] -= 1.0f;
				player->syncoffset++;
		}

		player->synccount += g_Vars.lvupdate60freal;

		if (player->synccount > 60.0f) {
				player->synccount = 0.0f;
				player->syncchange = (RANDOMFRAC() - 0.5f) * 0.2f / 60.0f;
		}

		if (player->syncchange + sp4c > 0.0f) {
				player->gunsync += player->syncchange;
		}

		if (player->gunsync > 0.5f) {
				player->gunsync = 0.5f;
		}
		else if (player->gunsync < -0.5f) {
				player->gunsync = -0.5f;
		}
		else if (player->gunsync < 0.1f && player->gunsync > -0.1f) {
				if (player->gunsync > 0.0f) {
						player->gunsync = -0.1f;
				}
				else {
						player->gunsync = 0.1f;
				}
		}

		dampt[1] = dampt[0] + player->syncoffset + player->gunsync;

		while (dampt[1] >= 1.0f) {
				bgunCalculateBlend(HAND_LEFT);
				dampt[1] -= 1.0f;
				player->syncoffset--;
		}

		for (i = 0; i < 2; i++) {
				player->hands[i].dampt = dampt[i];
				player->hands[i].adjustdamp.x = -1.75f * arg3 + -0.8f * arg4;
				player->hands[i].adjustdamp.y = -2.0f * arg2;
		}
		*/
}

bool bgunIsLoaded(void)
{
    if (g_Vars.currentplayer->gunctrl.gunmemowner != GUNMEMOWNER_BONDGUN) {
        return false;
    }

    return g_Vars.currentplayer->gunctrl.gunmemtype == WEAPON_NONE
           || (g_Vars.currentplayer->gunctrl.gunmemnew < 0
               && g_Vars.currentplayer->gunctrl.masterloadstate == MASTERLOADSTATE_LOADED);
}

u32 bgunGetGunMemType(void)
{
    return g_Vars.currentplayer->gunctrl.gunmemtype;
}

struct modeldef* bgunGetGunModeldef(void)
{
    return g_Vars.currentplayer->gunctrl.gunmodeldef;
}

u8* bgunGetGunMem(void)
{
    return g_Vars.currentplayer->gunctrl.gunmem;
}

u32 bgunCalculateGunMemCapacity(void)
{
    if (IS4MB() && PLAYERCOUNT() == 2) {
        return g_BgunGunMemBaseSize4Mb2P;
    }

    if (PLAYERCOUNT() == 1) {
#ifdef PLATFORM_N64
        switch (g_Vars.stagenum) {
				case STAGE_CHICAGO:
				case STAGE_AIRBASE:
				case STAGE_VILLA:
				case STAGE_AIRFORCEONE:
				case STAGE_ATTACKSHIP:
						return g_BgunGunMemBaseSizeDefault + 25 * 1024;
				}
#else
        return g_BgunGunMemBaseSizeDefault + stageGetCurrent()->extragunmem;
#endif
    }

    return g_BgunGunMemBaseSizeDefault;
}

void bgunFreeGunMem(void)
{
    g_Vars.currentplayer->gunctrl.gunmemowner = GUNMEMOWNER_FREE;
}

void bgunSetGunMemWeapon(s32 weaponnum)
{
    struct player* player = g_Vars.currentplayer;

    if (player->gunctrl.gunmemowner == GUNMEMOWNER_BONDGUN) {
        player->gunctrl.masterloadstate = MASTERLOADSTATE_FLUX;
        player->gunctrl.gunloadstate = GUNLOADSTATE_FLUX;
        player->gunctrl.gunmemnew = weaponnum;
        player->gunctrl.gunlocktimer = -1;
    }
    else {
        player->gunctrl.gunmemnew = weaponnum;
    }
}

void bgunEnterFlux(void)
{
    s32 i;
    struct casing* end;
    struct casing* casing;

    g_Vars.currentplayer->gunctrl.handfilenum = 0xffff;
    g_Vars.currentplayer->gunctrl.handmodeldef = NULL;
    g_Vars.currentplayer->gunctrl.handmemloadptr = 0;
    g_Vars.currentplayer->gunctrl.handmemloadremaining = 0;
    g_Vars.currentplayer->gunctrl.masterloadstate = MASTERLOADSTATE_FLUX;
    g_Vars.currentplayer->gunctrl.gunloadstate = GUNLOADSTATE_FLUX;

    end = g_Casings + ARRAYCOUNT(g_Casings);
    casing = g_Casings;

    while (casing < end) {
        casing->modeldef = NULL;
        casing++;
    }

    g_CasingsActive = false;
}

bool bgunChangeGunMem(s32 newowner)
{
    struct player* player = g_Vars.currentplayer;

    if (player->gunctrl.gunmemowner == newowner) {
        return true;
    }

    if (player->gunctrl.gunlocktimer < 0) {
        player->gunctrl.gunlocktimer--;

        if (player->gunctrl.gunlocktimer < -2) {
            player->gunctrl.gunlocktimer = 0;
            player->gunctrl.gunmemowner = newowner;
            return true;
        }
    }
    else {
        bool unlock = false;

        switch (player->gunctrl.gunmemowner) {
            case GUNMEMOWNER_BONDGUN:
                if (player->gunctrl.gunmemtype != -1) {
                    player->gunctrl.gunmemnew = player->gunctrl.gunmemtype;
                }

                player->gunctrl.gunmemtype = -1;
                bgunEnterFlux();
                player->gunctrl.loadall = true;
                unlock = true;
                break;
            case GUNMEMOWNER_CHRBODY:
                if (g_Vars.mplayerisrunning) {
                    unlock = true;
                }

                if (!player->haschrbody) {
                    unlock = true;
                }

                if (newowner == GUNMEMOWNER_INVMENU && var8009dfc0 != 0) {
                    unlock = true;
                    playerRemoveChrBody();
                }
                break;
            case GUNMEMOWNER_3:
                unlock = true;
                break;
            case GUNMEMOWNER_CHANGING:
            case GUNMEMOWNER_FREE:
                unlock = true;
                break;
        }

        if (unlock) {
            player->gunctrl.gunlocktimer = -1;
            player->gunctrl.gunmemowner = GUNMEMOWNER_CHANGING;
        }
    }

    return false;
}

/**
 * This function loads resources for a gun change.
 *
 * The caller sets properties in the player's gunctrl struct which tell this
 * function what to load, where the gunmem is that it can use, and how much
 * gunmem there is. This function is then called a couple of times on subsequent
 * ticks, loading data incrementally to avoid a significant lag spike.
 *
 * The function keeps track of its progress in the gunloadstate property, and
 * updates the gunmem properties to reflect its usage.
 *
 * The first call loads the model definition from the ROM and decompresses it.
 * The second call loads and decompress to 3 textures.
 * This continues on further calls until all textures are loaded.
 * The final call does some one-off processing on the model's display lists.
 *
 * Although the name contains "Gun", it's used for more than just the gun model.
 * It's used for the hand model, the gun model and the cartridge model.
 */
void bgunTickGunLoad(void)
{
    s32 i;
    s32 numthistick;
    u64 remaining;
    s32 padding;
    u64 allocsize;
    u64 loadsize;
    uintptr_t ptr;
    struct player* player = g_Vars.currentplayer;
    struct modeldef* modeldef;
    struct fileinfo* fileinfo;
    struct fileinfo* gunfileinfo;
    uintptr_t newvalue;
    uintptr_t end;
    u32 stack;
#if VERSION >= VERSION_NTSC_1_0
    u32 stack2;
#endif

    if (player->gunctrl.gunloadstate == GUNLOADSTATE_MODEL) {
        osSyncPrintf("BriGun:  BriGunLoadTick process GUN_LOADSTATE_LOAD_OBJ\n");

        ptr = *player->gunctrl.loadmemptr;
        remaining = *player->gunctrl.loadmemremaining;

        // Align ptr to the next 16 byte boundary
        if (ptr % 16) {
            padding = 16 - (ptr % 16);
            ptr += padding;
            remaining -= padding;
        }

        *player->gunctrl.loadmemptr = ptr;
        *player->gunctrl.loadmemremaining = remaining;

        loadsize = ALIGN64(fileGetInflatedSize(player->gunctrl.loadfilenum, LOADTYPE_MODEL)) + 0x8000;

        osSyncPrintf("BriGun:  Loading - %s, pMem 0x%08x Size %d\n");

        if (loadsize > remaining) {
            osSyncPrintf("BriGun:  Warning: LoadSize > MemSize, clamping decomp. buffer from %d to %d (%d Bytes)\n", allocsize, remaining, remaining);
            loadsize = remaining;
        }

        // Load the model file to ptr
        g_LoadType = LOADTYPE_GUN;

        osSyncPrintf("BriGun:  obLoadto at 0x%08x, size %d\n", ptr, loadsize);

        // VR: the preprocess picks the parts to drop by the weapon being loaded. It read the global
        // weaponnum, which some of the hand code sets in passing (last to the left hand's weapon in
        // bgunTickGameplay); a holster switch loads in the tick it commits, when that is still the
        // old weapon, so fists after a gun kept both hands per hand. Set for this load only: other
        // model loads (the main menu's gun) read the global as before.
        g_VrGunLoadWeaponnum = vrHolsterFastSwitch() ? player->gunctrl.gunmemnew : -1;
        modeldef = fileLoadToAddr(player->gunctrl.loadfilenum, FILELOADMETHOD_EXTRAMEM, (u8*)ptr, loadsize);
        g_VrGunLoadWeaponnum = -1;

        // Reserve some space for textures
        allocsize = fileGetLoadedSize(player->gunctrl.loadfilenum) + 0xe00;
#ifdef PLATFORM_64BIT
        allocsize += 0xe00;
#endif

        osSyncPrintf("BriGun:  Used size %d (Ob Size %d)\n");
        osSyncPrintf("BriGun:  block len %d usedsize %d\n");
        osSyncPrintf("BriGun:  obln ram_len %d block_len %d\n");
        osSyncPrintf("BriGun:  new used size %d\n");

        fileGetLoadedSize(player->gunctrl.loadfilenum);

        fileinfo = &g_FileInfo[player->gunctrl.loadfilenum];
        fileinfo->allocsize = allocsize;
        end = ALIGN16((uintptr_t)ptr + allocsize);
        allocsize = end - ptr;
        if (1);
        remaining -= allocsize;

        osSyncPrintf("BriGun:  Texture Block at 0x%08x size %d, endp 0x%08x\n");

        texInitPool(&player->gunctrl.texpool, (u8*)end, remaining);

        // Tidy up the model
        modelPromoteTypeToPointer(modeldef);
        modelPromoteOffsetsToPointers(modeldef, 0x05000000, (uintptr_t)modeldef);

        *player->gunctrl.loadtomodeldef = modeldef;

        player->gunctrl.nexttexturetoload = 0;
        player->gunctrl.fileinfo = *fileinfo;

        osSyncPrintf("BriGun:  Set Load State: GUN_LOADSTATE_DECOMPRESS_TEXTURES\n");
        player->gunctrl.gunloadstate = GUNLOADSTATE_TEXTURES;
        return;
    }

    if (player->gunctrl.gunloadstate == GUNLOADSTATE_TEXTURES) {
        osSyncPrintf("BriGun:  BriGunLoadTick process GUN_LOADSTATE_DECOMPRESS_TEXTURES\n");

        gunfileinfo = &player->gunctrl.fileinfo;
        fileinfo = &g_FileInfo[player->gunctrl.loadfilenum];
        *fileinfo = *gunfileinfo;
        modeldef = *player->gunctrl.loadtomodeldef;

        // Load textures - up to 3 per call
        numthistick = 0;

        for (i = player->gunctrl.nexttexturetoload; i < modeldef->numtexconfigs; i++) {
            osSyncPrintf("BriGun:  at texture %d\n", i);

            if (modeldef->texconfigs[i].texturenum < NUM_TEXTURES) {
                osSyncPrintf("BriGun:  Uncompress %d of %d\n", i, modeldef->numtexconfigs);
                texLoad(&modeldef->texconfigs[i].texturenum, &player->gunctrl.texpool, true);
                modeldef->texconfigs[i].unk0b = 1;
            }

            numthistick++;

            if (numthistick == 3) {
                return;
            }

            // @bug: This should be incremented prior to the return, otherwise
            // subsequent ticks will waste time loading a texture that's already
            // been loaded.
            player->gunctrl.nexttexturetoload++;
        }

        *gunfileinfo = *fileinfo;

        osSyncPrintf("BriGun:  Set Load State: GUN_LOADSTATE_DECOMPRESS_DLS\n");
        player->gunctrl.gunloadstate = GUNLOADSTATE_DLS;
        return;
    }

    if (player->gunctrl.gunloadstate == GUNLOADSTATE_DLS) {
        osSyncPrintf("BriGun:  BriGunLoadTick process GUN_LOADSTATE_DECOMPRESS_DLS\n");

        fileinfo = &g_FileInfo[player->gunctrl.loadfilenum];
        *fileinfo = player->gunctrl.fileinfo;
        modeldef = *player->gunctrl.loadtomodeldef;

        modeldef0f1a7560(modeldef, player->gunctrl.loadfilenum, 0x05000000, modeldef, &player->gunctrl.texpool, false);

        fileGetInflatedSize(player->gunctrl.loadfilenum, LOADTYPE_MODEL);
        fileGetLoadedSize(player->gunctrl.loadfilenum);
        fileGetLoadedSize(player->gunctrl.loadfilenum);
        fileGetLoadedSize(player->gunctrl.loadfilenum);

        modelAllocateRwData(modeldef);

        osSyncPrintf("BriGun:  propgfx_decompress 0x%08x\n");
        osSyncPrintf("BriGun:  DL waste space %d from %d (Used %d, Ramlen %d, ObSize %d)\n");
        osSyncPrintf("Increase GUNSAVESIZE to %d!!!\n");

        newvalue = ALIGN64(texGetPoolLeftPos(&player->gunctrl.texpool));
        remaining = *player->gunctrl.loadmemremaining;
        remaining -= (intptr_t)(newvalue - *player->gunctrl.loadmemptr);

        *player->gunctrl.loadmemptr = newvalue;
        *player->gunctrl.loadmemremaining = remaining;

        osSyncPrintf("BriGun:  Set Load State: GUN_LOADSTATE_LOADED\n");
        player->gunctrl.gunloadstate = GUNLOADSTATE_LOADED;

#if PIRACYCHECKS
        {
						s32* ptr = (s32*)&tagsReset;
						s32* end = (s32*)&tagFindById;
						u32 checksum = 0;

						while (ptr < end) {
								checksum -= ~*ptr;
								ptr++;
						}

						if (checksum != CHECKSUM_PLACEHOLDER) {
								ptr = (s32*)&tagsReset + 3;

								if (1);
								end = &ptr[7];

								while (ptr < end) {
										*ptr |= 0xff;
										ptr++;
								}
						}
				}
#endif
    }
}

const char var7f1abcd8[] = "need a new gun loading (lock %d gunmemnew %d)\n";
const char var7f1abd08[] = "loading gun file: %d type: %d\n";
const char var7f1abd28[] = "BriGun: Process MASTER_GUN_LOADSTATE_FLUX\n";
const char var7f1abd54[] = "BriGun: Set Master State: MASTER_GUN_LOADSTATE_HANDS\n";
const char var7f1abd8c[] = "BriGun: Process MASTER_GUN_LOADSTATE_HANDS\n";
const char var7f1abdb8[] = "BriGun: Setup Hand Load\n";
const char var7f1abdd4[] = "Hand  : Using cached hands\n";
const char var7f1abdf0[] = "Hand  : Look ma no hands!\n";
const char var7f1abe0c[] = "BriGun: Set Master State: MASTER_GUN_LOADSTATE_GUN\n";
const char var7f1abe40[] = "BriGun: Process MASTER_GUN_LOADSTATE_GUN\n";
const char var7f1abe6c[] = "BriGun: Setup Gun Load\n";
const char var7f1abe84[] = "BriGun: Set Master State: MASTER_GUN_LOADSTATE_CARTS\n";
const char var7f1abebc[] = "BriGun: Process MASTER_GUN_LOADSTATE_CARTS\n";
const char var7f1abee8[] = "BriGun: Cart Loaded setting GUN_LOADSTATE_FLUX\n";
const char var7f1abf18[] = "BriGun: Cart loading - looking for carts\n";
const char var7f1abf44[] = "BriGun: Loading cart %d\n";
const char var7f1abf60[] = "BriGun: Request for cart %d ignored - cart already loaded\n";
const char var7f1abf9c[] = "BriGun: Compile Hand 0x%08x Gun 0x%0x8\n";
const char var7f1abfc4[] = "Gun   : Compiled Gun 0x%08x\n";
const char var7f1abfe4[] = "Gun   : Compiled Size %d\n";
const char var7f1ac000[] = "Hand  : Compiled Hand 0x%08x\n";
const char var7f1ac020[] = "Hand  : Compiled Size %d\n";
const char var7f1ac03c[] = "Gun   : Compile overhead %d bytes\n";
const char var7f1ac060[] = "Hand  : Hand Obj 0x%08x Gun Obj 0x%08x \n";
const char var7f1ac08c[] = "Gun   : After Comp : Base 0x%08x Free %d\n";
const char var7f1ac0b8[] = "Gun   : After Cached Setup : Base 0x%08x Free %d\n";
const char var7f1ac0ec[] = "Gun   : TotalUsed %d, Free %d\n";
const char var7f1ac10c[] = "BriGun: Set Master State: MASTER_GUN_LOADSTATE_LOADED\n";
const char var7f1ac144[] = "GunLockTimer: %d\n";

void bgunTickMasterLoad(void)
{
    s32 newweaponnum;
    struct player* player = g_Vars.currentplayer;
    bool hashands;
    u16 handfilenum;
    s32 sum;
    u16 filenum;
    s32 i;
    struct casing* casing;
    struct hand* hand;
    struct weaponfunc* func;
    struct weaponfunc* shootfunc;
    struct weapon* weapondef;
    s32 casingindex;
    struct inventory_ammo* ammodef;
    s32 value;
    s32 bodynum;
    s32 headnum;


    if ((player->gunctrl.gunmemowner == GUNMEMOWNER_BONDGUN || bgunChangeGunMem(GUNMEMOWNER_BONDGUN)) && player->gunctrl.gunmemnew >= 0) {
        if (player->gunctrl.gunlocktimer == 0) {
            newweaponnum = player->gunctrl.gunmemnew;

            playerChooseBodyAndHead(&bodynum, &headnum, NULL);

            handfilenum = g_HeadsAndBodies[bodynum].handfilenum;


            if (IS4MB()) {
                handfilenum = FILE_GCOMBATHANDSLOD;
            }

            filenum = weaponGetFileNum(newweaponnum);

#ifndef PLATFORM_N64
            // VR: the combat boost is held as the data uplink (invitem_combatboost)
            if (newweaponnum == WEAPON_COMBATBOOST) {
                filenum = FILE_GCOMMSUPLINK;
            }
#endif

            if (player->gunctrl.masterloadstate != MASTERLOADSTATE_LOADED || newweaponnum != player->gunctrl.gunmemtype) {
                if (filenum) {
                    hashands = false;

                    if (weaponHasFlag(newweaponnum, WEAPONFLAG_HASHANDS)) {
                        hashands = true;
                    }

                    if (newweaponnum == WEAPON_UNARMED) {
                        // For unarmed, the fists are implemented
                        // as weapon models rather than hand models
                        filenum = handfilenum;
                        handfilenum = 0 * (player->gunctrl.gunloadstate == 4);
                        hashands = false;
                    }

                    if (player->gunctrl.masterloadstate == MASTERLOADSTATE_FLUX) {
                        casing = g_Casings;

                        while (casing < &g_Casings[ARRAYCOUNT(g_Casings)]) {
                            if (casing->modeldef == player->gunctrl.cartmodeldef) {
                                casing->modeldef = NULL;
                            }

                            casing++;
                        }

                        g_CasingsActive = false;

                        casing = g_Casings;

                        while (casing < &g_Casings[ARRAYCOUNT(g_Casings)]) {
                            if (casing->modeldef != NULL) {
                                g_CasingsActive = true;
                            }

                            casing++;
                        }

                        player->gunctrl.cartmodeldef = NULL;
                        player->gunctrl.masterloadstate = MASTERLOADSTATE_HANDS;
                    }
                    else if (player->gunctrl.masterloadstate == MASTERLOADSTATE_HANDS) {
                        if (hashands) {
                            if (handfilenum != player->gunctrl.handfilenum) {
                                if (player->gunctrl.gunloadstate == GUNLOADSTATE_FLUX) {
                                    player->gunctrl.handmemloadptr = bgunGetGunMem();
                                    player->gunctrl.handmemloadremaining = bgunCalculateGunMemCapacity();
                                    player->gunctrl.gunloadstate = GUNLOADSTATE_MODEL;
                                    player->gunctrl.loadfilenum = handfilenum;
                                    player->gunctrl.loadtomodeldef = &player->gunctrl.handmodeldef;
                                    player->gunctrl.loadmemptr = (uintptr_t*)&player->gunctrl.handmemloadptr;
                                    player->gunctrl.loadmemremaining = (uintptr_t*)&player->gunctrl.handmemloadremaining;
                                }

                                bgunTickGunLoad();

                                if (player->gunctrl.gunloadstate == GUNLOADSTATE_LOADED) {
                                    player->gunctrl.handfilenum = handfilenum;
                                }
                                else {
                                    return;
                                }
                            }
                        }
                        else {
                            player->gunctrl.handfilenum = 0;
                            player->gunctrl.handmodeldef = NULL;
                            player->gunctrl.handmemloadptr = bgunGetGunMem();
                            player->gunctrl.handmemloadremaining = bgunCalculateGunMemCapacity();
                        }

                        player->gunctrl.masterloadstate = MASTERLOADSTATE_GUN;
                        player->gunctrl.gunloadstate = GUNLOADSTATE_FLUX;
                    }
                    else if (player->gunctrl.masterloadstate == MASTERLOADSTATE_GUN) {
                        if (player->gunctrl.gunloadstate == GUNLOADSTATE_FLUX) {
                            player->gunctrl.memloadptr = (u8*)player->gunctrl.handmemloadptr;
                            player->gunctrl.memloadremaining = player->gunctrl.handmemloadremaining;
                            player->gunctrl.gunloadstate = GUNLOADSTATE_MODEL;
                            player->gunctrl.loadfilenum = filenum;
                            player->gunctrl.loadtomodeldef = &player->gunctrl.gunmodeldef;
                            player->gunctrl.loadmemptr = (uintptr_t*)&player->gunctrl.memloadptr;
                            player->gunctrl.loadmemremaining = (uintptr_t*)&player->gunctrl.memloadremaining;
                        }

                        bgunTickGunLoad();

                        if (player->gunctrl.gunloadstate == GUNLOADSTATE_LOADED) {
                            player->gunctrl.masterloadstate = MASTERLOADSTATE_CARTS;
                            player->gunctrl.gunloadstate = GUNLOADSTATE_FLUX;
                        }
                    }
                    else if (player->gunctrl.masterloadstate == MASTERLOADSTATE_CARTS) {
                        if (player->gunctrl.gunloadstate == GUNLOADSTATE_LOADED) {
                            player->gunctrl.gunloadstate = GUNLOADSTATE_FLUX;
                        }

                        if (player->gunctrl.gunloadstate == GUNLOADSTATE_FLUX && player->gunctrl.cartmodeldef == NULL && PLAYERCOUNT() == 1) {
                            for (i = 0; i < 2; i++) {
                                hand = player->hands + i;
                                func = gsetGetWeaponFunction2(&hand->gset);
                                shootfunc = NULL;
                                weapondef = weaponFindById(player->gunctrl.weaponnum);
                                casingindex = -1;

                                if (func != NULL) {
                                    if ((func->type & 0xff) == INVENTORYFUNCTYPE_SHOOT) {
                                        shootfunc = func;
                                    }

                                    if (weapondef && shootfunc) {
                                        ammodef = func->ammoindex >= 0 ? weapondef->ammos[func->ammoindex] : NULL;

                                        if (ammodef) {
                                            casingindex = ammodef->casingeject;
                                        }
                                    }

                                    if (casingindex >= 0) {
                                        if (player->gunctrl.cartmodeldef == NULL) {
                                            filenum = g_CartFileNums[casingindex];
                                            player->gunctrl.loadfilenum = filenum;
                                            player->gunctrl.gunloadstate = GUNLOADSTATE_MODEL;
                                            player->gunctrl.loadtomodeldef = &player->gunctrl.cartmodeldef;
                                            player->gunctrl.loadmemptr = (uintptr_t*)&player->gunctrl.memloadptr;
                                            player->gunctrl.loadmemremaining = (uintptr_t*)&player->gunctrl.memloadremaining;
                                            break;
                                        }

                                        break;
                                    }
                                }
                            }
                        }

                        if (player->gunctrl.gunloadstate != GUNLOADSTATE_FLUX) {
                            bgunTickGunLoad();
                            return;
                        }

                        sum = 0;

                        for (i = 0; i < 2; i++) {
                            hand = &player->hands[i];

                            modelInit(&hand->gunmodel, player->gunctrl.gunmodeldef, hand->unk0a6c, 0);

                            if (player->gunctrl.handmodeldef != 0) {
                                modelInit(&hand->handmodel, player->gunctrl.handmodeldef, hand->handsavedata, false);
                            }

                            hand->unk0dcc = (uintptr_t*)player->gunctrl.memloadptr;

                            value = bgunCreateModelCmdList(&hand->gunmodel, player->gunctrl.gunmodeldef->rootnode, (uintptr_t*)player->gunctrl.memloadptr);

                            sum += value;
                            player->gunctrl.memloadptr += value;
                            player->gunctrl.memloadremaining -= value;

                            if (player->gunctrl.handmodeldef != 0) {
                                hand->unk0dd0 = (uintptr_t*)player->gunctrl.memloadptr;

                                value = bgunCreateModelCmdList(&hand->handmodel, player->gunctrl.handmodeldef->rootnode, (uintptr_t*)player->gunctrl.memloadptr);

                                sum += value;
                                player->gunctrl.memloadptr += value;
                                player->gunctrl.memloadremaining -= value;
                            }
                        }

                        hand = &player->hands[0];
                        hand->unk0dd4 = -1;

                        if (player->gunctrl.memloadremaining > 50 * sizeof(Mtxf)) {
                            hand->unk0dd8 = (Mtxf*)player->gunctrl.memloadptr;
                            player->gunctrl.memloadptr += 50 * sizeof(Mtxf);
                            player->gunctrl.memloadremaining -= 50 * sizeof(Mtxf);
                        }
                        else {
                            hand->unk0dd8 = NULL;
                        }

                        bgunCalculateGunMemCapacity();

                        player->gunctrl.masterloadstate = MASTERLOADSTATE_LOADED;
                        player->gunctrl.gunmemtype = newweaponnum;
                        player->gunctrl.gunmemnew = -1;

                    }
                }
#if VERSION >= VERSION_NTSC_1_0
                else {
                    player->gunctrl.masterloadstate = MASTERLOADSTATE_LOADED;
                    player->gunctrl.gunmemtype = newweaponnum;
                    player->gunctrl.gunmemnew = -1;


                }

#endif
            }
        }
        else {
            player->gunctrl.gunlocktimer--;

            if (player->gunctrl.gunlocktimer < -2) {
                player->gunctrl.gunlocktimer = 0;
            }
        }
    }

}

void bgunTickLoad(void)
{
    s32 i;

    for (i = 0; i < g_Vars.lvupdate240; i += 8) {
        bgunTickMasterLoad();
    }
}

/**
 * Load all gun data (models, hands etc) again after returning from a different
 * gunmem type. For example, after returning from a cutscene, pause menu or from
 * controlling the eyespy.
 *
 * Gun data is typically loaded over several frames when switching, but that
 * cannot happen here as the gun must be available for the next frame.
 *
 * If the previous gun mem owner cannot release its memory right now,
 * lockscreen will be set. This causes the game to repaint the previous frame.
 *
 * Return false on success, or true if the load all should be retried on the
 * next frame.
 */
bool bgunLoadAll(void)
{
    // PAL adds a check for the eyespy being used
#if VERSION >= VERSION_PAL_BETA
    if ((g_Vars.currentplayer->devicesactive & ~g_Vars.currentplayer->devicesinhibit & DEVICE_EYESPY)) {
				g_Vars.currentplayer->gunctrl.loadall = false;
				return false;
		}
#endif

    bgunEnterFlux();

    if (g_Vars.currentplayer->gunctrl.weaponnum != WEAPON_NONE) {
        g_Vars.currentplayer->gunctrl.gunmemnew = g_Vars.currentplayer->gunctrl.weaponnum;
    }
    else {
        return false;
    }

    if (g_Vars.currentplayer->gunctrl.gunmemtype != -1) {
        return false;
    }

    if (g_Vars.currentplayer->gunctrl.gunmemowner != GUNMEMOWNER_BONDGUN) {
        bgunChangeGunMem(GUNMEMOWNER_BONDGUN);

        if (g_Vars.currentplayer->gunctrl.gunmemowner != GUNMEMOWNER_BONDGUN) {
            g_Vars.lockscreen = true;
            return true;
        }
    }

    bgunEnterFlux();

    do {
        bgunTickMasterLoad();
    } while (!bgunIsLoaded());

    g_Vars.currentplayer->gunctrl.loadall = false;

    return false;
}

struct modeldef* bgunGetCartModeldef(void)
{
    return g_Vars.currentplayer->gunctrl.cartmodeldef;
}

void bgun0f09ebcc(struct defaultobj* obj, struct coord* coord, RoomNum* rooms, Mtxf* matrix1, struct coord* velocity, Mtxf* matrix2, struct prop* prop, struct coord* pos)
{
    struct prop* objprop = obj->prop;

    if (objprop) {
        propActivate(objprop);
        propEnable(objprop);
        mtx00015f04(obj->model->scale, matrix1);
        func0f06a580(obj, coord, matrix1, rooms);

        if (obj->type == OBJTYPE_WEAPON && ((struct weaponobj*)obj)->weaponnum == WEAPON_BOLT) {
            s32 beamnum = boltbeamFindByProp(objprop);

            if (beamnum == -1) {
                beamnum = boltbeamCreate(objprop);
            }

            if (beamnum != -1) {
                boltbeamSetHeadPos(beamnum, pos);
                boltbeamSetTailPos(beamnum, pos);
            }
        }

        func0f0685e4(objprop);

        if (obj->hidden & OBJHFLAG_PROJECTILE) {
            obj->projectile->flags |= PROJECTILEFLAG_AIRBORNE;
            obj->projectile->ownerprop = prop;

            projectileSetSticky(objprop);
            mtx4Copy(matrix2, (Mtxf*)&obj->projectile->mtx);

            obj->projectile->speed.x = velocity->x;
            obj->projectile->speed.y = velocity->y;
            obj->projectile->speed.z = velocity->z;
            obj->projectile->obj = obj;
            obj->projectile->unk0d8 = g_Vars.lvframenum;
        }
    }
}

void bgun0f09ed2c(struct defaultobj* obj, struct coord* newpos, Mtxf* arg2, struct coord* velocity, Mtxf* arg4)
{
    struct prop* objprop = obj->prop;
    struct coord pos;
    RoomNum rooms[8];

    if (objprop) {
        struct prop* playerprop = g_Vars.currentplayer->prop;

        pos.x = playerprop->pos.x;
        pos.y = playerprop->pos.y;
        pos.z = playerprop->pos.z;

        roomsCopy(playerprop->rooms, rooms);

        bgun0f09ebcc(obj, &pos, rooms, arg2, velocity, arg4, playerprop, newpos);

        if (obj->hidden & OBJHFLAG_PROJECTILE) {
            obj->projectile->flags |= PROJECTILEFLAG_LAUNCHING;

            obj->projectile->nextsteppos.x = newpos->x;
            obj->projectile->nextsteppos.y = newpos->y;
            obj->projectile->nextsteppos.z = newpos->z;
        }
    }
}

struct defaultobj* bgunCreateThrownProjectile2(struct chrdata* chr, struct gset* gset, struct coord* pos, RoomNum* rooms, Mtxf* arg4, struct coord* velocity)
{
    struct defaultobj* obj = NULL;
    struct weaponfunc* basefunc;
    struct weaponfunc_throw* func;
    struct weapon* weapon = weaponFindById(gset->weaponnum);
    struct weaponobj* weaponobj;
    struct autogunobj* autogun;
    Mtxf mtx;
    s32 playernum;

    if (weapon == NULL) {
        return false;
    }

    basefunc = weapon->functions[gset->weaponfunc];
    func = (struct weaponfunc_throw*)basefunc;

    if (func == NULL) {
        return false;
    }

    if (gset->weaponnum == WEAPON_COMBATKNIFE) {
        guRotateF(mtx.m, 90.0f / (RANDOMFRAC() + 12.1f),
                  arg4->m[1][0], arg4->m[1][1], arg4->m[1][2]);
    }
    else {
        mtxLoadRandomRotation(&mtx);
    }

    if (gset->weaponnum == WEAPON_LAPTOPGUN) {
        autogun = laptopDeploy(func->projectilemodelnum, gset, chr);

        if (autogun != NULL) {
            obj = &autogun->base;
        }
    }
    else {
        weaponobj = weaponCreateProjectileFromGset(func->projectilemodelnum, gset, chr);

        if (weaponobj != NULL) {
            obj = &weaponobj->base;
            // Note this timer is converted to 240 time immediately below
            weaponobj->timer240 = func->activatetime60;

            if (weaponobj->timer240 >= 2) {
                weaponobj->timer240 = TICKS(weaponobj->timer240 * 4);
            }

            if (weaponobj->weaponnum == WEAPON_GRENADE || weaponobj->weaponnum == WEAPON_NBOMB) {
                propSetDangerous(weaponobj->base.prop);
            }

            if (func->projectilemodelnum == MODEL_CHRREMOTEMINE
                || func->projectilemodelnum == MODEL_CHRTIMEDMINE
                || func->projectilemodelnum == MODEL_CHRPROXIMITYMINE
                || func->projectilemodelnum == MODEL_CHRECMMINE) {
                weaponobj->base.flags3 |= OBJFLAG3_00000008;
            }
        }
    }

    if (obj != NULL) {
        bgun0f09ebcc(obj, pos, rooms, arg4, velocity, &mtx, chr->prop, pos);
        obj->hidden &= 0x0fffffff;

        if (g_Vars.normmplayerisrunning) {
            playernum = mpPlayerGetIndex(chr);
        }
        else {
            playernum = playermgrGetPlayerNumByProp(chr->prop);
        }

        obj->hidden |= playernum << 28;

        if (obj->hidden & OBJHFLAG_PROJECTILE) {
            obj->projectile->flags |= PROJECTILEFLAG_00000002;
            obj->projectile->unk08c = 0.1f;
            obj->projectile->pickuptimer240 = TICKS(240);

            psCreate(NULL, obj->prop, SFX_THROW, -1,
                     -1, 0, 0, PSTYPE_NONE, NULL, -1, NULL, -1, -1, -1, -1);
        }
    }

    return obj;
}




/**
 * handnum supports some unusual values:
 *
 * 0 = right hand
 * 1 = left hand
 * 2 = fumbling grenade from right hand (due to nbomb)
 * 3 = fumbling grenade from left hand (actually not possible)
 */
void bgunCreateThrownProjectile(s32 handnum, struct gset* gset)
{

    Mtxf sp1f4;
    struct coord gunpos;
    struct coord gundir;
    struct prop* playerprop = g_Vars.currentplayer->prop;
    struct coord* prevpos = &g_Vars.currentplayer->bondprevpos;
    struct coord* extrapos = &g_Vars.currentplayer->bondextrapos;
    Mtxf sp190;
    struct defaultobj* obj;
    struct weaponobj* weapon;
    struct coord muzzlepos;
    struct coord spawnpos;
    RoomNum spawnrooms[8];
    bool droppinggrenade = false;
    struct hand* hand;
    struct coord aimpos;
    struct coord sp140;
    f32 frac;
    f32 radians;
    Mtxf spf8;
    Mtxf spb8;
    Mtxf sp78;
    f32 sp68[4];
    f32 sp58[4];
    f32 sp48[4];
    struct trainingdata* data;
    u32 stack;

    if (handnum >= 2) {
        droppinggrenade = true;
        handnum -= 2;
    }

    hand = g_Vars.currentplayer->hands + handnum;

    muzzlepos.x = g_Vars.currentplayer->hands[handnum].muzzlepos.x;
    muzzlepos.y = g_Vars.currentplayer->hands[handnum].muzzlepos.y;
    muzzlepos.z = g_Vars.currentplayer->hands[handnum].muzzlepos.z;

    mtx4LoadIdentity(&sp1f4);

    if (gset->weaponnum == WEAPON_COMBATKNIFE) {
        mtx4LoadZRotation(4.711639f, &sp1f4);
        mtx4LoadXRotation(3.1410925f, &sp190);
        mtx4MultMtx4InPlace(&sp190, &sp1f4);
    }

    mtx4Copy(&g_Vars.currentplayer->hands[handnum].muzzlemat, &sp190);

    guNormalize(&sp190.m[0][0], &sp190.m[0][1], &sp190.m[0][2]);
    guNormalize(&sp190.m[1][0], &sp190.m[1][1], &sp190.m[1][2]);
    guNormalize(&sp190.m[2][0], &sp190.m[2][1], &sp190.m[2][2]);

    sp190.m[3][0] = 0.0f;
    sp190.m[3][1] = 0.0f;
    sp190.m[3][2] = 0.0f;

    mtx4MultMtx4InPlace(&sp190, &sp1f4);

    playerSetPerimEnabled(playerprop, false);

    if (cdTestLos11(&playerprop->pos, playerprop->rooms, &muzzlepos, spawnrooms, CDTYPE_ALL) != CDRESULT_COLLISION) {
        spawnpos.x = muzzlepos.x;
        spawnpos.y = muzzlepos.y;
        spawnpos.z = muzzlepos.z;
    }
    else {
        spawnpos.x = playerprop->pos.x;
        spawnpos.y = playerprop->pos.y;
        spawnpos.z = playerprop->pos.z;

        roomsCopy(playerprop->rooms, spawnrooms);
    }

    playerSetPerimEnabled(playerprop, true);

    bgunCalculatePlayerShotSpread(&gunpos, &gundir, handnum, true);
    mtx4RotateVecInPlace(camGetProjectionMtxF(), &gundir);

    if (droppinggrenade) {
        // Dropping a grenade because player is in an nbomb storm (comportement identique dans les deux modes)
        velocity.x = gundir.x * 1.6666666f;
        velocity.y = gundir.y * 1.6666666f;
        velocity.z = gundir.z * 1.6666666f;
    }

    else if (VrMotionThrowing && vrMotionThrowWeapon(gset->weaponnum, gset->weaponfunc)) {
        // VR Motion Throwing is enabled: the velocity has already been calculated by vr_throw()
        // in bgunTickIncAttackingThrow, so `velocity` is left unchanged here.
        // do nothing
    }

    else if (gsetHasFunctionFlags(&hand->gset, FUNCFLAG_CALCULATETRAJECTORY)) {
        // Calculate the velocity based on the trajectory to the aimpos
        // (utilisé en mode classique pour les grenades/mines quand VrMotionThrowing == false,
        //  ou pour toute arme normale possédant ce flag)
        propFindAimingAt(handnum, false, FINDPROPCONTEXT_QUERY);
        if (hand->hasdotinfo) {
            aimpos.x = hand->dotpos.x;
            aimpos.y = hand->dotpos.y;
            aimpos.z = hand->dotpos.z;

            chrCalculateTrajectory(&spawnpos, 21.666666f, &aimpos, &sp140);

            radians = acosf(gundir.f[0] * sp140.f[0] + gundir.f[1] * sp140.f[1] + gundir.f[2] * sp140.f[2]);

            // Check within 20 degrees
            if (radians > 0.34901026f || radians < -0.34901026f) {
                mtx00016b58(&spf8, 0, 0, 0, gundir.x, gundir.y, gundir.z, 0, 1, 0);
                mtx00016b58(&spb8, 0, 0, 0, sp140.x, sp140.y, sp140.z, 0, 1, 0);

                quaternion0f097044(&spf8, sp68);
                quaternion0f097044(&spb8, sp58);
                quaternion0f0976c0(sp68, sp58);

                frac = 0.34901025891304f / radians;

                if (frac < 0.0f) {
                    frac = -frac;
                }

                quaternionSlerp(sp68, sp58, frac, sp48);
                quaternionToMtx(sp48, &sp78);

                gundir.x = -sp78.m[2][0];
                gundir.y = -sp78.m[2][1];
                gundir.z = -sp78.m[2][2];
            }
            else {
                gundir.x = sp140.x;
                gundir.y = sp140.y;
                gundir.z = sp140.z;
            }
        }

        velocity.x = gundir.x * 21.666666f;
        velocity.y = gundir.y * 21.666666f;
        velocity.z = gundir.z * 21.666666f;
    }

    else {
        // Simple velocity
        // Chemin pris par le lancer classique au bouton (VrMotionThrowing == false)
        // pour les grenades/mines si l'arme n'a pas le flag FUNCFLAG_CALCULATETRAJECTORY,
        // ainsi que pour toutes les autres armes lancées classiques.
        velocity.x = gundir.x * 16.666666f;
        velocity.y = gundir.y * 16.666666f;
        velocity.z = gundir.z * 16.666666f;

        if (gset->weaponnum == WEAPON_GRENADE || gset->weaponnum == WEAPON_NBOMB) {
            velocity.y += 1.6666666f;
        } else {
            velocity.y += 5.0f;
        }
    }

    if (gset->weaponnum == WEAPON_LAPTOPGUN) {
        bgunFreeWeapon(handnum);
    }

    // Add player movement to velocity
    if (g_Vars.lvupdate240 > 0) {
        velocity.x += (playerprop->pos.x - prevpos->x + extrapos->x) / g_Vars.lvupdate60freal;
        velocity.y += (playerprop->pos.y - prevpos->y + extrapos->y) / g_Vars.lvupdate60freal;
        velocity.z += (playerprop->pos.z - prevpos->z + extrapos->z) / g_Vars.lvupdate60freal;
    }

    obj = bgunCreateThrownProjectile2(g_Vars.currentplayer->prop->chr, gset, &spawnpos, spawnrooms, &sp1f4, &velocity);

    if (obj) {
        if (obj->type == OBJTYPE_WEAPON) {
            weapon = (struct weaponobj*)obj;

            if (gset->weaponnum == WEAPON_GRENADE && gset->weaponfunc == FUNC_PRIMARY) {
                if (weapon->timer240 < hand->primetimer60 * 4) {
                    weapon->timer240 = 0;
                }
                else {
                    weapon->timer240 -= hand->primetimer60 * 4;
                }

                weapon->gunfunc = gset->weaponfunc;
            }
            else if (gset->weaponnum == WEAPON_ECMMINE && g_Vars.stagenum == STAGE_CITRAINING) {
                data = dtGetData();

                if (data->intraining) {
                    data->obj = obj;
                }
            }
        }

        if (obj->hidden & OBJHFLAG_PROJECTILE) {
            obj->projectile->flags |= PROJECTILEFLAG_LAUNCHING;
            obj->projectile->nextsteppos.x = muzzlepos.x;
            obj->projectile->nextsteppos.y = muzzlepos.y;
            obj->projectile->nextsteppos.z = muzzlepos.z;

            if (gset->weaponnum == WEAPON_GRENADE && gset->weaponfunc == FUNC_SECONDARY) {
                obj->projectile->unk08c = 1.0f;
            }

            if (gset->weaponnum == WEAPON_COMBATKNIFE) {
                // In theory, weapon can be uninitialised here,
                // but in practice it's always set.
                weapon->base.projectile->flags |= PROJECTILEFLAG_00000002;
                weapon->base.projectile->unk08c = 0.1f;
                weapon->base.projectile->pickuptimer240 = TICKS(240);
                weapon->base.hidden |= OBJHFLAG_THROWNKNIFE;
            }
        }
    }
}

void bgunUpdateHeldRocket(s32 handnum)
{
    struct hand* hand = &g_Vars.currentplayer->hands[handnum];
    struct defaultobj* obj = &hand->rocket->base;

    if (obj) {
        struct prop* objprop = obj->prop;
        Mtxf mtx;

        if (objprop) {
            struct prop* playerprop = g_Vars.currentplayer->prop;
            struct model* model = obj->model;

            if (!hand->firedrocket) {
                mtx4Copy(&hand->posmtx, &mtx);

                mtx.m[3][0] = 0;
                mtx.m[3][1] = 0;
                mtx.m[3][2] = 0;

                mtx00015f04(obj->model->scale, &mtx);
                func0f06a580(obj, &hand->muzzlepos, &mtx, playerprop->rooms);
                propDeregisterRooms(objprop);
            }

            model->matrices = gfxAllocate(model->definition->nummatrices * sizeof(Mtxf));

            mtx4Copy(&hand->muzzlemat, &model->matrices[0]);
            modelUpdateRelationsQuick(model, model->definition->rootnode);

            objprop->flags |= PROPFLAG_ONANYSCREENTHISTICK | PROPFLAG_ONTHISSCREENTHISTICK;
            objprop->z = -model->matrices[0].m[3][2];
        }
    }
}

void bgunCreateHeldRocket(s32 handnum, struct weaponfunc_shootprojectile* func)
{
    struct hand* hand = &g_Vars.currentplayer->hands[handnum];
    struct weaponobj* obj;

    if (hand->rocket == NULL) {
#if VERSION >= VERSION_NTSC_1_0
        hand->firedrocket = false;
#endif

        obj = weaponCreateProjectileFromWeaponNum(func->projectilemodelnum, WEAPON_ROCKET, g_Vars.currentplayer->prop->chr);

        if (obj != NULL) {
            hand->rocket = obj;
            hand->firedrocket = false;

            obj->timer240 = 1;
#if VERSION >= VERSION_NTSC_1_0
            obj->base.flags |= OBJFLAG_HELDROCKET;
#endif
            obj->base.flags2 |= OBJFLAG2_THROWTHROUGH;
        }
    }
}

void bgunFreeHeldRocket(s32 handnum)
{
    struct hand* hand = &g_Vars.currentplayer->hands[handnum];

    if (hand->rocket) {
        objFreePermanently(&hand->rocket->base, true);
        hand->rocket = NULL;
    }
}



void bgunCreateFiredProjectile(s32 handnum)
{
    struct weapon* weapondef;
    struct hand* hand;
    Mtxf sp270;
    struct coord sp264;
    f32 sp260;
    f32 sp25c;
    struct coord sp250;
    Mtxf sp210;
    struct coord gunpos;
    struct coord gundir;
    struct prop* playerprop;
    struct coord* prevpos;
    struct coord* extrapos;
    struct coord spawnpos;
    struct weaponobj* weapon;
    struct weaponfunc* tmp;
    struct weaponfunc_shootprojectile* funcdef;
    struct coord aimpos;
    struct coord sp1bc;
    f32 frac;
    f32 radians;
    Mtxf sp174;
    Mtxf sp134;
    Mtxf spf4;
    f32 spe4[4];
    f32 spd4[4];
    f32 spc4[4];



    hand = g_Vars.currentplayer->hands + handnum;

    playerprop = g_Vars.currentplayer->prop;
    prevpos = &g_Vars.currentplayer->bondprevpos;
    extrapos = &g_Vars.currentplayer->bondextrapos;

    weapondef = weaponFindById(hand->gset.weaponnum);

    if (weapondef) {
        tmp = weapondef->functions[hand->gset.weaponfunc];

        if (tmp && tmp->type == INVENTORYFUNCTYPE_SHOOT_PROJECTILE) {
            funcdef = (struct weaponfunc_shootprojectile*)tmp;

            mtx4LoadIdentity(&sp270);
            bgunCalculatePlayerShotSpread(&gunpos, &gundir, handnum, true);
            mtx4RotateVecInPlace(camGetProjectionMtxF(), &gundir);

            spawnpos.x = hand->muzzlepos.x;
            spawnpos.y = hand->muzzlepos.y;
            spawnpos.z = hand->muzzlepos.z;

            if (hand->gset.weaponnum == WEAPON_SLAYER && hand->gset.weaponfunc == FUNC_SECONDARY) {
                spawnpos.x += 50.0f * gundir.x;
                spawnpos.y += 50.0f * gundir.y;
                spawnpos.z += 50.0f * gundir.z;
            }

            sp260 = funcdef->speed * 1.6666666f / 60.0f;
            sp25c = funcdef->traveldist * 1.6666666f;


            if (gsetHasFunctionFlags(&hand->gset, FUNCFLAG_CALCULATETRAJECTORY)) {
                propFindAimingAt(handnum, false, FINDPROPCONTEXT_QUERY);

                if (hand->hasdotinfo) {
                    aimpos.x = hand->dotpos.x;
                    aimpos.y = hand->dotpos.y;
                    aimpos.z = hand->dotpos.z;

                    chrCalculateTrajectory(&spawnpos, sp25c, &aimpos, &sp1bc);

                    radians = acosf(gundir.f[0] * sp1bc.f[0] + gundir.f[1] * sp1bc.f[1] + gundir.f[2] * sp1bc.f[2]);

                    if (radians > 0.17450513f || radians < -0.17450513f) {
                        mtx00016b58(&sp174, 0.0f, 0.0f, 0.0f, gundir.x, gundir.y, gundir.z, 0.0f, 1.0f, 0.0f);
                        mtx00016b58(&sp134, 0.0f, 0.0f, 0.0f, sp1bc.x, sp1bc.y, sp1bc.z, 0.0f, 1.0f, 0.0f);

                        quaternion0f097044(&sp174, spe4);
                        quaternion0f097044(&sp134, spd4);
                        quaternion0f0976c0(spe4, spd4);

                        frac = 0.17450513f / radians;

                        if (frac < 0.0f) {
                            frac = -frac;
                        }

                        quaternionSlerp(spe4, spd4, frac, spc4);
                        quaternionToMtx(spc4, &spf4);

                        gundir.x = -spf4.m[2][0];
                        gundir.y = -spf4.m[2][1];
                        gundir.z = -spf4.m[2][2];
                    }
                    else {
                        gundir.x = sp1bc.x;
                        gundir.y = sp1bc.y;
                        gundir.z = sp1bc.z;

                    }
                }
            }
            sp250.x = gundir.x * sp260;
            sp250.y = gundir.y * sp260;
            sp250.z = gundir.z * sp260;

            sp264.x = sp250.f[0] * g_Vars.lvupdate60freal + gundir.f[0] * sp25c;
            sp264.y = sp250.f[1] * g_Vars.lvupdate60freal + gundir.f[1] * sp25c;
            sp264.z = sp250.f[2] * g_Vars.lvupdate60freal + gundir.f[2] * sp25c;

            if ((funcdef->base.base.flags & FUNCFLAG_FLYBYWIRE) == 0 && g_Vars.lvupdate240 > 0) {
                sp264.x += (playerprop->pos.x - prevpos->x + extrapos->x) / g_Vars.lvupdate60freal;
                sp264.y += (playerprop->pos.y - prevpos->y + extrapos->y) / g_Vars.lvupdate60freal;
                sp264.z += (playerprop->pos.z - prevpos->z + extrapos->z) / g_Vars.lvupdate60freal;
            }


            // VR: rebuild sp210 from gundir (already in world space)
            // mtx00016b58 builds a "look-at" matrix from a direction
            mtx00016b58(&sp210, 0.0f, 0.0f, 0.0f,
                        -gundir.x, -gundir.y, -gundir.z,   // forward = -gundir (axe -Z)
                        0.0f, 1.0f, 0.0f);                  // up = Y world
            sp210.m[3][0] = 0.0f;
            sp210.m[3][1] = 0.0f;
            sp210.m[3][2] = 0.0f;

            if (hand->rocket) {
                hand->firedrocket = true;

                weapon = hand->rocket;
                weapon->base.flags2 &= ~OBJFLAG2_THROWTHROUGH;
#if VERSION >= VERSION_NTSC_1_0
                weapon->base.flags &= ~OBJFLAG_HELDROCKET;
#endif

                if (funcdef->base.base.flags & FUNCFLAG_HOMINGROCKET) {
                    weapon->weaponnum = WEAPON_HOMINGROCKET;
                }
            }
            else if (hand->gset.weaponnum == WEAPON_ROCKETLAUNCHER || hand->gset.weaponnum == WEAPON_SLAYER) {
                u32 stack;
                s32 weaponnum = WEAPON_ROCKET;

                if (funcdef->base.base.flags & FUNCFLAG_HOMINGROCKET) {
                    weaponnum = WEAPON_HOMINGROCKET;
                }

                weapon = weaponCreateProjectileFromWeaponNum(funcdef->projectilemodelnum, weaponnum, g_Vars.currentplayer->prop->chr);
            }
            else if (hand->gset.weaponnum == WEAPON_CROSSBOW) {
                weapon = weaponCreateProjectileFromWeaponNum(funcdef->projectilemodelnum, WEAPON_BOLT, g_Vars.currentplayer->prop->chr);

                if (weapon) {
                    weapon->gunfunc = hand->gset.weaponfunc;
                }
            }
            else if (hand->gset.weaponnum == WEAPON_DEVASTATOR) {
                weapon = weaponCreateProjectileFromWeaponNum(funcdef->projectilemodelnum, WEAPON_GRENADEROUND, g_Vars.currentplayer->prop->chr);

                if (weapon) {
                    weapon->gunfunc = hand->gset.weaponfunc;
                }
            }
            else if (hand->gset.weaponnum == WEAPON_SUPERDRAGON) {
                weapon = weaponCreateProjectileFromWeaponNum(funcdef->projectilemodelnum, WEAPON_GRENADEROUND, g_Vars.currentplayer->prop->chr);

                if (weapon) {
                    weapon->gunfunc = FUNC_2;
                }
            }
            else {
                weapon = weaponCreateProjectileFromGset(funcdef->projectilemodelnum, &hand->gset, g_Vars.currentplayer->prop->chr);
            }

            if (weapon) {
#if VERSION >= VERSION_NTSC_1_0
                bool failed = false;
                Mtxf sp78;
                struct coord sp6c;
                struct coord sp60;


                if (weapon->base.model && weapon->base.model->definition) {
                    weapon->timer240 = funcdef->timer60;

                    if (weapon->timer240 != -1) {
                        weapon->timer240 = TICKS(weapon->timer240 * 4);
                    }

                    weapon->base.hidden &= 0x0fffffff;
                    weapon->base.hidden |= g_Vars.currentplayernum << 28;

                    bgun0f09ed2c(&weapon->base, &spawnpos, &sp210, &sp264, &sp270);

                    if (weapon->base.hidden & OBJHFLAG_PROJECTILE) {
                        if (funcdef->base.base.flags & FUNCFLAG_PROJECTILE_LIGHTWEIGHT) {
                            weapon->base.projectile->flags |= PROJECTILEFLAG_LIGHTWEIGHT;
                        }
                        else if (funcdef->base.base.flags & FUNCFLAG_PROJECTILE_POWERED) {
                            weapon->base.projectile->flags |= PROJECTILEFLAG_POWERED;
                        }


                        weapon->base.projectile->targetprop = g_Vars.currentplayer->trackedprops[0].prop;

                        if (funcdef->scale != 1.0f) {
                            weapon->base.model->scale *= funcdef->scale;

                            mtx3ToMtx4(weapon->base.realrot, &sp78);
                            mtx00015f04(funcdef->scale, &sp78);
                            mtx4ToMtx3(&sp78, weapon->base.realrot);
                        }

                        weapon->base.projectile->powerlimit240 = TICKS(1200);
                        weapon->base.projectile->unk0a8 = weapon->base.prop->pos.y;
                        weapon->base.projectile->unk0ac = weapon->base.projectile->speed.y;
                        weapon->base.projectile->unk010 = sp250.x;
                        weapon->base.projectile->unk014 = sp250.y;
                        weapon->base.projectile->unk018 = sp250.z;
                        weapon->base.projectile->pickuptimer240 = TICKS(240);
                        weapon->base.projectile->unk08c = funcdef->reflectangle;
                        weapon->base.projectile->unk098 = funcdef->unk50 * 1.6666666f;

                        if (funcdef->soundnum > 0) {
                            psCreate(NULL, weapon->base.prop, funcdef->soundnum, -1, -1, 0, 0, PSTYPE_NONE, 0, -1.0f, 0, -1, -1.0f, -1.0f, -1.0f);
                        }

                        if (funcdef->base.base.flags & FUNCFLAG_FLYBYWIRE) {
                            playerLaunchSlayerRocket(weapon);
                        }

                        if (weapon->base.projectile->flags & PROJECTILEFLAG_LAUNCHING) {
                            projectileLaunch(&weapon->base, weapon->base.projectile, &sp6c, &sp60);
                        }
                    }
                    else {
                        failed = true;
                    }
                }
                else {
                    failed = true;
                }

                if (failed) {
                    weapon->timer240 = -1;

                    if (weapon->base.prop) {
                        propFree(weapon->base.prop);
                    }

                    if (weapon->base.model) {
                        modelmgrFreeModel(weapon->base.model);
                    }

                    weapon->base.prop = NULL;
                    weapon->base.model = NULL;
                }
#else
                // NTSC beta doesn't have any of the failure checks
								Mtxf sp78;
								struct coord sp6c;
								struct coord sp60;

								weapon->timer240 = funcdef->timer60;

								if (weapon->timer240 != -1) {
										weapon->timer240 = TICKS(weapon->timer240 * 4);
								}

								weapon->base.hidden &= 0x0fffffff;
								weapon->base.hidden |= g_Vars.currentplayernum << 28;

								bgun0f09ed2c(&weapon->base, &spawnpos, &sp210, &sp264, &sp270);

								if (weapon->base.hidden & OBJHFLAG_PROJECTILE) {
										if (funcdef->base.base.flags & FUNCFLAG_PROJECTILE_LIGHTWEIGHT) {
												weapon->base.projectile->flags |= PROJECTILEFLAG_LIGHTWEIGHT;
										}
										else if (funcdef->base.base.flags & FUNCFLAG_PROJECTILE_POWERED) {
												weapon->base.projectile->flags |= PROJECTILEFLAG_POWERED;
										}

										weapon->base.projectile->targetprop = g_Vars.currentplayer->trackedprops[0].prop;

										if (funcdef->scale != 1.0f) {
												weapon->base.model->scale *= funcdef->scale;

												mtx3ToMtx4(weapon->base.realrot, &sp78);
												mtx00015f04(funcdef->scale, &sp78);
												mtx4ToMtx3(&sp78, weapon->base.realrot);
										}

										weapon->base.projectile->powerlimit240 = TICKS(1200);
										weapon->base.projectile->unk0a8 = weapon->base.prop->pos.y;
										weapon->base.projectile->unk0ac = weapon->base.projectile->speed.y;
										weapon->base.projectile->unk010 = sp250.x;
										weapon->base.projectile->unk014 = sp250.y;
										weapon->base.projectile->unk018 = sp250.z;
										weapon->base.projectile->pickuptimer240 = TICKS(240);
										weapon->base.projectile->unk08c = funcdef->reflectangle;
										weapon->base.projectile->unk098 = funcdef->unk50 * 1.6666666f;

										if (funcdef->soundnum > 0) {
												psCreate(NULL, weapon->base.prop, funcdef->soundnum, -1, -1, 0, 0, PSTYPE_NONE, 0, -1.0f, 0, -1, -1.0f, -1.0f, -1.0f);
										}

										if (funcdef->base.base.flags & FUNCFLAG_FLYBYWIRE) {
												playerLaunchSlayerRocket(weapon);
										}

										if (weapon->base.projectile->flags & PROJECTILEFLAG_LAUNCHING) {
												projectileLaunch(&weapon->base, weapon->base.projectile, &sp6c, &sp60);
										}
								}
#endif
            }
        }
    }

}




void bgunSwivel(f32 screenx, f32 screeny, f32 crossdamp, f32 aimdamp)
{
    f32 screenwidth = camGetScreenWidth();
    f32 screenheight = camGetScreenHeight();
    struct player *player = g_Vars.currentplayer;
    struct coord aimpos;
    s32 h;
    f32 x[2];
    f32 y[2];
    bool ignore[2] = {false, false};
    s32 numframes;
    struct hand *hand;
    struct coord sp94;
    f32 sp8c[2];


    x[HAND_RIGHT] = screenx;
    x[HAND_LEFT]  = screenx;
    y[HAND_RIGHT] = screeny;
    y[HAND_LEFT]  = screeny;

    ignore[HAND_LEFT]  = !player->hands[HAND_LEFT].inuse;
    ignore[HAND_RIGHT] = !player->hands[HAND_RIGHT].inuse;

    if (!player->hands[HAND_LEFT].inuse &&
        player->hands[HAND_RIGHT].state == HANDSTATE_RELOAD &&
        player->hands[HAND_RIGHT].unk0ce8) {
        numframes = 25;
        if (player->hands[HAND_RIGHT].gset.weaponnum == WEAPON_CROSSBOW) {
            numframes = 5;
        }
        if ((s32)bgun0f09815c(&player->hands[HAND_RIGHT]) <=
            modelGetNumAnimFrames(&player->hands[HAND_RIGHT].gunmodel) - numframes) {
            x[HAND_RIGHT] = 0.0f;
            y[HAND_RIGHT] = 0.0f;
            ignore[HAND_RIGHT] = true;
        }
    }

    // Compute the raw weapon direction in screen space. VR: not before the first frame has set the
    // camera's world-to-screen matrix (it is NULL at level start): with holsters on, the level-start
    // equip no longer waits 3 ticks to free the old gun, so a hand is in use in the first tick,
    // before any frame is drawn.
    for (h = 0; h < 2; h++) {
        if (!ignore[h] && camGetWorldToScreenMtxf() != NULL) {
            hand = &player->hands[h];
            Mtxf *matrix = hand->useposrot ? &hand->posrotmtx : &hand->posmtx;

            struct coord vrdir = {0.0f, 0.0f, -1.0f};
            mtx4RotateVecInPlace(matrix, &vrdir);
            vrdir.y = -vrdir.y;

            f32 norm = sqrtf(vrdir.x * vrdir.x + vrdir.y * vrdir.y + vrdir.z * vrdir.z);
            if (norm > 0.0001f) {
                vrdir.x /= norm;
                vrdir.y /= norm;
                vrdir.z /= norm;

                vr_rotate_vector_by_quaternion(&vrdir, &vr_HMD_rot_Q);
                vr_rotate_vector_by_quaternion(&vrdir, &vr_joy_rot_Q);


                if ((old_dotpos_init[h] &&
                     hand->dotpos.x == old_dotposX[h] &&
                     hand->dotpos.y == old_dotposY[h] &&
                     hand->dotpos.z == old_dotposZ[h]) ||
                    player->hands[HAND_RIGHT].gset.weaponnum == WEAPON_REAPER){ // TODO fix reaper sight

                    sp94.x = hand->muzzlepos.x + vrdir.x * 100000.0f;
                    sp94.y = hand->muzzlepos.y - vrdir.y * 100000.0f;
                    sp94.z = hand->muzzlepos.z + vrdir.z * 100000.0f;
                    show_laser_dot[h] = false;
                } else {
                    old_dotposX[h] = hand->dotpos.x;
                    old_dotposY[h] = hand->dotpos.y;
                    old_dotposZ[h] = hand->dotpos.z;
                    old_dotpos_init[h] = true;

                    sp94.x = hand->dotpos.x;
                    sp94.y = hand->dotpos.y;
                    sp94.z = hand->dotpos.z;
                    show_laser_dot[h] = true;
                }


                mtx4TransformVecInPlace(camGetWorldToScreenMtxf(), &sp94);
                cam0f0b4d04(&sp94, sp8c);
                x[h] = sp8c[0];
                y[h] = sp8c[1];

                x[h] = (2.0f * x[h]) / viGetViewWidth()  - 1.0f;
                y[h] = (2.0f * y[h]) / viGetViewHeight() - 1.0f;
            }
        }
    }

    player->oldcrosspos[0] = player->crosspos[0];
    player->oldcrosspos[1] = player->crosspos[1];

    player->guncrossdamp = crossdamp;
    player->gunaimdamp   = aimdamp;

    if (crossdamp < 1.0f) {
        player->crosspossum[0] = x[HAND_RIGHT] / (1.0f - crossdamp);
        player->crosspossum[1] = y[HAND_RIGHT] / (1.0f - crossdamp);
    }
    if (aimdamp < 1.0f) {
        player->crosssum2[0] = x[HAND_RIGHT] / (1.0f - aimdamp);
        player->crosssum2[1] = y[HAND_RIGHT] / (1.0f - aimdamp);
    }

    // --- crosspos: raw position, without smoothing ---
    player->crosspos[0] = x[HAND_RIGHT] * screenwidth  * 0.5f + screenwidth  * 0.5f;
    player->crosspos[1] = y[HAND_RIGHT] * screenheight * 0.5f + screenheight * 0.5f;


    if      (player->crosspos[0] < 3.0f)              player->crosspos[0] = 3.0f;
    else if (player->crosspos[0] > screenwidth - 4.0f) player->crosspos[0] = screenwidth - 4.0f;
    if      (player->crosspos[1] < 3.0f)               player->crosspos[1] = 3.0f;
    else if (player->crosspos[1] > screenheight - 4.0f) player->crosspos[1] = screenheight - 4.0f;

    player->crosspos[0] += camGetScreenLeft();
    player->crosspos[1] += camGetScreenTop();

    // --- HUD crosshair ---
    for (h = 0; h < 2; h++) {
        hand = &player->hands[h];

        hand->guncrosspossum[0] *= (PAL ? 0.913f : 0.9269697f);
        hand->guncrosspossum[1] *= (PAL ? 0.913f : 0.9269697f);

        hand->crosspos[0] = screenwidth  * 0.5f;
        hand->crosspos[1] = screenheight * 0.5f;

        if      (hand->crosspos[0] < 3.0f)               hand->crosspos[0] = 3.0f;
        else if (hand->crosspos[0] > screenwidth - 4.0f)  hand->crosspos[0] = screenwidth - 4.0f;
        if      (hand->crosspos[1] < 3.0f)               hand->crosspos[1] = 3.0f;
        else if (hand->crosspos[1] > screenheight - 4.0f) hand->crosspos[1] = screenheight - 4.0f;

        hand->crosspos[0] += camGetScreenLeft();
        hand->crosspos[1] += camGetScreenTop();
    }

    if(!vr_invert_hands) {
        // --- Store the left-hand position for the HUD crosshair (sight.c only) ---
        // A parked gun (hidden while the other gun reloads) has no crosshair
        vr_LeftCrossValid = !ignore[HAND_LEFT] && player->hands[HAND_LEFT].inuse
            && !vrHandParked(HAND_LEFT);
        if (vr_LeftCrossValid) {
            f32 lx = x[HAND_LEFT] * screenwidth * 0.5f + screenwidth * 0.5f;
            f32 ly = y[HAND_LEFT] * screenheight * 0.5f + screenheight * 0.5f;
            lx = CLAMP(lx, 3.0f, screenwidth - 4.0f);
            ly = CLAMP(ly, 3.0f, screenheight - 4.0f);
            vr_LeftCrossX = lx + camGetScreenLeft();
            vr_LeftCrossY = ly + camGetScreenTop();

        }
    }else{
        vr_LeftCrossValid = !ignore[HAND_RIGHT] && player->hands[HAND_RIGHT].inuse;
        if (vr_LeftCrossValid) {
            f32 lx = x[HAND_RIGHT] * screenwidth * 0.5f + screenwidth * 0.5f;
            f32 ly = y[HAND_RIGHT] * screenheight * 0.5f + screenheight * 0.5f;
            lx = CLAMP(lx, 3.0f, screenwidth - 4.0f);
            ly = CLAMP(ly, 3.0f, screenheight - 4.0f);
            vr_LeftCrossX = lx + camGetScreenLeft();
            vr_LeftCrossY = ly + camGetScreenTop();
        }
    }

    // --- crosspos2 (bullet aim): raw position, without smoothing ---
    player->crosspos2[0] = x[HAND_RIGHT] * screenwidth  * 0.5f + screenwidth  * 0.5f;
    player->crosspos2[1] = y[HAND_RIGHT] * screenheight * 0.5f + screenheight * 0.5f;

    player->crosspos2[0] += camGetScreenLeft();
    player->crosspos2[1] += camGetScreenTop();

    cam0f0b4c3c(player->crosspos2, &aimpos, 1000);
    bgunSetAimPos(&aimpos);

}



/**
 * Swivel the gun towards the given screen coordinates, dampening the movement
 * speed as it reaches the target.
 *
 * This is used for auto aim, the CMP's follow lock-on, and general turning.
 */
void bgunSwivelWithDamp(f32 screenx, f32 screeny, f32 crossdamp)
{
    struct weapon* weapon = weaponFindById(bgunGetWeaponNum(HAND_RIGHT));
    f32 aimdamp = PAL ? weapon->aimsettings->aimdamppal : weapon->aimsettings->aimdamp;

//    if (aimdamp < crossdamp) { // Removed for VR
//        aimdamp = crossdamp;
//    }

    bgunSwivel(screenx, screeny, crossdamp, aimdamp);
}

/**
 * Swivel the gun towards the given screen coordinates without slowing the speed
 * speed as it reaches the target.
 *
 * This is used when manual aiming.
 */
void bgunSwivelWithoutDamp(f32 screenx, f32 screeny)
{
    struct weapon* weapon = weaponFindById(bgunGetWeaponNum(HAND_RIGHT));
    f32 aimdamp = PAL ? weapon->aimsettings->aimdamppal : weapon->aimsettings->aimdamp;

    bgunSwivel(screenx, screeny, PAL ? 0.935f : 0.945f, aimdamp);
}

void bgunGetCrossPos(f32* x, f32* y)
{
    struct player* player = g_Vars.currentplayer;

    *x = player->crosspos[0];
    *y = player->crosspos[1];
}

void bgun0f0a0c08(struct coord* arg0, struct coord* arg1)
{
    arg0->x = 0;
    arg0->y = 0;
    arg0->z = 0;

    cam0f0b4c3c(g_Vars.currentplayer->crosspos, arg1, 1);
}

void bgun0f0a0c44(s32 handnum, struct coord* arg1, struct coord* arg2)
{
    arg1->x = 0;
    arg1->y = 0;
    arg1->z = 0;

    cam0f0b4c3c(g_Vars.currentplayer->hands[handnum].crosspos, arg2, 1);
}

void bgunCalculatePlayerShotSpread(struct coord* gunpos2d, struct coord* gundir2d, s32 handnum, bool dorandom)
{
    f32 spread = 0;
    f32 scaledspread;
    f32 randfactor;
    struct weaponfunc* func = currentPlayerGetWeaponFunction(handnum);
    struct player* player = g_Vars.currentplayer;
    struct hand* hand = &player->hands[handnum];

    if (func != NULL && (func->type & 0xff) == INVENTORYFUNCTYPE_SHOOT) {
        struct weaponfunc_shoot* shootfunc = (struct weaponfunc_shoot*)func;
        spread = shootfunc->spread;
    }

    if (weaponHasAimFlag(bgunGetWeaponNum2(handnum), INVAIMFLAG_ACCURATESINGLESHOT)
        && player->hands[handnum].burstbullets == 1) {
        spread *= 0.25f;
    }

    if (bmoveGetCrouchPos() == CROUCHPOS_SQUAT) {
        spread *= 0.5f;
    }

    if (player->hands[HAND_LEFT].inuse) {
        spread *= 1.5f;
    }

    scaledspread = 120.0f * spread / viGetFovY();

    // --- VR: firing direction from the hand ---
    struct coord vr_dir = { 0, 0, -1 };
    mtx4RotateVecInPlace(&hand->posrotmtx, &vr_dir);
    vr_dir.y = -vr_dir.y;

    float norm = sqrtf(vr_dir.x * vr_dir.x + vr_dir.y * vr_dir.y + vr_dir.z * vr_dir.z);
    if (norm > 0.0001f) {
        vr_dir.x /= norm;
        vr_dir.y /= norm;
        vr_dir.z /= norm;
    }

    // VR: the CMP150's Follow Lock-on (vrCmpFollowLockOn) and the Farsight's Target Locator aim from
    // the muzzle at what the original turns the crosshair or the view onto, all in camera space, as VR
    // shots ignore both. The CMP150: the target its crosshair follows (autoaimTick: the first locked
    // target inside the middle three quarters of the view, 2/3 up a chr's box), at the depth of the
    // target's prop. The Farsight, in sight mode (aimed with two hands, Task J8a) with its secondary
    // selected: the target farsightChooseTarget picks (the enemy nearest the view's line, walls or
    // not), at its chest, where the original turns the view (Task J8d, the user: "farsights target
    // locator needs the cmp150 treatment")
    if (handnum == HAND_RIGHT) {
        struct coord target;
        bool vrhastarget = false;

        if (vrCmpFollowLockOn() && player->autoxaimprop != NULL && player->autoxaimprop == player->autoyaimprop) {
            f32 aimpos[2];
            struct coord ray;

            target = player->autoxaimprop->pos;
            aimpos[0] = (player->autoaimx + 1.0f) * camGetScreenWidth() * 0.5f + camGetScreenLeft();
            aimpos[1] = (player->autoaimy + 1.0f) * camGetScreenHeight() * 0.5f + camGetScreenTop();
            cam0f0b4c3c(aimpos, &ray, 1.0f);
            mtx4TransformVecInPlace(camGetWorldToScreenMtxf(), &target);

            if (target.z < 0.0f && ray.z < 0.0f) {
                f32 depth = target.z / ray.z;

                target.x = ray.x * depth;
                target.y = ray.y * depth;
                target.z = ray.z * depth;
                vrhastarget = true;
            }
        } else if (bgunGetWeaponNum(HAND_RIGHT) == WEAPON_FARSIGHT && hand->gset.weaponfunc == FUNC_SECONDARY
                && player->insightaimmode && player->autoerasertarget != NULL && player->autoeraserdist > 0
                && player->autoerasertarget->chr != NULL) {
            struct chrdata *chr = player->autoerasertarget->chr;

            target.x = player->autoerasertarget->pos.x;
            target.y = chr->ground + chr->height * 0.65f;
            target.z = player->autoerasertarget->pos.z;
            mtx4TransformVecInPlace(camGetWorldToScreenMtxf(), &target);
            vrhastarget = true;
        }

        if (vrhastarget) {
            struct coord muzzle = hand->muzzlepos;
            struct coord d;
            f32 len;

            mtx4TransformVecInPlace(camGetWorldToScreenMtxf(), &muzzle);
            d.x = target.x - muzzle.x;
            d.y = target.y - muzzle.y;
            d.z = target.z - muzzle.z;
            len = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);

            if (len > 0.0001f) {
                vr_dir.x = d.x / len;
                vr_dir.y = d.y / len;
                vr_dir.z = d.z / len;
            }
        }
    }

    // --- Apply spread to vr_dir ---
    // Build two axes perpendicular to vr_dir (right and up),
    // then perturb the direction by a random angle within that plane.
    if (dorandom && scaledspread > 0.0f) {
        // "right" axis: perpendicular to vr_dir in the horizontal plane
        struct coord right;
        struct coord world_up = { 0.0f, 1.0f, 0.0f };

        // If vr_dir is nearly vertical, use another reference vector
        if (fabsf(vr_dir.y) > 0.99f) {
            world_up.x = 1.0f;
            world_up.y = 0.0f;
            world_up.z = 0.0f;
        }

        // right = vr_dir × world_up
        right.x = vr_dir.y * world_up.z - vr_dir.z * world_up.y;
        right.y = vr_dir.z * world_up.x - vr_dir.x * world_up.z;
        right.z = vr_dir.x * world_up.y - vr_dir.y * world_up.x;

        float rlen = sqrtf(right.x*right.x + right.y*right.y + right.z*right.z);
        if (rlen > 0.0001f) { right.x /= rlen; right.y /= rlen; right.z /= rlen; }

        // up = right × vr_dir
        struct coord up;
        up.x = right.y * vr_dir.z - right.z * vr_dir.y;
        up.y = right.z * vr_dir.x - right.x * vr_dir.z;
        up.z = right.x * vr_dir.y - right.y * vr_dir.x;

        // Convert scaledspread (pixels) to angle (radians)
        // Same scale as the original: spread in pixels / screen width → angle
        float spread_angle = scaledspread / camGetScreenWidth() * viGetFovY() * (3.14159265f / 180.0f);

        if(VrWeaponRecoil && VrTwoHandsGun(g_Vars.currentplayer->gunctrl.weaponnum) && vrTwoHandGripHeld()) {
            spread_angle *= 0.5f; // spread -50%
        }

        float rx = (RANDOMFRAC() - 0.5f) * RANDOMFRAC() * spread_angle;
        float ry = (RANDOMFRAC() - 0.5f) * RANDOMFRAC() * spread_angle;


        vr_dir.x += right.x * rx + up.x * ry;
        vr_dir.y += right.y * rx + up.y * ry;
        vr_dir.z += right.z * rx + up.z * ry;

        norm = sqrtf(vr_dir.x*vr_dir.x + vr_dir.y*vr_dir.y + vr_dir.z*vr_dir.z);
        if (norm > 0.0001f) {
            vr_dir.x /= norm;
            vr_dir.y /= norm;
            vr_dir.z /= norm;
        }
    }

    *gundir2d = vr_dir;

    // --- Project the muzzle into screen space ---
    struct coord muzzlescreen;
    muzzlescreen.x = hand->muzzlepos.x;
    muzzlescreen.y = hand->muzzlepos.y;
    muzzlescreen.z = hand->muzzlepos.z;
    mtx4TransformVecInPlace(camGetWorldToScreenMtxf(), &muzzlescreen);

    gunpos2d->x = muzzlescreen.x;
    gunpos2d->y = muzzlescreen.y;
    gunpos2d->z = muzzlescreen.z;
}

void bgunCalculateBotShotSpread(struct coord* arg0, s32 weaponnum, s32 funcnum, bool arg3, s32 crouchpos, bool dual)
{
    f32 spread = 0.0f;
    f32 radius;
    struct weapon* weapondef = weaponFindById(weaponnum);
    f32 x;
    f32 y;
    Mtxf mtx;
    struct coord sp48;
    u32 stack;

    if (weapondef) {
        struct weaponfunc* funcdef = weapondef->functions[funcnum];

        if (funcdef && (funcdef->type & 0xff) == INVENTORYFUNCTYPE_SHOOT) {
            struct weaponfunc_shoot* shootfunc = (struct weaponfunc_shoot*)funcdef;
            spread = shootfunc->spread;
        }
    }

    if (arg3 && weaponHasAimFlag(weaponnum, INVAIMFLAG_ACCURATESINGLESHOT)) {
        spread *= 0.25f;
    }

    if (crouchpos == CROUCHPOS_SQUAT) {
        spread *= 0.5f;
    }

    if (dual) {
        spread *= 1.5f;
    }

    radius = 120.0f * spread / viGetFovY();
    x = (RANDOMFRAC() - 0.5f) * RANDOMFRAC() * radius;
    y = (RANDOMFRAC() - 0.5f) * RANDOMFRAC() * radius;

    sp48.x = g_Vars.currentplayer->c_scalex * x;
    sp48.y = g_Vars.currentplayer->c_scaley * y;
    sp48.z = -1.0f;

    guNormalize(&sp48.x, &sp48.y, &sp48.z);
    mtx00016b58(&mtx, 0.0f, 0.0f, 0.0f, arg0->x, arg0->y, arg0->z, 0.0f, -1.0f, 0.0f);
    mtx4RotateVec(&mtx, &sp48, arg0);
}

bool bgunGetLastShootInfo(struct coord* pos, struct coord* dir, s32 handnum)
{
    struct hand* hand = &g_Vars.currentplayer->hands[handnum];

    if (!hand->lastdirvalid) {
        return false;
    }

    pos->x = hand->lastshootpos.x;
    pos->y = hand->lastshootpos.y;
    pos->z = hand->lastshootpos.z;

    dir->x = hand->lastshootdir.x;
    dir->y = hand->lastshootdir.y;
    dir->z = hand->lastshootdir.z;

    return true;
}

void bgunSetLastShootInfo(struct coord* pos, struct coord* dir, s32 handnum)
{
    struct hand* hand = &g_Vars.currentplayer->hands[handnum];

    hand->lastdirvalid = true;

    hand->lastshootpos.x = pos->x;
    hand->lastshootpos.y = pos->y;
    hand->lastshootpos.z = pos->z;

    hand->lastshootdir.x = dir->x;
    hand->lastshootdir.y = dir->y;
    hand->lastshootdir.z = dir->z;
}

s32 bgunGetShotsToTake(s32 handnum)
{
    struct hand* hand = &g_Vars.currentplayer->hands[handnum];

    return hand->shotstotake;
}

void bgunFreeWeapon(s32 handnum)
{
    struct player* player = g_Vars.currentplayer;
    s32 i;

    if (player->hands[handnum].inuse) {
        for (i = 0; i < 2; i++) {
            if (player->gunctrl.ammotypes[i] >= 0) {
                s32 spaceinclip = player->hands[handnum].clipsizes[i] - player->hands[handnum].loadedammo[i];
                s32 index = bgunGetUnequippedReloadIndex(player->gunctrl.weaponnum);

                if (index != -1) {
#if VERSION >= VERSION_JPN_FINAL
                    player->hands[handnum].gunroundsspent[index] = (spaceinclip << 8) + 0xff;
#elif VERSION >= VERSION_PAL_BETA
                    player->hands[handnum].gunroundsspent[index] = spaceinclip * 213 + 212;
#else
                    player->hands[handnum].gunroundsspent[index] = (spaceinclip << 8) | 0xff;
#endif
                }

                if (player->hands[handnum].loadedammo[i] > 0) {
                    player->ammoheldarr[player->gunctrl.ammotypes[i]] += player->hands[handnum].loadedammo[i];
                }

                player->hands[handnum].loadedammo[i] = 0;
            }
        }
    }

    if (g_Vars.mplayerisrunning && (IS8MB() || PLAYERCOUNT() != 1)) {
        playermgrDeleteWeapon(handnum);
    }

    bgunFreeHeldRocket(handnum);
}

// VR: the rest of a holster switch, in the tick that committed it, so the frame drawn after this
// tick shows the new weapon on the hand: no frame without it, none with the old gun. The model load
// runs to the end at once (the files are in memory; bgunLoadAll does the same after a pause), then
// each hand waiting in the load goes on to the raise and to idle, which bgunTickIncChangeGun's
// holster paths do in one call. The pose is rebuilt first, as bgunTickInc does, for the new
// weapon's grip offset. A load that cannot run now (the gun memory is not the gun's) is left to
// the per-frame load and the next ticks, as before.
static void bgunFinishFastSwitch(void)
{
    struct player *player = g_Vars.currentplayer;
    struct gunctrl *ctrl = &player->gunctrl;
    s32 waiting = false;
    s32 h, i;

    // Not committed yet: a hand is still busy (firing, a reload, a throw)
    if (ctrl->switchtoweaponnum >= 0) {
        return;
    }

    for (h = 0; h < 2; h++) {
        if (player->hands[h].state == HANDSTATE_CHANGEGUN
            && player->hands[h].stateminor == HANDSTATEMINOR_CHANGEGUN_LOAD) {
            waiting = true;
        }
    }

    if (!waiting) {
        return;
    }

    if (ctrl->gunmemnew >= 0 && ctrl->gunmemowner == GUNMEMOWNER_BONDGUN && !ctrl->loadall) {
        u64 start = sysGetMicroseconds();

        // Bounded: each call is one stage (the lock wait, the hands, the gun, its textures a few
        // at a time, the cartridge)
        for (i = 0; i < 256 && !bgunIsLoaded(); i++) {
            bgunTickMasterLoad();
        }

        vrHolsterNoteLoad((s32) (sysGetMicroseconds() - start));
    }

    for (h = 0; h < 2; h++) {
        struct hand *hand = &player->hands[h];
        struct handweaponinfo info;
        s32 prevstate = hand->state;
        s32 result;

        if (hand->state != HANDSTATE_CHANGEGUN || hand->stateminor != HANDSTATEMINOR_CHANGEGUN_LOAD) {
            continue;
        }

        bgunGetWeaponInfo(&info, h);
        hand->useposrot = false;
        vr_gun_pos_rot(h, hand);
        result = bgunTickIncChangeGun(&info, h, hand, 0);

        // bgunTickInc's bookkeeping
        hand->statelastframe = hand->stateframes;

        if (hand->state != prevstate) {
            hand->statelastframe = -result;
        } else {
            hand->stateframes -= result;
            hand->statecycles++;
        }
    }
}


/**
 * Put on the disguise: it leaves the inventory and the hand goes back to the
 * previous weapon. The original does this as the switch to it completes.
 */
void bgunWearDisguise(s32 weaponnum)
{
    struct player *player = g_Vars.currentplayer;
    struct chrdata *chr = player->prop->chr;

    sndStart(var80095200, SFX_DISGUISE_ON, 0, -1, -1, -1, -1, -1);

    vrHolsterHudLine("Disguise on", 0, -1);

    player->disguised = true;

    chr->hidden |= CHRHFLAG_DISGUISED;

    if (g_Vars.stagenum == STAGE_RESCUE) {
        chr->hidden |= CHRHFLAG_UNTARGETABLE;
    }

    invRemoveItemByNum(weaponnum);

    // VR: with holsters the core's weapon follows the disguise
    if (!vrHolsterEngineSwitch()) {
        bgunCycleBack();
    }
}

void bgunTickSwitch2(void)
{
    struct player* player = g_Vars.currentplayer;
    struct gunctrl* ctrl = &g_Vars.currentplayer->gunctrl;
    s32 i;

    if (ctrl->switchtoweaponnum >= 0) {
        if (bgunCanFreeWeapon(HAND_RIGHT) && bgunCanFreeWeapon(HAND_LEFT)) {
            s32 weaponnum = player->gunctrl.weaponnum;
            s32 previnuse = player->hands[HAND_LEFT].inuse;
            struct hand* lefthand;
            struct hand* righthand;

            // VR: a device drawn from the chest pouch is held, its trigger the switch
            // (vrHolsterIsDevice); the original only toggles devices from the inventory
            if (currentPlayerGetDeviceState(ctrl->switchtoweaponnum) != DEVICESTATE_UNEQUIPPED
#ifndef PLATFORM_N64
                && !(vrHolsterActive() && vrHolsterIsDevice(ctrl->switchtoweaponnum))
#endif
                ) {
                ctrl->switchtoweaponnum = WEAPON_UNARMED;
            }

#if (VERSION == VERSION_JPN_FINAL) && defined(PLATFORM_N64)
            if (ctrl->switchtoweaponnum == WEAPON_COMBATKNIFE) {
								ctrl->switchtoweaponnum = WEAPON_UNARMED;
						}
#endif

            if (ctrl->dualwielding && !invHasDoubleWeaponIncAllGuns(ctrl->switchtoweaponnum, ctrl->switchtoweaponnum)) {
                ctrl->dualwielding = false;
            }

            func0f0d7364();

            bgunFreeWeapon(HAND_LEFT);
            bgunFreeWeapon(HAND_RIGHT);

            if (weaponnum == WEAPON_HORIZONSCANNER) {
                g_Vars.currentplayer->insightaimmode = false;
            }

            if (weaponnum == WEAPON_RCP120) {
                s32 amount = player->hands[HAND_RIGHT].matmot1;

                if (amount > player->ammoheldarr[ctrl->ammotypes[0]]) {
                    amount = player->ammoheldarr[ctrl->ammotypes[0]];
                }

                player->ammoheldarr[ctrl->ammotypes[0]] -= amount;
            }

            if (weaponnum == WEAPON_HORIZONSCANNER) {
                g_Vars.currentplayer->zoomintimemax = 0;
                g_Vars.currentplayer->zoomintime = g_Vars.currentplayer->zoomintimemax;
                g_Vars.currentplayer->zoominfovynew = 60;
                g_Vars.currentplayer->zoominfovy = g_Vars.currentplayer->zoominfovynew;
            }

            lefthand = &player->hands[HAND_LEFT];
            righthand = &player->hands[HAND_RIGHT];

            if (ctrl->switchtoweaponnum == WEAPON_NONE) {
                righthand->inuse = true;
                ctrl->weaponnum = WEAPON_NONE;
            }
            else {
                bgunSetGunMemWeapon(ctrl->switchtoweaponnum);
                ctrl->weaponnum = ctrl->switchtoweaponnum;
                lefthand->inuse = true;
                righthand->inuse = true;
            }

            // VR: a holster switch takes the knife away at once, also mid wind-up (it runs in the idle
            // state): its flags must not throw the next knife drawn
            if (weaponnum == WEAPON_COMBATKNIFE && ctrl->weaponnum != WEAPON_COMBATKNIFE
                && vrHolsterActive()) {
                bgunKnifeReset();
            }

            // VR holsters: the remote mine is a single, with no detonator in the left hand: the off
            // hand stays free (the fists draw it) and a B tap detonates (vrHolsterBTapDetonates)
            if (ctrl->weaponnum == WEAPON_REMOTEMINE && !vrHolsterActive()) {
                ctrl->dualwielding = true;
            }

            // WEAPON_UNARMED (fists): always enable both VR hands
            if (ctrl->weaponnum == WEAPON_UNARMED) // VR
                ctrl->dualwielding = true;

            if (!ctrl->dualwielding) {
                lefthand->inuse = false;
            }

            if (weaponnum <= WEAPON_PSYCHOSISGUN && weaponnum >= WEAPON_UNARMED) {
                player->gunctrl.prevweaponnum = weaponnum;
            }

            if (previnuse) {
                player->gunctrl.prevwasdualwielding = true;
            }
            else {
                player->gunctrl.prevwasdualwielding = false;
            }

            g_Vars.currentplayer->gunctrl.invertgunfunc = false;
            g_Vars.currentplayer->usedowntime = -1;

            for (i = 0; i < 2; i++) {
                player->hands[i].ejectstate = EJECTSTATE_INACTIVE;
                player->hands[i].ejecttype = EJECTTYPE_GUN;
                player->hands[i].unk0d0f_02 = false;
                player->hands[i].activatesecondary = false;

                player->hands[i].matmot1 = 0.0f;
                player->hands[i].matmot2 = 0.0f;
                player->hands[i].matmot3 = 0.0f;
                player->hands[i].angledamper = 0.0f;
                player->hands[i].gunsmokepoint = 0.0f;
                player->hands[i].burstbullets = 0;
                player->hands[i].loadslide = 0.0f;
                player->hands[i].allowshootframe = 0;
                player->hands[i].lastshootframe60 = 0;
                player->hands[i].gset.weaponfunc = FUNC_PRIMARY;
                player->hands[i].gset.weaponnum = ctrl->weaponnum;
                player->hands[i].gset.unk0639 = (ctrl->upgradewant >> (i * 4)) & 0xf;
                player->hands[i].gangstarot = 0.0f;

                bgun0f0abd30(i);

                animInit(&player->hands[i].anim);

                if (player->hands[i].audiohandle && sndGetState(player->hands[i].audiohandle) != AL_STOPPED) {
                    audioStop(player->hands[i].audiohandle);
                }
            }

            invCalculateCurrentIndex();

            ctrl->switchtoweaponnum = -1;
            ctrl->fnfader = 0;

            // VR: with holsters the disguise is held (the briefcase in the hand) and
            // the trigger puts it on (vrHolsterDeviceTrigger), not the draw
            if ((ctrl->weaponnum == WEAPON_DISGUISE40 || ctrl->weaponnum == WEAPON_DISGUISE41)
                    && !vrHolsterActive()) {
                bgunWearDisguise(ctrl->weaponnum);
            }

            ctrl->curfnstr = 0;
            ctrl->fnstrtimer = 0;
            ctrl->throwing = false;
        }

    } else {
        if (((player->hands[HAND_LEFT].inuse && !player->gunctrl.dualwielding)
             || (!player->hands[HAND_LEFT].inuse && player->gunctrl.dualwielding))
            && bgunCanFreeWeapon(HAND_LEFT)) {
            bgunFreeWeapon(HAND_LEFT);
            player->hands[HAND_LEFT].inuse = player->gunctrl.dualwielding;
        }
    }

    if (vrHolsterFastSwitch()) {
        bgunFinishFastSwitch();
    }
}

void bgunEquipWeapon(s32 weaponnum)
{
    struct player* player = g_Vars.currentplayer;

    if (player->gunctrl.weaponnum == weaponnum && player->gunctrl.switchtoweaponnum == -1) {
        return;
    }

    player->gunctrl.switchtoweaponnum = weaponnum;
    player->gunctrl.wantammo = false;

}

s32 bgunGetWeaponNum(s32 handnum)
{
    if (!g_Vars.currentplayer->hands[handnum].inuse) {
        return WEAPON_NONE;
    }

    return g_Vars.currentplayer->gunctrl.weaponnum;
}

s32 bgunGetWeaponNum2(s32 handnum)
{
    return bgunGetWeaponNum(handnum);
}

bool bgun0f0a1a10(s32 weaponnum)
{
    if (weaponHasFlag(weaponnum, WEAPONFLAG_00000400)
        && (bgunGetAmmoTypeForWeapon(weaponnum, FUNC_PRIMARY) == 0 || bgunGetAmmoQtyForWeapon(weaponnum, FUNC_PRIMARY) > 0)) {
        return true;
    }

    return false;
}

s32 bgunGetSwitchToWeapon(s32 handnum)
{
    s32 weaponnum;

//    LOGI("weaponnum %d", weaponnum);

    if (g_Vars.currentplayer->gunctrl.switchtoweaponnum >= 0) {
        weaponnum = g_Vars.currentplayer->gunctrl.switchtoweaponnum;
    } else {
        weaponnum = g_Vars.currentplayer->gunctrl.weaponnum;
    }

    // VR: Do not force WEAPON_NONE on the left hand if it is UNARMED or NONE
    if (!g_Vars.currentplayer->gunctrl.dualwielding && handnum == HAND_LEFT
        && weaponnum != WEAPON_UNARMED) {
        weaponnum = WEAPON_UNARMED;
    }


    return weaponnum;
}


void bgunSwitchToPrevious(void)
{
    if (g_Vars.tickmode != TICKMODE_CUTSCENE) {
        struct player* player = g_Vars.currentplayer;
        s32 dualweaponnum;

        // VR: with holsters the return after a throw or a deployed laptop gun is the core's
        // weapon (fists when the hands are empty)
        if (vrHolsterEngineSwitch()) {
            return;
        }

#if VERSION >= VERSION_NTSC_1_0
        if (invHasSingleWeaponIncAllGuns(player->gunctrl.prevweaponnum)) {
            bgunEquipWeapon2(HAND_RIGHT, player->gunctrl.prevweaponnum);

            dualweaponnum = invHasDoubleWeaponIncAllGuns(player->gunctrl.prevweaponnum, player->gunctrl.prevweaponnum)
                            * player->gunctrl.prevweaponnum * player->gunctrl.prevwasdualwielding;
            bgunEquipWeapon2(HAND_LEFT, dualweaponnum);
        }
        else {
            bgunAutoSwitchWeapon();
        }
#else
        bgunEquipWeapon2(HAND_RIGHT, player->gunctrl.prevweaponnum);
				bgunEquipWeapon2(HAND_LEFT, player->gunctrl.prevweaponnum * player->gunctrl.prevwasdualwielding);
#endif
    }
}

// The weapons a forward cycle moves to (right and left hand). Reads only. start >= 0: cycle from
// that weapon held single instead of the hands' current or pending weapons.
static void bgunChooseCycleForward(s32 start, s32 *ptr1, s32 *ptr2)
{
    s32 weaponnum1;
    s32 weaponnum2;
    struct player* player = g_Vars.currentplayer;

    weaponnum1 = start >= 0 ? start : bgunGetSwitchToWeapon(HAND_RIGHT);
    weaponnum2 = start >= 0 ? WEAPON_NONE : bgunGetSwitchToWeapon(HAND_LEFT);

    if (weaponnum1 > WEAPON_PSYCHOSISGUN || weaponnum2 > WEAPON_PSYCHOSISGUN) {
        weaponnum1 = player->gunctrl.prevweaponnum;
        // VR: the A-tap never selects dual wield (pairs come from
        // holsters), even when returning to a previous weapon that was
        // held dual
        weaponnum2 = WEAPON_NONE;
    }
    else {
        invChooseCycleForwardWeapon(&weaponnum1, &weaponnum2, false);
    }

    *ptr1 = weaponnum1;
    *ptr2 = weaponnum2;
}

// The weapons a back cycle moves to (right and left hand). Reads only. start: as for
// bgunChooseCycleForward.
static void bgunChooseCycleBack(s32 start, s32 *ptr1, s32 *ptr2)
{
    s32 weaponnum1;
    s32 weaponnum2;
    struct player* player = g_Vars.currentplayer;

    weaponnum1 = start >= 0 ? start : bgunGetSwitchToWeapon(HAND_RIGHT);
    weaponnum2 = start >= 0 ? WEAPON_NONE : bgunGetSwitchToWeapon(HAND_LEFT);

    if (weaponnum2 == WEAPON_REMOTEMINE) {
        weaponnum2 = WEAPON_NONE;
    }

    // VR
    if (weaponnum2 == WEAPON_UNARMED) {
        weaponnum2 = WEAPON_NONE;
    }

    if (weaponnum1 > WEAPON_PSYCHOSISGUN || weaponnum2 > WEAPON_PSYCHOSISGUN) {
        weaponnum1 = player->gunctrl.prevweaponnum;
        // VR: never select dual wield (pairs come from holsters), even
        // when returning to a previous weapon that was held dual. This
        // runs for the A-tap, when a disguise is used up and from
        // aiChrDropWeapon while holding a gadget
        weaponnum2 = WEAPON_NONE;
    }
    else {
        invChooseCycleBackWeapon(&weaponnum1, &weaponnum2, false);
    }

    *ptr1 = weaponnum1;
    *ptr2 = weaponnum2;
}

s32 bgunGetCycleWeapon(bool forward, s32 start)
{
    s32 weaponnum1;
    s32 weaponnum2;

    if (forward) {
        bgunChooseCycleForward(start, &weaponnum1, &weaponnum2);
    } else {
        bgunChooseCycleBack(start, &weaponnum1, &weaponnum2);
    }

    return weaponnum1;
}

void bgunCycleForward(void)
{
    s32 weaponnum1;
    s32 weaponnum2;
    struct player* player = g_Vars.currentplayer;

    if (g_Vars.tickmode != TICKMODE_CUTSCENE) {
        bgunChooseCycleForward(-1, &weaponnum1, &weaponnum2);

        if (weaponnum2 != weaponnum1) {
            player->gunctrl.dualwielding = false;
        }
        else {
            player->gunctrl.dualwielding = true;
        }


        bgunEquipWeapon(weaponnum1);
    }
}

void bgunCycleBack(void)
{
    s32 weaponnum1;
    s32 weaponnum2;
    struct player* player = g_Vars.currentplayer;

    if (g_Vars.tickmode != TICKMODE_CUTSCENE) {
        bgunChooseCycleBack(-1, &weaponnum1, &weaponnum2);

        if (weaponnum2 == WEAPON_NONE) {
            player->gunctrl.dualwielding = false;
        }
        else {
            player->gunctrl.dualwielding = true;
        }

        bgunEquipWeapon(weaponnum1);
    }
}

/**
 * Return true if the player has ammo for the given weapon (for either function)
 * or if the weapon doesn't support ammo.
 *
 * Used by the active menu to colour the slots.
 */
bool bgunHasAmmoForWeapon(s32 weaponnum)
{
    bool ammodefexists = false;
    bool hasammo = false;
    struct weapon* weapon = weaponFindById(weaponnum);
    s32 i;

    if (weapon == NULL) {
        return true;
    }

    for (i = 0; i < 2; i++) {
        struct weaponfunc* func = weaponGetFunctionById(weaponnum, i);

        if (func && func->ammoindex >= 0) {
            struct inventory_ammo* ammo = weapon->ammos[func->ammoindex];

            if (ammo) {
                ammodefexists = true;

                if (bgunGetAmmoCount(ammo->type) > 0) {
                    hasammo = true;
                }
            }
        }
    }

    if (!ammodefexists) {
        return true;
    }

    if (hasammo == true) {
        return true;
    }

    return false;
}

u8 g_AutoSwitchWeaponsPrimary[] = {
        WEAPON_RCP120,
        WEAPON_RCP45,
        WEAPON_SUPERDRAGON, // primary function
        WEAPON_K7AVENGER,
        WEAPON_AR34,
        WEAPON_AR53,
        WEAPON_KF7SPECIAL,
        WEAPON_CALLISTO,
        WEAPON_LAPTOPGUN,
        WEAPON_DRAGON,
        WEAPON_CMP150,
        WEAPON_CYCLONE,
        WEAPON_ZZT,
        WEAPON_DMC,
        WEAPON_KL01313,
        WEAPON_FARSIGHT,
        WEAPON_SHOTGUN,
        WEAPON_REAPER,
        WEAPON_DY357LX,
        WEAPON_MAULER,
        WEAPON_DY357MAGNUM,
        WEAPON_MAGSEC4,
        WEAPON_PHOENIX,
        WEAPON_FALCON2_SCOPE,
        WEAPON_FALCON2,
        WEAPON_FALCON2_SILENCER,
        WEAPON_PP9I,
        WEAPON_CC13,
        WEAPON_SNIPERRIFLE,
        WEAPON_CROSSBOW,
        WEAPON_TRANQUILIZER,
        WEAPON_LASER,
        WEAPON_SUPERDRAGON, // secondary function
        WEAPON_DEVASTATOR,
        WEAPON_ROCKETLAUNCHER,
        WEAPON_SLAYER,
        WEAPON_GRENADE,
        WEAPON_NBOMB,
        WEAPON_PROXIMITYMINE,
        WEAPON_TIMEDMINE,
        WEAPON_REMOTEMINE,
        WEAPON_COMBATKNIFE,
        WEAPON_UNARMED,
};

u8 g_AutoSwitchWeaponsSecondary[] = {
        WEAPON_REAPER,
        WEAPON_DY357LX,
        WEAPON_DY357MAGNUM,
        WEAPON_FALCON2_SCOPE,
        WEAPON_FALCON2,
        WEAPON_FALCON2_SILENCER,
        WEAPON_UNARMED,
};

/**
 * Automatically choose and equip a new weapon after trying to fire a weapon
 * which is out of ammo.
 *
 * The weapon preference order is stored in two arrays; one for weapons which
 * should have their primary functions considered and which require ammo, and
 * another for weapons which should have their secondary functions considered
 * and don't require ammo for those functions. The second is only used if no
 * weapons are usable from the primary array.
 *
 * For the primary array, the weapon must not be out of ammo. If the player's
 * current weapon is not in the primary array then the first available primary
 * will be selected. If the player's current weapon is in the primary array then
 * the last available weapon earlier than their current weapon will be selected.
 * If there are no weapons earlier than their current weapon then the first
 * weapon after their current weapon is selected.
 *
 * In the primary array, the SuperDragon is a special case and appears twice.
 * The first use is for the primary function while the second use is for the
 * secondary function.
 *
 * For the secondary array, the player's current weapon must not be in the
 * array. The first available weapon is selected. The player's "wantammo" flag
 * will be set which will force the weapon onto the second function.
 */
void bgunAutoSwitchWeapon(void)
{
    s32 i;
    struct weapon* weapon;
    struct weaponfunc* func;
    s32 weaponnum;
    s32 newweaponnum = -1;
    s32 firstweaponnum = -1;
    s32 foundsuperdragon = 0;
    bool foundcurrent = false;
    s32 curweaponnum = g_Vars.currentplayer->gunctrl.weaponnum;
    bool wantammo = false;

    if (g_Vars.tickmode == TICKMODE_CUTSCENE) {
        return;
    }

    // VR: with holsters the core decides: an empty gun stays in the hand, anything else goes back
    // to the core's weapon
    if (vrHolsterEngineSwitch()) {
        return;
    }

    // Loop through g_AutoSwitchWeaponsPrimary, checking which weapons the
    // player has which are usable. Stop when both a usable weapon is found
    // and when the player's current weapon is found. Note the first and last
    // usable weapons.
    i = 0;

    do {
        bool usable = false;

        if (invHasSingleWeaponIncAllGuns(g_AutoSwitchWeaponsPrimary[i])) {
            weaponnum = g_AutoSwitchWeaponsPrimary[i];
            weapon = weaponFindById(weaponnum);
            func = weaponGetFunctionById(weaponnum, FUNC_PRIMARY);

            if (!bgun0f0990b0(func, weapon) && (func->flags & FUNCFLAG_AUTOSWITCHUNSELECTABLE) == 0) {
                usable = true;
            }

            if (weaponnum == WEAPON_SUPERDRAGON && !foundsuperdragon) {
                foundsuperdragon++;
            }
            else {
                func = weaponGetFunctionById(weaponnum, FUNC_SECONDARY);

                if (!bgun0f0990b0(func, weapon) && (func->flags & FUNCFLAG_AUTOSWITCHUNSELECTABLE) == 0) {
                    usable = true;
                }
            }

            if (weaponnum == curweaponnum) {
                foundcurrent = true;
            }
            else if (usable) {
                newweaponnum = weaponnum;

                if (firstweaponnum == -1) {
                    firstweaponnum = weaponnum;
                }
            }
        }

        if (++i >= ARRAYCOUNT(g_AutoSwitchWeaponsPrimary)) {
            break;
        }
    } while (newweaponnum == -1 || !foundcurrent);

    if (!foundcurrent) {
        newweaponnum = firstweaponnum;
    }

    if (newweaponnum == -1) {
        newweaponnum = WEAPON_UNARMED;
    }

    if (newweaponnum == WEAPON_UNARMED) {
        bool foundcurrent = false;
        s32 firstweaponnum = -1;
        s32 weaponnum;

        // No usable weapon was found in the primary array,
        // so search the secondary array.
        for (i = 0; i < ARRAYCOUNT(g_AutoSwitchWeaponsSecondary); i++) {
            weaponnum = g_AutoSwitchWeaponsSecondary[i];

            if (invHasSingleWeaponIncAllGuns(weaponnum)) {
                if (weaponnum == curweaponnum) {
                    foundcurrent = true;
                }

                if (firstweaponnum == -1) {
                    firstweaponnum = weaponnum;
                }
            }
        }

        newweaponnum = firstweaponnum;

        if (newweaponnum == -1) {
            newweaponnum = WEAPON_UNARMED;
        }

        if (foundcurrent) {
            newweaponnum = -1;
        }

        wantammo = true;
    }

    // Switch to newweaponnum
    if (newweaponnum >= 0 && newweaponnum != curweaponnum) {
        if (invHasDoubleWeaponIncAllGuns(newweaponnum, newweaponnum)) {
            g_Vars.currentplayer->gunctrl.dualwielding = true;
        }
        else {
            g_Vars.currentplayer->gunctrl.dualwielding = false;
        }

        bgunEquipWeapon(newweaponnum);

        if (wantammo) {
            g_Vars.currentplayer->gunctrl.wantammo = true;
        }
    }
}

void bgunEquipWeapon2(s32 handnum, s32 weaponnum)
{
    if (handnum == HAND_LEFT) {
        if (weaponnum == WEAPON_NONE) {
            g_Vars.currentplayer->gunctrl.dualwielding = false;
        }
        else {
            g_Vars.currentplayer->gunctrl.dualwielding = true;
        }
    }
    else {
        if (weaponnum > WEAPON_SUICIDEPILL) {
            weaponnum = WEAPON_UNARMED;
        }

        bgunEquipWeapon(weaponnum);
    }
}


s32 bgunIsFiring(s32 handnum)
{
    return g_Vars.currentplayer->hands[handnum].firing;
}

s32 bgunGetAttackType(s32 handnum)
{
    return g_Vars.currentplayer->hands[handnum].attacktype;
}

char* bgunGetName(s32 weaponnum)
{
    struct weapon* weapon = g_Weapons[weaponnum];

    if (weapon) {
        return langGet(weapon->name);
    }

    return "** error\n";
}

u16 bgunGetNameId(s32 weaponnum)
{
    struct weapon* weapon = g_Weapons[weaponnum];

    if (weapon) {
        return weapon->name;
    }

    return 0;
}

char* bgunGetShortName(s32 weaponnum)
{
    struct weapon* weapon = g_Weapons[weaponnum];

    if (weapon) {
        return langGet(weapon->shortname);
    }

    return "** error\n";
}

const char var7f1ac170[] = "wantedfn %d tiggle %d\n";

void bgunReloadIfPossible(s32 handnum)
{
    struct player* player = g_Vars.currentplayer;

    if (bgunGetAmmoTypeForWeapon(bgunGetWeaponNum(handnum), FUNC_PRIMARY)
        && player->hands[handnum].modenext == HANDMODE_NONE) {
        player->hands[handnum].modenext = HANDMODE_RELOAD;
    }
}

void bgunSetAdjustPos(f32 angle)
{
    struct player* player = g_Vars.currentplayer;

    player->hands[0].adjustpos.z = (1 - cosf(angle)) * 5;
    player->hands[1].adjustpos.z = (1 - cosf(angle)) * 5;
}

void bgunStartSlide(s32 handnum)
{
    g_Vars.currentplayer->hands[handnum].slideinc = true;
}

/**
 * Update the slide on weapons which have them (eg. Falcon 2).
 *
 * The slide moves back and then forward when firing. If the gun no longer has
 * any ammo loaded in it, the slide moves back and remains in the back position.
 */
void bgunUpdateSlide(s32 handnum)
{
    f32 slidemax = 0.0f;
    struct weaponfunc* funcdef = currentPlayerGetWeaponFunction(handnum);
    struct player* player = g_Vars.currentplayer;

    // Manual reload: the off-hand is racking and vrReloadTick moves the slide with it
    if (vrReloadSlideHeld(handnum)) {
        return;
    }

    if (funcdef && ((funcdef->type & 0xff) == INVENTORYFUNCTYPE_SHOOT)) {
        struct weaponfunc_shoot* shootfunc = (struct weaponfunc_shoot*)funcdef;
        slidemax = shootfunc->slidemax;
    }

    if (player->hands[handnum].slideinc) {
        // Slide is moving backwards
        if (player->hands[handnum].slidetrans < slidemax) {
            player->hands[handnum].slidetrans += slidemax * 0.25f * g_Vars.lvupdate60freal;
        }

        if (player->hands[handnum].slidetrans >= slidemax) {
            player->hands[handnum].slidetrans = slidemax;
            player->hands[handnum].slideinc = false;
        }
    }
    // VR: the slide comes forward with no round loaded too. Locked back on an empty gun (the Falcon
    // 2s, the MagSec 4, the Phoenix, the Mauler) it looked racked, unlike every other empty gun, which
    // shows nothing and clicks on the trigger (the user: "falcon when out of ammo should not have its
    // rack racked like it is when empty, other weapons dont do that")
    else {
        if (bgun0f098a44(&player->hands[handnum], 3)) {
            // Slide is moving forwards
            if (player->hands[handnum].slidetrans > 0.0f) {
                player->hands[handnum].slidetrans -= slidemax * 0.16666667f * g_Vars.lvupdate60freal;
            }

            if (player->hands[handnum].slidetrans < 0.0f) {
                player->hands[handnum].slidetrans = 0.0f;
            }
        }
    }
}

f32 bgun0f0a2498(f32 arg0, f32 arg1, f32 arg2, f32 arg3)
{
    f32 a = arg0 - arg2;

    return asinf(a / sqrtf(a * a + (arg1 - arg3) * (arg1 - arg3)));
}

void bgun0f0a24f0(struct coord* arg0, s32 handnum)
{
    struct coord b;
    struct coord a;

    bgun0f0a0c44(handnum, &a, &b);

    b.x *= 1000;
    b.y *= 1000;
    b.z *= 1000;

    arg0->x = b.x;
    arg0->y = b.y;
    arg0->z = b.z;
}

/**
 * This function is a callback that is passed to model code.
 */
void bgun0f0a256c(s32 mtxindex, Mtxf* mtx)
{
    Mtxf sp78;
    Mtxf sp38;
    struct coord rot;

    if (mtxindex == var8009d148) {
        if (var8009d144->ejectstate == EJECTSTATE_INIT) {
            var8009d144->unk0d14 = mtx->m[3][0];
            var8009d144->unk0d18 = mtx->m[3][1];
            var8009d144->unk0d1c = mtx->m[3][2];

            var8009d144->unk0d2c[0][0] = mtx->m[0][0];
            var8009d144->unk0d2c[0][1] = mtx->m[0][1];
            var8009d144->unk0d2c[0][2] = mtx->m[0][2];
            var8009d144->unk0d2c[1][0] = mtx->m[1][0];
            var8009d144->unk0d2c[1][1] = mtx->m[1][1];
            var8009d144->unk0d2c[1][2] = mtx->m[1][2];
            var8009d144->unk0d2c[2][0] = mtx->m[2][0];
            var8009d144->unk0d2c[2][1] = mtx->m[2][1];
            var8009d144->unk0d2c[2][2] = mtx->m[2][2];
        }
        else if (var8009d144->ejectstate >= EJECTSTATE_AIRBORNE) {
            mtx->m[3][0] = var8009d144->unk0d14;
            mtx->m[3][1] = var8009d144->unk0d18;
            mtx->m[3][2] = var8009d144->unk0d1c;

            mtx->m[0][0] = var8009d144->unk0d2c[0][0];
            mtx->m[0][1] = var8009d144->unk0d2c[0][1];
            mtx->m[0][2] = var8009d144->unk0d2c[0][2];
            mtx->m[1][0] = var8009d144->unk0d2c[1][0];
            mtx->m[1][1] = var8009d144->unk0d2c[1][1];
            mtx->m[1][2] = var8009d144->unk0d2c[1][2];
            mtx->m[2][0] = var8009d144->unk0d2c[2][0];
            mtx->m[2][1] = var8009d144->unk0d2c[2][1];
            mtx->m[2][2] = var8009d144->unk0d2c[2][2];
        }
    }

    if (mtxindex == var8009d0dc) {
        rot.x = 0.0f;
        rot.y = 0.0f;
        rot.z = var8009d140;

        mtx4LoadIdentity(&sp78);
        mtx4LoadRotation(&rot, &sp78);
        mtx4MultMtx4(mtx, &sp78, &sp38);
        mtx4Copy(&sp38, mtx);
    }

    if (mtxindex == var8009d0f0[0] || mtxindex == var8009d0f0[1] || mtxindex == var8009d0f0[2]) {
        rot.x = 0.0f;
        rot.y = 0.0f;
        rot.z = 2.0f * -var8009d140;

        mtx4LoadIdentity(&sp78);
        mtx4LoadRotation(&rot, &sp78);
        mtx4MultMtx4(mtx, &sp78, &sp38);
        mtx4Copy(&sp38, mtx);
    }
}

bool bgun0f0a27c8(void)
{
    struct hand* hand;
    struct weaponfunc* func;

    hand = &g_Vars.currentplayer->hands[HAND_RIGHT];
    func = gsetGetWeaponFunction2(&hand->gset);

    if (func
        && (func->type & 0xff) == INVENTORYFUNCTYPE_MELEE
        && hand->state == HANDSTATE_ATTACK
        && hand->unk0ce8 != NULL
        && hand->animmode == HANDANIMMODE_BUSY
        && !bgun0f098a44(hand, 2)) {
        return true;
    }

    hand = &g_Vars.currentplayer->hands[HAND_LEFT];

    if (hand->inuse) {
        func = gsetGetWeaponFunction2(&hand->gset);

        if (func
            && (func->type & 0xff) == INVENTORYFUNCTYPE_MELEE
            && hand->state == HANDSTATE_ATTACK
            && hand->unk0ce8 != NULL
            && hand->animmode == HANDANIMMODE_BUSY
            && !bgun0f098a44(hand, 2)) {
            return true;
        }
    }

    return false;
}

/**
 * This function is the same as above but it doesn't call bgun0f098a44().
 *
 * This function is unused.
 */
bool bgun0f0a28d8(void)
{
    struct hand* hand;
    struct weaponfunc* func;

    hand = &g_Vars.currentplayer->hands[HAND_RIGHT];
    func = gsetGetWeaponFunction2(&hand->gset);

    if (func
        && (func->type & 0xff) == INVENTORYFUNCTYPE_MELEE
        && hand->state == HANDSTATE_ATTACK
        && hand->unk0ce8 != NULL
        && hand->animmode == HANDANIMMODE_BUSY) {
        return true;
    }

    hand = &g_Vars.currentplayer->hands[HAND_LEFT];

    if (hand->inuse) {
        func = gsetGetWeaponFunction2(&hand->gset);

        if (func
            && (func->type & 0xff) == INVENTORYFUNCTYPE_MELEE
            && hand->state == HANDSTATE_ATTACK
            && hand->unk0ce8 != NULL
            && hand->animmode == HANDANIMMODE_BUSY) {
            return true;
        }
    }

    return false;
}

void bgunHandlePlayerDead(void)
{
    struct player* player = g_Vars.currentplayer;
    s32 i;

    if (player->gunctrl.weaponnum != WEAPON_NONE && player->gunctrl.switchtoweaponnum != WEAPON_NONE) {
        // Eject held weapons
        if (player->hands[HAND_LEFT].inuse) {
            player->hands[HAND_LEFT].ejectstate = EJECTSTATE_INIT;
            player->hands[HAND_LEFT].ejecttype = EJECTTYPE_GUN;
        }

        if (player->hands[HAND_RIGHT].inuse) {
            player->hands[HAND_RIGHT].ejectstate = EJECTSTATE_INIT;
            player->hands[HAND_RIGHT].ejecttype = EJECTTYPE_GUN;
        }

        for (i = 0; i < 2; i++) {
            player->hands[i].matmot1 = 0;
            player->hands[i].matmot2 = 0;
            player->hands[i].matmot3 = 0;

            bgunSetState(i, HANDSTATE_IDLE);
        }

        bgunEquipWeapon2(HAND_LEFT, WEAPON_NONE);
        bgunEquipWeapon2(HAND_RIGHT, WEAPON_NONE);
    }
}

#if VERSION >= VERSION_NTSC_1_0
bool bgunIsMissionCritical(s32 weaponnum)
{
    if (weaponnum == WEAPON_TIMEDMINE
        || weaponnum == WEAPON_REMOTEMINE
        || weaponnum == WEAPON_ECMMINE
        || weaponnum == WEAPON_TRACERBUG) {
        return true;
    }

    return false;
}
#endif

void bgunDisarm(struct prop* attackerprop)
{
    struct player* player = g_Vars.currentplayer;
    s32 weaponnum = player->hands[0].gset.weaponnum;
    struct chrdata* chr;
    s32 modelnum;
    s32 i;
    bool drop;

    if (!weaponHasFlag(weaponnum, WEAPONFLAG_UNDROPPABLE) && weaponnum <= WEAPON_RCP45) {
#if VERSION >= VERSION_NTSC_1_0
        // Coop must not allow player to drop a mission critical weapon
        // because AI lists can fail the mission if the player has zero
        // quantity.
        if (g_Vars.coopplayernum >= 0
            && (attackerprop == g_Vars.bond->prop || attackerprop == g_Vars.coop->prop)
            && bgunIsMissionCritical(weaponnum)) {
            return;
        }
#endif

        if (weaponnum <= WEAPON_UNARMED || player->gunctrl.switchtoweaponnum != -1) {
            return;
        }

        chr = player->prop->chr;
        drop = true;

        // RC-P120 and cloaking device: turn off cloak if active
        if (weaponnum == WEAPON_RCP120) {
            g_Vars.currentplayer->devicesactive &= ~DEVICE_CLOAKRCP120;
        }

        if (weaponnum == WEAPON_CLOAKINGDEVICE) {
            g_Vars.currentplayer->devicesactive &= ~DEVICE_CLOAKDEVICE;
        }

        // Grenade and nbomb: if pin is pulled, throw it?
        // Or drop it at player's feet with the pin pulled maybe...
        if (weaponnum == WEAPON_GRENADE || weaponnum == WEAPON_NBOMB) {
            for (i = 0; i < 2; i++) {
                struct weaponfunc* func = gsetGetWeaponFunction(&player->hands[i].gset);

#ifdef AVOID_UB
                if (func && (func->type & 0xff) == INVENTORYFUNCTYPE_THROW
                    && player->hands[i].state == HANDSTATE_ATTACK
                    && player->hands[i].stateminor == HANDSTATEMINOR_ATTACK_THROW_0) {
#else
                    if ((func->type & 0xff) == INVENTORYFUNCTYPE_THROW
										&& player->hands[i].state == HANDSTATE_ATTACK
										&& player->hands[i].stateminor == HANDSTATEMINOR_ATTACK_THROW_0) {
#endif
                    drop = false;
                    bgunCreateThrownProjectile(i + 2, &player->hands[i].gset);
                }
            }
        }

        weaponDeleteFromChr(chr, HAND_RIGHT);
        weaponDeleteFromChr(chr, HAND_LEFT);

        // Actually drop the weapon
        modelnum = playermgrGetModelOfWeapon(weaponnum);

        if (modelnum >= 0 && drop) {
            struct prop* prop2 = weaponCreateForChr(chr, modelnum, weaponnum, OBJFLAG_WEAPON_AICANNOTUSE, NULL, NULL);

            if (prop2 && prop2->obj) {
                struct defaultobj* obj = prop2->obj;
                objSetDropped(prop2, DROPTYPE_DEFAULT);

                if (obj->hidden & OBJHFLAG_PROJECTILE) {
                    obj->projectile->pickuptimer240 = TICKS(240);
                    obj->projectile->pickupby = attackerprop;
                }

                objDrop(prop2, true);
            }
        }

        invRemoveItemByNum(weaponnum);

        player->hands[1].state = HANDSTATE_IDLE;
        player->hands[1].ejectstate = EJECTSTATE_INIT;
        player->hands[1].ejecttype = EJECTTYPE_GUN;
        player->hands[0].ejectstate = EJECTSTATE_INIT;
        player->hands[0].ejecttype = EJECTTYPE_GUN;
        player->hands[0].state = HANDSTATE_IDLE;

        // Exit slayer rocket mode if player was using it
        if (player->visionmode == VISIONMODE_SLAYERROCKET) {
            struct weaponobj* rocket = g_Vars.currentplayer->slayerrocket;

            if (rocket && rocket->base.prop) {
                rocket->timer240 = 0;
            }

            player->visionmode = VISIONMODE_NORMAL;
        }

        bgunEquipWeapon2(HAND_RIGHT, WEAPON_UNARMED);
        bgunEquipWeapon2(HAND_LEFT, WEAPON_UNARMED); // VR
    }
}

/**
 * Execute some sort of command list that was generated by the function below.
 *
 * With this function stubbed, part of the CMP150 model does not render.
 */
void bgunExecuteModelCmdList(uintptr_t * ptr)
{
    union modelrwdata* rwdata;
    struct modelnode* node;

    if (ptr != NULL) {
        while (*ptr != 6) {
            switch (*ptr) {
                case 0:
                    rwdata = (union modelrwdata*)ptr[1];
                    node = (struct modelnode*)ptr[2];
                    rwdata->distance.visible = false;
                    node->child = (struct modelnode*)ptr[3];
                    ptr += 4;
                    break;
                case 1:
                    rwdata = (union modelrwdata*)ptr[1];
                    node = (struct modelnode*)ptr[2];
                    rwdata->toggle.visible = true;
                    node->child = (struct modelnode*)ptr[3];
                    ptr += 4;
                    break;
                case 2:
                    rwdata = (union modelrwdata*)ptr[1];
                    rwdata->headspot.headmodeldef = NULL;
                    rwdata->headspot.rwdatas = NULL;
                    ptr += 2;
                    break;
                case 3:
                    rwdata = (union modelrwdata*)ptr[1];
                    rwdata->type0b.unk00 = 0;
                    ptr += 2;
                    break;
                case 4:
                    rwdata = (union modelrwdata*)ptr[1];
                    rwdata->chrgunfire.visible = false;
                    ptr += 2;
                    break;
                case 5:
                    rwdata = (union modelrwdata*)ptr[1];
                    rwdata->dl.vertices = (Vtx*)ptr[2];
                    rwdata->dl.gdl = (Gfx*)ptr[3];
                    rwdata->dl.colours = (Col*)ptr[4];
                    ptr += 5;
                    break;
            }
        }
    }
}

/**
 * Generate some sort of command list to be executed by the function above.
 *
 * This appears to be a performance optimisation, so the tick code can quickly
 * iterate the command list to update part visibility rather than iterate the
 * full model tree.
 */
s32 bgunCreateModelCmdList(struct model* model, struct modelnode* nodearg, uintptr_t * ptr)
{
    s32 len = 0;
    struct modelnode* node = nodearg;
    union modelrodata* rodata;
    union modelrwdata* rwdata;

    while (node) {
        u32 type = node->type;

        switch ((u8)type) {
            case MODELNODETYPE_DISTANCE:
                rodata = node->rodata;
                rwdata = modelGetNodeRwData(model, node);
                rwdata->distance.visible = false;
                node->child = rodata->distance.target;
                ptr[0] = 0;
                ptr[1] = (uintptr_t)rwdata;
                ptr[2] = (uintptr_t)node;
                ptr[3] = (uintptr_t)rodata->distance.target;
                ptr += 4;
                len += 4 * sizeof(uintptr_t);
                break;
            case MODELNODETYPE_TOGGLE:
                rodata = node->rodata;
                rwdata = modelGetNodeRwData(model, node);
                rwdata->toggle.visible = true;
                node->child = rodata->toggle.target;
                ptr[0] = 1;
                ptr[1] = (uintptr_t)rwdata;
                ptr[2] = (uintptr_t)node;
                ptr[3] = (uintptr_t)rodata->toggle.target;
                ptr += 4;
                len += 4 * sizeof(uintptr_t);
                break;
            case MODELNODETYPE_HEADSPOT:
                rwdata = modelGetNodeRwData(model, node);
                rwdata->headspot.headmodeldef = NULL;
                rwdata->headspot.rwdatas = NULL;
                ptr[0] = 2;
                ptr[1] = (uintptr_t)rwdata;
                ptr += 2;
                len += 2 * sizeof(uintptr_t);
                break;
            case MODELNODETYPE_0B:
                rwdata = modelGetNodeRwData(model, node);
                rwdata->type0b.unk00 = 0;
                ptr[0] = 3;
                ptr[1] = (uintptr_t)rwdata;
                ptr += 2;
                len += 2 * sizeof(uintptr_t);
                break;
            case MODELNODETYPE_CHRGUNFIRE:
                rwdata = modelGetNodeRwData(model, node);
                rwdata->chrgunfire.visible = false;
                ptr[0] = 4;
                ptr[1] = (uintptr_t)rwdata;
                ptr += 2;
                len += 2 * sizeof(uintptr_t);
                break;
            case MODELNODETYPE_DL:
                rodata = node->rodata;
                rwdata = modelGetNodeRwData(model, node);
                rwdata->dl.vertices = rodata->dl.vertices;
                rwdata->dl.gdl = rodata->dl.opagdl;
                rwdata->dl.colours = (void*)ALIGN8((uintptr_t)&rodata->dl.vertices[rodata->dl.numvertices]);
                ptr[0] = 5;
                ptr[1] = (uintptr_t)rwdata;
                ptr[2] = (uintptr_t)rwdata->dl.vertices;
                ptr[3] = (uintptr_t)rwdata->dl.gdl;
                ptr[4] = (uintptr_t)rwdata->dl.colours;
                ptr += 5;
                len += 5 * sizeof(uintptr_t);
                break;
        }

        if (node->child) {
            node = node->child;
        }
        else {
            while (node) {
                if (node == nodearg->parent) {
                    node = NULL;
                    break;
                }

                if (node->next) {
                    node = node->next;
                    break;
                }

                node = node->parent;
            }
        }
    }

    *ptr = 6;
    len += sizeof(uintptr_t);

    return len;
}

u32 var800701ec = 0x00000000;
u32 var800701f0 = 0x00000000;
u32 var800701f4 = 0x00000000;
u32 var800701f8 = 0x00000000;
u32 var800701fc = 0x00000000;

struct guncmd var80070200[2] = {
        { GUNCMD_PLAYANIMATION, 0, ANIM_0434, 10000 },
        { GUNCMD_END },
};

void bgunStartDetonateAnimation(s32 playernum)
{
    s32 prevplayernum = g_Vars.currentplayernum;
    setCurrentPlayerNum(playernum);

    // VR holsters: the remote mine is a single, but bgunTickSwitch2 names it in both hands' gset:
    // the unused left hand plays no detonator animation
    if (g_Vars.currentplayer->hands[HAND_LEFT].gset.weaponnum == WEAPON_REMOTEMINE
        && (g_Vars.currentplayer->hands[HAND_LEFT].inuse || !vrHolsterActive())) {
        bgunStartAnimation(var80070200, 1, &g_Vars.currentplayer->hands[HAND_LEFT]);
    }

    setCurrentPlayerNum(prevplayernum);
}

/**
 * Update the gangsta-style rotation of the player's gun.
 *
 * When close to an enemy and aiming at them with a pistol, the gun is rotated
 * sideways. The enemy and aiming check is done elsewhere (autoaim code) and
 * sets the gunctrl's gangsta property to true or false based on whether this
 * criteria is met on the current (or previous?) frame.
 *
 * bgunUpdateGangsta uses this property and increments the rotation of the gun
 * accordingly. It also checks that the gun is in a state that allows gangsta
 * rotation (reloading and equip/unequip do not). It also implements a delay on
 * reverting to the normal rotation.
 */
void bgunUpdateGangsta(struct hand* hand, s32 handnum, struct coord* arg2, struct weaponfunc* funcdef, Mtxf * arg4, Mtxf * arg5)
{
    f32 tmp;
    struct coord sp38 = { 0, 0, 0 };

    /* // Removed for VR

    if (g_Vars.currentplayer->gunctrl.gangsta
        && funcdef
        && (funcdef->type & 0xff) == INVENTORYFUNCTYPE_SHOOT
        && (hand->state == HANDSTATE_IDLE
            || hand->state == HANDSTATE_2
            || hand->state == HANDSTATE_ATTACKEMPTY
            || hand->state == HANDSTATE_ATTACK)) {
        if (hand->gangstarot < 1.0f) {
            // Rotate into gangsta position
            hand->ispare1 += g_Vars.lvupdate240;

            if (hand->ispare1 > TICKS(60)) {
                hand->gangstarot += LVUPDATE60FREAL() / 30.0f;

                if (hand->gangstarot > 1.0f) {
                    hand->gangstarot = 1.0f;
                }
            }
        }
        else {
            // Already in gangsta position
            hand->ispare1 = 0;
        }
    }
    else {
        // At this point we don't want the gun to be in the gangsta position.
        // However we don't want it to revert immediately, so a timer is used.
        f32 inversespeed = 30.0f;

        if (hand->animmode == HANDANIMMODE_BUSY) {
            // Revert faster
            inversespeed = 15.0f;
        }

        if (hand->gangstarot > 0.0f) {
            bool revert = false;

            hand->ispare1 += g_Vars.lvupdate240;

            if (hand->gangstarot < 1.0f) {
                hand->ispare1 = TICKS(244);
            }

            if (hand->ispare1 > TICKS(120)) {
                revert = true;
            }

            if (hand->animmode == HANDANIMMODE_BUSY && funcdef && (funcdef->type & 0xff) != INVENTORYFUNCTYPE_SHOOT) {
                revert = true;
            }

            if (hand->state != HANDSTATE_IDLE
                && hand->state != HANDSTATE_2
                && hand->state != HANDSTATE_ATTACKEMPTY
                && hand->state != HANDSTATE_ATTACK) {
                revert = true;
            }

            if (revert) {
                hand->gangstarot -= LVUPDATE60FREAL() / inversespeed;
            }

            if (hand->gangstarot < 0.0f) {
                hand->gangstarot = 0.0f;
            }
        }
        else {
            // Not rotated
            hand->ispare1 = 0;
        }
    }

    tmp = -cosf(hand->gangstarot * M_PI) * 0.5f + 0.50f;
    sp38.z = (tmp * 66.6f * 0.017453292f) * (handnum != HAND_RIGHT ? 1.0f : -1.0f);

    mtx4LoadRotation(&sp38, arg4);
    mtx00015be0(arg4, arg5);

    arg2->y += 4.0f * hand->gangstarot;
    arg2->x += 2.0f * hand->gangstarot * (handnum != HAND_RIGHT ? 1.0f : -1.0f);

     */
}

/**
 * Check if smoke needs to be created at the muzzle of the current weapon.
 *
 * gunsmokepoint is basically the temperature of the gun. It increases when
 * firing and cools down when idle. It's only used for pistols; automatics will
 * create smoke based on the number of shots in the current burst.
 *
 * createsmoke must be set to create any smoke at all.
 *
 * forcecreatesmoke controls whether smoke should be created while the gun is
 * still firing.
 */
void bgunUpdateSmoke(struct hand* hand, s32 handnum, s32 weaponnum, struct weaponfunc* funcdef)
{
    if (hand->firing) {
        if (weaponnum == WEAPON_DY357MAGNUM || weaponnum == WEAPON_DY357LX) {
            if ((funcdef->type & 0xff) == INVENTORYFUNCTYPE_SHOOT) {
                hand->gunsmokepoint += 0.6f;
            }
        }
        else {
            hand->gunsmokepoint += 0.2f;
        }
    }

    hand->gunsmokepoint -= LVUPDATE60FREAL() / 120.0f;

    if (hand->gunsmokepoint < 0.0f) {
        hand->gunsmokepoint = 0.0f;
    }

    if (funcdef && (funcdef->type & 0xff) == INVENTORYFUNCTYPE_SHOOT) {
        f32 mult = 1.0f;

        if (g_Vars.currentplayer->hands[HAND_LEFT].inuse) {
            mult = 1.5f;
        }

        hand->forcecreatesmoke = false;

        switch (weaponnum) {
            case WEAPON_FALCON2:
            case WEAPON_FALCON2_SCOPE:
                if (hand->gunsmokepoint * mult > 0.66f) {
                    hand->createsmoke = true;
                }
                break;
            case WEAPON_MAGSEC4:
            case WEAPON_MAULER:
                if (hand->gunsmokepoint * mult > 0.75f) {
                    hand->createsmoke = true;
                }
                break;
            case WEAPON_DY357MAGNUM:
            case WEAPON_DY357LX:
                if (hand->gunsmokepoint * mult > 0.9f) {
                    hand->createsmoke = true;
                }
                break;
            case WEAPON_CMP150:
            case WEAPON_DRAGON:
            case WEAPON_K7AVENGER:
            case WEAPON_AR34:
            case WEAPON_SUPERDRAGON:
                hand->forcecreatesmoke = true;

                if (hand->burstbullets > 14) {
                    hand->createsmoke = true;
                }
                break;
            case WEAPON_CYCLONE:
            case WEAPON_LAPTOPGUN:
                if (hand->burstbullets > 20) {
                    hand->createsmoke = true;
                }

                hand->forcecreatesmoke = true;
                break;
            case WEAPON_RCP120:
                hand->forcecreatesmoke = true;

                if (hand->burstbullets > 25) {
                    hand->createsmoke = true;
                }
                break;
            case WEAPON_REAPER:
                hand->forcecreatesmoke = true;
                // fall-through
            case WEAPON_SHOTGUN:
                if (hand->firing) {
                    hand->createsmoke = true;
                }
                break;
        }
    }

    if (hand->createsmoke && (hand->state != HANDSTATE_ATTACK || hand->forcecreatesmoke)) {
        struct coord smokepos;
        RoomNum smokerooms[2];
        s32 smoketype = SMOKETYPE_MUZZLE_AUTOMATIC;

        switch (weaponnum) {
            case WEAPON_FALCON2:
            case WEAPON_FALCON2_SCOPE:
            case WEAPON_MAGSEC4:
            case WEAPON_MAULER:
            case WEAPON_DY357MAGNUM:
            case WEAPON_DY357LX:
                smoketype = SMOKETYPE_MUZZLE_PISTOL;
                break;
            case WEAPON_REAPER:
                smoketype = SMOKETYPE_MUZZLE_REAPER;
                break;
            case WEAPON_SHOTGUN:
                smoketype = SMOKETYPE_MUZZLE_SHOTGUN;
                break;
        }

        smokerooms[0] = g_Vars.currentplayer->cam_room;
        smokerooms[1] = -1;

        smokepos.x = hand->muzzlepos.x;
        smokepos.y = hand->muzzlepos.y;
        smokepos.z = hand->muzzlepos.z;

        hand->gunsmokepoint = 0.0f;

        if (smokeCreateForHand(&smokepos, smokerooms, smoketype, handnum)) {
            hand->createsmoke = false;
        }
    }
}


/**
 * Update the red beam and dot (used by the Falcon 2 and its variants).
 */
void bgunUpdateLasersight(struct hand* hand, struct modeldef* modeldef, s32 handnum, u8* allocation)
{
    struct modelnode* node;
    struct coord beamfar, dotpos, dotrot, beamnear;
    struct coord sp54, sp48, sp3c, sp30;
    s32 mtxindex;
    bool busy;

    node = modelGetPart(modeldef, MODELPART_GUN_LASERSIGHT);

    if (node) {
        mtxindex = modelFindNodeMtxIndex(node, 0);

        beamnear.x = ((Mtxf*)((uintptr_t)allocation + mtxindex * sizeof(Mtxf)))->m[3][0];
        beamnear.y = ((Mtxf*)((uintptr_t)allocation + mtxindex * sizeof(Mtxf)))->m[3][1];
        beamnear.z = ((Mtxf*)((uintptr_t)allocation + mtxindex * sizeof(Mtxf)))->m[3][2];

        mtx4TransformVecInPlace(camGetProjectionMtxF(), &beamnear);

        if (hand->useposrot
            || (g_Vars.currentplayer->devicesactive & ~g_Vars.currentplayer->devicesinhibit & DEVICE_XRAYSCANNER)) {
            beamfar.x = 0.0f;
            beamfar.y = 0.0f;
            beamfar.z = 1.0f;

            mtx4RotateVecInPlace(&hand->cammtx, &beamfar);

            sp48.x = beamfar.x; sp48.y = beamfar.y; sp48.z = beamfar.z;
            sp3c.x = beamnear.x; sp3c.y = beamnear.y; sp3c.z = beamnear.z;

            mtx4TransformVec(camGetWorldToScreenMtxf(), &sp3c, &sp54);
            mtx4RotateVec(camGetProjectionMtxF(), &sp48, &sp30);

            beamfar.x *= 500.0f;
            beamfar.y *= 500.0f;
            beamfar.z *= 500.0f;

            mtx4RotateVecInPlace(camGetProjectionMtxF(), &beamfar);

            beamfar.x += beamnear.x;
            beamfar.y += beamnear.y;
            beamfar.z += beamnear.z;

            lasersightSetBeam(handnum, 1, &beamnear, &beamfar);

            if (show_laser_dot[handnum]) {
                dotpos.x = hand->dotpos.x; dotpos.y = hand->dotpos.y; dotpos.z = hand->dotpos.z;
                dotrot.x = hand->dotrot.x; dotrot.y = hand->dotrot.y; dotrot.z = hand->dotrot.z;
                lasersightSetDot(handnum, &dotpos, &dotrot);
            }

            return;
        }

        busy = (hand->animmode == HANDANIMMODE_BUSY);

        if (busy) {
            mtxindex = modelFindNodeMtxIndex(node, 0);
            beamfar.x = 0.0f; beamfar.y = 0.0f; beamfar.z = 500.0f;
            mtx4TransformVecInPlace((Mtxf*)((uintptr_t)allocation + mtxindex * sizeof(Mtxf)), &beamfar);
        } else {
            cam0f0b4c3c(g_Vars.currentplayer->crosspos, &beamfar, 1);
            beamfar.x *= 500.0f; beamfar.y *= 500.0f; beamfar.z *= 500.0f;
        }

        mtx4TransformVecInPlace(camGetProjectionMtxF(), &beamfar);
        lasersightSetBeam(handnum, 1, &beamnear, &beamfar);

        if (show_laser_dot[handnum]) {
            dotpos.x = hand->dotpos.x; dotpos.y = hand->dotpos.y; dotpos.z = hand->dotpos.z;
            dotrot.x = hand->dotrot.x; dotrot.y = hand->dotrot.y; dotrot.z = hand->dotrot.z;
            lasersightSetDot(handnum, &dotpos, &dotrot);
        }
    } else if (VrlaserDotForALL) {

        // ======= VR OTHER WEAPONS ONLY =======
        // Node missing = no Falcon2 → create the slot with an invisible dummy beam
        // then display the dot if the position is valid


        // Left hand without a weapon should not have a dot
        if (!hand->inuse) {
            lasersightFree(handnum);
            return;
        }

        if (vrLaserDotAllowed(hand->gset.weaponnum)
            && (hand->dotpos.x != 0.0f || hand->dotpos.y != 0.0f || hand->dotpos.z != 0.0f))
        {
            struct coord dummyNear = {0.0f, 0.0f, 0.0f};
            struct coord dummyFar  = {0.0f, 0.0f, 0.0f};
            lasersightSetBeam(handnum, 0, &dummyNear, &dummyFar);

            dotpos.x = hand->dotpos.x;
            dotpos.y = hand->dotpos.y;
            dotpos.z = hand->dotpos.z;
            dotrot.x = hand->dotrot.x;
            dotrot.y = hand->dotrot.y;
            dotrot.z = hand->dotrot.z;
            lasersightSetDot(handnum, &dotpos, &dotrot);
        }
    }else{
        lasersightFree(handnum);

    }
}

/**
 * Increment the main barrel spinning, play sounds and (probably) fire shots.
 */
void bgunUpdateReaper(struct hand* hand, struct modeldef* modeldef)
{
    struct modelnode* node;
    f32 f2;
    f32 f12;
    s32 tmp;

    node = modelGetPart(modeldef, MODELPART_REAPER_002C);

    if (hand->matmot3 <= hand->matmot2) {
        if (hand->matmot2 < 0.0f) {
            hand->matmot2 += 0.01f * LVUPDATE60FREAL();

            if (hand->matmot2 > 0.0f) {
                hand->matmot2 = 0.0f;
            }
        }

        hand->matmot3 = hand->matmot2;
    }
    else {
        f12 = LVUPDATE60FREAL() * 0.005;

        if (hand->matmot2 < 0.0000001f) {
            hand->matmot2 = -0.14f;

            if (hand->matmot3 < 0.15f) {
                f12 *= 4.0f;
            }
        }

        f2 = hand->matmot3 - hand->matmot2;

        if (f12 < f2) {
            f2 = f12;
        }

        hand->matmot3 -= f2;
    }

    if (hand->matmot3 < 0.0f) {
        hand->matmot1 = hand->matmot1 - (1.0f - cosf(hand->matmot3 * M_PI)) * 0.5f * LVUPDATE60FREAL() * 0.2f;
    }
    else {
        hand->matmot1 = hand->matmot1 + (1.0f - cosf(hand->matmot3 * M_PI)) * 0.5f * LVUPDATE60FREAL() * 0.2f;
    }

    tmp = hand->matmot1 / 6.2831802368164f;
    hand->matmot1 -= tmp * 6.2831802368164f;
    var8009d140 = hand->matmot1;

    if (hand->audiohandle == NULL && hand->matmot3 > 0.1f && g_Vars.lvupdate240 != 0) {
        sndStart(var80095200, SFX_805E, &hand->audiohandle, -1, -1, -1.0f, -1, -1);
    }

    if (hand->audiohandle != NULL) {
        f32 sp34 = hand->matmot3 / 0.50f + 0.4f;
        s32 volume = AL_VOL_FULL;

        if (hand->matmot3 < 0.1f) {
            audioStop(hand->audiohandle);
        }
        else {
            if (hand->matmot3 < 0.6f) {
                volume = (hand->matmot3 - 0.1f) * AL_VOL_FULL / 0.5f;
            }

            audioPostEvent(hand->audiohandle, AL_SNDP_VOL_EVT, volume);
            audioPostEvent(hand->audiohandle, AL_SNDP_PITCH_EVT, *(s32*)&sp34);
        }
    }

    if (node) {
        var8009d0dc = modelFindNodeMtxIndex(node, 0);
        g_ModelJointPositionedFunc = bgun0f0a256c;
        var8009d0f0[0] = var8009d0f0[1] = var8009d0f0[2] = -1;
    }

    node = modelGetPart(modeldef, MODELPART_REAPER_002D);

    if (node) {
        var8009d0f0[0] = modelFindNodeMtxIndex(node, 0);
    }

    node = modelGetPart(modeldef, MODELPART_REAPER_002E);

    if (node) {
        var8009d0f0[1] = modelFindNodeMtxIndex(node, 0);
    }

    node = modelGetPart(modeldef, MODELPART_REAPER_002F);

    if (node) {
        var8009d0f0[2] = modelFindNodeMtxIndex(node, 0);
    }
}

/**
 * Move/extend the scope on the gun model when the zoom function is used.
 */
void bgunUpdateSniperRifle(struct modeldef* modeldef, u8 * allocation)
{
    struct modelnode* nodes[4];
    f32 sp88[4] = { 0, 0, 0, 0 };
    s32 i;
    f32 f26;
    s32 mtxindex;
    struct coord sp70;

    f26 = 1.0f - (currentPlayerGetGunZoomFov() - 2.0f) / 58.0f;

    nodes[0] = modelGetPart(modeldef, MODELPART_SNIPERRIFLE_SCOPE1);
    nodes[1] = modelGetPart(modeldef, MODELPART_SNIPERRIFLE_SCOPE2);
    nodes[2] = modelGetPart(modeldef, MODELPART_SNIPERRIFLE_SCOPE3);
    nodes[3] = modelGetPart(modeldef, MODELPART_SNIPERRIFLE_SCOPE4);

    for (i = 0; i < ARRAYCOUNT(nodes); i++) {
        if (nodes[i]) {
            f32 f20 = f26 * 4.0f;
            mtxindex = modelFindNodeMtxIndex(nodes[i], 0);
            sp88[i] = f20 - i;

            if (f20 < i) {
                sp88[i] = 0.0f;
            }

            sp88[i] *= 100.0f;

            sp70.x = 0.0f;
            sp70.y = 0.0f;
            sp70.z = sp88[i];

            mtx4RotateVecInPlace((Mtxf*)((uintptr_t)allocation + mtxindex * sizeof(Mtxf)), &sp70);

            ((Mtxf*)((uintptr_t)allocation + mtxindex * sizeof(Mtxf)))->m[3][0] += sp70.x;
            ((Mtxf*)((uintptr_t)allocation + mtxindex * sizeof(Mtxf)))->m[3][1] += sp70.y;
            ((Mtxf*)((uintptr_t)allocation + mtxindex * sizeof(Mtxf)))->m[3][2] += sp70.z;
        }
    }
}

/**
 * Animate the cartridge slider thing in the Devastator model.
 */
void bgunUpdateDevastator(struct hand* hand, u8 * allocation, struct modeldef* modeldef)
{
    struct modelnode* node = modelGetPart(modeldef, MODELPART_DEVASTATOR_0028);

    if (node) {
        s32 mtxindex = modelFindNodeMtxIndex(node, 0);
        struct coord sp24;

        hand->loadslide += 0.01f * LVUPDATE60FREAL();

        if (hand->loadslide > 1.0f) {
            hand->loadslide = 1.0f;
        }

        sp24.x = hand->loadslide * -10.0f * 1.636f;
        sp24.y = 0.0f;
        sp24.z = 0.0f;

        mtx4RotateVecInPlace((Mtxf*)((uintptr_t)allocation + mtxindex * sizeof(Mtxf)), &sp24);

        ((Mtxf*)((uintptr_t)allocation + mtxindex * sizeof(Mtxf)))->m[3][0] += sp24.x;
        ((Mtxf*)((uintptr_t)allocation + mtxindex * sizeof(Mtxf)))->m[3][1] += sp24.y;
        ((Mtxf*)((uintptr_t)allocation + mtxindex * sizeof(Mtxf)))->m[3][2] += sp24.z;
    }
}

/**
 * Display the shotgun's starburst when appropriate.
 *
 * This logic is different to most guns, likely because most guns display the
 * starburst when the trigger is pressed while the shotgun has the double blast
 * function.
 */
void bgunUpdateShotgun(struct hand* hand, u8 * allocation, bool* arg2, struct modeldef* modeldef)
{
    if (hand->flashon) {
        hand->matmot1 = 1.0f;
    }

    if (hand->matmot1 > 0.0f) {
        hand->matmot1 -= LVUPDATE60FREAL() / 6.0f;

        if (hand->matmot1 < 0.01f) {
            hand->matmot1 = 0.0f;
        }
    }

    if (hand->matmot1 > 0.0f) {
        s32 sp34;
        s32 sp28[3] = { 0, 0, 0 };
        struct modelnode* node = modelGetPart(modeldef, MODELPART_SHOTGUN_0050);

        *arg2 = true;

        if (node) {
            sp34 = modelFindNodeMtxIndex(node, 0);

            mtx00015ea8((1.0f - hand->matmot1) * 8.0f + 0.5f, (Mtxf*)((uintptr_t)allocation + sp34 * sizeof(Mtxf)));
            mtx00015df0((1.0f - hand->matmot1) * 3.0f + 1.0f, (Mtxf*)((uintptr_t)allocation + sp34 * sizeof(Mtxf)));
            mtx00015e4c((1.0f - hand->matmot1) * 3.0f + 1.0f, (Mtxf*)((uintptr_t)allocation + sp34 * sizeof(Mtxf)));
        }
    }
}

void bgunUpdateLaser(struct hand* hand)
{
    if (hand->firing && hand->gset.weaponfunc == FUNC_SECONDARY) {
        if (hand->audiohandle == NULL && g_Vars.lvupdate240 != 0) {
            sndStart(var80095200, SFX_LASER_STREAM, &hand->audiohandle, -1, -1, -1, -1, -1);
        }

        hand->matmot1 = 1;
        return;
    }

    if (hand->matmot1 > 0) {
        hand->matmot1 -= LVUPDATE60FREAL() / 10.0f;
    }
    else if (hand->audiohandle != NULL && sndGetState(hand->audiohandle) != AL_STOPPED) {
        audioStop(hand->audiohandle);
    }
}

/**
 * Create ammo casing so they can be ejected during reload.
 */
void bgunUpdateMagnum(struct hand* hand, s32 handnum, struct modeldef* modeldef, Mtxf * mtx)
{
    f32 ground = g_Vars.currentplayer->vv_ground;
    s32 i;

    if (modeldef != NULL) {
        for (i = 0; i < hand->unk0cc8_04; i++) {
            struct modelnode* node = modelGetPart(modeldef, 0x0a + rngRandom() % 6);

            if (node) {
                s32 index = modelFindNodeMtxIndex(node, 0);
                Mtxf* tmp = mtx;
                Mtxf sp4c;

                tmp += index;

                mtx4Copy(tmp, &sp4c);
                mtx00015f04(9.999999f, &sp4c);
                mtx4MultMtx4InPlace(camGetProjectionMtxF(), &sp4c);

                casingCreateForHand(handnum, ground, &sp4c);
            }
        }
    }
}

/**
 * Create and/or update the rocket prop that sits inside the rocket launcher.
 */
void bgunUpdateRocketLauncher(struct hand* hand, s32 handnum, struct weaponfunc_shootprojectile* func)
{
    if (hand->rocket == NULL && hand->loadedammo[0] > 0) {
        bgunCreateHeldRocket(handnum, func);
    }

    if (hand->rocket) {
        bgunUpdateHeldRocket(handnum);
    }
}

void bgun0f0a45d0(struct hand* hand, struct modeldef* modeldef, bool isdetonator)
{
    struct modelnode* node = NULL;

    switch (hand->ejecttype) {
        case EJECTTYPE_GUN:
            if (isdetonator) {
                node = modelGetPart(modeldef, 0x2a);
            }
            else {
                node = modelGetPart(modeldef, 0x37);
            }
            break;
        case EJECTTYPE_GRENADEPIN:
            node = modelGetPart(modeldef, 0x2b);
            break;
        case EJECTTYPE_TRANQCASE:
            node = modelGetPart(modeldef, 0x2b);
            break;
    }

    if (node) {
        var8009d148 = modelFindNodeMtxIndex(node, 0);
        g_ModelJointPositionedFunc = bgun0f0a256c;
    }
    else {
        var8009d148 = -1;
    }
}

/**
 * With this function stubbed, the tranquilizer's spent ammo does not detach
 * when reloading, and the pulled pin on grenades and nbombs appears to move
 * with the model rather than detaching properly.
 */
void bgunTickEject(struct hand* hand, struct modeldef* modeldef, bool isdetonator)
{
    f32 lvupdate;
    struct coord spd0;
    Mtxf sp90;
    struct coord sp84;
    Mtxf sp44;
    s32 i;
    f32 newval;
    f32 mult = 3;

    switch (hand->ejectstate) {
        case EJECTSTATE_INIT:
            switch (hand->ejecttype) {
                case EJECTTYPE_GUN:
                    hand->unk0d20.f[0] = (RANDOMFRAC() - 0.5f) * 0.5333333f * 0.0625f + 0.5333333f;
                    hand->unk0d20.f[1] = RANDOMFRAC() * 2.5f * 0.0625f + 2.5f;
                    hand->unk0d20.f[2] = 0.0f;
#if VERSION >= VERSION_PAL_BETA
                    spd0.f[0] = RANDOMFRAC() * PALUPF(2.0f * M_BADTAU) / 184.0f - 0.03414231f;
						spd0.f[1] = RANDOMFRAC() * PALUPF(2.0f * M_BADTAU) / 184.0f - 0.03414231f;
						spd0.f[2] = RANDOMFRAC() * PALUPF(2.0f * M_BADTAU) / 184.0f - 0.03414231f;
#else
                    spd0.f[0] = RANDOMFRAC() * 2.0f * M_BADTAU / 184.0f - 0.03414231f;
                    spd0.f[1] = RANDOMFRAC() * 2.0f * M_BADTAU / 184.0f - 0.03414231f;
                    spd0.f[2] = RANDOMFRAC() * 2.0f * M_BADTAU / 184.0f - 0.03414231f;
#endif
                    break;
                case EJECTTYPE_GRENADEPIN:
                    hand->unk0d20.f[0] = -((RANDOMFRAC() - 0.5f) * 0.5333333f * 0.0625f + mult * 0.5333333f);
                    hand->unk0d20.f[1] = RANDOMFRAC() * 2.5f * 0.125f + 2.5f;
                    hand->unk0d20.f[2] = -(RANDOMFRAC() + 1.0f);
                    spd0.f[0] = (RANDOMFRAC() + 3.0f) * PALUPF(M_BADTAU) / 208.0f;
#if VERSION >= VERSION_PAL_BETA
                    spd0.f[1] = RANDOMFRAC() * PALUPF(2.0f * M_BADTAU) / 544.0f - 0.0115481345f;
						spd0.f[2] = RANDOMFRAC() * PALUPF(2.0f * M_BADTAU) / 544.0f - 0.0115481345f;
#else
                    spd0.f[1] = RANDOMFRAC() * 2.0f * M_BADTAU / 544.0f - 0.0115481345f;
                    spd0.f[2] = RANDOMFRAC() * 2.0f * M_BADTAU / 544.0f - 0.0115481345f;
#endif
                    break;
                case EJECTTYPE_TRANQCASE:
                    hand->unk0d20.f[0] = 0.0f;
                    hand->unk0d20.f[1] = RANDOMFRAC() * 2.5f * 0.125f + 2.5f;
                    hand->unk0d20.f[2] = (RANDOMFRAC() + 1.0f) * 0.25f;
                    spd0.f[0] = (RANDOMFRAC() + 3.0f) * PALUPF(M_BADTAU) / 368.0f;
#if VERSION >= VERSION_PAL_BETA
                    spd0.f[1] = RANDOMFRAC() * PALUPF(2.0f * M_BADTAU) / 944.0f - 0.006654857f;
						spd0.f[2] = RANDOMFRAC() * PALUPF(2.0f * M_BADTAU) / 944.0f - 0.006654857f;
#else
                    spd0.f[1] = RANDOMFRAC() * 2.0f * M_BADTAU / 944.0f - 0.006654857f;
                    spd0.f[2] = RANDOMFRAC() * 2.0f * M_BADTAU / 944.0f - 0.006654857f;
#endif
                    break;
            }

            hand->unk0d10 = hand->unk0d14 - 200.0f;

            mtx4LoadRotation(&spd0, &sp90);
            mtx4ToMtx3(&sp90, hand->unk0d50);

            if (g_Vars.lvupdate240 > 0 && hand->ejecttype != EJECTTYPE_GUN) {
                sp84.f[0] = (hand->posmtx.m[3][0] - hand->prevmtx.m[3][0]) / g_Vars.lvupdate60freal;
                sp84.f[1] = (hand->posmtx.m[3][1] - hand->prevmtx.m[3][1]) / g_Vars.lvupdate60freal;
                sp84.f[2] = (hand->posmtx.m[3][2] - hand->prevmtx.m[3][2]) / g_Vars.lvupdate60freal;

                mtx00017588(hand->posmtx.m, sp44.m);
                mtx4RotateVecInPlace(&sp44, &sp84);

                hand->unk0d20.f[0] += sp84.f[0] * 0.3f;
                hand->unk0d20.f[1] += sp84.f[1] * 0.3f;
                hand->unk0d20.f[2] += sp84.f[2] * 0.3f;
            }

            hand->ejectstate = EJECTSTATE_AIRBORNE;
            break;
        case EJECTSTATE_AIRBORNE:
            lvupdate = g_Vars.lvupdate60freal;

            if (g_Vars.currentplayer->isdead && lvupdate > 1.5f) {
                lvupdate = 1.5f;
            }

            newval = hand->unk0d20.f[1] - lvupdate * 0.2777778f;

            if (hand->unk0d18 < hand->unk0d10) {
                hand->ejectstate = EJECTSTATE_FINISHED;
                break;
            }

            hand->unk0d18 += lvupdate * 0.5f * (hand->unk0d20.f[1] + newval);
            hand->unk0d14 += lvupdate * hand->unk0d20.f[0];
            hand->unk0d1c += lvupdate * hand->unk0d20.f[2];

            hand->unk0d20.f[1] = newval;

            for (i = 0; i < g_Vars.lvupdate240; i++) {
                mtx00016110(hand->unk0d50, hand->unk0d2c);
            }

            break;
    }
}

void bgun0f0a4e44(struct hand* hand, struct weapon* weapondef, struct modeldef* modeldef,
                  struct weaponfunc* funcdef, s32 maxburst, u8 * allocation, s32 weaponnum,
                  bool** arg7, s32 mtxindex, Mtxf * arg9, Mtxf * arg10)
{
    Mtxf spd8;
    s32 index;
    s32 shotstotake;
    bool spc4[3] = { false, false, false };
    Mtxf* mtx;
    s32 i;
    s32 partnum;
    f32 spb4;
    f32 muzzlez;
    Mtxf sp70;

    index = hand->burstbullets % maxburst;
    shotstotake = hand->shotstotake;

    spb4 = RANDOMFRAC() * 0.25f + 1.0f;
    muzzlez = weapondef->muzzlez;

    mtx4LoadIdentity(&spd8);

    if (funcdef && (funcdef->flags & FUNCFLAG_00000001)) {
        mtx4LoadZRotation(RANDOMFRAC() * M_BADTAU, &spd8);
    }

    mtx4LoadZRotation((RANDOMFRAC() * 0.3 - 0.15), &spd8);

    mtx = (Mtxf*)allocation;
    mtx += mtxindex;

    mtx4MultMtx4InPlace(mtx, &spd8);
    mtx00015f04(spb4, &spd8);
    mtx00015ea8(muzzlez, &spd8);
    mtx4Copy(&spd8, mtx);

    if (shotstotake == 0 && weaponnum != WEAPON_REAPER) {
        shotstotake++;
    }

    for (i = 0; i < shotstotake; i++) {
        spc4[index] = true;
        index++;

        if (index >= maxburst) {
            index = 0;
        }
    }

    for (i = 0; i < maxburst; i++) {
        if (spc4[i] && arg7[i] != NULL) {
            *arg7[i] = true;
        }
    }

    for (partnum = 0x50; partnum <= 0x52; partnum++) {
        struct modelnode* node = modelGetPart(modeldef, partnum);
        struct coord sp60;

        if (node && weaponnum != WEAPON_REAPER && weaponnum != WEAPON_SHOTGUN) {
            struct modelrodata_position* rodata = &node->rodata->position;
            s32 mtxindex = modelFindNodeMtxIndex(node, 0);

            sp60.x = rodata->pos.x * spd8.m[0][0] + rodata->pos.y * spd8.m[1][0] + rodata->pos.z * spd8.m[2][0] + spd8.m[3][0];
            sp60.y = rodata->pos.x * spd8.m[0][1] + rodata->pos.y * spd8.m[1][1] + rodata->pos.z * spd8.m[2][1] + spd8.m[3][1];
            sp60.z = rodata->pos.x * spd8.m[0][2] + rodata->pos.y * spd8.m[1][2] + rodata->pos.z * spd8.m[2][2] + spd8.m[3][2];

            mtx4LoadIdentity(&sp70);
            mtx4Align(sp70.m, RANDOMFRAC() * M_BADTAU, -sp60.x, -sp60.y, -sp60.z);
            mtx00015f04(0.10000001f * spb4, &sp70);

            mtx = (Mtxf*)allocation;

            mtx00016e98(arg10->m, 0, mtx->m[3][0] - hand->aimpos.x, mtx->m[3][1] - hand->aimpos.y, mtx->m[3][2] - hand->aimpos.z);
            mtx4MultMtx4InPlace(arg10, &sp70);
            mtx00016710(muzzlez, sp70.m);
            mtx4MultMtx4InPlace(arg9, &sp70);
            mtx4SetTranslation(&sp60, &sp70);

            mtx = (Mtxf*)allocation;
            mtx += mtxindex;

            mtx4Copy(&sp70, mtx);
        }
    }
}

/**
 * Create casing and beam for a fired weapon,
 * and uncloak if the weapon is a throwable or fired projectile.
 */
void bgunCreateFx(struct hand* hand, s32 handnum, struct weaponfunc* funcdef, s32 weaponnum, struct modeldef* modeldef, u8 * allocation)
{
    f32 ground;
    bool createbeam = true;

    g_Vars.currentplayer->gunctrl.throwing = false;

    if (funcdef) {
        ground = g_Vars.currentplayer->vv_ground;


        // --- notifie le recul VR ---
        vrRecoilNotifyShotFired(handnum);
        //---

        if (modeldef && weaponnum != WEAPON_DY357MAGNUM && weaponnum != WEAPON_DY357LX) {
            s32 partnum = MODELPART_GUN_CARTEJECTPOS;
            struct modelnode* node;

            if (weaponnum == WEAPON_REAPER) {
                partnum = (hand->burstbullets & 1) == 1 ? MODELPART_REAPER_CARTEJECTPOS1 : MODELPART_REAPER_CARTEJECTPOS2;
            }

            node = modelGetPart(modeldef, partnum);

            if (node) {
                Mtxf* mtx = (Mtxf*)allocation;
                Mtxf sp24;

                mtx += modelFindNodeMtxIndex(node, 0);

                mtx4Copy(mtx, &sp24);
                mtx00015f04(9.999999f, &sp24);
                mtx4MultMtx4InPlace(camGetProjectionMtxF(), &sp24);

                casingCreateForHand(handnum, ground, &sp24);
            }
            else {
                casingCreateForHand(handnum, ground, &hand->posmtx);
            }

            bgunSetPartVisible(MODELPART_GUN_CARTFLAPCLOSED, false, hand, modeldef);
            bgunSetPartVisible(MODELPART_GUN_CARTFLAPOPEN, true, hand, modeldef);
        }

        if (funcdef->type == INVENTORYFUNCTYPE_SHOOT_PROJECTILE) {
            chrUncloakTemporarily(g_Vars.currentplayer->prop->chr);
        }
        else if ((funcdef->type & 0xff) == INVENTORYFUNCTYPE_THROW) {
            chrUncloakTemporarily(g_Vars.currentplayer->prop->chr);
        }
    }

    if (funcdef) {
        if ((funcdef->type & 0xff) == INVENTORYFUNCTYPE_MELEE || (funcdef->type & INVENTORYFUNCTYPE_0200)) {
            createbeam = false;
        }

        if ((funcdef->type & 0xff) == INVENTORYFUNCTYPE_SPECIAL) {
            createbeam = false;
        }

        if ((funcdef->type & 0xff) == INVENTORYFUNCTYPE_THROW) {
            createbeam = false;
        }
    }

    if (createbeam) {
        switch (weaponnum) {
            case WEAPON_FALCON2:
            case WEAPON_FALCON2_SILENCER:
            case WEAPON_FALCON2_SCOPE:
            case WEAPON_MAGSEC4:
            case WEAPON_MAULER:
            case WEAPON_PHOENIX:
            case WEAPON_DY357MAGNUM:
            case WEAPON_DY357LX:
            case WEAPON_CMP150:
            case WEAPON_CYCLONE:
            case WEAPON_CALLISTO:
            case WEAPON_RCP120:
            case WEAPON_LAPTOPGUN:
            case WEAPON_DRAGON:
            case WEAPON_K7AVENGER:
            case WEAPON_AR34:
            case WEAPON_SUPERDRAGON:
            case WEAPON_REAPER:
            case WEAPON_SNIPERRIFLE:
            case WEAPON_FARSIGHT:
            case WEAPON_TRANQUILIZER:
            case WEAPON_PP9I:
            case WEAPON_CC13:
            case WEAPON_KL01313:
            case WEAPON_KF7SPECIAL:
            case WEAPON_ZZT:
            case WEAPON_DMC:
            case WEAPON_AR53:
            case WEAPON_RCP45:
                beamCreateForHand(handnum);
                hand->numfires++;
                return;
            case WEAPON_LASER:
                hand->numfires++;
                beamCreateForHand(handnum);
                break;
        }
    }
}

#ifndef PLATFORM_N64

// offset calculation from NeonNyan/perfect-dark

static inline f32 bgunGetFovOffsetZ(void)
{
    return (PLAYER_DEFAULT_FOV - 60.f) / 3.f;
}

static inline f32 bgunGetFovOffsetY(void)
{
    return (PLAYER_DEFAULT_FOV - 60.f) / (2.75f * 4.f);
}

#endif

static Mtxf g_vrScopeGunMtx[2];
static bool g_vrScopeGunMtxValid[2] = { false, false };

// VR: the animation the idle pose cache (unk0dd8) was built from: the cache outlives it, so after a
// shot the model holds the fire animation while the hand idles on the equip animation's pose
static s32 g_VrIdlePoseAnim = -1;

// VR: the Laptop Gun's idle pose is the equip animation's last frame (ANIM_GUN_LAPTOP_EQUIP, the same
// from frame 40 on), which leaves part 39 (matrix 42, a block skinned into the body's display list,
// and 43, its bottom plate) 97 units back along the body's z, and 43 a unit along -x: a gap ahead of
// the stock, around the handle. The fire and reload animations and the rest pose have them in place
// (tools/vr_measure, Task F8a; the rotations agree). A flaw of the asset the flat camera never shows,
// so it is not gated on manual reload: while the hand idles on that pose (from the cache, or live with
// the model on the animation), both are moved by the offset on the drawn body's rows. The drawn
// matrices are absolute and carry the 0.1 and bg_scale scales and the left-handed mirror (row 0
// negated), so a body-frame offset in model units goes through the body's rows as it stands.
// The unequip animation, which has a gap of its own, plays only while switching away: left.
static void vrLaptopIdlePoseFix(struct hand *hand, s32 weaponnum, bool cached, Mtxf *mtxs)
{
    static const f32 offset[2][3] = { { 0.0f, 0.0f, 97.0f }, { 1.0f, 0.0f, 97.0f } };
    Mtxf *body = &mtxs[33];
    s32 i, j;

    if (weaponnum != WEAPON_LAPTOPGUN || hand->gunmodel.definition->nummatrices <= 43
        || hand->animmode != HANDANIMMODE_IDLE
        || (cached ? g_VrIdlePoseAnim : modelGetAnimNum(&hand->gunmodel)) != ANIM_GUN_LAPTOP_EQUIP) {
        return;
    }

    for (i = 0; i < 2; i++) {
        for (j = 0; j < 3; j++) {
            mtxs[42 + i].m[3][j] += offset[i][0] * body->m[0][j] + offset[i][1] * body->m[1][j]
                + offset[i][2] * body->m[2][j];
        }
    }
}

void bgun0f0a5550(s32 handnum) {
    u8 *mtxallocation;
    Mtxf sp2c4;
    Mtxf sp284;
    struct modeldef *modeldef = NULL;
    struct coord sp274 = {0, 0, 0};
    Mtxf sp234;
    Mtxf sp1f4;
    union modelrodata *rodata;
    bool *sp1e4[3] = {NULL, NULL, NULL};
    s32 sp1e0 = 0;
    struct modelnode *node;
    struct player *player = g_Vars.currentplayer;
    struct hand *hand = player->hands + handnum;
    struct weaponfunc *funcdef;
    struct weaponfunc_shoot *shootfunc = NULL;
    s32 i;
    s32 weaponnum = bgunGetWeaponNum2(handnum);
    struct weapon *weapondef;
    Mtxf *mtx;
    bool isdetonator = false;
    f32 fspare1;
    f32 fspare2;
    struct coord sp1a4;
    Mtxf sp164;
    Mtxf sp124;
    struct coord sp118;
    s32 j;

    if (handnum >= 0 && handnum < 2) {
        g_vrScopeGunMtxValid[handnum] = false;
    }

    weapondef = weaponFindById(weaponnum);

    if (handnum == HAND_LEFT && weaponnum == WEAPON_REMOTEMINE) {
        isdetonator = true;
    }

    funcdef = gsetGetWeaponFunction2(&hand->gset);

    if (funcdef && (funcdef->type & 0xff) == INVENTORYFUNCTYPE_SHOOT) {
        shootfunc = (struct weaponfunc_shoot *) funcdef;
    }

    bgunUpdateBlend(hand, handnum);

    // Chase the torso yaw toward the head yaw. HAND_RIGHT only so the rate is one step per tick
    // regardless of how many hands/copy models are drawn. Uses the shortest-angle difference so a
    // full 360 physical spin tracks correctly instead of unwinding.
    if (handnum == HAND_RIGHT) {
        static bool sBodyYawInit = false;
        f32 w = gRawHeadQ.w, y = gRawHeadQ.y;
        f32 n = sqrtf(w*w + y*y);
        if (n > 1e-6f) {
            w /= n; y /= n;
            if (w < 0.0f) { w = -w; y = -y; }
            f32 hy = 2.0f * atan2f(y, w);
            if (!sBodyYawInit) { VrBodyYaw = hy; sBodyYawInit = true; }
            f32 d = hy - VrBodyYaw;
            while (d >  (f32)M_PI) d -= 2.0f * (f32)M_PI;
            while (d < -(f32)M_PI) d += 2.0f * (f32)M_PI;
            VrBodyYaw += d * VrArmBodyFollow;
        }
    }


#ifndef PLATFORM_N64
    // adjust viewmodel position for different FOVs
    sp274.y -= bgunGetFovOffsetY();
    sp274.z += bgunGetFovOffsetZ();
#endif

    hand->visible = true;

    if (!weaponHasFlag(weaponnum, WEAPONFLAG_00000040)
        || weaponHasFlag(weaponnum, WEAPONFLAG_00000080)
        || hand->mode == HANDMODE_6
        || hand->mode == HANDMODE_7
        || !bgunIsLoaded()
        || hand->inuse == false
        || bgunGetGunMemType() == 0) {
        hand->visible = false;
    }

    // VR: the CamSpy's drone is in the hand only until it is launched
    if (weaponnum == WEAPON_EYESPY && player->eyespy && !player->eyespy->held) {
        hand->visible = false;
    }

    // VR: a parked gun (hidden while the other gun reloads) is not drawn. bgunUpdateLasersight is
    // then not called for it, so its last laser beam and dot are freed here.
    if (vrHandParked(handnum)) {
        hand->visible = false;
        lasersightFree(handnum);
    }

    if (hand->visible) {
        modeldef = player->gunctrl.gunmodeldef;

        mtxallocation = gfxAllocate(modeldef->nummatrices * sizeof(Mtxf));

        if (weaponHasFlag(weaponnum, WEAPONFLAG_02000000)) {
            for (i = 0; i < modeldef->nummatrices; i++) {
                mtx = (Mtxf *) (mtxallocation + i * sizeof(Mtxf));
                mtx4LoadIdentity(mtx);
            }
        }

        bgunExecuteModelCmdList(hand->unk0dcc);

        if (player->gunctrl.handmodeldef != NULL) {
            bgunExecuteModelCmdList(hand->unk0dd0);
        }

        bgun0f098030(hand, modeldef);

        if (weaponHasFlag(weaponnum, WEAPONFLAG_00002000)) {
            bgun0f0981e8(hand, modeldef);
        }

    }

    mtx4LoadIdentity(&sp234);

    if (PLAYERCOUNT() == 1 && IS8MB() && weaponHasFlag(weaponnum, WEAPONFLAG_GANGSTA)) {
        bgunUpdateGangsta(hand, handnum, &sp274, funcdef, &sp284, &sp234);
    }

    if (hand->useposrot) {
        sp274.f[0] += hand->posrotmtx.m[3][0];
        sp274.f[1] += hand->posrotmtx.m[3][1];
        sp274.f[2] += hand->posrotmtx.m[3][2];

        mtx00015be0(&hand->posrotmtx, &sp234);

        sp234.m[3][0] = 0.0f;
        sp234.m[3][1] = 0.0f;
        sp234.m[3][2] = 0.0f;

    } else {
        hand->rotxoffset = 0.0f;
        hand->posoffset.x = 0.0f;
        hand->posoffset.y = 0.0f;
        hand->posoffset.z = 0.0f;
    }

    mtx00016d58(&sp284, 0.0f, 0.0f, 0.0f,
                hand->damplook.x, hand->damplook.y, hand->damplook.z,
                hand->dampup.x, hand->dampup.y, hand->dampup.z);


    mtx00015be0(&sp284, &sp234);

    sp1a4.x = 0.0f;
    sp1a4.y = M_PI;
    sp1a4.z = 0.0f;

    mtx4LoadRotation(&sp1a4, &sp164);

    sp1a4.y = 0.0f;

    bgun0f0a24f0(&sp118, handnum);

    sp1a4.y = -bgun0f0a2498(sp118.x, sp118.z, sp274.f[0], sp274.f[2]);
    sp1a4.x = bgun0f0a2498(sp118.y, sp118.z, sp274.f[1], sp274.f[2]);

    hand->lastrotangx = sp1a4.f[0];
    hand->lastrotangy = sp1a4.f[1];

    // CLEAN-SLATE TEST: also freeze the crosshair auto-aim (matches Build 18's rotation state)
    // so ALL gun rotation is frozen. Combined with identity posrotmtx, the gun cannot rotate at
    // all -> any remaining head-motion drift would be genuine POSITION drift, not rotation.
    sp1a4.x = 0.0f;
    sp1a4.y = 0.0f;

    mtx4LoadRotation(&sp1a4, &sp124);
    mtx4MultMtx4(&sp124, &sp164, &sp284);
    mtx4MultMtx4InPlace(&sp284, &sp234);
    mtx4Copy(&sp234, &sp2c4);

    mtx4SetTranslation(&sp274, &sp2c4);

    // GRIP OFFSET PLACEMENT. sp274 arrived here as FovOffset + gCtrlPos + off, where FovOffset and
    // `off` are CONSTANT view-space vectors -- so camMtx orbits them about the head and the gun swims.
    // Re-place the offset properly instead:
    //   * rotate it by hand->posrotmtx, the clean controller orientation, so it becomes a fixed vector
    //     in the CONTROLLER's frame that camMtx cancels the head out of;
    //   * negate X/Z afterwards. gCtrlQuat is stored {w,-x,y,-z}, which is exactly conjugation by a
    //     180deg Y rotation -- a change of basis. The render undoes that for the ROTATION (sp164), but
    //     the translation is written by mtx4SetTranslation afterwards and never gets the compensating
    //     180Y, so the offset would land mirrored. Measured proof: sweeping controller pitch gives
    //     pipe_pitch = 90 - true_pitch (slope exactly -1), and the mis-placement peaks at 2*|off|;
    //   * strip the FOV nudge, a flat-viewmodel hack that is wrong for a physically tracked gun.
    // Net: sp2c4.m[3] = gCtrlPos + Ry180 * Rctrl * off, which tracks 1:1 on all 6 DOF.
    {
        struct coord o = gVrGunOff[handnum];

        // Dual-wield / unarmed draw the LEFT hand as an X-mirrored copy of the right (the
        // mtx00015e24(-1) further down). The per-player X trim is measured against the RIGHT hand, so
        // on the mirrored copy it pushes the grip the wrong way. Cancel it there rather than mirror
        // it: measured, the left grip sat one trim-width off, and negating o.x outright overshot by
        // exactly 2x. The per-weapon base X is 0 for every dual-wieldable weapon, so this leaves the
        // left hand on the model's own symmetric grip point.


        int handToMirror = VrLeftHandedMode ? HAND_RIGHT : HAND_LEFT;
        if (handnum == handToMirror
            && (weaponHasFlag(weaponnum, WEAPONFLAG_DUALFLIP) || weaponnum == WEAPON_UNARMED)) {
            o.x -= VrGunOffX;
        }


        if((!weaponHasFlag(weaponnum, WEAPONFLAG_DUALFLIP)) && weaponnum != WEAPON_UNARMED && VrLeftHandedMode){
            o.x -= VrGunOffX;
        }


        Mtxf *pr = &hand->posrotmtx;
        float orx = -(o.x*pr->m[0][0] + o.y*pr->m[1][0] + o.z*pr->m[2][0]);
        float ory =   o.x*pr->m[0][1] + o.y*pr->m[1][1] + o.z*pr->m[2][1];
        float orz = -(o.x*pr->m[0][2] + o.y*pr->m[1][2] + o.z*pr->m[2][2]);
        sp2c4.m[3][0] += -o.x + orx;
        sp2c4.m[3][1] += -o.y + ory + bgunGetFovOffsetY();   // undo sp274.y -= FovOffsetY
        sp2c4.m[3][2] += -o.z + orz - bgunGetFovOffsetZ();   // undo sp274.z += FovOffsetZ
    }

    mtx4Copy(&sp2c4, &hand->cammtx);
    mtx4Copy(&hand->posmtx, &hand->prevmtx);

    mtx00015be4(camGetProjectionMtxF(), &hand->cammtx, &hand->posmtx);

    if (hand->visible) {
        for (j = 0x5a; j < 0x5d; j++) {
            node = modelGetPart(modeldef, j);

            if (node) {
                rodata = node->rodata;
                sp1e4[sp1e0] = (bool *) &hand->unk0a6c[rodata->toggle.rwdataindex];
                sp1e0++;
            }
        }


        hand->gunmodel.matrices = (Mtxf *) mtxallocation;
        hand->handmodel.matrices = (Mtxf *) mtxallocation;


        // VR - adjust weapon size according to the level's vr_world_scale
        float bg_scale = bgGetScaleBg2Gfx();
        if (bg_scale != 1.0f && bg_scale != 0.0f) {
            mtx00015f04(1.0f * bg_scale, &sp2c4);

            // 2. Correct the position relative to the camera (translation vector)
            // Reduce the offset so that vr_world_scale restores it to the correct size
            sp2c4.m[3][0] *= bg_scale; // Axe X
            sp2c4.m[3][1] *= bg_scale; // Axe Y
            sp2c4.m[3][2] *= bg_scale; // Axe Z
        }
        // ----------------------------


        // VR laser for all guns
        if (hand->visible) {
            bgunUpdateLasersight(hand, modeldef, handnum, mtxallocation);
        }


        int handToMirror = VrLeftHandedMode ? HAND_RIGHT : HAND_LEFT;

        // The Laser's model is a left arm: with holsters it is on the gun hand (vrLaserOnOffHand),
        // so it is mirrored when that is the right hand, not the left
        if (weaponnum == WEAPON_LASER && !vrLaserOnOffHand(weaponnum)) {
            handToMirror = VrLeftHandedMode ? HAND_LEFT : HAND_RIGHT;
        }

        if ((weaponHasFlag(weaponnum, WEAPONFLAG_DUALFLIP) || weaponnum == WEAPON_UNARMED) &&
            handnum == handToMirror) {
            mtx00015e24(-1, &sp2c4);
        }


        if((!weaponHasFlag(weaponnum, WEAPONFLAG_DUALFLIP)) && weaponnum != WEAPON_UNARMED && VrLeftHandedMode){
            mtx00015e24(-1, &sp2c4);
        }

        // Adjust the position of the K7 Avenger using the pivot point as well
        // Maybe TODO this for all weapons, replace code in vr_gun_pos_rot ?
        if(g_Vars.currentplayer->gunctrl.weaponnum == WEAPON_K7AVENGER) {
            sp2c4.m[3][0] -= sp2c4.m[0][0] * -3.00f + sp2c4.m[1][0] * 0.00f + sp2c4.m[2][0] * -15.00f;
            sp2c4.m[3][1] -= sp2c4.m[0][1] * -3.00f + sp2c4.m[1][1] * 0.00f + sp2c4.m[2][1] * -15.00f;
            sp2c4.m[3][2] -= sp2c4.m[0][2] * -3.00f + sp2c4.m[1][2] * 0.00f + sp2c4.m[2][2] * -15.00f;
        }

        if (handnum >= 0 && handnum < 2) {
            f32 invScale = (bg_scale != 0.0f && bg_scale != 1.0f) ? (1.0f / bg_scale) : 1.0f;
            s32 r, c;
            mtx4Copy(&sp2c4, &g_vrScopeGunMtx[handnum]);
            for (r = 0; r < 4; r++) {
                for (c = 0; c < 3; c++) {
                    g_vrScopeGunMtx[handnum].m[r][c] *= invScale;
                }
            }
            g_vrScopeGunMtxValid[handnum] = true;
        }

        mtx00015f04(0.10000001f, &sp2c4);

        // VR: a held object's model (a device, a disguise, the door decoder) centred on the grip,
        // ahead of it, and sized (g_VrHeldFits; nothing for any other weapon). The remote mine's row
        // is G5 Building's Detonator's only.
        if (weaponnum != WEAPON_REMOTEMINE || vrHolsterIsG5Detonator(weaponnum)) {
            vrHeldFitApply(weaponnum, &sp2c4);
        }

        mtx4Copy(&sp2c4, (Mtxf *) mtxallocation);

        if (hand->unk0cc8_04 > 0) {
            switch (weaponnum) {
                case WEAPON_GRENADE:
                case WEAPON_NBOMB:
                    hand->ejectstate = EJECTSTATE_INIT;
                    hand->ejecttype = EJECTTYPE_GRENADEPIN;
                    break;
                case WEAPON_TRANQUILIZER:
                    hand->ejectstate = EJECTSTATE_INIT;
                    hand->ejecttype = EJECTTYPE_TRANQCASE;
                    break;
            }
        }

        var8009d144 = hand;

        if (hand->ejectstate > EJECTSTATE_INACTIVE) {
            bgun0f0a45d0(hand, modeldef, isdetonator);
        }

        var8009d0dc = -1;
        var8009d0f0[0] = var8009d0f0[1] = var8009d0f0[2] = -1;

        switch (weaponnum) {
            case WEAPON_LASER:
                bgunUpdateLaser(hand);
                break;
            case WEAPON_REAPER:
                bgunUpdateReaper(hand, modeldef);
                break;
        }


        {
            bool a0 = true;
            struct modelrenderdata renderdata = {NULL, true, 3};
#if VERSION >= VERSION_PAL_BETA
            bool a3 = false;
#endif
            s32 spcc;
            Mtxf *spc8;
            Mtxf *spc4;
            Mtxf sp84;
            u32 sp80;
            struct coord sp74;
            s32 stack;
            s32 sp6c;

            renderdata.unk00 = &sp2c4;
            renderdata.unk10 = hand->gunmodel.matrices;

            if (hand->animmode != HANDANIMMODE_IDLE) {
                a0 = false;
            }

            switch (weaponnum) {
                case WEAPON_REAPER:
                    a0 = false;
                    break;
                case WEAPON_COMBATKNIFE:
                    if (player->hands[HAND_LEFT].loadedammo[0] == 0) {
                        a0 = false;
                    }
                    // fall through
                case WEAPON_GRENADE:
                case WEAPON_NBOMB:
                case WEAPON_TIMEDMINE:
                case WEAPON_PROXIMITYMINE:
                case WEAPON_REMOTEMINE:
                case WEAPON_ECMMINE:
                    if (player->hands[HAND_RIGHT].loadedammo[0] == 0) {
                        a0 = false;
                    }

                    if (player->hands[handnum].state == HANDSTATE_AUTOSWITCH) {
                        a0 = false;
                    }

                    if (player->hands[handnum].state == HANDSTATE_ATTACK) {
                        a0 = false;
                    }
                    break;
            }

            if (hand->ejectstate != EJECTSTATE_INACTIVE) {
                a0 = false;
            }

            if (player->hands[handnum].state == HANDSTATE_CHANGEGUN
                && player->hands[handnum].stateminor <= HANDSTATEMINOR_CHANGEGUN_LOWER
                && weapondef->unequip_animation != NULL) {
                a0 = false;
            }

#if VERSION >= VERSION_PAL_BETA
            switch (modelGetAnimNum(&hand->gunmodel)) {
                        case ANIM_GUN_CROSSBOW_EQUIP:
                        case ANIM_GUN_LAPTOP_EQUIP:
                        case ANIM_GUN_LAPTOP_UNEQUIP:
                        case ANIM_GUN_LAPTOP_RELOAD:
                        case ANIM_GUN_FALCON2_RELOAD:
                        case ANIM_GUN_CMP150_RELOAD:
                        case ANIM_GUN_FARSIGHT_SHOOT:
                        case ANIM_GUN_SHOTGUN_SHOOT_SINGLE:
                        case ANIM_GUN_REAPER_SHOOT:
                        case ANIM_GUN_MAGSEC4_RELOAD:
                        case ANIM_GUN_CYCLONE_RELOAD:
                        case ANIM_GUN_SNIPER_RELOAD:
                        case ANIM_GUN_PHOENIX_RELOAD:
                        case ANIM_GUN_FALCON2_RELOAD_SCOPE:
                        case ANIM_GUN_REMOTEMINE_EQUIP:
                                a3 = 1;
                                break;
                        }
#endif


            // VR: a model with a chr's root (the CamSpy's drone, CeyespyZ: one matrix) has no gun
            // animation to pose it, and modelUpdateChrNodeMtx would read the hand's anim (none,
            // anim 0: a human's) against the drone's skeleton. Its matrix is the hand's.
            if ((hand->gunmodel.definition->rootnode->type & 0xff) == MODELNODETYPE_CHRINFO) {
                for (spcc = 0; spcc < hand->gunmodel.definition->nummatrices; spcc++) {
                    mtx4Copy(&sp2c4, &hand->gunmodel.matrices[spcc]);
                }
            } else if (a0) {
                if (player->hands[HAND_RIGHT].unk0dd4 == -1) {
                    mtx4LoadIdentity(&sp84);

                    spc4 = hand->gunmodel.matrices;

                    renderdata.unk00 = &sp84;
                    renderdata.unk10 = player->hands[HAND_RIGHT].unk0dd8;


#if VERSION >= VERSION_PAL_BETA
                    var8005efd8_2 = true;

                    if (a3) {
                        var8005efb0_2 = true;
                    }

                    modelSetMatricesWithAnim(&renderdata, &hand->gunmodel);

                    var8005efd8_2 = false;

                    if (a3) {
                        var8005efb0_2 = false;
                    }
#else
                    modelSetMatricesWithAnim(&renderdata, &hand->gunmodel);
#endif

                    player->hands[HAND_RIGHT].unk0dd4 = 1;
                    g_VrIdlePoseAnim = modelGetAnimNum(&hand->gunmodel);

                    hand->gunmodel.matrices = spc4;


                }

                spc8 = player->hands[HAND_RIGHT].unk0dd8;
                spc4 = hand->gunmodel.matrices;

                for (spcc = 0; spcc < hand->gunmodel.definition->nummatrices; spcc++) {
                    mtx00015be4(&sp2c4, spc8, spc4);
                    spc8++;
                    spc4++;
                }
            } else {

#if VERSION >= VERSION_PAL_BETA
                var8005efd8_2 = true;

                if (a3) {
                    var8005efb0_2 = true;
                }

                modelSetMatricesWithAnim(&renderdata, &hand->gunmodel);

                var8005efd8_2 = false;

                if (a3) {
                    var8005efb0_2 = false;
                }
#else
                modelSetMatricesWithAnim(&renderdata, &hand->gunmodel);
#endif


            }

            g_ModelJointPositionedFunc = 0;

            // VR: G5 Building's Detonator alone on the grip (vrG5DetonatorPlace)
            if (vrHolsterIsG5Detonator(weaponnum)) {
                vrG5DetonatorPlace((Mtxf *) mtxallocation, hand->gunmodel.definition->nummatrices, &sp2c4);
            }

            node = modelGetPart(modeldef, MODELPART_GUN_SLIDE);

            if (node) {
                sp80 = modelFindNodeMtxIndex(node, 0);

                bgunUpdateSlide(handnum);

                sp74.f[0] = 0.0f;
                sp74.f[1] = 0.0f;
                sp74.f[2] = -hand->slidetrans;

                mtx = (Mtxf *) mtxallocation;
                mtx += sp80;


                mtx4RotateVecInPlace(mtx, &sp74);

                mtx->m[3][0] += sp74.f[0];
                mtx->m[3][1] += sp74.f[1];
                mtx->m[3][2] += sp74.f[2];
            }

            // VR: the Laptop Gun's idle pose with its block and bottom plate put back in place, first
            // of the VR moves, so the reload's capture and playbacks see the gun as drawn
            vrLaptopIdlePoseFix(hand, weaponnum, a0, (Mtxf *) mtxallocation);

            // Manual reload: a clip drawn at its row's seat on the body (the Laptop Gun's, under the
            // front half, where its own matrix sits in the grip outside the reload), first, so every
            // read and move of the clip below starts there.
            vrReloadSeatClip(handnum, modeldef, (Mtxf *) mtxallocation);
            // Manual reload: a holder the clip sits in (the Tranquilizer's vial) drawn open while the
            // clip is out, before the capture, so the well is taken from the open holder.
            vrReloadPoseHolder(handnum, modeldef, (Mtxf *) mtxallocation);
            // Manual reload: after an insert the gun plays the rest of its reload animation on its own
            // parts (the Phoenix's orb pressed in and its side plates), and after B a row's eject
            // range (the Laptop Gun's pump), posed relative to the gun body, so the body and the hands
            // stay on the controller. Before the capture, as the holder.
            // The driven magazine's reload animation (the selected function's row: Task F9e)
            struct vrreloaddef *vrPlayDef = vrReloadOwnedDef(handnum);
            bool vrPlayHeldShown = vrReloadPosePlay(handnum, &hand->gunmodel,
                    vrPlayDef != NULL && hand->gset.weaponnum >= 0 && hand->gset.weaponnum < NUM_WEAPONS
                    ? VR_ANIM_ID(hand->gset.weaponnum, vrPlayDef->ammoindex) : -1);
            // Manual reload: a rack on a part with no slide of its own (the RC-P120's knob) drawn
            // pulled back with the hand, as the slide offset above moves a slide. Before the capture.
            vrReloadMoveRack(handnum, modeldef, (Mtxf *) mtxallocation);
            // Manual reload grab areas: record the clip and slide parts as drawn (slide offset
            // included, before anything below hides or moves parts) for the next gameplay ticks.
            vrReloadCaptureParts(handnum, modeldef, (Mtxf *) mtxallocation, bg_scale);
            // A clip sliding into the well: moved after the capture, so the well stays put.
            vrReloadPlaceClip(handnum, modeldef, (Mtxf *) mtxallocation);
            // An ejected clip falling out of the gun: replaces the clip matrix, so it comes after the slide in.
            vrReloadDropClip(handnum, modeldef, (Mtxf *) mtxallocation, bg_scale);
            // A clip in the gun drawn narrower than modelled where the grip is thinner: after the
            // eject record above, so a dropped clip falls at full width.
            vrReloadNarrowClip(handnum, modeldef, (Mtxf *) mtxallocation);
            // No clip drawn in the gun: its matrix collapsed, so a face of the clip on another drawn
            // part would go with it (harmless for part 0x42, the CMP150's clip base, which
            // bgun0f098030 hides on every frame). Last of the clip's moves.
            vrReloadCollapseClip(handnum, modeldef, (Mtxf *) mtxallocation);


            if (sp1e4[0] != NULL) {
                *sp1e4[0] = false;
            }

            if (sp1e4[1] != NULL) {
                *sp1e4[1] = false;
            }

            if (sp1e4[2] != NULL) {
                *sp1e4[2] = false;
            }




            s32 currentWeapon = player->hands[HAND_RIGHT].gset.weaponnum;
            bool CrossbowLaserUnarmed =
                    currentWeapon == WEAPON_CROSSBOW || currentWeapon == WEAPON_LASER || (!VrMotionThrowing && currentWeapon == WEAPON_UNARMED);
            if (!CrossbowLaserUnarmed) {
                // bgun0f0a5550 runs once per hand, so the primary arm side follows handnum:
                // RIGHT hand's gun -> +1 (its arm on the gun-hand side), LEFT hand's gun -> -1 (the other side).
                vr_wrist_rot(modeldef, hand->gunmodel.matrices, (handnum == HAND_RIGHT) ? 1.0f : -1.0f);
            }

            if (vrSwitchGun == true && modeldef != NULL) {
                vrBuildHandIndexList(player->gunctrl.handmodeldef);
                vrBuildMtxPartsList(hand, player->gunctrl.gunmodeldef, true);
                vrSwitchGun = false;
            }

            if (VrTwoHandsGun(g_Vars.currentplayer->gunctrl.weaponnum)) {
                bool vrFreeGrip = get_button_state(0, "grip");

                // Manual reload: the free hand's grip also takes and holds a clip (the AR34 and the K7
                // Avenger are two-handed guns with reload rows). A hand holding one is not the support
                // hand: with the grip counted, the gun would draw its own left hand on the forend
                // beside the copy model's hand with the clip, and aim along the line to the clip hand.
                // The grip stays off the gun until it is let go, as a clip pushed in to seated inserts
                // with the grip still held and the hand at the clip, behind the pistol grip. A hand
                // racking (the RC-P120's knob, gripped and pulled) is not the support hand either: the
                // core ends the rack as the pull completes, and the latch keeps that grip off the gun
                // until it is let go. Nor is a grip while the gun waits for that rack, from its press
                // (Task F7b: the two-hand aim turned the gun, and the knob with it, before the core
                // took the grab, so the rack never started), nor while it waits for its slap (Task F8d:
                // the Laptop Gun was aimed with both hands with the slap still due), nor anywhere in a
                // reload (Task F9a: after B the Laptop Gun could still be aimed with both hands):
                // vrReloadMidReload covers them all, from B to ready; a gun fired dry with its clip in
                // and a ready gun keep the grip as support. The Shotgun's pump is the exception
                // (vrReloadPumpHand, Task G0c): the support hand pumps the forend it holds, which slides
                // along the barrel, so the grip stays the support and the two-hand aim stays on.
                if ((vrReloadFreeHandBusy(HAND_RIGHT) && !vrReloadPumpHand(HAND_RIGHT))
                        || (vrFreeGrip && vrReloadMidReload(HAND_RIGHT))) {
                    VrTwoHandReloadGrip = true;
                } else if (!vrFreeGrip) {
                    VrTwoHandReloadGrip = false;
                }

                VrTwoHandGrip = vrFreeGrip && !VrTwoHandReloadGrip;
            }

            if (weaponnum == WEAPON_REMOTEMINE || weaponnum == WEAPON_LASER) {
                vrBuildMtxPartsList(hand, player->gunctrl.gunmodeldef, false);

            } else if (VrTwoHandGrip && VrTwoHandsGun(g_Vars.currentplayer->gunctrl.weaponnum)) {
                // VR: two-handed grip - keep the gun visible even if the copy-weapon
                // path left parts hidden (e.g. after a reload or weapon switch, issue #43).
                for (s32 i = 0; i < s_gunMovableCount; i++) s_vrPartHidden[i] = false;
                vrHideGunParts(hand->gunmodel.matrices, handnum);
            } else {
                if (hand->state != HANDSTATE_RELOAD && VrTwoHandsGun(weaponnum)) {
                    vrHideOnly(LeftHandMtx);
                    vrHideGunParts(hand->gunmodel.matrices, handnum);
                }
                vrBuildMtxPartsList(hand, player->gunctrl.gunmodeldef, false);
            }

            // Only for a gun model with hands (vrModelHasHands, as vr_wrist_rot): the handless classic
            // guns have no arms, and their matrix 1 is another part (the muzzle position). The fists
            // have arms though they load as the gun model with no hands model (unarmed)
            if(VrHideArms && vrModelHasHands(player->gunctrl.gunmodeldef)) {
                vrHideOnly(ArmsMtx);
                vrHideGunParts(hand->handmodel.matrices, handnum);
            }


            bool FALCON2S = g_Vars.currentplayer->gunctrl.weaponnum == WEAPON_FALCON2
                            || g_Vars.currentplayer->gunctrl.weaponnum == WEAPON_FALCON2_SILENCER
                            || g_Vars.currentplayer->gunctrl.weaponnum == WEAPON_FALCON2_SCOPE;

            // The gunviscmds (bgun0f098030, earlier this frame) hide the clip part of the Falcon 2
            // and the MagSec 4 (the Mauler's, the Tranquilizer's and the CMP150's show by default),
            // so an owned gun's clip is shown or hidden here from the manual reload state, or the
            // gun draws with no clip. A gun it does not own keeps the engine's visibility (the
            // Falcon 2 and MagSec 4: no clip at idle, the reload script's clip during a reload).
            // The Falcon 2 alone is the exception: its clip always shows (the VR port's look before
            // manual reload).
            struct vrreloaddef *def = vrReloadOwnedDef(handnum); /* NULL unless this hand's gun is owned */
            if (def) {
                // A row with no clip part (the Phoenix) has nothing to draw in the gun
                if (def->clippart >= 0) {
                    bgunSetPartVisible(def->clippart,
                            vrReloadClipShownInGun(handnum) || vrReloadClipFalling(handnum), hand, modeldef);
                }
                // The off-hand's clip in this model hangs off its left wrist, which the idle pose
                // puts far from the gun (the Tranquilizer's vial, about 60 cm below it): the
                // gunviscmds hide the other rows' but not the Tranquilizer's, which only the
                // engine's reload hides, so it is hidden here. The copy-weapon model, with its own
                // part states, draws the clip in the off-hand. The exceptions are the playback of
                // an insert (the Phoenix's orb pressed in), which posed it on the gun above, and, for
                // a row with no clip part, its fall after the playback (the Cyclone's bar). Every part
                // of a held part made of several (the DY357's six rounds) goes together.
                for (s32 vrhp = def->heldpart; vrhp <= vrReloadHeldPartLast(def); vrhp++) {
                    bgunSetPartVisible(vrhp,
                            vrPlayHeldShown || (def->clippart < 0 && vrReloadClipFalling(handnum)), hand, modeldef);
                }

                // The gun's other magazine, the one the selected function does not use (Task F9e: the
                // SuperDragon's grenade magazine with the rifle selected, or the rifle clip with the
                // grenade launcher), is drawn in or out as it was left; its hand part stays hidden
                bool vrParkedIn = true;
                struct vrreloaddef *vrParked = vrReloadParkedDef(handnum, &vrParkedIn);

                if (vrParked) {
                    if (vrParked->clippart >= 0) {
                        bgunSetPartVisible(vrParked->clippart, vrParkedIn, hand, modeldef);
                    }

                    for (s32 vrhp = vrParked->heldpart; vrhp <= vrReloadHeldPartLast(vrParked); vrhp++) {
                        bgunSetPartVisible(vrhp, false, hand, modeldef);
                    }
                }
            } else if (FALCON2S) {
                // Not driven by manual reload (manual reload off, cheat): always show
                // the clip, as the old block did whenever its own reload was idle. Falcon only:
                // the other rows' guns look as in the original game with manual reload off.
                bgunSetPartVisible(MODELPART_FALCON2_MAGAZINE1, true, hand, modeldef);
            }



            switch (weaponnum) {
                case WEAPON_SNIPERRIFLE:
                    bgunUpdateSniperRifle(modeldef, mtxallocation);
                    break;
                case WEAPON_DEVASTATOR:
                    bgunUpdateDevastator(hand, mtxallocation, modeldef);
                    break;
                case WEAPON_SHOTGUN:
                    bgunUpdateShotgun(hand, mtxallocation, sp1e4[0], modeldef);
                    break;
            }

            node = modelGetPart(modeldef, MODELPART_GUN_MUZZLEPOS);

            if (weaponnum == WEAPON_REAPER) {
                if (hand->flashon || hand->firing) {
                    node = modelGetPart(modeldef, MODELPART_REAPER_001E + (hand->burstbullets % 3));
                } else {
                    node = modelGetPart(modeldef, MODELPART_REAPER_001E + (g_Vars.lvframenum % 3));
                }
            }

            if (node) {
                f32 invBgScale = (bg_scale != 0.0f && bg_scale != 1.0f) ? (1.0f / bg_scale) : 1.0f;
                sp6c = modelFindNodeMtxIndex(node, 0);

                mtx = (Mtxf *) mtxallocation;
                mtx += sp6c;

                hand->muzzlepos.f[0] = mtx->m[3][0] * invBgScale;
                hand->muzzlepos.f[1] = mtx->m[3][1] * invBgScale;
                hand->muzzlepos.f[2] = mtx->m[3][2] * invBgScale;

                mtx4Copy(mtx, &hand->muzzlemat);
                mtx4TransformVecInPlace(camGetProjectionMtxF(), &hand->muzzlepos);

                hand->muzzlez = -((Mtxf *) ((uintptr_t) mtxallocation +
                                            sp6c * sizeof(Mtxf)))->m[3][2];

                if (hand->flashon && sp1e0 > 0 && weaponnum != WEAPON_SHOTGUN &&
                    g_Vars.lvupdate240 != 0) {
                    bgun0f0a4e44(hand, weapondef, modeldef, funcdef, sp1e0, mtxallocation,
                                 weaponnum, sp1e4, sp6c, &sp234, &sp1f4);
                }
            } else if (weaponnum == WEAPON_GRENADE
                       || weaponnum == WEAPON_TIMEDMINE
                       || weaponnum == WEAPON_REMOTEMINE
                       || weaponnum == WEAPON_PROXIMITYMINE
                       || weaponnum == WEAPON_NBOMB) {
                f32 invBgScale = (bg_scale != 0.0f && bg_scale != 1.0f) ? (1.0f / bg_scale) : 1.0f;
                sp6c = modelFindNodeMtxIndex(modelGetPart(modeldef, MODELPART_GUN_HOLDPOS), 0);

                mtx = (Mtxf *) mtxallocation;
                mtx += sp6c;

                hand->muzzlepos.x = mtx->m[3][0] * invBgScale;
                hand->muzzlepos.y = mtx->m[3][1] * invBgScale;
                hand->muzzlepos.z = mtx->m[3][2] * invBgScale;

                mtx4Copy(mtx, &hand->muzzlemat);
                mtx4TransformVecInPlace(camGetProjectionMtxF(), &hand->muzzlepos);

                hand->muzzlez = -((Mtxf *) ((uintptr_t) mtxallocation +
                                            sp6c * sizeof(Mtxf)))->m[3][2];
            } else {
                hand->muzzlepos.x = hand->posmtx.m[3][0];
                hand->muzzlepos.y = hand->posmtx.m[3][1];
                hand->muzzlepos.z = hand->posmtx.m[3][2];

                mtx4Copy(&hand->posmtx, &hand->muzzlemat);

                hand->muzzlez = -hand->cammtx.m[3][2];
            }
        }


    } else {
        hand->muzzlepos.x = hand->posmtx.m[3][0];
        hand->muzzlepos.y = hand->posmtx.m[3][1];
        hand->muzzlepos.z = hand->posmtx.m[3][2];

        mtx4Copy(&hand->posmtx, &hand->muzzlemat);

        hand->muzzlez = -hand->cammtx.m[3][2];
    }


    // VR manual reload: the spent rounds' casings fall from the swung-out chambers at the end of B's
    // swing (a row with ejectcasings: the DY357s), through the engine's own path for the reload
    // script's popoutsackofpills, which it counts in unk0cc8_04 for this draw. Taken once, and the
    // count cleared after, so they fall once (a hand not drawn has no model: none fall)
    s32 vrCasings = vrReloadTakeCasings(handnum);

    if (vrCasings > 0) {
        hand->unk0cc8_04 = vrCasings;
    }

    switch (weaponnum) {
        case WEAPON_ROCKETLAUNCHER:
            bgunUpdateRocketLauncher(hand, handnum, (struct weaponfunc_shootprojectile *) funcdef);
            break;
        case WEAPON_DY357MAGNUM:
        case WEAPON_DY357LX:
            if (hand->unk0cc8_04 > 0) {
                bgunUpdateMagnum(hand, handnum, modeldef, (Mtxf *) mtxallocation);
            }
            break;
    }

    if (vrCasings > 0) {
        hand->unk0cc8_04 = 0;
    }

    if (hand->firing && g_Vars.lvupdate240 != 0) {
        bgunCreateFx(hand, handnum, funcdef, weaponnum, modeldef, mtxallocation);
    }

    if (PLAYERCOUNT() == 1 && IS8MB() && g_Vars.lvupdate240 != 0) {
        bgunUpdateSmoke(hand, handnum, weaponnum, funcdef);
    }

    if (hand->ejectstate > EJECTSTATE_INACTIVE) {
        bgunTickEject(hand, modeldef, isdetonator);
    }

    if (PLAYERCOUNT() == 1 && IS8MB() && hand->visible
        && weaponnum >= WEAPON_FALCON2 && weaponnum <= WEAPON_FALCON2_SCOPE) {
        bgunUpdateLasersight(hand, modeldef, handnum, mtxallocation);
    }

//    else { // Deletes for VR
//        lasersightFree(handnum);
//    }



    hand->animframeinc = 0;


#if VERSION >= VERSION_PAL_BETA
    hand->animframeincfreal = 0;
#endif
}

void bgunTickMaulerCharge(void)
{
    struct player* player = g_Vars.currentplayer;
    s32 i;

    for (i = 0; i < 2; i++) {
        struct hand* hand = &player->hands[i];
        u32 charging = false;

        if (hand->inuse) {
            if (bgunIsReloading(hand)) {
                // Reloading - reset charge amount
                hand->matmot1 = 0;
            }
            else if (hand->gset.weaponfunc == FUNC_SECONDARY) {
                // Charging or fully charged
                s32 oldvalue = hand->matmot1;
                s32 newvalue;

                if (hand->loadedammo[0] >= 2 && hand->matmot1 < 5) {
                    charging = true;
                    hand->matmot1 += g_Vars.lvupdate60freal * 0.05f;
                }

                if (hand->matmot1 > 5) {
                    hand->matmot1 = 5;
                }

                newvalue = hand->matmot1;

                if (oldvalue != newvalue && hand->loadedammo[0] >= 2) {
                    hand->loadedammo[0]--;
                }
            }
            else {
                // Using primary function - make the charge wear off slowly
                hand->matmot1 -= g_Vars.lvupdate60freal * 0.005f;

                if (hand->matmot1 < 0) {
                    hand->matmot1 = 0;
                }
            }

            /**
						 * Probable @bug: In other places where audio is started and then
						 * its speed is adjusted, the game raises the priority of the main
						 * thread (this thread) to above the audio thread's priority so that
						 * the audio thread cannot execute and start playing the audio
						 * between the calls to sndStart and audioPostEvent. But this pattern
						 * is not done here.
						 *
						 * There is a known issue where the Mauler charge sound is played
						 * correctly then the game proceeds to play other unrelated sound
						 * effects before eventually crashing. It's suspected that this lack
						 * of thread priority adjusting is the root cause. Perhaps a race
						 * condition exists where the audio thread does something with the
						 * sound while this thread is in the middle of reconfiguring it.
						 * This is not yet confirmed.
						 */
            if (hand->audiohandle == NULL
                && hand->matmot1 > 0.1f
                && charging
                && g_Vars.lvupdate240 != 0) {
                sndStart(var80095200, SFX_MAULER_CHARGE, &hand->audiohandle, -1, -1, -1, -1, -1);
            }

            if (hand->audiohandle) {
                f32 speed = 0.5f + hand->matmot1 / 3.0f + sinf(g_20SecIntervalFrac * M_PI * 32.0f) * 0.03f;

                if (hand->matmot1 < 0.1f || !charging) {
                    audioStop(hand->audiohandle);
                }
                else {
                    audioPostEvent(hand->audiohandle, AL_SNDP_PITCH_EVT, *(s32*)&speed);
                }
            }
        }
    }
}

void bgunTickGameplay2(void)
{
    struct player* player = g_Vars.currentplayer;
    struct hand* hand;
    u32 stack[3];
    s32 i;

#if VERSION >= VERSION_NTSC_1_0
    if (g_Vars.currentplayernum == 0) {
        projectilesDebug();
    }
#endif

    if (player->gunctrl.loadall) {
        // empty
    }
    else {
        bgunTickLoad();
    }

    // Return control to Jo if eyespy has been deselected
    if ((g_Vars.currentplayer->devicesactive & ~g_Vars.currentplayer->devicesinhibit & DEVICE_EYESPY) == 0
        && player->eyespy) {
        player->eyespy->active = false;
    }

    if ((g_Vars.currentplayer->devicesactive & ~g_Vars.currentplayer->devicesinhibit & DEVICE_XRAYSCANNER)
        && (bgunGetWeaponNum(HAND_RIGHT) != WEAPON_FARSIGHT || player->gunsightoff)) {
        // Using normal xray scanner (not Farsight zoom)
        if (player->visionmode != VISIONMODE_XRAY) {
            player->erasertime = 0;
        }
        else {
            player->erasertime += g_Vars.lvupdate240;
        }

        player->visionmode = VISIONMODE_XRAY;
        player->ecol_1 = 24;
        player->ecol_2 = 8;
        player->ecol_3 = 24;
        player->epcol_0 = 2;
        player->epcol_1 = 0;
        player->epcol_2 = 1;
    }
    else {
        if (player->gunsightoff == 0) {
            if (player->hands[HAND_RIGHT].gset.weaponnum == WEAPON_FARSIGHT) {
                // Aiming with the Farsight
                if (player->visionmode != VISIONMODE_XRAY) {
                    player->erasertime = 0;
                }
                else {
                    player->erasertime += g_Vars.lvupdate240;
                }

                player->visionmode = VISIONMODE_XRAY;
                player->ecol_1 = 16;
                player->ecol_2 = 24;
                player->ecol_3 = 8;
                player->epcol_0 = 0;
                player->epcol_1 = 1;
                player->epcol_2 = 2;
            }
            else {
                // Aiming with non-Farsight
                if (player->visionmode != VISIONMODE_SLAYERROCKET) {
                    player->visionmode = VISIONMODE_NORMAL;
                }
            }
        }
        else {
            // Not aiming
            if (player->visionmode != VISIONMODE_SLAYERROCKET) {
                player->visionmode = VISIONMODE_NORMAL;
            }
        }
    }

    if (player->gunctrl.weaponnum == WEAPON_MAULER) {
        bgunTickMaulerCharge();
    }

    if (g_Vars.lvupdate240 == 0) {
        for (i = 0; i < 2; i++) {
            hand = &player->hands[i];

            if (hand->audiohandle) {
                audioStop(hand->audiohandle);
            }
        }
    }

    if (g_Vars.currentplayer->devicesactive &
        ~g_Vars.currentplayer->devicesinhibit & DEVICE_CLOAKRCP120) {
        if (player->gunctrl.weaponnum == WEAPON_RCP120) {
            struct chrdata* chr = player->prop->chr;

            // Handle RCP120 cloak ammo usage
            if ((chr->hidden & CHRHFLAG_CLOAKED) && chr->cloakfadefinished == true) {
                hand = &player->hands[HAND_RIGHT];
                hand->matmot1 += LVUPDATE60FREAL() * 0.4f;

                if (hand->matmot1 > 1.0f) {
                    s32 usedqty = hand->matmot1;

                    if (usedqty > hand->loadedammo[0]) {
                        usedqty = hand->loadedammo[0];
                    }

                    hand->matmot1 -= usedqty;
                    hand->loadedammo[0] -= usedqty;

                    // If out of ammo, turn off cloak. VR: manual reload never refills the clip by
                    // itself, so the empty clip ends it whatever the reserve (it stayed on for free,
                    // and the trigger, a dry click now, could not turn it off: Task J8)
                    if (hand->loadedammo[0] == 0 && hand->state != HANDSTATE_RELOAD) {
                        s32 stilltogo = hand->matmot1;

                        if (stilltogo > player->ammoheldarr[player->gunctrl.ammotypes[0]]
                                || vrReloadOwnsAmmo(HAND_RIGHT, 0)) {
                            g_Vars.currentplayer->devicesactive &= ~DEVICE_CLOAKRCP120;
                        }
                    }
                }
            }
        }
        else {
            // No longer using RCP120, so turn off cloak
            player->devicesactive &= ~DEVICE_CLOAKRCP120;
        }
    }
    else if (player->gunctrl.weaponnum == WEAPON_RCP120) {
        hand = &player->hands[HAND_RIGHT];

        // I think this is handling situations where the player has turned off
        // RCP120 cloak but there's still a bit of ammo to be subtracted on
        // this tick.
        if (hand->matmot1 > 1.0f) {
            s32 usedqty = hand->matmot1;

            if (usedqty > hand->loadedammo[0]) {
                usedqty = hand->loadedammo[0];
            }

            hand->matmot1 -= usedqty;
            hand->loadedammo[0] -= usedqty;

            if (hand->matmot1 > 1.0f) {
                s32 usedqty = hand->matmot1;

                if (usedqty > player->ammoheldarr[player->gunctrl.ammotypes[0]]) {
                    usedqty = player->ammoheldarr[player->gunctrl.ammotypes[0]];
                }

                player->ammoheldarr[player->gunctrl.ammotypes[0]] -= usedqty;
                hand->matmot1 = 0;
            }
        }
    }

    bgunTickUnequippedReload();
    bgun0f0a5550(HAND_RIGHT);


    if (player->hands[HAND_LEFT].inuse) {
        bgun0f0a5550(HAND_LEFT);
    }
    else {
        player->hands[HAND_LEFT].ejectstate = EJECTSTATE_INACTIVE;

        // VR: a hand out of use is not drawn. A holster switch can take the left hand out of use
        // in the tick it starts, with no frame computed in the load, so its visibility from its
        // last frame in use (fists) would stand while its model is the new gun's.
        if (vrHolsterFastSwitch()) {
            player->hands[HAND_LEFT].visible = false;
        }
    }

    bgunIsUsingSecondaryFunction();
}

s8 bgunFreeFireslotWrapper(s32 slotnum)
{
#if VERSION < VERSION_NTSC_1_0
    if (slotnum >= 0) {
				if (g_Fireslots[slotnum].unk04nb && sndGetState(g_Fireslots[slotnum].unk04nb) != AL_STOPPED) {
						audioStop(g_Fireslots[slotnum].unk04nb);
				}

				if (g_Fireslots[slotnum].unk08nb && sndGetState(g_Fireslots[slotnum].unk08nb) != AL_STOPPED) {
						audioStop(g_Fireslots[slotnum].unk08nb);
				}
		}
#endif

    return bgunFreeFireslot(slotnum);
}

s8 bgunFreeFireslot(s32 fireslot_id)
{
#if VERSION >= VERSION_NTSC_1_0
    if (fireslot_id >= 0 && fireslot_id < ARRAYCOUNT(g_Fireslots)) {
        g_Fireslots[fireslot_id].endlvframe = -1;
    }
#else
    if (fireslot_id >= 0) {
				g_Fireslots[fireslot_id].endlvframe = -1;
		}
#endif

    return -1;
}

s32 bgunAllocateFireslot(void)
{
    s32 index = -1;
    s32 i;

    for (i = 0; i < ARRAYCOUNT(g_Fireslots); i++) {
        if (g_Fireslots[i].endlvframe < 0) {
            g_Fireslots[i].endlvframe = 0;

#if VERSION < VERSION_NTSC_1_0
            g_Fireslots[i].unk04nb = 0;
						g_Fireslots[i].unk08nb = 0;
#endif

            g_Fireslots[i].beam.age = -1;
            index = i;
            break;
        }
    }

    return index;
}

u32 var8007029c = 0x00000000;
u32 var800702a0 = 0x00000001;
u32 var800702a4 = 0x00000003;
u32 var800702a8 = 0x00000000;
u32 var800702ac = 0x00000000;
u32 var800702b0 = 0x00000000;
u32 var800702b4 = 0x00000000;
u32 var800702b8 = 0x00000000;
u32 var800702bc = 0x00000000;
u32 var800702c0 = 0x00000000;
u32 var800702c4 = 0x00000000;
u32 var800702c8 = 0x00000000;
u32 var800702cc = 0x00000000;
u32 var800702d0 = 0x00000000;
u32 var800702d4 = 0x00000000;
u32 var800702d8 = 0x00000000;
u32 var800702dc = 0x00000001;

#if MATCHING
#if PAL
GLOBAL_ASM(
		glabel bgunRender
		.late_rodata
		glabel var7f1aca8c
		.word 0x3faaaaab
		glabel var7f1aca90
		.word 0x3f3ebebf
		.text
		/*  f0a7138:	27bdfeb0 */ 	addiu	$sp, $sp, -336
		/*  f0a713c:	afbf0034 */ 	sw	$ra, 0x34($sp)
		/*  f0a7140:	afb50030 */ 	sw	$s5, 0x30($sp)
		/*  f0a7144:	afb4002c */ 	sw	$s4, 0x2c($sp)
		/*  f0a7148:	afb30028 */ 	sw	$s3, 0x28($sp)
		/*  f0a714c:	afb20024 */ 	sw	$s2, 0x24($sp)
		/*  f0a7150:	afb10020 */ 	sw	$s1, 0x20($sp)
		/*  f0a7154:	afb0001c */ 	sw	$s0, 0x1c($sp)
		/*  f0a7158:	afa40150 */ 	sw	$a0, 0x150($sp)
		/*  f0a715c:	8c8f0000 */ 	lw	$t7, 0x0($a0)
		/*  f0a7160:	3c198007 */ 	lui	$t9, % hi(var8007029c)
		/*  f0a7164:	3c11800a */ 	lui	$s1, % hi(g_Vars)
		/*  f0a7168:	2739029c */ 	addiu	$t9, $t9, % lo(var8007029c)
		/*  f0a716c:	26319fc0 */ 	addiu	$s1, $s1, % lo(g_Vars)
		/*  f0a7170:	272a003c */ 	addiu	$t2, $t9, 0x3c
		/*  f0a7174:	27b8010c */ 	addiu	$t8, $sp, 0x10c
		/*  f0a7178:	afaf014c */ 	sw	$t7, 0x14c($sp)
		.L0f0a717c:
/*  f0a717c:	8f210000 */ 	lw	$at, 0x0($t9)
/*  f0a7180:	2739000c */ 	addiu	$t9, $t9, 0xc
/*  f0a7184:	2718000c */ 	addiu	$t8, $t8, 0xc
/*  f0a7188:	af01fff4 */ 	sw	$at, -0xc($t8)
/*  f0a718c:	8f21fff8 */ 	lw	$at, -0x8($t9)
/*  f0a7190:	af01fff8 */ 	sw	$at, -0x8($t8)
/*  f0a7194:	8f21fffc */ 	lw	$at, -0x4($t9)
/*  f0a7198:	172afff8 */ 	bne	$t9, $t2, .L0f0a717c
/*  f0a719c:	af01fffc */ 	sw	$at, -0x4($t8)
/*  f0a71a0:	8f210000 */ 	lw	$at, 0x0($t9)
/*  f0a71a4:	af010000 */ 	sw	$at, 0x0($t8)
/*  f0a71a8:	8e330284 */ 	lw	$s3, 0x284($s1)
/*  f0a71ac:	24010001 */ 	addiu	$at, $zero, 0x1
/*  f0a71b0:	966b0010 */ 	lhu	$t3, 0x10($s3)
/*  f0a71b4:	1561000d */ 	bne	$t3, $at, .L0f0a71ec
/*  f0a71b8:	00001025 */ or $v0, $zero, $zero
/*  f0a71bc:	24040f48 */ 	addiu	$a0, $zero, 0xf48
/*  f0a71c0:	8e2c0284 */ 	lw	$t4, 0x284($s1)
.L0f0a71c4:
/*  f0a71c4:	01821821 */ 	addu	$v1, $t4, $v0
/*  f0a71c8:	8c6d0854 */ 	lw	$t5, 0x854($v1)
/*  f0a71cc:	244207a4 */ 	addiu	$v0, $v0, 0x7a4
/*  f0a71d0:	11a00002 */ 	beqz	$t5, .L0f0a71dc
/*  f0a71d4:	00000000 */ 	nop
/*  f0a71d8:	ac600850 */ 	sw	$zero, 0x850($v1)
.L0f0a71dc:
/*  f0a71dc:	5444fff9 */ 	bnel	$v0, $a0, .L0f0a71c4
/*  f0a71e0:	8e2c0284 */ 	lw	$t4, 0x284($s1)
/*  f0a71e4:	100002d5 */ 	b.L0f0a7d3c
/*  f0a71e8:	8fbf0034 */ 	lw	$ra, 0x34($sp)
.L0f0a71ec :
		/*  f0a71ec:	0fc5d9ad */ 	jal	zbufSaveArtifactDepths
		/*  f0a71f0:	8fa4014c */ 	lw	$a0, 0x14c($sp)
		/*  f0a71f4:	afa2014c */ 	sw	$v0, 0x14c($sp)
		/*  f0a71f8:	0c002ca0 */ 	jal	viPrepareZbuf
		/*  f0a71fc:	00402025 */ or $a0, $v0, $zero
		/*  f0a7200:	afa2014c */ 	sw	$v0, 0x14c($sp)
		/*  f0a7204:	0c002c74 */ 	jal	vi0000b1d0
		/*  f0a7208:	00402025 */ or $a0, $v0, $zero
		/*  f0a720c:	244e0008 */ 	addiu	$t6, $v0, 0x8
		/*  f0a7210:	afae014c */ 	sw	$t6, 0x14c($sp)
		/*  f0a7214:	0c002f40 */ 	jal	viGetViewLeft
		/*  f0a7218:	0040a825 */ or $s5, $v0, $zero
		/*  f0a721c:	00028400 */ 	sll	$s0, $v0, 0x10
		/*  f0a7220:	00107c03 */ 	sra	$t7, $s0, 0x10
		/*  f0a7224:	0c002f44 */ 	jal	viGetViewTop
		/*  f0a7228:	01e08025 */ or $s0, $t7, $zero
		/*  f0a722c:	44822000 */ 	mtc1	$v0, $f4
		/*  f0a7230:	44908000 */ 	mtc1	$s0, $f16
		/*  f0a7234:	3c014080 */ 	lui	$at, 0x4080
		/*  f0a7238:	468021a0 */ 	cvt.s.w	$f6, $f4
		/*  f0a723c:	44810000 */ 	mtc1	$at, $f0
		/*  f0a7240:	3c01ed00 */ 	lui	$at, 0xed00
		/*  f0a7244:	468084a0 */ 	cvt.s.w	$f18, $f16
		/*  f0a7248:	46003202 */ 	mul.s	$f8, $f6, $f0
		/*  f0a724c:	00000000 */ 	nop
		/*  f0a7250:	46009102 */ 	mul.s	$f4, $f18, $f0
		/*  f0a7254:	4600428d */ 	trunc.w.s	$f10, $f8
		/*  f0a7258:	4600218d */ 	trunc.w.s	$f6, $f4
		/*  f0a725c:	44085000 */ 	mfc1	$t0, $f10
		/*  f0a7260:	440b3000 */ 	mfc1	$t3, $f6
		/*  f0a7264:	310a0fff */ 	andi	$t2, $t0, 0xfff
		/*  f0a7268:	0141c825 */ or $t9, $t2, $at
		/*  f0a726c:	316c0fff */ 	andi	$t4, $t3, 0xfff
		/*  f0a7270:	000c6b00 */ 	sll	$t5, $t4, 0xc
		/*  f0a7274:	032d7025 */ or $t6, $t9, $t5
		/*  f0a7278:	0c002f22 */ 	jal	viGetViewWidth
		/*  f0a727c:	aeae0000 */ 	sw	$t6, 0x0($s5)
		/*  f0a7280:	00029400 */ 	sll	$s2, $v0, 0x10
		/*  f0a7284:	00127c03 */ 	sra	$t7, $s2, 0x10
		/*  f0a7288:	0c002f40 */ 	jal	viGetViewLeft
		/*  f0a728c:	01e09025 */ or $s2, $t7, $zero
		/*  f0a7290:	0002a400 */ 	sll	$s4, $v0, 0x10
		/*  f0a7294:	00144c03 */ 	sra	$t1, $s4, 0x10
		/*  f0a7298:	0c002f44 */ 	jal	viGetViewTop
		/*  f0a729c:	0120a025 */ or $s4, $t1, $zero
		/*  f0a72a0:	00028400 */ 	sll	$s0, $v0, 0x10
		/*  f0a72a4:	00104403 */ 	sra	$t0, $s0, 0x10
		/*  f0a72a8:	0c002f26 */ 	jal	viGetViewHeight
		/*  f0a72ac:	01008025 */ or $s0, $t0, $zero
		/*  f0a72b0:	00505021 */ 	addu	$t2, $v0, $s0
		/*  f0a72b4:	448a4000 */ 	mtc1	$t2, $f8
		/*  f0a72b8:	0292c821 */ 	addu	$t9, $s4, $s2
		/*  f0a72bc:	44992000 */ 	mtc1	$t9, $f4
		/*  f0a72c0:	468042a0 */ 	cvt.s.w	$f10, $f8
		/*  f0a72c4:	3c014080 */ 	lui	$at, 0x4080
		/*  f0a72c8:	44810000 */ 	mtc1	$at, $f0
		/*  f0a72cc:	3c053fc0 */ 	lui	$a1, 0x3fc0
		/*  f0a72d0:	3c06447a */ 	lui	$a2, 0x447a
		/*  f0a72d4:	468021a0 */ 	cvt.s.w	$f6, $f4
		/*  f0a72d8:	46005402 */ 	mul.s	$f16, $f10, $f0
		/*  f0a72dc:	00000000 */ 	nop
		/*  f0a72e0:	46003202 */ 	mul.s	$f8, $f6, $f0
		/*  f0a72e4:	4600848d */ 	trunc.w.s	$f18, $f16
		/*  f0a72e8:	4600428d */ 	trunc.w.s	$f10, $f8
		/*  f0a72ec:	440b9000 */ 	mfc1	$t3, $f18
		/*  f0a72f0:	440e5000 */ 	mfc1	$t6, $f10
		/*  f0a72f4:	316c0fff */ 	andi	$t4, $t3, 0xfff
		/*  f0a72f8:	31cf0fff */ 	andi	$t7, $t6, 0xfff
		/*  f0a72fc:	000f4b00 */ 	sll	$t1, $t7, 0xc
		/*  f0a7300:	01894025 */ or $t0, $t4, $t1
		/*  f0a7304:	aea80004 */ 	sw	$t0, 0x4($s5)
		/*  f0a7308:	0c002b29 */ 	jal	vi0000aca4
		/*  f0a730c:	8fa4014c */ 	lw	$a0, 0x14c($sp)
		/*  f0a7310:	8e2a0284 */ 	lw	$t2, 0x284($s1)
		/*  f0a7314:	afa2014c */ 	sw	$v0, 0x14c($sp)
		/*  f0a7318:	91581bfc */ 	lbu	$t8, 0x1bfc($t2)
		/*  f0a731c:	53000016 */ 	beqzl	$t8, .L0f0a7378
		/*  f0a7320:	8e2b006c */ 	lw	$t3, 0x6c($s1)
		/*  f0a7324:	0fc54bc7 */ 	jal	optionsGetScreenRatio
		/*  f0a7328:	00000000 */ 	nop
		/*  f0a732c:	24010001 */ 	addiu	$at, $zero, 0x1
		/*  f0a7330:	14410008 */ 	bne	$v0, $at, .L0f0a7354
		/*  f0a7334:	00000000 */ 	nop
		/*  f0a7338:	0fc2f4d6 */ 	jal	player0f0bd358
		/*  f0a733c:	00000000 */ 	nop
		/*  f0a7340:	3c017f1b */ 	lui	$at, % hi(var7f1aca8c)
		/*  f0a7344:	c430ca8c */ 	lwc1	$f16, % lo(var7f1aca8c)($at)
		/*  f0a7348:	46100082 */ 	mul.s	$f2, $f0, $f16
		/*  f0a734c:	10000005 */ 	b.L0f0a7364
		/*  f0a7350:	44061000 */ 	mfc1	$a2, $f2
		.L0f0a7354:
/*  f0a7354:	0fc2f4d6 */ 	jal	player0f0bd358
/*  f0a7358:	00000000 */ 	nop
/*  f0a735c:	46000086 */ 	mov.s	$f2, $f0
/*  f0a7360:	44061000 */ 	mfc1	$a2, $f2
.L0f0a7364 :
		/*  f0a7364:	8fa4014c */ 	lw	$a0, 0x14c($sp)
		/*  f0a7368:	0c002c3a */ 	jal	vi0000b0e8
		/*  f0a736c:	3c054270 */ 	lui	$a1, 0x4270
		/*  f0a7370:	afa2014c */ 	sw	$v0, 0x14c($sp)
		/*  f0a7374:	8e2b006c */ 	lw	$t3, 0x6c($s1)
		.L0f0a7378:
/*  f0a7378:	24010001 */ 	addiu	$at, $zero, 0x1
/*  f0a737c:	51600004 */ 	beqzl	$t3, .L0f0a7390
/*  f0a7380:	00002825 */ or $a1, $zero, $zero
/*  f0a7384:	10000002 */ 	b.L0f0a7390
/*  f0a7388:	24050001 */ 	addiu	$a1, $zero, 0x1
/*  f0a738c:	00002825 */ or $a1, $zero, $zero
.L0f0a7390:
/*  f0a7390:	8e390068 */ 	lw	$t9, 0x68($s1)
/*  f0a7394:	53200004 */ 	beqzl	$t9, .L0f0a73a8
/*  f0a7398:	00002025 */ or $a0, $zero, $zero
/*  f0a739c:	10000002 */ 	b.L0f0a73a8
/*  f0a73a0:	24040001 */ 	addiu	$a0, $zero, 0x1
/*  f0a73a4:	00002025 */ or $a0, $zero, $zero
.L0f0a73a8:
/*  f0a73a8:	8e2d0064 */ 	lw	$t5, 0x64($s1)
/*  f0a73ac:	51a00004 */ 	beqzl	$t5, .L0f0a73c0
/*  f0a73b0:	00001025 */ or $v0, $zero, $zero
/*  f0a73b4:	10000002 */ 	b.L0f0a73c0
/*  f0a73b8:	24020001 */ 	addiu	$v0, $zero, 0x1
/*  f0a73bc:	00001025 */ or $v0, $zero, $zero
.L0f0a73c0:
/*  f0a73c0:	8e2e0070 */ 	lw	$t6, 0x70($s1)
/*  f0a73c4:	51c00004 */ 	beqzl	$t6, .L0f0a73d8
/*  f0a73c8:	00001825 */ or $v1, $zero, $zero
/*  f0a73cc:	10000002 */ 	b.L0f0a73d8
/*  f0a73d0:	24030001 */ 	addiu	$v1, $zero, 0x1
/*  f0a73d4:	00001825 */ or $v1, $zero, $zero
.L0f0a73d8:
/*  f0a73d8:	00627821 */ 	addu	$t7, $v1, $v0
/*  f0a73dc:	01e46021 */ 	addu	$t4, $t7, $a0
/*  f0a73e0:	01854821 */ 	addu	$t1, $t4, $a1
/*  f0a73e4:	15210008 */ 	bne	$t1, $at, .L0f0a7408
/*  f0a73e8:	3c088009 */ 	lui	$t0, % hi(g_Is4Mb)
/*  f0a73ec:	91080af0 */ 	lbu	$t0, % lo(g_Is4Mb)($t0)
/*  f0a73f0:	24010001 */ 	addiu	$at, $zero, 0x1
/*  f0a73f4:	51010005 */ 	beql	$t0, $at, .L0f0a740c
/*  f0a73f8:	0000a025 */ or $s4, $zero, $zero
/*  f0a73fc:	0fc2be93 */ 	jal	lasersightRenderBeam
/*  f0a7400:	8fa4014c */ 	lw	$a0, 0x14c($sp)
/*  f0a7404:	afa2014c */ 	sw	$v0, 0x14c($sp)
.L0f0a7408:
/*  f0a7408:	0000a025 */ or $s4, $zero, $zero
.L0f0a740c :
		/*  f0a740c:	26700638 */ 	addiu	$s0, $s3, 0x638
		/*  f0a7410:	8fb500e4 */ 	lw	$s5, 0xe4($sp)
		/*  f0a7414:	24120019 */ 	addiu	$s2, $zero, 0x1e
		.L0f0a7418 :
		/*  f0a7418:	0fc2867c */ 	jal	bgunGetWeaponNum2
		/*  f0a741c:	02802025 */ or $a0, $s4, $zero
		/*  f0a7420:	afa200ec */ 	sw	$v0, 0xec($sp)
		/*  f0a7424:	820a0007 */ 	lb	$t2, 0x7($s0)
		/*  f0a7428:	8fa4014c */ 	lw	$a0, 0x14c($sp)
		/*  f0a742c:	260501dc */ 	addiu	$a1, $s0, 0x1dc
		/*  f0a7430:	114001f2 */ 	beqz	$t2, .L0f0a7bfc
		/*  f0a7434:	00003025 */ or $a2, $zero, $zero
		/*  f0a7438:	26180384 */ 	addiu	$t8, $s0, 0x384
		/*  f0a743c:	afb8003c */ 	sw	$t8, 0x3c($sp)
		/*  f0a7440:	0fc2b2e4 */ 	jal	beamRender
		/*  f0a7444:	00003825 */ or $a3, $zero, $zero
		/*  f0a7448:	afa2014c */ 	sw	$v0, 0x14c($sp)
		/*  f0a744c:	92040000 */ 	lbu	$a0, 0x0($s0)
		/*  f0a7450:	0fc2c5f0 */ 	jal	weaponHasFlag
		/*  f0a7454:	34058000 */ 	dli	$a1, 0x8000
		/*  f0a7458:	10400030 */ 	beqz	$v0, .L0f0a751c
		/*  f0a745c:	8fab014c */ 	lw	$t3, 0x14c($sp)
		/*  f0a7460:	25790008 */ 	addiu	$t9, $t3, 0x8
		/*  f0a7464:	afb9014c */ 	sw	$t9, 0x14c($sp)
		/*  f0a7468:	3c0dbc00 */ 	lui	$t5, 0xbc00
		/*  f0a746c:	3c0e8000 */ 	lui	$t6, 0x8000
		/*  f0a7470:	35ce0040 */ 	ori	$t6, $t6, 0x40
		/*  f0a7474:	35ad0002 */ 	ori	$t5, $t5, 0x2
		/*  f0a7478:	ad6d0000 */ 	sw	$t5, 0x0($t3)
		/*  f0a747c:	ad6e0004 */ 	sw	$t6, 0x4($t3)
		/*  f0a7480:	8faf014c */ 	lw	$t7, 0x14c($sp)
		/*  f0a7484:	3c090386 */ 	lui	$t1, 0x386
		/*  f0a7488:	3c088007 */ 	lui	$t0, % hi(var80070090 + 0x08)
		/*  f0a748c:	25ec0008 */ 	addiu	$t4, $t7, 0x8
		/*  f0a7490:	afac014c */ 	sw	$t4, 0x14c($sp)
		/*  f0a7494:	25080098 */ 	addiu	$t0, $t0, % lo(var80070090 + 0x08)
		/*  f0a7498:	35290010 */ 	ori	$t1, $t1, 0x10
		/*  f0a749c:	ade90000 */ 	sw	$t1, 0x0($t7)
		/*  f0a74a0:	ade80004 */ 	sw	$t0, 0x4($t7)
		/*  f0a74a4:	8faa014c */ 	lw	$t2, 0x14c($sp)
		/*  f0a74a8:	3c0b0388 */ 	lui	$t3, 0x388
		/*  f0a74ac:	3c198007 */ 	lui	$t9, % hi(var80070090)
		/*  f0a74b0:	25580008 */ 	addiu	$t8, $t2, 0x8
		/*  f0a74b4:	afb8014c */ 	sw	$t8, 0x14c($sp)
		/*  f0a74b8:	27390090 */ 	addiu	$t9, $t9, % lo(var80070090)
		/*  f0a74bc:	356b0010 */ 	ori	$t3, $t3, 0x10
		/*  f0a74c0:	ad4b0000 */ 	sw	$t3, 0x0($t2)
		/*  f0a74c4:	ad590004 */ 	sw	$t9, 0x4($t2)
		/*  f0a74c8:	8fad014c */ 	lw	$t5, 0x14c($sp)
		/*  f0a74cc:	3c0f0384 */ 	lui	$t7, 0x384
		/*  f0a74d0:	35ef0010 */ 	ori	$t7, $t7, 0x10
		/*  f0a74d4:	25ae0008 */ 	addiu	$t6, $t5, 0x8
		/*  f0a74d8:	afae014c */ 	sw	$t6, 0x14c($sp)
		/*  f0a74dc:	adaf0000 */ 	sw	$t7, 0x0($t5)
		/*  f0a74e0:	0fc2d5ea */ 	jal	camGetLookAt
		/*  f0a74e4:	afad00d4 */ 	sw	$t5, 0xd4($sp)
		/*  f0a74e8:	8fa500d4 */ 	lw	$a1, 0xd4($sp)
		/*  f0a74ec:	3c080382 */ 	lui	$t0, 0x382
		/*  f0a74f0:	35080010 */ 	ori	$t0, $t0, 0x10
		/*  f0a74f4:	aca20004 */ 	sw	$v0, 0x4($a1)
		/*  f0a74f8:	8fac014c */ 	lw	$t4, 0x14c($sp)
		/*  f0a74fc:	25890008 */ 	addiu	$t1, $t4, 0x8
		/*  f0a7500:	afa9014c */ 	sw	$t1, 0x14c($sp)
		/*  f0a7504:	ad880000 */ 	sw	$t0, 0x0($t4)
		/*  f0a7508:	0fc2d5ea */ 	jal	camGetLookAt
		/*  f0a750c:	afac00d0 */ 	sw	$t4, 0xd0($sp)
		/*  f0a7510:	8fa300d0 */ 	lw	$v1, 0xd0($sp)
		/*  f0a7514:	244a0010 */ 	addiu	$t2, $v0, 0x10
		/*  f0a7518:	ac6a0004 */ 	sw	$t2, 0x4($v1)
		.L0f0a751c:
/*  f0a751c:	8fb8014c */ 	lw	$t8, 0x14c($sp)
/*  f0a7520:	3c19bc00 */ 	lui	$t9, 0xbc00
/*  f0a7524:	3739000e */ 	ori	$t9, $t9, 0xe
/*  f0a7528:	270b0008 */ 	addiu	$t3, $t8, 0x8
/*  f0a752c:	afab014c */ 	sw	$t3, 0x14c($sp)
/*  f0a7530:	3c014396 */ 	lui	$at, 0x4396
/*  f0a7534:	44817000 */ 	mtc1	$at, $f14
/*  f0a7538:	44806000 */ 	mtc1	$zero, $f12
/*  f0a753c:	af190000 */ 	sw	$t9, 0x0($t8)
/*  f0a7540:	0c005b73 */ 	jal	mtx00016dcc
/*  f0a7544:	afb800cc */ 	sw	$t8, 0xcc($sp)
/*  f0a7548:	8fa300cc */ 	lw	$v1, 0xcc($sp)
/*  f0a754c:	24050010 */ 	addiu	$a1, $zero, 0x10
/*  f0a7550:	ac620004 */ 	sw	$v0, 0x4($v1)
/*  f0a7554:	0c006a47 */ 	jal	modelGetPart
/*  f0a7558:	8e04038c */ 	lw	$a0, 0x38c($s0)
/*  f0a755c:	10400014 */ 	beqz	$v0, .L0f0a75b0
/*  f0a7560:	afa200e8 */ 	sw	$v0, 0xe8($sp)
/*  f0a7564:	8e04038c */ 	lw	$a0, 0x38c($s0)
/*  f0a7568:	0c006a47 */ 	jal	modelGetPart
/*  f0a756c:	24050011 */ 	addiu	$a1, $zero, 0x11
/*  f0a7570:	8fa4003c */ 	lw	$a0, 0x3c($sp)
/*  f0a7574:	0c006a87 */ 	jal	modelGetNodeRwData
/*  f0a7578:	00402825 */ or $a1, $v0, $zero
/*  f0a757c:	10400003 */ 	beqz	$v0, .L0f0a758c
/*  f0a7580:	3c06800a */ 	lui	$a2, % hi(var8009cf88)
/*  f0a7584:	240d0001 */ 	addiu	$t5, $zero, 0x1
/*  f0a7588:	ac4d0000 */ 	sw	$t5, 0x0($v0)
.L0f0a758c:
/*  f0a758c:	240e0001 */ 	addiu	$t6, $zero, 0x1
/*  f0a7590:	afae0014 */ 	sw	$t6, 0x14($sp)
/*  f0a7594:	8fa4003c */ 	lw	$a0, 0x3c($sp)
/*  f0a7598:	8fa500e8 */ 	lw	$a1, 0xe8($sp)
/*  f0a759c:	24c6cf88 */ 	addiu	$a2, $a2, % lo(var8009cf88)
/*  f0a75a0:	8fa7014c */ 	lw	$a3, 0x14c($sp)
/*  f0a75a4:	0fc1fefe */ 	jal	tvscreenRender
/*  f0a75a8:	afa00010 */ 	sw	$zero, 0x10($sp)
/*  f0a75ac:	afa2014c */ 	sw	$v0, 0x14c($sp)
.L0f0a75b0:
/*  f0a75b0:	8faf014c */ 	lw	$t7, 0x14c($sp)
/*  f0a75b4:	8e250284 */ 	lw	$a1, 0x284($s1)
/*  f0a75b8:	240c0004 */ 	addiu	$t4, $zero, 0x4
/*  f0a75bc:	afac013c */ 	sw	$t4, 0x13c($sp)
/*  f0a75c0:	afaf0118 */ 	sw	$t7, 0x118($sp)
/*  f0a75c4:	8ca300d8 */ 	lw	$v1, 0xd8($a1)
/*  f0a75c8:	3c078007 */ 	lui	$a3, % hi(g_InCutscene)
/*  f0a75cc:	14600013 */ 	bnez	$v1, .L0f0a761c
/*  f0a75d0:	00000000 */ 	nop
/*  f0a75d4:	8ce70764 */ 	lw	$a3, % lo(g_InCutscene)($a3)
/*  f0a75d8:	14e00010 */ 	bnez	$a3, .L0f0a761c
/*  f0a75dc:	00000000 */ 	nop
/*  f0a75e0:	8ca20480 */ 	lw	$v0, 0x480($a1)
/*  f0a75e4:	50400007 */ 	beqzl	$v0, .L0f0a7604
/*  f0a75e8:	8caa1c54 */ 	lw	$t2, 0x1c54($a1)
/*  f0a75ec:	1040000b */ 	beqz	$v0, .L0f0a761c
/*  f0a75f0:	00000000 */ 	nop
/*  f0a75f4:	80490037 */ 	lb	$t1, 0x37($v0)
/*  f0a75f8:	15200008 */ 	bnez	$t1, .L0f0a761c
/*  f0a75fc:	00000000 */ 	nop
/*  f0a7600:	8caa1c54 */ 	lw	$t2, 0x1c54($a1)
.L0f0a7604:
/*  f0a7604:	8ca800c4 */ 	lw	$t0, 0xc4($a1)
/*  f0a7608:	0140c027 */ 	nor	$t8, $t2, $zero
/*  f0a760c:	01185824 */ and $t3, $t0, $t8
/*  f0a7610:	31790001 */ 	andi	$t9, $t3, 0x1
/*  f0a7614:	57200016 */ 	bnezl	$t9, .L0f0a7670
/*  f0a7618:	92681615 */ 	lbu	$t0, 0x1615($s3)
.L0f0a761c:
/*  f0a761c:	14600078 */ 	bnez	$v1, .L0f0a7800
/*  f0a7620:	3c078007 */ 	lui	$a3, % hi(g_InCutscene)
/*  f0a7624:	8ce70764 */ 	lw	$a3, % lo(g_InCutscene)($a3)
/*  f0a7628:	54e00076 */ 	bnezl	$a3, .L0f0a7804
/*  f0a762c:	926d1614 */ 	lbu	$t5, 0x1614($s3)
/*  f0a7630:	8ca20480 */ 	lw	$v0, 0x480($a1)
/*  f0a7634:	50400007 */ 	beqzl	$v0, .L0f0a7654
/*  f0a7638:	8caf1c54 */ 	lw	$t7, 0x1c54($a1)
/*  f0a763c:	50400071 */ 	beqzl	$v0, .L0f0a7804
/*  f0a7640:	926d1614 */ 	lbu	$t5, 0x1614($s3)
/*  f0a7644:	804d0037 */ 	lb	$t5, 0x37($v0)
/*  f0a7648:	55a0006e */ 	bnezl	$t5, .L0f0a7804
/*  f0a764c:	926d1614 */ 	lbu	$t5, 0x1614($s3)
/*  f0a7650:	8caf1c54 */ 	lw	$t7, 0x1c54($a1)
.L0f0a7654:
/*  f0a7654:	8cae00c4 */ 	lw	$t6, 0xc4($a1)
/*  f0a7658:	01e06027 */ 	nor	$t4, $t7, $zero
/*  f0a765c:	01cc4824 */ and $t1, $t6, $t4
/*  f0a7660:	312a0008 */ 	andi	$t2, $t1, 0x8
/*  f0a7664:	51400067 */ 	beqzl	$t2, .L0f0a7804
/*  f0a7668:	926d1614 */ 	lbu	$t5, 0x1614($s3)
/*  f0a766c:	92681615 */ 	lbu	$t0, 0x1615($s3)
.L0f0a7670:
/*  f0a7670:	92781614 */ 	lbu	$t8, 0x1614($s3)
/*  f0a7674:	26641614 */ 	addiu	$a0, $s3, 0x1614
/*  f0a7678:	0118082a */ 	slt	$at, $t0, $t8
/*  f0a767c:	50200009 */ 	beqzl	$at, .L0f0a76a4
/*  f0a7680:	90820002 */ 	lbu	$v0, 0x2($a0)
/*  f0a7684:	90830000 */ 	lbu	$v1, 0x0($a0)
/*  f0a7688:	908b0002 */ 	lbu	$t3, 0x2($a0)
/*  f0a768c:	0163082a */ 	slt	$at, $t3, $v1
/*  f0a7690:	50200004 */ 	beqzl	$at, .L0f0a76a4
/*  f0a7694:	90820002 */ 	lbu	$v0, 0x2($a0)
/*  f0a7698:	1000000a */ 	b.L0f0a76c4
/*  f0a769c:	00601025 */ or $v0, $v1, $zero
/*  f0a76a0:	90820002 */ 	lbu	$v0, 0x2($a0)
.L0f0a76a4:
/*  f0a76a4:	90860001 */ 	lbu	$a2, 0x1($a0)
/*  f0a76a8:	00401825 */ or $v1, $v0, $zero
/*  f0a76ac:	0046082a */ 	slt	$at, $v0, $a2
/*  f0a76b0:	10200003 */ 	beqz	$at, .L0f0a76c0
/*  f0a76b4:	00000000 */ 	nop
/*  f0a76b8:	10000001 */ 	b.L0f0a76c0
/*  f0a76bc:	00c01825 */ or $v1, $a2, $zero
.L0f0a76c0:
/*  f0a76c0:	00601025 */ or $v0, $v1, $zero
.L0f0a76c4 :
		/*  f0a76c4:	90890003 */ 	lbu	$t1, 0x3($a0)
		/*  f0a76c8:	0002ce00 */ 	sll	$t9, $v0, 0x18
		/*  f0a76cc:	00026c00 */ 	sll	$t5, $v0, 0x10
		/*  f0a76d0:	032d7825 */ or $t7, $t9, $t5
		/*  f0a76d4:	00027200 */ 	sll	$t6, $v0, 0x8
		/*  f0a76d8:	01ee6025 */ or $t4, $t7, $t6
		/*  f0a76dc:	012c5021 */ 	addu	$t2, $t1, $t4
		/*  f0a76e0:	afaa0140 */ 	sw	$t2, 0x140($sp)
		/*  f0a76e4:	8ca300d8 */ 	lw	$v1, 0xd8($a1)
		/*  f0a76e8:	14600021 */ 	bnez	$v1, .L0f0a7770
		/*  f0a76ec:	00000000 */ 	nop
		/*  f0a76f0:	14e0001f */ 	bnez	$a3, .L0f0a7770
		/*  f0a76f4:	00000000 */ 	nop
		/*  f0a76f8:	8ca20480 */ 	lw	$v0, 0x480($a1)
		/*  f0a76fc:	50400007 */ 	beqzl	$v0, .L0f0a771c
		/*  f0a7700:	8cab1c54 */ 	lw	$t3, 0x1c54($a1)
		/*  f0a7704:	1040001a */ 	beqz	$v0, .L0f0a7770
		/*  f0a7708:	00000000 */ 	nop
		/*  f0a770c:	80480037 */ 	lb	$t0, 0x37($v0)
		/*  f0a7710:	15000017 */ 	bnez	$t0, .L0f0a7770
		/*  f0a7714:	00000000 */ 	nop
		/*  f0a7718:	8cab1c54 */ 	lw	$t3, 0x1c54($a1)
		.L0f0a771c:
/*  f0a771c:	8cb800c4 */ 	lw	$t8, 0xc4($a1)
/*  f0a7720:	3c02800a */ 	lui	$v0, % hi(var8009caec + 0x3)
/*  f0a7724:	0160c827 */ 	nor	$t9, $t3, $zero
/*  f0a7728:	03196824 */ and $t5, $t8, $t9
/*  f0a772c:	31af0001 */ 	andi	$t7, $t5, 0x1
/*  f0a7730:	11e0000f */ 	beqz	$t7, .L0f0a7770
/*  f0a7734:	00000000 */ 	nop
/*  f0a7738:	9042caef */ 	lbu	$v0, % lo(var8009caec + 0x3)($v0)
/*  f0a773c:	3c06800a */ 	lui	$a2, % hi(var8009caf0)
/*  f0a7740:	90c6caf0 */ 	lbu	$a2, % lo(var8009caf0)($a2)
/*  f0a7744:	00027600 */ 	sll	$t6, $v0, 0x18
/*  f0a7748:	00024c00 */ 	sll	$t1, $v0, 0x10
/*  f0a774c:	01c96025 */ or $t4, $t6, $t1
/*  f0a7750:	00025200 */ 	sll	$t2, $v0, 0x8
/*  f0a7754:	018a4025 */ or $t0, $t4, $t2
/*  f0a7758:	afa200b0 */ 	sw	$v0, 0xb0($sp)
/*  f0a775c:	afa200b4 */ 	sw	$v0, 0xb4($sp)
/*  f0a7760:	afa200b8 */ 	sw	$v0, 0xb8($sp)
/*  f0a7764:	00c8a821 */ 	addu	$s5, $a2, $t0
/*  f0a7768:	1000001f */ 	b.L0f0a77e8
/*  f0a776c:	afa600bc */ 	sw	$a2, 0xbc($sp)
.L0f0a7770:
/*  f0a7770:	5460001e */ 	bnezl	$v1, .L0f0a77ec
/*  f0a7774:	8fb900ec */ 	lw	$t9, 0xec($sp)
/*  f0a7778:	54e0001c */ 	bnezl	$a3, .L0f0a77ec
/*  f0a777c:	8fb900ec */ 	lw	$t9, 0xec($sp)
/*  f0a7780:	8ca20480 */ 	lw	$v0, 0x480($a1)
/*  f0a7784:	50400007 */ 	beqzl	$v0, .L0f0a77a4
/*  f0a7788:	8cb91c54 */ 	lw	$t9, 0x1c54($a1)
/*  f0a778c:	50400017 */ 	beqzl	$v0, .L0f0a77ec
/*  f0a7790:	8fb900ec */ 	lw	$t9, 0xec($sp)
/*  f0a7794:	804b0037 */ 	lb	$t3, 0x37($v0)
/*  f0a7798:	55600014 */ 	bnezl	$t3, .L0f0a77ec
/*  f0a779c:	8fb900ec */ 	lw	$t9, 0xec($sp)
/*  f0a77a0:	8cb91c54 */ 	lw	$t9, 0x1c54($a1)
.L0f0a77a4:
/*  f0a77a4:	8cb800c4 */ 	lw	$t8, 0xc4($a1)
/*  f0a77a8:	240200ff */ 	addiu	$v0, $zero, 0xff
/*  f0a77ac:	03206827 */ 	nor	$t5, $t9, $zero
/*  f0a77b0:	030d7824 */ and $t7, $t8, $t5
/*  f0a77b4:	31ee0008 */ 	andi	$t6, $t7, 0x8
/*  f0a77b8:	11c0000b */ 	beqz	$t6, .L0f0a77e8
/*  f0a77bc:	24050080 */ 	addiu	$a1, $zero, 0x80
/*  f0a77c0:	00024e00 */ 	sll	$t1, $v0, 0x18
/*  f0a77c4:	00006400 */ 	sll	$t4, $zero, 0x10
/*  f0a77c8:	012c5025 */ or $t2, $t1, $t4
/*  f0a77cc:	00004200 */ 	sll	$t0, $zero, 0x8
/*  f0a77d0:	01485825 */ or $t3, $t2, $t0
/*  f0a77d4:	00aba821 */ 	addu	$s5, $a1, $t3
/*  f0a77d8:	afa200a0 */ 	sw	$v0, 0xa0($sp)
/*  f0a77dc:	afa000a4 */ 	sw	$zero, 0xa4($sp)
/*  f0a77e0:	afa000a8 */ 	sw	$zero, 0xa8($sp)
/*  f0a77e4:	afa500ac */ 	sw	$a1, 0xac($sp)
.L0f0a77e8:
/*  f0a77e8:	8fb900ec */ 	lw	$t9, 0xec($sp)
.L0f0a77ec :
		/*  f0a77ec:	24010001 */ 	addiu	$at, $zero, 0x1
		/*  f0a77f0:	5721003d */ 	bnel	$t9, $at, .L0f0a78e8
		/*  f0a77f4:	8e6f00bc */ 	lw	$t7, 0xbc($s3)
		/*  f0a77f8:	1000003a */ 	b.L0f0a78e4
		/*  f0a77fc:	afb50140 */ 	sw	$s5, 0x140($sp)
		.L0f0a7800:
/*  f0a7800:	926d1614 */ 	lbu	$t5, 0x1614($s3)
.L0f0a7804 :
		/*  f0a7804:	92781617 */ 	lbu	$t8, 0x1617($s3)
		/*  f0a7808:	92691615 */ 	lbu	$t1, 0x1615($s3)
		/*  f0a780c:	92681616 */ 	lbu	$t0, 0x1616($s3)
		/*  f0a7810:	000d7e00 */ 	sll	$t7, $t5, 0x18
		/*  f0a7814:	030f7025 */ or $t6, $t8, $t7
		/*  f0a7818:	00096400 */ 	sll	$t4, $t1, 0x10
		/*  f0a781c:	01cc5025 */ or $t2, $t6, $t4
		/*  f0a7820:	00085a00 */ 	sll	$t3, $t0, 0x8
		/*  f0a7824:	014bc825 */ or $t9, $t2, $t3
		/*  f0a7828:	afb90140 */ 	sw	$t9, 0x140($sp)
		/*  f0a782c:	920d0000 */ 	lbu	$t5, 0x0($s0)
		/*  f0a7830:	24010006 */ 	addiu	$at, $zero, 0x6
		/*  f0a7834:	0320a825 */ or $s5, $t9, $zero
		/*  f0a7838:	15a1002a */ 	bne	$t5, $at, .L0f0a78e4
		/*  f0a783c:	3c04ff00 */ 	lui	$a0, 0xff00
		/*  f0a7840:	3c014248 */ 	lui	$at, 0x4248
		/*  f0a7844:	44812000 */ 	mtc1	$at, $f4
		/*  f0a7848:	c612023c */ 	lwc1	$f18, 0x23c($s0)
		/*  f0a784c:	24060001 */ 	addiu	$a2, $zero, 0x1
		/*  f0a7850:	3c014f00 */ 	lui	$at, 0x4f00
		/*  f0a7854:	46049182 */ 	mul.s	$f6, $f18, $f4
		/*  f0a7858:	3484007f */ 	ori	$a0, $a0, 0x7f
		/*  f0a785c:	4458f800 */ 	cfc1	$t8, $31
		/*  f0a7860:	44c6f800 */ 	ctc1	$a2, $31
		/*  f0a7864:	00000000 */ 	nop
		/*  f0a7868:	46003224 */ 	cvt.w.s	$f8, $f6
		/*  f0a786c:	4446f800 */ 	cfc1	$a2, $31
		/*  f0a7870:	00000000 */ 	nop
		/*  f0a7874:	30c60078 */ 	andi	$a2, $a2, 0x78
		/*  f0a7878:	50c00013 */ 	beqzl	$a2, .L0f0a78c8
		/*  f0a787c:	44064000 */ 	mfc1	$a2, $f8
		/*  f0a7880:	44814000 */ 	mtc1	$at, $f8
		/*  f0a7884:	24060001 */ 	addiu	$a2, $zero, 0x1
		/*  f0a7888:	46083201 */ 	sub.s	$f8, $f6, $f8
		/*  f0a788c:	44c6f800 */ 	ctc1	$a2, $31
		/*  f0a7890:	00000000 */ 	nop
		/*  f0a7894:	46004224 */ 	cvt.w.s	$f8, $f8
		/*  f0a7898:	4446f800 */ 	cfc1	$a2, $31
		/*  f0a789c:	00000000 */ 	nop
		/*  f0a78a0:	30c60078 */ 	andi	$a2, $a2, 0x78
		/*  f0a78a4:	14c00005 */ 	bnez	$a2, .L0f0a78bc
		/*  f0a78a8:	00000000 */ 	nop
		/*  f0a78ac:	44064000 */ 	mfc1	$a2, $f8
		/*  f0a78b0:	3c018000 */ 	lui	$at, 0x8000
		/*  f0a78b4:	10000007 */ 	b.L0f0a78d4
		/*  f0a78b8:	00c13025 */ or $a2, $a2, $at
		.L0f0a78bc:
/*  f0a78bc:	10000005 */ 	b.L0f0a78d4
/*  f0a78c0:	2406ffff */ 	addiu	$a2, $zero, -1
/*  f0a78c4:	44064000 */ 	mfc1	$a2, $f8
.L0f0a78c8 :
		/*  f0a78c8:	00000000 */ 	nop
		/*  f0a78cc:	04c0fffb */ 	bltz	$a2, .L0f0a78bc
		/*  f0a78d0:	00000000 */ 	nop
		.L0f0a78d4 :
		/*  f0a78d4:	44d8f800 */ 	ctc1	$t8, $31
		/*  f0a78d8:	0fc01a40 */ 	jal	colourBlend
		/*  f0a78dc:	03202825 */ or $a1, $t9, $zero
		/*  f0a78e0:	afa20140 */ 	sw	$v0, 0x140($sp)
		.L0f0a78e4 :
		/*  f0a78e4:	8e6f00bc */ 	lw	$t7, 0xbc($s3)
		.L0f0a78e8 :
		/*  f0a78e8:	0fc08af9 */ 	jal	chrGetCloakAlpha
		/*  f0a78ec:	8de40004 */ 	lw	$a0, 0x4($t7)
		/*  f0a78f0:	284100ff */ 	slti	$at, $v0, 0xff
		/*  f0a78f4:	1020000f */ 	beqz	$at, .L0f0a7934
		/*  f0a78f8:	240c0001 */ 	addiu	$t4, $zero, 0x1
		/*  f0a78fc:	44825000 */ 	mtc1	$v0, $f10
		/*  f0a7900:	3c017f1b */ 	lui	$at, % hi(var7f1aca90)
		/*  f0a7904:	c432ca90 */ 	lwc1	$f18, % lo(var7f1aca90)($at)
		/*  f0a7908:	46805420 */ 	cvt.s.w	$f16, $f10
		/*  f0a790c:	8fa40140 */ 	lw	$a0, 0x140($sp)
		/*  f0a7910:	240e0005 */ 	addiu	$t6, $zero, 0x5
		/*  f0a7914:	afae013c */ 	sw	$t6, 0x13c($sp)
		/*  f0a7918:	afa40144 */ 	sw	$a0, 0x144($sp)
		/*  f0a791c:	46128102 */ 	mul.s	$f4, $f16, $f18
		/*  f0a7920:	4600218d */ 	trunc.w.s	$f6, $f4
		/*  f0a7924:	44033000 */ 	mfc1	$v1, $f6
		/*  f0a7928:	00000000 */ 	nop
		/*  f0a792c:	24750041 */ 	addiu	$s5, $v1, 0x41
		/*  f0a7930:	afb50140 */ 	sw	$s5, 0x140($sp)
		.L0f0a7934:
/*  f0a7934:	0c0059d8 */ 	jal	mtx00016760
/*  f0a7938:	afac0110 */ 	sw	$t4, 0x110($sp)
/*  f0a793c:	8e020218 */ 	lw	$v0, 0x218($s0)
/*  f0a7940:	50400017 */ 	beqzl	$v0, .L0f0a79a0
/*  f0a7944:	8fa400ec */ 	lw	$a0, 0xec($sp)
/*  f0a7948:	8c460018 */ 	lw	$a2, 0x18($v0)
/*  f0a794c:	afa00094 */ 	sw	$zero, 0x94($sp)
/*  f0a7950:	50c00013 */ 	beqzl	$a2, .L0f0a79a0
/*  f0a7954:	8fa400ec */ 	lw	$a0, 0xec($sp)
/*  f0a7958:	8cc80008 */ 	lw	$t0, 0x8($a2)
/*  f0a795c:	240a0001 */ 	addiu	$t2, $zero, 0x1
/*  f0a7960:	27a4010c */ 	addiu	$a0, $sp, 0x10c
/*  f0a7964:	1100000d */ 	beqz	$t0, .L0f0a799c
/*  f0a7968:	00c02825 */ or $a1, $a2, $zero
/*  f0a796c:	afaa0094 */ 	sw	$t2, 0x94($sp)
/*  f0a7970:	0c0087bd */ 	jal	modelRender
/*  f0a7974:	afa60098 */ 	sw	$a2, 0x98($sp)
/*  f0a7978:	8fa60098 */ 	lw	$a2, 0x98($sp)
/*  f0a797c:	8ccb0008 */ 	lw	$t3, 0x8($a2)
/*  f0a7980:	8cc4000c */ 	lw	$a0, 0xc($a2)
/*  f0a7984:	0fc30cfc */ 	jal	mtxF2LBulk
/*  f0a7988:	8565000e */ 	lh	$a1, 0xe($t3)
/*  f0a798c:	8e0d021c */ 	lw	$t5, 0x21c($s0)
/*  f0a7990:	51a00003 */ 	beqzl	$t5, .L0f0a79a0
/*  f0a7994:	8fa400ec */ 	lw	$a0, 0xec($sp)
/*  f0a7998:	ae000218 */ 	sw	$zero, 0x218($s0)
.L0f0a799c:
/*  f0a799c:	8fa400ec */ 	lw	$a0, 0xec($sp)
.L0f0a79a0 :
		/*  f0a79a0:	0fc2c5f0 */ 	jal	weaponHasFlag
		/*  f0a79a4:	24050020 */ 	addiu	$a1, $zero, 0x20
		/*  f0a79a8:	1040000e */ 	beqz	$v0, .L0f0a79e4
		/*  f0a79ac:	24010001 */ 	addiu	$at, $zero, 0x1
		/*  f0a79b0:	8fb80118 */ 	lw	$t8, 0x118($sp)
		/*  f0a79b4:	3c0fb600 */ 	lui	$t7, 0xb600
		/*  f0a79b8:	24093000 */ 	addiu	$t1, $zero, 0x3000
		/*  f0a79bc:	27190008 */ 	addiu	$t9, $t8, 0x8
		/*  f0a79c0:	afb90118 */ 	sw	$t9, 0x118($sp)
		/*  f0a79c4:	af090004 */ 	sw	$t1, 0x4($t8)
		/*  f0a79c8:	16800004 */ 	bnez	$s4, .L0f0a79dc
		/*  f0a79cc:	af0f0000 */ 	sw	$t7, 0x0($t8)
		/*  f0a79d0:	240e0003 */ 	addiu	$t6, $zero, 0x3
		/*  f0a79d4:	10000003 */ 	b.L0f0a79e4
		/*  f0a79d8:	afae0148 */ 	sw	$t6, 0x148($sp)
		.L0f0a79dc:
/*  f0a79dc:	240c0002 */ 	addiu	$t4, $zero, 0x2
/*  f0a79e0:	afac0148 */ 	sw	$t4, 0x148($sp)
.L0f0a79e4 :
		/*  f0a79e4:	8e28006c */ 	lw	$t0, 0x6c($s1)
		/*  f0a79e8:	00002825 */ or $a1, $zero, $zero
		/*  f0a79ec:	00002025 */ or $a0, $zero, $zero
		/*  f0a79f0:	11000003 */ 	beqz	$t0, .L0f0a7a00
		/*  f0a79f4:	00001025 */ or $v0, $zero, $zero
		/*  f0a79f8:	10000001 */ 	b.L0f0a7a00
		/*  f0a79fc:	24050001 */ 	addiu	$a1, $zero, 0x1
		.L0f0a7a00:
/*  f0a7a00:	8e2a0068 */ 	lw	$t2, 0x68($s1)
/*  f0a7a04:	00001825 */ or $v1, $zero, $zero
/*  f0a7a08:	11400003 */ 	beqz	$t2, .L0f0a7a18
/*  f0a7a0c:	00000000 */ 	nop
/*  f0a7a10:	10000001 */ 	b.L0f0a7a18
/*  f0a7a14:	24040001 */ 	addiu	$a0, $zero, 0x1
.L0f0a7a18:
/*  f0a7a18:	8e2b0064 */ 	lw	$t3, 0x64($s1)
/*  f0a7a1c:	11600003 */ 	beqz	$t3, .L0f0a7a2c
/*  f0a7a20:	00000000 */ 	nop
/*  f0a7a24:	10000001 */ 	b.L0f0a7a2c
/*  f0a7a28:	24020001 */ 	addiu	$v0, $zero, 0x1
.L0f0a7a2c:
/*  f0a7a2c:	8e2d0070 */ 	lw	$t5, 0x70($s1)
/*  f0a7a30:	11a00003 */ 	beqz	$t5, .L0f0a7a40
/*  f0a7a34:	00000000 */ 	nop
/*  f0a7a38:	10000001 */ 	b.L0f0a7a40
/*  f0a7a3c:	24030001 */ 	addiu	$v1, $zero, 0x1
.L0f0a7a40:
/*  f0a7a40:	0062c021 */ 	addu	$t8, $v1, $v0
/*  f0a7a44:	0304c821 */ 	addu	$t9, $t8, $a0
/*  f0a7a48:	03257821 */ 	addu	$t7, $t9, $a1
/*  f0a7a4c:	15e10036 */ 	bne	$t7, $at, .L0f0a7b28
/*  f0a7a50:	24050041 */ 	addiu	$a1, $zero, 0x41
/*  f0a7a54:	0c006a47 */ 	jal	modelGetPart
/*  f0a7a58:	8e04038c */ 	lw	$a0, 0x38c($s0)
/*  f0a7a5c:	50400033 */ 	beqzl	$v0, .L0f0a7b2c
/*  f0a7a60:	27a4010c */ 	addiu	$a0, $sp, 0x10c
/*  f0a7a64:	8c440004 */ 	lw	$a0, 0x4($v0)
/*  f0a7a68:	00002825 */ or $a1, $zero, $zero
/*  f0a7a6c:	84890010 */ 	lh	$t1, 0x10($a0)
/*  f0a7a70:	5920002e */ 	blezl	$t1, .L0f0a7b2c
/*  f0a7a74:	27a4010c */ 	addiu	$a0, $sp, 0x10c
/*  f0a7a78:	8e2a0034 */ 	lw	$t2, 0x34($s1)
.L0f0a7a7c:
/*  f0a7a7c:	8c8e000c */ 	lw	$t6, 0xc($a0)
/*  f0a7a80:	00056080 */ 	sll	$t4, $a1, 0x2
/*  f0a7a84:	01520019 */ 	multu	$t2, $s2
/*  f0a7a88:	01856023 */ 	subu	$t4, $t4, $a1
/*  f0a7a8c:	000c6080 */ 	sll	$t4, $t4, 0x2
/*  f0a7a90:	01cc1021 */ 	addu	$v0, $t6, $t4
/*  f0a7a94:	8448000a */ 	lh	$t0, 0xa($v0)
/*  f0a7a98:	0005c880 */ 	sll	$t9, $a1, 0x2
/*  f0a7a9c:	0325c823 */ 	subu	$t9, $t9, $a1
/*  f0a7aa0:	0019c880 */ 	sll	$t9, $t9, 0x2
/*  f0a7aa4:	24420002 */ 	addiu	$v0, $v0, 0x2
/*  f0a7aa8:	00005812 */ 	mflo	$t3
/*  f0a7aac:	010b6823 */ 	subu	$t5, $t0, $t3
/*  f0a7ab0:	a44d0008 */ 	sh	$t5, 0x8($v0)
/*  f0a7ab4:	8c98000c */ 	lw	$t8, 0xc($a0)
/*  f0a7ab8:	03197821 */ 	addu	$t7, $t8, $t9
/*  f0a7abc:	85e9000a */ 	lh	$t1, 0xa($t7)
/*  f0a7ac0:	2921a000 */ 	slti	$at, $t1, -24576
/*  f0a7ac4:	50200014 */ 	beqzl	$at, .L0f0a7b18
/*  f0a7ac8:	84980010 */ 	lh	$t8, 0x10($a0)
/*  f0a7acc:	848e0010 */ 	lh	$t6, 0x10($a0)
/*  f0a7ad0:	00001825 */ or $v1, $zero, $zero
/*  f0a7ad4:	59c00010 */ 	blezl	$t6, .L0f0a7b18
/*  f0a7ad8:	84980010 */ 	lh	$t8, 0x10($a0)
/*  f0a7adc:	8c8c000c */ 	lw	$t4, 0xc($a0)
.L0f0a7ae0:
/*  f0a7ae0:	00035080 */ 	sll	$t2, $v1, 0x2
/*  f0a7ae4:	01435023 */ 	subu	$t2, $t2, $v1
/*  f0a7ae8:	000a5080 */ 	sll	$t2, $t2, 0x2
/*  f0a7aec:	018a1021 */ 	addu	$v0, $t4, $t2
/*  f0a7af0:	8448000a */ 	lh	$t0, 0xa($v0)
/*  f0a7af4:	24630001 */ 	addiu	$v1, $v1, 0x1
/*  f0a7af8:	24420002 */ 	addiu	$v0, $v0, 0x2
/*  f0a7afc:	250b2000 */ 	addiu	$t3, $t0, 0x2000
/*  f0a7b00:	a44b0008 */ 	sh	$t3, 0x8($v0)
/*  f0a7b04:	848d0010 */ 	lh	$t5, 0x10($a0)
/*  f0a7b08:	006d082a */ 	slt	$at, $v1, $t5
/*  f0a7b0c:	5420fff4 */ 	bnezl	$at, .L0f0a7ae0
/*  f0a7b10:	8c8c000c */ 	lw	$t4, 0xc($a0)
/*  f0a7b14:	84980010 */ 	lh	$t8, 0x10($a0)
.L0f0a7b18:
/*  f0a7b18:	24a50001 */ 	addiu	$a1, $a1, 0x1
/*  f0a7b1c:	00b8082a */ 	slt	$at, $a1, $t8
/*  f0a7b20:	5420ffd6 */ 	bnezl	$at, .L0f0a7a7c
/*  f0a7b24:	8e2a0034 */ 	lw	$t2, 0x34($s1)
.L0f0a7b28 :
		/*  f0a7b28:	27a4010c */ 	addiu	$a0, $sp, 0x10c
		.L0f0a7b2c :
		/*  f0a7b2c:	0c0087bd */ 	jal	modelRender
		/*  f0a7b30:	8fa5003c */ 	lw	$a1, 0x3c($sp)
		/*  f0a7b34:	8e791594 */ 	lw	$t9, 0x1594($s3)
		/*  f0a7b38:	3c0f8007 */ 	lui	$t7, % hi(var800702dc)
		/*  f0a7b3c:	53200013 */ 	beqzl	$t9, .L0f0a7b8c
		/*  f0a7b40:	8fac0118 */ 	lw	$t4, 0x118($sp)
		/*  f0a7b44:	8def02dc */ 	lw	$t7, % lo(var800702dc)($t7)
		/*  f0a7b48:	8fa90140 */ 	lw	$t1, 0x140($sp)
		/*  f0a7b4c:	51e0000f */ 	beqzl	$t7, .L0f0a7b8c
		/*  f0a7b50:	8fac0118 */ 	lw	$t4, 0x118($sp)
		/*  f0a7b54:	afa9007c */ 	sw	$t1, 0x7c($sp)
		/*  f0a7b58:	8e0e0390 */ 	lw	$t6, 0x390($s0)
		/*  f0a7b5c:	26050534 */ 	addiu	$a1, $s0, 0x534
		/*  f0a7b60:	00a02025 */ or $a0, $a1, $zero
		/*  f0a7b64:	ae0e0540 */ 	sw	$t6, 0x540($s0)
		/*  f0a7b68:	0c007308 */ 	jal	modelUpdateRelations
		/*  f0a7b6c:	afa50054 */ 	sw	$a1, 0x54($sp)
		/*  f0a7b70:	8fa50054 */ 	lw	$a1, 0x54($sp)
		/*  f0a7b74:	afb50140 */ 	sw	$s5, 0x140($sp)
		/*  f0a7b78:	0c0087bd */ 	jal	modelRender
		/*  f0a7b7c:	27a4010c */ 	addiu	$a0, $sp, 0x10c
		/*  f0a7b80:	8fa4007c */ 	lw	$a0, 0x7c($sp)
		/*  f0a7b84:	afa40140 */ 	sw	$a0, 0x140($sp)
		/*  f0a7b88:	8fac0118 */ 	lw	$t4, 0x118($sp)
		.L0f0a7b8c:
/*  f0a7b8c:	8fa400ec */ 	lw	$a0, 0xec($sp)
/*  f0a7b90:	24050020 */ 	addiu	$a1, $zero, 0x20
/*  f0a7b94:	0fc2c5f0 */ 	jal	weaponHasFlag
/*  f0a7b98:	afac014c */ 	sw	$t4, 0x14c($sp)
/*  f0a7b9c:	10400007 */ 	beqz	$v0, .L0f0a7bbc
/*  f0a7ba0:	8faa014c */ 	lw	$t2, 0x14c($sp)
/*  f0a7ba4:	25480008 */ 	addiu	$t0, $t2, 0x8
/*  f0a7ba8:	afa8014c */ 	sw	$t0, 0x14c($sp)
/*  f0a7bac:	3c0bb600 */ 	lui	$t3, 0xb600
/*  f0a7bb0:	240d3000 */ 	addiu	$t5, $zero, 0x3000
/*  f0a7bb4:	ad4d0004 */ 	sw	$t5, 0x4($t2)
/*  f0a7bb8:	ad4b0000 */ 	sw	$t3, 0x0($t2)
.L0f0a7bbc:
/*  f0a7bbc:	8e18038c */ 	lw	$t8, 0x38c($s0)
/*  f0a7bc0:	8e040390 */ 	lw	$a0, 0x390($s0)
/*  f0a7bc4:	0fc30cfc */ 	jal	mtxF2LBulk
/*  f0a7bc8:	8705000e */ 	lh	$a1, 0xe($t8)
/*  f0a7bcc:	0c0059e1 */ 	jal	mtx00016784
/*  f0a7bd0:	00000000 */ 	nop
/*  f0a7bd4:	8fb9014c */ 	lw	$t9, 0x14c($sp)
/*  f0a7bd8:	3c09bc00 */ 	lui	$t1, 0xbc00
/*  f0a7bdc:	3529000e */ 	ori	$t1, $t1, 0xe
/*  f0a7be0:	272f0008 */ 	addiu	$t7, $t9, 0x8
/*  f0a7be4:	afaf014c */ 	sw	$t7, 0x14c($sp)
/*  f0a7be8:	af290000 */ 	sw	$t1, 0x0($t9)
/*  f0a7bec:	0c002adb */ 	jal	viGetPerspScale
/*  f0a7bf0:	afb90074 */ 	sw	$t9, 0x74($sp)
/*  f0a7bf4:	8fa30074 */ 	lw	$v1, 0x74($sp)
/*  f0a7bf8:	ac620004 */ 	sw	$v0, 0x4($v1)
.L0f0a7bfc:
/*  f0a7bfc:	26940001 */ 	addiu	$s4, $s4, 0x1
/*  f0a7c00:	24010002 */ 	addiu	$at, $zero, 0x2
/*  f0a7c04:	1681fe04 */ 	bne	$s4, $at, .L0f0a7418
/*  f0a7c08:	261007a4 */ 	addiu	$s0, $s0, 0x7a4
/*  f0a7c0c:	afb500e4 */ 	sw	$s5, 0xe4($sp)
/*  f0a7c10:	0fc2baf8 */ 	jal	casingsRender
/*  f0a7c14:	27a4014c */ 	addiu	$a0, $sp, 0x14c
/*  f0a7c18:	0fc5d8a6 */ 	jal	zbufSwap
/*  f0a7c1c:	00000000 */ 	nop
/*  f0a7c20:	0fc5d8ab */ 	jal	zbufConfigureRdp
/*  f0a7c24:	8fa4014c */ 	lw	$a0, 0x14c($sp)
/*  f0a7c28:	afa2014c */ 	sw	$v0, 0x14c($sp)
/*  f0a7c2c:	0c002c74 */ 	jal	vi0000b1d0
/*  f0a7c30:	00402025 */ or $a0, $v0, $zero
/*  f0a7c34:	244e0008 */ 	addiu	$t6, $v0, 0x8
/*  f0a7c38:	afae014c */ 	sw	$t6, 0x14c($sp)
/*  f0a7c3c:	0c002f40 */ 	jal	viGetViewLeft
/*  f0a7c40:	00408825 */ or $s1, $v0, $zero
/*  f0a7c44:	00028400 */ 	sll	$s0, $v0, 0x10
/*  f0a7c48:	00106403 */ 	sra	$t4, $s0, 0x10
/*  f0a7c4c:	0c002f44 */ 	jal	viGetViewTop
/*  f0a7c50:	01808025 */ or $s0, $t4, $zero
/*  f0a7c54:	44824000 */ 	mtc1	$v0, $f8
/*  f0a7c58:	44902000 */ 	mtc1	$s0, $f4
/*  f0a7c5c:	3c014080 */ 	lui	$at, 0x4080
/*  f0a7c60:	468042a0 */ 	cvt.s.w	$f10, $f8
/*  f0a7c64:	44810000 */ 	mtc1	$at, $f0
/*  f0a7c68:	3c01ed00 */ 	lui	$at, 0xed00
/*  f0a7c6c:	468021a0 */ 	cvt.s.w	$f6, $f4
/*  f0a7c70:	46005402 */ 	mul.s	$f16, $f10, $f0
/*  f0a7c74:	00000000 */ 	nop
/*  f0a7c78:	46003202 */ 	mul.s	$f8, $f6, $f0
/*  f0a7c7c:	4600848d */ 	trunc.w.s	$f18, $f16
/*  f0a7c80:	4600428d */ 	trunc.w.s	$f10, $f8
/*  f0a7c84:	44089000 */ 	mfc1	$t0, $f18
/*  f0a7c88:	44195000 */ 	mfc1	$t9, $f10
/*  f0a7c8c:	310b0fff */ 	andi	$t3, $t0, 0xfff
/*  f0a7c90:	01616825 */ or $t5, $t3, $at
/*  f0a7c94:	332f0fff */ 	andi	$t7, $t9, 0xfff
/*  f0a7c98:	000f4b00 */ 	sll	$t1, $t7, 0xc
/*  f0a7c9c:	01a97025 */ or $t6, $t5, $t1
/*  f0a7ca0:	0c002f22 */ 	jal	viGetViewWidth
/*  f0a7ca4:	ae2e0000 */ 	sw	$t6, 0x0($s1)
/*  f0a7ca8:	00029400 */ 	sll	$s2, $v0, 0x10
/*  f0a7cac:	00126403 */ 	sra	$t4, $s2, 0x10
/*  f0a7cb0:	0c002f40 */ 	jal	viGetViewLeft
/*  f0a7cb4:	01809025 */ or $s2, $t4, $zero
/*  f0a7cb8:	0002a400 */ 	sll	$s4, $v0, 0x10
/*  f0a7cbc:	00145403 */ 	sra	$t2, $s4, 0x10
/*  f0a7cc0:	0c002f44 */ 	jal	viGetViewTop
/*  f0a7cc4:	0140a025 */ or $s4, $t2, $zero
/*  f0a7cc8:	00028400 */ 	sll	$s0, $v0, 0x10
/*  f0a7ccc:	00104403 */ 	sra	$t0, $s0, 0x10
/*  f0a7cd0:	0c002f26 */ 	jal	viGetViewHeight
/*  f0a7cd4:	01008025 */ or $s0, $t0, $zero
/*  f0a7cd8:	00505821 */ 	addu	$t3, $v0, $s0
/*  f0a7cdc:	448b8000 */ 	mtc1	$t3, $f16
/*  f0a7ce0:	02926821 */ 	addu	$t5, $s4, $s2
/*  f0a7ce4:	448d5000 */ 	mtc1	$t5, $f10
/*  f0a7ce8:	468084a0 */ 	cvt.s.w	$f18, $f16
/*  f0a7cec:	3c014080 */ 	lui	$at, 0x4080
/*  f0a7cf0:	44812000 */ 	mtc1	$at, $f4
/*  f0a7cf4:	46805420 */ 	cvt.s.w	$f16, $f10
/*  f0a7cf8:	46049182 */ 	mul.s	$f6, $f18, $f4
/*  f0a7cfc:	44819000 */ 	mtc1	$at, $f18
/*  f0a7d00:	00000000 */ 	nop
/*  f0a7d04:	46128102 */ 	mul.s	$f4, $f16, $f18
/*  f0a7d08:	4600320d */ 	trunc.w.s	$f8, $f6
/*  f0a7d0c:	4600218d */ 	trunc.w.s	$f6, $f4
/*  f0a7d10:	44194000 */ 	mfc1	$t9, $f8
/*  f0a7d14:	440e3000 */ 	mfc1	$t6, $f6
/*  f0a7d18:	332f0fff */ 	andi	$t7, $t9, 0xfff
/*  f0a7d1c:	31cc0fff */ 	andi	$t4, $t6, 0xfff
/*  f0a7d20:	000c5300 */ 	sll	$t2, $t4, 0xc
/*  f0a7d24:	01ea4025 */ or $t0, $t7, $t2
/*  f0a7d28:	ae280004 */ 	sw	$t0, 0x4($s1)
/*  f0a7d2c:	8fb80150 */ 	lw	$t8, 0x150($sp)
/*  f0a7d30:	8fab014c */ 	lw	$t3, 0x14c($sp)
/*  f0a7d34:	af0b0000 */ 	sw	$t3, 0x0($t8)
/*  f0a7d38:	8fbf0034 */ 	lw	$ra, 0x34($sp)
.L0f0a7d3c:
/*  f0a7d3c:	8fb0001c */ 	lw	$s0, 0x1c($sp)
/*  f0a7d40:	8fb10020 */ 	lw	$s1, 0x20($sp)
/*  f0a7d44:	8fb20024 */ 	lw	$s2, 0x24($sp)
/*  f0a7d48:	8fb30028 */ 	lw	$s3, 0x28($sp)
/*  f0a7d4c:	8fb4002c */ 	lw	$s4, 0x2c($sp)
/*  f0a7d50:	8fb50030 */ 	lw	$s5, 0x30($sp)
/*  f0a7d54:	03e00008 */ 	jr	$ra
/*  f0a7d58:	27bd0150 */ 	addiu	$sp, $sp, 0x150
);
#elif VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
		glabel bgunRender
		.late_rodata
		glabel var7f1aca8c
		.word 0x3faaaaab
		glabel var7f1aca90
		.word 0x3f3ebebf
		.text
		/*  f0a7138:	27bdfeb0 */ 	addiu	$sp, $sp, -336
		/*  f0a713c:	afbf0034 */ 	sw	$ra, 0x34($sp)
		/*  f0a7140:	afb50030 */ 	sw	$s5, 0x30($sp)
		/*  f0a7144:	afb4002c */ 	sw	$s4, 0x2c($sp)
		/*  f0a7148:	afb30028 */ 	sw	$s3, 0x28($sp)
		/*  f0a714c:	afb20024 */ 	sw	$s2, 0x24($sp)
		/*  f0a7150:	afb10020 */ 	sw	$s1, 0x20($sp)
		/*  f0a7154:	afb0001c */ 	sw	$s0, 0x1c($sp)
		/*  f0a7158:	afa40150 */ 	sw	$a0, 0x150($sp)
		/*  f0a715c:	8c8f0000 */ 	lw	$t7, 0x0($a0)
		/*  f0a7160:	3c198007 */ 	lui	$t9, % hi(var8007029c)
		/*  f0a7164:	3c11800a */ 	lui	$s1, % hi(g_Vars)
		/*  f0a7168:	2739029c */ 	addiu	$t9, $t9, % lo(var8007029c)
		/*  f0a716c:	26319fc0 */ 	addiu	$s1, $s1, % lo(g_Vars)
		/*  f0a7170:	272a003c */ 	addiu	$t2, $t9, 0x3c
		/*  f0a7174:	27b8010c */ 	addiu	$t8, $sp, 0x10c
		/*  f0a7178:	afaf014c */ 	sw	$t7, 0x14c($sp)
		.L0f0a717c:
/*  f0a717c:	8f210000 */ 	lw	$at, 0x0($t9)
/*  f0a7180:	2739000c */ 	addiu	$t9, $t9, 0xc
/*  f0a7184:	2718000c */ 	addiu	$t8, $t8, 0xc
/*  f0a7188:	af01fff4 */ 	sw	$at, -0xc($t8)
/*  f0a718c:	8f21fff8 */ 	lw	$at, -0x8($t9)
/*  f0a7190:	af01fff8 */ 	sw	$at, -0x8($t8)
/*  f0a7194:	8f21fffc */ 	lw	$at, -0x4($t9)
/*  f0a7198:	172afff8 */ 	bne	$t9, $t2, .L0f0a717c
/*  f0a719c:	af01fffc */ 	sw	$at, -0x4($t8)
/*  f0a71a0:	8f210000 */ 	lw	$at, 0x0($t9)
/*  f0a71a4:	af010000 */ 	sw	$at, 0x0($t8)
/*  f0a71a8:	8e330284 */ 	lw	$s3, 0x284($s1)
/*  f0a71ac:	24010001 */ 	addiu	$at, $zero, 0x1
/*  f0a71b0:	966b0010 */ 	lhu	$t3, 0x10($s3)
/*  f0a71b4:	1561000d */ 	bne	$t3, $at, .L0f0a71ec
/*  f0a71b8:	00001025 */ or $v0, $zero, $zero
/*  f0a71bc:	24040f48 */ 	addiu	$a0, $zero, 0xf48
/*  f0a71c0:	8e2c0284 */ 	lw	$t4, 0x284($s1)
.L0f0a71c4:
/*  f0a71c4:	01821821 */ 	addu	$v1, $t4, $v0
/*  f0a71c8:	8c6d0854 */ 	lw	$t5, 0x854($v1)
/*  f0a71cc:	244207a4 */ 	addiu	$v0, $v0, 0x7a4
/*  f0a71d0:	11a00002 */ 	beqz	$t5, .L0f0a71dc
/*  f0a71d4:	00000000 */ 	nop
/*  f0a71d8:	ac600850 */ 	sw	$zero, 0x850($v1)
.L0f0a71dc:
/*  f0a71dc:	5444fff9 */ 	bnel	$v0, $a0, .L0f0a71c4
/*  f0a71e0:	8e2c0284 */ 	lw	$t4, 0x284($s1)
/*  f0a71e4:	100002d5 */ 	b.L0f0a7d3c
/*  f0a71e8:	8fbf0034 */ 	lw	$ra, 0x34($sp)
.L0f0a71ec :
		/*  f0a71ec:	0fc5d9ad */ 	jal	zbufSaveArtifactDepths
		/*  f0a71f0:	8fa4014c */ 	lw	$a0, 0x14c($sp)
		/*  f0a71f4:	afa2014c */ 	sw	$v0, 0x14c($sp)
		/*  f0a71f8:	0c002ca0 */ 	jal	viPrepareZbuf
		/*  f0a71fc:	00402025 */ or $a0, $v0, $zero
		/*  f0a7200:	afa2014c */ 	sw	$v0, 0x14c($sp)
		/*  f0a7204:	0c002c74 */ 	jal	vi0000b1d0
		/*  f0a7208:	00402025 */ or $a0, $v0, $zero
		/*  f0a720c:	244e0008 */ 	addiu	$t6, $v0, 0x8
		/*  f0a7210:	afae014c */ 	sw	$t6, 0x14c($sp)
		/*  f0a7214:	0c002f40 */ 	jal	viGetViewLeft
		/*  f0a7218:	0040a825 */ or $s5, $v0, $zero
		/*  f0a721c:	00028400 */ 	sll	$s0, $v0, 0x10
		/*  f0a7220:	00107c03 */ 	sra	$t7, $s0, 0x10
		/*  f0a7224:	0c002f44 */ 	jal	viGetViewTop
		/*  f0a7228:	01e08025 */ or $s0, $t7, $zero
		/*  f0a722c:	44822000 */ 	mtc1	$v0, $f4
		/*  f0a7230:	44908000 */ 	mtc1	$s0, $f16
		/*  f0a7234:	3c014080 */ 	lui	$at, 0x4080
		/*  f0a7238:	468021a0 */ 	cvt.s.w	$f6, $f4
		/*  f0a723c:	44810000 */ 	mtc1	$at, $f0
		/*  f0a7240:	3c01ed00 */ 	lui	$at, 0xed00
		/*  f0a7244:	468084a0 */ 	cvt.s.w	$f18, $f16
		/*  f0a7248:	46003202 */ 	mul.s	$f8, $f6, $f0
		/*  f0a724c:	00000000 */ 	nop
		/*  f0a7250:	46009102 */ 	mul.s	$f4, $f18, $f0
		/*  f0a7254:	4600428d */ 	trunc.w.s	$f10, $f8
		/*  f0a7258:	4600218d */ 	trunc.w.s	$f6, $f4
		/*  f0a725c:	44085000 */ 	mfc1	$t0, $f10
		/*  f0a7260:	440b3000 */ 	mfc1	$t3, $f6
		/*  f0a7264:	310a0fff */ 	andi	$t2, $t0, 0xfff
		/*  f0a7268:	0141c825 */ or $t9, $t2, $at
		/*  f0a726c:	316c0fff */ 	andi	$t4, $t3, 0xfff
		/*  f0a7270:	000c6b00 */ 	sll	$t5, $t4, 0xc
		/*  f0a7274:	032d7025 */ or $t6, $t9, $t5
		/*  f0a7278:	0c002f22 */ 	jal	viGetViewWidth
		/*  f0a727c:	aeae0000 */ 	sw	$t6, 0x0($s5)
		/*  f0a7280:	00029400 */ 	sll	$s2, $v0, 0x10
		/*  f0a7284:	00127c03 */ 	sra	$t7, $s2, 0x10
		/*  f0a7288:	0c002f40 */ 	jal	viGetViewLeft
		/*  f0a728c:	01e09025 */ or $s2, $t7, $zero
		/*  f0a7290:	0002a400 */ 	sll	$s4, $v0, 0x10
		/*  f0a7294:	00144c03 */ 	sra	$t1, $s4, 0x10
		/*  f0a7298:	0c002f44 */ 	jal	viGetViewTop
		/*  f0a729c:	0120a025 */ or $s4, $t1, $zero
		/*  f0a72a0:	00028400 */ 	sll	$s0, $v0, 0x10
		/*  f0a72a4:	00104403 */ 	sra	$t0, $s0, 0x10
		/*  f0a72a8:	0c002f26 */ 	jal	viGetViewHeight
		/*  f0a72ac:	01008025 */ or $s0, $t0, $zero
		/*  f0a72b0:	00505021 */ 	addu	$t2, $v0, $s0
		/*  f0a72b4:	448a4000 */ 	mtc1	$t2, $f8
		/*  f0a72b8:	0292c821 */ 	addu	$t9, $s4, $s2
		/*  f0a72bc:	44992000 */ 	mtc1	$t9, $f4
		/*  f0a72c0:	468042a0 */ 	cvt.s.w	$f10, $f8
		/*  f0a72c4:	3c014080 */ 	lui	$at, 0x4080
		/*  f0a72c8:	44810000 */ 	mtc1	$at, $f0
		/*  f0a72cc:	3c053fc0 */ 	lui	$a1, 0x3fc0
		/*  f0a72d0:	3c06447a */ 	lui	$a2, 0x447a
		/*  f0a72d4:	468021a0 */ 	cvt.s.w	$f6, $f4
		/*  f0a72d8:	46005402 */ 	mul.s	$f16, $f10, $f0
		/*  f0a72dc:	00000000 */ 	nop
		/*  f0a72e0:	46003202 */ 	mul.s	$f8, $f6, $f0
		/*  f0a72e4:	4600848d */ 	trunc.w.s	$f18, $f16
		/*  f0a72e8:	4600428d */ 	trunc.w.s	$f10, $f8
		/*  f0a72ec:	440b9000 */ 	mfc1	$t3, $f18
		/*  f0a72f0:	440e5000 */ 	mfc1	$t6, $f10
		/*  f0a72f4:	316c0fff */ 	andi	$t4, $t3, 0xfff
		/*  f0a72f8:	31cf0fff */ 	andi	$t7, $t6, 0xfff
		/*  f0a72fc:	000f4b00 */ 	sll	$t1, $t7, 0xc
		/*  f0a7300:	01894025 */ or $t0, $t4, $t1
		/*  f0a7304:	aea80004 */ 	sw	$t0, 0x4($s5)
		/*  f0a7308:	0c002b29 */ 	jal	vi0000aca4
		/*  f0a730c:	8fa4014c */ 	lw	$a0, 0x14c($sp)
		/*  f0a7310:	8e2a0284 */ 	lw	$t2, 0x284($s1)
		/*  f0a7314:	afa2014c */ 	sw	$v0, 0x14c($sp)
		/*  f0a7318:	91581bfc */ 	lbu	$t8, 0x1bfc($t2)
		/*  f0a731c:	53000016 */ 	beqzl	$t8, .L0f0a7378
		/*  f0a7320:	8e2b006c */ 	lw	$t3, 0x6c($s1)
		/*  f0a7324:	0fc54bc7 */ 	jal	optionsGetScreenRatio
		/*  f0a7328:	00000000 */ 	nop
		/*  f0a732c:	24010001 */ 	addiu	$at, $zero, 0x1
		/*  f0a7330:	14410008 */ 	bne	$v0, $at, .L0f0a7354
		/*  f0a7334:	00000000 */ 	nop
		/*  f0a7338:	0fc2f4d6 */ 	jal	player0f0bd358
		/*  f0a733c:	00000000 */ 	nop
		/*  f0a7340:	3c017f1b */ 	lui	$at, % hi(var7f1aca8c)
		/*  f0a7344:	c430ca8c */ 	lwc1	$f16, % lo(var7f1aca8c)($at)
		/*  f0a7348:	46100082 */ 	mul.s	$f2, $f0, $f16
		/*  f0a734c:	10000005 */ 	b.L0f0a7364
		/*  f0a7350:	44061000 */ 	mfc1	$a2, $f2
		.L0f0a7354:
/*  f0a7354:	0fc2f4d6 */ 	jal	player0f0bd358
/*  f0a7358:	00000000 */ 	nop
/*  f0a735c:	46000086 */ 	mov.s	$f2, $f0
/*  f0a7360:	44061000 */ 	mfc1	$a2, $f2
.L0f0a7364 :
		/*  f0a7364:	8fa4014c */ 	lw	$a0, 0x14c($sp)
		/*  f0a7368:	0c002c3a */ 	jal	vi0000b0e8
		/*  f0a736c:	3c054270 */ 	lui	$a1, 0x4270
		/*  f0a7370:	afa2014c */ 	sw	$v0, 0x14c($sp)
		/*  f0a7374:	8e2b006c */ 	lw	$t3, 0x6c($s1)
		.L0f0a7378:
/*  f0a7378:	24010001 */ 	addiu	$at, $zero, 0x1
/*  f0a737c:	51600004 */ 	beqzl	$t3, .L0f0a7390
/*  f0a7380:	00002825 */ or $a1, $zero, $zero
/*  f0a7384:	10000002 */ 	b.L0f0a7390
/*  f0a7388:	24050001 */ 	addiu	$a1, $zero, 0x1
/*  f0a738c:	00002825 */ or $a1, $zero, $zero
.L0f0a7390:
/*  f0a7390:	8e390068 */ 	lw	$t9, 0x68($s1)
/*  f0a7394:	53200004 */ 	beqzl	$t9, .L0f0a73a8
/*  f0a7398:	00002025 */ or $a0, $zero, $zero
/*  f0a739c:	10000002 */ 	b.L0f0a73a8
/*  f0a73a0:	24040001 */ 	addiu	$a0, $zero, 0x1
/*  f0a73a4:	00002025 */ or $a0, $zero, $zero
.L0f0a73a8:
/*  f0a73a8:	8e2d0064 */ 	lw	$t5, 0x64($s1)
/*  f0a73ac:	51a00004 */ 	beqzl	$t5, .L0f0a73c0
/*  f0a73b0:	00001025 */ or $v0, $zero, $zero
/*  f0a73b4:	10000002 */ 	b.L0f0a73c0
/*  f0a73b8:	24020001 */ 	addiu	$v0, $zero, 0x1
/*  f0a73bc:	00001025 */ or $v0, $zero, $zero
.L0f0a73c0:
/*  f0a73c0:	8e2e0070 */ 	lw	$t6, 0x70($s1)
/*  f0a73c4:	51c00004 */ 	beqzl	$t6, .L0f0a73d8
/*  f0a73c8:	00001825 */ or $v1, $zero, $zero
/*  f0a73cc:	10000002 */ 	b.L0f0a73d8
/*  f0a73d0:	24030001 */ 	addiu	$v1, $zero, 0x1
/*  f0a73d4:	00001825 */ or $v1, $zero, $zero
.L0f0a73d8:
/*  f0a73d8:	00627821 */ 	addu	$t7, $v1, $v0
/*  f0a73dc:	01e46021 */ 	addu	$t4, $t7, $a0
/*  f0a73e0:	01854821 */ 	addu	$t1, $t4, $a1
/*  f0a73e4:	15210008 */ 	bne	$t1, $at, .L0f0a7408
/*  f0a73e8:	3c088009 */ 	lui	$t0, % hi(g_Is4Mb)
/*  f0a73ec:	91080af0 */ 	lbu	$t0, % lo(g_Is4Mb)($t0)
/*  f0a73f0:	24010001 */ 	addiu	$at, $zero, 0x1
/*  f0a73f4:	51010005 */ 	beql	$t0, $at, .L0f0a740c
/*  f0a73f8:	0000a025 */ or $s4, $zero, $zero
/*  f0a73fc:	0fc2be93 */ 	jal	lasersightRenderBeam
/*  f0a7400:	8fa4014c */ 	lw	$a0, 0x14c($sp)
/*  f0a7404:	afa2014c */ 	sw	$v0, 0x14c($sp)
.L0f0a7408:
/*  f0a7408:	0000a025 */ or $s4, $zero, $zero
.L0f0a740c :
		/*  f0a740c:	26700638 */ 	addiu	$s0, $s3, 0x638
		/*  f0a7410:	8fb500e4 */ 	lw	$s5, 0xe4($sp)
		/*  f0a7414:	24120019 */ 	addiu	$s2, $zero, 0x19
		.L0f0a7418 :
		/*  f0a7418:	0fc2867c */ 	jal	bgunGetWeaponNum2
		/*  f0a741c:	02802025 */ or $a0, $s4, $zero
		/*  f0a7420:	afa200ec */ 	sw	$v0, 0xec($sp)
		/*  f0a7424:	820a0007 */ 	lb	$t2, 0x7($s0)
		/*  f0a7428:	8fa4014c */ 	lw	$a0, 0x14c($sp)
		/*  f0a742c:	260501dc */ 	addiu	$a1, $s0, 0x1dc
		/*  f0a7430:	114001f2 */ 	beqz	$t2, .L0f0a7bfc
		/*  f0a7434:	00003025 */ or $a2, $zero, $zero
		/*  f0a7438:	26180384 */ 	addiu	$t8, $s0, 0x384
		/*  f0a743c:	afb8003c */ 	sw	$t8, 0x3c($sp)
		/*  f0a7440:	0fc2b2e4 */ 	jal	beamRender
		/*  f0a7444:	00003825 */ or $a3, $zero, $zero
		/*  f0a7448:	afa2014c */ 	sw	$v0, 0x14c($sp)
		/*  f0a744c:	92040000 */ 	lbu	$a0, 0x0($s0)
		/*  f0a7450:	0fc2c5f0 */ 	jal	weaponHasFlag
		/*  f0a7454:	34058000 */ 	dli	$a1, 0x8000
		/*  f0a7458:	10400030 */ 	beqz	$v0, .L0f0a751c
		/*  f0a745c:	8fab014c */ 	lw	$t3, 0x14c($sp)
		/*  f0a7460:	25790008 */ 	addiu	$t9, $t3, 0x8
		/*  f0a7464:	afb9014c */ 	sw	$t9, 0x14c($sp)
		/*  f0a7468:	3c0dbc00 */ 	lui	$t5, 0xbc00
		/*  f0a746c:	3c0e8000 */ 	lui	$t6, 0x8000
		/*  f0a7470:	35ce0040 */ 	ori	$t6, $t6, 0x40
		/*  f0a7474:	35ad0002 */ 	ori	$t5, $t5, 0x2
		/*  f0a7478:	ad6d0000 */ 	sw	$t5, 0x0($t3)
		/*  f0a747c:	ad6e0004 */ 	sw	$t6, 0x4($t3)
		/*  f0a7480:	8faf014c */ 	lw	$t7, 0x14c($sp)
		/*  f0a7484:	3c090386 */ 	lui	$t1, 0x386
		/*  f0a7488:	3c088007 */ 	lui	$t0, % hi(var80070090 + 0x08)
		/*  f0a748c:	25ec0008 */ 	addiu	$t4, $t7, 0x8
		/*  f0a7490:	afac014c */ 	sw	$t4, 0x14c($sp)
		/*  f0a7494:	25080098 */ 	addiu	$t0, $t0, % lo(var80070090 + 0x08)
		/*  f0a7498:	35290010 */ 	ori	$t1, $t1, 0x10
		/*  f0a749c:	ade90000 */ 	sw	$t1, 0x0($t7)
		/*  f0a74a0:	ade80004 */ 	sw	$t0, 0x4($t7)
		/*  f0a74a4:	8faa014c */ 	lw	$t2, 0x14c($sp)
		/*  f0a74a8:	3c0b0388 */ 	lui	$t3, 0x388
		/*  f0a74ac:	3c198007 */ 	lui	$t9, % hi(var80070090)
		/*  f0a74b0:	25580008 */ 	addiu	$t8, $t2, 0x8
		/*  f0a74b4:	afb8014c */ 	sw	$t8, 0x14c($sp)
		/*  f0a74b8:	27390090 */ 	addiu	$t9, $t9, % lo(var80070090)
		/*  f0a74bc:	356b0010 */ 	ori	$t3, $t3, 0x10
		/*  f0a74c0:	ad4b0000 */ 	sw	$t3, 0x0($t2)
		/*  f0a74c4:	ad590004 */ 	sw	$t9, 0x4($t2)
		/*  f0a74c8:	8fad014c */ 	lw	$t5, 0x14c($sp)
		/*  f0a74cc:	3c0f0384 */ 	lui	$t7, 0x384
		/*  f0a74d0:	35ef0010 */ 	ori	$t7, $t7, 0x10
		/*  f0a74d4:	25ae0008 */ 	addiu	$t6, $t5, 0x8
		/*  f0a74d8:	afae014c */ 	sw	$t6, 0x14c($sp)
		/*  f0a74dc:	adaf0000 */ 	sw	$t7, 0x0($t5)
		/*  f0a74e0:	0fc2d5ea */ 	jal	camGetLookAt
		/*  f0a74e4:	afad00d4 */ 	sw	$t5, 0xd4($sp)
		/*  f0a74e8:	8fa500d4 */ 	lw	$a1, 0xd4($sp)
		/*  f0a74ec:	3c080382 */ 	lui	$t0, 0x382
		/*  f0a74f0:	35080010 */ 	ori	$t0, $t0, 0x10
		/*  f0a74f4:	aca20004 */ 	sw	$v0, 0x4($a1)
		/*  f0a74f8:	8fac014c */ 	lw	$t4, 0x14c($sp)
		/*  f0a74fc:	25890008 */ 	addiu	$t1, $t4, 0x8
		/*  f0a7500:	afa9014c */ 	sw	$t1, 0x14c($sp)
		/*  f0a7504:	ad880000 */ 	sw	$t0, 0x0($t4)
		/*  f0a7508:	0fc2d5ea */ 	jal	camGetLookAt
		/*  f0a750c:	afac00d0 */ 	sw	$t4, 0xd0($sp)
		/*  f0a7510:	8fa300d0 */ 	lw	$v1, 0xd0($sp)
		/*  f0a7514:	244a0010 */ 	addiu	$t2, $v0, 0x10
		/*  f0a7518:	ac6a0004 */ 	sw	$t2, 0x4($v1)
		.L0f0a751c:
/*  f0a751c:	8fb8014c */ 	lw	$t8, 0x14c($sp)
/*  f0a7520:	3c19bc00 */ 	lui	$t9, 0xbc00
/*  f0a7524:	3739000e */ 	ori	$t9, $t9, 0xe
/*  f0a7528:	270b0008 */ 	addiu	$t3, $t8, 0x8
/*  f0a752c:	afab014c */ 	sw	$t3, 0x14c($sp)
/*  f0a7530:	3c014396 */ 	lui	$at, 0x4396
/*  f0a7534:	44817000 */ 	mtc1	$at, $f14
/*  f0a7538:	44806000 */ 	mtc1	$zero, $f12
/*  f0a753c:	af190000 */ 	sw	$t9, 0x0($t8)
/*  f0a7540:	0c005b73 */ 	jal	mtx00016dcc
/*  f0a7544:	afb800cc */ 	sw	$t8, 0xcc($sp)
/*  f0a7548:	8fa300cc */ 	lw	$v1, 0xcc($sp)
/*  f0a754c:	24050010 */ 	addiu	$a1, $zero, 0x10
/*  f0a7550:	ac620004 */ 	sw	$v0, 0x4($v1)
/*  f0a7554:	0c006a47 */ 	jal	modelGetPart
/*  f0a7558:	8e04038c */ 	lw	$a0, 0x38c($s0)
/*  f0a755c:	10400014 */ 	beqz	$v0, .L0f0a75b0
/*  f0a7560:	afa200e8 */ 	sw	$v0, 0xe8($sp)
/*  f0a7564:	8e04038c */ 	lw	$a0, 0x38c($s0)
/*  f0a7568:	0c006a47 */ 	jal	modelGetPart
/*  f0a756c:	24050011 */ 	addiu	$a1, $zero, 0x11
/*  f0a7570:	8fa4003c */ 	lw	$a0, 0x3c($sp)
/*  f0a7574:	0c006a87 */ 	jal	modelGetNodeRwData
/*  f0a7578:	00402825 */ or $a1, $v0, $zero
/*  f0a757c:	10400003 */ 	beqz	$v0, .L0f0a758c
/*  f0a7580:	3c06800a */ 	lui	$a2, % hi(var8009cf88)
/*  f0a7584:	240d0001 */ 	addiu	$t5, $zero, 0x1
/*  f0a7588:	ac4d0000 */ 	sw	$t5, 0x0($v0)
.L0f0a758c:
/*  f0a758c:	240e0001 */ 	addiu	$t6, $zero, 0x1
/*  f0a7590:	afae0014 */ 	sw	$t6, 0x14($sp)
/*  f0a7594:	8fa4003c */ 	lw	$a0, 0x3c($sp)
/*  f0a7598:	8fa500e8 */ 	lw	$a1, 0xe8($sp)
/*  f0a759c:	24c6cf88 */ 	addiu	$a2, $a2, % lo(var8009cf88)
/*  f0a75a0:	8fa7014c */ 	lw	$a3, 0x14c($sp)
/*  f0a75a4:	0fc1fefe */ 	jal	tvscreenRender
/*  f0a75a8:	afa00010 */ 	sw	$zero, 0x10($sp)
/*  f0a75ac:	afa2014c */ 	sw	$v0, 0x14c($sp)
.L0f0a75b0:
/*  f0a75b0:	8faf014c */ 	lw	$t7, 0x14c($sp)
/*  f0a75b4:	8e250284 */ 	lw	$a1, 0x284($s1)
/*  f0a75b8:	240c0004 */ 	addiu	$t4, $zero, 0x4
/*  f0a75bc:	afac013c */ 	sw	$t4, 0x13c($sp)
/*  f0a75c0:	afaf0118 */ 	sw	$t7, 0x118($sp)
/*  f0a75c4:	8ca300d8 */ 	lw	$v1, 0xd8($a1)
/*  f0a75c8:	3c078007 */ 	lui	$a3, % hi(g_InCutscene)
/*  f0a75cc:	14600013 */ 	bnez	$v1, .L0f0a761c
/*  f0a75d0:	00000000 */ 	nop
/*  f0a75d4:	8ce70764 */ 	lw	$a3, % lo(g_InCutscene)($a3)
/*  f0a75d8:	14e00010 */ 	bnez	$a3, .L0f0a761c
/*  f0a75dc:	00000000 */ 	nop
/*  f0a75e0:	8ca20480 */ 	lw	$v0, 0x480($a1)
/*  f0a75e4:	50400007 */ 	beqzl	$v0, .L0f0a7604
/*  f0a75e8:	8caa1c54 */ 	lw	$t2, 0x1c54($a1)
/*  f0a75ec:	1040000b */ 	beqz	$v0, .L0f0a761c
/*  f0a75f0:	00000000 */ 	nop
/*  f0a75f4:	80490037 */ 	lb	$t1, 0x37($v0)
/*  f0a75f8:	15200008 */ 	bnez	$t1, .L0f0a761c
/*  f0a75fc:	00000000 */ 	nop
/*  f0a7600:	8caa1c54 */ 	lw	$t2, 0x1c54($a1)
.L0f0a7604:
/*  f0a7604:	8ca800c4 */ 	lw	$t0, 0xc4($a1)
/*  f0a7608:	0140c027 */ 	nor	$t8, $t2, $zero
/*  f0a760c:	01185824 */ and $t3, $t0, $t8
/*  f0a7610:	31790001 */ 	andi	$t9, $t3, 0x1
/*  f0a7614:	57200016 */ 	bnezl	$t9, .L0f0a7670
/*  f0a7618:	92681615 */ 	lbu	$t0, 0x1615($s3)
.L0f0a761c:
/*  f0a761c:	14600078 */ 	bnez	$v1, .L0f0a7800
/*  f0a7620:	3c078007 */ 	lui	$a3, % hi(g_InCutscene)
/*  f0a7624:	8ce70764 */ 	lw	$a3, % lo(g_InCutscene)($a3)
/*  f0a7628:	54e00076 */ 	bnezl	$a3, .L0f0a7804
/*  f0a762c:	926d1614 */ 	lbu	$t5, 0x1614($s3)
/*  f0a7630:	8ca20480 */ 	lw	$v0, 0x480($a1)
/*  f0a7634:	50400007 */ 	beqzl	$v0, .L0f0a7654
/*  f0a7638:	8caf1c54 */ 	lw	$t7, 0x1c54($a1)
/*  f0a763c:	50400071 */ 	beqzl	$v0, .L0f0a7804
/*  f0a7640:	926d1614 */ 	lbu	$t5, 0x1614($s3)
/*  f0a7644:	804d0037 */ 	lb	$t5, 0x37($v0)
/*  f0a7648:	55a0006e */ 	bnezl	$t5, .L0f0a7804
/*  f0a764c:	926d1614 */ 	lbu	$t5, 0x1614($s3)
/*  f0a7650:	8caf1c54 */ 	lw	$t7, 0x1c54($a1)
.L0f0a7654:
/*  f0a7654:	8cae00c4 */ 	lw	$t6, 0xc4($a1)
/*  f0a7658:	01e06027 */ 	nor	$t4, $t7, $zero
/*  f0a765c:	01cc4824 */ and $t1, $t6, $t4
/*  f0a7660:	312a0008 */ 	andi	$t2, $t1, 0x8
/*  f0a7664:	51400067 */ 	beqzl	$t2, .L0f0a7804
/*  f0a7668:	926d1614 */ 	lbu	$t5, 0x1614($s3)
/*  f0a766c:	92681615 */ 	lbu	$t0, 0x1615($s3)
.L0f0a7670:
/*  f0a7670:	92781614 */ 	lbu	$t8, 0x1614($s3)
/*  f0a7674:	26641614 */ 	addiu	$a0, $s3, 0x1614
/*  f0a7678:	0118082a */ 	slt	$at, $t0, $t8
/*  f0a767c:	50200009 */ 	beqzl	$at, .L0f0a76a4
/*  f0a7680:	90820002 */ 	lbu	$v0, 0x2($a0)
/*  f0a7684:	90830000 */ 	lbu	$v1, 0x0($a0)
/*  f0a7688:	908b0002 */ 	lbu	$t3, 0x2($a0)
/*  f0a768c:	0163082a */ 	slt	$at, $t3, $v1
/*  f0a7690:	50200004 */ 	beqzl	$at, .L0f0a76a4
/*  f0a7694:	90820002 */ 	lbu	$v0, 0x2($a0)
/*  f0a7698:	1000000a */ 	b.L0f0a76c4
/*  f0a769c:	00601025 */ or $v0, $v1, $zero
/*  f0a76a0:	90820002 */ 	lbu	$v0, 0x2($a0)
.L0f0a76a4:
/*  f0a76a4:	90860001 */ 	lbu	$a2, 0x1($a0)
/*  f0a76a8:	00401825 */ or $v1, $v0, $zero
/*  f0a76ac:	0046082a */ 	slt	$at, $v0, $a2
/*  f0a76b0:	10200003 */ 	beqz	$at, .L0f0a76c0
/*  f0a76b4:	00000000 */ 	nop
/*  f0a76b8:	10000001 */ 	b.L0f0a76c0
/*  f0a76bc:	00c01825 */ or $v1, $a2, $zero
.L0f0a76c0:
/*  f0a76c0:	00601025 */ or $v0, $v1, $zero
.L0f0a76c4 :
		/*  f0a76c4:	90890003 */ 	lbu	$t1, 0x3($a0)
		/*  f0a76c8:	0002ce00 */ 	sll	$t9, $v0, 0x18
		/*  f0a76cc:	00026c00 */ 	sll	$t5, $v0, 0x10
		/*  f0a76d0:	032d7825 */ or $t7, $t9, $t5
		/*  f0a76d4:	00027200 */ 	sll	$t6, $v0, 0x8
		/*  f0a76d8:	01ee6025 */ or $t4, $t7, $t6
		/*  f0a76dc:	012c5021 */ 	addu	$t2, $t1, $t4
		/*  f0a76e0:	afaa0140 */ 	sw	$t2, 0x140($sp)
		/*  f0a76e4:	8ca300d8 */ 	lw	$v1, 0xd8($a1)
		/*  f0a76e8:	14600021 */ 	bnez	$v1, .L0f0a7770
		/*  f0a76ec:	00000000 */ 	nop
		/*  f0a76f0:	14e0001f */ 	bnez	$a3, .L0f0a7770
		/*  f0a76f4:	00000000 */ 	nop
		/*  f0a76f8:	8ca20480 */ 	lw	$v0, 0x480($a1)
		/*  f0a76fc:	50400007 */ 	beqzl	$v0, .L0f0a771c
		/*  f0a7700:	8cab1c54 */ 	lw	$t3, 0x1c54($a1)
		/*  f0a7704:	1040001a */ 	beqz	$v0, .L0f0a7770
		/*  f0a7708:	00000000 */ 	nop
		/*  f0a770c:	80480037 */ 	lb	$t0, 0x37($v0)
		/*  f0a7710:	15000017 */ 	bnez	$t0, .L0f0a7770
		/*  f0a7714:	00000000 */ 	nop
		/*  f0a7718:	8cab1c54 */ 	lw	$t3, 0x1c54($a1)
		.L0f0a771c:
/*  f0a771c:	8cb800c4 */ 	lw	$t8, 0xc4($a1)
/*  f0a7720:	3c02800a */ 	lui	$v0, % hi(var8009caec + 0x3)
/*  f0a7724:	0160c827 */ 	nor	$t9, $t3, $zero
/*  f0a7728:	03196824 */ and $t5, $t8, $t9
/*  f0a772c:	31af0001 */ 	andi	$t7, $t5, 0x1
/*  f0a7730:	11e0000f */ 	beqz	$t7, .L0f0a7770
/*  f0a7734:	00000000 */ 	nop
/*  f0a7738:	9042caef */ 	lbu	$v0, % lo(var8009caec + 0x3)($v0)
/*  f0a773c:	3c06800a */ 	lui	$a2, % hi(var8009caf0)
/*  f0a7740:	90c6caf0 */ 	lbu	$a2, % lo(var8009caf0)($a2)
/*  f0a7744:	00027600 */ 	sll	$t6, $v0, 0x18
/*  f0a7748:	00024c00 */ 	sll	$t1, $v0, 0x10
/*  f0a774c:	01c96025 */ or $t4, $t6, $t1
/*  f0a7750:	00025200 */ 	sll	$t2, $v0, 0x8
/*  f0a7754:	018a4025 */ or $t0, $t4, $t2
/*  f0a7758:	afa200b0 */ 	sw	$v0, 0xb0($sp)
/*  f0a775c:	afa200b4 */ 	sw	$v0, 0xb4($sp)
/*  f0a7760:	afa200b8 */ 	sw	$v0, 0xb8($sp)
/*  f0a7764:	00c8a821 */ 	addu	$s5, $a2, $t0
/*  f0a7768:	1000001f */ 	b.L0f0a77e8
/*  f0a776c:	afa600bc */ 	sw	$a2, 0xbc($sp)
.L0f0a7770:
/*  f0a7770:	5460001e */ 	bnezl	$v1, .L0f0a77ec
/*  f0a7774:	8fb900ec */ 	lw	$t9, 0xec($sp)
/*  f0a7778:	54e0001c */ 	bnezl	$a3, .L0f0a77ec
/*  f0a777c:	8fb900ec */ 	lw	$t9, 0xec($sp)
/*  f0a7780:	8ca20480 */ 	lw	$v0, 0x480($a1)
/*  f0a7784:	50400007 */ 	beqzl	$v0, .L0f0a77a4
/*  f0a7788:	8cb91c54 */ 	lw	$t9, 0x1c54($a1)
/*  f0a778c:	50400017 */ 	beqzl	$v0, .L0f0a77ec
/*  f0a7790:	8fb900ec */ 	lw	$t9, 0xec($sp)
/*  f0a7794:	804b0037 */ 	lb	$t3, 0x37($v0)
/*  f0a7798:	55600014 */ 	bnezl	$t3, .L0f0a77ec
/*  f0a779c:	8fb900ec */ 	lw	$t9, 0xec($sp)
/*  f0a77a0:	8cb91c54 */ 	lw	$t9, 0x1c54($a1)
.L0f0a77a4:
/*  f0a77a4:	8cb800c4 */ 	lw	$t8, 0xc4($a1)
/*  f0a77a8:	240200ff */ 	addiu	$v0, $zero, 0xff
/*  f0a77ac:	03206827 */ 	nor	$t5, $t9, $zero
/*  f0a77b0:	030d7824 */ and $t7, $t8, $t5
/*  f0a77b4:	31ee0008 */ 	andi	$t6, $t7, 0x8
/*  f0a77b8:	11c0000b */ 	beqz	$t6, .L0f0a77e8
/*  f0a77bc:	24050080 */ 	addiu	$a1, $zero, 0x80
/*  f0a77c0:	00024e00 */ 	sll	$t1, $v0, 0x18
/*  f0a77c4:	00006400 */ 	sll	$t4, $zero, 0x10
/*  f0a77c8:	012c5025 */ or $t2, $t1, $t4
/*  f0a77cc:	00004200 */ 	sll	$t0, $zero, 0x8
/*  f0a77d0:	01485825 */ or $t3, $t2, $t0
/*  f0a77d4:	00aba821 */ 	addu	$s5, $a1, $t3
/*  f0a77d8:	afa200a0 */ 	sw	$v0, 0xa0($sp)
/*  f0a77dc:	afa000a4 */ 	sw	$zero, 0xa4($sp)
/*  f0a77e0:	afa000a8 */ 	sw	$zero, 0xa8($sp)
/*  f0a77e4:	afa500ac */ 	sw	$a1, 0xac($sp)
.L0f0a77e8:
/*  f0a77e8:	8fb900ec */ 	lw	$t9, 0xec($sp)
.L0f0a77ec :
		/*  f0a77ec:	24010001 */ 	addiu	$at, $zero, 0x1
		/*  f0a77f0:	5721003d */ 	bnel	$t9, $at, .L0f0a78e8
		/*  f0a77f4:	8e6f00bc */ 	lw	$t7, 0xbc($s3)
		/*  f0a77f8:	1000003a */ 	b.L0f0a78e4
		/*  f0a77fc:	afb50140 */ 	sw	$s5, 0x140($sp)
		.L0f0a7800:
/*  f0a7800:	926d1614 */ 	lbu	$t5, 0x1614($s3)
.L0f0a7804 :
		/*  f0a7804:	92781617 */ 	lbu	$t8, 0x1617($s3)
		/*  f0a7808:	92691615 */ 	lbu	$t1, 0x1615($s3)
		/*  f0a780c:	92681616 */ 	lbu	$t0, 0x1616($s3)
		/*  f0a7810:	000d7e00 */ 	sll	$t7, $t5, 0x18
		/*  f0a7814:	030f7025 */ or $t6, $t8, $t7
		/*  f0a7818:	00096400 */ 	sll	$t4, $t1, 0x10
		/*  f0a781c:	01cc5025 */ or $t2, $t6, $t4
		/*  f0a7820:	00085a00 */ 	sll	$t3, $t0, 0x8
		/*  f0a7824:	014bc825 */ or $t9, $t2, $t3
		/*  f0a7828:	afb90140 */ 	sw	$t9, 0x140($sp)
		/*  f0a782c:	920d0000 */ 	lbu	$t5, 0x0($s0)
		/*  f0a7830:	24010006 */ 	addiu	$at, $zero, 0x6
		/*  f0a7834:	0320a825 */ or $s5, $t9, $zero
		/*  f0a7838:	15a1002a */ 	bne	$t5, $at, .L0f0a78e4
		/*  f0a783c:	3c04ff00 */ 	lui	$a0, 0xff00
		/*  f0a7840:	3c014248 */ 	lui	$at, 0x4248
		/*  f0a7844:	44812000 */ 	mtc1	$at, $f4
		/*  f0a7848:	c612023c */ 	lwc1	$f18, 0x23c($s0)
		/*  f0a784c:	24060001 */ 	addiu	$a2, $zero, 0x1
		/*  f0a7850:	3c014f00 */ 	lui	$at, 0x4f00
		/*  f0a7854:	46049182 */ 	mul.s	$f6, $f18, $f4
		/*  f0a7858:	3484007f */ 	ori	$a0, $a0, 0x7f
		/*  f0a785c:	4458f800 */ 	cfc1	$t8, $31
		/*  f0a7860:	44c6f800 */ 	ctc1	$a2, $31
		/*  f0a7864:	00000000 */ 	nop
		/*  f0a7868:	46003224 */ 	cvt.w.s	$f8, $f6
		/*  f0a786c:	4446f800 */ 	cfc1	$a2, $31
		/*  f0a7870:	00000000 */ 	nop
		/*  f0a7874:	30c60078 */ 	andi	$a2, $a2, 0x78
		/*  f0a7878:	50c00013 */ 	beqzl	$a2, .L0f0a78c8
		/*  f0a787c:	44064000 */ 	mfc1	$a2, $f8
		/*  f0a7880:	44814000 */ 	mtc1	$at, $f8
		/*  f0a7884:	24060001 */ 	addiu	$a2, $zero, 0x1
		/*  f0a7888:	46083201 */ 	sub.s	$f8, $f6, $f8
		/*  f0a788c:	44c6f800 */ 	ctc1	$a2, $31
		/*  f0a7890:	00000000 */ 	nop
		/*  f0a7894:	46004224 */ 	cvt.w.s	$f8, $f8
		/*  f0a7898:	4446f800 */ 	cfc1	$a2, $31
		/*  f0a789c:	00000000 */ 	nop
		/*  f0a78a0:	30c60078 */ 	andi	$a2, $a2, 0x78
		/*  f0a78a4:	14c00005 */ 	bnez	$a2, .L0f0a78bc
		/*  f0a78a8:	00000000 */ 	nop
		/*  f0a78ac:	44064000 */ 	mfc1	$a2, $f8
		/*  f0a78b0:	3c018000 */ 	lui	$at, 0x8000
		/*  f0a78b4:	10000007 */ 	b.L0f0a78d4
		/*  f0a78b8:	00c13025 */ or $a2, $a2, $at
		.L0f0a78bc:
/*  f0a78bc:	10000005 */ 	b.L0f0a78d4
/*  f0a78c0:	2406ffff */ 	addiu	$a2, $zero, -1
/*  f0a78c4:	44064000 */ 	mfc1	$a2, $f8
.L0f0a78c8 :
		/*  f0a78c8:	00000000 */ 	nop
		/*  f0a78cc:	04c0fffb */ 	bltz	$a2, .L0f0a78bc
		/*  f0a78d0:	00000000 */ 	nop
		.L0f0a78d4 :
		/*  f0a78d4:	44d8f800 */ 	ctc1	$t8, $31
		/*  f0a78d8:	0fc01a40 */ 	jal	colourBlend
		/*  f0a78dc:	03202825 */ or $a1, $t9, $zero
		/*  f0a78e0:	afa20140 */ 	sw	$v0, 0x140($sp)
		.L0f0a78e4 :
		/*  f0a78e4:	8e6f00bc */ 	lw	$t7, 0xbc($s3)
		.L0f0a78e8 :
		/*  f0a78e8:	0fc08af9 */ 	jal	chrGetCloakAlpha
		/*  f0a78ec:	8de40004 */ 	lw	$a0, 0x4($t7)
		/*  f0a78f0:	284100ff */ 	slti	$at, $v0, 0xff
		/*  f0a78f4:	1020000f */ 	beqz	$at, .L0f0a7934
		/*  f0a78f8:	240c0001 */ 	addiu	$t4, $zero, 0x1
		/*  f0a78fc:	44825000 */ 	mtc1	$v0, $f10
		/*  f0a7900:	3c017f1b */ 	lui	$at, % hi(var7f1aca90)
		/*  f0a7904:	c432ca90 */ 	lwc1	$f18, % lo(var7f1aca90)($at)
		/*  f0a7908:	46805420 */ 	cvt.s.w	$f16, $f10
		/*  f0a790c:	8fa40140 */ 	lw	$a0, 0x140($sp)
		/*  f0a7910:	240e0005 */ 	addiu	$t6, $zero, 0x5
		/*  f0a7914:	afae013c */ 	sw	$t6, 0x13c($sp)
		/*  f0a7918:	afa40144 */ 	sw	$a0, 0x144($sp)
		/*  f0a791c:	46128102 */ 	mul.s	$f4, $f16, $f18
		/*  f0a7920:	4600218d */ 	trunc.w.s	$f6, $f4
		/*  f0a7924:	44033000 */ 	mfc1	$v1, $f6
		/*  f0a7928:	00000000 */ 	nop
		/*  f0a792c:	24750041 */ 	addiu	$s5, $v1, 0x41
		/*  f0a7930:	afb50140 */ 	sw	$s5, 0x140($sp)
		.L0f0a7934:
/*  f0a7934:	0c0059d8 */ 	jal	mtx00016760
/*  f0a7938:	afac0110 */ 	sw	$t4, 0x110($sp)
/*  f0a793c:	8e020218 */ 	lw	$v0, 0x218($s0)
/*  f0a7940:	50400017 */ 	beqzl	$v0, .L0f0a79a0
/*  f0a7944:	8fa400ec */ 	lw	$a0, 0xec($sp)
/*  f0a7948:	8c460018 */ 	lw	$a2, 0x18($v0)
/*  f0a794c:	afa00094 */ 	sw	$zero, 0x94($sp)
/*  f0a7950:	50c00013 */ 	beqzl	$a2, .L0f0a79a0
/*  f0a7954:	8fa400ec */ 	lw	$a0, 0xec($sp)
/*  f0a7958:	8cc80008 */ 	lw	$t0, 0x8($a2)
/*  f0a795c:	240a0001 */ 	addiu	$t2, $zero, 0x1
/*  f0a7960:	27a4010c */ 	addiu	$a0, $sp, 0x10c
/*  f0a7964:	1100000d */ 	beqz	$t0, .L0f0a799c
/*  f0a7968:	00c02825 */ or $a1, $a2, $zero
/*  f0a796c:	afaa0094 */ 	sw	$t2, 0x94($sp)
/*  f0a7970:	0c0087bd */ 	jal	modelRender
/*  f0a7974:	afa60098 */ 	sw	$a2, 0x98($sp)
/*  f0a7978:	8fa60098 */ 	lw	$a2, 0x98($sp)
/*  f0a797c:	8ccb0008 */ 	lw	$t3, 0x8($a2)
/*  f0a7980:	8cc4000c */ 	lw	$a0, 0xc($a2)
/*  f0a7984:	0fc30cfc */ 	jal	mtxF2LBulk
/*  f0a7988:	8565000e */ 	lh	$a1, 0xe($t3)
/*  f0a798c:	8e0d021c */ 	lw	$t5, 0x21c($s0)
/*  f0a7990:	51a00003 */ 	beqzl	$t5, .L0f0a79a0
/*  f0a7994:	8fa400ec */ 	lw	$a0, 0xec($sp)
/*  f0a7998:	ae000218 */ 	sw	$zero, 0x218($s0)
.L0f0a799c:
/*  f0a799c:	8fa400ec */ 	lw	$a0, 0xec($sp)
.L0f0a79a0 :
		/*  f0a79a0:	0fc2c5f0 */ 	jal	weaponHasFlag
		/*  f0a79a4:	24050020 */ 	addiu	$a1, $zero, 0x20
		/*  f0a79a8:	1040000e */ 	beqz	$v0, .L0f0a79e4
		/*  f0a79ac:	24010001 */ 	addiu	$at, $zero, 0x1
		/*  f0a79b0:	8fb80118 */ 	lw	$t8, 0x118($sp)
		/*  f0a79b4:	3c0fb600 */ 	lui	$t7, 0xb600
		/*  f0a79b8:	24093000 */ 	addiu	$t1, $zero, 0x3000
		/*  f0a79bc:	27190008 */ 	addiu	$t9, $t8, 0x8
		/*  f0a79c0:	afb90118 */ 	sw	$t9, 0x118($sp)
		/*  f0a79c4:	af090004 */ 	sw	$t1, 0x4($t8)
		/*  f0a79c8:	16800004 */ 	bnez	$s4, .L0f0a79dc
		/*  f0a79cc:	af0f0000 */ 	sw	$t7, 0x0($t8)
		/*  f0a79d0:	240e0003 */ 	addiu	$t6, $zero, 0x3
		/*  f0a79d4:	10000003 */ 	b.L0f0a79e4
		/*  f0a79d8:	afae0148 */ 	sw	$t6, 0x148($sp)
		.L0f0a79dc:
/*  f0a79dc:	240c0002 */ 	addiu	$t4, $zero, 0x2
/*  f0a79e0:	afac0148 */ 	sw	$t4, 0x148($sp)
.L0f0a79e4 :
		/*  f0a79e4:	8e28006c */ 	lw	$t0, 0x6c($s1)
		/*  f0a79e8:	00002825 */ or $a1, $zero, $zero
		/*  f0a79ec:	00002025 */ or $a0, $zero, $zero
		/*  f0a79f0:	11000003 */ 	beqz	$t0, .L0f0a7a00
		/*  f0a79f4:	00001025 */ or $v0, $zero, $zero
		/*  f0a79f8:	10000001 */ 	b.L0f0a7a00
		/*  f0a79fc:	24050001 */ 	addiu	$a1, $zero, 0x1
		.L0f0a7a00:
/*  f0a7a00:	8e2a0068 */ 	lw	$t2, 0x68($s1)
/*  f0a7a04:	00001825 */ or $v1, $zero, $zero
/*  f0a7a08:	11400003 */ 	beqz	$t2, .L0f0a7a18
/*  f0a7a0c:	00000000 */ 	nop
/*  f0a7a10:	10000001 */ 	b.L0f0a7a18
/*  f0a7a14:	24040001 */ 	addiu	$a0, $zero, 0x1
.L0f0a7a18:
/*  f0a7a18:	8e2b0064 */ 	lw	$t3, 0x64($s1)
/*  f0a7a1c:	11600003 */ 	beqz	$t3, .L0f0a7a2c
/*  f0a7a20:	00000000 */ 	nop
/*  f0a7a24:	10000001 */ 	b.L0f0a7a2c
/*  f0a7a28:	24020001 */ 	addiu	$v0, $zero, 0x1
.L0f0a7a2c:
/*  f0a7a2c:	8e2d0070 */ 	lw	$t5, 0x70($s1)
/*  f0a7a30:	11a00003 */ 	beqz	$t5, .L0f0a7a40
/*  f0a7a34:	00000000 */ 	nop
/*  f0a7a38:	10000001 */ 	b.L0f0a7a40
/*  f0a7a3c:	24030001 */ 	addiu	$v1, $zero, 0x1
.L0f0a7a40:
/*  f0a7a40:	0062c021 */ 	addu	$t8, $v1, $v0
/*  f0a7a44:	0304c821 */ 	addu	$t9, $t8, $a0
/*  f0a7a48:	03257821 */ 	addu	$t7, $t9, $a1
/*  f0a7a4c:	15e10036 */ 	bne	$t7, $at, .L0f0a7b28
/*  f0a7a50:	24050041 */ 	addiu	$a1, $zero, 0x41
/*  f0a7a54:	0c006a47 */ 	jal	modelGetPart
/*  f0a7a58:	8e04038c */ 	lw	$a0, 0x38c($s0)
/*  f0a7a5c:	50400033 */ 	beqzl	$v0, .L0f0a7b2c
/*  f0a7a60:	27a4010c */ 	addiu	$a0, $sp, 0x10c
/*  f0a7a64:	8c440004 */ 	lw	$a0, 0x4($v0)
/*  f0a7a68:	00002825 */ or $a1, $zero, $zero
/*  f0a7a6c:	84890010 */ 	lh	$t1, 0x10($a0)
/*  f0a7a70:	5920002e */ 	blezl	$t1, .L0f0a7b2c
/*  f0a7a74:	27a4010c */ 	addiu	$a0, $sp, 0x10c
/*  f0a7a78:	8e2a0034 */ 	lw	$t2, 0x34($s1)
.L0f0a7a7c:
/*  f0a7a7c:	8c8e000c */ 	lw	$t6, 0xc($a0)
/*  f0a7a80:	00056080 */ 	sll	$t4, $a1, 0x2
/*  f0a7a84:	01520019 */ 	multu	$t2, $s2
/*  f0a7a88:	01856023 */ 	subu	$t4, $t4, $a1
/*  f0a7a8c:	000c6080 */ 	sll	$t4, $t4, 0x2
/*  f0a7a90:	01cc1021 */ 	addu	$v0, $t6, $t4
/*  f0a7a94:	8448000a */ 	lh	$t0, 0xa($v0)
/*  f0a7a98:	0005c880 */ 	sll	$t9, $a1, 0x2
/*  f0a7a9c:	0325c823 */ 	subu	$t9, $t9, $a1
/*  f0a7aa0:	0019c880 */ 	sll	$t9, $t9, 0x2
/*  f0a7aa4:	24420002 */ 	addiu	$v0, $v0, 0x2
/*  f0a7aa8:	00005812 */ 	mflo	$t3
/*  f0a7aac:	010b6823 */ 	subu	$t5, $t0, $t3
/*  f0a7ab0:	a44d0008 */ 	sh	$t5, 0x8($v0)
/*  f0a7ab4:	8c98000c */ 	lw	$t8, 0xc($a0)
/*  f0a7ab8:	03197821 */ 	addu	$t7, $t8, $t9
/*  f0a7abc:	85e9000a */ 	lh	$t1, 0xa($t7)
/*  f0a7ac0:	2921a000 */ 	slti	$at, $t1, -24576
/*  f0a7ac4:	50200014 */ 	beqzl	$at, .L0f0a7b18
/*  f0a7ac8:	84980010 */ 	lh	$t8, 0x10($a0)
/*  f0a7acc:	848e0010 */ 	lh	$t6, 0x10($a0)
/*  f0a7ad0:	00001825 */ or $v1, $zero, $zero
/*  f0a7ad4:	59c00010 */ 	blezl	$t6, .L0f0a7b18
/*  f0a7ad8:	84980010 */ 	lh	$t8, 0x10($a0)
/*  f0a7adc:	8c8c000c */ 	lw	$t4, 0xc($a0)
.L0f0a7ae0:
/*  f0a7ae0:	00035080 */ 	sll	$t2, $v1, 0x2
/*  f0a7ae4:	01435023 */ 	subu	$t2, $t2, $v1
/*  f0a7ae8:	000a5080 */ 	sll	$t2, $t2, 0x2
/*  f0a7aec:	018a1021 */ 	addu	$v0, $t4, $t2
/*  f0a7af0:	8448000a */ 	lh	$t0, 0xa($v0)
/*  f0a7af4:	24630001 */ 	addiu	$v1, $v1, 0x1
/*  f0a7af8:	24420002 */ 	addiu	$v0, $v0, 0x2
/*  f0a7afc:	250b2000 */ 	addiu	$t3, $t0, 0x2000
/*  f0a7b00:	a44b0008 */ 	sh	$t3, 0x8($v0)
/*  f0a7b04:	848d0010 */ 	lh	$t5, 0x10($a0)
/*  f0a7b08:	006d082a */ 	slt	$at, $v1, $t5
/*  f0a7b0c:	5420fff4 */ 	bnezl	$at, .L0f0a7ae0
/*  f0a7b10:	8c8c000c */ 	lw	$t4, 0xc($a0)
/*  f0a7b14:	84980010 */ 	lh	$t8, 0x10($a0)
.L0f0a7b18:
/*  f0a7b18:	24a50001 */ 	addiu	$a1, $a1, 0x1
/*  f0a7b1c:	00b8082a */ 	slt	$at, $a1, $t8
/*  f0a7b20:	5420ffd6 */ 	bnezl	$at, .L0f0a7a7c
/*  f0a7b24:	8e2a0034 */ 	lw	$t2, 0x34($s1)
.L0f0a7b28 :
		/*  f0a7b28:	27a4010c */ 	addiu	$a0, $sp, 0x10c
		.L0f0a7b2c :
		/*  f0a7b2c:	0c0087bd */ 	jal	modelRender
		/*  f0a7b30:	8fa5003c */ 	lw	$a1, 0x3c($sp)
		/*  f0a7b34:	8e791594 */ 	lw	$t9, 0x1594($s3)
		/*  f0a7b38:	3c0f8007 */ 	lui	$t7, % hi(var800702dc)
		/*  f0a7b3c:	53200013 */ 	beqzl	$t9, .L0f0a7b8c
		/*  f0a7b40:	8fac0118 */ 	lw	$t4, 0x118($sp)
		/*  f0a7b44:	8def02dc */ 	lw	$t7, % lo(var800702dc)($t7)
		/*  f0a7b48:	8fa90140 */ 	lw	$t1, 0x140($sp)
		/*  f0a7b4c:	51e0000f */ 	beqzl	$t7, .L0f0a7b8c
		/*  f0a7b50:	8fac0118 */ 	lw	$t4, 0x118($sp)
		/*  f0a7b54:	afa9007c */ 	sw	$t1, 0x7c($sp)
		/*  f0a7b58:	8e0e0390 */ 	lw	$t6, 0x390($s0)
		/*  f0a7b5c:	26050534 */ 	addiu	$a1, $s0, 0x534
		/*  f0a7b60:	00a02025 */ or $a0, $a1, $zero
		/*  f0a7b64:	ae0e0540 */ 	sw	$t6, 0x540($s0)
		/*  f0a7b68:	0c007308 */ 	jal	modelUpdateRelations
		/*  f0a7b6c:	afa50054 */ 	sw	$a1, 0x54($sp)
		/*  f0a7b70:	8fa50054 */ 	lw	$a1, 0x54($sp)
		/*  f0a7b74:	afb50140 */ 	sw	$s5, 0x140($sp)
		/*  f0a7b78:	0c0087bd */ 	jal	modelRender
		/*  f0a7b7c:	27a4010c */ 	addiu	$a0, $sp, 0x10c
		/*  f0a7b80:	8fa4007c */ 	lw	$a0, 0x7c($sp)
		/*  f0a7b84:	afa40140 */ 	sw	$a0, 0x140($sp)
		/*  f0a7b88:	8fac0118 */ 	lw	$t4, 0x118($sp)
		.L0f0a7b8c:
/*  f0a7b8c:	8fa400ec */ 	lw	$a0, 0xec($sp)
/*  f0a7b90:	24050020 */ 	addiu	$a1, $zero, 0x20
/*  f0a7b94:	0fc2c5f0 */ 	jal	weaponHasFlag
/*  f0a7b98:	afac014c */ 	sw	$t4, 0x14c($sp)
/*  f0a7b9c:	10400007 */ 	beqz	$v0, .L0f0a7bbc
/*  f0a7ba0:	8faa014c */ 	lw	$t2, 0x14c($sp)
/*  f0a7ba4:	25480008 */ 	addiu	$t0, $t2, 0x8
/*  f0a7ba8:	afa8014c */ 	sw	$t0, 0x14c($sp)
/*  f0a7bac:	3c0bb600 */ 	lui	$t3, 0xb600
/*  f0a7bb0:	240d3000 */ 	addiu	$t5, $zero, 0x3000
/*  f0a7bb4:	ad4d0004 */ 	sw	$t5, 0x4($t2)
/*  f0a7bb8:	ad4b0000 */ 	sw	$t3, 0x0($t2)
.L0f0a7bbc:
/*  f0a7bbc:	8e18038c */ 	lw	$t8, 0x38c($s0)
/*  f0a7bc0:	8e040390 */ 	lw	$a0, 0x390($s0)
/*  f0a7bc4:	0fc30cfc */ 	jal	mtxF2LBulk
/*  f0a7bc8:	8705000e */ 	lh	$a1, 0xe($t8)
/*  f0a7bcc:	0c0059e1 */ 	jal	mtx00016784
/*  f0a7bd0:	00000000 */ 	nop
/*  f0a7bd4:	8fb9014c */ 	lw	$t9, 0x14c($sp)
/*  f0a7bd8:	3c09bc00 */ 	lui	$t1, 0xbc00
/*  f0a7bdc:	3529000e */ 	ori	$t1, $t1, 0xe
/*  f0a7be0:	272f0008 */ 	addiu	$t7, $t9, 0x8
/*  f0a7be4:	afaf014c */ 	sw	$t7, 0x14c($sp)
/*  f0a7be8:	af290000 */ 	sw	$t1, 0x0($t9)
/*  f0a7bec:	0c002adb */ 	jal	viGetPerspScale
/*  f0a7bf0:	afb90074 */ 	sw	$t9, 0x74($sp)
/*  f0a7bf4:	8fa30074 */ 	lw	$v1, 0x74($sp)
/*  f0a7bf8:	ac620004 */ 	sw	$v0, 0x4($v1)
.L0f0a7bfc:
/*  f0a7bfc:	26940001 */ 	addiu	$s4, $s4, 0x1
/*  f0a7c00:	24010002 */ 	addiu	$at, $zero, 0x2
/*  f0a7c04:	1681fe04 */ 	bne	$s4, $at, .L0f0a7418
/*  f0a7c08:	261007a4 */ 	addiu	$s0, $s0, 0x7a4
/*  f0a7c0c:	afb500e4 */ 	sw	$s5, 0xe4($sp)
/*  f0a7c10:	0fc2baf8 */ 	jal	casingsRender
/*  f0a7c14:	27a4014c */ 	addiu	$a0, $sp, 0x14c
/*  f0a7c18:	0fc5d8a6 */ 	jal	zbufSwap
/*  f0a7c1c:	00000000 */ 	nop
/*  f0a7c20:	0fc5d8ab */ 	jal	zbufConfigureRdp
/*  f0a7c24:	8fa4014c */ 	lw	$a0, 0x14c($sp)
/*  f0a7c28:	afa2014c */ 	sw	$v0, 0x14c($sp)
/*  f0a7c2c:	0c002c74 */ 	jal	vi0000b1d0
/*  f0a7c30:	00402025 */ or $a0, $v0, $zero
/*  f0a7c34:	244e0008 */ 	addiu	$t6, $v0, 0x8
/*  f0a7c38:	afae014c */ 	sw	$t6, 0x14c($sp)
/*  f0a7c3c:	0c002f40 */ 	jal	viGetViewLeft
/*  f0a7c40:	00408825 */ or $s1, $v0, $zero
/*  f0a7c44:	00028400 */ 	sll	$s0, $v0, 0x10
/*  f0a7c48:	00106403 */ 	sra	$t4, $s0, 0x10
/*  f0a7c4c:	0c002f44 */ 	jal	viGetViewTop
/*  f0a7c50:	01808025 */ or $s0, $t4, $zero
/*  f0a7c54:	44824000 */ 	mtc1	$v0, $f8
/*  f0a7c58:	44902000 */ 	mtc1	$s0, $f4
/*  f0a7c5c:	3c014080 */ 	lui	$at, 0x4080
/*  f0a7c60:	468042a0 */ 	cvt.s.w	$f10, $f8
/*  f0a7c64:	44810000 */ 	mtc1	$at, $f0
/*  f0a7c68:	3c01ed00 */ 	lui	$at, 0xed00
/*  f0a7c6c:	468021a0 */ 	cvt.s.w	$f6, $f4
/*  f0a7c70:	46005402 */ 	mul.s	$f16, $f10, $f0
/*  f0a7c74:	00000000 */ 	nop
/*  f0a7c78:	46003202 */ 	mul.s	$f8, $f6, $f0
/*  f0a7c7c:	4600848d */ 	trunc.w.s	$f18, $f16
/*  f0a7c80:	4600428d */ 	trunc.w.s	$f10, $f8
/*  f0a7c84:	44089000 */ 	mfc1	$t0, $f18
/*  f0a7c88:	44195000 */ 	mfc1	$t9, $f10
/*  f0a7c8c:	310b0fff */ 	andi	$t3, $t0, 0xfff
/*  f0a7c90:	01616825 */ or $t5, $t3, $at
/*  f0a7c94:	332f0fff */ 	andi	$t7, $t9, 0xfff
/*  f0a7c98:	000f4b00 */ 	sll	$t1, $t7, 0xc
/*  f0a7c9c:	01a97025 */ or $t6, $t5, $t1
/*  f0a7ca0:	0c002f22 */ 	jal	viGetViewWidth
/*  f0a7ca4:	ae2e0000 */ 	sw	$t6, 0x0($s1)
/*  f0a7ca8:	00029400 */ 	sll	$s2, $v0, 0x10
/*  f0a7cac:	00126403 */ 	sra	$t4, $s2, 0x10
/*  f0a7cb0:	0c002f40 */ 	jal	viGetViewLeft
/*  f0a7cb4:	01809025 */ or $s2, $t4, $zero
/*  f0a7cb8:	0002a400 */ 	sll	$s4, $v0, 0x10
/*  f0a7cbc:	00145403 */ 	sra	$t2, $s4, 0x10
/*  f0a7cc0:	0c002f44 */ 	jal	viGetViewTop
/*  f0a7cc4:	0140a025 */ or $s4, $t2, $zero
/*  f0a7cc8:	00028400 */ 	sll	$s0, $v0, 0x10
/*  f0a7ccc:	00104403 */ 	sra	$t0, $s0, 0x10
/*  f0a7cd0:	0c002f26 */ 	jal	viGetViewHeight
/*  f0a7cd4:	01008025 */ or $s0, $t0, $zero
/*  f0a7cd8:	00505821 */ 	addu	$t3, $v0, $s0
/*  f0a7cdc:	448b8000 */ 	mtc1	$t3, $f16
/*  f0a7ce0:	02926821 */ 	addu	$t5, $s4, $s2
/*  f0a7ce4:	448d5000 */ 	mtc1	$t5, $f10
/*  f0a7ce8:	468084a0 */ 	cvt.s.w	$f18, $f16
/*  f0a7cec:	3c014080 */ 	lui	$at, 0x4080
/*  f0a7cf0:	44812000 */ 	mtc1	$at, $f4
/*  f0a7cf4:	46805420 */ 	cvt.s.w	$f16, $f10
/*  f0a7cf8:	46049182 */ 	mul.s	$f6, $f18, $f4
/*  f0a7cfc:	44819000 */ 	mtc1	$at, $f18
/*  f0a7d00:	00000000 */ 	nop
/*  f0a7d04:	46128102 */ 	mul.s	$f4, $f16, $f18
/*  f0a7d08:	4600320d */ 	trunc.w.s	$f8, $f6
/*  f0a7d0c:	4600218d */ 	trunc.w.s	$f6, $f4
/*  f0a7d10:	44194000 */ 	mfc1	$t9, $f8
/*  f0a7d14:	440e3000 */ 	mfc1	$t6, $f6
/*  f0a7d18:	332f0fff */ 	andi	$t7, $t9, 0xfff
/*  f0a7d1c:	31cc0fff */ 	andi	$t4, $t6, 0xfff
/*  f0a7d20:	000c5300 */ 	sll	$t2, $t4, 0xc
/*  f0a7d24:	01ea4025 */ or $t0, $t7, $t2
/*  f0a7d28:	ae280004 */ 	sw	$t0, 0x4($s1)
/*  f0a7d2c:	8fb80150 */ 	lw	$t8, 0x150($sp)
/*  f0a7d30:	8fab014c */ 	lw	$t3, 0x14c($sp)
/*  f0a7d34:	af0b0000 */ 	sw	$t3, 0x0($t8)
/*  f0a7d38:	8fbf0034 */ 	lw	$ra, 0x34($sp)
		glabel bgunRender
		.late_rodata
		glabel var7f1aca8c
		.word 0x3faaaaab
		glabel var7f1aca90
		.word 0x3f3ebebf
		.text
		/*  f0a4e84:	27bdfeb8 */ 	addiu	$sp, $sp, -328
		/*  f0a4e88:	afbf0034 */ 	sw	$ra, 0x34($sp)
		/*  f0a4e8c:	afb50030 */ 	sw	$s5, 0x30($sp)
		/*  f0a4e90:	afb4002c */ 	sw	$s4, 0x2c($sp)
		/*  f0a4e94:	afb30028 */ 	sw	$s3, 0x28($sp)
		/*  f0a4e98:	afb20024 */ 	sw	$s2, 0x24($sp)
		/*  f0a4e9c:	afb10020 */ 	sw	$s1, 0x20($sp)
		/*  f0a4ea0:	afb0001c */ 	sw	$s0, 0x1c($sp)
		/*  f0a4ea4:	afa40148 */ 	sw	$a0, 0x148($sp)
		/*  f0a4ea8:	8c8f0000 */ 	lw	$t7, 0x0($a0)
		/*  f0a4eac:	3c198007 */ 	lui	$t9, % hi(var8007029c)
		/*  f0a4eb0:	3c11800a */ 	lui	$s1, % hi(g_Vars)
		/*  f0a4eb4:	2739295c */ 	addiu	$t9, $t9, % lo(var8007029c)
		/*  f0a4eb8:	2631e6c0 */ 	addiu	$s1, $s1, % lo(g_Vars)
		/*  f0a4ebc:	272a003c */ 	addiu	$t2, $t9, 0x3c
		/*  f0a4ec0:	27b80104 */ 	addiu	$t8, $sp, 0x104
		/*  f0a4ec4:	afaf0144 */ 	sw	$t7, 0x144($sp)
		.NB0f0a4ec8:
/*  f0a4ec8:	8f210000 */ 	lw	$at, 0x0($t9)
/*  f0a4ecc:	2739000c */ 	addiu	$t9, $t9, 0xc
/*  f0a4ed0:	2718000c */ 	addiu	$t8, $t8, 0xc
/*  f0a4ed4:	af01fff4 */ 	sw	$at, -0xc($t8)
/*  f0a4ed8:	8f21fff8 */ 	lw	$at, -0x8($t9)
/*  f0a4edc:	af01fff8 */ 	sw	$at, -0x8($t8)
/*  f0a4ee0:	8f21fffc */ 	lw	$at, -0x4($t9)
/*  f0a4ee4:	172afff8 */ 	bne	$t9, $t2, .NB0f0a4ec8
/*  f0a4ee8:	af01fffc */ 	sw	$at, -0x4($t8)
/*  f0a4eec:	8f210000 */ 	lw	$at, 0x0($t9)
/*  f0a4ef0:	af010000 */ 	sw	$at, 0x0($t8)
/*  f0a4ef4:	8e330284 */ 	lw	$s3, 0x284($s1)
/*  f0a4ef8:	24010001 */ 	addiu	$at, $zero, 0x1
/*  f0a4efc:	966b0010 */ 	lhu	$t3, 0x10($s3)
/*  f0a4f00:	1561000d */ 	bne	$t3, $at, .NB0f0a4f38
/*  f0a4f04:	00001025 */ or $v0, $zero, $zero
/*  f0a4f08:	24040f48 */ 	addiu	$a0, $zero, 0xf48
/*  f0a4f0c:	8e2c0284 */ 	lw	$t4, 0x284($s1)
.NB0f0a4f10:
/*  f0a4f10:	01821821 */ 	addu	$v1, $t4, $v0
/*  f0a4f14:	8c6d0854 */ 	lw	$t5, 0x854($v1)
/*  f0a4f18:	244207a4 */ 	addiu	$v0, $v0, 0x7a4
/*  f0a4f1c:	11a00002 */ 	beqz	$t5, .NB0f0a4f28
/*  f0a4f20:	00000000 */ 	sll	$zero, $zero, 0x0
/*  f0a4f24:	ac600850 */ 	sw	$zero, 0x850($v1)
.NB0f0a4f28:
/*  f0a4f28:	5444fff9 */ 	bnel	$v0, $a0, .NB0f0a4f10
/*  f0a4f2c:	8e2c0284 */ 	lw	$t4, 0x284($s1)
/*  f0a4f30:	100002cd */ 	beqz	$zero, .NB0f0a5a68
/*  f0a4f34:	8fbf0034 */ 	lw	$ra, 0x34($sp)
.NB0f0a4f38 :
		/*  f0a4f38:	0fc5c4d5 */ 	jal	zbufSaveArtifactDepths
		/*  f0a4f3c:	8fa40144 */ 	lw	$a0, 0x144($sp)
		/*  f0a4f40:	afa20144 */ 	sw	$v0, 0x144($sp)
		/*  f0a4f44:	0c002d00 */ 	jal	viPrepareZbuf
		/*  f0a4f48:	00402025 */ or $a0, $v0, $zero
		/*  f0a4f4c:	afa20144 */ 	sw	$v0, 0x144($sp)
		/*  f0a4f50:	0c002cd4 */ 	jal	vi0000b1d0
		/*  f0a4f54:	00402025 */ or $a0, $v0, $zero
		/*  f0a4f58:	244e0008 */ 	addiu	$t6, $v0, 0x8
		/*  f0a4f5c:	afae0144 */ 	sw	$t6, 0x144($sp)
		/*  f0a4f60:	0c002fb5 */ 	jal	viGetViewLeft
		/*  f0a4f64:	0040a825 */ or $s5, $v0, $zero
		/*  f0a4f68:	00028400 */ 	sll	$s0, $v0, 0x10
		/*  f0a4f6c:	00107c03 */ 	sra	$t7, $s0, 0x10
		/*  f0a4f70:	0c002fb9 */ 	jal	viGetViewTop
		/*  f0a4f74:	01e08025 */ or $s0, $t7, $zero
		/*  f0a4f78:	44822000 */ 	mtc1	$v0, $f4
		/*  f0a4f7c:	44908000 */ 	mtc1	$s0, $f16
		/*  f0a4f80:	3c014080 */ 	lui	$at, 0x4080
		/*  f0a4f84:	468021a0 */ 	cvt.s.w	$f6, $f4
		/*  f0a4f88:	44810000 */ 	mtc1	$at, $f0
		/*  f0a4f8c:	3c01ed00 */ 	lui	$at, 0xed00
		/*  f0a4f90:	468084a0 */ 	cvt.s.w	$f18, $f16
		/*  f0a4f94:	46003202 */ 	mul.s	$f8, $f6, $f0
		/*  f0a4f98:	00000000 */ 	sll	$zero, $zero, 0x0
		/*  f0a4f9c:	46009102 */ 	mul.s	$f4, $f18, $f0
		/*  f0a4fa0:	4600428d */ 	trunc.w.s	$f10, $f8
		/*  f0a4fa4:	4600218d */ 	trunc.w.s	$f6, $f4
		/*  f0a4fa8:	44085000 */ 	mfc1	$t0, $f10
		/*  f0a4fac:	440b3000 */ 	mfc1	$t3, $f6
		/*  f0a4fb0:	310a0fff */ 	andi	$t2, $t0, 0xfff
		/*  f0a4fb4:	0141c825 */ or $t9, $t2, $at
		/*  f0a4fb8:	316c0fff */ 	andi	$t4, $t3, 0xfff
		/*  f0a4fbc:	000c6b00 */ 	sll	$t5, $t4, 0xc
		/*  f0a4fc0:	032d7025 */ or $t6, $t9, $t5
		/*  f0a4fc4:	0c002f97 */ 	jal	viGetViewWidth
		/*  f0a4fc8:	aeae0000 */ 	sw	$t6, 0x0($s5)
		/*  f0a4fcc:	00029400 */ 	sll	$s2, $v0, 0x10
		/*  f0a4fd0:	00127c03 */ 	sra	$t7, $s2, 0x10
		/*  f0a4fd4:	0c002fb5 */ 	jal	viGetViewLeft
		/*  f0a4fd8:	01e09025 */ or $s2, $t7, $zero
		/*  f0a4fdc:	0002a400 */ 	sll	$s4, $v0, 0x10
		/*  f0a4fe0:	00144c03 */ 	sra	$t1, $s4, 0x10
		/*  f0a4fe4:	0c002fb9 */ 	jal	viGetViewTop
		/*  f0a4fe8:	0120a025 */ or $s4, $t1, $zero
		/*  f0a4fec:	00028400 */ 	sll	$s0, $v0, 0x10
		/*  f0a4ff0:	00104403 */ 	sra	$t0, $s0, 0x10
		/*  f0a4ff4:	0c002f9b */ 	jal	viGetViewHeight
		/*  f0a4ff8:	01008025 */ or $s0, $t0, $zero
		/*  f0a4ffc:	00505021 */ 	addu	$t2, $v0, $s0
		/*  f0a5000:	448a4000 */ 	mtc1	$t2, $f8
		/*  f0a5004:	0292c821 */ 	addu	$t9, $s4, $s2
		/*  f0a5008:	44992000 */ 	mtc1	$t9, $f4
		/*  f0a500c:	468042a0 */ 	cvt.s.w	$f10, $f8
		/*  f0a5010:	3c014080 */ 	lui	$at, 0x4080
		/*  f0a5014:	44810000 */ 	mtc1	$at, $f0
		/*  f0a5018:	3c053fc0 */ 	lui	$a1, 0x3fc0
		/*  f0a501c:	3c06447a */ 	lui	$a2, 0x447a
		/*  f0a5020:	468021a0 */ 	cvt.s.w	$f6, $f4
		/*  f0a5024:	46005402 */ 	mul.s	$f16, $f10, $f0
		/*  f0a5028:	00000000 */ 	sll	$zero, $zero, 0x0
		/*  f0a502c:	46003202 */ 	mul.s	$f8, $f6, $f0
		/*  f0a5030:	4600848d */ 	trunc.w.s	$f18, $f16
		/*  f0a5034:	4600428d */ 	trunc.w.s	$f10, $f8
		/*  f0a5038:	440b9000 */ 	mfc1	$t3, $f18
		/*  f0a503c:	440e5000 */ 	mfc1	$t6, $f10
		/*  f0a5040:	316c0fff */ 	andi	$t4, $t3, 0xfff
		/*  f0a5044:	31cf0fff */ 	andi	$t7, $t6, 0xfff
		/*  f0a5048:	000f4b00 */ 	sll	$t1, $t7, 0xc
		/*  f0a504c:	01894025 */ or $t0, $t4, $t1
		/*  f0a5050:	aea80004 */ 	sw	$t0, 0x4($s5)
		/*  f0a5054:	0c002b89 */ 	jal	vi0000aca4
		/*  f0a5058:	8fa40144 */ 	lw	$a0, 0x144($sp)
		/*  f0a505c:	8e2a0284 */ 	lw	$t2, 0x284($s1)
		/*  f0a5060:	afa20144 */ 	sw	$v0, 0x144($sp)
		/*  f0a5064:	91581bfc */ 	lbu	$t8, 0x1bfc($t2)
		/*  f0a5068:	53000016 */ 	beqzl	$t8, .NB0f0a50c4
		/*  f0a506c:	8e2b006c */ 	lw	$t3, 0x6c($s1)
		/*  f0a5070:	0fc53582 */ 	jal	optionsGetScreenRatio
		/*  f0a5074:	00000000 */ 	sll	$zero, $zero, 0x0
		/*  f0a5078:	24010001 */ 	addiu	$at, $zero, 0x1
		/*  f0a507c:	14410008 */ 	bne	$v0, $at, .NB0f0a50a0
		/*  f0a5080:	00000000 */ 	sll	$zero, $zero, 0x0
		/*  f0a5084:	0fc2ebf0 */ 	jal	player0f0bd358
		/*  f0a5088:	00000000 */ 	sll	$zero, $zero, 0x0
		/*  f0a508c:	3c017f1a */ 	lui	$at, % hi(var7f1aca8c)
		/*  f0a5090:	c4306dd4 */ 	lwc1	$f16, % lo(var7f1aca8c)($at)
		/*  f0a5094:	46100082 */ 	mul.s	$f2, $f0, $f16
		/*  f0a5098:	10000005 */ 	beqz	$zero, .NB0f0a50b0
		/*  f0a509c:	44061000 */ 	mfc1	$a2, $f2
		.NB0f0a50a0:
/*  f0a50a0:	0fc2ebf0 */ 	jal	player0f0bd358
/*  f0a50a4:	00000000 */ 	sll	$zero, $zero, 0x0
/*  f0a50a8:	46000086 */ 	mov.s	$f2, $f0
/*  f0a50ac:	44061000 */ 	mfc1	$a2, $f2
.NB0f0a50b0 :
		/*  f0a50b0:	8fa40144 */ 	lw	$a0, 0x144($sp)
		/*  f0a50b4:	0c002c9a */ 	jal	vi0000b0e8
		/*  f0a50b8:	3c054270 */ 	lui	$a1, 0x4270
		/*  f0a50bc:	afa20144 */ 	sw	$v0, 0x144($sp)
		/*  f0a50c0:	8e2b006c */ 	lw	$t3, 0x6c($s1)
		.NB0f0a50c4:
/*  f0a50c4:	24010001 */ 	addiu	$at, $zero, 0x1
/*  f0a50c8:	51600004 */ 	beqzl	$t3, .NB0f0a50dc
/*  f0a50cc:	00002825 */ or $a1, $zero, $zero
/*  f0a50d0:	10000002 */ 	beqz	$zero, .NB0f0a50dc
/*  f0a50d4:	24050001 */ 	addiu	$a1, $zero, 0x1
/*  f0a50d8:	00002825 */ or $a1, $zero, $zero
.NB0f0a50dc:
/*  f0a50dc:	8e390068 */ 	lw	$t9, 0x68($s1)
/*  f0a50e0:	53200004 */ 	beqzl	$t9, .NB0f0a50f4
/*  f0a50e4:	00002025 */ or $a0, $zero, $zero
/*  f0a50e8:	10000002 */ 	beqz	$zero, .NB0f0a50f4
/*  f0a50ec:	24040001 */ 	addiu	$a0, $zero, 0x1
/*  f0a50f0:	00002025 */ or $a0, $zero, $zero
.NB0f0a50f4:
/*  f0a50f4:	8e2d0064 */ 	lw	$t5, 0x64($s1)
/*  f0a50f8:	51a00004 */ 	beqzl	$t5, .NB0f0a510c
/*  f0a50fc:	00001025 */ or $v0, $zero, $zero
/*  f0a5100:	10000002 */ 	beqz	$zero, .NB0f0a510c
/*  f0a5104:	24020001 */ 	addiu	$v0, $zero, 0x1
/*  f0a5108:	00001025 */ or $v0, $zero, $zero
.NB0f0a510c:
/*  f0a510c:	8e2e0070 */ 	lw	$t6, 0x70($s1)
/*  f0a5110:	51c00004 */ 	beqzl	$t6, .NB0f0a5124
/*  f0a5114:	00001825 */ or $v1, $zero, $zero
/*  f0a5118:	10000002 */ 	beqz	$zero, .NB0f0a5124
/*  f0a511c:	24030001 */ 	addiu	$v1, $zero, 0x1
/*  f0a5120:	00001825 */ or $v1, $zero, $zero
.NB0f0a5124:
/*  f0a5124:	00627821 */ 	addu	$t7, $v1, $v0
/*  f0a5128:	01e46021 */ 	addu	$t4, $t7, $a0
/*  f0a512c:	01854821 */ 	addu	$t1, $t4, $a1
/*  f0a5130:	15210008 */ 	bne	$t1, $at, .NB0f0a5154
/*  f0a5134:	3c088009 */ 	lui	$t0, % hi(g_Is4Mb)
/*  f0a5138:	910830e0 */ 	lbu	$t0, % lo(g_Is4Mb)($t0)
/*  f0a513c:	24010001 */ 	addiu	$at, $zero, 0x1
/*  f0a5140:	51010005 */ 	beql	$t0, $at, .NB0f0a5158
/*  f0a5144:	0000a025 */ or $s4, $zero, $zero
/*  f0a5148:	0fc2b5eb */ 	jal	lasersightRenderBeam
/*  f0a514c:	8fa40144 */ 	lw	$a0, 0x144($sp)
/*  f0a5150:	afa20144 */ 	sw	$v0, 0x144($sp)
.NB0f0a5154:
/*  f0a5154:	0000a025 */ or $s4, $zero, $zero
.NB0f0a5158 :
		/*  f0a5158:	26700638 */ 	addiu	$s0, $s3, 0x638
		/*  f0a515c:	8fb500dc */ 	lw	$s5, 0xdc($sp)
		/*  f0a5160:	24120019 */ 	addiu	$s2, $zero, 0x19
		.NB0f0a5164 :
		/*  f0a5164:	0fc27de9 */ 	jal	bgunGetWeaponNum2
		/*  f0a5168:	02802025 */ or $a0, $s4, $zero
		/*  f0a516c:	afa200e4 */ 	sw	$v0, 0xe4($sp)
		/*  f0a5170:	820a0007 */ 	lb	$t2, 0x7($s0)
		/*  f0a5174:	8fa40144 */ 	lw	$a0, 0x144($sp)
		/*  f0a5178:	260501dc */ 	addiu	$a1, $s0, 0x1dc
		/*  f0a517c:	114001ea */ 	beqz	$t2, .NB0f0a5928
		/*  f0a5180:	00003025 */ or $a2, $zero, $zero
		/*  f0a5184:	26180384 */ 	addiu	$t8, $s0, 0x384
		/*  f0a5188:	afb80038 */ 	sw	$t8, 0x38($sp)
		/*  f0a518c:	0fc2aa3c */ 	jal	beamRender
		/*  f0a5190:	00003825 */ or $a3, $zero, $zero
		/*  f0a5194:	afa20144 */ 	sw	$v0, 0x144($sp)
		/*  f0a5198:	92040000 */ 	lbu	$a0, 0x0($s0)
		/*  f0a519c:	0fc2bd48 */ 	jal	weaponHasFlag
		/*  f0a51a0:	34058000 */ 	dli	$a1, 0x8000
		/*  f0a51a4:	10400030 */ 	beqz	$v0, .NB0f0a5268
		/*  f0a51a8:	8fab0144 */ 	lw	$t3, 0x144($sp)
		/*  f0a51ac:	25790008 */ 	addiu	$t9, $t3, 0x8
		/*  f0a51b0:	afb90144 */ 	sw	$t9, 0x144($sp)
		/*  f0a51b4:	3c0dbc00 */ 	lui	$t5, 0xbc00
		/*  f0a51b8:	3c0e8000 */ 	lui	$t6, 0x8000
		/*  f0a51bc:	35ce0040 */ 	ori	$t6, $t6, 0x40
		/*  f0a51c0:	35ad0002 */ 	ori	$t5, $t5, 0x2
		/*  f0a51c4:	ad6d0000 */ 	sw	$t5, 0x0($t3)
		/*  f0a51c8:	ad6e0004 */ 	sw	$t6, 0x4($t3)
		/*  f0a51cc:	8faf0144 */ 	lw	$t7, 0x144($sp)
		/*  f0a51d0:	3c090386 */ 	lui	$t1, 0x386
		/*  f0a51d4:	3c088007 */ 	lui	$t0, % hi(var80070090 + 0x8)
		/*  f0a51d8:	25ec0008 */ 	addiu	$t4, $t7, 0x8
		/*  f0a51dc:	afac0144 */ 	sw	$t4, 0x144($sp)
		/*  f0a51e0:	25082758 */ 	addiu	$t0, $t0, % lo(var80070090 + 0x8)
		/*  f0a51e4:	35290010 */ 	ori	$t1, $t1, 0x10
		/*  f0a51e8:	ade90000 */ 	sw	$t1, 0x0($t7)
		/*  f0a51ec:	ade80004 */ 	sw	$t0, 0x4($t7)
		/*  f0a51f0:	8faa0144 */ 	lw	$t2, 0x144($sp)
		/*  f0a51f4:	3c0b0388 */ 	lui	$t3, 0x388
		/*  f0a51f8:	3c198007 */ 	lui	$t9, % hi(var80070090)
		/*  f0a51fc:	25580008 */ 	addiu	$t8, $t2, 0x8
		/*  f0a5200:	afb80144 */ 	sw	$t8, 0x144($sp)
		/*  f0a5204:	27392750 */ 	addiu	$t9, $t9, % lo(var80070090)
		/*  f0a5208:	356b0010 */ 	ori	$t3, $t3, 0x10
		/*  f0a520c:	ad4b0000 */ 	sw	$t3, 0x0($t2)
		/*  f0a5210:	ad590004 */ 	sw	$t9, 0x4($t2)
		/*  f0a5214:	8fad0144 */ 	lw	$t5, 0x144($sp)
		/*  f0a5218:	3c0f0384 */ 	lui	$t7, 0x384
		/*  f0a521c:	35ef0010 */ 	ori	$t7, $t7, 0x10
		/*  f0a5220:	25ae0008 */ 	addiu	$t6, $t5, 0x8
		/*  f0a5224:	afae0144 */ 	sw	$t6, 0x144($sp)
		/*  f0a5228:	adaf0000 */ 	sw	$t7, 0x0($t5)
		/*  f0a522c:	0fc2cd42 */ 	jal	camGetLookAt
		/*  f0a5230:	afad00cc */ 	sw	$t5, 0xcc($sp)
		/*  f0a5234:	8fa500cc */ 	lw	$a1, 0xcc($sp)
		/*  f0a5238:	3c080382 */ 	lui	$t0, 0x382
		/*  f0a523c:	35080010 */ 	ori	$t0, $t0, 0x10
		/*  f0a5240:	aca20004 */ 	sw	$v0, 0x4($a1)
		/*  f0a5244:	8fac0144 */ 	lw	$t4, 0x144($sp)
		/*  f0a5248:	25890008 */ 	addiu	$t1, $t4, 0x8
		/*  f0a524c:	afa90144 */ 	sw	$t1, 0x144($sp)
		/*  f0a5250:	ad880000 */ 	sw	$t0, 0x0($t4)
		/*  f0a5254:	0fc2cd42 */ 	jal	camGetLookAt
		/*  f0a5258:	afac00c8 */ 	sw	$t4, 0xc8($sp)
		/*  f0a525c:	8fa300c8 */ 	lw	$v1, 0xc8($sp)
		/*  f0a5260:	244a0010 */ 	addiu	$t2, $v0, 0x10
		/*  f0a5264:	ac6a0004 */ 	sw	$t2, 0x4($v1)
		.NB0f0a5268:
/*  f0a5268:	8fb80144 */ 	lw	$t8, 0x144($sp)
/*  f0a526c:	3c19bc00 */ 	lui	$t9, 0xbc00
/*  f0a5270:	3739000e */ 	ori	$t9, $t9, 0xe
/*  f0a5274:	270b0008 */ 	addiu	$t3, $t8, 0x8
/*  f0a5278:	afab0144 */ 	sw	$t3, 0x144($sp)
/*  f0a527c:	3c014396 */ 	lui	$at, 0x4396
/*  f0a5280:	44817000 */ 	mtc1	$at, $f14
/*  f0a5284:	44806000 */ 	mtc1	$zero, $f12
/*  f0a5288:	af190000 */ 	sw	$t9, 0x0($t8)
/*  f0a528c:	0c005f57 */ 	jal	mtx00016dcc
/*  f0a5290:	afb800c4 */ 	sw	$t8, 0xc4($sp)
/*  f0a5294:	8fa300c4 */ 	lw	$v1, 0xc4($sp)
/*  f0a5298:	24050010 */ 	addiu	$a1, $zero, 0x10
/*  f0a529c:	ac620004 */ 	sw	$v0, 0x4($v1)
/*  f0a52a0:	0c006ea3 */ 	jal	modelGetPart
/*  f0a52a4:	8e04038c */ 	lw	$a0, 0x38c($s0)
/*  f0a52a8:	10400014 */ 	beqz	$v0, .NB0f0a52fc
/*  f0a52ac:	afa200e0 */ 	sw	$v0, 0xe0($sp)
/*  f0a52b0:	8e04038c */ 	lw	$a0, 0x38c($s0)
/*  f0a52b4:	0c006ea3 */ 	jal	modelGetPart
/*  f0a52b8:	24050011 */ 	addiu	$a1, $zero, 0x11
/*  f0a52bc:	8fa40038 */ 	lw	$a0, 0x38($sp)
/*  f0a52c0:	0c006bab */ 	jal	modelGetNodeRwData
/*  f0a52c4:	00402825 */ or $a1, $v0, $zero
/*  f0a52c8:	10400003 */ 	beqz	$v0, .NB0f0a52d8
/*  f0a52cc:	3c06800a */ 	lui	$a2, % hi(var8009cf88)
/*  f0a52d0:	240d0001 */ 	addiu	$t5, $zero, 0x1
/*  f0a52d4:	ac4d0000 */ 	sw	$t5, 0x0($v0)
.NB0f0a52d8:
/*  f0a52d8:	240e0001 */ 	addiu	$t6, $zero, 0x1
/*  f0a52dc:	afae0014 */ 	sw	$t6, 0x14($sp)
/*  f0a52e0:	8fa40038 */ 	lw	$a0, 0x38($sp)
/*  f0a52e4:	8fa500e0 */ 	lw	$a1, 0xe0($sp)
/*  f0a52e8:	24c616b8 */ 	addiu	$a2, $a2, % lo(var8009cf88)
/*  f0a52ec:	8fa70144 */ 	lw	$a3, 0x144($sp)
/*  f0a52f0:	0fc1f99c */ 	jal	tvscreenRender
/*  f0a52f4:	afa00010 */ 	sw	$zero, 0x10($sp)
/*  f0a52f8:	afa20144 */ 	sw	$v0, 0x144($sp)
.NB0f0a52fc:
/*  f0a52fc:	8faf0144 */ 	lw	$t7, 0x144($sp)
/*  f0a5300:	8e250284 */ 	lw	$a1, 0x284($s1)
/*  f0a5304:	240c0004 */ 	addiu	$t4, $zero, 0x4
/*  f0a5308:	afac0134 */ 	sw	$t4, 0x134($sp)
/*  f0a530c:	afaf0110 */ 	sw	$t7, 0x110($sp)
/*  f0a5310:	8ca300d8 */ 	lw	$v1, 0xd8($a1)
/*  f0a5314:	3c078007 */ 	lui	$a3, % hi(g_InCutscene)
/*  f0a5318:	14600013 */ 	bnez	$v1, .NB0f0a5368
/*  f0a531c:	00000000 */ 	sll	$zero, $zero, 0x0
/*  f0a5320:	8ce72e24 */ 	lw	$a3, % lo(g_InCutscene)($a3)
/*  f0a5324:	14e00010 */ 	bnez	$a3, .NB0f0a5368
/*  f0a5328:	00000000 */ 	sll	$zero, $zero, 0x0
/*  f0a532c:	8ca20480 */ 	lw	$v0, 0x480($a1)
/*  f0a5330:	50400007 */ 	beqzl	$v0, .NB0f0a5350
/*  f0a5334:	8caa1c54 */ 	lw	$t2, 0x1c54($a1)
/*  f0a5338:	1040000b */ 	beqz	$v0, .NB0f0a5368
/*  f0a533c:	00000000 */ 	sll	$zero, $zero, 0x0
/*  f0a5340:	80490037 */ 	lb	$t1, 0x37($v0)
/*  f0a5344:	15200008 */ 	bnez	$t1, .NB0f0a5368
/*  f0a5348:	00000000 */ 	sll	$zero, $zero, 0x0
/*  f0a534c:	8caa1c54 */ 	lw	$t2, 0x1c54($a1)
.NB0f0a5350:
/*  f0a5350:	8ca800c4 */ 	lw	$t0, 0xc4($a1)
/*  f0a5354:	0140c027 */ 	nor	$t8, $t2, $zero
/*  f0a5358:	01185824 */ and $t3, $t0, $t8
/*  f0a535c:	31790001 */ 	andi	$t9, $t3, 0x1
/*  f0a5360:	57200016 */ 	bnezl	$t9, .NB0f0a53bc
/*  f0a5364:	92681615 */ 	lbu	$t0, 0x1615($s3)
.NB0f0a5368:
/*  f0a5368:	14600078 */ 	bnez	$v1, .NB0f0a554c
/*  f0a536c:	3c078007 */ 	lui	$a3, % hi(g_InCutscene)
/*  f0a5370:	8ce72e24 */ 	lw	$a3, % lo(g_InCutscene)($a3)
/*  f0a5374:	54e00076 */ 	bnezl	$a3, .NB0f0a5550
/*  f0a5378:	926d1614 */ 	lbu	$t5, 0x1614($s3)
/*  f0a537c:	8ca20480 */ 	lw	$v0, 0x480($a1)
/*  f0a5380:	50400007 */ 	beqzl	$v0, .NB0f0a53a0
/*  f0a5384:	8caf1c54 */ 	lw	$t7, 0x1c54($a1)
/*  f0a5388:	50400071 */ 	beqzl	$v0, .NB0f0a5550
/*  f0a538c:	926d1614 */ 	lbu	$t5, 0x1614($s3)
/*  f0a5390:	804d0037 */ 	lb	$t5, 0x37($v0)
/*  f0a5394:	55a0006e */ 	bnezl	$t5, .NB0f0a5550
/*  f0a5398:	926d1614 */ 	lbu	$t5, 0x1614($s3)
/*  f0a539c:	8caf1c54 */ 	lw	$t7, 0x1c54($a1)
.NB0f0a53a0:
/*  f0a53a0:	8cae00c4 */ 	lw	$t6, 0xc4($a1)
/*  f0a53a4:	01e06027 */ 	nor	$t4, $t7, $zero
/*  f0a53a8:	01cc4824 */ and $t1, $t6, $t4
/*  f0a53ac:	312a0008 */ 	andi	$t2, $t1, 0x8
/*  f0a53b0:	51400067 */ 	beqzl	$t2, .NB0f0a5550
/*  f0a53b4:	926d1614 */ 	lbu	$t5, 0x1614($s3)
/*  f0a53b8:	92681615 */ 	lbu	$t0, 0x1615($s3)
.NB0f0a53bc:
/*  f0a53bc:	92781614 */ 	lbu	$t8, 0x1614($s3)
/*  f0a53c0:	26641614 */ 	addiu	$a0, $s3, 0x1614
/*  f0a53c4:	0118082a */ 	slt	$at, $t0, $t8
/*  f0a53c8:	50200009 */ 	beqzl	$at, .NB0f0a53f0
/*  f0a53cc:	90820002 */ 	lbu	$v0, 0x2($a0)
/*  f0a53d0:	90830000 */ 	lbu	$v1, 0x0($a0)
/*  f0a53d4:	908b0002 */ 	lbu	$t3, 0x2($a0)
/*  f0a53d8:	0163082a */ 	slt	$at, $t3, $v1
/*  f0a53dc:	50200004 */ 	beqzl	$at, .NB0f0a53f0
/*  f0a53e0:	90820002 */ 	lbu	$v0, 0x2($a0)
/*  f0a53e4:	1000000a */ 	beqz	$zero, .NB0f0a5410
/*  f0a53e8:	00601025 */ or $v0, $v1, $zero
/*  f0a53ec:	90820002 */ 	lbu	$v0, 0x2($a0)
.NB0f0a53f0:
/*  f0a53f0:	90860001 */ 	lbu	$a2, 0x1($a0)
/*  f0a53f4:	00401825 */ or $v1, $v0, $zero
/*  f0a53f8:	0046082a */ 	slt	$at, $v0, $a2
/*  f0a53fc:	10200003 */ 	beqz	$at, .NB0f0a540c
/*  f0a5400:	00000000 */ 	sll	$zero, $zero, 0x0
/*  f0a5404:	10000001 */ 	beqz	$zero, .NB0f0a540c
/*  f0a5408:	00c01825 */ or $v1, $a2, $zero
.NB0f0a540c:
/*  f0a540c:	00601025 */ or $v0, $v1, $zero
.NB0f0a5410 :
		/*  f0a5410:	90890003 */ 	lbu	$t1, 0x3($a0)
		/*  f0a5414:	0002ce00 */ 	sll	$t9, $v0, 0x18
		/*  f0a5418:	00026c00 */ 	sll	$t5, $v0, 0x10
		/*  f0a541c:	032d7825 */ or $t7, $t9, $t5
		/*  f0a5420:	00027200 */ 	sll	$t6, $v0, 0x8
		/*  f0a5424:	01ee6025 */ or $t4, $t7, $t6
		/*  f0a5428:	012c5021 */ 	addu	$t2, $t1, $t4
		/*  f0a542c:	afaa0138 */ 	sw	$t2, 0x138($sp)
		/*  f0a5430:	8ca300d8 */ 	lw	$v1, 0xd8($a1)
		/*  f0a5434:	14600021 */ 	bnez	$v1, .NB0f0a54bc
		/*  f0a5438:	00000000 */ 	sll	$zero, $zero, 0x0
		/*  f0a543c:	14e0001f */ 	bnez	$a3, .NB0f0a54bc
		/*  f0a5440:	00000000 */ 	sll	$zero, $zero, 0x0
		/*  f0a5444:	8ca20480 */ 	lw	$v0, 0x480($a1)
		/*  f0a5448:	50400007 */ 	beqzl	$v0, .NB0f0a5468
		/*  f0a544c:	8cab1c54 */ 	lw	$t3, 0x1c54($a1)
		/*  f0a5450:	1040001a */ 	beqz	$v0, .NB0f0a54bc
		/*  f0a5454:	00000000 */ 	sll	$zero, $zero, 0x0
		/*  f0a5458:	80480037 */ 	lb	$t0, 0x37($v0)
		/*  f0a545c:	15000017 */ 	bnez	$t0, .NB0f0a54bc
		/*  f0a5460:	00000000 */ 	sll	$zero, $zero, 0x0
		/*  f0a5464:	8cab1c54 */ 	lw	$t3, 0x1c54($a1)
		.NB0f0a5468:
/*  f0a5468:	8cb800c4 */ 	lw	$t8, 0xc4($a1)
/*  f0a546c:	3c02800a */ 	lui	$v0, % hi(var8009caef)
/*  f0a5470:	0160c827 */ 	nor	$t9, $t3, $zero
/*  f0a5474:	03196824 */ and $t5, $t8, $t9
/*  f0a5478:	31af0001 */ 	andi	$t7, $t5, 0x1
/*  f0a547c:	11e0000f */ 	beqz	$t7, .NB0f0a54bc
/*  f0a5480:	00000000 */ 	sll	$zero, $zero, 0x0
/*  f0a5484:	9042122f */ 	lbu	$v0, % lo(var8009caef)($v0)
/*  f0a5488:	3c06800a */ 	lui	$a2, % hi(var8009caf0)
/*  f0a548c:	90c61230 */ 	lbu	$a2, % lo(var8009caf0)($a2)
/*  f0a5490:	00027600 */ 	sll	$t6, $v0, 0x18
/*  f0a5494:	00024c00 */ 	sll	$t1, $v0, 0x10
/*  f0a5498:	01c96025 */ or $t4, $t6, $t1
/*  f0a549c:	00025200 */ 	sll	$t2, $v0, 0x8
/*  f0a54a0:	018a4025 */ or $t0, $t4, $t2
/*  f0a54a4:	afa200a8 */ 	sw	$v0, 0xa8($sp)
/*  f0a54a8:	afa200ac */ 	sw	$v0, 0xac($sp)
/*  f0a54ac:	afa200b0 */ 	sw	$v0, 0xb0($sp)
/*  f0a54b0:	00c8a821 */ 	addu	$s5, $a2, $t0
/*  f0a54b4:	1000001f */ 	beqz	$zero, .NB0f0a5534
/*  f0a54b8:	afa600b4 */ 	sw	$a2, 0xb4($sp)
.NB0f0a54bc:
/*  f0a54bc:	5460001e */ 	bnezl	$v1, .NB0f0a5538
/*  f0a54c0:	8fb900e4 */ 	lw	$t9, 0xe4($sp)
/*  f0a54c4:	54e0001c */ 	bnezl	$a3, .NB0f0a5538
/*  f0a54c8:	8fb900e4 */ 	lw	$t9, 0xe4($sp)
/*  f0a54cc:	8ca20480 */ 	lw	$v0, 0x480($a1)
/*  f0a54d0:	50400007 */ 	beqzl	$v0, .NB0f0a54f0
/*  f0a54d4:	8cb91c54 */ 	lw	$t9, 0x1c54($a1)
/*  f0a54d8:	50400017 */ 	beqzl	$v0, .NB0f0a5538
/*  f0a54dc:	8fb900e4 */ 	lw	$t9, 0xe4($sp)
/*  f0a54e0:	804b0037 */ 	lb	$t3, 0x37($v0)
/*  f0a54e4:	55600014 */ 	bnezl	$t3, .NB0f0a5538
/*  f0a54e8:	8fb900e4 */ 	lw	$t9, 0xe4($sp)
/*  f0a54ec:	8cb91c54 */ 	lw	$t9, 0x1c54($a1)
.NB0f0a54f0:
/*  f0a54f0:	8cb800c4 */ 	lw	$t8, 0xc4($a1)
/*  f0a54f4:	240200ff */ 	addiu	$v0, $zero, 0xff
/*  f0a54f8:	03206827 */ 	nor	$t5, $t9, $zero
/*  f0a54fc:	030d7824 */ and $t7, $t8, $t5
/*  f0a5500:	31ee0008 */ 	andi	$t6, $t7, 0x8
/*  f0a5504:	11c0000b */ 	beqz	$t6, .NB0f0a5534
/*  f0a5508:	24050080 */ 	addiu	$a1, $zero, 0x80
/*  f0a550c:	00024e00 */ 	sll	$t1, $v0, 0x18
/*  f0a5510:	00006400 */ 	sll	$t4, $zero, 0x10
/*  f0a5514:	012c5025 */ or $t2, $t1, $t4
/*  f0a5518:	00004200 */ 	sll	$t0, $zero, 0x8
/*  f0a551c:	01485825 */ or $t3, $t2, $t0
/*  f0a5520:	00aba821 */ 	addu	$s5, $a1, $t3
/*  f0a5524:	afa20098 */ 	sw	$v0, 0x98($sp)
/*  f0a5528:	afa0009c */ 	sw	$zero, 0x9c($sp)
/*  f0a552c:	afa000a0 */ 	sw	$zero, 0xa0($sp)
/*  f0a5530:	afa500a4 */ 	sw	$a1, 0xa4($sp)
.NB0f0a5534:
/*  f0a5534:	8fb900e4 */ 	lw	$t9, 0xe4($sp)
.NB0f0a5538 :
		/*  f0a5538:	24010001 */ 	addiu	$at, $zero, 0x1
		/*  f0a553c:	5721003d */ 	bnel	$t9, $at, .NB0f0a5634
		/*  f0a5540:	8e6f00bc */ 	lw	$t7, 0xbc($s3)
		/*  f0a5544:	1000003a */ 	beqz	$zero, .NB0f0a5630
		/*  f0a5548:	afb50138 */ 	sw	$s5, 0x138($sp)
		.NB0f0a554c:
/*  f0a554c:	926d1614 */ 	lbu	$t5, 0x1614($s3)
.NB0f0a5550 :
		/*  f0a5550:	92781617 */ 	lbu	$t8, 0x1617($s3)
		/*  f0a5554:	92691615 */ 	lbu	$t1, 0x1615($s3)
		/*  f0a5558:	92681616 */ 	lbu	$t0, 0x1616($s3)
		/*  f0a555c:	000d7e00 */ 	sll	$t7, $t5, 0x18
		/*  f0a5560:	030f7025 */ or $t6, $t8, $t7
		/*  f0a5564:	00096400 */ 	sll	$t4, $t1, 0x10
		/*  f0a5568:	01cc5025 */ or $t2, $t6, $t4
		/*  f0a556c:	00085a00 */ 	sll	$t3, $t0, 0x8
		/*  f0a5570:	014bc825 */ or $t9, $t2, $t3
		/*  f0a5574:	afb90138 */ 	sw	$t9, 0x138($sp)
		/*  f0a5578:	920d0000 */ 	lbu	$t5, 0x0($s0)
		/*  f0a557c:	24010006 */ 	addiu	$at, $zero, 0x6
		/*  f0a5580:	0320a825 */ or $s5, $t9, $zero
		/*  f0a5584:	15a1002a */ 	bne	$t5, $at, .NB0f0a5630
		/*  f0a5588:	3c04ff00 */ 	lui	$a0, 0xff00
		/*  f0a558c:	3c014248 */ 	lui	$at, 0x4248
		/*  f0a5590:	44812000 */ 	mtc1	$at, $f4
		/*  f0a5594:	c612023c */ 	lwc1	$f18, 0x23c($s0)
		/*  f0a5598:	24060001 */ 	addiu	$a2, $zero, 0x1
		/*  f0a559c:	3c014f00 */ 	lui	$at, 0x4f00
		/*  f0a55a0:	46049182 */ 	mul.s	$f6, $f18, $f4
		/*  f0a55a4:	3484007f */ 	ori	$a0, $a0, 0x7f
		/*  f0a55a8:	4458f800 */ 	cfc1	$t8, $31
		/*  f0a55ac:	44c6f800 */ 	ctc1	$a2, $31
		/*  f0a55b0:	00000000 */ 	sll	$zero, $zero, 0x0
		/*  f0a55b4:	46003224 */ 	cvt.w.s	$f8, $f6
		/*  f0a55b8:	4446f800 */ 	cfc1	$a2, $31
		/*  f0a55bc:	00000000 */ 	sll	$zero, $zero, 0x0
		/*  f0a55c0:	30c60078 */ 	andi	$a2, $a2, 0x78
		/*  f0a55c4:	50c00013 */ 	beqzl	$a2, .NB0f0a5614
		/*  f0a55c8:	44064000 */ 	mfc1	$a2, $f8
		/*  f0a55cc:	44814000 */ 	mtc1	$at, $f8
		/*  f0a55d0:	24060001 */ 	addiu	$a2, $zero, 0x1
		/*  f0a55d4:	46083201 */ 	sub.s	$f8, $f6, $f8
		/*  f0a55d8:	44c6f800 */ 	ctc1	$a2, $31
		/*  f0a55dc:	00000000 */ 	sll	$zero, $zero, 0x0
		/*  f0a55e0:	46004224 */ 	cvt.w.s	$f8, $f8
		/*  f0a55e4:	4446f800 */ 	cfc1	$a2, $31
		/*  f0a55e8:	00000000 */ 	sll	$zero, $zero, 0x0
		/*  f0a55ec:	30c60078 */ 	andi	$a2, $a2, 0x78
		/*  f0a55f0:	14c00005 */ 	bnez	$a2, .NB0f0a5608
		/*  f0a55f4:	00000000 */ 	sll	$zero, $zero, 0x0
		/*  f0a55f8:	44064000 */ 	mfc1	$a2, $f8
		/*  f0a55fc:	3c018000 */ 	lui	$at, 0x8000
		/*  f0a5600:	10000007 */ 	beqz	$zero, .NB0f0a5620
		/*  f0a5604:	00c13025 */ or $a2, $a2, $at
		.NB0f0a5608:
/*  f0a5608:	10000005 */ 	beqz	$zero, .NB0f0a5620
/*  f0a560c:	2406ffff */ 	addiu	$a2, $zero, -1
/*  f0a5610:	44064000 */ 	mfc1	$a2, $f8
.NB0f0a5614 :
		/*  f0a5614:	00000000 */ 	sll	$zero, $zero, 0x0
		/*  f0a5618:	04c0fffb */ 	bltz	$a2, .NB0f0a5608
		/*  f0a561c:	00000000 */ 	sll	$zero, $zero, 0x0
		.NB0f0a5620 :
		/*  f0a5620:	44d8f800 */ 	ctc1	$t8, $31
		/*  f0a5624:	0fc01990 */ 	jal	colourBlend
		/*  f0a5628:	03202825 */ or $a1, $t9, $zero
		/*  f0a562c:	afa20138 */ 	sw	$v0, 0x138($sp)
		.NB0f0a5630 :
		/*  f0a5630:	8e6f00bc */ 	lw	$t7, 0xbc($s3)
		.NB0f0a5634 :
		/*  f0a5634:	0fc089b4 */ 	jal	chrGetCloakAlpha
		/*  f0a5638:	8de40004 */ 	lw	$a0, 0x4($t7)
		/*  f0a563c:	284100ff */ 	slti	$at, $v0, 0xff
		/*  f0a5640:	1020000f */ 	beqz	$at, .NB0f0a5680
		/*  f0a5644:	240c0001 */ 	addiu	$t4, $zero, 0x1
		/*  f0a5648:	44825000 */ 	mtc1	$v0, $f10
		/*  f0a564c:	3c017f1a */ 	lui	$at, % hi(var7f1aca90)
		/*  f0a5650:	c4326dd8 */ 	lwc1	$f18, % lo(var7f1aca90)($at)
		/*  f0a5654:	46805420 */ 	cvt.s.w	$f16, $f10
		/*  f0a5658:	8fa40138 */ 	lw	$a0, 0x138($sp)
		/*  f0a565c:	240e0005 */ 	addiu	$t6, $zero, 0x5
		/*  f0a5660:	afae0134 */ 	sw	$t6, 0x134($sp)
		/*  f0a5664:	afa4013c */ 	sw	$a0, 0x13c($sp)
		/*  f0a5668:	46128102 */ 	mul.s	$f4, $f16, $f18
		/*  f0a566c:	4600218d */ 	trunc.w.s	$f6, $f4
		/*  f0a5670:	44033000 */ 	mfc1	$v1, $f6
		/*  f0a5674:	00000000 */ 	sll	$zero, $zero, 0x0
		/*  f0a5678:	24750041 */ 	addiu	$s5, $v1, 0x41
		/*  f0a567c:	afb50138 */ 	sw	$s5, 0x138($sp)
		.NB0f0a5680:
/*  f0a5680:	0c005dbc */ 	jal	mtx00016760
/*  f0a5684:	afac0108 */ 	sw	$t4, 0x108($sp)
/*  f0a5688:	8e020218 */ 	lw	$v0, 0x218($s0)
/*  f0a568c:	5040000f */ 	beqzl	$v0, .NB0f0a56cc
/*  f0a5690:	8fa400e4 */ 	lw	$a0, 0xe4($sp)
/*  f0a5694:	8c450018 */ 	lw	$a1, 0x18($v0)
/*  f0a5698:	27a40104 */ 	addiu	$a0, $sp, 0x104
/*  f0a569c:	0c008be3 */ 	jal	modelRender
/*  f0a56a0:	afa50090 */ 	sw	$a1, 0x90($sp)
/*  f0a56a4:	8fa60090 */ 	lw	$a2, 0x90($sp)
/*  f0a56a8:	8cc80008 */ 	lw	$t0, 0x8($a2)
/*  f0a56ac:	8cc4000c */ 	lw	$a0, 0xc($a2)
/*  f0a56b0:	0fc303f0 */ 	jal	mtxF2LBulk
/*  f0a56b4:	8505000e */ 	lh	$a1, 0xe($t0)
/*  f0a56b8:	8e0a021c */ 	lw	$t2, 0x21c($s0)
/*  f0a56bc:	51400003 */ 	beqzl	$t2, .NB0f0a56cc
/*  f0a56c0:	8fa400e4 */ 	lw	$a0, 0xe4($sp)
/*  f0a56c4:	ae000218 */ 	sw	$zero, 0x218($s0)
/*  f0a56c8:	8fa400e4 */ 	lw	$a0, 0xe4($sp)
.NB0f0a56cc:
/*  f0a56cc:	0fc2bd48 */ 	jal	weaponHasFlag
/*  f0a56d0:	24050020 */ 	addiu	$a1, $zero, 0x20
/*  f0a56d4:	1040000e */ 	beqz	$v0, .NB0f0a5710
/*  f0a56d8:	24010001 */ 	addiu	$at, $zero, 0x1
/*  f0a56dc:	8fab0110 */ 	lw	$t3, 0x110($sp)
/*  f0a56e0:	3c18b600 */ 	lui	$t8, 0xb600
/*  f0a56e4:	24193000 */ 	addiu	$t9, $zero, 0x3000
/*  f0a56e8:	256d0008 */ 	addiu	$t5, $t3, 0x8
/*  f0a56ec:	afad0110 */ 	sw	$t5, 0x110($sp)
/*  f0a56f0:	ad790004 */ 	sw	$t9, 0x4($t3)
/*  f0a56f4:	16800004 */ 	bnez	$s4, .NB0f0a5708
/*  f0a56f8:	ad780000 */ 	sw	$t8, 0x0($t3)
/*  f0a56fc:	240f0003 */ 	addiu	$t7, $zero, 0x3
/*  f0a5700:	10000003 */ 	beqz	$zero, .NB0f0a5710
/*  f0a5704:	afaf0140 */ 	sw	$t7, 0x140($sp)
.NB0f0a5708:
/*  f0a5708:	24090002 */ 	addiu	$t1, $zero, 0x2
/*  f0a570c:	afa90140 */ 	sw	$t1, 0x140($sp)
.NB0f0a5710 :
		/*  f0a5710:	8e2e006c */ 	lw	$t6, 0x6c($s1)
		/*  f0a5714:	00002825 */ or $a1, $zero, $zero
		/*  f0a5718:	00002025 */ or $a0, $zero, $zero
		/*  f0a571c:	11c00003 */ 	beqz	$t6, .NB0f0a572c
		/*  f0a5720:	00001025 */ or $v0, $zero, $zero
		/*  f0a5724:	10000001 */ 	beqz	$zero, .NB0f0a572c
		/*  f0a5728:	24050001 */ 	addiu	$a1, $zero, 0x1
		.NB0f0a572c:
/*  f0a572c:	8e2c0068 */ 	lw	$t4, 0x68($s1)
/*  f0a5730:	00001825 */ or $v1, $zero, $zero
/*  f0a5734:	11800003 */ 	beqz	$t4, .NB0f0a5744
/*  f0a5738:	00000000 */ 	sll	$zero, $zero, 0x0
/*  f0a573c:	10000001 */ 	beqz	$zero, .NB0f0a5744
/*  f0a5740:	24040001 */ 	addiu	$a0, $zero, 0x1
.NB0f0a5744:
/*  f0a5744:	8e280064 */ 	lw	$t0, 0x64($s1)
/*  f0a5748:	11000003 */ 	beqz	$t0, .NB0f0a5758
/*  f0a574c:	00000000 */ 	sll	$zero, $zero, 0x0
/*  f0a5750:	10000001 */ 	beqz	$zero, .NB0f0a5758
/*  f0a5754:	24020001 */ 	addiu	$v0, $zero, 0x1
.NB0f0a5758:
/*  f0a5758:	8e2a0070 */ 	lw	$t2, 0x70($s1)
/*  f0a575c:	11400003 */ 	beqz	$t2, .NB0f0a576c
/*  f0a5760:	00000000 */ 	sll	$zero, $zero, 0x0
/*  f0a5764:	10000001 */ 	beqz	$zero, .NB0f0a576c
/*  f0a5768:	24030001 */ 	addiu	$v1, $zero, 0x1
.NB0f0a576c:
/*  f0a576c:	00625821 */ 	addu	$t3, $v1, $v0
/*  f0a5770:	01646821 */ 	addu	$t5, $t3, $a0
/*  f0a5774:	01a5c021 */ 	addu	$t8, $t5, $a1
/*  f0a5778:	17010036 */ 	bne	$t8, $at, .NB0f0a5854
/*  f0a577c:	24050041 */ 	addiu	$a1, $zero, 0x41
/*  f0a5780:	0c006ea3 */ 	jal	modelGetPart
/*  f0a5784:	8e04038c */ 	lw	$a0, 0x38c($s0)
/*  f0a5788:	50400033 */ 	beqzl	$v0, .NB0f0a5858
/*  f0a578c:	27a40104 */ 	addiu	$a0, $sp, 0x104
/*  f0a5790:	8c440004 */ 	lw	$a0, 0x4($v0)
/*  f0a5794:	00002825 */ or $a1, $zero, $zero
/*  f0a5798:	84990010 */ 	lh	$t9, 0x10($a0)
/*  f0a579c:	5b20002e */ 	blezl	$t9, .NB0f0a5858
/*  f0a57a0:	27a40104 */ 	addiu	$a0, $sp, 0x104
/*  f0a57a4:	8e2c0034 */ 	lw	$t4, 0x34($s1)
.NB0f0a57a8:
/*  f0a57a8:	8c8f000c */ 	lw	$t7, 0xc($a0)
/*  f0a57ac:	00054880 */ 	sll	$t1, $a1, 0x2
/*  f0a57b0:	01920019 */ 	multu	$t4, $s2
/*  f0a57b4:	01254823 */ 	subu	$t1, $t1, $a1
/*  f0a57b8:	00094880 */ 	sll	$t1, $t1, 0x2
/*  f0a57bc:	01e91021 */ 	addu	$v0, $t7, $t1
/*  f0a57c0:	844e000a */ 	lh	$t6, 0xa($v0)
/*  f0a57c4:	00056880 */ 	sll	$t5, $a1, 0x2
/*  f0a57c8:	01a56823 */ 	subu	$t5, $t5, $a1
/*  f0a57cc:	000d6880 */ 	sll	$t5, $t5, 0x2
/*  f0a57d0:	24420002 */ 	addiu	$v0, $v0, 0x2
/*  f0a57d4:	00004012 */ 	mflo	$t0
/*  f0a57d8:	01c85023 */ 	subu	$t2, $t6, $t0
/*  f0a57dc:	a44a0008 */ 	sh	$t2, 0x8($v0)
/*  f0a57e0:	8c8b000c */ 	lw	$t3, 0xc($a0)
/*  f0a57e4:	016dc021 */ 	addu	$t8, $t3, $t5
/*  f0a57e8:	8719000a */ 	lh	$t9, 0xa($t8)
/*  f0a57ec:	2b21a000 */ 	slti	$at, $t9, -24576
/*  f0a57f0:	50200014 */ 	beqzl	$at, .NB0f0a5844
/*  f0a57f4:	848b0010 */ 	lh	$t3, 0x10($a0)
/*  f0a57f8:	848f0010 */ 	lh	$t7, 0x10($a0)
/*  f0a57fc:	00001825 */ or $v1, $zero, $zero
/*  f0a5800:	59e00010 */ 	blezl	$t7, .NB0f0a5844
/*  f0a5804:	848b0010 */ 	lh	$t3, 0x10($a0)
/*  f0a5808:	8c89000c */ 	lw	$t1, 0xc($a0)
.NB0f0a580c:
/*  f0a580c:	00036080 */ 	sll	$t4, $v1, 0x2
/*  f0a5810:	01836023 */ 	subu	$t4, $t4, $v1
/*  f0a5814:	000c6080 */ 	sll	$t4, $t4, 0x2
/*  f0a5818:	012c1021 */ 	addu	$v0, $t1, $t4
/*  f0a581c:	844e000a */ 	lh	$t6, 0xa($v0)
/*  f0a5820:	24630001 */ 	addiu	$v1, $v1, 0x1
/*  f0a5824:	24420002 */ 	addiu	$v0, $v0, 0x2
/*  f0a5828:	25c82000 */ 	addiu	$t0, $t6, 0x2000
/*  f0a582c:	a4480008 */ 	sh	$t0, 0x8($v0)
/*  f0a5830:	848a0010 */ 	lh	$t2, 0x10($a0)
/*  f0a5834:	006a082a */ 	slt	$at, $v1, $t2
/*  f0a5838:	5420fff4 */ 	bnezl	$at, .NB0f0a580c
/*  f0a583c:	8c89000c */ 	lw	$t1, 0xc($a0)
/*  f0a5840:	848b0010 */ 	lh	$t3, 0x10($a0)
.NB0f0a5844:
/*  f0a5844:	24a50001 */ 	addiu	$a1, $a1, 0x1
/*  f0a5848:	00ab082a */ 	slt	$at, $a1, $t3
/*  f0a584c:	5420ffd6 */ 	bnezl	$at, .NB0f0a57a8
/*  f0a5850:	8e2c0034 */ 	lw	$t4, 0x34($s1)
.NB0f0a5854 :
		/*  f0a5854:	27a40104 */ 	addiu	$a0, $sp, 0x104
		.NB0f0a5858 :
		/*  f0a5858:	0c008be3 */ 	jal	modelRender
		/*  f0a585c:	8fa50038 */ 	lw	$a1, 0x38($sp)
		/*  f0a5860:	8e6d1594 */ 	lw	$t5, 0x1594($s3)
		/*  f0a5864:	3c188007 */ 	lui	$t8, % hi(var800702dc)
		/*  f0a5868:	51a00013 */ 	beqzl	$t5, .NB0f0a58b8
		/*  f0a586c:	8fa90110 */ 	lw	$t1, 0x110($sp)
		/*  f0a5870:	8f18299c */ 	lw	$t8, % lo(var800702dc)($t8)
		/*  f0a5874:	8fb90138 */ 	lw	$t9, 0x138($sp)
		/*  f0a5878:	5300000f */ 	beqzl	$t8, .NB0f0a58b8
		/*  f0a587c:	8fa90110 */ 	lw	$t1, 0x110($sp)
		/*  f0a5880:	afb90078 */ 	sw	$t9, 0x78($sp)
		/*  f0a5884:	8e0f0390 */ 	lw	$t7, 0x390($s0)
		/*  f0a5888:	26050534 */ 	addiu	$a1, $s0, 0x534
		/*  f0a588c:	00a02025 */ or $a0, $a1, $zero
		/*  f0a5890:	ae0f0540 */ 	sw	$t7, 0x540($s0)
		/*  f0a5894:	0c007728 */ 	jal	modelUpdateRelations
		/*  f0a5898:	afa50050 */ 	sw	$a1, 0x50($sp)
		/*  f0a589c:	8fa50050 */ 	lw	$a1, 0x50($sp)
		/*  f0a58a0:	afb50138 */ 	sw	$s5, 0x138($sp)
		/*  f0a58a4:	0c008be3 */ 	jal	modelRender
		/*  f0a58a8:	27a40104 */ 	addiu	$a0, $sp, 0x104
		/*  f0a58ac:	8fa40078 */ 	lw	$a0, 0x78($sp)
		/*  f0a58b0:	afa40138 */ 	sw	$a0, 0x138($sp)
		/*  f0a58b4:	8fa90110 */ 	lw	$t1, 0x110($sp)
		.NB0f0a58b8:
/*  f0a58b8:	8fa400e4 */ 	lw	$a0, 0xe4($sp)
/*  f0a58bc:	24050020 */ 	addiu	$a1, $zero, 0x20
/*  f0a58c0:	0fc2bd48 */ 	jal	weaponHasFlag
/*  f0a58c4:	afa90144 */ 	sw	$t1, 0x144($sp)
/*  f0a58c8:	10400007 */ 	beqz	$v0, .NB0f0a58e8
/*  f0a58cc:	8fac0144 */ 	lw	$t4, 0x144($sp)
/*  f0a58d0:	258e0008 */ 	addiu	$t6, $t4, 0x8
/*  f0a58d4:	afae0144 */ 	sw	$t6, 0x144($sp)
/*  f0a58d8:	3c08b600 */ 	lui	$t0, 0xb600
/*  f0a58dc:	240a3000 */ 	addiu	$t2, $zero, 0x3000
/*  f0a58e0:	ad8a0004 */ 	sw	$t2, 0x4($t4)
/*  f0a58e4:	ad880000 */ 	sw	$t0, 0x0($t4)
.NB0f0a58e8:
/*  f0a58e8:	8e0b038c */ 	lw	$t3, 0x38c($s0)
/*  f0a58ec:	8e040390 */ 	lw	$a0, 0x390($s0)
/*  f0a58f0:	0fc303f0 */ 	jal	mtxF2LBulk
/*  f0a58f4:	8565000e */ 	lh	$a1, 0xe($t3)
/*  f0a58f8:	0c005dc5 */ 	jal	mtx00016784
/*  f0a58fc:	00000000 */ 	sll	$zero, $zero, 0x0
/*  f0a5900:	8fad0144 */ 	lw	$t5, 0x144($sp)
/*  f0a5904:	3c19bc00 */ 	lui	$t9, 0xbc00
/*  f0a5908:	3739000e */ 	ori	$t9, $t9, 0xe
/*  f0a590c:	25b80008 */ 	addiu	$t8, $t5, 0x8
/*  f0a5910:	afb80144 */ 	sw	$t8, 0x144($sp)
/*  f0a5914:	adb90000 */ 	sw	$t9, 0x0($t5)
/*  f0a5918:	0c002b3b */ 	jal	viGetPerspScale
/*  f0a591c:	afad0070 */ 	sw	$t5, 0x70($sp)
/*  f0a5920:	8fa30070 */ 	lw	$v1, 0x70($sp)
/*  f0a5924:	ac620004 */ 	sw	$v0, 0x4($v1)
.NB0f0a5928:
/*  f0a5928:	26940001 */ 	addiu	$s4, $s4, 0x1
/*  f0a592c:	24010002 */ 	addiu	$at, $zero, 0x2
/*  f0a5930:	1681fe0c */ 	bne	$s4, $at, .NB0f0a5164
/*  f0a5934:	261007a4 */ 	addiu	$s0, $s0, 0x7a4
/*  f0a5938:	afb500dc */ 	sw	$s5, 0xdc($sp)
/*  f0a593c:	0fc2b250 */ 	jal	casingsRender
/*  f0a5940:	27a40144 */ 	addiu	$a0, $sp, 0x144
/*  f0a5944:	0fc5c3ce */ 	jal	zbufSwap
/*  f0a5948:	00000000 */ 	sll	$zero, $zero, 0x0
/*  f0a594c:	0fc5c3d3 */ 	jal	zbufConfigureRdp
/*  f0a5950:	8fa40144 */ 	lw	$a0, 0x144($sp)
/*  f0a5954:	afa20144 */ 	sw	$v0, 0x144($sp)
/*  f0a5958:	0c002cd4 */ 	jal	vi0000b1d0
/*  f0a595c:	00402025 */ or $a0, $v0, $zero
/*  f0a5960:	244f0008 */ 	addiu	$t7, $v0, 0x8
/*  f0a5964:	afaf0144 */ 	sw	$t7, 0x144($sp)
/*  f0a5968:	0c002fb5 */ 	jal	viGetViewLeft
/*  f0a596c:	00408825 */ or $s1, $v0, $zero
/*  f0a5970:	00028400 */ 	sll	$s0, $v0, 0x10
/*  f0a5974:	00104c03 */ 	sra	$t1, $s0, 0x10
/*  f0a5978:	0c002fb9 */ 	jal	viGetViewTop
/*  f0a597c:	01208025 */ or $s0, $t1, $zero
/*  f0a5980:	44824000 */ 	mtc1	$v0, $f8
/*  f0a5984:	44902000 */ 	mtc1	$s0, $f4
/*  f0a5988:	3c014080 */ 	lui	$at, 0x4080
/*  f0a598c:	468042a0 */ 	cvt.s.w	$f10, $f8
/*  f0a5990:	44810000 */ 	mtc1	$at, $f0
/*  f0a5994:	3c01ed00 */ 	lui	$at, 0xed00
/*  f0a5998:	468021a0 */ 	cvt.s.w	$f6, $f4
/*  f0a599c:	46005402 */ 	mul.s	$f16, $f10, $f0
/*  f0a59a0:	00000000 */ 	sll	$zero, $zero, 0x0
/*  f0a59a4:	46003202 */ 	mul.s	$f8, $f6, $f0
/*  f0a59a8:	4600848d */ 	trunc.w.s	$f18, $f16
/*  f0a59ac:	4600428d */ 	trunc.w.s	$f10, $f8
/*  f0a59b0:	440e9000 */ 	mfc1	$t6, $f18
/*  f0a59b4:	440d5000 */ 	mfc1	$t5, $f10
/*  f0a59b8:	31c80fff */ 	andi	$t0, $t6, 0xfff
/*  f0a59bc:	01015025 */ or $t2, $t0, $at
/*  f0a59c0:	31b80fff */ 	andi	$t8, $t5, 0xfff
/*  f0a59c4:	0018cb00 */ 	sll	$t9, $t8, 0xc
/*  f0a59c8:	01597825 */ or $t7, $t2, $t9
/*  f0a59cc:	0c002f97 */ 	jal	viGetViewWidth
/*  f0a59d0:	ae2f0000 */ 	sw	$t7, 0x0($s1)
/*  f0a59d4:	00029400 */ 	sll	$s2, $v0, 0x10
/*  f0a59d8:	00124c03 */ 	sra	$t1, $s2, 0x10
/*  f0a59dc:	0c002fb5 */ 	jal	viGetViewLeft
/*  f0a59e0:	01209025 */ or $s2, $t1, $zero
/*  f0a59e4:	0002a400 */ 	sll	$s4, $v0, 0x10
/*  f0a59e8:	00146403 */ 	sra	$t4, $s4, 0x10
/*  f0a59ec:	0c002fb9 */ 	jal	viGetViewTop
/*  f0a59f0:	0180a025 */ or $s4, $t4, $zero
/*  f0a59f4:	00028400 */ 	sll	$s0, $v0, 0x10
/*  f0a59f8:	00107403 */ 	sra	$t6, $s0, 0x10
/*  f0a59fc:	0c002f9b */ 	jal	viGetViewHeight
/*  f0a5a00:	01c08025 */ or $s0, $t6, $zero
/*  f0a5a04:	00504021 */ 	addu	$t0, $v0, $s0
/*  f0a5a08:	44888000 */ 	mtc1	$t0, $f16
/*  f0a5a0c:	02925021 */ 	addu	$t2, $s4, $s2
/*  f0a5a10:	448a5000 */ 	mtc1	$t2, $f10
/*  f0a5a14:	468084a0 */ 	cvt.s.w	$f18, $f16
/*  f0a5a18:	3c014080 */ 	lui	$at, 0x4080
/*  f0a5a1c:	44812000 */ 	mtc1	$at, $f4
/*  f0a5a20:	46805420 */ 	cvt.s.w	$f16, $f10
/*  f0a5a24:	46049182 */ 	mul.s	$f6, $f18, $f4
/*  f0a5a28:	44819000 */ 	mtc1	$at, $f18
/*  f0a5a2c:	00000000 */ 	sll	$zero, $zero, 0x0
/*  f0a5a30:	46128102 */ 	mul.s	$f4, $f16, $f18
/*  f0a5a34:	4600320d */ 	trunc.w.s	$f8, $f6
/*  f0a5a38:	4600218d */ 	trunc.w.s	$f6, $f4
/*  f0a5a3c:	440d4000 */ 	mfc1	$t5, $f8
/*  f0a5a40:	440f3000 */ 	mfc1	$t7, $f6
/*  f0a5a44:	31b80fff */ 	andi	$t8, $t5, 0xfff
/*  f0a5a48:	31e90fff */ 	andi	$t1, $t7, 0xfff
/*  f0a5a4c:	00096300 */ 	sll	$t4, $t1, 0xc
/*  f0a5a50:	030c7025 */ or $t6, $t8, $t4
/*  f0a5a54:	ae2e0004 */ 	sw	$t6, 0x4($s1)
/*  f0a5a58:	8fab0148 */ 	lw	$t3, 0x148($sp)
/*  f0a5a5c:	8fa80144 */ 	lw	$t0, 0x144($sp)
/*  f0a5a60:	ad680000 */ 	sw	$t0, 0x0($t3)
/*  f0a5a64:	8fbf0034 */ 	lw	$ra, 0x34($sp)
.NB0f0a5a68:
/*  f0a5a68:	8fb0001c */ 	lw	$s0, 0x1c($sp)
/*  f0a5a6c:	8fb10020 */ 	lw	$s1, 0x20($sp)
/*  f0a5a70:	8fb20024 */ 	lw	$s2, 0x24($sp)
/*  f0a5a74:	8fb30028 */ 	lw	$s3, 0x28($sp)
/*  f0a5a78:	8fb4002c */ 	lw	$s4, 0x2c($sp)
/*  f0a5a7c:	8fb50030 */ 	lw	$s5, 0x30($sp)
/*  f0a5a80:	03e00008 */ 	jr	$ra
/*  f0a5a84:	27bd0148 */ 	addiu	$sp, $sp, 0x148
);
#endif
#else
// VR: true when the off-hand draws no gun of its own: not dual wielding, or the left gun of the
// pair is parked (hidden while the right gun reloads, holster core rule 9). The off-hand copy of
// the gun (the held clip) and the fists then draw for it as for a single gun.
static bool vrOffHandHasNoGun(void)
{
    return !g_Vars.currentplayer->gunctrl.dualwielding || vrHandParked(HAND_LEFT);
}

// VR holsters: a throwable holds one per clip, so the right hand's reload after a throw only
// equips the next one: the off hand stays free (the fists draw it, in their idle pose)
static bool vrThrowableReequip(struct player *player)
{
    return vrHolsterActive() && player->hands[HAND_RIGHT].state == HANDSTATE_RELOAD
        && weaponHasFlag(player->gunctrl.weaponnum, WEAPONFLAG_THROWABLE);
}

// VR: whether the off-hand copy is drawn in bgunRender, and the models line's copy field. This tests
// the loaded copy model (g_VrCopyWepModeldef), not the weapon: the copy must have hands. The copy
// draw keeps the left hand's matrices 17 to 32 (LeftHandMtx, vrLeftHandAndMagMtx), turns 17 and 18
// (vr_wrist_rot) and draws the hands model with the copy's matrix array, so a copy without them has
// no left hand to draw. weaponnum only keeps out the ECM mine and, with holsters off, the weapons
// past the PD run (see below). This replaces a test of weaponnum < 36 (WEAPON_PP9I: unarmed to the
// combat boost, the run of PD weapons at the start of g_Weapons): every copy below 36 has hands (33
// to 50 matrices, ntsc-final ROM), and above it the KL01313, KF7 Special, ZZT, DMC, AR53 and RC-P45
// (3 to 5 matrices) have none and stay out. Of those with the bones above 36:
// - the Psychosis Gun (39: the Tranquilizer's model) has a reload row, so it always comes in: its
//   free hand and held vial are drawn and the guide measures from the vial, as the Tranquilizer's;
// - the PP9i and CC13 (38), the second rocket launcher (38) and the data uplink (35), which have
//   hands, and the test gun (50), which has the bones but no hands model (no WEAPONFLAG_HASHANDS),
//   come in only with holsters on, where the fists draw their free off hand (as C6c gave the ECM
//   mine); with holsters off they play as before: the uplink's and the second rocket launcher's
//   own left hand stays drawn, so the copy's would make two, and the PP9i, CC13 (their left hand
//   hidden by gunviscmds_classicpistol) and the test gun drew no off hand.
// The gadgets and items drawn as props (1 matrix, no parts) and the IR scanner (3, no parts) get no
// copy (vrCopyWepLoad). The ECM mine (35) stays out as before: the holsters give it the fists only
// (vrOffHandFistsOnly). G5 Building's Detonator (the remote mine, 37) stays out: it is drawn alone,
// as the held devices, with nothing in the free hand.
static bool vrOffHandCopyDrawn(s32 weaponnum)
{
    return vrModelHasHands(g_VrCopyWepModeldef) && weaponnum != WEAPON_ECMMINE
        && !vrHolsterIsG5Detonator(weaponnum)
        && (weaponnum < WEAPON_PP9I || vrReloadGetDef(weaponnum, -1) != NULL || vrHolsterActive());
}

// VR holsters: the ECM mine has no copy draw (vrOffHandCopyDrawn), but its free off hand gets the
// fists as the other throwables'
static bool vrOffHandFistsOnly(s32 weaponnum)
{
    return vrHolsterActive() && weaponnum == WEAPON_ECMMINE;
}

// VR: the weapons whose free off hand gets the off-hand pose and the fists in bgunRender (the
// copy draw itself is vrOffHandCopyDrawn only), and the models line's fists field
static bool vrOffHandPoseWeapon(s32 weaponnum)
{
    return vrOffHandCopyDrawn(weaponnum) || vrOffHandFistsOnly(weaponnum);
}

// Is the off-hand genuinely FREE, i.e. safe to replace with the fists model? Returns false whenever
// the legacy copy hand is doing real work -- physically on the gun (reload / two-hand grip),
// mid weapon-switch, or on weapons whose left hand has a real role. Note it deliberately does NOT
// test the bare left grip: gating on it would pop the fists out every time the player squeezed it.
static bool vrOffHandIsFree(struct player *player)
{
    struct hand *rhand = &player->hands[HAND_RIGHT];
    struct hand *lhand = &player->hands[HAND_LEFT];
    s32 wep = player->gunctrl.weaponnum;

    // the left gun of a pair, unless parked
    if (!vrOffHandHasNoGun()) return false;

    if (vrReloadOwnsGun(HAND_RIGHT)) {
        // Manual reload drives the gun, so the old reload conditions do not apply: the off-hand
        // is busy only while it holds a clip. The gates below still apply.
        if (vrReloadHandHoldsClip(HAND_RIGHT)) return false;
        // ... or slaps the clip (a row with rackslap): the copy model draws it
        if (vrReloadSlapDrawn(HAND_RIGHT, NULL)) return false;
        // ... or racks the slide (the copy model draws it on the slide):
        if (vrReloadRackDrawn(HAND_RIGHT, NULL)) return false;
        // ... or the engine reloads a function whose ammo has no row (none today; manual reload
        // never reloads its own ammo so), whose left hand the gun model draws
        if (rhand->state == HANDSTATE_RELOAD) return false;
    } else {
        // off-hand is on the gun
        if (rhand->state == HANDSTATE_RELOAD && !vrThrowableReequip(player)) return false;
        if (lhand->state == HANDSTATE_RELOAD) return false;
    }
    if (VrTwoHandGrip && VrTwoHandsGun(wep)) return false;

    // mid weapon switch: the legacy path runs the lower/raise pitch dip
    if (rhand->state == HANDSTATE_CHANGEGUN || rhand->state == HANDSTATE_AUTOSWITCH) return false;
    // (this also covers the left hand's lower: its minor means LOWER only in the CHANGEGUN state,
    // and a parked gun's attack uses the same minor number)
    if (lhand->state == HANDSTATE_CHANGEGUN || lhand->state == HANDSTATE_AUTOSWITCH) return false;

    // weapons whose off-hand legitimately isn't free
    if (wep == WEAPON_UNARMED || wep == WEAPON_NONE) return false;  // already real fists on both hands
    if (wep == WEAPON_REMOTEMINE && !vrHolsterActive()) return false; // left hand holds the detonator
    if (vrLaserOnOffHand(wep)) return false; // worn on the off hand (holsters off)

    // asset / load-state safety
    if (player->gunctrl.handmodeldef == NULL) return false;
    if (!bgunIsLoaded()) return false;

    return true;
}

// VR: the render-time half of the holster models line: vrHolsterNoteFrame asks for it on the frame it
// logs that line, and bgunRender logs each hand just before its gun model is drawn (or that it is
// not drawn): the root's translation and handedness (mirrored when the determinant is negative),
// every matrix's distance from the root, the matrices carrying the hide signal (m[0][3] > 1.5) and
// the collapsed ones (scale under 0.001), then each toggle (T) and distance (D) node's rwdata index
// and visible state, in tree order
static bool g_VrHandRenderDump = false;

void bgunRequestHandRenderDump(void)
{
    g_VrHandRenderDump = true;
}

static void bgunDumpHandRender(s32 h, struct hand *hand)
{
    char buf[1400];
    s32 n, i, len = sizeof(buf);
    struct modeldef *def = hand->gunmodel.definition;
    Mtxf *m = hand->gunmodel.matrices;
    struct modelnode *node;

    if (!hand->visible || def == NULL || m == NULL) {
        snprintf(buf, len, "%c not drawn: use%d vis%d", h == HAND_RIGHT ? 'R' : 'L', hand->inuse, hand->visible);
        vrHolsterDiagText(buf);
        return;
    }

    n = snprintf(buf, len, "%c n%d root(%.1f %.1f %.1f) det%.4f dist[", h == HAND_RIGHT ? 'R' : 'L',
            def->nummatrices, m[0].m[3][0], m[0].m[3][1], m[0].m[3][2],
            m[0].m[0][0] * (m[0].m[1][1] * m[0].m[2][2] - m[0].m[1][2] * m[0].m[2][1])
            - m[0].m[0][1] * (m[0].m[1][0] * m[0].m[2][2] - m[0].m[1][2] * m[0].m[2][0])
            + m[0].m[0][2] * (m[0].m[1][0] * m[0].m[2][1] - m[0].m[1][1] * m[0].m[2][0]));

    for (i = 0; i < def->nummatrices && n > 0 && n < len; i++) {
        f32 dx = m[i].m[3][0] - m[0].m[3][0], dy = m[i].m[3][1] - m[0].m[3][1], dz = m[i].m[3][2] - m[0].m[3][2];

        n += snprintf(buf + n, len - n, i ? " %.0f" : "%.0f", sqrtf(dx * dx + dy * dy + dz * dz));
    }

    if (n > 0 && n < len) {
        n += snprintf(buf + n, len - n, "] hid[");
    }

    for (i = 0; i < def->nummatrices && n > 0 && n < len; i++) {
        if (m[i].m[0][3] > 1.5f) {
            n += snprintf(buf + n, len - n, " %d", i);
        }
    }

    if (n > 0 && n < len) {
        n += snprintf(buf + n, len - n, " ] col[");
    }

    for (i = 0; i < def->nummatrices && n > 0 && n < len; i++) {
        f32 sc = sqrtf(m[i].m[0][0] * m[i].m[0][0] + m[i].m[0][1] * m[i].m[0][1] + m[i].m[0][2] * m[i].m[0][2]);

        if (sc < 0.001f) {
            n += snprintf(buf + n, len - n, " %d", i);
        }
    }

    if (n > 0 && n < len) {
        n += snprintf(buf + n, len - n, " ] nodes");
    }

    node = def->rootnode;

    while (node && n > 0 && n < len) {
        u32 type = node->type & 0xff;

        if (type == MODELNODETYPE_TOGGLE || type == MODELNODETYPE_DISTANCE) {
            union modelrwdata *rw = modelGetNodeRwData(&hand->gunmodel, node);

            n += snprintf(buf + n, len - n, " %c%d:%d", type == MODELNODETYPE_TOGGLE ? 'T' : 'D',
                    type == MODELNODETYPE_TOGGLE ? node->rodata->toggle.rwdataindex : node->rodata->distance.rwdataindex,
                    rw == NULL ? -1 : type == MODELNODETYPE_TOGGLE ? rw->toggle.visible : rw->distance.visible);
        }

        if (node->child) {
            node = node->child;
        } else {
            while (node) {
                if (node->next) {
                    node = node->next;
                    break;
                }

                node = node->parent;
            }
        }
    }

    vrHolsterDiagText(buf);
}

// VR: what bgunRender draws per hand, for the PD-VRR line a few frames after a holster switch or a
// park (vrHolsterNoteFrame): the weapon, dual wield, the hands model (handfilenum) and per hand its
// use, visibility (set by bgun0f0a5550 this frame), whether its gun is parked, state/stateminor,
// mode, anim mode and number, and whether its model is the loaded gun's; then whether the off-hand
// weapon copy and the VR fists draw, and the idle pose cache state (unk0dd4)
void bgunDescribeHandModels(char *buf, s32 len)
{
    struct player *player = g_Vars.currentplayer;
    struct gunctrl *ctrl = &player->gunctrl;
    s32 copy = vrOffHandHasNoGun() && vrOffHandCopyDrawn(ctrl->weaponnum)
        && ctrl->weaponnum != WEAPON_LASER;
    // The fists as bgunRender decides them: the Laser's off hand is free with holsters (Task F11h),
    // which vrOffHandIsFree reads below
    s32 fistpose = g_VrCopyWepModeldef != NULL && vrOffHandHasNoGun() && vrOffHandPoseWeapon(ctrl->weaponnum);
    s32 n, h;

    n = snprintf(buf, len, "w%d dual%d hands%d(%d) side%d", ctrl->weaponnum, ctrl->dualwielding,
            ctrl->handmodeldef != NULL, ctrl->handfilenum, VrLeftHandedMode);

    for (h = 0; h < 2 && n > 0 && n < len; h++) {
        struct hand *hand = &player->hands[h];

        n += snprintf(buf + n, len - n,
                " | %c use%d vis%d park%d st%d/%d mode%d am%d anim%d%s gun%d%s%s",
                h == HAND_RIGHT ? 'R' : 'L', hand->inuse, hand->visible, vrHandParked(h),
                hand->state, hand->stateminor, hand->mode, hand->animmode, hand->anim.animnum,
                hand->gunmodel.anim == &hand->anim ? "" : "(not hand's)", hand->gset.weaponnum,
                hand->gunmodel.definition == ctrl->gunmodeldef ? "" : " model-stale",
                ctrl->handmodeldef != NULL && hand->handmodel.definition != ctrl->handmodeldef ? " hands-stale" : "");

        // The two hand bones as drawn this frame (bone 2 the right hand, 18 the left; the length of
        // row 0 is the scale, 0 when collapsed; the root distance in model units) and the gun
        // model's hand part toggles (1 shown, 0 hidden, - none)
        if (hand->visible && hand->gunmodel.matrices != NULL && hand->gunmodel.definition != NULL
            && hand->gunmodel.definition->nummatrices > 18 && n > 0 && n < len) {
            Mtxf *m = hand->gunmodel.matrices;
            struct modelnode *pl = modelGetPart(hand->gunmodel.definition, MODELPART_HAND_LEFT);
            struct modelnode *pr = modelGetPart(hand->gunmodel.definition, MODELPART_HAND_RIGHT);
            f32 s2 = sqrtf(m[2].m[0][0] * m[2].m[0][0] + m[2].m[0][1] * m[2].m[0][1] + m[2].m[0][2] * m[2].m[0][2]);
            f32 s18 = sqrtf(m[18].m[0][0] * m[18].m[0][0] + m[18].m[0][1] * m[18].m[0][1] + m[18].m[0][2] * m[18].m[0][2]);
            f32 dx = m[18].m[3][0] - m[2].m[3][0], dy = m[18].m[3][1] - m[2].m[3][1], dz = m[18].m[3][2] - m[2].m[3][2];

            n += snprintf(buf + n, len - n, " b2 s%.3f h%.0f b18 s%.3f h%.0f d%.1f tL%c tR%c",
                    s2, m[2].m[0][3], s18, m[18].m[0][3], sqrtf(dx * dx + dy * dy + dz * dz),
                    pl ? '0' + (char) hand->unk0a6c[pl->rodata->toggle.rwdataindex] : '-',
                    pr ? '0' + (char) hand->unk0a6c[pr->rodata->toggle.rwdataindex] : '-');
        }
    }

    // The idle pose cache's hand bones (relative to the model root)
    if (player->hands[HAND_RIGHT].unk0dd8 != NULL && player->hands[HAND_RIGHT].unk0dd4 != -1 && n > 0 && n < len) {
        Mtxf *c = player->hands[HAND_RIGHT].unk0dd8;

        n += snprintf(buf + n, len - n, " | cache b2 s%.3f b18 s%.3f (%.0f %.0f %.0f)",
                sqrtf(c[2].m[0][0] * c[2].m[0][0] + c[2].m[0][1] * c[2].m[0][1] + c[2].m[0][2] * c[2].m[0][2]),
                sqrtf(c[18].m[0][0] * c[18].m[0][0] + c[18].m[0][1] * c[18].m[0][1] + c[18].m[0][2] * c[18].m[0][2]),
                c[18].m[3][0], c[18].m[3][1], c[18].m[3][2]);
    }

    if (n > 0 && n < len) {
        snprintf(buf + n, len - n, " | copy%d%s fists%d cache%d", copy,
                g_VrCopyWepModeldef != NULL && g_VrCopyWepModeldef != ctrl->gunmodeldef ? "-stale" : "",
                fistpose && vrOffHandIsFree(player), player->hands[HAND_RIGHT].unk0dd4);
    }
}

// VR holsters: with the off hand free (vrOffHandIsFree: the fists draw it), only the fists draw the
// left hand. The right hand's model (its gun and hands models) and the off-hand copy of the weapon
// then hide every left-hand part before they are drawn: the hands model's left-hand matrices (the
// hide signal, as vrHideGunParts sends it) and the MODELPART_HAND_LEFT toggles of the model and of
// its hands model, restored after the draw. The part-visibility lists hide only the hands model's
// left hand, and only for some weapons; the grenade's and N-bomb's gun models carry both hands in
// their own geometry, on the same bones and with no toggle (checkpoint C run 1: 324 triangles in
// their gun model, against 70 for the timed mine), so their left arm followed the gun hand.
//
// The left-hand matrices come from the hands model's structure (gunctrl.handmodeldef, drawn with
// the gun model's matrices): those of the position nodes under its MODELPART_HAND_LEFT toggle, less
// any the MODELPART_HAND_RIGHT toggle also uses. Without such nodes the matrices its display lists
// hang from are listed for the log only: a display list's bone can carry other geometry, so they
// are not hidden (the toggles still are). Cached per hands model, as the fists are.
#define VR_LEFT_HAND_MAX_MTX 48
static s32 sVrLeftMtx[VR_LEFT_HAND_MAX_MTX];
static s32 sVrLeftMtxCount;
static struct modeldef *sVrLeftMtxDef;
static s32 sVrLeftMtxFile = -1;
static const char *sVrLeftMtxFrom = "none";
static bool sVrLeftMtxHide;     // the list is the position nodes' (hidden), not the display lists'

// The PD-VRR line of the dump frame (bgunRequestHandRenderDump): what each model would draw of the
// left hand before the hide
static char sVrLeftDiag[900];
static s32 sVrLeftDiagLen;

// Old toggle states of a model and its hands model while the hide is on (vrFreeLeftHandRestore)
struct vrlefthide {
    bool *rw[2];
    bool old[2];
};

// Adds to list the matrix of each node of type a or b in the subtree of sub. A toggle's or distance
// node's children are walked whatever its state (node->child is set at each draw).
static void vrLeftHandWalk(struct modelnode *sub, u32 a, u32 b, s32 *list, s32 *count)
{
    struct modelnode *node = sub;

    while (node) {
        u32 type = node->type & 0xff;
        struct modelnode *child = node->child;

        if (type == a || type == b) {
            s32 idx = modelFindNodeMtxIndex(node, 0);
            s32 k;

            for (k = 0; k < *count && list[k] != idx; k++) {
            }

            // Never the root (0)
            if (idx > 0 && k == *count && *count < VR_LEFT_HAND_MAX_MTX) {
                list[(*count)++] = idx;
            }
        }

        if (type == MODELNODETYPE_TOGGLE) {
            child = node->rodata->toggle.target;
        } else if (type == MODELNODETYPE_DISTANCE) {
            child = node->rodata->distance.target;
        }

        if (child) {
            node = child;
        } else {
            while (node != NULL && node != sub && node->next == NULL) {
                node = node->parent;
            }

            node = node == NULL || node == sub ? NULL : node->next;
        }
    }
}

// The same for the children of the hands model's toggle part
static void vrLeftHandWalkPart(struct modeldef *md, s32 part, u32 a, u32 b, s32 *list, s32 *count)
{
    struct modelnode *toggle = modelGetPart(md, part);
    struct modelnode *sub;

    if (toggle == NULL || (toggle->type & 0xff) != MODELNODETYPE_TOGGLE) {
        return;
    }

    for (sub = toggle->rodata->toggle.target; sub != NULL; sub = sub->parent == toggle ? sub->next : NULL) {
        vrLeftHandWalk(sub, a, b, list, count);
    }
}

static void vrLeftHandEnsureBuilt(void)
{
    struct gunctrl *ctrl = &g_Vars.currentplayer->gunctrl;
    struct modeldef *md = ctrl->handmodeldef;
    s32 right[VR_LEFT_HAND_MAX_MTX];
    s32 nright = 0;
    s32 i, k, n;

    if (md == sVrLeftMtxDef && (s32) ctrl->handfilenum == sVrLeftMtxFile) {
        return;
    }

    sVrLeftMtxDef = md;
    sVrLeftMtxFile = (s32) ctrl->handfilenum;
    sVrLeftMtxCount = 0;
    sVrLeftMtxFrom = "none";
    sVrLeftMtxHide = false;

    if (md == NULL || md->rootnode == NULL) {
        return;
    }

    vrLeftHandWalkPart(md, MODELPART_HAND_LEFT, MODELNODETYPE_POSITION, MODELNODETYPE_POSITIONHELD,
            sVrLeftMtx, &sVrLeftMtxCount);
    sVrLeftMtxFrom = "position nodes";
    sVrLeftMtxHide = true;

    if (sVrLeftMtxCount == 0) {
        vrLeftHandWalkPart(md, MODELPART_HAND_LEFT, MODELNODETYPE_GUNDL, MODELNODETYPE_DL, sVrLeftMtx,
                &sVrLeftMtxCount);
        sVrLeftMtxFrom = "display lists (not hidden)";
        sVrLeftMtxHide = false;
    }

    vrLeftHandWalkPart(md, MODELPART_HAND_RIGHT, MODELNODETYPE_POSITION, MODELNODETYPE_POSITIONHELD, right, &nright);
    vrLeftHandWalkPart(md, MODELPART_HAND_RIGHT, MODELNODETYPE_GUNDL, MODELNODETYPE_DL, right, &nright);

    for (i = 0, n = 0; i < sVrLeftMtxCount; i++) {
        for (k = 0; k < nright && right[k] != sVrLeftMtx[i]; k++) {
        }

        if (k == nright) {
            sVrLeftMtx[n++] = sVrLeftMtx[i];
        }
    }

    sVrLeftMtxCount = n;

    if (n == 0) {
        sVrLeftMtxFrom = "none";
    }
}

// The visible flag of a model's MODELPART_HAND_LEFT toggle, NULL when it has none
static bool *vrLeftToggleRw(struct model *model)
{
    struct modelnode *node;
    union modelrwdata *rw;

    if (model == NULL || model->definition == NULL || model->rwdatas == NULL) {
        return NULL;
    }

    node = modelGetPart(model->definition, MODELPART_HAND_LEFT);

    if (node == NULL || (node->type & 0xff) != MODELNODETYPE_TOGGLE) {
        return NULL;
    }

    rw = modelGetNodeRwData(model, node);

    return rw ? &rw->toggle.visible : NULL;
}

static void vrLeftDiagCat(const char *text)
{
    s32 len = sizeof(sVrLeftDiag);

    if (sVrLeftDiagLen >= 0 && sVrLeftDiagLen < len) {
        s32 w = snprintf(sVrLeftDiag + sVrLeftDiagLen, len - sVrLeftDiagLen, "%s", text);

        sVrLeftDiagLen = w < 0 ? len : sVrLeftDiagLen + w;
    }
}

// Adds a model to the dump line: its left toggle and its hands model's (1 shown, 0 hidden, - none)
// and the matrices of list (count of them) it would draw (no hide signal, not collapsed), before
// the hide
static void vrFreeLeftHandNote(const char *label, struct model *gun, struct model *hands, Mtxf *m, s32 n,
        bool hide, const s32 *list, s32 count)
{
    char num[48];
    bool *gl = vrLeftToggleRw(gun);
    bool *hl = vrLeftToggleRw(hands);
    s32 k;

    if (sVrLeftDiagLen == 0) {
        snprintf(num, sizeof(num), "left hand: w%d hands %d left mtx[", g_Vars.currentplayer->gunctrl.weaponnum,
                (s32) g_Vars.currentplayer->gunctrl.handfilenum);
        vrLeftDiagCat(num);

        for (k = 0; k < sVrLeftMtxCount; k++) {
            snprintf(num, sizeof(num), k ? " %d" : "%d", sVrLeftMtx[k]);
            vrLeftDiagCat(num);
        }

        vrLeftDiagCat("] from ");
        vrLeftDiagCat(sVrLeftMtxFrom);
    }

    snprintf(num, sizeof(num), " | %s n%d gun tL%c hands tL%c drew[", label, n,
            gl ? (*gl ? '1' : '0') : '-', hl ? (*hl ? '1' : '0') : '-');
    vrLeftDiagCat(num);

    for (k = 0; k < count; k++) {
        s32 idx = list[k];

        if (idx < n && m[idx].m[0][3] <= 1.5f
                && m[idx].m[0][0] * m[idx].m[0][0] + m[idx].m[0][1] * m[idx].m[0][1]
                + m[idx].m[0][2] * m[idx].m[0][2] >= 0.001f * 0.001f) {
            snprintf(num, sizeof(num), " %d", idx);
            vrLeftDiagCat(num);
        }
    }

    vrLeftDiagCat(hide ? " ] -> hidden" : " ] kept");
}

// The hide for one model before its draw (gun with matrices m, the model of weapon weaponnum, hands
// its hands model or NULL)
static void vrFreeLeftHandHide(const char *label, bool note, struct model *gun, struct model *hands, Mtxf *m,
        s32 weaponnum, struct vrlefthide *saved)
{
    struct model *models[2] = { gun, hands };
    s32 n = gun != NULL && gun->definition != NULL ? gun->definition->nummatrices : 0;
    // The Laser's arm is the model's left arm (its hands file keeps only that one): with holsters it
    // is the gun hand's arm, mirrored onto the right hand (Task F11h), so it stays (Task F12c, the
    // user: "the hand and arm that were under it are gone")
    bool hide = vrHolsterActive() && vrOffHandIsFree(g_Vars.currentplayer) && weaponnum != WEAPON_LASER;
    s32 j, k;

    saved->rw[0] = saved->rw[1] = NULL;

    if (m == NULL || n <= 0) {
        return;
    }

    vrLeftHandEnsureBuilt();

    if (g_VrHandRenderDump && note && vrHolsterActive()) {
        vrFreeLeftHandNote(label, gun, hands, m, n, hide, sVrLeftMtx, sVrLeftMtxCount);
    }

    if (!hide) {
        return;
    }

    for (k = 0; k < sVrLeftMtxCount && sVrLeftMtxHide; k++) {
        if (sVrLeftMtx[k] < n) {
            m[sVrLeftMtx[k]].m[0][3] = 2.0f;
        }
    }

    for (j = 0; j < 2; j++) {
        bool *rw = vrLeftToggleRw(models[j]);

        if (rw != NULL) {
            saved->rw[j] = rw;
            saved->old[j] = *rw;
            *rw = false;
        }
    }
}

static void vrFreeLeftHandRestore(struct vrlefthide *saved)
{
    s32 j;

    for (j = 0; j < 2; j++) {
        if (saved->rw[j] != NULL) {
            *saved->rw[j] = saved->old[j];
        }
    }
}

// VR: the copy-weapon model posed at a frame of the gun's reload animation (animid, VR_ANIM_ID; -1:
// none, the model as it is) with renderdata's root: the clip-hold pose and the slap's frames. The
// anim is dropped once the pose is built, so the branches that pose the model otherwise never reuse
// it.
static void vrCopyWepPoseAt(struct modelrenderdata *renderdata, int animid, f32 frame)
{
    renderdata->unk10 = g_VrCopyWepModel.matrices;
    if (animid >= 0) {
        if (!g_VrClipHoldAnimReady) {
            animInit(&g_VrClipHoldAnim);
            g_VrClipHoldAnimReady = true;
        }
        g_VrCopyWepModel.anim = &g_VrClipHoldAnim;
        modelSetAnimation(&g_VrCopyWepModel, animid, false, frame, 0, 0.0f);
    } else {
        g_VrCopyWepModel.anim = NULL;
    }

    modelUpdateRelations(&g_VrCopyWepModel);
    var8005efd8_2 = true;
    var8005efb0_2 = true;
    modelSetMatricesWithAnim(renderdata, &g_VrCopyWepModel);
    var8005efd8_2 = false;
    var8005efb0_2 = false;
    g_VrCopyWepModel.anim = NULL;
}

// Mismatch: Goal uses different codegen for accessing vertices
void bgunRender(Gfx * *gdlptr)
{
    Gfx* gdl = *gdlptr;
    struct modelrenderdata renderdata = { NULL, true, 3 }; // 10c
    struct player* player;
    s32 i;

    static bool renderhand = true; // var800702dc

    player = g_Vars.currentplayer;


    if (player->visionmode == VISIONMODE_XRAY) {
        for (i = 0; i < 2; i++) {
            if (g_Vars.currentplayer->hands[i].firedrocket) {
                g_Vars.currentplayer->hands[i].rocket = NULL;
            }
        }
        return;
    }

    // VR: the PD-VRR line of a holster switch ends on the first frame drawing the new weapon
    vrHolsterNoteFrame();

#ifdef PLATFORM_N64 // TODO
    gdl = zbufSaveArtifactDepths(gdl);
#endif
    gdl = viPrepareZbuf(gdl);
    gdl = vi0000b1d0(gdl);

    gDPSetScissor(gdl++, G_SC_NON_INTERLACE, viGetViewLeft(), viGetViewTop(),
                  viGetViewLeft() + viGetViewWidth(), viGetViewTop() + viGetViewHeight());

    gdl = vi0000aca4(gdl, 1.5, 1000);

    if (g_Vars.currentplayer->teleportstate != TELEPORTSTATE_INACTIVE) {
        f32 f2;

#ifdef PLATFORM_N64
        if (optionsGetScreenRatio() == SCREENRATIO_16_9) {
						f2 = player0f0bd358() * 1.3333334f;
				}
				else {
						f2 = player0f0bd358();
				}
#else
        f2 = player0f0bd358();
#endif

        gdl = vi0000b0e8(gdl, 60, f2);
    }

    if (PLAYERCOUNT() == 1 && IS8MB()) {
        gdl = lasersightRenderBeam(gdl);
    }

    for (i = 0; i < 2; i++) {
        struct hand *hand;
        s32 j;
        s32 alpha;
        s32 weaponnum; // ec
        struct modelnode *node; // e8
        u32 colour; // e4

        hand = player->hands + i;

        weaponnum = bgunGetWeaponNum2(i);

        if (g_VrHandRenderDump && !hand->visible) {
            bgunDumpHandRender(i, hand);
        }

        if (hand->visible) {
            gdl = beamRender(gdl, &hand->beam, 0, 0);


            if (weaponHasFlag(hand->gset.weaponnum, WEAPONFLAG_00008000)) {
                gSPSetLights1(gdl++, var80070090);
                gSPLookAt(gdl++, camGetLookAt());
            }

            gSPPerspNormalize(gdl++, mtx00016dcc(0, 300));

            // There is support for guns having a TV screen on them
            // but no guns have this model part so it's not used.
            node = modelGetPart(hand->gunmodel.definition, MODELPART_0010);

            if (node) {
                union modelrwdata *rwdata = modelGetNodeRwData(&hand->gunmodel, modelGetPart(
                        hand->gunmodel.definition, MODELPART_0011));

                if (rwdata) {
                    rwdata->toggle.visible = true;
                }

                gdl = tvscreenRender(&hand->gunmodel, node, &var8009cf88, gdl, 0, 1);
            }


            renderdata.gdl = gdl;
            renderdata.unk30 = 4;


            if (USINGDEVICE(DEVICE_NIGHTVISION) || USINGDEVICE(DEVICE_IRSCANNER)) {
                // 67c
                u8 *col = player->gunshadecol;
                u32 shade;
                s32 spb0[4];
                s32 spa0[4];

                if (col[0] > col[1] && col[0] > col[2]) {
                    shade = col[0];
                } else if (col[1] > col[2]) {
                    shade = col[1];
                } else {
                    shade = col[2];
                }

                renderdata.envcolour = (shade << 24 | shade << 16 | shade << 8) + col[3];

                if (USINGDEVICE(DEVICE_NIGHTVISION)) {
                    spb0[0] = var8009caef;
                    spb0[1] = var8009caef;
                    spb0[2] = var8009caef;
                    spb0[3] = var8009caf0;

                    colour = (spb0[0] << 24 | spb0[1] << 16 | spb0[2] << 8) + spb0[3];
                } else if (USINGDEVICE(DEVICE_IRSCANNER)) {
                    spa0[0] = 0xff;
                    spa0[1] = 0;
                    spa0[2] = 0;
                    spa0[3] = 0x80;

                    colour = (spa0[0] << 24 | spa0[1] << 16 | spa0[2] << 8) + spa0[3];
                }

                if (weaponnum == WEAPON_UNARMED) {
                    renderdata.envcolour = colour;
                }
            } else {
                renderdata.envcolour = player->gunshadecol[0] << 24 | player->gunshadecol[1] << 16 |
                                       player->gunshadecol[2] << 8 | player->gunshadecol[3];
                colour = renderdata.envcolour;

                // 838
                if (hand->gset.weaponnum == WEAPON_MAULER) {
                    u32 weight = hand->matmot1 * 50.0f;
                    renderdata.envcolour = colourBlend(0xff00007f, renderdata.envcolour, weight);
                }
            }

            // Apply transparency based on player's cloak
            alpha = chrGetCloakAlpha(player->prop->chr);

            if (alpha < 255) {
                colour = (s32) (alpha * 0.74509805f) + 0x41;
                renderdata.unk30 = 5;
                renderdata.fogcolour = renderdata.envcolour;
                renderdata.envcolour = colour;
            }

            renderdata.zbufferenabled = true;

            mtx00016760();

            // Render rocket launcher's rocket if it's in Jo's hand or in the launcher
            if (hand->rocket) {
                struct model *rocketmodel = hand->rocket->base.model; // 98
                bool sp94 = false;

#if VERSION >= VERSION_NTSC_1_0
                if (rocketmodel && rocketmodel->definition) {
                    sp94 = true;

                    modelRender(&renderdata, rocketmodel);

                    mtxF2LBulk(rocketmodel->matrices, rocketmodel->definition->nummatrices);

                    if (hand->firedrocket) {
                        hand->rocket = NULL;
                    }
                }

                if (sp94);
#else
                modelRender(&renderdata, rocketmodel);

                                mtxF2LBulk(rocketmodel->matrices, rocketmodel->definition->nummatrices);

                                if (hand->firedrocket) {
                                        hand->rocket = NULL;
                                }
#endif
            }


            // VR
            gSPClearGeometryMode(renderdata.gdl++, G_CULL_BOTH);
            renderdata.cullmode = CULLMODE_NONE;


            // Slide the laser's liquid texture
            if (PLAYERCOUNT() == 1) {
                node = modelGetPart(hand->gunmodel.definition, MODELPART_GUN_LASERLIQUID);

                // a5c
                if (node) {
                    struct modelrodata_gundl *rodata;
                    rodata = &node->rodata->gundl;

                    for (j = 0; j < rodata->numvertices; j++) {
                        // a7c
                        s32 stack[2];
                        s32 k;

                        (rodata->vertices + j)->t -= g_Vars.lvupdate240 * PALUP(25);

                        if ((rodata->vertices + j)->t < -0x6000) {
                            for (k = 0; k < rodata->numvertices; k++) {
                                (rodata->vertices + k)->t += 0x2000;
                            }
                        }
                    }
                }
            }



            // VR holsters: with the off hand free, the right hand's model draws no left hand
            struct vrlefthide vrleftsaved = { { NULL, NULL } };

            if (i == HAND_RIGHT) {
                vrFreeLeftHandHide("R", true, &hand->gunmodel,
                        player->gunctrl.handmodeldef != NULL ? &hand->handmodel : NULL, hand->gunmodel.matrices,
                        hand->gset.weaponnum, &vrleftsaved);
            }

            // Render the gun
            if (g_VrHandRenderDump) {
                bgunDumpHandRender(i, hand);
                gDPNoOpTag(renderdata.gdl++, VR_HANDTRIS_BEGIN + i);
            }

            modelRender(&renderdata, &hand->gunmodel);

            if (g_VrHandRenderDump) {
                gDPNoOpTag(renderdata.gdl++, VR_HANDTRIS_END);
            }

            // Render the hand
            if (player->gunctrl.handmodeldef && renderhand) {
                s32 prevcolour = renderdata.envcolour; // 7c

                hand->handmodel.matrices = hand->gunmodel.matrices;

                modelUpdateRelations(&hand->handmodel);

                renderdata.envcolour = colour;

                modelRender(&renderdata, &hand->handmodel);

                renderdata.envcolour = prevcolour;
            }

            vrFreeLeftHandRestore(&vrleftsaved);


            // Clean up
            gdl = renderdata.gdl;

            if (weaponHasFlag(weaponnum, WEAPONFLAG_DUALFLIP) ||
                weaponnum == WEAPON_UNARMED) { // VR mirror flip face
                gSPClearGeometryMode(gdl++, G_CULL_BOTH);
            }

            mtxF2LBulk(hand->gunmodel.matrices, hand->gunmodel.definition->nummatrices);
            mtx00016784();

            gSPPerspNormalize(gdl++, viGetPerspScale());

        }



        // VR: Display left hand and gun part for reload
        struct coord vr_sp274 = {0, 0, 0};
        struct coord vr_sp1a4, vr_sp118;
        struct hand *rhand = &player->hands[HAND_RIGHT];
        struct hand *lhand = &player->hands[HAND_LEFT];

        bool OnlyWeapons = vrOffHandCopyDrawn(g_Vars.currentplayer->gunctrl.weaponnum);
        // The off-hand pose and the fists, without the copy for the ECM mine (vrOffHandFistsOnly).
        // The ECM mine still needs g_VrCopyWepModeldef though its copy is never drawn: this block
        // and the fists below hang on it, so the off hand is not drawn if the copy failed to load.
        bool OffHandPose = vrOffHandPoseWeapon(g_Vars.currentplayer->gunctrl.weaponnum);

        if (OffHandPose && g_VrCopyWepModeldef != NULL
            && vrOffHandHasNoGun()) {

            lhand->useposrot = false;
            vr_gun_pos_rot(HAND_LEFT, lhand); // TEST

            // Right-hand based transition animation ---
            f32 pitchAngle = 0.0f;
            s32 delayLower = g_Vars.normmplayerisrunning ? TICKS(12) : TICKS(16);
            s32 delayRaise = g_Vars.normmplayerisrunning ? TICKS(12) : TICKS(23);
            // Holster switch: bgunTickIncChangeGun lowers and raises at once (a throw can hold the
            // lower), and so does the auto switch's lower (bgunTickIncAutoSwitch), so both branches
            // use these (the CHANGEGUN branch's AUTOSWITCH_UNEQUIP clause also matches
            // CHANGEGUN_UNEQUIP: both are 0).
            s32 delayChangeLower = vrHolsterFastSwitch() ? 1 : delayLower;
            s32 delayChangeRaise = vrHolsterFastSwitch() ? 1 : delayRaise;

            if (rhand->state == HANDSTATE_CHANGEGUN) {
                if (rhand->stateminor == HANDSTATEMINOR_CHANGEGUN_LOWER || rhand->stateminor == HANDSTATEMINOR_AUTOSWITCH_UNEQUIP) {
                    pitchAngle = (f32)rhand->stateframes * MAX_PITCH / (f32)delayChangeLower;

                } else if (rhand->stateminor == HANDSTATEMINOR_CHANGEGUN_RAISE || rhand->stateminor == HANDSTATEMINOR_CHANGEGUN_EQUIP) {
                    pitchAngle = (f32)(delayChangeRaise - rhand->count60) * MAX_PITCH / (f32)delayChangeRaise;
                }
            } else if (rhand->state == HANDSTATE_AUTOSWITCH) {
                if (rhand->stateminor == HANDSTATEMINOR_AUTOSWITCH_UNEQUIP) {
                    pitchAngle = (f32)rhand->stateframes * MAX_PITCH / (f32)delayChangeLower;
                }
            }
            // Apply pitch, move the copy hand up/down
            if (pitchAngle > 0.0f) {
                if (pitchAngle > MAX_PITCH) pitchAngle = MAX_PITCH;
                bgunSetArmPitch(lhand, pitchAngle);
            }


            vr_sp274.y -= bgunGetFovOffsetY();
            vr_sp274.z += bgunGetFovOffsetZ();

            // Off-hand is "free" (not reloading) -> replicate the good/unarmed pose (bgun0f0a5550):
            // add the damplook step and freeze the auto-aim. During reload keep the copy-hand behavior.
            bool offHandIdle = (rhand->state != HANDSTATE_RELOAD) || vrThrowableReequip(player);

            mtx4LoadIdentity(&vr_sp234);



            if (lhand->useposrot) {
                vr_sp274.f[0] += lhand->posrotmtx.m[3][0];
                vr_sp274.f[1] += lhand->posrotmtx.m[3][1];
                vr_sp274.f[2] += lhand->posrotmtx.m[3][2];
                mtx00015be0(&lhand->posrotmtx, &vr_sp234);
                vr_sp234.m[3][0] = 0.0f;
                vr_sp234.m[3][1] = 0.0f;
                vr_sp234.m[3][2] = 0.0f;


                // DAMPLOOK step -- the piece the good/unarmed path (bgun0f0a5550) has that this path
                // lacks. Guard against a zero damplook vector (mtx00016d58 would normalize 0 -> NaN).
                float dl2 = lhand->damplook.x*lhand->damplook.x
                            + lhand->damplook.y*lhand->damplook.y
                            + lhand->damplook.z*lhand->damplook.z;
                if (offHandIdle && dl2 > 1e-6f) {
                    Mtxf dampMtx;
                    mtx00016d58(&dampMtx, 0.0f, 0.0f, 0.0f,
                                lhand->damplook.x, lhand->damplook.y, lhand->damplook.z,
                                lhand->dampup.x, lhand->dampup.y, lhand->dampup.z);
                    mtx00015be0(&dampMtx, &vr_sp234);
                }
            }
            else {
                lhand->rotxoffset = 0.0f;
                lhand->posoffset.x = 0.0f;
                lhand->posoffset.y = 0.0f;
                lhand->posoffset.z = 0.0f;
            }

            vr_sp1a4.x = 0.0f;
            vr_sp1a4.y = M_PI;
            vr_sp1a4.z = 0.0f;
            mtx4LoadRotation(&vr_sp1a4, &vr_sp164);
            vr_sp1a4.y = 0.0f;

            bgun0f0a24f0(&vr_sp118, HAND_LEFT);
            vr_sp1a4.y = -bgun0f0a2498(vr_sp118.x, vr_sp118.z, vr_sp274.f[0], vr_sp274.f[2]);
            vr_sp1a4.x = bgun0f0a2498(vr_sp118.y, vr_sp118.z, vr_sp274.f[1], vr_sp274.f[2]);

            // Freeze the auto-aim when idle (matches the good/unarmed path).
            if (offHandIdle) {
                vr_sp1a4.x = 0.0f;
                vr_sp1a4.y = 0.0f;
            }

            mtx4LoadRotation(&vr_sp1a4, &vr_sp124);
            mtx4MultMtx4(&vr_sp124, &vr_sp164, &vr_sp284);
            mtx4MultMtx4InPlace(&vr_sp284, &vr_sp234);
            mtx4Copy(&vr_sp234, &vr_sp2c4);
            mtx4SetTranslation(&vr_sp274, &vr_sp2c4);


            // FREE LEFT HAND WORLD-LOCK. This path builds its own vr_sp2c4 rather than sharing
            // bgun0f0a5550's, so it needs its own copy of the same grip-offset placement -- omitting
            // it here is what left the off-hand misaligned while unarmed (which uses the main path)
            // looked correct. See the matching block in bgun0f0a5550 for the derivation.
            //
            // The free hand sits at the default grip offset (vr_gun_pos_rot's default base plus the
            // per-player trim) whatever the gun (Task F7a): the gun's own base (the rifles', the
            // Callisto's, the Dragons') put the fists, their wrist capture (vrReloadNoteFistWrist) and
            // every ammo hand aligned to it 8 to 13 cm off the controller, a different place per gun,
            // and the slap and rack tests' fixed grab point (VRR_HAND_GRAB_X, measured on the Falcon,
            // which has the default) missed by as much. The copy model's legacy left hand (drawn on
            // the free controller while the fists are not: a switch's dip, holsters off with the
            // remote mine) keeps the gun's own base, which its per-weapon shove below was tuned on:
            // vrCopyOff is what it adds back after the fists' snapshot.
            f32 vrCopyOff[3];

            {
                struct coord added = gVrGunOff[HAND_LEFT]; // what vr_sp274 has in it (lhand->posrotmtx.m[3])
                struct coord o = { VrGunOffX, 16.0f + VrGunOffY, -4.0f + VrGunOffZ };
                struct coord gun = { added.x - o.x, added.y - o.y, added.z - o.z }; // the gun's base over it

                // The per-weapon base X is already mirrored with VrLeftHandedMode (vr_gun_pos_rot),
                // the per-player X trim is not: in left-handed mode the free hand is the physical
                // right one, so the trim is mirrored here, or that hand sits two trim widths (4.7 cm
                // at 2.35) nearer the body's midline than a mirror of the left one. Measured from the
                // open hand's bone (PD-VRR fistwrist): both hands read x +2.3 in the off-controller
                // frame before, where a mirror reads -2.3. Only the turned offset is mirrored: the
                // plain one taken back out is the one vr_sp274 got, or the difference (4.7 cm along
                // the view's X, not turned with the hand) stays in (headset, checkpoint E run 2: the
                // right hand read x -6.9, and y and z moved with the hand's turn).
                if (VrLeftHandedMode) {
                    o.x -= 2.0f * VrGunOffX;
                }

                Mtxf *pr = &lhand->posrotmtx;
                float orx = -(o.x*pr->m[0][0] + o.y*pr->m[1][0] + o.z*pr->m[2][0]);
                float ory =   o.x*pr->m[0][1] + o.y*pr->m[1][1] + o.z*pr->m[2][1];
                float orz = -(o.x*pr->m[0][2] + o.y*pr->m[1][2] + o.z*pr->m[2][2]);
                vr_sp2c4.m[3][0] += -added.x + orx;
                vr_sp2c4.m[3][1] += -added.y + ory + bgunGetFovOffsetY();
                vr_sp2c4.m[3][2] += -added.z + orz - bgunGetFovOffsetZ();

                // The trim cancels out of the difference, its left-handed mirror too
                vrCopyOff[0] = -(gun.x*pr->m[0][0] + gun.y*pr->m[1][0] + gun.z*pr->m[2][0]);
                vrCopyOff[1] =   gun.x*pr->m[0][1] + gun.y*pr->m[1][1] + gun.z*pr->m[2][1];
                vrCopyOff[2] = -(gun.x*pr->m[0][2] + gun.y*pr->m[1][2] + gun.z*pr->m[2][2]);
            }



            // FISTS POSE: snapshot vr_sp2c4 HERE -- at this point it is the fully world-locked free
            // left-hand frame and nothing below has run yet. Everything that follows (the per-weapon
            // foregrip shove) exists purely to drag the GUN so its left-hand BONE
            // lands on the controller, which is meaningless for a first-class hand model -- so the
            // fists must not inherit any of it. Then apply the same tail transforms bgun0f0a5550
            // gives the unarmed left hand.
            {
                Mtxf fist = vr_sp2c4;

                // Clench amount is eased HERE (this block runs before the fists render in the same
                // loop iteration) because the roll depends on it: the clenched anim-1002 hand sits at
                // a different orientation than the rest pose, so the neutral roll over-rotates it.
                // Single-handed weapons only -- on two-handers the grip button already means "two-hand
                // grab pose", so binding the clench there would fight it.
                {
                    f32 want = (VrFistClench
                                && !VrTwoHandsGun(g_Vars.currentplayer->gunctrl.weaponnum)
                                && get_button_state(0, "grip")) ? 1.0f : 0.0f;
                    VrFistClenchAmt += (want - VrFistClenchAmt) * 0.35f;
                }

                // The drawn root, and the open hand's root: the manual reload pins the clip hand to
                // where the open fists' hand bone sits, whatever the clench drawn now
                vrFistRoot(&fist, VrFistClenchAmt, &g_VrFistSp2c4);
                vrFistRoot(&fist, 0.0f, &g_VrFistOpenSp2c4);
                g_VrFistReady = true;
            }

            // The copy model at the gun's own base (see vrCopyOff). The held and slapping hands drawn
            // from it are placed by the wrist capture and the gun, so only its turn reaches them
            vr_sp2c4.m[3][0] += vrCopyOff[0];
            vr_sp2c4.m[3][1] += vrCopyOff[1];
            vr_sp2c4.m[3][2] += vrCopyOff[2];

            if(VrLeftHandedMode){
                mtx00015e24(-1, &vr_sp2c4);
            }

            float roll_angle = -1.30; // Angle en radians
            Mtxf rotZ;
            Mtxf tmp;

            // Génère directement la matrice de rotation Z
            mtx4LoadZRotation(roll_angle, &rotZ);

            // Applique la multiplication
            mtx4MultMtx4(&vr_sp2c4, &rotZ, &tmp);
            vr_sp2c4 = tmp;


            // VR Fix left Hand position for some weapons.
            if(g_Vars.currentplayer->gunctrl.weaponnum == WEAPON_DRAGON
               || g_Vars.currentplayer->gunctrl.weaponnum == WEAPON_SUPERDRAGON){
                vr_sp2c4.m[3][0] -= vr_sp2c4.m[0][0] * 20.0f + vr_sp2c4.m[1][0] * 0.0f + vr_sp2c4.m[2][0] * 32.0f;
                vr_sp2c4.m[3][1] -= vr_sp2c4.m[0][1] * 20.0f + vr_sp2c4.m[1][1] * 0.0f + vr_sp2c4.m[2][1] * 32.0f;
                vr_sp2c4.m[3][2] -= vr_sp2c4.m[0][2] * 20.0f + vr_sp2c4.m[1][2] * 0.0f + vr_sp2c4.m[2][2] * 32.0f;
            }else if (g_Vars.currentplayer->gunctrl.weaponnum == WEAPON_K7AVENGER){
                vr_sp2c4.m[3][0] -= vr_sp2c4.m[0][0] * -4.0f + vr_sp2c4.m[1][0] * 0.0f + vr_sp2c4.m[2][0] * 4.0f;
                vr_sp2c4.m[3][1] -= vr_sp2c4.m[0][1] * -4.0f + vr_sp2c4.m[1][1] * 0.0f + vr_sp2c4.m[2][1] * 4.0f;
                vr_sp2c4.m[3][2] -= vr_sp2c4.m[0][2] * -4.0f + vr_sp2c4.m[1][2] * 0.0f + vr_sp2c4.m[2][2] * 4.0f;
            }else{
                vr_sp2c4.m[3][0] -= vr_sp2c4.m[0][0] * 20.0f + vr_sp2c4.m[1][0] * 0.0f + vr_sp2c4.m[2][0] * 0.0f;
                vr_sp2c4.m[3][1] -= vr_sp2c4.m[0][1] * 20.0f + vr_sp2c4.m[1][1] * 0.0f + vr_sp2c4.m[2][1] * 0.0f;
                vr_sp2c4.m[3][2] -= vr_sp2c4.m[0][2] * 20.0f + vr_sp2c4.m[1][2] * 0.0f + vr_sp2c4.m[2][2] * 0.0f;
            }





            mtx00015f04(0.10000001f, &vr_sp2c4);

            g_VrCopyWepSp2c4 = vr_sp2c4;
            g_VrCopyWepReadyToRender = true;
        }


        // Save the free position of the left hand: the clip-in-hand pose is drawn there
        g_VrLeftHandFreeSp2c4 = g_VrCopyWepSp2c4;



        if (g_VrCopyWepModeldef != NULL
            && g_VrCopyWepReadyToRender
            && vrOffHandHasNoGun()
            && OffHandPose
            && g_Vars.currentplayer->gunctrl.weaponnum != WEAPON_LASER) {

            g_VrCopyWepReadyToRender = false;

            renderdata.gdl = gdl;
            renderdata.zbufferenabled = true;
            renderdata.unk30 = 4;
            renderdata.envcolour = player->gunshadecol[0] << 24
                                   | player->gunshadecol[1] << 16
                                   | player->gunshadecol[2] << 8
                                   | player->gunshadecol[3];
            renderdata.cullmode = G_CULL_BOTH;

            if (OnlyWeapons && g_VrCopyWepModeldef != NULL && g_VrCopyWepModel.matrices != NULL) {

                s32 gnm = g_VrCopyWepModeldef->nummatrices;
                Mtxf *vr_mtxalloc = (Mtxf *) gfxAllocate(gnm * sizeof(Mtxf));

                if (vrSwitchCopyGun == true) {
                    vrBuildMtxPartsList(hand, g_VrCopyWepModeldef, false);
                    vrSwitchCopyGun = false;
                }

                if (vr_mtxalloc) {
                    // Render VrCopyWep
                    g_VrCopyWepModel.matrices = vr_mtxalloc;
                    mtx4Copy(&g_VrCopyWepSp2c4, vr_mtxalloc);
                    renderdata.unk00 = &g_VrCopyWepSp2c4;
                    renderdata.unk10 = g_VrCopyWepModel.matrices;

                    struct vrreloaddef *holddef = vrReloadHandHoldsClip(HAND_RIGHT)
                                                  ? vrReloadOwnedDef(HAND_RIGHT) : NULL;
                    f32 slapframe = 0.0f;
                    f32 rackframe = 0.0f;
                    bool drawnarm = false;   // the hand holding a clip or slapping: its arm is the fists' (Task F9c)

                    if (vrReloadSlapDrawn(HAND_RIGHT, &slapframe)) {
                        // Manual reload, a row with rackslap: the free hand slaps the clip in from
                        // below. The left hand of the reload animation's slap frames, placed on the
                        // gun's drawn clip (vrReloadPlaceSlapHand), not on the controller, so the palm
                        // meets the clip; then it goes back to the controller.
                        s32 _wep = g_Vars.currentplayer->gunctrl.weaponnum;
                        struct vrreloaddef *_slapdef = vrReloadOwnedDef(HAND_RIGHT);
                        int _id = _slapdef != NULL ? VR_ANIM_ID(_wep, _slapdef->ammoindex) : -1;
                        bool placed = false;
                        f32 contactframe = 0.0f;

                        mtx4Copy(&g_VrLeftHandFreeSp2c4, vr_mtxalloc);
                        renderdata.unk00 = &g_VrLeftHandFreeSp2c4;

                        // A row with a clip seat (the Laptop Gun) places the hand by its clip as the
                        // animation has it at the contact: read once per gun, from a pose at that frame
                        if (_id >= 0 && vrReloadSlapNeedsContact(HAND_RIGHT, &contactframe)) {
                            vrCopyWepPoseAt(&renderdata, _id, contactframe);
                            vrReloadNoteSlapClip(HAND_RIGHT, g_VrCopyWepModeldef, g_VrCopyWepModel.matrices);
                        }

                        vrCopyWepPoseAt(&renderdata, _id, slapframe);
                        placed = _id >= 0 && vrReloadPlaceSlapHand(HAND_RIGHT, g_VrCopyWepModeldef,
                                                                     g_VrCopyWepModel.matrices, 18);
                        vrHideAllExcept(placed ? LeftHandMtx : HideAll);
                        vrHideGunParts(g_VrCopyWepModel.matrices, handnum);
                        drawnarm = placed;

                    } else if (vrReloadRackDrawn(HAND_RIGHT, &rackframe)) {
                        s32 _wep = g_Vars.currentplayer->gunctrl.weaponnum;
                        struct vrreloaddef *_rackdef = vrReloadOwnedDef(HAND_RIGHT);
                        int _id = _rackdef != NULL ? VR_ANIM_ID(_wep, _rackdef->ammoindex) : -1;
                        bool placed = false;

                        mtx4Copy(&g_VrLeftHandFreeSp2c4, vr_mtxalloc);
                        renderdata.unk00 = &g_VrLeftHandFreeSp2c4;

                        vrCopyWepPoseAt(&renderdata, _id, rackframe);
                        placed = _id >= 0 && vrReloadPlaceRackHand(HAND_RIGHT, g_VrCopyWepModeldef,
                                                                     g_VrCopyWepModel.matrices, 18);
                        vrHideAllExcept(placed ? LeftHandMtx : HideAll);
                        vrHideGunParts(g_VrCopyWepModel.matrices, handnum);
                        drawnarm = placed;

                    } else if (holddef) {
                        // Manual reload: the off-hand holds a clip. Draw the left hand and the clip
                        // at the free left-hand pose, following the controller (no snap to the gun).
                        // The driven magazine's reload animation (the SuperDragon's grenades: 0x3fc).
                        s32 _wep = g_Vars.currentplayer->gunctrl.weaponnum;
                        int _id = VR_ANIM_ID(_wep, holddef->ammoindex);
                        bool isguided = vrReloadClipIsGuided(HAND_RIGHT);
                        f32 fromframe = (holddef->insertframefrom > 0.0f) ? holddef->insertframefrom : holddef->holdframe;
                        f32 curframe = fromframe;
                        bool placed = false;

                        if (isguided && holddef->insertframeto > 0.0f) {
                            f32 prog = vrReloadGuideProgress(HAND_RIGHT);
                            curframe = fromframe + prog * (holddef->insertframeto - fromframe);
                        }

                        mtx4Copy(&g_VrLeftHandFreeSp2c4, vr_mtxalloc);
                        renderdata.unk00 = &g_VrLeftHandFreeSp2c4;

                        vrCopyWepPoseAt(&renderdata, _id, curframe);

                        if (isguided && vrReloadPlaceGuidedHand(HAND_RIGHT, g_VrCopyWepModeldef,
                                                               g_VrCopyWepModel.matrices, 18)) {
                            placed = true;
                        } else {
                            vrReloadAlignHeldHand(HAND_RIGHT, g_VrCopyWepModeldef, g_VrCopyWepModel.matrices,
                                                  &g_VrLeftHandFreeSp2c4, 18);
                            vrReloadCaptureHeldClip(HAND_RIGHT, g_VrCopyWepModeldef, g_VrCopyWepModel.matrices);
                            vrReloadPoseHeldLook(HAND_RIGHT, g_VrCopyWepModeldef, g_VrCopyWepModel.matrices, 18);
                            placed = true;
                        }

                        // A clip guided into the well is drawn in the gun: the hand stays in its
                        // holding pose, empty, so it does not jump away from the clip.
                        // Guns with no clip in gun model (Crossbow) keep held bolt visible.
                        bool drawClip = vrReloadHeldClipDrawn(HAND_RIGHT) || (holddef->clippart < 0);
                        vrHideAllExcept(drawClip ? vrLeftHandAndMagMtx(holddef) : LeftHandMtx);
                        vrHideGunParts(g_VrCopyWepModel.matrices, handnum);
                        drawnarm = placed;

                    } else if ((rhand->state == HANDSTATE_RELOAD)
                               || (VrTwoHandGrip &&
                                   VrTwoHandsGun(g_Vars.currentplayer->gunctrl.weaponnum))) {
                        modelUpdateRelations(&g_VrCopyWepModel);
                        modelSetMatricesWithAnim(&renderdata, &g_VrCopyWepModel);
                        vrHideAllExcept(HideAll);
                        vrHideGunParts(g_VrCopyWepModel.matrices, handnum);
                    } else {
                        g_VrCopyWepModel.anim = NULL;
                        modelUpdateRelations(&g_VrCopyWepModel);
                        modelSetMatricesWithAnim(&renderdata, &g_VrCopyWepModel);

                        if (vrOffHandIsFree(player)) {
                            // The fists model draws the off-hand now, so drop the legacy left hand.
                            vrHideAllExcept(HideAll);
                        } else {
                            vrHideAllExcept(LeftHandMtx);
                        }

                        vrHideGunParts(g_VrCopyWepModel.matrices, handnum);

                    }
                    //-------------------

                    // The left hand's lower is part of its CHANGEGUN state (its minor alone also
                    // matches a parked gun's attack)
                    if (lhand->state == HANDSTATE_CHANGEGUN) {
                        vrHideAllExcept(HideAll);
                        vrHideGunParts(g_VrCopyWepModel.matrices, handnum);
                    }


                    s32 currentWeapon = player->hands[HAND_RIGHT].gset.weaponnum;
                    bool CrossbowLaserUnarmed =
                            currentWeapon == WEAPON_CROSSBOW || currentWeapon == WEAPON_LASER || (!VrMotionThrowing && currentWeapon == WEAPON_UNARMED);
                    bool isReloading = (drawnarm || vrReloadHandHoldsClip(HAND_RIGHT)
                                        || vrReloadSlapDrawn(HAND_RIGHT, NULL) || vrReloadRackDrawn(HAND_RIGHT, NULL)
                                        || hand->state == HANDSTATE_RELOAD || vrReloadMidReload(handnum));
                    if (!CrossbowLaserUnarmed && !isReloading) {
                        // TODO VR fixe left wrist rot on vr_world_scale = 42.5f levels
                        vr_wrist_rot(g_VrCopyWepModeldef, g_VrCopyWepModel.matrices, -1.0f);
                    }

                    // Manual reload (Task F9c): the arm of the hand holding a clip or slapping is the
                    // free fists' arm, from the controller as they draw it, hung from the drawn wrist
                    // and tucked as theirs, so taking a clip, putting it in or slapping changes only
                    // the hand and the clip. The hand and clip turns, the align and the slap's
                    // placing moved the reload animation's elbow (17) rigidly with the hand, and
                    // vr_wrist_rot tucks it inboard, so it came out of the torso.
                    if (drawnarm && !isReloading && vrModelHasHands(g_VrCopyWepModeldef)
                            && vrReloadPlaceHeldArm(HAND_RIGHT, g_VrCopyWepModeldef, g_VrCopyWepModel.matrices, 17, 18)) {
                        vr_tuck_held_arm(g_VrCopyWepModel.matrices, 17, 18);
                    }

                    if (hand->state == HANDSTATE_RELOAD) {
                        vrHideAllExcept(HideAll);
                        vrHideGunParts(g_VrCopyWepModel.matrices, handnum);
                    }

                    if(g_VrCopyWepModel.matrices != NULL && VrHideArms) {
                        vrHideOnly(ArmsMtx);
                        vrHideGunParts(g_VrCopyWepModel.matrices, handnum);
                    }

                    // VR holsters: with the off hand free, the copy draws no left hand either
                    // (it draws twice, once per loop pass: the line notes the first)
                    struct vrlefthide vrcopysaved;

                    vrFreeLeftHandHide("copy", i == HAND_RIGHT, &g_VrCopyWepModel,
                            g_VrCopyHandModeldef != NULL ? &g_VrCopyHandModel : NULL, g_VrCopyWepModel.matrices,
                            player->gunctrl.weaponnum, &vrcopysaved);

                    modelRender(&renderdata, &g_VrCopyWepModel);
                    mtxF2LBulk(g_VrCopyWepModel.matrices, gnm);

                    // ── Render left hand and weapon part ─────────────────────────
                    if (g_VrCopyHandModeldef != NULL) {
                        g_VrCopyHandModel.matrices = g_VrCopyWepModel.matrices;  // Same buffer as the weapon
                        modelUpdateRelations(&g_VrCopyHandModel);
                        modelRender(&renderdata, &g_VrCopyHandModel);
                    }

                    vrFreeLeftHandRestore(&vrcopysaved);

                }
            }

            // ── FISTS OFF-HAND ───────────────────────────────────────────────────────────
            // Render the real unarmed fists model for the free off-hand, from its own matrix
            // buffer and the clean g_VrFistSp2c4 pose. i == HAND_RIGHT because this whole VR
            // off-hand section sits inside the for(i = 0; i < 2) loop and would otherwise draw twice.
            if (i == HAND_RIGHT && g_VrFistReady && vrOffHandIsFree(player)
                && vrFistEnsureLoaded()) {
                s32 fnm = g_VrFistModeldef->nummatrices;
                Mtxf *fm = (Mtxf *)gfxAllocate(fnm * sizeof(Mtxf));

                if (fm) {
                    // Manual reload: the OPEN hand's hand bone for vrReloadNoteFistWrist, from a pass
                    // of its own with the rest pose and the open roll, so the clench drawn now never
                    // moves the capture (a pair's free hand shows the fists still clenched from
                    // gripping its parked gun). Only the root and the pose place the hand bone:
                    // vr_wrist_rot moves the forearm alone, so this pass needs nothing else.
                    if (VrManualReloading && fnm <= VR_FIST_OPEN_MAX_MTX) {
                        Mtxf *om = g_VrFistOpenMtx;

                        {
                            struct anim drawnanim = g_VrFistAnim;

                            g_VrFistAnim.animnum = 0;   // inert -> rest pose, as the open branch below
                            g_VrFistAnim.animnum2 = 0;
                            g_VrFistAnim.fracmerge = 0.0f;
                            mtx4Copy(&g_VrFistOpenSp2c4, om);
                            renderdata.unk00 = &g_VrFistOpenSp2c4;
                            renderdata.unk10 = om;
                            g_VrFistModel.matrices = om;
                            modelUpdateRelations(&g_VrFistModel);
                            modelSetMatricesWithAnim(&renderdata, &g_VrFistModel);
                            // ... and the open palm, the middle of the hand bone and its four
                            // knuckles (a hands model has them: 33 matrices), for the slap's test
                            // (Task F7d)
                            f32 palm[3];
                            bool haspalm = fnm > 14;
                            s32 c;

                            for (c = 0; c < 3 && haspalm; c++) {
                                palm[c] = (om[2].m[3][c] + (om[5].m[3][c] + om[8].m[3][c]
                                        + om[11].m[3][c] + om[14].m[3][c]) * 0.25f) * 0.5f;
                            }

                            vrReloadNoteFistWrist(&om[2], haspalm ? palm : NULL, VrFistClenchAmt);
                            // ... and the open arm, from the same pass, for the hand holding a clip
                            // or slapping (Task F9c: the copy model draws this arm, tucked as the
                            // fists' is). This pass runs no vr_wrist_rot: it equals the drawn fists'
                            // arm before the elbow solve while the hand is open (the clench pose and
                            // roll turn it too)
                            vrReloadNoteFistArm(&om[1], &om[2]);
                            g_VrFistAnim = drawnanim;
                        }
                    }

                    mtx4Copy(&g_VrFistSp2c4, fm);   // seed matrices[0] with the root, as the gun path does

                    // NOTE: modelSetMatrices ADVANCES renderdata.unk10, so it must be re-pointed here
                    // (the copy-weapon draw above already consumed it).
                    renderdata.unk00 = &g_VrFistSp2c4;
                    renderdata.unk10 = fm;

                    // CLENCH: hold a fixed frame of the unarmed grip anim (1002, fully closed at 28),
                    // eased so it opens/closes smoothly. Driven directly rather than via
                    // bgunStartAnimation because this model has no hand state machine behind it.
                    // Single-handed weapons ONLY: on two-handers the grip button already means "two-hand
                    // grab pose", so binding the clench there would fight it.
                    // VrFistClenchAmt was eased in the pose block above (the roll depends on it).
                    // Note the grip is read there from the RAW left button, not vr_button_L_grip:
                    // that flag is gated on vr_leftHasWeapon (port/src/input.c:884) and so is
                    // permanently false for a FREE off-hand -- exactly our case.
                    if (VrFistClenchAmt > 0.02f) {
                        modelSetAnimation(&g_VrFistModel, 1002, false, VrFistClenchAmt * 28.0f, 0.0f, 0.0f);
                    } else {
                        g_VrFistAnim.animnum = 0;   // inert -> modeldef rest pose (open hand)
                    }

                    g_VrFistModel.matrices = fm;
                    modelUpdateRelations(&g_VrFistModel);
                    modelSetMatricesWithAnim(&renderdata, &g_VrFistModel);

                    // ARM IK -- without this the forearm renders as a rigid rest pose. Same call the
                    // unarmed hand gets in bgun0f0a5550 and the legacy copy hand gets: turns the
                    // forearm about the wrist onto the elbow solved from the off-hand's shoulder
                    // (vr_arm_solve). Must run after modelSetMatricesWithAnim and before the bones
                    // are collapsed, as the gun path does.
                    vr_wrist_rot(g_VrFistModeldef, fm, VR_FIST_SIDE);

                    // We render the model's RIGHT hand mirrored (as the unarmed left hand is built),
                    // so collapse the native left-hand bones.
                    vrFistHideRange(fm, fnm, 17, 32);

                    // Mirroring inverts winding -> flip culling, as the unarmed left hand does.
                    // modelApplyCullMode only auto-clears G_CULL_BOTH for CULLMODE_NONE, so bracket it.
                    s32 prevcull = renderdata.cullmode;
                    gSPClearGeometryMode(renderdata.gdl++, G_CULL_BOTH);

                    renderdata.cullmode = CULLMODE_NONE;

                    if(g_VrFistModel.matrices != NULL && VrHideArms) {
                        vrHideOnly(ArmsMtx);
                        vrHideGunParts(g_VrFistModel.matrices, handnum);
                    }

                    // PD-VRR: the fists' own left-hand bones (vrFistHideRange's 17-32) left after
                    // their hide (none expected)
                    if (g_VrHandRenderDump && vrHolsterActive()) {
                        static const s32 fistleft[16] = {
                            17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
                        };

                        vrLeftHandEnsureBuilt();
                        vrFreeLeftHandNote("fists 17-32", &g_VrFistModel, NULL, fm, fnm, false, fistleft, 16);
                    }

                    modelRender(&renderdata, &g_VrFistModel);
                    mtxF2LBulk(fm, fnm);

                    renderdata.cullmode = prevcull;
                    gSPClearGeometryMode(renderdata.gdl++, G_CULL_BOTH);
                }
            }
            g_VrFistReady = false;


            gdl = renderdata.gdl;
        }
    }

    if (g_VrHandRenderDump && sVrLeftDiagLen > 0) {
        vrHolsterDiagText(sVrLeftDiag);
    }

    sVrLeftDiagLen = 0;
    g_VrHandRenderDump = false;

    casingsRender(&gdl);
    zbufSwap();

    gdl = zbufConfigureRdp(gdl);
    gdl = vi0000b1d0(gdl);

    gDPSetScissor(gdl++, G_SC_NON_INTERLACE, viGetViewLeft(), viGetViewTop(),
                  viGetViewLeft() + viGetViewWidth(), viGetViewTop() + viGetViewHeight());

    *gdlptr = gdl;
}
#endif

/**
 * Find and return an available audio handle out of a pool of four.
 */
struct sndstate** bgunAllocateAudioHandle(void)
{
    s32 i;

    for (i = 0; i < ARRAYCOUNT(g_BgunAudioHandles); i++) {
        if (g_BgunAudioHandles[i] == NULL) {
            return &g_BgunAudioHandles[i];
        }
    }

    return NULL;
}

void bgunPlayPropHitSound(struct gset* gset, struct prop* prop, s32 texturenum)
{
#if VERSION >= VERSION_NTSC_1_0
    u32 rand1 = rngRandom();
    u32 rand2 = rngRandom();
    struct sndstate** handle;

    if (g_Vars.lvupdate240 <= 0) {
        return;
    }

    if (texturenum >= 0 && texturenum < NUM_TEXTURES
        && g_SurfaceTypes[g_Textures[texturenum].soundsurfacetype]->numsounds == 0) {
        return;
    }

    if (gset->weaponnum == WEAPON_REMOTEMINE
        || gset->weaponnum == WEAPON_PROXIMITYMINE
        || gset->weaponnum == WEAPON_TIMEDMINE
        || gset->weaponnum == WEAPON_COMMSRIDER
        || gset->weaponnum == WEAPON_TRACERBUG
        || gset->weaponnum == WEAPON_TARGETAMPLIFIER
        || gset->weaponnum == WEAPON_ECMMINE) {
        psCreate(NULL, prop, SFX_80AA, -1, -1, 0, 0, PSTYPE_NONE, NULL, -1, NULL, -1, -1, -1, -1);
        return;
    }

    handle = bgunAllocateAudioHandle();

    if (handle) {
        if (prop->type == PROPTYPE_CHR || prop->type == PROPTYPE_PLAYER) {
            struct chrdata* chr = prop->chr;
            s16 soundnum = -1;
            bool overridden = false;
            s32 vol;
            s32 pan;

            if (chrGetShield(chr) > 0) {
                soundnum = SFX_SHIELD_DAMAGE;
            }
            else if (gset->weaponnum == WEAPON_COMBATKNIFE
                     || gset->weaponnum == WEAPON_COMBATKNIFE // duplicate
                     || gset->weaponnum == WEAPON_BOLT) {
                soundnum = SFX_05F6;
                overridden = true;
            }
            else if (gset->weaponnum == WEAPON_UNARMED
                     || (gset->weaponfunc == FUNC_SECONDARY
                         && (gset->weaponnum == WEAPON_FALCON2
                             || gset->weaponnum == WEAPON_FALCON2_SILENCER
                             || gset->weaponnum == WEAPON_FALCON2_SCOPE
                             || gset->weaponnum == WEAPON_DY357MAGNUM
                             || gset->weaponnum == WEAPON_DY357LX))) {
                s16 sounds[] = { SFX_002F, SFX_0030, SFX_0031 };
                soundnum = sounds[rand1 % ARRAYCOUNT(sounds)];
            }
            else {
                s16 sounds[] = { SFX_HIT_CHR, SFX_HIT_CHR };
                soundnum = sounds[rand1 % ARRAYCOUNT(sounds)];
            }

            if (soundnum != -1) {
                psGetTheoreticalVolPan(&prop->pos, prop->rooms, soundnum, &vol, &pan);

                if (vol) {
                    sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);

                    if (*handle) {
                        sndAdjust(handle, 0, vol, pan, soundnum, 1, 1, -1, 1);
                    }
                }
            }

            if (overridden) {
                return;
            }
        }
        else {
            s16 soundnum = -1;
            bool overridden = false;
            s32 vol;
            s32 pan;
            u32 stack;

            if (texturenum == 10000) {
                soundnum = SFX_SHIELD_DAMAGE;
            }
            else if (gset->weaponnum == WEAPON_LASER) {
                if (gset->weaponfunc == FUNC_PRIMARY || ((gset->unk063a % 4) == 0 && (rngRandom() % 2))) {
                    if ((rngRandom() % 2) == 0) {
                        soundnum = SFX_CLOAK_ON;
                    }
                    else {
                        soundnum = SFX_CLOAK_OFF;
                    }
                }

                overridden = true;
            }
            else {
                if (gset->weaponnum == WEAPON_COMBATKNIFE || gset->weaponnum == WEAPON_BOLT) {
                    soundnum = SFX_HIT_METAL_8079;
                    overridden = true;
                }
                else {
                    s16 sounds[] = {
                            SFX_001B, SFX_001C, SFX_001D, SFX_001E,
                            SFX_001B, SFX_001C, SFX_001D, SFX_001E,
                            SFX_001B, SFX_001C, SFX_001D, SFX_001E,
                            SFX_0023, SFX_0024, SFX_0025, SFX_0026,
                            SFX_0027, SFX_0028, SFX_0029, SFX_002A,
                    };

                    soundnum = sounds[rand1 % ARRAYCOUNT(sounds)];
                }
            }

            if (soundnum != -1) {
                psGetTheoreticalVolPan(&prop->pos, prop->rooms, soundnum, &vol, &pan);

                if (vol) {
                    sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);

                    if (*handle) {
                        sndAdjust(handle, 0, vol, pan, soundnum, 1, 1, -1, 1);
                    }
                }
            }

            if (overridden) {
                return;
            }
        }
    }

    if (texturenum >= 0 && texturenum < NUM_TEXTURES && g_SurfaceTypes[g_Textures[texturenum].soundsurfacetype]) {
        s16 soundnum = -1;

        handle = bgunAllocateAudioHandle();

        if (handle) {
            if (g_SurfaceTypes[g_Textures[texturenum].soundsurfacetype]->numsounds > 0) {
                s32 index = rand2 % g_SurfaceTypes[g_Textures[texturenum].soundsurfacetype]->numsounds;
                soundnum = g_SurfaceTypes[g_Textures[texturenum].soundsurfacetype]->sounds[index];

                if (soundnum != -1) {
                    sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
                }
            }

            if (*handle && soundnum != -1) {
                psApplyVolPan(*handle, &prop->pos, 400, 2500, 3000, prop->rooms, soundnum, AL_VOL_FULL, 0);
            }
        }
    }
#else
    u32 rand1 = rngRandom();
		u32 rand2 = rngRandom();
		struct sndstate** handle;

		if (g_Vars.lvupdate240 <= 0) {
				return;
		}

		if (texturenum >= 0 && texturenum < NUM_TEXTURES
				&& g_SurfaceTypes[g_Textures[texturenum].soundsurfacetype]->numsounds == 0) {
				return;
		}

		if (gset->weaponnum == WEAPON_REMOTEMINE
				|| gset->weaponnum == WEAPON_PROXIMITYMINE
				|| gset->weaponnum == WEAPON_TIMEDMINE
				|| gset->weaponnum == WEAPON_COMMSRIDER
				|| gset->weaponnum == WEAPON_TRACERBUG
				|| gset->weaponnum == WEAPON_TARGETAMPLIFIER
				|| gset->weaponnum == WEAPON_ECMMINE) {
				psCreate(NULL, prop, SFX_80AA, -1, -1, 0, 0, PSTYPE_NONE, NULL, -1, NULL, -1, -1, -1, -1);
				return;
		}

		handle = bgunAllocateAudioHandle();

		if (handle) {
				if (prop->type == PROPTYPE_CHR || prop->type == PROPTYPE_PLAYER) {
						struct chrdata* chr = prop->chr;
						s16 soundnum;
						bool overridden = false;

						if (chrGetShield(chr) > 0) {
								sndStart(var80095200, SFX_SHIELD_DAMAGE, handle, -1, -1, -1, -1, -1);
								soundnum = SFX_SHIELD_DAMAGE;
						}
						else if (gset->weaponnum == WEAPON_COMBATKNIFE
								|| gset->weaponnum == WEAPON_COMBATKNIFE // duplicate
								|| gset->weaponnum == WEAPON_BOLT) {
								sndStart(var80095200, SFX_05F6, handle, -1, -1, -1, -1, -1);
								soundnum = SFX_05F6;
								overridden = true;
						}
						else if (gset->weaponnum == WEAPON_UNARMED
								|| (gset->weaponfunc == FUNC_SECONDARY
										&& (gset->weaponnum == WEAPON_FALCON2
												|| gset->weaponnum == WEAPON_FALCON2_SILENCER
												|| gset->weaponnum == WEAPON_FALCON2_SCOPE
												|| gset->weaponnum == WEAPON_DY357MAGNUM
												|| gset->weaponnum == WEAPON_DY357LX))) {
								s16 sounds[] = { SFX_002F, SFX_0030, SFX_0031 };
								soundnum = sounds[rand1 % ARRAYCOUNT(sounds)];
								sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
						}
						else {
								s16 sounds[] = { SFX_HIT_CHR, SFX_HIT_CHR };
								soundnum = sounds[rand1 % ARRAYCOUNT(sounds)];
								sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
						}

						if (*handle) {
								psApplyVolPan(*handle, &prop->pos, 400, 2500, 3000, prop->rooms, soundnum, AL_VOL_FULL, 0);
						}

						if (overridden) {
								return;
						}
				}
				else {
						s16 soundnum;
						bool overridden = false;
						u32 stack;

						if (texturenum == 10000) {
								sndStart(var80095200, SFX_SHIELD_DAMAGE, handle, -1, -1, -1, -1, -1);
								soundnum = SFX_SHIELD_DAMAGE;
						}
						else if (gset->weaponnum == WEAPON_LASER) {
								if (gset->weaponfunc == FUNC_PRIMARY || (gset->unk063a % 8) == 0) {
										if ((rngRandom() % 2) == 0) {
												soundnum = SFX_CLOAK_ON;
										}
										else {
												soundnum = SFX_CLOAK_OFF;
										}

										sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
										overridden = true;
								}
						}
						else {
								if (gset->weaponnum == WEAPON_COMBATKNIFE || gset->weaponnum == WEAPON_BOLT) {
										soundnum = SFX_HIT_METAL_8079;
										sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
										overridden = true;
								}
								else {
										s16 sounds[] = {
											SFX_001B, SFX_001C, SFX_001D, SFX_001E,
											SFX_001B, SFX_001C, SFX_001D, SFX_001E,
											SFX_001B, SFX_001C, SFX_001D, SFX_001E,
											SFX_0023, SFX_0024, SFX_0025, SFX_0026,
											SFX_0027, SFX_0028, SFX_0029, SFX_002A,
										};

										soundnum = sounds[rand1 % ARRAYCOUNT(sounds)];
										sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
								}
						}

						if (*handle) {
								psApplyVolPan(*handle, &prop->pos, 400, 2500, 3000, prop->rooms, soundnum, AL_VOL_FULL, 0);
						}

						if (overridden) {
								return;
						}
				}
		}

		if (texturenum >= 0 && texturenum < NUM_TEXTURES && g_SurfaceTypes[g_Textures[texturenum].soundsurfacetype]) {
				s16 soundnum = -1;

				handle = bgunAllocateAudioHandle();

				if (handle) {
						if (g_SurfaceTypes[g_Textures[texturenum].soundsurfacetype]->numsounds > 0) {
								s32 index = rand2 % g_SurfaceTypes[g_Textures[texturenum].soundsurfacetype]->numsounds;
								soundnum = g_SurfaceTypes[g_Textures[texturenum].soundsurfacetype]->sounds[index];

								sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
						}

						if (*handle) {
								psApplyVolPan(*handle, &prop->pos, 400, 2500, 3000, prop->rooms, soundnum, AL_VOL_FULL, 0);
						}
				}
		}
#endif
}

void bgunPlayGlassHitSound(struct coord* pos, RoomNum * rooms, s32 texturenum)
{
    if (g_Vars.lvupdate240 > 0) {
        struct sndstate** handle = bgunAllocateAudioHandle();

        if (handle) {
            sndStart(var80095200, SFX_HIT_GLASS, handle, -1, -1, -1, -1, -1);

            if (*handle) {
                psApplyVolPan(*handle, pos, 400, 2500, 3000, rooms, SFX_HIT_GLASS, AL_VOL_FULL, 0);
            }
        }
    }
}

void bgunPlayBgHitSound(struct gset* gset, struct coord* hitpos, s32 texturenum, RoomNum * rooms)
{
#if VERSION >= VERSION_NTSC_1_0
    struct sndstate** handle;
    u32 rand1 = rngRandom();
    u32 rand2 = rngRandom();
    bool playdefault;
    s16 soundnum;
    bool overridden;

    if (g_Vars.lvupdate240 <= 0) {
        return;
    }

    if (texturenum >= 0 && texturenum < NUM_TEXTURES && g_SurfaceTypes[g_Textures[texturenum].soundsurfacetype]->numsounds == 0) {
        return;
    }

    playdefault = true;
    handle = bgunAllocateAudioHandle();

    if (handle) {
        soundnum = -1;
        overridden = false;

        if (gset->weaponnum == WEAPON_LASER) {
            playdefault = false;

            if (gset->weaponfunc == FUNC_PRIMARY || ((gset->unk063a % 4) == 0 && (rngRandom() % 2))) {
                // Laser sounds
                s16 sounds[] = { SFX_CLOAK_ON, SFX_CLOAK_OFF };
                soundnum = sounds[rand1 % ARRAYCOUNT(sounds)];
                sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
                overridden = true;
            }
        }
        else if (gset->weaponnum == WEAPON_COMBATKNIFE || gset->weaponnum == WEAPON_BOLT) {
            // Knives and bolts make a metal sound
            soundnum = SFX_HIT_METAL_8079;
            sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
            overridden = true;
        }
        else if (gset->weaponnum == WEAPON_REMOTEMINE
                 || gset->weaponnum == WEAPON_PROXIMITYMINE
                 || gset->weaponnum == WEAPON_TIMEDMINE
                 || gset->weaponnum == WEAPON_COMMSRIDER
                 || gset->weaponnum == WEAPON_TRACERBUG
                 || gset->weaponnum == WEAPON_TARGETAMPLIFIER
                 || gset->weaponnum == WEAPON_ECMMINE) {
            // Mine landing/activation sound
            soundnum = SFX_80AA;
            sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
            overridden = true;
        }
        else {
            // Ricochet sounds
            s16 sounds[] = {
                    SFX_0013, SFX_0014, SFX_0015, SFX_0016,
                    SFX_0017, SFX_0018, SFX_0019, SFX_001A,
                    SFX_0017, SFX_0018, SFX_0019, SFX_001A,
                    SFX_0017, SFX_0018, SFX_0019, SFX_001A,
                    SFX_001F, SFX_0020, SFX_0020, SFX_0021,
                    SFX_001F, SFX_0020, SFX_0020, SFX_0021,
                    SFX_001F, SFX_0020, SFX_0020, SFX_0021,
                    SFX_0023, SFX_0024, SFX_0025, SFX_0026,
                    SFX_0027, SFX_0028, SFX_0029, SFX_002A,
            };

            soundnum = sounds[rand1 % ARRAYCOUNT(sounds)];
            sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
            overridden = false;
        }

        if (*handle != NULL) {
            psApplyVolPan(*handle, hitpos, 400, 2500, 3000, rooms, soundnum, AL_VOL_FULL, 0);
        }

        if (overridden) {
            return;
        }
    }

    if (playdefault) {
        handle = bgunAllocateAudioHandle();

        if (handle != NULL && texturenum >= 0 && texturenum < NUM_TEXTURES) {
            s16 soundnum;
            struct surfacetype* type = g_SurfaceTypes[g_Textures[texturenum].soundsurfacetype];

            if (type->numsounds > 0) {
                soundnum = -1;

                if (type != NULL) {
                    s32 index = rand2 % type->numsounds;
                    soundnum = type->sounds[index];
                    sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
                }

                if (*handle != NULL) {
                    psApplyVolPan(*handle, hitpos, 400, 2500, 3000, rooms, soundnum, AL_VOL_FULL, 0);
                }
            }
        }
    }
#else
    struct sndstate** handle;
		u32 rand1 = rngRandom();
		u32 rand2 = rngRandom();
		s16 soundnum;
		bool overridden;

		if (g_Vars.lvupdate240 <= 0) {
				return;
		}

		if (texturenum >= 0 && texturenum < NUM_TEXTURES && g_SurfaceTypes[g_Textures[texturenum].soundsurfacetype]->numsounds == 0) {
				return;
		}

		handle = bgunAllocateAudioHandle();

		if (handle) {
				overridden = false;

				if (gset->weaponnum == WEAPON_LASER) {
						if (gset->weaponfunc == FUNC_PRIMARY || (gset->unk063a % 8) == 0) {
								// Laser sounds
								s16 sounds[] = { SFX_CLOAK_ON, SFX_CLOAK_OFF };
								soundnum = sounds[rand1 % ARRAYCOUNT(sounds)];
								sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
								overridden = true;
						}
				}
				else if (gset->weaponnum == WEAPON_COMBATKNIFE || gset->weaponnum == WEAPON_BOLT) {
						// Knives and bolts make a metal sound
						soundnum = SFX_HIT_METAL_8079;
						sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
						overridden = true;
				}
				else if (gset->weaponnum == WEAPON_REMOTEMINE
						|| gset->weaponnum == WEAPON_PROXIMITYMINE
						|| gset->weaponnum == WEAPON_TIMEDMINE
						|| gset->weaponnum == WEAPON_COMMSRIDER
						|| gset->weaponnum == WEAPON_TRACERBUG
						|| gset->weaponnum == WEAPON_TARGETAMPLIFIER
						|| gset->weaponnum == WEAPON_ECMMINE) {
						// Mine landing/activation sound
						sndStart(var80095200, SFX_80AA, handle, -1, -1, -1, -1, -1);
						overridden = true;
				}
				else {
						// Ricochet sounds
						s16 sounds[] = {
							SFX_0013, SFX_0014, SFX_0015, SFX_0016,
							SFX_0017, SFX_0018, SFX_0019, SFX_001A,
							SFX_0017, SFX_0018, SFX_0019, SFX_001A,
							SFX_0017, SFX_0018, SFX_0019, SFX_001A,
							SFX_001F, SFX_0020, SFX_0020, SFX_0021,
							SFX_001F, SFX_0020, SFX_0020, SFX_0021,
							SFX_001F, SFX_0020, SFX_0020, SFX_0021,
							SFX_0023, SFX_0024, SFX_0025, SFX_0026,
							SFX_0027, SFX_0028, SFX_0029, SFX_002A,
						};

						soundnum = sounds[rand1 % ARRAYCOUNT(sounds)];
						sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
						overridden = false;
				}

				if (*handle != NULL) {
						psApplyVolPan(*handle, hitpos, 400, 2500, 3000, rooms, soundnum, AL_VOL_FULL, 0);
				}

				if (overridden) {
						return;
				}
		}

		// Play default surface hit sound
		handle = bgunAllocateAudioHandle();

		if (handle != NULL && texturenum >= 0 && texturenum < NUM_TEXTURES) {
				s16 soundnum;
				struct surfacetype* type = g_SurfaceTypes[g_Textures[texturenum].soundsurfacetype];

				if (type->numsounds > 0) {
						soundnum = -1;

						if (type != NULL) {
								s32 index = rand2 % type->numsounds;
								soundnum = type->sounds[index];
								sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
						}

						if (*handle != NULL) {
								psApplyVolPan(*handle, hitpos, 400, 2500, 3000, rooms, soundnum, AL_VOL_FULL, 0);
						}
				}
		}
#endif
}

void bgunSetTriggerOn(s32 handnum, bool on)
{
    struct hand* hand = &g_Vars.currentplayer->hands[handnum];

    hand->triggerprev = hand->triggeron;
    hand->triggeron = on;

    if (!on) {
        hand->triggerreleased = true;
    }


}

#define SETFUNCPRI() g_PlayerConfigsArray[g_Vars.currentplayerstats->mpindex].gunfuncs[(g_Vars.currentplayer->gunctrl.weaponnum - 1) >> 3] &= ~(1 << ((g_Vars.currentplayer->gunctrl.weaponnum - 1) & 7))
#define SETFUNCSEC() g_PlayerConfigsArray[g_Vars.currentplayerstats->mpindex].gunfuncs[(g_Vars.currentplayer->gunctrl.weaponnum - 1) >> 3] |= 1 << ((g_Vars.currentplayer->gunctrl.weaponnum - 1) & 7)

/**
 * This is called once B has been held for 25 ticks, or earlier if pressing B+Z.
 *
 * The function may choose whether to change the gun function,
 * or activate the secondary function without switching to it.
 *
 * Return the following;
 * - USETIMER_CONTINUE if the B button timer should continue incrementing.
 * - USETIMER_STOP if the B button timer should stop (ie. the B press is consumed)
 * - USETIMER_REPEAT if this function should be called again on each frame until B is released.
 */
s32 bgunConsiderToggleGunFunction(s32 usedowntime, bool trigpressed, bool fromactivemenu, bool fromdedicatedbutton)
{
#ifndef PLATFORM_N64
    const bool extcontrols = PLAYER_EXTCFG().extcontrols;
    bool docontinue;
#endif
    switch (bgunGetWeaponNum(HAND_RIGHT)) {
        case WEAPON_SNIPERRIFLE:
            if (extcontrols && usedowntime < 0) {
                return USETIMER_CONTINUE;
            }

            // At 25 ticks (or B+Z), start showing the new function
            g_Vars.currentplayer->gunctrl.invertgunfunc = true;

            // B+Z immediately triggers crouch or stand
            if (trigpressed) {
                if (extcontrols) {
                    g_Vars.currentplayer->hands[HAND_RIGHT].activatesecondary = true;
                }
                return USETIMER_STOP;
            }

            if (fromdedicatedbutton) {
                g_Vars.currentplayer->hands[HAND_RIGHT].activatesecondary = true;
                return USETIMER_CONTINUE;
            }

#ifndef PLATFORM_N64
            if (extcontrols) {
                docontinue = (ABS(usedowntime) < 0);
            }
            else {
                // Don't do anything if B hasn't been held for 50/60ths of a second
                docontinue = (usedowntime < TICKS(50));
            }
            if (docontinue) {
#else
                if (usedowntime < TICKS(50)) {
#endif
                return USETIMER_CONTINUE;
            }

            if (g_Vars.currentplayer->hands[HAND_RIGHT].gset.weaponfunc != FUNC_SECONDARY) {
                return USETIMER_CONTINUE;
            }

            // Do crouch or stand
            g_Vars.currentplayer->hands[HAND_RIGHT].activatesecondary = true;
            return (extcontrols ? USETIMER_STOP : USETIMER_REPEAT);
        case WEAPON_RCP120:
#ifndef PLATFORM_N64
            // very special alt-button handling for RCP-120's cloaking
            if (!trigpressed && extcontrols && fromdedicatedbutton) {
                if (g_Vars.currentplayer->devicesactive & DEVICE_CLOAKRCP120) {
                    g_Vars.currentplayer->devicesactive &= ~DEVICE_CLOAKRCP120;
                }
                else {
                    g_Vars.currentplayer->devicesactive = (g_Vars.currentplayer->devicesactive & ~DEVICE_CLOAKRCP120) | DEVICE_CLOAKRCP120;
                }
                g_Vars.currentplayer->gunctrl.invertgunfunc = false;
                return USETIMER_STOP;
            }
#endif
        case WEAPON_LAPTOPGUN:
        case WEAPON_DRAGON:
        case WEAPON_REMOTEMINE:
            // These weapons use temporary alt functions
            if (extcontrols) {
                g_Vars.currentplayer->gunctrl.invertgunfunc = !g_Vars.currentplayer->gunctrl.invertgunfunc;
            }
            else {
                g_Vars.currentplayer->gunctrl.invertgunfunc = true;
            }

            if (fromactivemenu && bgunIsUsingSecondaryFunction() == true) {
                g_Vars.currentplayer->hands[HAND_RIGHT].activatesecondary = true;
            }

            return USETIMER_STOP;
        case WEAPON_FALCON2:
        case WEAPON_FALCON2_SCOPE:
        case WEAPON_FALCON2_SILENCER:
        case WEAPON_DY357MAGNUM:
        case WEAPON_DY357LX:
        case WEAPON_COMBATKNIFE:
            if (VrMotionThrowing) {
                // VR: disable function switching via the standard key
                // (let the VR logic handle FUNC_SECONDARY)
                return USETIMER_STOP;
            }
        case WEAPON_MAULER:
        case WEAPON_CMP150:
        case WEAPON_K7AVENGER:
        case WEAPON_AR34:
        case WEAPON_FARSIGHT:
        case WEAPON_TIMEDMINE:
            // These weapons disallow B+Z
            if (!trigpressed) {
                if (VALIDWEAPON()) {
                    if (1 - FUNCISSEC()) {
                        SETFUNCSEC();
                    }
                    else {
                        SETFUNCPRI();
                    }
                }

                return USETIMER_STOP;
            }

            return USETIMER_CONTINUE;
        default:
            if (trigpressed) {
                g_Vars.currentplayer->gunctrl.invertgunfunc = true;
            }
            else {
                if (VALIDWEAPON()) {
                    if (!FUNCISSEC()) {
                        SETFUNCSEC();
                    }
                    else {
                        SETFUNCPRI();
                    }
                }
            }

            return USETIMER_STOP;
    }
}

void bgun0f0a8c50(void)
{
#ifndef PLATFORM_N64
    switch (bgunGetWeaponNum(HAND_RIGHT)) {
        case WEAPON_RCP120:
        case WEAPON_LAPTOPGUN:
        case WEAPON_DRAGON:
        case WEAPON_REMOTEMINE:
            if (PLAYER_EXTCFG().extcontrols) {
                return;
            }
            break;
    }
#endif
    if (g_Vars.currentplayer->hands[HAND_RIGHT].activatesecondary == false) {
        g_Vars.currentplayer->gunctrl.invertgunfunc = false;
    }
}

bool bgunIsUsingSecondaryFunction(void)
{
    struct player* player = g_Vars.currentplayer;
    s32 weaponnum = player->gunctrl.weaponnum;

    if (weaponnum >= WEAPON_UNARMED && weaponnum <= WEAPON_COMBATBOOST) {
        s32 index = (weaponnum - 1) >> 3;
        s32 value = 1 << ((weaponnum - 1) & 7);

        if (g_PlayerConfigsArray[g_Vars.currentplayerstats->mpindex].gunfuncs[index] & value) {
            if (player->gunctrl.invertgunfunc == true) {
                return false;
            }

            return true;
        }
    }

    if (player->gunctrl.invertgunfunc == true) {
        return true;
    }

    return false;
}





// Rotate a vector v into the local space of quaternion q
// = multiply by the conjugate (qw, -qx, -qy, -qz).
// v and q must be expressed in the same basis, or the result is meaningless.
static void worldToLocal(const float q[4], const float v[3], float out[3]) // VR
{
    float qw =  q[0], qx = -q[1], qy = -q[2], qz = -q[3];

    float tx = 2.0f * (qy * v[2] - qz * v[1]);
    float ty = 2.0f * (qz * v[0] - qx * v[2]);
    float tz = 2.0f * (qx * v[1] - qy * v[0]);

    out[0] = v[0] + qw * tx + qy * tz - qz * ty;
    out[1] = v[1] + qw * ty + qz * tx - qx * tz;
    out[2] = v[2] + qw * tz + qx * ty - qy * tx;
}



/**
 * Tick gun-related things during first-person gameplay.
 *
 * This function is not called during cutscenes.
 */
// VR fist swings. A swing starts when its thrust (toward the knuckles, controller -Y) passes half
// the punch threshold (1.5 m/s) or its slash (across them) half of 2.5 m/s, and ends when both fall
// back under; the punch trigger re-arms only then, so one swing gives at most one punch even when it
// hovers around a threshold. A swing that punched or was held back is a stroke: its end time and its
// direction at peak speed (play space) are kept for the return-stroke test.
//
// The PD-VRR punch log: a line at each punch start, hold-back or rejection, with the hand, the
// signed thrust and the slash at that moment, the controller-frame velocity, the direction against
// the head's forward and the vertical (cosines), the grip and the outcome (attacked, restarted,
// no attack when the idle tick did something else, held back after a holster switch, return stroke,
// open hand); the start lines are written after the hands tick, with the real outcome. A swing with
// none of these logs one line when it ends, with its peaks: under the thresholds, or hand busy (it
// waited for the last punch's hit and the hand never became free).
#define VR_FIST_THRUST 1.5f        // m/s toward the knuckles: a punch
#define VR_FIST_SLASH 2.5f         // m/s across the knuckles: a slap or hook
#define VR_FIST_HALF_THRUST 0.75f  // a swing is under way above half of either
#define VR_FIST_HALF_SLASH 1.25f
#define VR_FIST_RETURN60 24   // a stroke crossing the threshold this soon after the end of the last
                              // one, opposite to it, is its return
#define VR_FIST_RETURN_DOT -0.5f
#define VR_FIST_BACKSLASH 0.2f // a slash-only swing with thrust under -this x slash moves back

// VR combat knife (Task H2): the same swing tracking at lower speeds, a thrust or a slash in any
// direction across the blade. One slash per swing: it is due from the swing's threshold crossing
// until the swing ends (under half of both), so a hand still busy with the last slash (before its
// hit) or with the draw slashes as soon as it is free. No return-stroke test: a slash back is a
// swipe too. The "knife:" log lines follow the "punch:" ones.
#define VR_KNIFE_THRUST 1.0f       // m/s toward the blade's point: a stab
#define VR_KNIFE_SLASH 1.2f        // m/s across the blade: a slash
#define VR_KNIFE_HALF_THRUST 0.5f
#define VR_KNIFE_HALF_SLASH 0.6f
#define VR_KNIFE_LOGMISS 0.8f      // a missed knife swing is logged only above this x either threshold

// Per motion-loop hand: the fist was in an attack last tick, so it holds the clench once the attack
// ends (bgunFistGripTick)
static bool vr_fist_wasattacking[2];

static struct {
    bool active;     // a swing is under way (over half the thresholds)
    bool logged;     // this swing logged a start, hold-back or rejection
    bool waited;     // this swing waited for the last punch's hit
    bool stroke;     // this swing punched or was held back
    bool due;        // the knife: this swing crossed its threshold and has not slashed yet
    f32 peakspeed, peakthrust, peakback, peakslash; // thrust's forward and backward peaks
    f32 dir[3];      // play-space direction at peak speed
    f32 local[3];
    s32 grip;
    bool havelast;   // the last stroke, for the return-stroke test
    s32 lastend60;
    f32 lastdir[3];
    bool pending;    // a start to log after the hands tick
    bool pendingrestart;
    s32 pendingframe; // lvframenum of the start
    char pendingtext[160];
} g_VrFist[2];

static void bgunFistDirCosines(const f32 *dir, f32 *fwd, f32 *up)
{
    // The head's forward (-Z) in play space
    f32 qw = gRawHeadQ.w, qx = gRawHeadQ.x, qy = gRawHeadQ.y, qz = gRawHeadQ.z;
    f32 fx = -2.0f * (qx * qz + qw * qy);
    f32 fy = -2.0f * (qy * qz - qw * qx);
    f32 fz = -(1.0f - 2.0f * (qx * qx + qy * qy));

    *fwd = dir[0] * fx + dir[1] * fy + dir[2] * fz;
    *up = dir[1];
}

// The values at this moment, for a log line
static void bgunFistDescribe(char *buf, s32 len, s32 handnum, f32 thrust, f32 slash, const f32 *relvel,
        const f32 *localvel, s32 grip, bool knife)
{
    f32 speed = sqrtf(relvel[0] * relvel[0] + relvel[1] * relvel[1] + relvel[2] * relvel[2]);
    f32 dir[3] = { 0.0f, 0.0f, 0.0f };
    f32 fwd, up;

    if (speed > 0.0f) {
        dir[0] = relvel[0] / speed;
        dir[1] = relvel[1] / speed;
        dir[2] = relvel[2] / speed;
    }

    bgunFistDirCosines(dir, &fwd, &up);
    snprintf(buf, len, "%s: hand %c %.2f m/s, thrust %.2f of %.2f, slash %.2f of %.2f, local(%.2f %.2f %.2f),"
            " fwd %.2f up %.2f, grip %d", knife ? "knife" : "punch", handnum == HAND_RIGHT ? 'R' : 'L', speed,
            thrust, knife ? VR_KNIFE_THRUST : VR_FIST_THRUST, slash, knife ? VR_KNIFE_SLASH : VR_FIST_SLASH,
            localvel[0], localvel[1], localvel[2], fwd, up, grip);
}

static void bgunFistLog(s32 i, s32 handnum, f32 thrust, f32 slash, const f32 *relvel, const f32 *localvel,
        s32 grip, bool knife, const char *outcome)
{
    char buf[256];
    s32 n;

    bgunFistDescribe(buf, sizeof(buf), handnum, thrust, slash, relvel, localvel, grip, knife);
    n = strlen(buf);
    snprintf(buf + n, sizeof(buf) - n, ": %s", outcome);
    vrHolsterDiagLine(buf);
    g_VrFist[i].logged = true;
}

// Each tick, before the punch (or knife slash) decisions: the swing's start, peaks and end
static void bgunFistTrack(s32 i, s32 handnum, f32 thrust, f32 slash, const f32 *relvel, const f32 *localvel,
        s32 grip, bool knife)
{
    bool half = knife ? thrust > VR_KNIFE_HALF_THRUST || slash > VR_KNIFE_HALF_SLASH
        : thrust > VR_FIST_HALF_THRUST || slash > VR_FIST_HALF_SLASH;
    f32 speed = sqrtf(relvel[0] * relvel[0] + relvel[1] * relvel[1] + relvel[2] * relvel[2]);

    if (half) {
        if (!g_VrFist[i].active) {
            g_VrFist[i].active = true;
            g_VrFist[i].logged = false;
            g_VrFist[i].waited = false;
            g_VrFist[i].stroke = false;
            g_VrFist[i].due = false;
            g_VrFist[i].peakspeed = 0.0f;
            g_VrFist[i].peakthrust = 0.0f;
            g_VrFist[i].peakback = 0.0f;
            g_VrFist[i].peakslash = 0.0f;
        }

        if (thrust > g_VrFist[i].peakthrust) {
            g_VrFist[i].peakthrust = thrust;
        }

        if (thrust < g_VrFist[i].peakback) {
            g_VrFist[i].peakback = thrust;
        }

        if (slash > g_VrFist[i].peakslash) {
            g_VrFist[i].peakslash = slash;
        }

        if (speed > g_VrFist[i].peakspeed && speed > 0.0f) {
            g_VrFist[i].peakspeed = speed;
            g_VrFist[i].dir[0] = relvel[0] / speed;
            g_VrFist[i].dir[1] = relvel[1] / speed;
            g_VrFist[i].dir[2] = relvel[2] / speed;
            g_VrFist[i].local[0] = localvel[0];
            g_VrFist[i].local[1] = localvel[1];
            g_VrFist[i].local[2] = localvel[2];
            g_VrFist[i].grip = grip;
        }
    } else if (g_VrFist[i].active) {
        g_VrFist[i].active = false;
        g_VrFist[i].due = false;

        if (g_VrFist[i].stroke) {
            g_VrFist[i].havelast = true;
            g_VrFist[i].lastend60 = g_Vars.lvframe60;
            g_VrFist[i].lastdir[0] = g_VrFist[i].dir[0];
            g_VrFist[i].lastdir[1] = g_VrFist[i].dir[1];
            g_VrFist[i].lastdir[2] = g_VrFist[i].dir[2];
        }

        // A knife swing well under its thresholds is ordinary hand movement: not logged
        if (!g_VrFist[i].logged && (!knife || g_VrFist[i].waited
                || g_VrFist[i].peakthrust > VR_KNIFE_LOGMISS * VR_KNIFE_THRUST
                || g_VrFist[i].peakslash > VR_KNIFE_LOGMISS * VR_KNIFE_SLASH)) {
            f32 fwd, up;
            char buf[256];

            bgunFistDirCosines(g_VrFist[i].dir, &fwd, &up);
            snprintf(buf, sizeof(buf), "%s: hand %c swing peak %.2f m/s, thrust forward %.2f back %.2f of %.2f,"
                    " slash %.2f of %.2f, local(%.2f %.2f %.2f), fwd %.2f up %.2f, grip %d: %s",
                    knife ? "knife" : "punch", handnum == HAND_RIGHT ? 'R' : 'L', g_VrFist[i].peakspeed,
                    g_VrFist[i].peakthrust, g_VrFist[i].peakback, knife ? VR_KNIFE_THRUST : VR_FIST_THRUST,
                    g_VrFist[i].peakslash, knife ? VR_KNIFE_SLASH : VR_FIST_SLASH,
                    g_VrFist[i].local[0], g_VrFist[i].local[1], g_VrFist[i].local[2],
                    fwd, up, g_VrFist[i].grip, g_VrFist[i].waited ? "hand busy" : "under the thresholds");
            vrHolsterDiagLine(buf);
        }
    }
}

// A swing past the thresholds that is the hand coming back, not a punch: the pull-back after a
// punch reaches the slash threshold as the controller tilts (run 10's punch log). It is a return
// stroke when it crosses the threshold within VR_FIST_RETURN60 of the end of the hand's last stroke
// and points against it, or when only its slash passed and its thrust points back toward the
// wrist.
static const char *bgunFistReturnStroke(s32 i, f32 thrust, f32 slash, const f32 *relvel)
{
    f32 speed = sqrtf(relvel[0] * relvel[0] + relvel[1] * relvel[1] + relvel[2] * relvel[2]);

    s32 since = g_Vars.lvframe60 - g_VrFist[i].lastend60;

    if (g_VrFist[i].havelast && speed > 0.0f && since >= 0 && since < VR_FIST_RETURN60
            && (relvel[0] * g_VrFist[i].lastdir[0] + relvel[1] * g_VrFist[i].lastdir[1]
                + relvel[2] * g_VrFist[i].lastdir[2]) / speed < VR_FIST_RETURN_DOT) {
        return "return stroke (against the last stroke)";
    }

    if (thrust <= VR_FIST_THRUST && thrust < -VR_FIST_BACKSLASH * slash) {
        return "return stroke (slash moving back)";
    }

    return NULL;
}

// A new level or life (lvframe60 starts again from 0) and a gun side change (the hands' controllers
// swap): no swing, stroke or pending log carries over
void bgunFistReset(void)
{
    memset(g_VrFist, 0, sizeof(g_VrFist));
    vr_fist_wasattacking[0] = vr_fist_wasattacking[1] = false;
}

// After the hands tick: each punch start this tick, with what the idle tick made of it
static void bgunFistFlushLog(void)
{
    s32 i;

    for (i = 0; i < 2; i++) {
        if (g_VrFist[i].pending) {
            struct hand *hand = &g_Vars.currentplayer->hands[i == 0 ? HAND_RIGHT : HAND_LEFT];
            char buf[256];

            char later[32] = "";

            // A start in a tick with no 60ths: the hands tick (and so the outcome) came later
            if (g_Vars.lvframenum != g_VrFist[i].pendingframe) {
                snprintf(later, sizeof(later), " (seen %d frames later)", g_Vars.lvframenum - g_VrFist[i].pendingframe);
            }

            if (hand->state == HANDSTATE_ATTACK) {
                snprintf(buf, sizeof(buf), "%s: %s%s", g_VrFist[i].pendingtext,
                        g_VrFist[i].pendingrestart ? "restarted" : "attacked", later);
            } else {
                snprintf(buf, sizeof(buf), "%s: no attack (state %d/%d)%s", g_VrFist[i].pendingtext,
                        hand->state, hand->stateminor, later);
            }

            vrHolsterDiagLine(buf);
            g_VrFist[i].pending = false;
        }
    }
}

// VR fists: put the hand straight on the clench's end pose (RIGHTPUNCH's last frame, mirrored on the
// left) without playing it. The hand stays in its busy animation mode with no command list, so
// bgun0f09815c reads frame 0 and the draw (bgun0f0981e8) never takes it to idle, whose cached pose
// (shared by both hands) is the open hand. With VrMotionThrowing animLoadFrame always loads frame 29
// of anim 1002, so this is also the pose the clench and the motion punches draw on every frame.
static void bgunFistHoldClench(struct hand *hand)
{
    struct player *player = g_Vars.currentplayer;

    // The model must be the loaded fists: modelSetAnimation reads its definition
    if (!bgunIsLoaded() || hand->gunmodel.definition == NULL
            || hand->gunmodel.definition != player->gunctrl.gunmodeldef) {
        return;
    }

    bgunResetAnim(hand);
    modelSetAnimation(&hand->gunmodel, ANIM_GUN_UNARMED_RIGHTPUNCH, false, 0.0f,
            vr_hand_grip_anim[0].unk04 / 10000.0f, 0.0f);
    modelSetAnimFrame(&hand->gunmodel, modelGetAnimEndFrame(&hand->gunmodel));
    hand->animmode = HANDANIMMODE_BUSY;
}

// VR fists, each tick of a fist with its grip held (motion-loop hand i). A grip press plays the
// clench. Otherwise the hand holds the clench end pose (bgunFistHoldClench) wherever it would show
// the open idle pose:
// - a punch on its last frame: its attack ends here (bgunSetState idle, as its own end would in the
//   tick after), before the draw that would take the hand to idle;
// - an attack that ended another way (the 60-tick limit) with its punch still playing;
// - an idle hand with no animation: fists that arrive with the grip already held (a holster switch,
//   the pause menu), or a punch that went idle anyway.
// restart: this tick ended the last punch for a new one, which bgunTickHand starts next.
static void bgunFistGripTick(s32 i, s32 handnum, struct hand *hand, bool pressed, bool restart)
{
    if (hand->state == HANDSTATE_ATTACK) {
        if (hand->stateminor == HANDSTATEMINOR_ATTACK_MELEE_2
                && hand->animmode == HANDANIMMODE_BUSY
                && hand->animload < 0
                && hand->unk0ce8 != NULL
                && bgun0f09815c(hand) >= modelGetNumAnimFrames(&hand->gunmodel) - 1) {
            bgunSetState(handnum, HANDSTATE_IDLE);
            bgunFistHoldClench(hand);
        }
    } else if (pressed) {
        bgunStartAnimation(vr_hand_grip_anim, handnum, hand);
    } else if (hand->state == HANDSTATE_IDLE && !restart
            && (vr_fist_wasattacking[i] || (!bgunIsAnimBusy(hand) && hand->animload < 0))) {
        bgunFistHoldClench(hand);
    }

    // The clench stops on its last frame: with no command list the draw never takes it to idle
    if (hand->unk0ce8 == vr_hand_grip_anim && bgun0f09815c(hand) >= 28.00f) {
        hand->unk0ce8 = NULL;
    }
}

/**
 * VR motion attacks (pistol whip, knife slash) must never be started while the
 * hand is in the middle of a weapon change, a reload or a function change.
 *
 * During a switch, bgunTickSwitch2() already writes the NEW weapon number into
 * hand->gset.weaponnum while the hand state machine is still in
 * HANDSTATE_CHANGEGUN (LOAD -> RAISE -> EQUIP). If a swing is detected at that
 * moment, forcing HANDSTATE_ATTACK throws away the CHANGEGUN state: the new gun
 * never gets its HANDMODE_6 -> HANDMODE_7 -> EQUIP transition, so it is never
 * raised/displayed and the switching logic can no longer complete.
 */
static bool vrHandCanStartMotionAttack(s32 handnum, struct hand *hand)
{
    struct player *player = g_Vars.currentplayer;
    struct gunctrl *ctrl = &player->gunctrl;
    s32 i;

    if (!hand->inuse) {
        return false;
    }

    // A weapon switch is pending or the new gun model is still being loaded
    if (ctrl->switchtoweaponnum != -1 || ctrl->gunmemnew >= 0 || !bgunIsLoaded()) {
        return false;
    }

    // Do not interrupt a change/autoswitch on either hand
    for (i = 0; i < 2; i++) {
        s32 state = player->hands[i].state;

        if (state == HANDSTATE_CHANGEGUN || state == HANDSTATE_AUTOSWITCH) {
            return false;
        }
    }

    // Only allow from states where starting an attack is safe
    switch (hand->state) {
        case HANDSTATE_IDLE:
        case HANDSTATE_ATTACK:
        case HANDSTATE_ATTACKEMPTY:
            return true;
        default:
            // RELOAD, CHANGEFUNC, CHANGEGUN, AUTOSWITCH, ...
            return false;
    }
}

// VR: manual reload refuses a shot (vrReloadAllowFire: the clip out, a rack, slap or pump due, the
// gun dry) by keeping the trigger off, so the engine never reaches its empty state
// (bgunTickIncAttackEmpty) and its dry click, for every gun with a row. The click is played here
// instead: on the pull and every 25 ticks while it is held, as the empty state repeats it (Task F11d:
// the Shotgun needing its pump or out of shells). A hand the engine has just clicked (its own empty
// state, a gun run dry with the trigger held) waits the 25 ticks first. -1: no refused pull held
static s32 VrDryClickTimer60[2] = { -1, -1 };

static void vrDryTriggerTick(s32 handnum, bool refused)
{
    struct hand *hand = &g_Vars.currentplayer->hands[handnum];

    if (!refused || g_Vars.tickmode != TICKMODE_NORMAL || g_Vars.lvupdate240 <= 0) {
        if (!refused) {
            VrDryClickTimer60[handnum] = -1;
        }

        return;
    }

    if (hand->state == HANDSTATE_ATTACKEMPTY) {
        VrDryClickTimer60[handnum] = 0;
        return;
    }

    if (hand->state != HANDSTATE_IDLE) {
        return;
    }

    if (VrDryClickTimer60[handnum] < 0) {
        VrDryClickTimer60[handnum] = 0;
        bgunPlayEmptyClick(hand->gset.weaponnum);
        vrDryLog(handnum, hand, "click (refused pull)");
        return;
    }

    VrDryClickTimer60[handnum] += g_Vars.lvupdate60;

    if (VrDryClickTimer60[handnum] > TICKS(25)) {
        VrDryClickTimer60[handnum] -= TICKS(25);
        bgunPlayEmptyClick(hand->gset.weaponnum);
    }
}

void bgunTickGameplay(bool triggeron) {
    s32 gunsfiring[2] = {false, false};
    struct player *player = g_Vars.currentplayer;
    s32 i;


    // Remove weapons if in passive mode
    if (g_Vars.currentplayer->gunctrl.passivemode) {
        struct chrdata *chr = g_Vars.currentplayer->prop->chr;

        triggeron = false;

        if (invGetCount() > 1) {
            invClear();
            invGiveSingleWeapon(WEAPON_UNARMED);

        }

        if (g_Vars.currentplayer->gunctrl.weaponnum != WEAPON_UNARMED
            && g_Vars.currentplayer->gunctrl.switchtoweaponnum != WEAPON_UNARMED) {
            bgunEquipWeapon(WEAPON_UNARMED);
        }

        g_Vars.currentplayer->gunctrl.dualwielding = true; // VR dualwielding on start

        g_Vars.currentplayer->devicesactive = 0;

        chr->cloakpause = 0;
        chr->cloakfadefrac = 0;
        chr->cloakfadefinished = false;
        chr->hidden &= ~CHRHFLAG_CLOAKED;

    }

    // Remove throwable items from inventory if there's no more left
    for (i = 0; i < invGetCount(); i++) {
        struct weapon *weapon;
        s32 weaponnum = invGetWeaponNumByIndex(i);
        s32 equippedweaponnum;

        switch (weaponnum) {
            case WEAPON_COMBATKNIFE:
            case WEAPON_GRENADE:
            case WEAPON_NBOMB:
            case WEAPON_COMBATBOOST:
            case WEAPON_CLOAKINGDEVICE:
            case WEAPON_ECMMINE:
            case WEAPON_COMMSRIDER:
            case WEAPON_TRACERBUG:
            case WEAPON_TARGETAMPLIFIER:
                weapon = weaponFindById(weaponnum);

                if (weapon && weapon->ammos[0]
                    && bgunGetAmmoCount(weapon->ammos[0]->type) == 0) {
                    equippedweaponnum = bgunGetWeaponNum(HAND_RIGHT);
                    invRemoveItemByNum(weaponnum);

                    if (weaponnum == equippedweaponnum &&
                        !invHasSingleWeaponIncAllGuns(weaponnum)) {
                        invCalculateCurrentIndex();

                        // VR: with holsters a used-up throwable returns to the core's weapon
                        if (!vrHolsterEngineSwitch()) {
                            bgunEquipWeapon(invGetWeaponNumByIndex(g_Vars.currentplayer->equipcuritem));
                        }
                    }
                }
        }
    }

    if (g_Vars.tickmode == TICKMODE_CUTSCENE) {
        triggeron = false;
        g_Vars.currentplayer->hands[HAND_LEFT].firing = false;
        g_Vars.currentplayer->hands[HAND_RIGHT].firing = false;
    }



    // VR...
    static bool vr_hand_triggered[2] = {false, false};
    int handnums[2] = {HAND_RIGHT, HAND_LEFT};
    int handnum = 0;
    int ctrlIndex = 0;
    bool vr_in_motion_triggered = false;

    struct hand *hand = &player->hands[handnum];
    struct weaponfunc *func = gsetGetWeaponFunction(&hand->gset);

    if(hand->gset.weaponfunc == FUNC_SECONDARY){ // only for vr_input.cpp
        VR_FUNC_SECONDARY = true;
    }else{
        VR_FUNC_SECONDARY = false;
    }

    if(VrMotionThrowing) {
        vr_R_trigger = get_button_state(1, "trigger"); //  for knif
        vr_L_trigger = get_button_state(0, "trigger"); //  for knif
    }

    for (int i = 0; i < 2; i++) {
        handnum = handnums[i];
        hand = &player->hands[handnum];
        weaponnum = player->hands[handnum].gset.weaponnum;

        // VR: only a grip press seen while a fist plays the clench: fists that arrive with the grip
        // already held hold the clench at once (bgunFistGripTick)
        if (weaponnum != WEAPON_UNARMED) {
            *(handnum == HAND_RIGHT ? &vr_is_R_fist : &vr_is_L_fist) = false;
            vr_fist_wasattacking[i] = false;
        }

        ctrlIndex = (handnum == HAND_RIGHT ? 1 : 0);
        if (vrLaserOnOffHand(g_Vars.currentplayer->hands[HAND_RIGHT].gset.weaponnum)) {
            ctrlIndex = (handnum == HAND_RIGHT ? 0 : 1);
        }

        // Swing velocity in the controller's own frame, relative to the body.
        //
        // Both inputs are play space in raw OpenXR axes, so the conjugate
        // rotation actually lands in controller-local space. The old pairing of
        // gCtrlQuat with vr_ctrl_velocity did not: gCtrlQuat is head-relative
        // and carries the mirror plus the 90deg gun offset, so the player's head
        // yaw survived into the result and the gesture only ever fired while
        // facing the room's forward.
        //
        // Subtracting the head keeps this a measure of the hand moving relative
        // to the player rather than to the room, so walking does not punch.
        float relVel[3];
        float localVel[3];

        relVel[0] = vr_ctrl_velocity_play[ctrlIndex][0] - vr_head_velocity_play[0];
        relVel[1] = vr_ctrl_velocity_play[ctrlIndex][1] - vr_head_velocity_play[1];
        relVel[2] = vr_ctrl_velocity_play[ctrlIndex][2] - vr_head_velocity_play[2];

        worldToLocal(vr_ctrl_quat_play[ctrlIndex], relVel, localVel);
        vr_set_motion_triggered = false;

        if(VrMotionThrowing) {
            // --- Record the peak velocity for grenades, mines... ---
            // FUNC_PRIMARY: the laptop gun, the Dragon and the knife record no samples here (the
            // knife records its own at the release, below)
            if (vrMotionThrowWeapon(weaponnum, FUNC_PRIMARY) && triggeron) {

                vr_record_throw_sample(
                        ctrlIndex,
                        vr_ctrl_velocity_play[ctrlIndex][0],
                        vr_ctrl_velocity_play[ctrlIndex][1],
                        vr_ctrl_velocity_play[ctrlIndex][2]
                );

            }


            if (weaponnum == WEAPON_COMBATKNIFE && handnum == HAND_RIGHT && vr_R_trigger &&
                !vr_R_knife_sec_anim_run) {
                bgunSetState(HAND_RIGHT, HANDSTATE_IDLE);
                bgunStartAnimation(vr_knife_sec_anim, HAND_RIGHT, hand);
                vr_R_knife_sec_anim_run = true;
                vr_R_func_secondary_knife = true;
                g_VrFist[i].due = false; // the trigger's wind-up and throw: no slash carries over
            } else if (weaponnum == WEAPON_COMBATKNIFE && handnum == HAND_RIGHT && vr_R_trigger &&
                       vr_R_knife_sec_anim_run) {
                vr_R_func_secondary_knife = true;
                g_VrFist[i].due = false;
                f32 current_frame = bgun0f09815c(hand);
                if (hand->unk0ce8 == vr_knife_sec_anim && current_frame >= 46.00f) {
                    hand->unk0ce8 = NULL;
                }
                continue;
            } else if (weaponnum == WEAPON_COMBATKNIFE && handnum == HAND_RIGHT && !vr_R_trigger &&
                       vr_R_func_secondary_knife) {
                bgunStartAnimation(vr_knife_sec_anim_throw, HAND_RIGHT, hand);
                vr_throw_cancelled = true;
                vr_record_throw_sample(
                        ctrlIndex,
                        vr_ctrl_velocity_play[ctrlIndex][0],
                        vr_ctrl_velocity_play[ctrlIndex][1],
                        vr_ctrl_velocity_play[ctrlIndex][2]
                );
                vr_R_knife_sec_anim_run = false;
                vr_R_func_secondary_knife = false;
                g_VrFist[i].due = false;
                hand->gset.weaponfunc = FUNC_SECONDARY;
                bgunSetState(HAND_RIGHT, HANDSTATE_ATTACK);
                continue;
            }


            // VR: with holsters an item is never dual, so HAND_LEFT has the knife only as the copy
            // of the gun side's weapon number (bgunTickSwitch2 sets both hands'), not in use: the
            // free hand's trigger must not wind up and throw a knife from it
            bool leftknife = weaponnum == WEAPON_COMBATKNIFE && handnum == HAND_LEFT
                    && (!vrHolsterActive() || hand->inuse);

            if (leftknife && vr_L_trigger &&
                !vr_L_knife_sec_anim_run) {
                bgunSetState(HAND_LEFT, HANDSTATE_IDLE);
                bgunStartAnimation(vr_knife_sec_anim, HAND_LEFT, hand);
                vr_L_knife_sec_anim_run = true;
                vr_L_func_secondary_knife = true;
                g_VrFist[i].due = false;
            } else if (leftknife && vr_L_trigger &&
                       vr_L_knife_sec_anim_run) {
                vr_L_func_secondary_knife = true;
                g_VrFist[i].due = false;
                f32 current_frame = bgun0f09815c(hand);
                if (hand->unk0ce8 == vr_knife_sec_anim && current_frame >= 46.00f) {
                    hand->unk0ce8 = NULL;
                }
                continue;
            } else if (leftknife && !vr_L_trigger &&
                       vr_L_func_secondary_knife) {
                vr_throw_cancelled = true;
                vr_record_throw_sample(
                        ctrlIndex,
                        vr_ctrl_velocity_play[ctrlIndex][0],
                        vr_ctrl_velocity_play[ctrlIndex][1],
                        vr_ctrl_velocity_play[ctrlIndex][2]
                );
                vr_L_knife_sec_anim_run = false;
                vr_L_func_secondary_knife = false;
                g_VrFist[i].due = false;
                hand->gset.weaponfunc = FUNC_SECONDARY;
                bgunSetState(HAND_LEFT, HANDSTATE_ATTACK);
                continue;
            }


            // In the OpenXR grip pose the wrist-to-knuckles direction (along the
            // barrel) is -Y for both hands; left-handed mode only swaps which
            // controller feeds which hand. So a thrust is -Y, and a slash is
            // whatever is left in the perpendicular XZ plane. (VR: this was +Y,
            // so a punch fired on the pull-back, run 9's punch log.) Testing the
            // plane by magnitude keeps the knife's "any of four directions"
            // behaviour without depending on which way is "up".
            f32 thrust = -localVel[1];
            f32 slash  = sqrtf(localVel[0] * localVel[0] + localVel[2] * localVel[2]);
            bool vrknife = weaponnum == WEAPON_COMBATKNIFE && hand->gset.weaponfunc == FUNC_PRIMARY;
            s32 vrgrip = (handnum == HAND_RIGHT ? vr_button_R_grip : vr_button_L_grip) ? 1 : 0;

            if (vrknife) {
                // VR knife (Task H2): a stab or a slash in any direction across the blade, one per
                // swing, due until the swing ends (see VR_KNIFE_THRUST). Before this a slash was any tick
                // over 1 m/s on either, re-armed as soon as the speed dipped under it, and every one
                // restarted the attack from its first frame: a swing (and its return) restarted the
                // slash before its hit frame, so it rarely landed. A hand not in use (with holsters,
                // HAND_LEFT's copy of the gun side's knife) never slashes.
                if (!hand->inuse) {
                    vr_hand_triggered[i] = false;
                    continue;
                }

                bgunFistTrack(i, handnum, thrust, slash, relVel, localVel, vrgrip, true);

                if (!vr_hand_triggered[i] && (thrust > VR_KNIFE_THRUST || slash > VR_KNIFE_SLASH)) {
                    g_VrFist[i].due = true;
                }

                vr_set_motion_triggered = g_VrFist[i].due;
            } else if (weaponnum == WEAPON_UNARMED) {
                // Straight punch, or a hook thrown across the body.
                vr_set_motion_triggered = (thrust > VR_FIST_THRUST) || (slash > VR_FIST_SLASH);

            } else if (weaponnum == WEAPON_FALCON2 ||
                       weaponnum == WEAPON_FALCON2_SCOPE ||
                       weaponnum == WEAPON_FALCON2_SILENCER ||
                       weaponnum == WEAPON_DY357MAGNUM ||
                       weaponnum == WEAPON_DY357LX) {
                // VR: with holsters a gun never whips (a draw is a fast swing, and the melee is the
                // fists': let go and punch)
                if (vrHolsterFastSwitch()) {
                    vr_hand_triggered[i] = false;
                    continue;
                }

                // A pistol whip is a swing, not a thrust: the grip travels in an
                // arc about the wrist, so most of the speed lands perpendicular
                // to the barrel. Testing thrust alone missed nearly all of them.
                vr_set_motion_triggered = (thrust > 2.0f) || (slash > 2.0f);
            } else {
                vr_hand_triggered[i] = false;
                continue;
            }

            // Ignore swings while the hand is switching weapon / reloading, otherwise
            // bgunSetState(ATTACK) below overwrites HANDSTATE_CHANGEGUN and the new
            // weapon is never equipped (no weapon + switching locked).
            // The knife's slash stays due (g_VrFist due) and starts once the hand is free.
            if (vr_set_motion_triggered
                && weaponnum != WEAPON_UNARMED
                && !vrHandCanStartMotionAttack(handnum, hand)) {
                vr_set_motion_triggered = false;

                if (vrknife) {
                    g_VrFist[i].waited = true;
                }
            }


            // VR fists: the swing tracking (bgunFistTrack), then the punch decisions in order
            bool vrfist = weaponnum == WEAPON_UNARMED;
            bool vrbelowhalf = thrust < VR_FIST_HALF_THRUST && slash < VR_FIST_HALF_SLASH;
            bool vrpunchrestart = false;
            bool vrpunchwait = false;

            if (vrfist) {
                bgunFistTrack(i, handnum, thrust, slash, relVel, localVel, vrgrip, false);
            }

            if (vr_set_motion_triggered && !vr_hand_triggered[i] && vrfist) {
                const char *vrreturn;

                if (!vrgrip) {
                    // Only a closed fist attacks: an open hand (grip released) never punches or slaps
                    vr_hand_triggered[i] = true;
                    bgunFistLog(i, handnum, thrust, slash, relVel, localVel, vrgrip, false, "open hand");
                } else if (vrHolsterSwingSettling()) {
                    // Letting go of a holster gun gives fists at once, often while the hand is still
                    // moving into the holster, fast enough to punch. For half a second after a holster
                    // switch no fist punch starts: any swing then counts as already started, so one
                    // still fast at the end punches only after the hand slows and swings again.
                    vrHolsterNoteSwing(handnum, false);
                    vr_hand_triggered[i] = true;
                    g_VrFist[i].stroke = true;
                    bgunFistLog(i, handnum, thrust, slash, relVel, localVel, vrgrip, false,
                            "held back after a holster switch");
                } else if ((vrreturn = bgunFistReturnStroke(i, thrust, slash, relVel)) != NULL) {
                    vr_hand_triggered[i] = true;
                    bgunFistLog(i, handnum, thrust, slash, relVel, localVel, vrgrip, false, vrreturn);
                } else if (hand->state != HANDSTATE_IDLE) {
                    // A fist swing lands a new punch even while the last one plays. Once the last
                    // punch's hit is applied (melee stateminor 2, or 3 between a double punch's hits)
                    // its attack ends here as its own end would (bgunSetState idle), so this tick's
                    // trigger starts the new punch from its first frame. Before that hit, or in any
                    // other busy state, the swing's rising edge waits for the next tick while it stays
                    // over the thresholds: one swing still gives one punch.
                    if (hand->state == HANDSTATE_ATTACK
                        && (hand->stateminor == HANDSTATEMINOR_ATTACK_MELEE_2
                            || hand->stateminor == HANDSTATEMINOR_ATTACK_MELEE_3)) {
                        bgunResetAnim(hand);
                        bgunSetState(handnum, HANDSTATE_IDLE);
                        vrpunchrestart = true;
                    } else {
                        vrpunchwait = true;
                        g_VrFist[i].waited = true;
                    }
                }
            }

            if (vr_set_motion_triggered && !vr_hand_triggered[i] && vrknife) {
                if (vrHolsterSwingSettling()) {
                    // As the fists: a draw from the right hip is a fast swing. For half a second after
                    // a holster switch the swing counts as already started, so it gives no slash.
                    vrHolsterNoteSwing(handnum, false);
                    vr_hand_triggered[i] = true;
                    g_VrFist[i].due = false;
                    bgunFistLog(i, handnum, thrust, slash, relVel, localVel, vrgrip, true,
                            "held back after a holster switch");
                } else if (hand->state == HANDSTATE_ATTACK) {
                    // As the fists: once the last slash's hit is applied (melee stateminor 2, or 3
                    // before a second hit) the new slash starts from its first frame; before that hit
                    // the slash stays due and starts once it is applied, while the swing lasts.
                    if (hand->stateminor == HANDSTATEMINOR_ATTACK_MELEE_2
                            || hand->stateminor == HANDSTATEMINOR_ATTACK_MELEE_3) {
                        vrpunchrestart = true;
                    } else {
                        vrpunchwait = true;
                        g_VrFist[i].waited = true;
                    }
                }
            }

            if (vr_set_motion_triggered && !vr_hand_triggered[i] && !vrpunchwait) {
                vrHolsterNoteSwing(handnum, true);

                if (vrfist || vrknife) {
                    g_VrFist[i].stroke = true;
                    g_VrFist[i].pending = true;
                    g_VrFist[i].pendingrestart = vrpunchrestart;
                    g_VrFist[i].pendingframe = g_Vars.lvframenum;
                    g_VrFist[i].logged = true;
                    bgunFistDescribe(g_VrFist[i].pendingtext, sizeof(g_VrFist[i].pendingtext), handnum,
                            thrust, slash, relVel, localVel, vrgrip, vrknife);
                }

                if (vrknife) {
                    g_VrFist[i].due = false;
                    vr_in_motion_triggered = true;
                    // Cancel any ongoing animation and put the hand back to idle
                    bgunResetAnim(hand);
                    hand->animmode = HANDANIMMODE_IDLE;
                    // Trigger the attack directly, its animation from the stroke (Task H3b)
                    vr_knife_fromstroke[handnum] = true;
                    bgunSetState(handnum, HANDSTATE_ATTACK);

                }

                if (weaponnum == WEAPON_FALCON2 ||
                    weaponnum == WEAPON_FALCON2_SCOPE ||
                    weaponnum == WEAPON_FALCON2_SILENCER ||
                    weaponnum == WEAPON_DY357MAGNUM ||
                    weaponnum == WEAPON_DY357LX) {

                    vr_in_motion_triggered = true;
                    hand->gset.weaponfunc = FUNC_SECONDARY;
                    // Cancel any ongoing animation and put the hand back to idle
                    bgunResetAnim(hand);
                    hand->animmode = HANDANIMMODE_IDLE;
                    // Trigger the attack directly
                    bgunSetState(handnum, HANDSTATE_ATTACK);

                }

                if (handnum == HAND_RIGHT) {
                    gunsfiring[HAND_RIGHT] = player->hands[HAND_RIGHT].inuse;
                } else if (handnum == HAND_LEFT) {
                    gunsfiring[HAND_LEFT] = player->hands[HAND_LEFT].inuse;
                }
                vr_hand_triggered[i] = true;
            }

            if (vrfist) {
                // VR: a held grip shows a closed fist: the clench plays once when the grip closes,
                // and the hand holds its end pose between punches (bgunFistGripTick). Restarting
                // the clench every tick of a swing replaced the punch script (the hit waited for the
                // swing's end). A released grip opens the hand (the idle pose).
                bool *vrwasopen = handnum == HAND_RIGHT ? &vr_is_R_fist : &vr_is_L_fist;

                if (vrgrip) {
                    bgunFistGripTick(i, handnum, hand, *vrwasopen, vrpunchrestart);
                    *vrwasopen = false;
                } else if (!*vrwasopen) {
                    hand->animmode = HANDANIMMODE_IDLE;
                    *vrwasopen = true;
                }
            }


            vr_fist_wasattacking[i] = vrfist && hand->state == HANDSTATE_ATTACK;

            // VR: a fist or the knife re-arms only once the swing has ended (both under half the
            // thresholds), so jitter around a threshold does not punch or slash twice
            if (!vr_set_motion_triggered
                    && (vrknife ? !g_VrFist[i].active : (!vrfist || vrbelowhalf))) {
                vr_hand_triggered[i] = false;
            }

        }

    }


    // --- Read left controller get_button_state(0, "trigger") button state to fire --- VR
    // A parked gun (hidden while the other gun reloads) is not in the hand: its grip does not set
    // vr_button_L_grip (port/src/input.c), the left gun's grip for the crosshair
    if (player->hands[HAND_LEFT].inuse && !vrHandParked(HAND_LEFT)) {
        vr_leftHasWeapon = true;
    } else {
        vr_leftHasWeapon = false;
    }

    // Trigger Right controller
    bool rightTrig = triggeron;

    leftTrig = get_button_state(0, "trigger"); //  Trigger left controller

    bool leftTrigPressed = (leftTrig && !vr_prevLeftTrig);
    vr_prevLeftTrig = leftTrig;

    // Keep the "global" logic (useful for doautoselect / timers)
    bool anyTrig = rightTrig || leftTrig;

    player->playertriggerprev = player->playertriggeron;
    player->playertriggeron = anyTrig;

    if (!anyTrig && player->playertriggerprev) {
        // Releasing trigger (both)
        player->doautoselect = true;
    }

    if (player->playertriggeron) {
        player->playertrigtime240 += g_Vars.lvupdate240;
    }
    else {
        player->playertrigtime240 = 0;
    }


    if(VrMotionThrowing && player->hands[handnum].gset.weaponnum == WEAPON_UNARMED) {
        vr_grip_for_unarmed = true;

    }else if (weaponnum == WEAPON_COMBATKNIFE && (vr_R_trigger || vr_L_trigger)){
        // nothing
    }else if(!vr_in_motion_triggered){
        vr_grip_for_unarmed = false;
        // bypass the alternating behavior, map 1:1
        gunsfiring[HAND_RIGHT] = (rightTrig && player->hands[HAND_RIGHT].inuse);
        gunsfiring[HAND_LEFT] = (leftTrig && player->hands[HAND_LEFT].inuse);
    }else{
        vr_grip_for_unarmed = false;
    }


    // VR Remote mine: use the left trigger for the detonator
    // (activate the secondary function without going through B + right trigger)
    // With holsters the free hand's trigger does nothing while the gun side holds an item (its gun
    // is parked): a B tap detonates instead (vrHolsterBTapDetonates, bmoveProcessInput)
    if (hand->gset.weaponnum == WEAPON_REMOTEMINE && leftTrigPressed && !vrHolsterActive()) {
        g_Vars.currentplayer->gunctrl.invertgunfunc = true;
        g_Vars.currentplayer->hands[HAND_RIGHT].activatesecondary = true;
    }else if (hand->gset.weaponnum == WEAPON_REMOTEMINE && triggeron){
        g_Vars.currentplayer->gunctrl.invertgunfunc = false;

    }


    // A parked gun (hidden while the other gun reloads) does not fire. A park in this tick happens
    // after this (vrHolsterTick, in HAND_RIGHT's tick): bgunTickHand drops that gun's trigger.
    for (i = 0; i < 2; i++) {
        static bool vrdrypulled[2];

        if (gunsfiring[i] && !vrdrypulled[i]) {
            struct hand *h = &player->hands[i];
            struct weaponfunc *f = gsetGetWeaponFunction(&h->gset);

            if (f != NULL && f->ammoindex >= 0 && h->loadedammo[f->ammoindex] == 0) {
                vrDryLog(i, h, vrReloadAllowFire(i) ? (vrHandParked(i) ? "pull parked" : "pull allowed")
                        : "pull refused");
            }
        }

        vrdrypulled[i] = gunsfiring[i];
    }

    bgunSetTriggerOn(HAND_RIGHT, gunsfiring[HAND_RIGHT] && vrReloadAllowFire(HAND_RIGHT)
            && !vrHandParked(HAND_RIGHT));
    vrDryTriggerTick(HAND_RIGHT, gunsfiring[HAND_RIGHT] && !vrReloadAllowFire(HAND_RIGHT)
            && !vrHandParked(HAND_RIGHT));
    if (player->hands[HAND_LEFT].gset.weaponnum != WEAPON_REMOTEMINE) {
        bgunSetTriggerOn(HAND_LEFT, gunsfiring[HAND_LEFT] && vrReloadAllowFire(HAND_LEFT)
                && !vrHandParked(HAND_LEFT));
        vrDryTriggerTick(HAND_LEFT, gunsfiring[HAND_LEFT] && !vrReloadAllowFire(HAND_LEFT)
                && !vrHandParked(HAND_LEFT));
    }


    if (g_Vars.tickmode == TICKMODE_NORMAL && g_Vars.lvupdate240 > 0) {
        bgunTickHand(HAND_RIGHT);
        bgunTickHand(HAND_LEFT);
        bgunTickSwitch();
        bgunFistFlushLog(); // VR: PD-VRR, the punch starts of this tick with their outcome


        if (cheatIsActive(CHEAT_UNLIMITEDAMMONORELOADS)) {
            s32 i;
            struct weapon* weapon;
            struct hand* lhand = &g_Vars.currentplayer->hands[HAND_LEFT];
            struct hand* rhand = &g_Vars.currentplayer->hands[HAND_RIGHT];

            weapon = weaponFindById(rhand->gset.weaponnum);

            for (i = 0; i != 2; i++) {
                if (weapon && weapon->ammos[i] &&
                    bgunAmmotypeAllowsUnlimitedAmmo(weapon->ammos[i]->type)) {
                    rhand->loadedammo[i] = rhand->clipsizes[i];
                    lhand->loadedammo[i] = lhand->clipsizes[i];
                }
            }

            bgunGiveMaxAmmo(false);
        }
        else if (cheatIsActive(CHEAT_UNLIMITEDAMMO)) {
            bgunGiveMaxAmmo(false);
        }
    }

    bgunDecreaseNoiseRadius();

    if (player->resetshadecol) {
        propCalculateShadeColour(g_Vars.currentplayer->prop, player->gunshadecol, player->floorcol);
        player->resetshadecol = 0;
    }
    else {
        u8 shadecol[4];
        propCalculateShadeColour(g_Vars.currentplayer->prop, shadecol, player->floorcol);
        colourTween(player->gunshadecol, shadecol);
    }

    invIncrementHeldTime(bgunGetWeaponNum(HAND_RIGHT), bgunGetWeaponNum(HAND_LEFT));
}

void bgunSetPassiveMode(bool enable)
{
    s32 i;

    for (i = 0; i < PLAYERCOUNT(); i++) {
        g_Vars.players[i]->gunctrl.passivemode = enable;
    }
}

void bgunSetAimType(u32 aimtype)
{
    g_Vars.currentplayer->aimtype = aimtype;
}

void bgunSetAimPos(struct coord* coord)
{
    struct player* player = g_Vars.currentplayer;

    player->hands[HAND_RIGHT].aimpos.x = handGetXShift(HAND_RIGHT) + coord->x;
    player->hands[HAND_RIGHT].aimpos.y = coord->y;
    player->hands[HAND_RIGHT].aimpos.z = coord->z;

    player->hands[HAND_LEFT].aimpos.x = handGetXShift(HAND_LEFT) + coord->x;
    player->hands[HAND_LEFT].aimpos.y = coord->y;
    player->hands[HAND_LEFT].aimpos.z = coord->z;
}

void bgunSetHitPos(struct coord* coord)
{
    struct player* player = g_Vars.currentplayer;

    player->hands[HAND_LEFT].hitpos.x = player->hands[HAND_RIGHT].hitpos.x = coord->x;
    player->hands[HAND_LEFT].hitpos.y = player->hands[HAND_RIGHT].hitpos.y = coord->y;
    player->hands[HAND_LEFT].hitpos.z = player->hands[HAND_RIGHT].hitpos.z = coord->z;
}

void bgun0f0a9494(u32 operation)
{
    switch (operation) {
        case 0:
            g_Vars.currentplayer->hands[HAND_LEFT].hasdotinfo = g_Vars.currentplayer->hands[HAND_RIGHT].hasdotinfo = false;
            break;
        case 1:
            break;
    }
}

void bgun0f0a94d0(u32 operation, struct coord* pos, struct coord* rot)
{
    struct player* player = g_Vars.currentplayer;

    switch (operation) {
        case 0:
            if (pos->x > -100000.0f && pos->x < 100000.0f
                && pos->y > -100000.0f && pos->y < 100000.0f
                && pos->z > -100000.0f && pos->z < 100000.0f) {
                player->hands[HAND_RIGHT].hasdotinfo = true;
                player->hands[HAND_LEFT].hasdotinfo = true;

                player->hands[HAND_RIGHT].dotpos.x = pos->x;
                player->hands[HAND_RIGHT].dotpos.y = pos->y;
                player->hands[HAND_RIGHT].dotpos.z = pos->z;

                player->hands[HAND_RIGHT].dotrot.x = rot->x;
                player->hands[HAND_RIGHT].dotrot.y = rot->y;
                player->hands[HAND_RIGHT].dotrot.z = rot->z;
            }
            break;
        case 1:
            if (pos->x > -100000.0f && pos->x < 100000.0f
                && pos->y > -100000.0f && pos->y < 100000.0f
                && pos->z > -100000.0f && pos->z < 100000.0f) {
                player->hands[HAND_RIGHT].hasdotinfo = true;
                player->hands[HAND_LEFT].hasdotinfo = true;

                player->hands[HAND_LEFT].dotpos.x = pos->x;
                player->hands[HAND_LEFT].dotpos.y = pos->y;
                player->hands[HAND_LEFT].dotpos.z = pos->z;

                player->hands[HAND_LEFT].dotrot.x = rot->x;
                player->hands[HAND_LEFT].dotrot.y = rot->y;
                player->hands[HAND_LEFT].dotrot.z = rot->z;
            }
        case 2:
            //rien
            break;
    }
}

void bgunSetGunAmmoVisible(u32 reason, bool enable)
{
    if (enable) {
        g_Vars.currentplayer->gunammooff &= ~reason;
    }
    else {
        g_Vars.currentplayer->gunammooff |= reason;
    }
}

struct ammotype g_AmmoTypes[] = {
        { 0,            0, 0  },
        { 800,          0, 0  }, // AMMOTYPE_PISTOL
        { 800,          0, 0  }, // AMMOTYPE_SMG
        { 69,           0, 0  }, // AMMOTYPE_CROSSBOW
        { 400,          0, -2 }, // AMMOTYPE_RIFLE
        { 100,          0, 0  }, // AMMOTYPE_SHOTGUN
        { 100,          0, 0  }, // AMMOTYPE_FARSIGHT
        { 12,           0, 0  }, // AMMOTYPE_GRENADE
        { 3,            0, -2 }, // AMMOTYPE_ROCKET
        { 10,           0, 0  }, // AMMOTYPE_KNIFE
        { 200,          0, 0  }, // AMMOTYPE_MAGNUM
        { 40,           0, 0  }, // AMMOTYPE_DEVASTATOR
        { 10,           0, 1  }, // AMMOTYPE_REMOTE_MINE
        { 10,           0, 1  }, // AMMOTYPE_PROXY_MINE
        { 10,           0, 1  }, // AMMOTYPE_TIMED_MINE
        { 800,          0, 0  }, // AMMOTYPE_REAPER
        { 15,           0, -2 }, // AMMOTYPE_HOMINGROCKET
        { 50,           0, 0  }, // AMMOTYPE_DART
        { 10,           0, 0  }, // AMMOTYPE_NBOMB
        { 200,          0, 0  }, // AMMOTYPE_SEDATIVE
        { TICKS(18000), 0, 0  }, // AMMOTYPE_CLOAK
        { 4,            0, 0  }, // AMMOTYPE_BOOST
        { 200,          0, 0  }, // AMMOTYPE_PSYCHOSIS
        { 2,            0, 0  }, // AMMOTYPE_17
        { 10,           0, 0  }, // AMMOTYPE_BUG
        { 10,           0, 0  }, // AMMOTYPE_MICROCAMERA
        { 10,           0, 0  }, // AMMOTYPE_PLASTIQUE
        { 1000,         0, 0  }, // AMMOTYPE_1B
        { 10,           0, 0  }, // AMMOTYPE_1C
        { 50,           0, -1 }, // AMMOTYPE_1D
        { 1,            0, 0  }, // AMMOTYPE_TOKEN
        { 200,          0, 0  }, // AMMOTYPE_1F
        { 10,           0, 0  }, // AMMOTYPE_ECM_MINE
};

void bgunSetAmmoQuantity(s32 ammotype, s32 quantity)
{
    struct player* player = g_Vars.currentplayer;
    s32 weaponnum = bgunGetWeaponNum(HAND_RIGHT);
    s32 funcnum = -1;
    s32 magamount;

    // Check if this ammo type applies to the player's equipped weapon
    if (bgunGetAmmoTypeForWeapon(weaponnum, FUNC_PRIMARY) == ammotype) {
        funcnum = FUNC_PRIMARY;
    }

    if (bgunGetAmmoTypeForWeapon(weaponnum, FUNC_SECONDARY) == ammotype) {
        funcnum = FUNC_SECONDARY;
    }

#ifndef PLATFORM_N64
    // VR: the cloak held from the chest pouch has no clip (bgun0f0abd30): its charge is the reserve
    if (weaponnum == WEAPON_CLOAKINGDEVICE && vrHolsterActive()) {
        funcnum = -1;
    }
#endif

    if (funcnum != -1 && weaponHasAmmoFlag(weaponnum, funcnum, AMMOFLAG_NORESERVE)) {
        // For cloak and combat boost, ammo cannot be held outside of the weapon.
        // So just add it to the loaded clip.
        player->hands[0].loadedammo[funcnum] += quantity;

        if (player->hands[0].loadedammo[funcnum] > player->hands[0].clipsizes[funcnum]) {
            player->hands[0].loadedammo[funcnum] = player->hands[0].clipsizes[funcnum];
        }

        player->ammoheldarr[ammotype] = 0;
        return;
    }

    magamount = 0;

    // For throwable items, the capacity applies to reserve + loaded
    if (funcnum != -1 && weaponHasAmmoFlag(weaponnum, funcnum, AMMOFLAG_EQUIPPEDISRESERVE)) {
        magamount = player->hands[0].loadedammo[funcnum] + player->hands[1].loadedammo[funcnum];
    }

    if (quantity > g_AmmoTypes[ammotype].capacity - magamount) {
        player->ammoheldarr[ammotype] = g_AmmoTypes[ammotype].capacity - magamount;
    }
    else {
        player->ammoheldarr[ammotype] = quantity;
    }
}

s32 bgunGetReservedAmmoCount(s32 ammotype)
{
    s32 i;
    s32 j;
    s32 total = g_Vars.currentplayer->ammoheldarr[ammotype];
    struct player* player = g_Vars.currentplayer;

    for (i = 0; i < 2; i++) {
        if (player->hands[i].inuse) {
            for (j = 0; j < 2; j++) {
                if (player->gunctrl.ammotypes[j] == ammotype && weaponHasAmmoFlag(player->hands[i].gset.weaponnum, j, AMMOFLAG_NORESERVE)) {
                    total = total + player->hands[i].loadedammo[j];
                }
            }
        }
    }

    return total;
}

s32 bgunGetAmmoCount(s32 ammotype)
{
    s32 i;
    s32 j;
    s32 total = g_Vars.currentplayer->ammoheldarr[ammotype];
    struct player* player = g_Vars.currentplayer;

    for (i = 0; i < 2; i++) {
        if (player->hands[i].inuse) {
            for (j = 0; j < 2; j++) {
                if (player->gunctrl.ammotypes[j] == ammotype) {
                    total = total + player->hands[i].loadedammo[j];
                }
            }
        }
    }

    return total;
}

s32 bgunGetCapacityByAmmotype(s32 ammotype)
{
    return g_AmmoTypes[ammotype].capacity;
}

bool bgunAmmotypeAllowsUnlimitedAmmo(u32 ammotype)
{
    switch (ammotype) {
        case AMMOTYPE_REMOTE_MINE:
            if (g_Vars.stagenum == STAGE_CHICAGO) {
                return false;
            }
            break;
        case AMMOTYPE_TIMED_MINE:
            if (g_Vars.stagenum == STAGE_AIRFORCEONE) {
                return false;
            }
            break;
        case AMMOTYPE_PSYCHOSIS:
        case AMMOTYPE_17:
        case AMMOTYPE_BUG:
        case AMMOTYPE_MICROCAMERA:
        case AMMOTYPE_PLASTIQUE:
        case AMMOTYPE_1B:
        case AMMOTYPE_1C:
        case AMMOTYPE_1D:
        case AMMOTYPE_TOKEN:
        case AMMOTYPE_1F:
        case AMMOTYPE_ECM_MINE:
            return false;
    }

    return true;
}

void bgunGiveMaxAmmo(bool force)
{
    s32 i;

    for (i = 0; i < ARRAYCOUNT(g_AmmoTypes); i++) {
        bool give = true;

        if (!force) {
            give = bgunAmmotypeAllowsUnlimitedAmmo(i);
        }

        if (give) {
            bgunSetAmmoQuantity(i, g_AmmoTypes[i].capacity);
        }
    }
}

u32 bgunGetAmmoTypeForWeapon(u32 weaponnum, u32 func)
{
    struct weapon* weapon = weaponFindById(weaponnum);

    if (!weapon) {
        return 0;
    }

    if (!weapon->ammos[func]) {
        return 0;
    }

    return weapon->ammos[func]->type;
}

s32 bgunGetAmmoQtyForWeapon(u32 weaponnum, u32 func)
{
    struct weapon* weapon = weaponFindById(weaponnum);

    if (weapon) {
        struct inventory_ammo* ammo = weapon->ammos[func];

        if (ammo) {
            return bgunGetReservedAmmoCount(ammo->type);
        }
    }

    return 0;
}

void bgunSetAmmoQtyForWeapon(u32 weaponnum, u32 func, u32 quantity)
{
    struct weapon* weapon = weaponFindById(weaponnum);

    if (weapon) {
        struct inventory_ammo* ammo = weapon->ammos[func];

        if (ammo) {
            bgunSetAmmoQuantity(ammo->type, quantity);
        }
    }
}

s32 bgunGetAmmoCapacityForWeapon(s32 weaponnum, s32 func)
{
    struct weapon* weapon = weaponFindById(weaponnum);
    struct inventory_ammo* ammo = weapon->ammos[func];

    if (ammo) {
        return g_AmmoTypes[ammo->type].capacity;
    }

    return 0;
}

Gfx* bgunDrawHudString(Gfx * gdl, char* text, s32 x, bool halign, s32 y, s32 valign, u32 colour)
{
    s32 x1 = 0;
    s32 y1 = 0;
    s32 x2 = 0;
    s32 y2 = 0;
    s32 textheight;
    s32 textwidth;

    textwidth = 0;
    textheight = 0;

#if VERSION >= VERSION_JPN_FINAL
    textMeasure(&textheight, &textwidth, text, g_CharsNumeric, g_FontNumeric, -1);
#else
    textMeasure(&textheight, &textwidth, text, g_CharsNumeric, g_FontNumeric, 0);
#endif

    if (halign == HUDHALIGN_LEFT) { // left
        x2 = x + textwidth;
        x1 = x;
    }
    else if (halign == HUDHALIGN_RIGHT) { // right
        x1 = x - textwidth;
        x2 = x;
    }
    else if (halign == HUDHALIGN_MIDDLE) { // middle
        x2 = x + textwidth / 2;
        x1 = x2 - textwidth;
    }

    if (valign == HUDVALIGN_TOP) { // top
        y2 = y + textheight;
        y1 = y;
    }
    else if (valign == HUDVALIGN_BOTTOM) { // bottom
        y1 = y - textheight;
        y2 = y;
    }
    else if (valign == HUDVALIGN_MIDDLE) { // middle
        y2 = y + textheight / 2;
        y1 = y2 - textheight;
    }


    gdl = text0f153858(gdl, &x1, &y1, &x2, &y2);
    gdl = textRender(gdl, &x1, &y1, text, g_CharsNumeric, g_FontNumeric, colour, 0x000000a0, viGetWidth() , viGetHeight(), 0, 0);

    return gdl;
}

Gfx* bgunDrawHudInteger(Gfx * gdl, s32 value, s32 x, bool halign, s32 y, s32 valign, u32 colour)
{
    char buffer[12];

    sprintf(buffer, "%d\n", value);
    gdl = bgunDrawHudString(gdl, buffer, x, halign, y, valign, colour);

    return gdl;
}

void bgunResetAbmag(struct abmag* abmag)
{
    abmag->loadedammo = 0;
    abmag->change = 0;
    abmag->ref = 0;
    abmag->timer60 = 0;
}

void bgun0f0a9da8(struct abmag* mag, s32 remaining, s32 capacity, s32 height)
{
    s32 newchange;

    if (capacity > 20) {
        s32 newremaining = height * remaining / capacity;

        if (remaining > 0 && newremaining < 1) {
            newremaining = 1;
        }

        capacity = height;

        if (newremaining == mag->ref && mag->loadedammo > remaining) {
            mag->ref++;
        }

        mag->loadedammo = remaining;
        remaining = newremaining;
    }

    newchange = remaining - mag->ref;

    if ((mag->change < 0 && newchange > 0) || (mag->change > 0 && newchange < 0)) {
        mag->ref += mag->change;
        mag->change = 0;
        mag->timer60 = 0;

        newchange = remaining - mag->ref;
    }

    if (mag->change < 0 && mag->change > newchange) {
        if (mag->timer60 > -mag->change * TICKS(64)) {
            mag->timer60 = -mag->change * TICKS(64);
        }
    }

    mag->change = newchange;

    if (mag->change > 0) {
        height = capacity;

        if (capacity < 6) {
            height = 6;
        }
    }
    else {
        height = 8;

        if (mag->change < -3) {
            height += -mag->change * 2;
        }
    }

    if (mag->change != 0) {
        mag->timer60 += (s16)g_Vars.lvupdate60 * height;

        if (mag->timer60 > TICKS(255)) {
            if (mag->change > 0) {
                while (mag->timer60 > TICKS(255) && mag->change > 0) {
                    mag->change--;
                    mag->ref++;
                    mag->timer60 -= TICKS(64);
                }
            }
            else {
                while (mag->timer60 > TICKS(255) && mag->change < 0) {
                    mag->change++;
                    mag->ref--;
                    mag->timer60 -= TICKS(64);
                }
            }
        }
    }
    else {
        mag->timer60 = 0;
    }
}

/**
 * Render an ammo gauge on the HUD.
 *
 * Ammo gauges can be displayed in two ways. If the capacity is less than 20,
 * each bullet is displayed as a separate block with a gap between each. If the
 * capacity is 20 or more, a single block covers the whole gauge and the block
 * is partitioned into two (filled and empty).
 *
 * For the separated mode, a unit refers to a single bullet/block.
 * For the merged mode, a unit refers to a single 1px high line in the gauge.
 */
Gfx *bgunDrawHudGauge(Gfx *gdl, s32 x1, s32 y1, s32 x2, s32 y2, struct abmag *abmag, s32 remaining, s32 capacity, u32 emptycolour, u32 filledcolour, bool flip)
{
    s32 gaugeheight = y2 - y1;
    s32 unitheight;
    s32 remainder1;
    s32 remainder2;
    s32 gaugetop;
    f32 ref;
    s32 numunits = capacity;
    s32 i;


    bgun0f0a9da8(abmag, remaining, numunits, gaugeheight);

    if (numunits > 20) {
        // Use a single merged bar
        ref = abmag->ref;
        unitheight = 1;
        numunits = gaugeheight;
        gaugetop = y2 - gaugeheight;
    } else {
        // Use a separate block for each bullet
        ref = abmag->ref;
        unitheight = gaugeheight / numunits;
        remainder1 = unitheight * numunits - gaugeheight;
        remainder2 = (unitheight + 1) * numunits - gaugeheight;

        if (remainder1 < 0) {
            remainder1 = -remainder1;
        }

        if (remainder2 < 0) {
            remainder2 = -remainder2;
        }

        if (remainder2 < remainder1) {
            unitheight++;
        }

        gaugetop = y2 - unitheight * capacity + 1;

        if (unitheight <= 2) {
            gaugetop--;
        }
    }

    if (unitheight == 0) {
        /**
         * Using separate blocks, but the clip capacity is more than the gauge
         * height meaning each block is less than 1px. This is impossible
         * because the gauge switches modes away from separate blocks at 20,
         * therefore this code is unreachable.
         *
         * This code renders the gauge in the merged style, but uses 1px per
         * bullet and truncates the gauge at the gaugetop if needed. This is
         * clearly an early revision of the code, as it is visually misleading
         * and also lacks the transition effect.
         */
        s32 partitiony;
        s32 tmp;

        gaugeheight = y2 - gaugetop;
        partitiony = y2 - gaugeheight * ref / numunits;
        tmp = y2;

        if (partitiony > gaugetop) {
            // Render empty partition
            gdl = textSetPrimColour(gdl, emptycolour);

            if (flip) {
                gDPFillRectangleScaled(gdl++, x1, y2 - partitiony + y1, x2, gaugeheight + y1);
            } else {
                gDPFillRectangleScaled(gdl++, x1, gaugetop, x2, partitiony);
            }

            gdl = text0f153838(gdl);
        }

        // Render filled partition
        gdl = textSetPrimColour(gdl, filledcolour);

        if (flip) {
            gDPFillRectangleScaled(gdl++, x1, y2 - tmp + y1, x2, y2 - partitiony + y1);
        } else {
            gDPFillRectangleScaled(gdl++, x1, partitiony, x2, y2);
        }
    } else {
        u32 colour;
        s32 unittop;
        s32 unitbottom;

        gdl = textSetPrimColour(gdl, emptycolour);

        unittop = gaugetop;
        unitbottom = -1;

        for (i = 0; i < numunits; i++) {
            bool newstate = false;
            u32 weight;

            if (1);

            if (abmag->change > 0) {
                // Loading or reloading
                if (i >= numunits - (s32)ref - abmag->change && i < numunits - (s32)ref) {
                    // Unit is potentially unsettled
                    s32 fadeamount = abmag->timer60 - (numunits - (s32)ref - i - 1) * TICKS(64);

                    if (fadeamount >= 0) {
                        if (fadeamount >= TICKS(64)) {
                            // Unit is transitioning to filled
#if PAL
                            weight = (fadeamount * 4 - TICKS(250)) / 3;
#else
                            weight = (fadeamount * 4 - TICKS(252)) / 3;
#endif
                            weight = PALUP(weight);

                            if (weight > 255) {
                                weight = 255;
                            }

                            colour = colourBlend(filledcolour, 0xffffffbf, weight);
                        } else {
                            // Unit is bright and has not started transitioning to filled yet
                            weight = fadeamount * 4;
                            weight = PALUP(weight);

#if VERSION >= VERSION_PAL_BETA
                            if (weight > 255) {
								weight = 255;
							}
#endif

                            colour = colourBlend(0xffffffbf, emptycolour, weight);
                        }

                        newstate = true;
                    }
                }
            } else if (abmag->change < 0) {
                // Firing
                if (i < numunits - (s32)ref - abmag->change && i >= numunits - (s32) ref) {
                    s32 fadeamount = abmag->timer60 - (i - numunits + (s32) ref) * TICKS(64);

                    if (fadeamount >= 0) {
                        weight = PALUP(fadeamount);

                        if (weight > 255) {
                            colour = emptycolour;
                        } else {
                            // Unit was recently emptied
                            colour = colourBlend(emptycolour, filledcolour | 0xff, weight);
                        }

                        newstate = true;
                    }
                }
            }

            // Special case for units which are one after the last one being
            // faded. I think their colour is calculated incorrectly by the code
            // above and this is resetting them to the normal filled colour.
            if (abmag->change < 0) {
                // Firing
                if (i == numunits - (s32) ref - abmag->change) {
                    colour = filledcolour;
                    newstate = true;
                }
            } else {
                if (i == numunits - (s32) ref) {
                    colour = filledcolour;
                    newstate = true;
                }
            }

            // Calculate unittop and unitbottom. For merged gauges keep unittop
            // as it is if possible, so the empty and filled partitions can be
            // drawn whenever the state is changed in order to save gfx calls.
            if (unitheight <= 2) {
                if (newstate) {
                    if (unitbottom >= 0) {
                        // Render empty or transitioning unit of merged gauge
                        if (flip) {
                            gDPFillRectangleScaled(gdl++, x1, y2 - unitbottom + y1, x2, y2 - unittop + y1);
                        } else {
                            gDPFillRectangleScaled(gdl++, x1, unittop, x2, unitbottom);
                        }
                    }

                    unittop = gaugetop + i * unitheight;
                }

                unitbottom = gaugetop + i * unitheight + unitheight;
            } else {
                // Separate blocks - reduce unitbottom by 1 to make a gap
                unittop = gaugetop + i * unitheight;
                unitbottom = gaugetop + i * unitheight + unitheight - 1;
            }

            if (newstate) {
                gDPSetPrimColorViaWord(gdl++, 0, 0, colour);
            }

            // For separate blocks, clip the unit bottom to the bottom of the gauge
            if (unitbottom >= y2 - 1 && unitheight >= 2) {
                unitbottom = y2;
            }

            // Render separated blocks
            if (unitheight >= 3) {
                if (flip) {
                    gDPFillRectangleScaled(gdl++, x1, y2 - unitbottom + y1, x2, y2 - unittop + y1);
                } else {
                    gDPFillRectangleScaled(gdl++, x1, unittop, x2, unitbottom);
                }
            }
        } // end loop

        // For merged gauges, render the final partition
        if (unitheight <= 2) {
            s32 stack;

            if (flip) {
                gDPFillRectangleScaled(gdl++, x1, y2 - unitbottom + y1, x2, y2 - unittop + y1);
            } else {
                gDPFillRectangleScaled(gdl++, x1, unittop, x2, unitbottom);
            }
        }
    }

    gdl = text0f153838(gdl);

    gDPSetRenderMode(gdl++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);

    return gdl;
}


Gfx *bgunDrawHud(Gfx *gdl)
{

    struct player *player = g_Vars.currentplayer;
    s32 bottom = viGetViewTop() + viGetViewHeight()  -200; // VR
    s32 playercount = PLAYERCOUNT();
    s32 playernum = g_Vars.currentplayernum;
    struct gunctrl *ctrl;
    s32 secs60;
    s32 speedpilltime;
    s32 ammoindex = 0;
    s32 barwidth = 9;
    s32 reserveheight = 36;
    s32 clipheight = 57;
    s32 xpos;
    struct weapon *weapon = weaponFindById(player->gunctrl.weaponnum);
    u32 alpha;
    u32 fncolour;
    s32 funcnum;
    s32 fnfaderinc;
#if VERSION >= VERSION_NTSC_1_0
    s32 tmpfuncnum;
    struct handweaponinfo info;
#endif
    struct hand *hand = &player->hands[HAND_RIGHT];
    char *str;
    u32 colour;
    s32 x;
    s32 y;
    s32 textheight;
    s32 textwidth;
    struct weaponfunc *func;
    u16 nameid;
    struct hand *lefthand = &player->hands[HAND_LEFT];

    ctrl = &player->gunctrl;

    gDPNoOpTag(gdl++, VR_WEP_HUD_CAPTURE_BEGIN_R);

    if (player->isdead) {
        gDPNoOpTag(gdl++, VR_WEP_HUD_CAPTURE_END_L);
        gDPNoOpTag(gdl++, VR_WEP_HUD_CAPTURE_END_R);
        return gdl;
    }

    if (g_Vars.currentplayer->gunctrl.passivemode) {
        gDPNoOpTag(gdl++, VR_WEP_HUD_CAPTURE_END_L);
        gDPNoOpTag(gdl++, VR_WEP_HUD_CAPTURE_END_R);
        return gdl;
    }

    if (g_Vars.lvframenum < 5) {
        gDPNoOpTag(gdl++, VR_WEP_HUD_CAPTURE_END_L);
        gDPNoOpTag(gdl++, VR_WEP_HUD_CAPTURE_END_R);
        return gdl;
    }


#if PAL
    g_ScaleX = 1;
#else
    g_ScaleX = g_ViRes == VIRES_HI ? 2 : 1;
#endif

    gdl = text0f153628(gdl);

#ifndef PLATFORM_N64
    if (playercount < 2 || (playercount == 2 && optionsGetScreenSplit() == SCREENSPLIT_HORIZONTAL)) {
        gSPExtraGeometryModeEXT(gdl++, G_ASPECT_MODE_EXT, g_HudAlignModeR);
    }
#endif

    if (playercount >= 2) {
        barwidth = 5;
        reserveheight = 26;
        clipheight = 47;

        if (playercount == 2) {
            if (IS4MB() || (optionsGetScreenSplit() != SCREENSPLIT_VERTICAL && playernum == 0)) {
                bottom += 10;
            } else {
                bottom += 2;
            }
        } else if (playercount >= 3) {
            if (playernum < 2) {
                bottom += 10;
            } else {
                bottom += 2;
            }
        }
    } else if (optionsGetEffectiveScreenSize() != SCREENSIZE_FULL) {
        bottom += 8;
    }

    fncolour = 0xff000040;
    funcnum = hand->gset.weaponfunc;
    fnfaderinc = PALUP(g_Vars.lvupdate240 * 2);

#if VERSION >= VERSION_NTSC_1_0
    bgunGetWeaponInfo(&info, HAND_RIGHT);
    tmpfuncnum = bgunIsUsingSecondaryFunction();

    if (bgun0f098ca0(tmpfuncnum, &info, hand) >= 0) {
        funcnum = tmpfuncnum;
    }
#endif

    xpos = (viGetViewLeft() + viGetViewWidth()) / g_ScaleX - barwidth - 230;  // VR

    if (playercount == 2 && (optionsGetScreenSplit() == SCREENSPLIT_VERTICAL || IS4MB()) && playernum == 0) {
        xpos += 15;
    } else if (playercount >= 3 && (playernum % 2) == 0) {
        xpos += 15;
    }

    // Draw function square
    if (funcnum == FUNC_SECONDARY && ctrl->fnfader < 255) {
        if (ctrl->fnfader < 128) {
            ctrl->fnfader = 128;
        }

        if (ctrl->fnfader + fnfaderinc > 255) {
            ctrl->fnfader = 255;
        } else {
            ctrl->fnfader += fnfaderinc;
        }
    }

    if (funcnum == FUNC_PRIMARY && ctrl->fnfader > 0) {
        if (ctrl->fnfader - fnfaderinc < 0) {
            ctrl->fnfader = 0;
        } else {
            ctrl->fnfader -= fnfaderinc;
        }
    }

    if (ctrl->fnfader > 128) {
        fncolour = ((ctrl->fnfader * 2) - 256) << 16 | 0xff000040;
    }


    gDPPipeSync(gdl++);
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColorViaWord(gdl++, 0, 0, fncolour);

    gDPFillRectangleScaled(gdl++, xpos - 13, bottom - 11, xpos - 2, bottom);


    gdl = text0f153838(gdl);

    // Draw weapon name and function name
    if (optionsGetShowGunFunction(g_Vars.currentplayerstats->mpindex)) {
#if VERSION >= VERSION_NTSC_1_0
        func = weaponGetFunctionById(hand->gset.weaponnum, funcnum);
#else
        func = weaponGetFunctionById(hand->gset.weaponnum, hand->gset.weaponfunc);
#endif
        nameid = invGetNameIdByIndex(invGetCurrentIndex());
        str = langGet(nameid);

        if (ctrl->curgunstr != nameid) {
            ctrl->guntypetimer = 0;
            ctrl->curgunstr = nameid;
        }

        if (ctrl->guntypetimer < 255) {
            colour = 0x55ffffff;

            if (ctrl->guntypetimer);

            if (ctrl->guntypetimer + g_Vars.lvupdate60 > 255) {
                ctrl->guntypetimer = 255;
            } else {
                ctrl->guntypetimer += (u16) g_Vars.lvupdate60;
            }

            textMeasure(&textheight, &textwidth, str, g_CharsHandelGothicXs, g_FontHandelGothicXs, 0);
            textwidth += 2;

            if (textwidth > ctrl->guntypetimer * 3) {
                textwidth = ctrl->guntypetimer * 3;
            }

            if (playercount >= 2) {
                x = xpos - textwidth - 13;
            } else {
                x = xpos - textwidth - 2;
            }

#if VERSION == VERSION_JPN_FINAL
            y = bottom - textheight - 10;
#else
            y = bottom - textheight - 15;
#endif

            if (ctrl->guntypetimer > 192) {
                alpha = 255 - (ctrl->guntypetimer - 192) * 255 / 63U;
                colour = (colour & 0xffffff00) | alpha;
                if (0xffffff00);
            }

            gdl = textSetPrimColour(gdl, 0);

            gDPFillRectangleScaled(gdl++, x - 1, y - 1, xpos - 11, bottom);

            gdl = text0f153838(gdl);
            textSetWaveBlend(g_20SecIntervalFrac * 50.0f, 0, 50);
            textSetWaveColours(0xffffffff, 0xffffffff);

            gdl = textRenderProjected(gdl, &x, &y, str, g_CharsHandelGothicXs, g_FontHandelGothicXs, colour, textwidth, 1000, 0, 0);
            textResetBlends();
        }

        if (func) {
            langGet(func->name);

            colour = 0xff5555ff;

            if ((ctrl->curfnstr != func->name && ctrl->fnfader > 128) || ctrl->curfnstr == 0) {
                ctrl->fnstrtimer = 0;
                ctrl->curfnstr = func->name;
            }

            str = langGet(ctrl->curfnstr);

            if (ctrl->fnstrtimer < 255) {
                if (ctrl->fnstrtimer + g_Vars.lvupdate60 > 255) {
                    ctrl->fnstrtimer = 255;
                } else {
                    ctrl->fnstrtimer += (u16) g_Vars.lvupdate60;
                }

#if VERSION >= VERSION_NTSC_1_0
                if (funcnum == FUNC_SECONDARY && func->name == ctrl->curfnstr) {
                    colour |= 0x00ff0000;
                }

                if (funcnum == FUNC_PRIMARY && func->name != ctrl->curfnstr) {
                    colour |= 0x00ff0000;
                }
#else
                if (hand->gset.weaponfunc == FUNC_SECONDARY && func->name == ctrl->curfnstr) {
					colour |= 0x00ff0000;
				}

				if (hand->gset.weaponfunc == FUNC_PRIMARY && func->name != ctrl->curfnstr) {
					colour |= 0x00ff0000;
				}
#endif

                textMeasure(&textheight, &textwidth, str, g_CharsHandelGothicXs, g_FontHandelGothicXs, 0);
                textwidth += 2;

                if (textwidth > ctrl->fnstrtimer * 3) {
                    textwidth = ctrl->fnstrtimer * 3;
                }

                x = xpos - textwidth - 13;
#if VERSION == VERSION_JPN_FINAL
                y = bottom - textheight + 3;
#else
                y = bottom - textheight - 1;
#endif

                if (ctrl->fnstrtimer > 192) {
                    alpha = 255 - (ctrl->fnstrtimer - 192) * 255 / 63U;
                    colour = (colour & 0xffffff00) | alpha;
                }

                gdl = textSetPrimColour(gdl, 0);

                gDPFillRectangleScaled(gdl++, x - 1, y - 1, xpos - 11, bottom + 3);
                gdl = text0f153838(gdl);

                textSetWaveBlend(g_20SecIntervalFrac * 50.0f, 0, 50);
                textSetWaveColours(0xffffffff, 0xffffffff);

                gdl = textRenderProjected(gdl, &x, &y, str,
                                          g_CharsHandelGothicXs, g_FontHandelGothicXs, colour, textwidth,
                                          1000, 0, 0);

                textResetBlends();
            }
        }
    }

    if (weapon && weapon->functions[hand->gset.weaponfunc] != NULL) {
        ammoindex = ((struct weaponfunc *)(weapon->functions[hand->gset.weaponfunc]))->ammoindex;
    }

    if (ammoindex == -1) {
        if (weapon->functions[1 - hand->gset.weaponfunc] != NULL) {
            ammoindex = ((struct weaponfunc *)(weapon->functions[1 - hand->gset.weaponfunc]))->ammoindex;
        }

        if (ammoindex == -1) {
#ifndef PLATFORM_N64
            gSPClearExtraGeometryModeEXT(gdl++, G_ASPECT_MODE_EXT);
#endif
            gdl = text0f153780(gdl);
            g_ScaleX = 1;
            gDPNoOpTag(gdl++, VR_WEP_HUD_CAPTURE_END_R); // VR
            return gdl;
        }
    }

    if (ammoindex != ctrl->lastmag) {
        bgunResetAbmag(&player->hands[HAND_LEFT].abmag);
        bgunResetAbmag(&hand->abmag);
        bgunResetAbmag(&ctrl->abmag);
        ctrl->lastmag = ammoindex;
    }


    gDPNoOpTag(gdl++, VR_WEP_HUD_CAPTURE_BEGIN_L);

    // Left hand - mag
    if (lefthand->inuse
        && weapon->ammos[ammoindex] != NULL
        && lefthand->gset.weaponnum != WEAPON_REMOTEMINE) {

        xpos = viGetViewLeft() / g_ScaleX + 210;

        if (playercount == 2 && (optionsGetScreenSplit() == SCREENSPLIT_VERTICAL || IS4MB()) && playernum == 1) {
            xpos -= 14;
        } else if (playercount >= 3 && (playernum & 1) == 1) {
            xpos -= 14;
        }

#ifndef PLATFORM_N64
        if (playercount < 2 || (playercount == 2 && optionsGetScreenSplit() == SCREENSPLIT_HORIZONTAL)) {
            gSPExtraGeometryModeEXT(gdl++, G_ASPECT_MODE_EXT, g_HudAlignModeL);
        }
#endif

        if (lefthand->clipsizes[ammoindex] > 0 && (weapon->ammos[ammoindex]->flags & AMMOFLAG_EQUIPPEDISRESERVE) == 0) {
            gdl = bgunDrawHudGauge(gdl,
                                   xpos, bottom - reserveheight - clipheight - 3, xpos + barwidth, bottom - reserveheight - 3,
                                   &lefthand->abmag, lefthand->loadedammo[ammoindex], lefthand->clipsizes[ammoindex],
                                   0x00300080, 0x00ff0040, false);
            gdl = bgunDrawHudInteger(gdl, lefthand->loadedammo[ammoindex], xpos + barwidth + 2, true,
                                     bottom - reserveheight - 8, 0, 0x00ff00a0);
        }
    }

    gDPNoOpTag(gdl++, VR_WEP_HUD_CAPTURE_END_L);

    // Right hand - mag, reserve and combat boost timer
    if (hand->inuse && ctrl->ammotypes[ammoindex] >= 0) {
        s32 ammotype;
        s32 ammoheld;
        s32 ammototal;

        ammotype = player->gunctrl.ammotypes[ammoindex];

#if VERSION >= VERSION_NTSC_1_0
        xpos = (viGetViewLeft() + viGetViewWidth()) / g_ScaleX - barwidth - 230; // VR
#else
        // NTSC Beta omits the brackets here. This would normally cause the
		// ammo info to be misaligned for players on the right side of the
		// screen and when using hi-res, but I'm not sure if hi-res can even be
		// active when using multiple players...
		xpos = viGetViewLeft() + viGetViewWidth() / g_ScaleX - barwidth - 24;
#endif

        if (playercount == 2 && (optionsGetScreenSplit() == SCREENSPLIT_VERTICAL || IS4MB()) && playernum == 0) {
            xpos += 15;
        } else if (playercount >= 3 && (playernum % 2) == 0) {
            xpos += 15;
        }

#ifndef PLATFORM_N64
        if (playercount < 2 || (playercount == 2 && optionsGetScreenSplit() == SCREENSPLIT_HORIZONTAL)) {
            gSPExtraGeometryModeEXT(gdl++, G_ASPECT_MODE_EXT, g_HudAlignModeR);
        }
#endif

        // Mag
        ammoheld = player->ammoheldarr[ammotype];

        if (hand->clipsizes[ammoindex] > 0
            && weapon->ammos[ammoindex] != NULL
            && (weapon->ammos[ammoindex]->flags & AMMOFLAG_EQUIPPEDISRESERVE) == 0) {
            // VR: a pump gun's tube counts while it waits for the pump (vrReloadHudLoaded)
            s32 vrhudloaded = vrReloadHudLoaded(HAND_RIGHT, ammoindex, hand->loadedammo[ammoindex]);

            gdl = bgunDrawHudGauge(gdl, xpos, bottom - reserveheight - clipheight - 3, xpos + barwidth,
                                   bottom - reserveheight - 3, &hand->abmag, vrhudloaded, hand->clipsizes[ammoindex],
                                   0x00300080, 0x00ff0040, false);
            gdl = bgunDrawHudInteger(gdl, vrhudloaded, xpos - 2, false,
                                     bottom - reserveheight - 8, 0, 0x00ff00a0);
        }

        // Reserve
        if (g_AmmoTypes[ammotype].capacity > 0
            && (weapon->ammos[ammoindex]->flags & AMMOFLAG_NORESERVE) == 0) {
            ammototal = ammoheld;

            if (weapon->ammos[ammoindex]->flags & AMMOFLAG_EQUIPPEDISRESERVE) {
                if (hand->clipsizes[ammoindex] > 0) {
                    ammototal += hand->loadedammo[ammoindex];
                }

                if (lefthand->clipsizes[ammoindex] > 0) {
                    ammototal += lefthand->loadedammo[ammoindex];
                }
            }

            gdl = bgunDrawHudGauge(gdl, xpos, bottom - reserveheight, xpos + barwidth,
                                   bottom, &ctrl->abmag, ammototal, g_AmmoTypes[ammotype].capacity,
                                   0x00403080, 0x00ffc040, true);
            gdl = bgunDrawHudInteger(gdl, ammototal, xpos - 2, false, bottom - reserveheight + 1, 0, 0x00ffc0a0);
        }

        // Combat boost timer
        if (hand->gset.weaponnum == WEAPON_COMBATBOOST) {
            s32 mins;
            char text[32];

            speedpilltime = g_Vars.speedpilltime;
            mins = speedpilltime / TICKS(3600);
            secs60 = speedpilltime - mins * TICKS(3600);

            if (mins >= 1) {
                sprintf(text, "%02d:%02d:%02d\n", mins, secs60 / TICKS(60), (secs60 - (secs60 / TICKS(60)) * TICKS(60)) * 100 / TICKS(60));
            } else {
                sprintf(text, "%02d:%02d\n", secs60 / TICKS(60), (secs60 - (secs60 / TICKS(60)) * TICKS(60)) * 100 / TICKS(60));
            }

            gdl = bgunDrawHudString(gdl, text, xpos + barwidth - 2, false, bottom - reserveheight + 1, 0, 0x00ffc0a0);
        }
    }

#ifndef PLATFORM_N64
    gSPClearExtraGeometryModeEXT(gdl++, G_ASPECT_MODE_EXT);
#endif

    gdl = text0f153780(gdl);
    g_ScaleX = 1;
    gDPNoOpTag(gdl++, VR_WEP_HUD_CAPTURE_END_R); // VR
    return gdl;
}


void bgunAddBoost(s32 amount)
{
    g_Vars.speedpilltime += amount;

    if (g_Vars.speedpilltime > 5 * 60 * TICKS(60)) { // 5 minutes
        g_Vars.speedpilltime = 5 * 60 * TICKS(60);
    }

    if (!g_Vars.speedpillwant) {
        u32 sound = lvGetSlowMotionType() ? SFX_ARGH_JO_02AD : SFX_JO_BOOST_ACTIVATE;

        sndStart(var80095200, sound, 0, -1, -1, -1, -1, -1);
    }

    g_Vars.speedpillwant = true;
}

void bgunSubtractBoost(s32 amount)
{
    g_Vars.speedpilltime -= amount;

    if (g_Vars.speedpilltime <= 0) {
        g_Vars.speedpilltime = 0;
        g_Vars.speedpillwant = false;
    }
}

void bgunApplyBoost(void)
{
    if (lvGetSlowMotionType() != SLOWMOTION_OFF) {
        bgunSubtractBoost(TICKS(1200));
    }
    else {
        bgunAddBoost(TICKS(600));
    }
}

void bgunRevertBoost(void)
{
    if (lvGetSlowMotionType() != SLOWMOTION_OFF) {
        bgunAddBoost(TICKS(1200));
    }
    else {
        bgunSubtractBoost(TICKS(600));
    }
}

/**
 * The main tick function as called from lvTick.
 *
 * This function doesn't do much because it's called during both cutscenes and
 * gameplay, while most gun tick operations happen during gameplay only.
 * See bgunTickGameplay for that.
 */
void bgunTickBoost(void)
{
    if (g_Vars.speedpillon && g_Vars.speedpilltime > 0 && !g_Vars.in_cutscene) {
        g_Vars.speedpilltime -= g_Vars.lvupdate60;

        if (g_Vars.speedpilltime <= 0) {
            g_Vars.speedpilltime = 0;
            g_Vars.speedpillwant = false;
        }
    }
}

/**
 * gunsightoff is 0 if the full sight is visible, ie. player is holding R.
 *
 * Otherwise, gunsightoff holds bit values for reasons why the sight is off.
 * This is typically 2, which is GUNSIGHTREASON_NOTAIMING.
 *
 * If the visible argument is true, it removes the reason from the field, thus
 * making the sight visible if there are no other reasons.
 */
void bgunSetSightVisible(u32 reason, bool visible)
{
    if (visible) {
        g_Vars.currentplayer->gunsightoff &= ~reason;
        return;
    }

    g_Vars.currentplayer->gunsightoff |= reason;
}

Gfx* bgunDrawSight(Gfx * gdl)
{
    if (g_Vars.currentplayer->gunsightoff == 0 && !g_Vars.currentplayer->mpmenuon) {
        // Player is aiming with R
        gdl = sightDraw(gdl, true, currentPlayerGetSight());
    }
    else {
        gdl = sightDraw(gdl, false, currentPlayerGetSight());
    }

    return gdl;
}

void bgun0f0abd30(s32 handnum)
{
    struct player* player = g_Vars.currentplayer;
    struct hand* hand = &player->hands[handnum];
    struct gunctrl* gunctrl = &player->gunctrl;
    struct weapon* weapon = weaponFindById(hand->gset.weaponnum);
    s32 i;

    for (i = 0; i < 2; i++) {
        if (handnum == HAND_RIGHT) {
            gunctrl->ammotypes[i] = -1;
        }

#ifndef PLATFORM_N64
        // VR: the cloak held from the chest pouch has no clip: its charge (ticks, up to 18000) stays
        // in the reserve chr.c drains. As a clip of 10 the equip would load 10 and zero the rest
        // (AMMOFLAG_NORESERVE), and the empty clip would ask for reloads.
        if (hand->gset.weaponnum == WEAPON_CLOAKINGDEVICE && vrHolsterActive()) {
            hand->clipsizes[i] = 0;
            hand->loadedammo[i] = 0;
            continue;
        }
#endif

        if (weapon && weapon->ammos[i]) {
            if (handnum == HAND_RIGHT) {
                gunctrl->ammotypes[i] = weapon->ammos[i]->type;
            }

            hand->clipsizes[i] = weapon->ammos[i]->clipsize;

            if (handnum == HAND_LEFT && hand->gset.weaponnum == WEAPON_REMOTEMINE) {
                hand->clipsizes[i] = 0;
            }

            hand->loadedammo[i] = 0;
        }
    }

    hand->upgrademult[0] = 1;
    hand->upgrademult[1] = 1;
    hand->finalmult[0] = 1;
    hand->finalmult[1] = 1;

    if (gunctrl->ammotypes[0] >= 0) {
        bgunResetAbmag(&hand->abmag);

        if (handnum == HAND_RIGHT) {
            bgunResetAbmag(&gunctrl->abmag);
        }

        gunctrl->lastmag = false;
    }
}

/* ==========================================================================
 * 3D VR Weapon Scope System (ported from GoldenEye VR)
 * Excludes WEAPON_FARSIGHT and WEAPON_HORIZONSCANNER per design.
 * ========================================================================== */

GevrScopeState gevrScope[2];
s32 gevrScopeOn = 0;
s32 gevrScopeHand[2] = { 0, 0 };

/* Runtime trim in meters: [0]=right, [1]=up, [2]=back(+eye), [3]=size delta */
static f32 s_vrScopeTrim[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

/*
 * Configuration de la lunette 3D VR par arme (1 unité = 1 mm sur le modèle 3D) :
 * - weapon         : ID de l'arme (ex: WEAPON_SNIPERRIFLE)
 * - x              : Décalage latéral (Gauche / Droite).
 *                    En mode droitier, augmenter x déplace le cercle vers la GAUCHE de l'arme,
 *                    diminuer x (ou valeur négative) le déplace vers la DROITE.
 * - y              : Hauteur (Haut / Bas).
 *                    Augmenter y monte le cercle vers le HAUT, diminuer y le descend vers le BAS.
 * - z              : Profondeur le long du canon (Avant / Arrière).
 *                    Une valeur plus négative (ex: -130.0f) rapproche le cercle vers l'OEIL (arrière de l'arme),
 *                    une valeur moins négative ou positive (ex: -20.0f) l'éloigne vers le CANON (avant de l'arme).
 * - r              : Rayon du cercle de la lunette.
 *                    Augmenter r agrandit la taille du cercle, diminuer r le réduit.
 * - followLockon   : Si true, le réticule interne suit la cible verrouillée.
 * - defaultZoomFov : Niveau de zoom de la lunette (FOV en degrés, base 60° = 1x :
 *                    30.0f = zoom 2x, 20.0f = zoom 3x, 15.0f = zoom 4x, etc.
 *                    Plus la valeur est petite, plus le zoom est fort).
 * - camOffsetY     : Décalage vertical (en mm, même échelle que y) de la caméra qui filme
 *                    à l'intérieur du scope, sans déplacer le cercle 3D sur l'arme
 *                    (valeur négative = abaisse le point de vue du scope vers le canon).
 * - camOffsetX     : Décalage horizontal (en mm, même échelle que x) de la caméra qui filme
 *                    à l'intérieur du scope, sans déplacer le cercle 3D sur l'arme.
 */
struct vrScopeDef {
    s32 weapon;
    f32 x;
    f32 y;
    f32 z;
    f32 r;
    bool followLockon;
    f32 defaultZoomFov;
    f32 camOffsetY;
    f32 camOffsetX;
};

static struct vrScopeDef s_vrScopes[] = {
    { WEAPON_SNIPERRIFLE,   68.0f, 232.5f, -37.0f, 17.0f, false, 15.0f, -128.0f, -6.0f },
    { WEAPON_FALCON2_SCOPE, -18.0f, 106.0f, 44.0f, 10.0f, false, 30.0f, -45.0f,  0.0f },
    { WEAPON_MAGSEC4,         3.0f, 85.0f, 164.0f, 11.0f, false, 30.0f, -30.0f,  0.0f },
    { WEAPON_LAPTOPGUN,      22.0f, 89.0f, 149.0f, 18.0f, false, 30.0f, -56.0f, -2.0f },
    { WEAPON_DRAGON,        34.0f, 215.0f, -81.0f, 18.0f, false, 30.0f, -66.0f, 10.0f },
    { WEAPON_SUPERDRAGON,    33.0f, 215.0f, -82.0f, 18.0f, false, 30.0f, -66.0f, 11.0f },
    { WEAPON_K7AVENGER,     -48.0f, 169.0f, -157.0f, 18.0f, false, 20.0f, -58.0f, 0.0f },
    { WEAPON_AR34,          18.5f, 184.0f, -84.0f, 16.0f, false, 15.0f, -111.0f, 7.0f },
    { WEAPON_KF7SPECIAL,     0.0f, 105.0f, 127.0f, 16.0f, false, 30.0f, -131.0f, -16.0f },
    { WEAPON_AR53,          0.5f, 129.0f, -5.0f, 16.0f, false, 20.0f, -106.0f, -8.0f },
};

extern float vr_world_scale;

/*
static const char *vrScopeWeaponName(s32 weaponnum)
{
    switch (weaponnum) {
        case WEAPON_SNIPERRIFLE:   return "WEAPON_SNIPERRIFLE";
        case WEAPON_FALCON2_SCOPE: return "WEAPON_FALCON2_SCOPE";
        case WEAPON_MAGSEC4:       return "WEAPON_MAGSEC4";
        case WEAPON_LAPTOPGUN:     return "WEAPON_LAPTOPGUN";
        case WEAPON_DRAGON:        return "WEAPON_DRAGON";
        case WEAPON_SUPERDRAGON:   return "WEAPON_SUPERDRAGON";
        case WEAPON_K7AVENGER:     return "WEAPON_K7AVENGER";
        case WEAPON_AR34:          return "WEAPON_AR34";
        case WEAPON_AR53:          return "WEAPON_AR53";
        case WEAPON_KF7SPECIAL:    return "WEAPON_KF7SPECIAL";
        default:                   return "WEAPON_UNKNOWN";
    }
}
*/

/* Temporary in-game keyboard tuning for scope position (x, y, z), radius (r), camOffsetY, and camOffsetX (commented out)
static void vrScopeKeyboardTune(struct vrScopeDef *sc)
{
    static u32 s_lastTuneTick = 0;
    static bool s_wasAnyPressed = false;
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    u32 now;
    bool left, right, up, down, zFwd, zBack, rPlus, rMinus, camYPlus, camYMinus, camXPlus, camXMinus, any;
    f32 step, rstep;

    if (!keys || !sc) {
        return;
    }

    left      = keys[SDL_SCANCODE_LEFT] != 0;
    right     = keys[SDL_SCANCODE_RIGHT] != 0;
    up        = keys[SDL_SCANCODE_UP] != 0;
    down      = keys[SDL_SCANCODE_DOWN] != 0;
    zFwd      = (keys[SDL_SCANCODE_PAGEUP] != 0)   || (keys[SDL_SCANCODE_KP_8] != 0);
    zBack     = (keys[SDL_SCANCODE_PAGEDOWN] != 0) || (keys[SDL_SCANCODE_KP_2] != 0);
    rPlus     = (keys[SDL_SCANCODE_KP_PLUS] != 0)  || (keys[SDL_SCANCODE_P] != 0);
    rMinus    = (keys[SDL_SCANCODE_KP_MINUS] != 0) || (keys[SDL_SCANCODE_O] != 0);
    camYPlus  = (keys[SDL_SCANCODE_HOME] != 0)     || (keys[SDL_SCANCODE_U] != 0) || (keys[SDL_SCANCODE_KP_9] != 0);
    camYMinus = (keys[SDL_SCANCODE_END] != 0)      || (keys[SDL_SCANCODE_I] != 0) || (keys[SDL_SCANCODE_KP_3] != 0);
    camXPlus  = (keys[SDL_SCANCODE_J] != 0)        || (keys[SDL_SCANCODE_KP_4] != 0);
    camXMinus = (keys[SDL_SCANCODE_K] != 0)        || (keys[SDL_SCANCODE_KP_6] != 0);

    any = left || right || up || down || zFwd || zBack || rPlus || rMinus || camYPlus || camYMinus || camXPlus || camXMinus;
    if (!any) {
        s_wasAnyPressed = false;
        return;
    }

    now = SDL_GetTicks();
    if (s_wasAnyPressed && (now - s_lastTuneTick) < 80) {
        return;
    }
    s_wasAnyPressed = true;
    s_lastTuneTick = now;

    if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) {
        step  = 0.2f;
        rstep = 0.1f;
    } else if (keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL]) {
        step  = 5.0f;
        rstep = 2.0f;
    } else {
        step  = 1.0f;
        rstep = 0.5f;
    }

    if (left)      sc->x += step;
    if (right)     sc->x -= step;
    if (up)        sc->y += step;
    if (down)      sc->y -= step;
    if (zFwd)      sc->z += step;
    if (zBack)     sc->z -= step;
    if (rPlus)     sc->r += rstep;
    if (rMinus) {
        sc->r -= rstep;
        if (sc->r < 1.0f) sc->r = 1.0f;
    }
    if (camYPlus)  sc->camOffsetY += step;
    if (camYMinus) sc->camOffsetY -= step;
    if (camXPlus)  sc->camOffsetX += step;
    if (camXMinus) sc->camOffsetX -= step;

    vr_log("[VR_SCOPE] { %s, %.1ff, %.1ff, %.1ff, %.1ff, %s, %.1ff, %.1ff, %.1ff },",
           vrScopeWeaponName(sc->weapon),
           sc->x, sc->y, sc->z, sc->r,
           sc->followLockon ? "true" : "false",
           sc->defaultZoomFov, sc->camOffsetY, sc->camOffsetX);
}
*/

static struct vrScopeDef *vrFindScope(s32 weaponnum)
{
    s32 i;

    /* Explicitly exclude FarSight XR-20 and Horizon Scanner */
    if (weaponnum == WEAPON_FARSIGHT || weaponnum == WEAPON_HORIZONSCANNER) {
        return NULL;
    }

    for (i = 0; i < (s32)(sizeof(s_vrScopes) / sizeof(s_vrScopes[0])); i++) {
        if (s_vrScopes[i].weapon == weaponnum) {
            return &s_vrScopes[i];
        }
    }
    return NULL;
}

bool vrScopeForWeapon(s32 weaponnum)
{
    return vrFindScope(weaponnum) != NULL;
}

/* The hand's selected function aims through the lens: the lens is the sight of the gun's own fire,
 * so not a function that launches something else (the SuperDragon's grenade launcher, a projectile;
 * the Dragon's self-destruct and the Laptop Gun's sentry, thrown) or strikes with it (the Falcon 2
 * Scope's pistol whip). The Sniper Rifle's crouch, a stance with no fire of its own, keeps it (Task
 * F11c, the user: "scope for grenade launcher on superdragon should not appear") */
static bool vrScopeFuncZooms(struct hand *hand)
{
    struct weaponfunc *func = gsetGetWeaponFunction(&hand->gset);

    if (func == NULL) {
        return true;
    }

    return func->type != INVENTORYFUNCTYPE_SHOOT_PROJECTILE
        && (func->type & 0xff) != INVENTORYFUNCTYPE_THROW
        && (func->type & 0xff) != INVENTORYFUNCTYPE_MELEE;
}

void vrScopeBegin(void)
{
    s32 h;

    gevrScopeOn = 0;
    gevrScopeHand[0] = 0;
    gevrScopeHand[1] = 0;

    if (!g_Vars.currentplayer
        || g_Vars.currentplayer->activemenumode != AMMODE_CLOSED
        || g_Vars.currentplayer->gunctrl.passivemode
        || g_Vars.currentplayer->bondhealth <= 0.0f) {
        return;
    }

    for (h = 0; h < 2; h++) {
        GevrScopeState *st = &gevrScope[h];
        struct hand *hand = &g_Vars.currentplayer->hands[h];
        s32 weaponnum = hand->gset.weaponnum;
        struct vrScopeDef *sc = vrFindScope(weaponnum);
        s32 handToMirror;
        bool isMirrored;
        f32 bg_scale, worldScale, cmToStereoM, mmToStereoM;
        f32 headFovY, headAspect;
        f32 zoomFov, s, zn, zf, nf, rlen, udot;
        f32 f[3], u[3], r[3], o[3];
        s32 i;

        st->gunValid = 0;

        {
            /* Only while the gun is aimed with both hands (vrTwoHandAiming), the Sniper Rifle too (shown
             * always before), never from a grip alone or a pair's left gun. The Falcon 2 Scope's lens
             * shows one-handed, on either gun of a pair, as before S1 (user, Stage F run 1: "since that
             * is its purpose"; Task F7f): a pistol is never aimed with both hands, and its aim smoothing
             * stays the two-handed aim's (vr_input.cpp). Never for a selected function that does not
             * aim through it (vrScopeFuncZooms: the SuperDragon's grenade launcher, Task F11c) */
            bool showLens = ((h == HAND_RIGHT && vrTwoHandAiming()) || weaponnum == WEAPON_FALCON2_SCOPE)
                && vrScopeFuncZooms(hand);

            /* Do not display scope while weapon is being switched, unequipped, raised, deployed, or reloaded */
            if (hand->state == HANDSTATE_CHANGEGUN
                || hand->state == HANDSTATE_AUTOSWITCH
                || hand->state == HANDSTATE_RELOAD
                || hand->mode == HANDMODE_EQUIP
                || g_Vars.currentplayer->gunctrl.switchtoweaponnum != -1
                || g_Vars.currentplayer->gunctrl.gunmemnew >= 0
                || !bgunIsLoaded()
                || !g_vrScopeGunMtxValid[h]) {
                continue;
            }

            if (!sc || !hand->inuse || !hand->visible || !showLens) {
                continue;
            }
        }

        /*
        if (h == HAND_RIGHT) {
            vrScopeKeyboardTune(sc);
        }
        */

        handToMirror = VrLeftHandedMode ? HAND_RIGHT : HAND_LEFT;
        isMirrored = ((weaponHasFlag(weaponnum, WEAPONFLAG_DUALFLIP) || weaponnum == WEAPON_UNARMED) && h == handToMirror)
                  || (!weaponHasFlag(weaponnum, WEAPONFLAG_DUALFLIP) && weaponnum != WEAPON_UNARMED && VrLeftHandedMode);

        /* Match the exact stereo world scale used by the game's VR shader:
         * in vr_get_eye_view_offset(), out_tx = localEyeX * vr_world_scale,
         * and in bgun0f0a5550(), the weapon matrix is scaled by bg_scale.
         * Therefore 1 stereo meter in viewSpace = (vr_world_scale / bg_scale) cm-units. */
        bg_scale = bgGetScaleBg2Gfx();
        if (bg_scale <= 0.0001f) {
            bg_scale = 1.0f;
        }
        worldScale = (vr_world_scale > 0.0001f) ? vr_world_scale : (85.0f * bg_scale);
        cmToStereoM = bg_scale / worldScale;
        mmToStereoM = 0.1f * cmToStereoM;

        /* Extract weapon pose in viewSpace (stereo meters) from the final rendered weapon matrix:
         * gunMtx->m[0] = gun left (or right when mirrored), gunMtx->m[1] = gun up,
         * gunMtx->m[2] = gun forward, gunMtx->m[3] = gun origin in viewSpace (cm). */
        {
            const Mtxf *gunMtx = g_vrScopeGunMtxValid[h] ? &g_vrScopeGunMtx[h] : &hand->cammtx;
            f32 mirrorSign = (g_vrScopeGunMtxValid[h] && isMirrored) ? 1.0f : -1.0f;

            for (i = 0; i < 3; i++) {
                f32 posCm = gunMtx->m[3][i];
                if (!g_vrScopeGunMtxValid[h] && weaponnum == WEAPON_K7AVENGER) {
                    posCm -= (gunMtx->m[0][i] * -3.0f + gunMtx->m[2][i] * -15.0f);
                }
                st->gunOrigin[i]  = posCm * cmToStereoM;
                st->gunAxes[0][i] = mirrorSign * gunMtx->m[0][i]; /* right (+X of quad) */
                st->gunAxes[1][i] = gunMtx->m[1][i];              /* up (+Y of quad) */
                st->gunAxes[2][i] = -gunMtx->m[2][i];             /* back (+Z toward player's eye) */
            }

            st->lens[0] = (isMirrored ? 1.0f : -1.0f) * sc->x * mmToStereoM + s_vrScopeTrim[0];
            st->lens[1] = sc->y * mmToStereoM + s_vrScopeTrim[1];
            st->lens[2] = -sc->z * mmToStereoM + s_vrScopeTrim[2];
            st->lens[3] = 2.0f * sc->r * mmToStereoM * 2.0f + s_vrScopeTrim[3];
            st->gunValid = 1;

            /* Fixed scope zoom FOV per weapon, except Sniper Rifle which has adjustable zoom */
            if (weaponnum == WEAPON_SNIPERRIFLE) {
                zoomFov = currentPlayerGetGunZoomFov();
                if (zoomFov <= 0.0f) {
                    zoomFov = sc->defaultZoomFov;
                }
            } else {
                zoomFov = sc->defaultZoomFov;
            }
            if (zoomFov > 60.0f) {
                zoomFov = 60.0f;
            }
            if (st->lensTan > 1e-3f) {
                f32 t = st->lensTan * tanf(zoomFov * 0.5f * (3.14159265f / 180.0f))
                        / tanf(60.0f * 0.5f * (3.14159265f / 180.0f));
                zoomFov = 2.0f * atanf(t) * (180.0f / 3.14159265f);
            }
            if (zoomFov < 0.5f) {
                zoomFov = 0.5f;
            }
            st->fovDeg = zoomFov;

            {
                extern s32 g_LookingAtPropHandMask;
                if (g_Vars.currentplayer->lookingatprop.prop != NULL
                    && sightIsReactiveToProp(g_Vars.currentplayer->lookingatprop.prop)
                    && ((g_LookingAtPropHandMask & (1 << h)) != 0 || !g_Vars.currentplayer->hands[HAND_LEFT].inuse)) {
                    st->crosshairState = 1;
                    if (sightIsPropFriendly(g_Vars.currentplayer->lookingatprop.prop)) {
                        st->crosshairColor = 0x0040ffdfU;
                    } else {
                        st->crosshairColor = 0xff0000dfU;
                    }
                } else {
                    st->crosshairState = 0;
                    st->crosshairColor = (PLAYER_EXTCFG().crosshaircolour & 0xffffff00U) | 0xb0U;
                }
            }

            headFovY = (g_Vars.currentplayer->fovy > 1.0f) ? g_Vars.currentplayer->fovy : 60.0f;
            headAspect = (g_Vars.currentplayer->aspect > 0.01f) ? g_Vars.currentplayer->aspect : 1.0f;
            st->headP[1] = 1.0f / tanf(headFovY * 0.5f * (3.14159265f / 180.0f));
            st->headP[0] = st->headP[1] / headAspect;

            {
                f32 camLensX = st->lens[0] + (isMirrored ? 1.0f : -1.0f) * sc->camOffsetX * mmToStereoM;
                f32 camLensY = st->lens[1] + sc->camOffsetY * mmToStereoM;
                for (i = 0; i < 3; i++) {
                    f[i] = gunMtx->m[2][i]; /* unit barrel forward in camera space */
                    u[i] = gunMtx->m[1][i]; /* unit gun up in camera space */
                    o[i] = (st->gunOrigin[i]
                          + st->gunAxes[0][i] * camLensX
                          + st->gunAxes[1][i] * camLensY
                          + st->gunAxes[2][i] * st->lens[2]) * worldScale;
                    st->origin[i] = o[i];
                }
            }
        }

        /* r = normalize(f x u), u = r x f */
        r[0] = f[1] * u[2] - f[2] * u[1];
        r[1] = f[2] * u[0] - f[0] * u[2];
        r[2] = f[0] * u[1] - f[1] * u[0];
        rlen = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
        if (rlen > 1e-5f) {
            r[0] /= rlen;
            r[1] /= rlen;
            r[2] /= rlen;
        }
        u[0] = r[1] * f[2] - r[2] * f[1];
        u[1] = r[2] * f[0] - r[0] * f[2];
        u[2] = r[0] * f[1] - r[1] * f[0];

        /* Ensure scope +Y stays right-side-up in camera space */
        udot = u[0] * hand->cammtx.m[1][0] + u[1] * hand->cammtx.m[1][1] + u[2] * hand->cammtx.m[1][2];
        if (udot < 0.0f) {
            r[0] = -r[0]; r[1] = -r[1]; r[2] = -r[2];
            u[0] = -u[0]; u[1] = -u[1]; u[2] = -u[2];
        }

        /* Column-major VP = P_scope * V_scope */
        s = 1.0f / tanf(zoomFov * 0.5f * (3.14159265f / 180.0f));
        zn = 10.0f;
        zf = 30000.0f;
        nf = 1.0f / (zn - zf);

        st->vp[0]  = s * r[0];
        st->vp[1]  = s * u[0];
        st->vp[2]  = (zf + zn) * nf * (-f[0]);
        st->vp[3]  = f[0];

        st->vp[4]  = s * r[1];
        st->vp[5]  = s * u[1];
        st->vp[6]  = (zf + zn) * nf * (-f[1]);
        st->vp[7]  = f[1];

        st->vp[8]  = s * r[2];
        st->vp[9]  = s * u[2];
        st->vp[10] = (zf + zn) * nf * (-f[2]);
        st->vp[11] = f[2];

        {
            f32 tx = -(r[0] * o[0] + r[1] * o[1] + r[2] * o[2]);
            f32 ty = -(u[0] * o[0] + u[1] * o[1] + u[2] * o[2]);
            f32 tz = -(-f[0] * o[0] - f[1] * o[1] - f[2] * o[2]);
            st->vp[12] = s * tx;
            st->vp[13] = s * ty;
            st->vp[14] = (zf + zn) * nf * tz + 2.0f * zf * zn * nf;
            st->vp[15] = -tz;
        }

        gevrScopeHand[h] = 1;
        gevrScopeOn |= (1 << h);
    }
}

Gfx *vrScopeDrawReticle(Gfx *gdl, s32 hand)
{
    s32 cx = SCREEN_WIDTH_LO / 2;
    s32 cy = SCREEN_HEIGHT_LO / 2;
    s32 gap = 4, outer = 90;
    (void)hand;

    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(gdl++, 0, 0, 220, 0, 0, 220);

    /* Horizontal hair (left and right of center gap) */
    gDPFillRectangle(gdl++, cx - outer, cy, cx - gap, cy + 1);
    gDPFillRectangle(gdl++, cx + gap + 1, cy, cx + outer + 1, cy + 1);
    /* Vertical hair (top and bottom of center gap) */
    gDPFillRectangle(gdl++, cx, cy - outer, cx + 1, cy - gap);
    gDPFillRectangle(gdl++, cx, cy + gap + 1, cx + 1, cy + outer + 1);
    /* Center dot */
    gDPSetPrimColor(gdl++, 0, 0, 255, 40, 40, 255);
    gDPFillRectangle(gdl++, cx, cy, cx + 1, cy + 1);

    return gdl;
}
