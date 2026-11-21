#include <stdint.h>
#include <string.h>
#include "vrtest.h"
#include "game/vr_reload_core.h"

/*
 * Contract tests for Task G0a (pump action) and G0b (shells one at a time), the Shotgun.
 * G0a rule: with vrrinput.pumpaction, firing leaves the chamber empty (no round chambered from
 * the clip, the tube, by the shot), so after each shot the gun needs a rack (the pump) when the
 * tube has rounds, and the trigger is refused until then; the rack chambers one from the tube as
 * for any rack gun. The shot is seen by vrrSyncFromEngineRow(g, loaded, pumpaction).
 * G0b rule: with vrrinput.shellbyshell, the tube never leaves the gun and B does nothing to it;
 * while the tube has room (clipsize minus chamber minus clip above 0) and the reserve has rounds,
 * a grip at the shoulder takes one shell (VRR_HOLDING_CLIP, held 1, magin 1) and an insert at the
 * port adds one to the tube and takes one from the reserve; a drop takes nothing; either way the
 * gun then is in the state its chamber calls for. The rounds always add up.
 * Numbers follow the Shotgun: clipsize 9 (chamber 1 + tube 8), pump travel 7.4 cm.
 */

#define SG_CHECK_GUN(g, st, mi, ch, cl) do { \
	CHECK_EQ((g).state, (st)); \
	CHECK_EQ((g).magin, (mi)); \
	CHECK_EQ((g).chamber, (ch)); \
	CHECK_EQ((g).clip, (cl)); \
} while (0)

#define SG_TRAVEL 7.4f

/* ---------- helpers: every tick carries the flags given ---------- */

static struct vrrinput sg_input(int32_t pump, int32_t sbs)
{
	struct vrrinput in;
	memset(&in, 0, sizeof in);
	in.tracked = 1;
	in.clipsize = 9;
	in.reserve = 20;
	in.racktravel = SG_TRAVEL;
	in.pumpaction = pump;
	in.shellbyshell = sbs;
	return in;
}

static struct vrroutput sg_run(struct vrrgun *g, const struct vrrinput *in)
{
	struct vrroutput out;
	memset(&out, 0x5a, sizeof out);
	vrrTick(g, in, &out);
	return out;
}

static struct vrroutput sg_quiet(struct vrrgun *g, int32_t pump, int32_t sbs)
{
	struct vrrinput in = sg_input(pump, sbs);
	return sg_run(g, &in);
}

static struct vrroutput sg_eject(struct vrrgun *g, int32_t pump, int32_t sbs)
{
	struct vrrinput in = sg_input(pump, sbs);
	in.eject = 1;
	return sg_run(g, &in);
}

static struct vrroutput sg_grab(struct vrrgun *g, int32_t pump, int32_t sbs, int32_t reserve)
{
	struct vrrinput in = sg_input(pump, sbs);
	in.grip = 1;
	in.atshoulder = 1;
	in.reserve = reserve;
	return sg_run(g, &in);
}

static struct vrroutput sg_release(struct vrrgun *g, int32_t pump, int32_t sbs, int32_t inclip,
		int32_t reserve)
{
	struct vrrinput in = sg_input(pump, sbs);
	in.inclip = inclip;
	in.reserve = reserve;
	return sg_run(g, &in);
}

/* The rack grab tick (pull 0) */
static struct vrroutput sg_rack_grab(struct vrrgun *g, int32_t pump, int32_t sbs)
{
	struct vrrinput in = sg_input(pump, sbs);
	in.grip = 1;
	in.inrack = 1;
	return sg_run(g, &in);
}

static struct vrroutput sg_rack_pull(struct vrrgun *g, int32_t pump, int32_t sbs, float cm)
{
	struct vrrinput in = sg_input(pump, sbs);
	in.grip = 1;
	in.inrack = 1;
	in.rackpull = cm;
	return sg_run(g, &in);
}

static struct vrroutput sg_rack_release(struct vrrgun *g, int32_t pump, int32_t sbs)
{
	struct vrrinput in = sg_input(pump, sbs);
	in.inrack = 1;
	return sg_run(g, &in);
}

/* A full pump: grab, pull the full travel, let go. Returns the pull tick's output */
static struct vrroutput sg_pump(struct vrrgun *g, int32_t pump, int32_t sbs)
{
	struct vrroutput out;
	sg_rack_grab(g, pump, sbs);
	out = sg_rack_pull(g, pump, sbs, SG_TRAVEL);
	sg_rack_release(g, pump, sbs);
	return out;
}

/* One shot as the adapter sees it: the engine's count drops by one */
static void sg_fire(struct vrrgun *g, int32_t pump)
{
	int32_t l = vrrEngineLoaded(g);
	CHECK(l > 0);
	vrrSyncFromEngineRow(g, l - 1, pump);
}

/* One shell taken at the shoulder and inserted at the port */
static struct vrroutput sg_load_shell(struct vrrgun *g, int32_t pump, int32_t reserve)
{
	struct vrroutput out = sg_grab(g, pump, 1, reserve);
	CHECK_EQ(out.fx & VRR_FX_GRAB_CLIP, VRR_FX_GRAB_CLIP);
	return sg_release(g, pump, 1, 1, reserve);
}

