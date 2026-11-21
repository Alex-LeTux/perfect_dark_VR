/* Pure holster state machine: see vr_holster_core.h. No engine dependencies. */
#include "game/vr_holster_core.h"

#include <string.h>

/* Rule 51: the game's layout, in zone order (hip, shoulder, chest, right hip, left shoulder): gun 1
 * on the gun-hand shoulder, gun 2 on the other shoulder, gun 3 on the off-hand hip */
static const int32_t vrhDefaultLayout[VRH_NUM_ZONES - 1] = {
	VRH_GUN3, VRH_GUN1, VRH_POUCH, VRH_THROW, VRH_GUN2,
};

void vrhInit(struct vrhstate *s, int32_t gun1, int32_t gun2, int32_t gunside)
{
	int32_t z;

	memset(s, 0, sizeof(*s));
	s->holster[VRH_GUN1] = gun1;
	s->holster[VRH_GUN2] = gun2;
	s->held[VRH_L] = -1;
	s->held[VRH_R] = -1;
	s->gunside = gunside == VRH_L ? VRH_L : VRH_R;

	/* Rule 20: a grip held through a level start or death is not a press */
	s->prevgrip[VRH_L] = 1;
	s->prevgrip[VRH_R] = 1;

	s->layout[VRH_ZONE_NONE] = -1;

	for (z = 1; z < VRH_NUM_ZONES; z++) {
		s->layout[z] = vrhDefaultLayout[z - 1];
	}
}

/* Rule 50 */
int32_t vrhSetLayout(struct vrhstate *s, const int32_t *slot_of_zone)
{
	int32_t seen = 0, changed = 0;
	int32_t z;

	if (slot_of_zone == 0) {
		return 0;
	}

	for (z = 0; z < VRH_NUM_ZONES - 1; z++) {
		int32_t i = slot_of_zone[z];

		if (i < 0 || i >= VRH_NUM_SLOTS || (seen & (1 << i))) {
			return 0;
		}

		seen |= 1 << i;
	}

	for (z = 1; z < VRH_NUM_ZONES; z++) {
		if (s->layout[z] != slot_of_zone[z - 1]) {
			s->layout[z] = slot_of_zone[z - 1];
			changed = 1;
		}
	}

	return changed;
}

/* Rules 36 and 48: slots 3 and 4 hold items; only the three gun holsters swap or pair */
static int32_t vrhIsGunHolster(int32_t i)
{
	return i >= VRH_GUN1 && i < VRH_NUM_GUNS;
}

static int32_t vrhIsItemSlot(int32_t i)
{
	return i == VRH_POUCH || i == VRH_THROW;
}

/* The gun holster other than i that holds weapon, -1 for none (rules 54 to 56: the partner of the
 * swaps and checks of rules 6, 23, 34 and 35) */
static int32_t vrhOtherGunHolster(const struct vrhstate *s, int32_t i, int32_t weapon)
{
	int32_t j;

	for (j = VRH_GUN1; j < VRH_NUM_GUNS; j++) {
		if (j != i && s->holster[j] == weapon) {
			return j;
		}
	}

	return -1;
}

/* Rules 18 and 52: while reloading both shoulders are the reload's, whatever slots they hold */
static int32_t vrhEffectiveZone(int32_t zone, int32_t reloading)
{
	if (zone <= VRH_ZONE_NONE || zone >= VRH_NUM_ZONES) {
		return VRH_ZONE_NONE;
	}

	if (reloading && (zone == VRH_ZONE_SHOULDER || zone == VRH_ZONE_LSHOULDER)) {
		return VRH_ZONE_NONE;
	}

	return zone;
}

/* Rule 49: the layout's slot of a zone, -1 for none */
static int32_t vrhZoneHolster(const struct vrhstate *s, int32_t zone)
{
	if (zone <= VRH_ZONE_NONE || zone >= VRH_NUM_ZONES) {
		return -1;
	}

	return s->layout[zone];
}

static void vrhEmptyHand(struct vrhstate *s, int32_t h)
{
	s->held[h] = -1;
	s->parked[h] = 0;
}

/* Rules 9, 41 and 43: hand h's gun comes back; next to an empty hand it is the gun side (the item
 * went home or was lost). No fx: the callers that have an output add it. */
