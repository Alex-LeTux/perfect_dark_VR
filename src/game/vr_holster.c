/* Engine adapter for the VR body holsters. See vr_holster.h. */
#include <ultra64.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../../port/vr/vr_input.h"
#include "../../port/vr/vr_openxr.h"
#include "input.h"
#include "constants.h"
/* The port headers above bring stdbool.h; the engine's types.h defines bool as s32, which
 * replaced it anyway: undefining first only drops the redefinition warning */
#undef bool
#include "game/activemenu.h"
#include "game/bondgun.h"
#include "game/game_0b0fd0.h"
#include "game/hudmsg.h"
#include "game/inv.h"
#include "game/lv.h"
#include "game/vr_holster.h"
#include "game/vr_holster_core.h"
#include "game/vr_reload.h"
#include "lib/main.h"
#include "lib/snd.h"
#include "bss.h"
#include "types.h"

/* The mark tool's holster line goes under the reload's PD-VRR tag */
#ifdef ANDROID
#include <android/log.h>
#define VRH_DIAG(...) __android_log_print(ANDROID_LOG_INFO, "PD-VRR", __VA_ARGS__)
#else
/* Takes the arguments, so the log's locals are used when it compiles to nothing */
static inline void vrhDiagNop(const char *fmt, ...)
{
	(void) fmt;
}

#define VRH_DIAG(...) vrhDiagNop(__VA_ARGS__)
#endif

extern bool VrManualReloading;
extern int VrLeftHandedMode;
extern int VrLeftHandedPref;
extern int VrCtrlPoseMode;
extern bool vr_R_knife_sec_anim_run; /* bondgun.c: the knife's VR throw wind-up (in HANDSTATE_IDLE) */

/* Zones in the body frame (vrBodyPoint: cm, x right, y up, z back, from the head; x mirrored by
 * the left-handed setting): capsules, a segment from (_X _Y _Z) to (_X2 _Y2 _Z2) and a radius, and
 * the shoulder a point. The hip started as the smallest sphere around the three boundary marks the
 * user took in the headset at checkpoint A (left hand at (-16.8 -49.6 2.8), (-30.2 -60.7 22.5),
 * (-36.2 -61.0 22.2)), plus about 1 cm; at checkpoint C run 1 (2026-10-17) the user marked the
 * hip further forward and lower, at (-29.1 -67.8 -5.8), so the hip runs from that sphere's centre
 * to the mark. The right hip is its mirror. The chest ran from its first guess (0 -25 -10) down to
 * the average of the three chest marks of run 1, (-0.7 -36.5 -4.2), (-2.1 -42.9 -7.1) and
 * (-0.3 -42.7 -9.6), with x set to 0 for the left-handed mirror; at run 2 it reached too far
 * forward: the user marked (-4.2 -41.2 -17.4), inside but should not be, and (-1.1 -36.9 -14.5)
 * as where it should end (11.1 and 6.7 cm from that segment), so the segment moved 5 cm back (those
 * marks now 15.8 and 11.6 cm away; the run 1 marks 1.5 to 7.9 cm, inside). The shoulder is the
 * mirror of the reload's proven left-shoulder clip source (VRR_SHOULDER_*). Between the segments
 * the chest is 33.6 cm from either hip (radii 28): no overlap; it overlaps the shoulder (23.1 cm,
 * radii 32), where the nearest zone wins. On 2026-10-22 the user kept missing the hip on the body's
 * side and marked (-16.7 -49.6 -2.2) as inside (17.0 cm from the segment): the hip moved 5 cm in
 * toward the body, the direction stated (the mark now 14.3 cm away, inside), and the chest was
 * narrowed side to side (VRH_CHEST_XSCALE) to make up for it; the hip and the chest segments are
 * now 29.8 cm apart (radii 16 and 10.5 across). The right hip moved with it, as its mirror. Since
 * Task J4 the off-hand shoulder (VRH_LSHOULDER_*) is a zone too: the shoulder's mirror, the same
 * point as the reload's clip source when the gun is in the setting's gun hand (vrHolsterBuildInput
 * blanks it while the gun can take ammo there). The names below are those of the right-handed
 * setting; which slot each zone holds is the layout's (VrHolsterLayout, core rule 49). */
#define VRH_HIP_X      -21.5f
#define VRH_HIP_Y      -55.5f
#define VRH_HIP_Z       12.5f
#define VRH_HIP_X2     -24.1f
#define VRH_HIP_Y2     -67.8f
#define VRH_HIP_Z2      -5.8f
#define VRH_HIP_RAD     16.0f
#define VRH_SHOULDER_X  14.0f
#define VRH_SHOULDER_Y -12.0f
#define VRH_SHOULDER_Z   8.0f
#define VRH_SHOULDER_RAD 20.0f /* the reload's shoulder radius, confirmed in the headset */
#define VRH_CHEST_X      0.0f /* the chest pouch */
#define VRH_CHEST_Y    -25.0f
#define VRH_CHEST_Z     -5.0f
#define VRH_CHEST_X2     0.0f
#define VRH_CHEST_Y2   -40.7f
#define VRH_CHEST_Z2    -2.0f
#define VRH_CHEST_RAD   12.0f
#define VRH_CHEST_XSCALE (12.0f / 10.5f) /* the chest's x distance is scaled: 10.5 cm across, 12 up and back */
#define VRH_RHIP_X      21.5f /* the right hip (throwables): the hip's mirror, so with the x mirror */
#define VRH_RHIP_Y     -55.5f /* of vrBodyPoint it is the hip on the gun-hand side of the setting */
#define VRH_RHIP_Z      12.5f
#define VRH_RHIP_X2     24.1f
#define VRH_RHIP_Y2    -67.8f
#define VRH_RHIP_Z2     -5.8f
#define VRH_RHIP_RAD    16.0f
#define VRH_LSHOULDER_X  (-VRH_SHOULDER_X) /* the off-hand shoulder: the shoulder's mirror */
#define VRH_LSHOULDER_Y  VRH_SHOULDER_Y
#define VRH_LSHOULDER_Z  VRH_SHOULDER_Z
#define VRH_LSHOULDER_RAD VRH_SHOULDER_RAD

/* The holster layout (pd-vr.ini HolsterLayout, the Holster Layout page): the slot of each zone in
 * zone order (hip, shoulder, chest, right hip, left shoulder), relative to the gun hand of the
 * setting as the zones are. The core's default (rule 51): gun 1 on the gun-hand shoulder, gun 2 on
 * the other shoulder, gun 3 on the off-hand hip. */
s32 VrHolsterLayout[VRH_NUM_ZONES - 1] = { VRH_GUN3, VRH_GUN1, VRH_POUCH, VRH_THROW, VRH_GUN2 };

/* Haptic pulses per physical hand: amplitude 0..1 (scaled by the rumble strength setting),
 * duration in seconds. Swap and empty are two pulses, VRH_PULSE_GAP60 apart. */
#define VRH_BUZZ_DRAW_AMP     0.6f
#define VRH_BUZZ_DRAW_SECS    0.05f
#define VRH_BUZZ_HOLSTER_AMP  0.4f
#define VRH_BUZZ_HOLSTER_SECS 0.04f
#define VRH_BUZZ_SWAP_AMP     0.6f
#define VRH_BUZZ_SWAP_SECS    0.04f
#define VRH_BUZZ_EMPTY_AMP    0.3f
#define VRH_BUZZ_EMPTY_SECS   0.03f
#define VRH_BUZZ_ZONE_AMP     0.25f /* as the reload's zone buzz */
#define VRH_BUZZ_ZONE_SECS    0.03f
#define VRH_PULSE_GAP60       5     /* in 60ths: one tick would merge the two pulses */

static struct vrhstate sState;
static s32 sInitPending = 1;   /* fill the holsters at the next entry point (vrHolsterEnsureFilled) */
static s32 sForceApply;        /* the next tick equips the core's weapon even if unchanged */
static s32 sWasOn;             /* holsters (VrManualReloading) seen on since the tick last saw them off */
static s32 sPausePending;      /* the game paused since the last tick */

static s32 vrHolsterFullMenuOpen(void);
static s32 sPref;              /* VrLeftHandedPref at the last tick */
static s32 sLastWeapon;        /* the core's output last equipped */
static s32 sLastDual;
static s32 sModeSeen;          /* VrLeftHandedMode as the adapter last set or saw it */
static s32 sYFlipPending;      /* a Y press on the left gun of a pair, for the next tick */
static s32 sYFlipFrame;        /* g_Vars.lvframenum of that press */

/* A Y press older than this many frames is dropped: the gun ticks stopped (a cutscene, the eyespy) */
#define VRH_YFLIP_MAX_AGE 3

/* The lower face buttons (A, X: vrHolsterFaceButtons), per physical hand: the down time in 60ths,
 * 0 up, -1 acted or cancelled (waits for the release). The hand that opened the weapon menu, -1
 * when none is open, and its zone (after rules 18 and 52), or outside the five zones the zone of
 * the slot whose item that hand holds. */
static s32 sFaceDown[2];
static s32 sFaceFrame = -10;   /* g_Vars.lvframenum of the last call */
static s32 sMenuHand = -1;
static s32 sMenuZone;

/* Per physical hand: the zone blanked for the core this tick (VRH_ZONE_NONE: none): the shoulder
 * because the gun can take ammo there (vrReloadFreeHandAtAmmo), or the gun side's own holster while
 * its gun is mid-reload (Task H3d); and the zone the face buttons read (vrHolsterButtonZone): the
 * core's, or the zone the core did not see. The blank is for the core's grip press only: an A or X
 * tap there still steps the slot or opens its menu (Task F11g review) */
static s32 sZoneBlank[2];
static s32 sButtonZone[2];

/* The grab (vrHolsterGrabEdge): each physical hand's grip last call, and the hand whose press
 * asked for the grab, -1 none */
static s32 sGrabPrev[2] = { 1, 1 };
static s32 sGrabHand = -1;
static s32 sCrateHintShown;    /* the grab hint was shown this level (vrHolsterCrateHint) */
static s32 sHudId = -1;         /* the holster's last HUD message (vrHolsterHud), -1 none */

/* The grab hint looks for a grabbable object every this many frames */
#define VRH_CRATE_HINT_FRAMES 4

static s32 sPulse2[2];         /* per physical hand: a second pulse is due */
static s32 sPulse2Frame[2];    /* g_Vars.lvframe60 it is due at */
static f32 sPulse2Amp[2];
static f32 sPulse2Secs[2];

/* The swing hold-back (vrHolsterSwingSettling): lvframe60 of the last switch request or raise, and
 * the window after it in 60ths */
#define VRH_SWING_SETTLE60 30
static s32 sSettleFrom60 = -100000;

/* The PD-VRR switch line (vrHolsterNoteFrame): the weapon last asked of the engine, -1 when its
 * line is out, and the frame (lvframenum) and 60ths (lvframe60) it was asked at. The commit, the
 * load's end and the draw sound in frames after that (-1: not yet, or no draw sound), and the
 * synchronous load's time (-1: none). Then the follow-up lines: what each hand draws, and the
 * triangle counts the frame after. */
static s32 sDiagWeapon = -1;
static s32 sDiagFrame;
static s32 sDiag60;
static s32 sDiagCommit;
static s32 sDiagLoaded;
static s32 sDiagSound;
static s32 sDiagLoadUs;
static s32 sDiagModelsAt = -1; /* lvframenum of the follow-up line of what each hand draws, -1: none */
static s32 sDiagModelsWeapon;
static const char *sDiagModelsLabel = "holster switch"; /* what the models line follows */
static s32 sDiagTrisAt = -1;   /* lvframenum of the triangle count line (the frame after the dump) */

/* The PD-VRR lines after a drawn switch: its weapon (-1 when none is watched), the frame it was
 * drawn and whether a held-back swing was logged */
static s32 sShotWeapon = -1;
static s32 sShotFrom;
static s32 sShotFrom60;
static s32 sShotHeldLogged;

bool vrHolsterFastSwitch(void)
{
	return vrHolsterActive();
}

bool vrHolsterActive(void)
{
	/* Only the VR player draws from holsters */
	return VrManualReloading && g_Vars.currentplayernum == 0 && g_Vars.currentplayer != NULL;
}

