#include <string.h>
#include "vrtest.h"
#include "game/vr_holster_core.h"

/*
 * Contract tests for the holster core's three gun holsters and body-spot layout (Task J4, rules
 * 48 to 56 in docs/plans/2026-10-15-vr-holsters-phase2-plan.md, Stage J). Unlike
 * test_holster_core.c these scenarios run on the default layout from vrhInit (rule 51) unless a
 * test sets another one. Weapon numbers are arbitrary nonzero values.
 */

#define GA 11 /* starts in gun 1 */
#define GB 22 /* starts in gun 2 */
#define GC 33 /* starts in gun 3 */
#define GX 44 /* in no holster */
#define GY 47 /* in no holster */
#define IP 51 /* a pouch gadget */
#define IT 61 /* a throwable */

#define L VRH_L
#define R VRH_R
#define Z_NONE VRH_ZONE_NONE
#define Z_HIP VRH_ZONE_HIP
#define Z_SH VRH_ZONE_SHOULDER
#define Z_CHEST VRH_ZONE_CHEST
#define Z_RHIP VRH_ZONE_RHIP
#define Z_LSH VRH_ZONE_LSHOULDER

#define FXOF(fx, h) (((fx) >> VRH_FX_SHIFT(h)) & 0xff)
#define FXH(h, bits) ((int32_t)(bits) << VRH_FX_SHIFT(h))

/* rule 51: the default layout, as vrhSetLayout's slot_of_zone (zones 1 to 5) */
static const int32_t default_layout[5] = { VRH_GUN3, VRH_GUN1, VRH_POUCH, VRH_THROW, VRH_GUN2 };
/* guns on the hips: off-hand hip gun 1, gun-hand hip gun 2, chest gun 3, shoulders the items */
static const int32_t hips_layout[5] = { VRH_GUN1, VRH_POUCH, VRH_GUN3, VRH_GUN2, VRH_THROW };

/* the zone of each gun holster in the default layout */
static const int32_t default_zone_of_gun[VRH_NUM_GUNS] = { Z_SH, Z_LSH, Z_HIP };

struct rig {
	struct vrhstate s;
	struct vrhinput in;
	struct vrhoutput out;
};

static void step(struct rig *r)
{
	memset(&r->out, 0x5a, sizeof r->out);
	vrhTick(&r->s, &r->in, &r->out);
}

/* vrhInit(a, b), gun 3 gets c (0 leaves it empty), the layout is the default; one quiet tick */
static void setup(struct rig *r, int32_t a, int32_t b, int32_t c, int32_t gunside)
{
	memset(r, 0, sizeof *r);
	vrhInit(&r->s, a, b, gunside);
	r->s.holster[VRH_GUN3] = c;
	r->in.tracked[L] = 1;
	r->in.tracked[R] = 1;
	step(r);
}

static void move(struct rig *r, int32_t h, int32_t zone)
{
	r->in.zone[h] = zone;
	step(r);
}

static void press(struct rig *r, int32_t h, int32_t zone)
{
	if (r->in.zone[h] != zone) {
		move(r, h, zone);
	}
	r->in.grip[h] = 1;
	step(r);
}

static void release(struct rig *r, int32_t h, int32_t zone)
{
	if (r->in.zone[h] != zone) {
		move(r, h, zone);
	}
	r->in.grip[h] = 0;
	step(r);
}

#define CHECK_GUNS(r, a, b, c) do { \
	CHECK_EQ((r)->s.holster[VRH_GUN1], (a)); \
	CHECK_EQ((r)->s.holster[VRH_GUN2], (b)); \
	CHECK_EQ((r)->s.holster[VRH_GUN3], (c)); \
} while (0)

#define CHECK_HELD(r, hl, hr) do { \
	CHECK_EQ((r)->s.held[L], (hl)); \
	CHECK_EQ((r)->s.held[R], (hr)); \
} while (0)

#define CHECK_OUT(r, w, d, gs) do { \
	CHECK_EQ((r)->out.weapon, (w)); \
	CHECK_EQ((r)->out.dual, (d)); \
	CHECK_EQ((r)->out.gunside, (gs)); \
	CHECK_EQ((r)->s.gunside, (gs)); \
} while (0)

#define CHECK_LAYOUT(r, lay) do { \
	int _z; \
	CHECK_EQ((r)->s.layout[VRH_ZONE_NONE], -1); \
	for (_z = 1; _z < VRH_NUM_ZONES; _z++) { \
		CHECK_EQ((r)->s.layout[_z], (lay)[_z - 1]); \
	} \
} while (0)

/* the call returns 0 and the whole state, layout included, is unchanged */
#define CHECK_REFUSED(r, call) do { \
	struct vrhstate _before = (r)->s; \
	CHECK_EQ((call), 0); \
	CHECK(memcmp(&_before, &(r)->s, sizeof _before) == 0); \
} while (0)

#define CHECK_VW(r, w, d) do { \
	int32_t _w = -7, _d = -7; \
	vrhWeapon(&(r)->s, &_w, &_d); \
	CHECK_EQ(_w, (w)); \
	CHECK_EQ(_d, (d)); \
} while (0)

#define STEP(r, h, i, w, rl) vrhStepHolster(&(r)->s, (h), (i), (w), (rl))
#define PICK(r, h, z, w, rl) vrhOnMenuPickHand(&(r)->s, (h), (z), (w), (rl))

/* A built state on the default layout: held and gunside as given, nothing parked, both grips held
 * and both hands in no zone, matching the rig's inputs so a later tick sees no edge */
static void build(struct rig *r, int32_t a, int32_t b, int32_t c, int32_t heldl, int32_t heldr,
		int32_t gunside)
{
	memset(r, 0, sizeof *r);
	vrhInit(&r->s, a, b, gunside);
	r->s.holster[VRH_GUN3] = c;
	r->s.held[L] = heldl;
	r->s.held[R] = heldr;
	r->s.prevgrip[L] = 1;
	r->s.prevgrip[R] = 1;
	r->s.prevzone[L] = Z_NONE;
	r->s.prevzone[R] = Z_NONE;
	r->in.tracked[L] = 1;
	r->in.tracked[R] = 1;
	r->in.grip[L] = 1;
	r->in.grip[R] = 1;
}

