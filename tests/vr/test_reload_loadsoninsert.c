#include <stdint.h>
#include <string.h>
#include "vrtest.h"
#include "game/vr_reload_core.h"

/*
 * Contract tests for Task E2a: a gun that loads on insert (vrrinput.loadsoninsert = 1).
 * Rule: an insert into a gun with an empty chamber chambers a round at once (VRR_READY, never
 * VRR_NEED_RACK); nothing ever asks for a rack (no rack glow, a rack input is ignored); a
 * flagged gun with a clip in and an empty chamber (fired dry, or an empty clip inserted) is
 * VRR_READY and can fire (clicks dry); the eject returns the chambered round with the clip
 * (Task E2e, since Task F4a for every gun). Without the flag, unchanged.
 * Numbers follow the Falcon: clipsize 8 (chamber 1 + clip 7).
 */

#define LI_CHECK_GUN(g, st, mi, ch, cl) do { \
	CHECK_EQ((g).state, (st)); \
	CHECK_EQ((g).magin, (mi)); \
	CHECK_EQ((g).chamber, (ch)); \
	CHECK_EQ((g).clip, (cl)); \
} while (0)

#define RACK_BITS (VRR_GLOW_RACK | VRR_HOT_RACK)

/* ---------- helpers: every tick carries the flag given ---------- */

static struct vrrinput li_input(int32_t flag)
{
	struct vrrinput in;
	memset(&in, 0, sizeof in);
	in.tracked = 1;
	in.clipsize = 8;
	in.reserve = 20;
	in.loadsoninsert = flag;
	return in;
}

static struct vrroutput li_run(struct vrrgun *g, const struct vrrinput *in)
{
	struct vrroutput out;
	memset(&out, 0x5a, sizeof out);
	vrrTick(g, in, &out);
	return out;
}

static struct vrroutput li_quiet(struct vrrgun *g, int32_t flag)
{
	struct vrrinput in = li_input(flag);
	return li_run(g, &in);
}

static struct vrroutput li_eject(struct vrrgun *g, int32_t flag)
{
	struct vrrinput in = li_input(flag);
	in.eject = 1;
	return li_run(g, &in);
}

static struct vrroutput li_grab(struct vrrgun *g, int32_t flag, int32_t reserve)
{
	struct vrrinput in = li_input(flag);
	in.grip = 1;
	in.atshoulder = 1;
	in.reserve = reserve;
	return li_run(g, &in);
}

static struct vrroutput li_hold(struct vrrgun *g, int32_t flag, int32_t inclip)
{
	struct vrrinput in = li_input(flag);
	in.grip = 1;
	in.inclip = inclip;
	return li_run(g, &in);
}

static struct vrroutput li_release(struct vrrgun *g, int32_t flag, int32_t inclip)
{
	struct vrrinput in = li_input(flag);
	in.inclip = inclip;
	return li_run(g, &in);
}

static struct vrroutput li_seat(struct vrrgun *g, int32_t flag)
{
	struct vrrinput in = li_input(flag);
	in.grip = 1;
	in.inclip = 1;
	in.seated = 1;
	return li_run(g, &in);
}

static struct vrroutput li_rack(struct vrrgun *g, int32_t flag, float pull, float travel)
{
	struct vrrinput in = li_input(flag);
	in.grip = 1;
	in.inrack = 1;
	in.rackpull = pull;
	in.racktravel = travel;
	return li_run(g, &in);
}

static struct vrroutput li_rack_release(struct vrrgun *g, int32_t flag)
{
	struct vrrinput in = li_input(flag);
	in.inrack = 1;
	return li_run(g, &in);
}

/* NEED_RACK reached with the flag off: chamber 0, clip 8, grip released */
static void make_need_rack_li(struct vrrgun *g)
{
	vrrInit(g, 0);
	li_eject(g, 0);
	li_grab(g, 0, 20);
	li_release(g, 0, 1);
}

/* ---------- insert into an empty chamber ---------- */

static void test_li_empty_gun_insert_ready_and_fires(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrrInit(&g, 0);

	out = li_eject(&g, 1);
	LI_CHECK_GUN(g, VRR_CLIP_OUT, 0, 0, 0);
	CHECK_EQ(out.allowfire, 0);
	CHECK_EQ(out.glow & RACK_BITS, 0);

	out = li_grab(&g, 1, 20);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	CHECK_EQ(g.held, 8);

	out = li_release(&g, 1, 1);
	CHECK_EQ(out.fx & VRR_FX_INSERT, VRR_FX_INSERT);
	CHECK_EQ(out.fx & (VRR_FX_RACK | VRR_FX_WASTE_ROUND), 0);
	CHECK_EQ(out.reservedelta, -8);
	LI_CHECK_GUN(g, VRR_READY, 1, 1, 7);
	CHECK_EQ(out.loaded, 8);
	CHECK_EQ(vrrEngineLoaded(&g), 8);
	CHECK_EQ(out.allowfire, 1);
	CHECK_EQ(vrrAllowFire(&g), 1);
	CHECK_EQ(out.glow, 0);
	CHECK_EQ(out.handclip, 0);

	/* the engine fires one round */
	vrrSyncFromEngineRow(&g, 7, 0);
	out = li_quiet(&g, 1);
	LI_CHECK_GUN(g, VRR_READY, 1, 1, 6);
	CHECK_EQ(out.loaded, 7);
	CHECK_EQ(out.allowfire, 1);
}

