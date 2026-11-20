#ifndef VR_RELOAD_CORE_H
#define VR_RELOAD_CORE_H
#include <stdint.h>

/*
 * Pure manual-reload rules for one gun. No engine headers: the engine #defines bool as s32,
 * so every flag here is an int32_t that is 0 or 1. The engine adapter (vr_reload.c) feeds one
 * vrrinput per gameplay tick and applies the vrroutput.
 */

enum vrrstate {
	VRR_READY,        /* clip in, chamber loaded (or dry with nothing in the clip to chamber:
	                   * fired dry, racked, slapped or settled so; or with loadsoninsert any clip
	                   * in); nothing glows */
	VRR_CLIP_OUT,     /* no clip in the gun, off-hand empty; never with shellbyshell */
	VRR_HOLDING_CLIP, /* off-hand holds a clip taken from the shoulder; with shellbyshell one shell
	                   * (held 1), and the tube (the clip) stays in (magin 1) */
	VRR_NEED_RACK,    /* clip in, chamber empty, rounds in the clip to chamber (with none the gun
	                   * is VRR_READY and dry, on every path); never with loadsoninsert (with
	                   * rackslap a slap finishes it). With pumpaction, the state after each shot
	                   * while the tube has rounds */
	VRR_RACKING       /* off-hand gripping the rack; never with loadsoninsert or rackslap */
};

/* vrroutput.glow bits */
#define VRR_GLOW_CLIP 0x1 /* clip well marker visible */
#define VRR_GLOW_RACK 0x2 /* rack marker visible */
#define VRR_HOT_CLIP  0x4 /* off-hand is inside the glowing clip zone */
#define VRR_HOT_RACK  0x8 /* off-hand is inside the glowing rack zone */

/* vrroutput.fx bits: one-tick events for sounds and haptics */
#define VRR_FX_EJECT       0x001
#define VRR_FX_GRAB_CLIP   0x002
#define VRR_FX_INSERT      0x004
#define VRR_FX_RACK        0x008
#define VRR_FX_WASTE_ROUND 0x010
#define VRR_FX_NO_AMMO     0x020
#define VRR_FX_ZONE_ENTER  0x040
#define VRR_FX_DROP_CLIP   0x080

/* Rack pull used when vrrinput.racktravel is 0, and the least a gun row may set */
#define VRR_RACK_PULL_CM 3.0f

struct vrrgun {
	int32_t state;      /* enum vrrstate */
	int32_t magin;      /* 1 when a clip is in the gun */
	int32_t chamber;    /* rounds in the chamber: 0 or 1 */
	int32_t clip;       /* rounds in the inserted clip, excluding the chamber; 0 when magin is 0 */
	int32_t held;       /* rounds in the clip in the off-hand; 0 unless VRR_HOLDING_CLIP */
	int32_t racked;     /* the rack action happened at the end of the last grab (it ends the grab) */
	int32_t prevgrip;   /* off-hand grip on the previous accepted tick */
	int32_t prevhot;    /* VRR_HOT_* bits on the previous accepted tick */
};

