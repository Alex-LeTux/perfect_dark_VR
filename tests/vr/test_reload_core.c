#include <string.h>
#include "vrtest.h"
#include "game/vr_reload_core.h"

/*
 * Contract tests for the VR manual-reload state machine (vr_reload_core.h).
 * Default numbers follow the Falcon: clipsize 8, so a full gun reports loaded 8
 * (chamber 1 + clip 7).
 */

#define BASE_GLOW (VRR_GLOW_CLIP | VRR_GLOW_RACK)

#define CHECK_GUN(g, st, mi, ch, cl) do { \
	CHECK_EQ((g).state, (st)); \
	CHECK_EQ((g).magin, (mi)); \
	CHECK_EQ((g).chamber, (ch)); \
	CHECK_EQ((g).clip, (cl)); \
} while (0)

/* ---------- helpers ---------- */

static struct vrrinput def_input(void)
{
	struct vrrinput in;
	memset(&in, 0, sizeof in);
	in.tracked = 1;
	in.clipsize = 8;
	in.reserve = 20;
	return in;
}

/* Runs one tick. The output is prefilled with garbage so every field must be written. */
static struct vrroutput run_tick(struct vrrgun *g, const struct vrrinput *in)
{
	struct vrroutput out;
	memset(&out, 0x5a, sizeof out);
	vrrTick(g, in, &out);
	return out;
}

static struct vrroutput t_quiet(struct vrrgun *g)
{
	struct vrrinput in = def_input();
	return run_tick(g, &in);
}

static struct vrroutput t_eject(struct vrrgun *g)
{
	struct vrrinput in = def_input();
	in.eject = 1;
	return run_tick(g, &in);
}

static struct vrroutput t_shoulder_grab(struct vrrgun *g, int32_t reserve)
{
	struct vrrinput in = def_input();
	in.grip = 1;
	in.atshoulder = 1;
	in.reserve = reserve;
	return run_tick(g, &in);
}

static struct vrroutput t_hold(struct vrrgun *g, int32_t inclip)
{
	struct vrrinput in = def_input();
	in.grip = 1;
	in.inclip = inclip;
	return run_tick(g, &in);
}

static struct vrroutput t_release(struct vrrgun *g, int32_t inclip)
{
	struct vrrinput in = def_input();
	in.inclip = inclip;
	return run_tick(g, &in);
}

static struct vrroutput t_rack_pull(struct vrrgun *g, float cm)
{
	struct vrrinput in = def_input();
	in.grip = 1;
	in.inrack = 1;
	in.rackpull = cm;
	return run_tick(g, &in);
}

/* Rack pull with an explicit full rack travel (the gun's slide travel; 0 means VRR_RACK_PULL_CM). */
static struct vrroutput t_rack_pull_tr(struct vrrgun *g, float cm, float travel)
{
	struct vrrinput in = def_input();
	in.grip = 1;
	in.inrack = 1;
	in.rackpull = cm;
	in.racktravel = travel;
	return run_tick(g, &in);
}

/* Grip held with the clip pushed fully home (seated) while the hand is or is not in the clip zone. */
static struct vrroutput t_seat(struct vrrgun *g, int32_t inclip)
{
	struct vrrinput in = def_input();
	in.grip = 1;
	in.inclip = inclip;
	in.seated = 1;
	return run_tick(g, &in);
}

static struct vrroutput t_rack_grab(struct vrrgun *g)
{
	return t_rack_pull(g, 0.0f);
}

static struct vrroutput t_rack_release(struct vrrgun *g)
{
	struct vrrinput in = def_input();
	in.inrack = 1;
	return run_tick(g, &in);
}

/* HOLDING_CLIP with a full clip (reserve 20) in the off-hand. */
static void make_holding(struct vrrgun *g, int32_t loaded)
{
	vrtest_restored_clip_out(g, loaded);
	t_shoulder_grab(g, 20);
}

/* NEED_RACK: chamber 0, clip 8, grip released. */
static void make_need_rack(struct vrrgun *g)
{
	vrrInit(g, 0);
	t_eject(g);
	t_shoulder_grab(g, 20);
	t_release(g, 1);
}

/* ---------- vrrInit / vrrEngineLoaded ---------- */

static void test_init_full_gun(void)
{
	struct vrrgun g;
	memset(&g, 0x5a, sizeof g);
	vrrInit(&g, 8);
	CHECK_GUN(g, VRR_READY, 1, 1, 7);
	CHECK_EQ(g.held, 0);
	CHECK_EQ(g.racked, 0);
	CHECK_EQ(g.prevgrip, 0);
	CHECK_EQ(g.prevhot, 0);
	CHECK_EQ(vrrEngineLoaded(&g), 8);
}

static void test_init_empty_gun(void)
{
	struct vrrgun g;
	memset(&g, 0x5a, sizeof g);
	vrrInit(&g, 0);
	CHECK_GUN(g, VRR_READY, 1, 0, 0);
	CHECK_EQ(g.held, 0);
	CHECK_EQ(g.racked, 0);
	CHECK_EQ(g.prevgrip, 0);
	CHECK_EQ(g.prevhot, 0);
	CHECK_EQ(vrrEngineLoaded(&g), 0);
}

static void test_init_negative_loaded_is_empty(void)
{
	struct vrrgun g;
	memset(&g, 0x5a, sizeof g);
	vrrInit(&g, -3);
	CHECK_GUN(g, VRR_READY, 1, 0, 0);
	CHECK_EQ(vrrEngineLoaded(&g), 0);
}

static void test_init_one_round_is_chamber_only(void)
{
	struct vrrgun g;
	vrrInit(&g, 1);
	CHECK_GUN(g, VRR_READY, 1, 1, 0);
	CHECK_EQ(vrrEngineLoaded(&g), 1);
}

static void test_engine_loaded_is_zero_without_chamber(void)
{
	struct vrrgun g;
	vrrInit(&g, 8);
	g.chamber = 0;
	g.clip = 5;
	CHECK_EQ(vrrEngineLoaded(&g), 0);
	g.chamber = 1;
	g.clip = 4;
	CHECK_EQ(vrrEngineLoaded(&g), 5);
	g.clip = 0;
	g.magin = 0;
	CHECK_EQ(vrrEngineLoaded(&g), 1);
}

/* ---------- vrrSyncFromEngine ---------- */

static void test_sync_fire_three_rounds(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrrInit(&g, 8);
	vrrSyncFromEngineRow(&g, 5, 0);
	CHECK_GUN(g, VRR_READY, 1, 1, 4);
	CHECK_EQ(vrrEngineLoaded(&g), 5);
	out = t_quiet(&g);
	CHECK_EQ(out.loaded, 5);
	CHECK_EQ(out.allowfire, 1);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_EQ(g.state, VRR_READY);
}

static void test_sync_copies_engine_count(void)
{
	struct vrrgun g;
	vrrInit(&g, 3);
	vrrSyncFromEngineRow(&g, 6, 0);
	CHECK_GUN(g, VRR_READY, 1, 1, 5);
}

static void test_sync_negative_loaded_empties_gun(void)
{
	struct vrrgun g;
	vrrInit(&g, 8);
	vrrSyncFromEngineRow(&g, -1, 0);
	CHECK_GUN(g, VRR_READY, 1, 0, 0);
}

static void test_sync_ignored_when_chamber_empty(void)
{
	struct vrrgun g;
	make_need_rack(&g);
	CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
	vrrSyncFromEngineRow(&g, 5, 0);
	CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
	vrrSyncFromEngineRow(&g, 0, 0);
	CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
}

static void test_sync_with_clip_out_keeps_chamber_only(void)
{
	struct vrrgun g;
	vrtest_restored_clip_out(&g, 5);
	CHECK_GUN(g, VRR_CLIP_OUT, 0, 1, 0);
	vrrSyncFromEngineRow(&g, 1, 0);
	CHECK_GUN(g, VRR_CLIP_OUT, 0, 1, 0);
	vrrSyncFromEngineRow(&g, 4, 0);
	CHECK_GUN(g, VRR_CLIP_OUT, 0, 1, 0);
	vrrSyncFromEngineRow(&g, 0, 0);
	CHECK_GUN(g, VRR_CLIP_OUT, 0, 0, 0);
}

/* ---------- fire dry ---------- */

static void test_fire_dry_stays_ready(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrrInit(&g, 8);
	vrrSyncFromEngineRow(&g, 0, 0);
	CHECK_GUN(g, VRR_READY, 1, 0, 0);
	out = t_quiet(&g);
	CHECK_EQ(g.state, VRR_READY);
	CHECK_EQ(out.loaded, 0);
	CHECK_EQ(out.glow, 0);
	CHECK_EQ(out.allowfire, 1);
	CHECK_EQ(out.fx, 0);
}

static void test_fire_dry_then_eject(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrrInit(&g, 8);
	vrrSyncFromEngineRow(&g, 0, 0);
	out = t_eject(&g);
	CHECK_EQ(out.fx, VRR_FX_EJECT);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_GUN(g, VRR_CLIP_OUT, 0, 0, 0);
	CHECK_EQ(out.glow, VRR_GLOW_CLIP); /* clip out: never the rack (89fbd7a89) */
	CHECK_EQ(out.loaded, 0);
	CHECK_EQ(out.allowfire, 0);
}

static void test_rack_dry_gun_stays_ready_dry(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrrInit(&g, 8);
	vrrSyncFromEngineRow(&g, 0, 0);
	out = t_rack_grab(&g);
	CHECK_EQ(g.state, VRR_RACKING);
	CHECK_EQ(g.racked, 0);
	CHECK_EQ(out.fx, VRR_FX_ZONE_ENTER);
	/* the full pull racks and ends the grab on the same tick, grip still held */
	out = t_rack_pull(&g, 3.0f);
	CHECK_EQ(out.fx, VRR_FX_RACK);
	/* nothing in the clip to chamber: READY and dry, the trigger clicks, no rack glow */
	CHECK_GUN(g, VRR_READY, 1, 0, 0);
	CHECK_EQ(g.racked, 1);
	CHECK_EQ(out.loaded, 0);
	CHECK_EQ(out.glow & (VRR_GLOW_RACK | VRR_HOT_RACK), 0);
	CHECK_EQ(out.allowfire, 1);
	out = t_rack_release(&g);
	CHECK_GUN(g, VRR_READY, 1, 0, 0);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(out.glow & (VRR_GLOW_RACK | VRR_HOT_RACK), 0);
	CHECK_EQ(out.allowfire, 1);
	CHECK_EQ(out.loaded, 0);
}

