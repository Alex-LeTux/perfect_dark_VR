#include <stdint.h>
#include <string.h>
#include "vrtest.h"
#include "game/vr_reload_core.h"

/*
 * Contract tests for Task G3, the Crossbow (plan 2026-10-15 Stage G, G3 note): "bolts one at a
 * time with no pump (shellbyshell, each bolt chambered as it goes in when the chamber is empty)".
 * The row sets shellbyshell and loadsoninsert, pumpaction 0. B does nothing; a grip at the
 * shoulder takes one bolt while the gun has room (clipsize minus chamber minus clip above 0) and
 * the reserve has rounds; an insert with the chamber empty chambers the bolt at once (VRR_READY,
 * the trigger allowed), with a bolt chambered it goes into the tube; a shot (seen through
 * vrrSyncFromEngineRow with pumpaction 0) chambers the next bolt from the tube; a drop takes
 * nothing; the rounds always add up; no rack is ever due and no rack marker ever glows.
 * Numbers follow the Crossbow: clipsize 5.
 */

#define XB_CAP 5
#define XB_RACKBITS (VRR_GLOW_RACK | VRR_HOT_RACK)

#define XB_CHECK_GUN(g, st, ch, cl) do { \
	CHECK_EQ((g).state, (st)); \
	CHECK_EQ((g).magin, 1); \
	CHECK_EQ((g).chamber, (ch)); \
	CHECK_EQ((g).clip, (cl)); \
} while (0)

/* ---------- helpers: every tick carries the Crossbow's flags ---------- */

static struct vrrinput xb_input(int32_t reserve)
{
	struct vrrinput in;
	memset(&in, 0, sizeof in);
	in.tracked = 1;
	in.clipsize = XB_CAP;
	in.reserve = reserve;
	in.shellbyshell = 1;
	in.loadsoninsert = 1;
	in.pumpaction = 0;
	return in;
}

/* Every tick: no rack glow, no rack event, never a rack due */
static struct vrroutput xb_run(struct vrrgun *g, const struct vrrinput *in)
{
	struct vrroutput out;
	memset(&out, 0x5a, sizeof out);
	vrrTick(g, in, &out);
	CHECK_EQ(out.glow & XB_RACKBITS, 0);
	CHECK_EQ(out.fx & VRR_FX_RACK, 0);
	CHECK(g->state != VRR_NEED_RACK);
	CHECK(g->state != VRR_RACKING);
	return out;
}

static struct vrroutput xb_quiet(struct vrrgun *g, int32_t reserve)
{
	struct vrrinput in = xb_input(reserve);
	return xb_run(g, &in);
}

static struct vrroutput xb_grab(struct vrrgun *g, int32_t reserve)
{
	struct vrrinput in = xb_input(reserve);
	in.grip = 1;
	in.atshoulder = 1;
	return xb_run(g, &in);
}

static struct vrroutput xb_release(struct vrrgun *g, int32_t inclip, int32_t reserve)
{
	struct vrrinput in = xb_input(reserve);
	in.inclip = inclip;
	return xb_run(g, &in);
}

/* One bolt taken at the shoulder and let go at the groove. Returns the release tick's output */
static struct vrroutput xb_load_bolt(struct vrrgun *g, int32_t reserve)
{
	struct vrroutput out = xb_grab(g, reserve);
	CHECK_EQ(out.fx & VRR_FX_GRAB_CLIP, VRR_FX_GRAB_CLIP);
	return xb_release(g, 1, reserve);
}

/* One shot as the adapter sees it: the engine's count drops by one, pumpaction 0 */
static void xb_fire(struct vrrgun *g)
{
	int32_t l = vrrEngineLoaded(g);
	CHECK(l > 0);
	CHECK_EQ(vrrAllowFire(g), 1);
	vrrSyncFromEngineRow(g, l - 1, 0);
}

/* ---------- tests ---------- */