/* ---------- G0a: the pump ---------- */

static void test_pa_shot_needs_pump(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrrInit(&g, 9);
	SG_CHECK_GUN(g, VRR_READY, 1, 1, 8);

	vrrSyncFromEngineRow(&g, 8, 1);
	/* the shot leaves the chamber empty and the tube as it was */
	SG_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
	CHECK_EQ(vrrEngineLoaded(&g), 0);
	CHECK_EQ(vrrAllowFire(&g), 0);

	out = sg_quiet(&g, 1, 0);
	SG_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
	CHECK_EQ(out.loaded, 0);
	CHECK_EQ(out.allowfire, 0);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_EQ(out.fx, 0);
	CHECK(out.glow & VRR_GLOW_RACK);
}

static void test_pa_trigger_dry_until_pumped(void)
{
	struct vrrgun g;
	struct vrroutput out;
	int i;
	vrrInit(&g, 6);
	sg_fire(&g, 1);
	SG_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 5);

	/* the adapter keeps telling the core the engine's count (0) and ticking: nothing changes */
	for (i = 0; i < 10; i++) {
		vrrSyncFromEngineRow(&g, 0, 1);
		out = sg_quiet(&g, 1, 0);
		SG_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 5);
		CHECK_EQ(out.allowfire, 0);
		CHECK_EQ(out.loaded, 0);
		CHECK_EQ(out.reservedelta, 0);
	}

	/* a pull short of the travel does nothing yet */
	sg_rack_grab(&g, 1, 0);
	CHECK_EQ(g.state, VRR_RACKING);
	out = sg_rack_pull(&g, 1, 0, SG_TRAVEL * 0.5f);
	CHECK_EQ(out.fx & VRR_FX_RACK, 0);
	CHECK_EQ(out.allowfire, 0);
	CHECK_EQ(g.chamber, 0);

	/* the full pump chambers one from the tube: 0 + 5 becomes 1 + 4 */
	out = sg_rack_pull(&g, 1, 0, SG_TRAVEL);
	CHECK_EQ(out.fx, VRR_FX_RACK);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_EQ(out.loaded, 5);
	CHECK_EQ(out.allowfire, 1);
	SG_CHECK_GUN(g, VRR_READY, 1, 1, 4);
	sg_rack_release(&g, 1, 0);
	SG_CHECK_GUN(g, VRR_READY, 1, 1, 4);
}

static void test_pa_fire_pump_until_empty(void)
{
	struct vrrgun g;
	struct vrroutput out;
	int32_t shots = 0;
	vrrInit(&g, 9);

	while (vrrEngineLoaded(&g) > 0 && shots < 20) {
		int32_t before = g.chamber + g.clip;
		CHECK_EQ(vrrAllowFire(&g), 1);
		sg_fire(&g, 1);
		shots++;
		CHECK_EQ(g.chamber, 0);
		CHECK_EQ(g.chamber + g.clip, before - 1);
		if (g.clip > 0) {
			CHECK_EQ(g.state, VRR_NEED_RACK);
			CHECK_EQ(vrrAllowFire(&g), 0);
			out = sg_pump(&g, 1, 0);
			CHECK_EQ(out.fx, VRR_FX_RACK);
			CHECK_EQ(g.state, VRR_READY);
			CHECK_EQ(g.chamber, 1);
			CHECK_EQ(g.chamber + g.clip, before - 1);
		}
	}
	CHECK_EQ(shots, 9);
	/* the last shot leaves the gun dry and READY: the trigger clicks */
	SG_CHECK_GUN(g, VRR_READY, 1, 0, 0);
	out = sg_quiet(&g, 1, 0);
	CHECK_EQ(out.allowfire, 1);
	CHECK_EQ(out.loaded, 0);
}

static void test_pa_last_round_empty_tube(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrrInit(&g, 1);
	SG_CHECK_GUN(g, VRR_READY, 1, 1, 0);
	vrrSyncFromEngineRow(&g, 0, 1);
	/* nothing in the tube: no pump due, READY and dry as a rack gun fired dry */
	SG_CHECK_GUN(g, VRR_READY, 1, 0, 0);
	out = sg_quiet(&g, 1, 0);
	CHECK_EQ(g.state, VRR_READY);
	CHECK_EQ(out.allowfire, 1);
	CHECK_EQ(out.loaded, 0);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(out.reservedelta, 0);
}

/* A pump with an empty tube does what a rack gun's rack does: no rounds made */
static void test_pa_pump_empty_tube_as_rack_gun(void)
{
	struct vrrgun a, b;
	struct vrroutput oa, ob;
	vrrInit(&a, 1);
	vrrSyncFromEngineRow(&a, 0, 1);
	vrrInit(&b, 1);
	vrrSyncFromEngineRow(&b, 0, 0);
	CHECK(memcmp(&a, &b, sizeof a) == 0);

	sg_rack_grab(&a, 1, 0);
	sg_rack_grab(&b, 0, 0);
	oa = sg_rack_pull(&a, 1, 0, SG_TRAVEL);
	ob = sg_rack_pull(&b, 0, 0, SG_TRAVEL);
	CHECK_EQ(oa.fx, ob.fx);
	CHECK_EQ(oa.fx & VRR_FX_WASTE_ROUND, 0);
	CHECK_EQ(oa.reservedelta, 0);
	CHECK(memcmp(&a, &b, sizeof a) == 0);
	CHECK_EQ(a.chamber, 0);
	CHECK_EQ(a.clip, 0);
	CHECK_EQ(oa.loaded, 0);
}