/* Commits b11b32d7a and c032e6c3e: a gun with the clip in, nothing in the clip and an empty
 * chamber is READY and dry (the trigger allowed and clicking, no rack glow) on every path:
 * vrrSettle, an empty-clip insert, a full rack, a rack let go early. NEED_RACK means an empty
 * chamber with rounds in the clip. A rack with the clip out still ends in CLIP_OUT */
static void check_ready_dry(struct vrrgun *g, const struct vrroutput *out)
{
	CHECK_GUN(*g, VRR_READY, 1, 0, 0);
	CHECK_EQ(out->glow & (VRR_GLOW_RACK | VRR_HOT_RACK), 0);
	CHECK_EQ(out->allowfire, 1);
	CHECK_EQ(out->loaded, 0);
	CHECK_EQ(vrrAllowFire(g), 1);
}

static void test_rack_empty_no_glow_trigger_dry(void)
{
	struct vrrgun g;
	struct vrrinput in;
	struct vrroutput out;

	/* vrrSettle */
	vrrInit(&g, 8);
	g.chamber = 0;
	g.clip = 0;
	g.state = VRR_NEED_RACK;
	vrrSettle(&g, 0);
	CHECK_GUN(g, VRR_READY, 1, 0, 0);
	out = t_quiet(&g);
	check_ready_dry(&g, &out);

	/* a rack let go early */
	t_rack_grab(&g);
	CHECK_EQ(g.state, VRR_RACKING);
	t_rack_pull(&g, 1.0f);
	out = t_rack_release(&g);
	CHECK_EQ(out.fx & VRR_FX_RACK, 0);
	check_ready_dry(&g, &out);

	/* the full rack, the hand still in the rack zone: not hot, the zone is dark */
	t_quiet(&g);
	t_rack_grab(&g);
	out = t_rack_pull(&g, 3.0f);
	CHECK_EQ(out.fx, VRR_FX_RACK);
	CHECK_EQ(out.reservedelta, 0);
	check_ready_dry(&g, &out);
	out = t_rack_release(&g);
	check_ready_dry(&g, &out);
	out = t_quiet(&g);
	check_ready_dry(&g, &out);

	/* an empty clip inserted (clipsize 0 gives one) */
	vrtest_restored_clip_out(&g, 0);
	in = def_input();
	in.clipsize = 0;
	in.grip = 1;
	in.atshoulder = 1;
	run_tick(&g, &in);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	CHECK_EQ(g.held, 0);
	in = def_input();
	in.clipsize = 0;
	in.inclip = 1;
	out = run_tick(&g, &in);
	CHECK_EQ(out.fx & VRR_FX_INSERT, VRR_FX_INSERT);
	check_ready_dry(&g, &out);

	/* a rack with the clip out still ends in CLIP_OUT, the rack zone dark */
	vrtest_restored_clip_out(&g, 0);
	t_rack_grab(&g);
	out = t_rack_pull(&g, 3.0f);
	CHECK_EQ(out.fx, VRR_FX_RACK);
	CHECK_GUN(g, VRR_CLIP_OUT, 0, 0, 0);
	CHECK_EQ(out.allowfire, 0);
	CHECK_EQ(out.glow, VRR_GLOW_CLIP);
}

/* ---------- full reload sequences ---------- */

/* Task F4a: no tactical reload. B on a partly fired gun returns the clip and the chambered round;
 * the new clip needs a rack. Rounds add up throughout: 5 in the gun + 20 in reserve. */
static void test_reload_of_loaded_gun_needs_rack(void)
{
	struct vrrgun g;
	struct vrroutput out;
	int32_t reserve = 20;
	vrrInit(&g, 8);
	vrrSyncFromEngineRow(&g, 5, 0);
	CHECK_EQ(vrrEjectReturnsRow(&g, 0), 5);

	out = t_eject(&g);
	reserve += out.reservedelta;
	CHECK_EQ(out.fx, VRR_FX_EJECT);
	CHECK_EQ(out.reservedelta, 5);
	CHECK_GUN(g, VRR_CLIP_OUT, 0, 0, 0);
	CHECK_EQ(out.glow, VRR_GLOW_CLIP);
	CHECK_EQ(out.loaded, 0);
	CHECK_EQ(out.allowfire, 0);
	CHECK_EQ(out.handclip, 0);
	CHECK_EQ(g.chamber + g.clip + reserve, 25);

	out = t_shoulder_grab(&g, reserve);
	CHECK_EQ(out.fx, VRR_FX_GRAB_CLIP);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	CHECK_EQ(g.held, 8);
	CHECK_EQ(g.magin, 0);
	CHECK_EQ(out.handclip, 1);
	CHECK_EQ(out.loaded, 0);
	CHECK_EQ(out.allowfire, 0);

	out = t_hold(&g, 1);
	CHECK_EQ(out.fx, VRR_FX_ZONE_ENTER);
	CHECK_EQ(out.glow, VRR_GLOW_CLIP | VRR_HOT_CLIP);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);

	out = t_release(&g, 1);
	reserve += out.reservedelta;
	CHECK_EQ(out.fx, VRR_FX_INSERT);
	CHECK_EQ(out.reservedelta, -8);
	CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
	CHECK_EQ(g.held, 0);
	CHECK_EQ(out.loaded, 0);
	CHECK_EQ(out.glow, VRR_GLOW_RACK);
	CHECK_EQ(out.allowfire, 0);
	CHECK_EQ(out.handclip, 0);
	CHECK_EQ(g.chamber + g.clip + reserve, 25);

	t_rack_grab(&g);
	out = t_rack_pull(&g, 3.0f);
	CHECK_EQ(out.fx, VRR_FX_RACK);
	CHECK_GUN(g, VRR_READY, 1, 1, 7);
	CHECK_EQ(out.loaded, 8);
	CHECK_EQ(out.allowfire, 1);
	CHECK_EQ(reserve, 17);
	CHECK_EQ(g.chamber + g.clip + reserve, 25);
}

/* Task F4a, every load: B returns the clip plus the chambered round (as vrrEjectReturns says),
 * leaves the gun empty, and any new clip, full or partial, released or seated, needs a rack.
 * Rounds add up. */
static void test_eject_any_load_returns_all_then_rack(void)
{
	struct vrrgun g;
	struct vrroutput out;
	int32_t loaded, seated, part;

	for (seated = 0; seated <= 1; seated++) {
		for (part = 0; part <= 1; part++) {
			for (loaded = 1; loaded <= 8; loaded++) {
				int32_t reserve = part ? 0 : 20, total, want;
				vrrInit(&g, 8);
				vrrSyncFromEngineRow(&g, loaded, 0);
				total = g.chamber + g.clip + reserve;
				CHECK_EQ(vrrEjectReturnsRow(&g, 0), loaded);
				out = t_eject(&g);
				reserve += out.reservedelta;
				CHECK_EQ(out.fx, VRR_FX_EJECT);
				CHECK_EQ(out.reservedelta, loaded);
				CHECK_GUN(g, VRR_CLIP_OUT, 0, 0, 0);
				CHECK_EQ(out.loaded, 0);
				CHECK_EQ(out.allowfire, 0);
				CHECK_EQ(g.chamber + g.clip + reserve, total);

				/* part: only the returned rounds are in reserve, so the new clip is partial
				 * unless the gun was full */
				want = reserve < 8 ? reserve : 8;
				t_shoulder_grab(&g, reserve);
				CHECK_EQ(g.held, want);
				out = seated ? t_seat(&g, 1) : t_release(&g, 1);
				reserve += out.reservedelta;
				CHECK_EQ(out.fx, VRR_FX_INSERT);
				CHECK_EQ(out.reservedelta, -want);
				CHECK_GUN(g, VRR_NEED_RACK, 1, 0, want);
				CHECK_EQ(out.allowfire, 0);
				CHECK_EQ(g.chamber + g.clip + reserve, total);

				if (seated) {
					t_release(&g, 1);
				}
				t_rack_grab(&g);
				out = t_rack_pull(&g, 3.0f);
				reserve += out.reservedelta;
				CHECK_EQ(out.fx, VRR_FX_RACK);
				CHECK_GUN(g, VRR_READY, 1, 1, want - 1);
				CHECK_EQ(out.loaded, want);
				CHECK_EQ(g.chamber + g.clip + reserve, total);
			}
		}
	}
}

static void test_empty_reload_then_rack(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrrInit(&g, 8);
	vrrSyncFromEngineRow(&g, 0, 0);

	out = t_eject(&g);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_EQ(out.fx, VRR_FX_EJECT);
	CHECK_EQ(out.glow, VRR_GLOW_CLIP);

	out = t_shoulder_grab(&g, 20);
	CHECK_EQ(out.fx, VRR_FX_GRAB_CLIP);
	CHECK_EQ(g.held, 8);

	out = t_release(&g, 1);
	CHECK_EQ(out.fx, VRR_FX_INSERT);
	CHECK_EQ(out.reservedelta, -8);
	CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
	CHECK_EQ(g.held, 0);
	CHECK_EQ(out.loaded, 0);
	CHECK_EQ(out.glow, VRR_GLOW_RACK);
	CHECK_EQ(out.allowfire, 0);

	out = t_rack_grab(&g);
	CHECK_EQ(g.state, VRR_RACKING);
	CHECK_EQ(g.racked, 0);
	CHECK_EQ(out.fx, VRR_FX_ZONE_ENTER);
	CHECK_EQ(out.glow, VRR_GLOW_RACK | VRR_HOT_RACK);
	CHECK_EQ(out.loaded, 0);

	out = t_rack_pull(&g, 2.9f);
	CHECK_EQ(out.fx, 0);
	CHECK_GUN(g, VRR_RACKING, 1, 0, 8);
	CHECK_EQ(g.racked, 0);
	CHECK_EQ(out.loaded, 0);

	/* full pull: racked and ready on this tick without letting go */
	out = t_rack_pull(&g, 3.0f);
	CHECK_EQ(out.fx, VRR_FX_RACK);
	CHECK_GUN(g, VRR_READY, 1, 1, 7);
	CHECK_EQ(g.racked, 1);
	CHECK_EQ(out.loaded, 8);
	CHECK_EQ(out.allowfire, 1);
	CHECK_EQ(out.glow, 0);

	out = t_rack_release(&g);
	CHECK_EQ(out.fx, 0);
	CHECK_GUN(g, VRR_READY, 1, 1, 7);
	CHECK_EQ(out.allowfire, 1);
	CHECK_EQ(out.glow, 0);
	CHECK_EQ(out.loaded, 8);
}