static void test_xb_empty_loads_one_and_fires(void)
{
	struct vrrgun g;
	struct vrroutput out;
	int32_t reserve = 10;
	vrrInit(&g, 0);
	XB_CHECK_GUN(g, VRR_READY, 0, 0);
	out = xb_quiet(&g, reserve);
	CHECK_EQ(out.loaded, 0);

	out = xb_grab(&g, reserve);
	CHECK_EQ(out.fx & VRR_FX_GRAB_CLIP, VRR_FX_GRAB_CLIP);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_EQ(out.handclip, 1);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	CHECK_EQ(g.held, 1);
	CHECK_EQ(g.magin, 1);

	/* the bolt goes straight into the empty chamber: ready, no rack */
	out = xb_release(&g, 1, reserve);
	reserve += out.reservedelta;
	CHECK_EQ(out.fx & VRR_FX_INSERT, VRR_FX_INSERT);
	CHECK_EQ(out.fx & VRR_FX_DROP_CLIP, 0);
	CHECK_EQ(out.reservedelta, -1);
	CHECK_EQ(out.handclip, 0);
	CHECK_EQ(out.loaded, 1);
	CHECK_EQ(out.allowfire, 1);
	XB_CHECK_GUN(g, VRR_READY, 1, 0);
	CHECK_EQ(g.held, 0);
	CHECK_EQ(reserve, 9);

	/* the rest of the release does nothing more */
	out = xb_release(&g, 1, reserve);
	CHECK_EQ(out.reservedelta, 0);
	XB_CHECK_GUN(g, VRR_READY, 1, 0);

	/* it fires; dry and READY after */
	xb_fire(&g);
	XB_CHECK_GUN(g, VRR_READY, 0, 0);
	out = xb_quiet(&g, reserve);
	CHECK_EQ(out.loaded, 0);
	CHECK_EQ(out.allowfire, 1);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(out.reservedelta, 0);
}

/* Seated with the grip still held loads as a release does */
static void test_xb_seated_loads(void)
{
	struct vrrgun g;
	struct vrroutput out;
	struct vrrinput in = xb_input(10);
	vrrInit(&g, 0);
	xb_grab(&g, 10);
	in.grip = 1;
	in.inclip = 1;
	in.seated = 1;
	out = xb_run(&g, &in);
	CHECK_EQ(out.fx & VRR_FX_INSERT, VRR_FX_INSERT);
	CHECK_EQ(out.reservedelta, -1);
	CHECK_EQ(out.allowfire, 1);
	XB_CHECK_GUN(g, VRR_READY, 1, 0);
}

static void test_xb_load_to_capacity(void)
{
	struct vrrgun g;
	struct vrroutput out;
	int32_t reserve = 20, loads = 0;
	vrrInit(&g, 0);
	while (loads < 20) {
		out = xb_grab(&g, reserve);
		if (!(out.fx & VRR_FX_GRAB_CLIP)) {
			CHECK_EQ(out.fx & VRR_FX_NO_AMMO, 0);
			CHECK_EQ(out.handclip, 0);
			CHECK_EQ(g.held, 0);
			xb_release(&g, 0, reserve);
			break;
		}
		out = xb_release(&g, 1, reserve);
		reserve += out.reservedelta;
		loads++;
		CHECK_EQ(out.reservedelta, -1);
		/* the first bolt is chambered, the rest go into the tube */
		XB_CHECK_GUN(g, VRR_READY, 1, loads - 1);
		CHECK_EQ(out.allowfire, 1);
		CHECK_EQ(out.loaded, loads);
		CHECK_EQ(g.chamber + g.clip + reserve, 20);
	}
	CHECK_EQ(loads, XB_CAP);
	XB_CHECK_GUN(g, VRR_READY, 1, XB_CAP - 1);
	CHECK_EQ(reserve, 15);

	/* from a partly loaded restore (1 + 2): two more */
	vrrInit(&g, 3);
	XB_CHECK_GUN(g, VRR_READY, 1, 2);
	reserve = 20;
	loads = 0;
	while (loads < 20) {
		out = xb_grab(&g, reserve);
		if (!(out.fx & VRR_FX_GRAB_CLIP)) {
			xb_release(&g, 0, reserve);
			break;
		}
		out = xb_release(&g, 1, reserve);
		reserve += out.reservedelta;
		loads++;
	}
	CHECK_EQ(loads, 2);
	XB_CHECK_GUN(g, VRR_READY, 1, XB_CAP - 1);

	/* the reserve runs out first */
	vrrInit(&g, 0);
	reserve = 2;
	loads = 0;
	while (loads < 20) {
		out = xb_grab(&g, reserve);
		if (!(out.fx & VRR_FX_GRAB_CLIP)) {
			CHECK_EQ(out.fx & VRR_FX_NO_AMMO, VRR_FX_NO_AMMO);
			xb_release(&g, 0, reserve);
			break;
		}
		out = xb_release(&g, 1, reserve);
		reserve += out.reservedelta;
		loads++;
	}
	CHECK_EQ(loads, 2);
	CHECK_EQ(reserve, 0);
	XB_CHECK_GUN(g, VRR_READY, 1, 1);
}

