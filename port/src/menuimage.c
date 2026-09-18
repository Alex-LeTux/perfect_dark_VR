/**
 * Pictures the menus draw that are not the game's own textures.
 * See menuimage.h for what this is; this file is the how.
 *
 * There are two halves and they run on different threads. menuImageDraw()
 * builds a display list on the game thread and never touches the picture;
 * menuImageLoadReplacement() decodes it on the render thread, once, and hands
 * out copies. Nothing else reads or writes an image, which is why there is no
 * lock here and there is one in xblatex.c: the registry itself is built by a
 * constructor, before either thread exists, and is read-only afterwards.
 */

#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include <PR/ultratypes.h>
#include "constants.h"
#include "types.h"
#include "bss.h"
#include "data.h"
#include "system.h"
#include "pngread.h"
#include "menuimage.h"

// A handful: every one is a picture compiled into the game.
#define MENUIMAGE_MAX 8

static struct menuimage *images[MENUIMAGE_MAX];
static s32 numImages;

/**
 * What a stand-in holds if anything ever reads it, which happens only when the
 * picture will not decode. A dark blue rather than white: an empty frame in
 * the menu's own colours reads as "no picture", where white reads as a bug.
 */
static void menuImageFillTile(struct menuimage *img)
{
	const u16 texel = (2 << 11) | (4 << 6) | (8 << 1) | 1;
	s32 i;

	for (i = 0; i < MENUIMAGE_TILE * MENUIMAGE_TILE; i++) {
		img->tile[i * 2] = (u8)(texel >> 8);
		img->tile[i * 2 + 1] = (u8)texel;
	}
}

void menuImageRegister(struct menuimage *img)
{
	if (img == NULL || img->registered || numImages >= MENUIMAGE_MAX) {
		return;
	}

	menuImageFillTile(img);

	img->registered = 1;
	images[numImages++] = img;
}

s32 menuImageHaveImages(void)
{
	return numImages > 0;
}

/**
 * Decode the picture, or ask for one that is not a PNG in the binary.
 *
 * Not turned over, which is worth saying because the other place a PNG reaches
 * the renderer does turn one over: a texture pack's images are flipped on load
 * (texpack.c) because a pack is drawn from a *dump*, and a dump is written the
 * right way up for editing. What the renderer uploads is the top row first -
 * the order a PNG is already in, and the order xblatex.c hands its pictures
 * over in. Flipping here drew Joanna standing on her head.
 */
static u8 *menuImageDecode(struct menuimage *img)
{
	u8 *rgba;
	s32 width = 0;
	s32 height = 0;

	if (img->tried) {
		return img->rgba;
	}

	img->tried = 1;

	rgba = img->png
		? pngReadMem(img->png, img->pnglen, img->name, &width, &height)
		: (img->load ? img->load(&width, &height) : NULL);

	if (rgba == NULL) {
		sysLogPrintf(LOG_ERROR, "menuimage: %s did not decode", img->name);
		return NULL;
	}

	img->rgba = rgba;
	img->width = width;
	img->height = height;

	sysLogPrintf(LOG_NOTE, "menuimage: %s is %dx%d", img->name, width, height);

	return rgba;
}

u8 *menuImageLoadReplacement(const void *addr, s32 *outWidth, s32 *outHeight)
{
	s32 i;

	for (i = 0; i < numImages; i++) {
		struct menuimage *img = images[i];

		if ((const void *)img->tile != addr) {
			continue;
		}

		if (menuImageDecode(img) == NULL) {
			return NULL;
		}

		{
			const size_t size = (size_t)img->width * img->height * 4;
			u8 *copy = malloc(size);

			if (copy == NULL) {
				return NULL;
			}

			// A copy rather than the picture itself, because the renderer
			// frees what it is given and the picture is kept: it is asked for
			// again every time the texture cache is dropped, which the texture
			// pack menu does whenever anything is switched.
			memcpy(copy, img->rgba, size);

			*outWidth = img->width;
			*outHeight = img->height;

			return copy;
		}
	}

	return NULL;
}

void menuImageFreeReplacement(u8 *rgba)
{
	free(rgba);
}

/**
 * The rectangle, in menu units.
 *
 * The tile is bound rather than the picture - see the header - so the texture
 * coordinates run over the tile and the whole of it is one whole picture. The
 * horizontal ones carry g_ScaleX the way every other texture rectangle in the
 * menus does: menu units are the 320-wide space the menu is laid out in, and
 * the framebuffer is not.
 */
Gfx *menuImageDraw(Gfx *gdl, struct menuimage *img, s32 x1, s32 y1, s32 x2, s32 y2, u32 alpha)
{
	const s32 width = x2 - x1;
	const s32 height = y2 - y1;
	s32 dsdx;
	s32 dtdy;

	if (img == NULL || !img->registered || width <= 0 || height <= 0) {
		return gdl;
	}

	// 0x400 is one texel per pixel, so this is "the tile across the rectangle"
	// in the 5.10 the command wants.
	dsdx = (s32)(MENUIMAGE_TILE * 1024.0f / ((f32)width * g_ScaleX));
	dtdy = MENUIMAGE_TILE * 1024 / height;

	gDPPipeSync(gdl++);
	gDPSetCycleType(gdl++, G_CYC_1CYCLE);
	gDPSetTexturePersp(gdl++, G_TP_NONE);
	gDPSetTextureLOD(gdl++, G_TL_TILE);
	gDPSetTextureConvert(gdl++, G_TC_FILT);
	gDPSetTextureFilter(gdl++, G_TF_BILERP);
	gDPSetAlphaCompare(gdl++, G_AC_NONE);
	gDPSetRenderMode(gdl++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);

	// Colour from the picture; alpha from both. What the caller says with
	// alpha is how far a window has faded in, and a cover is opaque so for
	// years that was the whole of it - but the XBLA release's logo is a shape
	// on a transparent field, and taking the caller's alpha alone painted the
	// field as an opaque blue slab around it.
	gDPSetEnvColorViaWord(gdl++, 0xffffff00 | (alpha & 0xff));
	gDPSetCombineLERP(gdl++,
			0, 0, 0, TEXEL0,
			TEXEL0, 0, ENVIRONMENT, 0,
			0, 0, 0, TEXEL0,
			TEXEL0, 0, ENVIRONMENT, 0);

	gSPTexture(gdl++, 0xffff, 0xffff, 0, G_TX_RENDERTILE, G_ON);

	// Clamped rather than wrapped: the rectangle covers the tile exactly, and
	// what a filter reaches for past the last texel should be the last texel
	// rather than the other edge of the picture.
	gDPLoadTextureBlock(gdl++, img->tile, G_IM_FMT_RGBA, G_IM_SIZ_16b,
			MENUIMAGE_TILE, MENUIMAGE_TILE, 0,
			G_TX_CLAMP, G_TX_CLAMP,
			MENUIMAGE_TILE_MASK, MENUIMAGE_TILE_MASK, G_TX_NOLOD, G_TX_NOLOD);

	gSPTextureRectangle(gdl++,
			(s32)((x1 << 2) * g_ScaleX), y1 << 2,
			(s32)((x2 << 2) * g_ScaleX), y2 << 2,
			G_TX_RENDERTILE,
			0, 0, dsdx, dtdy);

	gDPPipeSync(gdl++);

	return gdl;
}
