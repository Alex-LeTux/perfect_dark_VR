#include <stdint.h>
#include <string.h>
#include "vrtest.h"
#include "game/vr_reload_core.h"

/*
 * Contract tests for Task F3a: a gun racked by a slap (vrrinput.rackslap = 1).
 * Rule: with the flag, a gun in VRR_NEED_RACK (a clip in, the chamber empty) is finished by a
 * slap (vrrinput.slap, one tick's event) instead of a grip and a pull: the slap does what a
 * completed rack does (a round from the clip into the chamber, VRR_FX_RACK; READY, or NEED_RACK
 * again with an empty clip). Grip and pull rack inputs are ignored for a flagged gun; a slap in
 * any other state, for an unflagged gun, or while paused is ignored. Otherwise a rack gun: an
 * insert with a round chambered is VRR_READY at once, vrrSettle takes it as a rack gun
 * (loadsoninsert 0). Task F4a: an eject returns the chambered round with the clip, as for every
 * gun, so every new clip needs the slap; a chambered round with the clip out exists only in a
 * restored or settled state.
 * Extension (implementer): a gun already VRR_RACKING when the flag is set ends the rack at once,
 * with no rack effect, and settles as a rack gun.
 * Numbers follow the AR34-like test gun: clipsize 8 (chamber 1 + clip 7).
 */

#define RS_CHECK_GUN(g, st, mi, ch, cl) do { \
	CHECK_EQ((g).state, (st)); \
	CHECK_EQ((g).magin, (mi)); \
	CHECK_EQ((g).chamber, (ch)); \
	CHECK_EQ((g).clip, (cl)); \
} while (0)

#define RS_RACKFX (VRR_FX_RACK | VRR_FX_WASTE_ROUND)

/* ---------- helpers: every tick carries the flag given ---------- */

static struct vrrinput rs_input(int32_t flag)
{
	struct vrrinput in;
	memset(&in, 0, sizeof in);
	in.tracked = 1;
	in.clipsize = 8;
	in.reserve = 20;
	in.rackslap = flag;
	return in;
}

static struct vrroutput rs_run(struct vrrgun *g, const struct vrrinput *in)
{
	struct vrroutput out;
	memset(&out, 0x5a, sizeof out);
	vrrTick(g, in, &out);
	return out;
}

static struct vrroutput rs_quiet(struct vrrgun *g, int32_t flag)
{
	struct vrrinput in = rs_input(flag);
	return rs_run(g, &in);
}

static struct vrroutput rs_slap(struct vrrgun *g, int32_t flag)
{
	struct vrrinput in = rs_input(flag);
	in.slap = 1;
	return rs_run(g, &in);
}

static struct vrroutput rs_eject(struct vrrgun *g, int32_t flag)
{
	struct vrrinput in = rs_input(flag);
	in.eject = 1;
	return rs_run(g, &in);
}

static struct vrroutput rs_grab(struct vrrgun *g, int32_t flag, int32_t reserve)
{
	struct vrrinput in = rs_input(flag);
	in.grip = 1;
	in.atshoulder = 1;
	in.reserve = reserve;
	return rs_run(g, &in);
}

static struct vrroutput rs_release(struct vrrgun *g, int32_t flag, int32_t inclip)
{
	struct vrrinput in = rs_input(flag);
	in.inclip = inclip;
	return rs_run(g, &in);
}

static struct vrroutput rs_seat(struct vrrgun *g, int32_t flag)
{
	struct vrrinput in = rs_input(flag);
	in.grip = 1;
	in.inclip = 1;
	in.seated = 1;
	return rs_run(g, &in);
}

static struct vrroutput rs_rack(struct vrrgun *g, int32_t flag, float pull, float travel)
{
	struct vrrinput in = rs_input(flag);
	in.grip = 1;
	in.inrack = 1;
	in.rackpull = pull;
	in.racktravel = travel;
	return rs_run(g, &in);
}

static struct vrroutput rs_rack_release(struct vrrgun *g, int32_t flag)
{
	struct vrrinput in = rs_input(flag);
	in.inrack = 1;
	return rs_run(g, &in);
}

/* An empty gun, a clip of `reserve` rounds (at most 8) inserted with the given flag: NEED_RACK */
static void make_need_rack_rs(struct vrrgun *g, int32_t flag, int32_t reserve)
{
	vrrInit(g, 0);
	rs_eject(g, flag);
	rs_grab(g, flag, reserve);
	rs_release(g, flag, 1);
}

/* ---------- the slap finishes the rack ---------- */

