#ifndef VR_RELOAD_DATA_H
#define VR_RELOAD_DATA_H
#include <ultra64.h>

#define VR_RELOAD_REACH_MAX 2 /* clipreach points a row can have */
#define VR_RELOAD_HELD_MTX_MAX 3 /* matrices a held part can be skinned to (heldmtx..heldmtxlast) */
#define VR_RELOAD_EJECT_SND_MAX 4 /* sounds an eject playback can play (ejectsnd) */
#define VR_RELOAD_PLAY_SND_MAX 4  /* sounds an insert playback can play (playsnd) */

/* One row per gun that uses manual reload. The grab areas come from the gun model's own part
 * matrices each frame (see vrReloadCaptureParts), so a row holds part ids, radii and directions in
 * a part's local frame, not positions. Distances are cm.
 *
 * A row with no clip part (clippart -1: the Phoenix, which loads an orb held against its side)
 * has an intake point instead of a well (intakemtx, intakepos): the held part loads when its
 * heldtop comes within clipguiderad of the point, or of a stretch from it to a clipreach point (the
 * Crossbow's groove), with no angle test and no slide in (the clip fields clipalignmax,
 * clipinsertdepth, clipaxis and heldaxis are 0, clipwidth 1),
 * and it is never drawn in the gun. B empties the gun with nothing falling (sndeject may be 0).
 * Such a row must load on insert (vrReloadResetAll checks). A row whose held part falls after its
 * playback (playdrop: the Cyclone's bar) sets heldaxis: the way the part slides out and falls. */