static void vrhUnparkHand(struct vrhstate *s, int32_t h)
{
	s->parked[h] = 0;

	if (s->held[1 - h] < 0) {
		s->gunside = h;
	}
}

/* Adds one hand's VRH_FX_* bit to the tick's fx */
static void vrhFx(int32_t *fx, int32_t h, int32_t bit)
{
	*fx |= bit << VRH_FX_SHIFT(h);
}

/* Rules 9 and 41: out of a reload and out of a pause, a parked hand whose grip is held unparks
 * unless the other hand holds an item (rule 39). A hand that unparks next to an empty hand is the
 * gun side (rule 41: the item went home, rule 40, or was lost, rule 43); without items the other
 * hand of a parked hand always holds the pair's other copy, so this never moves the gun side. */
static void vrhUnpark(struct vrhstate *s, const int32_t *grip, int32_t *fx)
{
	int32_t h;

	for (h = 0; h < 2; h++) {
		if (s->parked[h] && grip[h] && (!vrhIsItemSlot(s->held[1 - h]) || s->held[1 - h] == s->held[h])) {
			vrhUnparkHand(s, h);
			vrhFx(fx, h, VRH_FX_DRAW);
		}
	}
}

/* Rule 14: entering a zone where a press would draw, or a release would swap */
static int32_t vrhZoneBuzz(const struct vrhstate *s, const struct vrhinput *in, int32_t h, int32_t hol)
{
	int32_t o = 1 - h;

	/* Rule 30: while reloading the free hand's press does nothing (rule 29), so no buzz. Rule 39:
	 * nor for a hand parked behind an item. */
	if (s->parked[h] || (in->reloading && h != s->gunside)) {
		return 0;
	}

	/* Rule 44: an item slot buzzes only for an empty hand, and never when the other hand holds its
	 * item (items never pair, except throwable knife with pair) */
	if (vrhIsItemSlot(hol)) {
		if (hol == VRH_THROW && s->held[o] == hol && in->hasknifepair) {
			return s->held[h] < 0;
		}
		return s->held[h] < 0 && s->holster[hol] != 0 && s->held[o] != hol;
	}

	if (s->held[h] < 0) {
		return s->holster[hol] != 0 && (s->held[o] != hol || in->haspair[hol]);
	}

	/* Rule 44: an item released at a gun holster goes home, so no buzz there */
	if (vrhIsItemSlot(s->held[h])) {
		return 0;
	}

	return s->held[h] != hol && s->held[o] != s->held[h];
}

/* Grip release by hand h; hol is the slot of its zone, -1 for none */
static void vrhRelease(struct vrhstate *s, int32_t h, int32_t hol, int32_t *fx)
{
	int32_t o = 1 - h;
	int32_t src = s->held[h];
	int32_t tmp;

	/* Rule 9: a parked hand's release does nothing. While reloading every hand that holds and is
	 * not the gun side was parked at the start of the tick (rule 33), and rule 31 empties a
	 * parked hand when the gun side lets go, so no release here can need parking. Rule 39: a hand
	 * parked behind an item does nothing either. */
	if (src < 0 || s->parked[h]) {
		return;
	}

	/* Rules 7 and 17: one copy of a pair goes away, the other hand keeps the gun single. Items
	 * never pair (rule 37), so this is always a gun holster. */
	if (s->held[o] == src) {
		vrhEmptyHand(s, h);

		if (s->gunside == h) {
			s->gunside = o;

			/* Rule 31: a parked copy goes home with the gun side's, reloading or not, so both
			 * guns go away with one fx per hand. A release of o in this same tick changes
			 * nothing: before h's it is a parked release, after it o is already empty.
			 * A pause never gets here, because rule 11 holsters parked guns itself. */
			if (s->parked[o]) {
				vrhEmptyHand(s, o);
				vrhFx(fx, o, VRH_FX_HOLSTER);
			}
		}

		vrhFx(fx, h, VRH_FX_HOLSTER);
		return;
	}

	/* Rule 40: the gun side stays on this hand, now empty; a gun parked behind an item comes back
	 * by rule 41 */
	vrhEmptyHand(s, h);

	/* Rules 6 and 54: at another gun holster the two swap (the other may be empty), else it goes
	 * home. Rule 40: an item always goes home, and a gun let go in an item zone too. */
	if (vrhIsGunHolster(src) && vrhIsGunHolster(hol) && hol != src) {
		tmp = s->holster[hol];
		s->holster[hol] = s->holster[src];
		s->holster[src] = tmp;
		vrhFx(fx, h, VRH_FX_SWAP);
		return;
	}

	vrhFx(fx, h, VRH_FX_HOLSTER);
}