static void test_xb_fire_with_tube(void)
{
	struct vrrgun g;
	struct vrroutput out;
	int32_t left;
	vrrInit(&g, XB_CAP);
	XB_CHECK_GUN(g, VRR_READY, 1, XB_CAP - 1);
	for (left = XB_CAP - 1; left >= 1; left--) {
		xb_fire(&g);
		/* the next bolt from the tube is chambered by the shot: no pump */
		XB_CHECK_GUN(g, VRR_READY, 1, left - 1);
		out = xb_quiet(&g, 10);
		CHECK_EQ(out.allowfire, 1);
		CHECK_EQ(out.loaded, left);
		CHECK_EQ(out.fx, 0);
		CHECK_EQ(out.reservedelta, 0);
	}
	XB_CHECK_GUN(g, VRR_READY, 1, 0);
}

static void test_xb_last_bolt_then_reload(void)
{
	struct vrrgun g;
	struct vrroutput out;
	int32_t reserve = 10;
	vrrInit(&g, 1);
	XB_CHECK_GUN(g, VRR_READY, 1, 0);
	xb_fire(&g);
	XB_CHECK_GUN(g, VRR_READY, 0, 0);
	out = xb_quiet(&g, reserve);
	CHECK_EQ(out.allowfire, 1);
	CHECK_EQ(out.loaded, 0);

	/* the next bolt chambers on insert and fires */
	out = xb_load_bolt(&g, reserve);
	reserve += out.reservedelta;
	CHECK_EQ(out.reservedelta, -1);
	XB_CHECK_GUN(g, VRR_READY, 1, 0);
	CHECK_EQ(out.allowfire, 1);
	CHECK_EQ(out.loaded, 1);
	/* a second one goes into the tube */
	out = xb_load_bolt(&g, reserve);
	reserve += out.reservedelta;
	XB_CHECK_GUN(g, VRR_READY, 1, 1);
	CHECK_EQ(out.loaded, 2);
	xb_fire(&g);
	XB_CHECK_GUN(g, VRR_READY, 1, 0);
	xb_fire(&g);
	XB_CHECK_GUN(g, VRR_READY, 0, 0);
	CHECK_EQ(reserve, 8);
}

/* Fired while holding a bolt: the shot chambers from the tube, or empties the gun and the
 * insert then chambers */
static void test_xb_fire_while_holding(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrrInit(&g, 2);
	xb_grab(&g, 10);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	xb_fire(&g);
	CHECK_EQ(g.chamber, 1);
	CHECK_EQ(g.clip, 0);
	CHECK_EQ(vrrAllowFire(&g), 1);
	xb_fire(&g);
	CHECK_EQ(g.chamber, 0);
	CHECK_EQ(g.clip, 0);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	out = xb_release(&g, 1, 10);
	CHECK_EQ(out.reservedelta, -1);
	CHECK_EQ(out.allowfire, 1);
	XB_CHECK_GUN(g, VRR_READY, 1, 0);
}

static void test_xb_drop_takes_nothing(void)
{
	struct vrrgun g;
	struct vrroutput out;
	int32_t k;
	for (k = 0; k <= 3; k++) {
		int32_t ch, cl;
		vrrInit(&g, k);
		ch = g.chamber;
		cl = g.clip;
		out = xb_grab(&g, 10);
		CHECK_EQ(g.state, VRR_HOLDING_CLIP);
		/* let go away from the groove */
		out = xb_release(&g, 0, 10);
		CHECK_EQ(out.fx & VRR_FX_DROP_CLIP, VRR_FX_DROP_CLIP);
		CHECK_EQ(out.fx & VRR_FX_INSERT, 0);
		CHECK_EQ(out.reservedelta, 0);
		CHECK_EQ(out.handclip, 0);
		XB_CHECK_GUN(g, VRR_READY, ch, cl);
		CHECK_EQ(g.held, 0);
		CHECK_EQ(out.allowfire, 1);
	}
	/* at the groove, but the reserve emptied meanwhile: it goes back, nothing chambered */
	vrrInit(&g, 0);
	xb_grab(&g, 1);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	out = xb_release(&g, 1, 0);
	CHECK_EQ(out.fx & VRR_FX_DROP_CLIP, VRR_FX_DROP_CLIP);
	CHECK_EQ(out.reservedelta, 0);
	XB_CHECK_GUN(g, VRR_READY, 0, 0);
}