struct vrrinput {
	int32_t eject;      /* 1 on the tick the eject button was pressed (an edge, not a level) */
	int32_t grip;       /* off-hand grip is held */
	int32_t inclip;     /* off-hand is inside the clip zone */
	int32_t inrack;     /* off-hand is inside the rack zone */
	int32_t atshoulder; /* off-hand is inside the shoulder clip-source zone */
	int32_t directchest; /* alien guns (Phoenix, Callisto, FarSight): can grab ammo from chest without eject button */
	float rackpull;     /* cm the off-hand moved along the rack axis since the rack grab, >= 0 */
	float racktravel;   /* cm of pull for a full rack (the slide's travel); 0: VRR_RACK_PULL_CM.
	                     * The rack happens and the grab ends as the pull reaches it */
	int32_t loadsoninsert; /* the gun has no rack: an insert into an empty chamber chambers a
	                        * round from the clip at once (VRR_READY, never VRR_NEED_RACK), no
	                        * rack glows, a rack grab is ignored and a rack under way ends at once
	                        * with no rack. 0: an empty chamber needs a rack. An eject returns the
	                        * chambered round to the reserve with the clip's either way (no
	                        * tactical reload) */
	int32_t rackslap;   /* the gun is racked by a slap (the AR34's and the K7 Avenger's hand
	                     * slapping the clip in from below), not a grip and pull: in VRR_NEED_RACK a
	                     * slap does what a completed rack does (a round from the clip into the
	                     * chamber, VRR_FX_RACK); a rack grab is ignored and a rack under way ends at
	                     * once with no rack, and no rack marker glows (VRR_GLOW_RACK, VRR_HOT_RACK,
	                     * so no VRR_FX_ZONE_ENTER for the rack zone). Otherwise a rack gun: an
	                     * insert with a round chambered is VRR_READY, and vrrSettle takes it as a
	                     * rack gun (loadsoninsert 0). Not with loadsoninsert */
	int32_t pumpaction; /* the gun is pumped after every shot (the Shotgun): a shot leaves the
	                     * chamber empty, nothing is chambered from the clip (the tube) by it, and
	                     * the gun is VRR_NEED_RACK while the tube has rounds (VRR_READY and dry
	                     * with it empty, as a rack gun fired dry), so the trigger is refused until
	                     * the rack. The shot is seen by vrrSyncFromEngineRow, which needs the same
	                     * flag. Otherwise a rack gun: the rack chambers one round from the tube.
	                     * 0: a shot chambers the next round from the clip (the engine's count) */
	int32_t shellbyshell; /* the gun is loaded one round at a time (the Shotgun's shells): the tube
	                       * (the clip) never leaves the gun and the eject does nothing (no
	                       * VRR_FX_EJECT, no rounds back; vrrEjectReturnsRow is 0). In VRR_READY
	                       * or VRR_NEED_RACK a grip at the shoulder takes one shell (held 1,
	                       * VRR_HOLDING_CLIP, VRR_FX_GRAB_CLIP) while the tube has room (clipsize
	                       * minus chamber minus clip above 0) and the reserve has rounds
	                       * (VRR_FX_NO_AMMO with room and an empty reserve, nothing with a full
	                       * tube). An insert at the clip zone (the port) adds one round to the clip
	                       * and takes one from the reserve (VRR_FX_INSERT); one let go elsewhere, or
	                       * with no room or reserve left by then, goes back (VRR_FX_DROP_CLIP,
	                       * nothing taken). Either way the gun is then in the state its chamber
	                       * calls for: VRR_READY when chambered, VRR_NEED_RACK when not and the tube
	                       * has rounds, VRR_READY (dry) with both empty. No rack grab while a shell
	                       * is held. A gun found with its clip out has it put back in, empty */
	int32_t slap;       /* 1 on the tick the off-hand slapped the clip (an event, not a level): used
	                     * only with rackslap in VRR_NEED_RACK, ignored otherwise and while paused */
	int32_t seated;     /* the clip held in the clip zone has been pushed fully in: inserts as a
	                     * release inside the clip zone does, with the grip still held */
	int32_t clipsize;   /* full clip capacity in rounds, including the chamber, as the engine counts it */
	int32_t reserve;    /* rounds in reserve for this gun's ammo type */
	int32_t tracked;    /* both controllers tracked this tick */
	int32_t paused;     /* game is paused */
};

struct vrroutput {
	int32_t loaded;       /* value for hand->loadedammo: chamber ? chamber + clip : 0 */
	int32_t reservedelta; /* add to the reserve; negative when a clip is inserted */
	int32_t glow;         /* VRR_GLOW_* | VRR_HOT_* */
	int32_t fx;           /* VRR_FX_* events this tick */
	int32_t allowfire;    /* vrrAllowFire */
	int32_t handclip;     /* 1 when the off-hand should be drawn holding a clip */
};

void vrrInit(struct vrrgun *g, int32_t loaded);
/* Folds in the engine's loaded count (shots fired), for a row with or without pumpaction. With
 * pumpaction a count below chamber plus clip is a shot, which empties the chamber and leaves the rest
 * in the clip (clip = loaded), and a gun in VRR_READY with rounds left in the clip goes to
 * VRR_NEED_RACK. With pumpaction 0, or a count at or above chamber plus clip: a chambered gun takes
 * chamber 1 and clip loaded - 1 (the shot chambered the next round), an empty count empties both */
void vrrSyncFromEngineRow(struct vrrgun *g, int32_t loaded, int32_t pumpaction);
int32_t vrrEngineLoaded(const struct vrrgun *g);
/* 1 in VRR_READY, and in VRR_CLIP_OUT or VRR_HOLDING_CLIP while a round is chambered */
int32_t vrrAllowFire(const struct vrrgun *g);
/* Rounds an eject would return to the reserve now: 0 with shellbyshell (the eject does nothing to
 * it), and 0 unless the clip is in and the gun is not mid-rack (VRR_RACKING); else the clip's rounds
 * plus the chambered round, for every gun (no tactical reload: an eject empties the chamber too) */
int32_t vrrEjectReturnsRow(const struct vrrgun *g, int32_t shellbyshell);
/* Puts a gun with no grab in progress (restored from a save, filled at a draw, moved by a gun-side
 * flip) in the state its clip and chamber call for: VRR_CLIP_OUT with the clip out; with it in,
 * VRR_READY when chambered or when the clip is empty (dry), else VRR_NEED_RACK. With
 * loadsoninsert a clip in with an empty chamber chambers a round from the clip (VRR_READY).
 * Clears held, racked, prevgrip and prevhot (a held clip is dropped: its rounds never left the
 * reserve) */
void vrrSettle(struct vrrgun *g, int32_t loadsoninsert);
void vrrTick(struct vrrgun *g, const struct vrrinput *in, struct vrroutput *out);

#endif
