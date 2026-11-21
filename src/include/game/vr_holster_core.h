#ifndef VR_HOLSTER_CORE_H
#define VR_HOLSTER_CORE_H
#include <stdint.h>

/*
 * Pure holster rules: what each physical hand holds, what each holster contains, the engine
 * weapon, dual and the gun side. No engine headers: the engine #defines bool as s32, so every
 * flag here is an int32_t that is 0 or 1. The engine adapter (vr_holster.c) feeds one vrhinput
 * per gameplay tick and applies the vrhoutput. The numbered rules are in
 * docs/plans/2026-10-15-vr-holsters-phase2-plan.md, Task A4. Tick order, pause, untracked
 * hands and fx details are its rules 24-26. Releases run before presses so a hand putting a gun
 * back and the other hand drawing in the same tick do not depend on which hand is which.
 */

/* Physical hands */
#define VRH_L 0
#define VRH_R 1

/* Holster slots (rules 36 and 48): the three gun holsters (filled in this order, rule 53), then the
 * two item slots. The core does not know weapon kinds: the adapter puts only guns in the gun
 * holsters and only its own kind in each item slot. Where a slot is on the body is the layout's
 * (rule 49). */
#define VRH_GUN1      0
#define VRH_GUN2      1
#define VRH_GUN3      2
#define VRH_POUCH     3 /* held gadgets */
#define VRH_THROW     4 /* throwables */
#define VRH_NUM_GUNS  3
#define VRH_NUM_SLOTS 5

/* vrhinput.zone values: body spots (rule 49), relative to the gun hand of the left-handed setting */
#define VRH_ZONE_NONE      0
#define VRH_ZONE_HIP       1 /* the off-hand hip */
#define VRH_ZONE_SHOULDER  2 /* the gun-hand shoulder */
#define VRH_ZONE_CHEST     3
#define VRH_ZONE_RHIP      4 /* the gun-hand hip */
#define VRH_ZONE_LSHOULDER 5 /* the off-hand shoulder */
#define VRH_NUM_ZONES      6

/* vrhoutput.fx: one-tick events, one byte per physical hand */
#define VRH_FX_DRAW    0x01
#define VRH_FX_HOLSTER 0x02
#define VRH_FX_SWAP    0x04
#define VRH_FX_EMPTY   0x08 /* a press found no gun or item to take */
#define VRH_FX_ZONE    0x10 /* the hand entered a zone where it can draw or swap */
#define VRH_FX_SHIFT(h) (8 * (h))
#define VRH_FX_HAS(fx, h, bit) ((((fx) >> VRH_FX_SHIFT(h)) & (bit)) != 0)

struct vrhstate {
	int32_t holster[VRH_NUM_SLOTS]; /* weapon number in each slot, 0 (WEAPON_NONE) when empty; it
	                       * stays while a hand holds that gun or item */
	int32_t held[2];      /* per physical hand: slot the hand's gun or item came from, -1 empty */
	int32_t gunside;      /* physical hand of the engine's HAND_RIGHT (the primary gun, or the item) */
	int32_t parked[2];    /* per physical hand: its gun is hidden while the other gun reloads (Stage B)
	                       * or while the other hand holds an item (rule 39), and after the reload or
	                       * the item while its grip stays open (rules 9 and 41) */
	int32_t prevgrip[2];
	int32_t prevzone[2];  /* per physical hand: VRH_ZONE_* last tick, after the reload rule */
	int32_t layout[VRH_NUM_ZONES]; /* the slot of each zone (rule 49), -1 for VRH_ZONE_NONE */
};