/* A count at or above chamber + clip is no shot: the same as vrrSyncFromEngine */
static void test_pa_sync_not_a_shot(void)
{
	struct vrrgun g;
	vrrInit(&g, 5);
	vrrSyncFromEngineRow(&g, 5, 1);
	SG_CHECK_GUN(g, VRR_READY, 1, 1, 4);
	vrrSyncFromEngineRow(&g, 7, 1);
	SG_CHECK_GUN(g, VRR_READY, 1, 1, 6);
}

/* Without the flag: a shot chambers the next round from the clip, as before */
static void test_pa_flag_off_unchanged(void)
{
	struct vrrgun g;
	vrrInit(&g, 9);
	vrrSyncFromEngineRow(&g, 8, 0);
	SG_CHECK_GUN(g, VRR_READY, 1, 1, 7);
	CHECK_EQ(vrrAllowFire(&g), 1);
	vrrSyncFromEngineRow(&g, 7, 0);
	SG_CHECK_GUN(g, VRR_READY, 1, 1, 6);
}

static uint32_t sg_seed;

static int sg_rand(int n)
{
	sg_seed = sg_seed * 1103515245u + 12345u;
	return (int)(((sg_seed >> 16) & 0x7fff) % (uint32_t)n);
}

static struct vrrinput sg_random_input(int32_t clipsize, int32_t reserve, int32_t *grip)
{
	struct vrrinput in = sg_input(0, 0);
	if (sg_rand(4) == 0) {
		*grip = !*grip;
	}
	in.grip = *grip;
	in.eject = sg_rand(10) == 0;
	in.inclip = sg_rand(3) == 0;
	in.inrack = sg_rand(3) == 0;
	in.atshoulder = sg_rand(3) == 0;
	in.seated = sg_rand(5) == 0;
	in.rackpull = (float)sg_rand(90) / 10.0f;
	in.racktravel = (float)(sg_rand(3) * 3);
	in.clipsize = clipsize;
	in.reserve = reserve;
	in.tracked = sg_rand(12) != 0;
	in.paused = sg_rand(15) == 0;
	return in;
}

/* vrrSyncFromEngineRow with the flag 0 is vrrSyncFromEngine, from any state the play reaches */
static void test_pa_row_flag_off_is_plain_sync(void)
{
	int run;
	int32_t compared = 0;
	sg_seed = 777;
	for (run = 0; run < 100; run++) {
		struct vrrgun g;
		int32_t clipsize = 1 + sg_rand(12);
		int32_t reserve = sg_rand(40);
		int32_t grip = 0;
		int n;
		vrrInit(&g, sg_rand(clipsize + 1));
		for (n = 0; n < 200; n++) {
			struct vrrinput in = sg_random_input(clipsize, reserve, &grip);
			struct vrroutput out;
			struct vrrgun a, b;
			int32_t l;
			out = sg_run(&g, &in);
			reserve += out.reservedelta;
			l = sg_rand(clipsize + 2) - 1;
			a = g;
			b = g;
			vrrSyncFromEngineRow(&a, l, 0);
			vrrSyncFromEngineRow(&b, l, 0);
			CHECK(memcmp(&a, &b, sizeof a) == 0);
			compared++;
		}
	}
	CHECK(compared > 1000);
}

/* With no shots, a pumpaction gun ticks exactly as a rack gun: the rack, the clip and the eject */
static void test_pa_ticks_as_rack_gun(void)
{
	int run;
	int32_t racks = 0;
	sg_seed = 4242;
	for (run = 0; run < 150; run++) {
		struct vrrgun a, b;
		int32_t clipsize = 1 + sg_rand(12);
		int32_t reserve = sg_rand(40);
		int32_t grip = 0;
		int n, fails = 0;
		vrrInit(&a, sg_rand(clipsize + 1));
		b = a;
		for (n = 0; n < 300 && fails < 3; n++) {
			struct vrrinput in = sg_random_input(clipsize, reserve, &grip);
			struct vrrinput inp = in;
			struct vrroutput oa, ob;
			inp.pumpaction = 1;
			oa = sg_run(&a, &inp);
			ob = sg_run(&b, &in);
			reserve += ob.reservedelta;
			if (ob.fx & VRR_FX_RACK) {
				racks++;
			}
			if (memcmp(&a, &b, sizeof a) != 0 || memcmp(&oa, &ob, sizeof oa) != 0) {
				fails++;
				printf("  pa differential run %d tick %d: pump state %d ch %d clip %d fx %#x "
					"| rack gun state %d ch %d clip %d fx %#x\n", run, n, a.state, a.chamber,
					a.clip, oa.fx, b.state, b.chamber, b.clip, ob.fx);
			}
			CHECK(memcmp(&a, &b, sizeof a) == 0);
			CHECK(memcmp(&oa, &ob, sizeof oa) == 0);
		}
	}
	CHECK(racks > 50);
}

