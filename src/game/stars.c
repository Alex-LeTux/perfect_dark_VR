#include <ultra64.h>
#include <game/gfxmemory.h>
#include "constants.h"
#include "game/game_006900.h"
#include "game/tex.h"
#include "game/stars.h"
#include "game/game_1531a0.h"
#include "game/camera.h"
#include "bss.h"
#include "lib/vi.h"
#include "lib/memp.h"
#include "lib/rng.h"
#include "lib/mtx.h"
#include "data.h"
#include "types.h"

s32 g_StarCount;
s8 *g_StarPositions = NULL;
f32 *g_StarData3;
s32 g_StarGridSize;
s32 *g_StarPosIndexes;

bool g_StarsBelowHorizon = false;

/**
 * Star rendering in the 3D world.
 *
 * Each star is a tiny square placed on a sphere centered on the
 * camera (camera-relative coordinates). The vertices are generated only
 * once in starsReset. Every frame, the same matrix used for
 * light halos is loaded: the game's view rotation (camGetWorldToScreenMtxf)
 * without translation. The RSP then performs the transformation, projection, and clipping.
 */

// Radius of the star sphere, in world units around the camera.
#define STARS_RADIUS 6000.0f

// Angular radius of a star
#define STARS_ANGSIZE 0.002f

// Vertices reserved per star: 4 for the quad. struct starvtx
// is 12 bytes, so 4 * 12 = 48 ensures that each star's block
// starts on an 8-byte aligned address (required for RSP DMA).
#define STARS_STRIDE 4

// Stars per gSPVertex
#define STARS_BATCH 5

// Local vertex for this file, same layout (12 bytes) as the game's
// vertex struct (s16 x/y/z, u8 flags, u8 colour, s16 s/t). Defined here to not depend
// on its exact name or an include. You can replace it with the game's struct.
struct starvtx {
    s16 x;
    s16 y;
    s16 z;
    u8 flags;
    u8 colour;
    s16 s;
    s16 t;
};

struct starvtx *g_StarVertices = NULL;

void stars0f135c70(void)
{
    u32 stack[4];
    struct coord coord;
    f32 mult;
    s32 i;
    s32 j;
    s32 k;
    f32 tmp = g_StarGridSize * 0.5f;

    for (i = 0; i < 6; i++) {
        for (j = 0; j <= g_StarGridSize; j++) {
            for (k = 0; k <= g_StarGridSize; k++) {
                s32 index = ((i * (g_StarGridSize + 1) * (g_StarGridSize + 1)) + k + (j * (g_StarGridSize + 1))) * 3;

                switch (i) {
                    case 0:
                    case 1:
                        coord.x = (i == 0 ? -1.0f : 1.0f);
                        coord.y = k / tmp - 1;
                        coord.z = j / tmp - 1;
                        break;
                    case 2:
                    case 3:
                        coord.y = (i == 2 ? -1.0f : 1.0f);
                        coord.x = j / tmp - 1;
                        coord.z = k / tmp - 1;
                        break;
                    case 4:
                    case 5:
                        coord.z = (i == 4 ? -1.0f : 1.0f);
                        coord.x = k / tmp - 1;
                        coord.y = j / tmp - 1;
                        break;
                }

                mult = 1.0f / sqrtf(coord.f[0] * coord.f[0] + coord.f[1] * coord.f[1] + coord.f[2] * coord.f[2]);

                g_StarData3[index + 0] = coord.x * mult;
                g_StarData3[index + 1] = coord.y * mult;
                g_StarData3[index + 2] = coord.z * mult;
            }
        }
    }
}

/**
 * Insert a star position *after* the given index.
 */
void starInsert(s32 index, struct coord *arg1)
{
    s32 i;

    // Shuffle g_StarPositions forward after the insertion point
    for (i = g_StarPosIndexes[g_StarGridSize * 6 * g_StarGridSize] - 1; i >= g_StarPosIndexes[index + 1]; i--) {
        g_StarPositions[i * 3 + 3] = g_StarPositions[i * 3 + 0];
        g_StarPositions[i * 3 + 4] = g_StarPositions[i * 3 + 1];
        g_StarPositions[i * 3 + 5] = g_StarPositions[i * 3 + 2];
    }

    // Write new data
    g_StarPositions[g_StarPosIndexes[index + 1] * 3 + 0] = arg1->x * 127;
    g_StarPositions[g_StarPosIndexes[index + 1] * 3 + 1] = arg1->y * 127;
    g_StarPositions[g_StarPosIndexes[index + 1] * 3 + 2] = arg1->z * 127;

    // Increment indexes after the insertion point
    for (i = index + 1; i <= g_StarGridSize * 6 * g_StarGridSize; i++) {
        g_StarPosIndexes[i]++;
    }
}