struct vrreloaddef {
	s32 weaponnum;
	s32 ammoindex;       /* the gun's ammo slot (a function's ammoindex, an index into loadedammo) this
	                      * row reloads: the rows are keyed by weapon and ammo index (vrReloadGetDef).
	                      * 0 in every gun's row; a gun with a second magazine of its own has a row for
	                      * each (the SuperDragon: the rifle's clip 0, the grenade launcher's magazine
	                      * 1), the primary's listed first. Each magazine keeps its own state, the one
	                      * of the selected function's driven (vr_reload.c, sPending) */
	f32 clipguiderad;    /* cm from the well mouth at which a held, aligned clip starts sliding in (6) */
	f32 clipalignmax;    /* max angle in degrees between held clip and well axis to guide (35);
	                      * 180: any angle (the Tranquilizer's vial, dropped into an open holder) */
	f32 clipinsertdepth; /* cm the clip travels from the mouth to seated */
	f32 clipaxis[3];     /* insert direction in the clip part's local frame (into the gun) */
	f32 clipwidth;       /* scale of the clip's local x (its width) while drawn in the gun, seated
	                      * or guided in, so a clip wider than the grip's back stays inside it; full
	                      * width in the hand and while falling (1: as modelled) */
	f32 clipmid;         /* cm along clipaxis from the clip's joint to its middle, the point a falling
	                      * clip turns about (vrReloadStartDrop): for a clip whose joint is not at its
	                      * top (the Reaper's magazine: its joint at the face between the handle behind
	                      * and the tongue 23.8 cm ahead, so its middle is 8.95 cm ahead). 0: half the
	                      * insert depth back down the axis, the middle of a clip that is as long as
	                      * its depth with its joint at the top (every other row) */
	s32 numclipreach;    /* clipreach points in use, 0..VR_RELOAD_REACH_MAX */
	f32 clipreach[VR_RELOAD_REACH_MAX][3]; /* the guide also starts (and stays, with its exit
	                      * margin) with the held clip's top within clipguiderad of the segment from
	                      * the well mouth to each point, and to it with x negated (the other side, unless
	                      * clipreachoneside):
	                      * cm in clipmtx's local frame (an intake row's: from the intake point, in
	                      * intakemtx's, with no other test). The angle test is the same, and a start
	                      * still needs the top at most VRR_GUIDE_START_PAST_CM (2) past the mouth
	                      * along the insert axis, or, within VRR_GUIDE_REACH_DEEP_CM of a stretch,
	                      * up to VRR_GUIDE_START_PAST_CM past the stretch's depth there, capped at
	                      * the point's */
	s32 rackmtx;         /* the rack's frame, the frame of rackmin, rackmax and rackoffset, whose -z
	                      * is the pull back: -1, the model's MODELPART_GUN_SLIDE, which the engine's
	                      * slide offset (hand->slidetrans) moves and the rack pulls with the hand
	                      * (vrReloadMoveSlide). Else a matrix index, for a gun racked by a part with
	                      * no slide of its own (the CMP150: its body, 33, as nothing of the gun moves
	                      * in its rack): the rack area sits on it, nothing slides under the hand (the
	                      * buzz and sndrack are the feedback), and the engine's slide is left alone.
	                      * The index is trusted only while the row's clip part is in the model */
	f32 rackrad;         /* cm from the rack box to grip it */
	f32 rackmin[3];      /* rack box corners: the slide's extent (or the rack's place on rackmtx), */
	f32 rackmax[3];      /* cm in the rack frame (x mirrored with the model in left-handed mode);
	                      * moves with the slide */
	f32 rackoffset[3];   /* a grab point on the rack, cm in the rack frame, for the logs */
	f32 racktravel;      /* cm of hand pull for full slide travel, which racks; the slide's own
	                      * travel, so it moves with the hand (with rackmtx, only the pull). At least
	                      * VRR_RACK_PULL_CM, unless the row loads on insert: it has no rack and sets
	                      * 0 (vrReloadResetAll checks) */
	s32 loadsoninsert;   /* 1: the gun has no rack: an insert into an empty chamber chambers a
	                      * round at once, and nothing asks for a rack (vrrinput.loadsoninsert).
	                      * A gun whose model has no slide part and whose row has no rackmtx gets no
	                      * rack area, so its row must set this. 0: an empty chamber needs a rack */
	s32 rackslap;        /* 1: the rack is a slap of the clip from below (vrrinput.rackslap), not a
	                      * grip and pull: no rack area is needed, and the slap fields below say where
	                      * and how it is drawn. Not with loadsoninsert. On a row with no clip part (an
	                      * intake row) the rack is the close of a gun held open (closefrom, closeto: the
	                      * DY357's cylinder, shut by a flick or a push), no slap is drawn and the slap
	                      * frames are 0. 0: none */
	s32 pumpaction;      /* 1: the gun is pumped after every shot (vrrinput.pumpaction; the Shotgun): a
	                      * shot leaves the chamber empty and the gun waits for its rack, the pump
	                      * (rackmtx with rackmove: the forend), while the tube has rounds. The pump is
	                      * the support hand's: a grip on the forend works it while the gun waits for
	                      * it, and the two-hand aim stays on (vr_reload.c, vrReloadPumpHand). Not with
	                      * loadsoninsert or rackslap. 0: a shot chambers the next round itself */
	s32 shellbyshell;    /* 1: the gun is loaded one round at a time (vrrinput.shellbyshell; the
	                      * Shotgun's shells into its port): the tube never leaves the gun, B does
	                      * nothing to it, and each grab at the shoulder takes one round (the held
	                      * part), which loads at the intake point (an intake row: clippart -1) into the
	                      * tube; the gun is then as its chamber calls for (ready, or waiting for the
	                      * rack). Such a row needs no loadsoninsert (vrReloadResetAll checks); with it
	                      * (the Crossbow's bolts, no rack) a round put into an empty chamber is
	                      * chambered at once. 0: B
	                      * ejects the clip and a grab takes a full one */
	s32 clippart;        /* model part of the clip in the gun, for bgunSetPartVisible; -1: none (an
	                      * intake row, see above) */
	s32 clipmtx;         /* matrix index of the clip's joint: the clip part is a toggle whose
	                      * display list is skinned to this matrix, and it has no position node of
	                      * its own, so modelFindNodeMtxIndex(clippart) would return the gun root.
	                      * -1 with clippart -1 */
	s32 intakemtx;       /* a row with no clip part: matrix index of the gun body, the frame
	                      * intakepos is given in (trusted only while heldpart is in the model).
	                      * -1 for a row with a clip */
	f32 intakepos[3];    /* the intake point: where the held part's heldtop loads, cm in intakemtx's
	                      * local frame (x mirrored with the model in left-handed mode); 0 when
	                      * intakemtx is -1 */
	s32 holdermtx;       /* matrix index of a position node the clip sits in that the gun's own
	                      * animations hold turned shut and the reload animation turns open about its
	                      * x (the Tranquilizer's vial holder): drawn open, with what hangs off it,
	                      * while no clip is in or an inserted one still slides to seated, so the
	                      * well is captured open; then it turns shut over VRR_HOLDER_CLOSE_60THS and
	                      * plays sndrack, the rack slot, which only a row that loads on insert leaves
	                      * free: a row with a holder must set loadsoninsert (vrReloadResetAll checks).
	                      * -1: none */
	f32 holdframe;       /* reload animation frame where the off-hand holds the clip */
	f32 handturnaxis[3]; /* the turn that puts the hold frame's left wrist (matrix 18) where the */
	f32 handturndeg;     /* Falcon 2's frame-18 left wrist is, as a right-hand turn of handturndeg
	                      * degrees about handturnaxis (column vectors), in the model root's frame:
	                      * the held hand sits on the off controller as the Falcon's tested hand does
	                      * (vrReloadAlignHeldHand turns the copy model by it about the wrist, mirrored
	                      * with the model in left-handed mode). Measured by tools/vr_measure/handturn.py;
	                      * 0 and a zero axis: none (the Falcon, and rows the user finds lined up) */
	s32 heldpart;        /* model part of the clip in the off-hand, a toggle skinned to heldmtx */
	s32 heldmtx;         /* matrix index of the clip in the off-hand, in the copy-weapon model
	                      * drawn at holdframe (the same model as the gun) */
	s32 heldmtxlast;     /* the held part is skinned to the run of matrices heldmtx..heldmtxlast,
	                      * all kept drawn with the off-hand (at most VR_RELOAD_HELD_MTX_MAX; the
	                      * Phoenix's orb: 37 to 39); -1: heldmtx alone */
	f32 heldtop[3];      /* top of the held clip (the end that enters first), cm in heldmtx's
	                      * local frame; for an intake row, the point that loads at intakepos */
	f32 heldaxis[3];     /* the held clip's insert direction (base to top) in heldmtx's local frame */
	s32 sndeject, sndinsert, sndrack; /* 0: no sound */
	s32 sndinsert2;      /* a second sound insert2delay after the insert (a reload script's sound
	                      * after its insert sound); 0: none (every row since the AR34 and the K7
	                      * Avenger's tap became their slap, sndrack). An eject before it drops it */
	f32 insert2delay;    /* 60ths of game time from the insert to sndinsert2 (the script's frames) */
	f32 playfrom;        /* a row with no clip part: after the insert the gun plays its reload */
	f32 playto;          /* animation from frame playfrom to playto, at the script's speed (a frame
	                      * per 60th of game time), with only the matrices playmtx..playmtxlast posed
	                      * and those relative to the gun body (intakemtx), so the body and the right
	                      * hand stay on the controller (vrReloadPosePlay; the Phoenix: the orb
	                      * pressed into the intake and the side plates swinging out and back).
	                      * A row with a clip part, a slap and a bodymtx (the Laptop Gun) plays it
	                      * from the slap's start instead, on bodymtx, playmtx..playmtxlast its clip
	                      * matrix alone (the clip rocked flat and slid into the seat), playfrom
	                      * being slapfrom; until then an inserted or guided clip is drawn as the
	                      * animation has it at holdframe (hooked in at its front), read from the
	                      * copy model (vrReloadCaptureHeldClip). The clip's pose is drawn after the
	                      * capture (vrReloadPlaceClip), so the well and the slap point stay on the
	                      * seat. playto 0: none (the other rows with a clip: their insert is the
	                      * guided slide) */
	f32 playheldto;      /* the held part (heldpart) is drawn in the gun while the playback is below
	                      * this frame, where the reload script hides it */
	s32 playmtx;         /* the run of matrices the playback poses: under the body or under the left */
	s32 playmtxlast;     /* wrist (the orb), never the body or the right hand; -1 -1 with playto 0 */
	f32 slapfrom;        /* a row with rackslap: the reload animation's slap, frames slapfrom to slapto, */
	f32 slapcontact;     /* drawn on the free hand (the copy model placed on the gun's drawn clip) once */
	f32 slapto;          /* a fast swing from below reaches the slap point; the core gets the slap at
	                      * slapcontact, where the palm meets the clip. 0 0 0 without rackslap */
	f32 slappoint[3];    /* where the slapping palm meets the clip: cm in clipmtx's local frame (x
	                      * mirrored with the model in left-handed mode); the drawn free hand's palm
	                      * (vrReloadHandPalm) must come within slaprad of it. A row with a close
	                      * (closeto): where the push's palm closes it, cm in intakemtx's frame */
	f32 slaprad;         /* cm; 0 without rackslap */
	s32 rackmove;        /* 1: the rack matrix (rackmtx) is a part of its own that the rack pulls
	                      * back (the RC-P120's knob, a matrix with no part id, which the engine's
	                      * slide offset cannot reach): drawn moved back along the rack frame's -z by
	                      * the pull while racking (at most racktravel), as a slide is, and returning
	                      * over VRR_RACK_RETURN_60THS when the rack ends (vrReloadMoveRack). 0: the
	                      * rack frame stays put (the CMP150's body; and every row on the slide part,
	                      * rackmtx -1, which the slide offset moves) */
	s32 rackfollowmtx;   /* a row with rackmove: a second matrix the rack draws moved back with it, a
	                      * part linked to the one the hand pulls (the Devastator's front sleeve, matrix
	                      * 35, slides 6.4 cm back as the hand pulls its cocking lever, 34, 4.9):
	                      * moved back along its own -z by the rack matrix's pull times
	                      * rackfollowscale (vrReloadMoveRack). Only that matrix moves, as the rack
	                      * matrix (the drawn matrices are absolute: nothing under it follows). -1: none */
	f32 rackfollowscale; /* cm the follower moves per cm of pull; 0 with rackfollowmtx -1 */
	f32 playdrop;        /* a row with a playback whose held part leaves the gun at its end (the
	                      * Cyclone's bar, slid on through its slot and out the far side): the held
	                      * part's length in cm from heldtop back along -heldaxis. The part is drawn
	                      * in the gun through playto, then falls as an ejected clip does, from where
	                      * the playback drew it last, sliding on along heldaxis (vrReloadStartDrop).
	                      * 0: none (the held part is hidden from playheldto) */
	f32 playease;        /* a row with a playback: 60ths of game time over which the held part
	                      * (heldmtx) goes from where the free hand held it at the load to the
	                      * playback's pose of it, so it does not jump to the intake (vrReloadPosePlay).
	                      * 0: it is drawn at the playback's pose at once (the Phoenix) */
	s32 bodymtx;         /* a row with a clip part: the gun body's matrix, the frame of seatpos and
	                      * seatrot and the one the eject playback is posed on (the Laptop Gun's 33; a
	                      * row with no clip part poses it on intakemtx).
	                      * -1: none (no clipseat and no eject playback) */
	s32 clipseat;        /* 1: the clip matrix (clipmtx) is drawn every frame at seatpos and seatrot on
	                      * bodymtx, before the capture (vrReloadSeatClip), so the guide, the drawn
	                      * clip, its fall and the slap point all use that seat: for a gun whose clip
	                      * joint sits elsewhere outside its reload animation (the Laptop Gun's is
	                      * inside the pistol grip at rest, but the reload seats the clip under the
	                      * front half). The slap's drawn hand is then placed by the animation's clip
	                      * as posed at slapcontact, put on the seat (vrReloadNoteSlapClip), and
	                      * slappoint is in that frame; for a row whose clip plays with the slap
	                      * (playto, the Laptop Gun's), by the body. 0: the clip matrix as the gun's
	                      * animation poses it */
	f32 seatpos[3];      /* the seated clip's joint, cm in bodymtx's local frame (x mirrored with the
	                      * model in left-handed mode, through the matrix) */
	f32 seatrot[3][3];   /* the seated clip's local x, y and z axes (rows, unit) in bodymtx's frame */
	f32 ejectfrom;       /* B (an eject) plays the reload animation from frame ejectfrom to ejectto, at */
	f32 ejectto;         /* the script's speed, with only the matrices ejectmtx..ejectmtxlast posed,
	                      * relative to bodymtx for a row with a clip part (vrReloadPosePlay; the Laptop
	                      * Gun's pump of its forend halves), to intakemtx for a row with none (the
	                      * Rocket Launcher's hatch strips opening). ejectto 0: none */
	s32 ejectmtx;        /* the run of matrices the eject playback poses: never the body or the hands; */
	s32 ejectmtxlast;    /* -1 -1 with ejectto 0 */
	s32 ejectsnd[VR_RELOAD_EJECT_SND_MAX]; /* sounds the eject playback plays as it reaches frames */
	f32 ejectsndat[VR_RELOAD_EJECT_SND_MAX]; /* ejectsndat (the script's frames; the Laptop Gun's
	                      * pump strokes and the clip's fall); 0: none */
	s32 ejectdrop;       /* 1: the eject playback poses the clip matrix too (as the animation has it
	                      * against bodymtx, drawn after the capture, so the well stays seated): the
	                      * clip is drawn in the gun through ejectto and then falls from that pose, not
	                      * at the press (the Laptop Gun's clip, seated through the pump, tipping out
	                      * rear end first). A clip guided in meanwhile ends the playback with no fall.
	                      * 0: the clip falls at the press */
	s32 ejecthold;       /* 1: the eject playback's last frame (ejectto) stays posed while the gun has no
	                      * clip in (the core's magin 0: from the eject until the load), so the gun stays
	                      * open as the playback left it until it is loaded (the Rocket Launcher's hatch
	                      * strips); the insert's playback then takes over. Such a row takes its round
	                      * only once the eject playback has ended (the hatch fully open), and fires
	                      * only once the insert's has (the hatch shut). A row with a close (closeto)
	                      * stays open after the load too, until the close. 0: none */
	s32 emptyejects;     /* 1: a gun left empty with its clip in (a shot took its last round) ejects at
	                      * once, as B does, so a row with an eject playback opens after the shot (the
	                      * Rocket Launcher), and B on it with a round in is refused (vrReloadEjectRefused).
	                      * 0: only B ejects */
	s32 playhold;        /* a row with an insert playback (playto): 1: its held part is drawn through the
	                      * whole playback and, after it, posed at playto while the gun has a round
	                      * chambered, so the loaded round is seen in the gun (the Crossbow's bolt at
	                      * the nock: its loaded-bolt part has no geometry); playheldto is then playto.
	                      * A round inserted behind a chambered one plays no playback (it goes into
	                      * the tube unseen and the loaded round stays put).
	                      * 0: the held part is hidden from playheldto (or falls, playdrop) */
	s32 playsnd[VR_RELOAD_PLAY_SND_MAX]; /* sounds the insert's playback plays as it reaches */
	f32 playsndat[VR_RELOAD_PLAY_SND_MAX]; /* frames playsndat (the script's frames: the Rocket
	                      * Launcher's rocket landing and its hatch shutting); 0: none */
	f32 clipturnaxis[3]; /* after the hand turn, the turn of the held part alone (heldmtx..heldmtxlast) */
	f32 clipturndeg;     /* that puts it in the drawn hand as the Falcon 2's clip sits in its hand at
	                      * frame 18 (its insert axis and front in the wrist's frame as the Falcon
	                      * clip's): a right-hand turn of clipturndeg degrees about
	                      * clipturnaxis (column vectors) in the model root's frame, about
	                      * clipturnpivot, mirrored with the model in left-handed mode as the hand turn
	                      * is (vrReloadAlignHeldHand). Measured by tools/vr_measure/clipturn.py; 0 and a
	                      * zero axis: none (the rows whose hand holds the clip as the user finds right,
	                      * and the intake rows but the Rocket Launcher's, whose rocket it turns end for
	                      * end, and the Crossbow's, whose bolt it lays forward: tools/vr_measure/
	                      * rocketflip.py, boltturn.py) */
	f32 clipturnpivot[3]; /* the point it turns about, the palm (the middle of the hand bone's
	                      * vertices at the hold frame; the Rocket Launcher's: a point on the rocket's
	                      * axis), cm in heldmtx's local frame */
	f32 clipshift[3];    /* after the clip turn, the move of the held part alone (heldmtx..heldmtxlast)
	                      * that puts it where the Falcon 2's clip sits in its hand at frame 18, not on
	                      * the knuckles: the clip's grip point (the middle of its length and across,
	                      * on its palm side) at the Falcon's offset from the palm, the middle of the
	                      * hand bone and its four knuckles (vrReloadAlignHeldHand). cm in heldmtx's
	                      * local frame as turned (mirrored with the model in left-handed mode, as the
	                      * matrix is). Measured by tools/vr_measure/clipshift.py; zero: none (the rows
	                      * with no clip turn) */
	f32 heldlookaxis[3]; /* a draw-only turn of the held part alone (heldmtx..heldmtxlast), after the */
	f32 heldlookdeg;     /* capture (vrReloadPoseHeldLook), so the guide's held top and axis, and so
	                      * where the clip goes in, are as without it: a right-hand turn of heldlookdeg
	                      * degrees about heldlookaxis (column vectors) in the drawn left hand's frame
	                      * (the copy model's wrist, matrix 18: z along the hand to the knuckles, x
	                      * across the knuckles from the index to the little finger, -y the palm's
	                      * side, the way the fingers curl), about heldlookpivot, mirrored with the
	                      * model in left-handed mode. 0 and a zero axis: none (every row but the
	                      * Laptop Gun's) */
	f32 heldlookpivot[3]; /* the point it turns about, cm in heldmtx's local frame */
	f32 heldlookshift[3]; /* then a draw-only move of the held part alone, cm in the drawn left
	                      * hand's frame (as heldlookaxis); zero: none */
	const char *hint;    /* the reload hint (Task F9d), shown as "<gun name>: <hint>" on the HUD the
	                      * first time the gun is reloaded in a session and when the player is stuck
	                      * (vrReloadStepHint): short, so the line wraps to two at most with the longest
	                      * name in the VR wrap (VR_HUDMSG_WRAP_MAX, hudmsg.c;
	                      * tools/vr_measure/hintwidth.py). NULL: none */
	f32 insertframefrom; /* frame where insertion begins (0: use holdframe) */
	f32 insertframeto;   /* frame where clip is fully seated (0: no insertion anim) */
	f32 rackframefrom;   /* frame where the off-hand grips the slide/rack (0: no rack pose) */
	f32 rackframeto;     /* frame where the slide/rack is fully pulled back */
	s32 sndfull;         /* a shellbyshell row's sound for the round that fills the gun (chamber and
	                      * tube at clipsize), in place of sndinsert and sndinsert2 and at the time
	                      * the second would play (Task F11d: the user wants to hear it is full).
	                      * 0: as any other round; every row but the Shotgun's leaves it out */
	s32 heldpartlast;    /* the held part is the run of parts heldpart..heldpartlast, each its own toggle,
	                      * shown and hidden together in the gun (the DY357's six rounds, 0x28 to 0x2d,
	                      * all on matrix 46: the speedloader's load). 0, or not above heldpart: heldpart
	                      * alone; every row but the DY357s' leaves it out */
	f32 closefrom;       /* an intake row with rackslap and an eject playback held open (ejecthold): the */
	f32 closeto;         /* gun stays open (the eject's last frame) from the eject until the core's
	                      * rack, which is the close, not a slap of a clip: a flick of the gun hand or
	                      * a push of the free hand (vrReloadStepClose; the DY357's cylinder, swung out
	                      * by B, loaded with six rounds and shut). The close plays the reload animation
	                      * from closefrom to closeto on ejectmtx..ejectmtxlast, posed on the body as the
	                      * eject's, and the gun fires once it has ended. 0 0: none */
	f32 closesndat;      /* the close's frame at which sndrack plays (in closefrom..closeto), in place of
	                      * the rack sound at the rack itself */
	f32 flickaxis[3];    /* the close by a flick: the gun hand rolling at VRR_FLICK_RADS or more either
	                      * way about this axis (unit, in intakemtx's frame: the barrel; mirrored with
	                      * the model in left-handed mode, as a turn is) closes it. Zero: no flick */
	f32 closedir[3];     /* the close by a touch: the free hand's palm (vrReloadHandPalm) within slaprad of
	                      * slappoint (both in intakemtx's frame on such a row: the open part's middle)
	                      * moving at VRR_TOUCH_SPEED or more along this direction against the gun (unit,
	                      * in intakemtx's frame: the way the open part goes back in) closes it. Zero:
	                      * no touch */
	s32 ejectcasings;    /* 1: the eject playback's end (the cylinder swung out) throws a casing from the
	                      * chambers for each round the eject did not take back (the chambers left
	                      * without a live round: fired), through the engine's own path for the reload
	                      * script's casings (bgunUpdateMagnum: the DY357s' chamber parts 0x0a to 0x0f;
	                      * vrReloadTakeCasings). 0: none */
	s32 playstraightmtx; /* a row with an insert playback whose held part goes into this matrix, one of
	                      * playmtx..playmtxlast (the DY357's open cylinder, 34): the held matrix is drawn
	                      * on it straight, its turn there a right-hand roll of playstraightroll degrees
	                      * about its +z and its origin's x and y playstraightpos (cm in its frame), so the
	                      * held part's axis is that matrix's +z; only the origin's z is the playback's,
	                      * so it goes in along that axis (the reload animation's own rounds lean 1 to 3
	                      * degrees, turn 6 to 14 off the chambers and sit up to 0.6 cm off them, reaching
	                      * 0.3 cm out of the cylinder's side). Through the insert's playback, the pose
	                      * held after it and the close that carries it in. With playease the playback
	                      * waits at playfrom until the hand-off has ended, so the held part is first put
	                      * straight behind the matrix and only then goes in (Task H6b). 0: none (every
	                      * row but the DY357s') */
	f32 playstraightroll;
	f32 playstraightpos[2];
	s32 clipreachoneside; /* 1: the clipreach points are used as given only, not also with x negated, so
	                      * the reach widens one side of the gun (+x of the clip's or intake's frame is
	                      * the off hand's side on either gun side: the matrix carries the mirror; the
	                      * Shotgun's port, Task J6a). 0: both sides (every row but the Shotgun's) */
};

/* The row for this gun and ammo slot, NULL when it has none. ammoindex -1: the gun's first row (its
 * primary magazine's), NULL when the gun has no row at all */
struct vrreloaddef *vrReloadGetDef(s32 weaponnum, s32 ammoindex);
s32 vrReloadDefCount(void);
struct vrreloaddef *vrReloadDefAt(s32 index);       /* NULL outside 0..vrReloadDefCount()-1 */
/* The last part of the row's held part: heldpartlast when it is above heldpart, else heldpart */
s32 vrReloadHeldPartLast(const struct vrreloaddef *def);

#endif