/* ---------- rule 48: five slots, three gun holsters, a pair flag per gun holster ---------- */

static void test_layout_slot_and_zone_constants(void)
{
	struct vrhstate s;
	struct vrhinput in;
	CHECK_EQ(VRH_GUN1, 0);
	CHECK_EQ(VRH_GUN2, 1);
	CHECK_EQ(VRH_GUN3, 2);
	CHECK_EQ(VRH_POUCH, 3);
	CHECK_EQ(VRH_THROW, 4);
	CHECK_EQ(VRH_NUM_GUNS, 3);
	CHECK_EQ(VRH_NUM_SLOTS, 5);
	CHECK_EQ(VRH_ZONE_NONE, 0);
	CHECK_EQ(VRH_ZONE_HIP, 1);
	CHECK_EQ(VRH_ZONE_SHOULDER, 2);
	CHECK_EQ(VRH_ZONE_CHEST, 3);
	CHECK_EQ(VRH_ZONE_RHIP, 4);
	CHECK_EQ(VRH_ZONE_LSHOULDER, 5);
	CHECK_EQ(VRH_NUM_ZONES, 6);
	CHECK_EQ(sizeof s.holster / sizeof s.holster[0], 5);
	CHECK_EQ(sizeof in.haspair / sizeof in.haspair[0], 3);
	CHECK_EQ(sizeof s.layout / sizeof s.layout[0], 6);
}

/* rule 48 with rule 3: haspair[k] lets the second hand take a copy of gun holster k's gun only */
static void test_layout_haspair_per_gun_holster(void)
{
	struct rig r;
	int k, other;
	for (k = 0; k < VRH_NUM_GUNS; k++) {
		int32_t zone = default_zone_of_gun[k];
		int32_t gun = k == 0 ? GA : k == 1 ? GB : GC;
		/* the pair flag of this holster: the second hand takes the copy */
		setup(&r, GA, GB, GC, R);
		r.in.haspair[k] = 1;
		press(&r, R, zone);
		press(&r, L, zone);
		CHECK_HELD(&r, k, k);
		CHECK_OUT(&r, gun, 1, R);
		CHECK_EQ(r.out.fx, FXH(L, VRH_FX_DRAW));
		/* only the other two holsters' flags: no copy, an empty buzz */
		setup(&r, GA, GB, GC, R);
		for (other = 0; other < VRH_NUM_GUNS; other++) {
			r.in.haspair[other] = other != k;
		}
		press(&r, R, zone);
		press(&r, L, zone);
		CHECK_HELD(&r, -1, k);
		CHECK_OUT(&r, gun, 0, R);
		CHECK_EQ(r.out.fx, FXH(L, VRH_FX_EMPTY));
	}
}

/* rule 48 with rule 4 across three: drawing gun 3 while the other hand holds gun 1 sends gun 1
 * home; gun 2 is untouched */
static void test_layout_other_holster_draw_across_three(void)
{
	struct rig r;
	setup(&r, GA, GB, GC, R);
	press(&r, R, Z_SH);
	CHECK_HELD(&r, -1, VRH_GUN1);
	press(&r, L, Z_HIP);
	CHECK_HELD(&r, VRH_GUN3, -1);
	CHECK_OUT(&r, GC, 0, L);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_DRAW) | FXH(R, VRH_FX_HOLSTER));
	CHECK_GUNS(&r, GA, GB, GC);
}

/* ---------- rules 49 and 51: the default layout and the left shoulder ---------- */

static void test_layout_init_default_layout_and_guns(void)
{
	struct rig r;
	memset(&r, 0x5a, sizeof r);
	vrhInit(&r.s, GA, GB, R);
	CHECK_GUNS(&r, GA, GB, 0);
	CHECK_EQ(r.s.holster[VRH_POUCH], 0);
	CHECK_EQ(r.s.holster[VRH_THROW], 0);
	CHECK_HELD(&r, -1, -1);
	CHECK_EQ(r.s.gunside, R);
	CHECK_LAYOUT(&r, default_layout);
	CHECK_EQ(r.s.layout[Z_SH], VRH_GUN1);
	CHECK_EQ(r.s.layout[Z_LSH], VRH_GUN2);
	CHECK_EQ(r.s.layout[Z_HIP], VRH_GUN3);
	CHECK_EQ(r.s.layout[Z_CHEST], VRH_POUCH);
	CHECK_EQ(r.s.layout[Z_RHIP], VRH_THROW);
	/* the default is what vrhInit set: setting it again is no change */
	CHECK_EQ(vrhSetLayout(&r.s, default_layout), 0);
	/* a re-init restores the default over a layout set before */
	CHECK_EQ(vrhSetLayout(&r.s, hips_layout), 1);
	CHECK_LAYOUT(&r, hips_layout);
	vrhInit(&r.s, 0, GB, L);
	CHECK_LAYOUT(&r, default_layout);
	CHECK_GUNS(&r, 0, GB, 0);
	CHECK_EQ(r.s.gunside, L);
}

/* rule 49: every zone draws its default slot; the left shoulder is gun 2's */
static void test_layout_default_draw_every_zone(void)
{
	struct rig r;
	static const int32_t zones[5] = { Z_SH, Z_LSH, Z_HIP, Z_CHEST, Z_RHIP };
	static const int32_t slots[5] = { VRH_GUN1, VRH_GUN2, VRH_GUN3, VRH_POUCH, VRH_THROW };
	static const int32_t what[5] = { GA, GB, GC, IP, IT };
	int k;
	setup(&r, GA, GB, GC, R);
	CHECK_EQ(vrhSetItem(&r.s, VRH_POUCH, IP), 1);
	CHECK_EQ(vrhSetItem(&r.s, VRH_THROW, IT), 1);
	for (k = 0; k < 5; k++) {
		press(&r, R, zones[k]);
		CHECK_HELD(&r, -1, slots[k]);
		CHECK_OUT(&r, what[k], 0, R);
		CHECK_EQ(r.out.fx, FXH(R, VRH_FX_DRAW));
		release(&r, R, Z_NONE);
		CHECK_HELD(&r, -1, -1);
		CHECK_EQ(r.out.fx, FXH(R, VRH_FX_HOLSTER));
	}
	CHECK_GUNS(&r, GA, GB, GC);
}

