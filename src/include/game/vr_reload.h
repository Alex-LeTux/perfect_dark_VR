#ifndef VR_RELOAD_H
#define VR_RELOAD_H
#include <ultra64.h>
#include "types.h"

struct vrreloaddef;

/* Logical controller indices for haptics (vrReloadBuzz) and gCtrlPos: the free hand's and the gun
 * hand's, whatever the gun side */
#define VRR_CTRL_OFF 0
#define VRR_CTRL_GUN 1

/* The table row when manual reload owns this hand's current gun, else NULL. NULL for any player
 * other than player 0, with manual reload (and so the holsters) off, or while the
 * unlimited-ammo-no-reloads cheat is on. The right hand's gun is owned with or without dual wield;
 * the left hand's gun only in dual wield. The right gun is driven by the gestures; the left gun of
 * a pair is only tracked (its clip state, saved when it is put away, gates its trigger). Of a gun
 * with a row per magazine (the SuperDragon: the rifle clip and the grenade magazine, Task F9e), the
 * row of the magazine the core drives: the selected function's, taken over on the tick after a
 * function change (until then the one driven before); before the core drives the gun, the
 * selected function's (a function with no ammo, or none with a row: the gun's first row). */
struct vrreloaddef *vrReloadOwnedDef(s32 handnum);
bool vrReloadOwnsGun(s32 handnum);     /* vrReloadOwnedDef(handnum) != NULL */
/* Manual reload owns the gun and this ammo slot (a function's ammoindex): a slot with a row of its
 * own (0 in every gun's row; the SuperDragon's grenade magazine, 1, has one too since Task F9e), or
 * -1 (a function with no ammo: the pistol whip, the Dragon's self-destruct). A slot with no row (none
 * today; the SuperDragon's grenades until Task F9e) is the engine's: B with that function selected
 * does the engine's reload, the engine's auto reload of it works, its shots are not gated and a draw
 * fills it, while the row's clip stays owned, drawn and driven. The HUD is the engine's: it shows the
 * selected function's loadedammo, which each owned magazine keeps whether driven or parked */
bool vrReloadOwnsAmmo(s32 handnum, s32 ammoindex);
/* vrReloadOwnsAmmo for the selected function's ammo (currentPlayerGetWeaponFunction) */
bool vrReloadOwnsSelectedFunc(s32 handnum);
void vrReloadRequestEject(s32 handnum);
/* B is refused on the right hand's owned gun: a row that opens itself after the shot (emptyejects:
 * the Rocket Launcher's hatch) with its round in (the user, 2026-10-20: "rocket launcher B should not
 * work if its loaded"). An empty one (the hatch shut with nothing in, or still opening) takes B */
bool vrReloadEjectRefused(void);
/* Draw-time clip fill, called from the CHANGEGUN raise in place of the engine's refill
 * (bgun0f098f8c). When this hand's new gun is owned and was put away from this hand (saved shapes
 * are per weapon, engine hand and magazine) by a weapon switch, loads each saved magazine with
 * exactly the rounds it was put away with (fewer if the reserve no longer has them), takes them
 * from the reserve and returns the ammo slots it filled as bits (1 << ammoindex): the caller fills
 * the gun's other slots as the engine does (a magazine with no saved shape: never put away by a
 * switch, or no slot free). 0, and nothing changed (but the gun's slots used up): the caller does
 * the engine's refill of the whole gun. */
s32 vrReloadDrawFill(s32 handnum);
/* Called from bgunTickInc after vr_gun_pos_rot. bgunTickHand can loop bgunTickInc, so this may run
 * several times per frame; effects are edge-triggered, so the repeats do nothing. */
void vrReloadTick(s32 handnum);
/* Called from bgun0f0a5550 once the hand's gun part matrices are built and the slide offset is
 * applied, first of the manual reload's moves: for a row with a clip seat (clipseat: the Laptop
 * Gun), writes the clip matrix at the row's seat on the gun body, so every later read and move of
 * it (the capture, the guide, the fall, the slap) starts from there. Does nothing unless the hand's
 * gun is owned. */
