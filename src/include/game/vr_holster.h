#ifndef VR_HOLSTER_H
#define VR_HOLSTER_H
#include <ultra64.h>
#include "types.h"

/* Engine adapter for the VR body holsters: feeds the pure holster core (vr_holster_core.c) the
 * grips and body zones of both physical hands once per gameplay tick and applies its output: the
 * engine weapon, dual wield, the gun side (VrLeftHandedMode) and haptics. Everything here is off
 * unless VrManualReloading is on, and only drives the VR player (player 0). The rules are in
 * docs/plans/2026-10-15-vr-holsters-phase2-plan.md, Tasks A4 and A6 (the guns), C1 and C2 (the
 * gadgets and throwables) and J4 (three gun holsters and the layout). */

/* True when a weapon switch should be instant: holsters are on and this is the VR player (player
 * 0). bgunTickIncChangeGun then skips the unequip and equip animations and the lower and raise, the
 * old gun is freed in the tick the hands reach the load, and bgunTickSwitch2 loads the new model and
 * raises it in the tick it commits: the frame drawn after the tick that started the switch shows
 * the new weapon, unless a hand is busy (firing, a reload, a throw) or the gun memory is elsewhere,
 * when the switch finishes in later ticks. */
bool vrHolsterFastSwitch(void);

/* Holsters are on and the current player is the VR player */
bool vrHolsterActive(void);

/* A weapon the gun holsters hold: a gun from the Falcon 2 to the Psychosis Gun, not a throwable
 * (WEAPONFLAG_THROWABLE: knife, grenades, mines) and not the combat boost */
bool vrHolsterIsGun(s32 weaponnum);

/* The slot of a weapon's kind (core slots, vr_holster_core.h): 0 (VRH_GUN1) for a gun, which goes in
 * any of the three gun holsters; 3 (VRH_POUCH, gadgets) for the held gadgets (Data Uplink, Door Decoder,
 * explosives, Skedar bomb, suitcase, autosurgeon, both disguises, combat boost, horizon scanner,
 * the ECM mine, though it is thrown, and the devices: vrHolsterIsDevice); 4 (VRH_THROW, the
 * throwables) for the other WEAPONFLAG_THROWABLE weapons and the three bugs (Comms Rider, Tracer Bug,
 * Target Amplifier); -1 for everything else (fists, keys and other items) */
s32 vrHolsterSlotOf(s32 weaponnum);

/* The devices the original only toggles from the inventory (DEVICESTATE, currentPlayerSetDeviceActive):
 * Night Vision, X-Ray Scanner, IR Scanner, R-Tracker, Cloaking Device and CamSpy. With holsters they
 * are chest-pouch items: drawn, the hand holds the device's own model (no hands), and the trigger
 * toggles it (vrHolsterDeviceTrigger); bgunTickSwitch2 does not turn the draw into fists. The pause
 * Inventory still toggles them as before. */
bool vrHolsterIsDevice(s32 weaponnum);

/* Holsters are on, the stage is G5 Building and weaponnum is the remote mine: the level's Detonator
 * (a remote mine with no rounds), a chest pouch gadget whose trigger detonates, drawn as the
 * detonator alone at the hand */
bool vrHolsterIsG5Detonator(s32 weaponnum);

/* A device (vrHolsterIsDevice) or a disguise: held from the gadget slot it is its own model at the
 * hand (the disguises the briefcase) and the trigger is its action: a device on or off, a disguise
 * put on (bgunWearDisguise) */
bool vrHolsterIsTriggerItem(s32 weaponnum);

/* Holsters are on and the engine's HAND_RIGHT holds a device or a disguise (vrHolsterIsTriggerItem),
 * or G5 Building's Detonator: the trigger is its switch, not a shot (bmoveProcessInput) */
bool vrHolsterHoldsDevice(void);

/* A trigger press while vrHolsterHoldsDevice: the held device on or off, as the pause Inventory's
 * pick does it, with a HUD line ("<name>: on" or "off") and a buzz on the hand holding it. The
 * CamSpy turned on launches (or takes control of one already out); B returns to Joanna. True when
 * the hand holds G5 Building's Detonator: the caller detonates the remote mines, as a B tap does. */