static void test_rs_empty_gun_insert_slap_fires(void)
{
	struct vrrgun g;
	struct vrroutput out;
	int seated;

	for (seated = 0; seated <= 1; seated++) {
		vrrInit(&g, 0);
		out = rs_eject(&g, 1);
		RS_CHECK_GUN(g, VRR_CLIP_OUT, 0, 0, 0);
		CHECK_EQ(out.allowfire, 0);

		out = rs_grab(&g, 1, 20);
		CHECK_EQ(g.state, VRR_HOLDING_CLIP);
		CHECK_EQ(g.held, 8);

		out = seated ? rs_seat(&g, 1) : rs_release(&g, 1, 1);
		CHECK_EQ(out.fx & VRR_FX_INSERT, VRR_FX_INSERT);
		CHECK_EQ(out.fx & RS_RACKFX, 0);
		CHECK_EQ(out.reservedelta, -8);
		RS_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
		CHECK_EQ(out.loaded, 0);
		CHECK_EQ(out.allowfire, 0);

		/* quiet ticks do not finish it */
		out = rs_quiet(&g, 1);
		RS_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
		CHECK_EQ(out.fx & RS_RACKFX, 0);
		CHECK_EQ(out.allowfire, 0);

		/* the slap: chamber 1, clip 7, the rack effect, nothing wasted, no reserve change */
		out = rs_slap(&g, 1);
		CHECK_EQ(out.fx & VRR_FX_RACK, VRR_FX_RACK);
		CHECK_EQ(out.fx & VRR_FX_WASTE_ROUND, 0);
		CHECK_EQ(out.reservedelta, 0);
		RS_CHECK_GUN(g, VRR_READY, 1, 1, 7);
		CHECK_EQ(out.loaded, 8);
		CHECK_EQ(vrrEngineLoaded(&g), 8);
		CHECK_EQ(out.allowfire, 1);
		CHECK_EQ(vrrAllowFire(&g), 1);
		CHECK_EQ(out.handclip, 0);

		/* it fires */
		vrrSyncFromEngineRow(&g, 7, 0);
		out = rs_quiet(&g, 1);
		RS_CHECK_GUN(g, VRR_READY, 1, 1, 6);
		CHECK_EQ(out.loaded, 7);
		CHECK_EQ(out.allowfire, 1);
		CHECK_EQ(out.fx, 0);
	}

	/* a partial clip: 3 rounds, slap: chamber 1, clip 2 */
	make_need_rack_rs(&g, 1, 3);
	RS_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 3);
	out = rs_slap(&g, 1);
	CHECK_EQ(out.fx & VRR_FX_RACK, VRR_FX_RACK);
	RS_CHECK_GUN(g, VRR_READY, 1, 1, 2);
	CHECK_EQ(out.loaded, 3);
}

/* The slap matches a completed rack: same gun, same fx bits, same counts */
static void test_rs_slap_equals_rack(void)
{
	struct vrrgun a, b;
	struct vrroutput oa, ob;
	int32_t res;

	for (res = 1; res <= 8; res++) {
		make_need_rack_rs(&a, 0, res);
		rs_rack(&a, 0, 0.0f, 0.0f);
		oa = rs_rack(&a, 0, 3.0f, 0.0f);

		make_need_rack_rs(&b, 1, res);
		ob = rs_slap(&b, 1);

		CHECK_EQ(ob.fx & RS_RACKFX, oa.fx & RS_RACKFX);
		CHECK_EQ(b.state, a.state);
		CHECK_EQ(b.magin, a.magin);
		CHECK_EQ(b.chamber, a.chamber);
		CHECK_EQ(b.clip, a.clip);
		CHECK_EQ(ob.loaded, oa.loaded);
		CHECK_EQ(ob.allowfire, oa.allowfire);
		CHECK_EQ(ob.reservedelta, oa.reservedelta);
	}
}

/* A clip with one round: the slap chambers it, clip 0; fired, the gun is dry and a slap does nothing */
static void test_rs_one_round_clip(void)
{
	struct vrrgun g;
	struct vrroutput out;
	make_need_rack_rs(&g, 1, 1);
	RS_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 1);
	out = rs_slap(&g, 1);
	CHECK_EQ(out.fx & VRR_FX_RACK, VRR_FX_RACK);
	RS_CHECK_GUN(g, VRR_READY, 1, 1, 0);
	CHECK_EQ(out.loaded, 1);
	CHECK_EQ(out.allowfire, 1);

	vrrSyncFromEngineRow(&g, 0, 0);
	out = rs_quiet(&g, 1);
	RS_CHECK_GUN(g, VRR_READY, 1, 0, 0);
	out = rs_slap(&g, 1);
	CHECK_EQ(out.fx & RS_RACKFX, 0);
	RS_CHECK_GUN(g, VRR_READY, 1, 0, 0);
}

/* An empty clip (clipsize 0 is the only way through the API) inserted, then slapped:
 * the rack effect, NEED_RACK again, nothing chambered, no fire */