/* ---------- racking ---------- */

static void test_rack_wastes_chambered_round_until_empty(void)
{
	struct vrrgun g;
	struct vrroutput out;
	int i;
	vrrInit(&g, 8);
	for (i = 1; i <= 8; i++) {
		out = t_rack_grab(&g);
		CHECK_EQ(g.state, VRR_RACKING);
		CHECK_EQ(g.racked, 0);
		out = t_rack_pull(&g, 3.0f);
		CHECK_EQ(out.fx, VRR_FX_WASTE_ROUND | VRR_FX_RACK);
		CHECK_EQ(out.loaded, 8 - i);
		CHECK_EQ(vrrEngineLoaded(&g), 8 - i);
		/* the grab ends on the rack tick; the last one leaves nothing to chamber: READY, dry */
		CHECK_EQ(g.state, VRR_READY);
		/* the next rack needs a release and a fresh press */
		out = t_rack_release(&g);
		CHECK_EQ(out.fx & (VRR_FX_WASTE_ROUND | VRR_FX_RACK), 0);
		CHECK_EQ(g.state, VRR_READY);
	}
	CHECK_GUN(g, VRR_READY, 1, 0, 0);

	/* one more rack with nothing left: no waste */
	t_rack_grab(&g);
	CHECK_EQ(g.state, VRR_RACKING);
	out = t_rack_pull(&g, 3.0f);
	CHECK_EQ(out.fx, VRR_FX_RACK);
	CHECK_EQ(out.loaded, 0);
	CHECK_GUN(g, VRR_READY, 1, 0, 0);
	t_rack_release(&g);
	CHECK_GUN(g, VRR_READY, 1, 0, 0);
}

static void test_rack_with_no_clip_wastes_chamber(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrtest_restored_clip_out(&g, 5);
	CHECK_GUN(g, VRR_CLIP_OUT, 0, 1, 0);

	out = t_rack_grab(&g);
	CHECK_EQ(g.state, VRR_RACKING);
	CHECK_EQ(out.fx, VRR_FX_ZONE_ENTER);

	out = t_rack_pull(&g, 3.0f);
	CHECK_EQ(out.fx, VRR_FX_WASTE_ROUND | VRR_FX_RACK);
	CHECK_GUN(g, VRR_CLIP_OUT, 0, 0, 0);
	CHECK_EQ(out.loaded, 0);
	CHECK_EQ(out.allowfire, 0);
	/* clip out: only the clip zone glows, the hand in the rack zone is not hot */
	CHECK_EQ(out.glow, VRR_GLOW_CLIP);

	out = t_rack_release(&g);
	CHECK_EQ(out.fx, 0);
	CHECK_GUN(g, VRR_CLIP_OUT, 0, 0, 0);
	CHECK_EQ(out.glow, VRR_GLOW_CLIP);
}

static void test_rack_with_no_clip_and_empty_chamber(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrtest_restored_clip_out(&g, 0);
	t_rack_grab(&g);
	out = t_rack_pull(&g, 3.0f);
	CHECK_EQ(out.fx, VRR_FX_RACK);
	CHECK_GUN(g, VRR_CLIP_OUT, 0, 0, 0);
	t_rack_release(&g);
	CHECK_GUN(g, VRR_CLIP_OUT, 0, 0, 0);
}

static void test_rack_applies_once_per_grab(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrrInit(&g, 8);
	t_rack_grab(&g);
	out = t_rack_pull(&g, 3.0f);
	CHECK_EQ(out.loaded, 7);
	CHECK_EQ(g.state, VRR_READY);
	/* grip still held in the rack zone: pulling again does not start or apply a rack */
	out = t_rack_pull(&g, 4.0f);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(out.loaded, 7);
	out = t_rack_pull(&g, 6.0f);
	CHECK_EQ(out.fx, 0);
	out = t_rack_pull(&g, 0.0f);
	CHECK_EQ(out.fx, 0);
	out = t_rack_pull(&g, 3.5f);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(out.loaded, 7);
	CHECK_EQ(g.racked, 1);
	CHECK_EQ(g.state, VRR_READY);

	t_rack_release(&g);
	t_rack_grab(&g);
	CHECK_EQ(g.racked, 0);
	out = t_rack_pull(&g, 3.0f);
	CHECK_EQ(out.fx, VRR_FX_WASTE_ROUND | VRR_FX_RACK);
	CHECK_EQ(out.loaded, 6);
}

static void test_rack_pull_on_release_tick_counts(void)
{
	struct vrrgun g;
	struct vrrinput in;
	struct vrroutput out;
	make_need_rack(&g);
	t_rack_grab(&g);
	in = def_input();
	in.inrack = 1;
	in.rackpull = 3.0f;
	out = run_tick(&g, &in);
	CHECK_EQ(out.fx, VRR_FX_RACK);
	CHECK_GUN(g, VRR_READY, 1, 1, 7);
	CHECK_EQ(out.loaded, 8);
	CHECK_EQ(out.allowfire, 1);
}

static void test_rack_grab_with_full_pull_racks_same_tick(void)
{
	struct vrrgun g;
	struct vrroutput out;

	/* grab, rack and settle in one tick. The gun ends READY (rack zone dark), so no ZONE_ENTER. */
	vrrInit(&g, 8);
	out = t_rack_pull(&g, 3.0f);
	CHECK_EQ(g.racked, 1);
	CHECK_EQ(out.fx, VRR_FX_WASTE_ROUND | VRR_FX_RACK);
	CHECK_GUN(g, VRR_READY, 1, 1, 6);
	CHECK_EQ(out.loaded, 7);
	CHECK_EQ(out.allowfire, 1);
	out = t_rack_pull(&g, 4.0f);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(out.loaded, 7);
	t_rack_release(&g);
	CHECK_GUN(g, VRR_READY, 1, 1, 6);

	make_need_rack(&g);
	out = t_rack_pull(&g, 3.5f);
	CHECK_EQ(out.fx, VRR_FX_RACK);
	CHECK_GUN(g, VRR_READY, 1, 1, 7);
	CHECK_EQ(g.racked, 1);
	CHECK_EQ(out.loaded, 8);

	/* below the threshold on the grab tick: no rack yet */
	vrrInit(&g, 8);
	out = t_rack_pull(&g, 2.9f);
	CHECK_EQ(out.fx, VRR_FX_ZONE_ENTER);
	CHECK_EQ(g.racked, 0);
	CHECK_EQ(out.loaded, 8);
}

static void test_rack_release_without_pull_restores_state(void)
{
	struct vrrgun g;
	struct vrroutput out;

	vrrInit(&g, 8);
	t_rack_grab(&g);
	out = t_rack_release(&g);
	CHECK_EQ(out.fx, 0);
	CHECK_GUN(g, VRR_READY, 1, 1, 7);
	CHECK_EQ(out.loaded, 8);

	make_need_rack(&g);
	t_rack_grab(&g);
	out = t_rack_release(&g);
	CHECK_EQ(out.fx, 0);
	CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);

	vrtest_restored_clip_out(&g, 5);
	t_rack_grab(&g);
	out = t_rack_release(&g);
	CHECK_EQ(out.fx, 0);
	CHECK_GUN(g, VRR_CLIP_OUT, 0, 1, 0);
}

static void test_rack_grab_needs_rack_zone(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrrInit(&g, 8);
	out = t_hold(&g, 0);
	CHECK_EQ(g.state, VRR_READY);
	CHECK_EQ(out.fx, 0);
	/* grip already held when the hand enters the rack zone: no grab */
	out = t_rack_pull(&g, 0.0f);
	CHECK_EQ(g.state, VRR_READY);
	CHECK_EQ(out.fx, 0);
	out = t_rack_pull(&g, 3.0f);
	CHECK_EQ(g.state, VRR_READY);
	CHECK_EQ(out.loaded, 8);
}

static void test_rack_grab_not_from_holding_clip(void)
{
	struct vrrgun g;
	struct vrroutput out;
	make_holding(&g, 5);
	g.prevgrip = 0; /* force a grip down edge while holding */
	out = t_rack_grab(&g);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	CHECK_EQ(g.held, 8);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(out.handclip, 1);
}

static void test_rack_zone_wins_over_shoulder(void)
{
	struct vrrgun g;
	struct vrrinput in;
	struct vrroutput out;
	vrtest_restored_clip_out(&g, 5);
	in = def_input();
	in.grip = 1;
	in.inrack = 1;
	in.atshoulder = 1;
	out = run_tick(&g, &in);
	CHECK_EQ(g.state, VRR_RACKING);
	CHECK_EQ(g.held, 0);
	CHECK_EQ(out.fx & VRR_FX_GRAB_CLIP, 0);
	CHECK_EQ(out.fx, VRR_FX_ZONE_ENTER);
	CHECK_EQ(out.handclip, 0);
}