static void test_li_seated_insert_ready(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrrInit(&g, 0);
	li_eject(&g, 1);
	li_grab(&g, 1, 20);
	out = li_seat(&g, 1);
	CHECK_EQ(out.fx & VRR_FX_INSERT, VRR_FX_INSERT);
	CHECK_EQ(out.reservedelta, -8);
	LI_CHECK_GUN(g, VRR_READY, 1, 1, 7);
	CHECK_EQ(out.loaded, 8);
	CHECK_EQ(out.allowfire, 1);
	CHECK_EQ(out.glow & RACK_BITS, 0);
}

static void test_li_partial_clip(void)
{
	struct vrrgun g;
	struct vrroutput out;

	vrrInit(&g, 0);
	li_eject(&g, 1);
	li_grab(&g, 1, 5);
	CHECK_EQ(g.held, 5);
	out = li_release(&g, 1, 1);
	CHECK_EQ(out.reservedelta, -5);
	LI_CHECK_GUN(g, VRR_READY, 1, 1, 4);
	CHECK_EQ(out.loaded, 5);
	CHECK_EQ(out.allowfire, 1);
	CHECK_EQ(out.glow, 0);

	/* one round: it goes to the chamber, the clip is empty */
	vrrInit(&g, 0);
	li_eject(&g, 1);
	li_grab(&g, 1, 1);
	CHECK_EQ(g.held, 1);
	out = li_release(&g, 1, 1);
	CHECK_EQ(out.reservedelta, -1);
	LI_CHECK_GUN(g, VRR_READY, 1, 1, 0);
	CHECK_EQ(out.loaded, 1);
	CHECK_EQ(out.allowfire, 1);
}

/* The only way through the API to hold a clip with no rounds is a clipsize of 0. */
static void test_li_empty_clip_insert_ready_dry(void)
{
	struct vrrgun g;
	struct vrrinput in;
	struct vrroutput out;
	vrrInit(&g, 0);
	li_eject(&g, 1);

	in = li_input(1);
	in.clipsize = 0;
	in.grip = 1;
	in.atshoulder = 1;
	li_run(&g, &in);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	CHECK_EQ(g.held, 0);

	in = li_input(1);
	in.clipsize = 0;
	in.inclip = 1;
	out = li_run(&g, &in);
	CHECK_EQ(out.fx & VRR_FX_INSERT, VRR_FX_INSERT);
	CHECK_EQ(out.reservedelta, 0);
	LI_CHECK_GUN(g, VRR_READY, 1, 0, 0);
	CHECK_EQ(out.loaded, 0);
	CHECK_EQ(out.allowfire, 1);
	CHECK_EQ(out.glow, 0);
}

static void test_li_fired_dry_is_ready(void)
{
	struct vrrgun g;
	struct vrroutput out;
	int i;
	vrrInit(&g, 8);
	vrrSyncFromEngineRow(&g, 0, 0);
	for (i = 0; i < 3; i++) {
		out = li_quiet(&g, 1);
		LI_CHECK_GUN(g, VRR_READY, 1, 0, 0);
		CHECK_EQ(out.loaded, 0);
		CHECK_EQ(out.allowfire, 1);
		CHECK_EQ(out.glow, 0);
		CHECK_EQ(out.fx, 0);
	}

	/* the next reload: an eject and a new clip, chambered by the insert */
	out = li_eject(&g, 1);
	CHECK_EQ(out.reservedelta, 0);
	LI_CHECK_GUN(g, VRR_CLIP_OUT, 0, 0, 0);
	CHECK_EQ(out.glow, VRR_GLOW_CLIP);
	CHECK_EQ(out.allowfire, 0);
	li_grab(&g, 1, 20);
	out = li_release(&g, 1, 1);
	LI_CHECK_GUN(g, VRR_READY, 1, 1, 7);
	CHECK_EQ(out.loaded, 8);
}

/* ---------- eject (Task E2e): with the flag the eject empties the gun ---------- */

/* With the flag, the eject returns every loaded round (chamber + clip) to the reserve: chamber 0,
 * clip 0, CLIP_OUT, no fire until a new clip goes in. Since Task F4a the same without the flag. */