static void test_rs_empty_clip_slap(void)
{
	struct vrrgun g;
	struct vrrinput in;
	struct vrroutput out;

	vrrInit(&g, 0);
	rs_eject(&g, 1);
	in = rs_input(1);
	in.clipsize = 0;
	in.grip = 1;
	in.atshoulder = 1;
	rs_run(&g, &in);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	CHECK_EQ(g.held, 0);

	in = rs_input(1);
	in.clipsize = 0;
	in.inclip = 1;
	out = rs_run(&g, &in);
	CHECK_EQ(out.fx & VRR_FX_INSERT, VRR_FX_INSERT);
	/* an empty clip in, nothing to chamber: READY and dry, no slap due (c032e6c3e) */
	RS_CHECK_GUN(g, VRR_READY, 1, 0, 0);
	CHECK_EQ(out.allowfire, 1);
	CHECK_EQ(out.glow & (VRR_GLOW_RACK | VRR_HOT_RACK), 0);

	/* a slap in READY is ignored */
	in = rs_input(1);
	in.clipsize = 0;
	in.slap = 1;
	out = rs_run(&g, &in);
	CHECK_EQ(out.fx & RS_RACKFX, 0);
	CHECK_EQ(out.reservedelta, 0);
	RS_CHECK_GUN(g, VRR_READY, 1, 0, 0);
	CHECK_EQ(out.loaded, 0);
	CHECK_EQ(out.allowfire, 1);
	CHECK_EQ(out.glow & (VRR_GLOW_RACK | VRR_HOT_RACK), 0);

	/* the gun can still be reloaded: eject, a full clip, a slap */
	out = rs_eject(&g, 1);
	CHECK_EQ(out.reservedelta, 0);
	RS_CHECK_GUN(g, VRR_CLIP_OUT, 0, 0, 0);
	rs_grab(&g, 1, 20);
	rs_release(&g, 1, 1);
	RS_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
	rs_slap(&g, 1);
	RS_CHECK_GUN(g, VRR_READY, 1, 1, 7);
}

/* slap = 1 on several ticks in a row: each tick is one event (the input is not edge-detected by
 * the rule). The first finishes the rack; the rest land in READY and are ignored. */
static void test_rs_slap_held_several_ticks(void)
{
	struct vrrgun g;
	struct vrrinput in;
	struct vrroutput out;
	int i;

	make_need_rack_rs(&g, 1, 20);
	out = rs_slap(&g, 1);
	CHECK_EQ(out.fx & VRR_FX_RACK, VRR_FX_RACK);
	RS_CHECK_GUN(g, VRR_READY, 1, 1, 7);
	for (i = 0; i < 4; i++) {
		out = rs_slap(&g, 1);
		CHECK_EQ(out.fx & RS_RACKFX, 0);
		RS_CHECK_GUN(g, VRR_READY, 1, 1, 7);
		CHECK_EQ(out.loaded, 8);
	}

	/* an empty clip: the insert leaves the gun READY and dry, so every slap is ignored */
	vrrInit(&g, 0);
	rs_eject(&g, 1);
	in = rs_input(1);
	in.clipsize = 0;
	in.grip = 1;
	in.atshoulder = 1;
	rs_run(&g, &in);
	in = rs_input(1);
	in.clipsize = 0;
	in.inclip = 1;
	rs_run(&g, &in);
	RS_CHECK_GUN(g, VRR_READY, 1, 0, 0);
	for (i = 0; i < 3; i++) {
		in = rs_input(1);
		in.clipsize = 0;
		in.slap = 1;
		out = rs_run(&g, &in);
		CHECK_EQ(out.fx & RS_RACKFX, 0);
		RS_CHECK_GUN(g, VRR_READY, 1, 0, 0);
		CHECK_EQ(out.allowfire, 1);
	}
}

/* ---------- grip and pull are ignored with the flag ---------- */

