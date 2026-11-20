/* Pure manual-reload state machine for one gun: see vr_reload_core.h. No engine dependencies. */
#include "game/vr_reload_core.h"

#include <string.h>

void vrrInit(struct vrrgun *g, int32_t loaded)
{
	memset(g, 0, sizeof(*g));
	g->state = VRR_READY;
	g->magin = 1;

	if (loaded > 0) {
		g->chamber = 1;
		g->clip = loaded - 1;
	}
}

int32_t vrrEngineLoaded(const struct vrrgun *g)
{
	return g->chamber ? g->chamber + g->clip : 0;
}

void vrrSyncFromEngineRow(struct vrrgun *g, int32_t loaded, int32_t pumpaction)
{
	if (g->chamber == 0) {
		return;
	}

	if (pumpaction && loaded < g->chamber + g->clip) {
		/* A pump gun's shot: the chamber is left empty and what the engine still counts is the
		 * tube's. A gun that was ready waits for the pump while the tube has rounds; with the tube
		 * empty it is ready and dry, as a rack gun fired dry. A shell held stays held: its insert
		 * or drop settles the gun */
		g->chamber = 0;
		g->clip = g->magin && loaded > 0 ? loaded : 0;

		if (g->state == VRR_READY && g->clip > 0) {
			g->state = VRR_NEED_RACK;
		}
	} else if (loaded <= 0) {
		g->chamber = 0;
		g->clip = 0;
	} else if (g->magin == 0) {
		g->chamber = 1;
		g->clip = 0;
	} else {
		g->chamber = 1;
		g->clip = loaded - 1;
	}
}

int32_t vrrAllowFire(const struct vrrgun *g)
{
	/* With the clip out (in the hand or not) a chambered round can still be fired: an eject
	 * empties the chamber (no tactical reload), so only a gun restored or settled with the clip out
	 * and a round chambered has one. Firing it empties the chamber, so the clip inserted next needs
	 * a rack (or, with loadsoninsert, chambers a round itself). */
	return g->state == VRR_READY
		|| (g->chamber && (g->state == VRR_CLIP_OUT || g->state == VRR_HOLDING_CLIP));
}

/* What an eject of a gun whose clip leaves returns */
static int32_t vrrEjectReturnsClip(const struct vrrgun *g)
{
	/* No tactical reload (user, 2026-10-19): every gun's eject returns the chambered round with
	 * the clip, as only a gun that loads on insert did before */
	if (!g->magin || g->state == VRR_RACKING) {
		return 0;
	}

	return g->clip + g->chamber;
}

int32_t vrrEjectReturnsRow(const struct vrrgun *g, int32_t shellbyshell)
{
	return shellbyshell ? 0 : vrrEjectReturnsClip(g);
}

/* Puts the gun in the state its clip and chamber call for, with no grab in progress, on every path
 * (a settle, an insert, a shell's insert or drop, a rack or slap, a rack let go early). With
 * loadsoninsert there is no rack: a clip in with an empty chamber chambers a round from the clip,
 * and a gun with a clip in is ready even when it stays dry (an empty clip, fired dry). A gun with
 * nothing in the clip to chamber is VRR_READY and dry too: it clicks, as a gun fired dry, and
 * never glows for a rack, so an empty gun is not pumped on and on (B, or a shell, is the way on) */
static void vrrSettleState(struct vrrgun *g, int32_t loadsoninsert)
{
	if (loadsoninsert && g->magin && !g->chamber && g->clip > 0) {
		g->clip -= 1;
		g->chamber = 1;
	}

	g->state = !g->magin ? VRR_CLIP_OUT
		: (g->chamber || loadsoninsert || g->clip == 0 ? VRR_READY : VRR_NEED_RACK);
}

void vrrSettle(struct vrrgun *g, int32_t loadsoninsert)
{
	vrrSettleState(g, loadsoninsert);
	g->held = 0;
	g->racked = 0;
	g->prevgrip = 0;
	g->prevhot = 0;
}

static void vrrFillOutput(const struct vrrgun *g, const struct vrrinput *in, struct vrroutput *out)
{
	int32_t glow = 0;

	switch (g->state) {
	case VRR_CLIP_OUT:
	case VRR_HOLDING_CLIP:
		/* Only the clip zone: a rack with the clip out chambers nothing (and every eject leaves the
		 * chamber empty, so a rack glow here would show on every reload) */
		glow = VRR_GLOW_CLIP;
		break;
	case VRR_NEED_RACK:
		glow = VRR_GLOW_RACK;
		break;
	case VRR_RACKING:
		glow = g->racked ? 0 : VRR_GLOW_RACK;
		break;
	}

	/* A gun that loads on insert never asks for a rack, and a gun racked by a slap has no rack to
	 * grab: no rack marker, so no rack zone to enter (VRR_HOT_RACK, VRR_FX_ZONE_ENTER) */
	if (in->loadsoninsert || in->rackslap) {
		glow &= ~VRR_GLOW_RACK;
	}

	if (in->inclip && (glow & VRR_GLOW_CLIP)) {
		glow |= VRR_HOT_CLIP;
	}

	if (in->inrack && (glow & VRR_GLOW_RACK)) {
		glow |= VRR_HOT_RACK;
	}

	out->loaded = vrrEngineLoaded(g);
	out->glow = glow;
	out->allowfire = vrrAllowFire(g);
	out->handclip = g->state == VRR_HOLDING_CLIP;
}

/* What a completed rack does: a chambered round is thrown out, and a round from the clip goes in */
static void vrrRackAction(struct vrrgun *g, struct vrroutput *out)
{
	if (g->chamber) {
		g->chamber = 0;
		out->fx |= VRR_FX_WASTE_ROUND;
	}

	if (g->magin && g->clip > 0) {
		g->clip -= 1;
		g->chamber = 1;
	}

	out->fx |= VRR_FX_RACK;
}

