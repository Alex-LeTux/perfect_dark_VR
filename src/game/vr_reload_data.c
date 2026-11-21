/* Per-gun manual reload data: see vr_reload_data.h. */
#include <ultra64.h>
#include "constants.h"
#include "game/vr_reload_data.h"

/* Falcon 2 model (Gfalcon2Z, all three variants), read from the ROM's model data:
 * - The clip (MODELPART_FALCON2_MAGAZINE1) is a toggle under the root node; its 40 vertices are
 *   all skinned to matrix 38, a position node under the gun body (matrix 33). Matrix 38 is built
 *   every frame whether the toggle is on or off, so it is valid while the clip is out.
 * - In matrix 38's local frame the clip's vertices run along {0, 0.924, 0.381} (principal axis;
 *   the grip's 22 degree rake), from 13.3 cm below the joint up to the joint, so the joint is the
 *   top of the clip and that axis points from the clip's base into the gun, toward the slide.
 * - Seated, the clip's base is flush with the bottom of the grip (gun body vertices end 13.3-13.7
 *   cm along the same axis), so the clip travels 13.3 cm from the mouth of the well to seated. It
 *   stays inside the grip at every depth, so it is drawn at full width (clipwidth 1).
 * - MODELPART_GUN_SLIDE is position node matrix 37, the rack's frame (rackmtx -1). Its origin is at
 *   the rear end of the slide, 2.2 cm to the side of it: the slide's vertices span x -3.6..-0.8,
 *   y -2.5..2.2, z -0.6..20.3 cm in its local frame. The rack area is that box grown by 4 cm: nine
 *   grab points marked in the headset (test 2, the drawn hand where a grab should rack, see the
 *   mark in vr_reload.c) lie all round the slide, up to 3.4 cm outside it, e.g. {1.95, -4.57, 0.26}
 *   at the rear and below, {-1.16, 5.00, 16.84} above the front. The first, {1.95, -4.57, 0.26},
 *   stays as rackoffset for the logs. The box's top is raised from the slide's 2.2 to 5.0 cm (user,
 *   2026-10-17: a grab mark at {-5.95, 7.02, 4.58}, above and beside the slide, was 5.4 cm from the
 *   box, past rackrad; now 3.1), so a hand coming down onto the slide from directly above racks.
 *   The slide's travel is the Falcon's slidemax, 60 model units: 6 cm, so a 6 cm pull racks with
 *   the slide under the hand.
 * - The clip in the off-hand (MODELPART_FALCON2_MAGAZINE2) is a second toggle whose 40 vertices are
 *   all skinned to matrix 42, a position node under the left wrist (matrix 18). Its vertices are the
 *   gun clip's own vertices turned into a different local frame: a rigid fit of the 38 set onto the
 *   42 set (same vertex order) leaves at most 0.3 mm, so the 38 frame maps into the 42 frame by one
 *   rotation and offset. Matrix 42 is not oriented like 38, so the gun's clipaxis would point the
 *   held clip the wrong way. The fit puts 38's origin at {9.04, 8.79, -4.33} cm in 42's frame and
 *   maps the insert axis to {0.6574, 0.6694, -0.3461}, but the headset (test 2) shows the clip as
 *   drawn in the hand the other way round: held lined up under the grip, the right way up, that
 *   axis pointed 175 degrees off the well, and a guide measured from that point only started with
 *   the clip about 8 cm into the grip. The held clip's vertices run 13.24 cm from that point along
 *   the reversed axis, so as drawn its top is the far end, {0.34, -0.08, 0.26} (next to 42's joint,
 *   as 38's joint is at the gun clip's top), and base to top is {-0.6574, -0.6694, 0.3461}.
 * - No hand turn (handturnaxis and handturndeg zero): the frame-18 left wrist, as the headset shows
 *   it lined up with the off controller, is the pose the other rows' held hands are turned to
 *   (tools/vr_measure/handturn.py), so each sits on the controller as this one does. */
#define FALCON2_ROW(weapon) { \
	(weapon), 0, \
	6.0f, 35.0f, 13.3f, { 0.0f, 0.9245f, 0.3812f }, 1.0f, 0.0f, \
	0, { { 0.0f } }, \
	-1, 4.0f, { -3.6f, -2.5f, -0.6f }, { -0.8f, 5.0f, 20.3f }, { 1.95f, -4.57f, 0.26f }, 6.0f, 0, 0, 0, 0, \
	MODELPART_FALCON2_MAGAZINE1, 38, -1, { 0.0f, 0.0f, 0.0f }, -1, 18.0f, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, \
	MODELPART_FALCON2_MAGAZINE2, 42, -1, { 0.34f, -0.08f, 0.26f }, { -0.6574f, -0.6694f, 0.3461f }, \
	472, 473, 475, 0, 0.0f, \
	0.0f, 0.0f, 0.0f, -1, -1, \
	0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 0.0f, \
	0, -1, 0.0f, \
	0.0f, 0.0f, -1, 0, { 0.0f, 0.0f, 0.0f }, { { 0.0f } }, 0.0f, 0.0f, -1, -1, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, 0, \
	0, 0, 0, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, \
	"insert magazine, rack the slide", \
}

/* MagSec 4 model (Gleegun1Z), read from the ntsc-final ROM's model data and reload animation
 * (ANIM_GUN_MAGSEC4_RELOAD, the single-wield reload, which vrGetReloadAnimIdForWeapon picks for a
 * pair too) with tools/vr_measure:
 * - The clip (MODELPART_PISTOL_MAGAZINE1) is a toggle under the root node, hidden by the gun's
 *   gunviscmds as the Falcon's; its 12 vertices are all skinned to matrix 38, a position node under
 *   the gun body (matrix 33), with no part id of its own.
 * - In matrix 38's local frame the clip runs along {0, 0.9763, 0.2164} (principal axis; the grip's
 *   12.5 degree rake), from 16.7 cm below the joint up to the joint, so the joint is the top of the
 *   clip and that axis points from the clip's base into the gun, as the Falcon's.
 * - The clip is 16.7 cm long and its base stays out of the grip when seated, as the gun draws it:
 *   the grip's bottom edge is slanted, the gun body ending 15.5 cm along the axis at its back and
 *   14.3 cm at its front, the clip's base 16.7 and 16.0 cm. The clip travels 15.5 cm from the mouth
 *   of the well to seated, measured at the back as the Falcon's 13.3 is (its clip's base is flush
 *   there); 15.0 on the axis line, a difference a 6 cm guide does not notice.
 * - The clip is a flat box 1.6 cm wide (x -0.8..0.8 in matrix 38's frame, which is the body's
 *   frame moved, not turned), but the grip's back narrows to a wedge 0.7 cm wide (at y -2.5 in the
 *   body frame), so at full width both back corners poke out of the grip by up to 0.44 cm seated
 *   and 0.45 cm pushed in until 10 cm from seated (headset, checkpoint E run 1). Drawn in the gun at
 *   0.45 of its width (x -0.36..0.36), no point of it is more than 0.1 mm outside the grip at any
 *   depth (clipwidth 0.45; 0.5 leaves 0.5 mm).
 * - The guide's reach (clipreach): the user marked two spots where the guide should start but had
 *   not (checkpoint E run 2, right-handed, holding a clip, the slide locked back). The mark line
 *   then gave only the drawn off-hand's grab point, {4.81, -25.10, -8.74} and {2.66, -12.67, -3.67}
 *   in matrix 38's frame. The held clip's top is fixed to that point: from the copy model at frame
 *   18 with its -1.30 rad grip roll and its wrist on the capture {2.35, 2.98, -8.39}, it is 12.5 cm
 *   from the grab point along the held clip's axis and 3.7 cm off it, in the off controller's
 *   frame. Taking the clip lined up with the well and the off controller turned as little as
 *   possible from the gun (the hands parallel), the tops were {7.9, -13.4, -4.0}, beside the grip
 *   1.6 cm past the mouth and 8.1 cm from it, and {5.7, -0.9, 1.1}, beside the gun at the seated
 *   top. A turn about the clip's axis moves them 1.9 cm at 30 degrees and up to 7.4 cm at any
 *   angle; a turn of the off controller 45 degrees inward would have put the first 5.7 cm from
 *   the mouth, where the guide starts, and it had not. A tilt off the well axis within clipalignmax
 *   moves them up to 7.8 cm. Only the first is used: the user took the second mark for a flaw in
 *   their reloading (user, 2026-10-17), and a guide starts only with the top at most
 *   VRR_GUIDE_START_PAST_CM (2) past the mouth (vrReloadGuide), which the second lies 14.8 cm
 *   beyond; the first, 1.6 cm past, is inside that margin. The mark line now logs the top (and its
 *   distance to the reach), so later marks can replace these.
 * - MODELPART_GUN_SLIDE is position node matrix 36, not rotated from the body (matrix 33) in the
 *   rest pose or the reload, so +y is up as in the Falcon's 37. Its origin is the rear end of the
 *   slide; the slide is a shroud over the whole top of the gun, its 344 vertices spanning x
 *   -5.7..3.2, y -6.0..2.7, z -0.6..34.5 cm in its local frame (x -2.5..2.5 but for a small tab on
 *   the -x side at z 16..20 reaching x -5.7, and its rear 4 cm, out to x -3.2 and 3.2). The
 *   rack area is that box grown by 4 cm, the Falcon's tuned radius, with its top raised 2.8 cm (2.7
 *   to 5.5) as the Falcon's was, so a hand coming down onto the slide from above racks. rackoffset
 *   is the middle of the slide's rearmost edge (its only vertices at z -0.6 are {-1.4, 0.6, -0.6}
 *   and {1.4, 0.6, -0.6}), for the logs. The slide's travel is the MagSec's
 *   slidemax, 60 model units: 6 cm (the reload animation pulls it 4.8).
 * - The clip in the off-hand (MODELPART_PISTOL_MAGAZINE2) is a second toggle, also hidden by the
 *   gunviscmds; its 12 vertices are all skinned to matrix 39, a position node under the left wrist
 *   (matrix 18), and are the gun clip's vertices in the same local frame (a rigid fit of the 38 set
 *   onto the 39 set leaves 0.75 mm: no turn, 0.5 mm offset), unlike the Falcon's 42. The top is the
 *   end next to the joint, {-0.05, 0.05, 0.11}, and base to top is {-0.0015, 0.9763, 0.2164}: the
 *   Falcon's rule from its test 2. The reload animation agrees: the hand clip goes in top first,
 *   within 4 degrees of the well axis at frames 22 and 23. Its pose on the wrist is the Falcon's:
 *   in matrix 18's frame the top is {-11.27, -8.00, 13.43} cm and the axis {-0.836, -0.309, 0.453},
 *   the Falcon's row giving {-11.20, -7.91, 13.79} and {-0.836, -0.287, 0.467} (0.4 cm and 1.4
 *   degrees apart), so the held clip sits in the hand as the Falcon's tested one.
 * - The hand clip is fixed to the wrist in every frame of the reload, so the hold frame only poses
 *   the fingers: frame 18, the Falcon's, inside the frames the animation shows the hand clip
 *   (10 to 21), with the wrist turned from the model root as at the Falcon's frame 18 (within 4
 *   degrees).
 * - No hand turn: handturn.py measures 3.7 degrees from the Falcon's frame-18 wrist (it would move
 *   the held clip's top 0.9 cm), and the user finds the hand lined up with the controller (handgun
 *   test run 1).
 * - Sounds as the ROM's MagSec reload script plays them (bgun0f0981e8): eject SFX_01D8 (472),
 *   insert SFX_80F6 (33014), rack SFX_01DB (475). 33014 is a config id: sndStart maps it through
 *   g_AudioRussMappings[0xf6] to 0x81d9, whose sound id is 473, so it plays the same sample as the
 *   Falcon row's 473 (the pre-1.0 id); its audio config 61 only sets a falloff, which sndStart does
 *   not use. The script plays all three at 1.3x speed; the row plays them at normal speed.
 * - invitems.c: 9 rounds a clip, WEAPONFLAG_DUALWIELD. */
#define MAGSEC4_ROW(weapon) { \
	(weapon), 0, \
	6.0f, 35.0f, 15.5f, { 0.0f, 0.9763f, 0.2164f }, 0.45f, 0.0f, \
	1, { { 7.9f, -13.4f, -4.0f } }, \
	-1, 4.0f, { -5.7f, -6.0f, -0.6f }, { 3.2f, 5.5f, 34.5f }, { 0.0f, 0.6f, -0.6f }, 6.0f, 0, 0, 0, 0, \
	MODELPART_PISTOL_MAGAZINE1, 38, -1, { 0.0f, 0.0f, 0.0f }, -1, 18.0f, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, \
	MODELPART_PISTOL_MAGAZINE2, 39, -1, { -0.05f, 0.05f, 0.11f }, { -0.0015f, 0.9763f, 0.2164f }, \
	472, 33014, 475, 0, 0.0f, \
	0.0f, 0.0f, 0.0f, -1, -1, \
	0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 0.0f, \
	0, -1, 0.0f, \
	0.0f, 0.0f, -1, 0, { 0.0f, 0.0f, 0.0f }, { { 0.0f } }, 0.0f, 0.0f, -1, -1, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, 0, \
	0, 0, 0, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, \
	"insert magazine, rack the slide", \
}

/* Mauler model (GskpistolZ), read from the ntsc-final ROM's model data and reload animation
 * (ANIM_GUN_MAULER_RELOAD 0x3f4, the single-wield reload, which vrGetReloadAnimIdForWeapon picks
 * for a pair too) with tools/vr_measure:
 * - The clip (MODELPART_MAULER_MAGAZINE1) is a toggle under the root node; its 50 vertices are all
 *   skinned to matrix 37, a position node under the gun body (matrix 33), not rotated from it in
 *   the rest pose or any reload frame, with no part id of its own. Unlike the Falcon's and the
 *   MagSec's it is not hidden by the gunviscmds: the gun shows its clip at idle.
 * - The clip is not in a grip: it is a flat plate in the gun's plane, in front of the grip, at
 *   most 5.4 cm thick across (x), 28 cm long from its top (a 20 cm edge along the body's sloped
 *   underside, z -10.8..9.6 in matrix 37's frame, the joint at its middle) down to a point.
 *   Seated, its top is flush with the body's open underside (no point of it more than 0.3 mm
 *   inside the body), so the whole clip shows.
 * - The insert axis is the clip's +y, {0, 1, 0}: the seated pose's up. The animation brings the
 *   clip in rolled 26 to 28 degrees to the side (about the gun's length) and slides it along its
 *   own +y (its net travel from frame 16 to 19 within 3.5 degrees of its own +y (single frames 12
 *   to 22 off), no forward part) until the joint is at its seated spot (frame 19), then rocks it
 *   upright about the joint (frames 21 to 25). The principal axis of the vertices,
 *   {0, 0.976, 0.218}, is the blade's diagonal and leans 12.6 degrees forward of that travel.
 * - The insert depth is 5.0 cm, a length of travel rather than a well's depth (nothing of the
 *   clip goes into the gun but its back corner, up to 1.4 cm while it tips in and rocks): from the
 *   hold frame (16) to the last tilted frame (21) the joint rises 4.95 cm along the axis to seated,
 *   the clip tilted 26 to 28 degrees, within clipalignmax. 5 cm also leaves at least 2 cm of
 *   the insert drawn after the latest guide start (VRR_GUIDE_START_PAST_CM plus the pull-in). The
 *   guided clip is drawn upright and goes straight up from 5 cm below the gun to seated: no roll
 *   and no rock, as if the animation's clip were turned upright before it came in.
 * - Full width (clipwidth 1): with nothing of it inside the body, the clip at full width passes
 *   through no body surface seated or at any depth of the insert (sampled every 0.5 to 1 cm to
 *   5 cm), and it moves straight up, so it never meets the grip behind it.
 * - No reach points: nothing marked in the headset yet.
 * - MODELPART_GUN_SLIDE is position node matrix 34, not rotated from the body in the rest pose or
 *   the reload, so +y is up as in the Falcon's 37. Its 51 distinct vertices span x -2.4..2.4,
 *   y -4.6..1.1, z -12.1..16.7 cm in its local frame. The rack area is that box grown by 4 cm, the
 *   Falcon's tuned radius, with its top raised 2.8 cm (1.1 to 3.9) as the Falcon's and the
 *   MagSec's were. rackoffset is the middle of the slide's rearmost edge (its vertices at z -12.1
 *   are {-0.4, -2.8}, {0, -2.8}, {0.4, -2.8} and {0, -3.9}), for the logs. The slide's travel is the
 *   Mauler's slidemax, 60 model units: 6 cm (the reload animation pulls it 5.5 back and lifts it
 *   2.8; the engine's slide, and so the rack, moves it straight back).
 * - The clip in the off-hand (MODELPART_MAULER_MAGAZINE2) is a second toggle, hidden by the
 *   gunviscmds; its 50 vertices are all skinned to matrix 38, a position node under the left wrist
 *   (matrix 18), and are the gun clip's vertices in the same local frame (a rigid fit of the 37 set
 *   onto the 38 set leaves 0.9 mm: a 0.1 degree turn, 0.5 mm offset), as the MagSec's. heldtop is
 *   where the gun clip's joint lands in that frame, {-0.04, 0.00, 0.03}: the guide measures the
 *   held top against the mouth, which is the gun clip's joint moved back along the axis, and the
 *   end of the principal axis ({-0.04, 1.13, 0.91}) is 1.4 cm from the joint, which would seat the
 *   clip 1.1 cm early. heldaxis is the gun's +y in that frame, {-0.001, 1.0, 0.0}.
 * - The hand clip is fixed to the wrist in every frame (as the Falcon's and the MagSec's), and the
 *   left fingers do not move in frames 0 to 22: the hand holds the clip open, its finger joints
 *   bent 0 to 30 degrees, not curled round it as the Falcon's (up to 100). So the hold frame only
 *   sets the wrist's turn from the model root, which is how the clip sits on the off controller:
 *   frame 16, the start of the last approach. No frame of the hold matches the Falcon's frame 18:
 *   the closest wrist turn is 17 degrees off (frame 13), frame 16's is 21 (22 from the MagSec's).
 *   On the controller the held clip's joint sits 7.4 cm from the Falcon clip's top and its axis
 *   28 degrees off the Falcon's: the clip is held differently, not only drawn differently.
 * - No hand turn, though handturn.py measures 21.2 degrees from the Falcon's frame-18 wrist (about
 *   {0.877, -0.282, -0.389}): the user finds the hand lined up with the controller and the Mauler
 *   good to load (handgun test run 1), and the turn would move the held clip's top 3.4 cm and its
 *   insert direction 7 degrees on the controller, from 5 to 10 degrees off the insert axis with the
 *   two hands held parallel.
 * - Sounds as the ROM's Mauler reload script plays them (read from the ROM's data, as invitems.c
 *   has it): eject SFX_0053 (83) at frame 4, insert SFX_05C5 (1477) at frame 27 just after the clip
 *   shows seated, rack SFX_01DB (475) at 61. The script also plays SFX_DOOR_8012 at frame 20, as the
 *   clip's top meets the gun (config 0x12: sound 200), which the row has no slot for. It plays them
 *   at 1.2x, 2.0x and 1.0x speed; the row plays them at normal speed.
 * - The charged secondary takes up to 5 loaded rounds without firing (bgunTickMaulerCharge, only
 *   with 2 or more loaded), straight from loadedammo: vrReloadTick's vrReloadSync folds that
 *   into the clip count before the core runs, as it does a shot (chamber 1, clip loaded - 1). A gun
 *   with no clip in or an empty chamber has at most 1 loaded, so the charge never takes a round the
 *   engine does not count. The charge is not reset by a manual reload (no engine reload state).
 * - invitems.c: 20 rounds a clip, WEAPONFLAG_DUALWIELD. */
#define MAULER_ROW(weapon) { \
	(weapon), 0, \
	6.0f, 35.0f, 5.0f, { 0.0f, 1.0f, 0.0f }, 1.0f, 0.0f, \
	0, { { 0.0f } }, \
	-1, 4.0f, { -2.4f, -4.6f, -12.1f }, { 2.4f, 3.9f, 16.7f }, { 0.0f, -2.8f, -12.1f }, 6.0f, 0, 0, 0, 0, \
	MODELPART_MAULER_MAGAZINE1, 37, -1, { 0.0f, 0.0f, 0.0f }, -1, 16.0f, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, \
	MODELPART_MAULER_MAGAZINE2, 38, -1, { -0.04f, 0.0f, 0.03f }, { -0.001f, 1.0f, 0.0f }, \
	83, 1477, 475, 0, 0.0f, \
	0.0f, 0.0f, 0.0f, -1, -1, \
	0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 0.0f, \
	0, -1, 0.0f, \
	0.0f, 0.0f, -1, 0, { 0.0f, 0.0f, 0.0f }, { { 0.0f } }, 0.0f, 0.0f, -1, -1, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, 0, \
	0, 0, 0, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, \
	"insert magazine, rack the slide", \
}