static void test_rs_grip_and_pull_ignored(void)
{
	struct vrrgun g;
	struct vrroutput out;
	float pulls[] = { 0.0f, 1.0f, 3.0f, 6.0f, 10.0f };
	float travels[] = { 0.0f, 3.0f, 6.0f };
	int i, t;

	for (t = 0; t < 3; t++) {
		make_need_rack_rs(&g, 1, 20);
		RS_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
		for (i = 0; i < 5; i++) {
			out = rs_rack(&g, 1, pulls[i], travels[t]);
			CHECK(g.state != VRR_RACKING);
			RS_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
			CHECK_EQ(out.fx & RS_RACKFX, 0);
			CHECK_EQ(out.allowfire, 0);
			CHECK_EQ(out.reservedelta, 0);
		}
		out = rs_rack_release(&g, 1);
		RS_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
		CHECK_EQ(out.fx & RS_RACKFX, 0);

		/* a grab with a full pull on its first tick: nothing */
		out = rs_rack(&g, 1, 10.0f, travels[t]);
		RS_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
		CHECK_EQ(out.fx & RS_RACKFX, 0);
		rs_rack_release(&g, 1);

		/* the slap still works afterwards */
		out = rs_slap(&g, 1);
		CHECK_EQ(out.fx & VRR_FX_RACK, VRR_FX_RACK);
		RS_CHECK_GUN(g, VRR_READY, 1, 1, 7);
	}

	/* a slap on a tick where the hand also grips and pulls in the rack zone: the slap counts once */
	{
		struct vrrinput in;
		make_need_rack_rs(&g, 1, 20);
		in = rs_input(1);
		in.grip = 1;
		in.inrack = 1;
		in.rackpull = 6.0f;
		in.slap = 1;
		out = rs_run(&g, &in);
		CHECK_EQ(out.fx & RS_RACKFX, VRR_FX_RACK);
		RS_CHECK_GUN(g, VRR_READY, 1, 1, 7);
		out = rs_run(&g, &in);
		CHECK_EQ(out.fx & RS_RACKFX, 0);
		RS_CHECK_GUN(g, VRR_READY, 1, 1, 7);
	}

	/* READY and loaded: a grip and full pull would waste the chambered round on a rack gun; ignored */
	vrrInit(&g, 8);
	rs_rack(&g, 1, 0.0f, 0.0f);
	CHECK(g.state != VRR_RACKING);
	out = rs_rack(&g, 1, 6.0f, 0.0f);
	CHECK_EQ(out.fx & RS_RACKFX, 0);
	RS_CHECK_GUN(g, VRR_READY, 1, 1, 7);
	rs_rack_release(&g, 1);
	RS_CHECK_GUN(g, VRR_READY, 1, 1, 7);

	/* READY and fired dry: a rack gun would go to NEED_RACK; ignored */
	vrrInit(&g, 8);
	vrrSyncFromEngineRow(&g, 0, 0);
	rs_rack(&g, 1, 0.0f, 0.0f);
	out = rs_rack(&g, 1, 6.0f, 0.0f);
	CHECK_EQ(out.fx & RS_RACKFX, 0);
	RS_CHECK_GUN(g, VRR_READY, 1, 0, 0);

	/* clip out, round chambered (a restored state): a grip and pull do not waste it */
	vrtest_restored_clip_out(&g, 5);
	RS_CHECK_GUN(g, VRR_CLIP_OUT, 0, 1, 0);
	rs_rack(&g, 1, 0.0f, 0.0f);
	out = rs_rack(&g, 1, 6.0f, 0.0f);
	CHECK_EQ(out.fx & RS_RACKFX, 0);
	RS_CHECK_GUN(g, VRR_CLIP_OUT, 0, 1, 0);
}

/* A slap gun has no rack to grab: no rack marker (glow or hot) and no zone-enter buzz from the rack
 * zone, in NEED_RACK or with the clip out and the chamber empty; the clip marker is unchanged. The
 * same gun unflagged lights the rack marker and buzzes on entering the zone in NEED_RACK (with the
 * clip out no gun's rack marker shows, 89fbd7a89). */
static void test_rs_no_rack_marker(void)
{
	struct vrrgun g;
	struct vrroutput out;
	struct vrrinput in;
	int32_t flag;

	for (flag = 0; flag <= 1; flag++) {
		make_need_rack_rs(&g, flag, 20);
		RS_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);

		out = rs_quiet(&g, flag);
		CHECK_EQ(out.glow & VRR_GLOW_RACK, flag ? 0 : VRR_GLOW_RACK);

		/* the hand moves into the rack zone, no grip */
		in = rs_input(flag);
		in.inrack = 1;
		out = rs_run(&g, &in);
		CHECK_EQ(out.glow & (VRR_GLOW_RACK | VRR_HOT_RACK), flag ? 0 : (VRR_GLOW_RACK | VRR_HOT_RACK));
		CHECK_EQ(out.fx & VRR_FX_ZONE_ENTER, flag ? 0 : VRR_FX_ZONE_ENTER);
		RS_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);

		/* staying in it sends nothing more */
		out = rs_run(&g, &in);
		CHECK_EQ(out.fx & VRR_FX_ZONE_ENTER, 0);

		/* fired dry and ejected: clip out, chamber empty; the clip marker shows, the rack one
		 * never with the clip out, flagged or not (89fbd7a89) */
		vrrInit(&g, 0);
		out = rs_eject(&g, flag);
		RS_CHECK_GUN(g, VRR_CLIP_OUT, 0, 0, 0);
		CHECK_EQ(out.glow & VRR_GLOW_CLIP, VRR_GLOW_CLIP);
		CHECK_EQ(out.glow & VRR_GLOW_RACK, 0);
		out = rs_quiet(&g, flag);
		in = rs_input(flag);
		in.inrack = 1;
		out = rs_run(&g, &in);
		CHECK_EQ(out.glow & VRR_HOT_RACK, 0);
		CHECK_EQ(out.fx & VRR_FX_ZONE_ENTER, 0);
	}
}

/* ---------- a slap is ignored elsewhere ---------- */