void vrReloadSeatClip(s32 handnum, struct modeldef *modeldef, Mtxf *matrices);
/* Called from bgun0f0a5550 right after vrReloadSeatClip, before vrReloadCaptureParts. For a row
 * with a holder (holdermtx), draws it, and the position nodes under it, turned open from the gun
 * animation's shut pose (all the way while the clip is out or still sliding in, part way while it
 * shuts), so the capture takes the well from the open holder. Does nothing unless the hand's gun is
 * owned. */
void vrReloadPoseHolder(s32 handnum, struct modeldef *modeldef, Mtxf *matrices);
/* Called from bgun0f0a5550 right after vrReloadPoseHolder, with the hand's gun model (its matrices
 * the drawn ones, built and with the slide offset applied) and the row's reload animation
 * (VR_ANIM_ID). While the gun plays the insert of a row with a playback (playto: the Phoenix's orb
 * going in, started by the insert and stepped by the tick), poses the model at the playback's frame
 * with an anim state of its own into a scratch buffer and moves the drawn matrices
 * playmtx..playmtxlast to where that frame has them relative to the gun body (intakemtx): the body,
 * the hands and every other part stay as drawn. The eject playback of a row with one (ejectto: the
 * Laptop Gun's pump, started by B) poses ejectmtx..ejectmtxlast on bodymtx the same way (an intake
 * row's on intakemtx: the Rocket Launcher's hatch, started by B or the shot that empties it), and
 * so does a row's close (closefrom..closeto on the eject's matrices: the DY357's cylinder swinging
 * back in, carrying its rounds). With no playback running, a row that holds a last frame is posed
 * there the same way: ejectto while the gun has no clip in (ejecthold: the hatch held open to the
 * load), playto while a round is chambered (playhold: the Crossbow's bolt at the nock) or while a
 * row with a close is loaded and waits for it (the DY357's cylinder, out, with the rounds in).
 * Returns true when the held part is to be drawn in the gun (posed by the insert's playback, below
 * playheldto, through playto for a row whose held part then falls, throughout and after it with
 * playhold, while a row waits for its close and through the close); false, at no cost, while
 * nothing plays or is held. */
bool vrReloadPosePlay(s32 handnum, struct model *model, s32 animnum);
/* Called from bgun0f0a5550 before the DY357s' casing update (bgunUpdateMagnum): the casings a row with
 * ejectcasings throws at the end of its eject playback, for the driven gun, once (0 after): the
 * caller puts them in hand->unk0cc8_04, as the reload script's popoutsackofpills does, for this draw */
s32 vrReloadTakeCasings(s32 handnum);
/* Called from bgun0f0a5550 right after vrReloadPosePlay, before vrReloadCaptureParts: for a row with
 * rackmove (the RC-P120's knob, matrix 34, which has no part id for the engine's slide offset), draws
 * the rack matrix moved back along its own -z by the pull while racking, and going forward after
 * the rack, so the capture takes the rack box where the knob is drawn, as a slide's. Only that
 * matrix moves (the knob has no matrices under it), and a linked part's (rackfollowmtx: the
 * Devastator's front sleeve, moved with its cocking lever by rackfollowscale of the pull), and for a
 * row with pumpaction (the Shotgun's forend) the gun's own left arm and hand with it, which it draws
 * on the forend for the support grip. Does nothing unless the hand's gun is owned and driven. */
void vrReloadMoveRack(s32 handnum, struct modeldef *modeldef, Mtxf *matrices);
/* Called from bgun0f0a5550 once the hand's gun part matrices are built and the slide offset is
 * applied, before anything else moves or hides parts. Records where the clip and the rack frame
 * (the slide part, or the row's rackmtx) are (gCtrlPos frame, cm) for the following gameplay ticks,
 * which run before the next render; the clip alone for a model with no slide part and a row with no
 * rackmtx, and for a row with no clip part its intake point on the gun body instead of the clip.
 * Does nothing unless the hand's gun is owned. */
void vrReloadCaptureParts(s32 handnum, struct modeldef *modeldef, Mtxf *matrices, f32 bgscale);
/* Called from the copy-weapon render while it draws the off-hand holding a clip (holdframe), once
 * its matrices are built. Records where the held clip is relative to the off-hand controller, so
 * the gameplay ticks place it from the current controller pose. Does nothing unless the hand's gun
 * is owned. */