/* rules 14 and 49: the left shoulder buzzes on entry when gun 2 can be drawn, buzzes empty on a
 * press when gun 2 is empty, and draws with either hand */
static void test_layout_left_shoulder_zone(void)
{
	struct rig r;
	setup(&r, GA, GB, 0, R);
	move(&r, L, Z_LSH);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_ZONE));
	r.in.grip[L] = 1;
	step(&r);
	CHECK_HELD(&r, VRH_GUN2, -1);
	CHECK_OUT(&r, GB, 0, L);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_DRAW));
	/* gun 2 empty: no buzz on entry, an empty buzz on the press */
	setup(&r, GA, 0, GC, R);
	move(&r, R, Z_LSH);
	CHECK_EQ(r.out.fx, 0);
	r.in.grip[R] = 1;
	step(&r);
	CHECK_HELD(&r, -1, -1);
	CHECK_EQ(r.out.fx, FXH(R, VRH_FX_EMPTY));
}

/* ---------- rule 50: vrhSetLayout ---------- */

static void test_layout_set_refuses_non_permutations(void)
{
	struct rig r;
	static const int32_t repeat[5] = { VRH_GUN1, VRH_GUN1, VRH_POUCH, VRH_THROW, VRH_GUN2 };
	static const int32_t negative[5] = { VRH_GUN3, VRH_GUN1, VRH_POUCH, VRH_THROW, -1 };
	static const int32_t high[5] = { VRH_GUN3, VRH_GUN1, VRH_POUCH, VRH_THROW, 5 };
	static const int32_t missing_item[5] = { VRH_GUN3, VRH_GUN1, VRH_POUCH, VRH_POUCH, VRH_GUN2 };
	static const int32_t huge[5] = { 100, VRH_GUN1, VRH_POUCH, VRH_THROW, VRH_GUN2 };
	/* a non-default state, so a refusal that resets anything shows */
	setup(&r, GA, GB, GC, R);
	r.in.haspair[VRH_GUN1] = 1;
	press(&r, R, Z_SH);
	press(&r, L, Z_SH);
	CHECK_EQ(vrhSetLayout(&r.s, hips_layout), 1);
	CHECK_REFUSED(&r, vrhSetLayout(&r.s, NULL));
	CHECK_REFUSED(&r, vrhSetLayout(&r.s, repeat));
	CHECK_REFUSED(&r, vrhSetLayout(&r.s, negative));
	CHECK_REFUSED(&r, vrhSetLayout(&r.s, high));
	CHECK_REFUSED(&r, vrhSetLayout(&r.s, missing_item));
	CHECK_REFUSED(&r, vrhSetLayout(&r.s, huge));
	/* the same layout again: 0 and nothing changes */
	CHECK_REFUSED(&r, vrhSetLayout(&r.s, hips_layout));
	CHECK_LAYOUT(&r, hips_layout);
}

/* rule 50: a permutation changes only the layout: holsters, held, parked, gun side, prevgrip and
 * prevzone stay, and a held gun is unaffected */
static void test_layout_set_changes_only_the_layout(void)
{
	static const int32_t reversed[5] = { VRH_THROW, VRH_POUCH, VRH_GUN3, VRH_GUN2, VRH_GUN1 };
	struct rig r;
	struct vrhstate want;
	int z;
	/* a pair of gun 1, the left copy parked by a reload, grips held, hands in zones */
	setup(&r, GA, GB, GC, R);
	r.in.haspair[VRH_GUN1] = 1;
	press(&r, R, Z_SH);
	press(&r, L, Z_SH);
	move(&r, L, Z_CHEST);
	r.in.reloading = 1;
	step(&r);
	CHECK_EQ(r.s.parked[L], 1);
	CHECK_EQ(vrhSetItem(&r.s, VRH_THROW, IT), 1);
	want = r.s;
	CHECK_EQ(vrhSetLayout(&r.s, reversed), 1);
	for (z = 1; z < VRH_NUM_ZONES; z++) {
		want.layout[z] = reversed[z - 1];
	}
	CHECK(memcmp(&want, &r.s, sizeof want) == 0);
	CHECK_LAYOUT(&r, reversed);
	CHECK_VW(&r, GA, 1);
	/* the next quiet tick still holds the same pair */
	step(&r);
	CHECK_OUT(&r, GA, 1, R);
	CHECK_EQ(r.out.fx, 0);
}

/* rule 50: a gun held across a layout change goes home to its own slot, wherever that now is,
 * and a release in its new zone is no swap */
static void test_layout_held_gun_goes_home_after_change(void)
{
	struct rig r;
	setup(&r, GA, GB, GC, R);
	press(&r, R, Z_SH); /* gun 1 */
	CHECK_EQ(vrhSetLayout(&r.s, hips_layout), 1);
	CHECK_HELD(&r, -1, VRH_GUN1);
	CHECK_VW(&r, GA, 0);
	/* the shoulder is now the pouch's zone: a gun let go there goes home, no swap */
	release(&r, R, Z_SH);
	CHECK_HELD(&r, -1, -1);
	CHECK_GUNS(&r, GA, GB, GC);
	CHECK_EQ(r.out.fx, FXH(R, VRH_FX_HOLSTER));
	/* gun 1 is now drawn at the off-hand hip; let go there it is home again, no swap */
	press(&r, R, Z_HIP);
	CHECK_HELD(&r, -1, VRH_GUN1);
	CHECK_OUT(&r, GA, 0, R);
	release(&r, R, Z_HIP);
	CHECK_GUNS(&r, GA, GB, GC);
	CHECK_EQ(r.out.fx, FXH(R, VRH_FX_HOLSTER));
}

/* ---------- rule 49 with a non-default layout: the zones drive draws and swaps ---------- */