#define ABS2(value) ((value) < 0 ? -(value) : (value))

static s16 starsRoundS16(f32 value)
{
    return (s16)(value < 0.0f ? value - 0.5f : value + 0.5f);
}

static void starsSetVertex(struct starvtx *vtx, f32 x, f32 y, f32 z)
{
    vtx->x = starsRoundS16(x);
    vtx->y = starsRoundS16(y);
    vtx->z = starsRoundS16(z);
    vtx->flags = 0;
    vtx->colour = 0; // colour table index: no effect, colour comes from the prim colour
    vtx->s = 0;
    vtx->t = 0;
}

/**
 * Transforms g_StarPositions (s8 directions) into world quads.
 *
 * The vertex order follows g_StarPositions (sorted by grid cell
 * by starInsert), so the stars of a cell have contiguous
 * vertices: star n => vertices [n * STARS_STRIDE, n * STARS_STRIDE + 3].
 */
static void starsBuildVertices(void)
{
    s32 i;
    s32 count = g_StarPosIndexes[6 * g_StarGridSize * g_StarGridSize];
    f32 s = STARS_RADIUS * STARS_ANGSIZE;

    for (i = 0; i < count; i++) {
        struct coord dir;
        struct coord t1;
        struct coord t2;
        f32 len;
        f32 px, py, pz;
        struct starvtx *vtx = &g_StarVertices[i * STARS_STRIDE];

        dir.x = g_StarPositions[i * 3 + 0];
        dir.y = g_StarPositions[i * 3 + 1];
        dir.z = g_StarPositions[i * 3 + 2];

        len = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        if (len < 1.0f) {
            len = 1.0f;
        }
        dir.x /= len;
        dir.y /= len;
        dir.z /= len;

        /* Tangent: avoid degenerate cross product near ±Y */
        if (dir.y > 0.9f || dir.y < -0.9f) {
            t1.x = 0.0f;
            t1.y = -dir.z;
            t1.z =  dir.y;
        } else {
            t1.x =  dir.z;
            t1.y =  0.0f;
            t1.z = -dir.x;
        }

        len = sqrtf(t1.x * t1.x + t1.y * t1.y + t1.z * t1.z);
        t1.x /= len;
        t1.y /= len;
        t1.z /= len;

        /* t2 = dir × t1, then renormalize (numerical safety) */
        t2.x = dir.y * t1.z - dir.z * t1.y;
        t2.y = dir.z * t1.x - dir.x * t1.z;
        t2.z = dir.x * t1.y - dir.y * t1.x;

        len = sqrtf(t2.x * t2.x + t2.y * t2.y + t2.z * t2.z);
        t2.x /= len;
        t2.y /= len;
        t2.z /= len;

        px = dir.x * STARS_RADIUS;
        py = dir.y * STARS_RADIUS;
        pz = dir.z * STARS_RADIUS;

        /* Square in the tangent plane — 4 distinct vertices, NO overwrite */
        starsSetVertex(&vtx[0],
                       px - t1.x * s - t2.x * s,
                       py - t1.y * s - t2.y * s,
                       pz - t1.z * s - t2.z * s);
        starsSetVertex(&vtx[1],
                       px + t1.x * s - t2.x * s,
                       py + t1.y * s - t2.y * s,
                       pz + t1.z * s - t2.z * s);
        starsSetVertex(&vtx[2],
                       px + t1.x * s + t2.x * s,
                       py + t1.y * s + t2.y * s,
                       pz + t1.z * s + t2.z * s);
        starsSetVertex(&vtx[3],
                       px - t1.x * s + t2.x * s,
                       py - t1.y * s + t2.y * s,
                       pz - t1.z * s + t2.z * s);
    }

    osWritebackDCache(g_StarVertices, count * STARS_STRIDE * sizeof(struct starvtx));
}