/* Tranquilizer model (GdruggunZ, shared with the Psychosis Gun, which takes this row too: see the
 * end), read from the ntsc-final ROM's model data and reload animation
 * (ANIM_GUN_TRANQUILIZER_RELOAD 0x410) with tools/vr_measure:
 * - The vial in the gun (MODELPART_TRANQUILIZER_MAGAZINE2) is a toggle under the root node, shown
 *   at idle (the gunviscmds hide only MAGAZINE1); its 56 vertices are all skinned to matrix 36, a
 *   position node under the holder, matrix 34, which hangs off the gun body (matrix 33) 2.5 cm
 *   above its joint. MAGAZINE1 (matrix 35, also under the holder) is the spent vial the engine's
 *   reload throws out (EJECTTYPE_TRANQCASE); manual reload drops MAGAZINE2 instead, as the other
 *   rows drop their clip, from where it was drawn (the holder still shut).
 * - The holder (holdermtx 34) is shut at idle: the shoot animation (also the equip one) and the
 *   inject animation hold it turned -62.0 degrees about its x, the vial lying back along the top of
 *   the gun. The reload animation turns it upright at its first frame and shuts it again in frames
 *   57 to 72. Manual reload plays none of it for this gun, so vrReloadPoseHolder draws it open
 *   while the vial is out or sliding in (the guide's well is captured open; shut, the insert axis
 *   would be 62 degrees off) and turns it shut after.
 * - In matrix 36's frame, which the open holder does not turn from the body, the vial runs 5.7 cm
 *   along y, from 1.3 cm below the joint (the needle end, which goes in first) to 4.4 above. The
 *   insert axis is the vial's -y, {0, -1, 0}: straight down into the open holder, the seated pose's
 *   axis. The principal axis of the vertices, {-0.007, -0.998, 0.058}, is 3.4 degrees off it. The
 *   animation swings the hand vial in from above and behind, tilted 31 degrees at the hold frame
 *   and upright (within 5 degrees) at frame 37, 0.3 cm above its seat, as the Mauler's clip rocks
 *   in; the guided vial goes straight down.
 * - The insert depth is 5.8 cm, a length of travel: from the hold frame (30) to seated the hand
 *   vial's joint drops 5.8 cm along the axis (about the vial's own 5.7 cm length), so the guided
 *   vial starts with its needle 4.5 cm above the seated joint, clear of the holder (whose top is
 *   0.3 cm above that joint).
 * - Full width (clipwidth 1): the vial is 2.5 cm across (x -1.2..1.3) and the body around its seat
 *   5.0 (x -2.5..2.5); seated, its lower 2 cm sit in the body's pocket, and at no depth of the
 *   insert does it come out through a side of the body (sampled every 0.5 cm to 2 cm, where it
 *   clears the body's top; drawn at 0.8 of its width the samples are the same).
 * - The guide's reach (clipreach): the user marked where the guide should start (handgun test run
 *   2, right-handed, holding a vial; "mark weapon=28" at 21:33:09 and 21:33:18). The held vial's
 *   top was at {1.69, 4.10, -5.20} and {-1.87, 2.90, -4.10} in matrix 36's frame, 5.7 and 5.4 cm
 *   from the mouth ({0, 5.8, 0}: the seated joint moved back up the insert axis), 1.7 and 2.9 cm
 *   past it along the axis, at 107 and 94 degrees (no angle test). The second could not start a
 *   guide: its top was 2.9 cm past the mouth, beyond the 2 cm start margin (VRR_GUIDE_START_PAST_CM),
 *   which vrReloadGuide tests along the same axis from the same mouth the mark line logs. The first
 *   met every start test, 0.3 cm inside clipguiderad, so the guide would have started there; the
 *   mark line logged no guide state then (it does now, guided=), and the log has no insert
 *   between the two marks (no B drop, the vial still held at the second), so it shows no start
 *   that took. The user may have slipped in just after (user, 2026-10-18), so the reach only
 *   widens the guide a little past the marks: each point is its marked top pushed 2 cm further
 *   from the mouth on the line from the mouth through it, {2.28, 3.51, -7.02} and
 *   {-2.57, 1.82, -5.63}, each also used with x negated (vrReloadReachLocal), so both sides of the
 *   holder reach out to both marks. Near a stretch (within VRR_GUIDE_REACH_DEEP_CM, 1.5) the start
 *   margin follows it: up to 2 cm past the stretch's own depth where it passes the top, capped at
 *   the point's depth (2.3 and 4.0 cm past the mouth; vrReloadGuide), so a vial held at either
 *   mark (1.7 and 2.9 cm past), or up to 2 cm further out along its line, starts the guide; a start
 *   at 4.0 draws the vial 0.8 cm from seated (with the pull-in). Near the mouth and off the
 *   stretches the margin stays 2 cm: a vial held straight down the axis still starts only within 2.
 * - No slide (the model has no MODELPART_GUN_SLIDE): the row loads on insert (loadsoninsert 1), so
 *   a vial put into an empty gun chambers a dose at once. The rack fields are zero (and rackmtx
 *   -1): the capture gives no rack area (vrReloadCaptureParts) and the startup racktravel check
 *   skips the row.
 * - The vial in the off-hand (MODELPART_TRANQUILIZER_MAGAZINE3) is a toggle whose 56 vertices are
 *   all skinned to matrix 38, a position node under the left wrist (matrix 18), and are the gun
 *   vial's vertices in the same local frame (a rigid fit of the 36 set onto the 38 set leaves 0.6
 *   mm: a 0.15 degree turn), as the MagSec's and the Mauler's. heldtop is where the gun vial's
 *   joint lands in that frame, {0, 0, 0.06}, as the Mauler's: the end of the principal axis,
 *   {0, -1.29, 0.13}, would seat the vial 1.3 cm early. heldaxis is the gun's -y in that frame,
 *   {0, -1, 0}.
 * - The hand vial is fixed to the wrist in every frame (as the other rows' clips) and the left
 *   fingers do not move in the reload, so the hold frame only sets the wrist's turn from the model
 *   root, which is how the vial sits on the off controller: frame 30, in the last approach (the
 *   vial drops from frame 26 to 37). The wrist keeps turning through it (14 degrees from frame 26
 *   to 30), and no frame of the hold comes closer than 42 degrees to the Falcon's frame 18 (frames
 *   22 to 25; frame 30: 45, and 47 from the Mauler's frame 16). On the wrist the vial's top sits
 *   9.3 cm from the Falcon clip's top and its axis 57 degrees off, but with the wrist's turn the
 *   held vial's insert direction on the controller is 19 degrees from the Falcon clip's at frame 30
 *   (15 at frames 24 to 27).
 * - The hand turn (handturnaxis, handturndeg): 44.9 degrees about {0.534, 0.765, 0.360} in the model
 *   root's frame (tools/vr_measure/handturn.py) puts the frame-30 wrist where the Falcon's frame-18
 *   wrist is, so the hand sits on the off controller as the Falcon's does; without it the hand sat
 *   45 degrees turned from the controller (handgun test run 1). The vial turns with the hand (its top
 *   moves 11.6 cm about the wrist), so on the controller its insert direction is 57 degrees from the
 *   Falcon clip's, not 19: that is the vial as the animation holds it on the wrist. It goes in needle
 *   first from above, its insert axis 158 degrees from the Falcon's (up into the grip), so no hold
 *   that lines up the Falcon's clip lines up the vial: with the two hands held parallel the vial is
 *   97 degrees off its insert axis with the turn (137 without), and with the off hand turned as
 *   little as lines up the Falcon's clip, 107 (148). The turn brings it 40 degrees nearer; the hand
 *   turns the rest, pointing the needle down. Run 1's mark (mouthdist 3.1, angle 95) logged no
 *   controller turn, so it cannot be replayed with the turn; the mark line now logs it.
 * - No angle test (clipalignmax 180, VRR_GUIDE_ANY_ANGLE_DEG: vrReloadGuide skips the test, so a
 *   vial held exactly opposite the axis guides too): the vial drops into an open holder, and with
 *   the hand lined up as the Falcon's it is still 97 degrees off the insert axis, so the other rows'
 *   35 would make the user twist the wrist past 90 degrees (Task E4d). The position test and the
 *   2 cm start margin past the mouth (VRR_GUIDE_START_PAST_CM, widened along the reach) stay. The
 *   guided vial is not drawn in the hand: it is the gun's own vial (matrix 36) moved back up the insert axis
 *   (vrReloadPlaceClip), upright in the open holder whatever the hand's tilt, and the hand is drawn
 *   empty (vrReloadHeldClipDrawn), so from a large angle the vial leaves the hand for the upright
 *   pose above the holder and slides straight down, as a guided Falcon clip goes up the well.
 * - The hand vial's toggle is also on in the gun model, from the gun load until the engine's reload
 *   hides it (frame 39); the gunviscmds do not hide it, and the idle pose puts it at the left
 *   wrist, about 60 cm below the gun. bgun0f0a5550 hides an owned row's held part in the gun model
 *   (the off-hand's vial is drawn by the copy-weapon model).
 * - Sounds as the ROM's reload script plays them (read from the ROM's data, as invitems.c has it):
 *   eject SFX_DOOR_8016 (32790) at frame 3 as the holder opens, insert SFX_HIT_MUD_8083 (32899) at
 *   frame 39 as the vial seats, and SFX_PRESS_SWITCH (186) at frame 72 as the holder lands shut,
 *   which is the rack slot: with no rack, the row plays it as its holder lands
 *   (vrReloadStepHolder). 32790 and 32899 are config ids (sndStart: sounds 204 and 224; configs 15
 *   and 45 only set a falloff). The script also plays SFX_04F5 (1269) at frame 63 as the holder
 *   swings shut, which the row has no slot for. It plays them at 3.5x, 3.5x and 3.0x speed (1269 at
 *   2.07x); the row plays them at normal speed.
 * - invitems.c: 8 doses a vial; no WEAPONFLAG_DUALWIELD, so single wield only.
 * - The Psychosis Gun (WEAPON_PSYCHOSISGUN) takes this row unchanged. Its weapon (invitems.c, and
 *   the ROM's data, as the ntsc-final data segment has both weapons) is the same gun: the same
 *   models (GdruggunZ and its LOD), gunviscmds, part visibility and equip script, and its ammo
 *   points to the same reload script, invanim_tranquilizer_reload, so vrGetReloadAnimIdForWeapon
 *   resolves ANIM_GUN_TRANQUILIZER_RELOAD (0x410) for it and the hold frame, hand turn and sounds
 *   are the Tranquilizer's. 8 rounds a vial, no WEAPONFLAG_DUALWIELD. What differs is not in the
 *   row: its ammo is AMMOTYPE_PSYCHOSIS (the reserve the core reads), it has no secondary function
 *   (no lethal inject, so none of bondgun.c's WEAPON_TRANQUILIZER secondary clip rules), and it is
 *   held 1.0 cm lower and 4.5 cm further back (posy -16.2, posz -28.5 against -15.2, -24), which the
 *   capture follows, as it reads the parts from the drawn matrices. The engine's own reload throws
 *   out the spent vial (EJECTTYPE_TRANQCASE) only for the Tranquilizer; manual reload drops the gun's
 *   vial for both. The reach points are the Tranquilizer's marks, in the vial's frame, so they hold
 *   for this gun's offset too. */
#define TRANQUILIZER_ROW(weapon) { \
	(weapon), 0, \
	6.0f, 180.0f, 5.8f, { 0.0f, -1.0f, 0.0f }, 1.0f, 0.0f, \
	2, { { 2.28f, 3.51f, -7.02f }, { -2.57f, 1.82f, -5.63f } }, \
	-1, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 1, 0, 0, 0, \
	MODELPART_TRANQUILIZER_MAGAZINE2, 36, -1, { 0.0f, 0.0f, 0.0f }, 34, 30.0f, \
	{ 0.5337f, 0.7650f, 0.3604f }, 44.9f, \
	MODELPART_TRANQUILIZER_MAGAZINE3, 38, -1, { 0.0f, 0.0f, 0.06f }, { 0.0f, -1.0f, 0.0f }, \
	32790, 32899, 186, 0, 0.0f, \
	0.0f, 0.0f, 0.0f, -1, -1, \
	0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 0.0f, \
	0, -1, 0.0f, \
	0.0f, 0.0f, -1, 0, { 0.0f, 0.0f, 0.0f }, { { 0.0f } }, 0.0f, 0.0f, -1, -1, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, 0, \
	0, 0, 0, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, \
	"drop a vial into the holder", \
}

/* Phoenix model (GmaianpistolZ), read from the ntsc-final ROM's model data and reload animation
 * (ANIM_GUN_PHOENIX_RELOAD 0x415, the single-wield reload, which vrGetReloadAnimIdForWeapon picks for
 * a pair too; the dual-wield one, 0x423, never brings the orb within 40 cm of the gun) with
 * tools/vr_measure:
 * - No clip part and no slide: the model's four parts are the body (0x37, matrix 33), a node on it
 *   (0x32), a six-vertex toggle on the body (0x42) and the orb, MODELPART_PHOENIX_ORB (0x28), a
 *   toggle the gunviscmds and the part visibility hide, which the reload script shows at frame 1 and
 *   hides at 50. So the row has no clip part (clippart and clipmtx -1): nothing is drawn in the gun,
 *   loaded or empty; B empties it with nothing falling, and the empty gun waits for an orb.
 * - The orb's 116 vertices are all skinned to three matrices under the left wrist (matrix 18): 37,
 *   its middle (a flat disc, 25 vertices), and 38 and 39 under it, 5.3 cm either side along its z
 *   (51 and 40 vertices), so it is 11 cm long along that z and 5 to 6 cm across. All three stay
 *   drawn with the off-hand (heldmtx 37, heldmtxlast 39).
 * - The intake point (intakemtx 33, the gun body): the animation holds the orb's middle (matrix 37's
 *   joint) against the gun's side from frame 36, where it arrives, to 46, at {2.7, 0.9, 14.6} cm in
 *   the body's frame (frame 42 within 0.1 cm; the mean of frames 38 to 46 is {2.71, 0.86, 14.66},
 *   every one of them within 0.5 cm of it), where the two side plates (matrices 35 and 36, x 2.2 to
 *   2.8, z 7.8 to 14.9 and 14.9 to 20.6) meet, on the +x side, the side the animation's left hand
 *   comes from. Through the body matrix the point is mirrored with the model in left-handed mode, so
 *   it stays on the off-hand's side.
 * - The guide is a point test: the orb loads as the held orb's middle comes within clipguiderad (6
 *   cm, the other rows' value) of the intake point, from any side and at any angle (no clipaxis,
 *   heldaxis, insert depth or reach). It loads on arrival and is not drawn going in: the animation
 *   does not move it in either, it hides it at frame 50 as the hand pulls away (from 46 to 50 the
 *   orb moves 1 cm and its two ends turn up to 23 degrees).
 * - After the load the gun plays the orb going in (playfrom 36 to playto 85, user, handgun test run
 *   1: the orb's squish in should play, without the hand bringing it out): the frames where the
 *   orb sits at the intake, its two ends turn (matrices 38 and 39, 25 and 16 degrees by frame 49)
 *   and it is hidden at 50 (playheldto, the script's hidepart), while the side plates slide out
 *   and back without turning: plate 35 (the rear one) up to 8.5 cm from shut (frame 73: 7.6 out,
 *   3.9 back; it moves in frames 42 to 84 and passes 1.6 cm inside shut at 79), plate 36 up to 2.9
 *   cm (frame 61; it moves in frames 49 to 75). Only the plates and the
 *   orb are posed (playmtx 35 to playmtxlast 39), each relative to the body as the animation has
 *   it: the animation moves the body itself 18.8 cm and 41.5 degrees from idle, the root 22 cm, so
 *   it must not drive the gun. Frame 85 is the first with both plates back on their rest pose to
 *   the 0.1 mm (84 leaves plate 35 1 mm out), and the plates sit on that pose in every other frame
 *   of this animation, the shoot (0x43e) and the dual-wield reload (0x423), so the playback starts
 *   and ends on the idle look. 49 frames at speed 1: 0.82 s of game time. Measured by
 *   tools/vr_measure/phoenixplay.py, which also checks the rebase against a mirrored draw. The
 *   left hand bringing the orb in (frames 0 to 35) and pulling away (from 46) is not played: the
 *   playback leaves the gun model's left hand where the idle pose and the free-hand hide put it.
 * - heldtop is matrix 37's joint, {0, 0, 0}: the point the animation brings to the intake (the
 *   middle of the orb's vertices is within 0.5 cm of it).
 * - The orb is fixed to the wrist in frames 0 to 44 and the left fingers do not move until frame 46,
 *   so the hold frame only sets the wrist's turn from the model root, which is how the orb sits on
 *   the off controller: frame 36, where the orb reaches the intake (the wrist turns 8 degrees in
 *   the last approach, frames 30 to 36, and is 76 degrees from the Falcon's frame 18). On the wrist
 *   the orb's middle is at {-0.45, -6.51, 11.27} cm in matrix 18's frame, 11 cm from the Falcon
 *   clip's top: held in the palm. The left wrist and fingers are matrices 18 and 19 to 32, as in
 *   the other rows.
 * - The hand turn (handturnaxis, handturndeg): 76.1 degrees about {0.812, 0.502, 0.299} in the model
 *   root's frame (tools/vr_measure/handturn.py; the 76 above) puts the frame-36 wrist where the
 *   Falcon's frame-18 wrist is, so the hand sits on the off controller as the Falcon's does (handgun
 *   test run 1: the orb sat turned in the hand, and loaded only once the user lined it up). The orb
 *   turns with the hand and stays in the palm: its middle, 13 cm from the wrist, moves 15.7 cm on the
 *   controller. The intake test has no angle, so only where the orb sits matters.
 * - In the gun model the orb hangs off the idle left wrist, about 75 cm from the gun body: the
 *   gunviscmds hide it, and bgun0f0a5550 hides an owned row's held part anyway (the off-hand's orb
 *   is drawn by the copy-weapon model), except while the playback above poses it at the intake.
 * - No slide: the row loads on insert (loadsoninsert 1), so an orb put into an empty gun chambers a
 *   round at once. The rack fields are zero (and rackmtx -1): the capture gives no rack area and
 *   the startup racktravel check skips the row.
 * - Sounds: the ROM's reload script plays only SFX_RELOAD_FARSIGHT (1075, a sound id, not a config
 *   id) at frame 44 as the orb is absorbed, at 1.51x speed: the row's insert, at normal speed, which
 *   plays as the load starts the playback (8 frames, 0.13 s, before the script's frame 44). No
 *   rack (sndrack 0). The script has no eject sound, so B was silent (handgun test run 1), and the
 *   Tranquilizer's holder opening (SFX_DOOR_8016, 32790) it took then sounded wrong for this gun
 *   (user, handgun test run 2). B plays SFX_05C7 (1479, a sound id as 1075 is, not a config id):
 *   the Reaper's magazine release, a Skedar gun's (its reload script plays it at frame 49, before
 *   the magazine comes off at 70, at normal speed, as the row plays it), and no other gun's script
 *   uses it, so B sounds of an alien device letting go of its load. Runners-up, for the user to
 *   ask for: SFX_HIT_WATER (32896, a config id: sound 76, config 45 only a falloff), the Maian
 *   guns' own dry-fire "wet click" (bgunTickIncAttackEmpty plays it for the Phoenix, the Callisto
 *   and the Farsight), but at 2.07x: at the row's normal speed it is the water splash it is
 *   elsewhere; and SFX_EYESPYHIT (32908, a config id: sound 45, config 21 only a falloff), the
 *   Slayer's reload at frame 79 (Skedar), which the CamSpy also plays. Left out: the Callisto's
 *   and the Farsight's reload scripts play only SFX_RELOAD_FARSIGHT, the load; the Phoenix's,
 *   Callisto's and Farsight's shoot sounds (SFX_8071, 8072, 8063, 8064, 813E) are shots; the
 *   Phoenix has no draw or mode-switch sound of its own (its draw is its reload script); the
 *   Mauler's and the Slayer's eject is SFX_0053 (83), the clip release of most human guns.
 * - invitems.c, and the ROM's data: 8 rounds an orb (clip size 8), WEAPONFLAG_DUALWIELD. */
#define PHOENIX_ROW(weapon) { \
	(weapon), 0, \
	6.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f, \
	0, { { 0.0f } }, \
	-1, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 1, 0, 0, 0, \
	-1, -1, 33, { 2.7f, 0.9f, 14.6f }, -1, 36.0f, \
	{ 0.8118f, 0.5016f, 0.2990f }, 76.1f, \
	MODELPART_PHOENIX_ORB, 37, 39, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, \
	1479, 1075, 0, 0, 0.0f, \
	36.0f, 85.0f, 50.0f, 35, 39, \
	0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 0.0f, \
	0, -1, 0.0f, \
	0.0f, 0.0f, -1, 0, { 0.0f, 0.0f, 0.0f }, { { 0.0f } }, 0.0f, 0.0f, -1, -1, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, 0, \
	0, 0, 0, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, \
	"press an orb into the side intake", \
}

/* CMP150 model (Gcmp150Z), read from the ntsc-final ROM's model data and reload animation
 * (ANIM_GUN_CMP150_RELOAD 0x115, the single-wield reload) with tools/vr_measure (cmphold.py,
 * cmprack.py, cmpmove.py, cmpgrip.py, survey.py, handturn.py):
 * - The model has 40 matrices, the left hand 17 to 32 as the pistols'. The clip
 *   (MODELPART_CMP150_MAGAZINE1, 0x2a) is a toggle whose 8 vertices are all skinned to matrix 38, a
 *   position node under the gun body (matrix 33), not turned from it in the rest pose, with no part
 *   id of its own. As the Mauler's and the Tranquilizer's it is shown at idle (the gunviscmds and the
 *   part visibility hide only MAGAZINE2, the hand clip).
 * - In matrix 38's frame the clip is a box x -1..1, z -1.6..2.0, running 22.4 cm along y from 22.4
 *   below the joint up to it, so the joint is the top of the clip and the insert axis is its +y,
 *   {0, 1, 0}, the body's up (the grip is straight, not raked as the pistols'). The box's four sides
 *   are the clip part; its bottom face is in part 0x42 (below).
 * - The insert depth is 13.4 cm: the seated joint is 2.4 cm above the body's origin and the grip ends
 *   at y -11, so seated the clip's base hangs 9 cm under the grip, as the gun draws it, and the hand
 *   clip's top reaches the well mouth at the hold frame (below). The clip's front corners poke out of
 *   the grip by up to 0.57 cm over its bottom 3.5 cm (cmpgrip.py: there the grip's front at the
 *   clip's sides, x -1 and 1, is at z 1.4, and it narrows to x -0.15..0.15 at the clip's front, z 2);
 *   clipwidth stays 1 for the headset check (0.15 would keep it inside: a sliver).
 * - Part 0x42 (MODELPART_CMP150_0042), a toggle, holds the clip's bottom face, 4 vertices on matrix 38
 *   at y -22.4, with a front face of the gun (23 vertices on the body); bgun0f098030 hides it on every
 *   frame for every gun, so it is never drawn. Manual reload moves matrix 38 with the drawn clip
 *   (seated, guided in, falling) and collapses it while no clip is drawn in the gun
 *   (vrReloadCollapseClip), which is harmless for the hidden part.
 * - No slide: the model has no MODELPART_GUN_SLIDE and no part of the gun moves in the rack (every
 *   matrix past the hands but the clips' 38 and 39 stays within 0.3 cm and 3 degrees of its rest
 *   pose on the body in all 128 frames, survey.py), so the rack is on the body (rackmtx 33):
 *   nothing slides under the hand, and the buzz and the rack sound are the feedback. The
 *   animation's left hand takes the rear of the receiver and pulls it about 2 cm back, toward the
 *   body's -z, in frames 76 to 97 (the wrist from z -14.8 to -18.6, the fingers closing on the body
 *   at z -1 to -10; cmprack.py), with the rack sound at 86. The rack box is that rear part of the
 *   body: x -2..2 (its full width), y -4..5.4 (the fingers' span, up to the top of the receiver),
 *   z -10.9..-1 (from the rear end forward to where the hand reaches), rackrad 4 as the pistols';
 *   rackoffset {0, 0, -7}, on the body's centre line where the animation's middle finger closes on
 *   it (z -6.4 to -8.6 in frames 79 to 94), for the logs. racktravel 3: the animation pulls about
 *   2, and the row may not set less than VRR_RACK_PULL_CM (3).
 * - The clip in the off-hand (MODELPART_CMP150_MAGAZINE2, 0x2b) is a second toggle whose 8 vertices
 *   are all skinned to matrix 39, a position node under the left wrist (matrix 18), and are the gun
 *   clip's vertices in the same local frame (identical), as the MagSec's: heldtop is the joint,
 *   {0, 0, 0}, heldaxis +y, {0, 1, 0}. It has no bottom face (part 0x42 is the gun's): the engine's
 *   own reload draws it so too.
 * - The hand clip is fixed to the wrist in frames 28 to 43 (within 0.14 cm). The hold frame is 39: the
 *   held clip's top is 13.5 cm below the seated joint along the axis (at the well mouth), 0.3 cm off
 *   it, its axis 0.4 degrees from the well's (frames 40 to 42 push it in; the script hides it at 45).
 * - The hand turn (handturnaxis, handturndeg): 30.6 degrees about {0.9637, -0.2623, -0.0501} in the
 *   model root's frame (handturn.py) puts the frame-39 wrist where the Falcon's frame-18 wrist is, so
 *   the hand sits on the off controller as the Falcon's does. The held clip's top, 24.4 cm from the
 *   wrist, moves 6.4 cm with it; on the controller its insert direction is then 23 degrees from the
 *   Falcon clip's (32 without), and the well axis is 22 degrees from the Falcon's, so with the two
 *   hands held parallel the held clip is 20 degrees off the insert axis (19 without the turn), inside
 *   clipalignmax (35, as the pistols').
 * - No clip turn (clipturnaxis, clipturndeg and clipturnpivot zero; Task F7e): with the hand turn
 *   its held clip sits in the drawn hand 23 degrees off the Falcon 2's along the insert axis and 14
 *   off its front (a 26 degree turn would line it up, clipturn.py), and the user finds it right
 *   (Stage F run 1). No clip shift (clipshift zero; Task F8e) either.
 * - Sounds as the ROM's reload script plays them (invitems.c): eject SFX_01D8 (472) at frame 9 and
 *   insert SFX_80F6 (33014, a config id: sound 473, as the MagSec row's) at 44, both at 0.95x, rack
 *   SFX_0431 (1073, a sound id) at 86; the row plays them at normal speed. They are the MagSec's
 *   eject and insert.
 * - Pairs: the dual-wield reload (0x422) is not used for the hand: vrGetReloadAnimIdForWeapon poses
 *   the copy model from the single-wield one with the left hand out of use, as for the pistols.
 * - Both functions, rapid fire and follow lock-on, fire from the one clip (ammoindex 0), which the
 *   row's primary reads; slidemax 0 in both, so the engine draws no slide either.
 * - invitems.c: 32 rounds a clip, AMMOTYPE_SMG, WEAPONFLAG_DUALWIELD. */
#define CMP150_ROW(weapon) { \
	(weapon), 0, \
	6.0f, 35.0f, 13.4f, { 0.0f, 1.0f, 0.0f }, 1.0f, 0.0f, \
	0, { { 0.0f } }, \
	33, 4.0f, { -2.0f, -4.0f, -10.9f }, { 2.0f, 5.4f, -1.0f }, { 0.0f, 0.0f, -7.0f }, 3.0f, 0, 0, 0, 0, \
	MODELPART_CMP150_MAGAZINE1, 38, -1, { 0.0f, 0.0f, 0.0f }, -1, 39.0f, \
	{ 0.9637f, -0.2623f, -0.0501f }, 30.6f, \
	MODELPART_CMP150_MAGAZINE2, 39, -1, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, \
	472, 33014, 1073, 0, 0.0f, \
	0.0f, 0.0f, 0.0f, -1, -1, \
	0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 0.0f, \
	0, -1, 0.0f, \
	0.0f, 0.0f, -1, 0, { 0.0f, 0.0f, 0.0f }, { { 0.0f } }, 0.0f, 0.0f, -1, -1, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, 0, \
	0, 0, 0, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, \
	"insert magazine, pull the charging handle", \
}