/* ---------- G0b: shells one at a time ---------- */

static void test_sb_eject_does_nothing(void)
{
	struct vrrgun g, before;
	struct vrroutput out;
	int k;
	for (k = 0; k < 4; k++) {
		vrrInit(&g, 5);                      /* READY 1 + 4 */
		if (k == 1) {
			sg_fire(&g, 1);                  /* NEED_RACK 0 + 4 */
		} else if (k == 2) {
			vrrInit(&g, 0);                  /* READY dry */
		} else if (k == 3) {
			sg_grab(&g, 1, 1, 20);           /* holding a shell */
			CHECK_EQ(g.state, VRR_HOLDING_CLIP);
		}
		before = g;
		CHECK_EQ(vrrEjectReturnsRow(&g, 1), 0);
		CHECK_EQ(vrrEjectReturnsRow(&g, 0), vrrEjectReturnsRow(&g, 0));
		out = sg_eject(&g, 1, 1);
		CHECK_EQ(out.fx & VRR_FX_EJECT, 0);
		CHECK_EQ(out.reservedelta, 0);
		CHECK_EQ(g.magin, 1);
		CHECK_EQ(g.chamber, before.chamber);
		CHECK_EQ(g.clip, before.clip);
		CHECK(g.state != VRR_CLIP_OUT);
		if (k < 3) {
			CHECK_EQ(g.state, before.state);
		}
	}
	/* the row's returns without the flag are the plain ones */
	vrrInit(&g, 5);
	CHECK_EQ(vrrEjectReturnsRow(&g, 0), 5);
	CHECK_EQ(vrrEjectReturnsRow(&g, 0), 5);
}

static void test_sb_grab_needs_room_and_reserve(void)
{
	struct vrrgun g;
	struct vrroutput out;

	/* a full tube (1 + 8 of 9): nothing */
	vrrInit(&g, 9);
	out = sg_grab(&g, 1, 1, 20);
	CHECK_EQ(out.fx & (VRR_FX_GRAB_CLIP | VRR_FX_NO_AMMO), 0);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_EQ(out.handclip, 0);
	SG_CHECK_GUN(g, VRR_READY, 1, 1, 8);
	CHECK_EQ(g.held, 0);
	sg_release(&g, 1, 1, 1, 20);
	SG_CHECK_GUN(g, VRR_READY, 1, 1, 8);

	/* room but an empty reserve: no ammo */
	vrrInit(&g, 5);
	out = sg_grab(&g, 1, 1, 0);
	CHECK_EQ(out.fx & VRR_FX_NO_AMMO, VRR_FX_NO_AMMO);
	CHECK_EQ(out.fx & VRR_FX_GRAB_CLIP, 0);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_EQ(out.handclip, 0);
	SG_CHECK_GUN(g, VRR_READY, 1, 1, 4);
	CHECK_EQ(g.held, 0);
	sg_release(&g, 1, 1, 1, 0);
	SG_CHECK_GUN(g, VRR_READY, 1, 1, 4);

	/* room and reserve: one shell, nothing taken yet, the tube stays in */
	vrrInit(&g, 5);
	out = sg_grab(&g, 1, 1, 20);
	CHECK_EQ(out.fx & VRR_FX_GRAB_CLIP, VRR_FX_GRAB_CLIP);
	CHECK_EQ(out.fx & VRR_FX_NO_AMMO, 0);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_EQ(out.handclip, 1);
	CHECK_EQ(out.allowfire, 1);
	SG_CHECK_GUN(g, VRR_HOLDING_CLIP, 1, 1, 4);
	CHECK_EQ(g.held, 1);
	CHECK_EQ(out.loaded, 5);

	/* a reserve of one is enough */
	vrrInit(&g, 5);
	out = sg_grab(&g, 1, 1, 1);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	CHECK_EQ(g.held, 1);

	/* NEED_RACK with room (0 + 8 of 9): a grab works */
	vrrInit(&g, 9);
	sg_fire(&g, 1);
	SG_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
	out = sg_grab(&g, 1, 1, 20);
	CHECK_EQ(out.fx & VRR_FX_GRAB_CLIP, VRR_FX_GRAB_CLIP);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	CHECK_EQ(out.allowfire, 0);
}

static void test_sb_insert_adds_one(void)
{
	struct vrrgun g;
	struct vrroutput out;
	int32_t seated;
	for (seated = 0; seated <= 1; seated++) {
		vrrInit(&g, 5);
		sg_grab(&g, 1, 1, 20);
		if (seated) {
			struct vrrinput in = sg_input(1, 1);
			in.grip = 1;
			in.inclip = 1;
			in.seated = 1;
			out = sg_run(&g, &in);
		} else {
			out = sg_release(&g, 1, 1, 1, 20);
		}
		CHECK_EQ(out.fx & VRR_FX_INSERT, VRR_FX_INSERT);
		CHECK_EQ(out.fx & VRR_FX_DROP_CLIP, 0);
		CHECK_EQ(out.reservedelta, -1);
		CHECK_EQ(out.handclip, 0);
		CHECK_EQ(out.loaded, 6);
		CHECK_EQ(out.allowfire, 1);
		SG_CHECK_GUN(g, VRR_READY, 1, 1, 5);
		CHECK_EQ(g.held, 0);
		/* the rest of the release does nothing more */
		out = sg_release(&g, 1, 1, 1, 19);
		CHECK_EQ(out.reservedelta, 0);
		SG_CHECK_GUN(g, VRR_READY, 1, 1, 5);
	}
}