void starsReset(void)
{
    s32 v0;
    s32 v1;
    struct coord spd4;
    struct coord spc8;
    s32 i;
    f32 spc0;
    f32 spbc;
    f32 stack[1];
    s32 count;
    s32 spb0;
    f32 f0;
    s32 tmp;
    s32 tmp1;
    s32 tmp2;

    g_StarPositions = NULL;
    g_StarVertices = NULL;

    if (PLAYERCOUNT() < 2) {
        g_StarsBelowHorizon = false;
        g_StarGridSize = 3;

        if (g_Vars.stagenum == STAGE_TEST_OLD) {
            g_StarsBelowHorizon = true;
            g_StarCount = 1600;
        } else if (g_Vars.stagenum == STAGE_DEFECTION || g_Vars.stagenum == STAGE_EXTRACTION) {
            g_StarCount = 200;
            g_StarGridSize = 2;
        } else if (g_Vars.stagenum == STAGE_ATTACKSHIP) {
            g_StarsBelowHorizon = true;
            g_StarCount = 1200;
        } else {
            g_StarCount = 200;
            g_StarGridSize = 2;
        }

        tmp = g_StarGridSize + 1;
        g_StarPositions = mempAlloc(ALIGN64(g_StarCount * 3U + tmp * 72 * tmp + 6 * g_StarGridSize * g_StarGridSize * 4U + 4), MEMPOOL_STAGE);

        if (g_StarPositions != NULL) {
            g_StarPosIndexes = (s32 *)(g_StarPositions + g_StarCount * 3);

            for (i = 0; i < (6 * g_StarGridSize * g_StarGridSize + 1); i++) {
                g_StarPosIndexes[i] = 0;
            }

            count = 6 * g_StarGridSize * g_StarGridSize + 1;
            g_StarData3 = (f32 *)(count * sizeof(f32) + (uintptr_t)g_StarPosIndexes);

            stars0f135c70();

            for (i = 0; i < g_StarCount; i++) {
                spd4.f[0] = 2.0f * RANDOMFRAC() - 1.0f;
                // The subtraction allows stars to go slightly below the horizon line
                spd4.f[1] = g_StarsBelowHorizon ? 2.0f * RANDOMFRAC() - 1.0f : RANDOMFRAC() - 0.60f;
                spd4.f[2] = 2.0f * RANDOMFRAC() - 1.0f;

                guNormalize(&spd4.f[0], &spd4.f[1], &spd4.f[2]);

                f0 = (ABS2(spd4.f[0]) > ABS2(spd4.f[1])) ? (ABS2(spd4.f[0]) > ABS2(spd4.f[2]) ? ABS2(spd4.f[0]) : ABS2(spd4.f[2])) : (ABS2(spd4.f[1]) > ABS2(spd4.f[2]) ? ABS2(spd4.f[1]) : ABS2(spd4.f[2]));

                spc8.f[0] = spd4.f[0] / f0;
                spc8.f[1] = spd4.f[1] / f0;
                spc8.f[2] = spd4.f[2] / f0;

                tmp1 = g_StarGridSize * g_StarGridSize;

                if (spc8.f[0] == 1 || spc8.f[0] == -1) {
                    spb0 = spc8.f[0] == -1 ? 0 : 1;
                    spc0 = spc8.f[1];
                    spbc = spc8.f[2];
                } else if (spc8.f[1] == 1 || spc8.f[1] == -1) {
                    spb0 = spc8.f[1] == -1 ? 2 : 3;
                    spc0 = spc8.f[2];
                    spbc = spc8.f[0];
                } else if (spc8.f[2] == 1 || spc8.f[2] == -1) {
                    spb0 = spc8.f[2] == -1 ? 4 : 5;
                    spc0 = spc8.f[0];
                    spbc = spc8.f[1];
                } else {
                    // empty
                }

                v0 = (spc0 + 1) / 2 * g_StarGridSize;
                v1 = (spbc + 1) / 2 * g_StarGridSize;

                if (v0 == g_StarGridSize) {
                    v0--;
                }

                if (v1 == g_StarGridSize) {
                    v1--;
                }

                tmp2 = v0 + g_StarGridSize * v1;

                starInsert(spb0 * tmp1 + tmp2, &spd4);
            }

            // All stars are inserted and sorted: we generate the world quads
            g_StarVertices = mempAlloc(ALIGN64(g_StarCount * STARS_STRIDE * sizeof(struct starvtx)), MEMPOOL_STAGE);

            if (g_StarVertices != NULL) {
                starsBuildVertices();
            }
        }
    }
}