bool vrHolsterIsGun(s32 weaponnum)
{
	return weaponnum > WEAPON_UNARMED && weaponnum <= WEAPON_PSYCHOSISGUN
		&& weaponnum != WEAPON_COMBATBOOST
		&& !weaponHasFlag(weaponnum, WEAPONFLAG_THROWABLE);
}

/* The inventory holds the gun: as a single, or only as a pair (a stage can start with a pair) */
static s32 vrHolsterOwns(s32 weaponnum)
{
	return weaponnum != WEAPON_NONE
		&& (invHasSingleWeaponIncAllGuns(weaponnum) || invHasDoubleWeaponIncAllGuns(weaponnum, weaponnum));
}

/* G5 Building gives the player a remote mine with no rounds, renamed "Detonator" (setupdepo.c), to
 * set off the two mines the level places on the fire escape doors. On that stage only, the remote
 * mine is a gadget (the chest pouch) that a 0 count does not take away, and its trigger detonates
 * (vrHolsterDeviceTrigger); drawn, it is the detonator alone at the hand (bondgun.c). Everywhere else,
 * and with holsters off, it stays a throwable that leaves its slot at 0 rounds. */
bool vrHolsterIsG5Detonator(s32 weaponnum)
{
	return weaponnum == WEAPON_REMOTEMINE && mainGetStageNum() == STAGE_G5BUILDING && vrHolsterActive();
}

s32 vrHolsterSlotOf(s32 weaponnum)
{
	if (vrHolsterIsGun(weaponnum)) {
		return VRH_GUN1;
	}

	if (vrHolsterIsG5Detonator(weaponnum)) {
		return VRH_POUCH;
	}

	switch (weaponnum) {
	case WEAPON_DATAUPLINK:
	case WEAPON_DOORDECODER:
	case WEAPON_EXPLOSIVES:
	case WEAPON_SKEDARBOMB:
	case WEAPON_SUITCASE:
	case WEAPON_AUTOSURGEON:
	case WEAPON_DISGUISE40:
	case WEAPON_DISGUISE41:
	case WEAPON_COMBATBOOST:
	case WEAPON_HORIZONSCANNER:
	case WEAPON_ECMMINE: /* thrown, but a mission gadget: the chest (user, checkpoint C run 2) */
	case WEAPON_COMMSRIDER: /* the same, held at the hand as the door decoder (g_VrHeldFits) */
		return VRH_POUCH;
	case WEAPON_NIGHTVISION: /* the devices (vrHolsterIsDevice): held, the trigger toggles them */
	case WEAPON_XRAYSCANNER:
	case WEAPON_IRSCANNER:
	case WEAPON_RTRACKER:
	case WEAPON_CLOAKINGDEVICE:
	case WEAPON_EYESPY:
		return VRH_POUCH;
	case WEAPON_TRACERBUG:
	case WEAPON_TARGETAMPLIFIER:
		return VRH_THROW;
	}

	if (weaponnum > WEAPON_UNARMED && weaponnum <= WEAPON_SUICIDEPILL
			&& weaponHasFlag(weaponnum, WEAPONFLAG_THROWABLE)) {
		return VRH_THROW;
	}

	return -1;
}

/* Slot i takes weaponnum: a gun for any gun holster, else the slot's own kind */
static s32 vrHolsterFitsSlot(s32 weaponnum, s32 i)
{
	s32 kind = vrHolsterSlotOf(weaponnum);

	return kind >= 0 && kind == (i < VRH_NUM_GUNS ? VRH_GUN1 : i);
}

/* The inventory lists weaponnum (the menus' list: invGetWeaponNumByIndex, props with a weapon's
 * text override included) */
static s32 vrHolsterInvLists(s32 weaponnum)
{
	s32 n = invGetCount();
	s32 i;

	for (i = 0; i < n; i++) {
		if (invGetWeaponNumByIndex(i) == weaponnum) {
			return 1;
		}
	}

	return 0;
}

/* The inventory has weaponnum and it can be used: a gun as vrHolsterOwns; a gadget or throwable
 * listed in the inventory, except the timed, proximity and remote mines at 0 rounds (the engine
 * keeps those in the inventory, but the hand is empty after the last throw: fists detonate remote
 * mines; G5 Building's Detonator is the exception: vrHolsterIsG5Detonator). The round in the hand
 * counts (AMMOFLAG_EQUIPPEDISRESERVE: bgunGetAmmoCount adds the hands' loaded rounds to the
 * reserve, as the used-up check in bgunTickGameplay does). For the fill
 * and sync of the item slots, the A/X steps and the hold-A/X menus. */
static s32 vrHolsterUsable(s32 weaponnum)
{
	struct weapon *weapon;

	if (vrHolsterIsGun(weaponnum)) {
		return vrHolsterOwns(weaponnum);
	}

	if (weaponnum == WEAPON_NONE || !vrHolsterInvLists(weaponnum)) {
		return 0;
	}

	if ((weaponnum == WEAPON_TIMEDMINE || weaponnum == WEAPON_PROXIMITYMINE || weaponnum == WEAPON_REMOTEMINE)
			&& !vrHolsterIsG5Detonator(weaponnum)) {
		weapon = weaponFindById(weaponnum);

		if (weapon && weapon->ammos[0] && bgunGetAmmoCount(weapon->ammos[0]->type) <= 0) {
			return 0;
		}
	}

	return 1;
}

static s32 vrHolsterPrefSide(void)
{
	return VrLeftHandedPref ? VRH_L : VRH_R;
}

/* A gun side change remaps the face buttons, and one held through it reads as up from the next
 * poll (inputVrFaceButton). That drop must not count as a release: a B or A whose down timer runs
 * is cancelled as the mine detonation does (no activate, eject or cycle on release). A hold that
 * already acted (the function toggle, the weapon menu) has a timer of -1 or less and is left. */
static void vrHolsterCancelTaps(void)
{
	struct player *player = g_Vars.currentplayer;

	if (!VrManualReloading || player == NULL || g_Vars.currentplayernum != 0) {
		return;
	}

	if (player->usedowntime > 0) {
		player->usedowntime = -2;
	}

	if (player->invdowntime > 0) {
		player->invdowntime = -2;
	}

	/* The A and X timers (vrHolsterFaceButtons): the same for a tap still counting */
	if (sFaceDown[VRH_L] > 0) {
		sFaceDown[VRH_L] = -1;
	}

	if (sFaceDown[VRH_R] > 0) {
		sFaceDown[VRH_R] = -1;
	}
}

/* Every gun side change goes through here, so the reload's grabs end with it */
static void vrHolsterSetMode(s32 left)
{
	VrLeftHandedMode = left ? 1 : 0;

	if (VrLeftHandedMode != sModeSeen) {
		sModeSeen = VrLeftHandedMode;
		vrReloadSideChanged();
		bgunFistReset(); /* the fists' swing history is per controller */
		vrHolsterCancelTaps();
	}
}

/* Logical controller index for physical hand p (0 left, 1 right) in get_button_state and the
 * haptics, which flip their index by the gun side at the call */
static s32 vrHolsterLogical(s32 p)
{
	return p ^ (VrLeftHandedMode ? 1 : 0);
}

/* Index of physical hand p in gCtrlPos and vrCtrlPoseGood: the logical controller as the last
 * controller_pose mapped it (VrCtrlPoseMode), which differs from VrLeftHandedMode for the rest of
 * a frame in which a draw flipped the gun side */
static s32 vrHolsterPoseIndex(s32 p)
{
	return p ^ (VrCtrlPoseMode ? 1 : 0);
}

/* PD-VRR name of a slot */
static const char *vrHolsterSlotName(s32 i)
{
	return i == VRH_GUN1 ? "gun 1" : i == VRH_GUN2 ? "gun 2" : i == VRH_GUN3 ? "gun 3"
		: i == VRH_POUCH ? "gadgets" : i == VRH_THROW ? "throwables" : "none";
}

/* A zone by its physical side (HUD and PD-VRR): the zones are the setting's gun hand's (vrBodyPoint
 * mirrors them), so left-handed the off-hand side is the right */
static const char *vrHolsterZoneName(s32 zone)
{
	s32 lh = VrLeftHandedPref != 0;

	switch (zone) {
	case VRH_ZONE_HIP:
		return lh ? "right hip" : "left hip";
	case VRH_ZONE_SHOULDER:
		return lh ? "left shoulder" : "right shoulder";
	case VRH_ZONE_CHEST:
		return "chest";
	case VRH_ZONE_RHIP:
		return lh ? "left hip" : "right hip";
	case VRH_ZONE_LSHOULDER:
		return lh ? "right shoulder" : "left shoulder";
	}

	return "none";
}

/* The zone of a slot in the layout (core rule 49), VRH_ZONE_NONE for none */
static s32 vrHolsterSlotZone(s32 i)
{
	s32 z;

	for (z = VRH_ZONE_NONE + 1; z < VRH_NUM_ZONES; z++) {
		if (sState.layout[z] == i) {
			return z;
		}
	}

	return VRH_ZONE_NONE;
}

/* The slot of a zone in the layout, -1 for none */
static s32 vrHolsterZoneSlot(s32 zone)
{
	return zone > VRH_ZONE_NONE && zone < VRH_NUM_ZONES ? sState.layout[zone] : -1;
}

/* The core gets the setting's layout (after every vrhInit, and when the Holster Layout page changes
 * it). A layout that is not a permutation of the slots is refused there, and the core keeps its
 * own. */
static void vrHolsterApplyLayout(const char *why)
{
	if (vrhSetLayout(&sState, VrHolsterLayout)) {
		VRH_DIAG("holster layout: %s: hip %d shoulder %d chest %d rhip %d lshoulder %d", why,
				sState.layout[VRH_ZONE_HIP], sState.layout[VRH_ZONE_SHOULDER], sState.layout[VRH_ZONE_CHEST],
				sState.layout[VRH_ZONE_RHIP], sState.layout[VRH_ZONE_LSHOULDER]);
	}
}

/* An empty item slot gets the first usable item of its kind in inventory order (core rule 47). why
 * names the caller for PD-VRR. */
static void vrHolsterFillItems(const char *why)
{
	s32 n = invGetCount();
	s32 i, j, w;

	for (i = VRH_POUCH; i <= VRH_THROW; i++) {
		if (sState.holster[i] != WEAPON_NONE) {
			continue;
		}

		for (j = 0; j < n; j++) {
			w = invGetWeaponNumByIndex(j);

			if (vrHolsterFitsSlot(w, i) && vrHolsterUsable(w)) {
				if (vrhSetItem(&sState, i, w)) {
					VRH_DIAG("holster item: %s: %s gets weapon %d", why, vrHolsterSlotName(i), w);
				}

				break;
			}
		}
	}
}

/* The starting weapon in gun 1 (else the lowest-numbered gun), the next two guns after it by weapon
 * number (wrapping) in gun 2 and gun 3 (core rule 53's fill order), the first gadget and throwable
 * in inventory order in their slots. Hands empty, the gun side as the setting, the setting's layout
 * (rule 51: vrhInit gives the default). */
static void vrHolsterFill(void)
{
	s32 guns[VRH_NUM_GUNS] = { WEAPON_NONE, WEAPON_NONE, WEAPON_NONE };
	s32 count = WEAPON_PSYCHOSISGUN - WEAPON_FALCON2 + 1;
	s32 i, k, w;

	if (vrHolsterIsGun(g_DefaultWeapons[HAND_RIGHT]) && vrHolsterOwns(g_DefaultWeapons[HAND_RIGHT])) {
		guns[0] = g_DefaultWeapons[HAND_RIGHT];
	} else {
		for (w = WEAPON_FALCON2; w <= WEAPON_PSYCHOSISGUN; w++) {
			if (vrHolsterIsGun(w) && vrHolsterOwns(w)) {
				guns[0] = w;
				break;
			}
		}
	}

	if (guns[0] != WEAPON_NONE) {
		for (i = 1, k = 1; i < count && k < VRH_NUM_GUNS; i++) {
			w = WEAPON_FALCON2 + (guns[0] - WEAPON_FALCON2 + i) % count;

			if (vrHolsterIsGun(w) && vrHolsterOwns(w)) {
				guns[k++] = w;
			}
		}
	}

	vrhInit(&sState, guns[0], guns[1], vrHolsterPrefSide());
	vrhOnPickup(&sState, guns[2]);
	vrHolsterApplyLayout("fill");
	vrHolsterFillItems("fill");
	vrHolsterSetMode(VrLeftHandedPref);
	sPref = VrLeftHandedPref;
	sLastWeapon = 0;
	sLastDual = 0;
	sForceApply = 1;
	sInitPending = 0;
}