static void test_sb_load_to_capacity(void)
{
	struct vrrgun g;
	struct vrroutput out;
	int32_t reserve = 20, loads = 0;

	/* chambered: the tube takes 8 */
	vrrInit(&g, 1);
	while (loads < 20) {
		out = sg_grab(&g, 1, 1, reserve);
		if (!(out.fx & VRR_FX_GRAB_CLIP)) {
			sg_release(&g, 1, 1, 0, reserve); /* let the grip go before the next press */
			break;
		}
		out = sg_release(&g, 1, 1, 1, reserve);
		reserve += out.reservedelta;
		loads++;
		CHECK_EQ(g.state, VRR_READY);
		CHECK_EQ(g.chamber + g.clip + reserve, 21);
	}
	CHECK_EQ(loads, 8);
	SG_CHECK_GUN(g, VRR_READY, 1, 1, 8);
	CHECK_EQ(reserve, 12);
	CHECK_EQ(g.held, 0);

	/* dry: the tube takes 9 (capacity counts the chamber, which is empty), then the pump */
	vrrInit(&g, 0);
	reserve = 20;
	loads = 0;
	while (loads < 20) {
		out = sg_grab(&g, 1, 1, reserve);
		if (!(out.fx & VRR_FX_GRAB_CLIP)) {
			sg_release(&g, 1, 1, 0, reserve); /* let the grip go before the next press */
			break;
		}
		out = sg_release(&g, 1, 1, 1, reserve);
		reserve += out.reservedelta;
		loads++;
		CHECK_EQ(g.state, VRR_NEED_RACK);
		CHECK_EQ(g.chamber + g.clip + reserve, 20);
	}
	CHECK_EQ(loads, 9);
	SG_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 9);
	out = sg_pump(&g, 1, 1);
	CHECK_EQ(out.fx, VRR_FX_RACK);
	SG_CHECK_GUN(g, VRR_READY, 1, 1, 8);
	/* still full */
	out = sg_grab(&g, 1, 1, reserve);
	CHECK_EQ(out.fx & (VRR_FX_GRAB_CLIP | VRR_FX_NO_AMMO), 0);
	CHECK_EQ(g.state, VRR_READY);

	/* the reserve runs out before the tube is full */
	vrrInit(&g, 1);
	reserve = 3;
	loads = 0;
	while (loads < 20) {
		out = sg_grab(&g, 1, 1, reserve);
		if (!(out.fx & VRR_FX_GRAB_CLIP)) {
			CHECK_EQ(out.fx & VRR_FX_NO_AMMO, VRR_FX_NO_AMMO);
			sg_release(&g, 1, 1, 0, reserve);
			break;
		}
		out = sg_release(&g, 1, 1, 1, reserve);
		reserve += out.reservedelta;
		loads++;
	}
	CHECK_EQ(loads, 3);
	CHECK_EQ(reserve, 0);
	SG_CHECK_GUN(g, VRR_READY, 1, 1, 3);
}

static void test_sb_drop_takes_nothing(void)
{
	struct vrrgun g;
	struct vrroutput out;

	/* let go away from the port */
	vrrInit(&g, 5);
	sg_grab(&g, 1, 1, 20);
	out = sg_release(&g, 1, 1, 0, 20);
	CHECK_EQ(out.fx & VRR_FX_DROP_CLIP, VRR_FX_DROP_CLIP);
	CHECK_EQ(out.fx & VRR_FX_INSERT, 0);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_EQ(out.handclip, 0);
	SG_CHECK_GUN(g, VRR_READY, 1, 1, 4);
	CHECK_EQ(g.held, 0);

	/* at the port, but the reserve emptied meanwhile: the shell goes back */
	vrrInit(&g, 5);
	sg_grab(&g, 1, 1, 1);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	out = sg_release(&g, 1, 1, 1, 0);
	CHECK_EQ(out.fx & VRR_FX_DROP_CLIP, VRR_FX_DROP_CLIP);
	CHECK_EQ(out.fx & VRR_FX_INSERT, 0);
	CHECK_EQ(out.reservedelta, 0);
	SG_CHECK_GUN(g, VRR_READY, 1, 1, 4);

	/* dropped from NEED_RACK: back to NEED_RACK */
	vrrInit(&g, 5);
	sg_fire(&g, 1);
	sg_grab(&g, 1, 1, 20);
	out = sg_release(&g, 1, 1, 0, 20);
	CHECK_EQ(out.fx & VRR_FX_DROP_CLIP, VRR_FX_DROP_CLIP);
	CHECK_EQ(out.reservedelta, 0);
	SG_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 4);

	/* dropped from a dry gun: READY and dry */
	vrrInit(&g, 0);
	sg_grab(&g, 1, 1, 20);
	out = sg_release(&g, 1, 1, 0, 20);
	CHECK_EQ(out.reservedelta, 0);
	SG_CHECK_GUN(g, VRR_READY, 1, 0, 0);
	CHECK_EQ(out.allowfire, 1);
}