void vrReloadCaptureHeldClip(s32 handnum, struct modeldef *modeldef, Mtxf *matrices);
/* Called from the fists render with the free off-hand's matrices built in the open pose (the rest
 * pose and the open roll, whatever clench is drawn): records where its hand bone (matrix 2; matrix
 * 1 is the elbow) sits relative to the off-hand controller, kept per physical hand across gun side
 * changes, and its palm (the middle of the hand bone and the four knuckles, in the same matrices'
 * frame; NULL: none, the slap tests the grab point). clench is the drawn clench amount, for the log
 * only. */
void vrReloadNoteFistWrist(const Mtxf *wrist, const f32 *palm, f32 clench);
/* Called from the fists render's open pass (the rest pose and the open roll, as for
 * vrReloadNoteFistWrist; no vr_wrist_rot): records the open arm (the forearm bone, matrix 1 of the
 * right hand drawn mirrored as the left) and its offset from the hand bone (matrix 2), in the off
 * controller's frame, kept per physical hand, for vrReloadPlaceHeldArm */
void vrReloadNoteFistArm(const Mtxf *arm, const Mtxf *hand);
/* Called from the copy-weapon render while it draws the off-hand holding a clip or slapping, after
 * vr_wrist_rot: puts the copy model's left arm (armmtx, the forearm bone, 17) where the free fists'
 * open arm sits relative to the off controller (vrReloadNoteFistArm), hung from the drawn left hand
 * bone (wristmtx, 18), in place of the reload animation's elbow turned with the hand and clip turns
 * or placed with the slap. True when placed: the caller then tucks the elbow as the fists' (Task
 * F9c). False, and nothing changed, before that physical hand has shown the fists, or with no clip in
 * the hand and no slap drawn. */
bool vrReloadPlaceHeldArm(s32 handnum, struct modeldef *modeldef, Mtxf *matrices, s32 armmtx, s32 wristmtx);
/* Called from the copy-weapon render while it draws the off-hand holding a clip, after its
 * matrices are built and before vrReloadCaptureHeldClip: turns the whole model about its left hand
 * bone (matrix wristmtx) by the owned row's hand turn (handturnaxis, given in the frame of root, the
 * matrix the model was drawn from, whose x mirror in left-handed mode mirrors the turn with it), so
 * the hand sits on the controller as the Falcon's does, then moves it so that bone lands where the
 * open free hand's hand bone sits relative to the controller. The reload animation's holding frame
 * puts the hand well away from the free hand otherwise. Left as drawn while that physical hand has
 * never shown the fists as the free hand; not turned while handnum's gun is not owned. */
void vrReloadAlignHeldHand(s32 handnum, struct modeldef *modeldef, Mtxf *matrices, const Mtxf *root, s32 wristmtx);
/* Called from the copy-weapon render while it draws the off-hand holding a clip, right after
 * vrReloadCaptureHeldClip: the owned row's draw-only look of the held part (heldlookaxis,
 * heldlookdeg, heldlookpivot, heldlookshift: the Laptop Gun's), turned and moved in the drawn left
 * hand's frame (wristmtx, the left hand bone, 18), mirrored with it. After the capture, so the guide
 * reads the held clip as before it. Does nothing for a row with none or a gun not owned. */
void vrReloadPoseHeldLook(s32 handnum, struct modeldef *modeldef, Mtxf *matrices, s32 wristmtx);
/* Called from bgun0f0a5550 right after vrReloadCaptureParts: moves the gun's clip matrix back
 * along the insert axis by the cm it still has to travel to seated, while it is guided into the
 * well and for a moment after the insert. */
void vrReloadPlaceClip(s32 handnum, struct modeldef *modeldef, Mtxf *matrices);
/* Called from bgun0f0a5550 right after vrReloadPlaceClip. Records the clip matrix as drawn (in
 * world space) for the next eject, and while an ejected clip falls, replaces the clip matrix with
 * the falling pose. The fall lasts 1 s from the eject; a clip guided into the well, an insert, a
 * weapon switch, death or a level change ends it early. */