static void test_rs_slap_unflagged_ignored(void)
{
	struct vrrgun g;
	struct vrroutput out;
	int i;

	make_need_rack_rs(&g, 0, 20);
	RS_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
	for (i = 0; i < 3; i++) {
		out = rs_slap(&g, 0);
		CHECK_EQ(out.fx & RS_RACKFX, 0);
		RS_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
		CHECK_EQ(out.allowfire, 0);
	}
	/* the grip and pull still rack an unflagged gun */
	rs_rack(&g, 0, 0.0f, 0.0f);
	CHECK_EQ(g.state, VRR_RACKING);
	out = rs_rack(&g, 0, 3.0f, 0.0f);
	CHECK_EQ(out.fx & VRR_FX_RACK, VRR_FX_RACK);
	RS_CHECK_GUN(g, VRR_READY, 1, 1, 7);

	/* unflagged READY loaded: a slap does not waste the round */
	vrrInit(&g, 8);
	out = rs_slap(&g, 0);
	CHECK_EQ(out.fx & RS_RACKFX, 0);
	RS_CHECK_GUN(g, VRR_READY, 1, 1, 7);
}

static void check_slap_ignored(struct vrrgun *g, const struct vrrinput *base)
{
	struct vrrgun before = *g;
	struct vrrinput in = *base;
	struct vrroutput out;
	in.slap = 1;
	out = rs_run(g, &in);
	CHECK_EQ(out.fx & RS_RACKFX, 0);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_EQ(g->state, before.state);
	CHECK_EQ(g->magin, before.magin);
	CHECK_EQ(g->chamber, before.chamber);
	CHECK_EQ(g->clip, before.clip);
	CHECK_EQ(g->held, before.held);
}

static void test_rs_slap_other_states_ignored(void)
{
	struct vrrgun g;
	struct vrrinput in = rs_input(1);
	int32_t loaded;

	/* READY at every load, dry included */
	for (loaded = 0; loaded <= 8; loaded++) {
		vrrInit(&g, 8);
		vrrSyncFromEngineRow(&g, loaded, 0);
		rs_quiet(&g, 1);
		CHECK_EQ(g.state, VRR_READY);
		check_slap_ignored(&g, &in);
	}

	/* CLIP_OUT, chambered (a restored state) and not */
	vrtest_restored_clip_out(&g, 5);
	RS_CHECK_GUN(g, VRR_CLIP_OUT, 0, 1, 0);
	check_slap_ignored(&g, &in);
	vrrInit(&g, 0);
	rs_eject(&g, 1);
	RS_CHECK_GUN(g, VRR_CLIP_OUT, 0, 0, 0);
	check_slap_ignored(&g, &in);

	/* HOLDING_CLIP, grip held, outside the clip zone and inside it */
	vrrInit(&g, 0);
	rs_eject(&g, 1);
	rs_grab(&g, 1, 20);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	in.grip = 1;
	in.inclip = 0;
	check_slap_ignored(&g, &in);
	in.inclip = 1;
	check_slap_ignored(&g, &in);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	CHECK_EQ(g.held, 8);

	/* the clip still goes in after, and needs its own slap */
	rs_release(&g, 1, 1);
	RS_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
}

static void test_rs_slap_paused_ignored(void)
{
	struct vrrgun g;
	struct vrrinput in;
	struct vrroutput out;
	int i;

	make_need_rack_rs(&g, 1, 20);
	for (i = 0; i < 3; i++) {
		in = rs_input(1);
		in.slap = 1;
		in.paused = 1;
		out = rs_run(&g, &in);
		CHECK_EQ(out.fx & RS_RACKFX, 0);
		RS_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
		CHECK_EQ(out.allowfire, 0);
	}
	out = rs_slap(&g, 1);
	CHECK_EQ(out.fx & VRR_FX_RACK, VRR_FX_RACK);
	RS_CHECK_GUN(g, VRR_READY, 1, 1, 7);
}

/* ---------- the rest is a rack gun ---------- */

/* An insert with a round chambered (a restored or settled state) is READY at once, no slap */
static void test_rs_chambered_insert_ready(void)
{
	struct vrrgun g;
	struct vrroutput out;
	int seated;

	for (seated = 0; seated <= 1; seated++) {
		vrtest_restored_clip_out(&g, 5);
		RS_CHECK_GUN(g, VRR_CLIP_OUT, 0, 1, 0);
		out = rs_quiet(&g, 1);
		CHECK_EQ(out.allowfire, 1);
		CHECK_EQ(out.loaded, 1);
		rs_grab(&g, 1, 20);
		out = seated ? rs_seat(&g, 1) : rs_release(&g, 1, 1);
		CHECK_EQ(out.fx & VRR_FX_INSERT, VRR_FX_INSERT);
		CHECK_EQ(out.fx & RS_RACKFX, 0);
		CHECK_EQ(out.reservedelta, -7);
		RS_CHECK_GUN(g, VRR_READY, 1, 1, 7);
		CHECK_EQ(out.loaded, 8);
		CHECK_EQ(out.allowfire, 1);

		/* no slap needed, and one is ignored */
		out = rs_slap(&g, 1);
		CHECK_EQ(out.fx & RS_RACKFX, 0);
		RS_CHECK_GUN(g, VRR_READY, 1, 1, 7);
	}
}