bool vrHolsterDeviceTrigger(void);

/* The hold-A/X weapon menu with holsters on (amAssignWeaponSlots): true when it shows weaponnum.
 * Opened by vrHolsterFaceButtons in the gadget slot's zone (the chest by default) it shows the held
 * gadgets and devices, in the throwables' zone the throwables; outside the five zones by a hand
 * holding a gadget or throwable, that item's kind; anywhere else (and opened by the vanilla code
 * path) the guns; never fists, nor a mine at 0 rounds. A device picked there goes to the gadget
 * slot, not on (amApply). False with holsters off. */
bool vrHolsterMenuOffers(s32 weaponnum);

/* From bgunTickInc for the right hand, before vrReloadTick: the gun side it sets is the one the
 * reload reads. bgunTickInc can run several times per frame; this is safe to call repeatedly (the
 * core acts on edges, and the weapon is equipped only when the core's output changes). Does
 * nothing with holsters off (beyond restoring the gun side to the setting once when they are
 * turned off). */
void vrHolsterTick(void);

/* Level start (bgunReset) and a new life (playerStartNewLife): empty hands, the gun side back to
 * the left-handed setting. The holsters are filled on the first gameplay tick, once the stage has
 * given its starting weapons: the starting weapon in gun 1, the next two guns in the inventory in
 * gun 2 and gun 3, the first gadget and throwable in their slots, with the layout of the setting
 * (VrHolsterLayout). The player starts with fists. */
void vrHolsterReset(void);

/* lvSetPaused(true): the next tick (or menu pick) sees a pause, so the guns and items in the hands
 * go home even when no gameplay tick ran while the game was paused */
void vrHolsterNotePause(void);

/* A gun was picked up (already in the inventory): it fills the first empty gun holster (gun 1, 2,
 * then 3) */
void vrHolsterOnPickup(s32 weaponnum);

/* A weapon chosen in the pause-menu inventory (core rules 23 and 46; the weapon menu uses
 * vrHolsterOnMenuPickHand). True when the holsters took it (holsters on, and a gun, gadget or
 * throwable): the caller skips its own equip. Fists, keys and other items: false (the vanilla
 * equip runs), and the next tick equips the core's weapon again. The pause first holsters what the
 * hands hold (rule 11). A gun in hand is replaced (in its holster too); with empty hands gun 1 gets
 * the gun and the hands stay empty. A gadget or throwable goes to its slot and not into a hand: the
 * next grip there takes it, and the HUD names the body spot of the slot ("<name>: chest", "<name>:
 * right hip"; "<name>: none left" for a mine at 0 rounds, which is not taken). */
bool vrHolsterOnMenuPick(s32 weaponnum);

/* The lower face buttons with holsters (Task B7; A on the right controller, X on the left), once per
 * player tick from bmoveProcessInput: each acts for its own physical hand. A tap (let go before
 * TICKS(15)) in one of the five zones steps that slot to the next gun or item of its kind (core
 * rules 34 and 45: a firm buzz and its name on the HUD, or two weak pulses when refused); a tap
 * elsewhere returns true: the caller does the use action (doors, lifts, consoles). A hold opens
 * the weapon menu of that zone's kind (vrHolsterMenuOffers), or outside the zones of the kind of
 * the item that hand holds (a pick then replaces it, as in its slot's zone), steered by that hand
 * (vrHolsterMenuHand); refused with two weak pulses when the inventory has nothing of that kind.
 * allowed false (paused, dead, a cutscene, the first frames, the menu open, the eyespy) and
 * detonating (A + B on a remote mine, or a B tap: vrHolsterBTapDetonates) handle nothing: a
 * button held then waits for its release. False and nothing done with holsters off. */
bool vrHolsterFaceButtons(bool allowed, bool detonating);

/* The physical hand (0 left, 1 right) that opened the weapon menu with its lower button, while
 * that menu is open and holsters are on; else -1 */
s32 vrHolsterMenuHand(void);

/* The weapon menu closed (amClose): the hand and zone that opened it are cleared, so a menu opened
 * by another path never inherits them */
void vrHolsterMenuClosed(void);

