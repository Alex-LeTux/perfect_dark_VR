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
    fprintf(f, "WeaponRecoil=%d\n", VrWeaponRecoil ? 1 : 0);
    fprintf(f, "WorldScale=%.4f\n", VrSetWorldScale);
    fprintf(f, "UseSnapTurn=%d\n", VrUseSnapTurn ? 1 : 0);
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
        if (line[0] == '[' || line[0] == '\n') continue;

        if (sscanf(line, "%63[^=]=%d", key, &ival) == 2) {
            if (strcmp(key, "ManualReloading") == 0) VrManualReloading = ival != 0;
            else if (strcmp(key, "LaserDotForAll") == 0) VrlaserDotForALL = ival != 0;
            else if (strcmp(key, "SeatedMode") == 0) VrSeatedMode = ival != 0;
            else if (strcmp(key, "MotionThrowing") == 0) VrMotionThrowing = ival != 0;
            else if (strcmp(key, "WeaponRecoil") == 0) VrWeaponRecoil = (ival != 0);
            else if (strcmp(key, "UseSnapTurn") == 0) VrUseSnapTurn = (ival != 0);
        }

        if (sscanf(line, "%63[^=]=%f", key, &fval) == 2) {
            if (strcmp(key, "Vibration") == 0) inputRumbleGetStrength(fval);
            else if (strcmp(key, "StereoCrosshair") == 0) {
                if (fval < HUD_STEREO_DEPTH_MIN) fval = HUD_STEREO_DEPTH_MIN;
                if (fval > HUD_STEREO_DEPTH_MAX) fval = HUD_STEREO_DEPTH_MAX;
                VrStereoCrosshair = fval;
            }
            else if (strcmp(key, "WorldScale") == 0) {
                if (fval < WORLDSCALE_MIN) fval = WORLDSCALE_MIN;
                if (fval > WORLDSCALE_MAX) fval = WORLDSCALE_MAX;
                VrSetWorldScale = fval;
            }
        }
    }

    fclose(f);
}