/* Rule 37: grip press by empty hand h at item slot hol */
static void vrhPressItem(struct vrhstate *s, const struct vrhinput *in, int32_t h, int32_t hol, int32_t *fx)
{
	int32_t o = 1 - h;

	/* An empty slot */
	if (s->holster[hol] == 0) {
		vrhFx(fx, h, VRH_FX_EMPTY);
		return;
	}

	/* The other hand holds this item: no pairs of items, except throwable knife with pair */
	if (s->held[o] == hol) {
		if (hol == VRH_THROW && in->hasknifepair) {
			s->held[h] = hol;
			vrhFx(fx, h, VRH_FX_DRAW);
			return;
		}
		vrhFx(fx, h, VRH_FX_EMPTY);
		return;
	}

	if (vrhIsItemSlot(s->held[o])) {
		/* The other item goes back */
		vrhEmptyHand(s, o);
		vrhFx(fx, o, VRH_FX_HOLSTER);
	} else if (s->held[o] >= 0) {
		/* The engine holds an item only as HAND_RIGHT: the other hand's gun (single by rule 36,
		 * maybe already parked) hides until the item is away (rules 39 and 41) */
		s->parked[o] = 1;
	}

	s->held[h] = hol;
	s->gunside = h;
	vrhFx(fx, h, VRH_FX_DRAW);
}

/* Grip press by hand h; hol is the slot of its zone, -1 for none */
static void vrhPress(struct vrhstate *s, const struct vrhinput *in, int32_t h, int32_t hol, int32_t *fx)
{
	int32_t o = 1 - h;

	/* Rules 19 and 29: while reloading the free hand's press does nothing in any zone (no draw,
	 * no fx; a Falcon rack near the hip draws nothing; its parked copy stays parked). The edge is still consumed:
	 * vrhTick updates prevgrip, so a later release is a normal release edge (rule 9). */
	if (in->reloading && h != s->gunside) {
		return;
	}

	/* A parked hand holds, so it stops here. A press is a held grip, so out of a reload and with no
	 * item in the other hand vrhTick has already unparked the hand (rules 9 and 41) and it holds a
	 * gun: the press does nothing, with no second fx. During a reload only the free hand is parked
	 * (rule 33; rules 31 and 32 keep the gun side unparked), and rule 29 has taken its press. A
	 * hand parked behind an item stays parked: its press does nothing (rule 39). */
	if (s->held[h] >= 0 || hol < 0) {
		return;
	}

	if (vrhIsItemSlot(hol)) {
		vrhPressItem(s, in, h, hol, fx);
		return;
	}

	/* Rule 3: the other hand holds this holster's gun (parked counts): take the second copy if
	 * there is one */
	if (s->held[o] == hol) {
		if (in->haspair[hol]) {
			s->held[h] = hol;
			vrhFx(fx, h, VRH_FX_DRAW);
		} else {
			vrhFx(fx, h, VRH_FX_EMPTY);
		}

		return;
	}

	/* Rule 1; rule 38: an item in the other hand stays */
	if (s->holster[hol] == 0) {
		vrhFx(fx, h, VRH_FX_EMPTY);
		return;
	}

	/* Rule 4: the other hand's gun from another gun holster goes back. Rule 38: so does an item. */
	if (s->held[o] >= 0) {
		vrhEmptyHand(s, o);
		vrhFx(fx, o, VRH_FX_HOLSTER);
	}

	/* Rules 1, 2 and 4 */
	s->held[h] = hol;
	s->gunside = h;
	vrhFx(fx, h, VRH_FX_DRAW);
}