void vrReloadDropClip(s32 handnum, struct modeldef *modeldef, Mtxf *matrices, f32 bgscale);
/* Called from bgun0f0a5550 right after vrReloadDropClip: narrows the gun's clip matrix to the row's
 * clipwidth while the clip is drawn in the gun (seated or guided in) and not falling. Last, so the
 * capture and the eject record (vrReloadCaptureParts, vrReloadDropClip) read it at full width. */
void vrReloadNarrowClip(s32 handnum, struct modeldef *modeldef, Mtxf *matrices);
/* Called from bgun0f0a5550 right after vrReloadNarrowClip: while no clip is drawn in the gun (out,
 * or held in the off-hand and not guided in) and none falls, collapses the gun's clip matrix to
 * zero scale, so a drawn part holding a face of the clip on that matrix would go with the clip
 * (harmless for part 0x42, the CMP150's clip base: bgun0f098030 hides it on every frame for every
 * gun, so it is never drawn); the same for the clip matrix of the gun's other
 * magazine while it is out (vrReloadParkedDef: the SuperDragon's two caps). Last, so every read of
 * the matrix above sees it whole. Does nothing unless the hand's gun is owned. */
void vrReloadCollapseClip(s32 handnum, struct modeldef *modeldef, Mtxf *matrices);
/* The row of the magazine of this hand's owned gun that the core does not drive (the SuperDragon's
 * other function's: the grenade magazine with the rifle selected, the rifle clip with the grenade
 * launcher), NULL for a gun with one row. *clipin (may be NULL): whether that magazine is in the
 * gun, as it was left (in when it was never driven): the caller shows or hides its clip part by it,
 * and vrReloadCollapseClip collapses its clip matrix while it is out. Task F9e */
struct vrreloaddef *vrReloadParkedDef(s32 handnum, bool *clipin);
/* True while this hand's ejected clip is falling: the gun's clip part must be drawn (for a row with
 * no clip part whose held part falls after its playback, playdrop: the held part) */
bool vrReloadClipFalling(s32 handnum);
/* Whether the selected function may fire: true when not owned, for another function's ammo
 * (vrReloadOwnsAmmo) and for a function with no ammo (the pistol whip, the Dragon's self-destruct,
 * the Sniper Rifle's crouch); else the row's clip and chamber decide */
bool vrReloadAllowFire(s32 handnum);
/* True while the off-hand is racking this hand's owned gun on its slide part: the adapter sets
 * hand->slidetrans from the pull, so bgunUpdateSlide must leave it alone. False for a row that racks
 * on a rackmtx (it moves no slide) */
bool vrReloadSlideHeld(s32 handnum);
/* True while the off-hand grips the rack of this hand's owned gun (VRR_RACKING), on its slide part or
 * on a rackmtx: that free hand is racking, not supporting a two-handed gun */
bool vrReloadHandRacking(s32 handnum);
/* True while this hand's owned gun waits for its rack or slap with rounds in its clip (VRR_NEED_RACK;
 * not a gun fired dry): a reload under way that vrReloadBusy does not count */
bool vrReloadWaitsForRack(s32 handnum);
/* True while this hand's owned gun is mid-reload, not ready: B pressed on a clip that is in, the
 * clip out (taken, held, guided or not yet taken), a rack under way, a rack or slap due with rounds
 * in the clip (vrReloadWaitsForRack), or the free hand drawn slapping. A gun fired dry with its clip
 * in is not mid-reload, nor is a gun pumped after each shot (pumpaction) waiting for or getting its
 * pump, which is the support hand's (vrReloadPumpHand), nor B on a gun loaded shell by shell. The
 * free hand's grip then is the reload's, never a two-handed gun's support (Task F9a). Unlike
 * vrReloadBusy, a gun with its clip out and an empty reserve is mid-reload: it cannot fire, so no
 * aim is lost, while vrReloadBusy leaves it out because it gates the holsters (no clip can be
 * fetched, so the free hand must stay free to draw or switch). One exception: a row whose shot opens
 * it (emptyejects, the Rocket Launcher) is not mid-reload while the support grip held at that shot is
 * still held, so the two-hand aim stays on until it is let go; a new grip press then is the reload's */