static void test_xb_eject_does_nothing(void)
{
	struct vrrgun g, before;
	struct vrroutput out;
	int32_t k;
	for (k = 0; k < 4; k++) {
		struct vrrinput in = xb_input(10);
		vrrInit(&g, k == 3 ? 2 : k * 2);
		if (k == 3) {
			xb_grab(&g, 10);
			CHECK_EQ(g.state, VRR_HOLDING_CLIP);
		}
		before = g;
		CHECK_EQ(vrrEjectReturnsRow(&g, 1), 0);
		in.eject = 1;
		in.grip = k == 3;                   /* still holding the bolt */
		out = xb_run(&g, &in);
		CHECK_EQ(out.fx & VRR_FX_EJECT, 0);
		CHECK_EQ(out.reservedelta, 0);
		CHECK_EQ(g.magin, 1);
		CHECK_EQ(g.chamber, before.chamber);
		CHECK_EQ(g.clip, before.clip);
		CHECK_EQ(g.state, before.state);
	}
}

/* A rack grab and pull do nothing, and no rack marker glows in any state, the hand in the zone */
static void test_xb_no_rack(void)
{
	struct vrrgun g;
	struct vrroutput out;
	struct vrrinput in;
	int32_t k;
	for (k = 0; k < 4; k++) {
		int32_t rounds = k == 3 ? 2 : k * 2;
		vrrInit(&g, rounds);
		if (k == 3) {
			xb_grab(&g, 10);
		}
		in = xb_input(10);
		in.inrack = 1;
		out = xb_run(&g, &in);              /* hover */
		CHECK_EQ(out.fx & VRR_FX_ZONE_ENTER, 0);
		in.grip = 1;
		out = xb_run(&g, &in);              /* grab */
		in.rackpull = 9.0f;
		out = xb_run(&g, &in);              /* full pull */
		CHECK_EQ(out.reservedelta, 0);
		CHECK_EQ(out.fx & VRR_FX_WASTE_ROUND, 0);
		in.grip = 0;
		in.rackpull = 0.0f;
		out = xb_run(&g, &in);              /* let go */
		CHECK_EQ(g.magin, 1);
		CHECK_EQ(g.chamber + g.clip, rounds);
	}
	/* settled from a clip-out restore: the tube goes back in, chambered or dry, never a rack due */
	vrtest_restored_clip_out(&g, 3);
	out = xb_quiet(&g, 10);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_EQ(g.magin, 1);
	CHECK_EQ(g.state, VRR_READY);
	vrtest_restored_clip_out(&g, 0);
	out = xb_quiet(&g, 10);
	XB_CHECK_GUN(g, VRR_READY, 0, 0);
	out = xb_load_bolt(&g, 10);
	XB_CHECK_GUN(g, VRR_READY, 1, 0);
}

static uint32_t xb_seed;

static int xb_rand(int n)
{
	xb_seed = xb_seed * 1103515245u + 12345u;
	return (int)(((xb_seed >> 16) & 0x7fff) % (uint32_t)n);
}