void vrhTick(struct vrhstate *s, const struct vrhinput *in, struct vrhoutput *out)
{
	int32_t grip[2], zone[2], releasing[2], wasdual, h, fx = 0;

	for (h = 0; h < 2; h++) {
		grip[h] = in->tracked[h] ? in->grip[h] : s->prevgrip[h];
		zone[h] = in->tracked[h] ? vrhEffectiveZone(in->zone[h], in->reloading) : s->prevzone[h];
	}

	if (in->paused) {
		/* Rule 11: every grip counts as released and each gun goes to its own holster. Rule 43:
		 * items go to their own slots too, and a gun parked behind one goes home. */
		for (h = 0; h < 2; h++) {
			if (s->held[h] >= 0) {
				vrhEmptyHand(s, h);
				vrhFx(&fx, h, VRH_FX_HOLSTER);
			}
		}
	} else {
		/* Rule 24: parking (rule 33) and unparking a held grip (rule 9) come first. Rule 33:
		 * while reloading the free hand's copy parks at once (no fx), whatever its grip and
		 * tracking (parking is not a grip edge). It runs before the releases so that in the
		 * first tick of a reload the free hand's release does nothing and a gun-side release
		 * finds the copy parked and sends both home (rule 31). Before the buzz and not in a pause
		 * makes no visible difference: rule 30 mutes the free hand, and rule 11 holsters every
		 * gun. */
		/* A lone gun is always in the gun-side hand, so only the other copy of a pair parks */
		if (in->reloading) {
			if (s->held[1 - s->gunside] >= 0 && !vrhIsItemSlot(s->held[1 - s->gunside])) {
				s->parked[1 - s->gunside] = 1;
			}
		} else {
			/* Rule 9 (amended 2026-10-16): out of a reload a parked hand whose grip is held
			 * unparks at once, no press needed (a grip held through the rack brings the gun
			 * back). grip[] is the tick's reading: for an untracked hand its last tracked grip
			 * (rule 21). Running here, before the releases and presses, means a gun-side release
			 * in this tick is a plain rule 7 (this hand keeps the gun) and a press by this hand
			 * finds it holding and does nothing (no second fx, no draw). Rule 41: not while the
			 * other hand holds an item. */
			vrhUnpark(s, grip, &fx);
		}

		for (h = 0; h < 2; h++) {
			int32_t hol = vrhZoneHolster(s, zone[h]);

			if (hol >= 0 && zone[h] != s->prevzone[h] && vrhZoneBuzz(s, in, h, hol)) {
				vrhFx(&fx, h, VRH_FX_ZONE);
			}
		}

		/* An untracked hand's grip is its prevgrip, so it has no edge (rule 21) */
		for (h = 0; h < 2; h++) {
			releasing[h] = !grip[h] && s->prevgrip[h];
		}

		/* Rule 17 against the start of the tick: when both hands held (parked counts), no release
		 * in this tick swaps, so a hand left single by the other hand's rule 7 release earlier in
		 * this tick sends its gun home. The order of the hands does not matter. With an item and
		 * a gun parked behind it no release swaps anyway (rules 39 and 40). */
		wasdual = s->held[VRH_L] >= 0 && s->held[VRH_R] >= 0;

		for (h = 0; h < 2; h++) {
			if (releasing[h]) {
				vrhRelease(s, h, wasdual ? -1 : vrhZoneHolster(s, zone[h]), &fx);
			}
		}

		/* Rule 41: a gun parked behind an item that went home in this tick's releases comes back
		 * now if its grip is held. For a reload park this changes nothing: reloading is the same
		 * all tick, and the first run already took every such hand whose grip is held. */
		if (!in->reloading) {
			vrhUnpark(s, grip, &fx);
		}

		for (h = 0; h < 2; h++) {
			if (grip[h] && !s->prevgrip[h]) {
				vrhPress(s, in, h, vrhZoneHolster(s, zone[h]), &fx);
			}
		}
	}

	for (h = 0; h < 2; h++) {
		s->prevgrip[h] = grip[h];
		s->prevzone[h] = zone[h];
	}

	vrhWeapon(s, &out->weapon, &out->dual);
	out->gunside = s->gunside;
	out->fx = fx;
}