static void test_layout_guns_on_hips_draw_and_swap(void)
{
	struct rig r;
	setup(&r, GA, GB, GC, R);
	CHECK_EQ(vrhSetItem(&r.s, VRH_POUCH, IP), 1);
	CHECK_EQ(vrhSetLayout(&r.s, hips_layout), 1);
	/* off-hand hip gun 1, gun-hand hip gun 2, chest gun 3, shoulder the pouch */
	press(&r, R, Z_RHIP);
	CHECK_HELD(&r, -1, VRH_GUN2);
	CHECK_OUT(&r, GB, 0, R);
	release(&r, R, Z_NONE);
	press(&r, R, Z_CHEST);
	CHECK_HELD(&r, -1, VRH_GUN3);
	CHECK_OUT(&r, GC, 0, R);
	release(&r, R, Z_NONE);
	press(&r, R, Z_SH);
	CHECK_HELD(&r, -1, VRH_POUCH);
	CHECK_OUT(&r, IP, 0, R);
	release(&r, R, Z_NONE);
	/* gun 1 from the off-hand hip, buzz entering the gun-hand hip, let go there: gun 1 and gun 2
	 * swap, gun 3 keeps its gun */
	press(&r, R, Z_HIP);
	CHECK_HELD(&r, -1, VRH_GUN1);
	move(&r, R, Z_RHIP);
	CHECK_EQ(r.out.fx, FXH(R, VRH_FX_ZONE));
	release(&r, R, Z_RHIP);
	CHECK_EQ(r.out.fx, FXH(R, VRH_FX_SWAP));
	CHECK_GUNS(&r, GB, GA, GC);
	CHECK_HELD(&r, -1, -1);
	/* the gun-hand hip now draws gun 1's old gun */
	press(&r, R, Z_RHIP);
	CHECK_HELD(&r, -1, VRH_GUN2);
	CHECK_OUT(&r, GA, 0, R);
	/* entering the old gun-1 zone of the default layout (the gun-hand shoulder) is the pouch now:
	 * no buzz, and a release there sends the gun home */
	move(&r, R, Z_SH);
	CHECK_EQ(r.out.fx, 0);
	release(&r, R, Z_SH);
	CHECK_EQ(r.out.fx, FXH(R, VRH_FX_HOLSTER));
	CHECK_GUNS(&r, GB, GA, GC);
}

/* rule 49: a layout with gun 1 at the chest and the pouch at the left shoulder: the chest draws
 * gun 1, the left shoulder the gadget */
static void test_layout_gun_at_chest_pouch_at_left_shoulder(void)
{
	static const int32_t lay[5] = { VRH_GUN3, VRH_GUN2, VRH_GUN1, VRH_THROW, VRH_POUCH };
	struct rig r;
	setup(&r, GA, GB, GC, L);
	CHECK_EQ(vrhSetItem(&r.s, VRH_POUCH, IP), 1);
	CHECK_EQ(vrhSetLayout(&r.s, lay), 1);
	press(&r, L, Z_CHEST);
	CHECK_HELD(&r, VRH_GUN1, -1);
	CHECK_OUT(&r, GA, 0, L);
	/* gun 1 let go at the gun-hand shoulder (gun 2 now): swap */
	release(&r, L, Z_SH);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_SWAP));
	CHECK_GUNS(&r, GB, GA, GC);
	press(&r, L, Z_LSH);
	CHECK_HELD(&r, VRH_POUCH, -1);
	CHECK_OUT(&r, IP, 0, L);
}

/* ---------- rule 52: both shoulders are the reload's ---------- */

/* The gun side's empty hand presses at either shoulder while reloading: nothing, no buzz, also
 * when the shoulder holds the pouch; a hip or the chest still draws */
static void test_layout_reloading_both_shoulders_no_press(void)
{
	static const int32_t pouch_on_shoulder[5] = { VRH_GUN3, VRH_POUCH, VRH_GUN1, VRH_THROW, VRH_GUN2 };
	struct rig r;
	setup(&r, GA, GB, GC, R);
	r.in.reloading = 1;
	press(&r, R, Z_SH);
	CHECK_HELD(&r, -1, -1);
	CHECK_EQ(r.out.fx, 0);
	release(&r, R, Z_SH);
	press(&r, R, Z_LSH);
	CHECK_HELD(&r, -1, -1);
	CHECK_EQ(r.out.fx, 0);
	release(&r, R, Z_LSH);
	press(&r, R, Z_HIP);
	CHECK_HELD(&r, -1, VRH_GUN3);
	CHECK_OUT(&r, GC, 0, R);
	/* the pouch moved to the gun-hand shoulder, gun 1 to the chest */
	setup(&r, GA, GB, GC, R);
	CHECK_EQ(vrhSetItem(&r.s, VRH_POUCH, IP), 1);
	CHECK_EQ(vrhSetLayout(&r.s, pouch_on_shoulder), 1);
	r.in.reloading = 1;
	move(&r, R, Z_SH);
	CHECK_EQ(r.out.fx, 0); /* no zone buzz */
	r.in.grip[R] = 1;
	step(&r);
	CHECK_HELD(&r, -1, -1);
	CHECK_EQ(r.out.fx, 0);
	release(&r, R, Z_SH);
	move(&r, R, Z_CHEST);
	CHECK_EQ(r.out.fx, FXH(R, VRH_FX_ZONE));
	r.in.grip[R] = 1;
	step(&r);
	CHECK_HELD(&r, -1, VRH_GUN1);
	CHECK_OUT(&r, GA, 0, R);
	/* the same shoulder press without the reload draws the gadget */
	setup(&r, GA, GB, GC, R);
	CHECK_EQ(vrhSetItem(&r.s, VRH_POUCH, IP), 1);
	CHECK_EQ(vrhSetLayout(&r.s, pouch_on_shoulder), 1);
	press(&r, R, Z_SH);
	CHECK_HELD(&r, -1, VRH_POUCH);
	CHECK_OUT(&r, IP, 0, R);
}

/* While reloading, a single gun let go at either shoulder goes home (no swap), and entering a
 * shoulder gives no buzz; at the hip it still swaps */