static void test_rack_auto_finish_needs_fresh_press(void)
{
	struct vrrgun g;
	struct vrroutput out;

	/* one round left: the rack wastes it and leaves the gun READY and dry with the grip held in the zone */
	vrrInit(&g, 1);
	CHECK_GUN(g, VRR_READY, 1, 1, 0);
	t_rack_grab(&g);
	out = t_rack_pull(&g, 3.0f);
	CHECK_EQ(out.fx, VRR_FX_WASTE_ROUND | VRR_FX_RACK);
	CHECK_GUN(g, VRR_READY, 1, 0, 0);
	out = t_rack_pull(&g, 0.0f);
	CHECK_EQ(g.state, VRR_READY);
	CHECK_EQ(out.fx, 0);
	out = t_rack_pull(&g, 5.0f);
	CHECK_EQ(g.state, VRR_READY);
	CHECK_EQ(out.fx, 0);

	/* release outside the rack zone, press outside it: no rack */
	t_quiet(&g);
	out = t_hold(&g, 0);
	CHECK_EQ(g.state, VRR_READY);
	CHECK_EQ(out.fx, 0);
	/* hand moves into the zone with that grip still held: no rack */
	out = t_rack_pull(&g, 3.0f);
	CHECK_EQ(g.state, VRR_READY);
	CHECK_EQ(out.fx & VRR_FX_RACK, 0);

	/* fresh press inside the zone starts the next rack */
	t_rack_release(&g);
	out = t_rack_grab(&g);
	CHECK_EQ(g.state, VRR_RACKING);
	CHECK_EQ(g.racked, 0);
	out = t_rack_pull(&g, 3.0f);
	CHECK_EQ(out.fx, VRR_FX_RACK);
	CHECK_GUN(g, VRR_READY, 1, 0, 0);

	/* clip out, chamber loaded: the rack wastes the round, ends in CLIP_OUT, and holding on does nothing */
	vrtest_restored_clip_out(&g, 5);
	t_rack_grab(&g);
	out = t_rack_pull(&g, 3.0f);
	CHECK_EQ(out.fx, VRR_FX_WASTE_ROUND | VRR_FX_RACK);
	CHECK_GUN(g, VRR_CLIP_OUT, 0, 0, 0);
	out = t_rack_pull(&g, 6.0f);
	CHECK_EQ(out.fx, 0);
	CHECK_GUN(g, VRR_CLIP_OUT, 0, 0, 0);
	/* a grip held from the rack does not grab a clip at the shoulder either */
	out = t_shoulder_grab(&g, 20);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(g.state, VRR_CLIP_OUT);
	CHECK_EQ(g.held, 0);
}

static void test_rack_release_before_full_pull_no_rack(void)
{
	struct vrrgun g;
	struct vrroutput out;

	vrrInit(&g, 8);
	t_rack_grab(&g);
	out = t_rack_pull(&g, 2.9f);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(g.state, VRR_RACKING);
	CHECK_EQ(g.racked, 0);
	out = t_rack_release(&g);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_GUN(g, VRR_READY, 1, 1, 7);
	CHECK_EQ(g.racked, 0);
	CHECK_EQ(out.loaded, 8);

	make_need_rack(&g);
	t_rack_grab(&g);
	t_rack_pull(&g, 2.0f);
	out = t_rack_release(&g);
	CHECK_EQ(out.fx, 0);
	CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
	CHECK_EQ(out.loaded, 0);

	vrtest_restored_clip_out(&g, 5);
	t_rack_grab(&g);
	t_rack_pull(&g, 1.5f);
	out = t_rack_release(&g);
	CHECK_EQ(out.fx, 0);
	CHECK_GUN(g, VRR_CLIP_OUT, 0, 1, 0);
	CHECK_EQ(out.loaded, 1);
}

static void test_rack_travel_default_when_zero_or_negative(void)
{
	struct vrrgun g;
	struct vrroutput out;

	make_need_rack(&g);
	t_rack_grab(&g);
	out = t_rack_pull_tr(&g, 2.9f, 0.0f);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(g.state, VRR_RACKING);
	out = t_rack_pull_tr(&g, VRR_RACK_PULL_CM, 0.0f);
	CHECK_EQ(out.fx, VRR_FX_RACK);
	CHECK_GUN(g, VRR_READY, 1, 1, 7);

	make_need_rack(&g);
	t_rack_grab(&g);
	out = t_rack_pull_tr(&g, 2.9f, -1.0f);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(g.state, VRR_RACKING);
	out = t_rack_pull_tr(&g, 3.0f, -1.0f);
	CHECK_EQ(out.fx, VRR_FX_RACK);
	CHECK_GUN(g, VRR_READY, 1, 1, 7);
}

static void test_rack_travel_six_cm(void)
{
	struct vrrgun g;
	struct vrroutput out;

	/* Falcon slide: 3 to 5.9 cm is not a full rack */
	make_need_rack(&g);
	t_rack_grab(&g);
	out = t_rack_pull_tr(&g, 3.0f, 6.0f);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(g.state, VRR_RACKING);
	CHECK_EQ(g.racked, 0);
	out = t_rack_pull_tr(&g, 4.5f, 6.0f);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(g.state, VRR_RACKING);
	out = t_rack_pull_tr(&g, 5.9f, 6.0f);
	CHECK_EQ(out.fx, 0);
	CHECK_GUN(g, VRR_RACKING, 1, 0, 8);
	CHECK_EQ(out.loaded, 0);
	out = t_rack_pull_tr(&g, 6.0f, 6.0f);
	CHECK_EQ(out.fx, VRR_FX_RACK);
	CHECK_GUN(g, VRR_READY, 1, 1, 7);
	CHECK_EQ(out.loaded, 8);
	CHECK_EQ(out.allowfire, 1);

	/* wasting a round needs the full 6 cm too */
	vrrInit(&g, 8);
	t_rack_grab(&g);
	out = t_rack_pull_tr(&g, 5.9f, 6.0f);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(out.loaded, 8);
	out = t_rack_pull_tr(&g, 6.5f, 6.0f);
	CHECK_EQ(out.fx, VRR_FX_WASTE_ROUND | VRR_FX_RACK);
	CHECK_GUN(g, VRR_READY, 1, 1, 6);

	/* letting go at 5.9 cm: no rack */
	make_need_rack(&g);
	t_rack_grab(&g);
	t_rack_pull_tr(&g, 5.9f, 6.0f);
	out = t_rack_release(&g);
	CHECK_EQ(out.fx, 0);
	CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);

	/* full 6 cm on the grab tick racks at once */
	make_need_rack(&g);
	out = t_rack_pull_tr(&g, 6.0f, 6.0f);
	CHECK_EQ(out.fx, VRR_FX_RACK);
	CHECK_GUN(g, VRR_READY, 1, 1, 7);
}

/* ---------- clip handling ---------- */

static void test_drop_clip_outside_zone(void)
{
	struct vrrgun g;
	struct vrroutput out;
	make_holding(&g, 5);
	out = t_release(&g, 0);
	CHECK_EQ(out.fx, VRR_FX_DROP_CLIP);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_GUN(g, VRR_CLIP_OUT, 0, 1, 0);
	CHECK_EQ(g.held, 0);
	CHECK_EQ(out.handclip, 0);
	CHECK_EQ(out.glow, VRR_GLOW_CLIP);
	CHECK_EQ(out.loaded, 1);
}

static void test_shoulder_grab_with_no_reserve(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrtest_restored_clip_out(&g, 5);
	out = t_shoulder_grab(&g, 0);
	CHECK_EQ(out.fx, VRR_FX_NO_AMMO);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_EQ(g.state, VRR_CLIP_OUT);
	CHECK_EQ(g.held, 0);
	CHECK_EQ(out.handclip, 0);
}

static void test_shoulder_grab_with_small_reserve(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrtest_restored_clip_out(&g, 5);
	out = t_shoulder_grab(&g, 3);
	CHECK_EQ(out.fx, VRR_FX_GRAB_CLIP);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	CHECK_EQ(g.held, 3);
}

static void test_held_capped_by_clipsize(void)
{
	struct vrrgun g;
	struct vrrinput in;
	vrtest_restored_clip_out(&g, 5);
	t_shoulder_grab(&g, 100);
	CHECK_EQ(g.held, 8);

	vrtest_restored_clip_out(&g, 5);
	in = def_input();
	in.grip = 1;
	in.atshoulder = 1;
	in.clipsize = 12;
	in.reserve = 100;
	run_tick(&g, &in);
	CHECK_EQ(g.held, 12);
}

static void test_insert_partial_clip(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrtest_restored_clip_out(&g, 5);
	t_shoulder_grab(&g, 3);
	out = t_release(&g, 1);
	CHECK_EQ(out.fx, VRR_FX_INSERT);
	CHECK_EQ(out.reservedelta, -3);
	CHECK_GUN(g, VRR_READY, 1, 1, 3);
	CHECK_EQ(out.loaded, 4);
}

static void test_insert_partial_clip_with_empty_chamber(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrtest_restored_clip_out(&g, 0);
	t_shoulder_grab(&g, 3);
	out = t_release(&g, 1);
	CHECK_EQ(out.fx, VRR_FX_INSERT);
	CHECK_EQ(out.reservedelta, -3);
	CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 3);
	CHECK_EQ(out.loaded, 0);
}

static void test_insert_with_empty_chamber_uses_full_clipsize(void)
{
	struct vrrgun g;
	struct vrrinput in;
	struct vrroutput out;
	vrtest_restored_clip_out(&g, 0);
	in = def_input();
	in.clipsize = 12;
	in.reserve = 50;
	in.grip = 1;
	in.atshoulder = 1;
	run_tick(&g, &in);
	CHECK_EQ(g.held, 12);
	in = def_input();
	in.clipsize = 12;
	in.reserve = 50;
	in.inclip = 1;
	out = run_tick(&g, &in);
	CHECK_EQ(out.reservedelta, -12);
	CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 12);
}

static void test_shoulder_grab_needs_shoulder_zone(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrtest_restored_clip_out(&g, 5);
	out = t_hold(&g, 0);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(g.state, VRR_CLIP_OUT);
	/* hand reaches the shoulder with the grip already held: no grab */
	out = t_shoulder_grab(&g, 20);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(g.state, VRR_CLIP_OUT);
	CHECK_EQ(g.held, 0);
}

static void test_grip_held_does_not_regrab(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrtest_restored_clip_out(&g, 5);
	out = t_shoulder_grab(&g, 0);
	CHECK_EQ(out.fx, VRR_FX_NO_AMMO);
	out = t_shoulder_grab(&g, 20);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(g.state, VRR_CLIP_OUT);
	out = t_shoulder_grab(&g, 20);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(g.state, VRR_CLIP_OUT);

	t_quiet(&g);
	out = t_shoulder_grab(&g, 20);
	CHECK_EQ(out.fx, VRR_FX_GRAB_CLIP);
	CHECK_EQ(g.held, 8);
	/* keeping the grip held keeps the clip in hand */
	out = t_shoulder_grab(&g, 20);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	CHECK_EQ(g.held, 8);
	out = t_hold(&g, 0);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
}

static void test_shoulder_grab_only_when_clip_out(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrrInit(&g, 8);
	out = t_shoulder_grab(&g, 20);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(g.state, VRR_READY);
	CHECK_EQ(g.held, 0);

	make_need_rack(&g);
	out = t_shoulder_grab(&g, 20);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(g.state, VRR_NEED_RACK);
	CHECK_EQ(g.held, 0);
}