static void test_xb_random_play(void)
{
	int run;
	int32_t shots_all = 0, inserts_all = 0, chambered_all = 0;
	xb_seed = 31337;
	for (run = 0; run < 300; run++) {
		struct vrrgun g;
		int32_t reserve = xb_rand(30);
		int32_t total, shots = 0;
		int32_t grip = 0;
		int n, fails = 0;
		vrrInit(&g, xb_rand(XB_CAP + 1));
		total = g.chamber + g.clip + reserve;
		for (n = 0; n < 400 && fails < 3; n++) {
			struct vrrinput in = xb_input(reserve);
			struct vrroutput out;
			struct vrrgun before;
			int32_t bad = 0;
			if (xb_rand(4) == 0) {
				grip = !grip;
			}
			in.grip = grip;
			in.eject = xb_rand(10) == 0;
			in.inclip = xb_rand(3) == 0;
			in.inrack = xb_rand(3) == 0;
			in.atshoulder = xb_rand(3) == 0;
			in.seated = xb_rand(5) == 0;
			in.slap = xb_rand(8) == 0;
			in.rackpull = (float)xb_rand(90) / 10.0f;
			in.racktravel = (float)(xb_rand(3) * 3);
			in.tracked = xb_rand(12) != 0;
			in.paused = xb_rand(15) == 0;
			if (xb_rand(4) == 0 && vrrAllowFire(&g)) {
				int32_t l = vrrEngineLoaded(&g);
				if (l > 0) {
					int32_t pc = g.chamber, pl = g.clip;
					vrrSyncFromEngineRow(&g, l - 1, 0);
					shots++;
					shots_all++;
					if (g.chamber + g.clip != pc + pl - 1 || (pl > 0 && g.chamber != 1)) {
						bad = 1;
					}
				}
			}
			/* the adapter's every-tick sync with the engine's own count changes nothing */
			before = g;
			vrrSyncFromEngineRow(&g, vrrEngineLoaded(&g), 0);
			if (memcmp(&before, &g, sizeof g) != 0) {
				bad = 1;
			}
			out = xb_run(&g, &in);
			reserve += out.reservedelta;
			if (out.fx & VRR_FX_INSERT) {
				inserts_all++;
				if (!before.chamber) {
					chambered_all++;
				}
				/* into the empty chamber, else into the tube */
				if (out.reservedelta != -1 || g.chamber != 1
						|| g.clip != before.clip + (before.chamber ? 1 : 0)) {
					bad = 1;
				}
			} else if (out.reservedelta != 0) {
				bad = 1;
			}
			if ((out.fx & VRR_FX_GRAB_CLIP)
					&& (XB_CAP - before.chamber - before.clip <= 0 || in.reserve <= 0)) {
				bad = 1;
			}
			if ((out.fx & (VRR_FX_EJECT | VRR_FX_RACK | VRR_FX_WASTE_ROUND))
					|| (out.glow & XB_RACKBITS)) {
				bad = 1;
			}
			if (bad || reserve < 0 || !g.magin
					|| g.state == VRR_CLIP_OUT || g.state == VRR_NEED_RACK
					|| g.state == VRR_RACKING
					|| (!g.chamber && g.clip)
					|| g.held != (g.state == VRR_HOLDING_CLIP)
					|| g.chamber + g.clip > XB_CAP
					|| g.chamber + g.clip + reserve + shots != total
					|| out.loaded != vrrEngineLoaded(&g)
					|| out.allowfire != vrrAllowFire(&g)
					|| (g.state == VRR_READY && !out.allowfire)
					|| out.handclip != (g.state == VRR_HOLDING_CLIP)) {
				fails++;
				printf("  xb random run %d tick %d: in eject %d grip %d shoulder %d inclip %d "
					"inrack %d seated %d tracked %d paused %d reserve %d | before state %d "
					"ch %d clip %d held %d | after state %d magin %d ch %d clip %d held %d "
					"fx %#x glow %#x delta %d | total %d shots %d\n",
					run, n, in.eject, in.grip, in.atshoulder, in.inclip, in.inrack, in.seated,
					in.tracked, in.paused, in.reserve, before.state, before.chamber,
					before.clip, before.held, g.state, g.magin, g.chamber, g.clip, g.held,
					out.fx, out.glow, out.reservedelta, total, shots);
			}
			CHECK_EQ(bad, 0);
			CHECK(reserve >= 0);
			CHECK_EQ(g.chamber + g.clip + reserve + shots, total);
			CHECK(g.chamber + g.clip <= XB_CAP);
			CHECK(g.chamber || !g.clip);
			CHECK_EQ(out.loaded, vrrEngineLoaded(&g));
			CHECK_EQ(out.allowfire, vrrAllowFire(&g));
		}
	}
	CHECK(shots_all > 300);
	CHECK(inserts_all > 300);
	CHECK(chambered_all > 50);
}

void run_reload_crossbow_tests(void)
{
	test_xb_empty_loads_one_and_fires();
	test_xb_seated_loads();
	test_xb_load_to_capacity();
	test_xb_fire_with_tube();
	test_xb_last_bolt_then_reload();
	test_xb_fire_while_holding();
	test_xb_drop_takes_nothing();
	test_xb_eject_does_nothing();
	test_xb_no_rack();
	test_xb_random_play();
}