static void test_layout_reloading_both_shoulders_no_swap(void)
{
	struct rig r;
	int k;
	static const int32_t sh[2] = { Z_SH, Z_LSH };
	for (k = 0; k < 2; k++) {
		/* gun 3 from the hip; let go at a shoulder while reloading */
		setup(&r, GA, GB, GC, R);
		press(&r, R, Z_HIP);
		r.in.reloading = 1;
		move(&r, R, sh[k]);
		CHECK_EQ(r.out.fx, 0);
		release(&r, R, sh[k]);
		CHECK_HELD(&r, -1, -1);
		CHECK_GUNS(&r, GA, GB, GC);
		CHECK_EQ(r.out.fx, FXH(R, VRH_FX_HOLSTER));
	}
	/* gun 1 from the gun-hand shoulder, let go at the hip while reloading: swap */
	setup(&r, GA, GB, GC, R);
	press(&r, R, Z_SH);
	r.in.reloading = 1;
	move(&r, R, Z_HIP);
	CHECK_EQ(r.out.fx, FXH(R, VRH_FX_ZONE));
	release(&r, R, Z_HIP);
	CHECK_EQ(r.out.fx, FXH(R, VRH_FX_SWAP));
	CHECK_GUNS(&r, GC, GB, GA);
	/* guns on the hips: gun 1 let go at the gun-hand hip (gun 2) while reloading still swaps */
	setup(&r, GA, GB, GC, R);
	CHECK_EQ(vrhSetLayout(&r.s, hips_layout), 1);
	press(&r, R, Z_HIP);
	r.in.reloading = 1;
	release(&r, R, Z_RHIP);
	CHECK_EQ(r.out.fx, FXH(R, VRH_FX_SWAP));
	CHECK_GUNS(&r, GB, GA, GC);
}

/* ---------- rule 53: the fill order ---------- */

static void test_layout_pickup_fill_order(void)
{
	struct rig r;
	setup(&r, 0, 0, 0, R);
	CHECK_EQ(vrhOnPickup(&r.s, GA), VRH_GUN1);
	CHECK_EQ(vrhOnPickup(&r.s, GB), VRH_GUN2);
	CHECK_EQ(vrhOnPickup(&r.s, GC), VRH_GUN3);
	CHECK_GUNS(&r, GA, GB, GC);
	CHECK_EQ(r.s.holster[VRH_POUCH], 0);
	CHECK_EQ(r.s.holster[VRH_THROW], 0);
	/* all three full: -1, never an item slot */
	{
		struct vrhstate before = r.s;
		CHECK_EQ(vrhOnPickup(&r.s, GX), -1);
		CHECK(memcmp(&before, &r.s, sizeof before) == 0);
	}
	CHECK_EQ(r.s.holster[VRH_POUCH], 0);
	CHECK_EQ(r.s.holster[VRH_THROW], 0);
	/* a gun already holstered returns its holster and changes nothing */
	{
		struct vrhstate before = r.s;
		CHECK_EQ(vrhOnPickup(&r.s, GA), VRH_GUN1);
		CHECK_EQ(vrhOnPickup(&r.s, GB), VRH_GUN2);
		CHECK_EQ(vrhOnPickup(&r.s, GC), VRH_GUN3);
		CHECK_EQ(vrhOnPickup(&r.s, 0), -1);
		CHECK(memcmp(&before, &r.s, sizeof before) == 0);
	}
	/* holes fill in order: gun 1 before gun 3 */
	setup(&r, 0, GB, 0, R);
	CHECK_EQ(vrhOnPickup(&r.s, GA), VRH_GUN1);
	CHECK_EQ(vrhOnPickup(&r.s, GC), VRH_GUN3);
	CHECK_GUNS(&r, GA, GB, GC);
	setup(&r, GA, 0, GC, R);
	CHECK_EQ(vrhOnPickup(&r.s, GC), VRH_GUN3);
	CHECK_EQ(vrhOnPickup(&r.s, GB), VRH_GUN2);
	CHECK_GUNS(&r, GA, GB, GC);
	/* the order is by slot, not by body spot: a non-default layout fills gun 1 first too */
	setup(&r, 0, 0, 0, R);
	CHECK_EQ(vrhSetLayout(&r.s, hips_layout), 1);
	CHECK_EQ(vrhOnPickup(&r.s, GA), VRH_GUN1);
	CHECK_EQ(vrhOnPickup(&r.s, GB), VRH_GUN2);
	CHECK_EQ(vrhOnPickup(&r.s, GC), VRH_GUN3);
	/* a gun out in a hand still counts as holstered */
	setup(&r, GA, GB, GC, R);
	press(&r, R, Z_HIP);
	CHECK_EQ(vrhOnPickup(&r.s, GC), VRH_GUN3);
	CHECK_HELD(&r, -1, VRH_GUN3);
}

/* ---------- rule 54: swaps and zone buzz across three ---------- */

/* For every ordered pair of gun holsters: a single gun entering the other's zone buzzes, and let
 * go there the two swap while the third keeps its gun */
static void test_layout_release_swaps_every_pair(void)
{
	static const int32_t guns[3] = { GA, GB, GC };
	struct rig r;
	int i, j, k;
	for (i = 0; i < VRH_NUM_GUNS; i++) {
		for (j = 0; j < VRH_NUM_GUNS; j++) {
			int32_t want[3];
			if (i == j) {
				continue;
			}
			k = 3 - i - j;
			setup(&r, GA, GB, GC, R);
			press(&r, R, default_zone_of_gun[i]);
			CHECK_HELD(&r, -1, i);
			move(&r, R, default_zone_of_gun[j]);
			CHECK_EQ(r.out.fx, FXH(R, VRH_FX_ZONE));
			release(&r, R, default_zone_of_gun[j]);
			CHECK_EQ(r.out.fx, FXH(R, VRH_FX_SWAP));
			CHECK_HELD(&r, -1, -1);
			CHECK_OUT(&r, 0, 0, R);
			want[i] = guns[j];
			want[j] = guns[i];
			want[k] = guns[k];
			CHECK_GUNS(&r, want[0], want[1], want[2]);
		}
	}
}

/* rule 54: a single gun entering its own zone, an item zone or no zone gives no buzz; entering a
 * gun holster zone that is empty still offers the swap */