struct vrhinput {
	int32_t grip[2];      /* per physical hand */
	int32_t zone[2];      /* per physical hand: VRH_ZONE_* (a body spot) */
	int32_t haspair[VRH_NUM_GUNS]; /* per gun holster (items never pair): the inventory holds two of
	                       * that gun */
	int32_t hasknifepair; /* player has at least 2 combat knives for dual-wielding */
	int32_t reloading;    /* the gun side's gun is busy (the adapter's choice: since Task H3d a
	                       * manual reload only while the free hand holds a clip or round, racks,
	                       * slaps or an eject is pending, and with a pair any reload state but
	                       * ready): both shoulders belong to the reload (rules 18 and 52), the
	                       * free hand's gun parks at once (rule 33) and the free hand neither
	                       * draws nor buzzes (rules 29, 30) */
	int32_t tracked[2];
	int32_t paused;
};

struct vrhoutput {
	int32_t weapon;       /* engine weapon to hold: the held gun or item, or 0 for fists */
	int32_t dual;         /* both hands hold the gun */
	int32_t gunside;      /* physical hand for HAND_RIGHT */
	int32_t fx;           /* VRH_FX_* one-tick events per hand: draw, holster, swap, empty, zone */
};

/* Empty hands, prevgrip 1 (a held grip must be squeezed again), gun1 and gun2 in VRH_GUN1 and
 * VRH_GUN2 (0 for an empty holster), VRH_GUN3 and both item slots 0, the default layout (rule 51).
 * A gunside other than VRH_L or VRH_R is taken as VRH_R. */
void vrhInit(struct vrhstate *s, int32_t gun1, int32_t gun2, int32_t gunside);
void vrhTick(struct vrhstate *s, const struct vrhinput *in, struct vrhoutput *out);
/* A picked-up gun fills the first empty gun holster (VRH_GUN1, 2, 3: rule 53); returns the holster
 * or -1 (it goes to the wheel). Item slots are never filled here (rule 46). */
int32_t vrhOnPickup(struct vrhstate *s, int32_t weapon);
/* Rules 23 and 55: a gun chosen in the pause-menu inventory: replaces the gun in the hand (and its
 * holster), else fills VRH_GUN1; a hand holding an item counts as empty (rule 46). The hold-A/X menu
 * uses vrhOnMenuPickHand (rule 35). */
void vrhOnMenuPick(struct vrhstate *s, int32_t weapon);
/* Rules 27 and 42: the engine weapon (the gun side's, else the other hand's unless parked) and dual
 * the state gives (vrhoutput.weapon and .dual); changes nothing */
void vrhWeapon(const struct vrhstate *s, int32_t *weapon, int32_t *dual);
/* Rules 28 and 43: a gun or item that left the inventory leaves its slot (any of the five); a hand
 * holding it is empty */
void vrhOnLost(struct vrhstate *s, int32_t weapon);
/* Rule 32: both hands hold and neither is parked: the gun side moves to the other hand, returns
 * 1. Otherwise nothing changes and it returns 0. A left-gun reload flips first, so the reloading
 * gun is always the gun side's. */
int32_t vrhFlipGunSide(struct vrhstate *s);
/* Rules 34, 45 and 56: hand h tapped A/X in slot i's zone, weapon is the next gun or item of slot
 * i's kind; returns 1 on a change */
int32_t vrhStepHolster(struct vrhstate *s, int32_t h, int32_t i, int32_t weapon, int32_t reloading);
/* Rules 35, 45 and 56: a hold-A/X menu pick by hand h in zone (VRH_ZONE_*, already after rules 18
 * and 52: the caller counts both shoulders as no zone while reloading); returns 1 on a change */
int32_t vrhOnMenuPickHand(struct vrhstate *s, int32_t h, int32_t zone, int32_t weapon, int32_t reloading);
/* Rule 47: item slot i gets weapon (0 empties it) unless a hand holds its item; returns 1 on a change */
int32_t vrhSetItem(struct vrhstate *s, int32_t i, int32_t weapon);
/* Rule 50: the layout, slot_of_zone[z - 1] the slot of zone z (VRH_ZONE_HIP to VRH_ZONE_LSHOULDER):
 * taken only as a permutation of the five slots; returns 1 on a change, 0 when refused or the same */
int32_t vrhSetLayout(struct vrhstate *s, const int32_t *slot_of_zone);

#endif