/* ---------- seated clip insert ---------- */

/* The chambered round comes from a restored state (make_clip_out): since Task F4a B empties it */
static void test_seated_insert_with_chambered_round(void)
{
	struct vrrgun g;
	struct vrroutput out;
	make_holding(&g, 5);
	out = t_hold(&g, 1);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	out = t_seat(&g, 1);
	CHECK_EQ(out.fx, VRR_FX_INSERT);
	CHECK_EQ(out.reservedelta, -7);
	CHECK_GUN(g, VRR_READY, 1, 1, 7);
	CHECK_EQ(g.held, 0);
	CHECK_EQ(out.loaded, 8);
	CHECK_EQ(out.allowfire, 1);
	CHECK_EQ(out.handclip, 0);
	CHECK_EQ(out.glow, 0);

	/* keeping the grip and then letting go: no second insert, no drop */
	out = t_seat(&g, 1);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(out.reservedelta, 0);
	out = t_release(&g, 0);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_GUN(g, VRR_READY, 1, 1, 7);
}

static void test_seated_insert_empty_chamber(void)
{
	struct vrrgun g;
	struct vrroutput out;
	make_holding(&g, 0);
	out = t_seat(&g, 1);
	CHECK_EQ(out.fx & VRR_FX_INSERT, VRR_FX_INSERT);
	CHECK_EQ(out.fx & ~(VRR_FX_INSERT | VRR_FX_ZONE_ENTER), 0);
	CHECK_EQ(out.reservedelta, -8);
	CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
	CHECK_EQ(g.held, 0);
	CHECK_EQ(out.loaded, 0);
	CHECK_EQ(out.allowfire, 0);
	CHECK_EQ(out.handclip, 0);
	CHECK_EQ(out.glow, VRR_GLOW_RACK);
}

static void test_seated_insert_partial_clip(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrtest_restored_clip_out(&g, 5);
	t_shoulder_grab(&g, 3);
	t_hold(&g, 1);
	out = t_seat(&g, 1);
	CHECK_EQ(out.fx, VRR_FX_INSERT);
	CHECK_EQ(out.reservedelta, -3);
	CHECK_GUN(g, VRR_READY, 1, 1, 3);
	CHECK_EQ(out.loaded, 4);

	vrtest_restored_clip_out(&g, 0);
	t_shoulder_grab(&g, 3);
	t_hold(&g, 1);
	out = t_seat(&g, 1);
	CHECK_EQ(out.fx, VRR_FX_INSERT);
	CHECK_EQ(out.reservedelta, -3);
	CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 3);
}

static void test_seated_outside_clip_zone_does_nothing(void)
{
	struct vrrgun g;
	struct vrroutput out;
	make_holding(&g, 5);
	out = t_seat(&g, 0);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	CHECK_EQ(g.held, 8);
	CHECK_EQ(g.magin, 0);
	CHECK_EQ(out.handclip, 1);
	out = t_seat(&g, 0);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);

	/* moving into the zone while seated then inserts */
	out = t_seat(&g, 1);
	CHECK_EQ(out.fx & VRR_FX_INSERT, VRR_FX_INSERT);
	CHECK_GUN(g, VRR_READY, 1, 1, 7);
}

static void test_seated_ignored_without_held_clip(void)
{
	struct vrrgun g;
	struct vrroutput out;

	vrtest_restored_clip_out(&g, 5);
	out = t_seat(&g, 1);
	CHECK_EQ(out.fx & VRR_FX_INSERT, 0);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_GUN(g, VRR_CLIP_OUT, 0, 1, 0);

	vrrInit(&g, 8);
	out = t_seat(&g, 1);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_GUN(g, VRR_READY, 1, 1, 7);

	make_need_rack(&g);
	out = t_seat(&g, 1);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
}

static void test_seated_untracked_ignored(void)
{
	struct vrrgun g, before;
	struct vrrinput in;
	struct vrroutput out;
	make_holding(&g, 5);
	t_hold(&g, 1);
	before = g;
	in = def_input();
	in.tracked = 0;
	in.grip = 1;
	in.inclip = 1;
	in.seated = 1;
	out = run_tick(&g, &in);
	CHECK(memcmp(&g, &before, sizeof g) == 0);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_EQ(out.handclip, 1);
}

static void test_seated_insert_then_rack_needs_fresh_press(void)
{
	struct vrrgun g;
	struct vrroutput out;
	make_holding(&g, 0);
	t_hold(&g, 1);
	t_seat(&g, 1);
	CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);

	/* the same grip moves to the rack and pulls: no rack starts */
	out = t_rack_grab(&g);
	CHECK_EQ(g.state, VRR_NEED_RACK);
	CHECK_EQ(out.fx & VRR_FX_RACK, 0);
	out = t_rack_pull(&g, 3.0f);
	CHECK_EQ(g.state, VRR_NEED_RACK);
	CHECK_EQ(out.fx & VRR_FX_RACK, 0);
	CHECK_EQ(out.loaded, 0);
	out = t_rack_pull(&g, 6.0f);
	CHECK_EQ(out.fx & VRR_FX_RACK, 0);
	CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);

	/* release and press again in the rack zone: now it racks */
	t_rack_release(&g);
	out = t_rack_grab(&g);
	CHECK_EQ(g.state, VRR_RACKING);
	out = t_rack_pull(&g, 3.0f);
	CHECK_EQ(out.fx, VRR_FX_RACK);
	CHECK_GUN(g, VRR_READY, 1, 1, 7);
	CHECK_EQ(out.loaded, 8);
}

/* ---------- eject ---------- */

static void test_eject_from_need_rack(void)
{
	struct vrrgun g;
	struct vrroutput out;
	make_need_rack(&g);
	out = t_eject(&g);
	CHECK_EQ(out.fx, VRR_FX_EJECT);
	CHECK_EQ(out.reservedelta, 8);
	CHECK_GUN(g, VRR_CLIP_OUT, 0, 0, 0);
	CHECK_EQ(out.glow, VRR_GLOW_CLIP);
}

static void test_eject_full_gun_empties_it(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrrInit(&g, 8);
	out = t_eject(&g);
	CHECK_EQ(out.fx, VRR_FX_EJECT);
	CHECK_EQ(out.reservedelta, 8);
	CHECK_GUN(g, VRR_CLIP_OUT, 0, 0, 0);
	CHECK_EQ(out.loaded, 0);
	CHECK_EQ(out.allowfire, 0);
}

static void test_eject_ignored_with_clip_out(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrtest_restored_clip_out(&g, 5);
	out = t_eject(&g);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_GUN(g, VRR_CLIP_OUT, 0, 1, 0);

	make_holding(&g, 5);
	out = t_eject(&g); /* grip 0 also releases the clip outside the zone */
	CHECK_EQ(out.fx & VRR_FX_EJECT, 0);
	CHECK_EQ(out.reservedelta, 0);
}

static void test_eject_ignored_while_racking(void)
{
	struct vrrgun g;
	struct vrrinput in;
	struct vrroutput out;
	vrrInit(&g, 8);
	t_rack_grab(&g);
	in = def_input();
	in.grip = 1;
	in.inrack = 1;
	in.eject = 1;
	out = run_tick(&g, &in);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_GUN(g, VRR_RACKING, 1, 1, 7);
}

static void test_eject_and_shoulder_grab_same_tick(void)
{
	struct vrrgun g;
	struct vrrinput in;
	struct vrroutput out;
	vrrInit(&g, 5);
	in = def_input();
	in.eject = 1;
	in.grip = 1;
	in.atshoulder = 1;
	out = run_tick(&g, &in);
	CHECK_EQ(out.fx, VRR_FX_EJECT | VRR_FX_GRAB_CLIP);
	CHECK_EQ(out.reservedelta, 5);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	CHECK_EQ(g.held, 8);
	CHECK_EQ(g.magin, 0);
	CHECK_EQ(g.chamber, 0);
	CHECK_EQ(out.handclip, 1);
}

static void test_eject_and_rack_grab_same_tick(void)
{
	struct vrrgun g;
	struct vrrinput in;
	struct vrroutput out;
	vrrInit(&g, 5);
	in = def_input();
	in.eject = 1;
	in.grip = 1;
	in.inrack = 1;
	out = run_tick(&g, &in);
	CHECK_EQ(out.fx, VRR_FX_EJECT | VRR_FX_ZONE_ENTER);
	CHECK_EQ(out.reservedelta, 5);
	CHECK_GUN(g, VRR_RACKING, 0, 0, 0);
	CHECK_EQ(g.racked, 0);
}

/* ---------- pause ---------- */

static void test_paused_holding_clip_in_zone_inserts(void)
{
	struct vrrgun g;
	struct vrrinput in;
	struct vrroutput out;
	make_holding(&g, 5);
	t_hold(&g, 1);
	in = def_input();
	in.paused = 1;
	in.grip = 1;
	in.inclip = 1;
	out = run_tick(&g, &in);
	CHECK_EQ(out.fx, VRR_FX_INSERT);
	CHECK_EQ(out.reservedelta, -7);
	CHECK_GUN(g, VRR_READY, 1, 1, 7);
	CHECK_EQ(g.prevgrip, 0);
}

static void test_paused_holding_clip_outside_zone_drops(void)
{
	struct vrrgun g;
	struct vrrinput in;
	struct vrroutput out;
	make_holding(&g, 5);
	in = def_input();
	in.paused = 1;
	in.grip = 1;
	out = run_tick(&g, &in);
	CHECK_EQ(out.fx, VRR_FX_DROP_CLIP);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_EQ(g.state, VRR_CLIP_OUT);
	CHECK_EQ(g.held, 0);
}

static void test_paused_blocks_eject(void)
{
	struct vrrgun g;
	struct vrrinput in;
	struct vrroutput out;
	vrrInit(&g, 8);
	in = def_input();
	in.paused = 1;
	in.eject = 1;
	out = run_tick(&g, &in);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_GUN(g, VRR_READY, 1, 1, 7);
}