static void test_layout_zone_buzz_single_gun(void)
{
	struct rig r;
	setup(&r, GA, GB, 0, R);
	CHECK_EQ(vrhSetItem(&r.s, VRH_POUCH, IP), 1);
	press(&r, R, Z_LSH); /* gun 2 */
	move(&r, R, Z_CHEST);
	CHECK_EQ(r.out.fx, 0);
	move(&r, R, Z_NONE);
	CHECK_EQ(r.out.fx, 0);
	move(&r, R, Z_LSH);
	CHECK_EQ(r.out.fx, 0);
	move(&r, R, Z_SH);
	CHECK_EQ(r.out.fx, FXH(R, VRH_FX_ZONE));
	move(&r, R, Z_HIP);
	CHECK_EQ(r.out.fx, FXH(R, VRH_FX_ZONE));
	/* gun 3 was empty: the gun moves there */
	release(&r, R, Z_HIP);
	CHECK_EQ(r.out.fx, FXH(R, VRH_FX_SWAP));
	CHECK_GUNS(&r, GA, 0, GB);
	/* an empty hand gets no buzz at the now empty gun 2 */
	move(&r, R, Z_LSH);
	CHECK_EQ(r.out.fx, 0);
}

/* rules 17 and 54: a pair never swaps and gets no buzz at another gun holster */
static void test_layout_pair_never_swaps_across_three(void)
{
	struct rig r;
	setup(&r, GA, GB, GC, R);
	r.in.haspair[VRH_GUN3] = 1;
	press(&r, R, Z_HIP);
	press(&r, L, Z_HIP);
	CHECK_HELD(&r, VRH_GUN3, VRH_GUN3);
	move(&r, L, Z_SH);
	CHECK_EQ(r.out.fx, 0);
	move(&r, R, Z_LSH);
	CHECK_EQ(r.out.fx, 0);
	release(&r, R, Z_LSH);
	CHECK_GUNS(&r, GA, GB, GC);
	CHECK_HELD(&r, VRH_GUN3, -1);
	CHECK_OUT(&r, GC, 0, L);
	CHECK_EQ(r.out.fx, FXH(R, VRH_FX_HOLSTER));
}

/* ---------- rule 55: the pause-menu pick across three ---------- */

static void test_layout_menupick_across_three(void)
{
	struct rig r;
	/* gun 1 held, pick gun 3's gun: gun 1 and gun 3 swap, the hand holds the pick */
	setup(&r, GA, GB, GC, R);
	press(&r, R, Z_SH);
	vrhOnMenuPick(&r.s, GC);
	CHECK_GUNS(&r, GC, GB, GA);
	CHECK_HELD(&r, -1, VRH_GUN1);
	CHECK_VW(&r, GC, 0);
	/* gun 3 held, pick gun 2's gun: gun 3 and gun 2 swap, gun 1 stays */
	setup(&r, GA, GB, GC, R);
	press(&r, R, Z_HIP);
	vrhOnMenuPick(&r.s, GB);
	CHECK_GUNS(&r, GA, GC, GB);
	CHECK_HELD(&r, -1, VRH_GUN3);
	CHECK_VW(&r, GB, 0);
	/* gun 2 held, a gun in no holster: gun 2's holster gets it */
	setup(&r, GA, GB, GC, L);
	press(&r, L, Z_LSH);
	vrhOnMenuPick(&r.s, GX);
	CHECK_GUNS(&r, GA, GX, GC);
	CHECK_HELD(&r, VRH_GUN2, -1);
	CHECK_VW(&r, GX, 0);
	/* empty hands: gun 1 gets a new gun, even when full */
	setup(&r, GA, GB, GC, R);
	vrhOnMenuPick(&r.s, GX);
	CHECK_GUNS(&r, GX, GB, GC);
	CHECK_HELD(&r, -1, -1);
	/* empty hands, a gun already in gun 2 or gun 3: no change */
	setup(&r, GA, GB, GC, R);
	{
		struct vrhstate before = r.s;
		vrhOnMenuPick(&r.s, GC);
		CHECK(memcmp(&before, &r.s, sizeof before) == 0);
		vrhOnMenuPick(&r.s, GB);
		CHECK(memcmp(&before, &r.s, sizeof before) == 0);
	}
	/* a hand holding an item counts as empty: gun 1 gets the pick, the item stays held */
	setup(&r, GA, GB, GC, R);
	CHECK_EQ(vrhSetItem(&r.s, VRH_POUCH, IP), 1);
	press(&r, R, Z_CHEST);
	CHECK_HELD(&r, -1, VRH_POUCH);
	vrhOnMenuPick(&r.s, GX);
	CHECK_GUNS(&r, GX, GB, GC);
	CHECK_HELD(&r, -1, VRH_POUCH);
	CHECK_EQ(r.s.holster[VRH_POUCH], IP);
}

/* ---------- rule 56: steps across three ---------- */