static void test_li_eject_loaded_returns_all_rounds(void)
{
	struct vrrgun g;
	struct vrroutput out;
	int32_t loaded;

	for (loaded = 1; loaded <= 8; loaded++) {
		vrrInit(&g, 8);
		vrrSyncFromEngineRow(&g, loaded, 0);
		out = li_eject(&g, 1);
		CHECK_EQ(out.fx & VRR_FX_EJECT, VRR_FX_EJECT);
		CHECK_EQ(out.reservedelta, loaded);
		LI_CHECK_GUN(g, VRR_CLIP_OUT, 0, 0, 0);
		CHECK_EQ(out.loaded, 0);
		CHECK_EQ(vrrEngineLoaded(&g), 0);
		CHECK_EQ(out.allowfire, 0);
		CHECK_EQ(vrrAllowFire(&g), 0);
		CHECK_EQ(out.glow, VRR_GLOW_CLIP);
	}

	/* fire stays refused with the clip out and with the new clip in the hand */
	vrrInit(&g, 5);
	li_eject(&g, 1);
	out = li_quiet(&g, 1);
	CHECK_EQ(g.state, VRR_CLIP_OUT);
	CHECK_EQ(out.allowfire, 0);
	out = li_grab(&g, 1, 20);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	CHECK_EQ(out.allowfire, 0);
	CHECK_EQ(vrrAllowFire(&g), 0);
	out = li_hold(&g, 1, 1);
	CHECK_EQ(out.allowfire, 0);

	/* the next insert loads and fires: a full clip, chamber 1, clip 7 */
	out = li_release(&g, 1, 1);
	CHECK_EQ(out.reservedelta, -8);
	LI_CHECK_GUN(g, VRR_READY, 1, 1, 7);
	CHECK_EQ(out.loaded, 8);
	CHECK_EQ(out.allowfire, 1);
	vrrSyncFromEngineRow(&g, 7, 0);
	out = li_quiet(&g, 1);
	LI_CHECK_GUN(g, VRR_READY, 1, 1, 6);
	CHECK_EQ(out.loaded, 7);

	/* a seated insert after the eject does the same */
	vrrInit(&g, 3);
	li_eject(&g, 1);
	li_grab(&g, 1, 20);
	out = li_seat(&g, 1);
	CHECK_EQ(out.reservedelta, -8);
	LI_CHECK_GUN(g, VRR_READY, 1, 1, 7);

	/* rounds add up across eject and reload: 5 in the gun + 20 reserve */
	{
		int32_t reserve = 20;
		vrrInit(&g, 5);
		out = li_eject(&g, 1);
		reserve += out.reservedelta;
		CHECK_EQ(reserve, 25);
		CHECK_EQ(g.chamber + g.clip + reserve, 25);
		li_grab(&g, 1, reserve);
		out = li_release(&g, 1, 1);
		reserve += out.reservedelta;
		CHECK_EQ(reserve, 17);
		CHECK_EQ(g.chamber + g.clip + reserve, 25);
	}

	/* flag 0 (Task F4a): the eject empties the gun as well, and the new clip needs a rack */
	vrrInit(&g, 8);
	vrrSyncFromEngineRow(&g, 5, 0);
	out = li_eject(&g, 0);
	CHECK_EQ(out.reservedelta, 5);
	LI_CHECK_GUN(g, VRR_CLIP_OUT, 0, 0, 0);
	CHECK_EQ(out.loaded, 0);
	CHECK_EQ(out.allowfire, 0);
	CHECK_EQ(out.glow, VRR_GLOW_CLIP);
	li_grab(&g, 0, 20);
	out = li_release(&g, 0, 1);
	CHECK_EQ(out.reservedelta, -8);
	LI_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
	CHECK_EQ(out.loaded, 0);
}

/* Eject with the given flag; checks the reserve change equals vrrEjectReturns taken just before */
static void check_eject_matches_returns(struct vrrgun *g, int32_t flag, int32_t want)
{
	struct vrroutput out;
	int32_t r = vrrEjectReturnsRow(g, 0);
	CHECK_EQ(r, want);
	out = li_eject(g, flag);
	CHECK_EQ(out.reservedelta, r);
}

static void test_li_eject_returns_helper(void)
{
	struct vrrgun g;
	int32_t flag, loaded;

	for (flag = 0; flag <= 1; flag++) {
		/* clip out: 0, chambered (a restored state) or not */
		vrtest_restored_clip_out(&g, 5);
		CHECK_EQ(g.chamber, 1);
		check_eject_matches_returns(&g, flag, 0);
		vrrInit(&g, 0);
		li_eject(&g, 0);
		check_eject_matches_returns(&g, flag, 0);

		/* clip in the hand: 0 */
		vrrInit(&g, 5);
		li_eject(&g, 0);
		li_grab(&g, flag, 20);
		CHECK_EQ(g.state, VRR_HOLDING_CLIP);
		CHECK_EQ(vrrEjectReturnsRow(&g, 0), 0);

		/* mid-rack (entered with the flag off): 0 */
		vrrInit(&g, 8);
		li_rack(&g, 0, 0.0f, 0.0f);
		CHECK_EQ(g.state, VRR_RACKING);
		CHECK_EQ(vrrEjectReturnsRow(&g, 0), 0);
		make_need_rack_li(&g);
		li_rack(&g, 0, 0.0f, 0.0f);
		CHECK_EQ(g.state, VRR_RACKING);
		CHECK_EQ(g.clip, 8);
		CHECK_EQ(vrrEjectReturnsRow(&g, 0), 0);
		check_eject_matches_returns(&g, flag, 0);

		/* clip in, every load: the clip plus the chambered round, with the flag or without
		 * (Task F4a) */
		for (loaded = 0; loaded <= 8; loaded++) {
			int32_t clip, ch;
			vrrInit(&g, 8);
			vrrSyncFromEngineRow(&g, loaded, 0);
			clip = g.clip;
			ch = g.chamber;
			check_eject_matches_returns(&g, flag, clip + ch);
		}

		/* clip in, chamber empty, clip full (NEED_RACK, flag off): the clip only, either way */
		make_need_rack_li(&g);
		check_eject_matches_returns(&g, flag, 8);
	}

	/* the helper does not change the gun */
	{
		struct vrrgun before;
		vrrInit(&g, 6);
		before = g;
		vrrEjectReturnsRow(&g, 0);
		CHECK(memcmp(&before, &g, sizeof g) == 0);
	}
}