static void test_paused_blocks_grabs(void)
{
	struct vrrgun g;
	struct vrrinput in;
	struct vrroutput out;
	vrtest_restored_clip_out(&g, 5);
	in = def_input();
	in.paused = 1;
	in.grip = 1;
	in.atshoulder = 1;
	out = run_tick(&g, &in);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(g.state, VRR_CLIP_OUT);
	CHECK_EQ(g.prevgrip, 0);

	in = def_input();
	in.paused = 1;
	in.grip = 1;
	in.inrack = 1;
	run_tick(&g, &in);
	CHECK_EQ(g.state, VRR_CLIP_OUT);

	/* unpausing with the grip still held is a fresh grip down */
	out = t_shoulder_grab(&g, 20);
	CHECK_EQ(out.fx, VRR_FX_GRAB_CLIP);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
}

static void test_paused_releases_rack(void)
{
	struct vrrgun g;
	struct vrrinput in;
	struct vrroutput out;
	vrrInit(&g, 8);
	t_rack_grab(&g);
	in = def_input();
	in.paused = 1;
	in.grip = 1;
	in.inrack = 1;
	out = run_tick(&g, &in);
	CHECK_EQ(out.fx, 0);
	CHECK_GUN(g, VRR_READY, 1, 1, 7);
}

/* ---------- untracked ---------- */

static void test_untracked_changes_nothing(void)
{
	struct vrrgun g, before;
	struct vrrinput in;
	struct vrroutput out;
	vrrInit(&g, 8);
	before = g;
	in = def_input();
	in.tracked = 0;
	in.eject = 1;
	in.grip = 1;
	in.inrack = 1;
	in.atshoulder = 1;
	in.rackpull = 5.0f;
	out = run_tick(&g, &in);
	CHECK(memcmp(&g, &before, sizeof g) == 0);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_EQ(out.loaded, 8);
	CHECK_EQ(out.allowfire, 1);
	CHECK_EQ(out.handclip, 0);
	CHECK_EQ(out.glow, 0);
	CHECK_EQ(g.prevgrip, 0);
}

static void test_untracked_rack_pull_ignored(void)
{
	struct vrrgun g, before;
	struct vrrinput in;
	struct vrroutput out;
	make_need_rack(&g);
	t_rack_grab(&g);
	before = g;
	in = def_input();
	in.tracked = 0;
	in.grip = 1;
	in.inrack = 1;
	in.rackpull = 5.0f;
	out = run_tick(&g, &in);
	CHECK(memcmp(&g, &before, sizeof g) == 0);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(out.loaded, 0);
	CHECK_EQ(out.allowfire, 0);
	CHECK_EQ(out.glow, VRR_GLOW_RACK | VRR_HOT_RACK);
}

static void test_untracked_release_seen_on_next_tracked_tick(void)
{
	struct vrrgun g, before;
	struct vrrinput in;
	struct vrroutput out;
	make_holding(&g, 5);
	before = g;
	in = def_input();
	in.tracked = 0;
	in.inclip = 1;
	out = run_tick(&g, &in);
	CHECK(memcmp(&g, &before, sizeof g) == 0);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	CHECK_EQ(g.held, 8);
	CHECK_EQ(g.prevgrip, 1);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_EQ(out.handclip, 1);
	CHECK_EQ(out.allowfire, 1); /* chamber still loaded while holding the clip */
	CHECK_EQ(out.loaded, 1);
	CHECK_EQ(out.glow, VRR_GLOW_CLIP | VRR_HOT_CLIP);

	out = t_release(&g, 1);
	CHECK_EQ(out.fx, VRR_FX_INSERT);
	CHECK_EQ(out.reservedelta, -7);
	CHECK_GUN(g, VRR_READY, 1, 1, 7);
}

static void test_untracked_keeps_prevhot(void)
{
	struct vrrgun g;
	struct vrrinput in;
	struct vrroutput out;
	vrtest_restored_clip_out(&g, 5);
	out = t_release(&g, 1);
	CHECK_EQ(out.fx, VRR_FX_ZONE_ENTER);
	CHECK_EQ(g.prevhot, VRR_HOT_CLIP);
	in = def_input();
	in.tracked = 0;
	out = run_tick(&g, &in);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(g.prevhot, VRR_HOT_CLIP);
	out = t_release(&g, 1);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(out.glow, VRR_GLOW_CLIP | VRR_HOT_CLIP);
}

/* Untracked ticks still show the hot bit of each lit zone the hand is in, but store nothing; the
 * first tracked tick then fires ZONE_ENTER. One zone lit at a time (89fbd7a89): the clip zone
 * with the clip out, the rack zone in NEED_RACK; the dark zone never gets a hot bit */
static void test_untracked_glow_includes_hot_bits(void)
{
	struct vrrgun g, before;
	struct vrrinput in;
	struct vrroutput out;

	/* clip out: the clip zone lit, the rack zone dark */
	vrtest_restored_clip_out(&g, 0);
	CHECK_EQ(g.prevhot, 0);
	before = g;
	in = def_input();
	in.tracked = 0;
	in.inclip = 1;
	in.inrack = 1;
	out = run_tick(&g, &in);
	CHECK_EQ(out.glow, VRR_GLOW_CLIP | VRR_HOT_CLIP);
	CHECK_EQ(out.fx, 0);
	CHECK(memcmp(&g, &before, sizeof g) == 0);
	CHECK_EQ(g.prevhot, 0);
	/* prevhot was not stored, so the first tracked tick in the zone fires ZONE_ENTER */
	in.tracked = 1;
	out = run_tick(&g, &in);
	CHECK_EQ(out.fx, VRR_FX_ZONE_ENTER);
	CHECK_EQ(g.prevhot, VRR_HOT_CLIP);

	/* NEED_RACK: the rack zone lit, the clip zone dark */
	make_need_rack(&g);
	t_quiet(&g);
	CHECK_EQ(g.prevhot, 0);
	before = g;
	in = def_input();
	in.tracked = 0;
	in.inclip = 1;
	in.inrack = 1;
	out = run_tick(&g, &in);
	CHECK_EQ(out.glow, VRR_GLOW_RACK | VRR_HOT_RACK);
	CHECK_EQ(out.fx, 0);
	CHECK(memcmp(&g, &before, sizeof g) == 0);
	in.tracked = 1;
	out = run_tick(&g, &in);
	CHECK_EQ(out.fx, VRR_FX_ZONE_ENTER);
	CHECK_EQ(g.prevhot, VRR_HOT_RACK);

	/* untracked: HOT bits still need the matching glow */
	vrtest_restored_clip_out(&g, 5);
	in = def_input();
	in.tracked = 0;
	in.inclip = 1;
	in.inrack = 1;
	out = run_tick(&g, &in);
	CHECK_EQ(out.glow, VRR_GLOW_CLIP | VRR_HOT_CLIP);
	CHECK_EQ(out.fx, 0);

	vrrInit(&g, 8);
	out = run_tick(&g, &in);
	CHECK_EQ(out.glow, 0);
}

/* ---------- glow and zone enter ---------- */

static void test_zone_enter_fires_once(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrtest_restored_clip_out(&g, 5);
	out = t_release(&g, 1);
	CHECK_EQ(out.fx, VRR_FX_ZONE_ENTER);
	CHECK_EQ(out.glow, VRR_GLOW_CLIP | VRR_HOT_CLIP);
	out = t_release(&g, 1);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(out.glow, VRR_GLOW_CLIP | VRR_HOT_CLIP);
	out = t_release(&g, 1);
	CHECK_EQ(out.fx, 0);
	out = t_release(&g, 0);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(out.glow, VRR_GLOW_CLIP);
	CHECK_EQ(g.prevhot, 0);
	out = t_release(&g, 1);
	CHECK_EQ(out.fx, VRR_FX_ZONE_ENTER);
}

static void test_zone_enter_not_for_dark_zone(void)
{
	struct vrrgun g;
	struct vrrinput in;
	struct vrroutput out;

	vrrInit(&g, 8);
	in = def_input();
	in.inclip = 1;
	in.inrack = 1;
	out = run_tick(&g, &in);
	CHECK_EQ(out.glow, 0);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(g.prevhot, 0);

	vrtest_restored_clip_out(&g, 5); /* chamber loaded: rack not glowing */
	in = def_input();
	in.inrack = 1;
	out = run_tick(&g, &in);
	CHECK_EQ(out.glow, VRR_GLOW_CLIP);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(g.prevhot, 0);

	make_need_rack(&g); /* clip in: clip zone not glowing */
	in = def_input();
	in.inclip = 1;
	out = run_tick(&g, &in);
	CHECK_EQ(out.glow, VRR_GLOW_RACK);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(g.prevhot, 0);
}

/* A hand hot in one zone that becomes hot in another fires ZONE_ENTER again. One zone is lit at a
 * time (89fbd7a89), so the second zone lights by a state change: the clip inserted with the hand
 * in both zones turns the clip zone dark and the rack zone lit */
static void test_zone_enter_on_second_zone(void)
{
	struct vrrgun g;
	struct vrrinput in;
	struct vrroutput out;
	vrtest_restored_clip_out(&g, 0);
	out = t_release(&g, 1);
	CHECK_EQ(out.fx, VRR_FX_ZONE_ENTER);
	CHECK_EQ(g.prevhot, VRR_HOT_CLIP);

	/* the hand also in the rack zone: dark with the clip out, nothing */
	in = def_input();
	in.inclip = 1;
	in.inrack = 1;
	out = run_tick(&g, &in);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(out.glow, VRR_GLOW_CLIP | VRR_HOT_CLIP);
	CHECK_EQ(g.prevhot, VRR_HOT_CLIP);

	/* a clip from the shoulder, then back into both zones */
	t_quiet(&g);
	t_shoulder_grab(&g, 20);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	in = def_input();
	in.grip = 1;
	in.inclip = 1;
	in.inrack = 1;
	out = run_tick(&g, &in);
	CHECK_EQ(out.fx, VRR_FX_ZONE_ENTER);
	CHECK_EQ(out.glow, VRR_GLOW_CLIP | VRR_HOT_CLIP);
	CHECK_EQ(g.prevhot, VRR_HOT_CLIP);

	/* let go there: the clip goes in, the rack zone lights with the hand in it */
	in = def_input();
	in.inclip = 1;
	in.inrack = 1;
	out = run_tick(&g, &in);
	CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
	CHECK_EQ(out.fx, VRR_FX_INSERT | VRR_FX_ZONE_ENTER);
	CHECK_EQ(out.glow, VRR_GLOW_RACK | VRR_HOT_RACK);
	CHECK_EQ(g.prevhot, VRR_HOT_RACK);

	/* leaving the dark clip zone, staying in the rack zone: nothing more */
	in = def_input();
	in.inrack = 1;
	out = run_tick(&g, &in);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(out.glow, VRR_GLOW_RACK | VRR_HOT_RACK);
	CHECK_EQ(g.prevhot, VRR_HOT_RACK);
}

