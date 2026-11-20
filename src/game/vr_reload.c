/* Engine adapter for the VR manual reload: feeds vr_reload_core from the controllers and the
 * hand's ammo, then applies its output (ammo, sounds, haptics). See vr_reload.h. */
#include <ultra64.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../../port/vr/vr_input.h"
#include "../../port/vr/vr_openxr.h"
#include "input.h"
#include "constants.h"
#include "game/bg.h"
#include "game/bondgun.h"
#include "game/camera.h"
#include "game/cheats.h"
#include "game/game_0b0fd0.h"
#include "game/lv.h"
#include "game/quaternion.h"
#include "game/vr_holster.h"
#include "game/vr_reload.h"
#include "game/vr_reload_core.h"
#include "game/vr_reload_data.h"
#include "lib/model.h"
#include "lib/mtx.h"
#include "lib/snd.h"
#include "bss.h"
#include "system.h"
#include "types.h"

/* Logs the position mark (see sMarkPending) under the PD-VRR tag */
#ifdef ANDROID
#include <android/log.h>
#define VRR_DIAG(...) __android_log_print(ANDROID_LOG_INFO, "PD-VRR", __VA_ARGS__)
#else
#define VRR_DIAG(...) ((void) 0)
#endif

extern bool VrManualReloading;
extern int VrLeftHandedMode;
extern int VrLeftHandedPref;
extern int VrCtrlPoseMode;
extern float VrGunOffX;
extern struct coord gVrGunOff[2];
extern void vr_rotate_vector_by_quaternion(struct coord *v, const XrQuaternionf *q);
extern bool vrTwoHandGripHeld(void); /* bondgun.c: the free hand grips a two-handed gun as its support */

/* Shoulder clip-source point in the body frame, cm: x right, y up, z back. Tuned in the headset. */
#define VRR_SHOULDER_X -14.0f
#define VRR_SHOULDER_Y -12.0f
#define VRR_SHOULDER_Z   8.0f

#define VRR_SHOULDER_RAD 20.0f /* 15 missed a reach a little high (test 2) */

/* Where the drawn off-hand grabs, cm in the off-hand controller's frame (as vrReloadOffRot and
 * vrReloadGunToView turn it): the hand model sits away from the controller's origin, so a grab at
 * the origin missed the slide with the drawn hand on it. Measured in the headset (test 2) with the
 * drawn hand on the rear of the slide. x is mirrored in left-handed mode, with the hand model. The
 * free hand is drawn at the default grip offset whatever the gun (the free left-hand placement in
 * bgunRender, Task F7a), the Falcon's, so the point holds for every row. */
#define VRR_HAND_GRAB_X -5.0f
#define VRR_HAND_GRAB_Y  5.4f
#define VRR_HAND_GRAB_Z -2.7f

/* Haptic pulses: amplitude 0..1 (scaled by the rumble strength setting), duration in seconds */
#define VRR_BUZZ_ZONE_AMP    0.25f /* off-hand entered a glowing zone */
#define VRR_BUZZ_ZONE_SECS   0.03f
#define VRR_BUZZ_ACTION_AMP  0.7f  /* off-hand grabbed a clip, inserted it or racked */
#define VRR_BUZZ_ACTION_SECS 0.06f
#define VRR_BUZZ_EJECT_AMP   0.5f  /* gun hand: clip ejected */
#define VRR_BUZZ_EJECT_SECS  0.05f
#define VRR_BUZZ_NOAMMO_AMP  0.4f  /* off-hand, twice: no reserve to take a clip from */
#define VRR_BUZZ_NOAMMO_SECS 0.04f
#define VRR_BUZZ_SLAP_AMP    1.0f  /* off-hand: its slap met the clip (a row with rackslap) */
#define VRR_BUZZ_SLAP_SECS   0.08f

/* Gap before the second NO_AMMO pulse, in 60ths: one tick would merge it into the first pulse */
#define VRR_NOAMMO_GAP60 5

/* Captured parts older than this many frames (lvframenum) give no grab areas: the gun was not
 * drawn (hidden, switching) and its parts are not where they were. A tick normally sees age 1. */
#define VRR_PARTS_MAX_AGE 2

/* An eject waits at most this many frames for a record of the clip drawn on the current gun side
 * (see vrReloadBuildInput): after a gun side flip the first record comes from the next frame */
#define VRR_EJECT_WAIT_FRAMES 3

/* Guided insert. A held clip that is guided stays guided until its top is this much further from
 * the well, or its direction this much further off the insert axis, than it took to start, so it
 * does not flicker at the edge. */
#define VRR_GUIDE_EXIT_CM  2.0f
#define VRR_GUIDE_EXIT_DEG 10.0f
#define VRR_GUIDE_OVERSHOOT_CM 5.0f /* a guided clip pushed this far past seated stays guided */
#define VRR_PULL_IN_CM     1.0f /* the drawn clip runs this far ahead of the held clip's top */
#define VRR_GUIDE_START_PAST_CM 2.0f /* a guide starts with the top at most this far past the mouth */
#define VRR_GUIDE_REACH_DEEP_CM 1.5f /* a top this near a reach stretch: the start margin follows it */
#define VRR_GUIDE_ANY_ANGLE_DEG 180.0f /* a row's clipalignmax at which any angle guides */
#define VRR_FINISH_60THS   6.0f /* after the insert the clip slides the rest of the way in this time */

/* A row's holder (holdermtx) turns shut over this time once the clip is seated: the Tranquilizer's
 * reload animation turns it shut in frames 57 to 72 and plays the click (186) at 72 */
#define VRR_HOLDER_CLOSE_60THS 15.0f

/* A row's moving rack matrix (rackmove) goes forward from full travel over this time once the rack
 * ends: the RC-P120's knob springs forward in its reload animation's frames 122 to 126 */
#define VRR_RACK_RETURN_60THS 4.0f

/* A row with pumpaction (the Shotgun): after the pump's full pull the forend rides with the hand
 * (sPumpRide) while the grip is held, and the gun fires again only once it is back within this share of
 * racktravel of forward (or the grip lets go, when it springs forward as a knob does). Was a fixed
 * 1.5 cm, so a return that stopped short refused the next shot (Task J6c, the user: "shotgun pumping
 * sometimes doesnt read") */
#define VRR_PUMP_SHUT_FRAC 0.4f

/* A row with pumpaction pumped with nothing to chamber (the chamber and the tube empty: the gun is
 * ready and dry, so the core takes no pump): a pull back of this share of racktravel from the stroke's
 * most forward point plays the empty click, once per stroke, and the forend stays where it is (Task
 * J6d, the user: "if the pump cant pump because theres no ammo it should make the empty click noise
 * instead (no animation)"). The next stroke's click waits for the hand to come back within
 * VRR_DRY_PUMP_REARM_FRAC of that point, or a new grip */
#define VRR_DRY_PUMP_FRAC       0.5f
#define VRR_DRY_PUMP_REARM_FRAC 0.2f

/* The slap that racks a row with rackslap (the AR34 and the K7 Avenger, Task F3b), from below only
 * (user, 2026-10-19: "the controller motion comes from underneath so the way down doesn't trigger
 * it"). While the gun needs a rack and the free hand is empty, a slap starts when the free hand's
 * palm (vrReloadHandPalm: the drawn open fists' palm, where the slap points were measured, not the
 * curled fingers' grab point VRR_HAND_GRAB_X, 7.7 cm away; Task F7d) comes within the row's slaprad
 * of its slappoint while moving up the insert axis, relative to the gun hand, at VRR_SLAP_SPEED or
 * more, that up speed being at least VRR_SLAP_AXIAL_MIN of its whole speed (within about 45 degrees
 * of the axis, so a swing from the side does not slap), and when the palm was at least
 * VRR_SLAP_BELOW_CM under the slap point along the axis, within VRR_SLAP_BELOW_SIDE slaprads of the
 * axis through it, in the last VRR_SLAP_BELOW_60THS of game time (so a hand coming from above, beside
 * or at rest never slaps). The speed is the palm's against the slap point's, each from its
 * controller's runtime linear and angular velocity (vrReloadSlapVelocity), so a turn of the rifle
 * counts, and it does not depend on how many ticks a frame runs; the radius is tested on the palm's
 * path since the last frame. Tuned in the headset from the "slap:" lines. */
#define VRR_SLAP_SPEED       1.0f  /* m/s up the axis */
#define VRR_SLAP_AXIAL_MIN   0.7f
#define VRR_SLAP_BELOW_CM    2.0f
#define VRR_SLAP_BELOW_SIDE  1.5f
#define VRR_SLAP_BELOW_60THS 30
#define VRR_SLAP_LOG_SPEED   0.6f  /* m/s up: a swing this fast that does not slap is logged once ... */
#define VRR_SLAP_LOG_RADS    3.0f  /* ... when it came within this many slaprads of the slap point */
#define VRR_SLAP_EASE_60THS  8.0f  /* after slapto the drawn hand goes back to the controller in this time */

/* The close of a row held open after its load (closeto: the DY357's cylinder, Task G4), the core's rack
 * of a rackslap row with no clip part, by either of two gestures while the gun waits for it with the
 * insert's playback over (vrReloadStepClose). A flick: the gun controller turning at VRR_FLICK_RADS or
 * more about the row's flickaxis (the barrel) either way (user, 2026-10-20: "flick on revolvers need to
 * work to the right as well"; the log's refused rolls the other way were -15 to -20 rad/s), that roll
 * being at least VRR_FLICK_AXIAL of its whole turn rate (a wrist roll, not a swing of the arm). A
 * push: the free hand empty, its palm (vrReloadHandPalm) within the row's slaprad of its slappoint (the
 * open part's middle) and moving against the gun along the row's closedir (the way the open part goes
 * in) at VRR_TOUCH_SPEED or more (user's design, 2026-10-20: "a touch inside a small zone at the
 * cylinder with any motion toward the frame"). Both rates are the runtime's, in play space,
 * so they do not depend on how many ticks a frame runs. Tuned in the headset from the "close:" lines: a
 * roll of VRR_FLICK_LOG_RADS or more about the barrel that does not close is logged once when it ends */
#define VRR_FLICK_RADS     6.5f  /* rad/s, about 370 degrees a second (was 5.0, "slightly less sensitive":
                                     * run 14 closed at 5.1 to 9.6) */
#define VRR_FLICK_AXIAL    0.6f
#define VRR_FLICK_LOG_RADS 3.0f
#define VRR_TOUCH_SPEED    0.1f  /* m/s */

/* vrReloadStartPlay's kind: 0 the insert's playback, 1 the eject's, and the close (closefrom..closeto),
 * which is posed as the eject's (sPlayEject) on its matrices */
#define VRR_PLAY_CLOSE 2

static struct vrrgun sGun[2];
static s32 sGunWeapon[2] = { -1, -1 }; /* -1: core not initialised for the current gun */
static s32 sAmmoType[2] = { -1, -1 };  /* ammo type of the gun the core was last ticked for */
static s32 sAmmoIndex[2] = { -1, -1 }; /* its loadedammo index */
static s32 sEject[2];
static s32 sEjectSince[2] = { -1, -1 }; /* lvframenum the pending eject began waiting, -1: not */
static s32 sEjectWaited[2];            /* frames the last eject waited, for the drop log */
static f32 sRackGrab[2];               /* rel . rack axis at the rack grab, see vrReloadBuildInput */
static f32 sSlideGrab[2];              /* hand->slidetrans at the rack grab, see vrReloadMoveSlide */
static s32 sAllowFire[2] = { 1, 1 };
static s32 sHandClip[2];

/* Position marking: the trigger held and the thumbstick clicked on the physical left controller
 * (whatever the gun side and the left-handed setting), with manual reload on, buzzes
 * that controller and logs under PD-VRR: always the holster line (vrHolsterLogMark: both hands in
 * the body frame, for the zone points), and with an owned gun the "mark" line: where the drawn
 * off-hand's grab point and the held clip's top are in the gun parts' own frames (cm, the frames
 * the rows' offsets use), and the free controller's place and turn in the gun hand's frame. A spot
 * shown in the headset can then be copied into a gun row or a zone point instead of being
 * described. */
static s32 sMarkPrev;
static s32 sMarkPending[2];
static s32 sNoRackLogged = -1; /* weapon last logged by the capture for a rack row with no rack */

static void vrReloadHandGrab(f32 *out);
static f32 vrReloadDist(f32 *a, f32 *b);
static f32 vrReloadClamp(f32 v, f32 lo, f32 hi);
static s32 vrReloadReachLocal(struct vrreloaddef *def, f32 out[][3]);
static f32 vrReloadReachDist(const f32 *p, const f32 *mouth, f32 pts[][3], s32 n, f32 dist);
static void vrReloadHeldClip(s32 h, struct hand *hand, f32 *top, f32 *axis);
static void vrReloadFreeInGun(s32 h, struct hand *hand, f32 *pos, f32 *quat);
static void vrReloadRebaseMtx(Mtxf *out, const Mtxf *x, const Mtxf *from, const Mtxf *to);
static void vrReloadStartPlay(s32 h, struct hand *hand, struct vrreloaddef *def, s32 eject);
static s32 vrReloadRowsMirrored(const Mtxf *m);
static struct vrreloaddef *vrReloadSelectedDef(s32 handnum);
static s32 sNoAmmoPulse[2];            /* 1 while the second NO_AMMO pulse is pending */
static s32 sNoAmmoPulseFrame[2];

/* Gun model units per cm: bgun0f0a5550 scales the gun by 0.1 (mtx00015f04(0.10000001f)) */
#define VRR_MODEL_UNITS_PER_CM 10.0f

/* The gun models' left arm and hand (the forearm 17, the hand bone 18 and the fingers to 32), as the
 * hands models' right hand is 1 to 16 */
#define VRR_LEFT_HAND_MTX_FIRST 17
#define VRR_LEFT_HAND_MTX_LAST  32

/* The gun's clip and rack parts as last drawn, from vrReloadCaptureParts, in the gun hand's frame
 * (see vrReloadGunAnchor): cm, relative to the anchor, unturned by the gun rotation of that frame.
 * Rebuilt in view space each tick with that tick's anchor and rotation, so the one-frame delay
 * between the render and the next tick shows in neither position nor direction. */
struct vrrparts {
	s32 weaponnum;   /* gun they were captured from; -1: none */
	s32 frame;       /* g_Vars.lvframenum at the capture */
	f32 clippos[3];  /* clip joint (top of the clip), seated; for an intake row, the intake point */
	f32 clipaxis[3]; /* insert direction, unit: the clip matrix applied to def->clipaxis; zero for an
	                  * intake row */
	f32 rackpos[3];  /* rack grab point: def->rackoffset in the rack frame (the slide, with the
	                  * engine's slide offset, or the row's rackmtx) */
	f32 rackaxis[3]; /* the rack frame's own -Z, unit: the direction the engine moves a slide back */
	f32 rackorigin[3]; /* the rack frame's origin, with the engine's slide offset on a slide */
	f32 rackx[3];    /* the rack frame's own X and Y, unit (Z is -rackaxis) */
	f32 racky[3];
	s32 hasrack;     /* the model has a rack frame (a slide part, or the row's rackmtx): 0 (the
	                  * Tranquilizer) gives the clip's areas only, the rack fields zero and no rack
	                  * area */
	s32 numreach;    /* reach points in use: 2 per def->clipreach point (1 with clipreachoneside) */
	f32 reach[2 * VR_RELOAD_REACH_MAX][3]; /* def->clipreach points on the clip, as given and with x
	                                        * negated (as given only with clipreachoneside), seated */
	s32 hasslap;     /* a row with rackslap: slappos is def->slappoint on the seated clip (a row with
	                  * a close: on the gun body, the push's point, when it has a slaprad) */
	f32 slappos[3];
	s32 hasclose;    /* a row with a close (closeto): flickaxis and closedir, unit (or zero when the
	                  * row has none), def->flickaxis and def->closedir through the body as drawn, the
	                  * roll axis negated on a mirrored draw */
	f32 flickaxis[3];
	f32 closedir[3];
};

/* The same, placed in view space (the gCtrlPos frame, cm) for the current tick */
struct vrrpartsview {
	f32 clippos[3];
	f32 clipaxis[3];
	f32 rackpos[3];
	f32 rackaxis[3];
	f32 rackorigin[3];
	f32 rackx[3];
	f32 racky[3];
	s32 hasrack;
	s32 numreach;
	f32 reach[2 * VR_RELOAD_REACH_MAX][3];
	s32 hasslap;
	f32 slappos[3];
	s32 hasclose;
	f32 flickaxis[3];
	f32 closedir[3];
};

static struct vrrparts sParts[2] = { { -1 }, { -1 } };

/* The clip in the off-hand as last drawn (the copy-weapon model at holdframe), from
 * vrReloadCaptureHeldClip, in the off-hand controller's frame: cm relative to gCtrlPos, unturned by
 * the controller rotation of that frame. The hand pose is fixed, so any capture for the same gun
 * places the clip for every later tick, including the ticks while it is guided and not drawn. */
struct vrrheld {
	s32 weaponnum; /* gun it was captured for; -1: none */
	f32 top[3];    /* top of the clip, the end that enters the well first (an intake row: heldtop) */
	f32 axis[3];   /* insert direction, base to top, unit; zero for an intake row */
};

static struct vrrheld sHeld[2] = { { -1 }, { -1 } };

static s32 sGuided[2];         /* held clip is lined up with the well and drawn sliding into it */
static f32 sProgress[2];       /* cm the guided clip is in, 0..clipinsertdepth */
static f32 sFinish[2];         /* cm still to travel after the insert, 0 once seated */
static f32 sFinishStart[2];    /* sFinish at the insert */
static s32 sFinishFrame[2];    /* lvframenum of the last sFinish step */
static f32 sHolderOpen[2];     /* the holder's openness in time, for a row with one: 1 open, down to
                                * 0 shut (as the gun's animation poses it) over VRR_HOLDER_CLOSE_60THS */
static s32 sHolderFrame[2];    /* lvframenum of the last sHolderOpen step */
static f32 sRackBack[2];       /* a row with rackmove: cm its rack matrix is drawn back, the pull while
                                * racking, then going forward over VRR_RACK_RETURN_60THS; 0 at rest */
static s32 sRackBackFrame[2];  /* lvframenum of the last sRackBack step */
static s32 sPumpRide[2];       /* a row with pumpaction: the pump was pulled fully back with the grip
                                * still held, so the forend goes on following the hand's pull (sRackBack)
                                * until it is back within VRR_PUMP_SHUT_FRAC of forward or the grip lets
                                * go; no shot meanwhile (vrReloadStepRack) */
static s32 sShellGrip[2];      /* a row with shellbyshell: the free hand's grip held a shell and has not
                                * been let go since, so it takes no pump: a shell pushed in with the grip
                                * held leaves the gun waiting for its pump with the hand in the pump box
                                * (Task J6b, the user: "should not be able to grab the pump with a shotgun
                                * shell in the hand") */
static s32 sDryPump[2];        /* a row with pumpaction, ready and dry: 1 while the free hand's grip holds
                                * the forend (taken in the pump box or as the support grip), 2 once
                                * this stroke has clicked (VRR_DRY_PUMP_FRAC); 0: not held */
static f32 sDryPumpFwd[2];     /* its stroke's most forward place on the rack axis (along) */
static s32 sPumpZone[2];       /* a row with pumpaction: the free hand is on the forend (the pump box or
                                * the support grip, not at the shoulder, no shell held), whatever the
                                * gun's state, for sDryPump */
static f32 sPumpMaxPull[2];    /* a row with pumpaction: the longest pull of the stroke under way, for
                                * the PD-VRR "pump:" lines */
static f32 sPumpFwd[2];        /* a row with pumpaction: the most forward place on the rack axis (along)
                                * since the grab, tracked ticks only: the stroke's pull and the drawn
                                * forend are measured from it, so the hand settling forward after the
                                * grab costs no travel; the ride's end is still tested against the grab
                                * point (sRackGrab), so a drift forward never asks for a longer return */
static s32 sShotSupport[2];    /* a row with emptyejects: the shot opened the gun (not B) while the free
                                * hand gripped it as the support (vrTwoHandGripHeld), so that grip stays
                                * the support, the two-hand aim on, until it is let go: the gun does not
                                * jump to the gun hand's aim at the shot. Not mid-reload meanwhile
                                * (vrReloadMidReload); a new grip press while the gun is open is (F9a) */
static f32 sPlayFrame[2];      /* a row with a playback (playto): the reload animation frame the gun
                                * plays after the insert, playfrom..playto; 0: not playing */
static s32 sPlayFrameNum[2];   /* lvframenum of the last sPlayFrame step */
static s32 sPlayDrawn[2];      /* render frames that drew this playback, for the end's log line */
static s32 sPlayLogged[2];     /* the render has logged this playback (its first draw, or why not) */
static s32 sPlayEject[2];      /* the playback is the eject's (ejectfrom..ejectto on ejectmtx, posed on
                                * bodymtx: the Laptop Gun's pump), not the insert's */
static s32 sPlayClose[2];      /* with sPlayEject: the playback is the close's (closefrom..closeto on
                                * ejectmtx: the DY357's cylinder swinging back in), not the eject's */
static s32 sCasingsDue[2];     /* a row with ejectcasings: the casings its eject playback under way
                                * throws at its end (the chambers loaded last that the eject left
                                * without a live round) */
static s32 sCasings[2];        /* casings thrown and not yet drawn (vrReloadTakeCasings) */
/* A row with ejectcasings: the rounds the last load put in (each such chamber holds a live round or,
 * once fired, a casing; the others are empty), so B throws a casing for each one fired, this less the
 * live rounds it takes back. -1: not known (the gun drawn from the engine's count, or redrawn shut from
 * a saved shape): the clip size, less sCapped. Per engine hand, swapped with its gun on a flip */
static s32 sCylinder[2] = { -1, -1 };
static s32 sCapped[2];         /* rounds a redraw took from a gun restored shut with sCylinder unknown
                                * (vrReloadDrawFill: another gun used the reserve meanwhile), which were
                                * never fired: no casing for them */
static s32 sPlayHeldFrame[2] = { -1, -1 }; /* lvframenum the insert's playback last drew the held
                                * part in the gun: the fall of a row with playdrop starts from that
                                * draw (vrReloadDropClip records it) */

/* The held part's matrix (heldmtx) as last drawn in the off-hand (vrReloadCaptureHeldClip), in the
 * off controller's frame: rows turned by vrReloadViewToGun, the origin cm from gCtrlPos. The copy
 * model's matrices carry no bg_scale. For the hand-off of a row with playease */
struct vrrheldmtx {
	s32 weaponnum; /* -1: none */
	f32 rows[3][3];
	f32 pos[3];
};

static struct vrrheldmtx sHeldMtx[2] = { { -1 }, { -1 } };

/* A row with playease: where the held part was at the load, in the gun hand's frame (rows turned by
 * vrReloadViewToGun with posrotmtx, the origin cm from the anchor, vrReloadGunAnchor), so it stays
 * on the gun while the playback's pose takes over; left: 60ths of game time still to go, 0: none */
struct vrrhandoff {
	f32 left;
	f32 rows[3][3];
	f32 pos[3];
};

static struct vrrhandoff sHandoff[2];
static s32 sInsert2At[2] = { -1, -1 }; /* g_Vars.lvframe60 at which the row's sndinsert2 plays, -1: none */
static s32 sInsert2Snd[2];              /* the sound it plays then: sndinsert2, or sndfull for a shell that
                                         * fills the gun (Task F11d) */

/* Reload hints (Task F9d): the row's hint (vrreloaddef.hint) on the HUD as "<gun name>: <hint>", in
 * the holsters' message slot (vrHolsterHudLine) for VRR_HINT_SHOW_60THS (Task F10b: the default 80 was
 * gone too soon), in the HUD's blue (COLOR_09_BLUE, Task F11b: the user asked for blue, the holster
 * messages stay green), for the driven gun: the first time each row's gun
 * is reloaded in a session (its first eject), and when the player is stuck: half reloaded for
 * VRR_HINT_STUCK_60THS (the clip out with rounds in the reserve and none taken, or a rack or slap
 * due: vrReloadWaitsForRack) or VRR_HINT_MISSES missed racks or slaps in a row (a grip press that
 * does not start a due grip rack, a fast swing up refused near the slap point), no sooner than
 * VRR_HINT_GAP_60THS after the last hint. The first-reload marks belong to the agent file (Task J2,
 * vrReloadHintAgent); the rest starts again with each level and life (vrReloadResetAll) */
#define VRR_HINT_STUCK_60THS 300
#define VRR_HINT_GAP_60THS   900
#define VRR_HINT_SHOW_60THS  120 /* on screen 1.5 times the default message's 80 (Task F10b) */
#define VRR_HINT_MISSES      3
#define VRR_HINT_ROWS        64
static u8 sHintSeen[VRR_HINT_ROWS]; /* per row (vrReloadDefAt's index): its first-reload hint shown,
                                     * to the agent in sHintGuid */
static s32 sHintLast = -100000;     /* lvframe60 of the last hint */
static s32 sHintStuckKind;          /* 0: not stuck, 1: the clip out and none taken, 2: a rack or slap due */
static s32 sHintStuckSince;         /* lvframe60 the current kind began */
static s32 sHintMisses;             /* missed racks or slaps in a row */

/* The stuck state belongs to the driven gun: a switch, a suspend or a new level starts it again */
static void vrReloadHintReset(void)
{
	sHintStuckKind = 0;
	sHintStuckSince = 0;
	sHintMisses = 0;
}

/* The first-reload marks of each agent file (Task J2, the user: "initial reload prompt should only
 * happen the first time a weapon is picked up per account"): VRR_HINT_FILE holds a line per agent,
 * "agent=<fileid>,<deviceserial> seen=<weapon>.<ammoindex>,...", a row named by its weapon and ammo
 * index so the marks outlast rows added to the table. vrReloadHintAgent reads the line of the agent
 * in g_GameFileGuid into sHintSeen when the agent changes; vrReloadHintSave writes it back on each
 * new mark and keeps the other agents' lines */
#define VRR_HINT_FILE      "pd-vr-hints.ini"
#define VRR_HINT_FILE_MAX  8192
#define VRR_HINT_LINE_MAX  1024
static struct fileguid sHintGuid;
static s32 sHintGuidRead; /* sHintSeen holds sHintGuid's marks */

static s32 vrReloadHintRowOf(s32 weaponnum, s32 ammoindex)
{
	s32 i;

	for (i = 0; i < vrReloadDefCount() && i < VRR_HINT_ROWS; i++) {
		struct vrreloaddef *d = vrReloadDefAt(i);

		if (d->weaponnum == weaponnum && d->ammoindex == ammoindex) {
			return i;
		}
	}

	return -1;
}

/* The line's start for an agent: "agent=<fileid>,<deviceserial> " */
static void vrReloadHintPrefix(char *buf, size_t size, const struct fileguid *guid)
{
	snprintf(buf, size, "agent=%d,%d ", guid->fileid, guid->deviceserial);
}

static void vrReloadHintAgent(void)
{
	char prefix[40];
	char line[VRR_HINT_LINE_MAX];
	FILE *f;

	if (sHintGuidRead && sHintGuid.fileid == g_GameFileGuid.fileid
			&& sHintGuid.deviceserial == g_GameFileGuid.deviceserial) {
		return;
	}

	sHintGuid = g_GameFileGuid;
	sHintGuidRead = 1;
	memset(sHintSeen, 0, sizeof(sHintSeen));
	vrReloadHintPrefix(prefix, sizeof(prefix), &sHintGuid);
	f = fopen(VRR_HINT_FILE, "r");

	if (f == NULL) {
		return;
	}

	while (fgets(line, sizeof(line), f) != NULL) {
		const char *p;
		s32 weaponnum, ammoindex, used, row;

		if (strncmp(line, prefix, strlen(prefix)) != 0 || (p = strstr(line, "seen=")) == NULL) {
			continue;
		}

		p += 5;

		while (sscanf(p, "%d.%d%n", &weaponnum, &ammoindex, &used) == 2) {
			row = vrReloadHintRowOf(weaponnum, ammoindex);

			if (row >= 0) {
				sHintSeen[row] = 1;
			}

			p += used;

			if (*p != ',') {
				break;
			}

			p++;
		}

		break;
	}

	fclose(f);
}

static void vrReloadHintSave(void)
{
	static char keep[VRR_HINT_FILE_MAX];
	char prefix[40];
	char line[VRR_HINT_LINE_MAX];
	size_t used = 0, len;
	const char *sep = "";
	FILE *f = fopen(VRR_HINT_FILE, "r");
	s32 i;

	vrReloadHintPrefix(prefix, sizeof(prefix), &sHintGuid);

	if (f != NULL) {
		while (fgets(line, sizeof(line), f) != NULL) {
			len = strlen(line);

			if (strncmp(line, prefix, strlen(prefix)) != 0) {
				if (used + len + 1 >= sizeof(keep)) {
					sysLogPrintf(LOG_WARNING, "vr_reload: %s is over %d bytes: other agents' marks dropped",
							VRR_HINT_FILE, VRR_HINT_FILE_MAX);
					break;
				}

				memcpy(keep + used, line, len);
				used += len;

				if (keep[used - 1] != '\n') {
					keep[used++] = '\n';
				}
			}
		}

		fclose(f);
	}

	/* Written beside the file and renamed over it, so a crash mid-write keeps the old marks */
	f = fopen(VRR_HINT_FILE ".tmp", "w");

	if (f == NULL) {
		sysLogPrintf(LOG_WARNING, "vr_reload: could not write %s", VRR_HINT_FILE);
		return;
	}

	fwrite(keep, 1, used, f);
	fprintf(f, "%sseen=", prefix);

	for (i = 0; i < vrReloadDefCount() && i < VRR_HINT_ROWS; i++) {
		if (sHintSeen[i]) {
			fprintf(f, "%s%d.%d", sep, vrReloadDefAt(i)->weaponnum, vrReloadDefAt(i)->ammoindex);
			sep = ",";
		}
	}

	fputc('\n', f);

	if (fclose(f) != 0 || rename(VRR_HINT_FILE ".tmp", VRR_HINT_FILE) != 0) {
		sysLogPrintf(LOG_WARNING, "vr_reload: could not write %s", VRR_HINT_FILE);
	}
}

/* The slap of a row with rackslap (see VRR_SLAP_SPEED), per engine hand */
struct vrrslap {
	f32 frame;      /* the reload animation frame the free hand is drawn at, slapfrom..slapto; 0: none */
	s32 pending;    /* the drawn palm reached slapcontact: the core gets the slap (vrrinput.slap) on
	                 * the next tracked tick that is not paused */
	f32 ease;       /* 60ths of game time left of the hand's way back to the controller after the
	                 * slap frames (the frame stays put meanwhile); 0: not easing */
	s32 belowat;    /* g_Vars.lvframe60 the palm was last under the slap point, -1: never */
	s32 checkframe; /* lvframenum of the last step (once per frame) */
	s32 swing;      /* a swing up at VRR_SLAP_LOG_SPEED or more is under way, for the log (a row with a
	                 * close: a roll about flickaxis at VRR_FLICK_LOG_RADS or more either way) */
	f32 peak;       /* its fastest up speed (m/s) and the side speed then (a close's roll: its
	                 * largest roll rate, signed, and the whole turn rate then, rad/s) */
	f32 peakside;
	f32 mindist;    /* its palm's nearest to the slap point (cm), and its height over the slap */
	f32 minalong;   /* point along the axis there (negative: under it) */
	s32 fastaxial;  /* it went up fast enough and straight enough on some frame */
	s32 swingbelow; /* it came from below on some frame */
	s32 prevok;     /* prevd is the palm's offset from the slap point at the last checked frame, */
	f32 prevd[3];   /* in the gun hand's frame (vrReloadViewToGun): the start of this frame's path */
};

static struct vrrslap sSlap[2];

/* The gun's seated clip matrix as last drawn, in cm (the gun's matrices over bg_scale), for the slap's
 * drawn hand (vrReloadPlaceSlapHand); with the weapon and lvframenum of the capture */
static Mtxf sSlapClip[2];
static s32 sSlapClipFrame[2];
static s32 sSlapClipWeapon[2] = { -1, -1 };

/* The same capture's gun body (bodymtx) in cm, for the slap's drawn hand of a row whose clip plays with
 * the slap (vrReloadHasSlapPlay: the Laptop Gun), placed by the body */
static Mtxf sSlapBody[2];

/* The held hand's wrist (matrix 18) as last aligned (vrReloadAlignHeldHand), in the off controller's
 * frame (rows turned by vrReloadViewToGun, the origin cm from gCtrlPos): where the slap's hand goes
 * back to, as an emptied clip hand is drawn there until the fists take over */
struct vrrwrist {
	s32 weaponnum; /* -1: none */
	f32 rows[3][3];
	f32 pos[3];
};

static struct vrrwrist sHoldWrist[2] = { { -1 }, { -1 } };

#define VRR_RACK_EASE_60THS 8.0f

static f32 sRackEase[2];
static f32 sRackLastFrame[2];
static f32 sRackPull[2];

/* The gun's drawn slide matrix in cm (matrices over bg_scale), for placing the rack hand */
static Mtxf sRackSlide[2];
static s32 sRackSlideFrame[2];
static s32 sRackSlideWeapon[2] = { -1, -1 };
static s32 sRackSlideIndex[2] = { -1, -1 };

/* The gun body matrix in cm (matrices over bg_scale), for rebasing the rack and guided hands */
static Mtxf sGunBody[2];
static s32 sGunBodyFrame[2];
static s32 sGunBodyWeapon[2] = { -1, -1 };
static s32 sGunBodyIndex[2] = { -1, -1 };

/* The gun's drawn clip matrix while guided in cm */
static Mtxf sGuidedClip[2];
static s32 sGuidedClipFrame[2];
static s32 sGuidedClipWeapon[2] = { -1, -1 };

static void vrReloadSlapReset(s32 h)
{
	memset(&sSlap[h], 0, sizeof(sSlap[h]));
	sSlap[h].belowat = -1;
}
static struct anim sPlayAnim;  /* the playback's own anim state: the hand's (hand->anim) is never touched */
static s32 sPlayAnimReady;
/* The playback's pose with an identity root: never drawn, so not from display-list memory. The
 * largest model played is the DY357's, 47 matrices (the Laptop Gun's has 46, the Phoenix's 40, the
 * Callisto's 43); a larger one than this is not played (vrReloadPosePlay logs it). */
#define VRR_PLAY_MAX_MTX 48
static Mtxf sPlayPosed[VRR_PLAY_MAX_MTX];

/* A held last frame (ejecthold, playhold) poses the same frame on every draw: its matrices against the
 * body (posed[k] * posed[body]^-1, model units), posed once per weapon, frame and gun side and put on
 * the drawn body from then on (vrReloadPosePlay) */
struct vrrholdpose {
	s32 weaponnum; /* -1: none */
	s32 animnum;
	s32 eject;
	f32 frame;
	s32 flip;      /* the anim's flip (the left gun of a pair) */
	s32 lefthanded; /* VrLeftHandedMode at the pose: the gun side */
	Mtxf rel[VRR_PLAY_MAX_MTX];
};

static struct vrrholdpose sHoldPose[2] = { { -1 }, { -1 } };

/* The ejected clip's fall. World units per metre: 100, because gCtrlPos is the OpenXR position in
 * metres * 100 (vr_input.cpp) and the gun is drawn there as camera-space world units with no other
 * scale (hand->posoffset; the muzzle and the old markers convert it to world with
 * camGetProjectionMtxF alone). One real metre of hand travel is therefore 100 world units.
 * (vr_world_scale / bg_scale = 85 sets only the eye separation.) */
#define VRR_WORLD_UNITS_PER_M 100.0f
#define VRR_DROP_SPEED        0.5f  /* m/s out of the well, along the reversed insert axis */
#define VRR_DROP_GRAVITY      9.8f  /* m/s^2, world -Y */
#define VRR_DROP_SECS         1.0f  /* the clip is drawn this long after the eject, then hidden */
#define VRR_DROP_TUMBLE_DEG   90.0f /* deg/s end over end, about the clip's local X */
#define VRR_DROP_REST_CM      1.5f  /* half the clip's thickness: its centre line rests this high */
#define VRR_PLAYER_MAX_STEP   50.0f /* world units: a larger move of the player in one frame is a
                                     * warp, not walking, and gives no velocity */

/* The gun's clip matrix as last drawn (Task 15's slide included), in world space: rot rows are the
 * matrix's rotation rows without bg_scale, turned into world (they keep the model scale and the
 * left-handed X mirror), pos the clip joint. Recorded each frame by vrReloadDropClip, so an eject
 * starts the fall from where the clip was on screen. */
struct vrrclipworld {
	s32 weaponnum; /* -1: none */
	s32 frame;     /* g_Vars.lvframenum of the record */
	f32 rot[3][3];
	f32 pos[3];
};

static struct vrrclipworld sClipWorld[2] = { { -1 }, { -1 } };

/* The record is for this gun and from the last few drawn frames: a fall can start from it */
static s32 vrReloadClipWorldFresh(s32 h, struct hand *hand)
{
	struct vrrclipworld *c = &sClipWorld[h];

	return c->weaponnum >= 0 && c->weaponnum == hand->gset.weaponnum
		&& g_Vars.lvframenum - c->frame >= 0 && g_Vars.lvframenum - c->frame <= VRR_PARTS_MAX_AGE;
}

/* A falling clip, world space. The physics runs on the clip's centre (pivot, in the clip matrix's
 * local model units), which is also the tumble pivot. */
struct vrrdrop {
	s32 active;
	s32 weaponnum;
	s32 stepframe;  /* lvframenum of the last step, -1 before the first */
	s32 drawframe;  /* lvframenum of the last override (or the start) */
	s32 landed;
	f32 secs;       /* since the eject */
	f32 rot[3][3];  /* world rotation rows at the eject */
	f32 pivot[3];
	f32 centre0[3];
	f32 vel[3];     /* world units per second */
	f32 floory;     /* the floor under the player at the eject; the clip rests on it */
	s32 usefloor;   /* the clip started above that floor */
	f32 centre[3];  /* now */
	f32 angle;      /* tumble now, radians */
	s32 tumble;     /* the local axis it tumbles about: 0 (x) for a clip, end over end; 2 (z) for a
	                 * held part whose x is its length (the Cyclone's bar), so it turns end over end
	                 * too, not about its own length */
};

static struct vrrdrop sDrop[2];

/* The player's own movement (walking, running, falling), world units per second, measured from the
 * player prop's position once per frame by vrReloadTrackPlayer. Added to a dropped clip's start
 * velocity, so a clip ejected on the run carries the run. */
static f32 sPlayerPos[3];
static f32 sPlayerVel[3];
static s32 sPlayerFrame = -1; /* lvframenum of the last sample, -1: none */

/* Shape (clip in or out, chamber, clip rounds) of each owned gun put away by a weapon switch,
 * keyed by weapon number, engine hand and ammo slot (each magazine of a gun with a row per magazine,
 * the SuperDragon's, keeps its own: one slot per row and hand): the second copy of a pair
 * (HAND_LEFT) keeps its own. A
 * slot holds no rounds: every round of a holstered gun is in the reserve, as the engine keeps it
 * (bgunFreeWeapon returns loadedammo, the suspend returns an uncounted clip). vrReloadDrawFill takes
 * the rounds back out when the gun is drawn, so dropping a slot at any time is ammo-safe. Must be
 * at least twice vrReloadDefCount() (checked in vrReloadResetAll): a gun that finds no slot gets
 * the engine's full refill on its next draw. A pair in hand has no slot for its weapon (each draw
 * used its own), so a gun side flip (vrReloadSwapHands) leaves the slots alone. 18: exactly the
 * nine rows (the three Falcon 2s, the MagSec 4, the Mauler, the Tranquilizer, the Phoenix, the
 * Psychosis Gun, the CMP150) in both hands, though the Tranquilizer and the Psychosis Gun are
 * single-wield and need only one. 22: the eleven rows with the AR34 and the K7 Avenger (single-wield
 * too). 30: the fifteen rows with the RC-P120, the Dragon, the SuperDragon and the Sniper Rifle
 * (single-wield too). 36: the eighteen rows with the Callisto, the Cyclone (which dual wields) and
 * the Laptop Gun (Task F6). 38: the nineteen rows with the SuperDragon's grenade magazine (Task
 * F9e). 40: the twenty with the Shotgun (Task G0c, single-wield): a Shotgun put away always finds its
 * slot, so its draw restores it and never takes the engine's refill (bgun0f098f8c, which for the
 * Shotgun, the Crossbow and the DY357s adds the rounds a holstered gun "reloaded" meanwhile,
 * gunroundsspent). 46: the twenty-three rows with the Reaper, the Devastator and the Farsight (Task
 * G1, all single-wield). 48: the twenty-four with the Rocket Launcher (Task G2, single-wield, not one
 * of gunroundsspent's guns). 50: the twenty-five with the Crossbow (Task G3, single-wield, one of
 * gunroundsspent's: a Crossbow put away always finds its slot, as the Shotgun). 54: the twenty-seven
 * with the DY357 Magnum and the DY357-LX (Task G4, which dual wield, and are gunroundsspent's: each
 * copy of a pair put away finds its slot); a twenty-eighth row must raise it. */
#define VRR_NUM_SAVED 54

struct vrrsaved {
	s32 weaponnum; /* -1: free */
	s32 handnum;
	s32 ammoindex; /* the magazine's row's ammoindex */
	struct vrrgun gun;
};

/* Freed by vrReloadFreeSaved, not by an initialiser: a hand-written list shorter than the array
 * would leave the slots it misses at weapon 0, not free */
static struct vrrsaved sSaved[VRR_NUM_SAVED];
static s32 sSavedReady; /* 0 until vrReloadFreeSaved has run once */

/* Frees every slot. vrReloadResetAll runs it, and the slot lookups run it before the first reset */
static void vrReloadFreeSaved(void)
{
	s32 i;

	for (i = 0; i < VRR_NUM_SAVED; i++) {
		sSaved[i].weaponnum = -1;
	}

	sSavedReady = 1;
}

/* The magazines of the gun in hand that the core is not driving, per engine hand and ammo slot (a
 * row's ammoindex, an index into loadedammo): the shapes vrReloadDrawFill restored, applied on the
 * first owned tick after the draw, and, for a gun with a row per magazine (the SuperDragon's rifle
 * clip and grenade magazine, Task F9e), the other function's magazine, parked by a function change
 * (vrReloadSwapMagazine) and driven again from here on the next change back. Its loaded rounds are
 * in loadedammo (the engine keeps both slots while the gun is in hand); an uncounted clip (in, with
 * the chamber empty) is in the reserve and taken from it when the shape is applied. So dropping one
 * at any time is ammo-safe, as a saved slot: the engine then has the magazine as its loadedammo says.
 * Only one magazine of a hand is driven (sGun) at a time, so the B, guide, rack and slap state per
 * hand needs no copy per magazine: a function change ends every grab, as a weapon switch does, and the
 * other magazine's shape is all that stays (it is drawn from here, vrReloadParkedDef). */
#define VRR_MAGS 2
static struct vrrgun sPending[2][VRR_MAGS];
static s32 sPendingWeapon[2][VRR_MAGS] = { { -1, -1 }, { -1, -1 } };

static void vrReloadDropPending(s32 h)
{
	s32 i;

	for (i = 0; i < VRR_MAGS; i++) {
		sPendingWeapon[h][i] = -1;
	}
}

struct vrreloaddef *vrReloadOwnedDef(s32 handnum)
{
	struct player *player = g_Vars.currentplayer;

	/* VR is single-player: never drive another player's gun from these statics */
	if (player == NULL || g_Vars.currentplayernum != 0) {
		return NULL;
	}

	if (!VrManualReloading || handnum < 0 || handnum >= 2) {
		return NULL;
	}

	/* Manual reload turns on the holsters, which make pairs: both guns of a pair are owned, the
	 * right one driven, the left one only tracked (vrReloadTick). The left hand's gun is judged by
	 * the hand in use, not gunctrl.dualwielding: the holsters, the weapon menu and the cycle
	 * buttons set that flag when the switch is requested, while the gun to put away is still in
	 * hand, and the switch must still save that gun's state. */
	if (handnum == HAND_LEFT && !player->hands[HAND_LEFT].inuse) {
		return NULL;
	}

	/* The cheat refills loadedammo every frame, which the core would fight */
	if (cheatIsActive(CHEAT_UNLIMITEDAMMONORELOADS)) {
		return NULL;
	}

	/* The magazine the core drives while it drives this gun: a function change to another magazine's
	 * function is taken over on the next tick (vrReloadSwapMagazine), so the render and the getters
	 * never pair one magazine's row with the other's state. Else the selected function's */
	if (sGunWeapon[handnum] >= 0 && sGunWeapon[handnum] == player->hands[handnum].gset.weaponnum
			&& sAmmoIndex[handnum] >= 0) {
		struct vrreloaddef *driven = vrReloadGetDef(sGunWeapon[handnum], sAmmoIndex[handnum]);

		if (driven != NULL) {
			return driven;
		}
	}

	return vrReloadSelectedDef(handnum);
}

/* The row of the selected function's magazine for this hand's gun (its ammoindex's), else, for a
 * function with no ammo (the pistol whip, the Dragon's self-destruct) or none with a row, the gun's
 * first row (the primary's); NULL when the gun has no row. No ownership gates */
static struct vrreloaddef *vrReloadSelectedDef(s32 handnum)
{
	s32 weaponnum = g_Vars.currentplayer->hands[handnum].gset.weaponnum;
	struct weaponfunc *func = currentPlayerGetWeaponFunction(handnum);
	struct vrreloaddef *def = NULL;

	if (func != NULL && func->ammoindex >= 0) {
		def = vrReloadGetDef(weaponnum, func->ammoindex);
	}

	return def != NULL ? def : vrReloadGetDef(weaponnum, -1);
}

bool vrReloadOwnsGun(s32 handnum)
{
	return vrReloadOwnedDef(handnum) != NULL;
}

bool vrReloadOwnsAmmo(s32 handnum, s32 ammoindex)
{
	if (!vrReloadOwnsGun(handnum)) {
		return false;
	}

	/* A function with no ammo of its own (the pistol whip, the Dragon's self-destruct, the Sniper
	 * Rifle's crouch) goes with the gun */
	if (ammoindex < 0) {
		return true;
	}

	/* An ammo slot with a row (the SuperDragon's rifle clip and grenade magazine both have one, Task
	 * F9e): each is driven while its function is selected */
	return vrReloadGetDef(g_Vars.currentplayer->hands[handnum].gset.weaponnum, ammoindex) != NULL;
}

bool vrReloadOwnsSelectedFunc(s32 handnum)
{
	struct weaponfunc *func;

	if (!vrReloadOwnsGun(handnum)) {
		return false;
	}

	func = currentPlayerGetWeaponFunction(handnum);

	return vrReloadOwnsAmmo(handnum, func != NULL ? func->ammoindex : -1);
}

void vrReloadRequestEject(s32 handnum)
{
	if (handnum >= 0 && handnum < 2) {
		sEject[handnum] = 1;
		sEjectSince[handnum] = -1;
	}
}

void vrReloadBuzz(s32 ctrl, f32 amp, f32 secs)
{
	f32 strength = inputRumbleGetStrength(0);

	if (strength > 0.0f) {
		trigger_haptic_vibration_c(ctrl, amp * strength, secs);
	}
}

/* vrrSettle with this magazine's row: a gun whose row loads on insert has no rack */
static void vrReloadSettle(struct vrrgun *g, struct vrreloaddef *def)
{
	vrrSettle(g, def != NULL && def->loadsoninsert);
}

/* The hand's attack is a burst with shots still to come (the Shotgun's double blast, FUNCFLAG_BURST2:
 * its second shell 13 ticks after the first, bgun0f09a3f8), past its first shot */
static s32 vrReloadBurstGoesOn(struct hand *hand)
{
	struct weaponfunc *func = gsetGetWeaponFunction(&hand->gset);
	s32 count = 0;

	if (hand->state != HANDSTATE_ATTACK || func == NULL || hand->burstbullets <= 0) {
		return 0;
	}

	if (func->flags & FUNCFLAG_BURST2) {
		count = 2;
	} else if (func->flags & FUNCFLAG_BURST3) {
		count = 3;
	} else if (func->flags & FUNCFLAG_BURST5) {
		count = 5;
	}

	return hand->burstbullets < count;
}

/* Folds the engine's count of the hand's driven magazine (shots fired) into its state, with the row's
 * pumpaction: a shot leaves the chamber empty for the pump. A shot of a burst with more to come
 * chambers the next round from the tube as a gun with no pump does, so the double blast fires both its
 * shells, as in the original game (two rounds, the second from the tube), and the pump follows the
 * burst's last; with nothing left in the tube the second finds the count at 0 and the engine stops the
 * burst after one */
static void vrReloadSync(s32 h, struct hand *hand, struct vrreloaddef *def, s32 loaded)
{
	vrrSyncFromEngineRow(&sGun[h], loaded, def != NULL && def->pumpaction && !vrReloadBurstGoesOn(hand));
}

/* The row has a clip part in the gun. Without one (the Phoenix) it has an intake point instead of
 * a well: see vr_reload_data.h */
static s32 vrReloadHasClip(struct vrreloaddef *def)
{
	return def->clippart >= 0;
}

/* The rack is the model's slide part (rackmtx -1), which the engine's slide offset (hand->slidetrans)
 * moves and vrReloadMoveSlide pulls with the hand. A row with a rackmtx racks on a part that does not
 * move (the CMP150's body): the engine's slide is never touched for it (and only a model with a
 * slide part draws one) */
static s32 vrReloadRackIsSlide(struct vrreloaddef *def)
{
	return def != NULL && def->rackmtx < 0;
}

/* A row with a playback (playto) it can play: it poses its matrices on the gun body, intakemtx, so
 * only a row with no clip part has one, and it runs from playfrom (above 0, which is "not playing")
 * up to playto (vrReloadResetAll warns otherwise) */
static s32 vrReloadHasPlay(struct vrreloaddef *def)
{
	return def->playto > 0.0f && !vrReloadHasClip(def) && def->intakemtx >= 0 && def->playfrom > 0.0f
		&& def->playfrom < def->playto && def->playmtx >= 0 && def->playmtxlast >= def->playmtx;
}

/* The body a row's playbacks are posed on: a row with a clip part's bodymtx, an intake row's intakemtx
 * (the gun body, as for its insert's) */
static s32 vrReloadPlayBody(struct vrreloaddef *def)
{
	return vrReloadHasClip(def) ? def->bodymtx : def->intakemtx;
}

/* A row with an eject playback (ejectto) it can play: posed on its body (vrReloadPlayBody: the Laptop
 * Gun's bodymtx, the Rocket Launcher's intakemtx), from ejectfrom (above 0) up to ejectto
 * (vrReloadResetAll warns otherwise) */
static s32 vrReloadHasEjectPlay(struct vrreloaddef *def)
{
	return def->ejectto > 0.0f && vrReloadPlayBody(def) >= 0 && def->ejectfrom > 0.0f
		&& def->ejectfrom < def->ejectto && def->ejectmtx >= 0 && def->ejectmtxlast >= def->ejectmtx;
}

/* The eject playback's last frame held while the gun has no clip in (ejecthold: the Rocket Launcher's
 * hatch, open from the shot until a rocket is in) */
static s32 vrReloadHoldsEject(struct vrreloaddef *def)
{
	return def->ejecthold && vrReloadHasEjectPlay(def);
}

/* A row held open after its load too, until a flick or a push closes it (closeto: the DY357's cylinder):
 * an intake row racked by that close (rackslap), whose eject playback is held open, with the close's
 * frames in order (vrReloadResetAll warns otherwise) */
static s32 vrReloadHasClose(struct vrreloaddef *def)
{
	return def->closeto > 0.0f && def->closefrom > 0.0f && def->closefrom < def->closeto && def->rackslap
		&& !vrReloadHasClip(def) && vrReloadHoldsEject(def);
}

/* The gun is drawn held open at the eject's last frame: from the eject until the load (ejecthold), and,
 * on a row with a close, until the close (its rack, the core's VRR_NEED_RACK: loaded and still open) */
static s32 vrReloadHeldOpen(s32 h, struct vrreloaddef *def)
{
	return vrReloadHoldsEject(def)
		&& (!sGun[h].magin || (vrReloadHasClose(def) && sGun[h].state == VRR_NEED_RACK));
}

/* The insert playback's held part kept drawn at playto while a round is chambered (playhold: the
 * Crossbow's bolt at the nock) */
static s32 vrReloadHoldsPlay(struct vrreloaddef *def)
{
	return def->playhold && vrReloadHasPlay(def);
}

/* A row with a clip part whose playback (playto) runs with the slap, on bodymtx, posing its clip (the
 * Laptop Gun's clip rocked flat and slid into the seat), from playfrom (above 0) up to playto */
static s32 vrReloadHasSlapPlay(struct vrreloaddef *def)
{
	return def->playto > 0.0f && vrReloadHasClip(def) && def->rackslap && def->bodymtx >= 0
		&& def->playfrom > 0.0f && def->playfrom < def->playto && def->playmtx >= 0
		&& def->playmtxlast >= def->playmtx && def->clipmtx >= def->playmtx && def->clipmtx <= def->playmtxlast;
}

/* The eject playback carries the clip out and drops it at its end (ejectdrop) */
static s32 vrReloadEjectDrops(struct vrreloaddef *def)
{
	return def->ejectdrop && vrReloadHasClip(def) && vrReloadHasEjectPlay(def);
}

/* The playback under way (sPlayEject: the eject's) poses the row's clip matrix */
static s32 vrReloadPlaysClip(struct vrreloaddef *def, s32 eject)
{
	return eject ? vrReloadEjectDrops(def) : vrReloadHasSlapPlay(def);
}

/* The row's held part falls after its playback (playdrop: the Cyclone's bar) */
static s32 vrReloadPlayDrops(struct vrreloaddef *def)
{
	return def->playdrop > 0.0f && vrReloadHasPlay(def);
}

/* The matrix and part a fall draws: the clip's for a row with a clip part, the held part's for a row
 * whose held part falls after its playback; -1 for a row with neither (the Phoenix: nothing falls) */
static s32 vrReloadDropMtx(struct vrreloaddef *def)
{
	return vrReloadHasClip(def) ? def->clipmtx : (vrReloadPlayDrops(def) ? def->heldmtx : -1);
}

static s32 vrReloadDropPart(struct vrreloaddef *def)
{
	return vrReloadHasClip(def) ? def->clippart : (vrReloadPlayDrops(def) ? def->heldpart : -1);
}

/* A row with a clip seat and a slap (the Laptop Gun): the animation's clip at slapcontact against its
 * body, C = clip * body^-1 (row vectors; the turn, and the joint in model units), read once per gun
 * from the copy model posed at slapcontact (vrReloadNoteSlapClip). It is the same whatever the root
 * and its mirror. The slap's hand is placed by the clip at that pose, C on the copy model's body,
 * since the animation's clip moves under the palm in the slap frames (vrReloadPlaceSlapHand) */
struct vrrslapclip {
	s32 weaponnum; /* gun it was read for (or tried); -1: none */
	s32 valid;     /* 0: the read failed for that gun (logged once): the slap's hand is not drawn, and
	                * the copy model is not posed at the contact again */
	Mtxf c;
};

static struct vrrslapclip sSlapContact[2] = { { -1 }, { -1 } };

/* A row whose clip plays with the slap (vrReloadHasSlapPlay): the clip against the body as the reload
 * animation has it at holdframe (the Laptop Gun's, hooked in at its front, tilted 36 degrees), read
 * from the copy model posed there (vrReloadCaptureHeldClip), as sSlapContact is: an inserted or guided
 * clip is drawn so until its playback (vrReloadPlaceClip) */
static struct vrrslapclip sHookClip[2] = { { -1 }, { -1 } };

/* A playback that poses the clip (vrReloadPlaysClip): the clip against the body at the frame the
 * playback posed this lvframenum (vrReloadPosePlay), drawn after the capture (vrReloadPlaceClip) */
static Mtxf sPlayClip[2];
static s32 sPlayClipFrame[2] = { -1, -1 };

/* The slot saved for this weapon, engine hand and magazine (ammo slot), else NULL */
static struct vrrsaved *vrReloadFindSaved(s32 weaponnum, s32 handnum, s32 ammoindex)
{
	s32 i;

	if (!sSavedReady) {
		vrReloadFreeSaved();
	}

	for (i = 0; i < VRR_NUM_SAVED; i++) {
		if (sSaved[i].weaponnum == weaponnum && sSaved[i].handnum == handnum && sSaved[i].ammoindex == ammoindex) {
			return &sSaved[i];
		}
	}

	return NULL;
}

/* A free slot (either hand), else NULL */
static struct vrrsaved *vrReloadFindFreeSaved(void)
{
	s32 i;

	if (!sSavedReady) {
		vrReloadFreeSaved();
	}

	for (i = 0; i < VRR_NUM_SAVED; i++) {
		if (sSaved[i].weaponnum < 0) {
			return &sSaved[i];
		}
	}

	return NULL;
}

/* Puts one magazine's shape in its slot, settled. False when no slot is free: that magazine gets the
 * engine's refill on the next draw */
static s32 vrReloadSaveShape(s32 weaponnum, s32 h, s32 ammoindex, const struct vrrgun *g)
{
	struct vrrsaved *slot = vrReloadFindSaved(weaponnum, h, ammoindex);

	if (slot == NULL) {
		slot = vrReloadFindFreeSaved();
	}

	if (slot == NULL) {
		return 0;
	}

	slot->weaponnum = weaponnum;
	slot->handnum = h;
	slot->ammoindex = ammoindex;
	slot->gun = *g;
	vrReloadSettle(&slot->gun, vrReloadGetDef(weaponnum, ammoindex));

	return 1;
}

/* Remembers the shape of the gun being put away: the driven magazine's, and every other magazine
 * with a row (the SuperDragon's other function's): the shape parked for it (sPending), else the
 * engine's count, a clip in. Shots fired since the last tick are folded in first. A clip held in the
 * off-hand is dropped: its rounds never left the reserve. A parked uncounted clip is in the reserve
 * already, as a slot's rounds are */
static void vrReloadSave(s32 h)
{
	struct hand *hand = &g_Vars.currentplayer->hands[h];
	s32 weaponnum = sGunWeapon[h];
	s32 i;

	if (hand->gset.weaponnum == weaponnum && sAmmoIndex[h] >= 0) {
		vrReloadSync(h, hand, vrReloadGetDef(weaponnum, sAmmoIndex[h]), hand->loadedammo[sAmmoIndex[h]]);
	}

	/* more owned guns than slots: this one gets the engine's refill */
	if (!vrReloadSaveShape(weaponnum, h, sAmmoIndex[h], &sGun[h])) {
		return;
	}

	for (i = 0; i < VRR_MAGS; i++) {
		struct vrrgun g;

		if (i == sAmmoIndex[h] || vrReloadGetDef(weaponnum, i) == NULL) {
			continue;
		}

		if (sPendingWeapon[h][i] == weaponnum) {
			g = sPending[h][i];
		} else if (hand->gset.weaponnum == weaponnum) {
			vrrInit(&g, hand->loadedammo[i]);
		} else {
			continue;
		}

		sPendingWeapon[h][i] = -1;
		vrReloadSaveShape(weaponnum, h, i, &g);
	}
}

/* Ends the gun's playback of the insert (sPlayFrame), if one is under way: why is "done", "eject"
 * (B mid-playback: the gun is empty again and waits for the next one, which plays from the start),
 * "switch" (the gun is put away) or "stop" (the core stops driving it: manual reload off, the cheat,
 * an engine reload). Logged, with the frame it reached. */
static void vrReloadEndPlay(s32 h, const char *why)
{
	if (sPlayFrame[h] <= 0.0f) {
		return;
	}

	VRR_DIAG("play: frame=%d hand=%d weapon=%d end=%.1f why=%s drawn=%d eject=%d",
			g_Vars.lvframenum, h, sGunWeapon[h], sPlayFrame[h], why, sPlayDrawn[h], sPlayEject[h]);
	sPlayFrame[h] = 0.0f;
	sPlayDrawn[h] = 0;
	sPlayLogged[h] = 0;
	sPlayEject[h] = 0;
	sPlayClose[h] = 0;
	sCasingsDue[h] = 0;
	sHandoff[h].left = 0.0f;
}

/* Stop driving the gun: the core state is dropped and rebuilt on the next active tick. A clip that
 * is in the gun but not counted in loadedammo (chamber empty) goes back to the reserve, so no
 * round is ever held outside the engine's own counts while the core is not driving.
 * switching: the hand is changing gun, so the shape is saved for the next draw and a shape
 * restored by vrReloadDrawFill is kept for the first tick after it. Otherwise (manual reload
 * turned off, the cheat, an engine reload, the gun gone) the gun is handed to the
 * engine as it stands. */
static void vrReloadSuspend(s32 h, s32 switching)
{
	if (h == HAND_RIGHT) {
		vrReloadHintReset();
	}

	/* Mid-rack: the slide goes back to where it was at the grab (or stays fully back after the
	 * rack), a position bgunUpdateSlide handles for whatever gun is drawn next. A rack on a rackmtx
	 * moved no slide */
	if (sGunWeapon[h] >= 0 && sGun[h].state == VRR_RACKING && !sGun[h].racked
			&& vrReloadRackIsSlide(vrReloadGetDef(sGunWeapon[h], sAmmoIndex[h]))) {
		g_Vars.currentplayer->hands[h].slidetrans = sSlideGrab[h];
	}

	if (switching && sGunWeapon[h] >= 0) {
		vrReloadSave(h);
	}

	/* Not switching: the other magazine's parked shape goes too, its rounds where the engine has them */
	if (!switching) {
		vrReloadDropPending(h);
	}

	if (sGunWeapon[h] >= 0 && sAmmoType[h] >= 0
			&& sGun[h].magin && sGun[h].chamber == 0 && sGun[h].clip > 0) {
		g_Vars.currentplayer->ammoheldarr[sAmmoType[h]] += sGun[h].clip;
		sGun[h].clip = 0;
	}

	/* The playback goes with the gun: a gun drawn again shows its idle pose. Ended before the
	 * weapon is cleared, which its log line names */
	vrReloadEndPlay(h, switching ? "switch" : "stop");
	/* As the holder's click below: no tap from a gun no longer in hand, and no slap on it */
	sInsert2At[h] = -1;
	vrReloadSlapReset(h);
	sGunWeapon[h] = -1;
	sEject[h] = 0;
	sAllowFire[h] = 1;
	sHandClip[h] = 0;
	sNoAmmoPulse[h] = 0;
	sRackGrab[h] = 0.0f;
	sGuided[h] = 0;
	sFinish[h] = 0.0f;
	/* A switch or holster mid-close snaps the holder shut with no click, on purpose: the gun is going
	 * away and the click would sound from a gun no longer in hand */
	sHolderOpen[h] = 0.0f;
	/* The knob goes back with the gun: a gun drawn again shows it forward */
	sRackBack[h] = 0.0f;
	sPumpRide[h] = 0;
	sShellGrip[h] = 0;
	sDryPump[h] = 0;
	sShotSupport[h] = 0;
	sDrop[h].active = 0;
	sCasings[h] = 0;
}

/* The grip offset the render places (gVrGunOff, minus the X trim in left-handed mode, as the
 * "GRIP OFFSET PLACEMENT" block in bgun0f0a5550 does for the right hand, with or without dual
 * wield). Only valid for the right hand: the left gun of a pair is drawn mirrored, and it is never
 * driven. */
static void vrReloadGripOff(s32 h, f32 *g)
{
	g[0] = gVrGunOff[h].x;
	g[1] = gVrGunOff[h].y;
	g[2] = gVrGunOff[h].z;

	if (VrLeftHandedMode) {
		g[0] -= VrGunOffX;
	}
}

/* The gun hand's frame, as the render places the gun from hand->posrotmtx: the gun's root is at
 * posrotmtx.m[3] - g + G*g (g the grip offset above) and the gun turns as G = Ry180 * Rctrl, where
 * Rctrl*v takes the posrotmtx rows as basis vectors and Ry180 negates x and z (gCtrlQuat is stored
 * {w,-x,y,-z}). So with the anchor posrotmtx.m[3] - g every gun part sits at anchor + G*l for a
 * fixed l. The render's other rotations (damplook, the 180 Y turn) do not change between frames. */
static void vrReloadGunAnchor(s32 h, struct hand *hand, f32 *a)
{
	f32 g[3];
	s32 i;

	vrReloadGripOff(h, g);

	for (i = 0; i < 3; i++) {
		a[i] = hand->posrotmtx.m[3][i] - g[i];
	}
}

/* G*v, see vrReloadGunAnchor */
static void vrReloadGunToView(Mtxf *m, const f32 *v, f32 *out)
{
	out[0] = -(v[0] * m->m[0][0] + v[1] * m->m[1][0] + v[2] * m->m[2][0]);
	out[1] = v[0] * m->m[0][1] + v[1] * m->m[1][1] + v[2] * m->m[2][1];
	out[2] = -(v[0] * m->m[0][2] + v[1] * m->m[1][2] + v[2] * m->m[2][2]);
}

/* G^-1*w: Ry180 then the transpose of Rctrl (posrotmtx's rotation comes from a quaternion, so it
 * is orthonormal) */
static void vrReloadViewToGun(Mtxf *m, const f32 *w, f32 *out)
{
	f32 u[3];
	s32 k;

	u[0] = -w[0];
	u[1] = w[1];
	u[2] = -w[2];

	for (k = 0; k < 3; k++) {
		out[k] = m->m[k][0] * u[0] + m->m[k][1] * u[1] + m->m[k][2] * u[2];
	}
}

/* View-space point p (gCtrlPos frame, cm) of a part-local offset given in cm. The gun's matrices
 * are camera space (x right, y up, -z forward, the raw OpenXR view space gCtrlPos uses) with the
 * translation multiplied by bg_scale in bgun0f0a5550, so dividing by it gives cm, as the muzzle
 * position does there. The offset goes through the full part matrix, which also carries the model
 * scale and, in left-handed mode, the X mirror, so the offset is mirrored with the part. */
static void vrReloadMtxPoint(Mtxf *m, const f32 *offcm, f32 invscale, f32 *out)
{
	f32 v[3];
	s32 j;

	for (j = 0; j < 3; j++) {
		v[j] = offcm[j] * VRR_MODEL_UNITS_PER_CM;
	}

	for (j = 0; j < 3; j++) {
		out[j] = (m->m[3][j] + v[0] * m->m[0][j] + v[1] * m->m[1][j] + v[2] * m->m[2][j]) * invscale;
	}
}

/* The reverse of vrReloadMtxPoint: cm in the part's local frame of view-space point p. The part
 * matrix is a rotation (or mirror) times one scale, so its inverse is its transpose over the scale
 * squared. */
static void vrReloadMtxLocal(Mtxf *m, const f32 *p, f32 bgscale, f32 *outcm)
{
	f32 w[3];
	f32 s2;
	f32 sc = (bgscale != 0.0f && bgscale != 1.0f) ? bgscale : 1.0f;
	s32 i;

	for (i = 0; i < 3; i++) {
		w[i] = p[i] * sc - m->m[3][i];
	}

	s2 = m->m[0][0] * m->m[0][0] + m->m[0][1] * m->m[0][1] + m->m[0][2] * m->m[0][2];

	for (i = 0; i < 3; i++) {
		outcm[i] = s2 < 1e-12f ? 0.0f
			: (w[0] * m->m[i][0] + w[1] * m->m[i][1] + w[2] * m->m[i][2]) / s2 / VRR_MODEL_UNITS_PER_CM;
	}
}

/* Unit view-space direction of a part-local vector, as mtx4RotateVec turns it. Normalising removes
 * bg_scale and the model scale; the left-handed X mirror stays, as the part is drawn mirrored.
 * Returns false for a degenerate matrix. */
static s32 vrReloadMtxDir(Mtxf *m, const f32 *v, f32 *out)
{
	f32 len;

	out[0] = m->m[0][0] * v[0] + m->m[1][0] * v[1] + m->m[2][0] * v[2];
	out[1] = m->m[0][1] * v[0] + m->m[1][1] * v[1] + m->m[2][1] * v[2];
	out[2] = m->m[0][2] * v[0] + m->m[1][2] * v[1] + m->m[2][2] * v[2];

	len = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);

	if (len < 1e-6f) {
		return 0;
	}

	out[0] /= len;
	out[1] /= len;
	out[2] /= len;

	return 1;
}

/* Any later move of the clip's matrix (the guided insert, vrReloadPlaceClip, the falling clip,
 * vrReloadDropClip, the narrowing, vrReloadNarrowClip, and the collapse, vrReloadCollapseClip) must
 * happen after this call, or the seated clip and the well would move with it. */
void vrReloadCaptureParts(s32 handnum, struct modeldef *modeldef, Mtxf *matrices, f32 bgscale)
{
	static const f32 origin[3] = { 0.0f, 0.0f, 0.0f };
	static const f32 minusz[3] = { 0.0f, 0.0f, -1.0f };
	static const f32 unitx[3] = { 1.0f, 0.0f, 0.0f };
	static const f32 unity[3] = { 0.0f, 1.0f, 0.0f };
	struct vrreloaddef *def;
	struct modelnode *node = NULL;
	struct hand *hand;
	struct vrrpartsview v;
	struct vrrparts p;
	f32 anchor[3];
	f32 rel[3];
	s32 rackframe;
	s32 wellmtx;
	s32 hasclip;
	s32 i;
	f32 invscale;

	if (handnum < 0 || handnum >= 2 || modeldef == NULL || matrices == NULL) {
		return;
	}

	def = vrReloadOwnedDef(handnum);

	if (def == NULL) {
		return;
	}

	/* The rack's frame: the row's rackmtx, a part the rack pulls on with no slide of its own (the
	 * CMP150's body: nothing of it moves, so nothing slides under the hand), else the model's slide
	 * part, which the engine's slide offset moved above in bgun0f0a5550 */
	if (def->rackmtx >= 0) {
		rackframe = def->rackmtx;
	} else {
		node = modelGetPart(modeldef, MODELPART_GUN_SLIDE);
		rackframe = node != NULL ? modelFindNodeMtxIndex(node, 0) : -1;
	}

	/* A gun with neither (the Tranquilizer, whose row loads on insert) gets the clip's areas only:
	 * the rack fields stay zero and the off-hand is never in a rack area. A rackmtx past this model's
	 * matrices is not the model the row was measured on: no rack area either */
	if (rackframe >= modeldef->nummatrices) {
		rackframe = -1;
	}

	/* The well's frame: the clip's matrix, or for a row with no clip part the gun body the intake
	 * point is given in. The index is only trusted inside this model's matrix list, and only while
	 * the row's clip part (or, with none, its held part) exists in it (another model would not be
	 * the one the index came from) */
	hasclip = vrReloadHasClip(def);
	wellmtx = hasclip ? def->clipmtx : def->intakemtx;

	if (wellmtx < 0 || wellmtx >= modeldef->nummatrices
			|| modelGetPart(modeldef, hasclip ? def->clippart : def->heldpart) == NULL) {
		return;
	}

	invscale = (bgscale != 0.0f && bgscale != 1.0f) ? 1.0f / bgscale : 1.0f;

	memset(&v, 0, sizeof(v));
	v.hasrack = rackframe >= 0;

	/* A row that needs a rack on a model with no rack frame could never load again: logged once per
	 * gun. A row racked by a slap needs no rack frame */
	if (!v.hasrack && !def->loadsoninsert && !def->rackslap && sNoRackLogged != def->weaponnum) {
		sNoRackLogged = def->weaponnum;
		VRR_DIAG("capture: weapon=%d has no slide part and no usable rackmtx (%d) but its row needs a rack (loadsoninsert 0): it cannot be racked",
				def->weaponnum, def->rackmtx);
	}
	/* The seated clip's joint, or the intake point: through the full matrix, so the point is
	 * mirrored with the model in left-handed mode */
	vrReloadMtxPoint(&matrices[wellmtx], hasclip ? origin : def->intakepos, invscale, v.clippos);

	if (v.hasrack) {
		vrReloadMtxPoint(&matrices[rackframe], def->rackoffset, invscale, v.rackpos);
		vrReloadMtxPoint(&matrices[rackframe], origin, invscale, v.rackorigin);

		s32 bmtx = (def->bodymtx >= 0 && def->bodymtx < modeldef->nummatrices) ? def->bodymtx : 33;

		for (i = 0; i < 4; i++) {
			s32 j;
			for (j = 0; j < 4; j++) {
				sRackSlide[handnum].m[i][j] = matrices[rackframe].m[i][j] * (j < 3 ? invscale : 1.0f);
				if (bmtx >= 0 && bmtx < modeldef->nummatrices) {
					sGunBody[handnum].m[i][j] = matrices[bmtx].m[i][j] * (j < 3 ? invscale : 1.0f);
				}
			}
		}
		sRackSlideFrame[handnum] = g_Vars.lvframenum;
		sRackSlideWeapon[handnum] = g_Vars.currentplayer->hands[handnum].gset.weaponnum;
		sRackSlideIndex[handnum] = rackframe;

		if (bmtx >= 0 && bmtx < modeldef->nummatrices) {
			sGunBodyFrame[handnum] = g_Vars.lvframenum;
			sGunBodyWeapon[handnum] = g_Vars.currentplayer->hands[handnum].gset.weaponnum;
			sGunBodyIndex[handnum] = bmtx;
		}
	}

	if (sGunBodyFrame[handnum] != g_Vars.lvframenum) {
		s32 bmtx = (def->bodymtx >= 0 && def->bodymtx < modeldef->nummatrices) ? def->bodymtx : 33;
		if (bmtx >= 0 && bmtx < modeldef->nummatrices) {
			s32 ri, rj;
			for (ri = 0; ri < 4; ri++) {
				for (rj = 0; rj < 4; rj++) {
					sGunBody[handnum].m[ri][rj] = matrices[bmtx].m[ri][rj] * (rj < 3 ? invscale : 1.0f);
				}
			}
			sGunBodyFrame[handnum] = g_Vars.lvframenum;
			sGunBodyWeapon[handnum] = g_Vars.currentplayer->hands[handnum].gset.weaponnum;
			sGunBodyIndex[handnum] = bmtx;
		}
	}

	/* The guide's reach points, each as given and with x negated (as given only on a row with
	 * clipreachoneside): the matrix carries the left-handed mirror, so the pair covers both sides of
	 * the gun on either gun side */
	{
		f32 pts[2 * VR_RELOAD_REACH_MAX][3];

		v.numreach = vrReloadReachLocal(def, pts);

		for (i = 0; i < v.numreach; i++) {
			vrReloadMtxPoint(&matrices[wellmtx], pts[i], invscale, v.reach[i]);
		}
	}

	/* The slap point on the seated clip, and the clip's matrix in cm for the slap's drawn hand
	 * (vrReloadPlaceSlapHand), before any later move of it */
	if (hasclip && def->rackslap && def->slaprad > 0.0f) {
		v.hasslap = 1;
		vrReloadMtxPoint(&matrices[wellmtx], def->slappoint, invscale, v.slappos);

		s32 slapbody = vrReloadHasSlapPlay(def) && def->bodymtx < modeldef->nummatrices;

		for (i = 0; i < 4; i++) {
			s32 j;

			for (j = 0; j < 4; j++) {
				sSlapClip[handnum].m[i][j] = matrices[wellmtx].m[i][j] * (j < 3 ? invscale : 1.0f);

				if (slapbody) {
					sSlapBody[handnum].m[i][j] = matrices[def->bodymtx].m[i][j] * (j < 3 ? invscale : 1.0f);
				}
			}
		}

		sSlapClipFrame[handnum] = g_Vars.lvframenum;
		sSlapClipWeapon[handnum] = g_Vars.currentplayer->hands[handnum].gset.weaponnum;
	}

	/* A row closed by a flick or a push (the DY357's cylinder): the push's point on the gun body, and
	 * the flick's roll axis and the push's direction through the body as drawn, so they mirror with
	 * it. A roll is a turn, which a mirrored draw (left-handed mode) reverses: its axis is negated
	 * there, so the roll that takes the gun's top toward the open side still closes it */
	if (vrReloadHasClose(def)) {
		v.hasclose = 1;
		v.hasslap = def->slaprad > 0.0f;

		if (v.hasslap) {
			vrReloadMtxPoint(&matrices[wellmtx], def->slappoint, invscale, v.slappos);
		}

		/* A zero vector (the row has no flick or no push) stays zero */
		vrReloadMtxDir(&matrices[wellmtx], def->flickaxis, v.flickaxis);
		vrReloadMtxDir(&matrices[wellmtx], def->closedir, v.closedir);

		if (vrReloadRowsMirrored(&matrices[wellmtx])) {
			for (i = 0; i < 3; i++) {
				v.flickaxis[i] = -v.flickaxis[i];
			}
		}
	}

	/* An intake has no axis: the guide is a point test (vrReloadGuide), and clipaxis stays zero */
	if (hasclip && !vrReloadMtxDir(&matrices[wellmtx], def->clipaxis, v.clipaxis)) {
		return;
	}

	if (v.hasrack
			&& (!vrReloadMtxDir(&matrices[rackframe], minusz, v.rackaxis)
				|| !vrReloadMtxDir(&matrices[rackframe], unitx, v.rackx)
				|| !vrReloadMtxDir(&matrices[rackframe], unity, v.racky))) {
		return;
	}

	/* Into the gun hand's frame with the anchor and rotation this render used */
	hand = &g_Vars.currentplayer->hands[handnum];
	vrReloadGunAnchor(handnum, hand, anchor);

	memset(&p, 0, sizeof(p));

	for (i = 0; i < 3; i++) {
		rel[i] = v.clippos[i] - anchor[i];
	}

	vrReloadViewToGun(&hand->posrotmtx, rel, p.clippos);

	if (v.hasrack) {
		for (i = 0; i < 3; i++) {
			rel[i] = v.rackpos[i] - anchor[i];
		}

		vrReloadViewToGun(&hand->posrotmtx, rel, p.rackpos);

		for (i = 0; i < 3; i++) {
			rel[i] = v.rackorigin[i] - anchor[i];
		}

		vrReloadViewToGun(&hand->posrotmtx, rel, p.rackorigin);
		vrReloadViewToGun(&hand->posrotmtx, v.rackaxis, p.rackaxis);
		vrReloadViewToGun(&hand->posrotmtx, v.rackx, p.rackx);
		vrReloadViewToGun(&hand->posrotmtx, v.racky, p.racky);
	}

	for (p.numreach = 0; p.numreach < v.numreach; p.numreach++) {
		for (i = 0; i < 3; i++) {
			rel[i] = v.reach[p.numreach][i] - anchor[i];
		}

		vrReloadViewToGun(&hand->posrotmtx, rel, p.reach[p.numreach]);
	}
	vrReloadViewToGun(&hand->posrotmtx, v.clipaxis, p.clipaxis);

	if (v.hasslap) {
		for (i = 0; i < 3; i++) {
			rel[i] = v.slappos[i] - anchor[i];
		}

		vrReloadViewToGun(&hand->posrotmtx, rel, p.slappos);
		p.hasslap = 1;
	}

	if (v.hasclose) {
		vrReloadViewToGun(&hand->posrotmtx, v.flickaxis, p.flickaxis);
		vrReloadViewToGun(&hand->posrotmtx, v.closedir, p.closedir);
		p.hasclose = 1;
	}

	p.hasrack = v.hasrack;
	p.weaponnum = hand->gset.weaponnum;
	p.frame = g_Vars.lvframenum;
	sParts[handnum] = p;

	/* The held clip's top too (clip frame, where the guide measures from), with its distance to the
	 * well mouth and its angle to the well axis, as vrReloadGuide tests them. held: a capture of
	 * the held clip for this gun (without one the top is the controller's origin); holding: the
	 * core holds a clip now (the guide runs only then). A row with no clip part logs in the gun
	 * body's frame (intakemtx) instead, labelled body( for clip(: the grab point, the held part's
	 * top and its intakedist to the intake point (the guide's only test); angle is -1. free and
	 * freeq: the free controller's origin and turn in the gun hand's frame (vrReloadFreeInGun), so
	 * the mark can be replayed with the fistwrist capture and the row. past: cm the top is past the
	 * mouth along the insert axis (the guide's start margin tests it; 0 for an intake row); guided:
	 * the guide was on at the last tick */
	if (sMarkPending[handnum]) {
		f32 grab[3], inslide[3], inclip[3];
		f32 top[3], dir[3], intop[3], mouth[3];
		f32 freepos[3], freeq[4];
		f32 cosang, past;
		f32 pts[2 * VR_RELOAD_REACH_MAX][3];
		const char *framename = hasclip ? "clip" : "body";
		const char *distname = hasclip ? "mouthdist" : "intakedist";
		s32 npts = vrReloadReachLocal(def, pts);
		f32 reach;

		sMarkPending[handnum] = 0;
		vrReloadHandGrab(grab);

		/* No rack frame: its point is logged as zero and rackdist as -1. slide( is the grab point in
		 * the rack frame, the slide's or the row's rackmtx */
		if (v.hasrack) {
			vrReloadMtxLocal(&matrices[rackframe], grab, bgscale, inslide);
		} else {
			inslide[0] = inslide[1] = inslide[2] = 0.0f;
		}

		vrReloadMtxLocal(&matrices[wellmtx], grab, bgscale, inclip);
		vrReloadHeldClip(handnum, hand, top, dir);
		vrReloadMtxLocal(&matrices[wellmtx], top, bgscale, intop);

		for (i = 0; i < 3; i++) {
			mouth[i] = hasclip ? -def->clipinsertdepth * def->clipaxis[i] : def->intakepos[i];
		}

		cosang = vrReloadClamp(dir[0] * v.clipaxis[0] + dir[1] * v.clipaxis[1] + dir[2] * v.clipaxis[2],
				-1.0f, 1.0f);

		/* reach: the top's distance to the nearest stretch of the guide's reach (-1: the row has
		 * none), in the clip frame where the points are given */
		reach = npts > 0 ? vrReloadReachDist(intop, mouth, pts, npts, 1e9f) : -1.0f;
		past = hasclip ? (intop[0] - mouth[0]) * def->clipaxis[0] + (intop[1] - mouth[1]) * def->clipaxis[1]
			+ (intop[2] - mouth[2]) * def->clipaxis[2] : 0.0f;
		vrReloadFreeInGun(handnum, hand, freepos, freeq);

		VRR_DIAG("mark weapon=%d slidetrans=%.1f slide(%.2f %.2f %.2f) %s(%.2f %.2f %.2f) rackdist=%.1f"
				" top(%.2f %.2f %.2f) %s=%.1f past=%.1f reach=%.1f angle=%.0f held=%d holding=%d guided=%d"
				" free(%.2f %.2f %.2f) freeq(%.4f %.4f %.4f %.4f)",
				hand->gset.weaponnum, hand->slidetrans, inslide[0], inslide[1], inslide[2],
				framename, inclip[0], inclip[1], inclip[2],
				v.hasrack ? vrReloadDist(grab, v.rackpos) : -1.0f,
				intop[0], intop[1], intop[2], distname, vrReloadDist(intop, mouth), past,
				reach, hasclip ? RAD2DEG(acosf(cosang)) : -1.0f,
				sHeld[handnum].weaponnum >= 0 && sHeld[handnum].weaponnum == hand->gset.weaponnum,
				sGun[handnum].state == VRR_HOLDING_CLIP, sGuided[handnum],
				freepos[0], freepos[1], freepos[2], freeq[0], freeq[1], freeq[2], freeq[3]);
	}
}

/* The off-hand controller's rotation, as vr_gun_pos_rot builds the left hand's posrotmtx from it
 * (quaternionToMtx; the left hand gets no two-hand aim or roll for the rows' guns). The copy-weapon
 * model hangs off it the way the gun hangs off the gun hand, so vrReloadGunToView and
 * vrReloadViewToGun turn between the view and this frame. */
static void vrReloadOffRot(Mtxf *r)
{
	f32 q[4];
	s32 i;

	for (i = 0; i < 4; i++) {
		q[i] = gCtrlQuat[VRR_CTRL_OFF][i];
	}

	quaternionToMtx(q, r);
}

/* The free (off-hand) controller in the gun hand's frame (the frame vrReloadCaptureParts keeps the
 * parts in, as vrReloadViewToGun turns into it), for the mark line, so a mark can be replayed: pos
 * is the controller's origin, cm from the gun anchor, and quat {w, x, y, z} the unit quaternion of
 * the turn that takes a direction in the off controller's frame (vrReloadOffRot, the frame the copy
 * model and the captures hang off) into the gun's, column vectors */
static void vrReloadFreeInGun(s32 h, struct hand *hand, f32 *pos, f32 *quat)
{
	Mtxf r;
	f32 anchor[3];
	f32 rel[3];
	f32 x[3][3]; /* x[k]: the off frame's axis k in the gun frame (the turn's column k) */
	f32 tr, sq;
	s32 i, k;

	vrReloadGunAnchor(h, hand, anchor);

	for (i = 0; i < 3; i++) {
		rel[i] = gCtrlPos[VRR_CTRL_OFF][i] - anchor[i];
	}

	vrReloadViewToGun(&hand->posrotmtx, rel, pos);
	vrReloadOffRot(&r);

	for (k = 0; k < 3; k++) {
		f32 e[3] = { 0.0f, 0.0f, 0.0f };
		f32 w[3];

		e[k] = 1.0f;
		vrReloadGunToView(&r, e, w);
		vrReloadViewToGun(&hand->posrotmtx, w, x[k]);
	}

	/* The matrix's element (row i, column k) is x[k][i]; the largest of w, x, y, z is taken first so
	 * the division is never by a small number */
	tr = x[0][0] + x[1][1] + x[2][2];

	if (tr > 0.0f) {
		sq = sqrtf(tr + 1.0f) * 2.0f;
		quat[0] = 0.25f * sq;
		quat[1] = (x[1][2] - x[2][1]) / sq;
		quat[2] = (x[2][0] - x[0][2]) / sq;
		quat[3] = (x[0][1] - x[1][0]) / sq;
	} else if (x[0][0] > x[1][1] && x[0][0] > x[2][2]) {
		sq = sqrtf(1.0f + x[0][0] - x[1][1] - x[2][2]) * 2.0f;
		quat[0] = (x[1][2] - x[2][1]) / sq;
		quat[1] = 0.25f * sq;
		quat[2] = (x[1][0] + x[0][1]) / sq;
		quat[3] = (x[2][0] + x[0][2]) / sq;
	} else if (x[1][1] > x[2][2]) {
		sq = sqrtf(1.0f + x[1][1] - x[0][0] - x[2][2]) * 2.0f;
		quat[0] = (x[2][0] - x[0][2]) / sq;
		quat[1] = (x[1][0] + x[0][1]) / sq;
		quat[2] = 0.25f * sq;
		quat[3] = (x[2][1] + x[1][2]) / sq;
	} else {
		sq = sqrtf(1.0f + x[2][2] - x[0][0] - x[1][1]) * 2.0f;
		quat[0] = (x[0][1] - x[1][0]) / sq;
		quat[1] = (x[2][0] + x[0][2]) / sq;
		quat[2] = (x[2][1] + x[1][2]) / sq;
		quat[3] = 0.25f * sq;
	}
}

/* The drawn off-hand's grab point this tick, gCtrlPos frame, cm: see VRR_HAND_GRAB_X */
static void vrReloadHandGrab(f32 *out)
{
	f32 g[3];
	Mtxf r;
	s32 i;

	g[0] = VrLeftHandedMode ? -VRR_HAND_GRAB_X : VRR_HAND_GRAB_X;
	g[1] = VRR_HAND_GRAB_Y;
	g[2] = VRR_HAND_GRAB_Z;

	vrReloadOffRot(&r);
	vrReloadGunToView(&r, g, out);

	for (i = 0; i < 3; i++) {
		out[i] += gCtrlPos[VRR_CTRL_OFF][i];
	}
}

/* The free off-hand's wrist, off-controller frame, cm, per physical hand (0 left, 1 right): see
 * vrReloadNoteFistWrist. The off-controller is the free hand, the hand away from the gun side, so
 * a physical hand is the free hand under one gun side only: its capture is always taken and used
 * with the same mirroring, and stays valid while the gun side moves to it and back. The poses are
 * the physical controllers' (only the indices swap), so the frame itself is never mirrored. Always
 * the open hand (the render passes the open pose): a pair's free hand shows the fists from the park
 * at the B or Y press (holster core rule 33) to the clip grab, often still clenched from gripping
 * its gun, and the clenched hand bone sits elsewhere. The free-hand frame mirrors the grip trim
 * (VrGunOffX) with the gun side (the free left-hand placement in bgunRender), so the two hands'
 * captures are mirrors of each other (x negated, as VRR_HAND_GRAB_X is), but each hand still keeps
 * its own: each physical hand shows the fists from when it is free (single gun) or parked (pair)
 * until the press that grabs a clip, and the grab needs a press edge (a grip held from the parked
 * gun is let go first), so it has its own capture before its first clip. */
static f32 sFistWrist[2][3];
static s32 sFistWristOk[2];

/* The same capture's palm: the middle of the open hand bone and its four knuckles (the fists' bones
 * 5, 8, 11 and 14, the right hand's, drawn mirrored as the left), as the slap points' palm is the
 * middle of the animation's left wrist and knuckles (rifleslapmove.py). For the slap's test
 * (vrReloadHandPalm). The ntsc-final hands (GcombathandslodZ) put it 4.8 cm from the hand bone along
 * the hand, so it moves and turns with the drawn hand on the controller */
static f32 sFistPalm[2][3];
static s32 sFistPalmOk[2];

/* The free fists' open arm (the forearm bone, matrix 1 of the right hand drawn mirrored as the left,
 * from the open pass the wrist capture reads: the rest pose and the open roll, no vr_wrist_rot), per
 * physical hand: its rows and its offset from the hand bone (matrix 2), in the off controller's frame
 * (vrReloadViewToGun), so a hand holding a clip or slapping gets the fists' arm from the current
 * controller pose (vrReloadPlaceHeldArm, Task F9c). It is the drawn fists' arm before the elbow solve
 * (vr_arm_solve, bondgun.c) while the hand is open */
struct vrrarm {
	f32 rows[3][3];
	f32 off[3];
};

static struct vrrarm sFistArm[2];
static s32 sFistArmOk[2];

/* Diagnostics (PD-VRR): the capture last logged per physical hand and when (lvframe60), and the
 * last frame the held hand was aligned (a gap starts a new grab, logged once). After a hand's
 * first capture, a move is logged only when it is over VRR_FIST_LOG_CM and a second has passed, so
 * a fast arm swing does not flood the log. */
#define VRR_FIST_LOG_CM   2.0f
#define VRR_FIST_LOG_60THS 60
static f32 sFistWristLogged[2][3];
static s32 sFistWristLoggedOk[2];
static s32 sFistWristLogTime[2];
static s32 sAlignFrame = -2;

/* The physical hand of the off-controller's pose this frame, or -1 when a gun side change has not
 * reached the poses yet (the drawn hand is already mirrored for the new side) */
static s32 vrReloadOffPhysHand(void)
{
	if (VrLeftHandedMode != VrCtrlPoseMode) {
		return -1;
	}

	return VRR_CTRL_OFF ^ (VrLeftHandedMode ? 1 : 0);
}

/* The drawn free hand's palm this tick, gCtrlPos frame, cm (sFistPalm). Before the hand has a
 * capture, the grab point (vrReloadHandGrab) */
static void vrReloadHandPalm(f32 *out)
{
	Mtxf r;
	s32 i;
	s32 p = vrReloadOffPhysHand();

	if (p < 0 || !sFistPalmOk[p]) {
		vrReloadHandGrab(out);
		return;
	}

	vrReloadOffRot(&r);
	vrReloadGunToView(&r, sFistPalm[p], out);

	for (i = 0; i < 3; i++) {
		out[i] += gCtrlPos[VRR_CTRL_OFF][i];
	}
}

void vrReloadNoteFistWrist(const Mtxf *wrist, const f32 *palm, f32 clench)
{
	Mtxf r;
	f32 rel[3];
	s32 i;
	s32 p = vrReloadOffPhysHand();

	if (p < 0) {
		return;
	}

	for (i = 0; i < 3; i++) {
		rel[i] = wrist->m[3][i] - gCtrlPos[VRR_CTRL_OFF][i];
	}

	vrReloadOffRot(&r);
	vrReloadViewToGun(&r, rel, sFistWrist[p]);
	sFistWristOk[p] = 1;

	if (palm != NULL) {
		for (i = 0; i < 3; i++) {
			rel[i] = palm[i] - gCtrlPos[VRR_CTRL_OFF][i];
		}

		vrReloadViewToGun(&r, rel, sFistPalm[p]);
	}

	sFistPalmOk[p] = palm != NULL;

	/* Logged on the first capture of a hand and when it moves: it should stay put whatever the
	 * clench (the drawn clench is logged to show that) */
	if (!sFistWristLoggedOk[p]
			|| (vrReloadDist(sFistWrist[p], sFistWristLogged[p]) > VRR_FIST_LOG_CM
				&& (g_Vars.lvframe60 - sFistWristLogTime[p] >= VRR_FIST_LOG_60THS
					|| g_Vars.lvframe60 < sFistWristLogTime[p]))) {
		VRR_DIAG("fistwrist: frame=%d phys=%c clench=%.2f wrist=(%.1f %.1f %.1f) first=%d",
				g_Vars.lvframenum, p ? 'R' : 'L', clench,
				sFistWrist[p][0], sFistWrist[p][1], sFistWrist[p][2], !sFistWristLoggedOk[p]);

		for (i = 0; i < 3; i++) {
			sFistWristLogged[p][i] = sFistWrist[p][i];
		}

		sFistWristLoggedOk[p] = 1;
		sFistWristLogTime[p] = g_Vars.lvframe60;
	}
}

void vrReloadNoteFistArm(const Mtxf *arm, const Mtxf *hand)
{
	struct vrrarm *a;
	Mtxf r;
	f32 rel[3];
	s32 i;
	s32 p = vrReloadOffPhysHand();

	if (p < 0 || arm == NULL || hand == NULL) {
		return;
	}

	a = &sFistArm[p];
	vrReloadOffRot(&r);

	for (i = 0; i < 3; i++) {
		vrReloadViewToGun(&r, arm->m[i], a->rows[i]);
		rel[i] = arm->m[3][i] - hand->m[3][i];
	}

	vrReloadViewToGun(&r, rel, a->off);
	sFistArmOk[p] = 1;
}

bool vrReloadPlaceHeldArm(s32 handnum, struct modeldef *modeldef, Mtxf *matrices, s32 armmtx, s32 wristmtx)
{
	struct vrrarm *a;
	Mtxf r;
	f32 off[3];
	s32 i, j;
	s32 p = vrReloadOffPhysHand();

	if (p < 0 || !sFistArmOk[p] || (!vrReloadHandHoldsClip(handnum) && !vrReloadSlapDrawn(handnum, NULL) && !vrReloadRackDrawn(handnum, NULL))
			|| modeldef == NULL || matrices == NULL
			|| armmtx < 0 || armmtx >= modeldef->nummatrices || wristmtx < 0 || wristmtx >= modeldef->nummatrices) {
		return false;
	}

	a = &sFistArm[p];
	vrReloadOffRot(&r);

	/* The left arm's mesh, in its bone's frame, is the right arm's with x negated (the hands model is
	 * a mirror image about the middle between them: GcombathandslodZ's matrices 1 and 17 hold the
	 * same forearm, x -39..34 and -33..40, both from the elbow 33 cm to the wrist), so the right
	 * arm's rows with row 0 negated draw the same forearm from the left arm's bone, mirrored or not
	 * (the fists mirror the right hand in right-handed mode, the copy model the left one in
	 * left-handed mode) */
	for (i = 0; i < 3; i++) {
		vrReloadGunToView(&r, a->rows[i], matrices[armmtx].m[i]);
	}

	for (j = 0; j < 3; j++) {
		matrices[armmtx].m[0][j] = -matrices[armmtx].m[0][j];
	}

	/* From the drawn wrist (on the controller holding a clip, at the gun slapping), as the fists' arm
	 * is from theirs: the joint stays where it is drawn. The hand bone keeps its own turn: about 26
	 * degrees, mostly a roll about the forearm, from the hand the arm carries on every row
	 * (tools/vr_measure/wristseam.py) */
	vrReloadGunToView(&r, a->off, off);

	for (j = 0; j < 3; j++) {
		matrices[armmtx].m[3][j] = matrices[wristmtx].m[3][j] + off[j];
	}

	return true;
}

/* A row's turn (the hand turn, handturnaxis and handturndeg, or the clip turn, clipturnaxis and
 * clipturndeg) as a view-space turn, row vectors (v' = v * V, as the Mtxf rows turn): the turn is
 * given in the model root's frame, so it is conjugated through the drawn root's rotation Q (root's
 * rows normalised: the copy model's 0.1 scale goes, the left-handed x mirror stays), V = Q^T * Rt * Q
 * with Rt the turn as rows (the transpose of the column-vector turn about the axis). Q carries the
 * left-handed mirror, so V is the mirror image of the right-handed view turn and the turned wrist is
 * the Falcon's frame-18 wrist drawn with the mirrored root: a mirror of the right-handed hand, with
 * no mirroring of its own. Returns false for no turn or a degenerate root. */
static s32 vrReloadRootTurn(const f32 *axis, f32 deg, const Mtxf *root, f32 v[3][3])
{
	f32 q[3][3];
	f32 rt[3][3];
	f32 k[3];
	f32 len, c, s, t;
	s32 i, j, a, b;

	len = sqrtf(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);

	if (deg == 0.0f || len < 1e-6f) {
		return 0;
	}

	for (i = 0; i < 3; i++) {
		f32 rl = sqrtf(root->m[i][0] * root->m[i][0] + root->m[i][1] * root->m[i][1]
				+ root->m[i][2] * root->m[i][2]);

		if (rl < 1e-9f) {
			return 0;
		}

		for (j = 0; j < 3; j++) {
			q[i][j] = root->m[i][j] / rl;
		}

		k[i] = axis[i] / len;
	}

	/* Rodrigues, column vectors: R = cI + s[k]x + t k k^T; Rt is its transpose (the rows' form) */
	c = cosf(DEG2RAD(deg));
	s = sinf(DEG2RAD(deg));
	t = 1.0f - c;

	rt[0][0] = c + t * k[0] * k[0];
	rt[1][1] = c + t * k[1] * k[1];
	rt[2][2] = c + t * k[2] * k[2];
	rt[1][0] = t * k[0] * k[1] - s * k[2];
	rt[0][1] = t * k[0] * k[1] + s * k[2];
	rt[2][0] = t * k[0] * k[2] + s * k[1];
	rt[0][2] = t * k[0] * k[2] - s * k[1];
	rt[2][1] = t * k[1] * k[2] - s * k[0];
	rt[1][2] = t * k[1] * k[2] + s * k[0];

	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++) {
			v[i][j] = 0.0f;

			for (a = 0; a < 3; a++) {
				for (b = 0; b < 3; b++) {
					v[i][j] += q[a][i] * rt[a][b] * q[b][j];
				}
			}
		}
	}

	return 1;
}

void vrReloadAlignHeldHand(s32 handnum, struct modeldef *modeldef, Mtxf *matrices, const Mtxf *root, s32 wristmtx)
{
	struct vrreloaddef *def;
	Mtxf r;
	f32 want[3];
	f32 d[3];
	f32 wrist[3];
	f32 v[3][3];
	s32 turn, clipturn = 0, clipshift = 0;
	s32 i, j, k;
	s32 p = vrReloadOffPhysHand();
	s32 newgrab = g_Vars.lvframenum - sAlignFrame < 0 || g_Vars.lvframenum - sAlignFrame > 1;

	sAlignFrame = g_Vars.lvframenum;

	/* No capture yet for this hand (it has not shown the fists as the free hand): left as drawn */
	if (p < 0 || !sFistWristOk[p] || modeldef == NULL || matrices == NULL || root == NULL
			|| wristmtx < 0 || wristmtx >= modeldef->nummatrices) {
		if (newgrab) {
			VRR_DIAG("align: frame=%d phys=%c capture=0 (left as drawn)",
					g_Vars.lvframenum, p < 0 ? '-' : (p ? 'R' : 'L'));
		}

		return;
	}

	/* The row's hand turn first, about the wrist, so the hand sits on the controller as the
	 * Falcon's does (vrreloaddef.handturnaxis): every matrix, turned with its offset from the wrist,
	 * so the model stays rigid; then the move below brings the wrist to the capture */
	def = handnum >= 0 && handnum < 2 ? vrReloadOwnedDef(handnum) : NULL;
	turn = def != NULL && vrReloadRootTurn(def->handturnaxis, def->handturndeg, root, v);

	if (turn) {
		for (j = 0; j < 3; j++) {
			wrist[j] = matrices[wristmtx].m[3][j];
		}

		for (i = 0; i < modeldef->nummatrices; i++) {
			f32 m[4][3];

			for (j = 0; j < 3; j++) {
				m[0][j] = matrices[i].m[0][j];
				m[1][j] = matrices[i].m[1][j];
				m[2][j] = matrices[i].m[2][j];
				m[3][j] = matrices[i].m[3][j] - wrist[j];
			}

			for (j = 0; j < 4; j++) {
				for (k = 0; k < 3; k++) {
					matrices[i].m[j][k] = m[j][0] * v[0][k] + m[j][1] * v[1][k] + m[j][2] * v[2][k]
						+ (j == 3 ? wrist[k] : 0.0f);
				}
			}
		}
	}

	/* Then the row's clip turn (vrreloaddef.clipturnaxis), the held part's matrices alone, about the
	 * palm (clipturnpivot on heldmtx as now turned), so the clip sits in the hand as the Falcon 2's
	 * sits in its hand; mirrored with the root as the hand turn is */
	if (def != NULL && (def->insertframefrom <= 0.0f || def->weaponnum == WEAPON_CROSSBOW) && def->heldmtx >= 0 && def->heldmtx < modeldef->nummatrices
			&& vrReloadRootTurn(def->clipturnaxis, def->clipturndeg, root, v)) {
		s32 last = def->heldmtxlast >= def->heldmtx ? def->heldmtxlast : def->heldmtx;
		f32 pivot[3];

		if (last >= modeldef->nummatrices) {
			last = modeldef->nummatrices - 1;
		}

		clipturn = 1;

		/* The copy model's matrices carry no bg_scale: cm as they are */
		vrReloadMtxPoint(&matrices[def->heldmtx], def->clipturnpivot, 1.0f, pivot);

		for (i = def->heldmtx; i <= last; i++) {
			f32 m[4][3];

			for (j = 0; j < 3; j++) {
				m[0][j] = matrices[i].m[0][j];
				m[1][j] = matrices[i].m[1][j];
				m[2][j] = matrices[i].m[2][j];
				m[3][j] = matrices[i].m[3][j] - pivot[j];
			}

			for (j = 0; j < 4; j++) {
				for (k = 0; k < 3; k++) {
					matrices[i].m[j][k] = m[j][0] * v[0][k] + m[j][1] * v[1][k] + m[j][2] * v[2][k]
						+ (j == 3 ? pivot[k] : 0.0f);
				}
			}
		}
	}

	/* Then the row's clip shift (vrreloaddef.clipshift), the held part's matrices alone, so the clip
	 * sits in the palm as the Falcon 2's does, not on the knuckles: through heldmtx as now turned, so
	 * it is mirrored with the model in left-handed mode, as the pivot is. The capture (sHeld, the
	 * guide's top and axis) and the playback's hand-off (sHeldMtx) read the matrices after this */
	if (def != NULL && (def->insertframefrom <= 0.0f || def->weaponnum == WEAPON_CROSSBOW) && def->heldmtx >= 0 && def->heldmtx < modeldef->nummatrices
			&& (def->clipshift[0] != 0.0f || def->clipshift[1] != 0.0f || def->clipshift[2] != 0.0f)) {
		s32 last = def->heldmtxlast >= def->heldmtx ? def->heldmtxlast : def->heldmtx;
		f32 zero[3] = { 0.0f, 0.0f, 0.0f };
		f32 o[3], sh[3];

		if (last >= modeldef->nummatrices) {
			last = modeldef->nummatrices - 1;
		}

		clipshift = 1;

		/* The copy model's matrices carry no bg_scale: cm as they are */
		vrReloadMtxPoint(&matrices[def->heldmtx], zero, 1.0f, o);
		vrReloadMtxPoint(&matrices[def->heldmtx], def->clipshift, 1.0f, sh);

		for (j = 0; j < 3; j++) {
			sh[j] -= o[j];
		}

		for (i = def->heldmtx; i <= last; i++) {
			for (j = 0; j < 3; j++) {
				matrices[i].m[3][j] += sh[j];
			}
		}
	}

	vrReloadOffRot(&r);
	vrReloadGunToView(&r, sFistWrist[p], want);

	for (j = 0; j < 3; j++) {
		d[j] = want[j] + gCtrlPos[VRR_CTRL_OFF][j] - matrices[wristmtx].m[3][j];
	}

	if (newgrab) {
		VRR_DIAG("align: frame=%d phys=%c capture=1 wrist=(%.1f %.1f %.1f) correction=%.1fcm (%.1f %.1f %.1f) turn=%.1f clipturn=%.1f clipshift=%.1f",
				g_Vars.lvframenum, p ? 'R' : 'L', sFistWrist[p][0], sFistWrist[p][1], sFistWrist[p][2],
				sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), d[0], d[1], d[2],
				turn ? def->handturndeg : 0.0f, clipturn ? def->clipturndeg : 0.0f,
				clipshift ? sqrtf(def->clipshift[0] * def->clipshift[0] + def->clipshift[1] * def->clipshift[1]
					+ def->clipshift[2] * def->clipshift[2]) : 0.0f);
	}

	for (i = 0; i < modeldef->nummatrices; i++) {
		for (j = 0; j < 3; j++) {
			matrices[i].m[3][j] += d[j];
		}
	}

	/* The aligned wrist, in the off controller's frame, for the slap's way back */
	if (handnum >= 0 && handnum < 2) {
		struct vrrwrist *w = &sHoldWrist[handnum];
		f32 rel[3];

		for (i = 0; i < 3; i++) {
			vrReloadViewToGun(&r, matrices[wristmtx].m[i], w->rows[i]);
			rel[i] = matrices[wristmtx].m[3][i] - gCtrlPos[VRR_CTRL_OFF][i];
		}

		vrReloadViewToGun(&r, rel, w->pos);
		w->weaponnum = g_Vars.currentplayer->hands[handnum].gset.weaponnum;
	}
}

void vrReloadCaptureHeldClip(s32 handnum, struct modeldef *modeldef, Mtxf *matrices)
{
	struct vrreloaddef *def;
	struct vrrheld p;
	Mtxf r;
	f32 top[3];
	f32 axis[3];
	f32 rel[3];
	s32 i;

	if (handnum < 0 || handnum >= 2 || modeldef == NULL || matrices == NULL) {
		return;
	}

	def = vrReloadOwnedDef(handnum);

	/* As for clipmtx: the index is only trusted while the row's held-clip part is in this model */
	if (def == NULL || def->heldmtx < 0 || def->heldmtx >= modeldef->nummatrices
			|| modelGetPart(modeldef, def->heldpart) == NULL) {
		return;
	}

	/* The copy-weapon matrices are not multiplied by bg_scale (only the gun's are): cm as they are */
	vrReloadMtxPoint(&matrices[def->heldmtx], def->heldtop, 1.0f, top);

	/* A row with no clip part loads at a point and has no held axis: it stays zero */
	if (!vrReloadHasClip(def)) {
		axis[0] = axis[1] = axis[2] = 0.0f;
	} else if (!vrReloadMtxDir(&matrices[def->heldmtx], def->heldaxis, axis)) {
		return;
	}

	vrReloadOffRot(&r);

	for (i = 0; i < 3; i++) {
		rel[i] = top[i] - gCtrlPos[VRR_CTRL_OFF][i];
	}

	vrReloadViewToGun(&r, rel, p.top);
	vrReloadViewToGun(&r, axis, p.axis);
	p.weaponnum = g_Vars.currentplayer->hands[handnum].gset.weaponnum;
	sHeld[handnum] = p;

	/* A row whose clip plays with the slap: the gun clip against the body at the hold frame, from this
	 * copy model posed there (the hand and clip turns moved it rigidly with the body, or not at all),
	 * once per gun, or each frame until a read works */
	if (vrReloadHasSlapPlay(def) && sHookClip[handnum].weaponnum != p.weaponnum) {
		struct vrrslapclip *hc = &sHookClip[handnum];

		hc->weaponnum = p.weaponnum;
		hc->valid = 0;

		if (def->clipmtx < modeldef->nummatrices && def->bodymtx < modeldef->nummatrices
				&& modelGetPart(modeldef, def->clippart) != NULL) {
			Mtxf ident;

			mtx4LoadIdentity(&ident);
			vrReloadRebaseMtx(&hc->c, &matrices[def->clipmtx], &matrices[def->bodymtx], &ident);
			hc->valid = 1;
		}

		/* Tried again on a later frame (logged each time: not expected for a row's own model) */
		if (!hc->valid) {
			VRR_DIAG("hook: frame=%d hand=%d weapon=%d hold-frame clip pose not read: clipmtx=%d bodymtx=%d nummatrices=%d: the clip is drawn seated until its playback",
					g_Vars.lvframenum, handnum, hc->weaponnum, def->clipmtx, def->bodymtx, modeldef->nummatrices);
			hc->weaponnum = -1;
		}
	}

	/* The whole held matrix too, for the hand-off of a row with playease (vrReloadStartPlay) */
	if (def->playease > 0.0f) {
		struct vrrheldmtx *hm = &sHeldMtx[handnum];

		for (i = 0; i < 3; i++) {
			vrReloadViewToGun(&r, matrices[def->heldmtx].m[i], hm->rows[i]);
			rel[i] = matrices[def->heldmtx].m[3][i] - gCtrlPos[VRR_CTRL_OFF][i];
		}

		vrReloadViewToGun(&r, rel, hm->pos);
		hm->weaponnum = p.weaponnum;
	}
}

void vrReloadPoseHeldLook(s32 handnum, struct modeldef *modeldef, Mtxf *matrices, s32 wristmtx)
{
	struct vrreloaddef *def;
	s32 last;
	s32 i, j, k;
	f32 v[3][3];

	if (handnum < 0 || handnum >= 2 || modeldef == NULL || matrices == NULL
			|| wristmtx < 0 || wristmtx >= modeldef->nummatrices) {
		return;
	}

	def = vrReloadOwnedDef(handnum);

	if (def == NULL || (def->insertframefrom > 0.0f && def->weaponnum != WEAPON_CROSSBOW) || def->heldmtx < 0 || def->heldmtx >= modeldef->nummatrices) {
		return;
	}

	last = def->heldmtxlast >= def->heldmtx ? def->heldmtxlast : def->heldmtx;

	if (last >= modeldef->nummatrices) {
		last = modeldef->nummatrices - 1;
	}

	/* The turn, given in the wrist's frame: vrReloadRootTurn with the wrist as the root conjugates
	 * it through the wrist's rows, so it is mirrored with the model as the hand and clip turns are */
	if (vrReloadRootTurn(def->heldlookaxis, def->heldlookdeg, &matrices[wristmtx], v)) {
		f32 pivot[3];

		/* The copy model's matrices carry no bg_scale: cm as they are */
		vrReloadMtxPoint(&matrices[def->heldmtx], def->heldlookpivot, 1.0f, pivot);

		for (i = def->heldmtx; i <= last; i++) {
			f32 m[4][3];

			for (j = 0; j < 3; j++) {
				m[0][j] = matrices[i].m[0][j];
				m[1][j] = matrices[i].m[1][j];
				m[2][j] = matrices[i].m[2][j];
				m[3][j] = matrices[i].m[3][j] - pivot[j];
			}

			for (j = 0; j < 4; j++) {
				for (k = 0; k < 3; k++) {
					matrices[i].m[j][k] = m[j][0] * v[0][k] + m[j][1] * v[1][k] + m[j][2] * v[2][k]
						+ (j == 3 ? pivot[k] : 0.0f);
				}
			}
		}
	}

	/* The move, through the wrist's rows (its mirror and scale), as the clip shift goes through
	 * heldmtx's */
	if (def->heldlookshift[0] != 0.0f || def->heldlookshift[1] != 0.0f || def->heldlookshift[2] != 0.0f) {
		f32 zero[3] = { 0.0f, 0.0f, 0.0f };
		f32 o[3], sh[3];

		vrReloadMtxPoint(&matrices[wristmtx], zero, 1.0f, o);
		vrReloadMtxPoint(&matrices[wristmtx], def->heldlookshift, 1.0f, sh);

		for (i = def->heldmtx; i <= last; i++) {
			for (j = 0; j < 3; j++) {
				matrices[i].m[3][j] += sh[j] - o[j];
			}
		}
	}
}

/* The held clip's top and insert direction (unit) for this tick, gCtrlPos frame, cm. Without a
 * capture for this gun (the first tick of a hold, before the clip was ever drawn) the clip is taken
 * to be at the off-hand controller, pointing along the controller's forward axis. */
static void vrReloadHeldClip(s32 h, struct hand *hand, f32 *top, f32 *axis)
{
	static const f32 forward[3] = { 0.0f, 0.0f, 1.0f }; /* -Z of the controller, see vrReloadGunAnchor */
	Mtxf r;
	s32 i;

	vrReloadOffRot(&r);

	if (sHeld[h].weaponnum >= 0 && sHeld[h].weaponnum == hand->gset.weaponnum) {
		vrReloadGunToView(&r, sHeld[h].top, top);
		vrReloadGunToView(&r, sHeld[h].axis, axis);
	} else {
		top[0] = top[1] = top[2] = 0.0f;
		vrReloadGunToView(&r, forward, axis);
	}

	for (i = 0; i < 3; i++) {
		top[i] += gCtrlPos[VRR_CTRL_OFF][i];
	}
}

/* The captured parts placed with this tick's gun hand, when they belong to the gun in hand. fresh:
 * also require a capture from the last few drawn frames; without it the parts are still placed
 * from the last capture (the clip and rack directions do not change in the gun). */
static s32 vrReloadPartsNow(s32 h, struct hand *hand, s32 fresh, struct vrrpartsview *out)
{
	struct vrrparts *p = &sParts[h];
	f32 anchor[3];
	s32 i;

	if (p->weaponnum < 0 || p->weaponnum != hand->gset.weaponnum) {
		return 0;
	}

	if (fresh && (g_Vars.lvframenum - p->frame < 0 || g_Vars.lvframenum - p->frame > VRR_PARTS_MAX_AGE)) {
		return 0;
	}

	vrReloadGunAnchor(h, hand, anchor);
	vrReloadGunToView(&hand->posrotmtx, p->clippos, out->clippos);
	vrReloadGunToView(&hand->posrotmtx, p->clipaxis, out->clipaxis);
	vrReloadGunToView(&hand->posrotmtx, p->rackpos, out->rackpos);
	vrReloadGunToView(&hand->posrotmtx, p->rackaxis, out->rackaxis);
	vrReloadGunToView(&hand->posrotmtx, p->rackorigin, out->rackorigin);
	vrReloadGunToView(&hand->posrotmtx, p->rackx, out->rackx);
	vrReloadGunToView(&hand->posrotmtx, p->racky, out->racky);
	out->hasrack = p->hasrack;

	for (i = 0; i < 3; i++) {
		out->clippos[i] += anchor[i];
		out->rackpos[i] += anchor[i];
		out->rackorigin[i] += anchor[i];
	}

	for (out->numreach = 0; out->numreach < p->numreach; out->numreach++) {
		vrReloadGunToView(&hand->posrotmtx, p->reach[out->numreach], out->reach[out->numreach]);

		for (i = 0; i < 3; i++) {
			out->reach[out->numreach][i] += anchor[i];
		}
	}

	out->hasslap = p->hasslap;
	vrReloadGunToView(&hand->posrotmtx, p->slappos, out->slappos);
	out->hasclose = p->hasclose;
	vrReloadGunToView(&hand->posrotmtx, p->flickaxis, out->flickaxis);
	vrReloadGunToView(&hand->posrotmtx, p->closedir, out->closedir);

	for (i = 0; i < 3; i++) {
		out->slappos[i] += anchor[i];
	}

	return 1;
}

/* Mouth of the clip well and the insert axis (unit, into the gun), gCtrlPos frame, cm: the seated
 * clip's joint moved back clipinsertdepth along the axis; v: the fresh parts they come from.
 * Returns false when there are no fresh parts. */
static s32 vrReloadWell(s32 h, struct hand *hand, struct vrreloaddef *def, f32 *mouth, f32 *axis,
		struct vrrpartsview *v)
{
	s32 i;

	if (!vrReloadPartsNow(h, hand, 1, v)) {
		return 0;
	}

	for (i = 0; i < 3; i++) {
		axis[i] = v->clipaxis[i];
		mouth[i] = v->clippos[i] - def->clipinsertdepth * axis[i];
	}

	return 1;
}

static f32 vrReloadDist(f32 *a, f32 *b)
{
	f32 dx = a[0] - b[0];
	f32 dy = a[1] - b[1];
	f32 dz = a[2] - b[2];

	return sqrtf(dx * dx + dy * dy + dz * dz);
}

static f32 vrReloadClamp(f32 v, f32 lo, f32 hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

/* Distance from p to the segment from a to b; tout (may be NULL): where the nearest point is on
 * it, 0 at a to 1 at b */
static f32 vrReloadSegDist(const f32 *p, const f32 *a, const f32 *b, f32 *tout)
{
	f32 ab[3], ap[3], q[3];
	f32 len2 = 0.0f, t = 0.0f;
	s32 i;

	for (i = 0; i < 3; i++) {
		ab[i] = b[i] - a[i];
		ap[i] = p[i] - a[i];
		len2 += ab[i] * ab[i];
		t += ap[i] * ab[i];
	}

	t = len2 > 1e-6f ? vrReloadClamp(t / len2, 0.0f, 1.0f) : 0.0f;

	if (tout != NULL) {
		*tout = t;
	}

	for (i = 0; i < 3; i++) {
		q[i] = a[i] + t * ab[i];
	}

	return sqrtf((p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1])
			+ (p[2] - q[2]) * (p[2] - q[2]));
}

/* The row's guide reach points in the clip's local frame (cm): each def->clipreach point as given
 * and with x negated (as given only with clipreachoneside). Returns how many (out holds
 * 2 * VR_RELOAD_REACH_MAX). */
static s32 vrReloadReachLocal(struct vrreloaddef *def, f32 out[][3])
{
	s32 n = 0;
	s32 i;

	for (i = 0; i < def->numclipreach && i < VR_RELOAD_REACH_MAX; i++) {
		out[n][0] = def->clipreach[i][0];
		out[n][1] = def->clipreach[i][1];
		out[n][2] = def->clipreach[i][2];
		n++;

		if (def->clipreachoneside) {
			continue;
		}

		out[n][0] = -def->clipreach[i][0];
		out[n][1] = def->clipreach[i][1];
		out[n][2] = def->clipreach[i][2];
		n++;
	}

	return n;
}

/* The smaller of dist and p's distance to the stretches from the mouth to the n reach points, all
 * in one frame */
static f32 vrReloadReachDist(const f32 *p, const f32 *mouth, f32 pts[][3], s32 n, f32 dist)
{
	s32 i;

	for (i = 0; i < n; i++) {
		f32 d = vrReloadSegDist(p, mouth, pts[i], NULL);

		if (d < dist) {
			dist = d;
		}
	}

	return dist;
}

/* Whether the held clip is lined up with the well, and how far in it is (sGuided, sProgress). To
 * start, its top must be within clipguiderad of the well mouth; to stay guided, within that plus
 * VRR_GUIDE_EXIT_CM of the stretch of the insert axis from the mouth to VRR_GUIDE_OVERSHOOT_CM past
 * seated, so pushing the clip in, or a little too far, does not end the guide (starting there would show a clip held beside the grip half in). Its
 * direction must be within clipalignmax of the insert axis (plus VRR_GUIDE_EXIT_DEG to stay; any
 * direction with clipalignmax 180, the Tranquilizer's). Only
 * while the core holds a clip and the well is known. The row's reach (clipreach) widens both
 * tests: the top may instead be within the same distance of a stretch from the mouth to a reach
 * point, so a guide it starts does not end on the next tick. The pull is unchanged: the drawn clip
 * goes in as far as the top is along the axis. A guide starts only with the top at most
 * VRR_GUIDE_START_PAST_CM past the mouth along the axis, so a start is at most about 3 cm in (with
 * the pull-in), far from seated; a top within VRR_GUIDE_REACH_DEEP_CM of a reach stretch may be
 * VRR_GUIDE_START_PAST_CM deeper than the stretch where it passes the top, but never deeper than
 * the stretch's reach point (below). If the parts go stale for a frame mid-insert the guide drops
 * and can restart only within that margin past the mouth. A row with no clip part has a point test
 * instead (below). */
static void vrReloadGuide(s32 h, struct hand *hand, struct vrreloaddef *def)
{
	struct vrrpartsview v;
	f32 mouth[3];
	f32 wellaxis[3];
	f32 top[3];
	f32 dir[3];
	f32 rel[3];
	f32 seg[3];
	f32 along, s, dist, cosang, maxdist, maxangle, startpast;
	s32 i;

	if (sGun[h].state != VRR_HOLDING_CLIP || !vrReloadWell(h, hand, def, mouth, wellaxis, &v)) {
		sGuided[h] = 0;
		return;
	}

	vrReloadHeldClip(h, hand, top, dir);

	/* A row with no clip part: the mouth is the intake point, and the held part loads as its top
	 * comes within clipguiderad of it, or of a stretch from it to a reach point (clipreach: the
	 * Crossbow's groove), from any side and at any angle. Nothing slides in: it is
	 * seated at once (sProgress is the insert depth, 0), so the core inserts on this tick
	 * (vrReloadBuildInput sets seated) and the hand is drawn empty from then on, as the reload
	 * animation hides its orb once it is at the intake */
	if (!vrReloadHasClip(def)) {
		if (def->weaponnum == WEAPON_CROSSBOW && v.numreach > 0 && def->clipinsertdepth > 0.0f) {
			static s32 sBoltNockBuzz[2] = { 0, 0 };
			f32 t = 0.0f;
			f32 d = vrReloadSegDist(top, mouth, v.reach[0], &t);
			if (d <= def->clipguiderad + (sGuided[h] ? VRR_GUIDE_EXIT_CM : 0.0f)) {
				sGuided[h] = 1;
				sProgress[h] = vrReloadClamp(t, 0.0f, 1.0f) * def->clipinsertdepth;
				if (t >= 0.75f) {
					if (!sBoltNockBuzz[h]) {
						vrReloadBuzz(VRR_CTRL_OFF, 0.4f, 0.05f);
						sBoltNockBuzz[h] = 1;
					}
				} else if (t < 0.60f) {
					sBoltNockBuzz[h] = 0;
				}
			} else {
				sGuided[h] = 0;
				sProgress[h] = 0.0f;
				sBoltNockBuzz[h] = 0;
			}
			return;
		}

		/* A gun held open to its load (ejecthold: the Rocket Launcher's hatch) takes its round once the
		 * opening has played out, so the close starts from the open pose: a rocket held at the intake
		 * meanwhile loads as the hatch is fully open */
		sGuided[h] = vrReloadReachDist(top, mouth, v.reach, v.numreach, vrReloadDist(top, mouth)) <= def->clipguiderad
			&& !(vrReloadHoldsEject(def) && sPlayFrame[h] > 0.0f && sPlayEject[h]);
		sProgress[h] = def->clipinsertdepth;
		return;
	}

	for (i = 0; i < 3; i++) {
		rel[i] = top[i] - mouth[i];
	}

	along = rel[0] * wellaxis[0] + rel[1] * wellaxis[1] + rel[2] * wellaxis[2];
	s = vrReloadClamp(along, 0.0f, def->clipinsertdepth + VRR_GUIDE_OVERSHOOT_CM);

	for (i = 0; i < 3; i++) {
		seg[i] = mouth[i] + s * wellaxis[i];
	}

	dist = vrReloadDist(top, sGuided[h] ? seg : mouth);

	/* Both directions are unit: the angle is within the limit when its cosine is at least the
	 * limit's. A row with clipalignmax VRR_GUIDE_ANY_ANGLE_DEG has no angle test: tested by its
	 * cosine, the limit plus VRR_GUIDE_EXIT_DEG would end a guide held near opposite the axis, and
	 * a cosine just under -1 from rounding would fail exactly opposite */
	cosang = dir[0] * wellaxis[0] + dir[1] * wellaxis[1] + dir[2] * wellaxis[2];

	maxdist = def->clipguiderad + (sGuided[h] ? VRR_GUIDE_EXIT_CM : 0.0f);
	maxangle = def->clipalignmax + (sGuided[h] ? VRR_GUIDE_EXIT_DEG : 0.0f);

	/* A start needs the top at most VRR_GUIDE_START_PAST_CM past the mouth along the axis, so every
	 * insert is drawn sliding in from near the mouth (user, 2026-10-17: a clip held beside the
	 * grip, its top level with the seated one, would otherwise start nearly seated and go in at
	 * once). Along a reach stretch the margin follows the stretch: a top within
	 * VRR_GUIDE_REACH_DEEP_CM of it may be up to the margin past the stretch's own depth where it
	 * passes nearest the top, capped at the reach point's depth (the Tranquilizer's second mark lies
	 * 2.9 cm past its mouth, beside the holder, on its stretch). Every stretch starts at the mouth,
	 * so only tops near a stretch's far part go deeper: near the mouth and off the stretches the
	 * margin stays VRR_GUIDE_START_PAST_CM, and a row whose points are no deeper than that (the
	 * MagSec's) is unchanged */
	startpast = (def != NULL && def->weaponnum == WEAPON_REAPER) ? 5.0f : VRR_GUIDE_START_PAST_CM;

	for (i = 0; i < v.numreach; i++) {
		f32 t;
		f32 d = vrReloadSegDist(top, mouth, v.reach[i], &t);

		if (d < dist) {
			dist = d;
		}

		if (d <= VRR_GUIDE_REACH_DEEP_CM) {
			f32 pointpast = (v.reach[i][0] - mouth[0]) * wellaxis[0] + (v.reach[i][1] - mouth[1]) * wellaxis[1]
				+ (v.reach[i][2] - mouth[2]) * wellaxis[2];
			f32 allow = t * pointpast + VRR_GUIDE_START_PAST_CM;

			if (allow > pointpast) {
				allow = pointpast;
			}

			if (allow > startpast) {
				startpast = allow;
			}
		}
	}

	sGuided[h] = dist <= maxdist && (def->clipalignmax >= VRR_GUIDE_ANY_ANGLE_DEG || cosang >= cosf(DEG2RAD(maxangle)))
			&& (sGuided[h] || along <= startpast);

	if (sGuided[h]) {
		sProgress[h] = vrReloadClamp(along + VRR_PULL_IN_CM, 0.0f, def->clipinsertdepth);
	}
}

/* Body-frame point (cm: x right, y up, z back, from the head) in view space. gCtrlPos is in the raw
 * OpenXR view space, so the body-frame offset is turned by the smoothed torso yaw into the play
 * space, then by the inverse of the full head rotation (gRawHeadQ, the same frame VrBodyYaw is
 * measured in) into the view space. Using the torso yaw rather than the head yaw means a glance
 * leaves it put. mirror: x is negated first. */
void vrBodyPointMirror(const f32 *bodycm, s32 mirror, f32 *out)
{
	struct coord v;
	XrQuaternionf qbody;
	XrQuaternionf qheadinv;
	f32 yaw = vrGetBodyYaw();

	v.x = mirror ? -bodycm[0] : bodycm[0];
	v.y = bodycm[1];
	v.z = bodycm[2];

	qbody.x = 0.0f;
	qbody.y = sinf(yaw * 0.5f);
	qbody.z = 0.0f;
	qbody.w = cosf(yaw * 0.5f);

	qheadinv.x = -gRawHeadQ.x;
	qheadinv.y = -gRawHeadQ.y;
	qheadinv.z = -gRawHeadQ.z;
	qheadinv.w = gRawHeadQ.w;

	vr_rotate_vector_by_quaternion(&v, &qbody);
	vr_rotate_vector_by_quaternion(&v, &qheadinv);

	out[0] = v.x;
	out[1] = v.y;
	out[2] = v.z;
}

void vrBodyPoint(const f32 *bodycm, f32 *out)
{
	vrBodyPointMirror(bodycm, VrLeftHandedPref, out);
}

void vrBodyLocal(const f32 *view, f32 *outcm)
{
	struct coord v;
	XrQuaternionf qbodyinv;
	f32 yaw = vrGetBodyYaw();

	v.x = view[0];
	v.y = view[1];
	v.z = view[2];

	qbodyinv.x = 0.0f;
	qbodyinv.y = -sinf(yaw * 0.5f);
	qbodyinv.z = 0.0f;
	qbodyinv.w = cosf(yaw * 0.5f);

	/* The reverse of vrBodyPointMirror: the head rotation, then the inverse torso yaw */
	vr_rotate_vector_by_quaternion(&v, &gRawHeadQ);
	vr_rotate_vector_by_quaternion(&v, &qbodyinv);

	outcm[0] = v.x;
	outcm[1] = v.y;
	outcm[2] = v.z;
}

/* Shoulder clip-source point in view space, mirrored for left-handed mode (the gun side, not the
 * pref) so it stays on the off-hand's side */
static void vrReloadShoulderHeadPos(f32 *out)
{
	static const f32 shoulder[3] = { VRR_SHOULDER_X, VRR_SHOULDER_Y, VRR_SHOULDER_Z };

	vrBodyPointMirror(shoulder, VrLeftHandedMode, out);
}

/* Fills the core input from the controllers and the engine. Returns in *along the off-hand
 * position relative to the gun hand, projected on the rack axis: both are in view space, so the
 * head's motion and rotation cancel out of the difference. The grab areas come from the gun's
 * parts as last drawn; with none (the gun not drawn lately) the off-hand is in neither, and a gun
 * with no rack frame (no slide part and no rackmtx) has no rack area. */
/* cm from p to the rack box (def->rackmin..rackmax in the rack frame, the slide's local frame,
 * which moves with the slide, or the row's rackmtx); 0 inside it */
static f32 vrReloadRackDist(struct vrreloaddef *def, struct vrrpartsview *v, f32 *p)
{
	f32 d[3], local[3];
	f32 sum = 0.0f;
	s32 i;

	for (i = 0; i < 3; i++) {
		d[i] = p[i] - v->rackorigin[i];
	}

	local[0] = d[0] * v->rackx[0] + d[1] * v->rackx[1] + d[2] * v->rackx[2];
	local[1] = d[0] * v->racky[0] + d[1] * v->racky[1] + d[2] * v->racky[2];
	local[2] = -(d[0] * v->rackaxis[0] + d[1] * v->rackaxis[1] + d[2] * v->rackaxis[2]);

	for (i = 0; i < 3; i++) {
		f32 out = local[i] < def->rackmin[i] ? def->rackmin[i] - local[i]
			: local[i] > def->rackmax[i] ? local[i] - def->rackmax[i] : 0.0f;

		sum += out * out;
	}

	return sqrtf(sum);
}

static void vrReloadBuildInput(s32 h, struct hand *hand, struct vrreloaddef *def,
		s32 ammoindex, s32 ammotype, struct vrrinput *in, f32 *along)
{
	struct player *player = g_Vars.currentplayer;
	struct vrrpartsview fresh;
	struct vrrpartsview any;
	s32 hasfresh = vrReloadPartsNow(h, hand, 1, &fresh);
	s32 hasany = vrReloadPartsNow(h, hand, 0, &any);
	f32 shoulder[3];
	f32 off[3];
	f32 grab[3];
	f32 rel[3];
	s32 offfree;

	/* Every field starts at 0, so a flag added to vrrinput (pumpaction, shellbyshell) is off unless
	 * a path below sets it: left as stack garbage it made B do nothing (shellbyshell) */
	memset(in, 0, sizeof(*in));

	/* A holster draw flipped the gun side this frame: the poses keep last frame's mapping until the
	 * next pose update, so this tick reads no zones, as an untracked tick (no grabs, buzzes or
	 * guidance). An eject waits for the next tick. */
	if (VrLeftHandedMode != VrCtrlPoseMode) {
		in->clipsize = hand->clipsizes[ammoindex];
		in->reserve = player->ammoheldarr[ammotype];
		in->directchest = (def != NULL && (def->weaponnum == WEAPON_PHOENIX || def->weaponnum == WEAPON_CALLISTO || def->weaponnum == WEAPON_FARSIGHT));
		in->racktravel = def->racktravel;
		in->loadsoninsert = def->loadsoninsert;
		in->rackslap = def->rackslap;
		in->pumpaction = def->pumpaction;
		in->shellbyshell = def->shellbyshell;
		*along = 0.0f;
		return;
	}

	vrReloadShoulderHeadPos(shoulder);

	off[0] = gCtrlPos[VRR_CTRL_OFF][0];
	off[1] = gCtrlPos[VRR_CTRL_OFF][1];
	off[2] = gCtrlPos[VRR_CTRL_OFF][2];

	vrReloadHandGrab(grab);

	rel[0] = off[0] - gCtrlPos[VRR_CTRL_GUN][0];
	rel[1] = off[1] - gCtrlPos[VRR_CTRL_GUN][1];
	rel[2] = off[2] - gCtrlPos[VRR_CTRL_GUN][2];

	/* An eject waits, VRR_EJECT_WAIT_FRAMES at most, for a record of the clip as drawn on this gun
	 * side (vrReloadDropClip), or the fall would start from nowhere or from the other gun: the Y
	 * flip of a pair drops the record (vrReloadSwapHands), and the frame of any gun side flip
	 * records none (the next frame does). The limit
	 * keeps a gun that is not drawn from never ejecting. A row with no clip part drops nothing, so
	 * its eject never waits. */
	if (sEject[h] && vrReloadHasClip(def) && !vrReloadClipWorldFresh(h, hand)
			&& (sEjectSince[h] < 0 || g_Vars.lvframenum - sEjectSince[h] < VRR_EJECT_WAIT_FRAMES)) {
		if (sEjectSince[h] < 0) {
			sEjectSince[h] = g_Vars.lvframenum;
		}

		in->eject = 0;
	} else {
		if (sEject[h]) {
			sEjectWaited[h] = sEjectSince[h] < 0 ? 0 : g_Vars.lvframenum - sEjectSince[h];
		}

		in->eject = sEject[h];
		sEject[h] = 0;
		sEjectSince[h] = -1;
	}

	in->grip = get_button_state(VRR_CTRL_OFF, "grip") ? 1 : 0;

	/* The off-hand works the gun only when free: empty, or its gun of a pair parked (holster core
	 * rule 9). Holding the left gun, it is in no zone: it takes no clip and racks nothing. Its grip
	 * still goes in as it is, so the core never sees a squeeze it did not get. */
	offfree = !player->hands[HAND_LEFT].inuse || vrHandParked(HAND_LEFT);

	if (offfree) {
		vrReloadGuide(h, hand, def);
	} else {
		sGuided[h] = 0;
	}

	in->inclip = sGuided[h];
	in->inrack = offfree && hasfresh && fresh.hasrack && vrReloadRackDist(def, &fresh, grab) <= def->rackrad;
	in->atshoulder = offfree && vrReloadDist(off, shoulder) <= VRR_SHOULDER_RAD;

	/* A gun pumped after each shot (the Shotgun): its rack is the forend, where the support hand holds
	 * the gun, so the support grip (vrTwoHandGripHeld: the gun draws its own left hand on the forend
	 * wherever the controller is) is on the pump too. The pump is grabbed only while the gun waits for
	 * it (or is being pumped): a ready gun's forend is the support grip, and a squeeze there must not
	 * pump a loaded round out. Never at the shoulder: a squeeze there takes a shell, whatever the
	 * support grip read a frame late says */
	if (def->pumpaction) {
		sPumpZone[h] = !in->atshoulder && !sShellGrip[h] && (in->inrack || (offfree && vrTwoHandGripHeld()));
		in->inrack = (sGun[h].state == VRR_NEED_RACK || sGun[h].state == VRR_RACKING) && !in->atshoulder
			&& !sShellGrip[h] && (in->inrack || (offfree && vrTwoHandGripHeld()));
	}
	in->clipsize = hand->clipsizes[ammoindex];
	in->reserve = player->ammoheldarr[ammotype];
	in->tracked = vrCtrlPoseGood(VRR_CTRL_OFF) && vrCtrlPoseGood(VRR_CTRL_GUN);
	in->paused = player->pausemode != PAUSEMODE_UNPAUSED || lvIsPaused();
	in->rackpull = 0.0f;

	/* A stale capture still measures a pull already under way: the rack's direction in the gun
	 * does not change */
	*along = hasany && any.hasrack ? rel[0] * any.rackaxis[0] + rel[1] * any.rackaxis[1] + rel[2] * any.rackaxis[2] : 0.0f;

	if (sGun[h].state == VRR_RACKING) {
		/* Only a pull back racks: pushing the slide forward does nothing. A pump's pull is from the
		 * most forward point of the stroke (sPumpFwd, Task J6c) */
		f32 d;

		if (def->pumpaction && in->tracked && *along < sPumpFwd[h]) {
			sPumpFwd[h] = *along;
		}

		d = *along - (def->pumpaction ? sPumpFwd[h] : sRackGrab[h]);

		in->rackpull = d > 0.0f ? d : 0.0f;
	}

	sRackPull[h] = in->rackpull;
	if (sGun[h].state == VRR_RACKING) {
		sRackEase[h] = 0.0f;
	}

	/* The slide goes forward at the grab (vrReloadMoveSlide), so a rack is always a full pull */
	in->directchest = (def != NULL && (def->weaponnum == WEAPON_PHOENIX || def->weaponnum == WEAPON_CALLISTO || def->weaponnum == WEAPON_FARSIGHT));
	in->racktravel = def->racktravel;
	in->loadsoninsert = def->loadsoninsert;
	in->rackslap = def->rackslap;
	in->pumpaction = def->pumpaction;
	in->shellbyshell = def->shellbyshell;
	in->slap = sSlap[h].pending; /* vrReloadStepSlap: the drawn palm reached the clip */

	/* Pushed in until the drawn clip is seated: no need to let go */
	if (def != NULL && def->weaponnum == WEAPON_CROSSBOW) {
		/* Crossbow bolt stays gripped in hand until grip is released (like slide/culasse).
		 * When grip is released near the nock (progress >= 75%), it seats and locks into the crossbow. */
		in->seated = !in->grip && sGuided[h] && sProgress[h] >= def->clipinsertdepth * 0.75f;
	} else {
		in->seated = sGuided[h] && sProgress[h] >= def->clipinsertdepth;
	}
}

/* Applies the core output: ammo, cached getter values, sounds and haptics */
static void vrReloadApplyOutput(s32 h, struct hand *hand, struct vrreloaddef *def,
		s32 ammoindex, s32 ammotype, struct vrroutput *out)
{
	struct player *player = g_Vars.currentplayer;
	s32 fx = out->fx;

	hand->loadedammo[ammoindex] = out->loaded;

	if (out->reservedelta != 0) {
		s32 reserve = player->ammoheldarr[ammotype] + out->reservedelta;
		player->ammoheldarr[ammotype] = reserve < 0 ? 0 : reserve;
	}

	sAllowFire[h] = out->allowfire;
	sHandClip[h] = out->handclip;

	/* A row's sound 0 is none (the Phoenix has no rack sound) */
	if (fx & VRR_FX_EJECT) {
		if (def->sndeject != 0) {
			sndStart(var80095200, def->sndeject, 0, -1, -1, -1, -1, -1);
		}

		vrReloadBuzz(VRR_CTRL_GUN, VRR_BUZZ_EJECT_AMP, VRR_BUZZ_EJECT_SECS);
	}

	/* A shell that fills the gun (a row loaded shell by shell with a sndfull: the Shotgun's ninth
	 * round, chamber and tube) plays sndfull in place of the shell's own sound, so the player hears
	 * that it takes no more (Task F11d) */
	if ((fx & VRR_FX_INSERT) && def->shellbyshell && def->sndfull != 0
			&& sGun[h].chamber + sGun[h].clip >= hand->clipsizes[ammoindex]) {
		if (def->sndinsert2 != 0) {
			sInsert2At[h] = g_Vars.lvframe60 + (s32) (def->insert2delay + 0.5f);
			sInsert2Snd[h] = def->sndfull;
		} else {
			sndStart(var80095200, def->sndfull, 0, -1, -1, -1, -1, -1);
		}
	} else {
		if ((fx & VRR_FX_INSERT) && def->sndinsert != 0) {
			sndStart(var80095200, def->sndinsert, 0, -1, -1, -1, -1, -1);
		}

		/* The second insert sound follows (vrReloadStepInsert2) */
		if ((fx & VRR_FX_INSERT) && def->sndinsert2 != 0) {
			sInsert2At[h] = g_Vars.lvframe60 + (s32) (def->insert2delay + 0.5f);
			sInsert2Snd[h] = def->sndinsert2;
		}
	}

	/* A close (the DY357's cylinder) plays it in its playback, at closesndat */
	if ((fx & VRR_FX_RACK) && def->sndrack != 0 && !vrReloadHasClose(def)) {
		sndStart(var80095200, def->sndrack, 0, -1, -1, -1, -1, -1);
	}

	/* A slap is felt harder than a grab, an insert or a pulled rack. A close was felt as it was made, on
	 * the hand that made it (vrReloadStepClose) */
	if ((fx & VRR_FX_RACK) && def->rackslap) {
		if (!vrReloadHasClose(def)) {
			vrReloadBuzz(VRR_CTRL_OFF, VRR_BUZZ_SLAP_AMP, VRR_BUZZ_SLAP_SECS);
		}
	} else if (fx & (VRR_FX_GRAB_CLIP | VRR_FX_INSERT | VRR_FX_RACK)) {
		vrReloadBuzz(VRR_CTRL_OFF, VRR_BUZZ_ACTION_AMP, VRR_BUZZ_ACTION_SECS);
	} else if (fx & VRR_FX_ZONE_ENTER) {
		vrReloadBuzz(VRR_CTRL_OFF, VRR_BUZZ_ZONE_AMP, VRR_BUZZ_ZONE_SECS);
	}

	if (fx & VRR_FX_NO_AMMO) {
		vrReloadBuzz(VRR_CTRL_OFF, VRR_BUZZ_NOAMMO_AMP, VRR_BUZZ_NOAMMO_SECS);
		sNoAmmoPulse[h] = 1;
		sNoAmmoPulseFrame[h] = g_Vars.lvframe60 + VRR_NOAMMO_GAP60;
	} else if (sNoAmmoPulse[h] && g_Vars.lvframe60 >= sNoAmmoPulseFrame[h]) {
		vrReloadBuzz(VRR_CTRL_OFF, VRR_BUZZ_NOAMMO_AMP, VRR_BUZZ_NOAMMO_SECS);
		sNoAmmoPulse[h] = 0;
	}
}

/* Moves the drawn slide with the off-hand while the core is racking: bgunUpdateSlide leaves it
 * alone meanwhile (vrReloadSlideHeld). At the grab the slide goes fully forward, even when a shot's
 * recoil still has it back, and then follows the pull back: racktravel cm of pull is its whole travel. After the rack the slide stays
 * at full travel until the grip lets go. On release bgunUpdateSlide takes over again and brings it
 * forward, loaded or not (no slide locks back in VR). A slide let go before the rack on an empty gun
 * starts from where it was before the grab (sSlideGrab). Runs
 * after the output is applied, so loadedammo is this tick's. A row that racks on a rackmtx (the
 * CMP150's body) moves nothing: its pull only racks, and the engine's slide is left as it is. */
static void vrReloadMoveSlide(s32 h, struct hand *hand, struct vrreloaddef *def,
		struct weaponfunc *func, s32 wasracking, struct vrrinput *in)
{
	struct weaponfunc *selected;
	f32 slidemax, t;
	s32 loaded;

	if ((func->type & 0xff) != INVENTORYFUNCTYPE_SHOOT || !vrReloadRackIsSlide(def)) {
		return;
	}

	/* The primary's slidemax: bgunUpdateSlide reads the selected function's, which is 0 for the
	 * pistol whip */
	slidemax = ((struct weaponfunc_shoot *)func)->slidemax;

	loaded = hand->loadedammo[func->ammoindex] > 0;

	if (sGun[h].state != VRR_RACKING) {
		if (wasracking) {
			selected = currentPlayerGetWeaponFunction(h);

			if (!loaded && !sGun[h].racked) {
				hand->slidetrans = sSlideGrab[h];
			} else if (loaded && (selected == NULL || (selected->type & 0xff) != INVENTORYFUNCTYPE_SHOOT
						|| ((struct weaponfunc_shoot *)selected)->slidemax <= 0.0f)) {
				/* The pistol whip is selected: bgunUpdateSlide reads its slidemax of 0 and would
				 * never move the slide forward, so it goes forward here */
				hand->slidetrans = 0.0f;
			}
		}

		return;
	}

	if (!wasracking) {
		/* The last shot's recoil still under way on an empty gun: that slide is about to lock
		 * fully back, so the grab starts from there */
		sSlideGrab[h] = hand->slideinc && !loaded ? slidemax : hand->slidetrans;

		/* A shot's recoil still under way: the hand has the slide now, and on release it should go
		 * straight forward rather than finish the recoil first */
		hand->slideinc = false;
	}

	/* An untracked tick leaves the core and the slide as they were */
	if (!in->tracked) {
		return;
	}

	/* From fully forward, back with the pull: racktravel cm of pull is the slide's whole travel */
	t = (def->racktravel <= 0.0f) ? slidemax
		: in->rackpull / def->racktravel * slidemax;
	hand->slidetrans = vrReloadClamp(t, 0.0f, slidemax);
}

/* Row vector v through m's rotation (out_j = sum_i v_i * m[i][j], as mtx4RotateVec) */
static void vrReloadRot(Mtxf *m, const f32 *v, f32 *out)
{
	s32 j;

	for (j = 0; j < 3; j++) {
		out[j] = v[0] * m->m[0][j] + v[1] * m->m[1][j] + v[2] * m->m[2][j];
	}
}

/* Point v through m (as mtx4TransformVec) */
static void vrReloadXform(Mtxf *m, const f32 *v, f32 *out)
{
	s32 j;

	vrReloadRot(m, v, out);

	for (j = 0; j < 3; j++) {
		out[j] += m->m[3][j];
	}
}

/* Row vector v through the 3x3 rows r */
static void vrReloadRows(f32 r[3][3], const f32 *v, f32 *out)
{
	s32 j;

	for (j = 0; j < 3; j++) {
		out[j] = v[0] * r[0][j] + v[1] * r[1][j] + v[2] * r[2][j];
	}
}

/* The falling clip's world rotation now: the eject rotation turned end over end by d->angle about
 * the clip's local axis d->tumble, X or Z (applied in the local frame, so it is the same in
 * left-handed mode) */
static void vrReloadDropRot(struct vrrdrop *d, f32 out[3][3])
{
	f32 c = cosf(d->angle);
	f32 s = sinf(d->angle);
	s32 a = d->tumble == 2 ? 0 : 1; /* the two rows that turn */
	s32 b = d->tumble == 2 ? 1 : 2;
	s32 j;

	for (j = 0; j < 3; j++) {
		out[d->tumble == 2 ? 2 : 0][j] = d->rot[d->tumble == 2 ? 2 : 0][j];
		out[a][j] = c * d->rot[a][j] + s * d->rot[b][j];
		out[b][j] = -s * d->rot[a][j] + c * d->rot[b][j];
	}
}

/* Lowest world Y of the clip's centre line (its top and base) with the centre at y and rotation r */
static f32 vrReloadDropLow(struct vrrdrop *d, f32 r[3][3], f32 y)
{
	f32 half[3];

	vrReloadRows(r, d->pivot, half);

	return y - (half[1] < 0.0f ? -half[1] : half[1]);
}

/* Samples the player prop's position once per frame (the tick can run several times per frame)
 * and keeps the last frame's movement as a velocity. A skipped frame, a paused frame or a warp
 * gives none. */
static void vrReloadTrackPlayer(struct player *player)
{
	f32 pos[3];
	f32 dt = g_Vars.lvupdate60freal / 60.0f;
	f32 dist;
	s32 i;

	if (player->prop == NULL || sPlayerFrame == g_Vars.lvframenum) {
		return;
	}

	for (i = 0; i < 3; i++) {
		pos[i] = player->prop->pos.f[i];
	}

	dist = vrReloadDist(pos, sPlayerPos);

	for (i = 0; i < 3; i++) {
		sPlayerVel[i] = (sPlayerFrame >= 0 && sPlayerFrame == g_Vars.lvframenum - 1 && dt > 0.0f
				&& dist <= VRR_PLAYER_MAX_STEP) ? (pos[i] - sPlayerPos[i]) / dt : 0.0f;
		sPlayerPos[i] = pos[i];
	}

	sPlayerFrame = g_Vars.lvframenum;
}

/* Starts the ejected clip's fall from where it was last drawn (for a row with playdrop, the held
 * part's, as the playback drew it last). Nothing falls if the gun was not drawn lately. The gun
 * hand's velocity is added: vr_ctrl_velocity_play is m/s in the play space, which the inverse head
 * rotation turns into the view space (the gCtrlPos frame, as vrReloadShoulderHeadPos does) and
 * camGetProjectionMtxF into world. */
static void vrReloadStartDrop(s32 h, struct hand *hand, struct vrreloaddef *def)
{
	struct vrrclipworld *c = &sClipWorld[h];
	struct vrrdrop *d = &sDrop[h];
	Mtxf *cam2world = camGetProjectionMtxF();
	XrQuaternionf qheadinv;
	struct coord hv;
	f32 view[3];
	f32 world[3] = { 0.0f, 0.0f, 0.0f };
	f32 half[3];
	f32 axis[3];
	f32 len, sign;
	s32 i;

	d->active = 0;

	VRR_DIAG("drop: frame=%d record=%d weapon=%d/%d pos=(%.1f %.1f %.1f) waited=%d mode=%d posemode=%d ok=%d",
			g_Vars.lvframenum, c->frame, c->weaponnum, hand->gset.weaponnum,
			c->pos[0], c->pos[1], c->pos[2], sEjectWaited[h], VrLeftHandedMode, VrCtrlPoseMode,
			vrReloadClipWorldFresh(h, hand));

	if (!vrReloadClipWorldFresh(h, hand)) {
		return;
	}

	/* Half the insert depth below the clip's top: its middle when the depth is its length (Falcon
	 * 2), 0.6 cm above it for the MagSec 4 (16.7 cm clip, 15.5 cm depth), too little to see in a fall;
	 * a row with clipmid gives its clip's middle itself (the Reaper's magazine, whose joint is near
	 * its rear, would turn about a point 2 cm outside it by the depth). A held part that falls after its playback (playdrop: the Cyclone's bar) turns about its middle,
	 * half its length back from heldtop, and goes on the way it slid out (heldaxis, sign 1) where a
	 * clip comes back out of its well (sign -1) */
	if (vrReloadHasClip(def)) {
		f32 mid = def->clipmid != 0.0f ? def->clipmid : -def->clipinsertdepth * 0.5f;

		for (i = 0; i < 3; i++) {
			d->pivot[i] = def->clipaxis[i] * mid * VRR_MODEL_UNITS_PER_CM;
		}

		vrReloadRows(c->rot, def->clipaxis, axis);
		sign = -1.0f;
		d->tumble = 0;
	} else {
		for (i = 0; i < 3; i++) {
			d->pivot[i] = (def->heldtop[i] - def->heldaxis[i] * def->playdrop * 0.5f) * VRR_MODEL_UNITS_PER_CM;
		}

		vrReloadRows(c->rot, def->heldaxis, axis);
		sign = 1.0f;
		d->tumble = 2;
	}

	memcpy(d->rot, c->rot, sizeof(d->rot));
	vrReloadRows(d->rot, d->pivot, half);
	len = sqrtf(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);

	if (len < 1e-6f) {
		return;
	}

	if (cam2world != NULL && vrCtrlPoseGood(VRR_CTRL_GUN)) {
		hv.x = vr_ctrl_velocity_play[VRR_CTRL_GUN][0];
		hv.y = vr_ctrl_velocity_play[VRR_CTRL_GUN][1];
		hv.z = vr_ctrl_velocity_play[VRR_CTRL_GUN][2];

		qheadinv.x = -gRawHeadQ.x;
		qheadinv.y = -gRawHeadQ.y;
		qheadinv.z = -gRawHeadQ.z;
		qheadinv.w = gRawHeadQ.w;

		vr_rotate_vector_by_quaternion(&hv, &qheadinv);

		view[0] = hv.x;
		view[1] = hv.y;
		view[2] = hv.z;
		vrReloadRot(cam2world, view, world);
	}

	for (i = 0; i < 3; i++) {
		d->centre0[i] = c->pos[i] + half[i];
		d->centre[i] = d->centre0[i];
		d->vel[i] = (world[i] + sign * axis[i] / len * VRR_DROP_SPEED) * VRR_WORLD_UNITS_PER_M + sPlayerVel[i];
	}

	d->floory = g_Vars.currentplayer->vv_ground;
	d->usefloor = vrReloadDropLow(d, d->rot, d->centre0[1]) - VRR_DROP_REST_CM >= d->floory;
	d->weaponnum = hand->gset.weaponnum;
	d->stepframe = -1;
	d->drawframe = g_Vars.lvframenum;
	d->landed = 0;
	d->secs = 0.0f;
	d->angle = 0.0f;
	d->active = 1;
}

/* Advances the fall by this frame's game time: lvupdate60freal is 0 while paused, so the clip
 * stops in the air with the game */
static void vrReloadDropStep(struct vrrdrop *d)
{
	f32 r[3][3];
	f32 low, t;
	s32 i;

	d->secs += g_Vars.lvupdate60freal / 60.0f;

	if (d->secs >= VRR_DROP_SECS) {
		d->active = 0;
		return;
	}

	if (d->landed) {
		return;
	}

	t = d->secs;
	d->angle = DEG2RAD(VRR_DROP_TUMBLE_DEG) * t;

	for (i = 0; i < 3; i++) {
		d->centre[i] = d->centre0[i] + d->vel[i] * t;
	}

	d->centre[1] -= 0.5f * VRR_DROP_GRAVITY * VRR_WORLD_UNITS_PER_M * t * t;

	if (d->usefloor) {
		vrReloadDropRot(d, r);
		low = vrReloadDropLow(d, r, d->centre[1]) - VRR_DROP_REST_CM;

		if (low < d->floory) {
			d->centre[1] += d->floory - low;
			d->landed = 1;
		}
	}
}

/* The holder of a row with one (holdermtx), once per frame (the tick can run several times per
 * frame): open at once while no clip is in or the inserted one still slides to seated (sFinish), as
 * the reload animation turns it open at its first frame; then it turns shut over
 * VRR_HOLDER_CLOSE_60THS of game time (none while paused) and plays sndrack as it lands, as the
 * animation plays its click when the holder lands. vrReloadPoseHolder draws it. */
static void vrReloadStepHolder(s32 h, struct vrreloaddef *def)
{
	if (def->holdermtx < 0) {
		sHolderOpen[h] = 0.0f;
		return;
	}

	if (!sGun[h].magin || sFinish[h] > 0.0f) {
		sHolderOpen[h] = 1.0f;
		sHolderFrame[h] = g_Vars.lvframenum;
		return;
	}

	if (sHolderOpen[h] <= 0.0f || sHolderFrame[h] == g_Vars.lvframenum) {
		return;
	}

	sHolderFrame[h] = g_Vars.lvframenum;
	sHolderOpen[h] -= g_Vars.lvupdate60freal / VRR_HOLDER_CLOSE_60THS;

	if (sHolderOpen[h] <= 0.0f) {
		sHolderOpen[h] = 0.0f;

		if (def->sndrack != 0) {
			sndStart(var80095200, def->sndrack, 0, -1, -1, -1, -1, -1);
		}
	}
}

/* The rack matrix of a row with rackmove (the RC-P120's knob), after the core's tick: while racking it
 * follows the pull, up to racktravel (an untracked tick leaves it where it was); the tick the rack
 * ends (racked by a full pull, whose in->rackpull is the full travel, or let go early) it is where the
 * last pull put it, and then it goes forward once per frame over VRR_RACK_RETURN_60THS of full
 * travel's time (none passes while paused). vrReloadMoveRack draws it. A row with pumpaction (the
 * Shotgun's forend) racked by a full pull with the grip still held rides on with the hand instead
 * (sPumpRide): its pull (along, the hand's place on the rack axis, against sPumpFwd), so the pump
 * comes forward with the hand, not ahead of it, until the hand is within VRR_PUMP_SHUT_FRAC of the
 * travel of the grab point (sRackGrab) or the grip lets go (then it springs forward as the knob
 * does). */
static void vrReloadStepRack(s32 h, struct vrreloaddef *def, s32 wasracking, struct vrrinput *in, f32 along)
{
	if (!def->rackmove || def->racktravel <= 0.0f) {
		sRackBack[h] = 0.0f;
		sPumpRide[h] = 0;
		return;
	}

	if (sGun[h].state == VRR_RACKING || wasracking) {
		if (in->tracked) {
			sRackBack[h] = vrReloadClamp(in->rackpull, 0.0f, def->racktravel);
		}

		sRackBackFrame[h] = g_Vars.lvframenum;
		sPumpRide[h] = def->pumpaction && sGun[h].state != VRR_RACKING && sGun[h].racked && in->grip;
		return;
	}

	if (sPumpRide[h]) {
		f32 pull = along - sRackGrab[h];

		if (!in->tracked) {
			sRackBackFrame[h] = g_Vars.lvframenum;
			return;
		}

		if (in->grip && pull > def->racktravel * VRR_PUMP_SHUT_FRAC) {
			sRackBack[h] = vrReloadClamp(along - sPumpFwd[h], 0.0f, def->racktravel);
			sRackBackFrame[h] = g_Vars.lvframenum;
			return;
		}

		VRR_DIAG("pump: ride end frame=%d pull=%.1f why=%s", g_Vars.lvframenum, pull, in->grip ? "shut" : "released");
		sPumpRide[h] = 0;
	}

	if (sRackBack[h] <= 0.0f || sRackBackFrame[h] == g_Vars.lvframenum) {
		return;
	}

	sRackBackFrame[h] = g_Vars.lvframenum;
	sRackBack[h] -= def->racktravel * g_Vars.lvupdate60freal / VRR_RACK_RETURN_60THS;

	if (sRackBack[h] < 0.0f) {
		sRackBack[h] = 0.0f;
	}
}

/* The row's second insert sound (sndinsert2, or sndfull for a shell that fills the gun), insert2delay
 * 60ths of game time after the insert (none pass while paused): the AR34's and the K7 Avenger's hand
 * tapping the seated clip, the Shotgun's shell clicking in. A clip no longer in (B before it) drops it. */
static void vrReloadStepInsert2(s32 h, struct vrreloaddef *def)
{
	if (sInsert2At[h] < 0) {
		return;
	}

	if (!sGun[h].magin || sInsert2Snd[h] == 0) {
		sInsert2At[h] = -1;
		return;
	}

	if (g_Vars.lvframe60 < sInsert2At[h]) {
		return;
	}

	sInsert2At[h] = -1;
	sndStart(var80095200, sInsert2Snd[h], 0, -1, -1, -1, -1, -1);
}

/* The velocity in play space (m/s) of a point fixed to logical controller c, at view-space point p
 * (the gCtrlPos frame, cm): the controller's linear velocity plus its angular velocity crossed with the
 * point's offset from the controller, turned into play space by the head rotation. Both velocities
 * are the runtime's, in play space (vr_ctrl_velocity_play, vr_ctrl_angvel_play, logical indices as
 * gCtrlPos) */
static void vrReloadPointVelocity(s32 c, const f32 *p, f32 *out)
{
	struct coord r;
	f32 *w = vr_ctrl_angvel_play[c];

	r.x = (p[0] - gCtrlPos[c][0]) / 100.0f;
	r.y = (p[1] - gCtrlPos[c][1]) / 100.0f;
	r.z = (p[2] - gCtrlPos[c][2]) / 100.0f;
	vr_rotate_vector_by_quaternion(&r, &gRawHeadQ);

	out[0] = vr_ctrl_velocity_play[c][0] + w[1] * r.z - w[2] * r.y;
	out[1] = vr_ctrl_velocity_play[c][1] + w[2] * r.x - w[0] * r.z;
	out[2] = vr_ctrl_velocity_play[c][2] + w[0] * r.y - w[1] * r.x;
}

/* The free hand's palm's velocity relative to the gun's slap point, in gCtrlPos's frame (the
 * raw OpenXR view space), m/s: each point's own velocity (vrReloadPointVelocity), so a turn of
 * either hand counts (tipping the rifle down onto a still hand is a slap; a hand rising with a
 * rising clip is not), the difference turned into the view by the inverse head rotation as the
 * dropped clip's is. The runtime's velocities do not depend on how many ticks a frame runs. */
static void vrReloadSlapVelocity(const f32 *palm, const f32 *slap, f32 *out)
{
	XrQuaternionf qheadinv;
	struct coord v;
	f32 vo[3], vg[3];

	vrReloadPointVelocity(VRR_CTRL_OFF, palm, vo);
	vrReloadPointVelocity(VRR_CTRL_GUN, slap, vg);

	v.x = vo[0] - vg[0];
	v.y = vo[1] - vg[1];
	v.z = vo[2] - vg[2];

	qheadinv.x = -gRawHeadQ.x;
	qheadinv.y = -gRawHeadQ.y;
	qheadinv.z = -gRawHeadQ.z;
	qheadinv.w = gRawHeadQ.w;

	vr_rotate_vector_by_quaternion(&v, &qheadinv);

	out[0] = v.x;
	out[1] = v.y;
	out[2] = v.z;
}

/* Ends a swing being watched for the log (vrrslap.swing): one "refused" line when it came near the
 * slap point, with why it did not slap (the first test it never passed, or the gate that cut it
 * short) */
static void vrReloadSlapSwingEnd(s32 h, struct vrreloaddef *def, const char *gate)
{
	struct vrrslap *sl = &sSlap[h];

	if (!sl->swing) {
		return;
	}

	sl->swing = 0;

	if (sl->mindist <= def->slaprad * VRR_SLAP_LOG_RADS) {
		/* A swing near the clip that did not slap is a missed slap for the hints; a gate that cut it
		 * short (the state changed, tracking lost, a pause) is not */
		if (gate == NULL && h == HAND_RIGHT) {
			sHintMisses++;
		}

		VRR_DIAG("slap: frame=%d weapon=%d refused peak=%.2f side=%.2f mindist=%.1f along=%.1f below=%d why=%s",
				g_Vars.lvframenum, sGunWeapon[h], sl->peak, sl->peakside, sl->mindist, sl->minalong,
				sl->swingbelow,
				gate != NULL ? gate : sl->peak < VRR_SLAP_SPEED ? "slow" : !sl->fastaxial ? "side"
				: !sl->swingbelow ? "notbelow" : sl->mindist > def->slaprad ? "far" : "timing");
	}
}

/* Ends a roll of a row with a close being watched for the log (vrrslap.swing): one "refused" line, and a
 * missed close for the hints when it was a flick's speed (not about the barrel) and no gate cut it
 * short */
static void vrReloadCloseRollEnd(s32 h, const char *gate)
{
	struct vrrslap *sl = &sSlap[h];
	f32 mag = sl->peak < 0.0f ? -sl->peak : sl->peak;

	if (!sl->swing) {
		return;
	}

	sl->swing = 0;

	if (gate == NULL && mag >= VRR_FLICK_RADS && h == HAND_RIGHT) {
		sHintMisses++;
	}

	VRR_DIAG("close: frame=%d weapon=%d refused roll=%.2f spin=%.2f why=%s",
			g_Vars.lvframenum, sGunWeapon[h], sl->peak, sl->peakside,
			gate != NULL ? gate : mag < VRR_FLICK_RADS ? "slow" : "notroll");
}

/* The close of a row with one (vrReloadHasClose: the DY357's cylinder, loaded and waiting to be shut),
 * once per frame, in place of the slap of a clip (see VRR_FLICK_RADS): a flick of the gun hand or a
 * push of the free hand sets pending, which the input hands to the core (vrrinput.slap: its rack, the
 * close), and buzzes the hand that made it. No hand is drawn: the close's playback (vrReloadStartPlay,
 * VRR_PLAY_CLOSE) starts with the core's rack. Nothing while the rounds still go in (the insert's
 * playback), so the cylinder shuts on them */
static void vrReloadStepClose(s32 h, struct hand *hand, struct vrreloaddef *def)
{
	struct player *player = g_Vars.currentplayer;
	struct vrrslap *sl = &sSlap[h];
	struct vrrpartsview v;
	XrQuaternionf qheadinv;
	struct coord w;
	f32 palm[3], d[3], vel[3];
	f32 roll, rollmag, spin, dist, toward;
	const char *gate, *by;

	if (sl->checkframe == g_Vars.lvframenum) {
		return;
	}

	sl->checkframe = g_Vars.lvframenum;

	/* A close the core has not taken is dropped once the gun waits for none (B, a draw) */
	if (sl->pending && sGun[h].state != VRR_NEED_RACK) {
		sl->pending = 0;
	}

	if (sl->pending) {
		return;
	}

	gate = sGun[h].state != VRR_NEED_RACK ? "state"
		: sPlayFrame[h] > 0.0f ? "playing"
		: (!vrCtrlPoseGood(VRR_CTRL_OFF) || !vrCtrlPoseGood(VRR_CTRL_GUN) || VrLeftHandedMode != VrCtrlPoseMode) ? "tracking"
		: (player->pausemode != PAUSEMODE_UNPAUSED || lvIsPaused() || g_Vars.lvupdate60freal <= 0.0f) ? "paused"
		: (!vrReloadPartsNow(h, hand, 1, &v) || !v.hasclose) ? "parts" : NULL;

	if (gate != NULL) {
		vrReloadCloseRollEnd(h, gate);
		return;
	}

	/* The flick: the gun controller's turn rate (the runtime's, in play space, turned into the view by
	 * the inverse head rotation as the slap's velocities are) about the roll axis, either way */
	qheadinv.x = -gRawHeadQ.x;
	qheadinv.y = -gRawHeadQ.y;
	qheadinv.z = -gRawHeadQ.z;
	qheadinv.w = gRawHeadQ.w;
	w.x = vr_ctrl_angvel_play[VRR_CTRL_GUN][0];
	w.y = vr_ctrl_angvel_play[VRR_CTRL_GUN][1];
	w.z = vr_ctrl_angvel_play[VRR_CTRL_GUN][2];
	vr_rotate_vector_by_quaternion(&w, &qheadinv);

	spin = sqrtf(w.x * w.x + w.y * w.y + w.z * w.z);
	roll = w.x * v.flickaxis[0] + w.y * v.flickaxis[1] + w.z * v.flickaxis[2];
	rollmag = roll < 0.0f ? -roll : roll;
	by = rollmag >= VRR_FLICK_RADS && rollmag >= VRR_FLICK_AXIAL * spin ? "flick" : NULL;

	/* The push: the free hand empty (no gun of a pair in it), its palm at the open part, moving into the
	 * gun against it (each point's own velocity, so the gun swung onto a still hand counts). dist stays
	 * -1 for a flick */
	dist = -1.0f;
	toward = 0.0f;

	if (by == NULL && v.hasslap && !(player->hands[HAND_LEFT].inuse && !vrHandParked(HAND_LEFT))) {
		s32 i;

		vrReloadHandPalm(palm);

		for (i = 0; i < 3; i++) {
			d[i] = palm[i] - v.slappos[i];
		}

		dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);

		if (dist <= def->slaprad) {
			vrReloadSlapVelocity(palm, v.slappos, vel);
			toward = vel[0] * v.closedir[0] + vel[1] * v.closedir[1] + vel[2] * v.closedir[2];

			if (toward >= VRR_TOUCH_SPEED) {
				by = "touch";
			}
		}
	}

	if (by != NULL) {
		VRR_DIAG("close: frame=%d weapon=%d by=%s roll=%.2f spin=%.2f dist=%.1f toward=%.2f",
				g_Vars.lvframenum, sGunWeapon[h], by, roll, spin, dist, toward);
		sl->pending = 1;
		sl->swing = 0;
		vrReloadBuzz(dist < 0.0f ? VRR_CTRL_GUN : VRR_CTRL_OFF, VRR_BUZZ_SLAP_AMP, VRR_BUZZ_SLAP_SECS);
		return;
	}

	/* A fast roll about the barrel that did not close: one line when it ends */
	if (roll >= VRR_FLICK_LOG_RADS || roll <= -VRR_FLICK_LOG_RADS) {
		if (!sl->swing) {
			sl->swing = 1;
			sl->peak = 0.0f;
		}

		if ((roll < 0.0f ? -roll : roll) > (sl->peak < 0.0f ? -sl->peak : sl->peak)) {
			sl->peak = roll;
			sl->peakside = spin;
		}
	} else {
		vrReloadCloseRollEnd(h, NULL);
	}
}

/* The slap of a row with rackslap, once per frame (the tick can run several times per frame), before
 * the input is built: a slap under way steps a frame of the reload animation per 60th of game time
 * (none while paused) from slapfrom, and on reaching slapcontact sets pending, which the input hands
 * to the core (vrrinput.slap); from slapto the drawn hand goes back to the controller over
 * VRR_SLAP_EASE_60THS. B before the contact (the gun no longer needs a rack) gives no slap: the hand
 * goes back from where it is, and a clip grabbed meanwhile ends the slap at once (the clip hand is
 * drawn). With no slap under way and the gun needing a rack, the detection (see VRR_SLAP_SPEED)
 * starts one, and a fast swing up that does not is logged once when it ends. */
static void vrReloadStepSlap(s32 h, struct hand *hand, struct vrreloaddef *def)
{
	struct player *player = g_Vars.currentplayer;
	struct vrrslap *sl = &sSlap[h];
	struct vrrpartsview v;
	f32 palm[3], d[3], vel[3], gund[3], prev[3], zero[3] = { 0.0f, 0.0f, 0.0f };
	f32 along, lateral, dist, near, t, up, speed, side;
	s32 i, frombelow, fire, hadprev;
	const char *gate;

	/* A row with no clip closed by a flick or a push (the DY357's cylinder) has its own detection */
	if (vrReloadHasClose(def)) {
		vrReloadStepClose(h, hand, def);
		return;
	}

	if (!def->rackslap || def->slaprad <= 0.0f || def->slapto <= 0.0f || !vrReloadHasClip(def)
			|| vrReloadHandHoldsClip(h)) {
		if (sl->frame > 0.0f || sl->swing || sl->pending || sl->prevok) {
			vrReloadSlapReset(h);
		}

		return;
	}

	if (sl->checkframe == g_Vars.lvframenum) {
		return;
	}

	/* The last frame's palm is the start of this frame's path only if that frame was checked */
	hadprev = sl->prevok && g_Vars.lvframenum - sl->checkframe == 1;
	sl->prevok = 0;
	sl->checkframe = g_Vars.lvframenum;

	/* A contact the core has not taken is dropped once the gun needs no rack (B, a draw) */
	if (sl->pending && sGun[h].state != VRR_NEED_RACK) {
		sl->pending = 0;
	}

	if (sl->frame > 0.0f) {
		f32 was = sl->frame;

		if (sl->ease > 0.0f) {
			sl->ease -= g_Vars.lvupdate60freal;

			if (sl->ease <= 0.0f) {
				vrReloadSlapReset(h);
			}

			return;
		}

		/* The gun stopped needing a rack before the palm got there: no slap */
		if (was < def->slapcontact && sGun[h].state != VRR_NEED_RACK) {
			VRR_DIAG("slap: frame=%d weapon=%d stopped at anim %.1f (state %d): no slap",
					g_Vars.lvframenum, sGunWeapon[h], was, sGun[h].state);
			sl->ease = VRR_SLAP_EASE_60THS;
			return;
		}

		sl->frame += g_Vars.lvupdate60freal;

		if (was < def->slapcontact && sl->frame >= def->slapcontact) {
			sl->pending = 1;
			VRR_DIAG("slap: frame=%d weapon=%d contact at anim %.1f", g_Vars.lvframenum, sGunWeapon[h], sl->frame);
		}

		if (sl->frame >= def->slapto) {
			sl->frame = def->slapto;
			sl->ease = VRR_SLAP_EASE_60THS;
		}

		return;
	}

	/* The detection: the gun needs a rack, the free hand is empty (no gun of a pair in it; it holds
	 * no clip in VRR_NEED_RACK), both controllers are tracked with this frame's gun side, the game
	 * runs, and the seated clip was drawn lately. A gate that closes ends a swing being watched */
	gate = sGun[h].state != VRR_NEED_RACK ? "state"
		: (player->hands[HAND_LEFT].inuse && !vrHandParked(HAND_LEFT)) ? "hand"
		: (!vrCtrlPoseGood(VRR_CTRL_OFF) || !vrCtrlPoseGood(VRR_CTRL_GUN) || VrLeftHandedMode != VrCtrlPoseMode) ? "tracking"
		: (player->pausemode != PAUSEMODE_UNPAUSED || lvIsPaused() || g_Vars.lvupdate60freal <= 0.0f) ? "paused"
		: (!vrReloadPartsNow(h, hand, 1, &v) || !v.hasslap) ? "parts" : NULL;

	if (gate != NULL) {
		vrReloadSlapSwingEnd(h, def, gate);
		return;
	}

	vrReloadHandPalm(palm);
	vrReloadSlapVelocity(palm, v.slappos, vel);

	for (i = 0; i < 3; i++) {
		d[i] = palm[i] - v.slappos[i];
	}

	along = d[0] * v.clipaxis[0] + d[1] * v.clipaxis[1] + d[2] * v.clipaxis[2];
	dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	lateral = sqrtf(dist * dist - along * along > 0.0f ? dist * dist - along * along : 0.0f);
	up = vel[0] * v.clipaxis[0] + vel[1] * v.clipaxis[1] + vel[2] * v.clipaxis[2];
	speed = sqrtf(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);
	side = sqrtf(speed * speed - up * up > 0.0f ? speed * speed - up * up : 0.0f);

	/* The radius is tested on the path since the last frame, not only where the palm is now:
	 * at 4.7 m/s and 36 to 45 frames a second it moves 10 to 13 cm a frame and could step over the
	 * slaprad sphere (12.5 cm on the slap rows). The path is kept in the gun hand's frame, so the
	 * gun's own move between frames does not count as the hand's */
	near = dist;

	if (hadprev) {
		vrReloadGunToView(&hand->posrotmtx, sl->prevd, prev);
		near = vrReloadSegDist(zero, prev, d, &t);
	}

	vrReloadViewToGun(&hand->posrotmtx, d, sl->prevd);
	sl->prevok = 1;

	if (along <= -VRR_SLAP_BELOW_CM && lateral <= def->slaprad * VRR_SLAP_BELOW_SIDE) {
		sl->belowat = g_Vars.lvframe60;
	}

	frombelow = sl->belowat >= 0 && g_Vars.lvframe60 >= sl->belowat
		&& g_Vars.lvframe60 - sl->belowat <= VRR_SLAP_BELOW_60THS;
	fire = up >= VRR_SLAP_SPEED && up >= VRR_SLAP_AXIAL_MIN * speed && near <= def->slaprad && frombelow;

	if (fire) {
		VRR_DIAG("slap: frame=%d weapon=%d start up=%.2f side=%.2f dist=%.1f path=%.1f along=%.1f lateral=%.1f below=%d (%d 60ths ago) anim=%.0f..%.0f..%.0f",
				g_Vars.lvframenum, sGunWeapon[h], up, side, dist, near, along, lateral, frombelow,
				g_Vars.lvframe60 - sl->belowat, def->slapfrom, def->slapcontact, def->slapto);
		sl->frame = def->slapfrom > 0.0f ? def->slapfrom : 1.0f;
		sl->ease = 0.0f;
		sl->swing = 0;
		sl->belowat = -1;
		sl->prevok = 0;

		/* A row whose clip plays with the slap (the Laptop Gun's, rocked flat and slid into the seat)
		 * starts it with the hand, from playfrom (its slapfrom), stepping as the hand does */
		if (vrReloadHasSlapPlay(def)) {
			vrReloadStartPlay(h, hand, def, 0);
		}

		return;
	}

	/* A fast swing up that did not slap: one line when it ends, near the clip only */
	if (up >= VRR_SLAP_LOG_SPEED) {
		if (!sl->swing) {
			sl->swing = 1;
			sl->peak = 0.0f;
			sl->mindist = 1e9f;
			sl->fastaxial = 0;
			sl->swingbelow = 0;
		}

		if (up > sl->peak) {
			sl->peak = up;
			sl->peakside = side;
		}

		if (near < sl->mindist) {
			sl->mindist = near;
			sl->minalong = along;
		}

		sl->fastaxial |= up >= VRR_SLAP_SPEED && up >= VRR_SLAP_AXIAL_MIN * speed;
		sl->swingbelow |= frombelow;
	} else {
		vrReloadSlapSwingEnd(h, def, NULL);
	}
}

/* Starts a playback: the insert's (playfrom..playto, the Phoenix's orb going in) or, with eject,
 * the eject's (ejectfrom..ejectto, the Laptop Gun's pump), or with eject VRR_PLAY_CLOSE the close's
 * (closefrom..closeto on the eject's matrices: the DY357's cylinder swinging back in), replacing
 * any under way. Its first frame is drawn this frame, and it steps from the next. The insert's of a
 * row with playease hands the held part over from where the free hand held it: that pose is kept on
 * the gun (sHandoff) from the last capture of the held part (vrReloadCaptureHeldClip, last frame's
 * draw), placed with the off controller now */
static void vrReloadStartPlay(s32 h, struct hand *hand, struct vrreloaddef *def, s32 eject)
{
	s32 close = eject == VRR_PLAY_CLOSE;
	f32 from = close ? def->closefrom : eject ? def->ejectfrom : def->playfrom;
	s32 i;

	vrReloadEndPlay(h, "restart");
	sPlayFrame[h] = from;
	sPlayFrameNum[h] = g_Vars.lvframenum;
	sPlayDrawn[h] = 0;
	sPlayLogged[h] = 0;
	sPlayEject[h] = eject != 0;
	sPlayClose[h] = close;
	sHandoff[h].left = 0.0f;

	/* A stroke on the first frame plays at once */
	for (i = 0; i < VR_RELOAD_EJECT_SND_MAX; i++) {
		if (eject && !close && def->ejectsnd[i] != 0 && def->ejectsndat[i] <= from) {
			sndStart(var80095200, def->ejectsnd[i], 0, -1, -1, -1, -1, -1);
		}
	}

	if (close && def->sndrack != 0 && def->closesndat <= from) {
		sndStart(var80095200, def->sndrack, 0, -1, -1, -1, -1, -1);
	}

	for (i = 0; i < VR_RELOAD_PLAY_SND_MAX; i++) {
		if (!eject && def->playsnd[i] != 0 && def->playsndat[i] <= from) {
			sndStart(var80095200, def->playsnd[i], 0, -1, -1, -1, -1, -1);
		}
	}

	if (!eject && def->playease > 0.0f && sHeldMtx[h].weaponnum >= 0 && sHeldMtx[h].weaponnum == hand->gset.weaponnum) {
		struct vrrheldmtx *hm = &sHeldMtx[h];
		Mtxf r;
		f32 anchor[3], v[3], rel[3];

		vrReloadOffRot(&r);
		vrReloadGunAnchor(h, hand, anchor);

		for (i = 0; i < 3; i++) {
			vrReloadGunToView(&r, hm->rows[i], v);
			vrReloadViewToGun(&hand->posrotmtx, v, sHandoff[h].rows[i]);
		}

		vrReloadGunToView(&r, hm->pos, v);

		for (i = 0; i < 3; i++) {
			rel[i] = v[i] + gCtrlPos[VRR_CTRL_OFF][i] - anchor[i];
		}

		vrReloadViewToGun(&hand->posrotmtx, rel, sHandoff[h].pos);
		sHandoff[h].left = def->playease;
	}

	VRR_DIAG("play: frame=%d hand=%d weapon=%d start=%.0f to=%.0f heldto=%.0f mtx=%d..%d eject=%d ease=%.0f",
			g_Vars.lvframenum, h, sGunWeapon[h], sPlayFrame[h], close ? def->closeto : eject ? def->ejectto : def->playto,
			eject ? 0.0f : def->playheldto, eject ? def->ejectmtx : def->playmtx,
			eject ? def->ejectmtxlast : def->playmtxlast, eject, sHandoff[h].left);
}

/* The playback of a row with one (playto, or ejectto for the eject's, closeto for the close's),
 * once per frame (the tick can run several times per frame): a frame of the reload animation per
 * 60th of game time, the reload script's speed (none while paused), from playfrom, where the insert
 * starts it, to playto, which is drawn once before it ends, and plays playsnd as it reaches each
 * playsndat. A gun with no clip in (B mid-playback) ends the insert's. The eject's runs from
 * ejectfrom to ejectto whatever the clip does, and plays ejectsnd as it reaches each ejectsndat;
 * one that carries the clip out (ejectdrop) ends when a new clip is guided or put in; at the end of
 * one with ejectcasings the spent casings fall. The close's runs from closefrom to closeto and
 * plays sndrack as it reaches closesndat. A row that holds a last frame (ejecthold, playhold, a
 * close's wait) is drawn there after the end by vrReloadPosePlay from the gun's state, with no
 * playback running. At the end of the insert's of a row with playdrop the held part falls, and at
 * the end of an eject's with ejectdrop the clip, on the driven gun only (the left gun of a pair is
 * not drawn falling: its tick ends every fall). The slap's (vrReloadHasSlapPlay) runs as the
 * insert's. The hand-off (sHandoff) runs down here too. vrReloadPosePlay draws it. */
static void vrReloadStepPlay(s32 h, struct vrreloaddef *def)
{
	f32 to, prev;
	s32 i;

	if (sPlayFrame[h] <= 0.0f) {
		return;
	}

	if (sPlayClose[h] ? !vrReloadHasClose(def)
			: sPlayEject[h] ? !vrReloadHasEjectPlay(def) : !vrReloadHasPlay(def) && !vrReloadHasSlapPlay(def)) {
		vrReloadEndPlay(h, "stop");
		return;
	}

	if (!sPlayEject[h] && !sGun[h].magin) {
		vrReloadEndPlay(h, "eject");
		return;
	}

	/* A new clip guided or put in takes over the clip matrix from an eject playback carrying the old
	 * one out: it ends, and the old clip does not fall */
	if (sPlayEject[h] && vrReloadEjectDrops(def) && (sGuided[h] || sGun[h].magin)) {
		vrReloadEndPlay(h, "clip");
		return;
	}

	if (sPlayFrameNum[h] == g_Vars.lvframenum) {
		return;
	}

	sPlayFrameNum[h] = g_Vars.lvframenum;
	to = sPlayClose[h] ? def->closeto : sPlayEject[h] ? def->ejectto : def->playto;

	if (sPlayFrame[h] >= to) {
		s32 drop = (sPlayEject[h] ? vrReloadEjectDrops(def) : vrReloadPlayDrops(def)) && h == HAND_RIGHT;
		/* The eject's swing out ends: the spent rounds' casings fall from the chambers (ejectcasings),
		 * on the driven gun only, as a falling clip */
		s32 casings = sPlayEject[h] && !sPlayClose[h] && def->ejectcasings && h == HAND_RIGHT ? sCasingsDue[h] : 0;

		vrReloadEndPlay(h, "done");

		if (casings > 0) {
			sCasings[h] = casings;
			VRR_DIAG("casings: frame=%d weapon=%d %d", g_Vars.lvframenum, sGunWeapon[h], casings);
		}

		if (drop) {
			vrReloadStartDrop(h, &g_Vars.currentplayer->hands[h], def);
		}

		return;
	}

	/* A held part that goes in straight (playstraightmtx) is first eased onto playfrom's pose, behind
	 * the matrix it goes into: the playback waits there until the hand-off has ended. Eased while the
	 * playback ran, the DY357's rounds were half in the chambers before they had turned onto them */
	if (!sPlayEject[h] && def->playstraightmtx != 0 && sHandoff[h].left > 0.0f) {
		sHandoff[h].left -= g_Vars.lvupdate60freal;

		if (sHandoff[h].left < 0.0f) {
			sHandoff[h].left = 0.0f;
		}

		return;
	}

	prev = sPlayFrame[h];
	sPlayFrame[h] += g_Vars.lvupdate60freal;

	if (sPlayFrame[h] > to) {
		sPlayFrame[h] = to;
	}

	if (sPlayClose[h]) {
		if (def->sndrack != 0 && prev < def->closesndat && sPlayFrame[h] >= def->closesndat) {
			sndStart(var80095200, def->sndrack, 0, -1, -1, -1, -1, -1);
		}
	} else if (sPlayEject[h]) {
		for (i = 0; i < VR_RELOAD_EJECT_SND_MAX; i++) {
			if (def->ejectsnd[i] != 0 && prev < def->ejectsndat[i] && sPlayFrame[h] >= def->ejectsndat[i]) {
				sndStart(var80095200, def->ejectsnd[i], 0, -1, -1, -1, -1, -1);
			}
		}
	} else {
		for (i = 0; i < VR_RELOAD_PLAY_SND_MAX; i++) {
			if (def->playsnd[i] != 0 && prev < def->playsndat[i] && sPlayFrame[h] >= def->playsndat[i]) {
				sndStart(var80095200, def->playsnd[i], 0, -1, -1, -1, -1, -1);
			}
		}
	}

	if (sHandoff[h].left > 0.0f) {
		sHandoff[h].left -= g_Vars.lvupdate60freal;

		if (sHandoff[h].left < 0.0f) {
			sHandoff[h].left = 0.0f;
		}
	}
}

/* The row's hint on the HUD, "<gun name>: <hint>" (the weapon names end in a newline) */
static void vrReloadShowHint(s32 h, struct vrreloaddef *def, const char *why)
{
	char text[128];
	const char *name = bgunGetName(sGunWeapon[h]);
	s32 len = 0;

	while (name[len] != '\0' && name[len] != '\n' && len < 48) {
		len++;
	}

	snprintf(text, sizeof(text), "%.*s: %s", len, name, def->hint);
	vrHolsterHudLine(text, VRR_HINT_SHOW_60THS, COLOR_09_BLUE);
	sHintLast = g_Vars.lvframe60;
	VRR_DIAG("hint: frame=%d weapon=%d why=%s misses=%d", g_Vars.lvframenum, sGunWeapon[h], why, sHintMisses);
}

/* The reload hints for the driven gun, after the core's tick (see sHintSeen). wasstate and wasgrip
 * are the core's state and grip before it, in for the grip press and the reserve */
static void vrReloadStepHint(s32 h, struct vrreloaddef *def, const struct vrrinput *in, const struct vrroutput *out,
		s32 wasstate, s32 wasgrip)
{
	struct vrrgun *g = &sGun[h];
	struct vrreloaddef *first = vrReloadDefAt(0);
	s32 row = first != NULL ? (s32) (def - first) : -1;
	s32 now = g_Vars.lvframe60;
	s32 kind;

	if (h != HAND_RIGHT || def->hint == NULL || !in->tracked || in->paused) {
		return;
	}

	/* A new level restarts lvframe60: a last hint "in the future" is from the one before */
	if (sHintLast > now) {
		sHintLast = -100000;
	}

	/* A grip press of the free hand (empty, or its gun of a pair parked: not a pair's left gun) that
	 * does not start a due grip rack misses it; a rack started or a slap taken (both send the rack
	 * effect, or the core is racking) ends the run, as does a gun no longer due */
	if (wasstate == VRR_NEED_RACK && !def->rackslap && !def->loadsoninsert && in->grip && !wasgrip
			&& g->state != VRR_RACKING
			&& !(g_Vars.currentplayer->hands[HAND_LEFT].inuse && !vrHandParked(HAND_LEFT))) {
		sHintMisses++;
	}

	if ((out->fx & VRR_FX_RACK) || g->state != VRR_NEED_RACK) {
		sHintMisses = 0;
	}

	vrReloadHintAgent();

	/* The first reload of this row's gun by the agent: on its eject (B). A gun loaded shell by shell
	 * ejects nothing: on B all the same (it does nothing else there), or its first shell taken; one
	 * pumped after each shot, on its first wait for the pump (after its first shot) */
	if (((out->fx & VRR_FX_EJECT)
				|| (def->shellbyshell && (in->eject || (out->fx & VRR_FX_GRAB_CLIP)))
				|| (def->pumpaction && (g->state == VRR_NEED_RACK || g->state == VRR_RACKING)))
			&& row >= 0 && row < VRR_HINT_ROWS && !sHintSeen[row]) {
		sHintSeen[row] = 1;
		vrReloadHintSave();
		vrReloadShowHint(h, def, "first");
		return;
	}

	kind = g->state == VRR_CLIP_OUT && in->reserve > 0 ? 1 : vrReloadWaitsForRack(h) ? 2 : 0;

	if (kind != sHintStuckKind) {
		sHintStuckKind = kind;
		sHintStuckSince = now;
	}

	if (now - sHintLast < VRR_HINT_GAP_60THS) {
		return;
	}

	if (sHintMisses >= VRR_HINT_MISSES) {
		vrReloadShowHint(h, def, "misses");
		sHintMisses = 0;
	} else if (kind != 0 && now - sHintStuckSince >= VRR_HINT_STUCK_60THS) {
		vrReloadShowHint(h, def, kind == 1 ? "clipout" : "due");
	}
}

/* The function changed to another magazine's (the SuperDragon's rifle and grenade launcher, Task F9e):
 * the driven magazine (olddef's) is parked as it stands, its shape in sPending, and the core stops
 * driving it as a suspend does: a clip in the free hand is dropped (its rounds never left the
 * reserve), a rack or slap under way, the guide, the playback and a falling clip end, and an
 * uncounted clip goes back to the reserve (the parked shape keeps it, taken out again when it is
 * driven). The caller's tick then drives the selected function's magazine from its parked shape, or
 * from the engine's count with its clip in when it has none. One driven magazine per hand, swapped
 * here, rather than a core state per magazine driven side by side: only the selected function's
 * magazine can be ejected, guided or racked, and every per-hand gesture (the eject wait, the guide,
 * the playback, the slap, the captures of the drawn parts) would otherwise need a copy per magazine.
 * A B press not yet taken is the new function's magazine's and is kept for it; one already waiting
 * for the old clip's drawn position (sEjectSince) was the old magazine's and is dropped */
static void vrReloadSwapMagazine(s32 h, struct hand *hand, struct vrreloaddef *olddef, struct vrreloaddef *newdef)
{
	struct vrrgun old;
	struct vrrgun keep[VRR_MAGS];
	s32 keepweapon[VRR_MAGS];
	s32 weaponnum = sGunWeapon[h];
	s32 oldammo = olddef->ammoindex;
	s32 eject = sEject[h] && sEjectSince[h] < 0;
	s32 i;

	if (oldammo < 0 || oldammo >= VRR_MAGS) {
		vrReloadSuspend(h, 0);
		return;
	}

	/* Settled in place before the suspend, its count given to the engine: the grab ends (a held clip
	 * is dropped, its rounds never left the reserve) and a row that loads on insert chambers from an
	 * uncounted clip now, so the suspend returns only a clip still uncounted (a rack row's, in with
	 * the chamber empty) to the reserve, which the parked shape keeps and its next drive takes out
	 * again: no round counted twice. A rack let go here puts the slide back first, as the suspend
	 * would (it no longer sees the rack once settled) */
	vrReloadSync(h, hand, olddef, hand->loadedammo[oldammo]);

	if (sGun[h].state == VRR_RACKING && !sGun[h].racked && vrReloadRackIsSlide(olddef)) {
		hand->slidetrans = sSlideGrab[h];
	}

	vrReloadSettle(&sGun[h], olddef);
	hand->loadedammo[oldammo] = vrrEngineLoaded(&sGun[h]);
	old = sGun[h];

	for (i = 0; i < VRR_MAGS; i++) {
		keep[i] = sPending[h][i];
		keepweapon[i] = sPendingWeapon[h][i];
	}

	vrReloadSuspend(h, 0);

	for (i = 0; i < VRR_MAGS; i++) {
		sPending[h][i] = keep[i];
		sPendingWeapon[h][i] = keepweapon[i];
	}

	sPending[h][oldammo] = old;
	sPendingWeapon[h][oldammo] = weaponnum;
	sEject[h] = eject;
	sEjectSince[h] = -1;

	/* What the render captured was the old magazine's (its clip, its held clip, its hand): taken again
	 * from the next frame, as for a gun side flip (vrReloadSwapHands) */
	sParts[h].weaponnum = -1;
	sHeld[h].weaponnum = -1;
	sHeldMtx[h].weaponnum = -1;
	sClipWorld[h].weaponnum = -1;
	sHoldWrist[h].weaponnum = -1;
	sHookClip[h].weaponnum = -1;
	sSlapContact[h].weaponnum = -1;
	sSlapClipWeapon[h] = -1;
	sPlayClipFrame[h] = -1;
	sHandoff[h].left = 0.0f;

	VRR_DIAG("magazine: frame=%d hand=%d weapon=%d from=%d to=%d parked=(magin=%d chamber=%d clip=%d state=%d)",
			g_Vars.lvframenum, h, weaponnum, oldammo, newdef->ammoindex, old.magin, old.chamber, old.clip, old.state);
}

void vrReloadTick(s32 handnum)
{
	struct player *player = g_Vars.currentplayer;
	struct hand *hand;
	struct vrreloaddef *def;
	struct weaponfunc *func;
	struct vrrinput in;
	struct vrroutput out;
	f32 along;
	s32 ammoindex, ammotype, wasracking, wasstate, wasgrip, waschamber, shoteject;

	if (handnum < 0 || handnum >= 2 || player == NULL || g_Vars.currentplayernum != 0) {
		return;
	}

	def = vrReloadOwnedDef(handnum);

	/* Once per tick, for the driven gun: sMarkPrev is the one controller's edge */
	if (VrManualReloading && handnum == HAND_RIGHT) {
		/* get_button_state flips the trigger's index by the gun side and the stick click's by the
		 * pref: these indices both land on the physical left controller */
		s32 mark = get_button_state(VrLeftHandedMode ? 1 : 0, "trigger")
			&& get_button_state(VrLeftHandedPref ? 1 : 0, "thumbstick_click");

		if (mark && !sMarkPrev) {
			vrReloadBuzz(VrLeftHandedMode ? 1 : 0, VRR_BUZZ_ACTION_AMP, VRR_BUZZ_ACTION_SECS);
			vrHolsterLogMark();

			if (def != NULL) {
				sMarkPending[handnum] = 1;
			}
		}

		sMarkPrev = mark;
	}

	if (def == NULL) {
		vrReloadSuspend(handnum, 0);
		return;
	}

	hand = &player->hands[handnum];

	/* Weapon switch: the first CHANGEGUN tick still has the old gun and saves its shape; the
	 * draw (vrReloadDrawFill) loads the new gun's saved rounds; driving resumes once it is out */
	if (hand->state == HANDSTATE_CHANGEGUN) {
		vrReloadSuspend(handnum, 1);
		return;
	}

	if (hand->state == HANDSTATE_RELOAD) {
		/* The engine reloads the selected function's ammo. Manual reload never reloads its own
		 * that way, so this is another function's, one whose ammo has no row (none today: the
		 * SuperDragon's grenades, the engine's in Task F5, have their own row since Task F9e): the
		 * core waits as it is (the clip out, a clip in the free hand, a restored shape) and goes on
		 * once the engine's reload ends. A row's own ammo (not expected) hands the gun to the
		 * engine, as before */
		struct weaponfunc *reloading = gsetGetWeaponFunction(&hand->gset);

		if (reloading == NULL || vrReloadOwnsAmmo(handnum, reloading->ammoindex)) {
			vrReloadSuspend(handnum, 0);
		} else if (sGunWeapon[handnum] == hand->gset.weaponnum) {
			/* The grip goes on being read while frozen, so one squeezed during the engine's reload
			 * is not a new press (a shoulder grab) on the first tick after it */
			sGun[handnum].prevgrip = get_button_state(VRR_CTRL_OFF, "grip") ? 1 : 0;
		}

		return;
	}

	/* The primary function, for the slide (vrReloadMoveSlide): a gun with no ammo on it is not driven */
	func = weaponGetFunction(&hand->gset, FUNC_PRIMARY);

	if (func == NULL || func->ammoindex < 0) {
		vrReloadSuspend(handnum, 0);
		return;
	}

	/* A function change to another magazine's function (the SuperDragon's rifle and grenade
	 * launcher, Task F9e): that magazine is driven from this tick, the other parked as it is */
	{
		struct vrreloaddef *selected = vrReloadSelectedDef(handnum);

		if (selected != NULL && selected != def && sGunWeapon[handnum] == hand->gset.weaponnum) {
			vrReloadSwapMagazine(handnum, hand, def, selected);
			def = selected;
		}
	}

	ammoindex = def->ammoindex;

	if (ammoindex < 0 || ammoindex >= VRR_MAGS) {
		vrReloadSuspend(handnum, 0);
		return;
	}

	ammotype = player->gunctrl.ammotypes[ammoindex];

	if (ammotype < 0) {
		vrReloadSuspend(handnum, 0);
		return;
	}

	if (hand->gset.weaponnum != sGunWeapon[handnum]) {
		s32 i;

		/* The shape restored for this magazine: by the draw, or parked by a function change */
		if (sPendingWeapon[handnum][ammoindex] == hand->gset.weaponnum) {
			struct vrrgun *g = &sGun[handnum];

			*g = sPending[handnum][ammoindex];

			/* The clip of an unracked gun is not in loadedammo: take it from the reserve now */
			if (g->magin && g->chamber == 0 && g->clip > 0) {
				if (g->clip > player->ammoheldarr[ammotype]) {
					g->clip = player->ammoheldarr[ammotype] > 0 ? player->ammoheldarr[ammotype] : 0;
				}

				player->ammoheldarr[ammotype] -= g->clip;

				/* Loaded and never fired since (the DY357's cylinder put away open): its load is what
				 * it holds now */
				sCylinder[handnum] = g->clip;
				sCapped[handnum] = 0;
			}
		} else {
			vrrInit(&sGun[handnum], hand->loadedammo[ammoindex]);
			sCylinder[handnum] = -1;
			sCapped[handnum] = 0;
		}

		/* The other magazines' shapes stay parked while this gun is in hand; another gun's go */
		for (i = 0; i < VRR_MAGS; i++) {
			if (i == ammoindex || sPendingWeapon[handnum][i] != hand->gset.weaponnum) {
				sPendingWeapon[handnum][i] = -1;
			}
		}

		sGunWeapon[handnum] = hand->gset.weaponnum;
		sNoAmmoPulse[handnum] = 0;

		if (handnum == HAND_RIGHT) {
			vrReloadHintReset();
		}
	}

	sAmmoType[handnum] = ammotype;
	sAmmoIndex[handnum] = ammoindex;
	vrReloadSync(handnum, hand, def, hand->loadedammo[ammoindex]);

	/* The left gun of a pair is tracked, never driven: the shots fold in, its shape stays as it
	 * was (saved on a switch, swapped to the right hand by a gun side flip) and its clip state
	 * gates its trigger. The free hand and the gestures belong to the right gun. */
	if (handnum == HAND_LEFT) {
		sAllowFire[handnum] = vrrAllowFire(&sGun[handnum]);
		sHandClip[handnum] = 0;

		/* What only the driven path steps or ends: a flip right after an insert must not leave
		 * this clip drawn short of seated, nor a pulse or a fall pending */
		sEject[handnum] = 0;
		sGuided[handnum] = 0;
		sProgress[handnum] = 0.0f;
		sFinish[handnum] = 0.0f;
		sFinishStart[handnum] = 0.0f;
		sHolderOpen[handnum] = def->holdermtx >= 0 && !sGun[handnum].magin ? 1.0f : 0.0f;
		sRackGrab[handnum] = 0.0f;
		sRackBack[handnum] = 0.0f;
		sPumpRide[handnum] = 0;
		sShellGrip[handnum] = 0;
		sDryPump[handnum] = 0;
		sShotSupport[handnum] = 0;
		sNoAmmoPulse[handnum] = 0;
		sDrop[handnum].active = 0;
		/* The playback and the second insert sound are the effects that go on here: a gun side
		 * flip just after an insert brings them to this hand (vrReloadSwapHands), and they play
		 * out on this gun */
		vrReloadStepPlay(handnum, def);
		vrReloadStepInsert2(handnum, def);
		vrReloadSlapReset(handnum);
		return;
	}

	/* The clip's slide to seated after a guided insert, once per frame (this can run several times
	 * per frame), over VRR_FINISH_60THS whatever the frame rate */
	if (sFinish[handnum] > 0.0f && sFinishFrame[handnum] != g_Vars.lvframenum) {
		sFinishFrame[handnum] = g_Vars.lvframenum;
		sFinish[handnum] -= sFinishStart[handnum] * g_Vars.lvupdate60freal / VRR_FINISH_60THS;

		if (sFinish[handnum] < 0.0f) {
			sFinish[handnum] = 0.0f;
		}
	}

	vrReloadTrackPlayer(player);
	vrReloadStepSlap(handnum, hand, def);
	vrReloadBuildInput(handnum, hand, def, ammoindex, ammotype, &in, &along);

	/* A row that opens after the shot (emptyejects: the Rocket Launcher's hatch): a gun left empty with
	 * its clip in ejects as B would, on every tick until the core takes it (not while untracked or
	 * paused), so it opens and waits for the next round */
	shoteject = 0;

	if (def->emptyejects && sGun[handnum].magin && sGun[handnum].chamber == 0 && sGun[handnum].clip == 0
			&& sGun[handnum].state == VRR_READY) {
		shoteject = !in.eject;
		in.eject = 1;
	}

	wasracking = sGun[handnum].state == VRR_RACKING;
	wasstate = sGun[handnum].state;
	wasgrip = sGun[handnum].prevgrip;
	waschamber = sGun[handnum].chamber;

	/* The pump is the support hand's (a row with pumpaction): a grip already held on the forend when a
	 * shot leaves the gun waiting for its pump takes the pump as a new squeeze there would (the core
	 * takes a rack only on a squeeze), so the support hand pumps without letting go of the gun */
	if (def->pumpaction && in.inrack && in.grip && !in.atshoulder && sGun[handnum].state == VRR_NEED_RACK) {
		sGun[handnum].prevgrip = 0;
	}

	vrrTick(&sGun[handnum], &in, &out);
	vrReloadStepHint(handnum, def, &in, &out, wasstate, wasgrip);

	if (def->shellbyshell) {
		if (sGun[handnum].state == VRR_HOLDING_CLIP) {
			sShellGrip[handnum] = 1;
		} else if (!in.grip) {
			sShellGrip[handnum] = 0;
		}
	}

	/* The PD-VRR "pump:" lines (Task J6c): a squeeze while the gun waits for its pump that does not
	 * take it, and each stroke's end with its longest pull */
	if (def->pumpaction && in.tracked && !in.paused) {
		if (wasstate == VRR_NEED_RACK && sGun[handnum].state == VRR_NEED_RACK && in.grip && !wasgrip) {
			VRR_DIAG("pump: missed frame=%d inrack=%d atshoulder=%d support=%d shellgrip=%d along=%.1f",
					g_Vars.lvframenum, in.inrack, in.atshoulder, vrTwoHandGripHeld() ? 1 : 0,
					sShellGrip[handnum], along);
		}

		if (sGun[handnum].state == VRR_RACKING && in.rackpull > sPumpMaxPull[handnum]) {
			sPumpMaxPull[handnum] = in.rackpull;
		}

		if (wasracking && sGun[handnum].state != VRR_RACKING) {
			VRR_DIAG("pump: end frame=%d racked=%d maxpull=%.1f travel=%.1f grip=%d", g_Vars.lvframenum,
					(out.fx & VRR_FX_RACK) ? 1 : 0, sPumpMaxPull[handnum], def->racktravel, in.grip);
		}
	}

	/* A pump with nothing to chamber clicks (VRR_DRY_PUMP_FRAC): the grip taken on the forend of a dry
	 * gun starts a stroke, held through any later state (a shell taken ends it) */
	if (def->pumpaction && in.tracked && !in.paused) {
		struct vrrgun *g = &sGun[handnum];
		s32 dry = g->state == VRR_READY && g->magin && g->chamber == 0 && g->clip == 0;

		if (!in.grip || !dry) {
			sDryPump[handnum] = 0;
		} else if (sDryPump[handnum] == 0) {
			if (sPumpZone[handnum]) {
				sDryPump[handnum] = 1;
				sDryPumpFwd[handnum] = along;
			}
		} else {
			if (along < sDryPumpFwd[handnum]) {
				sDryPumpFwd[handnum] = along;
			}

			if (sDryPump[handnum] == 1 && along - sDryPumpFwd[handnum] >= def->racktravel * VRR_DRY_PUMP_FRAC) {
				sDryPump[handnum] = 2;
				sndStart(var80095200, (s16)SFX_FIREEMPTY, 0, -1, -1, -1, -1, -1);
				VRR_DIAG("pump: dry click frame=%d pull=%.1f travel=%.1f", g_Vars.lvframenum,
						along - sDryPumpFwd[handnum], def->racktravel);
			} else if (sDryPump[handnum] == 2
					&& along - sDryPumpFwd[handnum] <= def->racktravel * VRR_DRY_PUMP_REARM_FRAC) {
				sDryPump[handnum] = 1;
			}
		}
	}

	/* The core took the slap (a tick it ignores, untracked or paused, leaves it for the next) */
	if (in.tracked && !in.paused) {
		sSlap[handnum].pending = 0;
	}

	/* The core inserts only a guided clip: the drawn clip goes on from where the hand left it */
	if (out.fx & VRR_FX_INSERT) {
		/* The cylinder's load, for the casings B throws later (sCylinder) */
		if (def->ejectcasings) {
			sCylinder[handnum] = sGun[handnum].chamber + sGun[handnum].clip;
			sCapped[handnum] = 0;
		}

		sFinish[handnum] = sGuided[handnum] ? def->clipinsertdepth - sProgress[handnum] : 0.0f;
		sFinishStart[handnum] = sFinish[handnum];
		sFinishFrame[handnum] = g_Vars.lvframenum;

		/* A row with a playback (the Phoenix's orb, the Crossbow's bolt sliding in) starts it with
		 * the insert sound: its first frame is drawn this frame, and it steps from the next. */
		if (vrReloadHasPlay(def) && (def == NULL || def->weaponnum != WEAPON_CROSSBOW)) {
			vrReloadStartPlay(handnum, hand, def, 0);
		}
	}

	/* The shot opened the gun (emptyejects) with the support grip held: it stays the support until let
	 * go (sShotSupport). Ends with the grip, a load, or a gun side change (vrReloadSideChanged) */
	if ((out.fx & VRR_FX_EJECT) && shoteject && in.grip && vrTwoHandGripHeld()) {
		sShotSupport[handnum] = 1;
	} else if (!in.grip || sGun[handnum].magin) {
		sShotSupport[handnum] = 0;
	}

	/* A row with an eject playback (the Laptop Gun's pump) starts it with the eject sound; the clip
	 * falls at once, below, while it plays, or at its end when it carries the clip out (ejectdrop). A
	 * gun held open waiting for its close (the DY357's cylinder, loaded: B takes the rounds back) is open
	 * already: it stays so */
	if ((out.fx & VRR_FX_EJECT) && vrReloadHasEjectPlay(def) && !(vrReloadHasClose(def) && wasstate == VRR_NEED_RACK)) {
		vrReloadStartPlay(handnum, hand, def, 1);

		/* The chambers loaded last that the eject did not take a live round back from hold a fired
		 * casing: thrown at the swing's end (an empty chamber of a short load throws none) */
		if (def->ejectcasings) {
			s32 loaded = sCylinder[handnum] >= 0 ? sCylinder[handnum] : in.clipsize - sCapped[handnum];
			s32 spent = loaded - out.reservedelta;

			sCasingsDue[handnum] = spent < 0 ? 0 : spent > 7 ? 7 : spent;
		}
	}

	/* A row closed by a flick or a push (the DY357's cylinder): the core's rack is the close, which plays */
	if ((out.fx & VRR_FX_RACK) && vrReloadHasClose(def)) {
		vrReloadStartPlay(handnum, hand, def, VRR_PLAY_CLOSE);
	}

	if (sGun[handnum].state != VRR_HOLDING_CLIP) {
		sGuided[handnum] = 0;
	}

	if (!sGun[handnum].magin) {
		sFinish[handnum] = 0.0f;
	}

	/* A row with no clip part has nothing to drop: the gun just waits for the next one */
	if ((out.fx & VRR_FX_EJECT) && vrReloadHasClip(def) && !vrReloadEjectDrops(def)) {
		vrReloadStartDrop(handnum, hand, def);
	}

	/* The gun's clip part and matrix draw one clip: a new clip guided into the well (or inserted)
	 * takes them over, so the falling one goes. A held part falling after its playback (playdrop: the
	 * Cyclone's bar, whose fall starts with the bar loaded) goes when a new playback poses the part.
	 * So does a fall the render stopped drawing. */
	if (sDrop[handnum].active && ((vrReloadHasClip(def) ? sGuided[handnum] || sGun[handnum].magin
					: sPlayFrame[handnum] > 0.0f && !sPlayEject[handnum])
				|| player->isdead || g_Vars.lvframenum - sDrop[handnum].drawframe > VRR_PARTS_MAX_AGE)) {
		sDrop[handnum].active = 0;
	}

	vrReloadStepHolder(handnum, def);
	vrReloadStepRack(handnum, def, wasracking, &in, along);
	vrReloadStepPlay(handnum, def);
	vrReloadStepInsert2(handnum, def);

	/* The core just started racking: the pull is measured from here (it was 0 for this tick) */
	if (!wasracking && sGun[handnum].state == VRR_RACKING) {
		sRackGrab[handnum] = along;

		if (def->pumpaction) {
			sPumpFwd[handnum] = along;
			sPumpMaxPull[handnum] = 0.0f;
			VRR_DIAG("pump: grab frame=%d how=%s along=%.1f", g_Vars.lvframenum,
					wasgrip && in.grip ? "held" : vrTwoHandGripHeld() ? "support" : "press", along);
		}
	}

	vrReloadApplyOutput(handnum, hand, def, ammoindex, ammotype, &out);
	vrReloadMoveSlide(handnum, hand, def, func, wasracking, &in);

	if (wasracking && sGun[handnum].state != VRR_RACKING && def != NULL && def->rackframefrom > 0.0f) {
		sRackEase[handnum] = VRR_RACK_EASE_60THS;
	} else if (sRackEase[handnum] > 0.0f) {
		sRackEase[handnum] -= 1.0f;
	}

	/* The pump still back after its rack (vrReloadStepRack): no shot until it is forward */
	if (sPumpRide[handnum]) {
		sAllowFire[handnum] = 0;
	}

	/* A gun held open from its eject to its load (ejecthold: the Rocket Launcher's hatch) fires once the
	 * insert's playback has shut it, and one held open to its close (the DY357's cylinder) once the
	 * close's has */
	if (vrReloadHoldsEject(def) && sPlayFrame[handnum] > 0.0f && (!sPlayEject[handnum] || sPlayClose[handnum])) {
		sAllowFire[handnum] = 0;
	}
}

s32 vrReloadDrawFill(s32 handnum)
{
	struct player *player = g_Vars.currentplayer;
	struct hand *hand;
	struct vrrsaved *slots[VRR_MAGS];
	struct vrreloaddef *driven;
	s32 filled = 0;
	s32 found = 0;
	s32 i;

	if (handnum < 0 || handnum >= 2 || player == NULL || g_Vars.currentplayernum != 0) {
		return 0;
	}

	hand = &player->hands[handnum];

	/* Each magazine's slot is used up either way. Not owned now (manual reload off, the cheat): the
	 * engine fills the gun as vanilla does; the slots held no rounds, so nothing is lost */
	for (i = 0; i < VRR_MAGS; i++) {
		slots[i] = vrReloadFindSaved(hand->gset.weaponnum, handnum, i);

		if (slots[i] != NULL) {
			slots[i]->weaponnum = -1;
			found = 1;
		}
	}

	driven = vrReloadOwnedDef(handnum);

	if (!found || driven == NULL) {
		return 0;
	}

	for (i = 0; i < VRR_MAGS; i++) {
		struct vrreloaddef *def = vrReloadGetDef(hand->gset.weaponnum, i);
		struct vrrgun g;
		s32 ammotype, reserve, before;

		if (slots[i] == NULL || def == NULL) {
			continue;
		}

		ammotype = player->gunctrl.ammotypes[i];

		if (ammotype < 0) {
			continue;
		}

		g = slots[i]->gun;

		/* Settled before the cap, so the cap counts the round a gun that loads on insert chambers
		 * from its clip here (uncapped, a flagged gun saved with an empty chamber would load its whole
		 * clip and drive the reserve negative) */
		vrReloadSettle(&g, def);

		/* Another gun on the same ammo may have used the rounds meanwhile: load what is left */
		reserve = player->ammoheldarr[ammotype];
		before = vrrEngineLoaded(&g);

		if (g.chamber) {
			if (reserve <= 0) {
				g.chamber = 0;
				g.clip = 0;
			} else if (g.clip > reserve - 1) {
				g.clip = reserve - 1;
			}
		}

		/* Again, only to recompute the state: a cap that emptied the chamber of a gun without the
		 * flag leaves it needing a rack, not ready */
		vrReloadSettle(&g, def);
		hand->loadedammo[i] = vrrEngineLoaded(&g);
		player->ammoheldarr[ammotype] = reserve - hand->loadedammo[i];

		/* A gun restored shut: its load is not known (the clip size), less the live rounds the cap
		 * took, which were never fired (a gun drawn empty that never fired throws no casing) */
		if (def->ejectcasings) {
			sCylinder[handnum] = -1;
			sCapped[handnum] = before - hand->loadedammo[i];
		}

		/* The selected function's magazine is driven from the first tick, the other stays parked
		 * (vrReloadTick takes the shape for whichever it drives) */
		sPending[handnum][i] = g;
		sPendingWeapon[handnum][i] = hand->gset.weaponnum;
		filled |= 1 << i;
	}

	return filled;
}

/* The shape restored for the magazine the hand's gun is to drive (the selected function's: by the
 * draw, vrReloadDrawFill, or parked by a function change), while no tick has applied it, else NULL.
 * The other magazine's parked shape is vrReloadParkedDef's */
static struct vrrgun *vrReloadPendingFor(s32 handnum)
{
	struct vrreloaddef *def = vrReloadOwnedDef(handnum);
	s32 weaponnum = g_Vars.currentplayer->hands[handnum].gset.weaponnum;

	if (def == NULL || def->ammoindex < 0 || def->ammoindex >= VRR_MAGS || sGunWeapon[handnum] == weaponnum
			|| sPendingWeapon[handnum][def->ammoindex] != weaponnum) {
		return NULL;
	}

	return &sPending[handnum][def->ammoindex];
}

bool vrReloadAllowFire(s32 handnum)
{
	struct weaponfunc *func;
	struct vrrgun *pending;

	/* Another function's ammo with no row is the engine's (none today): it fires whatever the row's
	 * clip is doing */
	if (!vrReloadOwnsSelectedFunc(handnum)) {
		return true;
	}

	/* A function with no ammo (the Falcon 2's pistol whip, the Dragon's self-destruct throw, the
	 * Sniper Rifle's crouch) works whatever the clip and chamber are, as in the original game */
	func = currentPlayerGetWeaponFunction(handnum);

	if (func != NULL && func->ammoindex < 0) {
		return true;
	}

	/* The function was just changed to the other magazine's, which the next tick drives
	 * (vrReloadSwapMagazine): its parked shape decides, else the engine's count */
	if (func != NULL && func->ammoindex < VRR_MAGS && vrReloadSelectedDef(handnum) != vrReloadOwnedDef(handnum)) {
		s32 weaponnum = g_Vars.currentplayer->hands[handnum].gset.weaponnum;

		return sPendingWeapon[handnum][func->ammoindex] != weaponnum
			|| vrrAllowFire(&sPending[handnum][func->ammoindex]);
	}

	pending = vrReloadPendingFor(handnum);

	if (pending != NULL) {
		return vrrAllowFire(pending);
	}

	return sAllowFire[handnum];
}

s32 vrReloadHudLoaded(s32 handnum, s32 ammoindex, s32 engine)
{
	struct vrreloaddef *def = vrReloadOwnedDef(handnum);
	struct vrrgun *g;

	if (def == NULL || !def->pumpaction || def->ammoindex != ammoindex) {
		return engine;
	}

	g = vrReloadPendingFor(handnum);

	if (g == NULL) {
		if (sGunWeapon[handnum] < 0) {
			return engine;
		}

		g = &sGun[handnum];
	}

	/* The tube's rounds are out of loadedammo while the chamber is empty (vrrEngineLoaded), as an
	 * unracked clip's; a pump gun's tube is in the gun all along */
	return g->magin ? g->chamber + g->clip : engine;
}

bool vrReloadClipInGun(s32 handnum)
{
	struct vrrgun *pending;

	if (!vrReloadOwnsGun(handnum)) {
		return true;
	}

	pending = vrReloadPendingFor(handnum);

	if (pending != NULL) {
		return pending->magin;
	}

	if (sGunWeapon[handnum] < 0) {
		return true;
	}

	return sGun[handnum].magin;
}

bool vrReloadHandHoldsClip(s32 handnum)
{
	if (!vrReloadOwnsGun(handnum)) {
		return false;
	}

	return sHandClip[handnum] != 0;
}

bool vrReloadHeldClipDrawn(s32 handnum)
{
	/* A guided clip is drawn in the gun, so the hand is drawn empty */
	return vrReloadHandHoldsClip(handnum) && !sGuided[handnum];
}

/* The guided or finishing slide only applies to the core's own state: not while a restored shape
 * waits for its first tick, nor before the core drives the gun */
static s32 vrReloadSliding(s32 handnum)
{
	return vrReloadOwnsGun(handnum) && vrReloadPendingFor(handnum) == NULL && sGunWeapon[handnum] >= 0;
}

bool vrReloadSlideHeld(s32 handnum)
{
	/* A rack on a rackmtx holds no slide: bgunUpdateSlide goes on as without manual reload */
	return vrReloadSliding(handnum) && sGun[handnum].state == VRR_RACKING
		&& vrReloadRackIsSlide(vrReloadOwnedDef(handnum));
}

bool vrReloadHandRacking(s32 handnum)
{
	/* Any rack: on the slide part or on a rackmtx (the RC-P120's knob, the CMP150's body). The core
	 * leaves VRR_RACKING on the tick the rack completes, with the grip still held: the caller latches
	 * that grip until it lets go */
	return vrReloadSliding(handnum) && sGun[handnum].state == VRR_RACKING;
}

bool vrReloadWaitsForRack(s32 handnum)
{
	/* A clip with rounds: a gun fired dry (clip in, chamber and clip empty) waits for nothing */
	return vrReloadSliding(handnum) && sGun[handnum].state == VRR_NEED_RACK && sGun[handnum].clip > 0;
}

bool vrReloadMidReload(s32 handnum)
{
	struct vrrgun *g = &sGun[handnum];
	struct vrreloaddef *def;

	if (!vrReloadSliding(handnum)) {
		return false;
	}

	def = vrReloadOwnedDef(handnum);

	/* The shot opened the gun (emptyejects) under a held support grip: that grip stays the support
	 * until it is let go, so the two-hand aim does not drop at the shot (sShotSupport) */
	if (sShotSupport[handnum]) {
		return false;
	}

	/* B pressed and not yet taken by the tick: it ejects a clip that is in and not mid-rack. A gun
	 * loaded shell by shell ignores B */
	if (sEject[handnum] && g->magin && g->state != VRR_RACKING && !def->shellbyshell) {
		return true;
	}

	/* A gun pumped after each shot (the Shotgun) is pumped by the support hand, which keeps the two-hand
	 * aim (vrReloadPumpHand): a pump due or under way is not mid-reload. A shell held is the free
	 * hand's (vrReloadFreeHandBusy), and its tube never leaves */
	if (def->pumpaction) {
		return !g->magin || vrReloadSlapDrawn(handnum, NULL);
	}

	/* The clip out (in the free hand, guided into the well or not taken yet: the core's clip is out
	 * until the insert), a rack under way or due with rounds in the clip, or the free hand drawn
	 * slapping. A gun fired dry with its clip in (an empty clip: READY, or NEED_RACK with nothing to
	 * chamber) is not: it waits for B */
	return !g->magin || g->state == VRR_RACKING || vrReloadWaitsForRack(handnum)
		|| vrReloadSlapDrawn(handnum, NULL);
}

bool vrReloadPumpHand(s32 handnum)
{
	struct vrreloaddef *def = vrReloadOwnedDef(handnum);

	return def != NULL && def->pumpaction && vrReloadHandRacking(handnum);
}

bool vrReloadNeedsGripRack(s32 handnum)
{
	struct vrreloaddef *def = vrReloadOwnedDef(handnum);

	return def != NULL && !def->rackslap && !def->loadsoninsert && vrReloadWaitsForRack(handnum);
}

bool vrReloadFreeHandInRack(void)
{
	struct player *player = g_Vars.currentplayer;
	struct vrreloaddef *def;
	struct vrrpartsview v;
	f32 grab[3];

	/* As vrReloadBuildInput reads the rack zone: this frame's gun side in the poses, the free hand
	 * free (empty, or its gun of a pair parked), the rack's capture fresh */
	if (player == NULL || VrLeftHandedMode != VrCtrlPoseMode || !vrReloadNeedsGripRack(HAND_RIGHT)
			|| (player->hands[HAND_LEFT].inuse && !vrHandParked(HAND_LEFT))) {
		return false;
	}

	def = vrReloadOwnedDef(HAND_RIGHT);

	if (!vrReloadPartsNow(HAND_RIGHT, &player->hands[HAND_RIGHT], 1, &v) || !v.hasrack) {
		return false;
	}

	vrReloadHandGrab(grab);

	return vrReloadRackDist(def, &v, grab) <= def->rackrad;
}

bool vrReloadFreeHandBusy(s32 handnum)
{
	return vrReloadHandHoldsClip(handnum) || vrReloadHandRacking(handnum) || vrReloadSlapDrawn(handnum, NULL);
}

bool vrReloadClipShownInGun(s32 handnum)
{
	if (vrReloadSliding(handnum) && sGuided[handnum]) {
		return true;
	}

	/* An eject playback carrying the old clip out (ejectdrop) draws it until it falls */
	if (vrReloadSliding(handnum) && sPlayFrame[handnum] > 0.0f && sPlayEject[handnum]
			&& vrReloadEjectDrops(vrReloadOwnedDef(handnum))) {
		return true;
	}

	return vrReloadClipInGun(handnum);
}

/* cm the drawn clip still has to travel to seated: while guided, and for VRR_FINISH_60THS after
 * the insert. 0 when seated or not owned. */
static f32 vrReloadClipRenderOffset(s32 handnum)
{
	struct vrreloaddef *def;

	if (!vrReloadSliding(handnum)) {
		return 0.0f;
	}

	if (sGuided[handnum]) {
		def = vrReloadOwnedDef(handnum);
		return def->clipinsertdepth - sProgress[handnum];
	}

	return sGun[handnum].magin ? sFinish[handnum] : 0.0f;
}

void vrReloadPlaceClip(s32 handnum, struct modeldef *modeldef, Mtxf *matrices)
{
	struct vrreloaddef *def = vrReloadOwnedDef(handnum);
	f32 back = vrReloadClipRenderOffset(handnum);
	f32 v[3];
	Mtxf *m;
	s32 j;

	if (def == NULL || modeldef == NULL || matrices == NULL || !vrReloadHasClip(def)
			|| def->clipmtx < 0 || def->clipmtx >= modeldef->nummatrices) {
		return;
	}

	m = &matrices[def->clipmtx];

	/* A row whose playbacks pose the clip (the Laptop Gun), on the body as drawn, so it carries the
	 * body's pose, scales and mirror. The clip index is trusted only while the clip part is in the
	 * model, as the capture's. The eject's carries the old clip out while no new one is guided or in;
	 * the slap's rocks the inserted one into the seat. Else, until the slap's playback, an inserted or
	 * guided clip is drawn as the animation has it at the hold frame (hooked in at its front), then
	 * moved back along that pose's insert axis while guided below */
	if (def->bodymtx >= 0 && def->bodymtx < modeldef->nummatrices && def->bodymtx != def->clipmtx
			&& vrReloadSliding(handnum) && modelGetPart(modeldef, def->clippart) != NULL) {
		struct vrrslapclip *hc = &sHookClip[handnum];
		s32 weaponnum = g_Vars.currentplayer->hands[handnum].gset.weaponnum;
		Mtxf ident;

		mtx4LoadIdentity(&ident);

		if (sPlayFrame[handnum] > 0.0f && sPlayClipFrame[handnum] == g_Vars.lvframenum
				&& vrReloadPlaysClip(def, sPlayEject[handnum])
				&& (sPlayEject[handnum] ? !sGuided[handnum] && !sGun[handnum].magin : sGun[handnum].magin)) {
			vrReloadRebaseMtx(m, &sPlayClip[handnum], &ident, &matrices[def->bodymtx]);
		} else if (vrReloadHasSlapPlay(def) && hc->weaponnum == weaponnum && hc->valid
				&& (sGuided[handnum] || (sGun[handnum].magin && sGun[handnum].state == VRR_NEED_RACK))) {
			vrReloadRebaseMtx(m, &hc->c, &ident, &matrices[def->bodymtx]);
		}
	}

	if (back > 0.0f) {
		/* Back along the clip's own insert axis, in model units: the clip matrix turns it into camera
		 * space with the model scale, bg_scale and the left-handed mirror, as the engine's slide offset */
		for (j = 0; j < 3; j++) {
			v[j] = -def->clipaxis[j] * back * VRR_MODEL_UNITS_PER_CM;
		}

		for (j = 0; j < 3; j++) {
			m->m[3][j] += v[0] * m->m[0][j] + v[1] * m->m[1][j] + v[2] * m->m[2][j];
		}
	}

	if (sGuided[handnum]) {
		s32 ri, rj;
		for (ri = 0; ri < 4; ri++) {
			for (rj = 0; rj < 4; rj++) {
				sGuidedClip[handnum].m[ri][rj] = m->m[ri][rj];
			}
		}
		sGuidedClipFrame[handnum] = g_Vars.lvframenum;
		sGuidedClipWeapon[handnum] = g_Vars.currentplayer->hands[handnum].gset.weaponnum;
	}
}

void vrReloadMoveRack(s32 handnum, struct modeldef *modeldef, Mtxf *matrices)
{
	struct vrreloaddef *def = vrReloadOwnedDef(handnum);
	f32 back;
	f32 v[3];
	s32 i, j;

	/* The index is trusted only while the row's clip part (or, with none, its held part: the
	 * Shotgun's shell) is in the model, as the capture's */
	if (def == NULL || !def->rackmove || modeldef == NULL || matrices == NULL || !vrReloadSliding(handnum)
			|| def->rackmtx < 0 || def->rackmtx >= modeldef->nummatrices
			|| modelGetPart(modeldef, vrReloadHasClip(def) ? def->clippart : def->heldpart) == NULL) {
		return;
	}

	back = sRackBack[handnum] * VRR_MODEL_UNITS_PER_CM;

	if (back <= 0.0f) {
		return;
	}

	/* Back along the rack frame's own -z, in model units: the matrix's z row carries the model
	 * scale, bg_scale and the left-handed mirror, as the engine's slide offset */
	for (j = 0; j < 3; j++) {
		v[j] = back * matrices[def->rackmtx].m[2][j];
	}

	matrices[def->rackmtx].m[3][0] -= v[0];
	matrices[def->rackmtx].m[3][1] -= v[1];
	matrices[def->rackmtx].m[3][2] -= v[2];

	/* A gun pumped by the support hand (the Shotgun): the gun's own left arm and hand (matrices 17 to
	 * 32), which it draws on the forend while the free hand grips as its support (and hides
	 * otherwise), go back with the forend, so the hand pumps it. The pose holds them on the forend: the
	 * fire animation that would move them is not played for this row (bgunTickIncAttackingShoot) */
	if (def->pumpaction && modeldef->nummatrices > VRR_LEFT_HAND_MTX_LAST) {
		for (i = VRR_LEFT_HAND_MTX_FIRST; i <= VRR_LEFT_HAND_MTX_LAST; i++) {
			if (i == def->rackmtx) {
				continue;
			}

			for (j = 0; j < 3; j++) {
				matrices[i].m[3][j] -= v[j];
			}
		}
	}

	/* The linked part (rackfollowmtx: the Devastator's front sleeve), back along its own -z by its
	 * share of the pull */
	if (def->rackfollowmtx >= 0 && def->rackfollowmtx < modeldef->nummatrices
			&& def->rackfollowmtx != def->rackmtx && def->rackfollowscale > 0.0f) {
		for (j = 0; j < 3; j++) {
			matrices[def->rackfollowmtx].m[3][j] -= back * def->rackfollowscale * matrices[def->rackfollowmtx].m[2][j];
		}
	}
}

/* How far open to draw the holder: sHolderOpen, or from the clip state alone (open with the clip
 * out, else shut) while a restored shape waits for its first tick or the core is not driving the
 * gun. 0 (shut) leaves the holder as the gun's animation poses it. */
static f32 vrReloadHolderOpen(s32 handnum)
{
	if (!vrReloadSliding(handnum)) {
		return vrReloadClipInGun(handnum) ? 0.0f : 1.0f;
	}

	return sHolderOpen[handnum];
}

/* The position node that sets matrix mtx in this model, else NULL */
static struct modelnode *vrReloadMtxNode(struct modeldef *modeldef, s32 mtx)
{
	struct modelnode *node = modeldef->rootnode;

	while (node != NULL) {
		u32 type = node->type & 0xff;

		if ((type == MODELNODETYPE_POSITION || type == MODELNODETYPE_POSITIONHELD)
				&& modelFindNodeMtxIndex(node, 0) == mtx) {
			return node;
		}

		if (node->child != NULL) {
			node = node->child;
		} else {
			while (node != NULL && node->next == NULL) {
				node = node->parent;
			}

			if (node != NULL) {
				node = node->next;
			}
		}
	}

	return NULL;
}

/* Moves matrix x with the holder as its frame turns from old (rows of squared length s2) to the
 * rows turned: each of x's rows, and its origin from the holder's joint, is taken into the old
 * frame and back out of the turned one (the joint stays put). Column 3 is left alone: it carries
 * the part-hide signal (vrHideGunParts). */
static void vrReloadTurnMtx(Mtxf *x, Mtxf *old, f32 turned[3][3], f32 s2)
{
	f32 v[3], l[3];
	s32 i, j, k;

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 3; j++) {
			v[j] = x->m[i][j] - (i == 3 ? old->m[3][j] : 0.0f);
		}

		for (k = 0; k < 3; k++) {
			l[k] = (v[0] * old->m[k][0] + v[1] * old->m[k][1] + v[2] * old->m[k][2]) / s2;
		}

		for (j = 0; j < 3; j++) {
			x->m[i][j] = (i == 3 ? old->m[3][j] : 0.0f)
				+ l[0] * turned[0][j] + l[1] * turned[1][j] + l[2] * turned[2][j];
		}
	}
}

void vrReloadSeatClip(s32 handnum, struct modeldef *modeldef, Mtxf *matrices)
{
	struct vrreloaddef *def;
	Mtxf *b, *m;
	s32 i, j, k;

	if (handnum < 0 || handnum >= 2 || modeldef == NULL || matrices == NULL) {
		return;
	}

	def = vrReloadOwnedDef(handnum);

	/* The indices are trusted only while the row's clip part is in the model, as the capture's */
	if (def == NULL || !def->clipseat || !vrReloadHasClip(def) || def->bodymtx < 0
			|| def->bodymtx >= modeldef->nummatrices || def->clipmtx < 0 || def->clipmtx >= modeldef->nummatrices
			|| def->clipmtx == def->bodymtx || modelGetPart(modeldef, def->clippart) == NULL) {
		return;
	}

	/* The seat on the body as drawn (row vectors: the clip's axes and joint through the body's
	 * matrix, cm to model units), so it carries the body's pose, scales and left-handed mirror.
	 * Column 3 is left alone: it carries the part-hide signal (vrHideGunParts) */
	b = &matrices[def->bodymtx];
	m = &matrices[def->clipmtx];

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 3; j++) {
			f32 v = i == 3 ? b->m[3][j] : 0.0f;

			for (k = 0; k < 3; k++) {
				v += (i == 3 ? def->seatpos[k] * VRR_MODEL_UNITS_PER_CM : def->seatrot[i][k]) * b->m[k][j];
			}

			m->m[i][j] = v;
		}
	}
}

void vrReloadPoseHolder(s32 handnum, struct modeldef *modeldef, Mtxf *matrices)
{
	struct vrreloaddef *def;
	struct modelnode *holder;
	struct modelnode *node;
	Mtxf old;
	Mtxf *hm, *pm;
	f32 turned[3][3];
	f32 open, t, s2, ang, c, s;
	s32 parentmtx;
	s32 j;

	if (handnum < 0 || handnum >= 2 || modeldef == NULL || matrices == NULL) {
		return;
	}

	def = vrReloadOwnedDef(handnum);

	if (def == NULL || def->holdermtx < 0 || def->holdermtx >= modeldef->nummatrices) {
		return;
	}

	open = vrReloadHolderOpen(handnum);

	if (open <= 0.0f) {
		return;
	}

	holder = vrReloadMtxNode(modeldef, def->holdermtx);
	parentmtx = holder != NULL ? modelFindNodeMtxIndex(holder->parent, 0) : -1;

	if (parentmtx < 0 || parentmtx >= modeldef->nummatrices) {
		return;
	}

	hm = &matrices[def->holdermtx];
	pm = &matrices[parentmtx];
	/* The holder's own squared row length, which vrReloadTurnMtx divides by (the parent's is the same
	 * for a pure turn) */
	s2 = hm->m[0][0] * hm->m[0][0] + hm->m[0][1] * hm->m[0][1] + hm->m[0][2] * hm->m[0][2];

	if (s2 < 1e-12f) {
		return;
	}

	/* The animation's turn of the holder about its own x, from its parent: its y row is cos times
	 * the parent's y row plus sin times the parent's z row. Open is no turn; while it shuts the
	 * turn grows as the time fraction to the power 1.5, the reload animation's curve (frames 57 to
	 * 72: 8, 26, 49 and 75 percent at a fifth, two, three and four fifths of the time, against
	 * 9, 25, 46 and 72; its 4 degree bounce past shut in frames 73 to 76 is left out) */
	ang = atan2f(hm->m[1][0] * pm->m[2][0] + hm->m[1][1] * pm->m[2][1] + hm->m[1][2] * pm->m[2][2],
			hm->m[1][0] * pm->m[1][0] + hm->m[1][1] * pm->m[1][1] + hm->m[1][2] * pm->m[1][2]);
	t = open < 1.0f ? 1.0f - open : 0.0f;
	ang *= t * sqrtf(t);
	c = cosf(ang);
	s = sinf(ang);

	for (j = 0; j < 3; j++) {
		turned[0][j] = hm->m[0][j];
		turned[1][j] = c * pm->m[1][j] + s * pm->m[2][j];
		turned[2][j] = -s * pm->m[1][j] + c * pm->m[2][j];
	}

	old = *hm;

	/* The holder and every position node under it (the clip's joint among them) */
	node = holder;

	while (node != NULL) {
		u32 type = node->type & 0xff;

		if (type == MODELNODETYPE_POSITION || type == MODELNODETYPE_POSITIONHELD) {
			s32 index = modelFindNodeMtxIndex(node, 0);

			if (index >= 0 && index < modeldef->nummatrices) {
				vrReloadTurnMtx(&matrices[index], &old, turned, s2);
			}
		}

		if (node->child != NULL) {
			node = node->child;
		} else {
			while (node != NULL && node != holder && node->next == NULL) {
				node = node->parent;
			}

			node = node == NULL || node == holder ? NULL : node->next;
		}
	}
}

/* Matrix x, posed against the frame from, put on the frame to as it sat on from: x * from^-1 * to
 * (row vectors), each of x's rows, and its origin from from's joint, taken into from's frame and
 * out of to's. from is a turn times one scale (rows of squared length s2, 1 for a pose with an
 * identity root), so its inverse is its transpose over s2. Column 3 of out is left alone: it
 * carries the part-hide signal (vrHideGunParts). */
static void vrReloadRebaseMtx(Mtxf *out, const Mtxf *x, const Mtxf *from, const Mtxf *to)
{
	f32 v[3], l[3];
	f32 s2 = from->m[0][0] * from->m[0][0] + from->m[0][1] * from->m[0][1] + from->m[0][2] * from->m[0][2];
	s32 i, j, k;

	if (s2 < 1e-12f) {
		return;
	}

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 3; j++) {
			v[j] = x->m[i][j] - (i == 3 ? from->m[3][j] : 0.0f);
		}

		for (k = 0; k < 3; k++) {
			l[k] = (v[0] * from->m[k][0] + v[1] * from->m[k][1] + v[2] * from->m[k][2]) / s2;
		}

		for (j = 0; j < 3; j++) {
			out->m[i][j] = (i == 3 ? to->m[3][j] : 0.0f)
				+ l[0] * to->m[0][j] + l[1] * to->m[1][j] + l[2] * to->m[2][j];
		}
	}
}

bool vrReloadSlapDrawn(s32 handnum, f32 *frame)
{
	struct vrreloaddef *def;

	if (handnum < 0 || handnum >= 2 || sSlap[handnum].frame <= 0.0f) {
		return false;
	}

	def = vrReloadOwnedDef(handnum);

	if (def == NULL || !def->rackslap || sGunWeapon[handnum] != g_Vars.currentplayer->hands[handnum].gset.weaponnum) {
		return false;
	}

	if (frame != NULL) {
		*frame = sSlap[handnum].frame;
	}

	return true;
}

/* A turn's rows (unit, any handedness) and its quaternion: a mirrored turn (negative determinant)
 * has its x row negated first, so the quaternion is of a proper turn; returns whether it was
 * mirrored */
static s32 vrReloadRowsQuat(f32 rows[3][3], f32 q[4])
{
	Mtxf m;
	s32 i, j;
	f32 det = rows[0][0] * (rows[1][1] * rows[2][2] - rows[1][2] * rows[2][1])
		- rows[0][1] * (rows[1][0] * rows[2][2] - rows[1][2] * rows[2][0])
		+ rows[0][2] * (rows[1][0] * rows[2][1] - rows[1][1] * rows[2][0]);
	s32 mirrored = det < 0.0f;

	memset(&m, 0, sizeof(m));

	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++) {
			m.m[i][j] = (i == 0 && mirrored) ? -rows[i][j] : rows[i][j];
		}
	}

	quaternion0f097044(&m, q);

	return mirrored;
}

/* The wrist pose a fraction t (0..1) of the way from s to e, both the copy model's wrist matrix
 * (rows a turn times one scale, the origin cm): the turn by slerp, the scale and the origin straight.
 * A mirrored and an unmirrored pose cannot be blended: the turn is then s's until t reaches 1. */
static void vrReloadBlendWrist(const Mtxf *s, const Mtxf *e, f32 t, Mtxf *out)
{
	f32 rs[3][3], re[3][3];
	f32 qs[4], qe[4], q[4];
	f32 ss, se, sc, dot;
	Mtxf m;
	s32 ms, me, i, j;

	ss = sqrtf(s->m[0][0] * s->m[0][0] + s->m[0][1] * s->m[0][1] + s->m[0][2] * s->m[0][2]);
	se = sqrtf(e->m[0][0] * e->m[0][0] + e->m[0][1] * e->m[0][1] + e->m[0][2] * e->m[0][2]);
	*out = *s;

	if (ss < 1e-9f || se < 1e-9f) {
		return;
	}

	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++) {
			rs[i][j] = s->m[i][j] / ss;
			re[i][j] = e->m[i][j] / se;
		}

		out->m[3][i] = s->m[3][i] + (e->m[3][i] - s->m[3][i]) * t;
	}

	ms = vrReloadRowsQuat(rs, qs);
	me = vrReloadRowsQuat(re, qe);
	sc = ss + (se - ss) * t;

	if (ms != me) {
		if (t >= 1.0f) {
			for (i = 0; i < 3; i++) {
				for (j = 0; j < 3; j++) {
					out->m[i][j] = e->m[i][j];
				}
			}
		}

		return;
	}

	/* The shorter way round */
	dot = qs[0] * qe[0] + qs[1] * qe[1] + qs[2] * qe[2] + qs[3] * qe[3];

	if (dot < 0.0f) {
		for (i = 0; i < 4; i++) {
			qe[i] = -qe[i];
		}
	}

	quaternionSlerp(qs, qe, t, q);
	quaternionToMtx(q, &m);

	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++) {
			out->m[i][j] = ((i == 0 && ms) ? -m.m[i][j] : m.m[i][j]) * sc;
		}
	}
}

bool vrReloadSlapNeedsContact(s32 handnum, f32 *frame)
{
	struct vrreloaddef *def;

	if (!vrReloadSlapDrawn(handnum, NULL)) {
		return false;
	}

	def = vrReloadOwnedDef(handnum);

	/* A row whose clip plays with the slap places the hand by the body (vrReloadPlaceSlapHand) */
	if (!def->clipseat || vrReloadHasSlapPlay(def)
			|| sSlapContact[handnum].weaponnum == g_Vars.currentplayer->hands[handnum].gset.weaponnum) {
		return false;
	}

	*frame = def->slapcontact;

	return true;
}

void vrReloadNoteSlapClip(s32 handnum, struct modeldef *modeldef, Mtxf *matrices)
{
	struct vrreloaddef *def;
	struct vrrslapclip *sc;
	Mtxf ident;

	if (handnum < 0 || handnum >= 2 || modeldef == NULL || matrices == NULL) {
		return;
	}

	def = vrReloadOwnedDef(handnum);

	if (def == NULL || !def->clipseat) {
		return;
	}

	/* Recorded for the gun whether or not the read works, so a failed one is not tried (and the copy
	 * model posed at the contact) again every frame */
	sc = &sSlapContact[handnum];
	sc->weaponnum = g_Vars.currentplayer->hands[handnum].gset.weaponnum;
	sc->valid = 0;

	if (def->clipmtx < 0 || def->clipmtx >= modeldef->nummatrices
			|| def->bodymtx < 0 || def->bodymtx >= modeldef->nummatrices
			|| modelGetPart(modeldef, def->clippart) == NULL) {
		VRR_DIAG("slap: frame=%d hand=%d weapon=%d contact pose not read: clipmtx=%d bodymtx=%d nummatrices=%d clippart=%d: the slap's hand is not drawn",
				g_Vars.lvframenum, handnum, sc->weaponnum, def->clipmtx, def->bodymtx, modeldef->nummatrices,
				modelGetPart(modeldef, def->clippart) != NULL);
		return;
	}

	/* Column 3 of c is not read (vrReloadRebaseMtx reads and writes the rows' first three) */
	mtx4LoadIdentity(&ident);
	vrReloadRebaseMtx(&sc->c, &matrices[def->clipmtx], &matrices[def->bodymtx], &ident);
	sc->valid = 1;
}

bool vrReloadPlaceSlapHand(s32 handnum, struct modeldef *modeldef, Mtxf *matrices, s32 wristmtx)
{
	struct vrreloaddef *def;
	struct vrrslap *sl;
	struct vrrwrist *w;
	Mtxf from;
	s32 i, age, p, held, in;

	if (!vrReloadSlapDrawn(handnum, NULL) || modeldef == NULL || matrices == NULL) {
		return false;
	}

	def = vrReloadOwnedDef(handnum);
	sl = &sSlap[handnum];
	age = g_Vars.lvframenum - sSlapClipFrame[handnum];

	if (def->clipmtx < 0 || def->clipmtx >= modeldef->nummatrices || wristmtx < 0 || wristmtx >= modeldef->nummatrices
			|| modelGetPart(modeldef, def->clippart) == NULL
			|| sSlapClipWeapon[handnum] != g_Vars.currentplayer->hands[handnum].gset.weaponnum
			|| age < 0 || age > VRR_PARTS_MAX_AGE) {
		return false;
	}

	/* Every matrix as the animation has it against its clip, on the gun's drawn clip: the
	 * animation's own body and clip move (rifleslapmove.py) do not carry the hand away, and the
	 * gun's left-handed mirror carries over, so the hand is mirrored with the gun. With a clip seat
	 * (the Laptop Gun), against the animation's clip as it is at slapcontact (sSlapContact, on this
	 * frame's body): its clip moves under the palm in the slap frames (2.8 cm up and 21 degrees from
	 * frame 84 to 86), so the hand moves as it does against the body, and the palm lands on the
	 * seated clip where it lands on the animation's clip at the contact. Without that pose yet, the
	 * hand is not drawn */
	if (vrReloadHasSlapPlay(def)) {
		/* A row whose clip plays with the slap (the Laptop Gun, Task F7g): the gun's clip moves as the
		 * animation's against the body, so the hand is placed by the body, every matrix as the
		 * animation has it against its body on the gun's drawn body, and the palm meets the drawn clip
		 * as it meets the animation's */
		if (def->bodymtx < 0 || def->bodymtx >= modeldef->nummatrices) {
			return false;
		}

		from = matrices[def->bodymtx];

		for (i = 0; i < modeldef->nummatrices; i++) {
			vrReloadRebaseMtx(&matrices[i], &matrices[i], &from, &sSlapBody[handnum]);
		}
	} else {
		if (def->clipseat) {
			Mtxf ident;

			if (sSlapContact[handnum].weaponnum != g_Vars.currentplayer->hands[handnum].gset.weaponnum
					|| !sSlapContact[handnum].valid || def->bodymtx < 0 || def->bodymtx >= modeldef->nummatrices) {
				return false;
			}

			mtx4LoadIdentity(&ident);
			vrReloadRebaseMtx(&from, &sSlapContact[handnum].c, &ident, &matrices[def->bodymtx]);
		} else {
			from = matrices[def->clipmtx];
		}

		for (i = 0; i < modeldef->nummatrices; i++) {
			vrReloadRebaseMtx(&matrices[i], &matrices[i], &from, &sSlapClip[handnum]);
		}
	}

	/* The hand on the controller (home): the whole hand, rigid, its wrist where the held hand's wrist
	 * sits on the off controller now. Without a held wrist for this gun (drawn needing a rack, with no
	 * clip held since, or the gun side changed), the open fists' wrist (vrReloadNoteFistWrist),
	 * keeping the animation's turn, which the fists then take over. The way in (Task F8 review: with
	 * slaprad 12.5 the animation's hand at slapfrom can be 10 cm from the controller's): from slapfrom
	 * to slapcontact the hand goes from home to the animation's pose, so it travels to the clip and
	 * meets it at the contact, whose timing is the animation's as before. The way back: from the wrist
	 * at the slap's last frame (or, stopped before the contact, from where the way in had it) to home
	 * over VRR_SLAP_EASE_60THS */
	w = &sHoldWrist[handnum];
	p = vrReloadOffPhysHand();
	held = w->weaponnum == g_Vars.currentplayer->hands[handnum].gset.weaponnum;
	in = sl->frame < def->slapcontact && def->slapcontact > def->slapfrom;

	if ((sl->ease > 0.0f || in) && (held || (p >= 0 && sFistWristOk[p]))) {
		Mtxf r, home, wrist, start, now;

		wrist = matrices[wristmtx];
		home = wrist;
		vrReloadOffRot(&r);

		if (held) {
			for (i = 0; i < 3; i++) {
				vrReloadGunToView(&r, w->rows[i], home.m[i]);
			}
		}

		vrReloadGunToView(&r, held ? w->pos : sFistWrist[p], home.m[3]);

		for (i = 0; i < 3; i++) {
			home.m[3][i] += gCtrlPos[VRR_CTRL_OFF][i];
		}

		start = wrist;

		if (in) {
			vrReloadBlendWrist(&home, &wrist,
					vrReloadClamp((sl->frame - def->slapfrom) / (def->slapcontact - def->slapfrom), 0.0f, 1.0f), &start);
		}

		if (sl->ease > 0.0f) {
			vrReloadBlendWrist(&start, &home, vrReloadClamp(1.0f - sl->ease / VRR_SLAP_EASE_60THS, 0.0f, 1.0f), &now);
		} else {
			now = start;
		}

		for (i = 0; i < modeldef->nummatrices; i++) {
			vrReloadRebaseMtx(&matrices[i], &matrices[i], &wrist, &now);
		}
	}

	return true;
}

bool vrReloadRackDrawn(s32 handnum, f32 *frame)
{
	struct vrreloaddef *def;
	f32 pullRatio, f;

	if (handnum < 0 || handnum >= 2) {
		return false;
	}

	def = vrReloadOwnedDef(handnum);
	if (def == NULL || def->rackframefrom <= 0.0f) {
		return false;
	}

	if (sGun[handnum].state == VRR_RACKING) {
		pullRatio = def->racktravel > 0.0f ? (sRackPull[handnum] / def->racktravel) : 0.0f;
		if (pullRatio < 0.0f) pullRatio = 0.0f;
		if (pullRatio > 1.0f) pullRatio = 1.0f;

		f = def->rackframefrom + pullRatio * (def->rackframeto - def->rackframefrom);
		sRackLastFrame[handnum] = f;
		if (frame != NULL) {
			*frame = f;
		}
		return true;
	}

	if (sRackEase[handnum] > 0.0f) {
		if (frame != NULL) {
			*frame = sRackLastFrame[handnum];
		}
		return true;
	}

	return false;
}

bool vrReloadPlaceRackHand(s32 handnum, struct modeldef *modeldef, Mtxf *matrices, s32 wristmtx)
{
	struct vrreloaddef *def;
	struct vrrwrist *w;
	s32 age, i, p, held;
	s32 sidx, bidx;
	bool useSlide;
	Mtxf from;
	const Mtxf *to;

	if (handnum < 0 || handnum >= 2 || modeldef == NULL || matrices == NULL) {
		return false;
	}

	def = vrReloadOwnedDef(handnum);
	if (def == NULL) {
		return false;
	}

	age = g_Vars.lvframenum - sGunBodyFrame[handnum];
	if (age < 0 || age > VRR_PARTS_MAX_AGE
			|| sGunBodyWeapon[handnum] != g_Vars.currentplayer->hands[handnum].gset.weaponnum
			|| sGunBodyIndex[handnum] < 0 || sGunBodyIndex[handnum] >= modeldef->nummatrices
			|| wristmtx < 0 || wristmtx >= modeldef->nummatrices) {
		return false;
	}

	sidx = sRackSlideIndex[handnum];
	bidx = sGunBodyIndex[handnum];
	useSlide = (sidx >= 0 && sidx < modeldef->nummatrices
			&& sRackSlideFrame[handnum] == sGunBodyFrame[handnum]);

	/* from: slide (or gun body) in g_VrCopyWepModel (posed at rackframe)
	 * to:   slide (or gun body) on hand->gunmodel (real gun in camera space) */
	from = matrices[useSlide ? sidx : bidx];
	to = useSlide ? &sRackSlide[handnum] : &sGunBody[handnum];

	for (i = 0; i < modeldef->nummatrices; i++) {
		vrReloadRebaseMtx(&matrices[i], &matrices[i], &from, to);
	}

	/* Ease out when grip is released */
	w = &sHoldWrist[handnum];
	p = vrReloadOffPhysHand();
	held = w->weaponnum == g_Vars.currentplayer->hands[handnum].gset.weaponnum;

	if (sRackEase[handnum] > 0.0f && (held || (p >= 0 && sFistWristOk[p]))) {
		Mtxf r, home, wrist, now;

		wrist = matrices[wristmtx];
		home = wrist;
		vrReloadOffRot(&r);

		if (held) {
			for (i = 0; i < 3; i++) {
				vrReloadGunToView(&r, w->rows[i], home.m[i]);
			}
		}

		vrReloadGunToView(&r, held ? w->pos : sFistWrist[p], home.m[3]);

		for (i = 0; i < 3; i++) {
			home.m[3][i] += gCtrlPos[VRR_CTRL_OFF][i];
		}

		vrReloadBlendWrist(&wrist, &home,
				vrReloadClamp(1.0f - sRackEase[handnum] / VRR_RACK_EASE_60THS, 0.0f, 1.0f), &now);

		for (i = 0; i < modeldef->nummatrices; i++) {
			vrReloadRebaseMtx(&matrices[i], &matrices[i], &wrist, &now);
		}
	}

	return true;
}

bool vrReloadClipIsGuided(s32 handnum)
{
	return handnum >= 0 && handnum < 2 && sGuided[handnum] != 0;
}

f32 vrReloadGuideProgress(s32 handnum)
{
	struct vrreloaddef *def;
	f32 p;

	if (handnum < 0 || handnum >= 2 || !sGuided[handnum]) {
		return 0.0f;
	}

	def = vrReloadOwnedDef(handnum);
	if (def == NULL || def->clipinsertdepth <= 0.0f) {
		return 1.0f;
	}

	p = sProgress[handnum] / def->clipinsertdepth;
	if (p < 0.0f) p = 0.0f;
	if (p > 1.0f) p = 1.0f;
	return p;
}

bool vrReloadPlaceGuidedHand(s32 handnum, struct modeldef *modeldef, Mtxf *matrices, s32 wristmtx)
{
	struct vrreloaddef *def;
	s32 age, i;
	Mtxf from;

	if (handnum < 0 || handnum >= 2 || modeldef == NULL || matrices == NULL) {
		return false;
	}

	def = vrReloadOwnedDef(handnum);
	if (def == NULL) {
		return false;
	}

	age = g_Vars.lvframenum - sGunBodyFrame[handnum];
	if (age < 0 || age > VRR_PARTS_MAX_AGE
			|| sGunBodyWeapon[handnum] != g_Vars.currentplayer->hands[handnum].gset.weaponnum
			|| sGunBodyIndex[handnum] < 0 || sGunBodyIndex[handnum] >= modeldef->nummatrices
			|| wristmtx < 0 || wristmtx >= modeldef->nummatrices) {
		return false;
	}

	/* from: gun body in g_VrCopyWepModel (posed at guided insertion frame)
	 * to:   gun body on hand->gunmodel (real gun in camera space) */
	from = matrices[sGunBodyIndex[handnum]];

	for (i = 0; i < modeldef->nummatrices; i++) {
		vrReloadRebaseMtx(&matrices[i], &matrices[i], &from, &sGunBody[handnum]);
	}

	return true;
}

/* Whether a turn's rows are mirrored (a negative determinant) */
static s32 vrReloadRowsMirrored(const Mtxf *m)
{
	return m->m[0][0] * (m->m[1][1] * m->m[2][2] - m->m[1][2] * m->m[2][1])
		- m->m[0][1] * (m->m[1][0] * m->m[2][2] - m->m[1][2] * m->m[2][0])
		+ m->m[0][2] * (m->m[1][0] * m->m[2][1] - m->m[1][1] * m->m[2][0]) < 0.0f;
}

/* The hand-off of a row with playease: the held part's matrix x, as the playback posed it, drawn a
 * fraction of the way there from where the free hand held it at the load (sHandoff, on the gun), the
 * rest of the way as the hand-off runs down. The held pose comes from the copy model, whose matrices
 * have no bg_scale: its rows and origin are scaled by the gun's (both carry the same model scale). A
 * held pose mirrored against the played one (not expected: the copy model and the gun mirror together
 * in left-handed mode) takes the played turn and eases only its place. */
static void vrReloadEaseHandoff(s32 h, struct hand *hand, struct vrreloaddef *def, Mtxf *x)
{
	struct vrrhandoff *ho = &sHandoff[h];
	Mtxf start;
	f32 anchor[3];
	f32 sc, t;
	s32 i, j;

	if (ho->left <= 0.0f || def->playease <= 0.0f) {
		return;
	}

	vrReloadGunAnchor(h, hand, anchor);

	for (i = 0; i < 3; i++) {
		vrReloadGunToView(&hand->posrotmtx, ho->rows[i], start.m[i]);
	}

	vrReloadGunToView(&hand->posrotmtx, ho->pos, start.m[3]);

	/* The gun's matrices are camera space times bg_scale, applied only when it is not 0 or 1, as
	 * vrReloadDropClip has it; the copy model's have none */
	sc = bgGetScaleBg2Gfx();

	if (sc == 0.0f || sc == 1.0f) {
		sc = 1.0f;
	}

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 3; j++) {
			start.m[i][j] = (i == 3 ? start.m[i][j] + anchor[j] : start.m[i][j]) * sc;
		}
	}

	/* A mirrored pose cannot be blended with an unmirrored one. vrReloadBlendWrist would then hold
	 * the start's turn until t reaches 1 and snap; this takes the played turn from the start instead,
	 * so only the place eases */
	if (vrReloadRowsMirrored(&start) != vrReloadRowsMirrored(x)) {
		for (i = 0; i < 3; i++) {
			for (j = 0; j < 3; j++) {
				start.m[i][j] = x->m[i][j];
			}
		}
	}

	t = vrReloadClamp(1.0f - ho->left / def->playease, 0.0f, 1.0f);

	/* Column 3 is left alone: it carries the part-hide signal */
	{
		Mtxf now;

		vrReloadBlendWrist(&start, x, t, &now);

		for (i = 0; i < 4; i++) {
			for (j = 0; j < 3; j++) {
				x->m[i][j] = now.m[i][j];
			}
		}
	}
}

/* The held part goes in straight on playstraightmtx: both posed by the insert's playback (first..last),
 * the part on heldmtx alone (as the row checks in vrReloadResetAll warn) */
static s32 vrReloadPlaysStraight(struct vrreloaddef *def, s32 first, s32 last)
{
	return def->playstraightmtx != 0 && def->playstraightmtx >= first && def->playstraightmtx <= last
		&& def->playstraightmtx != def->heldmtx && def->heldmtx >= first && def->heldmtx <= last
		&& (def->heldmtxlast < 0 || def->heldmtxlast == def->heldmtx);
}

/* A row with playstraightmtx: the held part's matrix x put straight on that matrix's ref, both posed in
 * the same space (drawn, or against the body): its turn on ref a roll of playstraightroll degrees about
 * ref's +z and its origin's x and y playstraightpos, only the origin's z kept, so the held part goes in
 * along ref's axis. The roll and place are in ref's own frame, so a mirrored ref (left-handed mode, the
 * left gun) mirrors them with it. Column 3 is left alone: it carries the part-hide signal */
static void vrReloadPlayStraight(struct vrreloaddef *def, Mtxf *x, const Mtxf *ref)
{
	Mtxf ident, loc;
	f32 c = cosf(DEG2RAD(def->playstraightroll));
	f32 s = sinf(DEG2RAD(def->playstraightroll));
	s32 i;

	mtx4LoadIdentity(&ident);
	vrReloadRebaseMtx(&loc, x, ref, &ident);

	for (i = 0; i < 3; i++) {
		loc.m[0][i] = i == 0 ? c : i == 1 ? s : 0.0f;
		loc.m[1][i] = i == 0 ? -s : i == 1 ? c : 0.0f;
		loc.m[2][i] = i == 2 ? 1.0f : 0.0f;
	}

	loc.m[3][0] = def->playstraightpos[0] * VRR_MODEL_UNITS_PER_CM;
	loc.m[3][1] = def->playstraightpos[1] * VRR_MODEL_UNITS_PER_CM;
	vrReloadRebaseMtx(x, &loc, &ident, ref);
}

/* The gun model posed again at a frame of its reload animation, with the playback's own anim state,
 * into sPlayPosed with an identity root, as the idle pose cache (unk0dd8) is built: the hand's anim, the
 * drawn matrices and the joint callback are put back after. The model's part states are the ones its
 * own pose this frame used. flip as the hand's anim has it. */
static void vrReloadPoseScratch(struct model *model, s32 animnum, bool flip, f32 frame)
{
	struct modelrenderdata renderdata = { NULL, true, 3 };
	void (*jointfunc)(s32 mtxindex, Mtxf *mtx) = g_ModelJointPositionedFunc;
	struct anim *anim = model->anim;
	Mtxf *drawn = model->matrices;
	Mtxf root;

	if (!sPlayAnimReady) {
		animInit(&sPlayAnim);
		sPlayAnimReady = 1;
	}

	mtx4LoadIdentity(&root);
	renderdata.unk00 = &root;
	renderdata.unk10 = sPlayPosed;

	model->anim = &sPlayAnim;
	g_ModelJointPositionedFunc = NULL;
	modelSetAnimation(model, animnum, flip, frame, 0.0f, 0.0f);
	var8005efd8_2 = true;
	var8005efb0_2 = true;
	modelSetMatricesWithAnim(&renderdata, model);
	var8005efd8_2 = false;
	var8005efb0_2 = false;
	g_ModelJointPositionedFunc = jointfunc;
	model->anim = anim;
	model->matrices = drawn;
}

bool vrReloadPosePlay(s32 handnum, struct model *model, s32 animnum)
{
	struct vrreloaddef *def;
	struct modeldef *modeldef;
	Mtxf *drawn, *posed;
	Mtxf ident;
	bool flip, held;
	f32 frame;
	s32 k, eject, body, first, last, playing;

	if (handnum < 0 || handnum >= 2 || model == NULL || model->definition == NULL || model->matrices == NULL) {
		return false;
	}

	def = vrReloadOwnedDef(handnum);

	/* Only while one plays or a row holds a last frame: nothing to do, and no cost, otherwise */
	if (def == NULL || !vrReloadSliding(handnum)) {
		return false;
	}

	playing = sPlayFrame[handnum] > 0.0f;

	if (playing) {
		frame = sPlayFrame[handnum];
		eject = sPlayEject[handnum];
	} else if (vrReloadHasClose(def) && vrReloadHasPlay(def) && sGun[handnum].magin
			&& sGun[handnum].state == VRR_NEED_RACK) {
		/* Loaded and waiting for its close (the DY357's cylinder): the insert's last frame, the gun
		 * still open with the rounds in it, so the load reads as done */
		frame = def->playto;
		eject = 0;
	} else if (vrReloadHeldOpen(handnum, def)) {
		/* The gun stays open from the eject to the load (the Rocket Launcher's hatch), or to the close
		 * after it (the DY357's cylinder) */
		frame = def->ejectto;
		eject = 1;
	} else if (vrReloadHoldsPlay(def) && sGun[handnum].magin && sGun[handnum].chamber) {
		/* The loaded round stays seen where the insert's playback left it (the Crossbow's bolt) */
		frame = def->playto;
		eject = 0;
	} else {
		return false;
	}

	if (playing && sPlayClose[handnum] ? !vrReloadHasClose(def)
			: eject ? !vrReloadHasEjectPlay(def) : !vrReloadHasPlay(def) && !vrReloadHasSlapPlay(def)) {
		return false;
	}

	/* The insert's playback poses on the intake's body, the eject's and a clip row's on bodymtx, an
	 * intake row's eject on the intake's body (vrReloadPlayBody) */
	body = vrReloadPlayBody(def);
	first = eject ? def->ejectmtx : def->playmtx;
	last = eject ? def->ejectmtxlast : def->playmtxlast;
	modeldef = model->definition;

	if (animnum <= 0 || modeldef->nummatrices > VRR_PLAY_MAX_MTX
			|| body >= modeldef->nummatrices || last >= modeldef->nummatrices
			|| (vrReloadHasClip(def) && def->clipmtx >= modeldef->nummatrices)) {
		if (playing && !sPlayLogged[handnum]) {
			VRR_DIAG("play: frame=%d hand=%d weapon=%d not drawn: anim=%d nummatrices=%d eject=%d",
					g_Vars.lvframenum, handnum, sGunWeapon[handnum], animnum, modeldef->nummatrices, eject);
			sPlayLogged[handnum] = 1;
		}

		return false;
	}

	posed = sPlayPosed;
	drawn = model->matrices;
	flip = modelIsFlipped(model);
	mtx4LoadIdentity(&ident);

	/* A held last frame is no playback: its matrices against the body are posed once per weapon, frame
	 * and gun side (sHoldPose) and put on the drawn body on every draw; no clip (a hold's clip is out or
	 * absent), hand-off, log line, count or fall record. The held part shows with the loaded round
	 * (playhold, and a row loaded and waiting for its close), never with the gun held open with
	 * nothing in (ejecthold) */
	if (!playing) {
		struct vrrholdpose *hp = &sHoldPose[handnum];
		s32 weaponnum = g_Vars.currentplayer->hands[handnum].gset.weaponnum;

		if (hp->weaponnum != weaponnum || hp->animnum != animnum || hp->eject != eject || hp->frame != frame
				|| hp->flip != (s32) flip || hp->lefthanded != (s32) VrLeftHandedMode) {
			vrReloadPoseScratch(model, animnum, flip, frame);

			for (k = first; k <= last; k++) {
				vrReloadRebaseMtx(&hp->rel[k], &posed[k], &posed[body], &ident);
			}

			/* The loaded rounds held in the open cylinder, and so carried in by the close, straight in
			 * the chambers (playstraightmtx) */
			if (!eject && vrReloadPlaysStraight(def, first, last)) {
				vrReloadPlayStraight(def, &hp->rel[def->heldmtx], &hp->rel[def->playstraightmtx]);
			}

			hp->weaponnum = weaponnum;
			hp->animnum = animnum;
			hp->eject = eject;
			hp->frame = frame;
			hp->flip = flip;
			hp->lefthanded = VrLeftHandedMode;
		}

		for (k = first; k <= last; k++) {
			if (k != body && (!vrReloadHasClip(def) || k != def->clipmtx)) {
				vrReloadRebaseMtx(&drawn[k], &hp->rel[k], &ident, &drawn[body]);
			}
		}

		return !eject;
	}

	vrReloadPoseScratch(model, animnum, flip, frame);

	/* Each played matrix as the animation has it relative to the body, on the body as drawn: the
	 * drawn body carries the controller pose, the grip offset, the scales and the left-handed or
	 * left-gun mirror, so the plates and the orb follow the gun and mirror with it. The body, the
	 * right hand and the left hand stay as drawn. */
	for (k = first; k <= last; k++) {
		if (k != body && (!vrReloadHasClip(def) || k != def->clipmtx)) {
			vrReloadRebaseMtx(&drawn[k], &posed[k], &posed[body], &drawn[body]);
		}
	}

	/* A playback posing the clip (the Laptop Gun's eject and slap) leaves it to vrReloadPlaceClip,
	 * after the capture, so the well and the slap point stay on the seat: its pose against the body */
	if (vrReloadPlaysClip(def, eject)) {
		vrReloadRebaseMtx(&sPlayClip[handnum], &posed[def->clipmtx], &posed[body], &ident);
		sPlayClipFrame[handnum] = g_Vars.lvframenum;
	}

	/* The held part goes in straight (playstraightmtx: the DY357's rounds along the chambers), and the
	 * hand-off below eases it onto that */
	if (!eject && vrReloadPlaysStraight(def, first, last)) {
		vrReloadPlayStraight(def, &drawn[def->heldmtx], &drawn[def->playstraightmtx]);
	}

	/* The held part goes over from the free hand (playease: the Cyclone's bar, the DY357's rounds) */
	if (!eject && def->heldmtx >= first && def->heldmtx <= last) {
		vrReloadEaseHandoff(handnum, &g_Vars.currentplayer->hands[handnum], def, &drawn[def->heldmtx]);
	}

	/* The close carries the loaded rounds in with the cylinder (the first eject matrix): as they sat in
	 * it at the insert's last frame, read from the pose held while the gun waited (sHoldPose). Without
	 * that pose (not expected) they are not drawn */
	held = 0;

	if (sPlayClose[handnum] && vrReloadHasPlay(def) && def->heldmtx >= 0 && def->heldmtx < modeldef->nummatrices
			&& def->heldmtx >= def->playmtx && def->heldmtx <= def->playmtxlast
			&& first >= def->playmtx && first <= def->playmtxlast) {
		struct vrrholdpose *hp = &sHoldPose[handnum];

		if (hp->weaponnum == g_Vars.currentplayer->hands[handnum].gset.weaponnum && hp->animnum == animnum
				&& !hp->eject && hp->frame == def->playto && hp->flip == (s32) flip
				&& hp->lefthanded == (s32) VrLeftHandedMode) {
			vrReloadRebaseMtx(&drawn[def->heldmtx], &hp->rel[def->heldmtx], &hp->rel[first], &drawn[first]);
			held = 1;
		}
	}

	if (!sPlayLogged[handnum]) {
		VRR_DIAG("play: frame=%d hand=%d weapon=%d drawn anim=%d at=%.1f flip=%d eject=%d",
				g_Vars.lvframenum, handnum, sGunWeapon[handnum], animnum, frame, flip, eject);
		sPlayLogged[handnum] = 1;
	}

	sPlayDrawn[handnum]++;

	/* A held part that falls after the playback (playdrop) is drawn through playto, the frame its
	 * fall starts from (vrReloadDropClip records it); one kept seen after it (playhold) through the
	 * whole playback; else until playheldto, where the script hides it */
	held = held || (!eject && (vrReloadPlayDrops(def) ? frame <= def->playto
			: vrReloadHoldsPlay(def) || frame < def->playheldto));

	if (held) {
		sPlayHeldFrame[handnum] = g_Vars.lvframenum;
	}

	return held;
}

s32 vrReloadTakeCasings(s32 handnum)
{
	s32 n;

	if (handnum < 0 || handnum >= 2 || sCasings[handnum] <= 0) {
		return 0;
	}

	n = sCasings[handnum];
	sCasings[handnum] = 0;

	return vrReloadOwnsGun(handnum) && sGunWeapon[handnum] == g_Vars.currentplayer->hands[handnum].gset.weaponnum ? n : 0;
}

bool vrReloadClipFalling(s32 handnum)
{
	return handnum >= 0 && handnum < 2 && vrReloadSliding(handnum) && sDrop[handnum].active
		&& sDrop[handnum].weaponnum == g_Vars.currentplayer->hands[handnum].gset.weaponnum;
}

void vrReloadDropClip(s32 handnum, struct modeldef *modeldef, Mtxf *matrices, f32 bgscale)
{
	struct vrreloaddef *def;
	struct vrrclipworld rec;
	struct vrrdrop *d;
	Mtxf *cam2world = camGetProjectionMtxF();
	Mtxf *world2cam = camGetWorldToScreenMtxf();
	Mtxf *m;
	f32 r[3][3];
	f32 v[3];
	f32 half[3];
	f32 pos[3];
	f32 scale, invscale;
	s32 i, j;

	if (handnum < 0 || handnum >= 2 || modeldef == NULL || matrices == NULL
			|| cam2world == NULL || world2cam == NULL) {
		return;
	}

	def = vrReloadOwnedDef(handnum);

	/* The clip's matrix, or the held part's for a row whose held part falls after its playback */
	if (def == NULL || vrReloadDropMtx(def) < 0 || vrReloadDropMtx(def) >= modeldef->nummatrices
			|| modelGetPart(modeldef, vrReloadDropPart(def)) == NULL) {
		return;
	}

	/* The gun's matrices are camera space times bg_scale (only when it is not 0 or 1, as
	 * bgun0f0a5550 applies it); camGetProjectionMtxF takes unscaled camera space to world, as
	 * for hand->muzzlepos */
	scale = (bgscale != 0.0f && bgscale != 1.0f) ? bgscale : 1.0f;
	invscale = 1.0f / scale;
	m = &matrices[vrReloadDropMtx(def)];

	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++) {
			v[j] = m->m[i][j] * invscale;
		}

		vrReloadRot(cam2world, v, rec.rot[i]);
	}

	for (j = 0; j < 3; j++) {
		v[j] = m->m[3][j] * invscale;
	}

	vrReloadXform(cam2world, v, rec.pos);
	rec.weaponnum = g_Vars.currentplayer->hands[handnum].gset.weaponnum;
	rec.frame = g_Vars.lvframenum;

	/* A frame drawn with the pose mapping from before a gun side flip (the flip came after this
	 * frame's poses: Y on a pair, a holster draw) shows this hand's gun at the other controller:
	 * it records nothing, so an eject waits for the next frame's record (vrReloadBuildInput). The
	 * held part of a row with playdrop is recorded only as the playback drew it in the gun this
	 * frame: elsewhere its matrix is the gun model's idle left wrist's */
	if (VrLeftHandedMode == VrCtrlPoseMode
			&& (vrReloadHasClip(def) || sPlayHeldFrame[handnum] == g_Vars.lvframenum)) {
		sClipWorld[handnum] = rec;
	}

	if (!vrReloadClipFalling(handnum)) {
		return;
	}

	d = &sDrop[handnum];

	if (g_Vars.currentplayer->isdead) {
		d->active = 0;
		return;
	}

	if (d->stepframe != g_Vars.lvframenum) {
		d->stepframe = g_Vars.lvframenum;
		vrReloadDropStep(d);

		if (!d->active) {
			return;
		}
	}

	/* World pose of the clip matrix: the centre minus the turned pivot */
	vrReloadDropRot(d, r);
	vrReloadRows(r, d->pivot, half);

	for (j = 0; j < 3; j++) {
		pos[j] = d->centre[j] - half[j];
	}

	/* Back to camera space times bg_scale. Column 3 is left alone: it carries the part-hide
	 * signal (vrHideGunParts) */
	for (i = 0; i < 3; i++) {
		vrReloadRot(world2cam, r[i], v);

		for (j = 0; j < 3; j++) {
			m->m[i][j] = v[j] * scale;
		}
	}

	vrReloadXform(world2cam, pos, v);

	for (j = 0; j < 3; j++) {
		m->m[3][j] = v[j] * scale;
	}

	d->drawframe = g_Vars.lvframenum;
}

void vrReloadNarrowClip(s32 handnum, struct modeldef *modeldef, Mtxf *matrices)
{
	struct vrreloaddef *def;
	Mtxf *m;
	s32 j;

	if (handnum < 0 || handnum >= 2 || modeldef == NULL || matrices == NULL) {
		return;
	}

	def = vrReloadOwnedDef(handnum);

	if (def == NULL || def->clipwidth == 1.0f || def->clipmtx < 0 || def->clipmtx >= modeldef->nummatrices
			|| modelGetPart(modeldef, def->clippart) == NULL
			|| !vrReloadClipShownInGun(handnum) || vrReloadClipFalling(handnum)) {
		return;
	}

	/* The x row only: the joint (row 3) is the clip's top on its centre line, so the clip narrows
	 * about its own middle. Column 3 is left alone: it carries the part-hide signal */
	m = &matrices[def->clipmtx];

	for (j = 0; j < 3; j++) {
		m->m[0][j] *= def->clipwidth;
	}
}

/* The clip's matrix can also skin another part: the CMP150's part 0x42 holds the clip's bottom face
 * (4 vertices on matrix 38, the clip's) with a front face of the gun (on the body, 33), as the K7's,
 * the Dragons' and the Reaper's hold their clips' faces. The clip part is hidden while no clip is
 * drawn in the gun (bgun0f0a5550 shows it from vrReloadClipShownInGun and vrReloadClipFalling, as
 * here). Collapsing the matrix to zero scale puts everything skinned to it on its joint, so it draws
 * nothing, and leaves the rest of such a part as it is; the joint stays where it was, and column 3 is
 * left alone (the part-hide signal, vrHideGunParts). For part 0x42 this is harmless and changes
 * nothing seen: bgun0f098030 hides 0x42 on every frame for every gun, so it is never drawn. It would
 * matter for a part that is drawn: then the face would go with its clip, not float where the seated
 * clip's base was. A row whose clip matrix skins the clip alone (every pistol row, read from the
 * ROM's models) draws nothing on it then anyway: no change. */
struct vrreloaddef *vrReloadParkedDef(s32 handnum, bool *clipin)
{
	struct vrreloaddef *driven = vrReloadOwnedDef(handnum);
	s32 weaponnum;
	s32 i;

	if (driven == NULL) {
		return NULL;
	}

	weaponnum = g_Vars.currentplayer->hands[handnum].gset.weaponnum;

	for (i = 0; i < VRR_MAGS; i++) {
		struct vrreloaddef *def = i != driven->ammoindex ? vrReloadGetDef(weaponnum, i) : NULL;

		if (def == NULL || def == driven) {
			continue;
		}

		/* No parked shape: the magazine is as the engine has it, in (a gun never driven on that
		 * function, as vrReloadTick would start it) */
		if (clipin != NULL) {
			*clipin = sPendingWeapon[handnum][i] != weaponnum || sPending[handnum][i].magin;
		}

		return def;
	}

	return NULL;
}

/* A row's clip matrix collapsed to zero scale, when the model is the row's */
static void vrReloadCollapseMtx(struct vrreloaddef *def, struct modeldef *modeldef, Mtxf *matrices)
{
	Mtxf *m;
	s32 i, j;

	if (!vrReloadHasClip(def) || def->clipmtx < 0 || def->clipmtx >= modeldef->nummatrices
			|| modelGetPart(modeldef, def->clippart) == NULL) {
		return;
	}

	m = &matrices[def->clipmtx];

	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++) {
			m->m[i][j] = 0.0f;
		}
	}
}

void vrReloadCollapseClip(s32 handnum, struct modeldef *modeldef, Mtxf *matrices)
{
	struct vrreloaddef *def;
	bool parkedin = true;

	if (handnum < 0 || handnum >= 2 || modeldef == NULL || matrices == NULL) {
		return;
	}

	def = vrReloadOwnedDef(handnum);

	if (def == NULL) {
		return;
	}

	if (!vrReloadClipShownInGun(handnum) && !vrReloadClipFalling(handnum)) {
		vrReloadCollapseMtx(def, modeldef, matrices);
	}

	/* The other function's magazine, out as it was parked (the SuperDragon's grenade magazine with
	 * the rifle driven, or the rifle clip with the grenades) collapsed too (its cap on part 0x42 is
	 * hidden anyway) */
	def = vrReloadParkedDef(handnum, &parkedin);

	if (def != NULL && !parkedin) {
		vrReloadCollapseMtx(def, modeldef, matrices);
	}
}

/* The right hand's owned gun state: the shape a draw restored while its first tick has not applied
 * it, else the live one. NULL when the gun is not owned or the core is not driving it. */
static struct vrrgun *vrReloadHandGun(s32 handnum)
{
	struct vrrgun *g;

	if (!vrReloadOwnsGun(handnum)) {
		return NULL;
	}

	g = vrReloadPendingFor(handnum);

	if (g == NULL && sGunWeapon[handnum] >= 0) {
		g = &sGun[handnum];
	}

	return g;
}

static struct vrrgun *vrReloadRightGun(void)
{
	return vrReloadHandGun(HAND_RIGHT);
}

static s32 vrReloadHandReserve(s32 handnum)
{
	struct player *player = g_Vars.currentplayer;
	struct vrreloaddef *def = vrReloadOwnedDef(handnum);
	s32 ammotype = -1;

	if (def != NULL && def->ammoindex >= 0 && def->ammoindex < VRR_MAGS) {
		ammotype = player->gunctrl.ammotypes[def->ammoindex];
	}

	return ammotype >= 0 ? player->ammoheldarr[ammotype] : 0;
}

bool vrReloadEjectRefused(void)
{
	struct vrreloaddef *def = vrReloadOwnedDef(HAND_RIGHT);
	struct vrrgun *g = vrReloadRightGun();

	return def != NULL && g != NULL && def->emptyejects && g->magin && g->chamber + g->clip > 0;
}

/* Rounds in reserve for the right gun, as vrReloadBuildInput reads them: ammoheldarr of the driven
 * magazine's ammo type (the row's ammoindex: the SuperDragon's grenades with the grenade launcher
 * selected); 0 when the gun has none */
static s32 vrReloadRightReserve(void)
{
	struct player *player = g_Vars.currentplayer;
	struct vrreloaddef *def = vrReloadOwnedDef(HAND_RIGHT);
	s32 ammotype = -1;

	if (def != NULL && def->ammoindex >= 0 && def->ammoindex < VRR_MAGS) {
		ammotype = player->gunctrl.ammotypes[def->ammoindex];
	}

	return ammotype >= 0 ? player->ammoheldarr[ammotype] : 0;
}

bool vrReloadBusy(void)
{
	struct vrrgun *g;

	/* The free hand at work (vrReloadHoldsSwitch: a clip or round in it, a rack or a pump, the slap
	 * drawn, or an eject that will start a reload), or the clip out with rounds in the reserve the
	 * shoulder grab takes a clip from (as vrReloadBuildInput reads it: ammoheldarr of the driven
	 * magazine's ammo type); the other function's magazine, parked with its clip out, waits for its
	 * function. Not busy: a gun merely dry (empty chamber and clip: fired dry, flipped or redrawn so,
	 * or an empty clip racked), one with its clip out and an empty reserve, as no clip can be
	 * fetched, and one that only waits for its rack or slap (NEED_RACK: Task F7c, the RC-P120 could
	 * not be holstered so): nothing is in the free hand, and a switch saves it waiting (vrReloadSave
	 * settles it so, the suspend puts its uncounted clip back in the reserve and vrReloadDrawFill
	 * takes it out again). Otherwise the holsters (rule 29) would lock the free hand out until the
	 * gun was put away. B on a gun loaded shell by shell does nothing, so it is never busy */
	if (vrReloadHoldsSwitch()) {
		return true;
	}

	g = vrReloadRightGun();

	return g != NULL && !g->magin && vrReloadRightReserve() > 0;
}

bool vrReloadHoldsSwitch(void)
{
	struct vrreloaddef *def;
	struct vrrgun *g;

	if (!vrReloadOwnsGun(HAND_RIGHT)) {
		return false;
	}

	g = vrReloadRightGun();
	def = vrReloadOwnedDef(HAND_RIGHT);

	/* An eject the core has not taken yet (a frame or two) holds, as in vrReloadBusy, so the switch
	 * does not take a gun whose B is still on its way */
	if (sEject[HAND_RIGHT] && !def->shellbyshell && (g == NULL || (g->magin && g->state != VRR_RACKING
			&& (vrrEjectReturnsRow(g, 0) > 0
				|| vrReloadRightReserve() > 0)))) {
		return true;
	}

	if (g == NULL) {
		return false;
	}

	/* A clip or a round in the free hand, a rack (or a pump) under way, or the free hand drawn
	 * slapping: the free hand is in the middle of it. A gun with its clip out (the Rocket
	 * Launcher's hatch open) or waiting for its rack holds nothing: a switch saves it so */
	return g->state == VRR_HOLDING_CLIP || g->state == VRR_RACKING || vrReloadSlapDrawn(HAND_RIGHT, NULL);
}

bool vrReloadFreeHandAtAmmo(void)
{
	struct player *player = g_Vars.currentplayer;
	struct vrreloaddef *def;
	struct vrrgun *g;
	f32 shoulder[3], off[3];
	s32 clipsize;
	s32 gunhand = HAND_RIGHT;

	/* As vrReloadBuildInput reads the shoulder: this frame's gun side in the poses, the free hand free
	 * (empty, or its gun of a pair parked) */
	if (player == NULL || VrLeftHandedMode != VrCtrlPoseMode) {
		return false;
	}

	if (vrReloadOwnsGun(HAND_RIGHT) && (!player->hands[HAND_LEFT].inuse || vrHandParked(HAND_LEFT))) {
		gunhand = HAND_RIGHT;
	} else if (vrReloadOwnsGun(HAND_LEFT) && (!player->hands[HAND_RIGHT].inuse || vrHandParked(HAND_RIGHT))) {
		gunhand = HAND_LEFT;
	} else {
		return false;
	}

	g = vrReloadHandGun(gunhand);
	def = vrReloadOwnedDef(gunhand);

	if (g == NULL || def == NULL || def->ammoindex < 0 || def->ammoindex >= VRR_MAGS || vrReloadHandReserve(gunhand) <= 0) {
		return false;
	}

	/* What a squeeze there takes (vrrTick): a clip while the clip is out, a shell while a gun loaded
	 * shell by shell has room in its tube */
	clipsize = player->hands[gunhand].clipsizes[def->ammoindex];

	if (!(g->state == VRR_CLIP_OUT
				|| (def->shellbyshell && (g->state == VRR_READY || g->state == VRR_NEED_RACK)
					&& clipsize - g->chamber - g->clip > 0))) {
		return false;
	}

	vrReloadShoulderHeadPos(shoulder);
	off[0] = gCtrlPos[VRR_CTRL_OFF][0];
	off[1] = gCtrlPos[VRR_CTRL_OFF][1];
	off[2] = gCtrlPos[VRR_CTRL_OFF][2];

	return vrReloadDist(off, shoulder) <= VRR_SHOULDER_RAD;
}

#define VRR_SWAP(type, a, b) do { type vrrswaptmp = (a); (a) = (b); (b) = vrrswaptmp; } while (0)

bool vrReloadCanSwapHands(void)
{
	struct player *player = g_Vars.currentplayer;
	struct hand *r, *l;

	if (player == NULL || g_Vars.currentplayernum != 0) {
		return false;
	}

	r = &player->hands[HAND_RIGHT];
	l = &player->hands[HAND_LEFT];

	/* Only a pair in hand. The engine can hold the left draw back (bgunIsReadyToSwitch, while the
	 * right hand attacks, reloads or changes function) while the holsters already count two guns:
	 * rounds swapped into the unused hand would be skipped by bgunFreeWeapon and lost */
	return r->inuse && l->inuse && r->gset.weaponnum == l->gset.weaponnum;
}

bool vrReloadSwapHands(void)
{
	struct player *player = g_Vars.currentplayer;
	struct hand *r, *l;
	s32 i;

	if (!vrReloadCanSwapHands()) {
		return false;
	}

	r = &player->hands[HAND_RIGHT];
	l = &player->hands[HAND_LEFT];

	/* The engine's half of each gun: the rounds it counts and the slide. The core state below
	 * goes with them, so vrrSyncFromEngineRow never reads one gun's count against the other's shape */
	for (i = 0; i < 2; i++) {
		VRR_SWAP(s32, r->loadedammo[i], l->loadedammo[i]);
	}

	VRR_SWAP(f32, r->slidetrans, l->slidetrans);
	VRR_SWAP(bool, r->slideinc, l->slideinc);

	/* Each gun's own state, live or restored by a draw and not yet applied */
	VRR_SWAP(struct vrrgun, sGun[HAND_RIGHT], sGun[HAND_LEFT]);
	for (i = 0; i < VRR_MAGS; i++) {
		VRR_SWAP(struct vrrgun, sPending[HAND_RIGHT][i], sPending[HAND_LEFT][i]);
		VRR_SWAP(s32, sPendingWeapon[HAND_RIGHT][i], sPendingWeapon[HAND_LEFT][i]);
	}

	VRR_SWAP(s32, sGunWeapon[HAND_RIGHT], sGunWeapon[HAND_LEFT]);
	VRR_SWAP(s32, sAmmoType[HAND_RIGHT], sAmmoType[HAND_LEFT]);
	VRR_SWAP(s32, sAmmoIndex[HAND_RIGHT], sAmmoIndex[HAND_LEFT]);
	VRR_SWAP(s32, sAllowFire[HAND_RIGHT], sAllowFire[HAND_LEFT]);
	VRR_SWAP(s32, sHandClip[HAND_RIGHT], sHandClip[HAND_LEFT]);
	VRR_SWAP(f32, sSlideGrab[HAND_RIGHT], sSlideGrab[HAND_LEFT]);
	VRR_SWAP(s32, sNoAmmoPulse[HAND_RIGHT], sNoAmmoPulse[HAND_LEFT]);
	VRR_SWAP(s32, sNoAmmoPulseFrame[HAND_RIGHT], sNoAmmoPulseFrame[HAND_LEFT]);
	VRR_SWAP(f32, sFinish[HAND_RIGHT], sFinish[HAND_LEFT]);
	VRR_SWAP(f32, sFinishStart[HAND_RIGHT], sFinishStart[HAND_LEFT]);
	VRR_SWAP(s32, sFinishFrame[HAND_RIGHT], sFinishFrame[HAND_LEFT]);
	VRR_SWAP(f32, sHolderOpen[HAND_RIGHT], sHolderOpen[HAND_LEFT]);
	VRR_SWAP(s32, sHolderFrame[HAND_RIGHT], sHolderFrame[HAND_LEFT]);
	VRR_SWAP(f32, sRackBack[HAND_RIGHT], sRackBack[HAND_LEFT]);
	VRR_SWAP(s32, sRackBackFrame[HAND_RIGHT], sRackBackFrame[HAND_LEFT]);
	VRR_SWAP(s32, sPumpRide[HAND_RIGHT], sPumpRide[HAND_LEFT]);
	VRR_SWAP(s32, sShellGrip[HAND_RIGHT], sShellGrip[HAND_LEFT]);
	/* The playback follows its gun, which plays it out in its new hand (the left gun's tick steps
	 * it, and the render poses it on that gun's drawn body, mirrored or not) */
	VRR_SWAP(f32, sPlayFrame[HAND_RIGHT], sPlayFrame[HAND_LEFT]);
	VRR_SWAP(s32, sPlayFrameNum[HAND_RIGHT], sPlayFrameNum[HAND_LEFT]);
	VRR_SWAP(s32, sPlayDrawn[HAND_RIGHT], sPlayDrawn[HAND_LEFT]);
	VRR_SWAP(s32, sPlayLogged[HAND_RIGHT], sPlayLogged[HAND_LEFT]);
	VRR_SWAP(s32, sPlayEject[HAND_RIGHT], sPlayEject[HAND_LEFT]);
	VRR_SWAP(s32, sPlayClose[HAND_RIGHT], sPlayClose[HAND_LEFT]);
	VRR_SWAP(s32, sCasingsDue[HAND_RIGHT], sCasingsDue[HAND_LEFT]);
	VRR_SWAP(s32, sCylinder[HAND_RIGHT], sCylinder[HAND_LEFT]);
	VRR_SWAP(s32, sCapped[HAND_RIGHT], sCapped[HAND_LEFT]);
	VRR_SWAP(s32, sInsert2At[HAND_RIGHT], sInsert2At[HAND_LEFT]);
	VRR_SWAP(s32, sInsert2Snd[HAND_RIGHT], sInsert2Snd[HAND_LEFT]);

	/* The free hand's grab, the gesture state and what the render captured are per engine hand
	 * and pose: dropped, and captured again from the next frame */
	for (i = 0; i < 2; i++) {
		sEject[i] = 0;
		sGuided[i] = 0;
		sProgress[i] = 0.0f;
		sRackGrab[i] = 0.0f;
		sMarkPending[i] = 0;
		sDrop[i].active = 0;
		sCasings[i] = 0;
		sParts[i].weaponnum = -1;
		sHeld[i].weaponnum = -1;
		sHeldMtx[i].weaponnum = -1;
		sClipWorld[i].weaponnum = -1;
		vrReloadSlapReset(i);
		/* The held wrist was aligned for the other gun side's free hand */
		sHoldWrist[i].weaponnum = -1;
		/* Read again for the gun now in this hand */
		sHookClip[i].weaponnum = -1;
		sPlayClipFrame[i] = -1;
		/* A hand-off under way was kept on the other gun's frame: the part goes to the playback's
		 * pose at once */
		sHandoff[i].left = 0.0f;
	}

	return true;
}

void vrReloadSideChanged(void)
{
	struct player *player = g_Vars.currentplayer;
	s32 h;

	if (player == NULL || g_Vars.currentplayernum != 0) {
		return;
	}

	for (h = 0; h < 2; h++) {
		if (sGunWeapon[h] < 0) {
			continue;
		}

		/* As vrReloadSuspend: an unfinished rack puts the slide back where the grab found it (a rack
		 * on a rackmtx moved none) */
		if (sGun[h].state == VRR_RACKING && !sGun[h].racked
				&& vrReloadRackIsSlide(vrReloadGetDef(sGunWeapon[h], sAmmoIndex[h]))) {
			player->hands[h].slidetrans = sSlideGrab[h];
		}

		/* A held clip is dropped: its rounds never left the reserve. The clip in the gun, the
		 * chamber and the counts stay. A grip still held by the new off-hand is not a grab. */
		vrReloadSettle(&sGun[h], vrReloadGetDef(sGunWeapon[h], sAmmoIndex[h]));
		sGun[h].prevgrip = 1;
		sAllowFire[h] = vrrAllowFire(&sGun[h]);
		sHandClip[h] = 0;
		sGuided[h] = 0;
		sRackGrab[h] = 0.0f;
		/* The pump's ride follows a hand that is no longer the free one: it springs forward */
		sPumpRide[h] = 0;
		sShellGrip[h] = 0;
		sDryPump[h] = 0;
		/* So does a support grip kept from the shot */
		sShotSupport[h] = 0;
		vrReloadSlapReset(h);
		/* The held wrist was aligned for the other gun side's free hand */
		sHoldWrist[h].weaponnum = -1;
		sRackEase[h] = 0.0f;
		sRackPull[h] = 0.0f;
		sGunBodyWeapon[h] = -1;
		sRackSlideWeapon[h] = -1;
		sGuidedClipWeapon[h] = -1;
	}
}

void vrReloadResetAll(void)
{
	s32 i;

	memset(sGun, 0, sizeof(sGun));
	memset(sRackGrab, 0, sizeof(sRackGrab));
	memset(sSlideGrab, 0, sizeof(sSlideGrab));
	memset(sPending, 0, sizeof(sPending));
	memset(sRackEase, 0, sizeof(sRackEase));
	memset(sRackPull, 0, sizeof(sRackPull));
	memset(sRackLastFrame, 0, sizeof(sRackLastFrame));
	for (i = 0; i < 2; i++) {
		sGunBodyWeapon[i] = -1;
		sRackSlideWeapon[i] = -1;
		sGuidedClipWeapon[i] = -1;
	}
	vrReloadFreeSaved();
	sHintLast = -100000;
	vrReloadHintReset();

	/* A row that loads on insert has no rack, and a row racked by a slap no pull, so its racktravel
	 * is unused */
	for (i = 0; i < vrReloadDefCount(); i++) {
		struct vrreloaddef *d = vrReloadDefAt(i);

		if (!d->loadsoninsert && !d->rackslap && d->racktravel < VRR_RACK_PULL_CM) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d racktravel %.1f is below the %.1f cm rack pull",
					i, d->racktravel, VRR_RACK_PULL_CM);
		}

		/* A slap needs a slap point with a radius and its frames in order; a row with no clip is closed
		 * instead (the DY357's cylinder): it needs the close, and a flick axis or a push (a point with a
		 * radius and a direction) */
		if (d->rackslap && vrReloadHasClip(d) && (d->slaprad <= 0.0f || d->slapfrom <= 0.0f
					|| d->slapcontact < d->slapfrom || d->slapto < d->slapcontact)) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d has rackslap but slaprad %.1f and frames %.0f, %.0f, %.0f: it is never slapped",
					i, d->slaprad, d->slapfrom, d->slapcontact, d->slapto);
		}

		if (d->rackslap && !vrReloadHasClip(d)) {
			s32 flick = d->flickaxis[0] != 0.0f || d->flickaxis[1] != 0.0f || d->flickaxis[2] != 0.0f;
			s32 push = d->slaprad > 0.0f && (d->closedir[0] != 0.0f || d->closedir[1] != 0.0f || d->closedir[2] != 0.0f);

			if (!vrReloadHasClose(d) || (!flick && !push)) {
				sysLogPrintf(LOG_WARNING, "vr_reload: row %d has rackslap and no clip part but no close (closefrom %.0f, closeto %.0f, ejecthold %d), no flick axis and no push: it is never closed",
						i, d->closefrom, d->closeto, d->ejecthold);
			}
		}

		/* The close's fields are used only by a row with one */
		if (d->closeto > 0.0f && !vrReloadHasClose(d)) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d has a close (%.0f to %.0f) but is not an intake row with rackslap, an eject playback held open (ejecthold) and 0 < closefrom < closeto: it never closes",
					i, d->closefrom, d->closeto);
		}

		if (vrReloadHasClose(d) && d->sndrack != 0 && (d->closesndat < d->closefrom || d->closesndat > d->closeto)) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d plays its close sound at frame %.0f, outside its close (%.0f to %.0f): it plays at the start or never",
					i, d->closesndat, d->closefrom, d->closeto);
		}

		/* A slap racks a gun with an empty chamber, which a row that loads on insert never has */
		if (d->rackslap && d->loadsoninsert) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d has rackslap and loadsoninsert: it loads on insert and is never slapped",
					i);
		}

		/* A row with no clip part loads at its intake point: it needs the point's frame, and it has no
		 * rack (the Phoenix has no slide), but for one loaded shell by shell (the Shotgun's port), whose
		 * tube stays in and whose pump racks it, and one closed after its load (the DY357's cylinder),
		 * whose close is its rack */
		if (!vrReloadHasClip(d) && (d->intakemtx < 0 || (!d->loadsoninsert && !d->shellbyshell && !vrReloadHasClose(d)))) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d has no clip part but intakemtx %d and loadsoninsert %d (shellbyshell %d, close to %.0f): it needs the intake and to load on insert, shell by shell or with a close",
					i, d->intakemtx, d->loadsoninsert, d->shellbyshell, d->closeto);
		}

		/* A gun loaded shell by shell takes one round at a time at an intake point: its held part is a
		 * round, not a clip, and it has no clip part to eject. A pump gun is racked after each shot,
		 * which a row that loads on insert or is racked by a slap is not */
		if (d->shellbyshell && (vrReloadHasClip(d) || d->intakemtx < 0)) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d is loaded shell by shell but has a clip part (%d) or no intake (intakemtx %d): its shells go nowhere",
					i, d->clippart, d->intakemtx);
		}

		if (d->pumpaction && (d->loadsoninsert || d->rackslap || d->rackmtx < 0)) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d is pumped after each shot but loads on insert (%d), is slapped (%d) or has no rack frame (rackmtx %d): it is never pumped",
					i, d->loadsoninsert, d->rackslap, d->rackmtx);
		}

		/* No clip part, no clip matrix: the clip's moves (vrReloadPlaceClip, the fall) use clipmtx */
		if (!vrReloadHasClip(d) && d->clipmtx >= 0) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d has no clip part but clipmtx %d: it should be -1",
					i, d->clipmtx);
		}

		/* The off-hand's keep list holds VR_RELOAD_HELD_MTX_MAX held matrices (vrLeftHandAndMagMtx) */
		if (d->heldmtxlast >= 0 && (d->heldmtxlast < d->heldmtx
					|| d->heldmtxlast - d->heldmtx + 1 > VR_RELOAD_HELD_MTX_MAX)) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d held matrices %d..%d: backwards or more than %d, so only %d is drawn",
					i, d->heldmtx, d->heldmtxlast, VR_RELOAD_HELD_MTX_MAX, d->heldmtx);
		}

		/* A hand turn needs an axis: without one (shorter than vrReloadRootTurn's 1e-6) the hand is
		 * left unturned */
		if (d->handturndeg != 0.0f
				&& sqrtf(d->handturnaxis[0] * d->handturnaxis[0] + d->handturnaxis[1] * d->handturnaxis[1]
					+ d->handturnaxis[2] * d->handturnaxis[2]) < 1e-6f) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d has a hand turn of %.1f degrees but no axis: the hand is not turned",
					i, d->handturndeg);
		}

		/* As for the clip turn */
		if (d->clipturndeg != 0.0f
				&& sqrtf(d->clipturnaxis[0] * d->clipturnaxis[0] + d->clipturnaxis[1] * d->clipturnaxis[1]
					+ d->clipturnaxis[2] * d->clipturnaxis[2]) < 1e-6f) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d has a clip turn of %.1f degrees but no axis: the clip is not turned",
					i, d->clipturndeg);
		}

		/* A clip shift moves the held matrices (vrReloadAlignHeldHand): a row with none is never moved */
		if ((d->clipshift[0] != 0.0f || d->clipshift[1] != 0.0f || d->clipshift[2] != 0.0f) && d->heldmtx < 0) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d has a clip shift {%.2f, %.2f, %.2f} but no held matrix (heldmtx %d): the clip is not moved",
					i, d->clipshift[0], d->clipshift[1], d->clipshift[2], d->heldmtx);
		}

		/* The held look moves the held matrices too (vrReloadPoseHeldLook) */
		if ((d->heldlookdeg != 0.0f || d->heldlookshift[0] != 0.0f || d->heldlookshift[1] != 0.0f
				|| d->heldlookshift[2] != 0.0f) && d->heldmtx < 0) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d has a held look (%.1f deg, {%.2f, %.2f, %.2f}) but no held matrix (heldmtx %d): the clip is not moved",
					i, d->heldlookdeg, d->heldlookshift[0], d->heldlookshift[1], d->heldlookshift[2], d->heldmtx);
		}

		/* A playback poses its matrices on the gun body (vrReloadHasPlay), or, for a row with a clip,
		 * plays its clip with the slap on bodymtx (vrReloadHasSlapPlay) */
		if (d->playto > 0.0f && !vrReloadHasPlay(d) && !vrReloadHasSlapPlay(d)) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d playback %.0f to %.0f of matrices %d..%d needs no clip part and an intakemtx (%d), or a clip part, a slap, a bodymtx and the clip matrix in the run, and 0 < playfrom < playto: it does not play",
					i, d->playfrom, d->playto, d->playmtx, d->playmtxlast, d->intakemtx);
		}

		/* The slap's playback starts with the hand's slap frames and steps with them */
		if (vrReloadHasSlapPlay(d) && (d->playfrom != d->slapfrom || (d->playmtx <= 32 && d->playmtxlast >= 1))) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d slap playback from %.0f (the slap from %.0f) of matrices %d..%d: it should start at slapfrom and pose no hand matrix",
					i, d->playfrom, d->slapfrom, d->playmtx, d->playmtxlast);
		}

		if (vrReloadHasPlay(d)) {
			s32 heldlast = d->heldmtxlast >= 0 ? d->heldmtxlast : d->heldmtx;

			/* The held part shows in the gun while the playback is below playheldto: at or before
			 * playfrom it never shows, past playto it shows to the end */
			if (d->playheldto <= d->playfrom || d->playheldto > d->playto) {
				sysLogPrintf(LOG_WARNING, "vr_reload: row %d playheldto %.0f is outside its playback (%.0f, %.0f]",
						i, d->playheldto, d->playfrom, d->playto);
			}

			/* A held matrix the playback does not pose draws the held part at the gun model's idle
			 * left wrist, about 75 cm from the Phoenix's body */
			if (d->heldmtx < d->playmtx || heldlast > d->playmtxlast) {
				sysLogPrintf(LOG_WARNING, "vr_reload: row %d held matrices %d..%d are not all in its playback's %d..%d: the held part would draw at the idle wrist",
						i, d->heldmtx, heldlast, d->playmtx, d->playmtxlast);
			}

			/* The hands (1 to 16 the right, 17 to 32 the left) stay as drawn, on the controllers */
			if (d->playmtx <= 32 && d->playmtxlast >= 1) {
				sysLogPrintf(LOG_WARNING, "vr_reload: row %d playback matrices %d..%d reach the hand matrices 1 to 32: the hands would leave the controllers",
						i, d->playmtx, d->playmtxlast);
			}
		}

		/* A held part falls after the playback from where it drew it, along heldaxis: only heldmtx is
		 * moved by the fall, so a part skinned to a run of matrices would tear */
		if (d->playdrop > 0.0f && (!vrReloadHasPlay(d) || d->heldmtxlast > d->heldmtx
					|| vrReloadHeldPartLast(d) != d->heldpart
					|| d->heldmtx < d->playmtx || d->heldmtx > d->playmtxlast
					|| d->heldaxis[0] * d->heldaxis[0] + d->heldaxis[1] * d->heldaxis[1]
						+ d->heldaxis[2] * d->heldaxis[2] < 0.25f)) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d drops its held part (playdrop %.1f) but has no playback, more than one held matrix (%d..%d) or part, a held matrix its playback (%d..%d) does not pose or no heldaxis: it does not fall as it should",
					i, d->playdrop, d->heldmtx, d->heldmtxlast, d->playmtx, d->playmtxlast);
		}

		/* The hand-off moves heldmtx alone, inside the insert's playback: a part skinned to a run of
		 * matrices would tear */
		if (d->playease > 0.0f && (!vrReloadHasPlay(d) || d->heldmtx < d->playmtx || d->heldmtx > d->playmtxlast
					|| (d->heldmtxlast >= 0 && d->heldmtxlast != d->heldmtx))) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d eases its held part (playease %.0f) but its playback does not pose heldmtx %d or the part has more held matrices (to %d): it jumps or tears",
					i, d->playease, d->heldmtx, d->heldmtxlast);
		}

		/* A clip seat is a proper turn (unit, perpendicular rows, determinant 1) on the gun body, for a
		 * row with a clip part */
		if (d->clipseat) {
			f32 det = d->seatrot[0][0] * (d->seatrot[1][1] * d->seatrot[2][2] - d->seatrot[1][2] * d->seatrot[2][1])
				- d->seatrot[0][1] * (d->seatrot[1][0] * d->seatrot[2][2] - d->seatrot[1][2] * d->seatrot[2][0])
				+ d->seatrot[0][2] * (d->seatrot[1][0] * d->seatrot[2][1] - d->seatrot[1][1] * d->seatrot[2][0]);
			f32 worst = 0.0f;
			s32 a, b;

			for (a = 0; a < 3; a++) {
				for (b = a; b < 3; b++) {
					f32 dot = d->seatrot[a][0] * d->seatrot[b][0] + d->seatrot[a][1] * d->seatrot[b][1]
						+ d->seatrot[a][2] * d->seatrot[b][2];
					f32 off = dot - (a == b ? 1.0f : 0.0f);

					if (off < 0.0f) {
						off = -off;
					}

					if (off > worst) {
						worst = off;
					}
				}
			}

			if (!vrReloadHasClip(d) || d->bodymtx < 0 || d->bodymtx == d->clipmtx || worst > 0.01f
					|| det < 0.99f || det > 1.01f) {
				sysLogPrintf(LOG_WARNING, "vr_reload: row %d has a clip seat but no clip part, no bodymtx (%d) or a seat that is not a turn (rows off unit and perpendicular by %.3f, det %.3f): it is not seated",
						i, d->bodymtx, worst, det);
			}

			/* A playback posing the clip's matrix would move it off the seat, but for the slap's, which
			 * ends on it */
			if (d->playto > 0.0f && d->clipmtx >= d->playmtx && d->clipmtx <= d->playmtxlast
					&& !vrReloadHasSlapPlay(d)) {
				sysLogPrintf(LOG_WARNING, "vr_reload: row %d seats its clip (matrix %d) but its insert playback poses matrices %d..%d: the playback would move it off the seat",
						i, d->clipmtx, d->playmtx, d->playmtxlast);
			}
		}

		/* An eject playback poses its matrices on its body (bodymtx, or an intake row's intakemtx),
		 * never the body, the clip or the hands */
		if (d->ejectto > 0.0f && (!vrReloadHasEjectPlay(d)
					|| (d->ejectmtx <= 32 && d->ejectmtxlast >= 1)
					|| (vrReloadPlayBody(d) >= d->ejectmtx && vrReloadPlayBody(d) <= d->ejectmtxlast)
					|| (d->clipmtx >= d->ejectmtx && d->clipmtx <= d->ejectmtxlast))) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d eject playback %.0f to %.0f of matrices %d..%d needs a body (bodymtx with a clip part, intakemtx without: %d) outside the run, 0 < ejectfrom < ejectto and no hand or clip matrix in the run: it does not play as it should",
					i, d->ejectfrom, d->ejectto, d->ejectmtx, d->ejectmtxlast, vrReloadPlayBody(d));
		}

		/* An eject playback that carries the clip out needs one, and a clip */
		if (d->ejectdrop && !vrReloadEjectDrops(d)) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d drops its clip at the end of its eject playback (ejectdrop) but has no clip part or no eject playback: the clip never falls",
					i);
		}

		/* The open pose held to the load is the eject playback's last frame */
		if (d->ejecthold && !vrReloadHasEjectPlay(d)) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d holds its eject playback's last frame (ejecthold) but has no eject playback: nothing is held open",
					i);
		}

		/* The casings fall at the end of the eject playback */
		if (d->ejectcasings && !vrReloadHasEjectPlay(d)) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d throws casings at the end of its eject playback (ejectcasings) but has none: no casing falls",
					i);
		}

		/* The engine throws them only for the DY357s (bgunUpdateMagnum, from their chamber parts) */
		if (d->ejectcasings && d->weaponnum != WEAPON_DY357MAGNUM && d->weaponnum != WEAPON_DY357LX) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d (weapon %d) throws casings (ejectcasings), which the engine does only for the DY357s: no casing falls",
					i, d->weaponnum);
		}

		/* The held part goes in straight on a matrix of the insert's playback other than heldmtx, and
		 * moves as heldmtx alone (vrReloadPlayStraight) */
		if (d->playstraightmtx != 0 && (!vrReloadHasPlay(d) || d->playstraightmtx < d->playmtx
					|| d->playstraightmtx > d->playmtxlast || d->playstraightmtx == d->heldmtx
					|| d->heldmtx < d->playmtx || d->heldmtx > d->playmtxlast
					|| (d->heldmtxlast >= 0 && d->heldmtxlast != d->heldmtx))) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d puts its held part straight on matrix %d (playstraightmtx) but its playback (%d..%d) does not pose both it and heldmtx %d, or the part has more held matrices (to %d): it goes in as the animation has it",
					i, d->playstraightmtx, d->playmtx, d->playmtxlast, d->heldmtx, d->heldmtxlast);
		}

		/* The eject after the shot is B's, which a gun loaded shell by shell ignores */
		if (d->emptyejects && d->shellbyshell) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d ejects when emptied (emptyejects) but is loaded shell by shell: it never ejects",
					i);
		}

		/* The loaded round kept seen is the insert's playback's held part at its last frame */
		if (d->playhold && !vrReloadHoldsPlay(d)) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d keeps its held part seen (playhold) but has no insert playback: it is never drawn in the gun",
					i);
		}

		{
			s32 k;

			for (k = 0; k < VR_RELOAD_EJECT_SND_MAX; k++) {
				if (d->ejectsnd[k] != 0 && (d->ejectto <= 0.0f || d->ejectsndat[k] > d->ejectto)) {
					sysLogPrintf(LOG_WARNING, "vr_reload: row %d eject sound %d at frame %.0f is outside its eject playback (to %.0f): it never plays",
							i, d->ejectsnd[k], d->ejectsndat[k], d->ejectto);
				}
			}

			for (k = 0; k < VR_RELOAD_PLAY_SND_MAX; k++) {
				if (d->playsnd[k] != 0 && (!vrReloadHasPlay(d) || d->playsndat[k] > d->playto)) {
					sysLogPrintf(LOG_WARNING, "vr_reload: row %d insert playback sound %d at frame %.0f is outside its insert playback (to %.0f): it never plays",
							i, d->playsnd[k], d->playsndat[k], d->playto);
				}
			}
		}

		/* A body matrix is used only by a clip seat and an eject playback */
		if (d->bodymtx >= 0 && !d->clipseat && d->ejectto <= 0.0f) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d has a bodymtx (%d) but no clip seat and no eject playback: it is never used",
					i, d->bodymtx);
		}

		/* A rack frame on a row with no rack, or racked by a slap, is never used */
		if (d->rackmtx >= 0 && (d->loadsoninsert || d->rackslap)) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d has a rack frame (matrix %d) but loads on insert or is racked by a slap: it is never grabbed",
					i, d->rackmtx);
		}

		/* A moving rack matrix needs a rack on a rackmtx: the slide part moves with the engine's
		 * slide offset already */
		if (d->rackmove && (d->rackmtx < 0 || d->loadsoninsert || d->rackslap)) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d moves its rack matrix but has no rackmtx (%d) or no grip-and-pull rack: nothing moves",
					i, d->rackmtx);
		}

		/* A linked part moves with a moving rack matrix, by a share of its pull */
		if (d->rackfollowmtx >= 0 && (!d->rackmove || d->rackfollowmtx == d->rackmtx || d->rackfollowscale <= 0.0f)) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d has a linked rack part (matrix %d, scale %.2f) but no moving rack matrix (rackmove %d, rackmtx %d) or no share of the pull: it does not move",
					i, d->rackfollowmtx, d->rackfollowscale, d->rackmove, d->rackmtx);
		}

		/* Rows are keyed by weapon and ammo slot (loadedammo's two): a row past them is never driven,
		 * and of two rows for one magazine only the first is */
		if (d->ammoindex < 0 || d->ammoindex >= VRR_MAGS) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d ammoindex %d is not an ammo slot (0..%d): it is never driven",
					i, d->ammoindex, VRR_MAGS - 1);
		} else if (vrReloadGetDef(d->weaponnum, d->ammoindex) != d) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d repeats weapon %d's ammoindex %d: it is never driven",
					i, d->weaponnum, d->ammoindex);
		}

		/* A gun's first row is the one a function with no ammo, or none with a row, goes with
		 * (vrReloadGetDef(weapon, -1)): its primary magazine's, ammoindex 0 */
		if (d->ammoindex == 0 && vrReloadGetDef(d->weaponnum, -1) != d) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d (weapon %d, ammoindex 0) is not the gun's first row: list it first",
					i, d->weaponnum);
		}

		/* The holder's close plays sndrack, which only a row with no rack leaves free */
		if (d->holdermtx >= 0 && !d->loadsoninsert) {
			sysLogPrintf(LOG_WARNING, "vr_reload: row %d has a holder (matrix %d) but needs a rack: its close would play the rack sound",
					i, d->holdermtx);
		}
	}

	/* The first-reload hint marks are per row (sHintSeen): a row past them would show it each time */
	if (vrReloadDefCount() > VRR_HINT_ROWS) {
		sysLogPrintf(LOG_WARNING, "vr_reload: %d reload rows but %d first-reload hint marks; raise VRR_HINT_ROWS",
				vrReloadDefCount(), VRR_HINT_ROWS);
	}

	/* One slot per row and engine hand (each magazine of a gun with two rows saves its own): the
	 * second copy of a pair saves its own */
	if (vrReloadDefCount() * 2 > VRR_NUM_SAVED) {
		sysLogPrintf(LOG_WARNING, "vr_reload: %d reload rows need %d saved-state slots but there are %d; raise VRR_NUM_SAVED",
				vrReloadDefCount(), vrReloadDefCount() * 2, VRR_NUM_SAVED);
	}

	for (i = 0; i < 2; i++) {
		sGunWeapon[i] = -1;
		sAmmoType[i] = -1;
		sAmmoIndex[i] = -1;
		vrReloadDropPending(i);
		sEject[i] = 0;
		sAllowFire[i] = 1;
		sHandClip[i] = 0;
		sNoAmmoPulse[i] = 0;
		sNoAmmoPulseFrame[i] = 0;
		sParts[i].weaponnum = -1;
		sHeld[i].weaponnum = -1;
		sGuided[i] = 0;
		sProgress[i] = 0.0f;
		sFinish[i] = 0.0f;
		sFinishStart[i] = 0.0f;
		sFinishFrame[i] = 0;
		sHolderOpen[i] = 0.0f;
		sHolderFrame[i] = 0;
		sRackBack[i] = 0.0f;
		sRackBackFrame[i] = 0;
		sPumpRide[i] = 0;
		sShellGrip[i] = 0;
		sDryPump[i] = 0;
		sShotSupport[i] = 0;
		sHoldPose[i].weaponnum = -1;
		sPlayFrame[i] = 0.0f;
		sPlayFrameNum[i] = 0;
		sPlayDrawn[i] = 0;
		sPlayLogged[i] = 0;
		sPlayEject[i] = 0;
		sPlayClose[i] = 0;
		sCasingsDue[i] = 0;
		sCasings[i] = 0;
		sCylinder[i] = -1;
		sCapped[i] = 0;
		sPlayHeldFrame[i] = -1;
		sHandoff[i].left = 0.0f;
		sHeldMtx[i].weaponnum = -1;
		sInsert2At[i] = -1;
		vrReloadSlapReset(i);
		sSlapClipWeapon[i] = -1;
		sSlapContact[i].weaponnum = -1;
		sSlapContact[i].valid = 0;
		sHookClip[i].weaponnum = -1;
		sHookClip[i].valid = 0;
		sPlayClipFrame[i] = -1;
		sHoldWrist[i].weaponnum = -1;
		sClipWorld[i].weaponnum = -1;
		sDrop[i].active = 0;
		sFistWristOk[i] = 0;
		sFistPalmOk[i] = 0;
		sFistArmOk[i] = 0;
		sFistWristLoggedOk[i] = 0;
		sEjectSince[i] = -1;
		sEjectWaited[i] = 0;
	}

	sAlignFrame = -2;

	sPlayerFrame = -1;
	memset(sPlayerVel, 0, sizeof(sPlayerVel));
}