static void test_sb_insert_need_rack_stays(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrrInit(&g, 5);
	sg_fire(&g, 1);
	SG_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 4);
	out = sg_load_shell(&g, 1, 20);
	CHECK_EQ(out.fx & VRR_FX_INSERT, VRR_FX_INSERT);
	CHECK_EQ(out.reservedelta, -1);
	SG_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 5);
	CHECK_EQ(out.allowfire, 0);
	CHECK_EQ(out.loaded, 0);
	out = sg_pump(&g, 1, 1);
	CHECK_EQ(out.fx, VRR_FX_RACK);
	SG_CHECK_GUN(g, VRR_READY, 1, 1, 4);
}

static void test_sb_dry_gun_shell_then_pump(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrrInit(&g, 0);
	SG_CHECK_GUN(g, VRR_READY, 1, 0, 0);
	out = sg_load_shell(&g, 1, 20);
	CHECK_EQ(out.reservedelta, -1);
	SG_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 1);
	CHECK_EQ(out.allowfire, 0);
	CHECK_EQ(out.loaded, 0);
	out = sg_pump(&g, 1, 1);
	CHECK_EQ(out.fx, VRR_FX_RACK);
	SG_CHECK_GUN(g, VRR_READY, 1, 1, 0);
	CHECK_EQ(vrrEngineLoaded(&g), 1);
	sg_fire(&g, 1);
	SG_CHECK_GUN(g, VRR_READY, 1, 0, 0);
	CHECK_EQ(vrrAllowFire(&g), 1);
}

/* A gun fired dry and pumped (READY and dry, b11b32d7a) still takes a shell */
static void test_sb_pumped_dry_takes_shell(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrrInit(&g, 1);
	sg_fire(&g, 1);
	sg_pump(&g, 1, 1);
	SG_CHECK_GUN(g, VRR_READY, 1, 0, 0);
	CHECK_EQ(vrrAllowFire(&g), 1);
	out = sg_load_shell(&g, 1, 20);
	CHECK_EQ(out.reservedelta, -1);
	SG_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 1);
}

static void test_sb_no_rack_while_holding(void)
{
	struct vrrgun g;
	struct vrroutput out;
	struct vrrinput in;
	vrrInit(&g, 5);
	sg_fire(&g, 1);
	sg_grab(&g, 1, 1, 20);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);

	/* the hand with the shell goes to the pump and pulls: no rack */
	in = sg_input(1, 1);
	in.grip = 1;
	in.inrack = 1;
	out = sg_run(&g, &in);
	in.rackpull = SG_TRAVEL + 2.0f;
	out = sg_run(&g, &in);
	CHECK_EQ(out.fx & VRR_FX_RACK, 0);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	CHECK_EQ(g.held, 1);
	CHECK_EQ(g.chamber, 0);
	CHECK_EQ(g.clip, 4);

	/* let go there: the shell drops, the gun still needs the pump */
	out = sg_rack_release(&g, 1, 1);
	CHECK_EQ(out.fx & VRR_FX_DROP_CLIP, VRR_FX_DROP_CLIP);
	CHECK_EQ(out.reservedelta, 0);
	SG_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 4);

	/* a fresh grip on the pump racks */
	out = sg_pump(&g, 1, 1);
	CHECK_EQ(out.fx, VRR_FX_RACK);
	SG_CHECK_GUN(g, VRR_READY, 1, 1, 3);
}

static void test_sb_fire_while_holding(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrrInit(&g, 4);
	sg_grab(&g, 1, 1, 20);
	SG_CHECK_GUN(g, VRR_HOLDING_CLIP, 1, 1, 3);
	CHECK_EQ(vrrAllowFire(&g), 1);
	sg_fire(&g, 1);
	CHECK_EQ(g.chamber, 0);
	CHECK_EQ(g.clip, 3);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	CHECK_EQ(vrrAllowFire(&g), 0);
	out = sg_release(&g, 1, 1, 1, 20);
	CHECK_EQ(out.reservedelta, -1);
	SG_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 4);
}

static void test_sb_tube_put_back(void)
{
	struct vrrgun g;
	struct vrroutput out;

	vrtest_restored_clip_out(&g, 5);
	SG_CHECK_GUN(g, VRR_CLIP_OUT, 0, 1, 0);
	out = sg_quiet(&g, 1, 1);
	CHECK_EQ(out.reservedelta, 0);
	SG_CHECK_GUN(g, VRR_READY, 1, 1, 0);
	CHECK_EQ(out.handclip, 0);

	vrtest_restored_clip_out(&g, 0);
	out = sg_quiet(&g, 1, 1);
	CHECK_EQ(out.reservedelta, 0);
	SG_CHECK_GUN(g, VRR_READY, 1, 0, 0);
	CHECK_EQ(out.allowfire, 1);

	/* and it then takes shells */
	out = sg_load_shell(&g, 1, 20);
	CHECK_EQ(out.reservedelta, -1);
	SG_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 1);
}