/* AR34 model (Gar34Z), read from the ntsc-final ROM's model data and reload animation
 * (ANIM_GUN_AR34_RELOAD 0x419, 134 frames) with tools/vr_measure (riflegeo.py, rifleicp.py,
 * riflehold.py, riflewell.py, riflepoke.py, rifleslap.py, rifleslapmove.py, handturn.py,
 * alignturn.py):
 * - The model has 40 matrices, the left hand 17 to 32 as the pistols', the body 33. The clip
 *   (MODELPART_AR34_MAGAZINE1, 0x29) is a toggle whose 35 vertices are all skinned to matrix 38, a
 *   position node under the body, not turned from it in the rest pose, seated with its joint at
 *   {0, -2.7, -14.2} cm in the body's frame. As the CMP150's it is shown at idle (the gunviscmds
 *   and the part visibility hide only MAGAZINE2, the hand clip).
 * - The clip is curved: in matrix 38's frame it runs 15.0 cm down from the joint (y 0.4 to -14.6),
 *   its base bowed 3.4 cm forward (z -3.9..7.6). The insert axis is its +y at the top, {0, 1, 0}, the
 *   body's up: the animation's last 2.8 cm of travel is straight along it (riflehold.py: frames 64
 *   to 67, the hand clip's top 2.8 cm below the seated joint and 2.1 off it at 64, seated at 67,
 *   its axis within 4.3 degrees of +y).
 * - The insert depth is 3 cm, a length of travel: the AR34 has no well. The clip's top sits flush
 *   under the receiver's sloped underside (riflewell.py: y -3.1 to -2.0 in the body's frame along
 *   the clip, whose top is at -2.3) and the whole clip shows below it, so the guided clip comes up
 *   the last 3 cm along the axis, as the animation's does. No body vertex is inside the seated clip
 *   or within 1 cm of its sides more than 0.3 cm below its top (riflepoke.py): full width (clipwidth
 *   1). A start at the full VRR_GUIDE_START_PAST_CM (2) plus the 1 cm pull-in (VRR_PULL_IN_CM) is
 *   drawn seated at once.
 * - No base cap: part 0x42 (a toggle bgun0f098030 hides on every frame for every gun) has no vertex on
 *   matrix 38 (riflegeo.py), so
 *   vrReloadCollapseClip draws nothing differently for this gun.
 * - No part of the gun moves in the reload and the model has no MODELPART_GUN_SLIDE. After the
 *   insert the animation's left hand drops away and slaps the clip in from below (user, 2026-10-19,
 *   from the original game's reload): rackslap 1, so an empty gun needs the slap after an insert as a
 *   rack gun needs a rack (loadsoninsert 0; B empties it, as every gun since Task F4), and the rack fields are
 *   zero with rackmtx -1 (no rack area; a grip and pull do nothing). The slap (rifleslapmove.py, the
 *   palm the middle of the wrist and the four knuckles): the hand dips to frame 85, swings up at up
 *   to 4.7 m/s (frame 89) and stops on the clip's base at 93 (slapcontact: the first frame after the
 *   peak under 1 m/s up), the palm 1.8 cm from the clip, then holds it to 95 (slapto: from 96 it drops
 *   away at over 1 m/s; rifleslap.py: the fingers on the clip's side, 1.1 to 1.4 cm from it, at 93 to
 *   96). slappoint is the palm at 93, {-1.87, -16.19, 0.06} in matrix 38's frame, 1.6 cm under the
 *   clip's lowest point (y -14.6); slaprad 12.5 cm, as the K7 Avenger's (Task F8f; 8 before, a guess
 *   from when the test used the free hand's grab point, not its palm, Task F7d); slapfrom 91, the
 *   first frame of the swing with the palm within 8 cm of the slap point (3.4 cm out). With slaprad
 *   12.5 the swing can start up to 12.5 cm out, so the drawn hand is not put on the animation's at
 *   slapfrom: it goes from the controller's place to it from slapfrom to slapcontact
 *   (vrReloadPlaceSlapHand's way in, Task F8 review), and meets the clip at the contact. The clip sits
 *   on the body in those frames within 0.1 cm and 5 degrees of its rest pose, so the hand is placed on
 *   the gun's drawn clip (vrReloadPlaceSlapHand). The detection (VRR_SLAP_SPEED in vr_reload.c) is the
 *   adapter's, not the row's.
 * - The clip in the off-hand (MODELPART_AR34_MAGAZINE2, 0x2a) is a second toggle whose 35 vertices
 *   are all skinned to matrix 39, a position node under the left wrist (matrix 18), and are the gun
 *   clip's vertices in the same local frame, as the CMP150's: a rigid fit of the 38 set onto the 39
 *   set (rifleicp.py) leaves 0.5 mm with no turn and the gun clip's joint at {0.05, 0, 0} in 39's
 *   frame, within the fit. heldtop is the joint, {0, 0, 0}, heldaxis +y, {0, 1, 0}.
 * - The hold frame is 64: the hand clip is fixed to the wrist through the approach (within 1.1 cm,
 *   frames 30 to 79, riflehold.py) and at 64 its top is 2.8 cm below the seated joint along the axis (the well's
 *   mouth at this depth), 2.1 off it, its axis 3.6 degrees from the well's; the script shows the
 *   hand clip at 50 and swaps it for the gun clip at 72.
 * - The hand turn (handturnaxis, handturndeg): 165.8 degrees about {-0.5993, 0.6974, 0.3929} in the
 *   model root's frame (handturn.py) puts the frame-64 wrist where the Falcon's frame-18 wrist is, so
 *   the hand sits on the off controller as the Falcon's does (handgun test run 1: hands not lined
 *   up with the controller were the complaint). The held clip's top, 20.2 cm from the wrist, moves
 *   39.4 cm with it, and with the two hands held parallel the held clip is then 90 degrees off the
 *   insert axis (61 without the turn). The turn that would line the clip up with the well instead
 *   (61.2 degrees, alignturn.py) leaves the wrist 130 degrees from the Falcon's frame-18 wrist.
 * - The clip turn (clipturnaxis, clipturndeg, clipturnpivot; Task F7e, Stage F run 1: the held clip
 *   turned wrong in the hand): after the hand turn, 134.5 degrees about {0.4447, -0.1185, -0.8878}
 *   in the model root's frame (clipturn.py) turns the held clip alone about the palm, {-1.365,
 *   -13.246, 1.58} in matrix 39's frame, so it sits in the drawn hand as the Falcon 2's clip sits
 *   in its hand at frame 18: its insert axis and its front (the gun's forward) in the wrist's frame
 *   as the Falcon clip's (58 and 105 degrees off before); its top moves 15.9 cm. The guide still
 *   takes any angle; no reach points to recheck.
 * - The clip shift (clipshift; Task F8e, Stage F run 2: the rifles' held clips sat on the
 *   knuckles, not in the palm): after the clip turn, {-5.475, 1.135, -1.665} cm in matrix 39's
 *   frame as turned (clipshift.py) moves the held clip alone 5.8 cm, so its grip point (the
 *   middle of its length and across, on its palm side) sits at the Falcon 2's clip's offset from
 *   the palm (the middle of the hand bone and its four knuckles) at frame 18; before it, the
 *   grip point sat 5.6 cm too far toward the palm side (the clip level with the hand, beside its
 *   knuckles), 1.1 along the insert axis and 1.1 across. Its top, so the guide's start, moves
 *   with it (vrReloadCaptureHeldClip reads the matrix after the shift).
 * - No angle test (clipalignmax 180, VRR_GUIDE_ANY_ANGLE_DEG, as the Tranquilizer's): with the hand
 *   lined up the clip is held about 90 degrees off the well. A guided clip is the gun's own clip
 *   moved back down the insert axis (vrReloadPlaceClip), upright under the receiver whatever the
 *   hand's tilt, and the hand is drawn empty (vrReloadHeldClipDrawn).
 * - Sounds as the ROM's reload script plays them (invitems.c): eject SFX_0053 (83) at frame 16, insert
 *   SFX_8025 (32805, a config id: sndStart maps it through g_AudioRussMappings[0x25] to sound 222) at
 *   67 as the clip seats, and the hand's slap SFX_05C5 (1477, a sound id) at 95: the row's rack sound,
 *   which the slap plays at slapcontact (no second insert sound: sndinsert2 0). The script plays them
 *   at 0.9x, 1.4x and 0.9x speed; the row plays them at normal speed. The slap buzzes the free hand
 *   harder than a grab (vrReloadApplyOutput).
 * - The draw: the AR34's equip animation is this reload script (invitems.c). With manual reload on
 *   the holsters' fast switch puts the gun on its frame 0 without playing it (vrHolsterSetEquipPose,
 *   frame 0 for this WEAPONFLAG_00004000 gun), so no part command or sound of it runs, and
 *   bgun0f0a5550 sets both clips from the reload state every frame. With manual reload off a draw
 *   holds frame 0 too (unk0cc8_02 zeroes the frame step), as in the original game.
 * - The fall (vrReloadStartDrop) tumbles the clip about a point 1.5 cm below its top (half the insert
 *   depth) and rests that point on the floor, so an upright clip's base, 15 cm down, ends about 10
 *   cm under the floor for the rest of its 1 s fall.
 * - Both functions, burst fire and the scope, fire from the one clip (ammoindex 0); slidemax 0.
 * - invitems.c: 30 rounds a clip, AMMOTYPE_RIFLE, no WEAPONFLAG_DUALWIELD (single wield only);
 *   VrTwoHandsGun (the free hand's grip supports it, except mid-reload: from B until the slap,
 *   vrReloadMidReload, Tasks F8d and F9a). */
#define AR34_ROW(weapon) { \
	(weapon), 0, \
	6.0f, 180.0f, 3.0f, { 0.0f, 1.0f, 0.0f }, 1.0f, 0.0f, \
	0, { { 0.0f } }, \
	-1, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 0, 1, 0, 0, \
	MODELPART_AR34_MAGAZINE1, 38, -1, { 0.0f, 0.0f, 0.0f }, -1, 64.0f, \
	{ -0.5993f, 0.6974f, 0.3929f }, 165.8f, \
	MODELPART_AR34_MAGAZINE2, 39, -1, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, \
	83, 32805, 1477, 0, 0.0f, \
	0.0f, 0.0f, 0.0f, -1, -1, \
	91.0f, 93.0f, 95.0f, { -1.87f, -16.19f, 0.06f }, 12.5f, \
	0, -1, 0.0f, \
	0.0f, 0.0f, -1, 0, { 0.0f, 0.0f, 0.0f }, { { 0.0f } }, 0.0f, 0.0f, -1, -1, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, 0, \
	0, 0, 0, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, \
	{ 0.4447f, -0.1185f, -0.8878f }, 134.5f, { -1.365f, -13.246f, 1.580f }, \
	{ -5.475f, 1.135f, -1.665f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, \
	"insert magazine, slap it home", \
}

/* K7 Avenger model (Gk7avengerZ), read from the ntsc-final ROM's model data and reload animation
 * (ANIM_GUN_K7AVENGER_RELOAD 0xe9, 134 frames) with tools/vr_measure (as the AR34's row, whose
 * scripts measure both):
 * - The model has 41 matrices, the left hand 17 to 32 as the pistols', the body 33. The clip
 *   (MODELPART_K7AVENGER_MAGAZINE1, 0x29) is a toggle whose 28 vertices are all skinned to matrix
 *   39, a position node under the body, not turned from it in the rest pose, seated with its joint
 *   at {0, -6.0, -16.7} cm in the body's frame. As the AR34's it is shown at idle (the gunviscmds
 *   and the part visibility hide only MAGAZINE2, the hand clip).
 * - In matrix 39's frame the clip is a straight box beside its joint, not under it: x 1.4..3.0 (its
 *   middle 2.2 cm to the side), z -3.8..3.8, from the joint's level (y 0.1) down 11.2 cm at its
 *   back and 9.2 at its front. The insert axis is its +y, {0, 1, 0}, the body's up. The joint is the
 *   point the guide measures: the mouth is the seated joint moved back down the axis, and heldtop
 *   (below) is the same joint carried into the hand clip's frame, so a hand clip lined up with the
 *   seated one has its heldtop on the mouth's line, though 2.2 cm from the clip's own top middle.
 * - The insert depth is 3 cm, a length of travel, as the AR34's: the clip's top sits flush under the
 *   receiver (riflewell.py: the body's underside at y -5.9 over the clip, its top at -5.9), and at
 *   the hold frame (65) the hand clip's joint is 2.8 cm below the seated one along the axis, 0.3 off
 *   it, its axis 0.6 degrees from the well's (riflehold.py). No body vertex is inside the seated clip
 *   or within 1 cm of its sides more than 0.3 cm below its top (riflepoke.py): full width (clipwidth
 *   1). A start at the full VRR_GUIDE_START_PAST_CM (2) plus the 1 cm pull-in is drawn seated at
 *   once.
 * - Part 0x42 (MODELPART_K7AVENGER_0042), a toggle, holds the clip's bottom face, 4 vertices on
 *   matrix 39 (y -11.1 at the back, -9.1 at the front), with 20 on the body, as the CMP150's;
 *   bgun0f098030 hides it on every frame for every gun, so it is never drawn. Manual reload moves
 *   matrix 39 with the drawn clip (seated, guided in, falling) and collapses it while no clip is
 *   drawn in the gun (vrReloadCollapseClip, which reads clipmtx), which is harmless for the hidden
 *   part.
 * - The slap, as the AR34's (rackslap 1, loadsoninsert 0, the rack fields zero with rackmtx -1;
 *   rifleslapmove.py): the hand dips to frame 85, swings up at up to 4.8 m/s (89) and stops on the
 *   clip's base at 92 (slapcontact), the palm 3.1 cm from it, pushing on to 95 (slapto; the fingers
 *   on the clip's side, 1.2 to 1.5 cm from it at 93 to 95, rifleslap.py). slappoint is the palm at 92,
 *   {2.56, -13.50, -0.60} in matrix 39's frame, under the clip (its back corner reaches y -11.1);
 *   slapfrom 90 (6.6 cm out; the drawn hand goes from the controller's place to the animation's
 *   from slapfrom to slapcontact, as the AR34's). slaprad 12.5 (Task F8f, Stage F run 2; 8 before): the user marked a
 *   pose that should have slapped ("mark weapon=16", tools/vr_measure/marks.py), the drawn palm
 *   10.8 cm from the slap point, 10.6 beside it on the clip's left (the free hand's side) and 2.1
 *   above, the hand pointing across at the clip; a swing just before it was refused at 9.7 (why=far).
 *   12.5 takes the mark with 1.7 cm to spare; the AR34 and the Laptop Gun take the same radius. The clip sits on the body in those frames exactly as at
 *   rest.
 * - The clip in the off-hand (MODELPART_K7AVENGER_MAGAZINE2, 0x2a) is a second toggle whose 28
 *   vertices are all skinned to matrix 40, a position node under the left wrist (matrix 18), in a
 *   different local frame from the gun clip's, as the Falcon's 42: a rigid fit of the 39 set onto
 *   the 40 set (rifleicp.py, one-to-one) leaves 0.5 mm and puts 39's joint at
 *   {-0.937, -1.523, -0.266} in 40's frame (heldtop) and 39's +y along
 *   {-0.5289, 0.2256, 0.8182} (heldaxis). Matrix 40's joint is at the clip's top (the top's middle
 *   is at {0.10, 0.40, 0} in its frame), so heldtop sits 2.2 cm beside the drawn clip, where the
 *   gun clip's joint is.
 * - The hold frame is 65: the hand clip is fixed to the wrist through the approach (within 0.44 cm,
 *   frames 30 to 79, riflehold.py) and at 65 it is the nearest to the well's line short of the
 *   seated pose (above); the script shows the hand clip at 48 and swaps it for the gun clip at 69.
 * - The hand turn: 166.7 degrees about {-0.5862, 0.7276, 0.3563} in the model root's frame
 *   (handturn.py) puts the frame-65 wrist where the Falcon's frame-18 wrist is, as the AR34's (the two
 *   animations hold the left wrist alike). The held clip's top, 18.2 cm from the wrist, moves
 *   34.7 cm with it, and with the two hands held parallel the held clip is then 86 degrees off the
 *   insert axis (65 without the turn); the turn that lines it up with the well instead (64.7
 *   degrees, alignturn.py) leaves the wrist 128 degrees from the Falcon's.
 * - The clip turn (clipturnaxis, clipturndeg, clipturnpivot; Task F7e, Stage F run 1: the held clip
 *   turned wrong in the hand): after the hand turn, 134.9 degrees about {0.4804, -0.1168, -0.8693}
 *   in the model root's frame (clipturn.py) turns the held clip alone about the palm, {5.684,
 *   -3.199, -9.17} in matrix 40's frame, so it sits in the drawn hand as the Falcon 2's clip sits
 *   in its hand at frame 18: its insert axis and its front (the gun's forward) in the wrist's frame
 *   as the Falcon clip's (54 and 109 degrees off before); its top moves 10.3 cm. The guide still
 *   takes any angle; no reach points to recheck.
 * - The clip shift (clipshift; Task F8e, Stage F run 2: the rifles' held clips sat on the
 *   knuckles, not in the palm): after the clip turn, {-3.905, -3.132, 0.321} cm in matrix 40's
 *   frame as turned (clipshift.py) moves the held clip alone 5.0 cm, so its grip point (the
 *   middle of its length and across, on its palm side) sits at the Falcon 2's clip's offset from
 *   the palm (the middle of the hand bone and its four knuckles) at frame 18; before it, the
 *   grip point sat 4.7 cm too far toward the palm side (the clip level with the hand, beside its
 *   knuckles), 1.6 along the insert axis and 0.6 across. Its top, so the guide's start, moves
 *   with it (vrReloadCaptureHeldClip reads the matrix after the shift).
 * - No angle test (clipalignmax 180, VRR_GUIDE_ANY_ANGLE_DEG), as the AR34's and for the same reason.
 * - Sounds as the ROM's reload script plays them (invitems.c): eject SFX_0053 (83) at frame 15, insert
 *   SFX_8025 (32805, a config id: sound 222) at 69 as the clip seats, and the hand's slap SFX_05C5
 *   (1477) at 91, as the palm arrives: the row's rack sound, played at slapcontact. The script plays
 *   them at 1.2x, 1.4x and 1.0x speed; the row plays them at normal speed.
 * - The draw: the K7 has its own equip animation (ANIM_GUN_K7AVENGER_EQUIP), which the holsters'
 *   fast switch skips, and no WEAPONFLAG_00004000.
 * - The fall tumbles the clip about a point 1.5 cm below its joint (half the insert depth, on the
 *   joint's line, 2.2 cm beside the clip) and rests that point on the floor, so an upright clip's
 *   base, 11.1 cm down, ends about 7 cm under the floor, as the AR34's does.
 * - Both functions, burst fire and the threat detector, fire from the one clip (ammoindex 0);
 *   slidemax 0.
 * - invitems.c: 25 rounds a clip, AMMOTYPE_RIFLE, no WEAPONFLAG_DUALWIELD (single wield only);
 *   VrTwoHandsGun (the free hand's grip supports it, except mid-reload: from B until the slap,
 *   vrReloadMidReload, Tasks F8d and F9a). */
#define K7AVENGER_ROW(weapon) { \
	(weapon), 0, \
	6.0f, 180.0f, 3.0f, { 0.0f, 1.0f, 0.0f }, 1.0f, 0.0f, \
	0, { { 0.0f } }, \
	-1, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 0, 1, 0, 0, \
	MODELPART_K7AVENGER_MAGAZINE1, 39, -1, { 0.0f, 0.0f, 0.0f }, -1, 65.0f, \
	{ -0.5862f, 0.7276f, 0.3563f }, 166.7f, \
	MODELPART_K7AVENGER_MAGAZINE2, 40, -1, { -0.937f, -1.523f, -0.266f }, { -0.5289f, 0.2256f, 0.8182f }, \
	83, 32805, 1477, 0, 0.0f, \
	0.0f, 0.0f, 0.0f, -1, -1, \
	90.0f, 92.0f, 95.0f, { 2.56f, -13.50f, -0.60f }, 12.5f, \
	0, -1, 0.0f, \
	0.0f, 0.0f, -1, 0, { 0.0f, 0.0f, 0.0f }, { { 0.0f } }, 0.0f, 0.0f, -1, -1, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, 0, \
	0, 0, 0, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, \
	{ 0.4804f, -0.1168f, -0.8693f }, 134.9f, { 5.684f, -3.199f, -9.170f }, \
	{ -3.905f, -3.132f, 0.321f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, \
	"insert magazine, slap it home", \
}

