#include <string.h>
#include "vrtest.h"
#include "game/vr_holster_core.h"

/*
 * Contract tests for the VR holster core (vr_holster_core.h), written black-box from the rules
 * in docs/plans/2026-10-15-vr-holsters-phase2-plan.md, Task A4 (rules 1-33), Task B7 (rules
 * 34 and 35) and Task C1 (rules 36-47: the chest pouch and right hip item slots). Each scenario
 * names the rule it checks. Weapon numbers are arbitrary nonzero values.
 */

#define GUN_H 10 /* starts in the hip holster */
#define GUN_S 20 /* starts in the shoulder holster */
#define GUN_X 30 /* in no holster */
#define GUN_4 45 /* in no holster: a pickup once gun 3 (rule 53) has GUN_X */
#define ITEM_P 50 /* a held gadget for the chest pouch (rule 36) */
#define ITEM_P2 55 /* another pouch gadget, in no slot */
#define ITEM_T 60 /* a throwable for the right hip */
#define ITEM_T2 65 /* another throwable, in no slot */

/* Task J4 renamed the slots (rule 48) and made the zones' slots a layout (rule 49). These tests
 * keep their old names and meaning: setup_raw sets the legacy layout (hip gun 1, shoulder gun 2,
 * chest pouch, right hip throwables, left shoulder gun 3). */
#define VRH_HIP VRH_GUN1
#define VRH_SHOULDER VRH_GUN2
#define VRH_RHIP VRH_THROW

static const int32_t legacy_layout[VRH_NUM_ZONES - 1] = {
	VRH_GUN1, VRH_GUN2, VRH_POUCH, VRH_THROW, VRH_GUN3,
};

#define L VRH_L
#define R VRH_R
#define Z_NONE VRH_ZONE_NONE
#define Z_HIP VRH_ZONE_HIP
#define Z_SH VRH_ZONE_SHOULDER
#define Z_CHEST VRH_ZONE_CHEST /* the pouch's zone since rule 36 */
#define Z_RHIP VRH_ZONE_RHIP

/* fx bits of one hand, and the fx value of bits on one hand */
#define FXOF(fx, h) (((fx) >> VRH_FX_SHIFT(h)) & 0xff)
#define FXH(h, bits) ((int32_t)(bits) << VRH_FX_SHIFT(h))

struct rig {
	struct vrhstate s;
	struct vrhinput in;
	struct vrhoutput out;
};

/* ---------- helpers ---------- */

/* Runs one tick. The output is prefilled with garbage so every field must be written. */
static void step(struct rig *r)
{
	memset(&r->out, 0x5a, sizeof r->out);
	vrhTick(&r->s, &r->in, &r->out);
}

/* vrhInit only: both hands tracked, grips released, no zones; no tick run yet */
static void setup_raw(struct rig *r, int32_t hip, int32_t shoulder, int32_t gunside)
{
	memset(r, 0, sizeof *r);
	vrhInit(&r->s, hip, shoulder, gunside);
	vrhSetLayout(&r->s, legacy_layout);
	r->in.tracked[L] = 1;
	r->in.tracked[R] = 1;
}

/* vrhInit plus one quiet tick (grips released), which clears the init grips of rule 20 */
static void setup(struct rig *r, int32_t hip, int32_t shoulder, int32_t gunside)
{
	setup_raw(r, hip, shoulder, gunside);
	step(r);
}

/* Moves hand h to zone (keeping its grip) and runs one tick */
static void move(struct rig *r, int32_t h, int32_t zone)
{
	r->in.zone[h] = zone;
	step(r);
}

/* Hand h presses its grip in zone. If the hand is elsewhere it moves there on a tick of its own
 * first, so the press tick carries no zone-enter fx. */
static void press(struct rig *r, int32_t h, int32_t zone)
{
	if (r->in.zone[h] != zone) {
		move(r, h, zone);
	}
	r->in.grip[h] = 1;
	step(r);
}

/* Hand h releases its grip in zone, moving there on a tick of its own first */
static void release(struct rig *r, int32_t h, int32_t zone)
{
	if (r->in.zone[h] != zone) {
		move(r, h, zone);
	}
	r->in.grip[h] = 0;
	step(r);
}

/* Output and state agree on the basic picture */
#define CHECK_OUT(r, w, d, gs) do { \
	CHECK_EQ((r)->out.weapon, (w)); \
	CHECK_EQ((r)->out.dual, (d)); \
	CHECK_EQ((r)->out.gunside, (gs)); \
	CHECK_EQ((r)->s.gunside, (gs)); \
} while (0)

#define CHECK_HOLSTERS(r, hip, sh) do { \
	CHECK_EQ((r)->s.holster[VRH_HIP], (hip)); \
	CHECK_EQ((r)->s.holster[VRH_SHOULDER], (sh)); \
} while (0)

#define CHECK_HELD(r, hl, hr) do { \
	CHECK_EQ((r)->s.held[L], (hl)); \
	CHECK_EQ((r)->s.held[R], (hr)); \
} while (0)

/* R draws the hip gun, then L draws its pair: dual, gunside R */
static void make_dual_hip(struct rig *r)
{
	setup(r, GUN_H, GUN_S, L);
	r->in.haspair[VRH_HIP] = 1;
	press(r, R, Z_HIP);
	press(r, L, Z_HIP);
	r->in.zone[L] = Z_NONE;
	r->in.zone[R] = Z_NONE;
	step(r);
}

/* make_dual_hip, then a reload starts (rule 33 parks L on that tick) and L lets go (nothing) */
static void make_parked_left(struct rig *r)
{
	make_dual_hip(r);
	r->in.reloading = 1;
	release(r, L, Z_NONE);
}

/* make_parked_left, then the reload ends with no grip press: L stays parked (rule 9) */
static void make_parked_left_after_reload(struct rig *r)
{
	make_parked_left(r);
	r->in.reloading = 0;
	step(r);
	CHECK_EQ(r->s.parked[L], 1);
	CHECK_HELD(r, VRH_HIP, VRH_HIP);
	CHECK_EQ(r->s.gunside, R);
}

/* Hand gs draws holster i's gun and the other hand its pair; both move to no zone with their
 * grips held; then a reload starts, and rule 33 parks the other hand's copy on that tick */
static void make_parked_copy(struct rig *r, int32_t gs, int32_t i)
{
	int32_t zone = i == VRH_HIP ? Z_HIP : Z_SH;
	setup(r, GUN_H, GUN_S, 1 - gs);
	r->in.haspair[i] = 1;
	press(r, gs, zone);
	press(r, 1 - gs, zone);
	move(r, L, Z_NONE);
	move(r, R, Z_NONE);
	r->in.reloading = 1;
	step(r);
	CHECK_EQ(r->s.parked[1 - gs], 1);
	CHECK_EQ(r->s.parked[gs], 0);
	CHECK_HELD(r, i, i);
	CHECK_EQ(r->s.gunside, gs);
	CHECK_EQ(r->out.fx, 0);
}

/* A free hand holding a copy with its grip open, not reloading. Since rule 31 only a step or a
 * pick gets there: make_parked_left_after_reload (L parked, grip open), then L steps the hip to
 * GUN_X (rule 34: L becomes the gun side and unparks, R's copy goes back), R lets go and draws
 * the second copy (rule 3), and the side flips to R (rule 32). L never squeezed again. */
static void make_free_left_grip_open(struct rig *r)
{
	make_parked_left_after_reload(r);
	CHECK_EQ(vrhStepHolster(&r->s, L, VRH_HIP, GUN_X, 0), 1);
	CHECK_HELD(r, VRH_HIP, -1);
	CHECK_EQ(r->s.parked[L], 0);
	CHECK_EQ(r->s.gunside, L);
	release(r, R, Z_NONE);
	press(r, R, Z_HIP);
	CHECK_HELD(r, VRH_HIP, VRH_HIP);
	move(r, R, Z_NONE);
	CHECK_EQ(vrhFlipGunSide(&r->s), 1);
	step(r);
	CHECK_OUT(r, GUN_X, 1, R);
	CHECK_EQ(r->s.parked[L], 0);
	CHECK_EQ(r->s.prevgrip[L], 0);
	CHECK_EQ(r->out.fx, 0);
}

/* A built state: vrhInit, then held and gunside as given, nothing parked. prevgrip and prevzone
 * get marker values rules 34 and 35 may not touch; the rig's inputs match them, so a later tick
 * (for CHECK_WEAPON) sees no grip or zone edge. */
static void build(struct rig *r, int32_t hip, int32_t sh, int32_t heldl, int32_t heldr, int32_t gunside)
{
	setup_raw(r, hip, sh, gunside);
	r->s.held[L] = heldl;
	r->s.held[R] = heldr;
	r->s.prevgrip[L] = 1;
	r->s.prevgrip[R] = 1;
	r->s.prevzone[L] = Z_HIP;
	r->s.prevzone[R] = Z_SH;
	r->in.grip[L] = 1;
	r->in.grip[R] = 1;
	r->in.zone[L] = Z_HIP;
	r->in.zone[R] = Z_SH;
}

/* vrhWeapon gives w and d, changes nothing, and equals the next idle tick's weapon and dual.
 * The rig's inputs are steady (no grip or zone edges); paused must be 0. */
#define CHECK_WEAPON(r, w, d) check_weapon((r), (w), (d), __LINE__)

static void check_weapon(struct rig *r, int32_t w, int32_t d, int line)
{
	struct vrhstate before = r->s;
	int32_t gw = -7, gd = -7;
	vrhWeapon(&r->s, &gw, &gd);
	if (gw != w || gd != d) {
		printf("  (check_weapon called from line %d)\n", line);
	}
	CHECK_EQ(gw, w);
	CHECK_EQ(gd, d);
	CHECK(memcmp(&before, &r->s, sizeof before) == 0);
	r->in.paused = 0;
	step(r);
	CHECK_EQ(r->out.weapon, gw);
	CHECK_EQ(r->out.dual, gd);
	CHECK_EQ(r->out.fx, 0);
}

/* ---------- init (rule 20) ---------- */

static void test_init_state(void)
{
	struct rig r;
	setup_raw(&r, GUN_H, 0, L);
	CHECK_HOLSTERS(&r, GUN_H, 0);
	CHECK_HELD(&r, -1, -1);
	CHECK_EQ(r.s.gunside, L);
	CHECK_EQ(r.s.parked[L], 0);
	CHECK_EQ(r.s.parked[R], 0);
	CHECK_EQ(r.s.prevgrip[L], 1);
	CHECK_EQ(r.s.prevgrip[R], 1);

	setup_raw(&r, 0, GUN_S, R);
	CHECK_HOLSTERS(&r, 0, GUN_S);
	CHECK_EQ(r.s.gunside, R);
}

/* rules 36 and 48: five slots, gun 3 and both item slots 0 after vrhInit, even over a dirty state */
static void test_init_item_slots_zero(void)
{
	struct vrhstate s;
	CHECK_EQ(VRH_HIP, 0);
	CHECK_EQ(VRH_SHOULDER, 1);
	CHECK_EQ(VRH_GUN3, 2);
	CHECK_EQ(VRH_POUCH, 3);
	CHECK_EQ(VRH_RHIP, 4);
	CHECK_EQ(VRH_NUM_SLOTS, 5);
	CHECK_EQ(VRH_ZONE_RHIP, 4);
	memset(&s, 0x5a, sizeof s);
	vrhInit(&s, GUN_H, GUN_S, R);
	CHECK_EQ(s.holster[VRH_HIP], GUN_H);
	CHECK_EQ(s.holster[VRH_SHOULDER], GUN_S);
	CHECK_EQ(s.holster[VRH_GUN3], 0);
	CHECK_EQ(s.holster[VRH_POUCH], 0);
	CHECK_EQ(s.holster[VRH_RHIP], 0);
	CHECK_EQ(s.held[L], -1);
	CHECK_EQ(s.held[R], -1);
	CHECK_EQ(s.parked[L], 0);
	CHECK_EQ(s.parked[R], 0);
	/* a re-init empties item slots that were filled */
	CHECK_EQ(vrhSetItem(&s, VRH_POUCH, 50), 1);
	CHECK_EQ(vrhSetItem(&s, VRH_RHIP, 60), 1);
	vrhInit(&s, GUN_H, 0, L);
	CHECK_EQ(s.holster[VRH_POUCH], 0);
	CHECK_EQ(s.holster[VRH_RHIP], 0);
}

static void test_init_grip_held_from_first_tick_does_not_draw(void)
{
	struct rig r;
	int i;
	setup_raw(&r, GUN_H, GUN_S, L);
	r.in.grip[R] = 1;
	r.in.zone[R] = Z_HIP;
	for (i = 0; i < 4; i++) {
		step(&r);
		CHECK_EQ(r.s.held[R], -1);
		CHECK_EQ(r.out.weapon, 0);
		CHECK(!VRH_FX_HAS(r.out.fx, R, VRH_FX_DRAW));
	}
	/* let go and squeeze again: now it draws */
	r.in.grip[R] = 0;
	step(&r);
	CHECK_EQ(r.s.held[R], -1);
	r.in.grip[R] = 1;
	step(&r);
	CHECK_EQ(r.s.held[R], VRH_HIP);
	CHECK_EQ(r.out.weapon, GUN_H);
	CHECK(VRH_FX_HAS(r.out.fx, R, VRH_FX_DRAW));
}

static void test_quiet_start_is_fists(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	step(&r);
	CHECK_OUT(&r, 0, 0, R);
	CHECK_EQ(r.out.fx, 0);
	CHECK_HELD(&r, -1, -1);
}

/* ---------- draw (rules 1, 2, 10, 15, 16) ---------- */

static void test_draw_from_hip(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	press(&r, R, Z_HIP);
	CHECK_EQ(r.s.held[R], VRH_HIP);
	CHECK_EQ(r.s.held[L], -1);
	CHECK_OUT(&r, GUN_H, 0, R);
	CHECK_EQ(r.out.fx, FXH(R, VRH_FX_DRAW));
	/* rule 16: the holster still names its gun while it is drawn */
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
}

static void test_draw_from_shoulder(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	press(&r, L, Z_SH);
	CHECK_EQ(r.s.held[L], VRH_SHOULDER);
	CHECK_EQ(r.s.held[R], -1);
	CHECK_OUT(&r, GUN_S, 0, L);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_DRAW));
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
}

static void test_draw_empty_holster_buzzes(void)
{
	struct rig r;
	setup(&r, GUN_H, 0, R);
	press(&r, L, Z_SH);
	CHECK_HELD(&r, -1, -1);
	CHECK_OUT(&r, 0, 0, R);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_EMPTY));
	/* the buzz is an edge: holding the grip does not repeat it */
	step(&r);
	CHECK_EQ(r.out.fx, 0);
	CHECK_EQ(r.s.held[L], -1);
	/* the other holster still works for the right hand */
	press(&r, R, Z_HIP);
	CHECK_EQ(r.s.held[R], VRH_HIP);
	CHECK_EQ(r.out.fx, FXH(R, VRH_FX_DRAW));
}

static void test_press_outside_zones_draws_nothing(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	press(&r, R, Z_NONE);
	CHECK_HELD(&r, -1, -1);
	CHECK_OUT(&r, 0, 0, R);
	release(&r, R, Z_NONE);
	/* rule 36: the chest is the pouch's zone now; an empty pouch draws nothing and buzzes empty
	 * (rule 37), as an empty gun holster does */
	press(&r, R, Z_CHEST);
	CHECK_HELD(&r, -1, -1);
	CHECK_OUT(&r, 0, 0, R);
	CHECK_EQ(r.out.fx, FXH(R, VRH_FX_EMPTY));
	release(&r, R, Z_CHEST);
	press(&r, R, Z_RHIP);
	CHECK_HELD(&r, -1, -1);
	CHECK_OUT(&r, 0, 0, R);
	CHECK_EQ(r.out.fx, FXH(R, VRH_FX_EMPTY));
}

static void test_first_gun_sets_gunside_left(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	press(&r, L, Z_HIP);
	CHECK_OUT(&r, GUN_H, 0, L);
}

static void test_first_gun_sets_gunside_right(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, L);
	press(&r, R, Z_SH);
	CHECK_OUT(&r, GUN_S, 0, R);
}

/* ---------- pairs (rules 3, 2) ---------- */

static void test_pair_second_hand_takes_copy(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, L);
	r.in.haspair[VRH_HIP] = 1;
	press(&r, R, Z_HIP);
	CHECK_OUT(&r, GUN_H, 0, R);
	press(&r, L, Z_HIP);
	CHECK_HELD(&r, VRH_HIP, VRH_HIP);
	/* rule 2 only moves the side when the other hand holds nothing */
	CHECK_OUT(&r, GUN_H, 1, R);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_DRAW));
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
}

static void test_pair_at_shoulder(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	r.in.haspair[VRH_SHOULDER] = 1;
	press(&r, L, Z_SH);
	press(&r, R, Z_SH);
	CHECK_HELD(&r, VRH_SHOULDER, VRH_SHOULDER);
	CHECK_OUT(&r, GUN_S, 1, L);
}

static void test_no_pair_second_hand_buzzes_empty(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, L);
	r.in.haspair[VRH_SHOULDER] = 1; /* a pair of the other gun does not help */
	press(&r, R, Z_HIP);
	press(&r, L, Z_HIP);
	CHECK_HELD(&r, -1, VRH_HIP);
	CHECK_OUT(&r, GUN_H, 0, R);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_EMPTY));
}

/* ---------- drawing the other holster with a gun out (rules 4, 15) ---------- */

static void test_other_holster_returns_other_hands_gun(void)
{
	struct rig r;
	int i;
	setup(&r, GUN_H, GUN_S, L);
	press(&r, R, Z_HIP);
	CHECK_OUT(&r, GUN_H, 0, R);
	press(&r, L, Z_SH);
	CHECK_HELD(&r, VRH_SHOULDER, -1);
	CHECK_OUT(&r, GUN_S, 0, L);
	CHECK(VRH_FX_HAS(r.out.fx, L, VRH_FX_DRAW));
	CHECK(VRH_FX_HAS(r.out.fx, R, VRH_FX_HOLSTER));
	CHECK(!VRH_FX_HAS(r.out.fx, R, VRH_FX_SWAP));
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	/* R still grips at the hip: that is not a new press (edge only) */
	for (i = 0; i < 3; i++) {
		step(&r);
		CHECK_HELD(&r, VRH_SHOULDER, -1);
		CHECK_OUT(&r, GUN_S, 0, L);
		CHECK_EQ(r.out.fx, 0);
	}
	/* R lets go with an empty hand: nothing happens */
	release(&r, R, Z_HIP);
	CHECK_HELD(&r, VRH_SHOULDER, -1);
	CHECK_EQ(r.out.fx, 0);
	/* and a new squeeze takes the hip gun back the same way */
	press(&r, R, Z_HIP);
	CHECK_HELD(&r, -1, VRH_HIP);
	CHECK_OUT(&r, GUN_H, 0, R);
	CHECK(VRH_FX_HAS(r.out.fx, L, VRH_FX_HOLSTER));
}

static void test_other_holster_empty_buzzes(void)
{
	struct rig r;
	setup(&r, GUN_H, 0, R);
	press(&r, R, Z_HIP);
	press(&r, L, Z_SH);
	CHECK_EQ(r.s.held[L], -1);
	CHECK(VRH_FX_HAS(r.out.fx, L, VRH_FX_EMPTY));
	CHECK(!VRH_FX_HAS(r.out.fx, L, VRH_FX_DRAW));
}

/* ---------- hold (rule 5) ---------- */

static void test_hold_keeps_gun_through_zones(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	press(&r, R, Z_HIP);
	move(&r, R, Z_NONE);
	move(&r, R, Z_SH);
	move(&r, R, Z_CHEST);
	move(&r, R, Z_RHIP);
	move(&r, R, Z_HIP);
	step(&r);
	CHECK_EQ(r.s.held[R], VRH_HIP);
	CHECK_OUT(&r, GUN_H, 0, R);
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	CHECK(!VRH_FX_HAS(r.out.fx, R, VRH_FX_HOLSTER));
	/* the empty hand squeezing outside the zones changes nothing */
	press(&r, L, Z_NONE);
	CHECK_HELD(&r, -1, VRH_HIP);
	CHECK_OUT(&r, GUN_H, 0, R);
}

/* ---------- release (rules 6, 18) ---------- */

static void test_release_in_own_zone_holsters(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	press(&r, R, Z_HIP);
	release(&r, R, Z_HIP);
	CHECK_HELD(&r, -1, -1);
	CHECK_OUT(&r, 0, 0, R);
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	CHECK(VRH_FX_HAS(r.out.fx, R, VRH_FX_HOLSTER));
	CHECK(!VRH_FX_HAS(r.out.fx, R, VRH_FX_SWAP));
	CHECK_EQ(FXOF(r.out.fx, L), 0);
}

static void test_release_no_zone_holsters(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, L);
	press(&r, L, Z_SH);
	release(&r, L, Z_NONE);
	CHECK_HELD(&r, -1, -1);
	CHECK_OUT(&r, 0, 0, L);
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_HOLSTER));
}

/* rule 40: a gun released in an item zone goes to its own holster, empty slot or not */
static void test_release_at_chest_holsters(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	press(&r, R, Z_HIP);
	release(&r, R, Z_CHEST);
	CHECK_HELD(&r, -1, -1);
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	CHECK(VRH_FX_HAS(r.out.fx, R, VRH_FX_HOLSTER));
	CHECK(!VRH_FX_HAS(r.out.fx, R, VRH_FX_SWAP));
	press(&r, L, Z_SH);
	release(&r, L, Z_RHIP);
	CHECK_HELD(&r, -1, -1);
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_HOLSTER));
	CHECK_EQ(r.s.holster[VRH_POUCH], 0);
	CHECK_EQ(r.s.holster[VRH_RHIP], 0);
}

static void test_release_at_other_holster_swaps(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	press(&r, R, Z_HIP);
	release(&r, R, Z_SH);
	CHECK_HELD(&r, -1, -1);
	CHECK_OUT(&r, 0, 0, R);
	CHECK_HOLSTERS(&r, GUN_S, GUN_H);
	CHECK(VRH_FX_HAS(r.out.fx, R, VRH_FX_SWAP));
	CHECK_EQ(FXOF(r.out.fx, L), 0);
	/* the hip now gives the old shoulder gun */
	press(&r, L, Z_HIP);
	CHECK_EQ(r.s.held[L], VRH_HIP);
	CHECK_OUT(&r, GUN_S, 0, L);
}

static void test_release_shoulder_gun_at_hip_swaps(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	press(&r, L, Z_SH);
	release(&r, L, Z_HIP);
	CHECK_HOLSTERS(&r, GUN_S, GUN_H);
	CHECK_HELD(&r, -1, -1);
	CHECK(VRH_FX_HAS(r.out.fx, L, VRH_FX_SWAP));
}

static void test_release_at_empty_other_holster_swaps(void)
{
	struct rig r;
	setup(&r, GUN_H, 0, R);
	press(&r, R, Z_HIP);
	release(&r, R, Z_SH);
	CHECK_HOLSTERS(&r, 0, GUN_H);
	CHECK_HELD(&r, -1, -1);
	CHECK(VRH_FX_HAS(r.out.fx, R, VRH_FX_SWAP));
	/* the hip is now empty and the shoulder has the gun */
	press(&r, R, Z_HIP);
	CHECK_EQ(r.s.held[R], -1);
	CHECK(VRH_FX_HAS(r.out.fx, R, VRH_FX_EMPTY));
	release(&r, R, Z_HIP);
	press(&r, R, Z_SH);
	CHECK_EQ(r.s.held[R], VRH_SHOULDER);
	CHECK_OUT(&r, GUN_H, 0, R);
}

/* ---------- dual release (rules 7, 17, 26) ---------- */

static void test_dual_release_gunside_hand_moves_gunside(void)
{
	struct rig r;
	make_dual_hip(&r);
	CHECK_OUT(&r, GUN_H, 1, R);
	release(&r, R, Z_NONE);
	CHECK_HELD(&r, VRH_HIP, -1);
	CHECK_OUT(&r, GUN_H, 0, L);
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	CHECK(VRH_FX_HAS(r.out.fx, R, VRH_FX_HOLSTER));
	CHECK_EQ(FXOF(r.out.fx, L), 0);
}

static void test_dual_release_other_hand_keeps_gunside(void)
{
	struct rig r;
	make_dual_hip(&r);
	release(&r, L, Z_NONE);
	CHECK_HELD(&r, -1, VRH_HIP);
	CHECK_OUT(&r, GUN_H, 0, R);
	CHECK(VRH_FX_HAS(r.out.fx, L, VRH_FX_HOLSTER));
	CHECK_EQ(FXOF(r.out.fx, R), 0);
}

static void test_dual_release_at_other_holster_never_swaps(void)
{
	struct rig r;
	make_dual_hip(&r);
	release(&r, R, Z_SH);
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	CHECK_HELD(&r, VRH_HIP, -1);
	CHECK_OUT(&r, GUN_H, 0, L);
	CHECK(!VRH_FX_HAS(r.out.fx, R, VRH_FX_SWAP));
	CHECK(VRH_FX_HAS(r.out.fx, R, VRH_FX_HOLSTER));
	/* the remaining single gun can swap again */
	release(&r, L, Z_SH);
	CHECK_HOLSTERS(&r, GUN_S, GUN_H);
	CHECK(VRH_FX_HAS(r.out.fx, L, VRH_FX_SWAP));
}

/* Rule 17 judged against the start of the tick: both copies of a shoulder pair let go in one tick
 * with one hand inside the hip zone (the other holster). Each gun side, each hand at the hip: no
 * swap, both hands empty, one holster fx per hand. */
static void test_dual_both_release_same_tick_never_swaps(void)
{
	struct rig r;
	int gs, hiphand;
	for (gs = 0; gs < 2; gs++) {
		for (hiphand = 0; hiphand < 2; hiphand++) {
			int32_t first = gs, second = 1 - gs;
			setup(&r, GUN_H, GUN_S, R);
			r.in.haspair[VRH_SHOULDER] = 1;
			press(&r, first, Z_SH);
			press(&r, second, Z_SH);
			move(&r, L, Z_NONE);
			move(&r, R, Z_NONE);
			CHECK_HELD(&r, VRH_SHOULDER, VRH_SHOULDER);
			CHECK_OUT(&r, GUN_S, 1, gs);
			move(&r, hiphand, Z_HIP);
			r.in.grip[L] = 0;
			r.in.grip[R] = 0;
			step(&r);
			if (r.s.holster[VRH_HIP] != GUN_H) {
				printf("  (gunside %d, hip hand %d)\n", gs, hiphand);
			}
			CHECK_HOLSTERS(&r, GUN_H, GUN_S);
			CHECK_HELD(&r, -1, -1);
			CHECK_EQ(r.s.parked[L], 0);
			CHECK_EQ(r.s.parked[R], 0);
			CHECK_EQ(r.out.weapon, 0);
			CHECK_EQ(r.out.dual, 0);
			CHECK_EQ(r.out.fx, FXH(L, VRH_FX_HOLSTER) | FXH(R, VRH_FX_HOLSTER));
			step(&r);
			CHECK_EQ(r.out.fx, 0);
			CHECK_HOLSTERS(&r, GUN_H, GUN_S);

			/* rule 6 is unaffected in a later tick: a single gun released at the hip swaps */
			press(&r, hiphand, Z_SH);
			CHECK_EQ(r.s.held[hiphand], VRH_SHOULDER);
			release(&r, hiphand, Z_HIP);
			CHECK_HOLSTERS(&r, GUN_S, GUN_H);
			CHECK_HELD(&r, -1, -1);
			CHECK_EQ(r.out.fx, FXH(hiphand, VRH_FX_SWAP));
		}
	}
}

/* Rules 6 and 17: one copy of a pair lets go, the other lets go at the hip a tick later: at the
 * start of that tick only one hand held, so it swaps */
static void test_dual_release_then_single_release_next_tick_swaps(void)
{
	struct rig r;
	int gs;
	for (gs = 0; gs < 2; gs++) {
		setup(&r, GUN_H, GUN_S, R);
		r.in.haspair[VRH_SHOULDER] = 1;
		press(&r, gs, Z_SH);
		press(&r, 1 - gs, Z_SH);
		move(&r, L, Z_NONE);
		move(&r, R, Z_NONE);
		move(&r, 1 - gs, Z_HIP);
		r.in.grip[gs] = 0;
		step(&r);
		CHECK_HOLSTERS(&r, GUN_H, GUN_S);
		CHECK_EQ(r.out.fx, FXH(gs, VRH_FX_HOLSTER));
		r.in.grip[1 - gs] = 0;
		step(&r);
		CHECK_HOLSTERS(&r, GUN_S, GUN_H);
		CHECK_HELD(&r, -1, -1);
		CHECK_EQ(r.out.fx, FXH(1 - gs, VRH_FX_SWAP));
	}
}

/* ---------- the reload owns the shoulder (rules 8, 18) ---------- */

static void test_reloading_ignores_shoulder_press(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	press(&r, R, Z_HIP);
	r.in.reloading = 1;
	press(&r, L, Z_SH);
	CHECK_HELD(&r, -1, VRH_HIP);
	CHECK_OUT(&r, GUN_H, 0, R);
	CHECK_EQ(r.out.fx, 0);
	/* the same press without the reload is rule 4 */
	release(&r, L, Z_SH);
	r.in.reloading = 0;
	press(&r, L, Z_SH);
	CHECK_HELD(&r, VRH_SHOULDER, -1);
	CHECK_OUT(&r, GUN_S, 0, L);
}

static void test_reloading_ignores_shoulder_press_on_empty_holster(void)
{
	struct rig r;
	setup(&r, GUN_H, 0, R);
	press(&r, R, Z_HIP);
	r.in.reloading = 1;
	press(&r, L, Z_SH);
	CHECK_EQ(r.s.held[L], -1);
	CHECK_EQ(FXOF(r.out.fx, L), 0); /* no empty buzz either: the press is the reload's */
}

/* Rules 8 and 29: the free hand's hip press waits while reloading; once the reload ends the hip
 * draws the second copy */
static void test_reloading_free_hand_hip_draws_after_reload(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	r.in.haspair[VRH_HIP] = 1;
	press(&r, R, Z_HIP);
	move(&r, R, Z_NONE);
	r.in.reloading = 1;
	press(&r, L, Z_HIP);
	CHECK_HELD(&r, -1, VRH_HIP);
	CHECK_OUT(&r, GUN_H, 0, R);
	CHECK_EQ(r.out.fx, 0);
	release(&r, L, Z_HIP);
	CHECK_EQ(r.out.fx, 0);
	r.in.reloading = 0;
	press(&r, L, Z_HIP);
	CHECK_HELD(&r, VRH_HIP, VRH_HIP);
	CHECK_OUT(&r, GUN_H, 1, R);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_DRAW));
}

