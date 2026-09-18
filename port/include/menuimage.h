#ifndef _IN_MENUIMAGE_H
#define _IN_MENUIMAGE_H

#include <ultra64.h>
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A picture the menus draw that is not one of the game's own textures.
 *
 * The game has no way to draw an arbitrary image: everything the menus put on
 * screen is a texture number, and a texture number is a record in the ROM. A
 * community pack's cover art is neither - it is a PNG built into the binary -
 * so it reaches the screen the way the XBLA meshes' textures do (see
 * xblatex.h): a display list binds a small stand-in tile, and the renderer
 * swaps the real picture in against that tile's address on its way to the GPU.
 *
 * That means the tile's own texels are never seen, but the tile it declares is
 * real and is what the texture coordinates are measured against - a rectangle
 * covering the whole tile shows the whole picture, whatever size the picture
 * is.
 */

// Big enough that a rectangle's 10.5 texture coordinates land where they are
// asked to, small enough to be nothing: 512 bytes of texels nothing reads.
#define MENUIMAGE_TILE      16
#define MENUIMAGE_TILE_MASK 4 // log2 of the above, for gDPLoadTextureBlock
#define MENUIMAGE_TILE_BYTES (MENUIMAGE_TILE * MENUIMAGE_TILE * 2)

struct menuimage {
	// The picture, as a PNG in the binary. Decoded once, on the render thread,
	// the first time something asks to draw it.
	const u8 *png;
	u32 pnglen;

	// Or a picture that is not in the binary at all: the XBLA release's logo
	// comes out of the player's own package (xblaui.c). Called instead of
	// decoding a PNG, on the render thread, once; it hands over RGBA32 in the
	// renderer's row order (top row first) and the image owns it afterwards.
	u8 *(*load)(s32 *outWidth, s32 *outHeight);

	const char *name; // for the log, and for nothing else

	u8 *rgba;
	s32 width;
	s32 height;
	s32 tried; // so a picture that will not decode is not decoded every frame

	// The stand-in. Its address is the picture's name as far as the renderer
	// is concerned, so it has to be one buffer per picture and it has to
	// outlive every display list that binds it - which is why an image is a
	// static rather than something allocated.
	u8 tile[MENUIMAGE_TILE_BYTES];
	s32 registered;
};

/**
 * Make an image known to the renderer. Call before any thread exists - a
 * constructor is the place - so that the registry is only ever read afterwards.
 */
void menuImageRegister(struct menuimage *img);

/**
 * Draw one, filling the rectangle given in menu units. The aspect is the
 * caller's business: what is asked for is what is drawn.
 */
Gfx *menuImageDraw(Gfx *gdl, struct menuimage *img, s32 x1, s32 y1, s32 x2, s32 y2, u32 alpha);

/**
 * The renderer's side, called from import_texture() for every texture upload.
 * NULL for an address that is not a stand-in, which is all but a handful.
 */
s32 menuImageHaveImages(void);
u8 *menuImageLoadReplacement(const void *addr, s32 *outWidth, s32 *outHeight);
void menuImageFreeReplacement(u8 *rgba);

#ifdef __cplusplus
}
#endif

#endif