/* A shoulder grab on the eject's tick: HOLDING_CLIP, still empty, no fire */
static void test_li_eject_and_grab_same_tick(void)
{
	struct vrrgun g;
	struct vrrinput in;
	struct vrroutput out;

	vrrInit(&g, 5);
	in = li_input(1);
	in.eject = 1;
	in.grip = 1;
	in.atshoulder = 1;
	out = li_run(&g, &in);
	CHECK_EQ(out.fx & (VRR_FX_EJECT | VRR_FX_GRAB_CLIP), VRR_FX_EJECT | VRR_FX_GRAB_CLIP);
	CHECK_EQ(out.reservedelta, 5);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	CHECK_EQ(g.magin, 0);
	CHECK_EQ(g.chamber, 0);
	CHECK_EQ(g.clip, 0);
	CHECK_EQ(out.loaded, 0);
	CHECK_EQ(out.allowfire, 0);
	CHECK_EQ(out.handclip, 1);

	out = li_release(&g, 1, 1);
	LI_CHECK_GUN(g, VRR_READY, 1, 1, 7);
	CHECK_EQ(out.allowfire, 1);
}

/* A flagged gun out of the clip with a round still chambered (only from a settle, e.g. a save or a
 * rack under way that ended at once): a held clip with that round fired, then inserted, chambers one */
static void test_li_chambered_clip_out_from_settle_then_insert(void)
{
	struct vrrgun g;
	struct vrroutput out;

	vrrInit(&g, 5);
	g.magin = 0;
	g.clip = 0;
	vrrSettle(&g, 1);
	LI_CHECK_GUN(g, VRR_CLIP_OUT, 0, 1, 0);
	CHECK_EQ(vrrAllowFire(&g), 1);
	li_grab(&g, 1, 20);
	vrrSyncFromEngineRow(&g, 0, 0);
	out = li_hold(&g, 1, 1);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	CHECK_EQ(g.chamber, 0);
	CHECK_EQ(out.glow & RACK_BITS, 0);
	out = li_release(&g, 1, 1);
	CHECK_EQ(out.reservedelta, -8);
	LI_CHECK_GUN(g, VRR_READY, 1, 1, 7);
	CHECK_EQ(out.loaded, 8);

	/* same with the round chambered and not fired (a restored state: an eject empties the chamber
	 * since Task F4a): room clipsize - 1 */
	vrrInit(&g, 5);
	g.magin = 0;
	g.clip = 0;
	vrrSettle(&g, 1);
	li_grab(&g, 1, 20);
	out = li_release(&g, 1, 1);
	CHECK_EQ(out.reservedelta, -7);
	LI_CHECK_GUN(g, VRR_READY, 1, 1, 7);
}

/* ---------- the rack never glows, a rack input is ignored ---------- */

static void test_li_no_rack_glow_any_state(void)
{
	struct vrrgun g;
	struct vrrinput in;
	struct vrroutput out;
	int32_t st, ch, inrack;

	/* states reached through ticks, hand in the rack zone and out of it */
	for (inrack = 0; inrack <= 1; inrack++) {
		vrrInit(&g, 8);
		in = li_input(1);
		in.inrack = inrack;
		out = li_run(&g, &in);
		CHECK_EQ(out.glow & RACK_BITS, 0);

		vrrInit(&g, 0);
		in = li_input(1);
		in.inrack = inrack;
		out = li_run(&g, &in);
		CHECK_EQ(g.state, VRR_READY);
		CHECK_EQ(out.glow & RACK_BITS, 0);

		vrrInit(&g, 0);
		li_eject(&g, 1);
		in = li_input(1);
		in.inrack = inrack;
		out = li_run(&g, &in);
		CHECK_EQ(g.state, VRR_CLIP_OUT);
		CHECK_EQ(g.chamber, 0);
		CHECK_EQ(out.glow, VRR_GLOW_CLIP);

		/* flagged eject of a loaded gun: chamber emptied (Task E2e), no rack glow */
		vrrInit(&g, 5);
		li_eject(&g, 1);
		in = li_input(1);
		in.inrack = inrack;
		out = li_run(&g, &in);
		CHECK_EQ(g.state, VRR_CLIP_OUT);
		CHECK_EQ(g.chamber, 0);
		CHECK_EQ(out.glow, VRR_GLOW_CLIP);

		/* clip out with a round chambered (a restored state) */
		vrtest_restored_clip_out(&g, 5);
		in = li_input(1);
		in.inrack = inrack;
		out = li_run(&g, &in);
		CHECK_EQ(g.state, VRR_CLIP_OUT);
		CHECK_EQ(g.chamber, 1);
		CHECK_EQ(out.glow, VRR_GLOW_CLIP);

		vrrInit(&g, 0);
		li_eject(&g, 1);
		li_grab(&g, 1, 20);
		in = li_input(1);
		in.grip = 1;
		in.inrack = inrack;
		out = li_run(&g, &in);
		CHECK_EQ(g.state, VRR_HOLDING_CLIP);
		CHECK_EQ(out.glow & RACK_BITS, 0);
	}

	/* Robustness check only: the rule says NEED_RACK and RACKING do not occur with the flag.
	 * Every state, forced, both chamber values, tracked or not: still no rack glow */
	for (st = VRR_READY; st <= VRR_RACKING; st++) {
		for (ch = 0; ch <= 1; ch++) {
			int32_t tr;
			for (tr = 0; tr <= 1; tr++) {
				vrrInit(&g, 8);
				g.state = st;
				g.chamber = ch;
				g.magin = st == VRR_CLIP_OUT || st == VRR_HOLDING_CLIP ? 0 : 1;
				if (!g.magin) {
					g.clip = 0;
				}
				in = li_input(1);
				in.inrack = 1;
				in.tracked = tr;
				out = li_run(&g, &in);
				CHECK_EQ(out.glow & RACK_BITS, 0);
				CHECK_EQ(out.fx & VRR_FX_ZONE_ENTER, 0);
			}
		}
	}
}