/* Rules 8 and 29: the gun side's hand is not the free hand: while reloading its shoulder press is
 * the reload's, but its hip press still draws */
static void test_reloading_gunside_hand_hip_draws_shoulder_ignored(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	r.in.reloading = 1;
	press(&r, R, Z_SH);
	CHECK_HELD(&r, -1, -1);
	CHECK_EQ(r.out.fx, 0);
	release(&r, R, Z_SH);
	press(&r, R, Z_HIP);
	CHECK_HELD(&r, -1, VRH_HIP);
	CHECK_OUT(&r, GUN_H, 0, R);
	CHECK_EQ(r.out.fx, FXH(R, VRH_FX_DRAW));
	/* the same with the gun side on the left */
	setup(&r, GUN_H, GUN_S, L);
	r.in.reloading = 1;
	press(&r, L, Z_HIP);
	CHECK_HELD(&r, VRH_HIP, -1);
	CHECK_OUT(&r, GUN_H, 0, L);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_DRAW));
}

/* ---------- the free hand waits during a reload (rule 29) ---------- */

/* Rule 29: no draw (rule 1), no rule 4 return, no second copy (rule 3), no empty buzz, in any zone */
static void test_free_hand_press_while_reloading_does_nothing(void)
{
	struct rig r;
	struct vrhstate before;

	/* without the reload this hip press is rule 4: R's shoulder gun would go back */
	setup(&r, GUN_H, GUN_S, R);
	press(&r, R, Z_SH);
	move(&r, R, Z_NONE);
	r.in.reloading = 1;
	press(&r, L, Z_HIP);
	CHECK_HELD(&r, -1, VRH_SHOULDER);
	CHECK_OUT(&r, GUN_S, 0, R);
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	CHECK_EQ(r.out.fx, 0);

	/* without the reload this is rule 3 with no pair: an empty buzz */
	setup(&r, GUN_H, GUN_S, R);
	press(&r, R, Z_HIP);
	move(&r, R, Z_NONE);
	r.in.reloading = 1;
	press(&r, L, Z_HIP);
	CHECK_HELD(&r, -1, VRH_HIP);
	CHECK_OUT(&r, GUN_H, 0, R);
	CHECK_EQ(r.out.fx, 0);

	/* gun side on the left, R is the free hand: an empty hip would buzz empty (rule 1) */
	setup(&r, 0, GUN_S, L);
	press(&r, L, Z_SH);
	move(&r, L, Z_NONE);
	r.in.reloading = 1;
	press(&r, R, Z_HIP);
	CHECK_HELD(&r, VRH_SHOULDER, -1);
	CHECK_OUT(&r, GUN_S, 0, L);
	CHECK_EQ(r.out.fx, 0);
	release(&r, R, Z_HIP);
	CHECK_EQ(r.out.fx, 0);
	press(&r, R, Z_SH);
	CHECK_HELD(&r, VRH_SHOULDER, -1);
	CHECK_EQ(r.out.fx, 0);
	release(&r, R, Z_SH);
	press(&r, R, Z_NONE);
	CHECK_HELD(&r, VRH_SHOULDER, -1);
	CHECK_EQ(r.out.fx, 0);
	release(&r, R, Z_NONE);

	/* both hands empty: the gun side alone decides who is free; L would draw and take the side */
	setup(&r, GUN_H, GUN_S, R);
	r.in.reloading = 1;
	before = r.s;
	r.in.zone[L] = Z_HIP;
	r.in.grip[L] = 1;
	step(&r);
	CHECK_HELD(&r, -1, -1);
	CHECK_OUT(&r, 0, 0, R);
	CHECK_EQ(r.out.fx, 0);
	/* only prevgrip and prevzone moved */
	before.prevgrip[L] = 1;
	before.prevzone[L] = Z_HIP;
	CHECK(memcmp(&before, &r.s, sizeof before) == 0);

	/* a parked free hand: no unpark at the shoulder either (rules 19 and 29) */
	make_parked_left(&r);
	press(&r, L, Z_SH);
	CHECK_EQ(r.s.parked[L], 1);
	CHECK_HELD(&r, VRH_HIP, VRH_HIP);
	CHECK_OUT(&r, GUN_H, 1, R);
	CHECK_EQ(r.out.fx, 0);
}

/* Rule 29: the blocked press still takes the edge: a grip held past the end of the reload draws
 * nothing; the hand lets go and squeezes again to draw */
static void test_free_hand_blocked_press_edge_consumed(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	press(&r, R, Z_SH);
	move(&r, R, Z_NONE);
	r.in.reloading = 1;
	press(&r, L, Z_HIP);
	CHECK_EQ(r.s.prevgrip[L], 1);
	r.in.reloading = 0;
	step(&r);
	CHECK_EQ(r.out.fx, 0);
	step(&r);
	CHECK_EQ(r.out.fx, 0);
	CHECK_HELD(&r, -1, VRH_SHOULDER);
	release(&r, L, Z_HIP);
	CHECK_EQ(r.out.fx, 0);
	CHECK_HELD(&r, -1, VRH_SHOULDER);
	press(&r, L, Z_HIP);
	CHECK_HELD(&r, VRH_HIP, -1);
	CHECK_OUT(&r, GUN_H, 0, L);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_DRAW) | FXH(R, VRH_FX_HOLSTER));
}

/* Rules 29, 33 and 9: a free hand holding a copy with its grip open (make_free_left_grip_open)
 * parks on the first reloading tick; its press then does nothing (no unpark), and its release
 * does nothing either; the first squeeze after the reload unparks it */
static void test_free_hand_blocked_press_then_release_stays_parked(void)
{
	struct rig r;
	make_free_left_grip_open(&r); /* R the gun side, L the free hand, grip open */
	r.in.reloading = 1;
	step(&r);
	CHECK_OUT(&r, GUN_X, 1, R);
	CHECK_EQ(r.s.parked[L], 1);
	CHECK_EQ(r.out.fx, 0);
	press(&r, L, Z_NONE);
	CHECK_EQ(r.s.parked[L], 1);
	CHECK_HELD(&r, VRH_HIP, VRH_HIP);
	CHECK_OUT(&r, GUN_X, 1, R);
	CHECK_EQ(r.out.fx, 0);
	release(&r, L, Z_NONE);
	CHECK_EQ(r.s.parked[L], 1);
	CHECK_HELD(&r, VRH_HIP, VRH_HIP);
	CHECK_OUT(&r, GUN_X, 1, R);
	CHECK_EQ(r.out.fx, 0);
	r.in.reloading = 0;
	press(&r, L, Z_NONE);
	CHECK_EQ(r.s.parked[L], 0);
	CHECK_OUT(&r, GUN_X, 1, R);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_DRAW));
}

static void test_reloading_shoulder_release_holsters(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	press(&r, R, Z_HIP);
	r.in.reloading = 1;
	release(&r, R, Z_SH);
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	CHECK_HELD(&r, -1, -1);
	CHECK(VRH_FX_HAS(r.out.fx, R, VRH_FX_HOLSTER));
	CHECK(!VRH_FX_HAS(r.out.fx, R, VRH_FX_SWAP));
}

static void test_reloading_hip_release_of_shoulder_gun_still_swaps(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	press(&r, R, Z_SH);
	move(&r, R, Z_NONE);
	r.in.reloading = 1;
	release(&r, R, Z_HIP);
	CHECK_HOLSTERS(&r, GUN_S, GUN_H);
	CHECK(VRH_FX_HAS(r.out.fx, R, VRH_FX_SWAP));
}

/* ---------- parking (rules 9, 19, 26, 33) ---------- */

static void test_park_while_reloading_release_does_nothing(void)
{
	struct rig r;
	make_parked_left(&r);
	CHECK_EQ(r.s.parked[L], 1);
	CHECK_EQ(r.s.parked[R], 0);
	CHECK_HELD(&r, VRH_HIP, VRH_HIP);
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	/* rule 19: still dual; rule 26: no fx */
	CHECK_OUT(&r, GUN_H, 1, R);
	CHECK_EQ(r.out.fx, 0);
}

static void test_parked_release_at_hip_zone_stays_parked(void)
{
	struct rig r;
	make_dual_hip(&r);
	r.in.reloading = 1;
	release(&r, L, Z_HIP);
	CHECK_EQ(r.s.parked[L], 1);
	CHECK_HELD(&r, VRH_HIP, VRH_HIP);
	CHECK_OUT(&r, GUN_H, 1, R);
	CHECK_EQ(FXOF(r.out.fx, L), 0);
}

static void test_parked_press_during_reload_does_nothing(void)
{
	struct rig r;
	make_parked_left(&r);
	press(&r, L, Z_HIP);
	CHECK_EQ(r.s.parked[L], 1);
	CHECK_HELD(&r, VRH_HIP, VRH_HIP);
	CHECK_OUT(&r, GUN_H, 1, R);
	CHECK_EQ(r.out.fx, 0);
	release(&r, L, Z_NONE);
	CHECK_EQ(r.s.parked[L], 1);
	CHECK_EQ(r.out.fx, 0);
	press(&r, L, Z_NONE);
	CHECK_EQ(r.s.parked[L], 1);
	CHECK_EQ(r.out.fx, 0);
	release(&r, L, Z_NONE);
}

static void test_unpark_after_reload_wherever_the_hand_is(void)
{
	struct rig r;
	make_parked_left(&r);
	r.in.reloading = 0;
	step(&r);
	CHECK_EQ(r.s.parked[L], 1); /* the reload ends with the grip open: still parked */
	press(&r, L, Z_SH);
	CHECK_EQ(r.s.parked[L], 0);
	CHECK_HELD(&r, VRH_HIP, VRH_HIP); /* the hip gun comes back, not the shoulder gun */
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	CHECK_OUT(&r, GUN_H, 1, R);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_DRAW));
}

static void test_unpark_at_no_zone(void)
{
	struct rig r;
	make_parked_left(&r);
	r.in.reloading = 0;
	press(&r, L, Z_NONE);
	CHECK_EQ(r.s.parked[L], 0);
	CHECK_HELD(&r, VRH_HIP, VRH_HIP);
	CHECK_OUT(&r, GUN_H, 1, R);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_DRAW));
	/* a normal release afterwards is a dual release again (rule 7) */
	release(&r, L, Z_NONE);
	CHECK_HELD(&r, -1, VRH_HIP);
	CHECK_OUT(&r, GUN_H, 0, R);
}

/* Rule 9: a parked hand that squeezes and lets go again during the reload (the release does
 * nothing, also inside a holster zone) ends the reload with its grip open: it stays parked
 * until its next press. The end of the reload alone unparks only a held grip. */
static void test_parked_release_during_reload_stays_parked_after(void)
{
	struct rig r;
	make_parked_left(&r);
	press(&r, L, Z_NONE); /* during the reload: the reload's press, still parked */
	CHECK_EQ(r.s.parked[L], 1);
	release(&r, L, Z_NONE);
	CHECK_EQ(r.s.parked[L], 1);
	CHECK_HELD(&r, VRH_HIP, VRH_HIP);
	CHECK_OUT(&r, GUN_H, 1, R);
	CHECK_EQ(r.out.fx, 0);
	press(&r, L, Z_HIP);
	release(&r, L, Z_HIP);
	CHECK_EQ(r.s.parked[L], 1);
	CHECK_HELD(&r, VRH_HIP, VRH_HIP);
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	CHECK_EQ(r.out.fx, 0);
	r.in.reloading = 0;
	step(&r);
	step(&r);
	CHECK_EQ(r.s.parked[L], 1);
	CHECK_OUT(&r, GUN_H, 1, R);
	CHECK_EQ(r.out.fx, 0);
	/* the next press unparks */
	press(&r, L, Z_NONE);
	CHECK_EQ(r.s.parked[L], 0);
	CHECK_HELD(&r, VRH_HIP, VRH_HIP);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_DRAW));
}

/* Rules 7, 31 and 33: the gun side lets go in the tick its reload starts, L's grip still held. L
 * parks at the start of the tick (rule 33), so R's release finds it parked and both copies go
 * home (rule 31); R never parks. Later reloading ticks park nothing: no hand holds. */
static void test_gunside_release_on_first_reloading_tick_holsters_both(void)
{
	struct rig r;
	make_dual_hip(&r); /* gunside R */
	r.in.reloading = 1;
	release(&r, R, Z_NONE);
	CHECK_HELD(&r, -1, -1);
	CHECK_EQ(r.s.parked[L], 0);
	CHECK_EQ(r.s.parked[R], 0);
	CHECK_OUT(&r, 0, 0, L);
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_HOLSTER) | FXH(R, VRH_FX_HOLSTER));
	step(&r);
	CHECK_EQ(r.s.parked[L], 0);
	CHECK_EQ(r.s.parked[R], 0);
	CHECK_HELD(&r, -1, -1);
	CHECK_OUT(&r, 0, 0, L);
	CHECK_EQ(r.out.fx, 0);
}

/* Rules 7, 9 and 17: a parked copy counts as held, so after the reload the gun side's release
 * inside the other holster's zone is a dual release: no swap (rule 6 would swap a single gun),
 * and rule 31 sends both copies home */
static void test_parked_counts_as_held_for_dual_release(void)
{
	struct rig r;
	make_parked_left_after_reload(&r);
	release(&r, R, Z_SH);
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	CHECK_HELD(&r, -1, -1);
	CHECK_EQ(r.s.parked[L], 0);
	CHECK_OUT(&r, 0, 0, L);
	CHECK(!VRH_FX_HAS(r.out.fx, R, VRH_FX_SWAP));
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_HOLSTER) | FXH(R, VRH_FX_HOLSTER));
}

/* Rules 3, 9 and 14 on a built state (without items only a built state leaves a parked copy
 * alone; since rule 41 an item that goes home does too): L holds the hip gun parked with its grip
 * open (a held grip would unpark it, rule 9), R is empty and the gun side. The parked gun
 * counts as held: no zone buzz for R at the hip and no copy without a pair (EMPTY); with the
 * pair R takes the second copy and L stays parked. Rule 42: the lone parked gun is not the
 * engine's (fists) until R takes the copy. */
static void test_parked_counts_as_held_for_pairs(void)
{
	struct rig r;
	build(&r, GUN_H, GUN_S, VRH_HIP, -1, R);
	r.s.parked[L] = 1;
	r.s.prevgrip[L] = 0; /* grip open: rule 9 leaves it parked */
	r.in.grip[L] = 0;
	release(&r, R, Z_SH); /* an empty hand: nothing */
	CHECK_EQ(r.out.fx, 0);
	move(&r, R, Z_HIP);
	CHECK_EQ(r.out.fx, 0);
	press(&r, R, Z_HIP);
	CHECK_HELD(&r, VRH_HIP, -1);
	CHECK_EQ(r.s.parked[L], 1);
	CHECK_OUT(&r, 0, 0, R);
	CHECK_EQ(r.out.fx, FXH(R, VRH_FX_EMPTY));
	release(&r, R, Z_HIP);
	r.in.haspair[VRH_HIP] = 1;
	press(&r, R, Z_HIP);
	CHECK_HELD(&r, VRH_HIP, VRH_HIP);
	CHECK_EQ(r.s.parked[L], 1);
	CHECK_EQ(r.s.parked[R], 0);
	CHECK_OUT(&r, GUN_H, 1, R);
	CHECK_EQ(r.out.fx, FXH(R, VRH_FX_DRAW));
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
}

/* Rules 4 and 9 on a built state (as above): L holds the hip gun parked, R (empty, the gun side)
 * draws the shoulder gun. The parked gun counts as held, so it goes back to its holster: L is
 * empty and no longer parked, with a holster fx. */
static void test_parked_counts_as_held_for_other_holster(void)
{
	struct rig r;
	build(&r, GUN_H, GUN_S, VRH_HIP, -1, R);
	r.s.parked[L] = 1;
	r.s.prevgrip[L] = 0; /* grip open: rule 9 leaves it parked */
	r.in.grip[L] = 0;
	release(&r, R, Z_SH); /* an empty hand: nothing */
	CHECK_EQ(r.out.fx, 0);
	press(&r, R, Z_SH);
	CHECK_HELD(&r, -1, VRH_SHOULDER);
	CHECK_EQ(r.s.parked[L], 0);
	CHECK_EQ(r.s.parked[R], 0);
	CHECK_OUT(&r, GUN_S, 0, R);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_HOLSTER) | FXH(R, VRH_FX_DRAW));
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
}

/* ---------- park at once during a reload (rule 33) ---------- */

/* Rule 33: the first reloading tick parks the free hand's copy with its grip still held, no fx */
static void test_park_at_once_grip_held(void)
{
	struct rig r;
	make_dual_hip(&r); /* gunside R, both grips held */
	r.in.reloading = 1;
	step(&r);
	CHECK_EQ(r.s.parked[L], 1);
	CHECK_EQ(r.s.parked[R], 0);
	CHECK_HELD(&r, VRH_HIP, VRH_HIP);
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	CHECK_OUT(&r, GUN_H, 1, R);
	CHECK_EQ(r.out.fx, 0);
	/* its later release does nothing (rule 9) */
	release(&r, L, Z_NONE);
	CHECK_EQ(r.s.parked[L], 1);
	CHECK_HELD(&r, VRH_HIP, VRH_HIP);
	CHECK_EQ(r.out.fx, 0);

	/* mirrored: gunside L, R parks */
	setup(&r, GUN_H, GUN_S, R);
	r.in.haspair[VRH_SHOULDER] = 1;
	press(&r, L, Z_SH);
	press(&r, R, Z_SH);
	move(&r, R, Z_NONE);
	CHECK_OUT(&r, GUN_S, 1, L);
	r.in.reloading = 1;
	step(&r);
	CHECK_EQ(r.s.parked[R], 1);
	CHECK_EQ(r.s.parked[L], 0);
	CHECK_OUT(&r, GUN_S, 1, L);
	CHECK_EQ(r.out.fx, 0);
}

/* Rule 33: the free hand's grip opens in the very tick the reload starts: it parks (before the
 * release, rule 24), so the release does nothing: no rule 7 holster */
static void test_park_at_once_grip_released_same_tick(void)
{
	struct rig r;
	make_dual_hip(&r);
	r.in.reloading = 1;
	r.in.grip[L] = 0;
	step(&r);
	CHECK_EQ(r.s.parked[L], 1);
	CHECK_HELD(&r, VRH_HIP, VRH_HIP);
	CHECK_OUT(&r, GUN_H, 1, R);
	CHECK_EQ(r.out.fx, 0);
}

/* Rule 33: a free hand holding a copy with its grip open (make_free_left_grip_open) parks on the
 * first reloading tick with no grip edge at all */
static void test_park_at_once_grip_open(void)
{
	struct rig r;
	make_free_left_grip_open(&r); /* R the gun side, L the free hand, grip open */
	step(&r);
	CHECK_EQ(r.s.prevgrip[L], 0);
	CHECK_EQ(r.s.parked[L], 0);
	r.in.reloading = 1;
	step(&r);
	CHECK_EQ(r.s.parked[L], 1);
	CHECK_EQ(r.s.prevgrip[L], 0);
	CHECK_HELD(&r, VRH_HIP, VRH_HIP);
	CHECK_OUT(&r, GUN_X, 1, R);
	CHECK_EQ(r.out.fx, 0);
}

/* Rule 33: parking is not a grip edge, so an untracked free hand parks too, grip held or (while
 * untracked) opened; tracking returning later adds nothing */
static void test_park_at_once_untracked(void)
{
	struct rig r;
	int i;
	for (i = 0; i < 2; i++) {
		make_dual_hip(&r);
		r.in.tracked[L] = 0;
		r.in.grip[L] = i; /* 0: opened while untracked */
		r.in.reloading = 1;
		step(&r);
		CHECK_EQ(r.s.parked[L], 1);
		CHECK_HELD(&r, VRH_HIP, VRH_HIP);
		CHECK_OUT(&r, GUN_H, 1, R);
		CHECK_EQ(r.out.fx, 0);
		r.in.tracked[L] = 1;
		step(&r);
		CHECK_EQ(r.s.parked[L], 1);
		CHECK_HELD(&r, VRH_HIP, VRH_HIP);
		CHECK_EQ(r.out.fx, 0);
	}
}

/* Rules 24, 30 and 33: the free hand enters a holster zone in the first reloading tick: it is
 * already parked when the zone buzz runs, so no buzz; the gun side's hand still buzzes */
static void test_park_at_once_before_zone_buzz(void)
{
	struct rig r;
	make_dual_hip(&r);
	r.in.reloading = 1;
	r.in.zone[L] = Z_SH;
	step(&r);
	CHECK_EQ(r.s.parked[L], 1);
	CHECK_EQ(r.out.fx, 0);
	move(&r, L, Z_HIP);
	CHECK_EQ(r.out.fx, 0);
	/* the empty gun side's hand of a single gun: no park, its buzz is unaffected */
	setup(&r, GUN_H, GUN_S, R);
	r.in.reloading = 1;
	r.in.zone[R] = Z_HIP;
	step(&r);
	CHECK_EQ(r.out.fx, FXH(R, VRH_FX_ZONE));
}

/* Rules 11 and 33: a paused tick parks nothing; the pause holsters the pair, and once unpaused
 * with the reload still on, empty hands park nothing either */
static void test_park_not_while_paused(void)
{
	struct rig r;
	make_dual_hip(&r);
	r.in.reloading = 1;
	r.in.paused = 1;
	step(&r);
	CHECK_HELD(&r, -1, -1);
	CHECK_EQ(r.s.parked[L], 0);
	CHECK_EQ(r.s.parked[R], 0);
	CHECK_OUT(&r, 0, 0, R);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_HOLSTER) | FXH(R, VRH_FX_HOLSTER));
	step(&r);
	CHECK_EQ(r.s.parked[L], 0);
	CHECK_EQ(r.out.fx, 0);
	r.in.paused = 0;
	step(&r);
	CHECK_HELD(&r, -1, -1);
	CHECK_EQ(r.s.parked[L], 0);
	CHECK_EQ(r.s.parked[R], 0);
	CHECK_EQ(r.out.fx, 0);
}

/* Rule 33: a single gun (the free hand is empty) parks nothing, the gun side never parks */
static void test_park_not_for_single_gun(void)
{
	struct rig r;
	int gs;
	for (gs = 0; gs < 2; gs++) {
		setup(&r, GUN_H, GUN_S, R);
		press(&r, gs, Z_HIP);
		move(&r, gs, Z_NONE);
		r.in.reloading = 1;
		step(&r);
		step(&r);
		CHECK_EQ(r.s.parked[L], 0);
		CHECK_EQ(r.s.parked[R], 0);
		CHECK_EQ(r.s.held[gs], VRH_HIP);
		CHECK_EQ(r.s.held[1 - gs], -1);
		CHECK_OUT(&r, GUN_H, 0, gs);
		CHECK_EQ(r.out.fx, 0);
		/* the gun side's own release during its reload is rule 6 */
		release(&r, gs, Z_NONE);
		CHECK_HELD(&r, -1, -1);
		CHECK_EQ(r.out.fx, FXH(gs, VRH_FX_HOLSTER));
	}
}

/* Rules 32 and 33: parked at once (grip still held), the flip is refused */
static void test_park_at_once_flip_refused(void)
{
	struct rig r;
	struct vrhstate before;
	make_dual_hip(&r);
	r.in.reloading = 1;
	step(&r);
	before = r.s;
	CHECK_EQ(vrhFlipGunSide(&r.s), 0);
	CHECK(memcmp(&before, &r.s, sizeof before) == 0);
	step(&r);
	CHECK_OUT(&r, GUN_H, 1, R);
}

/* ---------- unparking when the reload ends (rule 9, amended 2026-10-16) ---------- */

/* Rule 9: a grip held through the end of the reload unparks in the first tick with reloading 0,
 * with no press, wherever the hand is: exactly one DRAW, the same copy, dual stays 1. Each side;
 * the free hand at no zone and inside the shoulder zone (a zone again once the reload ends; no
 * buzz for a hand of a pair, rule 14). Nothing parks or unparks after. */
static void test_unpark_grip_held_through_reload_end(void)
{
	struct rig r;
	int gs, z;
	for (gs = 0; gs < 2; gs++) {
		for (z = 0; z < 2; z++) {
			int32_t f = 1 - gs;
			make_parked_copy(&r, gs, VRH_HIP);
			if (z) {
				move(&r, f, Z_SH);
			}
			step(&r);
			CHECK_EQ(r.s.parked[f], 1);
			CHECK_EQ(r.out.fx, 0);
			r.in.reloading = 0;
			step(&r);
			if (r.s.parked[f] != 0) {
				printf("  (gunside %d, zone %d)\n", gs, z);
			}
			CHECK_EQ(r.s.parked[L], 0);
			CHECK_EQ(r.s.parked[R], 0);
			CHECK_HELD(&r, VRH_HIP, VRH_HIP);
			CHECK_HOLSTERS(&r, GUN_H, GUN_S);
			CHECK_OUT(&r, GUN_H, 1, gs);
			CHECK_EQ(r.out.fx, FXH(f, VRH_FX_DRAW));
			CHECK_WEAPON(&r, GUN_H, 1);
			step(&r);
			CHECK_EQ(r.s.parked[f], 0);
			CHECK_EQ(r.out.fx, 0);
		}
	}
}

/* Rule 9: a grip open when the reload ends leaves the copy parked (still dual) until the hand's
 * next press, wherever it is: then exactly one DRAW. Each side. */
static void test_unpark_grip_open_at_reload_end_waits_for_press(void)
{
	struct rig r;
	int gs;
	for (gs = 0; gs < 2; gs++) {
		int32_t f = 1 - gs;
		make_parked_copy(&r, gs, VRH_HIP);
		release(&r, f, Z_NONE); /* a parked release: nothing */
		CHECK_EQ(r.out.fx, 0);
		r.in.reloading = 0;
		step(&r);
		step(&r);
		step(&r);
		CHECK_EQ(r.s.parked[f], 1);
		CHECK_EQ(r.out.fx, 0);
		CHECK_WEAPON(&r, GUN_H, 1);
		press(&r, f, Z_NONE);
		CHECK_EQ(r.s.parked[f], 0);
		CHECK_HELD(&r, VRH_HIP, VRH_HIP);
		CHECK_OUT(&r, GUN_H, 1, gs);
		CHECK_EQ(r.out.fx, FXH(f, VRH_FX_DRAW));
		step(&r);
		CHECK_EQ(r.out.fx, 0);
	}
}

/* Rules 9 and 24: the parked hand presses in the very tick the reload ends, inside the shoulder
 * zone. The unpark runs first, so the press finds the hand holding and does nothing: one DRAW,
 * no shoulder draw, no rule 4 return, the holsters unchanged. Each side. */
static void test_unpark_same_tick_press_one_draw(void)
{
	struct rig r;
	int gs;
	for (gs = 0; gs < 2; gs++) {
		int32_t f = 1 - gs;
		make_parked_copy(&r, gs, VRH_HIP);
		release(&r, f, Z_NONE);
		move(&r, f, Z_SH);
		CHECK_EQ(r.out.fx, 0);
		r.in.reloading = 0;
		r.in.grip[f] = 1;
		step(&r);
		CHECK_EQ(r.s.parked[f], 0);
		CHECK_HELD(&r, VRH_HIP, VRH_HIP);
		CHECK_HOLSTERS(&r, GUN_H, GUN_S);
		CHECK_OUT(&r, GUN_H, 1, gs);
		CHECK_EQ(r.out.fx, FXH(f, VRH_FX_DRAW));
		step(&r);
		CHECK_EQ(r.out.fx, 0);
	}
}

/* Rules 9, 7 and 17: the gun side lets go in the first tick with reloading 0 while the parked
 * hand's grip is held. The unpark runs first, so this is plain rule 7, not rule 31: the other
 * hand keeps the gun single and becomes the gun side, DRAW on it and HOLSTER on the released
 * hand. The release is inside the shoulder zone: both held at the start of the tick, so no
 * swap (rule 17). Each side. */
static void test_unpark_grip_held_same_tick_gunside_release_is_rule_7(void)
{
	struct rig r;
	int gs;
	for (gs = 0; gs < 2; gs++) {
		int32_t f = 1 - gs;
		make_parked_copy(&r, gs, VRH_HIP);
		move(&r, gs, Z_SH);
		r.in.reloading = 0;
		r.in.grip[gs] = 0;
		step(&r);
		CHECK_EQ(r.s.held[f], VRH_HIP);
		CHECK_EQ(r.s.held[gs], -1);
		CHECK_EQ(r.s.parked[L], 0);
		CHECK_EQ(r.s.parked[R], 0);
		CHECK_HOLSTERS(&r, GUN_H, GUN_S);
		CHECK_OUT(&r, GUN_H, 0, f);
		CHECK_EQ(r.out.fx, FXH(f, VRH_FX_DRAW) | FXH(gs, VRH_FX_HOLSTER));
		CHECK_WEAPON(&r, GUN_H, 0);
	}
}

/* Rules 9, 11 and 25: a paused tick does not unpark, even with the grip held and the reload over:
 * the pause holsters both copies (one HOLSTER per hand, no DRAW) and keeps the side. The grips
 * still held after the pause are not presses. Each side. */
static void test_unpark_not_in_paused_tick(void)
{
	struct rig r;
	int gs;
	for (gs = 0; gs < 2; gs++) {
		make_parked_copy(&r, gs, VRH_HIP);
		r.in.reloading = 0;
		r.in.paused = 1;
		step(&r);
		CHECK_HELD(&r, -1, -1);
		CHECK_EQ(r.s.parked[L], 0);
		CHECK_EQ(r.s.parked[R], 0);
		CHECK_OUT(&r, 0, 0, gs);
		CHECK_EQ(r.out.fx, FXH(L, VRH_FX_HOLSTER) | FXH(R, VRH_FX_HOLSTER));
		r.in.paused = 0;
		step(&r);
		step(&r);
		CHECK_HELD(&r, -1, -1);
		CHECK_OUT(&r, 0, 0, gs);
		CHECK_EQ(r.out.fx, 0);
	}
}