/* Every entry point (tick, menu pick, pickup, engine switch) fills first when the holsters were
 * turned on, or not filled since the last reset, so a pick or pickup applies on top of the fill */
static void vrHolsterEnsureFilled(void)
{
	if (!sWasOn) {
		sWasOn = 1;
		sInitPending = 1;
	}

	if (sInitPending) {
		vrHolsterFill();
	}
}

/* PD-VRR: arms the models line of what each hand draws, a few frames from now (vrHolsterNoteFrame),
 * after the line tagged label. The park, switch and single lines share this one pending line: one
 * still pending is replaced, and a short note says which. */
static void vrHolsterArmModels(s32 weapon, const char *label)
{
	if (sDiagModelsAt >= 0) {
		VRH_DIAG("%s: models line replaced by %s", sDiagModelsLabel, label);
	}

	sDiagModelsAt = g_Vars.lvframenum + 3;
	sDiagModelsWeapon = weapon;
	sDiagModelsLabel = label;
}

/* PD-VRR: a park or unpark (rule 9) by the core call named why, against the parked state old,
 * then the park's models line of what each hand draws, a few frames later (vrHolsterNoteFrame) */
static void vrHolsterNoteParks(const s32 *old, const char *why)
{
	if (sState.parked[VRH_L] == old[VRH_L] && sState.parked[VRH_R] == old[VRH_R]) {
		return;
	}

	VRH_DIAG("holster park: %s: parked L/R %d/%d -> %d/%d, gun side %c", why, old[VRH_L], old[VRH_R],
			sState.parked[VRH_L], sState.parked[VRH_R], "LR"[sState.gunside]);
	vrHolsterArmModels(g_Vars.currentplayer->gunctrl.weaponnum, "holster park");
}

static void vrHolsterEquip(s32 weapon, s32 dual);

/* A gun that left the inventory (a deployed laptop gun or Dragon, a stage taking the weapons), a
 * used-up throwable or a worn disguise, or a mine at 0 rounds (vrHolsterUsable) leaves its slot,
 * and a hand holding it is empty (core rules 28 and 43). Then the empty item slots refill from the
 * inventory, which also takes item pickups and gifts. A lost item can move the gun side (rule 43:
 * a gun parked behind it whose grip is held comes back as the gun side), so the gun side and the
 * weapon follow here, for the callers that do not run the core after this. */
static void vrHolsterSyncInventory(void)
{
	s32 i, w, weapon, dual;
	s32 lost = 0;
	s32 old[2];

	for (i = 0; i < VRH_NUM_SLOTS; i++) {
		w = sState.holster[i];

		if (w == WEAPON_NONE || vrHolsterUsable(w)) {
			continue;
		}

		old[VRH_L] = sState.parked[VRH_L];
		old[VRH_R] = sState.parked[VRH_R];

		if (i >= VRH_NUM_GUNS) {
			VRH_DIAG("holster item: lost: %s weapon %d, held %d/%d", vrHolsterSlotName(i), w,
					sState.held[VRH_L], sState.held[VRH_R]);
		}

		vrhOnLost(&sState, w);
		vrHolsterNoteParks(old, i >= VRH_NUM_GUNS ? "lost item" : "lost");
		lost = 1;
	}

	vrHolsterFillItems("sync");

	if (lost) {
		vrHolsterSetMode(sState.gunside == VRH_L);
		vrhWeapon(&sState, &weapon, &dual);

		if (weapon != sLastWeapon || dual != sLastDual) {
			vrHolsterEquip(weapon, dual);
		}
	}
}

/* A switch to weapon starts its PD-VRR line. One still out (the engine had not drawn it yet: a hand
 * was busy, or a re-grip came first) gets its own line first. */
static void vrHolsterDiagStart(s32 weapon)
{
	if (sDiagWeapon >= 0) {
		VRH_DIAG("holster switch: weapon %d replaced by %d before it was drawn: commit +%d load +%d"
				" after %d frames", sDiagWeapon, weapon, sDiagCommit, sDiagLoaded,
				g_Vars.lvframenum - sDiagFrame);
	}

	sDiagWeapon = weapon;
	sShotWeapon = -1;
	sDiagFrame = g_Vars.lvframenum;
	sDiag60 = g_Vars.lvframe60;
	sDiagCommit = -1;
	sDiagLoaded = -1;
	sDiagLoadUs = -1;
	sDiagSound = -1;
}

/* Equips the core's weapon unless the engine already has it current or pending. Fists are the
 * engine's unarmed, which always fills both hands (bgunTickSwitch2), so the left hand asks for
 * fists too: WEAPON_NONE there would free the left fist when unarmed is already current. A gadget
 * or throwable is never dual: HAND_LEFT gets WEAPON_NONE (a gun parked behind it is not in the
 * engine). With holsters the remote mine is a single too, with no detonator in HAND_LEFT
 * (bgunTickSwitch2): a B tap detonates (vrHolsterBTapDetonates), which does not need it. */
static void vrHolsterEquip(s32 weapon, s32 dual)
{
	struct gunctrl *ctrl = &g_Vars.currentplayer->gunctrl;
	s32 target = weapon != WEAPON_NONE ? weapon : WEAPON_UNARMED;
	s32 cur = ctrl->switchtoweaponnum >= 0 ? ctrl->switchtoweaponnum : ctrl->weaponnum;

	if (!vrHolsterIsGun(target)) {
		dual = 0;
	}

	if (cur != target) {
		sSettleFrom60 = g_Vars.lvframe60;
		vrHolsterDiagStart(target);
		bgunEquipWeapon2(HAND_RIGHT, target);
	}

	/* PD-VRR: dual wield off with the same gun kept changes only the engine's left hand, so the
	 * switch line does not cover it: log it, then the models line once the left hand is out of
	 * use. Two cases: a pair down to a single (rule 7: a copy let go; or a copy holstered), left
	 * hand use1; or a pair draw cancelled before the left hand came up, left hand use0. */
	if (cur == target && vrHolsterIsGun(target) && ctrl->dualwielding && !dual) {
		VRH_DIAG("holster single: weapon %d dual wield off (pair let go, or pair draw cancelled),"
				" gun side %c, left hand use%d st%d/%d",
				target, "LR"[sState.gunside], g_Vars.currentplayer->hands[HAND_LEFT].inuse,
				g_Vars.currentplayer->hands[HAND_LEFT].state,
				g_Vars.currentplayer->hands[HAND_LEFT].stateminor);
		vrHolsterArmModels(target, "holster single");
	}

	if (target == WEAPON_UNARMED) {
		bgunEquipWeapon2(HAND_LEFT, WEAPON_UNARMED);
	} else {
		bgunEquipWeapon2(HAND_LEFT, dual ? target : WEAPON_NONE);
	}

	sLastWeapon = weapon;
	sLastDual = dual;
}

/* A pulse on physical hand p now; twice: a second one is queued for vrHolsterPlayFx, which a new
 * event replaces */
static void vrHolsterPulse(s32 p, f32 amp, f32 secs, s32 twice)
{
	vrReloadBuzz(vrHolsterLogical(p), amp, secs);
	sPulse2[p] = twice;
	sPulse2Frame[p] = g_Vars.lvframe60 + VRH_PULSE_GAP60;
	sPulse2Amp[p] = amp;
	sPulse2Secs[p] = secs;
}

/* One pulse per physical hand per tick: the strongest of its fx bits. A two-pulse event queues its
 * second pulse; any new event replaces a queued one. */
static void vrHolsterPlayFx(s32 fx)
{
	s32 p;

	for (p = 0; p < 2; p++) {
		f32 amp = 0.0f, secs = 0.0f;
		s32 twice = 0;

		/* No zone buzz for the free hand while the gun side's gun is mid-reload (vrReloadBusy: the
		 * clip out with rounds to fetch, or the free hand at work): on its way to the clip it passes
		 * the holster zones, which rule 30 muted while the core's reloading input covered the clip
		 * out (before Task H3d; H3 review) */
		if (p != sState.gunside && vrReloadBusy()) {
			fx &= ~(VRH_FX_ZONE << VRH_FX_SHIFT(p));
		}

		if (VRH_FX_HAS(fx, p, VRH_FX_SWAP)) {
			amp = VRH_BUZZ_SWAP_AMP;
			secs = VRH_BUZZ_SWAP_SECS;
			twice = 1;
		} else if (VRH_FX_HAS(fx, p, VRH_FX_DRAW)) {
			amp = VRH_BUZZ_DRAW_AMP;
			secs = VRH_BUZZ_DRAW_SECS;
		} else if (VRH_FX_HAS(fx, p, VRH_FX_HOLSTER)) {
			amp = VRH_BUZZ_HOLSTER_AMP;
			secs = VRH_BUZZ_HOLSTER_SECS;
		} else if (VRH_FX_HAS(fx, p, VRH_FX_EMPTY)) {
			amp = VRH_BUZZ_EMPTY_AMP;
			secs = VRH_BUZZ_EMPTY_SECS;
			twice = 1;
		} else if (VRH_FX_HAS(fx, p, VRH_FX_ZONE)) {
			amp = VRH_BUZZ_ZONE_AMP;
			secs = VRH_BUZZ_ZONE_SECS;
		}

		if (amp > 0.0f) {
			vrHolsterPulse(p, amp, secs, twice);
		} else if (sPulse2[p] && g_Vars.lvframe60 >= sPulse2Frame[p]) {
			vrReloadBuzz(vrHolsterLogical(p), sPulse2Amp[p], sPulse2Secs[p]);
			sPulse2[p] = 0;
		}
	}
}