/* Task F4a: B on a loaded flagged gun returns the clip and the chambered round; every new clip
 * needs the slap; rounds add up */
static void test_rs_eject_empties_gun(void)
{
	struct vrrgun g;
	struct vrroutput out;
	int32_t loaded;
	int seated;

	for (seated = 0; seated <= 1; seated++) {
		for (loaded = 1; loaded <= 8; loaded++) {
			int32_t reserve = 20, total;
			vrrInit(&g, 8);
			vrrSyncFromEngineRow(&g, loaded, 0);
			total = g.chamber + g.clip + reserve;
			CHECK_EQ(vrrEjectReturnsRow(&g, 0), loaded);
			out = rs_eject(&g, 1);
			CHECK_EQ(out.fx & VRR_FX_EJECT, VRR_FX_EJECT);
			CHECK_EQ(out.reservedelta, loaded);
			reserve += out.reservedelta;
			RS_CHECK_GUN(g, VRR_CLIP_OUT, 0, 0, 0);
			CHECK_EQ(out.loaded, 0);
			CHECK_EQ(out.allowfire, 0);
			CHECK_EQ(g.chamber + g.clip + reserve, total);

			/* the new clip needs the slap: a full clip, then a partial one */
			rs_grab(&g, 1, reserve);
			out = seated ? rs_seat(&g, 1) : rs_release(&g, 1, 1);
			reserve += out.reservedelta;
			CHECK_EQ(out.reservedelta, -8);
			RS_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
			CHECK_EQ(out.allowfire, 0);
			CHECK_EQ(out.fx & RS_RACKFX, 0);
			CHECK_EQ(g.chamber + g.clip + reserve, total);
			out = rs_slap(&g, 1);
			CHECK_EQ(out.fx & VRR_FX_RACK, VRR_FX_RACK);
			RS_CHECK_GUN(g, VRR_READY, 1, 1, 7);
			CHECK_EQ(g.chamber + g.clip + reserve, total);

			vrrSyncFromEngineRow(&g, 8 - loaded % 3, 0); /* the engine fires a few */
			total -= loaded % 3;
			out = rs_eject(&g, 1);
			reserve += out.reservedelta;
			CHECK_EQ(out.reservedelta, 8 - loaded % 3);
			RS_CHECK_GUN(g, VRR_CLIP_OUT, 0, 0, 0);
			rs_grab(&g, 1, 3);
			out = rs_release(&g, 1, 1);
			reserve += out.reservedelta;
			CHECK_EQ(out.reservedelta, -3);
			RS_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 3);
			rs_slap(&g, 1);
			RS_CHECK_GUN(g, VRR_READY, 1, 1, 2);
			CHECK_EQ(g.chamber + g.clip + reserve, total);
		}
	}

	/* eject from NEED_RACK: the clip's rounds back, chamber empty, then a reload and a slap */
	{
		int32_t reserve = 12, total;
		make_need_rack_rs(&g, 1, 20);
		total = g.chamber + g.clip + reserve;
		CHECK_EQ(vrrEjectReturnsRow(&g, 0), 8);
		out = rs_eject(&g, 1);
		CHECK_EQ(out.reservedelta, 8);
		reserve += out.reservedelta;
		RS_CHECK_GUN(g, VRR_CLIP_OUT, 0, 0, 0);
		CHECK_EQ(out.allowfire, 0);
		CHECK_EQ(g.chamber + g.clip + reserve, total);
		rs_grab(&g, 1, reserve);
		out = rs_release(&g, 1, 1);
		reserve += out.reservedelta;
		RS_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
		out = rs_slap(&g, 1);
		reserve += out.reservedelta;
		RS_CHECK_GUN(g, VRR_READY, 1, 1, 7);
		CHECK_EQ(g.chamber + g.clip + reserve, total);
	}
}