static void check_rack_ignored(struct vrrgun *g, int32_t loaded, int32_t eject_first)
{
	struct vrrgun before;
	struct vrroutput out;
	float pulls[] = { 0.0f, 1.0f, 3.0f, 6.0f, 10.0f };
	float travels[] = { 0.0f, 3.0f, 6.0f };
	int i, t;

	for (t = 0; t < 3; t++) {
		if (eject_first) {
			/* clip out, chamber as loaded left it (a restored state: an eject empties it) */
			vrtest_restored_clip_out(g, loaded);
		} else {
			vrrInit(g, loaded);
		}
		li_quiet(g, 1);
		before = *g;
		for (i = 0; i < 5; i++) {
			out = li_rack(g, 1, pulls[i], travels[t]);
			CHECK(g->state != VRR_RACKING);
			CHECK_EQ(g->state, before.state);
			CHECK_EQ(g->chamber, before.chamber);
			CHECK_EQ(g->clip, before.clip);
			CHECK_EQ(g->magin, before.magin);
			CHECK_EQ(out.fx & (VRR_FX_RACK | VRR_FX_WASTE_ROUND | VRR_FX_ZONE_ENTER), 0);
			CHECK_EQ(out.glow & RACK_BITS, 0);
			CHECK_EQ(out.reservedelta, 0);
		}
		out = li_rack_release(g, 1);
		CHECK_EQ(g->state, before.state);
		CHECK_EQ(g->chamber, before.chamber);
		CHECK_EQ(g->clip, before.clip);
		CHECK_EQ(out.fx, 0);
	}
}

static void test_li_rack_input_ignored(void)
{
	struct vrrgun g;
	struct vrrinput in;
	struct vrroutput out;

	check_rack_ignored(&g, 8, 0); /* READY, loaded: a rack would waste the chambered round */
	check_rack_ignored(&g, 0, 0); /* READY, fired dry */
	check_rack_ignored(&g, 5, 1); /* CLIP_OUT, round chambered */
	check_rack_ignored(&g, 0, 1); /* CLIP_OUT, chamber empty */

	/* Robustness check only: the rule says NEED_RACK does not occur with the flag. Forced
	 * NEED_RACK: a rack grab still does not rack */
	vrrInit(&g, 0);
	g.clip = 7;
	g.state = VRR_NEED_RACK;
	out = li_rack(&g, 1, 3.0f, 0.0f);
	CHECK(g.state != VRR_RACKING);
	CHECK_EQ(out.fx & (VRR_FX_RACK | VRR_FX_WASTE_ROUND), 0);

	/* rack zone and shoulder together with the clip out: the rack is ignored, the grab happens */
	vrrInit(&g, 5);
	li_eject(&g, 1);
	in = li_input(1);
	in.grip = 1;
	in.inrack = 1;
	in.atshoulder = 1;
	out = li_run(&g, &in);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	CHECK_EQ(out.fx & VRR_FX_GRAB_CLIP, VRR_FX_GRAB_CLIP);
	CHECK_EQ(g.held, 8);

	/* an eject while gripping in the rack zone is not blocked (the gun is never racking) */
	vrrInit(&g, 8);
	li_rack(&g, 1, 0.0f, 0.0f);
	in = li_input(1);
	in.grip = 1;
	in.inrack = 1;
	in.eject = 1;
	out = li_run(&g, &in);
	CHECK_EQ(out.fx & VRR_FX_EJECT, VRR_FX_EJECT);
	CHECK_EQ(out.reservedelta, 8);
	LI_CHECK_GUN(g, VRR_CLIP_OUT, 0, 0, 0);
}

/* ---------- vrrSettle ---------- */