/* Distance from pos to the segment a..b (a point when a == b) */
static f32 vrHolsterSegmentDist(const f32 *pos, const f32 *a, const f32 *b)
{
	f32 ab[3], ap[3], d[3];
	f32 len2 = 0.0f, t = 0.0f;
	s32 j;

	for (j = 0; j < 3; j++) {
		ab[j] = b[j] - a[j];
		ap[j] = pos[j] - a[j];
		len2 += ab[j] * ab[j];
		t += ap[j] * ab[j];
	}

	t = len2 > 0.0f ? t / len2 : 0.0f;
	t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;

	for (j = 0; j < 3; j++) {
		d[j] = ap[j] - ab[j] * t;
	}

	return sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

/* The zone of a controller origin (gCtrlPos frame): the nearest zone (distance to its segment, or
 * a shoulder's point) within its own radius. dist, if not NULL, gets the distance to the hip,
 * shoulder, chest, right hip and left shoulder (zone order). */
static s32 vrHolsterZone(const f32 *pos, f32 *dist)
{
	static const f32 starts[VRH_NUM_ZONES - 1][3] = {
		{ VRH_HIP_X, VRH_HIP_Y, VRH_HIP_Z },
		{ VRH_SHOULDER_X, VRH_SHOULDER_Y, VRH_SHOULDER_Z },
		{ VRH_CHEST_X, VRH_CHEST_Y, VRH_CHEST_Z },
		{ VRH_RHIP_X, VRH_RHIP_Y, VRH_RHIP_Z },
		{ VRH_LSHOULDER_X, VRH_LSHOULDER_Y, VRH_LSHOULDER_Z },
	};
	static const f32 ends[VRH_NUM_ZONES - 1][3] = {
		{ VRH_HIP_X2, VRH_HIP_Y2, VRH_HIP_Z2 },
		{ VRH_SHOULDER_X, VRH_SHOULDER_Y, VRH_SHOULDER_Z },
		{ VRH_CHEST_X2, VRH_CHEST_Y2, VRH_CHEST_Z2 },
		{ VRH_RHIP_X2, VRH_RHIP_Y2, VRH_RHIP_Z2 },
		{ VRH_LSHOULDER_X, VRH_LSHOULDER_Y, VRH_LSHOULDER_Z },
	};
	static const s32 zones[VRH_NUM_ZONES - 1] = {
		VRH_ZONE_HIP, VRH_ZONE_SHOULDER, VRH_ZONE_CHEST, VRH_ZONE_RHIP, VRH_ZONE_LSHOULDER,
	};
	static const f32 radii[VRH_NUM_ZONES - 1] = {
		VRH_HIP_RAD, VRH_SHOULDER_RAD, VRH_CHEST_RAD, VRH_RHIP_RAD, VRH_LSHOULDER_RAD,
	};
	f32 best = 0.0f;
	s32 zone = VRH_ZONE_NONE;
	s32 i;

	for (i = 0; i < VRH_NUM_ZONES - 1; i++) {
		f32 a[3], b[3], len;

		/* vrBodyPoint is linear (a mirror and a rotation), so the segment's ends map to the
		 * segment's ends */
		if (zones[i] == VRH_ZONE_CHEST) {
			/* In the body frame with x scaled: the chest's segment is at x 0, so the mirror
			 * does not move it */
			f32 body[3];

			vrBodyLocal(pos, body);
			body[0] *= VRH_CHEST_XSCALE;
			len = vrHolsterSegmentDist(body, starts[i], ends[i]);
		} else {
			vrBodyPoint(starts[i], a);
			vrBodyPoint(ends[i], b);
			len = vrHolsterSegmentDist(pos, a, b);
		}

		if (dist != NULL) {
			dist[i] = len;
		}

		if (len <= radii[i] && (zone == VRH_ZONE_NONE || len <= best)) {
			best = len;
			zone = zones[i];
		}
	}

	return zone;
}

/* The engine's HAND_RIGHT holds a gadget or throwable that is mid-attack: a throw's wind-up or
 * release (HANDSTATE_ATTACK, the empty one too), the redraw after a throw or a cancelled grenade
 * (HANDSTATE_RELOAD), or the knife's VR throw wind-up, which runs in HANDSTATE_IDLE. A weapon switch
 * starts only from the idle state (bgunTickIncIdle), so the engine keeps the item until the attack
 * ends; the knife's wind-up is the exception: a switch takes the knife away at once and drops the
 * wind-up (bgunTickSwitch2 clears its flags). */
static s32 vrHolsterItemBusy(void)
{
	struct hand *hand = &g_Vars.currentplayer->hands[HAND_RIGHT];
	s32 w = hand->gset.weaponnum;

	if (vrHolsterSlotOf(w) < VRH_NUM_GUNS) {
		return 0;
	}

	return hand->state == HANDSTATE_ATTACK || hand->state == HANDSTATE_ATTACKEMPTY
		|| hand->state == HANDSTATE_RELOAD || (w == WEAPON_COMBATKNIFE && vr_R_knife_sec_anim_run);
}

/* The core's reloading input means "the gun side's weapon is busy": the free hand in the middle of
 * a manual reload of its gun (vrReloadHoldsSwitch: a clip or round in it, a rack or a slap under
 * way), or its item mid-throw (vrHolsterItemBusy). While the item is mid-throw the gun side must not
 * move: the throw reads the gun side's trigger and motion samples. So the free hand neither draws
 * nor buzzes (rules 29, 30), its gun stays parked behind the item (rule 33, a no-op then), no parked
 * gun comes back (rules 9, 41) and a step or pick does not change the item in the hand (rules 34,
 * 35). A gun mid-reload with nothing in the free hand (its clip out, the Rocket Launcher's hatch
 * open) is not busy: a draw, a step or a pick switches it away and its state is saved with it (Task
 * H3d, the user: "rocket launcher ... cannot be swapped away in the middle of a reload anymore"). */
static s32 vrHolsterGunSideBusy(void)
{
	return vrReloadHoldsSwitch() || vrHolsterItemBusy();
}

/* The tick's reloading input: vrHolsterGunSideBusy, and with a pair (the free hand holds the other
 * copy, parked or not) any reload under way (vrReloadBusy), so the copy parks for the whole reload
 * (rule 33) and comes back only after it (rule 9): the free hand stays free to fetch the clip. The
 * whole reload runs to the rack (vrReloadMidReload): vrReloadBusy leaves out a gun that only waits
 * for its rack, so the copy came back with the clip seated, in the hand that racks (the user: "the
 * other hands magsec comes back after the clip is entered but before the rack") */
static s32 vrHolsterTickReloading(void)
{
	return vrHolsterGunSideBusy()
		|| ((vrReloadBusy() || vrReloadMidReload(HAND_RIGHT)) && sState.held[1 - sState.gunside] >= 0);
}

static void vrHolsterBuildInput(struct vrhinput *in, s32 forcepause)
{
	struct player *player = g_Vars.currentplayer;
	s32 p, i;

	for (p = 0; p < 2; p++) {
		in->grip[p] = get_button_state(vrHolsterLogical(p), "grip") ? 1 : 0;
		in->tracked[p] = vrCtrlPoseGood(vrHolsterPoseIndex(p)) ? 1 : 0;
		in->zone[p] = vrHolsterZone(gCtrlPos[vrHolsterPoseIndex(p)], NULL);

		/* The free hand (the off controller's pose, logical 0) inside the rack area of a gun waiting
		 * for a grip's rack: the rack wins, so no zone sees the press (vrReloadBusy does not close the
		 * shoulder for that wait, and the RC-P120's knob can sit in it) */
		if (vrHolsterPoseIndex(p) == 0 && in->zone[p] != VRH_ZONE_NONE && vrReloadFreeHandInRack()) {
			in->zone[p] = VRH_ZONE_NONE;
		}

		/* The free hand at the reload's shoulder while the gun can take ammo there: the ammo wins,
		 * whatever holster zone the hand is also in. The reload's shoulder is the free hand's own
		 * (mirrored by the gun side), the holster zones the left-handed setting's, so it is the
		 * off-hand shoulder zone with the gun in the setting's gun hand (Task J4) and the gun-hand
		 * shoulder zone with the gun in the other hand. A gun that is not busy (the Shotgun never
		 * is: vrReloadBusy) left the press to the holster too, which moved the shoulder gun (Task
		 * F11g, the user: "if you gripping a shot gun the right shoulder needs to be the ammo").
		 * Where the chest pouch's zone reaches the clip source, a squeeze for a clip also drew the
		 * pouch item, on every clip gun since Task H3d no longer closes the holsters with the clip
		 * out (H3 review). With the tube or magazine full, or no reserve, the zone gives its gun or
		 * item as before */
		sZoneBlank[p] = VRH_ZONE_NONE;

		if (vrHolsterPoseIndex(p) == 0 && in->zone[p] != VRH_ZONE_NONE && vrReloadFreeHandAtAmmo()) {
			sZoneBlank[p] = in->tracked[p] ? in->zone[p] : VRH_ZONE_NONE;
			in->zone[p] = VRH_ZONE_NONE;
		}

		/* The free hand at the holster of the gun side's gun while that gun is mid-reload: a press
		 * there would draw the pair's other copy (rule 3), which would park at once (rule 33). Only
		 * a single gun's reload gets here: with a pair the free hand's copy is parked */
		if (p != sState.gunside && in->zone[p] != VRH_ZONE_NONE && sState.held[sState.gunside] >= 0
				&& in->zone[p] == vrHolsterSlotZone(sState.held[sState.gunside]) && vrReloadBusy()) {
			sZoneBlank[p] = in->tracked[p] ? in->zone[p] : VRH_ZONE_NONE;
			in->zone[p] = VRH_ZONE_NONE;
		}
	}

	for (i = 0; i < VRH_NUM_GUNS; i++) {
		s32 w = sState.holster[i];

		in->haspair[i] = w != WEAPON_NONE && invHasDoubleWeaponIncAllGuns(w, w);
	}

	in->reloading = vrHolsterTickReloading() ? 1 : 0;
	in->paused = forcepause || player->pausemode != PAUSEMODE_UNPAUSED || lvIsPaused();
}

/* Two weak pulses on physical hand p: a refused Y, holster step or menu pick */
static void vrHolsterBuzzRefused(s32 p)
{
	vrHolsterPulse(p, VRH_BUZZ_EMPTY_AMP, VRH_BUZZ_EMPTY_SECS, 1);
}

/* Engine hand h is mid-shot: an attack (a dry fire included) under way. (hand->firing is no use
 * here: bgunTickInc clears the right hand's before the holster tick runs.) */
static s32 vrHolsterHandShooting(s32 h)
{
	struct hand *hand = &g_Vars.currentplayer->hands[h];

	return hand->state == HANDSTATE_ATTACK || hand->state == HANDSTATE_ATTACKEMPTY;
}

/* A Y press on the left gun of a pair (vrReloadEjectButton), at the top of the tick: that gun
 * reloads as the right one does. The flip makes its hand the gun side (rule 32) and the swap
 * makes it the engine's HAND_RIGHT with its own rounds; the eject then waits for the poses to
 * follow the new gun side (vrReloadBuildInput). Refused with two weak pulses while the gun side's
 * gun is mid-reload or waits for its rack or slap (one reload at a time: vrReloadBusy does not
 * count the wait, which holsters), during a weapon change (the engine's hands are not yet
 * the pair), mid-shot (the engine hand's attack would go on with the other gun) or with a hand
 * parked. Dropped when the tick holsters the guns (paused), the pair is gone or the press is more
 * than VRH_YFLIP_MAX_AGE frames old. */
static void vrHolsterApplyYFlip(s32 paused)
{
	struct player *player = g_Vars.currentplayer;
	s32 oldside = sState.gunside;
	const char *why = NULL;

	if (!sYFlipPending) {
		return;
	}

	sYFlipPending = 0;

	if (paused || !vrReloadOwnsGun(HAND_LEFT) || g_Vars.lvframenum - sYFlipFrame > VRH_YFLIP_MAX_AGE) {
		VRH_DIAG("holster reload Y: dropped, %s", paused ? "paused"
				: !vrReloadOwnsGun(HAND_LEFT) ? "the left gun is gone" : "too old");
		return;
	}

	if (vrReloadBusy()) {
		why = "the gun side's gun is mid-reload";
	} else if (vrReloadWaitsForRack(HAND_RIGHT)) {
		why = "the gun side's gun waits for its rack or slap";
	} else if (player->gunctrl.switchtoweaponnum >= 0
			|| player->hands[HAND_RIGHT].state == HANDSTATE_CHANGEGUN
			|| player->hands[HAND_LEFT].state == HANDSTATE_CHANGEGUN) {
		why = "weapon change";
	} else if (vrHolsterHandShooting(HAND_RIGHT) || vrHolsterHandShooting(HAND_LEFT)) {
		why = "mid-shot";
	} else if (!vrReloadCanSwapHands()) {
		why = "the engine has one gun";
	} else if (!vrhFlipGunSide(&sState)) {
		why = "no unparked pair";
	} else if (!vrReloadSwapHands()) {
		vrhFlipGunSide(&sState); /* checked above: cannot happen */
		why = "swap failed";
	}

	if (why != NULL) {
		VRH_DIAG("holster reload Y: refused (%s): gun side %c held %d/%d parked %d/%d states %d/%d",
				why, "LR"[oldside], sState.held[VRH_L], sState.held[VRH_R], sState.parked[VRH_L],
				sState.parked[VRH_R], player->hands[HAND_RIGHT].state, player->hands[HAND_LEFT].state);
		/* The left gun's controller: logical 0 (the xor is its own inverse, so vrHolsterLogical
		 * also gives the physical hand) */
		vrHolsterBuzzRefused(vrHolsterLogical(0));
		return;
	}

	/* After the swap, which drops any eject request */
	vrHolsterSetMode(sState.gunside == VRH_L);
	vrReloadRequestEject(HAND_RIGHT);
	VRH_DIAG("holster reload Y: gun side %c -> %c, hands swapped, eject requested", "LR"[oldside],
			"LR"[sState.gunside]);
}

/* Copies each physical hand's held and parked before a core call (a tick, a step, a menu pick) */
static void vrHolsterSaveHands(s32 *oldheld, s32 *oldparked)
{
	oldheld[VRH_L] = sState.held[VRH_L];
	oldheld[VRH_R] = sState.held[VRH_R];
	oldparked[VRH_L] = sState.parked[VRH_L];
	oldparked[VRH_R] = sState.parked[VRH_R];
}

/* Both hands hold copies of the same gun: a pair. Both hands holding is not enough since Stage C:
 * it can be an item and a gun parked behind it (core rule 39). */
static s32 vrHolsterIsPair(const s32 *held)
{
	return held[VRH_L] >= 0 && held[VRH_L] == held[VRH_R];
}

/* An item slot: gadgets or throwables */
static s32 vrHolsterIsItemSlot(s32 i)
{
	return i == VRH_POUCH || i == VRH_THROW;
}

/* PD-VRR: each hand that drew an item or let one go (home, or sent back by the other hand's draw
 * or a pause) since oldheld, by the core call named why */
static void vrHolsterNoteItems(const s32 *oldheld, const char *why)
{
	s32 p;

	for (p = 0; p < 2; p++) {
		s32 was = oldheld[p];
		s32 now = sState.held[p];

		if (was == now) {
			continue;
		}

		/* Let go mid-throw (only the item hand can, rule 29): the core sends the item home at
		 * once. In an attack (a throw's wind-up or release) the engine finishes it first (the
		 * throw still reads this gun side's trigger and motion, which vrHolsterGunSideBusy
		 * keeps), and the switch starts from idle; the thrown round is the engine's one
		 * decrement, and the slot keeps the item. The knife's wind-up runs in the idle state, so
		 * the switch takes the knife at once: no knife is thrown, and bgunTickSwitch2 clears the
		 * wind-up's flags. */
		if (vrHolsterIsItemSlot(was)) {
			struct hand *hand = &g_Vars.currentplayer->hands[HAND_RIGHT];

			/* Busy in the idle state is only the knife's wind-up */
			VRH_DIAG("holster item: %s: %c releases %s weapon %d, gun side %c, engine R weapon %d"
					" state %d/%d%s", why, "LR"[p], vrHolsterSlotName(was), sState.holster[was],
					"LR"[sState.gunside], hand->gset.weaponnum, hand->state, hand->stateminor,
					!vrHolsterItemBusy() ? "" : hand->state == HANDSTATE_IDLE ? ", knife wind-up dropped"
					: ", mid-throw: the switch waits for the attack's end");
		}

		if (vrHolsterIsItemSlot(now)) {
			VRH_DIAG("holster item: %s: %c draws %s weapon %d, other hand held %d parked %d",
					why, "LR"[p], vrHolsterSlotName(now), sState.holster[now], sState.held[1 - p],
					sState.parked[1 - p]);
		}
	}
}

/* The park line's reason: "behind an item" is added when either hand held an item before or
 * after (core rules 37, 39, 41 and 43) */
static const char *vrHolsterParkWhy(const s32 *oldheld, const char *why, const char *itemwhy)
{
	if (vrHolsterIsItemSlot(oldheld[VRH_L]) || vrHolsterIsItemSlot(oldheld[VRH_R])
			|| vrHolsterIsItemSlot(sState.held[VRH_L]) || vrHolsterIsItemSlot(sState.held[VRH_R])) {
		return itemwhy;
	}

	return why;
}

/* One core tick and its output: the gun side first (the reload reads it next), then the weapon on
 * a change of the core's output (or when forced), then haptics */
static void vrHolsterRun(s32 forcepause, s32 forceapply)
{
	struct vrhinput in;
	struct vrhoutput out;
	s32 prefchanged = VrLeftHandedPref != sPref;
	s32 paused = forcepause || sPausePending || prefchanged || vrHolsterFullMenuOpen();
	s32 oldside, waspair;
	s32 oldheld[2], oldparked[2];
	s32 left, i;

	/* Left-handed toggled (the menu already set the gun side to it): holster as for a pause, and
	 * the core's gun side follows the setting */
	sPref = VrLeftHandedPref;

	/* The gun side changed outside the adapter (the left-handed menu): the reload's grabs end too */
	vrHolsterSetMode(VrLeftHandedMode);

	vrHolsterApplyYFlip(paused || g_Vars.currentplayer->pausemode != PAUSEMODE_UNPAUSED || lvIsPaused());

	/* After the Y flip, which swaps the hands itself. An item and the gun parked behind it are not
	 * a pair: when the item goes home and the gun comes back as the gun side (rule 41), the engine
	 * holds only the item, so nothing swaps. */
	oldside = sState.gunside;
	waspair = vrHolsterIsPair(sState.held);

	vrHolsterBuildInput(&in, paused);
	sPausePending = 0;
	vrHolsterSaveHands(oldheld, oldparked);
	vrhTick(&sState, &in, &out);

	/* The face buttons read the zone the core would have seen without the blanks, but for the
	 * shoulders while reloading, which the core counts as no zone (core rules 18 and 52) */
	for (i = 0; i < 2; i++) {
		s32 shoulder = sZoneBlank[i] == VRH_ZONE_SHOULDER || sZoneBlank[i] == VRH_ZONE_LSHOULDER;

		sButtonZone[i] = sZoneBlank[i] == VRH_ZONE_NONE ? sState.prevzone[i]
			: in.reloading && shoulder ? VRH_ZONE_NONE : sZoneBlank[i];
	}

	vrHolsterNoteParks(oldparked, vrHolsterParkWhy(oldheld, "tick", "tick, behind an item"));
	vrHolsterNoteItems(oldheld, in.paused ? "pause" : "tick");

	/* The gun side moved while both hands held a copy (rule 7): the gun the engine keeps as
	 * HAND_RIGHT is the one in the other hand, so the two engine hands trade their rounds and reload
	 * state before the gun side follows. A copy let go then leaves from HAND_LEFT, and its shape is
	 * saved under that hand by the switch (vrReloadTick). The engine may not hold the second copy
	 * yet (its draw held back): then it has only the one gun and nothing moves. A release cannot be
	 * refused, so this swaps mid-shot too: at most one shot carries over to the other gun.
	 * Only when the new gun-side hand kept its copy: it gets VRH_FX_HOLSTER only when its copy went
	 * home (rule 31, or both grips in one tick), and then nothing swaps, whatever it draws in the
	 * same tick (rule 31 makes it the gun side before the presses, so rule 29 no longer blocks
	 * it; a redraw of the same gun is a fresh copy). Each engine hand then saves its own gun, and a
	 * gun let go mid-reload keeps its shape under HAND_RIGHT, as a single gun does, so its next
	 * single draw resumes that reload. */
	if (waspair && sState.gunside != oldside
			&& !VRH_FX_HAS(out.fx, sState.gunside, VRH_FX_HOLSTER)) {
		s32 swapped = vrReloadSwapHands();

		VRH_DIAG("holster side: core gun side %c -> %c from a pair, hands %s", "LR"[oldside],
				"LR"[sState.gunside], swapped ? "swapped" : "not swapped (engine has one gun)");
	}

	if (prefchanged) {
		sState.gunside = vrHolsterPrefSide();
		out.gunside = sState.gunside;
	}

	/* The core moves the gun side only at a draw (rules 2, 4) and when the gun-side hand lets go
	 * of one copy of a pair (rule 7): the gun the engine keeps must follow the hand still holding
	 * it */
	left = out.gunside == VRH_L;

	/* With a menu open the controllers are the setting's (vrHolsterMenuSide) */
	if (vrHolsterFullMenuOpen()) {
		left = VrLeftHandedPref;
	}

	vrHolsterSetMode(left);

	if (forceapply || out.weapon != sLastWeapon || out.dual != sLastDual) {
		vrHolsterEquip(out.weapon, out.dual);
	}

	vrHolsterPlayFx(out.fx);
}

void vrHolsterTick(void)
{
	if (g_Vars.currentplayernum != 0 || g_Vars.currentplayer == NULL) {
		return;
	}

	if (!VrManualReloading) {
		/* Turned off: the gun side goes back to the setting, and turning them on refills */
		if (sWasOn) {
			sWasOn = 0;
			sInitPending = 1;
			vrHolsterSetMode(VrLeftHandedPref);
		}

		sYFlipPending = 0;

		return;
	}

	vrHolsterEnsureFilled();
	vrHolsterSyncInventory();
	vrHolsterRun(0, sForceApply);
	sForceApply = 0;
}

void vrHolsterReset(void)
{
	s32 p;

	if (g_Vars.currentplayernum != 0) {
		return;
	}

	vrHolsterSetMode(VrLeftHandedPref);
	vrhInit(&sState, WEAPON_NONE, WEAPON_NONE, vrHolsterPrefSide());
	vrHolsterApplyLayout("reset");
	sInitPending = 1;
	sForceApply = 0;
	sPausePending = 0;
	sYFlipPending = 0;
	sPref = VrLeftHandedPref;
	sLastWeapon = 0;
	sLastDual = 0;
	sDiagWeapon = -1;
	sDiagModelsAt = -1;
	sDiagTrisAt = -1;
	sShotWeapon = -1;
	sSettleFrom60 = -100000;

	for (p = 0; p < 2; p++) {
		sPulse2[p] = 0;
		sFaceDown[p] = 0;
		sGrabPrev[p] = 1; /* a grip held through a level start or death is not a press */
		sZoneBlank[p] = VRH_ZONE_NONE;
		sButtonZone[p] = VRH_ZONE_NONE;
	}

	sMenuHand = -1;
	sFaceFrame = -10;
	sGrabHand = -1;
	sCrateHintShown = 0;
	/* hudmsgsReset restarts the ids at a level start. A new life (playerStartNewLife) clears it too,
	 * mid-level: the first holster message after a respawn may wait once behind the last one. */
	sHudId = -1;
}

/* A holster HUD message (the grab hint, a step's name, a pause-menu item pick): it replaces the
 * holster's previous one at once, queued or showing, instead of waiting for it to time out.
 * duration60: 0 for the default type's own (80 ticks); colournum: -1 for its own green */
static void vrHolsterHudFor(char *text, s32 duration60, s32 colournum)
{
	sHudId = hudmsgCreateReplacing(text, HUDMSGTYPE_DEFAULT, HUDMSGFLAG_ONLYIFALIVE | HUDMSGFLAG_ALLOWDUPES,
			sHudId, duration60, colournum);
}

static void vrHolsterHud(char *text)
{
	vrHolsterHudFor(text, 0, -1);
}

void vrHolsterHudLine(char *text, s32 duration60, s32 colournum)
{
	vrHolsterHudFor(text, duration60, colournum);
}

bool vrHolsterCrateHintDue(void)
{
	return vrHolsterActive() && !sCrateHintShown && g_Vars.lvframenum % VRH_CRATE_HINT_FRAMES == 0;
}

void vrHolsterCrateHint(void)
{
	static char text[] = "Grip with an empty hand to grab and push";

	if (!vrHolsterActive() || sCrateHintShown) {
		return;
	}

	sCrateHintShown = 1;
	vrHolsterHud(text);
	VRH_DIAG("hint: crate");
}

void vrHolsterDiagLine(const char *text)
{
	VRH_DIAG("%s", text);
}

void vrHolsterDiagText(const char *text)
{
	VRH_DIAG("%s: render %s", sDiagModelsLabel, text);
}

void vrHolsterNoteRaised(s32 handnum)
{
	if (handnum == HAND_RIGHT && vrHolsterActive()) {
		sSettleFrom60 = g_Vars.lvframe60;
	}
}

bool vrHolsterSwingSettling(void)
{
	return vrHolsterActive() && g_Vars.lvframe60 - sSettleFrom60 < VRH_SWING_SETTLE60;
}

void vrHolsterNoteDrawSound(s32 handnum)
{
	if (handnum == HAND_RIGHT && sDiagWeapon >= 0 && g_Vars.currentplayernum == 0) {
		sDiagSound = g_Vars.lvframenum - sDiagFrame;
	}
}

void vrHolsterNoteShot(s32 handnum)
{
	if (handnum != HAND_RIGHT || sShotWeapon < 0 || g_Vars.currentplayernum != 0) {
		return;
	}

	VRH_DIAG("holster switch: weapon %d first trigger attack +%d frames (%d 60ths) after the draw, func %d",
			sShotWeapon, g_Vars.lvframenum - sShotFrom, g_Vars.lvframe60 - sShotFrom60,
			g_Vars.currentplayer->hands[HAND_RIGHT].gset.weaponfunc);
	sShotWeapon = -1;
}

void vrHolsterNoteSwing(s32 handnum, bool started)
{
	if (sShotWeapon < 0 || g_Vars.currentplayernum != 0) {
		return;
	}

	if (started) {
		VRH_DIAG("holster switch: weapon %d swing attack by hand %d +%d frames after the draw",
				sShotWeapon, handnum, g_Vars.lvframenum - sShotFrom);
	} else if (!sShotHeldLogged) {
		VRH_DIAG("holster switch: weapon %d swing held back by hand %d +%d frames after the draw",
				sShotWeapon, handnum, g_Vars.lvframenum - sShotFrom);
		sShotHeldLogged = 1;
	}
}

void vrHolsterNoteLoad(s32 micros)
{
	if (sDiagWeapon >= 0) {
		sDiagLoadUs = micros;
	}
}

void vrHolsterNoteFrame(void)
{
	struct gunctrl *ctrl;
	struct hand *hand;
	s32 frames;

	if (g_Vars.currentplayernum != 0 || g_Vars.currentplayer == NULL) {
		return;
	}

	/* The follow-up: what each hand draws once the switch has settled */
	if (sDiagModelsAt >= 0 && g_Vars.lvframenum >= sDiagModelsAt) {
		char buf[640];

		bgunDescribeHandModels(buf, sizeof(buf));
		VRH_DIAG("%s: weapon %d models: %s", sDiagModelsLabel, sDiagModelsWeapon, buf);
		sDiagModelsAt = -1;
		bgunRequestHandRenderDump();
		memset(gVrHandTris, 0, sizeof(gVrHandTris));
		sDiagTrisAt = g_Vars.lvframenum + 1;
	}

	/* The dumped frame's gun model triangles per hand, counted by gfx_sp_tri1 once that frame was
	 * drawn: total, hidden, clipped, culled, drawn */
	if (sDiagTrisAt >= 0 && g_Vars.lvframenum >= sDiagTrisAt) {
		VRH_DIAG("%s: weapon %d tris R %d hid %d clip %d cull %d drawn %d"
				" | L %d hid %d clip %d cull %d drawn %d", sDiagModelsLabel, sDiagModelsWeapon,
				gVrHandTris[0][0], gVrHandTris[0][1], gVrHandTris[0][2], gVrHandTris[0][3],
				gVrHandTris[0][4], gVrHandTris[1][0], gVrHandTris[1][1], gVrHandTris[1][2], gVrHandTris[1][3],
				gVrHandTris[1][4]);
		sDiagTrisAt = -1;
	}

	/* No attack from the switched-to weapon within 10 s: stop watching */
	if (sShotWeapon >= 0 && g_Vars.lvframe60 - sShotFrom60 > 600) {
		sShotWeapon = -1;
	}

	if (sDiagWeapon < 0) {
		return;
	}

	ctrl = &g_Vars.currentplayer->gunctrl;
	hand = &g_Vars.currentplayer->hands[HAND_RIGHT];
	frames = g_Vars.lvframenum - sDiagFrame;

	if (sDiagCommit < 0 && ctrl->switchtoweaponnum < 0 && ctrl->weaponnum == sDiagWeapon) {
		sDiagCommit = frames;
	}

	if (sDiagCommit >= 0 && sDiagLoaded < 0 && bgunIsLoaded()) {
		sDiagLoaded = frames;
	}

	/* The gun hand's model is drawn this frame (bgun0f0a5550 set visible just before bgunRender) */
	if (sDiagLoaded >= 0 && hand->visible && hand->state != HANDSTATE_CHANGEGUN) {
		char sound[16];

		if (sDiagSound >= 0) {
			snprintf(sound, sizeof(sound), "+%d", sDiagSound);
		} else {
			snprintf(sound, sizeof(sound), "none");
		}

		VRH_DIAG("holster switch: weapon %d commit +%d load +%d drawn +%d frames (%d 60ths), load %d us, sound %s",
				sDiagWeapon, sDiagCommit, sDiagLoaded, frames, g_Vars.lvframe60 - sDiag60, sDiagLoadUs, sound);
		sShotWeapon = sDiagWeapon;
		sShotFrom = g_Vars.lvframenum;
		sShotFrom60 = g_Vars.lvframe60;
		sShotHeldLogged = 0;
		vrHolsterArmModels(sDiagWeapon, "holster switch");
		sDiagWeapon = -1;
	} else if (g_Vars.lvframe60 - sDiag60 > 600) {
		VRH_DIAG("holster switch: weapon %d not drawn after 10 s: commit +%d load +%d state %d/%d mode %d",
				sDiagWeapon, sDiagCommit, sDiagLoaded, hand->state, hand->stateminor, hand->mode);
		sDiagWeapon = -1;
	}
}

void vrHolsterNotePause(void)
{
	sPausePending = 1;
}

/* A full menu (a pause, a training's end, an endscreen; not the hold menu) is open */
static s32 vrHolsterFullMenuOpen(void)
{
	return g_MenuData.count > 0;
}

void vrHolsterMenuSide(void)
{
	if (!VrManualReloading || !vrHolsterFullMenuOpen()) {
		return;
	}

	if (VrLeftHandedMode != VrLeftHandedPref) {
		VRH_DIAG("holster side: a menu is open: gun side %c -> the setting's %c", "RL"[VrLeftHandedMode],
				"RL"[VrLeftHandedPref]);
		vrHolsterSetMode(VrLeftHandedPref);
	}

	/* The hands empty as for a pause when play resumes */
	sPausePending = 1;
}

void vrHolsterOnPickup(s32 weaponnum)
{
	if (!vrHolsterActive() || !vrHolsterIsGun(weaponnum) || !vrHolsterOwns(weaponnum)) {
		return;
	}

	vrHolsterEnsureFilled();
	vrhOnPickup(&sState, weaponnum);
}

/* The HUD line "<name>: <what>" for a pause-menu item pick: the body spot of the slot it went to
 * ("chest", "right hip": vrHolsterZoneName), or why it did not (the weapon names end in a
 * newline) */
static void vrHolsterItemHud(s32 weaponnum, const char *what)
{
	char text[80];
	const char *name = bgunGetName(weaponnum);
	s32 len = 0;

	while (name[len] != '\0' && name[len] != '\n' && len < 48) {
		len++;
	}

	snprintf(text, sizeof(text), "%.*s: %s", len, name, what);
	vrHolsterHud(text);
}

bool vrHolsterOnMenuPick(s32 weaponnum)
{
	s32 weapon, dual, slot;
	s32 old[2];

	slot = vrHolsterSlotOf(weaponnum);

	if (!vrHolsterActive()) {
		return false;
	}

	/* Fists, keys and other items (device toggles never get here) keep the vanilla equip, which
	 * the core does not see: the next tick equips the core's weapon again, so the hands never stay
	 * on the vanilla pick (a key equips no weapon) until the core's output next changes */
	if (slot < 0) {
		sForceApply = 1;
		return false;
	}

	vrHolsterEnsureFilled();
	vrHolsterSyncInventory();

	/* The pause menu: the core holsters the guns and items (rule 11) before the pick, as the first
	 * tick after the pause would have */
	if (sPausePending || g_Vars.currentplayer->pausemode != PAUSEMODE_UNPAUSED || lvIsPaused()) {
		vrHolsterRun(1, 0);
	}

	/* A gadget or throwable goes to its slot and not into a hand (user decision): the next grip
	 * there takes it. The hands are empty after the pause. A mine at 0 rounds is not taken. */
	if (slot != VRH_GUN1) {
		s32 from = sState.holster[slot];
		s32 ok = vrHolsterUsable(weaponnum) && vrhSetItem(&sState, slot, weaponnum);

		VRH_DIAG("holster item: menu pick: %s weapon %d -> %d: %s, held %d/%d", vrHolsterSlotName(slot),
				from, weaponnum, ok ? "done" : sState.holster[slot] == weaponnum ? "already there"
				: "refused", sState.held[VRH_L], sState.held[VRH_R]);

		if (sState.holster[slot] == weaponnum) {
			vrHolsterItemHud(weaponnum, vrHolsterZoneName(vrHolsterSlotZone(slot)));
		} else {
			vrHolsterItemHud(weaponnum, vrHolsterUsable(weaponnum) ? "not taken" : "none left");
		}

		return true;
	}

	old[VRH_L] = sState.parked[VRH_L];
	old[VRH_R] = sState.parked[VRH_R];
	vrhOnMenuPick(&sState, weaponnum);
	vrHolsterNoteParks(old, "menu pick");
	vrhWeapon(&sState, &weapon, &dual);
	vrHolsterEquip(weapon, dual);

	return true;
}

/* After a holster step or a hold-menu pick by physical hand p changed the state: as vrHolsterRun,
 * the engine's hands trade their guns' state when the gun side moved while both hands held a copy,
 * then the gun side and the weapon follow. The core gives no fx: hand p gets the draw buzz when
 * draw is set or the core unparked it, and a pair's other copy that went back gets the holster
 * buzz. */
static void vrHolsterApplyHand(s32 p, s32 oldside, const s32 *oldheld, const s32 *oldparked,
		s32 draw, const char *why)
{
	s32 o = 1 - p;
	s32 weapon, dual;
	s32 fx = 0;

	if (vrHolsterIsPair(oldheld) && sState.gunside != oldside) {
		s32 swapped = vrReloadSwapHands();

		VRH_DIAG("holster side: core gun side %c -> %c from a pair (%s), hands %s", "LR"[oldside],
				"LR"[sState.gunside], why, swapped ? "swapped" : "not swapped (engine has one gun)");
	}

	vrHolsterSetMode(sState.gunside == VRH_L);
	vrHolsterNoteParks(oldparked, why);
	vrhWeapon(&sState, &weapon, &dual);
	vrHolsterEquip(weapon, dual);

	if (draw || (oldparked[p] && !sState.parked[p])) {
		fx |= VRH_FX_DRAW << VRH_FX_SHIFT(p);
	}

	if (oldheld[o] >= 0 && sState.held[o] < 0) {
		fx |= VRH_FX_HOLSTER << VRH_FX_SHIFT(o);
	}

	vrHolsterPlayFx(fx);
}

/* The next weapon of slot i's kind in inventory order (invGetWeaponNumByIndex: bgunGetCycleWeapon
 * stops at NUM_CYCLEABLE_WEAPONS, before most gadgets), after the slot's weapon (after the engine's
 * weapon when the slot is empty), wrapping: a gun for the gun holsters, skipping the guns in the
 * other two gun holsters; a usable gadget or throwable (vrHolsterUsable) for the item slots. 0 when
 * there is none. */
static s32 vrHolsterNextItem(s32 i)
{
	s32 n = invGetCount();
	s32 start = sState.holster[i] != WEAPON_NONE ? sState.holster[i] : g_Vars.currentplayer->gunctrl.weaponnum;
	s32 from = -1;
	s32 j, k, w, g, other;

	if (n <= 0) {
		return WEAPON_NONE;
	}

	for (j = 0; j < n; j++) {
		if (invGetWeaponNumByIndex(j) == start) {
			from = j;
			break;
		}
	}

	for (k = 1; k <= n; k++) {
		w = invGetWeaponNumByIndex((from + k + n) % n);

		/* A gun in another gun holster: the core refuses the step (rules 34 and 56) */
		other = 0;

		for (g = VRH_GUN1; g < VRH_NUM_GUNS && i < VRH_NUM_GUNS; g++) {
			if (g != i && w == sState.holster[g]) {
				other = 1;
			}
		}

		if (vrHolsterFitsSlot(w, i) && vrHolsterUsable(w) && w != sState.holster[i] && !other) {
			return w;
		}
	}

	return WEAPON_NONE;
}

/* A tap of physical hand p's lower button in slot i's zone: the slot steps to the next gun or item
 * of its kind (rules 34 and 45), with a firm buzz and its name on the HUD; refused with two weak
 * pulses */
static void vrHolsterStep(s32 p, s32 i)
{
	s32 from = sState.holster[i];
	s32 weapon = vrHolsterNextItem(i);
	s32 oldside = sState.gunside;
	s32 zone = vrHolsterSlotZone(i);
	s32 oldheld[2], oldparked[2];

	vrHolsterSaveHands(oldheld, oldparked);

	if (!vrhStepHolster(&sState, p, i, weapon, vrHolsterGunSideBusy() ? 1 : 0)) {
		VRH_DIAG("button: %c tap, zone %s: step refused (holster %d, next %d, held %d/%d, reloading %d,"
				" item busy %d)", "LR"[p], vrHolsterZoneName(zone), from, weapon, sState.held[VRH_L],
				sState.held[VRH_R], vrReloadHoldsSwitch(), vrHolsterItemBusy());
		vrHolsterBuzzRefused(p);
		return;
	}

	VRH_DIAG("button: %c tap, zone %s: step %d -> %d, held %d/%d -> %d/%d, gun side %c -> %c",
			"LR"[p], vrHolsterZoneName(zone), from, weapon, oldheld[VRH_L], oldheld[VRH_R],
			sState.held[VRH_L], sState.held[VRH_R], "LR"[oldside], "LR"[sState.gunside]);
	vrHolsterApplyHand(p, oldside, oldheld, oldparked, 1, "step");
	vrHolsterHud(bgunGetName(weapon));
}

/* Physical hand p's lower face button (A on the right controller, X on the left), through
 * inputVrFaceButton: logical hand p ^ gun side, whose lower button get_button_state names "a" on
 * logical 1 and "x" on logical 0 */
static s32 vrHolsterLowerButton(s32 p)
{
	s32 l = vrHolsterLogical(p);

	return inputVrFaceButton(l, l ? "a" : "x") ? 1 : 0;
}

/* The kind of a zone's menu and slot (the layout's): an item slot's own, else guns (VRH_GUN1, also
 * for no zone) */
static s32 vrHolsterZoneKind(s32 zone)
{
	s32 i = vrHolsterZoneSlot(zone);

	return vrHolsterIsItemSlot(i) ? i : VRH_GUN1;
}

/* PD-VRR name of a kind (vrHolsterZoneKind) */
static const char *vrHolsterKindName(s32 kind)
{
	return kind == VRH_POUCH ? "gadget" : kind == VRH_THROW ? "throwable" : "gun";
}

/* The inventory has a usable weapon of a kind (a slot, VRH_GUN1 for guns): the menu of that kind
 * would not be empty */
static s32 vrHolsterHasKind(s32 kind)
{
	s32 n = invGetCount();
	s32 i, w;

	for (i = 0; i < n; i++) {
		w = invGetWeaponNumByIndex(i);

		if (vrHolsterFitsSlot(w, kind) && vrHolsterUsable(w)) {
			return 1;
		}
	}

	return 0;
}

bool vrHolsterFaceButtons(bool allowed, bool detonating)
{
	struct player *player = g_Vars.currentplayer;
	bool use = false;
	s32 p;

	if (!vrHolsterActive()) {
		sFaceDown[VRH_L] = 0;
		sFaceDown[VRH_R] = 0;
		sMenuHand = -1;
		return false;
	}

	if (player->activemenumode == AMMODE_CLOSED) {
		sMenuHand = -1;
	}

	/* A tick without this call (a cutscene) counts as not allowed: a button held through it waits */
	if (g_Vars.lvframenum - sFaceFrame > 1) {
		allowed = false;
	}

	sFaceFrame = g_Vars.lvframenum;
	vrHolsterEnsureFilled();
	vrHolsterSyncInventory();

	for (p = 0; p < 2; p++) {
		s32 zone = sButtonZone[p];

		if (!vrHolsterLowerButton(p)) {
			if (allowed && !detonating && sFaceDown[p] > 0) {
				if (vrHolsterZoneSlot(zone) >= 0) {
					vrHolsterStep(p, vrHolsterZoneSlot(zone));
				} else {
					VRH_DIAG("button: %c tap, zone %s: use", "LR"[p], vrHolsterZoneName(zone));
					use = true;
				}
			}

			sFaceDown[p] = 0;
		} else if (!allowed || detonating || player->activemenumode != AMMODE_CLOSED) {
			/* Held while buttons are not handled (a detonation by A + B or a B tap, the other
			 * hand's menu): it does nothing until it is let go */
			sFaceDown[p] = -1;
		} else if (sFaceDown[p] >= 0) {
			if (sFaceDown[p] > TICKS(15)) {
				/* Outside the five zones a hand holding an item opens the menu of that item's kind,
				 * as if in its slot's zone, so a pick replaces the item in the hand (rule 45); in a
				 * zone, the zone's kind */
				s32 menuzone = zone;
				const char *why = zone == VRH_ZONE_NONE ? "default" : "the zone's kind";

				if (zone == VRH_ZONE_NONE && vrHolsterIsItemSlot(sState.held[p])) {
					menuzone = vrHolsterSlotZone(sState.held[p]);
					why = "the item in hand's kind";
				}

				/* A menu of a kind the inventory has none of would be an empty wheel: refused
				 * as a step is */
				if (!vrHolsterHasKind(vrHolsterZoneKind(menuzone))) {
					VRH_DIAG("button: %c hold, zone %s: %s menu (%s) refused (nothing of its"
							" kind)", "LR"[p], vrHolsterZoneName(zone),
							vrHolsterKindName(vrHolsterZoneKind(menuzone)), why);
					vrHolsterBuzzRefused(p);
				} else {
					sMenuHand = p;
					sMenuZone = menuzone;
					VRH_DIAG("button: %c hold, zone %s: %s menu (%s), held %d/%d", "LR"[p],
							vrHolsterZoneName(zone), vrHolsterKindName(vrHolsterZoneKind(menuzone)), why,
							sState.held[VRH_L], sState.held[VRH_R]);
					amOpen();

					/* Not opened (passive mode): no menu inherits this hand and zone */
					if (player->activemenumode == AMMODE_CLOSED) {
						vrHolsterMenuClosed();
					}
				}

				sFaceDown[p] = -1;
			} else {
				sFaceDown[p] += g_Vars.lvupdate60;
			}
		}
	}

	return use;
}

s32 vrHolsterMenuHand(void)
{
	if (!vrHolsterActive() || g_Vars.currentplayer->activemenumode == AMMODE_CLOSED) {
		return -1;
	}

	return sMenuHand;
}

s32 vrHolsterMenuButton(void)
{
	s32 p = vrHolsterMenuHand();

	return p < 0 ? -1 : vrHolsterLowerButton(p);
}

/* The kind the hold-A/X menu shows (a slot, VRH_GUN1 for guns): a menu opened by
 * vrHolsterFaceButtons in an item slot's zone, or outside the zones by a hand holding an item,
 * shows that slot's kind; anywhere else, and a menu opened by the vanilla code path, shows guns */
static s32 vrHolsterMenuKind(void)
{
	return vrHolsterMenuHand() >= 0 ? vrHolsterZoneKind(sMenuZone) : VRH_GUN1;
}

void vrHolsterMenuClosed(void)
{
	sMenuHand = -1;
	sMenuZone = VRH_ZONE_NONE;
}

bool vrHolsterMenuOffers(s32 weaponnum)
{
	return vrHolsterActive() && vrHolsterFitsSlot(weaponnum, vrHolsterMenuKind()) && vrHolsterUsable(weaponnum);
}

bool vrHolsterOnMenuPickHand(s32 weaponnum)
{
	s32 p = vrHolsterMenuHand();
	s32 oldside, ok;
	s32 oldheld[2], oldparked[2];

	if (p < 0) {
		return vrHolsterOnMenuPick(weaponnum);
	}

	/* Fists, keys and other items keep the vanilla equip (the menu with holsters does not show
	 * them) */
	if (vrHolsterSlotOf(weaponnum) < 0) {
		return false;
	}

	vrHolsterEnsureFilled();
	vrHolsterSyncInventory();

	oldside = sState.gunside;
	vrHolsterSaveHands(oldheld, oldparked);

	/* The core takes the zone as the slot (rule 45): a weapon of another kind than the menu's
	 * (which shows only its own) would land in the wrong slot */
	ok = vrHolsterFitsSlot(weaponnum, vrHolsterMenuKind()) && vrHolsterUsable(weaponnum)
		&& vrhOnMenuPickHand(&sState, p, sMenuZone, weaponnum, vrHolsterGunSideBusy() ? 1 : 0);

	VRH_DIAG("holster menu pick: %c, zone %s, weapon %d: %s, slots %d/%d/%d/%d/%d, held %d/%d -> %d/%d,"
			" gun side %c -> %c, gun side busy %d", "LR"[p], vrHolsterZoneName(sMenuZone), weaponnum,
			ok ? "done" : "refused", sState.holster[VRH_GUN1], sState.holster[VRH_GUN2],
			sState.holster[VRH_GUN3], sState.holster[VRH_POUCH], sState.holster[VRH_THROW],
			oldheld[VRH_L], oldheld[VRH_R], sState.held[VRH_L], sState.held[VRH_R], "LR"[oldside],
			"LR"[sState.gunside], vrHolsterGunSideBusy());

	if (ok) {
		vrHolsterApplyHand(p, oldside, oldheld, oldparked, 0, "menu pick");
	} else {
		vrHolsterBuzzRefused(p);
	}

	return true;
}

s32 vrHolsterGrabEdge(void)
{
	s32 edge = 0;
	s32 p;

	if (!vrHolsterActive()) {
		sGrabHand = -1;
		sGrabPrev[VRH_L] = 1;
		sGrabPrev[VRH_R] = 1;
		return 0;
	}

	vrHolsterEnsureFilled();

	for (p = 0; p < 2; p++) {
		s32 grip = get_button_state(vrHolsterLogical(p), "grip") ? 1 : 0;

		if (grip && !sGrabPrev[p]) {
			s32 zone = vrHolsterZone(gCtrlPos[vrHolsterPoseIndex(p)], NULL);

			/* Not a press that draws, swaps or buzzes empty (any press in one of the five zones,
			 * the shoulders too while they are the reload's), nor one by a hand holding a gun or an
			 * item (parked counts) or one during a reload (the free hand's grip is the reload's,
			 * as it is while the gun waits for a grip's rack, which vrReloadBusy no longer counts) */
			if (sGrabHand < 0 && edge == 0 && sState.held[p] < 0 && !vrReloadBusy()
					&& !vrReloadNeedsGripRack(HAND_RIGHT)
					&& vrCtrlPoseGood(vrHolsterPoseIndex(p)) && zone == VRH_ZONE_NONE) {
				sGrabHand = p;
				edge = 1;
			}
		} else if (!grip && sGrabPrev[p] && p == sGrabHand) {
			sGrabHand = -1;
			edge = -1;
		}

		sGrabPrev[p] = grip;
	}

	return edge;
}

void vrHolsterGrabResult(bool grabbed)
{
	if (!grabbed) {
		sGrabHand = -1;
	}
}

s32 vrHolsterGrabPose(void)
{
	return vrHolsterActive() && sGrabHand >= 0 ? vrHolsterPoseIndex(sGrabHand) : -1;
}

bool vrHolsterEngineSwitch(void)
{
	s32 weapon, dual;

	if (!vrHolsterActive()) {
		return false;
	}

	vrHolsterEnsureFilled();
	vrHolsterSyncInventory();
	vrhWeapon(&sState, &weapon, &dual);
	vrHolsterEquip(weapon, dual);

	return true;
}

/* The remote mines thrown by the current player in the prop list from prop on (and their children:
 * a mine stuck to a chr or an object is its child) that have not gone off: a weapon prop marked at
 * the throw (OBJFLAG3_00000008, bgunCreateThrownProjectile) with the player's owner bits, which the
 * detonation matches (weaponTick). A remote mine lying in the level as a pickup has no such mark. */
static s32 vrHolsterCountPlacedRemoteMines(struct prop *prop)
{
	s32 n = 0;

	for (; prop != NULL; prop = prop->next) {
		if (prop->type == PROPTYPE_WEAPON && prop->weapon != NULL) {
			struct weaponobj *weapon = prop->weapon;
			struct defaultobj *obj = &weapon->base;

			if (weapon->weaponnum == WEAPON_REMOTEMINE && (obj->flags3 & OBJFLAG3_00000008)
					&& (obj->hidden & OBJHFLAG_DELETING) == 0 && weapon->timer240 != -1
					&& (s32) ((obj->hidden & 0xf0000000) >> 28) == g_Vars.currentplayernum) {
				n++;
			}
		}

		if (prop->child != NULL) {
			n += vrHolsterCountPlacedRemoteMines(prop->child);
		}
	}

	return n;
}

bool vrHolsterBTapDetonates(void)
{
	s32 weapon, mines;

	if (!vrHolsterActive()) {
		return false;
	}

	weapon = bgunGetWeaponNum(HAND_RIGHT);

	if (weapon != WEAPON_REMOTEMINE && weapon != WEAPON_UNARMED) {
		return false;
	}

	/* The whole list: the active props, then the paused ones (activepropstail->next) */
	mines = vrHolsterCountPlacedRemoteMines(g_Vars.activeprops);

	if (weapon == WEAPON_UNARMED && mines == 0) {
		return false;
	}

	VRH_DIAG("button: B tap: detonate, weapon %d, remote mines placed %d", weapon, mines);

	return true;
}

bool vrHandParked(s32 h)
{
	s32 p;

	if (!vrHolsterActive() || (h != HAND_RIGHT && h != HAND_LEFT)) {
		return false;
	}

	/* Only a pair's copy is the engine's: a gun parked behind an item, or after it until its grip
	 * closes (rule 41), is not in the engine at all (it holds the item, or fists), so the hide,
	 * fire and copy-weapon gates must not act on the item hand or a fist */
	if (!vrHolsterIsPair(sState.held)) {
		return false;
	}

	p = h == HAND_RIGHT ? sState.gunside : 1 - sState.gunside;

	return sState.parked[p] != 0;
}

bool vrReloadEjectButton(s32 h)
{
	if ((h != HAND_RIGHT && h != HAND_LEFT) || !vrReloadOwnsGun(h)) {
		return false;
	}

	/* A function's ammo with no row is the engine's (vrReloadOwnsAmmo; none today: the SuperDragon's
	 * grenade launcher has its own row since Task F9e, so B ejects its magazine): B does the engine's
	 * reload of it (the caller's bgunReloadIfPossible), except while the free hand is busy with the
	 * row's clip or rack, when the press is refused with the two weak pulses of a refused Y flip, on
	 * the gun hand's controller (logical 1). The left gun of a pair (no row's gun with a second ammo
	 * dual wields) keeps the Y flip */
	if (h == HAND_RIGHT && !vrReloadOwnsSelectedFunc(HAND_RIGHT)) {
		if (vrReloadFreeHandBusy(HAND_RIGHT)) {
			VRH_DIAG("holster reload B: refused, the selected function's reload waits for the free hand");
			vrHolsterBuzzRefused(vrHolsterLogical(1));
			return true;
		}

		return false;
	}

	/* The left gun of a pair (owned, so manual reload and the holsters are on): the flip waits for
	 * the next tick, where every gun side change happens (vrHolsterApplyYFlip) */
	if (h == HAND_LEFT) {
		sYFlipPending = 1;
		sYFlipFrame = g_Vars.lvframenum;
	} else if (vrReloadEjectRefused()) {
		/* A gun that opens itself after the shot (the Rocket Launcher's hatch) keeps its round: the
		 * two weak pulses of a refused Y flip, on the gun hand's controller */
		VRH_DIAG("holster reload B: refused, the gun opens after its shot and has its round in");
		vrHolsterBuzzRefused(vrHolsterLogical(1));
	} else {
		vrReloadRequestEject(HAND_RIGHT);
	}

	return true;
}

bool vrHolsterGripAimBlocked(void)
{
	struct gunctrl *ctrl;

	if (!vrHolsterActive()) {
		return false;
	}

	/* A pending draw counts: the grip is already holding the gun or item that is coming. An item
	 * blocks too (the horizon scanner is in sight mode while equipped anyway). */
	ctrl = &g_Vars.currentplayer->gunctrl;

	return vrHolsterSlotOf(ctrl->weaponnum) >= 0
		|| (ctrl->switchtoweaponnum >= 0 && vrHolsterSlotOf(ctrl->switchtoweaponnum) >= 0);
}

void vrHolsterLogMark(void)
{
	static const char names[2] = { 'L', 'R' };
	s32 p;

	/* The sounds started in the last 10 s, to name one heard in the headset */
	sndDumpRecent();

	VRH_DIAG("holster mark: body frame cm (x right, y up, z back, unmirrored); pref=%d side=%d"
			" hip(%.1f %.1f %.1f)-(%.1f %.1f %.1f) r%.0f shoulder(%.1f %.1f %.1f) r%.0f"
			" chest(%.1f %.1f %.1f)-(%.1f %.1f %.1f) r%.0f rhip(%.1f %.1f %.1f)-(%.1f %.1f %.1f) r%.0f"
			" lshoulder(%.1f %.1f %.1f) r%.0f, x negated when pref=1, distances to the segments;"
			" layout hip %d shoulder %d chest %d rhip %d lshoulder %d",
			VrLeftHandedPref, VrLeftHandedMode,
			VRH_HIP_X, VRH_HIP_Y, VRH_HIP_Z, VRH_HIP_X2, VRH_HIP_Y2, VRH_HIP_Z2, VRH_HIP_RAD,
			VRH_SHOULDER_X, VRH_SHOULDER_Y, VRH_SHOULDER_Z, VRH_SHOULDER_RAD,
			VRH_CHEST_X, VRH_CHEST_Y, VRH_CHEST_Z, VRH_CHEST_X2, VRH_CHEST_Y2, VRH_CHEST_Z2, VRH_CHEST_RAD,
			VRH_RHIP_X, VRH_RHIP_Y, VRH_RHIP_Z, VRH_RHIP_X2, VRH_RHIP_Y2, VRH_RHIP_Z2, VRH_RHIP_RAD,
			VRH_LSHOULDER_X, VRH_LSHOULDER_Y, VRH_LSHOULDER_Z, VRH_LSHOULDER_RAD,
			sState.layout[VRH_ZONE_HIP], sState.layout[VRH_ZONE_SHOULDER], sState.layout[VRH_ZONE_CHEST],
			sState.layout[VRH_ZONE_RHIP], sState.layout[VRH_ZONE_LSHOULDER]);

	for (p = 0; p < 2; p++) {
		f32 body[3], dist[VRH_NUM_ZONES - 1];
		const f32 *pos = gCtrlPos[vrHolsterPoseIndex(p)];
		s32 zone = vrHolsterZone(pos, dist);

		const f32 *q = gCtrlQuat[vrHolsterPoseIndex(p)];

		vrBodyLocal(pos, body);
		VRH_DIAG("holster mark %c pos(%.1f %.1f %.1f) zone=%d hip=%.1f shoulder=%.1f chest=%.1f rhip=%.1f"
				" lshoulder=%.1f tracked=%d raw(%.2f %.2f %.2f) q(%.4f %.4f %.4f %.4f)", names[p], body[0],
				body[1], body[2], zone, dist[0], dist[1], dist[2], dist[3], dist[4],
				vrCtrlPoseGood(vrHolsterPoseIndex(p)), pos[0], pos[1], pos[2], q[0], q[1], q[2], q[3]);
	}
}

/* The layout index (zone - 1) of a Holster Layout spot by its physical side: the zones are the
 * setting's gun hand's, so left-handed the left side is the gun hand's. -1 for a bad spot. */
static s32 vrHolsterSpotIndex(s32 spot)
{
	static const s32 right[VRH_NUM_SPOTS] = {
		VRH_ZONE_LSHOULDER, VRH_ZONE_SHOULDER, VRH_ZONE_CHEST, VRH_ZONE_HIP, VRH_ZONE_RHIP,
	};
	static const s32 left[VRH_NUM_SPOTS] = {
		VRH_ZONE_SHOULDER, VRH_ZONE_LSHOULDER, VRH_ZONE_CHEST, VRH_ZONE_RHIP, VRH_ZONE_HIP,
	};

	if (spot < 0 || spot >= VRH_NUM_SPOTS) {
		return -1;
	}

	return (VrLeftHandedPref ? left : right)[spot] - 1;
}

s32 vrHolsterLayoutGet(s32 spot)
{
	s32 k = vrHolsterSpotIndex(spot);

	return k < 0 ? 0 : VrHolsterLayout[k];
}

void vrHolsterLayoutSet(s32 spot, s32 slot)
{
	s32 k = vrHolsterSpotIndex(spot);
	s32 j;

	if (k < 0 || slot < 0 || slot >= VRH_NUM_SLOTS || VrHolsterLayout[k] == slot) {
		return;
	}

	/* The spot that had slot takes this spot's old one, so the layout stays a permutation */
	for (j = 0; j < VRH_NUM_ZONES - 1; j++) {
		if (VrHolsterLayout[j] == slot) {
			VrHolsterLayout[j] = VrHolsterLayout[k];
		}
	}

	VrHolsterLayout[k] = slot;
	vrHolsterApplyLayout("menu");
}

void vrHolsterLayoutReset(void)
{
	struct vrhstate def;
	s32 j;

	/* The core's default (rule 51) */
	vrhInit(&def, WEAPON_NONE, WEAPON_NONE, VRH_R);

	for (j = 0; j < VRH_NUM_ZONES - 1; j++) {
		VrHolsterLayout[j] = def.layout[j + 1];
	}

	vrHolsterApplyLayout("default");
}

bool vrHolsterIsDevice(s32 weaponnum)
{
	switch (weaponnum) {
	case WEAPON_NIGHTVISION:
	case WEAPON_XRAYSCANNER:
	case WEAPON_IRSCANNER:
	case WEAPON_RTRACKER:
	case WEAPON_CLOAKINGDEVICE:
	case WEAPON_EYESPY:
		return true;
	}

	return false;
}

bool vrHolsterIsTriggerItem(s32 weaponnum)
{
	return vrHolsterIsDevice(weaponnum) || weaponnum == WEAPON_DISGUISE40 || weaponnum == WEAPON_DISGUISE41;
}

bool vrHolsterHoldsDevice(void)
{
	s32 weaponnum = bgunGetWeaponNum(HAND_RIGHT);

	return vrHolsterActive() && (vrHolsterIsTriggerItem(weaponnum) || vrHolsterIsG5Detonator(weaponnum));
}

bool vrHolsterDeviceTrigger(void)
{
	s32 weaponnum = bgunGetWeaponNum(HAND_RIGHT);
	s32 state;
	s32 on;

	if (!vrHolsterHoldsDevice()) {
		return false;
	}

	/* The diagnostic mark is trigger + stick click (vrReloadTick): not a use */
	if (get_button_state(VrLeftHandedPref ? 1 : 0, "thumbstick_click")) {
		return false;
	}

	/* G5 Building's Detonator: the caller detonates, as a B tap does */
	if (vrHolsterIsG5Detonator(weaponnum)) {
		VRH_DIAG("device: G5 detonator: detonate by the trigger");
		vrHolsterPulse(sState.gunside, VRH_BUZZ_DRAW_AMP, VRH_BUZZ_DRAW_SECS, 0);
		return true;
	}

	/* The disguise goes on and leaves the hand (bgunWearDisguise), as the original's draw does */
	if (weaponnum == WEAPON_DISGUISE40 || weaponnum == WEAPON_DISGUISE41) {
		VRH_DIAG("device: disguise %d worn by the trigger", weaponnum);
		vrHolsterPulse(sState.gunside, VRH_BUZZ_DRAW_AMP, VRH_BUZZ_DRAW_SECS, 0);
		bgunWearDisguise(weaponnum);
		return false;
	}

	state = currentPlayerGetDeviceState(weaponnum);

	if (state == DEVICESTATE_UNEQUIPPED) {
		return false;
	}

	/* As the pause Inventory's pick: the vision modes stay exclusive, and the CamSpy turned on
	 * launches next tick (playerTick: eyespyTryLaunch), or takes control of one already out */
	on = state == DEVICESTATE_INACTIVE;
	currentPlayerSetDeviceActive(weaponnum, on);
	vrHolsterItemHud(weaponnum, on ? "on" : "off");
	vrHolsterPulse(sState.gunside, VRH_BUZZ_DRAW_AMP, VRH_BUZZ_DRAW_SECS, 0);
	VRH_DIAG("device: weapon %d %s by the trigger", weaponnum, on ? "on" : "off");

	return false;
}