/* A shot and a shell grab in the same frame (the adapter syncs the engine's count, then ticks):
 * the shot's room counts for the grab, and the shell then goes in with the gun needing the pump */
static void test_sg_shot_and_grab_same_tick(void)
{
	struct vrrgun g;
	struct vrroutput out;
	int32_t reserve = 20;

	/* a full tube: the shot makes the room the grab takes */
	vrrInit(&g, 9);
	vrrSyncFromEngineRow(&g, 8, 1);
	out = sg_grab(&g, 1, 1, reserve);
	CHECK_EQ(out.fx & VRR_FX_GRAB_CLIP, VRR_FX_GRAB_CLIP);
	SG_CHECK_GUN(g, VRR_HOLDING_CLIP, 1, 0, 8);
	CHECK_EQ(g.held, 1);
	CHECK_EQ(out.allowfire, 0);
	CHECK_EQ(out.loaded, 0);
	CHECK_EQ(out.reservedelta, 0);
	out = sg_release(&g, 1, 1, 1, reserve);
	reserve += out.reservedelta;
	SG_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 9);
	CHECK_EQ(reserve, 19);
	CHECK_EQ(g.chamber + g.clip + reserve + 1, 9 + 20);
	sg_pump(&g, 1, 1);
	SG_CHECK_GUN(g, VRR_READY, 1, 1, 8);

	/* room already: the grab and the shot both happen, nothing lost */
	vrrInit(&g, 5);
	reserve = 20;
	vrrSyncFromEngineRow(&g, 4, 1);
	out = sg_grab(&g, 1, 1, reserve);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	out = sg_release(&g, 1, 1, 1, reserve);
	reserve += out.reservedelta;
	SG_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 5);
	CHECK_EQ(g.chamber + g.clip + reserve + 1, 5 + 20);

	/* the last chambered round with an empty tube and the grab: dry, then the shell needs the pump */
	vrrInit(&g, 1);
	reserve = 20;
	vrrSyncFromEngineRow(&g, 0, 1);
	out = sg_grab(&g, 1, 1, reserve);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	out = sg_release(&g, 1, 1, 1, reserve);
	SG_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 1);
}

/* A larger engine count while a pump gun waits for the pump (the engine is told 0 loaded there):
 * as vrrSyncFromEngine, an empty chamber takes no count, so nothing changes and the pump still
 * works */
static void test_pa_larger_count_in_need_rack(void)
{
	struct vrrgun g, plain;
	struct vrroutput out;
	int32_t l;
	for (l = 5; l <= 12; l++) {
		vrrInit(&g, 6);
		sg_fire(&g, 1);
		SG_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 5);
		plain = g;
		vrrSyncFromEngineRow(&plain, l, 0);
		vrrSyncFromEngineRow(&g, l, 1);
		CHECK(memcmp(&g, &plain, sizeof g) == 0);
		SG_CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 5);
		CHECK_EQ(vrrAllowFire(&g), 0);
		out = sg_pump(&g, 1, 1);
		CHECK_EQ(out.fx, VRR_FX_RACK);
		SG_CHECK_GUN(g, VRR_READY, 1, 1, 4);
	}
}

/* ---------- the Shotgun: both flags in random play ---------- */