/* RC-P120 model (Grcp120Z), read from the ntsc-final ROM's model data and reload animation
 * (ANIM_GUN_RCP120_RELOAD 0x41a, 154 frames) with tools/vr_measure (f5geo.py, f5hold.py, f5trace.py,
 * f5bolt.py, f5well.py, f5poke.py, f5turn.py; Task F5, 2026-10-19):
 * - The model has 41 matrices, the left hand 17 to 32 as the pistols', the body 33. The clip
 *   (MODELPART_RCP120_MAGAZINE1, 0x28) is a toggle whose 20 vertices are all skinned to matrix 39,
 *   a position node under the body, not turned from it in the rest pose, seated with its joint at
 *   {0, 4.14, -18.68} cm in the body's frame. It is shown at idle (the gunviscmds and the part
 *   visibility hide only MAGAZINE2, the hand clip).
 * - In matrix 39's frame the clip is a flat box x -0.6..0.6, z -3.2..3.4, running 12.7 cm down from
 *   the joint (y 0 to -12.7), so the joint is the top of the clip and the insert axis is its +y,
 *   {0, 1, 0}, the body's up.
 * - The insert depth is 11.4 cm: at the hold frame (65) the hand clip's top is 11.4 below the
 *   seated joint along the axis, 0.2 off it, its axis 0.6 degrees from the well's, and the
 *   animation pushes it straight in from there (frames 65 to 73, f5hold.py). Seated, its base hangs
 *   below the body (y -8.6). No body vertex is inside the seated clip (f5well.py) and the body's
 *   sides enclose its top corners (f5poke.py): full width (clipwidth 1).
 * - No base cap: part 0x42 (a toggle bgun0f098030 hides on every frame for every gun) has no vertex on
 *   matrix 39 (f5geo.py), so
 *   vrReloadCollapseClip draws nothing differently for this gun.
 * - The rack is a knob: matrix 34, a position node on the body at {0, 6.33, 14.81} with no part id
 *   (so the engine's slide offset cannot reach it) and no matrix under it, drawn by 14 vertices of
 *   the body's display list, x -0.6..0.6, y 0..2.3, z -0.5..0.6 in its own frame, not turned from
 *   the body. After the insert the animation's left hand comes over the top, the index and middle
 *   fingertips close on the knob (0.7 to 1.4 cm from its vertices, frames 106 to 116) and slide it
 *   5.7 cm back along the body's -z (frames 106 to 116; it springs forward in 122 to 126), with the
 *   rack sound at 112 (f5bolt.py, f5trace.py). So the row racks on it (rackmtx 34): rackoffset
 *   {0, 1.2, 1.0} between the two fingertips (for the logs), racktravel 5.7, the knob's travel, and
 *   rackmove 1: the knob is drawn pulled back with the hand and goes forward after
 *   (vrReloadMoveRack), as a slide does. The knob sits on top of the receiver, about 33 cm in front
 *   of the clip.
 * - The rack box (Task F8b, Stage F run 2): the user reaches for the rear of the receiver's top, not
 *   the knob. Five grab points marked in the headset ("mark weapon=13", the drawn hand where a grab
 *   should rack, in the knob's frame) were {2.89, -0.90, -11.04}, {1.30, 5.03, -8.90}, {-1.45, 3.57,
 *   -10.83}, {-3.87, -0.84, -8.32} and {-6.15, -1.28, -2.41}: 8 to 11 cm behind the knob, up to 6 cm
 *   to either side, 7.5 to 12.6 cm from it (the knob's own box with rackrad 6, Task F7b, reached
 *   none). The box is x -5..5 (symmetric, so the left-handed mirror covers the marks mirrored), y
 *   -1..5.5, z -11.5..0.6 (the knob's front), rackrad 3: the first four marks are inside, the fifth
 *   1.2 cm out (1.8 to spare). A box, not a reach as the clip guide's: the marks spread over the
 *   receiver's top, which a box covers with the existing test, no new field. It stays above the
 *   support hand: the shoot animation's left hand (ANIM_GUN_RCP120_SHOOT 0x424, frame 0;
 *   tools/vr_measure/rcprack.py) grips under the forend, its finger joints at y -6.6 to -8.7 and z
 *   1 to 7 in the knob's frame, 3.0 cm or more past rackrad from the box (1.9 from the old knob
 *   box). The receiver's top behind the knob (the sight, y up to 7.1) is within rackrad of it; the
 *   pistol grip, z -12 to -25 and below y -7.9, is not. The knob's drawn motion is unchanged.
 * - The box's sides (Task H7a, run 16): two more grab points marked in the headset, {8.30, 1.42,
 *   -6.54} and {8.07, 0.73, -13.82} in the knob's frame (rackdist 11.2 and 16.9), are where a rack
 *   must not start (the user: "too close to the grip to allow for racking"); with x -5..5 they were
 *   0.30 and 0.85 cm past rackrad. The box is now x -4.2..4.2 (rackrad and the rest as above): the
 *   two marks and their x mirrors are 1.10 and 1.51 cm past rackrad, and F8b's five marks still rack,
 *   the first four inside the box, the fifth 2.0 cm out (1.0 to spare, 1.8 before), mirrored alike;
 *   the two sets sit only 2.15 cm apart in x, so the box's side splits the difference
 *   (tools/vr_measure/rcprack.py).
 * - The clip in the off-hand (MODELPART_RCP120_MAGAZINE2, 0x29) is a second toggle whose 20 vertices
 *   are all skinned to matrix 40, a position node under the left wrist (matrix 18), and are the gun
 *   clip's vertices in the same local frame within 0.5 mm (its box is x -0.5..0.6): a rigid fit of
 *   the 39 set onto the 40 set leaves 0.5 mm with no turn and the gun clip's joint at {0.05, 0, 0}
 *   (f5hold.py). heldtop is the joint, {0, 0, 0}, heldaxis +y, {0, 1, 0}.
 * - The hold frame is 65: the hand clip is fixed to the wrist through the approach (frames 55 to 65)
 *   and at 65 it is lined up with the well (above); the script shows it at 50 and swaps it for the
 *   gun clip at 75.
 * - The hand turn: 165.2 degrees about {-0.6412, 0.6488, 0.4097} in the model root's frame
 *   (f5turn.py, handturn.py's method) puts the frame-65 wrist where the Falcon's frame-18 wrist is, as
 *   the AR34's (the rifles' animations hold the left wrist alike). The held clip's top, 19.6 cm from
 *   the wrist, moves 36.9 cm with it, and with the two hands held parallel the held clip is then 85
 *   degrees off the insert axis (53 without the turn); the turn that lines it up with the well instead
 *   (53.2 degrees) leaves the wrist 136 degrees from the Falcon's.
 * - The clip turn (clipturnaxis, clipturndeg, clipturnpivot; Task F7e, Stage F run 1: the held clip
 *   turned wrong in the hand): after the hand turn, 138.9 degrees about {0.4959, -0.1209, -0.8599}
 *   in the model root's frame (clipturn.py) turns the held clip alone about the palm, {-2.172,
 *   -12.599, 0.022} in matrix 40's frame, so it sits in the drawn hand as the Falcon 2's clip sits
 *   in its hand at frame 18: its insert axis and its front (the gun's forward) in the wrist's frame
 *   as the Falcon clip's (53 and 112 degrees off before); its top moves 12.8 cm. The guide still
 *   takes any angle; no reach points to recheck.
 * - The clip shift (clipshift; Task F8e, Stage F run 2: the rifles' held clips sat on the
 *   knuckles, not in the palm): after the clip turn, {-5.439, 1.034, -1.386} cm in matrix 40's
 *   frame as turned (clipshift.py) moves the held clip alone 5.7 cm, so its grip point (the
 *   middle of its length and across, on its palm side) sits at the Falcon 2's clip's offset from
 *   the palm (the middle of the hand bone and its four knuckles) at frame 18; before it, the
 *   grip point sat 5.6 cm too far toward the palm side (the clip level with the hand, beside its
 *   knuckles), 1.0 along the insert axis and 0.8 across. Its top, so the guide's start, moves
 *   with it (vrReloadCaptureHeldClip reads the matrix after the shift).
 * - No angle test (clipalignmax 180, VRR_GUIDE_ANY_ANGLE_DEG), as the AR34's and for the same reason.
 * - Sounds as the ROM's reload script plays them (invitems.c): eject SFX_0053 (83) at frame 18,
 *   insert SFX_05C5 (1477, a sound id) at 74 as the clip seats, rack SFX_0431 (1073, a sound id, the
 *   CMP150's rack) at 112 as the knob reaches the back. The script plays them at 1.1x; the row plays
 *   them at normal speed.
 * - The draw: the RC-P120's equip animation is its shoot script, not the reload.
 * - The fall tumbles the clip about a point 5.7 cm below its top (half the insert depth).
 * - Both functions, rapid fire and the cloak, draw on the one clip (ammoindex 0), which the row's
 *   primary reads; slidemax 0.
 * - invitems.c: 120 rounds a clip, AMMOTYPE_SMG, no WEAPONFLAG_DUALWIELD (single wield only);
 *   VrTwoHandsGun (the free hand's grip supports it, except mid-reload: from B until the rack,
 *   vrReloadMidReload, Task F9a). */
#define RCP120_ROW(weapon) { \
	(weapon), 0, \
	6.0f, 180.0f, 11.4f, { 0.0f, 1.0f, 0.0f }, 1.0f, 0.0f, \
	0, { { 0.0f } }, \
	34, 3.0f, { -4.2f, -1.0f, -11.5f }, { 4.2f, 5.5f, 0.6f }, { 0.0f, 1.2f, 1.0f }, 5.7f, 0, 0, 0, 0, \
	MODELPART_RCP120_MAGAZINE1, 39, -1, { 0.0f, 0.0f, 0.0f }, -1, 65.0f, \
	{ -0.6412f, 0.6488f, 0.4097f }, 165.2f, \
	MODELPART_RCP120_MAGAZINE2, 40, -1, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, \
	83, 1477, 1073, 0, 0.0f, \
	0.0f, 0.0f, 0.0f, -1, -1, \
	0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 0.0f, \
	1, -1, 0.0f, \
	0.0f, 0.0f, -1, 0, { 0.0f, 0.0f, 0.0f }, { { 0.0f } }, 0.0f, 0.0f, -1, -1, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, 0, \
	0, 0, 0, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, \
	{ 0.4959f, -0.1209f, -0.8599f }, 138.9f, { -2.172f, -12.599f, 0.022f }, \
	{ -5.439f, 1.034f, -1.386f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, \
	"insert magazine, pull the cocking knob", \
}

/* Dragon model (GdydragonZ), read from the ntsc-final ROM's model data and reload animation
 * (ANIM_GUN_DRAGON_RELOAD 0x3fa, 108 frames) with tools/vr_measure (as the RC-P120's row, whose
 * scripts measure all four guns):
 * - The model has 41 matrices, the left hand 17 to 32 as the pistols', the body 33. The part names
 *   are swapped from the other rifles': the clip in the gun is MODELPART_DRAGON_MAGAZINE2 (0x2a), a
 *   toggle whose 46 vertices are all skinned to matrix 39, a position node under the body, not turned
 *   from it in the rest pose, seated with its joint at {0, 1.56, 13.44} cm in the body's frame, and
 *   shown at idle; the hand clip is MAGAZINE1 (0x28), which the gunviscmds and the part visibility
 *   hide.
 * - The clip is curved: in matrix 39's frame it runs from the joint's level (y 1.6) down 14.9 cm (to
 *   y -13.3), its base bowed forward (z -5.1..9.3). The insert axis is its +y at the top, {0, 1, 0},
 *   the body's up: the animation rocks it in (frames 34 to 39, from 65 to 0.3 degrees off) and then
 *   pushes it straight (f5hold.py).
 * - The insert depth is 3.3 cm, a length of travel: at the hold frame (39) the hand clip's top is 3.3
 *   below the seated joint along the axis, 1.5 off it, its axis 0.3 degrees from the well's, and it
 *   seats at 41. No body vertex is inside the seated clip (f5well.py) and the body's sides enclose its
 *   top (f5poke.py): full width (clipwidth 1).
 * - Part 0x42 (MODELPART_DRAGON_0042), a toggle, holds the clip's base cap, 14 vertices on matrix 39,
 *   with 129 on the body (f5geo.py), as the K7's; bgun0f098030 hides it on every frame for every gun,
 *   so it is never drawn. Manual reload moves matrix 39 with the drawn clip and collapses it while no
 *   clip is drawn in the gun (vrReloadCollapseClip, which reads clipmtx), which is harmless for the
 *   hidden part.
 * - No rack: after the insert the animation's hand goes away, no part of the gun moves and no sound
 *   plays after the seat (f5trace.py; the script's last sound is the insert). So the row loads on
 *   insert (loadsoninsert 1, B empties it, E2e), with the rack fields zero and rackmtx -1.
 * - The clip in the off-hand (MAGAZINE1, 0x28) is skinned to matrix 40, a position node under the
 *   left wrist (matrix 18), and is the gun clip's vertices in the same local frame (identical,
 *   f5hold.py): heldtop is the joint, {0, 0, 0}, heldaxis +y, {0, 1, 0}.
 * - The hold frame is 39 (above): the script shows the hand clip at 17 and swaps it for the gun clip
 *   at 41.
 * - The hand turn: 89.0 degrees about {-0.1296, 0.6937, 0.7085} in the model root's frame (f5turn.py)
 *   puts the frame-39 wrist where the Falcon's frame-18 wrist is. The held clip's top, 20.4 cm from
 *   the wrist, moves 27.7 cm with it, and with the two hands held parallel the held clip is then 78
 *   degrees off the insert axis. Unturned it would be 5 degrees off, but the hand would sit 89
 *   degrees turned from the controller (the coordinator's decision, 2026-10-19: the hands line up
 *   with the controller, as every other row).
 * - The clip turn (clipturnaxis, clipturndeg, clipturnpivot; Task F7e, Stage F run 1: the held clip
 *   turned wrong in the hand): after the hand turn, 91.9 degrees about {0.3932, -0.1619, -0.9051}
 *   in the model root's frame (clipturn.py) turns the held clip alone about the palm, {-1.828,
 *   -13.752, 3.974} in matrix 40's frame, so it sits in the drawn hand as the Falcon 2's clip sits
 *   in its hand at frame 18: its insert axis and its front (the gun's forward) in the wrist's frame
 *   as the Falcon clip's (49 and 73 degrees off before); its top moves 16.4 cm. The guide still
 *   takes any angle; no reach points to recheck.
 * - The clip shift (clipshift; Task F8e, Stage F run 2: the rifles' held clips sat on the
 *   knuckles, not in the palm): after the clip turn, {-6.384, -0.618, 0.388} cm in matrix 40's
 *   frame as turned (clipshift.py) moves the held clip alone 6.4 cm, so its grip point (the
 *   middle of its length and across, on its palm side) sits at the Falcon 2's clip's offset from
 *   the palm (the middle of the hand bone and its four knuckles) at frame 18; before it, the
 *   grip point sat 6.3 cm too far toward the palm side (the clip level with the hand, beside its
 *   knuckles), 0.6 along the insert axis and 1.1 across. Its top, so the guide's start, moves
 *   with it (vrReloadCaptureHeldClip reads the matrix after the shift).
 * - No angle test (clipalignmax 180, VRR_GUIDE_ANY_ANGLE_DEG), as the AR34's: with the hand lined up
 *   the clip is held about 78 degrees off the well, and the guided clip is the gun's own clip moved
 *   back down the insert axis, upright under the receiver.
 * - Sounds as the ROM's reload script plays them (invitems.c): eject SFX_0053 (83) at frame 8, insert
 *   SFX_05C5 (1477) at 41 as the clip seats, both at normal speed; no rack sound (sndrack 0).
 * - The draw: the Dragon's equip animation is its shoot script, not the reload.
 * - The fall tumbles the clip about a point 1.65 cm below its top (half the insert depth).
 * - The second function, the proximity self-destruct, has no ammo (ammoindex -1: it throws the gun),
 *   so it goes with the row (vrReloadOwnsAmmo); the rapid fire reads the clip (ammoindex 0).
 * - invitems.c: 30 rounds a clip, AMMOTYPE_RIFLE, no WEAPONFLAG_DUALWIELD (single wield only);
 *   VrTwoHandsGun. */
#define DRAGON_ROW(weapon) { \
	(weapon), 0, \
	6.0f, 180.0f, 3.3f, { 0.0f, 1.0f, 0.0f }, 1.0f, 0.0f, \
	0, { { 0.0f } }, \
	-1, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 1, 0, 0, 0, \
	MODELPART_DRAGON_MAGAZINE2, 39, -1, { 0.0f, 0.0f, 0.0f }, -1, 39.0f, \
	{ -0.1296f, 0.6937f, 0.7085f }, 89.0f, \
	MODELPART_DRAGON_MAGAZINE1, 40, -1, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, \
	83, 1477, 0, 0, 0.0f, \
	0.0f, 0.0f, 0.0f, -1, -1, \
	0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 0.0f, \
	0, -1, 0.0f, \
	0.0f, 0.0f, -1, 0, { 0.0f, 0.0f, 0.0f }, { { 0.0f } }, 0.0f, 0.0f, -1, -1, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, 0, \
	0, 0, 0, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, \
	{ 0.3932f, -0.1619f, -0.9051f }, 91.9f, { -1.828f, -13.752f, 3.974f }, \
	{ -6.384f, -0.618f, 0.388f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, \
	"insert magazine", \
}

/* SuperDragon model (GdysuperdragonZ), read from the ntsc-final ROM's model data and reload
 * animation (ANIM_GUN_SUPERDRAGON_RELOAD_PRI 0x3fb, 108 frames, the rifle's reload) with
 * tools/vr_measure (as the RC-P120's row). The rifle is the Dragon's (the same clip, hand clip, hold
 * frame, sounds and, within 0.2 degrees, hand turn); what differs:
 * - The model has 42 matrices. The rifle clip in the gun is MODELPART_SUPERDRAGON_MAGAZINE3 (0x2a) on
 *   matrix 38, seated at {0, 1.56, 13.44} in the body's frame as the Dragon's; the hand clip is
 *   MAGAZINE1 (0x28) on matrix 41 (identical vertices, heldtop {0, 0, 0}, heldaxis {0, 1, 0}). The
 *   gunviscmds hide MAGAZINE1 and MAGAZINE2 (the two hand clips).
 * - The grenade magazine has a row of its own (SUPERDRAGON_GRENADE_ROW, ammoindex 1, Task F9e; the
 *   engine's until then, Task F5): MAGAZINE4 (0x2b) on matrix 39 in the gun, seated 12.6 cm in front of
 *   the rifle clip, and MAGAZINE2 (0x29) on matrix 40 in the hand. This row is the rifle's, ammoindex 0.
 *   Each magazine keeps its own state whichever function is selected: B, the shoulder grab and the
 *   guide act on the selected function's, and the other is drawn in or out as it was left
 *   (vrReloadParkedDef).
 * - Part 0x42 holds both base caps: 14 vertices on matrix 38 (the rifle clip's) and 14 on 39 (the
 *   grenade magazine's), with 124 on the body (f5geo.py). vrReloadCollapseClip collapses each row's
 *   clipmtx while that magazine is out (the driven one's and the parked one's), which is harmless:
 *   bgun0f098030 hides part 0x42 on every frame for every gun, so the caps are never drawn.
 * - The hold frame is 39 (the hand clip 3.3 cm below the seated joint, 1.5 off, 0.3 degrees: the
 *   insert depth 3.3, f5hold.py); the script shows the hand clip at 16 and swaps it at 43. The
 *   hand turn: 88.9 degrees about {-0.1277, 0.6945, 0.7081} (f5turn.py), the held clip then 78
 *   degrees off the well with the hands parallel (5 unturned): no angle test (clipalignmax 180).
 * - The clip turn (clipturnaxis, clipturndeg, clipturnpivot; Task F7e, Stage F run 1: the held clip
 *   turned wrong in the hand): after the hand turn, 91.9 degrees about {0.3935, -0.1621, -0.9049}
 *   in the model root's frame (clipturn.py) turns the held clip alone about the palm, {-1.828,
 *   -13.752, 3.974} in matrix 41's frame, so it sits in the drawn hand as the Falcon 2's clip sits
 *   in its hand at frame 18: its insert axis and its front (the gun's forward) in the wrist's frame
 *   as the Falcon clip's (49 and 73 degrees off before); its top moves 16.4 cm. The guide still
 *   takes any angle; no reach points to recheck.
 * - The clip shift (clipshift; Task F8e, Stage F run 2: the rifles' held clips sat on the
 *   knuckles, not in the palm): after the clip turn, {-6.384, -0.618, 0.388} cm in matrix 41's
 *   frame as turned (clipshift.py) moves the held clip alone 6.4 cm, so its grip point (the
 *   middle of its length and across, on its palm side) sits at the Falcon 2's clip's offset from
 *   the palm (the middle of the hand bone and its four knuckles) at frame 18; before it, the
 *   grip point sat 6.3 cm too far toward the palm side (the clip level with the hand, beside its
 *   knuckles), 0.6 along the insert axis and 1.1 across. Its top, so the guide's start, moves
 *   with it (vrReloadCaptureHeldClip reads the matrix after the shift).
 * - No rack (as the Dragon's): loadsoninsert 1. Sounds: eject SFX_0053 (83) at frame 8, insert
 *   SFX_05C5 (1477) at 41; the equip animation is the shoot script.
 * - invitems.c: 30 rounds a clip, AMMOTYPE_RIFLE, no WEAPONFLAG_DUALWIELD; VrTwoHandsGun. */
#define SUPERDRAGON_ROW(weapon) { \
	(weapon), 0, \
	6.0f, 180.0f, 3.3f, { 0.0f, 1.0f, 0.0f }, 1.0f, 0.0f, \
	0, { { 0.0f } }, \
	-1, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 1, 0, 0, 0, \
	MODELPART_SUPERDRAGON_MAGAZINE3, 38, -1, { 0.0f, 0.0f, 0.0f }, -1, 39.0f, \
	{ -0.1277f, 0.6945f, 0.7081f }, 88.9f, \
	MODELPART_SUPERDRAGON_MAGAZINE1, 41, -1, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, \
	83, 1477, 0, 0, 0.0f, \
	0.0f, 0.0f, 0.0f, -1, -1, \
	0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 0.0f, \
	0, -1, 0.0f, \
	0.0f, 0.0f, -1, 0, { 0.0f, 0.0f, 0.0f }, { { 0.0f } }, 0.0f, 0.0f, -1, -1, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, 0, \
	0, 0, 0, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, \
	{ 0.3935f, -0.1621f, -0.9049f }, 91.9f, { -1.828f, -13.752f, 3.974f }, \
	{ -6.384f, -0.618f, 0.388f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, \
	"insert magazine", \
}

/* SuperDragon grenade launcher (Task F9e; user, 2026-10-19: "the super dragon grenade reload works
 * exactly like the regular reload - its just the second clip. we should be able to make that a manual
 * reload"): the second function's own magazine, ammoindex 1, read from the ntsc-final ROM's model
 * (GdysuperdragonZ) and its reload animation (ANIM_GUN_SUPERDRAGON_RELOAD_SEC 0x3fc, 108 frames;
 * invanim_superdragon_grenadereload) with tools/vr_measure, by the rifle row's method:
 * - The magazine in the gun is MODELPART_SUPERDRAGON_MAGAZINE4 (0x2b), a toggle whose 46 vertices are
 *   all skinned to matrix 39, a position node under the body, not turned from it in the rest pose,
 *   seated with its joint at {0, 1.52, 26.07} cm in the body's frame (12.6 in front of the rifle
 *   clip's), shown at idle (the gunviscmds hide only MAGAZINE1 and MAGAZINE2). In matrix 39's frame it
 *   runs from the joint (y 1.7) down 11.3 cm, its base bowed forward (z -4.0..7.1) (f5geo.py).
 * - The magazine in the off-hand is MAGAZINE2 (0x29), skinned to matrix 40 under the left wrist, with
 *   the gun magazine's vertices in the same local frame (identical, f5hold.py): heldtop the joint, {0,
 *   0, 0}, heldaxis +y, {0, 1, 0}. The script shows it at 30 and swaps it for the gun's at 64.
 * - The insert axis is the seated magazine's +y, {0, 1, 0}: the animation rocks it in (frames 56 to 61,
 *   from 68 to 1.5 degrees off) and then pushes it straight. The hold frame is 61: the hand magazine's
 *   top is 2.5 cm below the seated joint along the axis, 1.4 off it, its axis 1.5 degrees from the
 *   well's, and it seats at 63 (f5hold.py), so the insert depth is 2.5.
 * - Part 0x42 holds its base cap, 14 vertices on matrix 39 (f5geo.py): vrReloadCollapseClip collapses
 *   matrix 39 while this magazine is out, whichever function is selected, which is harmless: the part
 *   is hidden on every frame (bgun0f098030), so the cap is never drawn.
 * - No rack: after the seat the hand goes away and no part moves (f5trace.py SDgren), the script's
 *   last sound is the insert. So the row loads on insert (loadsoninsert 1, B empties it, E2e), as the
 *   rifle's, with the rack fields zero and rackmtx -1.
 * - Sounds as the script plays them: eject SFX_0053 (83) at frame 6, insert SFX_05C5 (1477) at 66,
 *   played at normal speed (the script sets the sound speed to 700 and 850; a row has no speed).
 * - The hand turn: 99.3 degrees about {-0.2130, 0.2963, 0.9310} in the model root's frame (f5turn.py)
 *   puts the frame-61 wrist where the Falcon's frame-18 wrist is; the held magazine's top, 15.4 cm
 *   from the wrist, moves 20.3 cm with it, and with the hands parallel it is then 99 degrees off the
 *   well (10 unturned): no angle test (clipalignmax 180, VRR_GUIDE_ANY_ANGLE_DEG), as the rifle's.
 * - The clip turn (as the rifle's, Task F7e): after the hand turn, 109.4 degrees about {0.2076,
 *   0.0462, -0.9771} in the model root's frame (clipturn.py) turns the held magazine alone about the
 *   palm, {-2.070, -10.034, 1.844} in matrix 40's frame (the middle of the hand bone's vertices at the
 *   hold frame, as the other rows' pivots), so its insert axis and front sit in the wrist's frame as
 *   the Falcon 2's frame-18 clip's (74 and 75 degrees off before); its top moves 15.0 cm.
 * - The clip shift (as the rifle's, Task F8e): after the clip turn, {-6.652, 1.198, -1.191} cm in
 *   matrix 40's frame as turned (clipshift.py) moves the held magazine alone 6.9 cm, so its grip point
 *   sits at the Falcon 2's clip's offset from the palm (before it, 6.7 cm too far toward the palm
 *   side, 1.2 along the insert axis and 0.5 across).
 * - The fall tumbles the magazine about a point 1.25 cm below its top (half the insert depth).
 * - invitems.c: 6 grenades a magazine, AMMOTYPE_DEVASTATOR, CASING_NONE; the SuperDragon does not dual
 *   wield, so no pair carries two magazines' states. */
#define SUPERDRAGON_GRENADE_ROW(weapon) { \
	(weapon), 1, \
	6.0f, 180.0f, 2.5f, { 0.0f, 1.0f, 0.0f }, 1.0f, 0.0f, \
	0, { { 0.0f } }, \
	-1, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 1, 0, 0, 0, \
	MODELPART_SUPERDRAGON_MAGAZINE4, 39, -1, { 0.0f, 0.0f, 0.0f }, -1, 61.0f, \
	{ -0.2130f, 0.2963f, 0.9310f }, 99.3f, \
	MODELPART_SUPERDRAGON_MAGAZINE2, 40, -1, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, \
	83, 1477, 0, 0, 0.0f, \
	0.0f, 0.0f, 0.0f, -1, -1, \
	0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 0.0f, \
	0, -1, 0.0f, \
	0.0f, 0.0f, -1, 0, { 0.0f, 0.0f, 0.0f }, { { 0.0f } }, 0.0f, 0.0f, -1, -1, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, 0, \
	0, 0, 0, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, \
	{ 0.2076f, 0.0462f, -0.9771f }, 109.4f, { -2.070f, -10.034f, 1.844f }, \
	{ -6.652f, 1.198f, -1.191f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, \
	"insert grenade magazine", \
}