/* Rules 9, 21 and 25: an untracked parked hand uses its last tracked grip. Last seen gripping, it
 * unparks when the reload ends (its untracked grip opening is not read); its deferred release on
 * return is then a rule 7 release. With a gun-side release in that same tick, plain rule 7. Last
 * seen open, it stays parked whatever its untracked grip, and its deferred press on return
 * unparks it with one DRAW. Each side. */
static void test_unpark_untracked_hand_uses_last_tracked_grip(void)
{
	struct rig r;
	int gs;
	for (gs = 0; gs < 2; gs++) {
		int32_t f = 1 - gs;

		/* last seen gripping: unparks */
		make_parked_copy(&r, gs, VRH_HIP);
		r.in.tracked[f] = 0;
		r.in.grip[f] = 0;
		step(&r);
		CHECK_EQ(r.s.parked[f], 1);
		r.in.reloading = 0;
		step(&r);
		CHECK_EQ(r.s.parked[f], 0);
		CHECK_HELD(&r, VRH_HIP, VRH_HIP);
		CHECK_OUT(&r, GUN_H, 1, gs);
		CHECK_EQ(r.out.fx, FXH(f, VRH_FX_DRAW));
		r.in.tracked[f] = 1;
		step(&r);
		CHECK_EQ(r.s.held[f], -1);
		CHECK_EQ(r.s.held[gs], VRH_HIP);
		CHECK_OUT(&r, GUN_H, 0, gs);
		CHECK_EQ(r.out.fx, FXH(f, VRH_FX_HOLSTER));

		/* last seen gripping, the gun side lets go in the same tick: plain rule 7 */
		make_parked_copy(&r, gs, VRH_HIP);
		r.in.tracked[f] = 0;
		r.in.grip[f] = 0;
		r.in.reloading = 0;
		r.in.grip[gs] = 0;
		step(&r);
		CHECK_EQ(r.s.held[f], VRH_HIP);
		CHECK_EQ(r.s.held[gs], -1);
		CHECK_EQ(r.s.parked[f], 0);
		CHECK_OUT(&r, GUN_H, 0, f);
		CHECK_EQ(r.out.fx, FXH(f, VRH_FX_DRAW) | FXH(gs, VRH_FX_HOLSTER));

		/* last seen open: stays parked until the deferred press */
		make_parked_copy(&r, gs, VRH_HIP);
		release(&r, f, Z_NONE);
		r.in.tracked[f] = 0;
		r.in.grip[f] = 1;
		r.in.reloading = 0;
		step(&r);
		step(&r);
		CHECK_EQ(r.s.parked[f], 1);
		CHECK_OUT(&r, GUN_H, 1, gs);
		CHECK_EQ(r.out.fx, 0);
		r.in.tracked[f] = 1;
		step(&r);
		CHECK_EQ(r.s.parked[f], 0);
		CHECK_HELD(&r, VRH_HIP, VRH_HIP);
		CHECK_OUT(&r, GUN_H, 1, gs);
		CHECK_EQ(r.out.fx, FXH(f, VRH_FX_DRAW));
	}
}

/* ---------- the gun side's release sends a parked copy back too (rule 31) ---------- */

/* Rule 31, gunside R, while reloading: R lets go and L's parked copy goes home too. Both hands
 * empty, one holster fx per hand and no draw, the side moves to L, the hip keeps its gun and the
 * engine gets fists. */
static void test_gunside_release_holsters_parked_copy_while_reloading(void)
{
	struct rig r;
	make_parked_left(&r); /* gunside R, still reloading */
	release(&r, R, Z_NONE);
	CHECK_HELD(&r, -1, -1);
	CHECK_EQ(r.s.parked[L], 0);
	CHECK_EQ(r.s.parked[R], 0);
	CHECK_OUT(&r, 0, 0, L);
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_HOLSTER) | FXH(R, VRH_FX_HOLSTER));
	CHECK_WEAPON(&r, 0, 0);
	/* still reloading: R is the free hand now, its press draws nothing (rule 29) */
	press(&r, R, Z_HIP);
	CHECK_HELD(&r, -1, -1);
	CHECK_EQ(r.out.fx, 0);
	release(&r, R, Z_HIP);
	CHECK_EQ(r.out.fx, 0);
	/* L, the gun side, draws a single gun at the hip (rules 1 and 8: the hip still works) */
	press(&r, L, Z_HIP);
	CHECK_HELD(&r, VRH_HIP, -1);
	CHECK_EQ(r.s.parked[L], 0);
	CHECK_OUT(&r, GUN_H, 0, L);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_DRAW));
}

/* Rules 31 and 17, mirrored: gunside L with a shoulder pair, R parked; L lets go inside the hip
 * zone (the other holster): no swap, both copies go home, the side moves to R */
static void test_gunside_release_holsters_parked_copy_mirrored(void)
{
	struct rig r;
	make_parked_copy(&r, L, VRH_SHOULDER);
	CHECK_OUT(&r, GUN_S, 1, L);
	release(&r, L, Z_HIP);
	CHECK_HELD(&r, -1, -1);
	CHECK_EQ(r.s.parked[L], 0);
	CHECK_EQ(r.s.parked[R], 0);
	CHECK_OUT(&r, 0, 0, R);
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_HOLSTER) | FXH(R, VRH_FX_HOLSTER));
	CHECK_WEAPON(&r, 0, 0);
}

/* Rule 31 after the reload: the free hand let go during the reload, so its copy stays parked when
 * the reload ends (rule 9), and the gun side's release still sends both home. Each side and each
 * holster; the release is inside the other holster's zone, where rule 17 keeps it from swapping.
 * A press at that holster then draws one gun, single (rules 1 and 2). */
static void test_gunside_release_after_reload_holsters_parked_copy(void)
{
	struct rig r;
	int gs, i;
	for (gs = 0; gs < 2; gs++) {
		for (i = 0; i < 2; i++) {
			int32_t zone = i == VRH_HIP ? Z_HIP : Z_SH;
			int32_t other = i == VRH_HIP ? Z_SH : Z_HIP;
			int32_t gun = i == VRH_HIP ? GUN_H : GUN_S;
			make_parked_copy(&r, gs, i);
			release(&r, 1 - gs, Z_NONE); /* a parked release: nothing */
			r.in.reloading = 0;
			step(&r);
			step(&r);
			CHECK_EQ(r.s.parked[1 - gs], 1);
			CHECK_OUT(&r, gun, 1, gs);
			CHECK_EQ(r.out.fx, 0);
			release(&r, gs, other);
			if (r.s.held[L] != -1 || r.s.held[R] != -1) {
				printf("  (gunside %d, holster %d)\n", gs, i);
			}
			CHECK_HELD(&r, -1, -1);
			CHECK_EQ(r.s.parked[L], 0);
			CHECK_EQ(r.s.parked[R], 0);
			CHECK_OUT(&r, 0, 0, 1 - gs);
			CHECK_HOLSTERS(&r, GUN_H, GUN_S);
			CHECK_EQ(r.out.fx, FXH(L, VRH_FX_HOLSTER) | FXH(R, VRH_FX_HOLSTER));
			CHECK_WEAPON(&r, 0, 0);
			press(&r, gs, zone);
			CHECK_EQ(r.s.held[gs], i);
			CHECK_EQ(r.s.held[1 - gs], -1);
			CHECK_EQ(r.s.parked[gs], 0);
			CHECK_OUT(&r, gun, 0, gs);
			CHECK_EQ(r.out.fx, FXH(gs, VRH_FX_DRAW));
			CHECK_HOLSTERS(&r, GUN_H, GUN_S);
		}
	}
}

/* Rule 31: a parked hand still squeezing (its press during the reload is ignored, rule 29) when
 * the gun side lets go during the reload goes home too; its own release later finds an empty
 * hand and does nothing. (After the reload a held grip has already unparked it, rule 9: see
 * test_unpark_grip_held_same_tick_gunside_release_is_rule_7.) */
static void test_parked_hand_with_grip_held_goes_home_with_gunside_release(void)
{
	struct rig r;
	make_parked_left(&r);
	press(&r, L, Z_NONE); /* the reload's press: still parked */
	CHECK_EQ(r.s.parked[L], 1);
	step(&r);
	CHECK_EQ(r.s.parked[L], 1);
	release(&r, R, Z_NONE);
	CHECK_HELD(&r, -1, -1);
	CHECK_EQ(r.s.parked[L], 0);
	CHECK_OUT(&r, 0, 0, L);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_HOLSTER) | FXH(R, VRH_FX_HOLSTER));
	release(&r, L, Z_NONE);
	CHECK_HELD(&r, -1, -1);
	CHECK_EQ(r.s.parked[L], 0);
	CHECK_OUT(&r, 0, 0, L);
	CHECK_EQ(r.out.fx, 0);
}

/* Rule 31, same tick, gunside R: L parks at the start of the tick (rule 33), its release does
 * nothing, then R lets go and L's copy goes home too */
static void test_same_tick_double_release_gunside_right(void)
{
	struct rig r;
	make_dual_hip(&r);
	r.in.reloading = 1;
	r.in.grip[L] = 0;
	r.in.grip[R] = 0;
	step(&r);
	CHECK_HELD(&r, -1, -1);
	CHECK_EQ(r.s.parked[L], 0);
	CHECK_EQ(r.s.parked[R], 0);
	CHECK_OUT(&r, 0, 0, L);
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_HOLSTER) | FXH(R, VRH_FX_HOLSTER));
	step(&r);
	CHECK_EQ(r.out.fx, 0);
	CHECK_OUT(&r, 0, 0, L);
}

/* Rule 31, same tick, gunside L: R parks at the start of the tick (rule 33); L lets go (rule 7,
 * the side moves to R) and R's copy, also letting go, goes home; R's release then finds an empty
 * hand */
static void test_same_tick_double_release_gunside_left(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	r.in.haspair[VRH_HIP] = 1;
	press(&r, L, Z_HIP);
	press(&r, R, Z_HIP);
	move(&r, L, Z_NONE);
	move(&r, R, Z_NONE);
	CHECK_OUT(&r, GUN_H, 1, L);
	r.in.reloading = 1;
	r.in.grip[L] = 0;
	r.in.grip[R] = 0;
	step(&r);
	CHECK_HELD(&r, -1, -1);
	CHECK_EQ(r.s.parked[L], 0);
	CHECK_EQ(r.s.parked[R], 0);
	CHECK_OUT(&r, 0, 0, R);
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_HOLSTER) | FXH(R, VRH_FX_HOLSTER));
}

/* Rule 31, same tick, L parked in an earlier tick and squeezed again during the reload (rule 29
 * ignores it), gunside R: both grips open in one tick, so both guns go home, one fx per hand. The
 * release tick runs with the reload still on, and again as the first tick with the reload ended:
 * L's grip is open in that tick, so rule 9 does not unpark it first. */
static void test_same_tick_double_release_parked_earlier_gunside_right(void)
{
	struct rig r;
	int i;
	for (i = 0; i < 2; i++) {
		make_parked_left(&r);
		press(&r, L, Z_NONE);
		CHECK_EQ(r.s.parked[L], 1);
		CHECK_EQ(r.out.fx, 0);
		step(&r);
		r.in.reloading = i == 0;
		r.in.grip[L] = 0;
		r.in.grip[R] = 0;
		step(&r);
		CHECK_HELD(&r, -1, -1);
		CHECK_EQ(r.s.parked[L], 0);
		CHECK_EQ(r.s.parked[R], 0);
		CHECK_OUT(&r, 0, 0, L);
		CHECK_HOLSTERS(&r, GUN_H, GUN_S);
		CHECK_EQ(r.out.fx, FXH(L, VRH_FX_HOLSTER) | FXH(R, VRH_FX_HOLSTER));
		step(&r);
		CHECK_EQ(r.out.fx, 0);
		CHECK_HELD(&r, -1, -1);
	}
}

/* The same, mirrored: gunside L, R parked earlier and squeezed again. L releases first (rule 24):
 * R's copy goes home with it, and R's own release then finds an empty hand. Again also in the
 * first tick with the reload ended. */
static void test_same_tick_double_release_parked_earlier_gunside_left(void)
{
	struct rig r;
	int i;
	for (i = 0; i < 2; i++) {
		setup(&r, GUN_H, GUN_S, R);
		r.in.haspair[VRH_HIP] = 1;
		press(&r, L, Z_HIP);
		press(&r, R, Z_HIP);
		move(&r, L, Z_NONE);
		move(&r, R, Z_NONE);
		CHECK_OUT(&r, GUN_H, 1, L);
		r.in.reloading = 1;
		release(&r, R, Z_NONE);
		CHECK_EQ(r.s.parked[R], 1);
		press(&r, R, Z_NONE);
		CHECK_EQ(r.s.parked[R], 1);
		CHECK_EQ(r.out.fx, 0);
		step(&r);
		r.in.reloading = i == 0;
		r.in.grip[L] = 0;
		r.in.grip[R] = 0;
		step(&r);
		CHECK_HELD(&r, -1, -1);
		CHECK_EQ(r.s.parked[L], 0);
		CHECK_EQ(r.s.parked[R], 0);
		CHECK_OUT(&r, 0, 0, R);
		CHECK_HOLSTERS(&r, GUN_H, GUN_S);
		CHECK_EQ(r.out.fx, FXH(L, VRH_FX_HOLSTER) | FXH(R, VRH_FX_HOLSTER));
		step(&r);
		CHECK_EQ(r.out.fx, 0);
		CHECK_HELD(&r, -1, -1);
	}
}

/* Rules 21 and 31: the parked hand is untracked when the gun side lets go during the reload. Rule
 * 31 needs no grip edge of that hand, so its copy goes home anyway: both hands empty, one holster
 * fx per hand. Its grip, opened while untracked, is a deferred release when tracking returns: it
 * finds an empty hand and does nothing. Last tracked with its grip open, the same also after the
 * reload (rule 9 leaves it parked). */
static void test_gunside_release_with_parked_hand_untracked_holsters_both(void)
{
	struct rig r;
	int i;
	make_parked_left(&r);
	press(&r, L, Z_NONE); /* ignored (rule 29), L's grip is held */
	r.in.tracked[L] = 0;
	r.in.grip[L] = 0;
	r.in.grip[R] = 0;
	step(&r);
	CHECK_HELD(&r, -1, -1);
	CHECK_EQ(r.s.parked[L], 0);
	CHECK_EQ(r.s.parked[R], 0);
	CHECK_OUT(&r, 0, 0, L);
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_HOLSTER) | FXH(R, VRH_FX_HOLSTER));
	step(&r);
	CHECK_EQ(r.out.fx, 0);
	r.in.tracked[L] = 1;
	step(&r);
	CHECK_HELD(&r, -1, -1);
	CHECK_EQ(r.s.parked[L], 0);
	CHECK_OUT(&r, 0, 0, L);
	CHECK_EQ(r.out.fx, 0);

	/* grip open when it went untracked: also home at once, reloading or not */
	for (i = 0; i < 2; i++) {
		make_parked_left(&r);
		r.in.tracked[L] = 0;
		r.in.grip[L] = 1; /* not read while untracked */
		r.in.reloading = i == 0;
		release(&r, R, Z_NONE);
		CHECK_HELD(&r, -1, -1);
		CHECK_EQ(r.s.parked[L], 0);
		CHECK_OUT(&r, 0, 0, L);
		CHECK_EQ(r.out.fx, FXH(L, VRH_FX_HOLSTER) | FXH(R, VRH_FX_HOLSTER));
	}
}

/* Rules 24, 29, 31 and 18, same tick, release and press: while reloading, the gun side lets go
 * and the parked hand squeezes in one tick. Releases run before presses (rule 24): rule 31 sends
 * both copies home and makes the parked hand the gun side, so rule 29 no longer blocks its press
 * (it is no longer the free hand, nor parked, rule 19). At a holster that works while reloading
 * it draws a single gun (rules 1 and 2), with HOLSTER and DRAW on that hand in one tick; at the
 * shoulder, no zone while reloading (rule 18), it draws nothing. Each side; the pair at the hip
 * or the shoulder; the press at the hip or the shoulder. */
static void test_gunside_release_and_parked_press_same_tick(void)
{
	struct rig r;
	int gs, i, p;
	for (gs = 0; gs < 2; gs++) {
		for (i = 0; i < 2; i++) {
			for (p = 0; p < 2; p++) {
				int32_t f = 1 - gs;
				int32_t pzone = p == VRH_HIP ? Z_HIP : Z_SH;
				int32_t draws = p == VRH_HIP;
				make_parked_copy(&r, gs, i);
				release(&r, f, Z_NONE); /* a parked release: nothing */
				CHECK_EQ(r.s.parked[f], 1);
				move(&r, f, pzone);
				CHECK_EQ(r.out.fx, 0);
				r.in.grip[gs] = 0;
				r.in.grip[f] = 1;
				step(&r);
				if (r.s.held[f] != (draws ? p : -1)) {
					printf("  (gunside %d, pair holster %d, press holster %d)\n", gs, i, p);
				}
				CHECK_EQ(r.s.held[gs], -1);
				CHECK_EQ(r.s.parked[L], 0);
				CHECK_EQ(r.s.parked[R], 0);
				CHECK_HOLSTERS(&r, GUN_H, GUN_S);
				if (draws) {
					CHECK_EQ(r.s.held[f], VRH_HIP);
					CHECK_OUT(&r, GUN_H, 0, f);
					CHECK_EQ(r.out.fx, FXH(gs, VRH_FX_HOLSTER) |
						FXH(f, VRH_FX_HOLSTER | VRH_FX_DRAW));
				} else {
					CHECK_EQ(r.s.held[f], -1);
					CHECK_OUT(&r, 0, 0, f);
					CHECK_EQ(r.out.fx, FXH(gs, VRH_FX_HOLSTER) | FXH(f, VRH_FX_HOLSTER));
				}
				step(&r);
				CHECK_EQ(r.out.fx, 0);
				CHECK_EQ(r.s.parked[f], 0);
			}
		}
	}
}

/* Rules 21 and 29: the free hand's press made while untracked arrives when tracking returns. Still
 * reloading: ignored, and its edge is used up. After the reload: an ordinary press (rule 4). */
static void test_free_hand_untracked_press_deferred_into_reload(void)
{
	struct rig r;
	int i;
	for (i = 0; i < 2; i++) {
		setup(&r, GUN_H, GUN_S, R);
		press(&r, R, Z_SH);
		move(&r, R, Z_NONE);
		move(&r, L, Z_HIP);
		r.in.reloading = 1;
		r.in.tracked[L] = 0;
		r.in.grip[L] = 1;
		step(&r);
		step(&r);
		CHECK_HELD(&r, -1, VRH_SHOULDER);
		CHECK_EQ(r.out.fx, 0);
		if (i == 1) {
			r.in.reloading = 0;
			step(&r);
			CHECK_EQ(r.out.fx, 0);
		}
		r.in.tracked[L] = 1;
		step(&r);
		if (i == 0) {
			CHECK_HELD(&r, -1, VRH_SHOULDER);
			CHECK_OUT(&r, GUN_S, 0, R);
			CHECK_EQ(r.out.fx, 0);
			/* the edge is used up: the reload ends with the grip still held, nothing */
			r.in.reloading = 0;
			step(&r);
			step(&r);
			CHECK_HELD(&r, -1, VRH_SHOULDER);
			CHECK_EQ(r.out.fx, 0);
		} else {
			CHECK_HELD(&r, VRH_HIP, -1);
			CHECK_OUT(&r, GUN_H, 0, L);
			CHECK_EQ(r.out.fx, FXH(L, VRH_FX_DRAW) | FXH(R, VRH_FX_HOLSTER));
		}
	}

	/* a parked free hand: the deferred press does not unpark during the reload (rules 19, 29) */
	make_parked_left(&r);
	r.in.tracked[L] = 0;
	r.in.grip[L] = 1;
	step(&r);
	r.in.tracked[L] = 1;
	step(&r);
	CHECK_EQ(r.s.parked[L], 1);
	CHECK_EQ(r.out.fx, 0);
}

/* Rules 11 and 31: a pause in the tick the gun side lets go is rule 11, not rule 7: both copies
 * go home as rule 31 would send them, but the side stays (rule 25) where rule 31 would move it */
static void test_pause_with_gunside_release_holsters_parked_copy(void)
{
	struct rig r;
	int i;
	for (i = 0; i < 2; i++) {
		make_parked_left(&r);
		r.in.reloading = i;
		r.in.paused = 1;
		r.in.grip[R] = 0;
		step(&r);
		CHECK_HELD(&r, -1, -1);
		CHECK_EQ(r.s.parked[L], 0);
		CHECK_EQ(r.s.parked[R], 0);
		CHECK_OUT(&r, 0, 0, R);
		CHECK_EQ(r.out.fx, FXH(L, VRH_FX_HOLSTER) | FXH(R, VRH_FX_HOLSTER));
	}
}

/* ---------- vrhFlipGunSide (rule 32) ---------- */

static void test_flip_gunside_both_hold(void)
{
	struct rig r;
	struct vrhstate before;
	make_dual_hip(&r); /* gunside R */
	before = r.s;
	CHECK_EQ(vrhFlipGunSide(&r.s), 1);
	before.gunside = L;
	CHECK(memcmp(&before, &r.s, sizeof before) == 0);
	step(&r);
	CHECK_OUT(&r, GUN_H, 1, L);
	CHECK_EQ(r.out.fx, 0);
	CHECK_EQ(vrhFlipGunSide(&r.s), 1);
	CHECK_EQ(r.s.gunside, R);
	step(&r);
	CHECK_OUT(&r, GUN_H, 1, R);
	CHECK_EQ(r.out.fx, 0);
	/* rule 7 follows the flipped side: L, the gun side after a flip, lets go and R takes the side */
	CHECK_EQ(vrhFlipGunSide(&r.s), 1);
	release(&r, L, Z_NONE);
	CHECK_HELD(&r, -1, VRH_HIP);
	CHECK_OUT(&r, GUN_H, 0, R);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_HOLSTER));
}

static void test_flip_gunside_refused_when_parked(void)
{
	struct rig r;
	struct vrhstate before;
	make_parked_left(&r); /* gunside R, L parked */
	before = r.s;
	CHECK_EQ(vrhFlipGunSide(&r.s), 0);
	CHECK(memcmp(&before, &r.s, sizeof before) == 0);
	step(&r);
	CHECK_OUT(&r, GUN_H, 1, R);
	/* after the reload, still parked: still refused */
	r.in.reloading = 0;
	step(&r);
	before = r.s;
	CHECK_EQ(vrhFlipGunSide(&r.s), 0);
	CHECK(memcmp(&before, &r.s, sizeof before) == 0);

	/* mirrored: gunside L, R parked */
	setup(&r, GUN_H, GUN_S, R);
	r.in.haspair[VRH_HIP] = 1;
	press(&r, L, Z_HIP);
	press(&r, R, Z_HIP);
	move(&r, L, Z_NONE);
	move(&r, R, Z_NONE);
	r.in.reloading = 1;
	release(&r, R, Z_NONE);
	CHECK_EQ(r.s.parked[R], 1);
	before = r.s;
	CHECK_EQ(vrhFlipGunSide(&r.s), 0);
	CHECK(memcmp(&before, &r.s, sizeof before) == 0);
	step(&r);
	CHECK_OUT(&r, GUN_H, 1, L);
}

static void test_flip_gunside_refused_one_hand_or_none(void)
{
	struct rig r;
	struct vrhstate before;
	setup(&r, GUN_H, GUN_S, R);
	before = r.s;
	CHECK_EQ(vrhFlipGunSide(&r.s), 0);
	CHECK(memcmp(&before, &r.s, sizeof before) == 0);
	press(&r, L, Z_HIP);
	before = r.s;
	CHECK_EQ(vrhFlipGunSide(&r.s), 0);
	CHECK(memcmp(&before, &r.s, sizeof before) == 0);
	step(&r);
	CHECK_OUT(&r, GUN_H, 0, L);
	release(&r, L, Z_NONE);
	press(&r, R, Z_SH);
	before = r.s;
	CHECK_EQ(vrhFlipGunSide(&r.s), 0);
	CHECK(memcmp(&before, &r.s, sizeof before) == 0);
	step(&r);
	CHECK_OUT(&r, GUN_S, 0, R);
	/* a built state: one hand holds, the side on the empty hand */
	r.s.gunside = L;
	before = r.s;
	CHECK_EQ(vrhFlipGunSide(&r.s), 0);
	CHECK(memcmp(&before, &r.s, sizeof before) == 0);
}

/* Rules 9, 19 and 32: after a flip the new free hand (R) parks on release during the reload, dual
 * stays 1, its press waits for the reload, and the first press after it unparks */
static void test_flip_then_free_right_hand_parks(void)
{
	struct rig r;
	make_dual_hip(&r);
	CHECK_EQ(vrhFlipGunSide(&r.s), 1); /* gunside L */
	r.in.reloading = 1;
	release(&r, R, Z_NONE);
	CHECK_EQ(r.s.parked[R], 1);
	CHECK_EQ(r.s.parked[L], 0);
	CHECK_HELD(&r, VRH_HIP, VRH_HIP);
	CHECK_OUT(&r, GUN_H, 1, L);
	CHECK_EQ(r.out.fx, 0);
	press(&r, R, Z_HIP);
	CHECK_EQ(r.s.parked[R], 1);
	CHECK_EQ(r.out.fx, 0);
	release(&r, R, Z_HIP);
	CHECK_EQ(r.s.parked[R], 1);
	CHECK_EQ(r.out.fx, 0);
	r.in.reloading = 0;
	press(&r, R, Z_NONE);
	CHECK_EQ(r.s.parked[R], 0);
	CHECK_HELD(&r, VRH_HIP, VRH_HIP);
	CHECK_OUT(&r, GUN_H, 1, L);
	CHECK_EQ(r.out.fx, FXH(R, VRH_FX_DRAW));
}

/* ---------- pause (rules 11, 25) ---------- */

static void test_pause_holsters_single_gun_and_keeps_gunside(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	press(&r, L, Z_HIP);
	move(&r, L, Z_SH); /* a pause is a release with no zone: no swap */
	r.in.paused = 1;
	step(&r);
	CHECK_HELD(&r, -1, -1);
	CHECK_OUT(&r, 0, 0, L);
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	CHECK(VRH_FX_HAS(r.out.fx, L, VRH_FX_HOLSTER));
	CHECK(!VRH_FX_HAS(r.out.fx, L, VRH_FX_SWAP));
	/* once */
	step(&r);
	CHECK_EQ(r.out.fx, 0);
	CHECK_OUT(&r, 0, 0, L);
}

static void test_pause_holsters_dual_and_keeps_gunside(void)
{
	struct rig r;
	make_dual_hip(&r);
	r.in.paused = 1;
	step(&r);
	CHECK_HELD(&r, -1, -1);
	CHECK_OUT(&r, 0, 0, R);
	CHECK(VRH_FX_HAS(r.out.fx, L, VRH_FX_HOLSTER));
	CHECK(VRH_FX_HAS(r.out.fx, R, VRH_FX_HOLSTER));
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
}

static void test_pause_holsters_parked_gun_once(void)
{
	struct rig r;
	make_parked_left(&r);
	r.in.paused = 1;
	step(&r);
	CHECK_HELD(&r, -1, -1);
	CHECK_EQ(r.s.parked[L], 0);
	CHECK_EQ(r.s.parked[R], 0);
	CHECK_OUT(&r, 0, 0, R);
	CHECK(VRH_FX_HAS(r.out.fx, L, VRH_FX_HOLSTER));
	CHECK(VRH_FX_HAS(r.out.fx, R, VRH_FX_HOLSTER));
	step(&r);
	CHECK_EQ(r.out.fx, 0);
	/* after the pause a fresh squeeze at no zone draws nothing (no longer parked) */
	r.in.paused = 0;
	r.in.reloading = 0;
	step(&r);
	press(&r, L, Z_NONE);
	CHECK_HELD(&r, -1, -1);
	CHECK_OUT(&r, 0, 0, R);
}

static void test_pause_grip_held_after_pause_is_not_a_press(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	press(&r, R, Z_HIP);
	r.in.paused = 1;
	step(&r);
	CHECK_EQ(r.s.held[R], -1);
	step(&r);
	r.in.paused = 0;
	step(&r);
	step(&r);
	CHECK_EQ(r.s.held[R], -1);
	CHECK_OUT(&r, 0, 0, R);
	CHECK_EQ(r.out.fx, 0);
	release(&r, R, Z_HIP);
	CHECK_EQ(r.out.fx, 0);
	press(&r, R, Z_HIP);
	CHECK_EQ(r.s.held[R], VRH_HIP);
	CHECK_OUT(&r, GUN_H, 0, R);
}

static void test_pause_press_during_pause_does_not_draw(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	move(&r, L, Z_HIP);
	r.in.paused = 1;
	step(&r);
	r.in.grip[L] = 1;
	step(&r);
	CHECK_EQ(r.s.held[L], -1);
	CHECK_OUT(&r, 0, 0, R);
	CHECK(!VRH_FX_HAS(r.out.fx, L, VRH_FX_DRAW));
	/* still gripping after the pause: not a new press */
	r.in.paused = 0;
	step(&r);
	CHECK_EQ(r.s.held[L], -1);
	CHECK_OUT(&r, 0, 0, R);
	CHECK(!VRH_FX_HAS(r.out.fx, L, VRH_FX_DRAW));
}

static void test_pause_zone_entered_during_pause_does_not_buzz_after(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	r.in.paused = 1;
	step(&r);
	move(&r, R, Z_HIP);
	step(&r);
	r.in.paused = 0;
	step(&r);
	CHECK(!VRH_FX_HAS(r.out.fx, R, VRH_FX_ZONE));
}

/* ---------- untracked hand (rules 11, 21, 25) ---------- */

static void test_untracked_release_deferred_with_zone_of_return_tick(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, L);
	press(&r, R, Z_HIP);
	r.in.tracked[R] = 0;
	r.in.grip[R] = 0;
	r.in.zone[R] = Z_NONE;
	step(&r);
	step(&r);
	CHECK_EQ(r.s.held[R], VRH_HIP);
	CHECK_OUT(&r, GUN_H, 0, R);
	CHECK_EQ(r.out.fx, 0);
	/* tracking returns with the hand at the shoulder: the release swaps there */
	r.in.tracked[R] = 1;
	r.in.zone[R] = Z_SH;
	step(&r);
	CHECK_EQ(r.s.held[R], -1);
	CHECK_HOLSTERS(&r, GUN_S, GUN_H);
	CHECK(VRH_FX_HAS(r.out.fx, R, VRH_FX_SWAP));
}