/* A gun with dirty hold and grip fields, as a save or a draw might leave it */
static void dirty_gun(struct vrrgun *g, int32_t state, int32_t magin, int32_t chamber, int32_t clip)
{
	vrrInit(g, 8);
	g->state = state;
	g->magin = magin;
	g->chamber = chamber;
	g->clip = clip;
	g->held = 6;
	g->racked = 1;
	g->prevgrip = 1;
	g->prevhot = VRR_HOT_CLIP | VRR_HOT_RACK;
}

#define CHECK_SETTLE_CLEARED(g) do { \
	CHECK_EQ((g).held, 0); \
	CHECK_EQ((g).racked, 0); \
	CHECK_EQ((g).prevgrip, 0); \
	CHECK_EQ((g).prevhot, 0); \
} while (0)

static void test_li_settle(void)
{
	struct vrrgun g;
	int32_t flag, st;

	for (flag = 0; flag <= 1; flag++) {
		/* clip out: CLIP_OUT, chamber as it was, from any state */
		for (st = VRR_READY; st <= VRR_RACKING; st++) {
			dirty_gun(&g, st, 0, 1, 0);
			vrrSettle(&g, flag);
			LI_CHECK_GUN(g, VRR_CLIP_OUT, 0, 1, 0);
			CHECK_SETTLE_CLEARED(g);

			dirty_gun(&g, st, 0, 0, 0);
			vrrSettle(&g, flag);
			LI_CHECK_GUN(g, VRR_CLIP_OUT, 0, 0, 0);
			CHECK_SETTLE_CLEARED(g);
		}

		/* clip in, chambered: READY, counts untouched */
		dirty_gun(&g, VRR_HOLDING_CLIP, 1, 1, 5);
		vrrSettle(&g, flag);
		LI_CHECK_GUN(g, VRR_READY, 1, 1, 5);
		CHECK_SETTLE_CLEARED(g);
		CHECK_EQ(vrrAllowFire(&g), 1);
	}

	/* clip in, empty chamber, no flag: NEED_RACK, nothing chambered */
	dirty_gun(&g, VRR_READY, 1, 0, 7);
	vrrSettle(&g, 0);
	LI_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 7);
	CHECK_SETTLE_CLEARED(g);
	CHECK_EQ(vrrAllowFire(&g), 0);

	/* clip in, empty chamber, no flag, empty clip: READY and dry (c032e6c3e) */
	dirty_gun(&g, VRR_NEED_RACK, 1, 0, 0);
	vrrSettle(&g, 0);
	LI_CHECK_GUN(g, VRR_READY, 1, 0, 0);
	CHECK_EQ(vrrAllowFire(&g), 1);

	/* with the flag and rounds in the clip: chambers one, READY, rounds conserved */
	dirty_gun(&g, VRR_NEED_RACK, 1, 0, 7);
	vrrSettle(&g, 1);
	LI_CHECK_GUN(g, VRR_READY, 1, 1, 6);
	CHECK_EQ(g.chamber + g.clip, 7);
	CHECK_EQ(vrrEngineLoaded(&g), 7);
	CHECK_SETTLE_CLEARED(g);
	CHECK_EQ(vrrAllowFire(&g), 1);

	dirty_gun(&g, VRR_RACKING, 1, 0, 1);
	vrrSettle(&g, 1);
	LI_CHECK_GUN(g, VRR_READY, 1, 1, 0);
	CHECK_EQ(vrrEngineLoaded(&g), 1);
	CHECK_SETTLE_CLEARED(g);

	/* with the flag and an empty clip: READY and dry */
	dirty_gun(&g, VRR_NEED_RACK, 1, 0, 0);
	vrrSettle(&g, 1);
	LI_CHECK_GUN(g, VRR_READY, 1, 0, 0);
	CHECK_EQ(vrrEngineLoaded(&g), 0);
	CHECK_EQ(vrrAllowFire(&g), 1);
	CHECK_SETTLE_CLEARED(g);

	/* settling twice changes nothing more (no second round chambered) */
	dirty_gun(&g, VRR_NEED_RACK, 1, 0, 7);
	vrrSettle(&g, 1);
	vrrSettle(&g, 1);
	LI_CHECK_GUN(g, VRR_READY, 1, 1, 6);

	/* a settled gun's next tick: no glow, no fx, ready */
	{
		struct vrroutput out;
		dirty_gun(&g, VRR_NEED_RACK, 1, 0, 7);
		vrrSettle(&g, 1);
		out = li_quiet(&g, 1);
		LI_CHECK_GUN(g, VRR_READY, 1, 1, 6);
		CHECK_EQ(out.glow, 0);
		CHECK_EQ(out.fx, 0);
		CHECK_EQ(out.loaded, 7);
	}
}