/* Sniper Rifle model (GsniperrifleZ), read from the ntsc-final ROM's model data and reload animation
 * (ANIM_GUN_SNIPER_RELOAD 0x40f, 158 frames) with tools/vr_measure (as the RC-P120's row):
 * - The model has 42 matrices, the left hand 17 to 32 as the pistols', the body 33. The clip
 *   (MODELPART_SNIPERRIFLE_MAGAZINE1, 0x28) is a toggle whose 22 vertices are all skinned to matrix
 *   40, a position node under the body, not turned from it in the rest pose, seated with its joint at
 *   {-0.02, 7.66, -17.97} cm in the body's frame, and shown at idle (the gunviscmds hide only 0x29,
 *   the hand clip).
 * - In matrix 40's frame the clip is a box x -0.9..1.0, z -5.6..5.3, running 13.3 cm down from the
 *   joint (y 0 to -13.3): the joint is the top of the clip and the insert axis its +y, {0, 1, 0}.
 * - The insert depth is 11.5 cm: at the hold frame (65) the hand clip's top is 11.5 below the seated
 *   joint along the axis, 2.3 off it, its axis 0.5 degrees from the well's, and the animation pushes
 *   it in to seated at frame 70 (f5hold.py). Seated, its base is 5.6 cm below the body's origin.
 *   No body vertex is inside the seated clip (f5well.py) and the body's sides enclose its top corners
 *   (f5poke.py): full width (clipwidth 1).
 * - No base cap: part 0x42 has no vertex on matrix 40 (f5geo.py).
 * - No rack: after the insert the animation's hand goes away forward along the gun (frames 72 to 106),
 *   no part of the gun moves and no sound plays after the seat (f5trace.py). So the row loads on insert
 *   (loadsoninsert 1, B empties it, E2e), with the rack fields zero and rackmtx -1.
 * - The clip in the off-hand (MODELPART_SNIPERRIFLE_MAGAZINE2, 0x29) is skinned to matrix 41, a
 *   position node under the left wrist (matrix 18), and is the gun clip's vertices in the same local
 *   frame (identical, f5hold.py): heldtop {0, 0, 0}, heldaxis {0, 1, 0}. The script shows it at 42
 *   (hiding the gun clip) and swaps it back at 72.
 * - The hand turn: 163.5 degrees about {-0.5698, 0.7175, 0.4007} in the model root's frame
 *   (f5turn.py) puts the frame-65 wrist where the Falcon's frame-18 wrist is. The held clip's top,
 *   20.3 cm from the wrist, moves 39.5 cm with it, and with the two hands held parallel the held clip
 *   is then 87 degrees off the insert axis (59 without the turn; the turn lining it up instead, 59.0
 *   degrees, leaves the wrist 127 degrees from the Falcon's).
 * - The clip turn (clipturnaxis, clipturndeg, clipturnpivot; Task F7e, Stage F run 1: the held clip
 *   turned wrong in the hand): after the hand turn, 135.4 degrees about {0.4711, -0.1176, -0.8742}
 *   in the model root's frame (clipturn.py) turns the held clip alone about the palm, {-1.471,
 *   -13.226, 2.194} in matrix 41's frame, so it sits in the drawn hand as the Falcon 2's clip sits
 *   in its hand at frame 18: its insert axis and its front (the gun's forward) in the wrist's frame
 *   as the Falcon clip's (55 and 108 degrees off before); its top moves 16.4 cm. The guide still
 *   takes any angle; no reach points to recheck.
 * - The clip shift (clipshift; Task F8e, Stage F run 2: the rifles' held clips sat on the
 *   knuckles, not in the palm): after the clip turn, {-5.356, 0.707, 0.970} cm in matrix 41's
 *   frame as turned (clipshift.py) moves the held clip alone 5.5 cm, so its grip point (the
 *   middle of its length and across, on its palm side) sits at the Falcon 2's clip's offset from
 *   the palm (the middle of the hand bone and its four knuckles) at frame 18; before it, the
 *   grip point sat 5.2 cm too far toward the palm side (the clip level with the hand, beside its
 *   knuckles), 0.7 along the insert axis and 1.5 across. Its top, so the guide's start, moves
 *   with it (vrReloadCaptureHeldClip reads the matrix after the shift).
 * - No angle test (clipalignmax 180, VRR_GUIDE_ANY_ANGLE_DEG), as the AR34's.
 * - Sounds as the ROM's reload script plays them (invitems.c): eject SFX_01D8 (472) at frame 22 and
 *   insert SFX_80F6 (33014, a config id: sound 473, as the CMP150's) at 72, both at 0.85x; the row
 *   plays them at normal speed. No rack sound.
 * - The draw: the Sniper Rifle has its own equip animation (ANIM_GUN_SNIPER_EQUIP).
 * - The fall tumbles the clip about a point 5.75 cm below its top (half the insert depth).
 * - The second function, crouch, has no ammo (ammoindex -1), so it goes with the row.
 * - invitems.c: 8 rounds a clip, AMMOTYPE_RIFLE, no WEAPONFLAG_DUALWIELD; VrTwoHandsGun. */
#define SNIPERRIFLE_ROW(weapon) { \
	(weapon), 0, \
	6.0f, 180.0f, 11.5f, { 0.0f, 1.0f, 0.0f }, 1.0f, 0.0f, \
	0, { { 0.0f } }, \
	-1, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 1, 0, 0, 0, \
	MODELPART_SNIPERRIFLE_MAGAZINE1, 40, -1, { 0.0f, 0.0f, 0.0f }, -1, 65.0f, \
	{ -0.5698f, 0.7175f, 0.4007f }, 163.5f, \
	MODELPART_SNIPERRIFLE_MAGAZINE2, 41, -1, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, \
	472, 33014, 0, 0, 0.0f, \
	0.0f, 0.0f, 0.0f, -1, -1, \
	0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 0.0f, \
	0, -1, 0.0f, \
	0.0f, 0.0f, -1, 0, { 0.0f, 0.0f, 0.0f }, { { 0.0f } }, 0.0f, 0.0f, -1, -1, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, 0, \
	0, 0, 0, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, \
	{ 0.4711f, -0.1176f, -0.8742f }, 135.4f, { -1.471f, -13.226f, 2.194f }, \
	{ -5.356f, 0.707f, 0.970f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, \
	"insert magazine", \
}

/* Callisto model (GmaiansmgZ), read from the ntsc-final ROM's model data and reload animation
 * (ANIM_GUN_CALLISTO_RELOAD 0x418, 138 frames) with tools/vr_measure (f6geo.py, f6callisto.py,
 * f6move.py, f6turn.py; Task F6, 2026-10-19). The user's "two-handed Phoenix": the same goo orb, so
 * the Phoenix's row shape (an intake row with a playback):
 * - The model has 43 matrices, the left hand 17 to 32 as the pistols', the body 33. No clip part and
 *   no slide: the orb, MODELPART_CALLISTO_ORB (0x28), is a toggle the gunviscmds and the part
 *   visibility hide, which the reload script shows at frame 30 and hides at 79. So the row has no
 *   clip part (clippart and clipmtx -1): nothing is drawn in the gun, loaded or empty; B empties it
 *   with nothing falling, and the empty gun waits for an orb.
 * - The orb's 116 vertices are skinned to three matrices: 40 (a flat disc, 23 vertices, 11 cm long
 *   along its z), a position node under the left wrist (matrix 18), and 41 and 42 under it (48 and
 *   45), a blob about 16 x 11 x 3 cm. All three stay drawn with the off-hand (heldmtx 40,
 *   heldmtxlast 42).
 * - The intake point (intakemtx 33, the gun body): matrix 40's joint arrives at {2.1, -8.1, 14.9} cm
 *   in the body's frame at frame 58 (f6callisto.py: {2.11, -8.14, 14.93}), under the gun on its +x
 *   side, where the two lower flaps (matrices 37 and 38, x 1.9..3.1 and -3.1..-1.9, z 8.2..16.4) open.
 *   The orb loads as matrix 40's joint (heldtop {0, 0, 0}) comes within clipguiderad (6 cm, the other
 *   rows' value) of it, from any side and at any angle, as the Phoenix's (no clipaxis, heldaxis,
 *   insert depth or reach). Mirrored with the model in left-handed mode through the body matrix.
 * - After the load the gun plays the orb going in (playfrom 58 to playto 96): the orb sits at the
 *   intake (it moves up to 1.8 cm on the wrist to frame 69, so the hand holds it there, then the hand
 *   pulls away from it) and is hidden at 79 (playheldto, the script's hidepart); the lower flaps swing open and back (37 up to
 *   5.4 cm and 36 degrees, frames 60 to 87; 38 up to 3.1 cm and 22 degrees, 60 to 80) and the top
 *   plate 35 slides out up to 3.3 cm (68 to 94) (f6move.py, f6callisto.py). Frame 96 is the first with
 *   every one of them back on its rest pose. Matrices 36 and 39 (the other top plate and a part on
 *   top of the body) do not move in the reload; they are in the run (playmtx 35 to playmtxlast 42) as the run
 *   must hold the orb's 40 to 42, and are drawn where they are. 38 frames at speed 1: 0.63 s of game
 *   time. The left hand bringing the orb in (frames 30 to 57) and pulling away is not played.
 * - The orb is fixed to the wrist through the approach (within 1.1 cm, frames 0 to 58), so the hold
 *   frame only sets the wrist's turn from the model root: frame 58, where the orb reaches the intake.
 *   On the wrist the orb's middle is 16 cm from it, held in the palm as the Phoenix's.
 * - The hand turn: 145.5 degrees about {-0.3166, 0.5468, 0.7751} in the model root's frame (f6turn.py,
 *   handturn.py's method) puts the frame-58 wrist where the Falcon's frame-18 wrist is, so the hand
 *   sits on the off controller as the Falcon's does. The orb turns with the hand and stays in the
 *   palm: its middle moves 24.2 cm on the controller. The intake test has no angle, so only where the
 *   orb sits matters.
 * - In the gun model the orb hangs off the idle left wrist: bgun0f0a5550 hides an owned row's held
 *   part except while the playback above poses it at the intake.
 * - No slide: the row loads on insert (loadsoninsert 1), so an orb put into an empty gun chambers a
 *   round at once, and the rack fields are zero (rackmtx -1). B empties it fully (E2e).
 * - Sounds: the ROM's reload script plays only SFX_RELOAD_FARSIGHT (1075, the Phoenix's load) at frame
 *   58 as the orb arrives, at 0.93x speed: the row's insert, at normal speed, played as the load
 *   starts the playback at 58. The script has no eject sound: B plays the Phoenix's, SFX_05C7 (1479,
 *   the Reaper's magazine release; see the Phoenix row for its runners-up). No rack sound.
 * - The two-hand grip: the Callisto is a two-handed gun (VrTwoHandsGun). A free hand holding an orb
 *   is not the support hand (bondgun.c: vrReloadFreeHandBusy latches VrTwoHandReloadGrip, which
 *   holds the grip off the gun until it is let go, so the grip still held after the load does not
 *   turn into a support grip). A hand gripping the gun as support is not at the shoulder, and the
 *   shoulder takes an orb only on a new squeeze there (the core's grip edge).
 * - The draw: the Callisto's equip animation is this reload script (invitems.c, WEAPONFLAG_00004000,
 *   as the AR34's): the holsters' fast switch holds frame 0, and bgun0f0a5550 sets the orb from the
 *   reload state every frame.
 * - Both functions, rapid fire and the high impact shells, fire from the one orb (ammoindex 0);
 *   slidemax 0.
 * - invitems.c: 32 rounds an orb, AMMOTYPE_SMG, no WEAPONFLAG_DUALWIELD (single wield only). */
#define CALLISTO_ROW(weapon) { \
	(weapon), 0, \
	6.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f, \
	0, { { 0.0f } }, \
	-1, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 1, 0, 0, 0, \
	-1, -1, 33, { 2.1f, -8.1f, 14.9f }, -1, 58.0f, \
	{ -0.3166f, 0.5468f, 0.7751f }, 145.5f, \
	MODELPART_CALLISTO_ORB, 40, 42, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, \
	1479, 1075, 0, 0, 0.0f, \
	58.0f, 96.0f, 79.0f, 35, 42, \
	0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 0.0f, \
	0, -1, 0.0f, \
	0.0f, 0.0f, -1, 0, { 0.0f, 0.0f, 0.0f }, { { 0.0f } }, 0.0f, 0.0f, -1, -1, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, 0, \
	0, 0, 0, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, \
	"press an orb into the side intake", \
}

/* Cyclone model (GcycloneZ), read from the ntsc-final ROM's model data and reload animation
 * (ANIM_GUN_CYCLONE_RELOAD 0x40e, 144 frames, the single-wield reload, which
 * vrGetReloadAnimIdForWeapon picks for a pair too) with tools/vr_measure (f6geo.py, f6cyclone.py,
 * f6move.py, f6turn.py; Task F6, 2026-10-19). An intake row (user, 2026-10-19: place it at the slot,
 * not a swipe):
 * - The model has 38 matrices, the left hand 17 to 32 as the pistols', the body 33. The gun has no
 *   clip of its own: MODELPART_CYCLONE_MAGAZINE (0x28), which the gunviscmds hide, is the bar the
 *   left hand brings, 22 vertices all skinned to matrix 37, a position node under the left wrist
 *   (matrix 18), and nothing of a magazine is in the body mesh. So the row has no clip part (clippart
 *   and clipmtx -1): nothing is drawn in the gun, loaded or empty, and B empties it with nothing
 *   falling.
 * - The bar runs 35.9 cm along matrix 37's +x from its joint (x 0..35.9, y -1.4..1.3, z -3.8..3.9):
 *   the joint is its leading end, and it goes in along -x (heldtop {0, 0, 0}, heldaxis {-1, 0, 0}).
 * - The intake point (intakemtx 33, the gun body): the bar's leading end stops at {1.6, 4.0, -1.8} in
 *   the body's frame in frames 58 to 61 (f6cyclone.py: {1.67, 4.01, -1.86} at 58, {1.61, 3.95, -1.72}
 *   at 60), at the mouth of a slot through the body (no body surface crosses a line along x at y 2.5 to
 *   4.5, z -4 to 0), the bar's +x lying along the body's +x within 8 degrees. The bar loads as its leading end
 *   comes within clipguiderad (6 cm) of the point, at any angle, as the Phoenix's orb (the point test
 *   of an intake row). Mirrored with the model in left-handed mode through the body matrix.
 * - After the load the gun plays the bar going through (playfrom 59 to playto 97, matrix 37 alone,
 *   playmtx and playmtxlast 37): from frame 60 it slides on along the body's -x about 1 cm a frame
 *   through the slot and out the far side, its leading end at x -34.4 at 97, where the left hand
 *   takes it away (from 98 it drops 3 to 4 cm a frame). The bar is drawn in the gun through playto
 *   (playheldto 97), and then falls (playdrop 35.9, its length: it turns about its middle, 18 cm back
 *   from the leading end, sliding on along heldaxis at the fall's 0.5 m/s, near the bar's own 0.6 to
 *   0.8 m/s in frames 94 to 97, and tumbling end over end about its z), as an ejected clip falls, so it is thrown out
 *   rather than vanishing. 38 frames at speed 1: 0.63 s of game time.
 * - The hand-off (playease 6): the playback's first frame puts the bar on the slot line, up to
 *   clipguiderad from where the hand held it and turned however the hand held it; it goes there from
 *   the hand's pose over 6/60 s (vrReloadEaseHandoff) instead of jumping.
 * - The bar is fixed to the wrist within 0.1 cm and 0.5 degrees until frame 59, so the hold frame only
 *   sets the wrist's turn from the model root: frame 58, where the bar reaches the slot. Its leading
 *   end is 28.0 cm from the wrist.
 * - The hand turn: 42.9 degrees about {0.8674, -0.4883, -0.0957} in the model root's frame (f6turn.py)
 *   puts the frame-58 wrist where the Falcon's frame-18 wrist is. The bar's leading end moves 8.3 cm
 *   with it; with the two hands held parallel the bar is then 99 degrees off the slot line (100
 *   unturned), which the point test does not mind: the playback lines it up.
 * - In the gun model the bar hangs off the idle left wrist: bgun0f0a5550 hides an owned row's held
 *   part except while the playback poses it in the slot or it falls.
 * - No slide: the row loads on insert (loadsoninsert 1), and the rack fields are zero (rackmtx -1).
 * - Sounds as the ROM's reload script plays them (invitems.c): only SFX_05D3 (1491, a sound id) at
 *   frame 60 as the bar seats in the slot: the row's insert, at normal speed, played as the load
 *   starts the playback at 59. The script has no eject sound: B plays the Phoenix's and the
 *   Callisto's, SFX_05C7 (1479). No rack sound.
 * - The draw: the Cyclone's equip animation is this reload script (invitems.c, WEAPONFLAG_00004000):
 *   the holsters' fast switch holds frame 0, and bgun0f0a5550 sets the bar from the reload state.
 * - Both functions, rapid fire and the magazine discharge, fire from the one bar (ammoindex 0);
 *   slidemax 0.
 * - invitems.c: 50 rounds a bar, AMMOTYPE_SMG. It has WEAPONFLAG_DUALFLIP but not
 *   WEAPONFLAG_DUALWIELD, so it pairs only with the dual wield all guns cheat (and the ROM has its
 *   dual-wield reload, 0x421, for that); a pair works as the Phoenix's: the right gun is driven, a
 *   gun side flip carries the playback to the other hand (vrReloadSwapHands), where it plays out on
 *   that gun, but the bar of a playback that ends on the left gun is not drawn falling. */
#define CYCLONE_ROW(weapon) { \
	(weapon), 0, \
	6.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f, \
	0, { { 0.0f } }, \
	-1, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 1, 0, 0, 0, \
	-1, -1, 33, { 1.6f, 4.0f, -1.8f }, -1, 58.0f, \
	{ 0.8674f, -0.4883f, -0.0957f }, 42.9f, \
	MODELPART_CYCLONE_MAGAZINE, 37, -1, { 0.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, \
	1479, 1491, 0, 0, 0.0f, \
	59.0f, 97.0f, 97.0f, 37, 37, \
	0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 0.0f, \
	0, -1, 0.0f, \
	35.9f, 6.0f, -1, 0, { 0.0f, 0.0f, 0.0f }, { { 0.0f } }, 0.0f, 0.0f, -1, -1, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, 0, \
	0, 0, 0, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, \
	"set the magazine into the slot", \
}