static void test_untracked_press_deferred(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, L);
	r.in.tracked[R] = 0;
	r.in.grip[R] = 1;
	r.in.zone[R] = Z_HIP;
	step(&r);
	CHECK_EQ(r.s.held[R], -1);
	CHECK_OUT(&r, 0, 0, L);
	CHECK_EQ(r.out.fx, 0);
	r.in.tracked[R] = 1;
	step(&r);
	CHECK_EQ(r.s.held[R], VRH_HIP);
	CHECK_OUT(&r, GUN_H, 0, R);
	CHECK(VRH_FX_HAS(r.out.fx, R, VRH_FX_DRAW));
}

static void test_untracked_press_and_release_unseen(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, L);
	move(&r, R, Z_HIP);
	r.in.tracked[R] = 0;
	r.in.grip[R] = 1;
	step(&r);
	CHECK_EQ(r.s.held[R], -1);
	CHECK_OUT(&r, 0, 0, L);
	CHECK_EQ(r.out.fx, 0);
	r.in.grip[R] = 0;
	step(&r);
	CHECK_EQ(r.s.held[R], -1);
	CHECK_OUT(&r, 0, 0, L);
	CHECK_EQ(r.out.fx, 0);
	r.in.tracked[R] = 1;
	step(&r);
	CHECK_EQ(r.s.held[R], -1);
	CHECK_EQ(r.out.fx, 0);
}

/* Rules 11, 21, 25: an untracked hand's gun still goes home on a pause, and its grip stays
 * frozen at the value from before tracking was lost */
static void test_untracked_hand_during_pause(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, L);
	press(&r, R, Z_HIP); /* R grips: prevgrip 1 */
	r.in.tracked[R] = 0;
	r.in.grip[R] = 0; /* not read while untracked */
	r.in.paused = 1;
	step(&r);
	CHECK_HELD(&r, -1, -1);
	CHECK_OUT(&r, 0, 0, R);
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	CHECK(VRH_FX_HAS(r.out.fx, R, VRH_FX_HOLSTER));
	CHECK_EQ(r.s.prevgrip[R], 1);
	step(&r);
	CHECK_EQ(r.out.fx, 0);
	CHECK_EQ(r.s.prevgrip[R], 1);
	r.in.paused = 0;
	step(&r);
	CHECK_EQ(r.s.held[R], -1);
	CHECK_EQ(r.s.prevgrip[R], 1);
	/* tracking returns gripping at the hip: judged against the frozen 1, not a press */
	r.in.tracked[R] = 1;
	r.in.grip[R] = 1;
	step(&r);
	CHECK_EQ(r.s.held[R], -1);
	CHECK_OUT(&r, 0, 0, R);
	CHECK(!VRH_FX_HAS(r.out.fx, R, VRH_FX_DRAW));
	/* let go and squeeze again: draws */
	release(&r, R, Z_HIP);
	press(&r, R, Z_HIP);
	CHECK_EQ(r.s.held[R], VRH_HIP);
	CHECK_OUT(&r, GUN_H, 0, R);
}

/* Rule 25: an untracked hand with a released grip keeps prevgrip 0 through a pause */
static void test_untracked_released_grip_frozen_through_pause(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, L);
	move(&r, R, Z_HIP);
	r.in.tracked[R] = 0;
	r.in.grip[R] = 1; /* not read while untracked */
	r.in.paused = 1;
	step(&r);
	CHECK_EQ(r.s.prevgrip[R], 0);
	step(&r);
	CHECK_EQ(r.s.prevgrip[R], 0);
	CHECK_EQ(r.s.held[R], -1);
}

static void test_untracked_hand_does_not_block_other_hand(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	r.in.tracked[R] = 0;
	press(&r, L, Z_SH);
	CHECK_EQ(r.s.held[L], VRH_SHOULDER);
	CHECK_OUT(&r, GUN_S, 0, L);
}

static void test_untracked_zone_frozen_then_buzz_on_return(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	r.in.tracked[R] = 0;
	move(&r, R, Z_HIP);
	CHECK(!VRH_FX_HAS(r.out.fx, R, VRH_FX_ZONE));
	r.in.tracked[R] = 1;
	step(&r);
	CHECK(VRH_FX_HAS(r.out.fx, R, VRH_FX_ZONE));
}

/* ---------- zone buzz (rules 14, 18) ---------- */

static void test_zone_buzz_empty_hand_at_gun_edge_only(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	move(&r, L, Z_HIP);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_ZONE));
	step(&r);
	CHECK_EQ(r.out.fx, 0);
	step(&r);
	CHECK_EQ(r.out.fx, 0);
	move(&r, L, Z_NONE);
	CHECK_EQ(r.out.fx, 0);
	move(&r, L, Z_HIP);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_ZONE));
	/* straight from one holster zone to the other is an entry too */
	move(&r, L, Z_SH);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_ZONE));
	/* the right hand gets its own bit */
	move(&r, R, Z_SH);
	CHECK_EQ(r.out.fx, FXH(R, VRH_FX_ZONE));
}

static void test_zone_buzz_not_for_empty_holster_or_chest(void)
{
	struct rig r;
	setup(&r, GUN_H, 0, R);
	move(&r, R, Z_SH);
	CHECK_EQ(r.out.fx, 0);
	/* the chest is the pouch's zone (rule 36), but the pouch is empty: no buzz (rule 44) */
	move(&r, R, Z_CHEST);
	CHECK_EQ(r.out.fx, 0);
	move(&r, R, Z_RHIP);
	CHECK_EQ(r.out.fx, 0);
	move(&r, R, Z_HIP);
	CHECK_EQ(r.out.fx, FXH(R, VRH_FX_ZONE));
}

static void test_zone_buzz_pair_rules(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	press(&r, R, Z_HIP);
	move(&r, R, Z_NONE);
	/* no pair: the other hand holds the hip gun, so no buzz there */
	move(&r, L, Z_HIP);
	CHECK_EQ(FXOF(r.out.fx, L), 0);
	move(&r, L, Z_NONE);
	/* the shoulder gun can be taken (rule 4): buzz */
	move(&r, L, Z_SH);
	CHECK_EQ(FXOF(r.out.fx, L), VRH_FX_ZONE);
	move(&r, L, Z_NONE);
	/* with a pair the hip buzzes */
	r.in.haspair[VRH_HIP] = 1;
	move(&r, L, Z_HIP);
	CHECK_EQ(FXOF(r.out.fx, L), VRH_FX_ZONE);
}

static void test_zone_buzz_single_gun_at_other_holster(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	press(&r, R, Z_HIP);
	move(&r, R, Z_NONE);
	/* own holster: a release there does not swap, no buzz */
	move(&r, R, Z_HIP);
	CHECK_EQ(r.out.fx, 0);
	move(&r, R, Z_NONE);
	move(&r, R, Z_SH);
	CHECK_EQ(r.out.fx, FXH(R, VRH_FX_ZONE));
}

static void test_zone_buzz_single_gun_at_empty_other_holster(void)
{
	struct rig r;
	setup(&r, GUN_H, 0, R);
	press(&r, R, Z_HIP);
	move(&r, R, Z_NONE);
	move(&r, R, Z_SH);
	CHECK_EQ(r.out.fx, FXH(R, VRH_FX_ZONE));
}

static void test_zone_buzz_not_for_dual_hand(void)
{
	struct rig r;
	make_dual_hip(&r);
	move(&r, R, Z_SH);
	CHECK_EQ(r.out.fx, 0);
	move(&r, L, Z_SH);
	CHECK_EQ(r.out.fx, 0);
}

static void test_zone_buzz_not_for_parked_hand(void)
{
	struct rig r;
	make_parked_left(&r);
	move(&r, L, Z_HIP);
	CHECK_EQ(r.out.fx, 0);
	move(&r, L, Z_NONE);
	r.in.reloading = 0;
	step(&r);
	move(&r, L, Z_SH);
	CHECK_EQ(r.out.fx, 0);
}

/* Rules 14 and 31: after the gun side's release sends both copies home, neither hand is parked:
 * each empty hand buzzes on entering a holster zone with a gun to take */
static void test_zone_buzz_after_gunside_release_holsters_both(void)
{
	struct rig r;
	make_parked_left_after_reload(&r);
	release(&r, R, Z_NONE);
	CHECK_HELD(&r, -1, -1);
	CHECK_EQ(r.s.parked[L], 0);
	move(&r, L, Z_HIP);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_ZONE));
	move(&r, R, Z_SH);
	CHECK_EQ(r.out.fx, FXH(R, VRH_FX_ZONE));
}

static void test_zone_buzz_reloading_ignores_shoulder(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	press(&r, R, Z_HIP);
	move(&r, R, Z_NONE);
	r.in.reloading = 1;
	/* a single gun at the other holster would swap, but the shoulder is the reload's */
	move(&r, R, Z_SH);
	CHECK_EQ(r.out.fx, 0);
	move(&r, R, Z_NONE);
	/* an empty hand at the shoulder gun: no buzz either */
	move(&r, L, Z_SH);
	CHECK_EQ(r.out.fx, 0);
	move(&r, L, Z_NONE);
	/* L is the free hand: no hip buzz while reloading either (rule 30), once it ends there is */
	r.in.haspair[VRH_HIP] = 1;
	move(&r, L, Z_HIP);
	CHECK_EQ(r.out.fx, 0);
	move(&r, L, Z_NONE);
	r.in.reloading = 0;
	move(&r, L, Z_HIP);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_ZONE));
}

/* Rule 30: while reloading the free hand gets no buzz at a gun it could otherwise take */
static void test_zone_buzz_free_hand_none_while_reloading(void)
{
	struct rig r;
	/* pair at the hip, gunside R */
	setup(&r, GUN_H, GUN_S, R);
	r.in.haspair[VRH_HIP] = 1;
	press(&r, R, Z_HIP);
	move(&r, R, Z_NONE);
	r.in.reloading = 1;
	move(&r, L, Z_HIP);
	CHECK_EQ(r.out.fx, 0);
	move(&r, L, Z_NONE);
	r.in.reloading = 0;
	step(&r);
	move(&r, L, Z_HIP);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_ZONE));

	/* gunside L holds the shoulder gun, R empty at the hip (rule 4 would draw), both empty */
	setup(&r, GUN_H, GUN_S, L);
	press(&r, L, Z_SH);
	move(&r, L, Z_NONE);
	r.in.reloading = 1;
	move(&r, R, Z_HIP);
	CHECK_EQ(r.out.fx, 0);
	setup(&r, GUN_H, GUN_S, L);
	r.in.reloading = 1;
	move(&r, R, Z_HIP);
	CHECK_EQ(r.out.fx, 0);
}

/* Rule 30 is for the free hand only: the gun side's hand still buzzes while reloading */
static void test_zone_buzz_gunside_hand_while_reloading(void)
{
	struct rig r;
	/* empty gun side hand at the hip gun */
	setup(&r, GUN_H, GUN_S, R);
	r.in.reloading = 1;
	move(&r, R, Z_HIP);
	CHECK_EQ(r.out.fx, FXH(R, VRH_FX_ZONE));
	/* the gun side holding the shoulder gun at the hip: a release there swaps */
	setup(&r, GUN_H, GUN_S, L);
	press(&r, L, Z_SH);
	move(&r, L, Z_NONE);
	r.in.reloading = 1;
	move(&r, L, Z_HIP);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_ZONE));
}

static void test_zone_buzz_same_tick_as_press(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	r.in.zone[L] = Z_HIP;
	r.in.grip[L] = 1;
	step(&r);
	CHECK_EQ(r.s.held[L], VRH_HIP);
	CHECK(VRH_FX_HAS(r.out.fx, L, VRH_FX_ZONE));
	CHECK(VRH_FX_HAS(r.out.fx, L, VRH_FX_DRAW));
	CHECK_EQ(FXOF(r.out.fx, R), 0);
}

/* ---------- fx encoding (rule 15) ---------- */

static void test_fx_encoding_macros(void)
{
	int32_t fx = FXH(R, VRH_FX_SWAP) | FXH(L, VRH_FX_DRAW);
	CHECK_EQ(VRH_FX_DRAW, 0x1);
	CHECK_EQ(VRH_FX_HOLSTER, 0x2);
	CHECK_EQ(VRH_FX_SWAP, 0x4);
	CHECK_EQ(VRH_FX_EMPTY, 0x8);
	CHECK_EQ(VRH_FX_ZONE, 0x10);
	CHECK_EQ(VRH_FX_SHIFT(L), 0);
	CHECK_EQ(VRH_FX_SHIFT(R), 8);
	CHECK(VRH_FX_HAS(fx, L, VRH_FX_DRAW));
	CHECK(!VRH_FX_HAS(fx, R, VRH_FX_DRAW));
	CHECK(VRH_FX_HAS(fx, R, VRH_FX_SWAP));
	CHECK(!VRH_FX_HAS(fx, L, VRH_FX_SWAP));
}

static void test_fx_per_hand_no_cross_talk(void)
{
	struct rig r;
	setup(&r, GUN_H, 0, R);
	press(&r, R, Z_HIP);
	CHECK(VRH_FX_HAS(r.out.fx, R, VRH_FX_DRAW));
	CHECK(!VRH_FX_HAS(r.out.fx, L, VRH_FX_DRAW));
	CHECK_EQ(r.out.fx, 0x100);
	release(&r, R, Z_NONE);
	CHECK(VRH_FX_HAS(r.out.fx, R, VRH_FX_HOLSTER));
	CHECK(!VRH_FX_HAS(r.out.fx, L, VRH_FX_HOLSTER));
	CHECK_EQ(r.out.fx, 0x200);
	press(&r, L, Z_SH);
	CHECK(VRH_FX_HAS(r.out.fx, L, VRH_FX_EMPTY));
	CHECK(!VRH_FX_HAS(r.out.fx, R, VRH_FX_EMPTY));
	CHECK_EQ(r.out.fx, 0x08);
	release(&r, L, Z_SH);
	CHECK_EQ(r.out.fx, 0);
	press(&r, L, Z_HIP);
	CHECK_EQ(r.out.fx, 0x01);
	release(&r, L, Z_SH);
	CHECK(VRH_FX_HAS(r.out.fx, L, VRH_FX_SWAP));
	CHECK(!VRH_FX_HAS(r.out.fx, R, VRH_FX_SWAP));
	CHECK_EQ(r.out.fx, 0x04);
}

/* ---------- tick order (rule 24) ---------- */

static void test_order_release_before_press(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	press(&r, R, Z_HIP);
	move(&r, L, Z_HIP);
	/* R puts the only hip gun back and L draws it in the same tick */
	r.in.grip[R] = 0;
	r.in.grip[L] = 1;
	step(&r);
	CHECK_HELD(&r, VRH_HIP, -1);
	CHECK_OUT(&r, GUN_H, 0, L);
	CHECK(VRH_FX_HAS(r.out.fx, R, VRH_FX_HOLSTER));
	CHECK(VRH_FX_HAS(r.out.fx, L, VRH_FX_DRAW));
	CHECK(!VRH_FX_HAS(r.out.fx, L, VRH_FX_EMPTY));
}

static void test_order_presses_left_first(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	move(&r, L, Z_HIP);
	move(&r, R, Z_SH);
	r.in.grip[L] = 1;
	r.in.grip[R] = 1;
	step(&r);
	/* L draws the hip gun, then R's shoulder draw sends it back (rule 4) */
	CHECK_HELD(&r, -1, VRH_SHOULDER);
	CHECK_OUT(&r, GUN_S, 0, R);
	CHECK(VRH_FX_HAS(r.out.fx, R, VRH_FX_DRAW));
	CHECK(VRH_FX_HAS(r.out.fx, L, VRH_FX_HOLSTER));
}

static void test_order_both_press_same_holster_no_pair(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	move(&r, L, Z_HIP);
	move(&r, R, Z_HIP);
	r.in.grip[L] = 1;
	r.in.grip[R] = 1;
	step(&r);
	CHECK_HELD(&r, VRH_HIP, -1);
	CHECK_OUT(&r, GUN_H, 0, L);
	CHECK(VRH_FX_HAS(r.out.fx, L, VRH_FX_DRAW));
	CHECK(VRH_FX_HAS(r.out.fx, R, VRH_FX_EMPTY));
}

static void test_order_releases_left_first(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	r.in.haspair[VRH_HIP] = 1;
	press(&r, L, Z_HIP);
	press(&r, R, Z_HIP);
	move(&r, L, Z_NONE);
	move(&r, R, Z_NONE);
	CHECK_OUT(&r, GUN_H, 1, L);
	r.in.grip[L] = 0;
	r.in.grip[R] = 0;
	step(&r);
	/* L (the gun side) lets go first, the side moves to R, then R lets go */
	CHECK_HELD(&r, -1, -1);
	CHECK_OUT(&r, 0, 0, R);
	CHECK(VRH_FX_HAS(r.out.fx, L, VRH_FX_HOLSTER));
	CHECK(VRH_FX_HAS(r.out.fx, R, VRH_FX_HOLSTER));
}

static void test_order_zone_buzz_uses_start_of_tick_state(void)
{
	struct rig r;
	/* L enters the hip zone while R draws the only hip gun in the same tick: at the start of
	 * the tick the gun could be taken, so L buzzes */
	setup(&r, GUN_H, GUN_S, R);
	move(&r, R, Z_HIP);
	r.in.zone[L] = Z_HIP;
	r.in.grip[R] = 1;
	step(&r);
	CHECK_EQ(r.s.held[R], VRH_HIP);
	CHECK(VRH_FX_HAS(r.out.fx, L, VRH_FX_ZONE));

	/* R puts the hip gun back while L enters the hip zone: at the start of the tick R held it
	 * with no pair, so no buzz */
	setup(&r, GUN_H, GUN_S, R);
	press(&r, R, Z_HIP);
	r.in.grip[R] = 0;
	r.in.zone[L] = Z_HIP;
	step(&r);
	CHECK_EQ(r.s.held[R], -1);
	CHECK(!VRH_FX_HAS(r.out.fx, L, VRH_FX_ZONE));
}

/* ---------- vrhOnPickup (rules 12, 22) ---------- */

static void test_pickup_fills_hip_then_shoulder(void)
{
	struct rig r;
	setup(&r, 0, 0, R);
	CHECK_EQ(vrhOnPickup(&r.s, GUN_H), VRH_HIP);
	CHECK_HOLSTERS(&r, GUN_H, 0);
	CHECK_EQ(vrhOnPickup(&r.s, GUN_S), VRH_SHOULDER);
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	CHECK_EQ(vrhOnPickup(&r.s, GUN_X), VRH_GUN3);
	CHECK_EQ(vrhOnPickup(&r.s, GUN_4), -1);
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
}

static void test_pickup_shoulder_when_hip_full(void)
{
	struct rig r;
	setup(&r, GUN_H, 0, R);
	CHECK_EQ(vrhOnPickup(&r.s, GUN_S), VRH_SHOULDER);
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	setup(&r, 0, GUN_S, R);
	CHECK_EQ(vrhOnPickup(&r.s, GUN_H), VRH_HIP);
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
}

static void test_pickup_weapon_zero(void)
{
	struct rig r;
	setup(&r, 0, 0, R);
	CHECK_EQ(vrhOnPickup(&r.s, 0), -1);
	CHECK_HOLSTERS(&r, 0, 0);
}

static void test_pickup_already_holstered(void)
{
	struct rig r;
	struct vrhstate before;
	setup(&r, 0, GUN_S, R);
	before = r.s;
	CHECK_EQ(vrhOnPickup(&r.s, GUN_S), VRH_SHOULDER);
	CHECK(memcmp(&before, &r.s, sizeof before) == 0);
	CHECK_HOLSTERS(&r, 0, GUN_S);

	setup(&r, GUN_H, 0, R);
	before = r.s;
	CHECK_EQ(vrhOnPickup(&r.s, GUN_H), VRH_HIP);
	CHECK(memcmp(&before, &r.s, sizeof before) == 0);
}

static void test_pickup_drawn_gun_keeps_its_holster(void)
{
	struct rig r;
	struct vrhstate before;
	setup(&r, GUN_H, 0, R);
	press(&r, R, Z_HIP);
	/* rule 16: the hip is not free while its gun is drawn */
	before = r.s;
	CHECK_EQ(vrhOnPickup(&r.s, GUN_H), VRH_HIP);
	CHECK(memcmp(&before, &r.s, sizeof before) == 0);
	CHECK_EQ(vrhOnPickup(&r.s, GUN_S), VRH_SHOULDER);
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	CHECK_EQ(r.s.held[R], VRH_HIP);
}

/* ---------- vrhOnMenuPick (rules 13, 23) ---------- */

static void test_menupick_zero_changes_nothing(void)
{
	struct rig r;
	struct vrhstate before;
	setup(&r, GUN_H, GUN_S, R);
	before = r.s;
	vrhOnMenuPick(&r.s, 0);
	CHECK(memcmp(&before, &r.s, sizeof before) == 0);
	press(&r, R, Z_HIP);
	before = r.s;
	vrhOnMenuPick(&r.s, 0);
	CHECK(memcmp(&before, &r.s, sizeof before) == 0);
}

static void test_menupick_held_gun_changes_nothing(void)
{
	struct rig r;
	struct vrhstate before;
	setup(&r, GUN_H, GUN_S, R);
	press(&r, L, Z_SH);
	before = r.s;
	vrhOnMenuPick(&r.s, GUN_S);
	CHECK(memcmp(&before, &r.s, sizeof before) == 0);

	make_dual_hip(&r);
	before = r.s;
	vrhOnMenuPick(&r.s, GUN_H);
	CHECK(memcmp(&before, &r.s, sizeof before) == 0);
}

static void test_menupick_gun_in_other_holster_swaps(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	press(&r, R, Z_HIP);
	vrhOnMenuPick(&r.s, GUN_S);
	CHECK_HOLSTERS(&r, GUN_S, GUN_H);
	CHECK_HELD(&r, -1, VRH_HIP);
	step(&r);
	CHECK_OUT(&r, GUN_S, 0, R);
	/* letting go puts the picked gun in the hip */
	release(&r, R, Z_NONE);
	CHECK_HOLSTERS(&r, GUN_S, GUN_H);
	CHECK(VRH_FX_HAS(r.out.fx, R, VRH_FX_HOLSTER));
}

static void test_menupick_shoulder_held_hip_gun_swaps(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	press(&r, L, Z_SH);
	vrhOnMenuPick(&r.s, GUN_H);
	CHECK_HOLSTERS(&r, GUN_S, GUN_H);
	CHECK_HELD(&r, VRH_SHOULDER, -1);
	step(&r);
	CHECK_OUT(&r, GUN_H, 0, L);
}

static void test_menupick_new_gun_replaces_source_holster(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	press(&r, R, Z_SH);
	vrhOnMenuPick(&r.s, GUN_X);
	CHECK_HOLSTERS(&r, GUN_H, GUN_X);
	CHECK_HELD(&r, -1, VRH_SHOULDER);
	step(&r);
	CHECK_OUT(&r, GUN_X, 0, R);
}

static void test_menupick_dual_drops_non_gunside_hand(void)
{
	struct rig r;
	make_dual_hip(&r); /* gunside R */
	vrhOnMenuPick(&r.s, GUN_X);
	CHECK_HOLSTERS(&r, GUN_X, GUN_S);
	CHECK_HELD(&r, -1, VRH_HIP);
	step(&r);
	CHECK_OUT(&r, GUN_X, 0, R);
	/* L still grips with an empty hand: letting go does nothing */
	release(&r, L, Z_NONE);
	CHECK_EQ(r.out.fx, 0);
	CHECK_HELD(&r, -1, VRH_HIP);
}

static void test_menupick_dual_drops_non_gunside_hand_left_side(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	r.in.haspair[VRH_SHOULDER] = 1;
	press(&r, L, Z_SH);
	press(&r, R, Z_SH);
	CHECK_OUT(&r, GUN_S, 1, L);
	vrhOnMenuPick(&r.s, GUN_H); /* the other holster's gun: swap */
	CHECK_HOLSTERS(&r, GUN_S, GUN_H);
	CHECK_HELD(&r, VRH_SHOULDER, -1);
	step(&r);
	CHECK_OUT(&r, GUN_H, 0, L);
}

static void test_menupick_parked_counts_as_held(void)
{
	struct rig r;
	make_parked_left(&r); /* R gunside, L parked */
	vrhOnMenuPick(&r.s, GUN_X);
	CHECK_HOLSTERS(&r, GUN_X, GUN_S);
	CHECK_HELD(&r, -1, VRH_HIP);
	r.in.reloading = 0;
	step(&r);
	CHECK_OUT(&r, GUN_X, 0, R);
}

static void test_menupick_empty_hands_fills_hip(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, L);
	vrhOnMenuPick(&r.s, GUN_X);
	CHECK_HOLSTERS(&r, GUN_X, GUN_S);
	CHECK_HELD(&r, -1, -1);
	CHECK_EQ(r.s.gunside, L);
	step(&r);
	CHECK_OUT(&r, 0, 0, L);
	press(&r, R, Z_HIP);
	CHECK_OUT(&r, GUN_X, 0, R);
}

static void test_menupick_empty_hands_already_holstered(void)
{
	struct rig r;
	struct vrhstate before;
	setup(&r, GUN_H, GUN_S, R);
	before = r.s;
	vrhOnMenuPick(&r.s, GUN_S);
	CHECK(memcmp(&before, &r.s, sizeof before) == 0);
	vrhOnMenuPick(&r.s, GUN_H);
	CHECK(memcmp(&before, &r.s, sizeof before) == 0);
}

static void test_menupick_empty_hands_empty_hip(void)
{
	struct rig r;
	setup(&r, 0, GUN_S, R);
	vrhOnMenuPick(&r.s, GUN_X);
	CHECK_HOLSTERS(&r, GUN_X, GUN_S);
}

/* ---------- vrhWeapon (rule 27) ---------- */

static void test_weapon_empty_hands(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	CHECK_WEAPON(&r, 0, 0);
	setup(&r, 0, 0, L);
	CHECK_WEAPON(&r, 0, 0);
}

static void test_weapon_single_and_dual(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	press(&r, L, Z_SH);
	CHECK_WEAPON(&r, GUN_S, 0);
	release(&r, L, Z_NONE);
	press(&r, R, Z_HIP);
	CHECK_WEAPON(&r, GUN_H, 0);
	make_dual_hip(&r);
	CHECK_WEAPON(&r, GUN_H, 1);
}

static void test_weapon_parked_counts_as_holding(void)
{
	struct rig r;
	make_parked_left(&r);
	CHECK_WEAPON(&r, GUN_H, 1);
	r.in.reloading = 0;
	step(&r);
	CHECK_EQ(r.s.parked[L], 1);
	CHECK_WEAPON(&r, GUN_H, 1);
	/* the gun side lets go after the reload: both copies go home (rule 31), fists */
	release(&r, R, Z_NONE);
	CHECK_HELD(&r, -1, -1);
	CHECK_WEAPON(&r, 0, 0);
	/* rule 42 on a built state: a parked hand alone that is not the gun side is not in the
	 * engine (fists; a tick reaches this when an item goes home, rule 41) */
	r.s.held[L] = VRH_HIP;
	r.s.parked[L] = 1;
	r.s.gunside = R;
	CHECK_WEAPON(&r, 0, 0);
	/* the same hand as the gun side: the gun side holds, so its gun is the weapon */
	r.s.gunside = L;
	CHECK_WEAPON(&r, GUN_H, 0);
}

static void test_weapon_formula_on_built_states(void)
{
	struct rig r;
	int32_t w, d;
	setup(&r, GUN_H, GUN_S, R);
	/* rule 42: the gun side's slot wins when both hold; two different slots are not a pair */
	r.s.held[L] = VRH_SHOULDER;
	r.s.held[R] = VRH_HIP;
	vrhWeapon(&r.s, &w, &d);
	CHECK_EQ(w, GUN_H);
	CHECK_EQ(d, 0);
	r.s.held[L] = VRH_HIP;
	r.s.held[R] = VRH_SHOULDER;
	vrhWeapon(&r.s, &w, &d);
	CHECK_EQ(w, GUN_S);
	CHECK_EQ(d, 0);
	r.s.gunside = L;
	vrhWeapon(&r.s, &w, &d);
	CHECK_EQ(w, GUN_H);
	CHECK_EQ(d, 0);
	/* the same slot in both hands is a pair */
	r.s.held[R] = VRH_HIP;
	vrhWeapon(&r.s, &w, &d);
	CHECK_EQ(w, GUN_H);
	CHECK_EQ(d, 1);
	r.s.gunside = R;
	/* right hand only */
	r.s.held[L] = -1;
	r.s.held[R] = VRH_SHOULDER;
	vrhWeapon(&r.s, &w, &d);
	CHECK_EQ(w, GUN_S);
	CHECK_EQ(d, 0);
	/* left hand only */
	r.s.held[L] = VRH_SHOULDER;
	r.s.held[R] = -1;
	vrhWeapon(&r.s, &w, &d);
	CHECK_EQ(w, GUN_S);
	CHECK_EQ(d, 0);
	/* a hand whose holster has since become 0 gives weapon 0 */
	r.s.holster[VRH_SHOULDER] = 0;
	vrhWeapon(&r.s, &w, &d);
	CHECK_EQ(w, 0);
	CHECK_EQ(d, 0);
	r.s.held[L] = -1;
	r.s.held[R] = VRH_SHOULDER;
	vrhWeapon(&r.s, &w, &d);
	CHECK_EQ(w, 0);
	CHECK_EQ(d, 0);
}

static void test_weapon_after_menu_pick(void)
{
	struct rig r;
	/* gun held, new gun */
	setup(&r, GUN_H, GUN_S, R);
	press(&r, R, Z_SH);
	vrhOnMenuPick(&r.s, GUN_X);
	CHECK_WEAPON(&r, GUN_X, 0);
	/* gun held, the other holster's gun */
	setup(&r, GUN_H, GUN_S, R);
	press(&r, R, Z_HIP);
	vrhOnMenuPick(&r.s, GUN_S);
	CHECK_WEAPON(&r, GUN_S, 0);
	/* dual: the non-gunside hand lets go */
	make_dual_hip(&r);
	vrhOnMenuPick(&r.s, GUN_X);
	CHECK_WEAPON(&r, GUN_X, 0);
	/* parked pair */
	make_parked_left(&r);
	vrhOnMenuPick(&r.s, GUN_X);
	CHECK_WEAPON(&r, GUN_X, 0);
	/* empty hands: the hip changes, the hands stay empty */
	setup(&r, GUN_H, GUN_S, R);
	vrhOnMenuPick(&r.s, GUN_X);
	CHECK_WEAPON(&r, 0, 0);
}