static void test_zone_enter_when_zone_lights_up_same_tick(void)
{
	struct vrrgun g;
	struct vrrinput in;
	struct vrroutput out;
	vrrInit(&g, 5);
	out = t_release(&g, 1);
	CHECK_EQ(out.glow, 0);
	CHECK_EQ(out.fx, 0);
	in = def_input();
	in.eject = 1;
	in.inclip = 1;
	out = run_tick(&g, &in);
	CHECK_EQ(out.fx, VRR_FX_EJECT | VRR_FX_ZONE_ENTER);
	CHECK_EQ(out.glow, VRR_GLOW_CLIP | VRR_HOT_CLIP);
	CHECK_EQ(g.prevhot, VRR_HOT_CLIP);
}

static void test_glow_by_state(void)
{
	struct vrrgun g;
	struct vrroutput out;

	vrrInit(&g, 8);
	out = t_quiet(&g);
	CHECK_EQ(out.glow, 0);

	vrtest_restored_clip_out(&g, 5);
	out = t_quiet(&g);
	CHECK_EQ(out.glow, VRR_GLOW_CLIP);

	/* clip out with the chamber empty: still only the clip zone (89fbd7a89) */
	vrtest_restored_clip_out(&g, 0);
	out = t_quiet(&g);
	CHECK_EQ(out.glow, VRR_GLOW_CLIP);

	/* clip in, both empty: READY and dry, nothing glows (c032e6c3e) */
	vrrInit(&g, 0);
	out = t_quiet(&g);
	CHECK_EQ(out.glow, 0);

	make_holding(&g, 0);
	out = t_hold(&g, 0);
	CHECK_EQ(out.glow, VRR_GLOW_CLIP);

	make_need_rack(&g);
	out = t_quiet(&g);
	CHECK_EQ(out.glow, VRR_GLOW_RACK);

	vrrInit(&g, 8);
	t_rack_grab(&g);
	out = t_hold(&g, 0);
	CHECK_EQ(g.state, VRR_RACKING);
	CHECK_EQ(out.glow, VRR_GLOW_RACK);
	out = t_rack_pull(&g, 0.0f);
	CHECK_EQ(out.glow, VRR_GLOW_RACK | VRR_HOT_RACK);
}

static void test_hot_bits_need_matching_glow(void)
{
	struct vrrgun g;
	struct vrrinput in;
	struct vrroutput out;
	make_holding(&g, 0);
	in = def_input();
	in.grip = 1;
	in.inclip = 1;
	in.inrack = 1;
	out = run_tick(&g, &in);
	CHECK_EQ(out.glow, VRR_GLOW_CLIP | VRR_HOT_CLIP);
	CHECK_EQ(g.prevhot, VRR_HOT_CLIP);

	vrrInit(&g, 8);
	t_rack_grab(&g);
	in = def_input();
	in.grip = 1;
	in.inclip = 1;
	in.inrack = 1;
	out = run_tick(&g, &in);
	CHECK_EQ(out.glow, VRR_GLOW_RACK | VRR_HOT_RACK);
	CHECK_EQ(g.prevhot, VRR_HOT_RACK);
}

/* ---------- firing the chambered round with the clip out ---------- */

/* A chambered round with the clip out (since Task F4a only a restored or settled state) stays
 * fireable. */
static void test_chambered_clip_out_allows_fire(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrtest_restored_clip_out(&g, 5);
	CHECK_GUN(g, VRR_CLIP_OUT, 0, 1, 0);
	CHECK_EQ(vrrAllowFire(&g), 1);

	out = t_quiet(&g);
	CHECK_EQ(out.loaded, 1);
	CHECK_EQ(out.allowfire, 1);
	out = t_quiet(&g);
	CHECK_EQ(g.state, VRR_CLIP_OUT);
	CHECK_EQ(out.allowfire, 1);

	out = t_shoulder_grab(&g, 20);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	CHECK_EQ(g.chamber, 1);
	CHECK_EQ(out.handclip, 1);
	CHECK_EQ(out.loaded, 1);
	CHECK_EQ(out.allowfire, 1);
	CHECK_EQ(vrrAllowFire(&g), 1);

	out = t_hold(&g, 1);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	CHECK_EQ(out.allowfire, 1);
}

/* Firing the chambered round with the clip out empties the chamber: the next clip needs a rack. */
static void test_fire_chambered_round_with_clip_out_needs_rack(void)
{
	struct vrrgun g;
	struct vrroutput out;
	int32_t reserve_total = 0;
	vrtest_restored_clip_out(&g, 5); /* restored: chamber 1, clip out */

	out = t_quiet(&g);
	CHECK_EQ(out.allowfire, 1);

	/* the engine fires the chambered round */
	vrrSyncFromEngineRow(&g, 0, 0);
	CHECK_GUN(g, VRR_CLIP_OUT, 0, 0, 0);
	CHECK_EQ(vrrAllowFire(&g), 0);

	out = t_quiet(&g);
	reserve_total += out.reservedelta;
	CHECK_GUN(g, VRR_CLIP_OUT, 0, 0, 0);
	CHECK_EQ(out.allowfire, 0);
	CHECK_EQ(out.loaded, 0);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_EQ(out.glow, VRR_GLOW_CLIP);

	out = t_shoulder_grab(&g, 20);
	reserve_total += out.reservedelta;
	CHECK_EQ(out.fx, VRR_FX_GRAB_CLIP);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	CHECK_EQ(g.chamber, 0);
	CHECK_EQ(g.held, 8);
	CHECK_EQ(out.allowfire, 0);
	CHECK_EQ(out.loaded, 0);

	out = t_release(&g, 1);
	reserve_total += out.reservedelta;
	CHECK_EQ(out.fx, VRR_FX_INSERT);
	CHECK_EQ(out.reservedelta, -8);
	CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
	CHECK(g.state != VRR_READY);
	CHECK_EQ(out.allowfire, 0);
	CHECK_EQ(out.loaded, 0);
	CHECK_EQ(out.glow, VRR_GLOW_RACK);

	out = t_rack_grab(&g);
	reserve_total += out.reservedelta;
	CHECK_EQ(g.state, VRR_RACKING);
	CHECK_EQ(out.allowfire, 0);

	out = t_rack_pull(&g, 3.0f);
	reserve_total += out.reservedelta;
	CHECK_EQ(out.fx & VRR_FX_RACK, VRR_FX_RACK);
	CHECK_EQ(out.fx & VRR_FX_WASTE_ROUND, 0);
	CHECK_GUN(g, VRR_READY, 1, 1, 7);
	CHECK_EQ(out.loaded, 8);
	CHECK_EQ(out.allowfire, 1);
	CHECK_EQ(vrrAllowFire(&g), 1);

	/* bookkeeping: 1 chambered - 1 fired - net reserve change (-8 drawn) = 8 loaded */
	CHECK_EQ(reserve_total, -8);
	CHECK_EQ(out.loaded, 1 - 1 - reserve_total);
}

/* Contrast: the same restored gun not fired inserts straight to READY, no rack (Task F4a keeps
 * this for a round chambered from a restored or settled state). */
static void test_chambered_clip_out_inserts_ready(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrtest_restored_clip_out(&g, 5);
	vrrSyncFromEngineRow(&g, 1, 0); /* engine still reports the chambered round */
	CHECK_GUN(g, VRR_CLIP_OUT, 0, 1, 0);
	t_shoulder_grab(&g, 20);
	out = t_release(&g, 1);
	CHECK_EQ(out.fx, VRR_FX_INSERT);
	CHECK_EQ(out.reservedelta, -7);
	CHECK_GUN(g, VRR_READY, 1, 1, 7);
	CHECK_EQ(out.loaded, 8);
	CHECK_EQ(out.allowfire, 1);
}

/* Empty-gun eject: nothing chambered, so no fire with the clip out or in the hand. */
static void test_empty_eject_blocks_fire(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrrInit(&g, 8);
	vrrSyncFromEngineRow(&g, 0, 0);

	out = t_eject(&g);
	CHECK_GUN(g, VRR_CLIP_OUT, 0, 0, 0);
	CHECK_EQ(out.allowfire, 0);
	CHECK_EQ(vrrAllowFire(&g), 0);
	out = t_quiet(&g);
	CHECK_EQ(out.allowfire, 0);

	out = t_shoulder_grab(&g, 20);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	CHECK_EQ(out.allowfire, 0);
	CHECK_EQ(vrrAllowFire(&g), 0);
	out = t_hold(&g, 1);
	CHECK_EQ(out.allowfire, 0);

	vrtest_restored_clip_out(&g, 0);
	out = t_quiet(&g);
	CHECK_EQ(out.allowfire, 0);
	make_holding(&g, 0);
	out = t_hold(&g, 0);
	CHECK_EQ(out.allowfire, 0);
}