bool vrReloadMidReload(s32 handnum);
/* True while the free hand pumps this hand's owned gun, a row with pumpaction (the Shotgun's forend,
 * VRR_RACKING): it is the support hand doing it, so its grip stays the two-handed gun's support and
 * the two-hand aim stays on (the pump slides along the barrel, so the line between the hands keeps
 * its direction). vrReloadMidReload is false for a pump due or under way, vrReloadFreeHandBusy true */
bool vrReloadPumpHand(s32 handnum);
/* True while this hand's owned gun waits for a rack by a grip and a pull (vrReloadWaitsForRack on a
 * row that is not racked by a slap): a grip of the free hand then is the rack's, never the two-handed
 * gun's support, from its press, before the core takes the rack (the RC-P120's knob, Task F7b) */
bool vrReloadNeedsGripRack(s32 handnum);
/* True while the right gun waits for a grip's rack (vrReloadNeedsGripRack) and the free hand's grab
 * point is inside its rack area, as the reload reads it: a press there is the rack's, not a holster
 * zone's */
bool vrReloadFreeHandInRack(void);
/* The free hand (empty, or its gun of a pair parked) is at the reload's shoulder (the free hand's
 * side, VrLeftHandedMode) and a squeeze there would take ammo for the right gun: its clip out, or a
 * gun loaded shell by shell with room in its tube, and rounds in the reserve. A press there is the
 * reload's, not the holster shoulder's (Task F11g: with the gun on the other side from the
 * left-handed setting the two are one zone) */
bool vrReloadFreeHandAtAmmo(void);
/* The free hand is busy with this hand's owned gun: it holds a clip, racks or is drawn slapping. An
 * engine reload of a function whose ammo has no row (vrReloadOwnsAmmo; none today) waits meanwhile,
 * as the gun model's own left hand would play it beside the free hand */
bool vrReloadFreeHandBusy(s32 handnum);
bool vrReloadClipInGun(s32 handnum);   /* true when not owned */
/* The rounds the HUD's magazine gauge and number show for this hand's ammo slot: engine (its
 * loadedammo) but for an owned gun pumped after each shot (pumpaction) with its tube in, whose chamber
 * plus tube it is, so it does not read 0 between a shot and the pump (the engine counts the tube only
 * with a round chambered) */
s32 vrReloadHudLoaded(s32 handnum, s32 ammoindex, s32 engine);
/* Whether to draw the gun's clip: in the gun, or held and guided into the well (true when not
 * owned) */
bool vrReloadClipShownInGun(s32 handnum);
/* Whether the off-hand holds a clip, guided into the well or not: the hand is drawn in its
 * clip-holding pose */
bool vrReloadHandHoldsClip(s32 handnum);
/* Whether to draw the clip in the off-hand: it holds one that is not guided into the well (a
 * guided clip is drawn in the gun) */
bool vrReloadHeldClipDrawn(s32 handnum);
/* A row with rackslap: the free hand is drawn slapping the clip (the reload animation's slap, from a
 * fast swing up from below until it is back on the controller), at reload animation frame *frame
 * (frame may be NULL). The free hand is then not free (no fists) and not the support hand */
bool vrReloadSlapDrawn(s32 handnum, f32 *frame);
/* The copy-weapon model posed at vrReloadSlapDrawn's frame (by the caller, with any root): every
 * matrix moved so the posed clip matrix lies on the gun's drawn clip, then the whole hand taken
 * rigidly from the held hand's place on the off controller (wristmtx: the left wrist, 18) to there
 * from slapfrom to slapcontact (the way in), and back towards that place after the slap frames (the
 * way back). False when the gun's clip was not drawn lately: the caller draws no hand then */
bool vrReloadPlaceSlapHand(s32 handnum, struct modeldef *modeldef, Mtxf *matrices, s32 wristmtx);
/* A row with a clip seat and a slap (the Laptop Gun) places the slap's hand by its animation's clip
 * as posed at slapcontact. True (and the frame) while a slap is drawn and that pose is not yet read
 * for the gun in hand: the caller poses the copy-weapon model at the frame and passes its matrices to
 * vrReloadNoteSlapClip, then poses it at vrReloadSlapDrawn's frame as usual. Read once per gun. */