/* Laptop Gun model (GpcgunZ), read from the ntsc-final ROM's model data and reload animation
 * (ANIM_GUN_LAPTOP_RELOAD 0xe4, 124 frames) with tools/vr_measure (f6geo.py, f6laptop.py, f6move.py,
 * f6turn.py, lapclip.py; Tasks F6 and F7g, 2026-10-19). User (2026-10-18, 2026-10-19): it reloads as
 * a clip gun, B a small rack, the clip falls, the new one goes in from below and is slapped; the ROM's
 * reload agrees, and (Stage F run 1: "the dropping out part is wrong animation wise you gutted it, and
 * the putting it back in is different too") the row plays its clip's moves:
 * - The model has 46 matrices, the left hand 17 to 32 as the pistols', the body 33. The clip
 *   (MODELPART_LAPTOPGUN_MAGAZINE1, 0x29) is a toggle whose 8 vertices are all skinned to matrix 44, a
 *   position node under the body: a box x -0.8..0.8, y -4.3..5.4, z -0.9..1.4 in its own frame. The
 *   gunviscmds and the part visibility hide it (and the hand clip); manual reload shows it from the
 *   reload state.
 * - The clip seat (clipseat 1, bodymtx 33): matrix 44's own pose outside the reload (the rest pose)
 *   is inside the pistol grip, {-0.08, -6.88, -0.47} in the body's
 *   frame, where the clip is never seen. The reload animation holds it under the front half at
 *   {-0.08, -3.28, 45.83} with rows {1, 0, 0}, {0, 0, 1}, {0, -1, 0} (its +y along the body's +z, its
 *   -z the body's up) in frames 0 to 38 (the clip in the gun before it drops at 42), 53 to 61 and 121
 *   to 123 (f6laptop.py; within 0.1 mm, the rows within 0.0003): the row draws the clip matrix there
 *   every frame (vrReloadSeatClip), so the guide and the slap point use it, and the clip is drawn
 *   there but while a playback moves it (below).
 *   Seated, the clip (body x -0.9..0.7, y -4.7..-2.4, z 41.5..51.2) sits up between the two forend
 *   halves (matrices 35 and 36, x -3.4..-0.4 and 0.4..3.5, y -7.9..-2.0), its sides 0.4 cm into
 *   them, its underside seen through the gap: clipwidth 1.
 * - The insert axis is the seat's -z, {0, 0, -1}: straight up into the gap. The insert depth is 2.8
 *   cm, the slap's push (the animation's clip goes 2.8 cm up the axis from frame 84 to 86). From the
 *   hold frame (63) to the slap (83) the animation holds the gun clip hooked in at its front, tilted
 *   36.2 degrees, its joint 4.6 cm from the seat's (lapclip.py): an inserted or guided clip is drawn
 *   so until the slap, its pose against the body read once from the copy model posed at the hold
 *   frame (vrReloadCaptureHeldClip), the guided one moved back down that pose's axis. The guide takes
 *   the clip at any angle
 *   (clipalignmax 180, VRR_GUIDE_ANY_ANGLE_DEG, as every rifle's since Task F2): the box is nearly
 *   symmetric, and the hand holds it however the user picked it up.
 * - The clip in the off-hand (MODELPART_LAPTOPGUN_MAGAZINE2, 0x2a) is a toggle whose 8 vertices are all
 *   skinned to matrix 45, a position node under the left wrist (matrix 18), and are the gun clip's in
 *   the same frame within 1 mm, vertex by vertex. As the script swaps them at frame 63, the gun clip's
 *   joint is at {0.29, -0.12, 0.21} in matrix 45's frame (heldtop) and its frame is turned 180 degrees
 *   about y, which the near-symmetric box does not show; heldaxis {0.02, -0.01, -1} is the mesh fit's
 *   direction with the swap's 1.5 degree tilt, used only by the mark line's angle, as the guide has no
 *   angle test. The hand clip is fixed to the wrist through the approach (frames 44 to 64).
 * - The hand turn: 156.0 degrees about {-0.3809, 0.4014, 0.8329} in the model root's frame (f6turn.py)
 *   puts the frame-63 wrist where the Falcon's frame-18 wrist is; the held clip's joint, 14.3 cm from
 *   the wrist, moves 17.2 cm with it, and with the two hands held parallel the clip is then 26 degrees
 *   off the insert axis (138 unturned).
 * - The clip turn (clipturnaxis, clipturndeg, clipturnpivot; Task F7e, Stage F run 1: the held clip
 *   turned wrong in the hand): after the hand turn, 68.2 degrees about {-0.2725, 0.8717, 0.4073} in
 *   the model root's frame (clipturn.py) turns the held clip alone about the palm, {1.089, -7.246,
 *   -1.33} in matrix 45's frame, so it sits in the drawn hand as the Falcon 2's clip sits in its
 *   hand at frame 18: its insert axis and its front (the gun's forward) in the wrist's frame as the
 *   Falcon clip's (56 and 61 degrees off before); its top moves 8.0 cm. The guide still takes any
 *   angle; no reach points to recheck.
 * - The clip shift (clipshift; Task F8e, Stage F run 2: the rifles' held clips sat on the
 *   knuckles, not in the palm): after the clip turn, {-2.404, -9.170, -8.819} cm in matrix 45's
 *   frame as turned (clipshift.py) moves the held clip alone 12.9 cm, so its grip point (the
 *   middle of its length and across, on its palm side) sits at the Falcon 2's clip's offset from
 *   the palm (the middle of the hand bone and its four knuckles) at frame 18; before it, the
 *   grip point sat 3.5 cm too far toward the palm side, 8.9 along the insert axis and 8.8 across
 *   (the clip out of the hand's side, as the user saw it in run 9). Its top, so the guide's start, moves
 *   with it (vrReloadCaptureHeldClip reads the matrix after the shift). The user's two marks
 *   (run 9, "mark weapon=14", holding the off controller where the hand should be with the clip
 *   going in; marks.py, clipshift.py) were taken as a check only: with the clip at its hooked
 *   insert pose (frame 63, where the guide ends) they put it 6.6 cm and 9 to 17 degrees apart
 *   from each other, their mean 0.6 cm from the drawn hand at the nearest, and this pose, in the
 *   fingers (0.7 cm from the index finger), is 2.3 cm and 20 to 72 degrees (the box's axes) from
 *   their mean, with the copy model's roll (its root is the off controller rolled -1.30 rad about
 *   its z, Task F12d; the check first left it out and found 10.7 cm, 37 to 66 degrees, which is why
 *   it was taken as no clip held in the drawn hand). No pose field from the marks: the Falcon's
 *   placement stands, its clip now in the fingers.
 * - The held clip's look (heldlookaxis, heldlookdeg, heldlookpivot, heldlookshift; Task F9b, Stage F
 *   run 3, the user: the clip "in the hand ... looks weird but the position for placing the clip in
 *   is correct", "rotate it forward direction some and bring it into the palm a little"; Task F10a,
 *   run 4: its end "furthest from the player could be rotated upwards slightly"; Task F11a, Stage G
 *   run 1: "the back end of the rectangle should be moving downwards as the rectangle rotates and
 *   the front goes up", the back end "vertically closer to the thumb"): drawn only, after
 *   the capture (vrReloadPoseHeldLook), so the guide's held top and axis stay as above. In the drawn
 *   left hand's frame (the wrist, matrix 18: z along the hand, 6 degrees off the off controller's
 *   forward; x across the knuckles; the fingers curl to -y, the palm's side; the off controller's up
 *   is {-0.96, -0.26, -0.07} there, with the copy model's -1.30 rad roll: F9b to F11a took the
 *   root's own y, {-0.50, 0.86, -0.10}, for it, so their angles to the level and their heights
 *   below are on those axes, not the controller's): turned 10 degrees about +x, about the clip's
 *   middle ({0, 0.5, 0.15} in matrix 45's frame), so its long side, which ran forward and up toward the back of the
 *   hand at 35 degrees to the hand, its far end 16 degrees above the off controller's level, lies at
 *   25 degrees, the far end 8 degrees above it. F9b turned it 20 degrees, which left the far end
 *   level (0 degrees): a turn about +x tips the far end down, so F10a's "up" is 10 degrees less of
 *   it. Then moved 1.5 cm along +y, into the palm, its middle from 6.5 to 5.0 cm off the hand bone on
 *   the palm's side (F9b's 2 cm puts a vertex of the hand inside the clip at 10 degrees). No vertex
 *   of the drawn hand (GcombathandslodZ) is then inside the clip's box (0.7 cm at the nearest;
 *   tools/vr_measure/heldlook.py). Once guided the gun's own clip is drawn, from the captured pose:
 *   the clip goes in where it did. F11a takes F10a's 10 degrees off (no turn: heldlookdeg 0, the far
 *   end 16 degrees above the level) and moves it 1 cm into the palm (1.5 puts the index finger's root
 *   0.36 cm inside the clip at 0 degrees, 1 cm 0.08): on the off controller's axes the back end
 *   (toward the player) drops 1.1 cm, from 3.3 to 4.4 cm below the controller, beside the thumb's
 *   middle joint and tip (3.5 and 4.1 below), and the far end rises 0.2 cm, to 1.7 below
 *   (heldlook.py's F11a lines). Those heights left out the copy model's roll (its root is the off
 *   controller rolled -1.30 rad about its z, bgunRender), so the user saw the rear end still high
 *   and the front brought down. F12e (Stage G run 2, the user: "the first mark is normal position and
 *   the second mark the amount and direction the clip needs to be rotated"): the free controller's
 *   turn between the two marks (freeq, controller to gun hand, w first: q1^-1 q2), 65.6 degrees about
 *   {-0.861, 0.037, 0.508} in the controller's frame, is the look turn, in place of F11a's none:
 *   65.6 degrees about {0.1377, -0.8048, 0.5773} in the wrist's frame, about the clip's middle. On the
 *   controller's axes (with the roll) the rear end drops 3.3 cm and the front end rises 3.5 cm. Moved
 *   0.5 cm into the palm, not F11a's 1: no vertex of the hand inside the turned clip (1 cm puts five
 *   0.26 cm inside it, 0.75 cm three 0.06 deep; heldlook.py's F12e lines). H5a (2026-10-20, the user:
 *   "first is where the laptop clip is based on first mark's hand position. second mark is the
 *   rotation and movement needed to be done to the laptop clip"): two more marks, the change against
 *   F12e's look as drawn, stacked on it: the turn q1^-1 q2, 28.3 degrees about {0.125, -0.960, 0.252}
 *   in the controller's frame (mostly about its down axis), and the move q1^-1 (free2 - free1),
 *   {1.99, 0.83, 0.85} cm there (2.3 cm); in the wrist's frame, with the roll, 28.3 degrees about
 *   {0.8724, 0.3843, 0.3020} and {-1.369, 1.766, 0.618} cm. Applied after F12e's look about the clip's
 *   middle (heldlookpivot is the middle of its vertices, which F12e's turn leaves in place), so the
 *   look is the change's turn after F12e's, 70.4 degrees about {0.5436, -0.7021, 0.4599}, and F12e's
 *   move plus the change's, {-1.369, 2.266, 0.618}. On the controller's axes (with the roll) the rear
 *   end rises 1.0 cm and moves 4.4 cm right, the front end rises 0.7 cm and moves 1.4 cm forward; no
 *   vertex of the hand inside the clip (0.9 cm at the nearest; heldlook.py's H5a lines).
 * - No rack part: after the swap the left hand drops away and slaps the clip up from below (rackslap
 *   1, loadsoninsert 0, the rack fields zero with rackmtx -1). The palm (the middle of the wrist and the
 *   four knuckles) swings up the insert axis at up to 2.6 m/s (frame 85) and stops at 86 (slapcontact);
 *   slapfrom 84 (the palm still swinging up), slapto 86 (from 87 it drops away at over 1 m/s); slaprad
 *   12.5, as the rifles' (Task F8f: the K7 Avenger's mark; 8 before). The animation's clip is not at
 *   the seat in the slap frames: the palm pushes it
 *   up (2.8 cm and 21 degrees from 84 to 86), and at 86 it sits 3.6 cm behind the seat, turned 10
 *   degrees, and slides 3.6 cm forward into the seat by 121 (lapclip.py). The row plays that: the
 *   slap's playback (playfrom 84, its slapfrom, to playto 121, playmtx and playmtxlast the clip's 44,
 *   on bodymtx; vrReloadHasSlapPlay) starts with the slap's hand and draws the clip rocked flat from
 *   the hooked pose and slid in (Task F7g), and the slap's drawn hand is placed by the body
 *   (vrReloadPlaceSlapHand), so the palm meets the drawn clip as the animation's does. slappoint, where
 *   the swing is tested, is the palm at 86 in the animation's clip frame then, {-0.58, -4.91, 6.48}
 *   (f6laptop.py), put on the seated clip: on the clip's centre line (x -0.8..0.8), 0.6 cm behind its
 *   rear end (y -4.3) and 5.1 cm under its underside (z 1.4; the seat's +z is down), 3.6 cm ahead of
 *   where the drawn palm meets the clip at 86, within slaprad.
 * - B, the pump and the clip's fall (ejectfrom 1 to ejectto 52 on matrices 35 and 36, ejectmtx and
 *   ejectmtxlast, posed on bodymtx, and the clip with ejectdrop 1): the palm pulls the two forend
 *   halves 9.9 cm back (frames 1 to 5), holds, and pushes them forward (14 to 21), with the script's
 *   SFX_RELOAD_04FB (1275) at frame 4, SFX_04FA (1274) at 15 and SFX_04F7 (1271) at 25; the clip stays
 *   seated (frames 0 to 38), then tips out rear end first, sliding back and down to 44 degrees (39 to
 *   52), with SFX_0053 (83) at 42 (ejectsnd, ejectsndat; the script plays the first two at 1.63x and 1.44x,
 *   the row all at normal speed; no sndeject at the press), and the animation hides it at 53. The core
 *   ejects at the press (the rounds go back then); the clip is drawn through the playback, posed after
 *   the capture so the well stays seated, and falls from its frame-52 pose (vrReloadStepPlay), unless a
 *   new clip is guided in first (then it goes).
 * - Sounds as the ROM's reload script plays them (invitems.c), besides the eject's: insert SFX_04F8
 *   (1272) at 63 as the hand puts the clip in (1.0x), and the slap SFX_05C5 (1477) at 85, the row's
 *   rack sound, played at slapcontact. Left out (no slot): SFX_04F9 (1273) at 91, after the slap.
 * - The draw: the Laptop Gun has its own equip and unequip animations, which do not touch the clips.
 * - The second function, the deploy (a sentry gun), has no ammo (ammoindex -1: it throws the gun), so
 *   it goes with the row (vrReloadOwnsAmmo): it throws whatever the clip, and B with it selected ejects
 *   the clip. Thrown, the gun leaves the inventory and the hand switches away: the switch saves the
 *   shape and puts a clip in but not slapped back in the reserve (vrReloadSuspend), so its rounds are
 *   not lost but the sentry, which takes up to 200 from the reserve as it deploys, may miss them. A
 *   deployed gun is not in hand, so there is nothing to reload.
 * - The burst fire reads the clip (ammoindex 0); slidemax 0.
 * - invitems.c: 50 rounds a clip, AMMOTYPE_SMG, no WEAPONFLAG_DUALWIELD (single wield only);
 *   VrTwoHandsGun (the free hand's grip supports it, except mid-reload: from B until the slap,
 *   vrReloadMidReload, Tasks F8d and F9a). */
#define LAPTOPGUN_ROW(weapon) { \
	(weapon), 0, \
	6.0f, 180.0f, 2.8f, { 0.0f, 0.0f, -1.0f }, 1.0f, 0.0f, \
	0, { { 0.0f } }, \
	-1, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 0, 1, 0, 0, \
	MODELPART_LAPTOPGUN_MAGAZINE1, 44, -1, { 0.0f, 0.0f, 0.0f }, -1, 63.0f, \
	{ -0.3809f, 0.4014f, 0.8329f }, 156.0f, \
	MODELPART_LAPTOPGUN_MAGAZINE2, 45, -1, { 0.29f, -0.12f, 0.21f }, { 0.02f, -0.01f, -1.0f }, \
	0, 1272, 1477, 0, 0.0f, \
	84.0f, 121.0f, 0.0f, 44, 44, \
	84.0f, 86.0f, 86.0f, { -0.58f, -4.91f, 6.48f }, 12.5f, \
	0, -1, 0.0f, \
	0.0f, 0.0f, 33, 1, { -0.08f, -3.28f, 45.83f }, { { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, -1.0f, 0.0f } }, \
	1.0f, 52.0f, 35, 36, { 1275, 1274, 1271, 83 }, { 4.0f, 15.0f, 25.0f, 42.0f }, 1, \
	0, 0, 0, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, \
	{ -0.2725f, 0.8717f, 0.4073f }, 68.2f, { 1.089f, -7.246f, -1.330f }, \
	{ -2.404f, -9.170f, -8.819f }, \
	{ 0.5436f, -0.7021f, 0.4599f }, 70.4f, { 0.0f, 0.5f, 0.15f }, { -1.369f, 2.266f, 0.618f }, \
	"insert magazine, slap it home", \
}

/* Reaper model (GskminigunZ), read from the ntsc-final ROM's model data and reload animation
 * (ANIM_GUN_REAPER_RELOAD 0x401, 208 frames) with tools/vr_measure (g1hold.py, g1turn.py; Task G1,
 * 2026-10-19):
 * - The model has 50 matrices, the left hand 17 to 32 as the pistols', the body 33. The magazine
 *   (MODELPART_REAPER_MAGAZINE1, 0x2a) is a toggle whose 63 vertices are all skinned to matrix 44, a
 *   position node under the body, not turned from it in the rest pose, seated with its joint at
 *   {13.4, 1.7, -5.4} cm in the body's frame, on the gun's right above the grip. It is shown at idle
 *   (the gunviscmds and the part visibility hide only MAGAZINE2, the hand magazine).
 * - In matrix 44's frame the magazine is a handle block behind its joint (z -5.9..0, x -2.6..2.6), a
 *   wide face at the joint (x -6.6..5.2, y -3.5..3.7) and a tongue tapering 23.8 cm forward from it
 *   along +z to a tip 1.3 cm wide. The tongue slides forward into the body over a block of it (the
 *   body's surfaces fill the tongue's box from z -5.4 in the body's frame, where the face sits seated:
 *   g1hold.py), so seated only the face and the handle show. The insert axis is its +z, {0, 0, 1}, the
 *   body's forward.
 * - The insert depth is 16.1 cm: at the hold frame (124, where the script plays the insert sound) the
 *   hand magazine's joint is 16.1 cm behind the seated joint along the axis, 3.2 off it, its axis 12
 *   degrees from the well's, and the animation slides it in to 134 (off the axis by 0.2 and 8 degrees
 *   by then), where the script swaps it for the gun magazine 2.3 cm short of seated. The guided
 *   magazine is the gun's own slid back down the axis, the tongue over the body's block as the
 *   animation's: full width (clipwidth 1).
 * - Part 0x42 (MODELPART_REAPER_0042), a toggle, holds only the magazine's face at its joint, 11
 *   vertices on matrix 44 and none on the body; bgun0f098030 hides it on every frame for every gun, so
 *   it is never drawn. vrReloadCollapseClip collapses matrix 44 while no magazine is drawn in the gun,
 *   which is harmless for the hidden part.
 * - No rack: the script plays nothing after the insert sound and no part of the gun but the magazines
 *   moves against the body in the reload (g1move.py). So the row loads on insert
 *   (loadsoninsert 1, B empties it), with the rack fields zero and rackmtx -1.
 * - The magazine in the off-hand (MODELPART_REAPER_MAGAZINE2, 0x2b) is a second toggle whose 63
 *   vertices are all skinned to matrix 49, a position node under the left wrist (matrix 18), and are the
 *   gun magazine's in the same local frame: a rigid fit leaves 1 mm and puts the gun magazine's joint
 *   at {-0.05, 0, 0.03}, within it, so heldtop {0, 0, 0} and heldaxis +z, {0, 0, 1}. It is fixed to the
 *   wrist through the approach (frames 100 to 136); the script shows it at 90.
 * - The hand turn: 19.1 degrees about {0.3992, 0.9147, 0.0634} in the model root's frame (g1turn.py,
 *   handturn.py's method) puts the frame-124 wrist where the Falcon's frame-18 wrist is; the held joint,
 *   11.9 cm from the wrist, moves 3.6 cm, and with the two hands held parallel the magazine is then 22
 *   degrees off the insert axis (27 unturned).
 * - The clip turn (Task F7e's; F11e and H7b below change it): after the hand turn, 57.9 degrees about {-0.3502, -0.8613, -0.3682} in
 *   the model root's frame turns the held magazine alone about the palm, {-0.287, 2.691, -4.962} in
 *   matrix 49's frame, so its insert axis lies in the wrist's frame along the Falcon 2's frame-18 clip's
 *   (57.9 degrees off before). The magazine goes in along the gun's forward, so it has no front apart
 *   from its axis to roll it by: the turn is the smallest that lines the axis up.
 * - The clip shift (Task F8e's): after the clip turn, {5.946, -5.082, 0} cm in matrix 49's frame as
 *   turned moves the held magazine alone 7.8 cm across its axis, so its palm side sits at the Falcon 2's
 *   clip's offset from the palm: before it the magazine sat 7.8 cm too far toward the palm, 12 of the
 *   drawn hand's 96 vertices inside its hull, 2 after (the Falcon's clip has 1, the AR34's 8). Not along
 *   the axis: clipshift.py's method would also move it 6.7 cm to centre the 30 cm magazine on the palm,
 *   where the animation holds it at its face, by the handle (7 hand vertices inside then).
 * - Turned forward in the hand (Task F11e, Stage G run 1, the user: it "would work better if it was
 *   rotated forward in the hand to about 45 degree forward"): the magazine as held above, its axis on
 *   the off controller {-0.85 right, 0.12 up, 0.52 forward} (59 degrees off the controller's forward,
 *   mostly across the hand), turned 45 degrees about the palm's normal (the wrist's y) through the palm,
 *   its tongue forward: {-0.33, -0.21, 0.92}, 23 degrees off. Folded into the clip turn and shift above,
 *   so the guide's held top and axis follow it: 21.1 degrees about {-0.1900, -0.3665, -0.9108} (the
 *   pivot kept), then {7.551, -4.678, -0.508} cm (was 57.9 degrees about {-0.3502, -0.8613, -0.3682} and
 *   {5.946, -5.082, 0}); no drawn-hand vertex inside its hull (2 before). The reach: lined up at the
 *   mouth the palm sits where it did (the turn is about it), 30 cm from the gun hand's wrist, and the
 *   off controller points 23 degrees off the gun's forward (4 to the right, 23 up), 59 before (54 to
 *   the right, across the body). tools/vr_measure/reapertilt.py.
 * - Spun in the hand (Task H7b, run 16, the user: "reaper clip needs to get rotated 45 degrees towards
 *   the right hand (so spin) its kinda upside down"): F11e's magazine turned 45 degrees about its own
 *   insert axis (+z of matrix 49) through its joint, so the guide's held top and axis stay where F11e
 *   put them, its top (+y, the gun's up when seated) toward the controller's right, where the gun
 *   hand is. On the off controller's right, up and forward (with the copy model's -1.30 rad roll,
 *   Task F12d) its top goes from {0.78, -0.49, 0.39} (half down) to {0.94, 0.24, 0.23} and its side (+x,
 *   the gun's right when seated) from {-0.56, -0.83, 0.06} to {0.16, -0.93, 0.32}; the other sense
 *   would point the top straight down. Folded into the clip turn and shift: 33.7 degrees about
 *   {-0.4172, -0.6027, 0.6803} (the pivot kept), then {0.044, -8.062, -0.508} cm (F11e's above
 *   before); no drawn-hand vertex inside its hull, the nearest 2.5 cm (2.1). Lined up at the mouth the
 *   off controller still points 23 degrees off the gun's forward and its up is 76 degrees off the
 *   gun's up (120 before), the palm 7.7 cm further right. The left-handed side mirrors through the
 *   drawn root, as the other turns (vrReloadRootTurn). tools/vr_measure/reaperspin.py.
 * - No angle test (clipalignmax 180, VRR_GUIDE_ANY_ANGLE_DEG), as the rifles'.
 * - Sounds as the ROM's reload script plays them (invitems.c): eject SFX_05C7 (1479, a sound id) at
 *   frame 49 as the hand lets the magazine go (it comes off at 70), insert SFX_05C6 (1478) at 124 as it
 *   slides in; both at normal speed, as the script plays them. No rack sound.
 * - The draw: the Reaper has its own equip and unequip animations, which do not touch the magazines.
 * - The fall tumbles the magazine about its middle, 8.95 cm ahead of its joint along the axis (clipmid:
 *   it runs z -5.9..23.8), not half the insert depth behind it, which is 2 cm past the handle's end.
 * - The second function, the grinder, has no ammo (ammoindex -1), so it goes with the row; the fire reads
 *   the magazine (ammoindex 0); slidemax 0.
 * - invitems.c: 200 rounds a magazine, AMMOTYPE_REAPER, no WEAPONFLAG_DUALWIELD (single wield only);
 *   VrTwoHandsGun (the free hand's grip supports it, except mid-reload, vrReloadMidReload, Task F9a; a
 *   hand holding a magazine is never the support hand). */
#define REAPER_ROW(weapon) { \
	(weapon), 0, \
	6.0f, 180.0f, 16.1f, { 0.0f, 0.0f, 1.0f }, 1.0f, 8.95f, \
	0, { { 0.0f } }, \
	-1, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 1, 0, 0, 0, \
	MODELPART_REAPER_MAGAZINE1, 44, -1, { 0.0f, 0.0f, 0.0f }, -1, 124.0f, \
	{ 0.3992f, 0.9147f, 0.0634f }, 19.1f, \
	MODELPART_REAPER_MAGAZINE2, 49, -1, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, \
	1479, 1478, 0, 0, 0.0f, \
	0.0f, 0.0f, 0.0f, -1, -1, \
	0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 0.0f, \
	0, -1, 0.0f, \
	0.0f, 0.0f, -1, 0, { 0.0f, 0.0f, 0.0f }, { { 0.0f } }, 0.0f, 0.0f, -1, -1, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, 0, \
	0, 0, 0, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, \
	{ -0.4172f, -0.6027f, 0.6803f }, 33.7f, { -0.287f, 2.691f, -4.962f }, \
	{ 0.044f, -8.062f, -0.508f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, \
	"insert magazine", \
}