/* Rule 32 */
int32_t vrhFlipGunSide(struct vrhstate *s)
{
	if (s->held[VRH_L] < 0 || s->held[VRH_R] < 0 || s->parked[VRH_L] || s->parked[VRH_R]) {
		return 0;
	}

	s->gunside = 1 - s->gunside;
	return 1;
}

/* Rules 10, 27 and 42 (rule 36: held[L] and held[R] are -1 or the same gun holster, or an item and
 * -1 or a parked gun, or -1 on the gun side next to a gun still parked after its item went home
 * with its grip open, rule 41). Without items a lone gun is always the gun side's, so this is rule
 * 27; that last state gives fists: the parked gun is not in the engine until it unparks. */
void vrhWeapon(const struct vrhstate *s, int32_t *weapon, int32_t *dual)
{
	int32_t g = s->gunside == VRH_L ? VRH_L : VRH_R;
	int32_t o = 1 - g;

	if (s->held[g] >= 0) {
		*weapon = s->holster[s->held[g]];
	} else if (s->held[o] >= 0 && !s->parked[o]) {
		*weapon = s->holster[s->held[o]];
	} else {
		*weapon = 0;
	}

	*dual = s->held[VRH_L] >= 0 && s->held[VRH_L] == s->held[VRH_R];
}

/* Rules 28 and 43: all five slots. A gun parked behind a lost item unparks at once when its last
 * grip is held, as the next quiet tick's rule 41 would (rule 27); else it stays parked until its
 * next press. Only that hand: a gun parked by a reload stays parked (vrhOnLost does not know
 * reloading). No fx: there is no output. */
void vrhOnLost(struct vrhstate *s, int32_t weapon)
{
	int32_t i, h;

	if (weapon == 0) {
		return;
	}

	for (i = 0; i < VRH_NUM_SLOTS; i++) {
		if (s->holster[i] == weapon) {
			s->holster[i] = 0;

			for (h = 0; h < 2; h++) {
				if (s->held[h] == i) {
					vrhEmptyHand(s, h);

					/* Items never pair, so the other hand is not emptied here and
					 * becomes the gun side */
					if (vrhIsItemSlot(i) && s->parked[1 - h] && s->prevgrip[1 - h]) {
						vrhUnparkHand(s, 1 - h);
					}
				}
			}
		}
	}
}

/* Rules 12, 22, 46 and 53: the gun holsters only, in fill order */
int32_t vrhOnPickup(struct vrhstate *s, int32_t weapon)
{
	int32_t i;

	if (weapon == 0) {
		return -1;
	}

	for (i = VRH_GUN1; i < VRH_NUM_GUNS; i++) {
		if (s->holster[i] == weapon) {
			return i;
		}
	}

	for (i = VRH_GUN1; i < VRH_NUM_GUNS; i++) {
		if (s->holster[i] == 0) {
			s->holster[i] = weapon;
			return i;
		}
	}

	return -1;
}

void vrhOnMenuPick(struct vrhstate *s, int32_t weapon)
{
	/* Rule 46: a hand holding an item counts as empty */
	int32_t src = vrhIsGunHolster(s->held[VRH_L]) ? s->held[VRH_L]
		: vrhIsGunHolster(s->held[VRH_R]) ? s->held[VRH_R] : -1;
	int32_t j;

	if (weapon == 0) {
		return;
	}

	/* Rules 23 and 55, empty hands: gun 1 gets it unless it is already in a gun holster */
	if (src < 0) {
		if (vrhOtherGunHolster(s, -1, weapon) < 0) {
			s->holster[VRH_GUN1] = weapon;
		}

		return;
	}

	if (s->holster[src] == weapon) {
		return;
	}

	/* Rules 23 and 55, a gun held (parked counts): the hand(s) keep src, which now names the picked
	 * gun; the gun holster that had the pick gets src's gun */
	j = vrhOtherGunHolster(s, src, weapon);

	if (j >= 0) {
		s->holster[j] = s->holster[src];
	}

	s->holster[src] = weapon;

	/* No pair is assumed: the hand that is not the gun side lets go. A gun parked behind an item
	 * is single: it stays (rule 46). */
	if (s->held[VRH_L] == src && s->held[VRH_R] == src) {
		vrhEmptyHand(s, 1 - s->gunside);
	}
}