static void test_rs_settle_as_rack_gun(void)
{
	struct vrrgun g;
	struct vrroutput out;

	/* clip in, chamber empty, rounds in the clip: NEED_RACK, nothing chambered */
	vrrInit(&g, 8);
	g.chamber = 0;
	g.clip = 7;
	g.state = VRR_READY;
	g.prevgrip = 1;
	vrrSettle(&g, 0);
	RS_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 7);
	CHECK_EQ(vrrAllowFire(&g), 0);
	/* flagged ticks do not chamber it on their own; a slap does */
	out = rs_quiet(&g, 1);
	RS_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 7);
	CHECK_EQ(out.fx & RS_RACKFX, 0);
	out = rs_slap(&g, 1);
	CHECK_EQ(out.fx & VRR_FX_RACK, VRR_FX_RACK);
	RS_CHECK_GUN(g, VRR_READY, 1, 1, 6);

	/* clip in, empty: vrrSettle gives READY and dry (c032e6c3e); a slap is ignored */
	vrrInit(&g, 8);
	g.chamber = 0;
	g.clip = 0;
	g.state = VRR_NEED_RACK;
	vrrSettle(&g, 0);
	RS_CHECK_GUN(g, VRR_READY, 1, 0, 0);
	CHECK_EQ(vrrAllowFire(&g), 1);
	out = rs_slap(&g, 1);
	CHECK_EQ(out.fx & RS_RACKFX, 0);
	RS_CHECK_GUN(g, VRR_READY, 1, 0, 0);
	CHECK_EQ(out.allowfire, 1);

	/* chambered: READY, counts untouched */
	vrrInit(&g, 5);
	g.state = VRR_NEED_RACK;
	vrrSettle(&g, 0);
	RS_CHECK_GUN(g, VRR_READY, 1, 1, 4);

	/* clip out: CLIP_OUT, chamber kept */
	vrrInit(&g, 5);
	g.magin = 0;
	g.clip = 0;
	g.state = VRR_HOLDING_CLIP;
	g.held = 6;
	vrrSettle(&g, 0);
	RS_CHECK_GUN(g, VRR_CLIP_OUT, 0, 1, 0);
	CHECK_EQ(g.held, 0);
	out = rs_quiet(&g, 1);
	RS_CHECK_GUN(g, VRR_CLIP_OUT, 0, 1, 0);
	CHECK_EQ(out.allowfire, 1);
}

/* ---------- extension: a rack under way when the flag is set ---------- */

static void test_rs_racking_ends_at_once(void)
{
	struct vrrgun g;
	struct vrroutput out;
	int32_t full;

	for (full = 0; full <= 1; full++) {
		float pull = full ? 6.0f : 1.0f;

		/* clip in, chamber empty mid-rack: the rack ends with no rack, NEED_RACK (a rack gun) */
		make_need_rack_rs(&g, 0, 20);
		rs_rack(&g, 0, 0.0f, 0.0f);
		CHECK_EQ(g.state, VRR_RACKING);
		out = rs_rack(&g, 1, pull, 0.0f);
		RS_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
		CHECK_EQ(out.fx & RS_RACKFX, 0);
		CHECK_EQ(out.allowfire, 0);
		/* still gripping and pulling: no rack */
		out = rs_rack(&g, 1, 10.0f, 0.0f);
		RS_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
		CHECK_EQ(out.fx & RS_RACKFX, 0);
		/* the slap finishes it */
		out = rs_slap(&g, 1);
		CHECK_EQ(out.fx & VRR_FX_RACK, VRR_FX_RACK);
		RS_CHECK_GUN(g, VRR_READY, 1, 1, 7);

		/* loaded mid-rack: READY, the chambered round is not wasted */
		vrrInit(&g, 8);
		rs_rack(&g, 0, 0.0f, 0.0f);
		CHECK_EQ(g.state, VRR_RACKING);
		out = rs_rack(&g, 1, pull, 0.0f);
		RS_CHECK_GUN(g, VRR_READY, 1, 1, 7);
		CHECK_EQ(out.fx & RS_RACKFX, 0);
		CHECK_EQ(out.loaded, 8);

		/* fired dry mid-rack: settles as a rack gun, READY and dry (c032e6c3e) */
		vrrInit(&g, 8);
		vrrSyncFromEngineRow(&g, 0, 0);
		rs_rack(&g, 0, 0.0f, 0.0f);
		CHECK_EQ(g.state, VRR_RACKING);
		out = rs_rack(&g, 1, pull, 0.0f);
		RS_CHECK_GUN(g, VRR_READY, 1, 0, 0);
		CHECK_EQ(out.fx & RS_RACKFX, 0);

		/* clip out mid-rack: CLIP_OUT, chamber kept (a restored chambered round) */
		vrtest_restored_clip_out(&g, 5);
		rs_rack(&g, 0, 0.0f, 0.0f);
		CHECK_EQ(g.state, VRR_RACKING);
		out = rs_rack(&g, 1, pull, 0.0f);
		RS_CHECK_GUN(g, VRR_CLIP_OUT, 0, 1, 0);
		CHECK_EQ(out.fx & RS_RACKFX, 0);
	}
}

/* ---------- random play with the flag on ---------- */

static uint32_t rs_seed;

static int rs_rand(int n)
{
	rs_seed = rs_seed * 1103515245u + 12345u;
	return (int)(((rs_seed >> 16) & 0x7fff) % (uint32_t)n);
}