/* Devastator model (GdydevastatorZ), read from the ntsc-final ROM's model data and reload animation
 * (ANIM_GUN_DEVASTATOR_RELOAD 0x3f8, 118 frames) with tools/vr_measure (g1hold.py, g1rack.py,
 * g1turn.py; Task G1, 2026-10-19):
 * - The model has 40 matrices, the left hand 17 to 32 as the pistols', the body 33. The part names are
 *   swapped from most guns': the magazine in the gun is MODELPART_DEVASTATOR_MAGAZINE2 (0x2a), a toggle
 *   whose 63 vertices are all skinned to matrix 37, a position node under the body, not turned from it
 *   in the rest pose, seated with its joint at {3.8, 4.0, 14.4} cm in the body's frame, on the gun's
 *   right; it is shown at idle (the gunviscmds and the part visibility hide only MAGAZINE1, the hand's).
 * - In matrix 37's frame the magazine is a flat box running 14.9 cm out to the gun's right from its
 *   joint (x 0..14.9, y -1.1..1.4, z -4..4): the joint is its inner end and the insert axis its -x,
 *   {-1, 0, 0}, into the gun's side.
 * - The insert depth is 3.5 cm, a length of travel: the animation swings the hand magazine in (frames 34
 *   to 41, from 67 to 7 degrees off) and pushes it straight from the hold frame (42: its joint 3.5 cm out
 *   from the seated one along the axis, 0.3 off it, its axis 4.7 degrees from the well's) to seated at
 *   45 (g1hold.py). Only the inner 1.5 cm of the seated magazine is inside the body; a body ledge sits in
 *   its box's front corner seated (z 17.4 to 18.2 in the body's frame, as the gun draws it at idle), and
 *   nothing of the body is in the guided magazine's box from 1.8 cm out: full width (clipwidth 1).
 * - The grenades: the magazine's toggle holds eight more, MODELPART_DEVASTATOR_CART1 to CART8 (0x64 to
 *   0x6b), each 8 to 10 vertices on matrix 38, a position node under 37 (MODELPART_DEVASTATOR_0028).
 *   bgun0f098030 shows the first loadedammo of them each frame (AMMOFLAG_QTYAFFECTSPARTVIS), and
 *   loadedammo is the core's engine count (vrrEngineLoaded: the chamber and the clip, 0 with the chamber
 *   empty), so they follow the rounds as before: a magazine seated but not yet racked shows none, the
 *   rack shows all eight and each shot hides one, and bgunUpdateDevastator slides matrix 38 along by
 *   loadslide after a shot as ever. Manual reload moves only matrix 37 (guided in, falling), not 38 under
 *   it (the drawn matrices are absolute), but the grenades show only with rounds loaded, when the
 *   magazine is seated where the animation has 37 and 38, and they hide with their magazine's toggle.
 * - No base cap: the model has no part 0x42.
 * - The rack is a cocking lever, not the front of the gun: matrix 34, a position node on the body at
 *   {1.9, 4.6, 1.8} with no part id, not turned from the body, drawn by 20 vertices of the body's
 *   display list, x -0.7..5.3, y -0.7..0.6, z -1.8..1.3 in its own frame: a handle sticking out to the
 *   gun's right, 8 cm behind the gun hand's wrist. After the insert the animation's left hand comes to
 *   it, the fingertips close on the handle (from frame 64; to 82 they stay within x 2.5..7.4, y
 *   -2.0..-0.4, z -1.5..3.1 in its frame) and pull it 4.9 cm back along the body's -z (68 to 78; 0.6 inward too), hold it to 82 and let it go
 *   forward by 86, with SFX_05CC (1484) at 66 (g1rack.py). The front sleeve, matrix 35 (86 vertices of
 *   the body's display list round the barrel's front, 35 cm ahead of the lever), slides 6.4 cm back with
 *   it in the same frames, the hand never on it. So the row racks on the lever (rackmtx 34): the box is
 *   the handle and the fingertips' span, x -0.7..7.5, y -2.0..0.6, z -1.8..3.1, rackrad 4 as the
 *   pistols' and the CMP150's; rackoffset {3.5, -1.5, 0.4}, between the index and middle fingertips at
 *   72, for the logs; racktravel 4.9, the lever's travel; rackmove 1, the lever drawn pulled back with
 *   the hand and going forward after (vrReloadMoveRack), and the sleeve drawn moving with it
 *   (rackfollowmtx 35, rackfollowscale 1.31: 6.4 cm for the lever's 4.9). The lever sits behind the
 *   magazine's slot (its box with rackrad reaches z 8.9 in the body, the magazine starts at 10.4), and
 *   the gun hand's wrist is 0.6 cm past that reach. Task F11f (Stage G run 1, three "mark weapon=23"
 *   lines with the rack due): the user reached for the lever from behind and above it, the grab point
 *   at (3.07 1.44 -6.33), (1.56 1.08 -11.52) and (2.07 2.41 -6.26) in its frame, 4.6 to 9.7 cm outside
 *   that box; the box now runs back to z -12.5 and up to y 3.0 (x -0.7..7.5, y -2.0..3.0, z
 *   -12.5..3.1), so the three are inside it and with x negated within 2.4 cm of it (rackrad 4); it grew
 *   away from the gun hand, whose wrist stays 4.8 cm out (its knuckles 14), as before (g1rack.py).
 * - The magazine in the off-hand (MODELPART_DEVASTATOR_MAGAZINE1, 0x29) is a toggle whose 133 vertices
 *   (the magazine with its eight grenades) are all skinned to matrix 39, a position node under the left
 *   wrist (matrix 18), in the gun magazine's own local frame: a rigid fit of the 37 set onto it leaves
 *   0.8 mm with no turn and the joint at {0.08, 0, 0}, within it, so heldtop {0, 0, 0} and heldaxis -x,
 *   {-1, 0, 0}. It is fixed to the wrist through the approach (frames 30 to 49); the script shows it at
 *   36 and swaps it for the gun magazine at 47.
 * - The hand turn: 16.1 degrees about {0.2555, -0.9386, 0.2318} in the model root's frame (g1turn.py,
 *   handturn.py's method) puts the frame-42 wrist where the Falcon's frame-18 wrist is; the held joint,
 *   16.1 cm from the wrist, moves 4.5 cm, and with the two hands held parallel the magazine is then 101
 *   degrees off the insert axis (106 unturned).
 * - The clip turn (Task F7e's): after the hand turn, 36.1 degrees about {-0.1277, 0.7881, 0.6021} in the
 *   model root's frame turns the held magazine alone about the palm, {10.232, 2.671, -3.92} in matrix
 *   39's frame, so its insert axis and its front (the gun's forward) sit in the wrist's frame as the
 *   Falcon 2's frame-18 clip's (30.8 and 27.8 degrees off before).
 * - The clip shift (Task F8e's): after the clip turn, {-4.501, -1.337, -5.286} cm in matrix 39's frame as
 *   turned moves the held magazine alone 7.1 cm, so its grip point (the middle of its length and across,
 *   on its palm side) sits at the Falcon 2's clip's offset from the palm; before it, 4.5 along the axis,
 *   1.9 toward the palm and 5.1 across, 25 of the drawn hand's 96 vertices inside the magazine's hull, 5
 *   after (the Falcon's clip has 1, the AR34's 8).
 * - No angle test (clipalignmax 180, VRR_GUIDE_ANY_ANGLE_DEG), as the rifles'.
 * - Sounds as the ROM's reload script plays them (invitems.c): eject SFX_0053 (83) at frame 15 (0.6x),
 *   insert SFX_05C5 (1477) at 47 as the magazine seats (0.7x), and the lever's SFX_05CC (1484, a sound
 *   id) at 66: the row's rack sound. The row plays them at normal speed.
 * - On B the old magazine falls from the side, along -clipaxis (out to the gun's right) at the fall's
 *   speed as an ejected clip does, tumbling about a point 1.75 cm out from its inner end (half the insert
 *   depth): the animation flings it out to the right in frames 16 to 24.
 * - The draw: the Devastator's equip animation is its shoot script, not the reload.
 * - Both functions, the grenade launcher and the wall hugger, fire from the one magazine (ammoindex 0);
 *   slidemax 0. AMMOTYPE_DEVASTATOR is also the SuperDragon's grenades' reserve.
 * - invitems.c: 8 rounds a magazine, no WEAPONFLAG_DUALWIELD (single wield only); VrTwoHandsGun (the
 *   free hand's grip supports it, except mid-reload: from B until the rack, vrReloadMidReload, Task F9a;
 *   so a grip at the lever while the rack is due racks, and never aims). */
#define DEVASTATOR_ROW(weapon) { \
	(weapon), 0, \
	6.0f, 180.0f, 3.5f, { -1.0f, 0.0f, 0.0f }, 1.0f, 0.0f, \
	0, { { 0.0f } }, \
	34, 4.0f, { -0.7f, -2.0f, -12.5f }, { 7.5f, 3.0f, 3.1f }, { 3.5f, -1.5f, 0.4f }, 4.9f, 0, 0, 0, 0, \
	MODELPART_DEVASTATOR_MAGAZINE2, 37, -1, { 0.0f, 0.0f, 0.0f }, -1, 42.0f, \
	{ 0.2555f, -0.9386f, 0.2318f }, 16.1f, \
	MODELPART_DEVASTATOR_MAGAZINE1, 39, -1, { 0.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, \
	83, 1477, 1484, 0, 0.0f, \
	0.0f, 0.0f, 0.0f, -1, -1, \
	0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 0.0f, \
	1, 35, 1.31f, \
	0.0f, 0.0f, -1, 0, { 0.0f, 0.0f, 0.0f }, { { 0.0f } }, 0.0f, 0.0f, -1, -1, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, 0, \
	0, 0, 0, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, \
	{ -0.1277f, 0.7881f, 0.6021f }, 36.1f, { 10.232f, 2.671f, -3.920f }, \
	{ -4.501f, -1.337f, -5.286f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, \
	"insert magazine, pull the cocking lever", \
}

/* Farsight XR-20 model (Gz2020Z), read from the ntsc-final ROM's model data and reload animation
 * (ANIM_GUN_FARSIGHT_RELOAD 0x3eb, 130 frames) with tools/vr_measure (g1hold.py, g1turn.py, g1rel.py;
 * Task G1, 2026-10-19). The Callisto's row type, an intake row with a playback: the same goo orb,
 * pressed against the gun's side:
 * - The model has 48 matrices, the left hand 17 to 32 as the pistols', the body 33. No clip part and no
 *   slide: the orb, MODELPART_FARSIGHT_ORB (0x28), is a toggle the gunviscmds and the part visibility
 *   hide, which the reload script shows at frame 1 and hides at 82. So the row has no clip part
 *   (clippart and clipmtx -1): nothing is drawn in the gun, loaded or empty; B empties it with nothing
 *   falling, and the empty gun waits for an orb.
 * - The orb's 64 vertices are all skinned to matrix 47, a position node under the left wrist (matrix
 *   18): a ball 7.5 cm across round its joint (heldmtx 47 alone, heldtop {0, 0, 0}).
 * - The intake point (intakemtx 33, the gun body): the orb arrives at frame 65 and its joint sits at
 *   {4.8, 6.6, 15.6} cm in the body's frame to frame 81 (the mean; every frame within 0.8 cm of it,
 *   g1hold.py), on the gun's right near the top, between the two flaps of its hatch (matrices 43 and
 *   44, position nodes on the body at {3.8, 6.3, 17.2} and {3.7, 7.0, 11.7}, 5 and 8 vertices of the
 *   body's display list). The orb loads as its joint comes within clipguiderad (6 cm) of the point,
 *   from any side and at any angle, as the Callisto's. Mirrored with the model in left-handed mode
 *   through the body matrix.
 * - After the load the gun plays the orb going in (playfrom 65 to playto 118): the orb sits at the
 *   intake while the hand lets go of it (from 66) and is hidden at 82 (playheldto, the script's
 *   hidepart), and the flaps open out and shut again without turning (43 up to 4.0 cm, 44 up to 5.7,
 *   frames 67 to 117; g1rel.py, g1hold.py). Frame 65 is the arrival with both flaps shut, 118 the first
 *   frame with both back on their rest pose. Matrices 45 and 46 (two nodes on the body) do not move in
 *   the reload; they are in the run (playmtx 43 to playmtxlast 47) as the run must hold the orb's 47,
 *   and are drawn where they are. 53 frames at speed 1: 0.88 s of game time. The left hand bringing
 *   the orb in (frames 1 to 64) and pulling away is not played.
 * - The hand-off (playease 6, as the Cyclone's): the playback's first frame puts the orb on the intake,
 *   up to clipguiderad from where the hand held it; it goes there from the hand's pose over 6/60 s
 *   (vrReloadEaseHandoff) instead of jumping.
 * - The orb is fixed to the wrist through the approach (frames 40 to 65; 0.2 cm off at 66, 1.0 at 68),
 *   so the hold frame only sets the wrist's turn from the model root: frame 65, where it reaches the
 *   intake.
 * - The hand turn: 68.2 degrees about {0.3395, 0.6700, 0.6602} in the model root's frame (g1turn.py,
 *   handturn.py's method) puts the frame-65 wrist where the Falcon's frame-18 wrist is, so the hand
 *   sits on the off controller as the Falcon's does. The orb turns with the hand: its joint, 10.6 cm
 *   from the wrist, moves 11.7 cm on the controller. The intake test has no angle, so only where the
 *   orb sits matters.
 * - In the gun model the orb hangs off the idle left wrist: bgun0f0a5550 hides an owned row's held part
 *   except while the playback above poses it at the intake.
 * - No slide: the row loads on insert (loadsoninsert 1), so an orb put into an empty gun chambers a
 *   round at once, and the rack fields are zero (rackmtx -1). B empties it fully.
 * - Sounds: the ROM's reload script plays only SFX_RELOAD_FARSIGHT (1075, the Phoenix's and the
 *   Callisto's load) at frame 72, at normal speed: the row's insert, played as the load starts the
 *   playback at 65. The script has no eject sound: B plays the Phoenix's and the Callisto's, SFX_05C7
 *   (1479). No rack sound.
 * - The zoom (Task S1: the game's full screen zoom, vrZoomViewOn, only in sight mode and aimed with
 *   two hands, vrTwoHandAiming): a free hand holding an orb is never the support hand (vrTwoHandAiming
 *   tests vrReloadHandHoldsClip, and bondgun.c latches VrTwoHandReloadGrip for it and for any grip
 *   from B until the load, vrReloadMidReload, Task F9a), so the view is unzoomed from B until the orb
 *   is in; the grip still held after the load stays off the gun until let go, and a new grip on the
 *   gun aims and zooms again.
 * - The draw: the Farsight's equip animation is its shoot script, not the reload.
 * - Both functions, the shot and the target locator, fire from the one orb (ammoindex 0); slidemax 0.
 * - invitems.c: 8 rounds an orb, AMMOTYPE_FARSIGHT, no WEAPONFLAG_DUALWIELD (single wield only);
 *   VrTwoHandsGun. */
#define FARSIGHT_ROW(weapon) { \
	(weapon), 0, \
	6.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f, \
	0, { { 0.0f } }, \
	-1, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 1, 0, 0, 0, \
	-1, -1, 33, { 4.8f, 6.6f, 15.6f }, -1, 65.0f, \
	{ 0.3395f, 0.6700f, 0.6602f }, 68.2f, \
	MODELPART_FARSIGHT_ORB, 47, -1, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, \
	1479, 1075, 0, 0, 0.0f, \
	65.0f, 118.0f, 82.0f, 43, 47, \
	0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 0.0f, \
	0, -1, 0.0f, \
	0.0f, 6.0f, -1, 0, { 0.0f, 0.0f, 0.0f }, { { 0.0f } }, 0.0f, 0.0f, -1, -1, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, 0, \
	0, 0, 0, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, \
	"press an orb into the side intake", \
}

/* Shotgun model (GshotgunZ), read from the ntsc-final ROM's model data, its reload animation
 * (ANIM_GUN_SHOTGUN_RELOAD 0x3ed, 130 frames) and its single shot (0x3ee, also its equip animation)
 * with tools/vr_measure (g0shotgun.py; Task G0c, 2026-10-19, Stage G). A pump gun loaded one shell at
 * a time (pumpaction and shellbyshell; the user, 2026-10-19: "what if we make the player pump it
 * instead" of the fire animation's pump):
 * - The model has 39 matrices, the left hand 17 to 32 as the pistols', the body 33. The gun has no
 *   magazine of its own: the tube never leaves it (shellbyshell), so the row has no clip part (clippart
 *   and clipmtx -1) and B does nothing. MODELPART_SHOTGUN_CART (0x2b), which the gunviscmds hide, is
 *   the shell the left hand brings: 26 vertices all on matrix 38, a position node under the left
 *   wrist (matrix 18), a 2 cm round box 4.1 cm long along its +z (x -0.9..1.0, y -1.0..0.9, z 0..4.1)
 *   with its joint at the rear: heldtop is its front face's middle, {-0.04, -0.02, 4.1}, the end that
 *   goes in first.
 * - The port (intakemtx 33, the gun body): the hand brings the shell up from under the gun (its front
 *   at y -11.4 at frame 52), with its front at the port's mouth, {-0.4, -0.9, 12.4} in the body's
 *   frame (-0.35, -0.87, 12.35 at frame 62), tilted 28 degrees up from the body's +z, and pushes it in
 *   along +z (frames 64 to 73: its rear from 9.1 to 12.8, at {-1.0, -1.2, 12.7} at 72), and the hand
 *   goes away with the script hiding it at 75. The shell loads as its front comes within clipguiderad
 *   (6 cm, the other rows' value) of the mouth, at any angle, as the Phoenix's orb (the point test of an
 *   intake row), mirrored with the model in left-handed mode through the body matrix; each adds one
 *   round to the tube (shellbyshell), while the tube has room.
 * - After the load the gun plays the shell going in (playfrom 62 to playto 74, matrix 38 alone,
 *   playmtx and playmtxlast 38, posed on the body): drawn in the gun through 73 (playheldto 74), its
 *   front from the mouth to 16.8 cm along the body; from 74 the animation's hand takes it back out,
 *   which is not played. The hand-off (playease 4): the shell goes from where the hand held it to the
 *   playback's first pose over 4/60 s. 12 frames at speed 1: 0.2 s of game time.
 * - The hold frame is 62: the shell is fixed to the wrist (within 0.14 cm and 0.6 degrees) from 40 to
 *   60 and at the mouth at 62; from 64 the fingers push it along the wrist (0.6 cm at 64, 2.2 at 72).
 * - The hand turn: 105.4 degrees about {0.2199, 0.1171, 0.9685} in the model root's frame
 *   (g0shotgun.py, handturn.py's method) puts the frame-62 wrist where the Falcon's frame-18 wrist is.
 *   The shell's front, 15.3 cm from the wrist, moves 13.6 cm with it. No clip turn or shift: to check
 *   in the headset.
 * - The pump (rackmtx 34, rackmove 1): matrix 34, a position node under the body with no part id, not
 *   turned from it, its origin at {0, 1.17, 25.22} on the body; its 63 vertices, the forend, span x
 *   -1.5..1.5, y -2.3..2.1, z -0.4..13.5 in its frame. The reload's left hand pulls it 7.4 cm back
 *   along its own -z (frames 105 to 112, back by 122; racktravel 7.4) after SFX_RELOAD_04FB (1275) at
 *   103, the row's rack sound, played as the pump racks; the single shot's script plays the same sound
 *   at 34 for its own pump (not played: see G0d). Drawn moved back with the pull and riding with the
 *   hand after it until it is forward again (vrReloadMoveRack, vrReloadStepRack), with the gun's own
 *   left hand on it.
 * - The rack box: the forend's own box grown to x -5..5 (symmetric, for the mirror), y -6..4, z -2..15,
 *   rackrad 3, so a free hand that grips the forend from beside, above or below takes the pump. The
 *   idle pose (frame 0 of the single shot, the holsters' fast switch pose) holds the left hand around
 *   the forend: its finger joints at x -3.2..2.8, y -5.4..1.6, z 5.3..11.2 in matrix 34's frame, the
 *   reload's pump hand at x -3.6..-0.1, y -6.0..0.3, z 6.6..10.1 (rackoffset {0, -1.0, 7.0}, the
 *   middle of them, for the logs). The port, 12.8 cm behind the forend's rear, is 7.8 cm past the box
 *   with rackrad. While the gun waits for its pump the support grip (vrTwoHandGripHeld) is on the pump
 *   wherever the controller is (vr_reload.c, vrReloadBuildInput): the gun draws its own left hand on
 *   the forend then, so that is where the player sees the hand.
 * - Sounds as the ROM's reload script plays them (invitems.c): SFX_01D8 (472) at frame 67 as the shell
 *   clicks in, the row's sndinsert2 5/60 s after the load at 62 (insert2delay 5; sndinsert 0); the
 *   pump's 1275. B plays nothing (the script's eject is the shell's click). The shell that fills
 *   the gun (the ninth round, chamber and tube) plays SFX_RELOAD_DEFAULT (0x804f, the reload sound
 *   the bots play, botact.c) in place of the click, at the click's time (sndfull, Task F11d: the
 *   user wants a different sound "so we know its full").
 * - The shell's intake reaches 4.4 cm toward the off hand (Task J6a, the user: "need shotgun shells to
 *   be more generous in the direction of the off hand"): a reach point at x 4.0 (the intake at -0.4),
 *   one-sided (clipreachoneside), so a shell within 6 cm of the stretch from the port to it loads; +x of
 *   the body is the off hand's side in either hand (the Phoenix's orb comes from +x, the DY357's
 *   cylinder swings out to +x). The gun hand's side is unchanged.
 * - invitems.c: 9 rounds (the tube and the chamber), AMMOTYPE_SHOTGUN, AMMOFLAG_INCREMENTALRELOAD, no
 *   WEAPONFLAG_DUALWIELD (single wield only); VrTwoHandsGun. Both functions, the single and the double
 *   blast (FUNCFLAG_BURST2: two shells 13 ticks apart), fire from the tube (ammoindex 0); recoildist and
 *   recoilangle 0, slidemax 0. The double blast fires the chambered shell and one from the tube, as in
 *   the original game, then waits for the pump (vrReloadSync); with the tube empty it fires one.
 * - The draw: the Shotgun's equip animation is its single shot script (WEAPONFLAG_00004000), whose
 *   frame 0 the holsters' fast switch holds. Vanilla refills it while holstered (gunroundsspent): a
 *   Shotgun put away is saved and restored instead (vrReloadDrawFill; VRR_NUM_SAVED, 50 with the Stage G rows, covers every row in both hands). */
#define SHOTGUN_ROW(weapon) { \
	(weapon), 0, \
	6.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f, \
	1, { { 4.0f, -0.9f, 12.4f } }, \
	34, 3.0f, { -5.0f, -6.0f, -2.0f }, { 5.0f, 4.0f, 15.0f }, { 0.0f, -1.0f, 7.0f }, 7.4f, 0, 0, 1, 1, \
	-1, -1, 33, { -0.4f, -0.9f, 12.4f }, -1, 62.0f, \
	{ 0.2199f, 0.1171f, 0.9685f }, 105.4f, \
	MODELPART_SHOTGUN_CART, 38, -1, { -0.04f, -0.02f, 4.1f }, { 0.0f, 0.0f, 0.0f }, \
	0, 0, 1275, 472, 5.0f, \
	62.0f, 74.0f, 74.0f, 38, 38, \
	0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 0.0f, \
	1, -1, 0.0f, \
	0.0f, 4.0f, -1, 0, { 0.0f, 0.0f, 0.0f }, { { 0.0f } }, 0.0f, 0.0f, -1, -1, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, 0, \
	0, 0, 0, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, \
	"load shells into the port, pump after each shot", \
	0x804f, \
	0, 0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0, 0, 0.0f, { 0.0f, 0.0f }, \
	1, \
}

/* Rocket Launcher model (GdyrocketZ), read from the ntsc-final ROM's model data, its reload animation
 * (ANIM_GUN_ROCKETLAUNCHER_RELOAD 0x3ef, 140 frames) and its shot (0x3f0, 38 frames, also its equip
 * animation) with tools/vr_measure (g2rows.py; Task G2, 2026-10-20). An intake row with an eject
 * playback held open (the user, 2026-10-19: "have it after firing open the glass hatch but stay where
 * the hand is, then the player reaches over the shoulder to grab and place in a new rocket"):
 * - The model has 38 matrices, the left hand 17 to 32 as the pistols', the body 33. No clip part and
 *   no slide: the rocket, MODELPART_ROCKETLAUNCHER_ROCKET (0x28), is a toggle the gunviscmds and the
 *   part visibility hide, which the reload script shows at frame 24 and hides at 76: 176 vertices all
 *   on matrix 37, a position node under the left wrist (matrix 18), a round body 9.5 cm across and
 *   20.3 cm long along its z (z -7.7..12.6 about its joint). So the row has no clip part (clippart and
 *   clipmtx -1): nothing is drawn in the gun, loaded or empty.
 * - The hatch: two strips of the body's display list on matrices 34 and 36 (20 and 16 vertices, at x
 *   3.4..4.6 and -4.9..-3.5 in their frames), position nodes on the body. The shot never moves them
 *   (its frames 0 to 37 hold them on its frame 0, the idle pose). The reload turns both 90 degrees in
 *   frames 14 to 44 (36 also moves 1.3 cm), holds them open to 104 and turns them back by 124.
 * - After the shot that empties it (emptyejects 1) or B, the gun plays the open (the eject playback:
 *   ejectfrom 14, the first frame they move, 1.3 degrees, to ejectto 44, fully open, on ejectmtx 34 to
 *   ejectmtxlast 36, posed on the body, intakemtx; 35 is an empty node in the run) and holds frame 44
 *   while the gun has no rocket in (ejecthold 1), so the hatch stays open from the shot until the load;
 *   the gun stays on the controller (the reload's body and hands never play). With the script's
 *   SFX_04F9 (1273) at 16 and SFX_0053 (83) at 40 (ejectsnd, ejectsndat; the script plays them at 0.9x,
 *   the row at normal speed); no sndeject. B with a rocket in does nothing but the two weak pulses of a
 *   refused press (vrReloadEjectRefused; the user, 2026-10-20: "rocket launcher B should not work if its
 *   loaded, right now you can keep reloading on top of the rocket"); B on an empty one opens it.
 * - The intake point (intakemtx 33): the hand drops the rocket in from above, its joint going from
 *   {1.2, 13.3, 5.3} at frame 60 down to {-0.6, 10.3, 5.9} at 74 (75 within 0.2 cm), its z along the
 *   body's forward within 15 degrees, and lifts it at 76, where the script hides it. The rocket loads as
 *   its joint (heldtop {0, 0, 0}, near its middle) comes within clipguiderad (6 cm) of the frame-74
 *   point, at any angle (the point test of an intake row), mirrored with the model in left-handed mode
 *   through the body matrix.
 * - After the load the gun plays the rocket going in and the hatch shutting (playfrom 66 to playto 125,
 *   playmtx 34 to playmtxlast 37): the rocket from 2.4 cm above the seat (frame 66, turned 9 degrees
 *   from its seat) down onto it (74), drawn to 76 (playheldto, the script's hidepart), the hatch still
 *   open to 104 and shut by 124. 59 frames at speed 1: 0.98 s of game time. The hand-off (playease 6):
 *   the rocket goes from where the hand held it to the playback's first pose over 6/60 s. The gun fires
 *   once the playback has ended (ejecthold). A rocket brought to the intake while the hatch still opens
 *   loads once it is fully open, so the close starts from the open pose. Sounds (playsnd, playsndat;
 *   the script's, at normal speed): SFX_05D1 (1489) at 77 as the rocket lands, SFX_04F9 (1273) at 106
 *   and SFX_0053 (83) at 123 as the hatch shuts. Left out: SFX_04FA (1274) at 135, after it is shut (the playback would run 11
 *   frames more with the gun held from firing). No sndinsert: the script has none at 74.
 * - No loaded rocket is drawn: the shut strips pass 0.5 cm from where the rocket's axis sat (g2rows.py),
 *   through it, so the rocket goes in as the script hides it.
 * - The rocket is fixed to the wrist through the approach (frames 24 to 76, within 0.01 cm), so the hold
 *   frame only sets the wrist's turn from the model root: frame 74, where it lands.
 * - The hand turn: 72.7 degrees about {0.5272, 0.7148, 0.4594} in the model root's frame (g2rows.py,
 *   handturn.py's method) puts the frame-74 wrist where the Falcon's frame-18 wrist is. The rocket's
 *   joint, 10.3 cm from the wrist, moves 12.2 cm with it; the palm (the middle of the hand bone and its
 *   knuckles) sits against the rocket's side, 5.9 cm from its axis.
 * - The clip turn (Task F12b, Stage G run 2, the user: "flip the rocket in the hand so it looks more
 *   natural"): the rocket turned end for end, 180 degrees about {0.1803, 0.6958, -0.6953} in the model
 *   root's frame after the hand turn ({-0.134, 0.642, -0.755} in the wrist's: the line from the rocket's
 *   axis out to the palm, square to the rocket, so the side against the palm stays against it), about
 *   {0, 0, -2.0} on its axis: the nose, which ran to the little finger's side 13.9 cm from the palm, runs
 *   to the thumb's 15.3 cm from it, and as many of the drawn hand's vertices are inside the body as
 *   unturned (18, 0.6 cm deep at most against 0.7; about the palm's own point, z -1.3, 46 and 1.5 cm;
 *   tools/vr_measure/rocketflip.py). A real pose, not a draw-only look (heldlookdeg): the capture reads
 *   the turned rocket, so the intake tests the joint as drawn, now 2.7 cm from the palm toward the nose
 *   (1.3 before; the joint moved 4 cm in the hand), and the hand-off starts from the rocket as drawn. A
 *   draw-only turn would test a point 1.3 cm from the palm toward the drawn nozzle, so a rocket put in
 *   nose first would load 4 cm later, and the hand-off would start from the unturned rocket, a half
 *   turn in one frame. No clip shift: to check in the headset.
 * - No slide: the row loads on insert (loadsoninsert 1), and the rack fields are zero (rackmtx -1).
 * - The two-hand grip: a two-handed gun (VrTwoHandsGun). The gun stays where the hands hold it at the
 *   shot: a support grip held when the shot opens the hatch stays the support, the two-hand aim on,
 *   until it is let go (sShotSupport in vr_reload.c). Otherwise the hatch open is mid-reload
 *   (vrReloadMidReload: no clip in): a new grip press while it is open, and B's open, keep the grip off
 *   the gun until let go, as after the Farsight's B.
 * - Both functions, the rocket and the homing rocket, fire from the one rocket (ammoindex 0).
 * - invitems.c: 1 round (clip size 1), AMMOTYPE_ROCKET, no WEAPONFLAG_DUALWIELD (single wield only).
 *   Not one of gunroundsspent's guns. */