static void test_weapon_after_pickup(void)
{
	struct rig r;
	setup(&r, 0, GUN_S, R);
	vrhOnPickup(&r.s, GUN_H);
	CHECK_WEAPON(&r, 0, 0);
	press(&r, R, Z_SH);
	vrhOnPickup(&r.s, GUN_X);
	CHECK_WEAPON(&r, GUN_S, 0);
}

/* ---------- vrhOnLost (rule 28) ---------- */

static void test_lost_zero_or_unholstered_changes_nothing(void)
{
	struct rig r;
	struct vrhstate before;
	/* an empty shoulder holds 0: losing weapon 0 must not touch it or anything else */
	setup(&r, GUN_H, 0, L);
	press(&r, R, Z_HIP);
	before = r.s;
	vrhOnLost(&r.s, 0);
	CHECK(memcmp(&before, &r.s, sizeof before) == 0);
	vrhOnLost(&r.s, GUN_X);
	CHECK(memcmp(&before, &r.s, sizeof before) == 0);
	CHECK_WEAPON(&r, GUN_H, 0);

	make_parked_left(&r);
	before = r.s;
	vrhOnLost(&r.s, 0);
	vrhOnLost(&r.s, GUN_S + 1);
	CHECK(memcmp(&before, &r.s, sizeof before) == 0);

	/* built state (rule 27's "a hand whose holster has since become 0"): weapon 0 still
	 * changes nothing, so the hand at the empty holster is not emptied */
	setup(&r, GUN_H, GUN_S, R);
	r.s.holster[VRH_SHOULDER] = 0;
	r.s.held[R] = VRH_SHOULDER;
	r.s.parked[R] = 1;
	before = r.s;
	vrhOnLost(&r.s, 0);
	CHECK(memcmp(&before, &r.s, sizeof before) == 0);
	CHECK_EQ(r.s.held[R], VRH_SHOULDER);
}

static void test_lost_holstered_gun_not_in_hand(void)
{
	struct rig r;
	struct vrhstate before;
	setup(&r, GUN_H, GUN_S, R);
	press(&r, R, Z_SH);
	before = r.s;
	vrhOnLost(&r.s, GUN_H);
	CHECK_HOLSTERS(&r, 0, GUN_S);
	/* the hand holding the other holster's gun keeps it; nothing else changes */
	before.holster[VRH_HIP] = 0;
	CHECK(memcmp(&before, &r.s, sizeof before) == 0);
	CHECK_WEAPON(&r, GUN_S, 0);
	/* the hip is now empty */
	press(&r, L, Z_HIP);
	CHECK_EQ(r.s.held[L], -1);
	CHECK_EQ(r.out.fx, FXH(L, VRH_FX_EMPTY));
}

static void test_lost_held_single_gun(void)
{
	struct rig r;
	struct vrhstate before;
	setup(&r, GUN_H, GUN_S, L);
	press(&r, R, Z_HIP); /* gunside moves to R; R keeps gripping at the hip */
	before = r.s;
	vrhOnLost(&r.s, GUN_H);
	CHECK_HOLSTERS(&r, 0, GUN_S);
	CHECK_HELD(&r, -1, -1);
	CHECK_EQ(r.s.parked[L], 0);
	CHECK_EQ(r.s.parked[R], 0);
	CHECK_EQ(r.s.gunside, R);
	CHECK_EQ(r.s.prevgrip[L], before.prevgrip[L]);
	CHECK_EQ(r.s.prevgrip[R], before.prevgrip[R]);
	CHECK_EQ(r.s.prevzone[L], before.prevzone[L]);
	CHECK_EQ(r.s.prevzone[R], before.prevzone[R]);
	/* no fx, and the outputs follow */
	CHECK_WEAPON(&r, 0, 0);
	CHECK_EQ(r.out.gunside, R);
	/* the emptied hand letting go does nothing */
	release(&r, R, Z_HIP);
	CHECK_EQ(r.out.fx, 0);
	CHECK_HOLSTERS(&r, 0, GUN_S);
	CHECK_OUT(&r, 0, 0, R);
	/* a press at the emptied holster buzzes empty */
	press(&r, R, Z_HIP);
	CHECK_EQ(r.s.held[R], -1);
	CHECK_EQ(r.out.fx, FXH(R, VRH_FX_EMPTY));
	/* the other holster still draws */
	release(&r, R, Z_HIP);
	press(&r, R, Z_SH);
	CHECK_EQ(r.s.held[R], VRH_SHOULDER);
	CHECK_OUT(&r, GUN_S, 0, R);
}

static void test_lost_held_gun_released_at_other_holster_does_not_swap(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	press(&r, R, Z_HIP);
	move(&r, R, Z_SH);
	vrhOnLost(&r.s, GUN_H);
	r.in.grip[R] = 0;
	step(&r);
	CHECK_EQ(r.out.fx, 0);
	CHECK_HOLSTERS(&r, 0, GUN_S);
	CHECK_HELD(&r, -1, -1);
}

static void test_lost_shoulder_gun_held_left(void)
{
	struct rig r;
	struct vrhstate before;
	setup(&r, GUN_H, GUN_S, R);
	press(&r, L, Z_SH);
	before = r.s;
	vrhOnLost(&r.s, GUN_S);
	CHECK_HOLSTERS(&r, GUN_H, 0);
	CHECK_HELD(&r, -1, -1);
	before.holster[VRH_SHOULDER] = 0;
	before.held[L] = -1;
	CHECK(memcmp(&before, &r.s, sizeof before) == 0);
	CHECK_WEAPON(&r, 0, 0);
	CHECK_EQ(r.out.gunside, L);
}

static void test_lost_held_pair(void)
{
	struct rig r;
	struct vrhstate before;
	make_dual_hip(&r);
	before = r.s;
	vrhOnLost(&r.s, GUN_H);
	CHECK_HOLSTERS(&r, 0, GUN_S);
	CHECK_HELD(&r, -1, -1);
	before.holster[VRH_HIP] = 0;
	before.held[L] = -1;
	before.held[R] = -1;
	CHECK(memcmp(&before, &r.s, sizeof before) == 0);
	CHECK_WEAPON(&r, 0, 0);
	/* both grips were held: letting go does nothing */
	r.in.grip[L] = 0;
	r.in.grip[R] = 0;
	step(&r);
	CHECK_EQ(r.out.fx, 0);
	CHECK_OUT(&r, 0, 0, R);
}

static void test_lost_parked_gun_clears_parked(void)
{
	struct rig r;
	struct vrhstate before;
	make_parked_left(&r);
	before = r.s;
	vrhOnLost(&r.s, GUN_H);
	CHECK_HELD(&r, -1, -1);
	CHECK_EQ(r.s.parked[L], 0);
	CHECK_EQ(r.s.parked[R], 0);
	CHECK_HOLSTERS(&r, 0, GUN_S);
	before.holster[VRH_HIP] = 0;
	before.held[L] = -1;
	before.held[R] = -1;
	before.parked[L] = 0;
	CHECK(memcmp(&before, &r.s, sizeof before) == 0);
	CHECK_WEAPON(&r, 0, 0);
	/* after the reload, the formerly parked hand's press is an ordinary press at no zone */
	r.in.reloading = 0;
	step(&r);
	press(&r, L, Z_NONE);
	CHECK_HELD(&r, -1, -1);
	CHECK(!VRH_FX_HAS(r.out.fx, L, VRH_FX_DRAW));
}

static void test_lost_gun_in_both_holsters_empties_both(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	r.s.holster[VRH_SHOULDER] = GUN_H; /* built state: "each holster i" */
	r.s.held[R] = VRH_SHOULDER;
	vrhOnLost(&r.s, GUN_H);
	CHECK_HOLSTERS(&r, 0, 0);
	CHECK_HELD(&r, -1, -1);
}

static void test_lost_then_pickup_refills(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, R);
	vrhOnLost(&r.s, GUN_H);
	CHECK_EQ(vrhOnPickup(&r.s, GUN_H), VRH_HIP);
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	CHECK_WEAPON(&r, 0, 0);
}

/* ---------- vrhStepHolster (rule 34) and vrhOnMenuPickHand (rule 35) ---------- */

/*
 * Written from the Task B7 spec: a step or a pick never changes the gun in a hand that did not
 * act, except that the other copy of the acting hand's pair goes back; while reloading no held
 * gun changes. Every check compares the whole state, so prevgrip, prevzone and the holster that
 * should not move are covered too.
 */

#define GUN_Y 40 /* in no holster */
#define HIPH VRH_HIP
#define SHH VRH_SHOULDER

#define CHECK_STATE(got, want) check_state((got), (want), __LINE__)

static void check_state(const struct vrhstate *g, const struct vrhstate *w, int line)
{
	int h;
	if (memcmp(g, w, sizeof *g) != 0) {
		printf("  (check_state called from line %d)\n", line);
	}
	for (h = 0; h < VRH_NUM_SLOTS; h++) {
		CHECK_EQ(g->holster[h], w->holster[h]);
	}
	for (h = 0; h < 2; h++) {
		CHECK_EQ(g->held[h], w->held[h]);
		CHECK_EQ(g->parked[h], w->parked[h]);
		CHECK_EQ(g->prevgrip[h], w->prevgrip[h]);
		CHECK_EQ(g->prevzone[h], w->prevzone[h]);
	}
	CHECK_EQ(g->gunside, w->gunside);
}

/* The call returns 0 and the state is unchanged */
#define CHECK_REFUSED(r, call) do { \
	struct vrhstate _before = (r)->s; \
	CHECK_EQ((call), 0); \
	check_state(&(r)->s, &_before, __LINE__); \
} while (0)

/* vrhWeapon (rule 27) on the state alone, no tick */
#define CHECK_VW(r, w, d) do { \
	int32_t _w = -7, _d = -7; \
	vrhWeapon(&(r)->s, &_w, &_d); \
	CHECK_EQ(_w, (w)); \
	CHECK_EQ(_d, (d)); \
} while (0)

#define STEP(r, h, i, w, rl) vrhStepHolster(&(r)->s, (h), (i), (w), (rl))
#define PICK(r, h, z, w, rl) vrhOnMenuPickHand(&(r)->s, (h), (z), (w), (rl))

/* -- rule 34 -- */

static void test_step_unheld_holster_empty_hands(void)
{
	struct rig r;
	struct vrhstate want;
	build(&r, GUN_H, GUN_S, -1, -1, R);
	want = r.s;
	CHECK_EQ(STEP(&r, L, HIPH, GUN_X, 0), 1);
	want.holster[HIPH] = GUN_X;
	CHECK_STATE(&r.s, &want);
	CHECK_VW(&r, 0, 0);
	CHECK_EQ(STEP(&r, R, SHH, GUN_Y, 0), 1);
	want.holster[SHH] = GUN_Y;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, 0, 0);

	/* an empty holster takes a step too, and a hand that draws there gets the new gun */
	setup(&r, 0, GUN_S, L);
	want = r.s;
	CHECK_EQ(STEP(&r, R, HIPH, GUN_X, 0), 1);
	want.holster[HIPH] = GUN_X;
	CHECK_STATE(&r.s, &want);
	press(&r, R, Z_HIP);
	CHECK_OUT(&r, GUN_X, 0, R);
	CHECK_HELD(&r, -1, VRH_HIP);
}

static void test_step_unheld_holster_other_hand_holds_other_holster(void)
{
	struct rig r;
	struct vrhstate want;
	build(&r, GUN_H, GUN_S, -1, VRH_SHOULDER, R);
	want = r.s;
	CHECK_EQ(STEP(&r, L, HIPH, GUN_X, 0), 1);
	want.holster[HIPH] = GUN_X;
	CHECK_STATE(&r.s, &want);
	CHECK_VW(&r, GUN_S, 0);
	/* the hand holding the shoulder gun steps at the hip: only the hip changes */
	CHECK_EQ(STEP(&r, R, HIPH, GUN_Y, 0), 1);
	want.holster[HIPH] = GUN_Y;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_S, 0);

	/* mirrored: L holds the hip gun, both hands step at the shoulder */
	build(&r, GUN_H, GUN_S, VRH_HIP, -1, L);
	want = r.s;
	CHECK_EQ(STEP(&r, R, SHH, GUN_X, 0), 1);
	want.holster[SHH] = GUN_X;
	CHECK_STATE(&r.s, &want);
	CHECK_EQ(STEP(&r, L, SHH, GUN_Y, 0), 1);
	want.holster[SHH] = GUN_Y;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_H, 0);
}

static void test_step_single_held_gun(void)
{
	struct rig r;
	struct vrhstate want;
	setup(&r, GUN_H, GUN_S, L);
	press(&r, R, Z_HIP);
	CHECK_OUT(&r, GUN_H, 0, R);
	want = r.s;
	CHECK_EQ(STEP(&r, R, HIPH, GUN_X, 0), 1);
	want.holster[HIPH] = GUN_X; /* held stays the hip, gunside stays R */
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_X, 0);
	CHECK_OUT(&r, GUN_X, 0, R);
	want = r.s;
	CHECK_EQ(STEP(&r, R, HIPH, GUN_Y, 0), 1);
	want.holster[HIPH] = GUN_Y;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_Y, 0);

	/* left hand at the shoulder; letting go puts the new gun in the shoulder */
	setup(&r, GUN_H, GUN_S, R);
	press(&r, L, Z_SH);
	CHECK_OUT(&r, GUN_S, 0, L);
	want = r.s;
	CHECK_EQ(STEP(&r, L, SHH, GUN_X, 0), 1);
	want.holster[SHH] = GUN_X;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_X, 0);
	release(&r, L, Z_NONE);
	CHECK_HOLSTERS(&r, GUN_H, GUN_X);
	CHECK_HELD(&r, -1, -1);
}

static void test_step_pair_from_gunside_hand(void)
{
	struct rig r;
	struct vrhstate want;
	make_dual_hip(&r); /* gunside R */
	CHECK_OUT(&r, GUN_H, 1, R);
	want = r.s;
	CHECK_EQ(STEP(&r, R, HIPH, GUN_X, 0), 1);
	want.holster[HIPH] = GUN_X;
	want.held[L] = -1;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_X, 0);
	CHECK_OUT(&r, GUN_X, 0, R);
}

static void test_step_pair_from_other_hand(void)
{
	struct rig r;
	struct vrhstate want;
	make_dual_hip(&r); /* gunside R */
	want = r.s;
	CHECK_EQ(STEP(&r, L, HIPH, GUN_X, 0), 1);
	want.holster[HIPH] = GUN_X;
	want.held[R] = -1;
	want.gunside = L;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_X, 0);
	CHECK_OUT(&r, GUN_X, 0, L);

	/* a pair steps the other holster: only that holster changes, the pair stays */
	make_dual_hip(&r);
	want = r.s;
	CHECK_EQ(STEP(&r, L, SHH, GUN_X, 0), 1);
	want.holster[SHH] = GUN_X;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_H, 1);

	/* a shoulder pair, gunside L, stepped by R */
	build(&r, GUN_H, GUN_S, VRH_SHOULDER, VRH_SHOULDER, L);
	CHECK_VW(&r, GUN_S, 1);
	want = r.s;
	CHECK_EQ(STEP(&r, R, SHH, GUN_X, 0), 1);
	want.holster[SHH] = GUN_X;
	want.held[L] = -1;
	want.gunside = R;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_X, 0);
}

static void test_step_pair_other_hand_parked(void)
{
	struct rig r;
	struct vrhstate want;
	make_parked_left_after_reload(&r);
	want = r.s;
	CHECK_EQ(STEP(&r, R, HIPH, GUN_X, 0), 1);
	want.holster[HIPH] = GUN_X;
	want.held[L] = -1;
	want.parked[L] = 0;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_X, 0);
	CHECK_OUT(&r, GUN_X, 0, R);
}

static void test_step_acting_hand_parked(void)
{
	struct rig r;
	struct vrhstate want;
	make_parked_left_after_reload(&r);
	want = r.s;
	CHECK_EQ(STEP(&r, L, HIPH, GUN_X, 0), 1);
	want.holster[HIPH] = GUN_X;
	want.held[R] = -1;
	want.parked[L] = 0;
	want.gunside = L;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_X, 0);
	CHECK_OUT(&r, GUN_X, 0, L);

	/* mirrored on a built shoulder pair: R parked, L the gun side */
	build(&r, GUN_H, GUN_S, VRH_SHOULDER, VRH_SHOULDER, L);
	r.s.parked[R] = 1;
	want = r.s;
	CHECK_EQ(STEP(&r, R, SHH, GUN_X, 0), 1);
	want.holster[SHH] = GUN_X;
	want.held[L] = -1;
	want.parked[R] = 0;
	want.gunside = R;
	CHECK_STATE(&r.s, &want);
	CHECK_VW(&r, GUN_X, 0);
}

static void test_step_refused_other_hand_holds_holster(void)
{
	struct rig r;
	setup(&r, GUN_H, GUN_S, L);
	press(&r, R, Z_HIP);
	CHECK_REFUSED(&r, STEP(&r, L, HIPH, GUN_X, 0));
	CHECK_VW(&r, GUN_H, 0);

	build(&r, GUN_H, GUN_S, VRH_SHOULDER, -1, L);
	CHECK_REFUSED(&r, STEP(&r, R, SHH, GUN_X, 0));
	/* a parked holder counts */
	r.s.parked[L] = 1;
	CHECK_REFUSED(&r, STEP(&r, R, SHH, GUN_X, 0));
}

static void test_step_refused_same_or_other_holster_gun(void)
{
	struct rig r;
	build(&r, GUN_H, GUN_S, -1, -1, R);
	CHECK_REFUSED(&r, STEP(&r, L, HIPH, GUN_H, 0));
	CHECK_REFUSED(&r, STEP(&r, L, HIPH, GUN_S, 0));
	CHECK_REFUSED(&r, STEP(&r, R, SHH, GUN_S, 0));
	CHECK_REFUSED(&r, STEP(&r, R, SHH, GUN_H, 0));

	/* the holding hand */
	build(&r, GUN_H, GUN_S, -1, VRH_HIP, R);
	CHECK_REFUSED(&r, STEP(&r, R, HIPH, GUN_H, 0));
	CHECK_REFUSED(&r, STEP(&r, R, HIPH, GUN_S, 0));
	CHECK_REFUSED(&r, STEP(&r, R, SHH, GUN_H, 0));
	CHECK_REFUSED(&r, STEP(&r, L, SHH, GUN_H, 0));

	/* a pair */
	make_dual_hip(&r);
	CHECK_REFUSED(&r, STEP(&r, L, HIPH, GUN_S, 0));
	CHECK_REFUSED(&r, STEP(&r, R, HIPH, GUN_H, 0));
	CHECK_VW(&r, GUN_H, 1);
}

static void test_step_refused_weapon_zero_and_bad_args(void)
{
	struct rig r;
	build(&r, GUN_H, GUN_S, -1, VRH_HIP, R);
	CHECK_REFUSED(&r, STEP(&r, R, HIPH, 0, 0));
	CHECK_REFUSED(&r, STEP(&r, L, SHH, 0, 0));
	CHECK_REFUSED(&r, STEP(&r, -1, SHH, GUN_X, 0));
	CHECK_REFUSED(&r, STEP(&r, 2, SHH, GUN_X, 0));
	CHECK_REFUSED(&r, STEP(&r, 99, HIPH, GUN_X, 0));
	CHECK_REFUSED(&r, STEP(&r, L, -1, GUN_X, 0));
	/* slots 2 and 3 are the item slots since rule 36 (rule 45); 4 is the first bad slot */
	CHECK_REFUSED(&r, STEP(&r, L, VRH_NUM_SLOTS, GUN_X, 0));
	CHECK_REFUSED(&r, STEP(&r, R, VRH_NUM_SLOTS, GUN_X, 0));
	CHECK_REFUSED(&r, STEP(&r, R, 99, GUN_X, 0));

	build(&r, 0, 0, -1, -1, R);
	CHECK_REFUSED(&r, STEP(&r, L, HIPH, 0, 0));
	CHECK_REFUSED(&r, STEP(&r, L, SHH, 0, 1));
}

static void test_step_reloading_refused_for_held_gun(void)
{
	struct rig r;
	build(&r, GUN_H, GUN_S, -1, VRH_HIP, R);
	CHECK_REFUSED(&r, STEP(&r, R, HIPH, GUN_X, 1));

	/* a pair whose other copy is parked: neither hand may step the hip */
	make_parked_left(&r);
	CHECK_REFUSED(&r, STEP(&r, R, HIPH, GUN_X, 1));
	CHECK_REFUSED(&r, STEP(&r, L, HIPH, GUN_X, 1));

	/* a pair not yet parked: emptying the other copy is a change too */
	build(&r, GUN_H, GUN_S, VRH_SHOULDER, VRH_SHOULDER, R);
	CHECK_REFUSED(&r, STEP(&r, R, SHH, GUN_X, 1));
	CHECK_REFUSED(&r, STEP(&r, L, SHH, GUN_X, 1));
}

static void test_step_reloading_allowed_for_unheld_holster(void)
{
	struct rig r;
	struct vrhstate want;
	build(&r, GUN_H, GUN_S, -1, VRH_HIP, R);
	want = r.s;
	CHECK_EQ(STEP(&r, L, SHH, GUN_X, 1), 1);
	want.holster[SHH] = GUN_X;
	CHECK_STATE(&r.s, &want);
	CHECK_EQ(STEP(&r, R, SHH, GUN_Y, 1), 1);
	want.holster[SHH] = GUN_Y;
	CHECK_STATE(&r.s, &want);
	CHECK_VW(&r, GUN_H, 0);

	build(&r, GUN_H, GUN_S, -1, -1, R);
	want = r.s;
	CHECK_EQ(STEP(&r, L, HIPH, GUN_X, 1), 1);
	want.holster[HIPH] = GUN_X;
	CHECK_STATE(&r.s, &want);

	/* the parked hand steps the holster it does not hold */
	make_parked_left(&r);
	want = r.s;
	CHECK_EQ(STEP(&r, L, SHH, GUN_X, 1), 1);
	want.holster[SHH] = GUN_X;
	CHECK_STATE(&r.s, &want);
	CHECK_VW(&r, GUN_H, 1);
}

static void test_step_never_makes_fx(void)
{
	struct rig r;
	/* the next tick after a step shows no fx; CHECK_WEAPON checks it */
	make_dual_hip(&r);
	CHECK_EQ(STEP(&r, L, HIPH, GUN_X, 0), 1);
	CHECK_WEAPON(&r, GUN_X, 0);
	setup(&r, GUN_H, GUN_S, R);
	CHECK_EQ(STEP(&r, R, SHH, GUN_X, 0), 1);
	CHECK_WEAPON(&r, 0, 0);
}

/* -- rule 35 -- */

static void test_pickhand_zone_new_gun_as_step(void)
{
	struct rig r;
	struct vrhstate want;
	/* hip zone, the hand holds the hip gun */
	setup(&r, GUN_H, GUN_S, L);
	press(&r, R, Z_HIP);
	want = r.s;
	CHECK_EQ(PICK(&r, R, Z_HIP, GUN_X, 0), 1);
	want.holster[HIPH] = GUN_X;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_X, 0);
	CHECK_OUT(&r, GUN_X, 0, R);

	/* shoulder zone, the hand holds the shoulder gun */
	build(&r, GUN_H, GUN_S, VRH_SHOULDER, -1, L);
	want = r.s;
	CHECK_EQ(PICK(&r, L, Z_SH, GUN_X, 0), 1);
	want.holster[SHH] = GUN_X;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_X, 0);

	/* the hand holds the other holster's gun: only the zone's holster changes */
	build(&r, GUN_H, GUN_S, -1, VRH_SHOULDER, R);
	want = r.s;
	CHECK_EQ(PICK(&r, R, Z_HIP, GUN_X, 0), 1);
	want.holster[HIPH] = GUN_X;
	CHECK_STATE(&r.s, &want);
	CHECK_VW(&r, GUN_S, 0);

	/* a pair picks at the other holster's zone: only that holster changes, the pair stays */
	build(&r, GUN_H, GUN_S, VRH_HIP, VRH_HIP, R);
	want = r.s;
	CHECK_EQ(PICK(&r, L, Z_SH, GUN_X, 0), 1);
	want.holster[SHH] = GUN_X;
	CHECK_STATE(&r.s, &want);
	CHECK_VW(&r, GUN_H, 1);

	/* an empty hand at a holster no hand holds */
	build(&r, GUN_H, GUN_S, -1, VRH_SHOULDER, R);
	want = r.s;
	CHECK_EQ(PICK(&r, L, Z_HIP, GUN_X, 0), 1);
	want.holster[HIPH] = GUN_X;
	CHECK_STATE(&r.s, &want);
	CHECK_VW(&r, GUN_S, 0);
	build(&r, GUN_H, GUN_S, -1, -1, R);
	want = r.s;
	CHECK_EQ(PICK(&r, L, Z_SH, GUN_X, 0), 1);
	want.holster[SHH] = GUN_X;
	CHECK_STATE(&r.s, &want);
	CHECK_VW(&r, 0, 0);
}

static void test_pickhand_zone_pair_other_copy_goes_back(void)
{
	struct rig r;
	struct vrhstate want;
	make_dual_hip(&r); /* gunside R */
	want = r.s;
	CHECK_EQ(PICK(&r, L, Z_HIP, GUN_X, 0), 1);
	want.holster[HIPH] = GUN_X;
	want.held[R] = -1;
	want.gunside = L;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_X, 0);
	CHECK_OUT(&r, GUN_X, 0, L);

	make_dual_hip(&r);
	want = r.s;
	CHECK_EQ(PICK(&r, R, Z_HIP, GUN_X, 0), 1);
	want.holster[HIPH] = GUN_X;
	want.held[L] = -1;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_X, 0);

	/* parked copies: the acting hand unparks and becomes the gun side; a parked other copy empties */
	make_parked_left_after_reload(&r);
	want = r.s;
	CHECK_EQ(PICK(&r, L, Z_HIP, GUN_X, 0), 1);
	want.holster[HIPH] = GUN_X;
	want.held[R] = -1;
	want.parked[L] = 0;
	want.gunside = L;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_X, 0);

	make_parked_left_after_reload(&r);
	want = r.s;
	CHECK_EQ(PICK(&r, R, Z_HIP, GUN_X, 0), 1);
	want.holster[HIPH] = GUN_X;
	want.held[L] = -1;
	want.parked[L] = 0;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_X, 0);
}

static void test_pickhand_zone_refused_other_hand_holds_zone_holster(void)
{
	struct rig r;
	build(&r, GUN_H, GUN_S, -1, VRH_HIP, R);
	CHECK_REFUSED(&r, PICK(&r, L, Z_HIP, GUN_X, 0));
	/* the swap would change R's gun too */
	CHECK_REFUSED(&r, PICK(&r, L, Z_HIP, GUN_S, 0));
	build(&r, GUN_H, GUN_S, VRH_SHOULDER, -1, L);
	CHECK_REFUSED(&r, PICK(&r, R, Z_SH, GUN_X, 0));
	CHECK_REFUSED(&r, PICK(&r, R, Z_SH, GUN_H, 0));
}

static void test_pickhand_zone_refused_gun_already_in_zone_holster(void)
{
	struct rig r;
	build(&r, GUN_H, GUN_S, -1, -1, R);
	CHECK_REFUSED(&r, PICK(&r, L, Z_HIP, GUN_H, 0));
	CHECK_REFUSED(&r, PICK(&r, R, Z_SH, GUN_S, 0));
	build(&r, GUN_H, GUN_S, -1, VRH_SHOULDER, R);
	CHECK_REFUSED(&r, PICK(&r, L, Z_HIP, GUN_H, 0));
	CHECK_REFUSED(&r, PICK(&r, R, Z_HIP, GUN_H, 0));
}

static void test_pickhand_zone_swap_hand_holds_zone_holster(void)
{
	struct rig r;
	struct vrhstate want;
	setup(&r, GUN_H, GUN_S, L);
	press(&r, R, Z_HIP);
	want = r.s;
	CHECK_EQ(PICK(&r, R, Z_HIP, GUN_S, 0), 1);
	want.holster[HIPH] = GUN_S;
	want.holster[SHH] = GUN_H;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_S, 0);
	release(&r, R, Z_NONE);
	CHECK_HOLSTERS(&r, GUN_S, GUN_H);

	build(&r, GUN_H, GUN_S, VRH_SHOULDER, -1, L);
	want = r.s;
	CHECK_EQ(PICK(&r, L, Z_SH, GUN_H, 0), 1);
	want.holster[HIPH] = GUN_S;
	want.holster[SHH] = GUN_H;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_H, 0);

	/* a pair: the other copy goes back */
	make_dual_hip(&r); /* gunside R */
	want = r.s;
	CHECK_EQ(PICK(&r, L, Z_HIP, GUN_S, 0), 1);
	want.holster[HIPH] = GUN_S;
	want.holster[SHH] = GUN_H;
	want.held[R] = -1;
	want.gunside = L;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_S, 0);

	/* the parked hand of a pair swaps: it unparks and becomes the gun side */
	make_parked_left_after_reload(&r);
	want = r.s;
	CHECK_EQ(PICK(&r, L, Z_HIP, GUN_S, 0), 1);
	want.holster[HIPH] = GUN_S;
	want.holster[SHH] = GUN_H;
	want.held[R] = -1;
	want.parked[L] = 0;
	want.gunside = L;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_S, 0);
}

static void test_pickhand_zone_swap_no_hand_holding(void)
{
	struct rig r;
	struct vrhstate want;
	build(&r, GUN_H, GUN_S, -1, -1, R);
	want = r.s;
	CHECK_EQ(PICK(&r, L, Z_HIP, GUN_S, 0), 1);
	want.holster[HIPH] = GUN_S;
	want.holster[SHH] = GUN_H;
	CHECK_STATE(&r.s, &want);
	CHECK_VW(&r, 0, 0);
	CHECK_EQ(PICK(&r, R, Z_SH, GUN_S, 0), 1);
	want.holster[HIPH] = GUN_H;
	want.holster[SHH] = GUN_S;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, 0, 0);

	/* the zone's holster is empty */
	build(&r, 0, GUN_S, -1, -1, R);
	want = r.s;
	CHECK_EQ(PICK(&r, L, Z_HIP, GUN_S, 0), 1);
	want.holster[HIPH] = GUN_S;
	want.holster[SHH] = 0;
	CHECK_STATE(&r.s, &want);
}