/* While vrHolsterMenuHand is a hand: 1 when its lower button is held, else 0; the menu stays open
 * while it is held (input.c adds it to CONT_A, so the gun hand's A, bound to CONT_A as well, also
 * keeps it open). -1 when there is no such hand: the gun hand's A keeps the menu open as before. */
s32 vrHolsterMenuButton(void);

/* A weapon chosen in the weapon menu (amApply). With a menu opened by vrHolsterFaceButtons, a gun,
 * gadget or throwable goes where the hand that opened it was (core rules 35 and 45, its zone when
 * the menu opened, or the slot of the item it held outside the zones): true, and the caller skips
 * its own equip; a refused pick gives that hand two weak pulses. Otherwise it is
 * vrHolsterOnMenuPick. */
bool vrHolsterOnMenuPickHand(s32 weaponnum);

/* The VR grab with holsters on, once per player tick: 1 when a grip press asks for the grab (by
 * an empty, tracked hand outside the five zones, and no reload under way), -1 when the
 * hand that asked lets go, else 0. Always 0 with holsters off. */
s32 vrHolsterGrabEdge(void);

/* After a press from vrHolsterGrabEdge: grabbed false (nothing was grabbed) frees the grab for
 * either hand's next press */
void vrHolsterGrabResult(bool grabbed);

/* The gCtrlPos index of the hand whose grip press grabbed (vrHolsterGrabEdge), while its grip is
 * held; -1 with holsters off or no grab asked */
s32 vrHolsterGrabPose(void);

/* The grab hint, once per level (vrHolsterReset): true every few ticks with holsters on while it
 * has not been shown, when the caller should look for a grabbable object in grab reach
 * (propFindForGripGrab). vrHolsterCrateHint shows it, when one is found or the A or X use finds
 * one, unless it was shown this level. */
bool vrHolsterCrateHintDue(void);
void vrHolsterCrateHint(void);

/* A HUD line in the holsters' message slot: it replaces the holsters' last message (and the last
 * line sent here) at once, queued or showing, instead of waiting for it to time out. For the reload
 * hints (vr_reload.c, Task F9d). duration60: how long it shows, in 60ths; 0: the default message's
 * 80. colournum: its text and box colour, an index into g_HudmsgColours (the hints' COLOR_09_BLUE,
 * Task F11b); -1: the default message's green */
void vrHolsterHudLine(char *text, s32 duration60, s32 colournum);

/* An engine-initiated switch (out-of-ammo auto switch, the return after a throw or a deployed
 * laptop gun, a used-up throwable). True when the holsters handled it (holsters on): the core's
 * weapon is equipped (fists when the hands are empty), or kept when the engine already has it,
 * so an empty gun stays in the hand. */
/* Each frame a full menu (a pause, a training's end, an endscreen; not the hold menu) is drawn
 * (menuRender): the gun side goes back to the left-handed setting, so A is the setting's gun hand's
 * and the menu panel sits on its off hand, whichever hand last drew from a holster; the hands empty
 * as for a pause when play resumes. A training's end opened its dialog with the gun side still on the
 * hand that last drew, which left A on the left controller's X and the menu on the right hand. */
void vrHolsterMenuSide(void);

bool vrHolsterEngineSwitch(void);

/* A B tap with holsters on (bmoveProcessInput): true when it detonates the player's remote mines
 * instead of reloading (Stage C decision): the engine's HAND_RIGHT holds the remote mine, or fists
 * while remote mines thrown by the player are placed (in flight or stuck, not gone off). The
 * detonation (playerActivateRemoteMineDetonator) needs only those mines, not the mine in the hand,
 * its detonator or a round in the inventory. Logs a PD-VRR "button:" line when true. False with
 * holsters off. */
bool vrHolsterBTapDetonates(void);

/* True when a grip must not send CONT_R (aim): holsters are on and the engine holds or is switching
 * to a gun or a gadget or throwable, so the grip means "hold it" (aim comes back in Stage
 * D) */
bool vrHolsterGripAimBlocked(void);

/* True when engine hand h's gun is parked (core rule 9: hidden while the other gun reloads): with
 * holsters on and both hands holding a pair, the physical hand holding it (the gun side for
 * HAND_RIGHT, the other hand for HAND_LEFT) is parked. A gun parked behind an item (rule 39) is not
 * in the engine, so it never counts: the item and fists are never hidden or stopped. False with
 * holsters off. */