/* vrrAllowFire agrees with out.allowfire in every state, chamber loaded or not. */
static void test_vrrallowfire_matches_output(void)
{
	struct vrrgun g;
	struct vrroutput out;
	int32_t st, ch;

	/* each state reached through ticks */
	vrrInit(&g, 8);
	out = t_quiet(&g);
	CHECK_EQ(g.state, VRR_READY);
	CHECK_EQ(out.allowfire, 1);
	CHECK_EQ(vrrAllowFire(&g), out.allowfire);

	vrrInit(&g, 8);
	vrrSyncFromEngineRow(&g, 0, 0);
	out = t_quiet(&g);
	CHECK_EQ(g.state, VRR_READY);
	CHECK_EQ(out.allowfire, 1); /* fired dry: READY still allows the trigger */
	CHECK_EQ(vrrAllowFire(&g), out.allowfire);

	vrtest_restored_clip_out(&g, 5);
	out = t_quiet(&g);
	CHECK_EQ(g.state, VRR_CLIP_OUT);
	CHECK_EQ(out.allowfire, 1);
	CHECK_EQ(vrrAllowFire(&g), out.allowfire);

	vrtest_restored_clip_out(&g, 0);
	out = t_quiet(&g);
	CHECK_EQ(g.state, VRR_CLIP_OUT);
	CHECK_EQ(out.allowfire, 0);
	CHECK_EQ(vrrAllowFire(&g), out.allowfire);

	make_holding(&g, 5);
	out = t_hold(&g, 0);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	CHECK_EQ(out.allowfire, 1);
	CHECK_EQ(vrrAllowFire(&g), out.allowfire);

	make_holding(&g, 0);
	out = t_hold(&g, 0);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	CHECK_EQ(out.allowfire, 0);
	CHECK_EQ(vrrAllowFire(&g), out.allowfire);

	make_need_rack(&g);
	out = t_quiet(&g);
	CHECK_EQ(g.state, VRR_NEED_RACK);
	CHECK_EQ(out.allowfire, 0);
	CHECK_EQ(vrrAllowFire(&g), out.allowfire);

	vrrInit(&g, 8);
	out = t_rack_grab(&g);
	CHECK_EQ(g.state, VRR_RACKING);
	CHECK_EQ(g.chamber, 1);
	CHECK_EQ(out.allowfire, 0);
	CHECK_EQ(vrrAllowFire(&g), out.allowfire);

	make_need_rack(&g);
	out = t_rack_grab(&g);
	CHECK_EQ(g.state, VRR_RACKING);
	CHECK_EQ(g.chamber, 0);
	CHECK_EQ(out.allowfire, 0);
	CHECK_EQ(vrrAllowFire(&g), out.allowfire);

	/* direct table over all five states and both chamber values */
	for (st = VRR_READY; st <= VRR_RACKING; st++) {
		for (ch = 0; ch <= 1; ch++) {
			int32_t want = st == VRR_READY
				|| (ch && (st == VRR_CLIP_OUT || st == VRR_HOLDING_CLIP));
			struct vrrinput in = def_input();
			vrrInit(&g, 8);
			g.state = st;
			g.chamber = ch;
			g.magin = st == VRR_CLIP_OUT || st == VRR_HOLDING_CLIP ? 0 : 1;
			if (!g.magin) {
				g.clip = 0;
			}
			in.tracked = 0; /* untracked: outputs filled without moving the state */
			out = run_tick(&g, &in);
			CHECK_EQ(g.state, st);
			CHECK_EQ(vrrAllowFire(&g), want);
			CHECK_EQ(out.allowfire, want);
		}
	}
}

/* Firing the chambered round while the clip is in the hand: the insert then needs a rack. */
static void test_fire_chambered_round_while_holding_clip_needs_rack(void)
{
	struct vrrgun g;
	struct vrroutput out;
	make_holding(&g, 5);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	CHECK_EQ(g.chamber, 1);
	CHECK_EQ(vrrAllowFire(&g), 1);

	vrrSyncFromEngineRow(&g, 0, 0);
	CHECK_EQ(g.chamber, 0);
	CHECK_EQ(g.held, 8);
	out = t_hold(&g, 1);
	CHECK_EQ(g.state, VRR_HOLDING_CLIP);
	CHECK_EQ(out.allowfire, 0);
	CHECK_EQ(out.loaded, 0);
	CHECK_EQ(out.handclip, 1);

	out = t_release(&g, 1);
	CHECK_EQ(out.fx, VRR_FX_INSERT);
	CHECK_EQ(out.reservedelta, -8);
	CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
	CHECK_EQ(out.allowfire, 0);
	CHECK_EQ(out.loaded, 0);

	t_rack_grab(&g);
	out = t_rack_pull(&g, 3.0f);
	CHECK_EQ(out.fx & VRR_FX_WASTE_ROUND, 0);
	CHECK_GUN(g, VRR_READY, 1, 1, 7);
	CHECK_EQ(out.loaded, 8);
	CHECK_EQ(out.allowfire, 1);

	/* same with a seated insert (grip still held) */
	make_holding(&g, 5);
	vrrSyncFromEngineRow(&g, 0, 0);
	out = t_seat(&g, 1);
	CHECK_EQ(out.fx & VRR_FX_INSERT, VRR_FX_INSERT);
	CHECK_EQ(out.reservedelta, -8);
	CHECK_GUN(g, VRR_NEED_RACK, 1, 0, 8);
	CHECK_EQ(out.allowfire, 0);
}

/* ---------- per-tick outputs ---------- */

static void test_allowfire_and_handclip_by_state(void)
{
	struct vrrgun g;
	struct vrroutput out;

	vrrInit(&g, 8);
	out = t_quiet(&g);
	CHECK_EQ(out.allowfire, 1);
	CHECK_EQ(out.handclip, 0);

	vrtest_restored_clip_out(&g, 5);
	out = t_quiet(&g);
	CHECK_EQ(out.allowfire, 1); /* chamber loaded: the round can be fired with the clip out */
	CHECK_EQ(out.handclip, 0);

	make_holding(&g, 5);
	out = t_hold(&g, 0);
	CHECK_EQ(out.allowfire, 1); /* chamber loaded while holding the clip */
	CHECK_EQ(out.handclip, 1);

	make_need_rack(&g);
	out = t_quiet(&g);
	CHECK_EQ(out.allowfire, 0);
	CHECK_EQ(out.handclip, 0);

	vrrInit(&g, 8);
	out = t_rack_grab(&g);
	CHECK_EQ(out.allowfire, 0);
	CHECK_EQ(out.handclip, 0);
}

static void test_quiet_tick_resets_fx_and_reservedelta(void)
{
	struct vrrgun g;
	struct vrroutput out;
	vrrInit(&g, 8);
	out = t_eject(&g);
	CHECK_EQ(out.fx, VRR_FX_EJECT);
	CHECK_EQ(out.reservedelta, 8);
	out = t_quiet(&g);
	CHECK_EQ(out.fx, 0);
	CHECK_EQ(out.reservedelta, 0);
	CHECK_EQ(out.loaded, 0);
	CHECK_EQ(out.glow, VRR_GLOW_CLIP);
	CHECK_EQ(out.allowfire, 0); /* the eject emptied the chamber (Task F4a) */
	CHECK_EQ(out.handclip, 0);
}

static void test_prevgrip_follows_grip(void)
{
	struct vrrgun g;
	struct vrrinput in;
	vrrInit(&g, 8);
	t_hold(&g, 0);
	CHECK_EQ(g.prevgrip, 1);
	t_quiet(&g);
	CHECK_EQ(g.prevgrip, 0);
	t_hold(&g, 0);
	in = def_input();
	in.paused = 1;
	in.grip = 1;
	run_tick(&g, &in);
	CHECK_EQ(g.prevgrip, 0);
}

void run_reload_core_tests(void)
{
	test_init_full_gun();
	test_init_empty_gun();
	test_init_negative_loaded_is_empty();
	test_init_one_round_is_chamber_only();
	test_engine_loaded_is_zero_without_chamber();

	test_sync_fire_three_rounds();
	test_sync_copies_engine_count();
	test_sync_negative_loaded_empties_gun();
	test_sync_ignored_when_chamber_empty();
	test_sync_with_clip_out_keeps_chamber_only();

	test_fire_dry_stays_ready();
	test_fire_dry_then_eject();
	test_rack_dry_gun_stays_ready_dry();
	test_rack_empty_no_glow_trigger_dry();

	test_reload_of_loaded_gun_needs_rack();
	test_eject_any_load_returns_all_then_rack();
	test_empty_reload_then_rack();

	test_rack_wastes_chambered_round_until_empty();
	test_rack_with_no_clip_wastes_chamber();
	test_rack_with_no_clip_and_empty_chamber();
	test_rack_applies_once_per_grab();
	test_rack_pull_on_release_tick_counts();
	test_rack_grab_with_full_pull_racks_same_tick();
	test_rack_release_without_pull_restores_state();
	test_rack_grab_needs_rack_zone();
	test_rack_grab_not_from_holding_clip();
	test_rack_zone_wins_over_shoulder();
	test_rack_auto_finish_needs_fresh_press();
	test_rack_release_before_full_pull_no_rack();
	test_rack_travel_default_when_zero_or_negative();
	test_rack_travel_six_cm();

	test_drop_clip_outside_zone();
	test_shoulder_grab_with_no_reserve();
	test_shoulder_grab_with_small_reserve();
	test_held_capped_by_clipsize();
	test_insert_partial_clip();
	test_insert_partial_clip_with_empty_chamber();
	test_insert_with_empty_chamber_uses_full_clipsize();
	test_shoulder_grab_needs_shoulder_zone();
	test_grip_held_does_not_regrab();
	test_shoulder_grab_only_when_clip_out();

	test_seated_insert_with_chambered_round();
	test_seated_insert_empty_chamber();
	test_seated_insert_partial_clip();
	test_seated_outside_clip_zone_does_nothing();
	test_seated_ignored_without_held_clip();
	test_seated_untracked_ignored();
	test_seated_insert_then_rack_needs_fresh_press();

	test_eject_from_need_rack();
	test_eject_full_gun_empties_it();
	test_eject_ignored_with_clip_out();
	test_eject_ignored_while_racking();
	test_eject_and_shoulder_grab_same_tick();
	test_eject_and_rack_grab_same_tick();

	test_paused_holding_clip_in_zone_inserts();
	test_paused_holding_clip_outside_zone_drops();
	test_paused_blocks_eject();
	test_paused_blocks_grabs();
	test_paused_releases_rack();

	test_untracked_changes_nothing();
	test_untracked_rack_pull_ignored();
	test_untracked_release_seen_on_next_tracked_tick();
	test_untracked_keeps_prevhot();
	test_untracked_glow_includes_hot_bits();

	test_zone_enter_fires_once();
	test_zone_enter_not_for_dark_zone();
	test_zone_enter_on_second_zone();
	test_zone_enter_when_zone_lights_up_same_tick();
	test_glow_by_state();
	test_hot_bits_need_matching_glow();

	test_chambered_clip_out_allows_fire();
	test_fire_chambered_round_with_clip_out_needs_rack();
	test_chambered_clip_out_inserts_ready();
	test_empty_eject_blocks_fire();
	test_vrrallowfire_matches_output();
	test_fire_chambered_round_while_holding_clip_needs_rack();

	test_allowfire_and_handclip_by_state();
	test_quiet_tick_resets_fx_and_reservedelta();
	test_prevgrip_follows_grip();
}
