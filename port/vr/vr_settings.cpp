#include <stdio.h>
#include <string.h>

#include "vr_settings.h"

extern "C" float inputRumbleGetStrength(int playernum);
extern "C" void inputRumbleSetStrength(int playernum, int strength);

extern "C" void vrSettingsSave(void)
{
    FILE *f = fopen(VR_INI_PATH, "w");
    if (!f) return;

    fprintf(f, "[VR]\n");
    fprintf(f, "ManualReloading=%d\n", VrManualReloading ? 1 : 0);
    fprintf(f, "LaserDotForAll=%d\n", VrlaserDotForALL ? 1 : 0);
    fprintf(f, "SeatedMode=%d\n", VrSeatedMode ? 1 : 0);
    fprintf(f, "MotionThrowing=%d\n", VrMotionThrowing ? 1 : 0);
    fprintf(f, "Vibration=%.4f\n", inputRumbleGetStrength(g_ExtMenuPlayer));
    fprintf(f, "StereoCrosshair=%.4f\n", VrStereoCrosshair);
    fprintf(f, "HudDistance=%.4f\n", VrHudDistance);
    fprintf(f, "WeaponRecoil=%d\n", VrWeaponRecoil ? 1 : 0);
    fprintf(f, "WorldScale=%.4f\n", VrSetWorldScale);
    fprintf(f, "PauseHub=%d\n", VrPauseHub ? 1 : 0);
    fprintf(f, "StickClickToCrouch=%d\n", VrStickClickToCrouch ? 1 : 0);
    fprintf(f, "SnapTurn=%.1f\n", VrUseSnapTurn);
    fprintf(f, "TwoHandedAiming=%d\n", VrTwoHandAim ? 1 : 0);
    fprintf(f, "LeftHandedMode=%d\n", VrLeftHandedMode ? 1 : 0);
    fprintf(f, "SwapJoysticks=%d\n", VrSwapJoysticks ? 1 : 0);
    fprintf(f, "HideArms=%d\n", VrHideArms ? 1 : 0);
    fprintf(f, "; Your standing EYE height in cm -- where your eyes are off the floor, which is\n");
    fprintf(f, "; what the headset reports, roughly 13 cm below the top of your head. Set it from\n");
    fprintf(f, "; the live reading beside the menu slider rather than from your stature.\n");
    fprintf(f, "PlayerHeight=%.1f\n", VrPlayerHeight);
    fprintf(f, "; 1 = stand at the height of the character you are playing instead of your own,\n");
    fprintf(f, "; so Elvis is short and Mr Blonde towers. 0 = you are your own height throughout.\n");
    fprintf(f, "MatchCharacterHeight=%d\n", VrMatchCharacterHeight ? 1 : 0);

    // --- VR hand placement (no menu UI; edit here) --------------------------------------------
    fprintf(f, "\n");
    fprintf(f, "; Where the gun sits in your hand, in the CONTROLLER's own frame (game units,\n");
    fprintf(f, "; roughly cm). Adjust if the model's trigger finger does not land on your real\n");
    fprintf(f, "; one. X = right, Y = up, Z = forward along the barrel. Everything else about the\n");
    fprintf(f, "; placement is measured and baked in; these vary with hand size and grip style.\n");
    fprintf(f, "GunOffX=%.4f\n", VrGunOffX);
    fprintf(f, "GunOffY=%.4f\n", VrGunOffY);
    fprintf(f, "GunOffZ=%.4f\n", VrGunOffZ);
    fprintf(f, "\n");
    fprintf(f, "; 0..1. How tightly the elbows are pulled in toward your body. 0 leaves them at the\n");
    fprintf(f, "; animation's rest pose (they splay outward), 1 pins them hard against the torso.\n");
    fprintf(f, "ArmElbowTuck=%.4f\n", VrArmElbowTuck);
    fprintf(f, "\n");
    fprintf(f, "; How fast the virtual torso turns to follow your head, per tick. The elbow anchor\n");
    fprintf(f, "; is held steady relative to that torso, so this trades two things off: too LOW and\n");
    fprintf(f, "; the elbows lag behind when you physically turn your whole body; too HIGH and they\n");
    fprintf(f, "; drift when you merely glance around. ~0.02 suits most people.\n");
    fprintf(f, "ArmBodyFollow=%.4f\n", VrArmBodyFollow);
    fprintf(f, "\n");
    fprintf(f, "; 1 = the empty off-hand closes into a fist while you squeeze the left grip,\n");
    fprintf(f, "; 0 = it stays open. Single-handed weapons only, since on two-handers that grip\n");
    fprintf(f, "; already means 'take the two-handed hold'.\n");
    fprintf(f, "FistClench=%d\n", VrFistClench);
    fclose(f);
}