static void test_pickhand_zone_swap_refused_other_hand_holds_other_holster(void)
{
	struct rig r;
	build(&r, GUN_H, GUN_S, -1, VRH_SHOULDER, R);
	CHECK_REFUSED(&r, PICK(&r, L, Z_HIP, GUN_S, 0));
	r.s.parked[R] = 1; /* parked counts as holding */
	CHECK_REFUSED(&r, PICK(&r, L, Z_HIP, GUN_S, 0));
	build(&r, GUN_H, GUN_S, VRH_HIP, -1, L);
	CHECK_REFUSED(&r, PICK(&r, R, Z_SH, GUN_H, 0));
	CHECK_VW(&r, GUN_H, 0);
}

/* Rule 45 (revised from the old "the chest counts as no zone" test): a pick at the chest is
 * rule 34 at the pouch. The pouch has no partner: a gun in a gun holster or the right hip's item
 * is no reason to refuse, and nothing swaps. */
static void test_pickhand_chest_is_pouch_slot(void)
{
	struct rig r;
	struct vrhstate want;
	/* empty hands: only the pouch changes, even for the hip's gun (no partner check) */
	build(&r, GUN_H, GUN_S, -1, -1, R);
	want = r.s;
	CHECK_EQ(PICK(&r, L, Z_CHEST, ITEM_P, 0), 1);
	want.holster[VRH_POUCH] = ITEM_P;
	CHECK_STATE(&r.s, &want);
	CHECK_REFUSED(&r, PICK(&r, L, Z_CHEST, ITEM_P, 0));
	CHECK_REFUSED(&r, PICK(&r, R, Z_CHEST, ITEM_P, 0));
	CHECK_EQ(PICK(&r, R, Z_CHEST, GUN_H, 0), 1);
	want.holster[VRH_POUCH] = GUN_H;
	CHECK_STATE(&r.s, &want);

	/* R holds the hip, L picks at the chest: the pouch, not the shoulder (as no zone would) */
	build(&r, GUN_H, GUN_S, -1, VRH_HIP, R);
	want = r.s;
	CHECK_EQ(PICK(&r, L, Z_CHEST, GUN_X, 0), 1);
	want.holster[VRH_POUCH] = GUN_X;
	CHECK_STATE(&r.s, &want);
	/* the gun hand picks at the chest while reloading: no held gun changes, so it works */
	CHECK_EQ(PICK(&r, R, Z_CHEST, ITEM_P, 1), 1);
	want.holster[VRH_POUCH] = ITEM_P;
	CHECK_STATE(&r.s, &want);
	CHECK_VW(&r, GUN_H, 0);

	/* R holds the shoulder and picks the hip gun at the chest: no swap, the pouch gets it */
	build(&r, GUN_H, GUN_S, -1, VRH_SHOULDER, R);
	want = r.s;
	CHECK_EQ(PICK(&r, R, Z_CHEST, GUN_H, 0), 1);
	want.holster[VRH_POUCH] = GUN_H;
	CHECK_STATE(&r.s, &want);

	/* the right hip's item picked at the chest: both slots name it, the right hip keeps it */
	build(&r, GUN_H, GUN_S, -1, -1, R);
	CHECK_EQ(vrhSetItem(&r.s, VRH_RHIP, ITEM_T), 1);
	want = r.s;
	CHECK_EQ(PICK(&r, L, Z_CHEST, ITEM_T, 0), 1);
	want.holster[VRH_POUCH] = ITEM_T;
	CHECK_STATE(&r.s, &want);
}

static void test_pickhand_invalid_zone_and_bad_hand(void)
{
	struct rig r;
	build(&r, GUN_H, GUN_S, -1, -1, R);
	CHECK_REFUSED(&r, PICK(&r, L, -1, GUN_X, 0));
	/* zone 4 is the right hip since rule 36 and zone 5 the left shoulder since rule 49 (the range
	 * ends at VRH_ZONE_LSHOULDER) */
	CHECK_REFUSED(&r, PICK(&r, L, VRH_NUM_ZONES, GUN_X, 0));
	CHECK_REFUSED(&r, PICK(&r, L, 100, GUN_X, 0));
	CHECK_REFUSED(&r, PICK(&r, -1, Z_NONE, GUN_X, 0));
	CHECK_REFUSED(&r, PICK(&r, 2, Z_HIP, GUN_X, 0));
	build(&r, GUN_H, GUN_S, -1, VRH_HIP, R);
	CHECK_REFUSED(&r, PICK(&r, R, VRH_NUM_ZONES, GUN_X, 0));
	CHECK_REFUSED(&r, PICK(&r, R, -1, GUN_S, 0));
	CHECK_REFUSED(&r, PICK(&r, 2, Z_NONE, GUN_X, 0));
}

static void test_pickhand_nozone_held_replace(void)
{
	struct rig r;
	struct vrhstate want;
	setup(&r, GUN_H, GUN_S, L);
	press(&r, R, Z_SH);
	want = r.s;
	CHECK_EQ(PICK(&r, R, Z_NONE, GUN_X, 0), 1);
	want.holster[SHH] = GUN_X;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_X, 0);
	CHECK_OUT(&r, GUN_X, 0, R);

	build(&r, GUN_H, GUN_S, VRH_HIP, -1, L);
	want = r.s;
	CHECK_EQ(PICK(&r, L, Z_NONE, GUN_X, 0), 1);
	want.holster[HIPH] = GUN_X;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_X, 0);
}

static void test_pickhand_nozone_held_swap(void)
{
	struct rig r;
	struct vrhstate want;
	setup(&r, GUN_H, GUN_S, L);
	press(&r, R, Z_HIP);
	want = r.s;
	CHECK_EQ(PICK(&r, R, Z_NONE, GUN_S, 0), 1);
	want.holster[HIPH] = GUN_S;
	want.holster[SHH] = GUN_H;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_S, 0);

	build(&r, GUN_H, GUN_S, VRH_SHOULDER, -1, L);
	want = r.s;
	CHECK_EQ(PICK(&r, L, Z_NONE, GUN_H, 0), 1);
	want.holster[HIPH] = GUN_S;
	want.holster[SHH] = GUN_H;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_H, 0);
}

static void test_pickhand_nozone_pair(void)
{
	struct rig r;
	struct vrhstate want;
	/* non-gunside hand: it keeps the gun and becomes the gun side */
	make_dual_hip(&r); /* gunside R */
	want = r.s;
	CHECK_EQ(PICK(&r, L, Z_NONE, GUN_X, 0), 1);
	want.holster[HIPH] = GUN_X;
	want.held[R] = -1;
	want.gunside = L;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_X, 0);
	CHECK_OUT(&r, GUN_X, 0, L);

	/* gunside hand */
	make_dual_hip(&r);
	want = r.s;
	CHECK_EQ(PICK(&r, R, Z_NONE, GUN_X, 0), 1);
	want.holster[HIPH] = GUN_X;
	want.held[L] = -1;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_X, 0);

	/* a swap from a pair */
	make_dual_hip(&r);
	want = r.s;
	CHECK_EQ(PICK(&r, L, Z_NONE, GUN_S, 0), 1);
	want.holster[HIPH] = GUN_S;
	want.holster[SHH] = GUN_H;
	want.held[R] = -1;
	want.gunside = L;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_S, 0);

	/* parked acting hand, then parked other copy */
	make_parked_left_after_reload(&r);
	want = r.s;
	CHECK_EQ(PICK(&r, L, Z_NONE, GUN_X, 0), 1);
	want.holster[HIPH] = GUN_X;
	want.held[R] = -1;
	want.parked[L] = 0;
	want.gunside = L;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_X, 0);

	make_parked_left_after_reload(&r);
	want = r.s;
	CHECK_EQ(PICK(&r, R, Z_NONE, GUN_X, 0), 1);
	want.holster[HIPH] = GUN_X;
	want.held[L] = -1;
	want.parked[L] = 0;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_X, 0);
}

static void test_pickhand_nozone_empty_hand(void)
{
	struct rig r;
	struct vrhstate want;
	build(&r, GUN_H, GUN_S, -1, -1, R);
	want = r.s;
	CHECK_EQ(PICK(&r, L, Z_NONE, GUN_X, 0), 1);
	want.holster[HIPH] = GUN_X;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, 0, 0);

	/* the other hand holds the shoulder gun: still the hip */
	build(&r, GUN_H, GUN_S, -1, VRH_SHOULDER, R);
	want = r.s;
	CHECK_EQ(PICK(&r, L, Z_NONE, GUN_X, 0), 1);
	want.holster[HIPH] = GUN_X;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_S, 0);

	/* the other hand holds the hip gun: the shoulder */
	build(&r, GUN_H, GUN_S, -1, VRH_HIP, R);
	want = r.s;
	CHECK_EQ(PICK(&r, L, Z_NONE, GUN_X, 0), 1);
	want.holster[SHH] = GUN_X;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, GUN_H, 0);
	build(&r, GUN_H, 0, VRH_HIP, -1, L);
	want = r.s;
	CHECK_EQ(PICK(&r, R, Z_NONE, GUN_X, 0), 1);
	want.holster[SHH] = GUN_X;
	CHECK_STATE(&r.s, &want);
	CHECK_VW(&r, GUN_H, 0);

	/* empty holsters */
	build(&r, 0, 0, -1, -1, R);
	want = r.s;
	CHECK_EQ(PICK(&r, R, Z_NONE, GUN_X, 0), 1);
	want.holster[HIPH] = GUN_X;
	CHECK_STATE(&r.s, &want);
}

static void test_pickhand_nozone_empty_hand_already_holstered(void)
{
	struct rig r;
	build(&r, GUN_H, GUN_S, -1, -1, R);
	CHECK_REFUSED(&r, PICK(&r, L, Z_NONE, GUN_H, 0));
	CHECK_REFUSED(&r, PICK(&r, L, Z_NONE, GUN_S, 0));
	build(&r, GUN_H, GUN_S, -1, VRH_HIP, R);
	CHECK_REFUSED(&r, PICK(&r, L, Z_NONE, GUN_H, 0));
	CHECK_REFUSED(&r, PICK(&r, L, Z_NONE, GUN_S, 0));
	/* the chest is the pouch now (rule 45): refused only for the pouch's own item */
	CHECK_EQ(vrhSetItem(&r.s, VRH_POUCH, GUN_S), 1);
	CHECK_REFUSED(&r, PICK(&r, L, Z_CHEST, GUN_S, 0));
}

static void test_pickhand_held_gun_or_zero_refused(void)
{
	struct rig r;
	build(&r, GUN_H, GUN_S, -1, VRH_HIP, R);
	CHECK_REFUSED(&r, PICK(&r, R, Z_NONE, GUN_H, 0));
	CHECK_REFUSED(&r, PICK(&r, R, Z_HIP, GUN_H, 0));
	CHECK_REFUSED(&r, PICK(&r, R, Z_SH, GUN_H, 0));
	CHECK_REFUSED(&r, PICK(&r, R, Z_CHEST, GUN_H, 0));
	CHECK_REFUSED(&r, PICK(&r, R, Z_NONE, 0, 0));
	CHECK_REFUSED(&r, PICK(&r, R, Z_HIP, 0, 0));
	CHECK_REFUSED(&r, PICK(&r, L, Z_SH, 0, 0));
	CHECK_REFUSED(&r, PICK(&r, L, Z_NONE, 0, 0));

	make_dual_hip(&r);
	CHECK_REFUSED(&r, PICK(&r, L, Z_NONE, GUN_H, 0));
	CHECK_REFUSED(&r, PICK(&r, L, Z_HIP, GUN_H, 0));
	CHECK_REFUSED(&r, PICK(&r, R, Z_SH, GUN_H, 0));
	make_parked_left_after_reload(&r);
	CHECK_REFUSED(&r, PICK(&r, L, Z_NONE, GUN_H, 0));
	CHECK_REFUSED(&r, PICK(&r, L, Z_HIP, GUN_H, 0));

	build(&r, 0, 0, -1, -1, R);
	CHECK_REFUSED(&r, PICK(&r, L, Z_NONE, 0, 0));
}

static void test_pickhand_reloading_refused_for_held_gun(void)
{
	struct rig r;
	build(&r, GUN_H, GUN_S, -1, VRH_HIP, R);
	CHECK_REFUSED(&r, PICK(&r, R, Z_NONE, GUN_X, 1));
	CHECK_REFUSED(&r, PICK(&r, R, Z_NONE, GUN_S, 1));
	CHECK_REFUSED(&r, PICK(&r, R, Z_HIP, GUN_X, 1));
	CHECK_REFUSED(&r, PICK(&r, R, Z_HIP, GUN_S, 1));

	make_parked_left(&r);
	CHECK_REFUSED(&r, PICK(&r, L, Z_NONE, GUN_X, 1));
	CHECK_REFUSED(&r, PICK(&r, R, Z_NONE, GUN_X, 1));
	CHECK_REFUSED(&r, PICK(&r, L, Z_HIP, GUN_X, 1));
	CHECK_REFUSED(&r, PICK(&r, R, Z_HIP, GUN_X, 1));
	CHECK_REFUSED(&r, PICK(&r, R, Z_HIP, GUN_S, 1));
	CHECK_REFUSED(&r, PICK(&r, L, Z_NONE, GUN_S, 1));
}

static void test_pickhand_reloading_allowed_for_unheld_holster(void)
{
	struct rig r;
	struct vrhstate want;
	/* no zone, empty hand: the shoulder, since the other hand holds the hip gun */
	build(&r, GUN_H, GUN_S, -1, VRH_HIP, R);
	want = r.s;
	CHECK_EQ(PICK(&r, L, Z_NONE, GUN_X, 1), 1);
	want.holster[SHH] = GUN_X;
	CHECK_STATE(&r.s, &want);
	CHECK_VW(&r, GUN_H, 0);

	/* a zone no hand holds */
	build(&r, GUN_H, GUN_S, -1, VRH_HIP, R);
	want = r.s;
	CHECK_EQ(PICK(&r, L, Z_SH, GUN_X, 1), 1);
	want.holster[SHH] = GUN_X;
	CHECK_STATE(&r.s, &want);
	CHECK_EQ(PICK(&r, R, Z_SH, GUN_Y, 1), 1);
	want.holster[SHH] = GUN_Y;
	CHECK_STATE(&r.s, &want);

	/* a swap of two holsters no hand holds */
	build(&r, GUN_H, GUN_S, -1, -1, R);
	want = r.s;
	CHECK_EQ(PICK(&r, L, Z_HIP, GUN_S, 1), 1);
	want.holster[HIPH] = GUN_S;
	want.holster[SHH] = GUN_H;
	CHECK_STATE(&r.s, &want);

	/* the parked hand picks for the holster it does not hold */
	make_parked_left(&r);
	want = r.s;
	CHECK_EQ(PICK(&r, L, Z_SH, GUN_X, 1), 1);
	want.holster[SHH] = GUN_X;
	CHECK_STATE(&r.s, &want);
	CHECK_VW(&r, GUN_H, 1);
}

static void test_pickhand_never_makes_pair(void)
{
	struct rig r;
	int32_t z, h, w;
	static const int32_t zones[] = { Z_NONE, Z_HIP, Z_SH, Z_CHEST, Z_RHIP };
	static const int32_t guns[] = { GUN_H, GUN_S, GUN_X };
	static const int32_t helds[][2] = { { -1, -1 }, { -1, VRH_HIP }, { VRH_SHOULDER, -1 },
		{ VRH_HIP, VRH_HIP }, { VRH_SHOULDER, VRH_SHOULDER } };
	int32_t k;
	for (k = 0; k < 5; k++) {
		for (h = 0; h < 2; h++) {
			for (z = 0; z < 5; z++) {
				for (w = 0; w < 3; w++) {
					int32_t was_pair = helds[k][0] >= 0 && helds[k][0] == helds[k][1];
					int32_t ret, d = -7, wp = -7;
					build(&r, GUN_H, GUN_S, helds[k][0], helds[k][1],
						helds[k][0] >= 0 && helds[k][1] < 0 ? L : R);
					ret = PICK(&r, h, zones[z], guns[w], 0);
					vrhWeapon(&r.s, &wp, &d);
					if (ret && !was_pair) {
						CHECK_EQ(d, 0);
						CHECK(!(r.s.held[L] >= 0 && r.s.held[L] == r.s.held[R]));
					} else if (ret && d) {
						/* a pair kept is the same pair with the same gun: the pick went
						 * to the other holster */
						CHECK_EQ(r.s.held[L], helds[k][0]);
						CHECK_EQ(r.s.held[R], helds[k][0]);
						CHECK_EQ(r.s.holster[helds[k][0]], helds[k][0] == VRH_HIP ? GUN_H : GUN_S);
					} else if (!ret) {
						CHECK_EQ(d, was_pair);
					}
				}
			}
		}
	}
}

/* ---------- item slots: chest pouch and right hip (rules 36-47) ---------- */

/*
 * Written from the Task C1 rule text. ITEM_P lives in the pouch (slot 2, chest zone) and ITEM_T
 * on the right hip (slot 3, right hip zone). Most scenarios run for both hands as the item hand
 * and for both item slots.
 */

#define ITEM_Y 70 /* in no slot */

static const int32_t item_slots[2] = { VRH_POUCH, VRH_RHIP };
static const int32_t gun_slots[2] = { VRH_HIP, VRH_SHOULDER };

/* rule 36: zone to slot */
static int32_t zone_of_slot(int32_t i)
{
	switch (i) {
	case VRH_HIP: return Z_HIP;
	case VRH_SHOULDER: return Z_SH;
	case VRH_POUCH: return Z_CHEST;
	default: return Z_RHIP;
	}
}

#define CHECK_ITEMS(r, p, t) do { \
	CHECK_EQ((r)->s.holster[VRH_POUCH], (p)); \
	CHECK_EQ((r)->s.holster[VRH_RHIP], (t)); \
} while (0)

/* setup, with the item slots filled by vrhSetItem (0 leaves a slot empty) before the quiet tick */
static void setup_items(struct rig *r, int32_t hip, int32_t sh, int32_t pouch, int32_t rhip, int32_t gunside)
{
	setup_raw(r, hip, sh, gunside);
	if (pouch) {
		CHECK_EQ(vrhSetItem(&r->s, VRH_POUCH, pouch), 1);
	}
	if (rhip) {
		CHECK_EQ(vrhSetItem(&r->s, VRH_RHIP, rhip), 1);
	}
	step(r);
}

/* Hand g draws gun slot gi's gun, then the other hand draws item slot ii's item and g's gun parks
 * behind it (rule 37). Both hands end at no zone with their grips held; the other hand is the gun
 * side. */
static void make_parked_behind_item(struct rig *r, int32_t g, int32_t gi, int32_t ii)
{
	int32_t o = 1 - g;
	setup_items(r, GUN_H, GUN_S, ITEM_P, ITEM_T, o);
	press(r, g, zone_of_slot(gi));
	CHECK_EQ(r->s.held[g], gi);
	CHECK_EQ(r->s.gunside, g);
	move(r, g, Z_NONE);
	press(r, o, zone_of_slot(ii));
	CHECK_EQ(r->s.held[o], ii);
	CHECK_EQ(r->s.held[g], gi);
	CHECK_EQ(r->s.parked[g], 1);
	CHECK_EQ(r->s.parked[o], 0);
	CHECK_OUT(r, r->s.holster[ii], 0, o);
	/* the gun parks with no fx; the item hand gets its draw */
	CHECK_EQ(r->out.fx, FXH(o, VRH_FX_DRAW));
	move(r, o, Z_NONE);
	CHECK_EQ(r->out.fx, 0);
}

/* -- rule 37: item draw -- */

static void test_item_draw_other_hand_empty(void)
{
	struct rig r;
	int32_t h, k;
	for (h = 0; h < 2; h++) {
		for (k = 0; k < 2; k++) {
			int32_t i = item_slots[k];
			int32_t item = i == VRH_POUCH ? ITEM_P : ITEM_T;
			/* the other hand is the gun side before: the first item takes the side */
			setup_items(&r, GUN_H, GUN_S, ITEM_P, ITEM_T, 1 - h);
			press(&r, h, zone_of_slot(i));
			CHECK_EQ(r.s.held[h], i);
			CHECK_EQ(r.s.held[1 - h], -1);
			CHECK_OUT(&r, item, 0, h);
			CHECK_EQ(r.out.fx, FXH(h, VRH_FX_DRAW));
			/* the slot still names its item while it is out (rule 16) */
			CHECK_HOLSTERS(&r, GUN_H, GUN_S);
			CHECK_ITEMS(&r, ITEM_P, ITEM_T);
			/* hold (rule 5): the item stays through every zone, with no buzz (rule 44) */
			move(&r, h, Z_HIP);
			CHECK_EQ(r.out.fx, 0);
			move(&r, h, Z_SH);
			CHECK_EQ(r.out.fx, 0);
			move(&r, h, Z_CHEST);
			CHECK_EQ(r.out.fx, 0);
			move(&r, h, Z_RHIP);
			CHECK_EQ(r.out.fx, 0);
			move(&r, h, Z_NONE);
			CHECK_EQ(r.s.held[h], i);
			CHECK_OUT(&r, item, 0, h);
			CHECK_WEAPON(&r, item, 0);
		}
	}
}

static void test_item_draw_empty_slot_buzzes(void)
{
	struct rig r;
	int32_t h;
	for (h = 0; h < 2; h++) {
		/* empty pouch, full right hip */
		setup_items(&r, GUN_H, GUN_S, 0, ITEM_T, 1 - h);
		press(&r, h, Z_CHEST);
		CHECK_HELD(&r, -1, -1);
		CHECK_OUT(&r, 0, 0, 1 - h);
		CHECK_EQ(r.out.fx, FXH(h, VRH_FX_EMPTY));
		/* the buzz is an edge */
		step(&r);
		CHECK_EQ(r.out.fx, 0);
		release(&r, h, Z_CHEST);
		/* empty right hip, full pouch */
		setup_items(&r, GUN_H, GUN_S, ITEM_P, 0, 1 - h);
		press(&r, h, Z_RHIP);
		CHECK_HELD(&r, -1, -1);
		CHECK_OUT(&r, 0, 0, 1 - h);
		CHECK_EQ(r.out.fx, FXH(h, VRH_FX_EMPTY));
	}
}

static void test_item_draw_parks_single_gun(void)
{
	struct rig r;
	int32_t g, a, b;
	for (g = 0; g < 2; g++) {
		for (a = 0; a < 2; a++) {
			for (b = 0; b < 2; b++) {
				int32_t ii = item_slots[b];
				make_parked_behind_item(&r, g, gun_slots[a], ii);
				CHECK_HOLSTERS(&r, GUN_H, GUN_S);
				CHECK_ITEMS(&r, ITEM_P, ITEM_T);
				/* rule 42: the item hand is the gun side, so the engine has the item */
				CHECK_WEAPON(&r, ii == VRH_POUCH ? ITEM_P : ITEM_T, 0);
				/* the parked gun stays parked while the item is out, grip held */
				step(&r);
				CHECK_EQ(r.s.parked[g], 1);
				CHECK_EQ(r.out.fx, 0);
			}
		}
	}
}

static void test_item_draw_other_item_goes_back(void)
{
	struct rig r;
	int32_t h, k;
	for (h = 0; h < 2; h++) {
		for (k = 0; k < 2; k++) {
			int32_t first = item_slots[k], second = item_slots[1 - k];
			int32_t o = 1 - h;
			setup_items(&r, GUN_H, GUN_S, ITEM_P, ITEM_T, h);
			press(&r, o, zone_of_slot(first));
			move(&r, o, Z_NONE);
			press(&r, h, zone_of_slot(second));
			CHECK_EQ(r.s.held[o], -1);
			CHECK_EQ(r.s.held[h], second);
			CHECK_EQ(r.s.parked[o], 0);
			CHECK_OUT(&r, second == VRH_POUCH ? ITEM_P : ITEM_T, 0, h);
			CHECK_EQ(r.out.fx, FXH(o, VRH_FX_HOLSTER) | FXH(h, VRH_FX_DRAW));
			CHECK_HOLSTERS(&r, GUN_H, GUN_S);
			CHECK_ITEMS(&r, ITEM_P, ITEM_T);
		}
	}
}

static void test_item_draw_same_item_buzzes_empty(void)
{
	struct rig r;
	int32_t h, k;
	for (h = 0; h < 2; h++) {
		for (k = 0; k < 2; k++) {
			int32_t i = item_slots[k];
			int32_t o = 1 - h;
			setup_items(&r, GUN_H, GUN_S, ITEM_P, ITEM_T, h);
			press(&r, o, zone_of_slot(i));
			move(&r, o, Z_NONE);
			press(&r, h, zone_of_slot(i));
			CHECK_EQ(r.s.held[o], i);
			CHECK_EQ(r.s.held[h], -1);
			CHECK_OUT(&r, i == VRH_POUCH ? ITEM_P : ITEM_T, 0, o);
			CHECK_EQ(r.out.fx, FXH(h, VRH_FX_EMPTY));
		}
	}
}

static void test_item_draw_over_parked_gun_after_item_home(void)
{
	struct rig r;
	int32_t g;
	for (g = 0; g < 2; g++) {
		int32_t o = 1 - g;
		make_parked_behind_item(&r, g, VRH_HIP, VRH_POUCH);
		release(&r, g, Z_NONE);
		release(&r, o, Z_NONE);
		CHECK_EQ(r.s.parked[g], 1);
		CHECK_EQ(r.s.held[o], -1);
		/* the other hand draws again: the other hand holds a gun (parked): it stays parked */
		press(&r, o, Z_RHIP);
		CHECK_EQ(r.s.held[o], VRH_RHIP);
		CHECK_EQ(r.s.held[g], VRH_HIP);
		CHECK_EQ(r.s.parked[g], 1);
		CHECK_OUT(&r, ITEM_T, 0, o);
		CHECK_EQ(r.out.fx, FXH(o, VRH_FX_DRAW));
	}
}

/* rule 29 comes first: the free hand draws no item during a reload */
static void test_item_draw_free_hand_blocked_while_reloading(void)
{
	struct rig r;
	int32_t g;
	for (g = 0; g < 2; g++) {
		int32_t f = 1 - g;
		setup_items(&r, GUN_H, GUN_S, ITEM_P, ITEM_T, f);
		press(&r, g, Z_HIP);
		move(&r, g, Z_NONE);
		r.in.reloading = 1;
		press(&r, f, Z_CHEST);
		CHECK_HELD(&r, g == L ? VRH_HIP : -1, g == R ? VRH_HIP : -1);
		CHECK_EQ(r.s.parked[g], 0);
		CHECK_OUT(&r, GUN_H, 0, g);
		CHECK_EQ(r.out.fx, 0);
		release(&r, f, Z_CHEST);
		CHECK_EQ(r.out.fx, 0);
		press(&r, f, Z_RHIP);
		CHECK_EQ(r.s.held[f], -1);
		CHECK_EQ(r.out.fx, 0);
		/* the edge was consumed: a grip held to the end of the reload draws nothing */
		r.in.reloading = 0;
		step(&r);
		CHECK_EQ(r.s.held[f], -1);
		CHECK_EQ(r.s.parked[g], 0);
		CHECK_EQ(r.out.fx, 0);
		/* a fresh squeeze draws, and the gun parks (rule 37) */
		release(&r, f, Z_RHIP);
		press(&r, f, Z_RHIP);
		CHECK_EQ(r.s.held[f], VRH_RHIP);
		CHECK_EQ(r.s.parked[g], 1);
		CHECK_OUT(&r, ITEM_T, 0, f);
	}
}

/* -- rule 38: gun draw with an item out -- */

static void test_gun_draw_sends_item_back(void)
{
	struct rig r;
	int32_t h, a, b;
	for (h = 0; h < 2; h++) {
		for (a = 0; a < 2; a++) {
			for (b = 0; b < 2; b++) {
				int32_t gi = gun_slots[a], ii = item_slots[b];
				int32_t o = 1 - h;
				setup_items(&r, GUN_H, GUN_S, ITEM_P, ITEM_T, h);
				press(&r, o, zone_of_slot(ii));
				move(&r, o, Z_NONE);
				press(&r, h, zone_of_slot(gi));
				CHECK_EQ(r.s.held[o], -1);
				CHECK_EQ(r.s.held[h], gi);
				CHECK_EQ(r.s.parked[L], 0);
				CHECK_EQ(r.s.parked[R], 0);
				/* rule 2: the other hand holds nothing once the item is back */
				CHECK_OUT(&r, gi == VRH_HIP ? GUN_H : GUN_S, 0, h);
				CHECK_EQ(r.out.fx, FXH(o, VRH_FX_HOLSTER) | FXH(h, VRH_FX_DRAW));
				CHECK_HOLSTERS(&r, GUN_H, GUN_S);
				CHECK_ITEMS(&r, ITEM_P, ITEM_T);
			}
		}
	}
}

static void test_gun_draw_empty_holster_item_stays(void)
{
	struct rig r;
	int32_t h;
	for (h = 0; h < 2; h++) {
		int32_t o = 1 - h;
		setup_items(&r, GUN_H, 0, ITEM_P, ITEM_T, h);
		press(&r, o, Z_CHEST);
		move(&r, o, Z_NONE);
		press(&r, h, Z_SH);
		CHECK_EQ(r.s.held[o], VRH_POUCH);
		CHECK_EQ(r.s.held[h], -1);
		CHECK_OUT(&r, ITEM_P, 0, o);
		CHECK_EQ(r.out.fx, FXH(h, VRH_FX_EMPTY));
	}
}

/* -- rule 39: parked behind an item -- */

static void test_parked_behind_item_press_and_release_do_nothing(void)
{
	struct rig r;
	int32_t g, a;
	for (g = 0; g < 2; g++) {
		for (a = 0; a < 2; a++) {
			int32_t gi = gun_slots[a];
			int32_t other_gun_zone = gi == VRH_HIP ? Z_SH : Z_HIP;
			int32_t o = 1 - g;
			make_parked_behind_item(&r, g, gi, VRH_POUCH);
			/* release: nothing, wherever (no holster, no swap at the other holster) */
			release(&r, g, other_gun_zone);
			CHECK_EQ(r.s.held[g], gi);
			CHECK_EQ(r.s.parked[g], 1);
			CHECK_EQ(r.s.held[o], VRH_POUCH);
			CHECK_OUT(&r, ITEM_P, 0, o);
			CHECK_EQ(r.out.fx, 0);
			CHECK_HOLSTERS(&r, GUN_H, GUN_S);
			/* press: nothing, no unpark, no draw, no fx; the edge is consumed */
			press(&r, g, other_gun_zone);
			CHECK_EQ(r.s.held[g], gi);
			CHECK_EQ(r.s.parked[g], 1);
			CHECK_OUT(&r, ITEM_P, 0, o);
			CHECK_EQ(r.out.fx, 0);
			CHECK_EQ(r.s.prevgrip[g], 1);
			/* a press in an item zone draws no item either */
			release(&r, g, Z_RHIP);
			press(&r, g, Z_RHIP);
			CHECK_EQ(r.s.held[g], gi);
			CHECK_EQ(r.s.parked[g], 1);
			CHECK_EQ(r.out.fx, 0);
			release(&r, g, Z_NONE);
			press(&r, g, Z_NONE);
			CHECK_EQ(r.s.parked[g], 1);
			CHECK_EQ(r.out.fx, 0);
			CHECK_WEAPON(&r, ITEM_P, 0);
		}
	}
}