Gfx *starsRender(Gfx *gdl)
{
    bool isddtower = false;
    Mtxf viewRot;
    Mtxf *mtxL;
    s32 i;
    f32 sp154;
    struct coord sp148;
    s32 j;
    s32 k;
    s32 l;
    u32 colours[4];

    if (g_StarPositions == NULL || g_StarVertices == NULL) {
        return gdl;
    }

    if (g_Vars.stagenum == STAGE_DEFECTION || g_Vars.stagenum == STAGE_EXTRACTION) {
        isddtower = true;
    }

    colours[0] = colourBlend(0xffffff7f, 0x7777777f, menuGetSinOscFrac(2) * 255);
    colours[1] = colourBlend(0x0000aa7f, 0x2222ff7f, menuGetSinOscFrac(4) * 255);
    colours[2] = colourBlend(0x0000ff7f, 0x5555ff7f, menuGetCosOscFrac(2) * 255);
    colours[3] = colourBlend(0xaaaaff7f, 0x7777ff7f, menuGetCosOscFrac(4) * 255);

    if (isddtower) {
        for (i = 0; i < 3; i++) {
            // Nothing is done with the return value here, so this has no
            // effect. Maybe the original code incorrectly did a comparison
            // instead of an assign? eg. colours[i] == colourBlend(...)
            // Doing this would make the stars more transparent.
            colourBlend(colours[i], colours[i] & 0xff, 0x5f);
        }
    }

    // Coarse culling by grid cell (unchanged): we only submit
    // the cells that intersect the view frustum. The RSP does the fine clipping.
    sp154 = cosf(0.017453199252486f * (90.0f - viGetFovY() / viGetAspect() * 0.5f));

    sp148.f[0] = g_Vars.currentplayer->cam_look.f[0];
    sp148.f[1] = g_Vars.currentplayer->cam_look.f[1];
    sp148.f[2] = g_Vars.currentplayer->cam_look.f[2];

    // We load a pure identity matrix.
    mtx4LoadIdentity(&viewRot);

    // We center the star grid on the player's absolute position in the world.
    viewRot.m[3][0] = g_Vars.currentplayer->cam_pos.f[0];
    viewRot.m[3][1] = g_Vars.currentplayer->cam_pos.f[1];
    viewRot.m[3][2] = g_Vars.currentplayer->cam_pos.f[2];

    mtxL = gfxAllocateMatrix();
    mtxF2L(&viewRot, mtxL);

    gdl = textSetPrimColour(gdl, 0xffffffff);

    gDPSetRenderMode(gdl++, G_RM_CLD_SURF, G_RM_CLD_SURF2);
    gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gSPTexture(gdl++, 0, 0, 0, 0, G_OFF);
    gSPClearGeometryMode(gdl++, G_CULL_BOTH | G_LIGHTING | G_FOG | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);

    // Overwriting the stack with G_MTX_LOAD. Hardware projection will handle head tracking.
    gSPMatrix(gdl++, osVirtualToPhysical(mtxL), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    for (i = 0; i < 6; i++) {
        if (g_StarsBelowHorizon || i != 2) {
            f32 f0;
            bool spd0[4][4];

            for (j = 0; j <= g_StarGridSize; j++) {
                for (k = 0; k <= g_StarGridSize; k++) {
                    spd0[k][j] = false; // CPU culling bypass for VR
                }
            }

            for (j = 0; j < g_StarGridSize; j++) {
                for (k = 0; k < g_StarGridSize; k++) {
                    if (spd0[k][j] == 0 || spd0[k + 1][j] == 0 || spd0[k][j + 1] == 0 || spd0[k + 1][j + 1] == 0) {
                        s32 cell = g_StarGridSize * g_StarGridSize * i + k + j * g_StarGridSize;
                        s32 first = g_StarPosIndexes[cell];
                        s32 last = g_StarPosIndexes[cell + 1];
                        s32 groupsize = (last - first) / 4 + 1;
                        s32 nextgroupstart = first;
                        s32 colourindex = 0;

                        for (l = first; l < last; l++) {
                            s32 slot = (l - first) % STARS_BATCH;

                            // Loads a batch of stars (STARS_STRIDE vertices each) into the RSP cache
                            if (slot == 0) {
                                s32 n = last - l;

                                if (n > STARS_BATCH) {
                                    n = STARS_BATCH;
                                }

                                gSPVertex(gdl++, osVirtualToPhysical(&g_StarVertices[l * STARS_STRIDE]), n * STARS_STRIDE, 0);
                            }

                            if (nextgroupstart == l) {
                                gDPSetPrimColorViaWord(gdl++, 0, 0, colours[colourindex]);

                                colourindex++;
                                nextgroupstart += groupsize;
                            }

                            // Rendering the two halves of the square (quad)
                            gSP1Triangle(gdl++, slot * STARS_STRIDE + 0, slot * STARS_STRIDE + 1, slot * STARS_STRIDE + 2, 0);
                            gSP1Triangle(gdl++, slot * STARS_STRIDE + 0, slot * STARS_STRIDE + 2, slot * STARS_STRIDE + 3, 0);
                        }
                    }
                }
            }
        }
    }

    gdl = text0f153838(gdl);

    return gdl;
}