static void test_rs_random_play(void)
{
	int run;
	int32_t slap_racks = 0, slap_ready = 0;
	rs_seed = 3101;

	for (run = 0; run < 200; run++) {
		struct vrrgun g;
		int32_t clipsize = 1 + rs_rand(12);
		int32_t start = rs_rand(clipsize + 1);
		int32_t reserve = rs_rand(40);
		int32_t total;
		int32_t grip = 0;
		int n, fails = 0;

		vrrInit(&g, start);
		total = g.chamber + g.clip + reserve;

		for (n = 0; n < 400 && fails < 3; n++) {
			struct vrrinput in = rs_input(1);
			struct vrroutput out;
			struct vrrgun before;
			int32_t bad = 0;

			if (rs_rand(6) == 0 && vrrAllowFire(&g)) {
				int32_t l = vrrEngineLoaded(&g);
				if (l > 0) {
					vrrSyncFromEngineRow(&g, l - 1, 0);
					total -= 1;
				}
			}
			before = g;

			if (rs_rand(4) == 0) {
				grip = !grip;
			}
			in.grip = grip;
			in.eject = rs_rand(10) == 0;
			in.inclip = rs_rand(3) == 0;
			in.inrack = rs_rand(3) == 0;
			in.atshoulder = rs_rand(3) == 0;
			in.seated = rs_rand(5) == 0;
			in.slap = rs_rand(4) == 0;
			in.rackpull = (float)rs_rand(90) / 10.0f;
			in.racktravel = (float)(rs_rand(3) * 3);
			in.clipsize = clipsize;
			in.reserve = reserve;
			in.tracked = rs_rand(12) != 0;
			in.paused = rs_rand(15) == 0;

			out = rs_run(&g, &in);
			reserve += out.reservedelta;

			/* a rack effect comes only from a live slap in NEED_RACK */
			if (out.fx & VRR_FX_RACK) {
				slap_racks++;
				if (!(in.slap && !in.paused && before.state == VRR_NEED_RACK)) {
					bad = 1;
				}
				if (before.clip > 0 && !(g.chamber == 1 && g.clip == before.clip - 1)) {
					bad = 1;
				}
				if (g.state == VRR_READY) {
					slap_ready++;
				}
			}
			/* a live slap in NEED_RACK (no eject on the tick) racks */
			if (in.slap && !in.paused && in.tracked && !in.eject
					&& before.state == VRR_NEED_RACK && !(out.fx & VRR_FX_RACK)) {
				bad = 1;
			}
			/* Task F4a: an eject returns the chambered round with the clip and empties the gun */
			if ((out.fx & VRR_FX_EJECT) && (out.reservedelta != before.chamber + before.clip
					|| g.chamber != 0 || g.clip != 0 || g.magin)) {
				bad = 1;
			}

			if (bad || g.state == VRR_RACKING
					|| (out.fx & VRR_FX_WASTE_ROUND)
					|| reserve < 0
					|| g.chamber + g.clip + reserve != total
					|| (g.state == VRR_NEED_RACK && (!g.magin || g.chamber))
					|| (g.magin && !g.chamber && g.clip != 0 && g.state != VRR_NEED_RACK)
					|| (g.magin && g.chamber + g.clip > clipsize)
					|| out.loaded != vrrEngineLoaded(&g)
					|| out.allowfire != vrrAllowFire(&g)
					|| out.handclip != (g.state == VRR_HOLDING_CLIP)) {
				fails++;
				printf("  rs random run %d tick %d: in slap %d paused %d tracked %d eject %d grip %d "
					"| before state %d ch %d clip %d | after state %d magin %d ch %d clip %d "
					"reserve %d total %d fx %#x loaded %d allowfire %d\n",
					run, n, in.slap, in.paused, in.tracked, in.eject, in.grip,
					before.state, before.chamber, before.clip,
					g.state, g.magin, g.chamber, g.clip, reserve, total, out.fx, out.loaded,
					out.allowfire);
			}
			CHECK_EQ(bad, 0);
			CHECK(g.state != VRR_RACKING);
			CHECK_EQ(out.fx & VRR_FX_WASTE_ROUND, 0);
			CHECK(reserve >= 0);
			CHECK_EQ(g.chamber + g.clip + reserve, total);
			CHECK(!(g.state == VRR_NEED_RACK && (!g.magin || g.chamber)));
			CHECK(!(g.magin && !g.chamber && g.clip != 0 && g.state != VRR_NEED_RACK));
			CHECK(!g.magin || g.chamber + g.clip <= clipsize);
			CHECK_EQ(out.loaded, vrrEngineLoaded(&g));
			CHECK_EQ(out.allowfire, vrrAllowFire(&g));
			CHECK_EQ(out.handclip, g.state == VRR_HOLDING_CLIP);
		}
	}
	/* the random play did finish racks with slaps */
	CHECK(slap_racks > 50);
	CHECK(slap_ready > 50);
}

void run_reload_rackslap_tests(void)
{
	test_rs_empty_gun_insert_slap_fires();
	test_rs_slap_equals_rack();
	test_rs_one_round_clip();
	test_rs_empty_clip_slap();
	test_rs_slap_held_several_ticks();
	test_rs_grip_and_pull_ignored();
	test_rs_no_rack_marker();
	test_rs_slap_unflagged_ignored();
	test_rs_slap_other_states_ignored();
	test_rs_slap_paused_ignored();
	test_rs_chambered_insert_ready();
	test_rs_eject_empties_gun();
	test_rs_settle_as_rack_gun();
	test_rs_racking_ends_at_once();
	test_rs_random_play();
}