static void test_parked_behind_item_no_zone_buzz(void)
{
	struct rig r;
	int32_t g;
	for (g = 0; g < 2; g++) {
		make_parked_behind_item(&r, g, VRH_HIP, VRH_POUCH);
		/* the shoulder would be a swap buzz for a single hip gun; the right hip has an item
		 * the other hand does not hold */
		move(&r, g, Z_SH);
		CHECK_EQ(r.out.fx, 0);
		move(&r, g, Z_HIP);
		CHECK_EQ(r.out.fx, 0);
		move(&r, g, Z_RHIP);
		CHECK_EQ(r.out.fx, 0);
		move(&r, g, Z_CHEST);
		CHECK_EQ(r.out.fx, 0);
		/* grip open too */
		release(&r, g, Z_NONE);
		move(&r, g, Z_SH);
		CHECK_EQ(r.out.fx, 0);
		move(&r, g, Z_RHIP);
		CHECK_EQ(r.out.fx, 0);
		CHECK_EQ(r.s.parked[g], 1);
	}
}

/* rules 3, 4 and 7 still see the parked gun once the item has gone home with its grip open */
static void test_parked_behind_item_counts_as_held(void)
{
	struct rig r;
	int32_t g;
	for (g = 0; g < 2; g++) {
		int32_t o = 1 - g;

		/* rule 3 without a pair: an empty buzz */
		make_parked_behind_item(&r, g, VRH_HIP, VRH_POUCH);
		release(&r, g, Z_NONE);
		release(&r, o, Z_NONE);
		CHECK_EQ(r.out.fx, FXH(o, VRH_FX_HOLSTER));
		CHECK_EQ(r.s.parked[g], 1);
		CHECK_EQ(r.s.gunside, o);
		press(&r, o, Z_HIP);
		CHECK_EQ(r.s.held[o], -1);
		CHECK_EQ(r.out.fx, FXH(o, VRH_FX_EMPTY));
		release(&r, o, Z_HIP);

		/* rule 3 with a pair: the second copy, dual; the parked copy stays parked */
		r.in.haspair[VRH_HIP] = 1;
		press(&r, o, Z_HIP);
		CHECK_HELD(&r, VRH_HIP, VRH_HIP);
		CHECK_EQ(r.s.parked[g], 1);
		CHECK_OUT(&r, GUN_H, 1, o);
		CHECK_EQ(r.out.fx, FXH(o, VRH_FX_DRAW));
		/* rule 31: the gun side lets go while the other copy is parked: both go home */
		release(&r, o, Z_NONE);
		CHECK_HELD(&r, -1, -1);
		CHECK_EQ(r.s.parked[g], 0);
		CHECK_OUT(&r, 0, 0, g);
		CHECK_EQ(r.out.fx, FXH(o, VRH_FX_HOLSTER) | FXH(g, VRH_FX_HOLSTER));

		/* rule 4: a draw at the other gun holster sends the parked gun home */
		make_parked_behind_item(&r, g, VRH_HIP, VRH_RHIP);
		release(&r, g, Z_NONE);
		release(&r, o, Z_NONE);
		press(&r, o, Z_SH);
		CHECK_EQ(r.s.held[o], VRH_SHOULDER);
		CHECK_EQ(r.s.held[g], -1);
		CHECK_EQ(r.s.parked[g], 0);
		CHECK_OUT(&r, GUN_S, 0, o);
		CHECK_EQ(r.out.fx, FXH(g, VRH_FX_HOLSTER) | FXH(o, VRH_FX_DRAW));
	}
}

static void test_step_pick_refused_for_gun_parked_behind_item(void)
{
	struct rig r;
	struct vrhstate want;
	int32_t g, a;
	for (g = 0; g < 2; g++) {
		for (a = 0; a < 2; a++) {
			int32_t gi = gun_slots[a], go = 1 - gun_slots[a];
			int32_t gz = zone_of_slot(gi), goz = zone_of_slot(go);
			int32_t gun = gi == VRH_HIP ? GUN_H : GUN_S, other = gi == VRH_HIP ? GUN_S : GUN_H;
			int32_t o = 1 - g;
			make_parked_behind_item(&r, g, gi, VRH_POUCH);
			/* anything that would change the parked gun */
			CHECK_REFUSED(&r, STEP(&r, g, gi, GUN_X, 0));
			CHECK_REFUSED(&r, PICK(&r, g, gz, GUN_X, 0));
			CHECK_REFUSED(&r, PICK(&r, g, gz, other, 0));
			CHECK_REFUSED(&r, PICK(&r, g, Z_NONE, GUN_X, 0));
			CHECK_REFUSED(&r, PICK(&r, g, Z_NONE, other, 0));
			CHECK_REFUSED(&r, PICK(&r, o, goz, gun, 0));
			/* the item hand's no-zone pick of the parked gun: already in a holster */
			CHECK_REFUSED(&r, PICK(&r, o, Z_NONE, gun, 0));
			/* the item hand at the parked gun's holster: the other hand holds it */
			CHECK_REFUSED(&r, STEP(&r, o, gi, GUN_X, 0));
			CHECK_REFUSED(&r, PICK(&r, o, gz, GUN_X, 0));
			/* the parked hand at the pouch the item hand holds */
			CHECK_REFUSED(&r, STEP(&r, g, VRH_POUCH, ITEM_P2, 0));
			CHECK_REFUSED(&r, PICK(&r, g, Z_CHEST, ITEM_P2, 0));
			CHECK_VW(&r, ITEM_P, 0);

			/* what changes no held gun still works */
			want = r.s;
			CHECK_EQ(STEP(&r, g, go, GUN_X, 0), 1);
			want.holster[go] = GUN_X;
			CHECK_STATE(&r.s, &want);
			CHECK_EQ(STEP(&r, o, go, GUN_Y, 0), 1);
			want.holster[go] = GUN_Y;
			CHECK_STATE(&r.s, &want);
			CHECK_EQ(STEP(&r, g, VRH_RHIP, ITEM_T2, 0), 1);
			want.holster[VRH_RHIP] = ITEM_T2;
			CHECK_STATE(&r.s, &want);
			CHECK_VW(&r, ITEM_P, 0);
		}
	}
}

/* -- rule 40: item release -- */

static void test_item_release_anywhere_goes_home(void)
{
	struct rig r;
	static const int32_t zones[] = { Z_NONE, Z_HIP, Z_SH, Z_CHEST, Z_RHIP };
	int32_t h, k, z;
	for (h = 0; h < 2; h++) {
		for (k = 0; k < 2; k++) {
			for (z = 0; z < 5; z++) {
				int32_t i = item_slots[k];
				setup_items(&r, GUN_H, GUN_S, ITEM_P, ITEM_T, 1 - h);
				press(&r, h, zone_of_slot(i));
				release(&r, h, zones[z]);
				CHECK_HELD(&r, -1, -1);
				/* gunside stays on the released hand */
				CHECK_OUT(&r, 0, 0, h);
				CHECK_EQ(r.out.fx, FXH(h, VRH_FX_HOLSTER));
				/* no swap: every slot keeps its content */
				CHECK_HOLSTERS(&r, GUN_H, GUN_S);
				CHECK_ITEMS(&r, ITEM_P, ITEM_T);
			}
		}
	}
}

static void test_gun_release_in_item_zone_goes_home(void)
{
	struct rig r;
	int32_t h, a, k;
	for (h = 0; h < 2; h++) {
		for (a = 0; a < 2; a++) {
			for (k = 0; k < 2; k++) {
				setup_items(&r, GUN_H, GUN_S, ITEM_P, ITEM_T, 1 - h);
				press(&r, h, zone_of_slot(gun_slots[a]));
				release(&r, h, zone_of_slot(item_slots[k]));
				CHECK_HELD(&r, -1, -1);
				CHECK_OUT(&r, 0, 0, h);
				CHECK_EQ(r.out.fx, FXH(h, VRH_FX_HOLSTER));
				CHECK_HOLSTERS(&r, GUN_H, GUN_S);
				CHECK_ITEMS(&r, ITEM_P, ITEM_T);
			}
		}
	}
}

/* -- rule 41: unpark after an item -- */

static void test_unpark_same_tick_as_item_release_grip_held(void)
{
	struct rig r;
	int32_t g, a, b;
	for (g = 0; g < 2; g++) {
		for (a = 0; a < 2; a++) {
			for (b = 0; b < 2; b++) {
				int32_t gi = gun_slots[a];
				int32_t o = 1 - g;
				make_parked_behind_item(&r, g, gi, item_slots[b]);
				r.in.grip[o] = 0;
				step(&r);
				CHECK_EQ(r.s.held[o], -1);
				CHECK_EQ(r.s.held[g], gi);
				CHECK_EQ(r.s.parked[g], 0);
				/* the other hand is empty: the unparked hand is the gun side */
				CHECK_OUT(&r, gi == VRH_HIP ? GUN_H : GUN_S, 0, g);
				CHECK_EQ(r.out.fx, FXH(o, VRH_FX_HOLSTER) | FXH(g, VRH_FX_DRAW));
				CHECK_WEAPON(&r, gi == VRH_HIP ? GUN_H : GUN_S, 0);
			}
		}
	}
}

/* rule 9's grip read: an untracked hand uses its last tracked grip */
static void test_unpark_after_item_untracked_grip_held(void)
{
	struct rig r;
	int32_t g;
	for (g = 0; g < 2; g++) {
		int32_t o = 1 - g;
		make_parked_behind_item(&r, g, VRH_SHOULDER, VRH_RHIP);
		r.in.tracked[g] = 0;
		r.in.grip[g] = 0; /* not read while untracked */
		r.in.grip[o] = 0;
		step(&r);
		CHECK_EQ(r.s.held[o], -1);
		CHECK_EQ(r.s.parked[g], 0);
		CHECK_OUT(&r, GUN_S, 0, g);
		CHECK_EQ(r.out.fx, FXH(o, VRH_FX_HOLSTER) | FXH(g, VRH_FX_DRAW));
	}
}

static void test_unpark_after_item_grip_open_waits_for_press(void)
{
	struct rig r;
	int32_t g, a;
	for (g = 0; g < 2; g++) {
		for (a = 0; a < 2; a++) {
			int32_t gi = gun_slots[a];
			int32_t gun = gi == VRH_HIP ? GUN_H : GUN_S;
			int32_t o = 1 - g;
			make_parked_behind_item(&r, g, gi, VRH_POUCH);
			release(&r, g, Z_NONE);
			release(&r, o, Z_NONE);
			CHECK_EQ(r.out.fx, FXH(o, VRH_FX_HOLSTER));
			CHECK_EQ(r.s.parked[g], 1);
			CHECK_EQ(r.s.held[g], gi);
			/* rule 42: fists until it unparks; the item hand stays the gun side */
			CHECK_OUT(&r, 0, 0, o);
			CHECK_WEAPON(&r, 0, 0);
			step(&r);
			CHECK_EQ(r.s.parked[g], 1);
			/* the next press unparks, in the other gun holster's zone too: one draw fx,
			 * no draw, no swap */
			press(&r, g, gi == VRH_HIP ? Z_SH : Z_HIP);
			CHECK_EQ(r.s.parked[g], 0);
			CHECK_EQ(r.s.held[g], gi);
			CHECK_EQ(r.s.held[o], -1);
			CHECK_OUT(&r, gun, 0, g);
			CHECK_EQ(r.out.fx, FXH(g, VRH_FX_DRAW));
			CHECK_HOLSTERS(&r, GUN_H, GUN_S);
		}
	}
}

/* the item goes home and the parked hand squeezes in the same tick: one draw, before the presses */
static void test_unpark_press_same_tick_as_item_release(void)
{
	struct rig r;
	int32_t g;
	for (g = 0; g < 2; g++) {
		int32_t o = 1 - g;
		make_parked_behind_item(&r, g, VRH_HIP, VRH_POUCH);
		release(&r, g, Z_SH);
		r.in.grip[o] = 0;
		r.in.grip[g] = 1;
		step(&r);
		CHECK_EQ(r.s.held[o], -1);
		CHECK_EQ(r.s.held[g], VRH_HIP);
		CHECK_EQ(r.s.parked[g], 0);
		CHECK_OUT(&r, GUN_H, 0, g);
		CHECK_EQ(r.out.fx, FXH(o, VRH_FX_HOLSTER) | FXH(g, VRH_FX_DRAW));
		CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	}
}

/* unparked while the other hand holds a gun: the gun side stays where it is */
static void test_unpark_after_item_other_hand_holds_copy(void)
{
	struct rig r;
	int32_t g;
	for (g = 0; g < 2; g++) {
		int32_t o = 1 - g;
		make_parked_behind_item(&r, g, VRH_HIP, VRH_POUCH);
		release(&r, g, Z_NONE);
		release(&r, o, Z_NONE);
		r.in.haspair[VRH_HIP] = 1;
		press(&r, o, Z_HIP);
		CHECK_HELD(&r, VRH_HIP, VRH_HIP);
		move(&r, o, Z_NONE);
		press(&r, g, Z_NONE);
		CHECK_EQ(r.s.parked[g], 0);
		CHECK_OUT(&r, GUN_H, 1, o);
		CHECK_EQ(r.out.fx, FXH(g, VRH_FX_DRAW));
		/* now rule 7: the gun side lets go, the other keeps it single and takes the side */
		release(&r, o, Z_NONE);
		CHECK_EQ(r.s.held[o], -1);
		CHECK_EQ(r.s.held[g], VRH_HIP);
		CHECK_OUT(&r, GUN_H, 0, g);
		CHECK_EQ(r.out.fx, FXH(o, VRH_FX_HOLSTER));
	}
}

/* -- rule 42: vrhWeapon with items -- */

static void test_weapon_rule42_built_states(void)
{
	struct rig r;
	setup_items(&r, GUN_H, GUN_S, ITEM_P, ITEM_T, L);
	/* the item hand is the gun side, the other gun parked */
	r.s.held[L] = VRH_POUCH;
	r.s.held[R] = VRH_HIP;
	r.s.parked[R] = 1;
	r.s.gunside = L;
	CHECK_VW(&r, ITEM_P, 0);
	/* the item gone, the gun still parked: fists */
	r.s.held[L] = -1;
	CHECK_VW(&r, 0, 0);
	/* the parked hand as the gun side still gives its gun */
	r.s.gunside = R;
	CHECK_VW(&r, GUN_H, 0);
	/* the gun side empty, the other hand holds unparked */
	r.s.parked[R] = 0;
	r.s.gunside = L;
	CHECK_VW(&r, GUN_H, 0);
	r.s.held[R] = VRH_RHIP;
	CHECK_VW(&r, ITEM_T, 0);
	/* pairs: both hands the same slot, parked or not */
	r.s.held[L] = VRH_SHOULDER;
	r.s.held[R] = VRH_SHOULDER;
	CHECK_VW(&r, GUN_S, 1);
	r.s.parked[R] = 1;
	CHECK_VW(&r, GUN_S, 1);
	r.s.gunside = R;
	CHECK_VW(&r, GUN_S, 1);
	/* different slots are not a pair; the gun side's wins */
	r.s.parked[R] = 0;
	r.s.held[L] = VRH_POUCH;
	r.s.held[R] = VRH_SHOULDER;
	CHECK_VW(&r, GUN_S, 0);
	r.s.gunside = L;
	CHECK_VW(&r, ITEM_P, 0);
	/* the gun side's slot became 0 */
	r.s.holster[VRH_POUCH] = 0;
	CHECK_VW(&r, 0, 0);
}

/* -- rule 43: pause and lost items -- */

static void test_pause_sends_item_and_parked_gun_home(void)
{
	struct rig r;
	int32_t g, b;
	for (g = 0; g < 2; g++) {
		for (b = 0; b < 2; b++) {
			int32_t o = 1 - g;
			make_parked_behind_item(&r, g, VRH_SHOULDER, item_slots[b]);
			r.in.paused = 1;
			step(&r);
			CHECK_HELD(&r, -1, -1);
			CHECK_EQ(r.s.parked[L], 0);
			CHECK_EQ(r.s.parked[R], 0);
			/* rule 25: the pause keeps the gun side */
			CHECK_OUT(&r, 0, 0, o);
			CHECK(VRH_FX_HAS(r.out.fx, o, VRH_FX_HOLSTER));
			CHECK(VRH_FX_HAS(r.out.fx, g, VRH_FX_HOLSTER));
			CHECK_HOLSTERS(&r, GUN_H, GUN_S);
			CHECK_ITEMS(&r, ITEM_P, ITEM_T);
			step(&r);
			CHECK_EQ(r.out.fx, 0);
			/* after the pause the held grips are not presses */
			r.in.paused = 0;
			step(&r);
			CHECK_HELD(&r, -1, -1);
			CHECK_OUT(&r, 0, 0, o);
			CHECK_EQ(r.out.fx, 0);
		}
	}
}

static void test_pause_sends_lone_item_home(void)
{
	struct rig r;
	int32_t h;
	for (h = 0; h < 2; h++) {
		setup_items(&r, GUN_H, GUN_S, ITEM_P, ITEM_T, 1 - h);
		press(&r, h, Z_RHIP);
		r.in.paused = 1;
		step(&r);
		CHECK_HELD(&r, -1, -1);
		CHECK_OUT(&r, 0, 0, h);
		CHECK_EQ(r.out.fx, FXH(h, VRH_FX_HOLSTER));
		CHECK_ITEMS(&r, ITEM_P, ITEM_T);
	}
}

static void test_lost_item_with_parked_gun(void)
{
	struct rig r;
	struct vrhstate before;
	int32_t g;
	for (g = 0; g < 2; g++) {
		int32_t o = 1 - g;

		/* grip open: the gun stays parked, fists, and unparks on the next press */
		make_parked_behind_item(&r, g, VRH_HIP, VRH_POUCH);
		release(&r, g, Z_NONE);
		before = r.s;
		vrhOnLost(&r.s, ITEM_P);
		before.holster[VRH_POUCH] = 0;
		before.held[o] = -1;
		CHECK_STATE(&r.s, &before);
		CHECK_EQ(r.s.parked[g], 1);
		CHECK_VW(&r, 0, 0);
		step(&r);
		CHECK_EQ(r.s.parked[g], 1);
		CHECK_OUT(&r, 0, 0, o);
		CHECK_EQ(r.out.fx, 0);
		press(&r, g, Z_NONE);
		CHECK_EQ(r.s.parked[g], 0);
		CHECK_OUT(&r, GUN_H, 0, g);
		CHECK_EQ(r.out.fx, FXH(g, VRH_FX_DRAW));
		/* the emptied pouch now buzzes empty */
		release(&r, o, Z_NONE);
		press(&r, o, Z_CHEST);
		CHECK_EQ(r.s.held[o], -1);
		CHECK_EQ(r.out.fx, FXH(o, VRH_FX_EMPTY));

		/* grip held (amended rule 43): vrhOnLost unparks it at once and makes it the gun side,
		 * so vrhWeapon gives its gun now and the next quiet tick agrees, with no fx */
		make_parked_behind_item(&r, g, VRH_SHOULDER, VRH_RHIP);
		before = r.s;
		vrhOnLost(&r.s, ITEM_T);
		before.holster[VRH_RHIP] = 0;
		before.held[o] = -1;
		before.parked[g] = 0;
		before.gunside = g;
		CHECK_STATE(&r.s, &before);
		CHECK_VW(&r, GUN_S, 0);
		step(&r);
		CHECK_EQ(r.s.parked[g], 0);
		CHECK_OUT(&r, GUN_S, 0, g);
		CHECK_EQ(r.out.fx, 0);
		/* the emptied hand's later release does nothing */
		release(&r, o, Z_NONE);
		CHECK_EQ(r.out.fx, 0);
		CHECK_HELD(&r, g == L ? VRH_SHOULDER : -1, g == R ? VRH_SHOULDER : -1);
	}
}

/* The amended rule 43 is only for a gun parked behind the lost item: a gun lost while a reload
 * parks a copy unparks nothing (rule 28 only) */
static void test_lost_during_reload_park_unparks_nothing(void)
{
	struct rig r;
	struct vrhstate want;
	int32_t gs;
	for (gs = 0; gs < 2; gs++) {
		/* the other holster's gun: the parked copy stays parked, grip held */
		make_parked_copy(&r, gs, VRH_HIP);
		CHECK_EQ(r.s.prevgrip[1 - gs], 1);
		want = r.s;
		vrhOnLost(&r.s, GUN_S);
		want.holster[VRH_SHOULDER] = 0;
		CHECK_STATE(&r.s, &want);
		/* an item in no hand: nothing but its slot */
		CHECK_EQ(vrhSetItem(&r.s, VRH_POUCH, ITEM_P), 1);
		want = r.s;
		vrhOnLost(&r.s, ITEM_P);
		want.holster[VRH_POUCH] = 0;
		CHECK_STATE(&r.s, &want);
		/* the pair's own gun: both hands empty, the gun side stays where it was */
		make_parked_copy(&r, gs, VRH_SHOULDER);
		want = r.s;
		vrhOnLost(&r.s, GUN_S);
		want.holster[VRH_SHOULDER] = 0;
		want.held[L] = -1;
		want.held[R] = -1;
		want.parked[1 - gs] = 0;
		CHECK_STATE(&r.s, &want);
		CHECK_EQ(r.s.gunside, gs);
	}
}

/* An item lost while the other hand is not parked: only rule 28 (slot 0, that hand empty;
 * gunside, prevgrip and prevzone stay) */
static void test_lost_item_other_hand_not_parked(void)
{
	struct rig r;
	struct vrhstate want;
	int32_t h, gripo;
	for (h = 0; h < 2; h++) {
		for (gripo = 0; gripo < 2; gripo++) {
			int32_t o = 1 - h;
			/* the other hand empty, its grip held or open */
			setup_items(&r, GUN_H, GUN_S, ITEM_P, ITEM_T, o);
			if (gripo) {
				press(&r, o, Z_NONE);
			}
			press(&r, h, Z_CHEST);
			CHECK_EQ(r.s.prevgrip[o], gripo);
			want = r.s;
			vrhOnLost(&r.s, ITEM_P);
			want.holster[VRH_POUCH] = 0;
			want.held[h] = -1;
			CHECK_STATE(&r.s, &want);
			CHECK_EQ(r.s.gunside, h);
			CHECK_WEAPON(&r, 0, 0);
		}
		/* the other hand holds a gun that never parked: impossible with an item out (rule 36),
		 * so a built state: the gun stays, unparked, and the gun side stays */
		build(&r, GUN_H, GUN_S, h == L ? VRH_RHIP : VRH_HIP, h == R ? VRH_RHIP : VRH_HIP, h);
		CHECK_EQ(vrhSetItem(&r.s, VRH_POUCH, ITEM_P), 1);
		r.s.holster[VRH_RHIP] = ITEM_T;
		want = r.s;
		vrhOnLost(&r.s, ITEM_T);
		want.holster[VRH_RHIP] = 0;
		want.held[h] = -1;
		CHECK_STATE(&r.s, &want);
	}
}

static void test_lost_item_slots(void)
{
	struct rig r;
	struct vrhstate want;
	/* not held */
	setup_items(&r, GUN_H, GUN_S, ITEM_P, ITEM_T, R);
	want = r.s;
	vrhOnLost(&r.s, ITEM_T);
	want.holster[VRH_RHIP] = 0;
	CHECK_STATE(&r.s, &want);
	/* in no slot: nothing */
	vrhOnLost(&r.s, ITEM_P2);
	CHECK_STATE(&r.s, &want);
	/* the same weapon in both item slots empties both */
	CHECK_EQ(vrhSetItem(&r.s, VRH_RHIP, ITEM_P), 1);
	vrhOnLost(&r.s, ITEM_P);
	CHECK_ITEMS(&r, 0, 0);
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	/* held alone, no parked gun */
	setup_items(&r, GUN_H, GUN_S, ITEM_P, ITEM_T, R);
	press(&r, L, Z_CHEST);
	vrhOnLost(&r.s, ITEM_P);
	CHECK_HELD(&r, -1, -1);
	CHECK_ITEMS(&r, 0, ITEM_T);
	CHECK_EQ(r.s.gunside, L);
	CHECK_WEAPON(&r, 0, 0);
	release(&r, L, Z_NONE);
	CHECK_EQ(r.out.fx, 0);
}

static void test_lost_gun_parked_behind_item(void)
{
	struct rig r;
	int32_t g;
	for (g = 0; g < 2; g++) {
		int32_t o = 1 - g;
		make_parked_behind_item(&r, g, VRH_HIP, VRH_POUCH);
		vrhOnLost(&r.s, GUN_H);
		CHECK_EQ(r.s.held[g], -1);
		CHECK_EQ(r.s.parked[g], 0);
		CHECK_EQ(r.s.held[o], VRH_POUCH);
		CHECK_HOLSTERS(&r, 0, GUN_S);
		CHECK_ITEMS(&r, ITEM_P, ITEM_T);
		CHECK_EQ(r.s.gunside, o);
		CHECK_WEAPON(&r, ITEM_P, 0);
	}
}

/* -- rule 44: zone buzz at the item zones -- */

static void test_zone_buzz_item_zones(void)
{
	struct rig r;
	int32_t h;
	for (h = 0; h < 2; h++) {
		int32_t o = 1 - h;
		setup_items(&r, GUN_H, GUN_S, ITEM_P, ITEM_T, R);
		move(&r, h, Z_CHEST);
		CHECK_EQ(r.out.fx, FXH(h, VRH_FX_ZONE));
		step(&r);
		CHECK_EQ(r.out.fx, 0);
		move(&r, h, Z_NONE);
		CHECK_EQ(r.out.fx, 0);
		move(&r, h, Z_RHIP);
		CHECK_EQ(r.out.fx, FXH(h, VRH_FX_ZONE));
		/* straight from one item zone to the other */
		move(&r, h, Z_CHEST);
		CHECK_EQ(r.out.fx, FXH(h, VRH_FX_ZONE));
		move(&r, h, Z_NONE);

		/* an empty slot gives none */
		setup_items(&r, GUN_H, GUN_S, 0, ITEM_T, R);
		move(&r, h, Z_CHEST);
		CHECK_EQ(r.out.fx, 0);
		move(&r, h, Z_RHIP);
		CHECK_EQ(r.out.fx, FXH(h, VRH_FX_ZONE));

		/* the other hand holds the pouch's item: none there; the right hip's item and a gun
		 * holster (rule 38) can still be taken */
		setup_items(&r, GUN_H, GUN_S, ITEM_P, ITEM_T, R);
		press(&r, o, Z_CHEST);
		move(&r, o, Z_NONE);
		move(&r, h, Z_CHEST);
		CHECK_EQ(r.out.fx, 0);
		move(&r, h, Z_RHIP);
		CHECK_EQ(r.out.fx, FXH(h, VRH_FX_ZONE));
		move(&r, h, Z_HIP);
		CHECK_EQ(r.out.fx, FXH(h, VRH_FX_ZONE));
		move(&r, h, Z_NONE);

		/* the item hand: none at the gun holsters (no swap) nor at the item zones */
		move(&r, o, Z_HIP);
		CHECK_EQ(r.out.fx, 0);
		move(&r, o, Z_SH);
		CHECK_EQ(r.out.fx, 0);
		move(&r, o, Z_RHIP);
		CHECK_EQ(r.out.fx, 0);
		move(&r, o, Z_CHEST);
		CHECK_EQ(r.out.fx, 0);

		/* a hand holding a gun: none at the item zones */
		setup_items(&r, GUN_H, GUN_S, ITEM_P, ITEM_T, R);
		press(&r, h, Z_SH);
		move(&r, h, Z_CHEST);
		CHECK_EQ(r.out.fx, 0);
		move(&r, h, Z_RHIP);
		CHECK_EQ(r.out.fx, 0);
		/* the other, empty hand still buzzes there */
		move(&r, o, Z_RHIP);
		CHECK_EQ(r.out.fx, FXH(o, VRH_FX_ZONE));
	}
}

/* rule 30 still applies at the item zones */
static void test_zone_buzz_item_zones_free_hand_reloading(void)
{
	struct rig r;
	int32_t g;
	for (g = 0; g < 2; g++) {
		int32_t f = 1 - g;
		setup_items(&r, GUN_H, GUN_S, ITEM_P, ITEM_T, f);
		press(&r, g, Z_HIP);
		move(&r, g, Z_NONE);
		r.in.reloading = 1;
		move(&r, f, Z_CHEST);
		CHECK_EQ(r.out.fx, 0);
		move(&r, f, Z_RHIP);
		CHECK_EQ(r.out.fx, 0);
		move(&r, f, Z_NONE);
		r.in.reloading = 0;
		step(&r);
		move(&r, f, Z_CHEST);
		CHECK_EQ(r.out.fx, FXH(f, VRH_FX_ZONE));
	}
}

/* -- rule 45: steps and picks at item slots -- */