static void sg_random_play(int32_t pump, int32_t sbs, uint32_t seed, int32_t *shots_out,
		int32_t *inserts_out, int32_t *pumps_out)
{
	int run;
	sg_seed = seed;

	for (run = 0; run < 200; run++) {
		struct vrrgun g;
		int32_t clipsize = sbs ? 2 + sg_rand(10) : 1 + sg_rand(12);
		int32_t reserve = sg_rand(40);
		int32_t total, shots = 0, wasted = 0;
		int32_t grip = 0;
		int n, fails = 0;

		vrrInit(&g, sg_rand(clipsize + 1));
		total = g.chamber + g.clip + reserve;

		for (n = 0; n < 400 && fails < 3; n++) {
			struct vrrinput in = sg_random_input(clipsize, reserve, &grip);
			struct vrroutput out;
			struct vrrgun before;
			int32_t bad = 0;

			in.pumpaction = pump;
			in.shellbyshell = sbs;
			in.racktravel = sg_rand(2) ? SG_TRAVEL : 0.0f;
			if (sg_rand(5) == 0 && vrrAllowFire(&g)) {
				int32_t l = vrrEngineLoaded(&g);
				if (l > 0) {
					struct vrrgun pre = g;
					vrrSyncFromEngineRow(&g, l - 1, pump);
					shots++;
					*shots_out += 1;
					if (pump && (g.chamber != 0 || g.clip != pre.clip
							|| (pre.state == VRR_READY && pre.clip > 0
								&& g.state != VRR_NEED_RACK))) {
						bad = 1;
					}
				}
			}
			/* the adapter's every-tick sync with the engine's own count changes nothing */
			before = g;
			vrrSyncFromEngineRow(&g, vrrEngineLoaded(&g), pump);
			if (memcmp(&before, &g, sizeof g) != 0) {
				bad = 1;
			}

			out = sg_run(&g, &in);
			reserve += out.reservedelta;
			if (out.fx & VRR_FX_WASTE_ROUND) {
				wasted++;
			}
			if (out.fx & VRR_FX_INSERT) {
				*inserts_out += 1;
			}
			if ((out.fx & VRR_FX_RACK) && g.chamber) {
				*pumps_out += 1;
			}

			if (sbs) {
				int32_t room = clipsize - before.chamber - before.clip;
				/* the tube never leaves and B does nothing */
				if (!g.magin || g.state == VRR_CLIP_OUT || (out.fx & VRR_FX_EJECT)) {
					bad = 1;
				}
				/* one shell at most, held only while holding */
				if (g.held != (g.state == VRR_HOLDING_CLIP ? 1 : 0)) {
					bad = 1;
				}
				/* a grab only with room and reserve */
				if ((out.fx & VRR_FX_GRAB_CLIP) && (room <= 0 || in.reserve <= 0)) {
					bad = 1;
				}
				/* an insert adds one and takes one */
				if ((out.fx & VRR_FX_INSERT) && (out.reservedelta != -1
						|| g.clip != before.clip + 1)) {
					bad = 1;
				}
				if (!(out.fx & VRR_FX_INSERT) && out.reservedelta != 0) {
					bad = 1;
				}
				/* a let-go settles as the chamber calls for */
				if ((out.fx & (VRR_FX_INSERT | VRR_FX_DROP_CLIP))
						&& g.state != (g.chamber ? VRR_READY
							: (g.clip ? VRR_NEED_RACK : VRR_READY))) {
					bad = 1;
				}
			}
			if (pump && !(out.fx & VRR_FX_RACK) && g.state == VRR_READY && !g.chamber
					&& g.clip > 0) {
				bad = 1;
			}

			if (bad || reserve < 0
					|| g.chamber + g.clip + reserve + shots + wasted != total
					|| (g.state == VRR_NEED_RACK && (!g.magin || g.chamber || !g.clip))
					|| (g.magin && !g.chamber && g.clip != 0
						&& g.state != VRR_NEED_RACK && g.state != VRR_RACKING
						&& g.state != VRR_HOLDING_CLIP)
					|| (g.magin && g.chamber + g.clip > clipsize)
					|| out.loaded != vrrEngineLoaded(&g)
					|| out.allowfire != vrrAllowFire(&g)
					|| out.handclip != (g.state == VRR_HOLDING_CLIP)) {
				fails++;
				printf("  sg random pump %d sbs %d run %d tick %d: in eject %d grip %d shoulder %d "
					"inclip %d inrack %d reserve %d | before state %d ch %d clip %d held %d "
					"| after state %d magin %d ch %d clip %d held %d fx %#x delta %d "
					"| total %d shots %d wasted %d\n",
					pump, sbs, run, n, in.eject, in.grip, in.atshoulder, in.inclip, in.inrack,
					in.reserve, before.state, before.chamber, before.clip, before.held,
					g.state, g.magin, g.chamber, g.clip, g.held, out.fx, out.reservedelta,
					total, shots, wasted);
			}
			CHECK_EQ(bad, 0);
			CHECK(reserve >= 0);
			CHECK_EQ(g.chamber + g.clip + reserve + shots + wasted, total);
			CHECK(!g.magin || g.chamber + g.clip <= clipsize);
			CHECK_EQ(out.loaded, vrrEngineLoaded(&g));
			CHECK_EQ(out.allowfire, vrrAllowFire(&g));
			CHECK_EQ(out.handclip, g.state == VRR_HOLDING_CLIP);
		}
	}
}

static void test_sg_random_play(void)
{
	int32_t shots = 0, inserts = 0, pumps = 0;
	/* the Shotgun */
	sg_random_play(1, 1, 9001, &shots, &inserts, &pumps);
	CHECK(shots > 200);
	CHECK(inserts > 200);
	CHECK(pumps > 100);
	/* each flag alone */
	shots = inserts = pumps = 0;
	sg_random_play(1, 0, 9002, &shots, &inserts, &pumps);
	CHECK(shots > 200);
	CHECK(pumps > 50);
	shots = inserts = pumps = 0;
	sg_random_play(0, 1, 9003, &shots, &inserts, &pumps);
	CHECK(shots > 200);
	CHECK(inserts > 200);
}

void run_reload_shotgun_tests(void)
{
	test_pa_shot_needs_pump();
	test_pa_trigger_dry_until_pumped();
	test_pa_fire_pump_until_empty();
	test_pa_last_round_empty_tube();
	test_pa_pump_empty_tube_as_rack_gun();
	test_pa_sync_not_a_shot();
	test_pa_flag_off_unchanged();
	test_pa_row_flag_off_is_plain_sync();
	test_pa_ticks_as_rack_gun();
	test_sb_eject_does_nothing();
	test_sb_grab_needs_room_and_reserve();
	test_sb_insert_adds_one();
	test_sb_load_to_capacity();
	test_sb_drop_takes_nothing();
	test_sb_insert_need_rack_stays();
	test_sb_dry_gun_shell_then_pump();
	test_sb_pumped_dry_takes_shell();
	test_sb_no_rack_while_holding();
	test_sb_fire_while_holding();
	test_sb_tube_put_back();
	test_sg_shot_and_grab_same_tick();
	test_pa_larger_count_in_need_rack();
	test_sg_random_play();
}