/* A rack already under way (entered with the flag off) ends at once on the first flagged tick */
static void test_li_racking_settles_at_once(void)
{
	struct vrrgun g;
	struct vrroutput out;
	int32_t full;

	for (full = 0; full <= 1; full++) {
		float pull = full ? 6.0f : 1.0f;

		/* loaded gun mid-rack: READY, the chambered round is not wasted */
		vrrInit(&g, 8);
		li_rack(&g, 0, 0.0f, 0.0f);
		CHECK_EQ(g.state, VRR_RACKING);
		out = li_rack(&g, 1, pull, 0.0f);
		LI_CHECK_GUN(g, VRR_READY, 1, 1, 7);
		CHECK_EQ(out.fx & (VRR_FX_RACK | VRR_FX_WASTE_ROUND), 0);
		CHECK_EQ(out.glow & RACK_BITS, 0);
		CHECK_EQ(out.allowfire, 1);
		CHECK_EQ(out.loaded, 8);

		/* clip in, empty chamber mid-rack: settles as vrrSettle with the flag does (chambers one) */
		make_need_rack_li(&g);
		li_rack(&g, 0, 0.0f, 0.0f);
		CHECK_EQ(g.state, VRR_RACKING);
		out = li_rack(&g, 1, pull, 0.0f);
		LI_CHECK_GUN(g, VRR_READY, 1, 1, 7);
		CHECK_EQ(out.fx & (VRR_FX_RACK | VRR_FX_WASTE_ROUND), 0);
		CHECK_EQ(out.glow & RACK_BITS, 0);
		CHECK_EQ(out.loaded, 8);

		/* fired dry mid-rack: READY and dry */
		vrrInit(&g, 8);
		vrrSyncFromEngineRow(&g, 0, 0);
		li_rack(&g, 0, 0.0f, 0.0f);
		CHECK_EQ(g.state, VRR_RACKING);
		out = li_rack(&g, 1, pull, 0.0f);
		LI_CHECK_GUN(g, VRR_READY, 1, 0, 0);
		CHECK_EQ(out.allowfire, 1);

		/* clip out mid-rack: CLIP_OUT, chamber kept (a restored chambered round) */
		vrtest_restored_clip_out(&g, 5);
		li_rack(&g, 0, 0.0f, 0.0f);
		CHECK_EQ(g.state, VRR_RACKING);
		out = li_rack(&g, 1, pull, 0.0f);
		LI_CHECK_GUN(g, VRR_CLIP_OUT, 0, 1, 0);
		CHECK_EQ(out.fx & (VRR_FX_RACK | VRR_FX_WASTE_ROUND), 0);
		CHECK_EQ(out.glow, VRR_GLOW_CLIP);
	}

	/* the grip is still held after the settle: holding on does not start a new rack */
	vrrInit(&g, 8);
	li_rack(&g, 0, 0.0f, 0.0f);
	li_rack(&g, 1, 1.0f, 0.0f);
	out = li_rack(&g, 1, 6.0f, 0.0f);
	LI_CHECK_GUN(g, VRR_READY, 1, 1, 7);
	CHECK_EQ(out.fx & (VRR_FX_RACK | VRR_FX_WASTE_ROUND), 0);
}

/* ---------- flag 0: unchanged ---------- */

static void test_li_flag_off_needs_rack(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrrInit(&g, 0);
	out = li_eject(&g, 0);
	CHECK_EQ(out.glow, VRR_GLOW_CLIP);
	li_grab(&g, 0, 20);
	out = li_release(&g, 0, 1);
	CHECK_EQ(out.reservedelta, -8);
	LI_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
	CHECK_EQ(out.loaded, 0);
	CHECK_EQ(out.allowfire, 0);
	CHECK_EQ(out.glow, VRR_GLOW_RACK);

	out = li_rack(&g, 0, 0.0f, 0.0f);
	CHECK_EQ(g.state, VRR_RACKING);
	out = li_rack(&g, 0, 3.0f, 0.0f);
	CHECK_EQ(out.fx & VRR_FX_RACK, VRR_FX_RACK);
	LI_CHECK_GUN(g, VRR_READY, 1, 1, 7);
	CHECK_EQ(out.loaded, 8);

	/* fired dry with the flag off: READY, a rack grab enters RACKING */
	vrrInit(&g, 8);
	vrrSyncFromEngineRow(&g, 0, 0);
	out = li_quiet(&g, 0);
	CHECK_EQ(g.state, VRR_READY);
	li_rack(&g, 0, 0.0f, 0.0f);
	CHECK_EQ(g.state, VRR_RACKING);
}

/* ---------- random play with the flag on ---------- */

static uint32_t li_seed;

static int li_rand(int n)
{
	li_seed = li_seed * 1103515245u + 12345u;
	return (int)(((li_seed >> 16) & 0x7fff) % (uint32_t)n);
}