bool vrHandParked(s32 h);

/* A reload button press (B: HAND_RIGHT, Y: HAND_LEFT; each the top button of the controller
 * holding that gun). False when manual reload does not own engine hand h's gun, or (HAND_RIGHT)
 * not the selected function's ammo (vrReloadOwnsSelectedFunc: a function's ammo with no row, none
 * today since the SuperDragon's grenades got one, Task F9e) while the
 * free hand is not busy with the row's clip or rack: the caller does the vanilla reload. Otherwise
 * true, and the vanilla reload must not run (with such a function selected and the free hand busy,
 * the press is refused with two weak pulses on the gun hand's controller). HAND_RIGHT: the eject
 * is requested. HAND_LEFT (the left gun of a pair): at the next tick the gun side moves to its
 * hand and the two engine hands trade their guns' state, so it reloads as the right gun and that
 * controller's buttons read as the gun hand's; or it is refused with two weak pulses on its
 * controller (one reload at a time, a weapon change, mid-shot, a hand parked). */
bool vrReloadEjectButton(s32 h);

/* PD-VRR diagnostic, one line per switch the adapter asks for: the weapon, then the frames from the
 * request to the commit, to the end of the model load and to the first frame drawing the new weapon
 * (with the 60ths and the synchronous load's time). vrHolsterNoteLoad: the time of a synchronous
 * load (bgunTickSwitch2). vrHolsterNoteFrame: once per frame from bgunRender. */
void vrHolsterNoteLoad(s32 micros);
void vrHolsterNoteFrame(void);

/* True for half a second after the adapter asks for a switch and after the gun hand's raise
 * (vrHolsterNoteRaised): no VR fist punch starts then (bgunTickGameplay), and one that is still fast
 * when it ends punches only after the hand slows and swings again, as the hand's motion is the
 * release. (With holsters guns never pistol whip.) False with holsters off. */
bool vrHolsterSwingSettling(void);

/* From bgunTickIncChangeGun when a hand's raise ends (gameplay tick, not render): the gun hand's
 * restarts the swing hold-back */
void vrHolsterNoteRaised(s32 handnum);

/* PD-VRR timing after a switch: the frame the raise asks for the draw sound, the first attack the
 * gun hand starts from the trigger (bgunTickIncIdle), and a VR swing that starts (started true) or is
 * held back by vrHolsterSwingSettling (false) */
void vrHolsterNoteDrawSound(s32 handnum);
void vrHolsterNoteShot(s32 handnum);
void vrHolsterNoteSwing(s32 handnum, bool started);

/* Logs one line of text under PD-VRR as it is (the punch log) */
void vrHolsterDiagLine(const char *text);

/* Logs one line of text under PD-VRR with the switch line's prefix (bgunRender's per-hand dump) */
void vrHolsterDiagText(const char *text);

/* The mark tool (vrReloadTick): logs the zones' geometry (the hips' and the chest's segments, the
 * shoulders' points, the radii) and the layout, then both hands' controller origins in the body
 * frame, their zones and their distances to each zone (hip, shoulder, chest, right hip, left
 * shoulder), under PD-VRR */
void vrHolsterLogMark(void);

/* The Holster Layout page (mainmenu.c, Task J4): the body spots in physical order, 0 left
 * shoulder, 1 right shoulder, 2 chest, 3 left hip, 4 right hip, each holding a slot (0 to 2 guns 1
 * to 3, 3 gadgets, 4 throwables: vr_holster_core.h). The setting (VrHolsterLayout, pd-vr.ini
 * HolsterLayout) is per zone relative to the gun hand, so the spots map through VrLeftHandedPref
 * and the left-handed setting mirrors the layout as it mirrors the zones. */
#define VRH_NUM_SPOTS 5

/* The slot at spot */
s32 vrHolsterLayoutGet(s32 spot);

/* spot gets slot, and the spot that had slot gets spot's old one (a swap); the live holsters take
 * the new layout at once (core rule 50) */
void vrHolsterLayoutSet(s32 spot, s32 slot);

/* The game's layout (core rule 51), applied at once */
void vrHolsterLayoutReset(void);

#endif