static void test_layout_step_across_three(void)
{
	struct rig r;
	struct vrhstate want;
	/* a weapon in either other gun holster: no change */
	build(&r, GA, GB, GC, -1, -1, R);
	CHECK_REFUSED(&r, STEP(&r, R, VRH_GUN1, GB, 0));
	CHECK_REFUSED(&r, STEP(&r, R, VRH_GUN1, GC, 0));
	CHECK_REFUSED(&r, STEP(&r, R, VRH_GUN3, GA, 0));
	CHECK_REFUSED(&r, STEP(&r, R, VRH_GUN3, GB, 0));
	CHECK_REFUSED(&r, STEP(&r, L, VRH_GUN2, GC, 0));
	CHECK_REFUSED(&r, STEP(&r, L, VRH_GUN2, GB, 0)); /* already there */
	/* a new gun at gun 3, no hand holding: only gun 3 changes */
	want = r.s;
	CHECK_EQ(STEP(&r, R, VRH_GUN3, GX, 0), 1);
	want.holster[VRH_GUN3] = GX;
	CHECK(memcmp(&want, &r.s, sizeof want) == 0);
	/* hand R holds gun 1 and steps gun 3: only gun 3 changes, R keeps gun 1 */
	build(&r, GA, GB, GC, -1, VRH_GUN1, R);
	want = r.s;
	CHECK_EQ(STEP(&r, R, VRH_GUN3, GX, 0), 1);
	want.holster[VRH_GUN3] = GX;
	CHECK(memcmp(&want, &r.s, sizeof want) == 0);
	CHECK_VW(&r, GA, 0);
	/* hand R holds gun 3 and steps it: R holds the new gun */
	build(&r, GA, GB, GC, -1, VRH_GUN3, R);
	CHECK_EQ(STEP(&r, R, VRH_GUN3, GX, 0), 1);
	CHECK_GUNS(&r, GA, GB, GX);
	CHECK_HELD(&r, -1, VRH_GUN3);
	CHECK_VW(&r, GX, 0);
	/* the other hand holds gun 2's gun: R's step at gun 2 is refused */
	build(&r, GA, GB, GC, VRH_GUN2, -1, L);
	CHECK_REFUSED(&r, STEP(&r, R, VRH_GUN2, GX, 0));
	/* while reloading: a step at a holster no hand holds works, at the held one it does not */
	build(&r, GA, GB, GC, -1, VRH_GUN1, R);
	CHECK_REFUSED(&r, STEP(&r, R, VRH_GUN1, GX, 1));
	CHECK_EQ(STEP(&r, R, VRH_GUN2, GX, 1), 1);
	CHECK_GUNS(&r, GA, GX, GC);
	/* slot range: gun 3 and both item slots are slots, 5 and -1 are not */
	build(&r, GA, GB, GC, -1, -1, R);
	CHECK_REFUSED(&r, STEP(&r, R, VRH_NUM_SLOTS, GX, 0));
	CHECK_REFUSED(&r, STEP(&r, R, -1, GX, 0));
}

/* ---------- rule 56: hold-menu picks across three ---------- */

/* a pick in a gun holster's zone: a pick in another gun holster swaps the two */
static void test_layout_pickhand_zone_swaps_across_three(void)
{
	struct rig r;
	/* no hand holding: R at the hip (gun 3) picks gun 1's gun: gun 3 and gun 1 swap */
	build(&r, GA, GB, GC, -1, -1, R);
	CHECK_EQ(PICK(&r, R, Z_HIP, GA, 0), 1);
	CHECK_GUNS(&r, GC, GB, GA);
	CHECK_HELD(&r, -1, -1);
	/* R holds gun 3 and picks gun 2's gun at the hip: they swap, R holds the pick */
	build(&r, GA, GB, GC, -1, VRH_GUN3, R);
	CHECK_EQ(PICK(&r, R, Z_HIP, GB, 0), 1);
	CHECK_GUNS(&r, GA, GC, GB);
	CHECK_HELD(&r, -1, VRH_GUN3);
	CHECK_VW(&r, GB, 0);
	/* R holds gun 1 and picks gun 2's gun at the hip: gun 3 and gun 2 swap, R keeps gun 1 */
	build(&r, GA, GB, GC, -1, VRH_GUN1, R);
	CHECK_EQ(PICK(&r, R, Z_HIP, GB, 0), 1);
	CHECK_GUNS(&r, GA, GC, GB);
	CHECK_HELD(&r, -1, VRH_GUN1);
	CHECK_VW(&r, GA, 0);
	/* the left shoulder is gun 2's zone: a pick of gun 3's gun there swaps gun 2 and gun 3 */
	build(&r, GA, GB, GC, -1, -1, R);
	CHECK_EQ(PICK(&r, L, Z_LSH, GC, 0), 1);
	CHECK_GUNS(&r, GA, GC, GB);
	/* a new gun at the left shoulder: gun 2 gets it */
	build(&r, GA, GB, GC, -1, -1, R);
	CHECK_EQ(PICK(&r, L, Z_LSH, GX, 0), 1);
	CHECK_GUNS(&r, GA, GX, GC);
}

/* a zone pick of another holster's gun is refused when either hand holds that gun */
static void test_layout_pickhand_zone_swap_refused_when_held(void)
{
	struct rig r;
	/* the other hand holds gun 1's gun: R at the hip picking it is refused */
	build(&r, GA, GB, GC, VRH_GUN1, -1, L);
	CHECK_REFUSED(&r, PICK(&r, R, Z_HIP, GA, 0));
	/* hand R itself holds gun 2's gun: picking it at the hip is refused (it already holds it) */
	build(&r, GA, GB, GC, -1, VRH_GUN2, R);
	CHECK_REFUSED(&r, PICK(&r, R, Z_HIP, GB, 0));
	/* the other hand holds gun 3's gun: R's pick at the hip (gun 3) is refused */
	build(&r, GA, GB, GC, VRH_GUN3, -1, L);
	CHECK_REFUSED(&r, PICK(&r, R, Z_HIP, GX, 0));
	CHECK_REFUSED(&r, PICK(&r, R, Z_HIP, GB, 0));
}

/* a pick with no zone and a gun in hand h: rule 23 with the three-way swap */
static void test_layout_pickhand_nozone_held_across_three(void)
{
	struct rig r;
	/* R holds gun 2, picks gun 3's gun: gun 2 and gun 3 swap, R holds the pick */
	build(&r, GA, GB, GC, -1, VRH_GUN2, R);
	CHECK_EQ(PICK(&r, R, Z_NONE, GC, 0), 1);
	CHECK_GUNS(&r, GA, GC, GB);
	CHECK_HELD(&r, -1, VRH_GUN2);
	CHECK_EQ(r.s.gunside, R);
	CHECK_VW(&r, GC, 0);
	/* R holds gun 3, picks a new gun: gun 3 gets it */
	build(&r, GA, GB, GC, -1, VRH_GUN3, R);
	CHECK_EQ(PICK(&r, R, Z_NONE, GX, 0), 1);
	CHECK_GUNS(&r, GA, GB, GX);
	CHECK_HELD(&r, -1, VRH_GUN3);
	/* a pair of gun 3 and L picks gun 1's gun: they swap, R's copy goes back, L is the gun side */
	build(&r, GA, GB, GC, VRH_GUN3, VRH_GUN3, R);
	CHECK_EQ(PICK(&r, L, Z_NONE, GA, 0), 1);
	CHECK_GUNS(&r, GC, GB, GA);
	CHECK_HELD(&r, VRH_GUN3, -1);
	CHECK_EQ(r.s.gunside, L);
	CHECK_VW(&r, GA, 0);
	/* while reloading: no change */
	build(&r, GA, GB, GC, -1, VRH_GUN2, R);
	CHECK_REFUSED(&r, PICK(&r, R, Z_NONE, GC, 1));
	CHECK_REFUSED(&r, PICK(&r, R, Z_NONE, GX, 1));
}