/* A gun loaded shell by shell, ready or waiting for the pump: a grip at the shoulder takes one
 * shell while the tube has room. Its round leaves the reserve only at the insert */
static void vrrGrabShell(struct vrrgun *g, const struct vrrinput *in, struct vrroutput *out,
		int32_t down)
{
	if (!in->shellbyshell || !down || !in->atshoulder || in->clipsize - g->chamber - g->clip <= 0) {
		return;
	}

	if (in->reserve > 0) {
		g->held = 1;
		g->state = VRR_HOLDING_CLIP;
		out->fx |= VRR_FX_GRAB_CLIP;
	} else {
		out->fx |= VRR_FX_NO_AMMO;
	}
}

void vrrTick(struct vrrgun *g, const struct vrrinput *in, struct vrroutput *out)
{
	int32_t grip, down, up, hot;

	out->fx = 0;
	out->reservedelta = 0;

	if (!in->tracked) {
		vrrFillOutput(g, in, out);
		return;
	}

	grip = in->paused ? 0 : in->grip;
	down = grip && !g->prevgrip;
	up = !grip && g->prevgrip;

	/* A gun loaded shell by shell has its tube in for good: one found with it out (a shape from
	 * before the flag) gets it back, empty */
	if (in->shellbyshell && !g->magin) {
		g->magin = 1;
		g->clip = 0;

		if (g->state == VRR_CLIP_OUT) {
			vrrSettleState(g, in->loadsoninsert);
		}
	}

	if (in->eject && g->magin && g->state != VRR_RACKING && !in->paused && !in->shellbyshell) {
		/* B empties the gun (no tactical reload): the clip and the chambered round go back to the
		 * reserve, so every new clip needs the rack, the slap or (loadsoninsert) loads at once */
		out->reservedelta += vrrEjectReturnsClip(g);
		g->clip = 0;
		g->chamber = 0;
		g->magin = 0;

		out->fx |= VRR_FX_EJECT;
		g->state = VRR_CLIP_OUT;
	}

	if (down && in->inrack && !in->loadsoninsert && !in->rackslap
			&& (g->state == VRR_READY || g->state == VRR_CLIP_OUT || g->state == VRR_NEED_RACK)) {
		g->state = VRR_RACKING;
		g->racked = 0;
		down = 0;
	}

	switch (g->state) {
	case VRR_READY:
		vrrGrabShell(g, in, out, down);
		break;
	case VRR_CLIP_OUT:
		if (down && in->atshoulder) {
			if (in->reserve > 0) {
				g->held = in->clipsize < in->reserve ? in->clipsize : in->reserve;
				g->state = VRR_HOLDING_CLIP;
				out->fx |= VRR_FX_GRAB_CLIP;
			} else {
				out->fx |= VRR_FX_NO_AMMO;
			}
		}
		break;
	case VRR_HOLDING_CLIP:
		if (in->shellbyshell) {
			if (up || (in->seated && in->inclip)) {
				/* The shell goes into the tube if it still has room and the reserve the round
				 * comes from still has it; otherwise it goes back, and nothing was taken */
				if (in->inclip && g->held > 0 && in->reserve > 0
						&& in->clipsize - g->chamber - g->clip > 0) {
					g->clip += 1;
					out->reservedelta -= 1;
					out->fx |= VRR_FX_INSERT;
				} else {
					out->fx |= VRR_FX_DROP_CLIP;
				}

				g->held = 0;
				vrrSettleState(g, in->loadsoninsert);
			}
			break;
		}

		if (up || (in->seated && in->inclip)) {
			if (in->inclip) {
				int32_t room = in->clipsize - g->chamber;

				if (room < 0) {
					room = 0;
				}

				g->clip = g->held < room ? g->held : room;
				out->reservedelta -= g->clip;
				g->magin = 1;
				g->held = 0;
				out->fx |= VRR_FX_INSERT;
				/* With no rack on this gun the insert itself chambers a round */
				vrrSettleState(g, in->loadsoninsert);
			} else {
				g->held = 0;
				out->fx |= VRR_FX_DROP_CLIP;
				g->state = VRR_CLIP_OUT;
			}
		}
		break;
	case VRR_NEED_RACK:
		/* A gun racked by a slap: the slap is the whole rack */
		if (in->rackslap && in->slap && !in->paused) {
			vrrRackAction(g, out);
			vrrSettleState(g, 0);
		}

		vrrGrabShell(g, in, out, down);
		break;
	case VRR_RACKING:
		/* A rack under way when the gun turns out to have none ends at once, with no rack. A gun
		 * racked by a slap ends it the same way, with no rack, as a rack gun */
		if (in->loadsoninsert || in->rackslap) {
			vrrSettleState(g, in->loadsoninsert);
			break;
		}

		if (!g->racked && in->rackpull >= (in->racktravel > 0.0f ? in->racktravel : VRR_RACK_PULL_CM)) {
			vrrRackAction(g, out);
			g->racked = 1;
		}

		/* Pump-action guns finish their rack at full pull so the forward stroke (sPumpRide) can run.
		 * Slide / cocking handle weapons remain gripped in VRR_RACKING until the player releases the grip button. */
		if (up || (in->pumpaction && g->racked)) {
			vrrSettleState(g, 0);
		}
		break;
	}

	g->prevgrip = grip;
	vrrFillOutput(g, in, out);

	hot = out->glow & (VRR_HOT_CLIP | VRR_HOT_RACK);

	if (hot & ~g->prevhot) {
		out->fx |= VRR_FX_ZONE_ENTER;
	}

	g->prevhot = hot;
}