static void test_li_random_play(void)
{
	int run;
	int32_t loaded_ejects = 0;
	li_seed = 2026;

	for (run = 0; run < 200; run++) {
		struct vrrgun g;
		int32_t clipsize = 1 + li_rand(12);
		int32_t start = li_rand(clipsize + 1);
		int32_t reserve = li_rand(40);
		int32_t total;
		int32_t grip = 0;
		int n, fails = 0;

		vrrInit(&g, start);
		total = g.chamber + g.clip + reserve;

		for (n = 0; n < 400 && fails < 3; n++) {
			struct vrrinput in = li_input(1);
			struct vrroutput out;
			int32_t badfx, before_rounds, returns;

			/* the engine fires a round, or fires dry */
			if (li_rand(6) == 0 && vrrAllowFire(&g)) {
				int32_t l = vrrEngineLoaded(&g);
				if (l > 0) {
					vrrSyncFromEngineRow(&g, l - 1, 0);
					total -= 1;
				}
			}

			before_rounds = g.chamber + g.clip; /* after the shot, before the tick */
			returns = vrrEjectReturnsRow(&g, 0);
			CHECK_EQ(returns, g.magin && g.state != VRR_RACKING ? before_rounds : 0);

			if (li_rand(4) == 0) {
				grip = !grip;
			}
			in.grip = grip;
			in.eject = li_rand(10) == 0;
			in.inclip = li_rand(3) == 0;
			in.inrack = li_rand(3) == 0;
			in.atshoulder = li_rand(3) == 0;
			in.seated = li_rand(5) == 0;
			in.rackpull = (float)li_rand(90) / 10.0f;
			in.racktravel = (float)(li_rand(3) * 3);
			in.clipsize = clipsize;
			in.reserve = reserve;
			in.tracked = li_rand(12) != 0;
			in.paused = li_rand(15) == 0;

			out = li_run(&g, &in);
			reserve += out.reservedelta;
			/* a live eject with rounds to return does eject */
			if (in.eject && in.tracked && !in.paused && returns > 0) {
				CHECK_EQ(out.fx & VRR_FX_EJECT, VRR_FX_EJECT);
			}

			/* Task E2e: a flagged eject returns every loaded round, the chambered one too */
			if (out.fx & VRR_FX_EJECT) {
				if (before_rounds > 0) {
					loaded_ejects++;
				}
				CHECK_EQ(out.reservedelta, before_rounds);
				CHECK_EQ(out.reservedelta, returns);
				CHECK_EQ(g.chamber, 0);
				CHECK_EQ(g.clip, 0);
				/* CLIP_OUT, or HOLDING_CLIP only when a shoulder grab lands on the same tick */
				CHECK(g.state == VRR_CLIP_OUT
					|| (g.state == VRR_HOLDING_CLIP && (out.fx & VRR_FX_GRAB_CLIP)));
				CHECK_EQ(g.magin, 0);
				CHECK_EQ(out.allowfire, 0);
			}

			badfx = out.fx & (VRR_FX_RACK | VRR_FX_WASTE_ROUND);
			if (g.state == VRR_NEED_RACK || g.state == VRR_RACKING
					|| (out.glow & RACK_BITS) || badfx
					|| reserve < 0
					|| g.chamber + g.clip + reserve != total
					|| (g.magin && !g.chamber && g.clip != 0)
					|| (g.magin && g.state != VRR_READY)
					|| (!g.magin && (g.chamber || out.allowfire))
					|| (g.chamber + g.clip > clipsize && g.magin)
					|| out.loaded != vrrEngineLoaded(&g)
					|| out.allowfire != vrrAllowFire(&g)
					|| out.handclip != (g.state == VRR_HOLDING_CLIP)) {
				fails++;
				printf("  li random run %d tick %d: state %d magin %d chamber %d clip %d held %d "
					"reserve %d total %d glow %#x fx %#x loaded %d allowfire %d\n",
					run, n, g.state, g.magin, g.chamber, g.clip, g.held, reserve, total,
					out.glow, out.fx, out.loaded, out.allowfire);
			}
			CHECK(g.state != VRR_NEED_RACK);
			CHECK(g.state != VRR_RACKING);
			CHECK_EQ(out.glow & RACK_BITS, 0);
			CHECK_EQ(badfx, 0);
			CHECK(reserve >= 0);
			CHECK_EQ(g.chamber + g.clip + reserve, total);
			CHECK(!(g.magin && !g.chamber && g.clip != 0));
			CHECK(!g.magin || g.state == VRR_READY);
			/* with the flag every clip-out comes from an eject: empty, no fire until an insert */
			CHECK(g.magin || (g.chamber == 0 && out.allowfire == 0));
			CHECK(!g.magin || g.chamber + g.clip <= clipsize);
			CHECK_EQ(out.loaded, vrrEngineLoaded(&g));
			CHECK_EQ(out.allowfire, vrrAllowFire(&g));
			CHECK_EQ(out.handclip, g.state == VRR_HOLDING_CLIP);
		}
	}
	/* the random play did eject loaded guns */
	CHECK(loaded_ejects > 50);
}

void run_reload_loadsoninsert_tests(void)
{
	test_li_empty_gun_insert_ready_and_fires();
	test_li_seated_insert_ready();
	test_li_partial_clip();
	test_li_empty_clip_insert_ready_dry();
	test_li_fired_dry_is_ready();
	test_li_eject_loaded_returns_all_rounds();
	test_li_eject_returns_helper();
	test_li_eject_and_grab_same_tick();
	test_li_chambered_clip_out_from_settle_then_insert();
	test_li_no_rack_glow_any_state();
	test_li_rack_input_ignored();
	test_li_settle();
	test_li_racking_settles_at_once();
	test_li_flag_off_needs_rack();
	test_li_random_play();
}