static void test_step_item_slot_no_hand_holds(void)
{
	struct rig r;
	struct vrhstate want;
	int32_t h;
	for (h = 0; h < 2; h++) {
		build(&r, GUN_H, GUN_S, -1, -1, R);
		CHECK_EQ(vrhSetItem(&r.s, VRH_POUCH, ITEM_P), 1);
		CHECK_EQ(vrhSetItem(&r.s, VRH_RHIP, ITEM_T), 1);
		want = r.s;
		CHECK_EQ(STEP(&r, h, VRH_POUCH, ITEM_P2, 0), 1);
		want.holster[VRH_POUCH] = ITEM_P2;
		CHECK_STATE(&r.s, &want);
		CHECK_EQ(STEP(&r, h, VRH_RHIP, ITEM_T2, 0), 1);
		want.holster[VRH_RHIP] = ITEM_T2;
		CHECK_STATE(&r.s, &want);
		/* weapon 0 or the slot's own item: no change */
		CHECK_REFUSED(&r, STEP(&r, h, VRH_POUCH, 0, 0));
		CHECK_REFUSED(&r, STEP(&r, h, VRH_POUCH, ITEM_P2, 0));
		CHECK_REFUSED(&r, STEP(&r, h, VRH_RHIP, ITEM_T2, 0));
		/* no partner: the other item slot's item or a gun holster's gun is no reason to
		 * refuse, and nothing swaps */
		CHECK_EQ(STEP(&r, h, VRH_POUCH, ITEM_T2, 0), 1);
		want.holster[VRH_POUCH] = ITEM_T2;
		CHECK_STATE(&r.s, &want);
		CHECK_EQ(STEP(&r, h, VRH_RHIP, GUN_H, 0), 1);
		want.holster[VRH_RHIP] = GUN_H;
		CHECK_STATE(&r.s, &want);
		/* while reloading: no held gun changes, so it works */
		CHECK_EQ(STEP(&r, h, VRH_POUCH, ITEM_P, 1), 1);
		want.holster[VRH_POUCH] = ITEM_P;
		CHECK_STATE(&r.s, &want);
		/* an empty item slot can be stepped into */
		vrhOnLost(&r.s, GUN_H);
		want.holster[VRH_RHIP] = 0;
		want.holster[VRH_HIP] = 0;
		CHECK_STATE(&r.s, &want);
		CHECK_EQ(STEP(&r, h, VRH_RHIP, ITEM_T, 0), 1);
		want.holster[VRH_RHIP] = ITEM_T;
		CHECK_STATE(&r.s, &want);
	}
}

static void test_step_item_slot_gun_hand(void)
{
	struct rig r;
	struct vrhstate want;
	build(&r, GUN_H, GUN_S, -1, VRH_HIP, R);
	CHECK_EQ(vrhSetItem(&r.s, VRH_POUCH, ITEM_P), 1);
	want = r.s;
	CHECK_EQ(STEP(&r, R, VRH_POUCH, ITEM_P2, 0), 1);
	want.holster[VRH_POUCH] = ITEM_P2;
	CHECK_STATE(&r.s, &want);
	CHECK_EQ(STEP(&r, R, VRH_RHIP, ITEM_T, 1), 1);
	want.holster[VRH_RHIP] = ITEM_T;
	CHECK_STATE(&r.s, &want);
	CHECK_EQ(STEP(&r, L, VRH_RHIP, ITEM_T2, 0), 1);
	want.holster[VRH_RHIP] = ITEM_T2;
	CHECK_STATE(&r.s, &want);
	CHECK_VW(&r, GUN_H, 0);
}

static void test_step_item_slot_hand_holds_item(void)
{
	struct rig r;
	struct vrhstate want;
	int32_t g, b;
	for (g = 0; g < 2; g++) {
		for (b = 0; b < 2; b++) {
			int32_t ii = item_slots[b], io = item_slots[1 - b];
			int32_t nw = ii == VRH_POUCH ? ITEM_P2 : ITEM_T2;
			int32_t o = 1 - g;
			/* item hand o, g's gun parked behind it */
			make_parked_behind_item(&r, g, VRH_HIP, ii);
			want = r.s;
			/* the parked hand may not change the item the other hand holds */
			CHECK_REFUSED(&r, STEP(&r, g, ii, nw, 0));
			CHECK_REFUSED(&r, STEP(&r, o, ii, r.s.holster[ii], 0));
			CHECK_REFUSED(&r, STEP(&r, o, ii, 0, 0));
			/* the item hand steps its own slot: it holds the new item, still the gun
			 * side, the gun stays parked */
			CHECK_EQ(STEP(&r, o, ii, nw, 0), 1);
			want.holster[ii] = nw;
			CHECK_STATE(&r.s, &want);
			CHECK_VW(&r, nw, 0);
			/* the item hand steps the other item slot: only that slot */
			CHECK_EQ(STEP(&r, o, io, GUN_X, 0), 1);
			want.holster[io] = GUN_X;
			CHECK_STATE(&r.s, &want);
			/* the same through a pick in the slot's zone */
			CHECK_EQ(PICK(&r, o, zone_of_slot(ii), ITEM_Y, 0), 1);
			want.holster[ii] = ITEM_Y;
			CHECK_STATE(&r.s, &want);
			CHECK_REFUSED(&r, PICK(&r, g, zone_of_slot(ii), nw, 0));
			CHECK_REFUSED(&r, PICK(&r, o, zone_of_slot(ii), ITEM_Y, 0));
			CHECK_EQ(PICK(&r, g, zone_of_slot(io), GUN_Y, 0), 1);
			want.holster[io] = GUN_Y;
			CHECK_STATE(&r.s, &want);
			/* a tick: the item hand holds the new item */
			step(&r);
			CHECK_OUT(&r, ITEM_Y, 0, o);
			CHECK_EQ(r.out.fx, 0);
		}
	}
}

static void test_pickhand_item_zones_as_step(void)
{
	struct rig r;
	struct vrhstate want;
	int32_t h;
	for (h = 0; h < 2; h++) {
		/* the right hip (zone 4 is valid since rule 45) */
		build(&r, GUN_H, GUN_S, -1, -1, R);
		want = r.s;
		CHECK_EQ(PICK(&r, h, Z_RHIP, ITEM_T, 0), 1);
		want.holster[VRH_RHIP] = ITEM_T;
		CHECK_STATE(&r.s, &want);
		CHECK_REFUSED(&r, PICK(&r, h, Z_RHIP, ITEM_T, 0));
		CHECK_REFUSED(&r, PICK(&r, h, Z_RHIP, 0, 0));
		CHECK_EQ(PICK(&r, h, Z_CHEST, ITEM_P, 0), 1);
		want.holster[VRH_POUCH] = ITEM_P;
		CHECK_STATE(&r.s, &want);
		/* no swap with the hip although the pick is the hip's gun */
		CHECK_EQ(PICK(&r, h, Z_RHIP, GUN_H, 0), 1);
		want.holster[VRH_RHIP] = GUN_H;
		CHECK_STATE(&r.s, &want);
		/* the gun hand picks at the right hip while reloading: only the slot */
		build(&r, GUN_H, GUN_S, h == L ? VRH_SHOULDER : -1, h == R ? VRH_SHOULDER : -1, h);
		want = r.s;
		CHECK_EQ(PICK(&r, h, Z_RHIP, ITEM_T2, 1), 1);
		want.holster[VRH_RHIP] = ITEM_T2;
		CHECK_STATE(&r.s, &want);
		CHECK_VW(&r, GUN_S, 0);
	}
}

static void test_pickhand_nozone_item_hand_counts_as_empty(void)
{
	struct rig r;
	struct vrhstate want;
	int32_t h;
	for (h = 0; h < 2; h++) {
		int32_t o = 1 - h;
		/* the other hand empty: the hip gets the pick; the item stays in the hand */
		setup_items(&r, GUN_H, GUN_S, ITEM_P, ITEM_T, o);
		press(&r, h, Z_CHEST);
		move(&r, h, Z_NONE);
		want = r.s;
		CHECK_EQ(PICK(&r, h, Z_NONE, GUN_X, 0), 1);
		want.holster[VRH_HIP] = GUN_X;
		CHECK_STATE(&r.s, &want);
		/* a gun already in a holster: no change */
		CHECK_REFUSED(&r, PICK(&r, h, Z_NONE, GUN_S, 0));
		CHECK_REFUSED(&r, PICK(&r, h, Z_NONE, GUN_X, 0));
		CHECK_WEAPON(&r, ITEM_P, 0);

		/* the other hand holds the hip's gun (parked): the shoulder, also while reloading */
		make_parked_behind_item(&r, o, VRH_HIP, VRH_RHIP);
		want = r.s;
		CHECK_EQ(PICK(&r, h, Z_NONE, GUN_X, 0), 1);
		want.holster[VRH_SHOULDER] = GUN_X;
		CHECK_STATE(&r.s, &want);
		CHECK_EQ(PICK(&r, h, Z_NONE, GUN_Y, 1), 1);
		want.holster[VRH_SHOULDER] = GUN_Y;
		CHECK_STATE(&r.s, &want);
		CHECK_VW(&r, ITEM_T, 0);

		/* the other hand holds the shoulder's gun (parked): the hip */
		make_parked_behind_item(&r, o, VRH_SHOULDER, VRH_POUCH);
		want = r.s;
		CHECK_EQ(PICK(&r, h, Z_NONE, GUN_X, 0), 1);
		want.holster[VRH_HIP] = GUN_X;
		CHECK_STATE(&r.s, &want);
	}
}

/* -- rule 46 -- */

static void test_menupick_item_hand_counts_as_empty(void)
{
	struct rig r;
	struct vrhstate want;
	int32_t h;
	for (h = 0; h < 2; h++) {
		setup_items(&r, GUN_H, GUN_S, ITEM_P, ITEM_T, 1 - h);
		press(&r, h, Z_RHIP);
		move(&r, h, Z_NONE);
		want = r.s;
		/* empty hands for rule 23: the hip gets the gun, the item stays in the hand */
		vrhOnMenuPick(&r.s, GUN_X);
		want.holster[VRH_HIP] = GUN_X;
		CHECK_STATE(&r.s, &want);
		/* already in a holster: nothing */
		vrhOnMenuPick(&r.s, GUN_S);
		CHECK_STATE(&r.s, &want);
		vrhOnMenuPick(&r.s, 0);
		CHECK_STATE(&r.s, &want);
		CHECK_WEAPON(&r, ITEM_T, 0);
	}

	/* a gun parked behind an item is the only gun held: its holster is the source; it is
	 * single, so it stays held and parked (no pair to drop), and the item stays */
	{
		int32_t g, a, b;
		for (g = 0; g < 2; g++) {
			for (a = 0; a < 2; a++) {
				for (b = 0; b < 2; b++) {
					int32_t gi = gun_slots[a], ii = item_slots[b];
					int32_t o = 1 - g;
					make_parked_behind_item(&r, g, gi, ii);
					want = r.s;
					vrhOnMenuPick(&r.s, GUN_X);
					want.holster[gi] = GUN_X;
					CHECK_STATE(&r.s, &want);
					CHECK_EQ(r.s.held[g], gi);
					CHECK_EQ(r.s.parked[g], 1);
					CHECK_EQ(r.s.held[o], ii);
					CHECK_ITEMS(&r, ITEM_P, ITEM_T);
					/* the other holster's gun: the two swap, the hands as before */
					vrhOnMenuPick(&r.s, gi == VRH_HIP ? GUN_S : GUN_H);
					want.holster[gi] = gi == VRH_HIP ? GUN_S : GUN_H;
					want.holster[1 - gi] = GUN_X;
					CHECK_STATE(&r.s, &want);
					CHECK_WEAPON(&r, ii == VRH_POUCH ? ITEM_P : ITEM_T, 0);
				}
			}
		}
	}
}

static void test_pickup_never_fills_item_slots(void)
{
	struct rig r;
	setup_items(&r, 0, 0, 0, 0, R);
	CHECK_EQ(vrhOnPickup(&r.s, GUN_H), VRH_HIP);
	CHECK_EQ(vrhOnPickup(&r.s, GUN_S), VRH_SHOULDER);
	CHECK_EQ(vrhOnPickup(&r.s, GUN_X), VRH_GUN3);
	CHECK_EQ(vrhOnPickup(&r.s, GUN_4), -1);
	CHECK_ITEMS(&r, 0, 0);
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	/* full item slots stay as they are too */
	setup_items(&r, GUN_H, GUN_S, ITEM_P, ITEM_T, R);
	CHECK_EQ(vrhOnPickup(&r.s, GUN_X), VRH_GUN3);
	CHECK_EQ(vrhOnPickup(&r.s, GUN_4), -1);
	CHECK_ITEMS(&r, ITEM_P, ITEM_T);
	CHECK_HOLSTERS(&r, GUN_H, GUN_S);
	/* an empty item slot with a gun holster free: the gun holster */
	setup_items(&r, GUN_H, 0, 0, 0, R);
	CHECK_EQ(vrhOnPickup(&r.s, GUN_X), VRH_SHOULDER);
	CHECK_ITEMS(&r, 0, 0);
}

/* -- rule 47: vrhSetItem -- */

static void test_setitem(void)
{
	struct rig r;
	struct vrhstate want;
	setup(&r, GUN_H, GUN_S, R);
	/* not an item slot */
	CHECK_REFUSED(&r, vrhSetItem(&r.s, VRH_HIP, ITEM_P));
	CHECK_REFUSED(&r, vrhSetItem(&r.s, VRH_SHOULDER, ITEM_P));
	CHECK_REFUSED(&r, vrhSetItem(&r.s, VRH_HIP, 0));
	CHECK_REFUSED(&r, vrhSetItem(&r.s, VRH_NUM_SLOTS, ITEM_P));
	CHECK_REFUSED(&r, vrhSetItem(&r.s, -1, ITEM_P));
	CHECK_REFUSED(&r, vrhSetItem(&r.s, 99, ITEM_P));
	/* same weapon (an empty slot set to 0) */
	CHECK_REFUSED(&r, vrhSetItem(&r.s, VRH_POUCH, 0));
	CHECK_REFUSED(&r, vrhSetItem(&r.s, VRH_RHIP, 0));
	/* set */
	want = r.s;
	CHECK_EQ(vrhSetItem(&r.s, VRH_POUCH, ITEM_P), 1);
	want.holster[VRH_POUCH] = ITEM_P;
	CHECK_STATE(&r.s, &want);
	CHECK_REFUSED(&r, vrhSetItem(&r.s, VRH_POUCH, ITEM_P));
	CHECK_EQ(vrhSetItem(&r.s, VRH_RHIP, ITEM_T), 1);
	want.holster[VRH_RHIP] = ITEM_T;
	CHECK_STATE(&r.s, &want);
	CHECK_EQ(vrhSetItem(&r.s, VRH_POUCH, ITEM_P2), 1);
	want.holster[VRH_POUCH] = ITEM_P2;
	CHECK_STATE(&r.s, &want);
	/* 0 empties */
	CHECK_EQ(vrhSetItem(&r.s, VRH_POUCH, 0), 1);
	want.holster[VRH_POUCH] = 0;
	CHECK_STATE(&r.s, &want);
	/* no fx */
	step(&r);
	CHECK_EQ(r.out.fx, 0);
	CHECK_OUT(&r, 0, 0, R);

	/* a hand holds the slot's item: no change, the other slot still works */
	press(&r, L, Z_RHIP);
	CHECK_EQ(r.s.held[L], VRH_RHIP);
	CHECK_REFUSED(&r, vrhSetItem(&r.s, VRH_RHIP, ITEM_T2));
	CHECK_REFUSED(&r, vrhSetItem(&r.s, VRH_RHIP, 0));
	want = r.s;
	CHECK_EQ(vrhSetItem(&r.s, VRH_POUCH, ITEM_P), 1);
	want.holster[VRH_POUCH] = ITEM_P;
	CHECK_STATE(&r.s, &want);
	CHECK_WEAPON(&r, ITEM_T, 0);
	/* once it is home the slot can change again */
	release(&r, L, Z_NONE);
	CHECK_EQ(vrhSetItem(&r.s, VRH_RHIP, ITEM_T2), 1);
	CHECK_ITEMS(&r, ITEM_P, ITEM_T2);
}

/* -- rule 36 (rule 16 extended) and rule 42 over random play -- */

static uint32_t fuzz_seed;

/* Rule 27: a copy of the rig runs one quiet tick (each hand's grip and zone as last recorded,
 * tracked, not paused, not reloading); its weapon and dual equal vrhWeapon on the state, with
 * no fx */
static int32_t quiet_tick_agrees(const struct rig *r)
{
	struct rig q = *r;
	int32_t w = -7, d = -7, h;
	vrhWeapon(&r->s, &w, &d);
	for (h = 0; h < 2; h++) {
		q.in.grip[h] = q.s.prevgrip[h];
		q.in.zone[h] = q.s.prevzone[h];
		q.in.tracked[h] = 1;
	}
	q.in.paused = 0;
	q.in.reloading = 0;
	step(&q);
	return q.out.weapon == w && q.out.dual == d && q.out.fx == 0;
}

static uint32_t fuzz_next(void)
{
	fuzz_seed = fuzz_seed * 1103515245u + 12345u;
	return (fuzz_seed >> 16) & 0x7fff;
}

/* At most one gun holster's gun and one item out; an item hand's other hand is empty or parked;
 * items never pair; only gun holster guns park; a parked hand is never the gun side; vrhTick's
 * weapon and dual are rule 42's */
static void test_items_invariants_random_play(void)
{
	struct rig r;
	int32_t n, h, bad = 0;
	fuzz_seed = 12345;
	setup_items(&r, GUN_H, GUN_S, ITEM_P, ITEM_T, R);
	r.in.haspair[VRH_HIP] = 1;
	for (n = 0; n < 40000 && bad < 5; n++) {
		const struct vrhstate *s = &r.s;
		int32_t ok = 1, w, d, gs, ot;
		for (h = 0; h < 2; h++) {
			if (fuzz_next() % 3 == 0) {
				r.in.grip[h] = !r.in.grip[h];
			}
			if (fuzz_next() % 4 == 0) {
				r.in.zone[h] = (int32_t)(fuzz_next() % 5);
			}
			r.in.tracked[h] = fuzz_next() % 40 != 0;
		}
		r.in.paused = fuzz_next() % 60 == 0;
		if (fuzz_next() % 50 == 0) {
			vrhOnLost(&r.s, fuzz_next() % 2 ? ITEM_P : ITEM_T);
			/* rule 27: vrhWeapon equals the next quiet tick's output, right after vrhOnLost */
			if (!quiet_tick_agrees(&r)) {
				bad++;
				printf("  random play tick %d: vrhWeapon after vrhOnLost differs from a quiet tick\n", n);
				CHECK(0);
			}
		}
		if (fuzz_next() % 100 == 0) {
			vrhSetItem(&r.s, VRH_POUCH, ITEM_P);
			vrhSetItem(&r.s, VRH_RHIP, ITEM_T);
		}
		step(&r);
		for (h = 0; h < 2; h++) {
			int32_t a = s->held[h], b = s->held[1 - h];
			if (a < -1 || a >= VRH_NUM_SLOTS) ok = 0;
			if ((a == VRH_POUCH || a == VRH_RHIP) && !(b == -1 || s->parked[1 - h])) ok = 0;
			if (s->parked[h] && !(a == VRH_HIP || a == VRH_SHOULDER)) ok = 0;
			if (s->parked[h] && s->gunside == h) ok = 0;
			if (a >= 0 && s->holster[a] == 0) ok = 0;
		}
		if (s->held[L] >= 0 && s->held[R] >= 0) {
			int32_t ga = s->held[L] == VRH_HIP || s->held[L] == VRH_SHOULDER;
			int32_t gb = s->held[R] == VRH_HIP || s->held[R] == VRH_SHOULDER;
			if (ga && gb && s->held[L] != s->held[R]) ok = 0; /* two gun holsters out */
			if (!ga && !gb) ok = 0; /* two items out */
		}
		if (r.in.paused && (s->held[L] != -1 || s->held[R] != -1)) ok = 0;
		gs = s->gunside;
		ot = 1 - gs;
		if (s->held[gs] >= 0) {
			w = s->holster[s->held[gs]];
		} else if (s->held[ot] >= 0 && !s->parked[ot]) {
			w = s->holster[s->held[ot]];
		} else {
			w = 0;
		}
		d = s->held[L] >= 0 && s->held[L] == s->held[R];
		if (r.out.weapon != w || r.out.dual != d || r.out.gunside != gs) ok = 0;
		if (!ok) {
			bad++;
			printf("  random play tick %d: held %d %d parked %d %d gunside %d weapon %d dual %d\n",
				n, s->held[L], s->held[R], s->parked[L], s->parked[R], gs, r.out.weapon,
				r.out.dual);
		}
		CHECK(ok);
		CHECK(quiet_tick_agrees(&r));
	}
}

/* ---------- entry ---------- */

void run_holster_core_tests(void)
{
	test_init_state();
	test_init_grip_held_from_first_tick_does_not_draw();
	test_quiet_start_is_fists();

	test_draw_from_hip();
	test_draw_from_shoulder();
	test_draw_empty_holster_buzzes();
	test_press_outside_zones_draws_nothing();
	test_first_gun_sets_gunside_left();
	test_first_gun_sets_gunside_right();

	test_pair_second_hand_takes_copy();
	test_pair_at_shoulder();
	test_no_pair_second_hand_buzzes_empty();

	test_other_holster_returns_other_hands_gun();
	test_other_holster_empty_buzzes();

	test_hold_keeps_gun_through_zones();

	test_release_in_own_zone_holsters();
	test_release_no_zone_holsters();
	test_release_at_chest_holsters();
	test_release_at_other_holster_swaps();
	test_release_shoulder_gun_at_hip_swaps();
	test_release_at_empty_other_holster_swaps();

	test_dual_release_gunside_hand_moves_gunside();
	test_dual_release_other_hand_keeps_gunside();
	test_dual_release_at_other_holster_never_swaps();
	test_dual_both_release_same_tick_never_swaps();
	test_dual_release_then_single_release_next_tick_swaps();

	test_reloading_ignores_shoulder_press();
	test_reloading_ignores_shoulder_press_on_empty_holster();
	test_reloading_free_hand_hip_draws_after_reload();
	test_reloading_gunside_hand_hip_draws_shoulder_ignored();
	test_reloading_shoulder_release_holsters();
	test_reloading_hip_release_of_shoulder_gun_still_swaps();

	test_free_hand_press_while_reloading_does_nothing();
	test_free_hand_blocked_press_edge_consumed();
	test_free_hand_blocked_press_then_release_stays_parked();

	test_park_while_reloading_release_does_nothing();
	test_parked_release_at_hip_zone_stays_parked();
	test_parked_press_during_reload_does_nothing();
	test_unpark_after_reload_wherever_the_hand_is();
	test_unpark_at_no_zone();
	test_parked_release_during_reload_stays_parked_after();
	test_gunside_release_on_first_reloading_tick_holsters_both();
	test_parked_counts_as_held_for_dual_release();
	test_parked_counts_as_held_for_pairs();
	test_parked_counts_as_held_for_other_holster();

	test_park_at_once_grip_held();
	test_park_at_once_grip_released_same_tick();
	test_park_at_once_grip_open();
	test_park_at_once_untracked();
	test_park_at_once_before_zone_buzz();
	test_park_not_while_paused();
	test_park_not_for_single_gun();
	test_park_at_once_flip_refused();

	test_unpark_grip_held_through_reload_end();
	test_unpark_grip_open_at_reload_end_waits_for_press();
	test_unpark_same_tick_press_one_draw();
	test_unpark_grip_held_same_tick_gunside_release_is_rule_7();
	test_unpark_not_in_paused_tick();
	test_unpark_untracked_hand_uses_last_tracked_grip();

	test_gunside_release_holsters_parked_copy_while_reloading();
	test_gunside_release_holsters_parked_copy_mirrored();
	test_gunside_release_after_reload_holsters_parked_copy();
	test_parked_hand_with_grip_held_goes_home_with_gunside_release();
	test_same_tick_double_release_gunside_right();
	test_same_tick_double_release_gunside_left();
	test_same_tick_double_release_parked_earlier_gunside_right();
	test_same_tick_double_release_parked_earlier_gunside_left();
	test_gunside_release_with_parked_hand_untracked_holsters_both();
	test_gunside_release_and_parked_press_same_tick();
	test_free_hand_untracked_press_deferred_into_reload();
	test_pause_with_gunside_release_holsters_parked_copy();

	test_flip_gunside_both_hold();
	test_flip_gunside_refused_when_parked();
	test_flip_gunside_refused_one_hand_or_none();
	test_flip_then_free_right_hand_parks();

	test_pause_holsters_single_gun_and_keeps_gunside();
	test_pause_holsters_dual_and_keeps_gunside();
	test_pause_holsters_parked_gun_once();
	test_pause_grip_held_after_pause_is_not_a_press();
	test_pause_press_during_pause_does_not_draw();
	test_pause_zone_entered_during_pause_does_not_buzz_after();

	test_untracked_release_deferred_with_zone_of_return_tick();
	test_untracked_press_deferred();
	test_untracked_press_and_release_unseen();
	test_untracked_hand_during_pause();
	test_untracked_released_grip_frozen_through_pause();
	test_untracked_hand_does_not_block_other_hand();
	test_untracked_zone_frozen_then_buzz_on_return();

	test_zone_buzz_empty_hand_at_gun_edge_only();
	test_zone_buzz_not_for_empty_holster_or_chest();
	test_zone_buzz_pair_rules();
	test_zone_buzz_single_gun_at_other_holster();
	test_zone_buzz_single_gun_at_empty_other_holster();
	test_zone_buzz_not_for_dual_hand();
	test_zone_buzz_not_for_parked_hand();
	test_zone_buzz_after_gunside_release_holsters_both();
	test_zone_buzz_reloading_ignores_shoulder();
	test_zone_buzz_free_hand_none_while_reloading();
	test_zone_buzz_gunside_hand_while_reloading();
	test_zone_buzz_same_tick_as_press();

	test_fx_encoding_macros();
	test_fx_per_hand_no_cross_talk();

	test_order_release_before_press();
	test_order_presses_left_first();
	test_order_both_press_same_holster_no_pair();
	test_order_releases_left_first();
	test_order_zone_buzz_uses_start_of_tick_state();

	test_pickup_fills_hip_then_shoulder();
	test_pickup_shoulder_when_hip_full();
	test_pickup_weapon_zero();
	test_pickup_already_holstered();
	test_pickup_drawn_gun_keeps_its_holster();

	test_menupick_zero_changes_nothing();
	test_menupick_held_gun_changes_nothing();
	test_menupick_gun_in_other_holster_swaps();
	test_menupick_shoulder_held_hip_gun_swaps();
	test_menupick_new_gun_replaces_source_holster();
	test_menupick_dual_drops_non_gunside_hand();
	test_menupick_dual_drops_non_gunside_hand_left_side();
	test_menupick_parked_counts_as_held();
	test_menupick_empty_hands_fills_hip();
	test_menupick_empty_hands_already_holstered();
	test_menupick_empty_hands_empty_hip();

	test_weapon_empty_hands();
	test_weapon_single_and_dual();
	test_weapon_parked_counts_as_holding();
	test_weapon_formula_on_built_states();
	test_weapon_after_menu_pick();
	test_weapon_after_pickup();

	test_lost_zero_or_unholstered_changes_nothing();
	test_lost_holstered_gun_not_in_hand();
	test_lost_held_single_gun();
	test_lost_held_gun_released_at_other_holster_does_not_swap();
	test_lost_shoulder_gun_held_left();
	test_lost_held_pair();
	test_lost_parked_gun_clears_parked();
	test_lost_gun_in_both_holsters_empties_both();
	test_lost_then_pickup_refills();

	test_step_unheld_holster_empty_hands();
	test_step_unheld_holster_other_hand_holds_other_holster();
	test_step_single_held_gun();
	test_step_pair_from_gunside_hand();
	test_step_pair_from_other_hand();
	test_step_pair_other_hand_parked();
	test_step_acting_hand_parked();
	test_step_refused_other_hand_holds_holster();
	test_step_refused_same_or_other_holster_gun();
	test_step_refused_weapon_zero_and_bad_args();
	test_step_reloading_refused_for_held_gun();
	test_step_reloading_allowed_for_unheld_holster();
	test_step_never_makes_fx();

	test_pickhand_zone_new_gun_as_step();
	test_pickhand_zone_pair_other_copy_goes_back();
	test_pickhand_zone_refused_other_hand_holds_zone_holster();
	test_pickhand_zone_refused_gun_already_in_zone_holster();
	test_pickhand_zone_swap_hand_holds_zone_holster();
	test_pickhand_zone_swap_no_hand_holding();
	test_pickhand_zone_swap_refused_other_hand_holds_other_holster();
	test_pickhand_chest_is_pouch_slot();
	test_pickhand_invalid_zone_and_bad_hand();
	test_pickhand_nozone_held_replace();
	test_pickhand_nozone_held_swap();
	test_pickhand_nozone_pair();
	test_pickhand_nozone_empty_hand();
	test_pickhand_nozone_empty_hand_already_holstered();
	test_pickhand_held_gun_or_zero_refused();
	test_pickhand_reloading_refused_for_held_gun();
	test_pickhand_reloading_allowed_for_unheld_holster();
	test_pickhand_never_makes_pair();

	test_init_item_slots_zero();
	test_item_draw_other_hand_empty();
	test_item_draw_empty_slot_buzzes();
	test_item_draw_parks_single_gun();
	test_item_draw_other_item_goes_back();
	test_item_draw_same_item_buzzes_empty();
	test_item_draw_over_parked_gun_after_item_home();
	test_item_draw_free_hand_blocked_while_reloading();
	test_gun_draw_sends_item_back();
	test_gun_draw_empty_holster_item_stays();
	test_parked_behind_item_press_and_release_do_nothing();
	test_parked_behind_item_no_zone_buzz();
	test_parked_behind_item_counts_as_held();
	test_step_pick_refused_for_gun_parked_behind_item();
	test_item_release_anywhere_goes_home();
	test_gun_release_in_item_zone_goes_home();
	test_unpark_same_tick_as_item_release_grip_held();
	test_unpark_after_item_untracked_grip_held();
	test_unpark_after_item_grip_open_waits_for_press();
	test_unpark_press_same_tick_as_item_release();
	test_unpark_after_item_other_hand_holds_copy();
	test_weapon_rule42_built_states();
	test_pause_sends_item_and_parked_gun_home();
	test_pause_sends_lone_item_home();
	test_lost_item_with_parked_gun();
	test_lost_during_reload_park_unparks_nothing();
	test_lost_item_other_hand_not_parked();
	test_lost_item_slots();
	test_lost_gun_parked_behind_item();
	test_zone_buzz_item_zones();
	test_zone_buzz_item_zones_free_hand_reloading();
	test_step_item_slot_no_hand_holds();
	test_step_item_slot_gun_hand();
	test_step_item_slot_hand_holds_item();
	test_pickhand_item_zones_as_step();
	test_pickhand_nozone_item_hand_counts_as_empty();
	test_menupick_item_hand_counts_as_empty();
	test_pickup_never_fills_item_slots();
	test_setitem();
	test_items_invariants_random_play();
}