bool vrReloadSlapNeedsContact(s32 handnum, f32 *frame);
void vrReloadNoteSlapClip(s32 handnum, struct modeldef *modeldef, Mtxf *matrices);
void vrReloadResetAll(void);
/* A haptic pulse on logical controller ctrl (flipped by the gun side, as trigger_haptic_vibration
 * does), scaled by the rumble strength setting: amplitude 0..1, duration in seconds */
void vrReloadBuzz(s32 ctrl, f32 amp, f32 secs);
/* The gun side (VrLeftHandedMode) just changed: ends any grab (a clip held in the off-hand is
 * dropped, an unfinished rack lets go) without touching the gun's clip, chamber or the reserve.
 * A grip still held must be squeezed again to grab. */
void vrReloadSideChanged(void);
/* True while the right hand's owned gun is mid-reload and can finish: an eject is pending (never on
 * a gun loaded shell by shell, which B does nothing to), a clip or a shell is in the free hand, the
 * clip is out and the reserve still has rounds for a new one, a rack (or a pump) is under way or the
 * free hand is drawn slapping. A gun that is only dry (empty chamber and clip), has
 * its clip out with an empty reserve, or only waits for its rack or slap (vrReloadWaitsForRack), is
 * not busy. Only one gun of a pair reloads at a time: the Y flip is refused while this is true or
 * the gun waits for its rack or slap. With a pair it is the holsters' "reloading" input
 * (vrhinput.reloading: the other copy parks, the shoulder is the reload's, and the free hand neither
 * draws nor buzzes); a single gun's is vrReloadHoldsSwitch. */
bool vrReloadBusy(void);
/* True while the right hand's owned gun is in a part of its reload that the free hand is doing and
 * a switch away would cut: a clip or a round in the free hand, a rack (or a pump) under way, the
 * free hand drawn slapping, or an eject the core has not taken yet (as vrReloadBusy counts it). A
 * gun with its clip out (the Rocket Launcher's hatch open) or waiting for its rack or slap is
 * mid-reload but holds nothing: a hip or shoulder draw, a step or a menu pick switches it away, and
 * its state is saved with it (Task H3d). The holsters' "reloading" input for a single gun. */
bool vrReloadHoldsSwitch(void);
/* A gun side flip of a pair: the two guns trade engine hands. Returns false and changes nothing
 * unless both hands are in use with the same weapon (the engine can hold the left draw back while
 * the holsters already count a pair); the caller then skips its flip. Otherwise swaps the hands'
 * loadedammo, slidetrans and slideinc and every per-hand state of the reload (each gun's core
 * state, restored shape, ammo slot, trigger gate, slide grab, pulses, clip finish), so each gun
 * keeps its own state in its new hand. Ends every grab and gesture (eject request, guided clip,
 * rack grab, falling clip) and drops the render captures, which the next frames take again (the
 * free hand's wrist is kept per physical hand). Saved shapes are not touched: a pair in hand has
 * none for its weapon. The caller changes the gun side after this (vrHolsterSetMode, which calls
 * vrReloadSideChanged). */
bool vrReloadSwapHands(void);
/* True when vrReloadSwapHands would swap: both engine hands in use with the same weapon */
bool vrReloadCanSwapHands(void);

/* A point in the body frame (cm: x right, y up, z back, from the head; turned by the torso yaw, so
 * a glance leaves it put) in the gCtrlPos frame, to compare with a controller origin. x is mirrored
 * by the left-handed setting (VrLeftHandedPref), not by the gun side. */
void vrBodyPoint(const f32 *bodycm, f32 *out);
/* vrBodyPoint with x mirrored when mirror is set, whatever the setting (the arms' shoulders: their
 * physical side) */
void vrBodyPointMirror(const f32 *bodycm, s32 mirror, f32 *out);
/* The reverse, unmirrored: a gCtrlPos-frame point in the body frame, cm */
void vrBodyLocal(const f32 *view, f32 *outcm);

#endif