#define ROCKETLAUNCHER_ROW(weapon) { \
	(weapon), 0, \
	6.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f, \
	0, { { 0.0f } }, \
	-1, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 1, 0, 0, 0, \
	-1, -1, 33, { -0.6f, 10.3f, 5.9f }, -1, 74.0f, \
	{ 0.5272f, 0.7148f, 0.4594f }, 72.7f, \
	MODELPART_ROCKETLAUNCHER_ROCKET, 37, -1, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, \
	0, 0, 0, 0, 0.0f, \
	66.0f, 125.0f, 76.0f, 34, 37, \
	0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 0.0f, \
	0, -1, 0.0f, \
	0.0f, 6.0f, -1, 0, { 0.0f, 0.0f, 0.0f }, { { 0.0f } }, 14.0f, 44.0f, 34, 36, { 1273, 83, 0, 0 }, { 16.0f, 40.0f, 0.0f, 0.0f }, 0, \
	1, 1, 0, { 1489, 1273, 83, 0 }, { 77.0f, 106.0f, 123.0f, 0.0f }, \
	{ 0.1803f, 0.6958f, -0.6953f }, 180.0f, { 0.0f, 0.0f, -2.0f }, \
	{ 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, \
	"load a rocket into the open hatch", \
}

/* Crossbow model (GcrossbowZ), read from the ntsc-final ROM's model data, its reload animation
 * (ANIM_GUN_CROSSBOW_RELOAD 0xd4, 90 frames) and its shot (0xc2, 40 frames) with tools/vr_measure
 * (g2rows.py; Task G3, 2026-10-20). The Shotgun's mechanism without the pump (the user, 2026-10-19:
 * "should pretty much work like the shotguns pellet reload without a b press"): one bolt per insert
 * (shellbyshell), and a bolt put into an empty gun is chambered at once (loadsoninsert):
 * - The model has 46 matrices, the left hand 17 to 32 as the pistols', the body 33, the bow's arms and
 *   string 34 to 44 (they flex in the shot, up to 46 degrees, and hold still in the reload). The gun
 *   has no magazine of its own: the bolts go in one at a time (the engine's reload loops frames 19 to
 *   68 until full, AMMOFLAG_INCREMENTALRELOAD), so the row has no clip part (clippart and clipmtx -1)
 *   and B does nothing. MODELPART_CROSSBOW_BOLT (0x29), which the gunviscmds hide, is the bolt the left
 *   hand brings: 99 vertices all on matrix 45, a position node under the left wrist (matrix 18), 21.5
 *   cm long along its -x from its joint, the rear (nock) end (x -21.5..0, 1.8 across).
 *   MODELPART_CROSSBOW_0028, which the script shows as the loaded bolt at 50, is a toggle with no
 *   geometry.
 * - The groove (intakemtx 33): the hand lays the bolt along the top of the gun at frame 33, its rear
 *   end at {-0.3, 0.1, 6.0} and its tip at {-0.5, 0.7, 27.5} (along the body's +z within 2 degrees),
 *   holds it there to 38, slides it back to the nock, its rear end at {0.3, -1.3, -6.7} (frames 38 to
 *   42), holds it to 45 and lifts it from 46; the script hides it at 50. The bolt loads as its middle
 *   (heldtop {-10.75, 0, 0}) comes within clipguiderad (6 cm) of where the middle lies at frame 33,
 *   {-0.4, 0.4, 16.8}, at any angle (the point test of an intake row), mirrored with the model in
 *   left-handed mode through the body matrix: the middle, not the rear end the animation lays in first,
 *   as the hand holds the bolt by its front (the palm 18.5 cm along it from the rear end) and any angle
 *   loads, so the bolt laid along the gun's top is what counts. Each adds one bolt (shellbyshell) while
 *   the gun has room.
 * - After the load the gun plays the bolt going in (playfrom 33 to playto 45, matrix 45 alone, playmtx
 *   and playmtxlast 45, posed on the body): in the groove, slid back to the nock and seated there; 12
 *   frames at speed 1, 0.2 s of game time. The hand-off (playease 6): the bolt goes from where the hand
 *   held it to the groove over 6/60 s. Then the loaded bolt stays drawn at the nock, frame 45, while a
 *   bolt is chambered (playhold 1; playheldto 45, playto), so the gun is seen loaded: a shot that leaves
 *   bolts keeps it, the last one takes it away. A bolt put in behind a chambered one goes into the tube
 *   unseen, with no playback, so the loaded bolt stays at the nock (its 1276 still plays).
 * - Sounds as the ROM's reload script plays them (invitems.c): only SFX_04FC (1276) at frame 45 as the
 *   bolt seats in the nock: the row's sndinsert2, 12/60 s after the load at 33 (insert2delay 12;
 *   sndinsert 0). B plays nothing (the gun ignores it).
 * - The bolt is fixed to the wrist in every frame of the reload (within 0.01 cm), so the hold frame only
 *   sets the wrist's turn from the model root: frame 33, where the bolt is laid in the groove.
 * - The hand turn: 76.0 degrees about {0.5801, 0.8103, 0.0828} in the model root's frame (g2rows.py,
 *   handturn.py's method) puts the frame-33 wrist where the Falcon's frame-18 wrist is. The bolt's
 *   middle, 14.4 cm from the wrist, moves 14.8 cm with it. The animation's hand holds the bolt near its
 *   tip, the palm 8.0 cm off its line (the fingers on it).
 * - The clip turn and shift (Task F12d, Stage G run 2, the user: "the bolt is straight up with the point
 *   down in the hand"): the copy model's root is the off controller's frame rolled -1.30 rad about its
 *   z (bgunRender's free left hand), so the bolt the hand turn leaves, {0.86, -0.50, -0.09} rear to tip
 *   in the root's frame, points 75 degrees below the controller's level: down. Turned 94.9 degrees
 *   about {-0.5025, -0.8646, 0} (the root's frame) about its point nearest the palm, 18.5 cm from the
 *   rear end ({-18.5, 0, 0} on matrix 45), it points along the controller's forward, tip first, level,
 *   as the groove runs when the hands are level; then moved 7.75 cm toward its tip (clipshift {-7.75,
 *   0, 0}), so the fingers hold its middle, 10.75 cm of it each way. No more of the drawn hand inside it
 *   than at any shift (5 vertices, the fingertips on it). The capture reads it so: the intake tests the
 *   middle as drawn, on the controller at {-5.6, 4.5, 0.4} cm (it was {-3.7, 12.0, 1.1}, 12 cm up the
 *   controller, which is why the user's marks put it 7 to 12 cm above the gun) (tools/vr_measure/
 *   boltturn.py).
 * - The reach (clipreach {0, 3.0, -7.0}, with x negated the same point): the user's ten marks (run 13,
 *   the bolt laid along the top anywhere from the nock to the front of the groove) replayed with the
 *   turned bolt put its middle 9.1 to 22.1 cm from the intake point, 0 to 5.8 cm above the body and from
 *   z -5.2 to 7.9; within 1.3 to 3.9 cm of the stretch from the intake point back along the groove to
 *   {0, 3.0, -7.0}, under clipguiderad (6), so a bolt laid there loads (vrReloadGuide).
 * - No slide: the row loads on insert (loadsoninsert 1), and the rack fields are zero (rackmtx -1). The
 *   core: an insert adds the bolt to the clip and, with the chamber empty, chambers it (vrrSettleState
 *   with loadsoninsert), so the gun is ready at once; with a bolt chambered it waits behind it.
 * - A one-handed gun (WEAPONFLAG_ONEHANDED; not VrTwoHandsGun): no support grip to keep off. Not mid-
 *   reload at any time (the tube never leaves, as the Shotgun's).
 * - Both functions, the sedate and the instant kill, fire from the one load (ammoindex 0).
 * - invitems.c: 5 bolts (clip size 5), AMMOTYPE_CROSSBOW, no WEAPONFLAG_DUALWIELD (single wield only).
 *   Vanilla refills it while holstered (gunroundsspent): a Crossbow put away is saved and restored
 *   instead (vrReloadDrawFill; VRR_NUM_SAVED 50 covers every row in both hands). */
#define CROSSBOW_ROW(weapon) { \
	(weapon), 0, \
	6.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f, \
	1, { { 0.0f, 3.0f, -7.0f } }, \
	-1, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 1, 0, 0, 1, \
	-1, -1, 33, { -0.4f, 0.4f, 16.8f }, -1, 33.0f, \
	{ 0.5801f, 0.8103f, 0.0828f }, 76.0f, \
	MODELPART_CROSSBOW_BOLT, 45, -1, { -10.75f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, \
	0, 0, 0, 1276, 12.0f, \
	33.0f, 45.0f, 45.0f, 45, 45, \
	0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 0.0f, \
	0, -1, 0.0f, \
	0.0f, 6.0f, -1, 0, { 0.0f, 0.0f, 0.0f }, { { 0.0f } }, 0.0f, 0.0f, -1, -1, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, 0, \
	0, 0, 1, { 0, 0, 0, 0 }, { 0.0f, 0.0f, 0.0f, 0.0f }, \
	{ -0.5025f, -0.8646f, 0.0f }, 94.9f, { -18.5f, 0.0f, 0.0f }, \
	{ -7.75f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, \
	"slide a bolt into the groove", \
}

/* DY357 Magnum and DY357-LX models (Gdy357Z, Gdy357trentZ: the same 47 matrices, parts and poses, the
 * LX's rounds a few vertices more), read from the ntsc-final ROM's model data and their reload
 * animation (ANIM_GUN_DY357_RELOAD 0x408, 166 frames, which vrGetReloadAnimIdForWeapon picks for a pair
 * too: the pair's 0x420 swings the cylinder the same but has no hand) with tools/vr_measure
 * (g4revolver.py; Task G4, 2026-10-20). A revolver (the design: "B swings out the cylinder; shoulder
 * gives a speedloader; release at the cylinder; flick or touch to close"): an intake row held open from
 * B through the load to the close:
 * - The model has 47 matrices, the left hand 17 to 32 as the pistols', the body 33. No clip part and no
 *   slide: the cylinder is matrix 34 (37 vertices of its own display list, 4.2 across and 4.8 long, x
 *   -2.1..2.1, z 0..4.8 in its frame), a position node on the body with 39 beside it, the chambers' node,
 *   and 40 to 45 under 39, empty position nodes with the parts 0x0a to 0x0f: the six chambers' mouths on
 *   the cylinder's rear face, where the engine's reload casings come from (bgunUpdateMagnum). Nothing is
 *   drawn loaded in the cylinder.
 * - B (ejecthold): the reload swings the cylinder and the chambers 3.8 cm out along the body's +x, with
 *   no turn, in frames 48 to 51 (1.1, 2.4, 3.4 and 3.8 cm), holds it there to 145 and swings it back in
 *   146 to 149. B plays the swing out (the eject playback: ejectfrom 45 to ejectto 56, the coordinator's
 *   range around it, on ejectmtx 34 to ejectmtxlast 45, posed on the body, intakemtx; 35 to 38, in the
 *   run, sit on their rest pose through the reload) and holds frame 56 while the gun has no rounds in and
 *   then, loaded, until the close (vrReloadHeldOpen). Sounds (ejectsnd, ejectsndat): the script's
 *   SFX_05CF (1487) at 50 as it swings, and SFX_01D8 (472, the script's at 80, played at 1.83x) at 56,
 *   the rounds falling out; no sndeject. At the swing's end the spent rounds' casings fall from the
 *   chambers (ejectcasings: the clip size less the live rounds B takes back, the engine's path for the
 *   script's six popoutsackofpills at 80 to 85, which always threw six).
 * - The load (an intake row): the six rounds, MODELPART_MAGNUM_CART1 to CART6 (0x28 to 0x2d, heldpart to
 *   heldpartlast), six toggles the gunviscmds hide, each a round 1.2 cm across and 3.4 long on matrix 46,
 *   a position node under the left wrist (matrix 18), together a ring 3.8 cm across (x -1.8..1.9, y
 *   -2..1.9, z 0..3.4 about the joint), their noses forward along its +z. The hand brings them up behind
 *   the open cylinder and pushes them in along the body's +z (axis within 4 degrees): their noses' middle
 *   (heldtop {0.03, -0.08, 3.4}, the LX's {0.05, -0.08, 3.4}) goes from {3.86, 2.37, 2.77} at frame 110
 *   to {3.86, 2.29, 4.04} at 113, 0.2 cm short of the rear face, and to {4.17, 1.93, 8.31} at 121, all in
 *   (the cylinder runs z 4.23 to 9.03 out); the script shows them at 92 and hides them at 121. They load
 *   as heldtop comes within clipguiderad (6 cm) of the open cylinder's rear face middle, {3.8, 2.2, 4.2}
 *   (intakepos, the face at f56: {3.8, 2.16, 4.23}), at any angle, mirrored with the model in
 *   left-handed mode through the body matrix: all six at once, as many as the reserve has up to six
 *   (the core's clip grab), a speedloader's. A gun held open takes them only once the swing out has
 *   ended (ejecthold).
 * - After the load the gun plays the rounds going in (playfrom 113 to playto 121 on playmtx 34 to
 *   playmtxlast 46: the open cylinder, as frame 56 has it, and the rounds), drawn to 121 (playheldto,
 *   the script's hidepart), with SFX_01D8 (472, the script's at 114 at 1.21x) at 114 (playsnd); no
 *   sndinsert. The hand-off (playease 6): the rounds go from where the hand held them to frame 113's pose
 *   over 6/60 s, the playback waiting at 113 meanwhile (playstraightmtx), then go in.
 * - The rounds straight in the chambers (playstraightmtx 34, Task H6b: the user saw them go in at an angle
 *   and clip outside): the animation's own rounds lean 3.0 degrees off the cylinder's axis at 113 (1.1 to
 *   2.4 through 121), are turned 6 to 14 degrees off the chambers about it (matrix 46 on 34 turns -66.1
 *   to -73.6 about +z, 113 to 121; the six rounds' ring matches the six chamber mouths', nodes 40 to 45, to
 *   1 degree and 0.03 cm, so one chamber step is -60), and their rear ends sit up to 0.6 cm off the
 *   mouths, so they reach 2.2 to 2.4 cm from the axis inside the 2.1 cylinder. Drawn instead on 34 with a
 *   roll of -60 degrees (playstraightroll) and the origin at {0.05, 0.07} cm (playstraightpos: the
 *   rounds' rear ends on the mouths within 0.03 cm, the LX's 0.06), only the origin's z the animation's
 *   (-3.59 cm at 113, the noses 0.2 short of the rear face, to 0.68 at 121): 0 degrees off the axis, 2.01
 *   cm from it at most. The ease had also overlapped the push: frames 114 to 119 drew the rounds already
 *   in (the noses past the face from 114) with 5/6 down to 1/6 of the hand's angle at the load still to
 *   turn out (any angle loads).
 * - The close (rackslap with a close: the core's rack of a gun loaded with an empty chamber is the
 *   close; loadsoninsert 0, so the load leaves it waiting, VRR_NEED_RACK, with the rounds in and the
 *   cylinder out). The original closes it with no hand (a flick of the gun, frames 146 to 149, SFX_05CD
 *   1485 at 147). Here either a flick (flickaxis {0, 0, -1}: a roll about the barrel's -z, either way
 *   since H3e, at VRR_FLICK_RADS) or a push of the free hand
 *   (its palm within slaprad 6 cm of the open cylinder's middle, slappoint {3.8, 2.2, 6.8}, moving along
 *   closedir {-1, 0, 0}, the way the cylinder goes in, at VRR_TOUCH_SPEED) closes it, once the rounds
 *   are in (vrReloadStepClose). The close plays frames 145 (the last open) to 150 (closefrom, closeto;
 *   the coordinator's 140 to 145 hold it still, so they are left out: the flick shuts it at once) on the
 *   eject's matrices, with sndrack, SFX_05CD (1485), at 147 (closesndat); the gun fires once it is shut.
 * - The rounds are fixed to the wrist through the load (frames 92 to 121, within 0.01 cm), so the hold
 *   frame only sets the wrist's turn from the model root: frame 113, where they meet the face.
 * - The hand turn: 61.2 degrees about {0.8116, 0.2669, 0.5196} in the model root's frame (g4revolver.py,
 *   handturn.py's method) puts the frame-113 wrist where the Falcon's frame-18 wrist is. heldtop, 13.7 cm
 *   from the wrist, moves 13.6 cm with it. The palm sits 4.9 cm behind the rounds' rear ends, {2.92,
 *   2.33, -4.85} in matrix 46's frame (the speedloader's body, not modelled, between). No clip turn or
 *   shift (an intake row): to check in the headset.
 * - A partial reload: B on a revolver with live rounds takes them back to the reserve with the chambers'
 *   spent casings falling (every gun's B empties it, Task F4), and the speedloader then loads up to six
 *   from the reserve, so the gun ends with what the original's top-up leaves (its loaded rounds plus as
 *   many of the reserve's as fit).
 * - The rack fields are zero (rackmtx -1): no rack area. A one-handed gun (not VrTwoHandsGun).
 * - The pistol whip (the second function) has no ammo and works whatever the cylinder does.
 * - invitems.c (invammo_dy357): 6 rounds (clip size 6), AMMOTYPE_MAGNUM, CASING_STANDARD,
 *   WEAPONFLAG_DUALWIELD: a pair reloads as every pair, the left gun's by Y (the gun side flips). Vanilla
 *   refills both DY357s while holstered (gunroundsspent): put away, each is saved and restored instead
 *   (vrReloadDrawFill; VRR_NUM_SAVED 54 covers every row in both hands). */
#define DY357_ROW(weapon) { \
	(weapon), 0, \
	6.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f, \
	0, { { 0.0f } }, \
	-1, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 0, 1, 0, 0, \
	-1, -1, 33, { 3.8f, 2.2f, 4.2f }, -1, 113.0f, \
	{ 0.8116f, 0.2669f, 0.5196f }, 61.2f, \
	MODELPART_MAGNUM_CART1, 46, -1, { 0.03f, -0.08f, 3.4f }, { 0.0f, 0.0f, 0.0f }, \
	0, 0, 1485, 0, 0.0f, \
	113.0f, 121.0f, 121.0f, 34, 46, \
	0.0f, 0.0f, 0.0f, { 3.8f, 2.2f, 6.8f }, 6.0f, \
	0, -1, 0.0f, \
	0.0f, 6.0f, -1, 0, { 0.0f, 0.0f, 0.0f }, { { 0.0f } }, 45.0f, 56.0f, 34, 45, { 1487, 472, 0, 0 }, { 50.0f, 56.0f, 0.0f, 0.0f }, 0, \
	1, 0, 0, { 472, 0, 0, 0 }, { 114.0f, 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, \
	{ 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, \
	"load the open cylinder, flick or push it shut", \
	0, \
	MODELPART_MAGNUM_CART6, \
	145.0f, 150.0f, 147.0f, \
	{ 0.0f, 0.0f, -1.0f }, \
	{ -1.0f, 0.0f, 0.0f }, \
	1, \
	34, -60.0f, { 0.05f, 0.07f }, \
}

static struct vrreloaddef g_VrReloadDefs[] = {
	FALCON2_ROW(WEAPON_FALCON2),
	FALCON2_ROW(WEAPON_FALCON2_SILENCER),
	FALCON2_ROW(WEAPON_FALCON2_SCOPE),
	MAGSEC4_ROW(WEAPON_MAGSEC4),
	MAULER_ROW(WEAPON_MAULER),
	TRANQUILIZER_ROW(WEAPON_TRANQUILIZER),
	TRANQUILIZER_ROW(WEAPON_PSYCHOSISGUN),
	PHOENIX_ROW(WEAPON_PHOENIX),
	CMP150_ROW(WEAPON_CMP150),
	AR34_ROW(WEAPON_AR34),
	K7AVENGER_ROW(WEAPON_K7AVENGER),
	RCP120_ROW(WEAPON_RCP120),
	DRAGON_ROW(WEAPON_DRAGON),
	SUPERDRAGON_ROW(WEAPON_SUPERDRAGON),
	SUPERDRAGON_GRENADE_ROW(WEAPON_SUPERDRAGON),
	SNIPERRIFLE_ROW(WEAPON_SNIPERRIFLE),
	CALLISTO_ROW(WEAPON_CALLISTO),
	CYCLONE_ROW(WEAPON_CYCLONE),
	LAPTOPGUN_ROW(WEAPON_LAPTOPGUN),
	SHOTGUN_ROW(WEAPON_SHOTGUN),
	REAPER_ROW(WEAPON_REAPER),
	DEVASTATOR_ROW(WEAPON_DEVASTATOR),
	FARSIGHT_ROW(WEAPON_FARSIGHT),
	ROCKETLAUNCHER_ROW(WEAPON_ROCKETLAUNCHER),
	CROSSBOW_ROW(WEAPON_CROSSBOW),
	DY357_ROW(WEAPON_DY357MAGNUM),
	DY357_ROW(WEAPON_DY357LX),
};

#define NUM_DEFS ((s32)(sizeof(g_VrReloadDefs) / sizeof(g_VrReloadDefs[0])))

struct vrreloaddef *vrReloadGetDef(s32 weaponnum, s32 ammoindex)
{
	s32 i;

	for (i = 0; i < NUM_DEFS; i++) {
		if (g_VrReloadDefs[i].weaponnum == weaponnum
				&& (ammoindex < 0 || g_VrReloadDefs[i].ammoindex == ammoindex)) {
			return &g_VrReloadDefs[i];
		}
	}

	return NULL;
}

s32 vrReloadDefCount(void)
{
	return NUM_DEFS;
}

struct vrreloaddef *vrReloadDefAt(s32 index)
{
	if (index < 0 || index >= NUM_DEFS) {
		return NULL;
	}

	return &g_VrReloadDefs[index];
}

s32 vrReloadHeldPartLast(const struct vrreloaddef *def)
{
	return def->heldpartlast > def->heldpart ? def->heldpartlast : def->heldpart;
}