/* a pick with no zone by an empty hand: gun 1, or gun 2 when the other hand holds gun 1's gun */
static void test_layout_pickhand_nozone_empty_fallback(void)
{
	struct rig r;
	struct vrhstate want;
	/* both hands empty: gun 1 */
	build(&r, GA, GB, GC, -1, -1, R);
	CHECK_EQ(PICK(&r, R, Z_NONE, GX, 0), 1);
	CHECK_GUNS(&r, GX, GB, GC);
	/* the other hand holds gun 1's gun: gun 2, and the other hand keeps gun 1 */
	build(&r, GA, GB, GC, VRH_GUN1, -1, L);
	want = r.s;
	CHECK_EQ(PICK(&r, R, Z_NONE, GX, 0), 1);
	want.holster[VRH_GUN2] = GX;
	CHECK(memcmp(&want, &r.s, sizeof want) == 0);
	CHECK_VW(&r, GA, 0);
	/* the other hand holds gun 3's gun: gun 1 */
	build(&r, GA, GB, GC, VRH_GUN3, -1, L);
	CHECK_EQ(PICK(&r, R, Z_NONE, GX, 0), 1);
	CHECK_GUNS(&r, GX, GB, GC);
	CHECK_HELD(&r, VRH_GUN3, -1);
	/* works while reloading: it changes no held gun */
	build(&r, GA, GB, GC, VRH_GUN1, -1, L);
	CHECK_EQ(PICK(&r, R, Z_NONE, GX, 1), 1);
	CHECK_GUNS(&r, GA, GX, GC);
	/* a weapon already in any gun holster: no change */
	build(&r, GA, GB, GC, -1, -1, R);
	CHECK_REFUSED(&r, PICK(&r, R, Z_NONE, GA, 0));
	CHECK_REFUSED(&r, PICK(&r, R, Z_NONE, GB, 0));
	CHECK_REFUSED(&r, PICK(&r, R, Z_NONE, GC, 0));
	/* hand R holding an item counts as empty: gun 1 gets the pick, the item stays held */
	build(&r, GA, GB, GC, -1, VRH_POUCH, R);
	r.s.holster[VRH_POUCH] = IP;
	CHECK_EQ(PICK(&r, R, Z_NONE, GX, 0), 1);
	CHECK_GUNS(&r, GX, GB, GC);
	CHECK_HELD(&r, -1, VRH_POUCH);
	CHECK_EQ(r.s.holster[VRH_POUCH], IP);
}

/* rule 49 for picks: with guns on the hips, a pick at the gun-hand hip goes to gun 2, and a pick at
 * the chest swaps gun 3 with the picked gun's holster */
static void test_layout_pickhand_follows_layout(void)
{
	struct rig r;
	build(&r, GA, GB, GC, -1, -1, R);
	CHECK_EQ(vrhSetLayout(&r.s, hips_layout), 1);
	CHECK_EQ(PICK(&r, R, Z_RHIP, GX, 0), 1);
	CHECK_GUNS(&r, GA, GX, GC);
	CHECK_EQ(PICK(&r, R, Z_CHEST, GA, 0), 1);
	CHECK_GUNS(&r, GC, GX, GA);
	/* the gun-hand shoulder is the pouch's zone now: a gadget picked there goes to the pouch */
	CHECK_EQ(PICK(&r, R, Z_SH, IP, 0), 1);
	CHECK_EQ(r.s.holster[VRH_POUCH], IP);
	CHECK_GUNS(&r, GC, GX, GA);
}

/* ---------- rule 56: the zone range ---------- */

static void test_layout_pickhand_zone_range(void)
{
	struct rig r;
	build(&r, GA, GB, GC, -1, -1, R);
	CHECK_REFUSED(&r, PICK(&r, R, VRH_NUM_ZONES, GX, 0));
	CHECK_REFUSED(&r, PICK(&r, R, VRH_ZONE_LSHOULDER + 1, GX, 0));
	CHECK_REFUSED(&r, PICK(&r, R, -1, GX, 0));
	CHECK_REFUSED(&r, PICK(&r, R, 1000, GX, 0));
	CHECK_EQ(PICK(&r, R, VRH_ZONE_LSHOULDER, GX, 0), 1);
	CHECK_GUNS(&r, GA, GX, GC);
}

void run_holster_layout_tests(void)
{
	test_layout_slot_and_zone_constants();
	test_layout_haspair_per_gun_holster();
	test_layout_other_holster_draw_across_three();
	test_layout_init_default_layout_and_guns();
	test_layout_default_draw_every_zone();
	test_layout_left_shoulder_zone();
	test_layout_set_refuses_non_permutations();
	test_layout_set_changes_only_the_layout();
	test_layout_held_gun_goes_home_after_change();
	test_layout_guns_on_hips_draw_and_swap();
	test_layout_gun_at_chest_pouch_at_left_shoulder();
	test_layout_reloading_both_shoulders_no_press();
	test_layout_reloading_both_shoulders_no_swap();
	test_layout_pickup_fill_order();
	test_layout_release_swaps_every_pair();
	test_layout_zone_buzz_single_gun();
	test_layout_pair_never_swaps_across_three();
	test_layout_menupick_across_three();
	test_layout_step_across_three();
	test_layout_pickhand_zone_swaps_across_three();
	test_layout_pickhand_zone_swap_refused_when_held();
	test_layout_pickhand_nozone_held_across_three();
	test_layout_pickhand_nozone_empty_fallback();
	test_layout_pickhand_follows_layout();
	test_layout_pickhand_zone_range();
}