/* Rules 34, 35, 45 and 56 at a slot: hand h puts weapon in slot i. At a gun holster a weapon in
 * another gun holster swaps the two when swap is set, else changes nothing; an item slot has no
 * partner. Returns 1 on a change. */
static int32_t vrhPutInHolster(struct vrhstate *s, int32_t h, int32_t i, int32_t weapon,
		int32_t reloading, int32_t swap)
{
	int32_t o = 1 - h;
	int32_t j = vrhIsGunHolster(i) ? vrhOtherGunHolster(s, i, weapon) : -1;

	if (weapon == 0 || s->holster[i] == weapon) {
		return 0;
	}

	/* Without swap a gun already in another gun holster changes nothing; with it, a hand holding
	 * that gun would lose it */
	if (j >= 0 && (!swap || s->held[h] == j || s->held[o] == j)) {
		return 0;
	}

	/* Only the acting hand's gun or item may change: the other hand keeps what it holds from
	 * slot i */
	if (s->held[o] == i && s->held[h] != i) {
		return 0;
	}

	/* While reloading a held gun never changes; a holster no hand holds may */
	if (reloading && s->held[h] == i) {
		return 0;
	}

	/* Rule 39: a gun parked behind an item keeps its gun (the other hand's case is the check
	 * above) */
	if (s->held[h] == i && s->parked[h] && vrhIsItemSlot(s->held[o])) {
		return 0;
	}

	if (j >= 0) {
		s->holster[j] = s->holster[i];
	}

	s->holster[i] = weapon;

	/* Hand h keeps held = i, which now names the new gun or item; a pair's other copy goes back
	 * (items never pair, so a gun parked behind hand h's item stays parked). A lone gun is the gun
	 * side's, and a hand parked after a reload unparks when it becomes the gun side, so the gun
	 * side is never parked. */
	if (s->held[h] == i) {
		if (s->held[o] == i) {
			vrhEmptyHand(s, o);
		}

		s->gunside = h;
		s->parked[h] = 0;
	}

	return 1;
}

/* Rules 34 and 45 */
int32_t vrhStepHolster(struct vrhstate *s, int32_t h, int32_t i, int32_t weapon, int32_t reloading)
{
	if ((h != VRH_L && h != VRH_R) || i < 0 || i >= VRH_NUM_SLOTS) {
		return 0;
	}

	return vrhPutInHolster(s, h, i, weapon, reloading, 0);
}

/* Rules 35 and 45 */
int32_t vrhOnMenuPickHand(struct vrhstate *s, int32_t h, int32_t zone, int32_t weapon, int32_t reloading)
{
	int32_t hol;

	if ((h != VRH_L && h != VRH_R) || zone < VRH_ZONE_NONE || zone >= VRH_NUM_ZONES) {
		return 0;
	}

	if (weapon == 0 || (s->held[h] >= 0 && s->holster[s->held[h]] == weapon)) {
		return 0;
	}

	/* In a zone: rule 34 at its slot; at a gun holster another gun holster's gun swaps in */
	hol = vrhZoneHolster(s, zone);

	if (hol >= 0) {
		return vrhPutInHolster(s, h, hol, weapon, reloading, 1);
	}

	/* No zone, a gun in hand h: rule 23 with hand h keeping the gun */
	if (vrhIsGunHolster(s->held[h])) {
		return vrhPutInHolster(s, h, s->held[h], weapon, reloading, 1);
	}

	/* No zone, hand h empty or holding an item (rule 45: the item stays): the first gun holster in
	 * fill order the other hand does not hold (rule 56; it holds at most one). A gun already in a
	 * gun holster changes nothing. */
	return vrhPutInHolster(s, h, s->held[1 - h] == VRH_GUN1 ? VRH_GUN2 : VRH_GUN1, weapon,
			reloading, 0);
}

/* Rule 47 */
int32_t vrhSetItem(struct vrhstate *s, int32_t i, int32_t weapon)
{
	if (!vrhIsItemSlot(i) || s->held[VRH_L] == i || s->held[VRH_R] == i || s->holster[i] == weapon) {
		return 0;
	}

	s->holster[i] = weapon;
	return 1;
}