extern "C" void vrSettingsLoad(void)
{
    FILE *f = fopen(VR_INI_PATH, "r");
    if (!f) return;

    char line[128];
    char key[64];
    float fval;
    int ival;

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '[' || line[0] == '\n' || line[0] == ';' || line[0] == '#') continue;

        if (sscanf(line, "%63[^=]=%d", key, &ival) == 2) {
            if (strcmp(key, "ManualReloading") == 0) VrManualReloading = ival != 0;
            else if (strcmp(key, "LaserDotForAll") == 0) VrlaserDotForALL = ival != 0;
            else if (strcmp(key, "SeatedMode") == 0) VrSeatedMode = ival != 0;
            else if (strcmp(key, "MotionThrowing") == 0) VrMotionThrowing = ival != 0;
            else if (strcmp(key, "WeaponRecoil") == 0) VrWeaponRecoil = (ival != 0);
            else if (strcmp(key, "StickClickToCrouch") == 0) VrStickClickToCrouch = (ival != 0);
            else if (strcmp(key, "PauseHub") == 0) VrPauseHub = (ival != 0);
            else if (strcmp(key, "TwoHandedAiming") == 0) VrTwoHandAim = (ival != 0);
            else if (strcmp(key, "LeftHandedMode") == 0) VrLeftHandedMode = (ival != 0);
            else if (strcmp(key, "SwapJoysticks") == 0) VrSwapJoysticks = (ival != 0);
            else if (strcmp(key, "HideArms") == 0) VrHideArms = (ival != 0);
            else if (strcmp(key, "MatchCharacterHeight") == 0) VrMatchCharacterHeight = (ival != 0);
            else if (strcmp(key, "FistClench") == 0) VrFistClench = ival;
        }

        if (sscanf(line, "%63[^=]=%f", key, &fval) == 2) {
            if (strcmp(key, "Vibration") == 0) inputRumbleGetStrength(fval);
            else if (strcmp(key, "StereoCrosshair") == 0) {
                if (fval < HUD_STEREO_DEPTH_MIN) fval = HUD_STEREO_DEPTH_MIN;
                if (fval > HUD_STEREO_DEPTH_MAX) fval = HUD_STEREO_DEPTH_MAX;
                VrStereoCrosshair = fval;
            }
            else if (strcmp(key, "HudDistance") == 0) {
                if (fval < HUD_DISTANCE_MIN) fval = HUD_DISTANCE_MIN;
                if (fval > HUD_DISTANCE_MAX) fval = HUD_DISTANCE_MAX;
                VrHudDistance = fval;
            }
            else if (strcmp(key, "WorldScale") == 0) {
                if (fval < WORLDSCALE_MIN) fval = WORLDSCALE_MIN;
                if (fval > WORLDSCALE_MAX) fval = WORLDSCALE_MAX;
                VrSetWorldScale = fval;
            }
            else if (strcmp(key, "PlayerHeight") == 0) {
                if (fval < PLAYERHEIGHT_MIN) fval = PLAYERHEIGHT_MIN;
                if (fval > PLAYERHEIGHT_MAX) fval = PLAYERHEIGHT_MAX;
                VrPlayerHeight = fval;
            }
            else if (strcmp(key, "SnapTurn") == 0) {
                if (fval < 0.0f) fval = 0.0f;
                if (fval > 90.0f) fval = 90.0f;
                VrUseSnapTurn = fval;
            }
            else if (strcmp(key, "ArmElbowTuck") == 0) VrArmElbowTuck = fval;
            else if (strcmp(key, "ArmBodyFollow") == 0) VrArmBodyFollow = fval;
            else if (strcmp(key, "GunOffX") == 0) VrGunOffX = fval;
            else if (strcmp(key, "GunOffY") == 0) VrGunOffY = fval;
            else if (strcmp(key, "GunOffZ") == 0) VrGunOffZ = fval;
        }
    }

    fclose(f);
}
