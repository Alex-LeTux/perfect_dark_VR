#include <ultra64.h>
#include "constants.h"
#include "game/game_0b2150.h"
#include "bss.h"
#include "lib/vi.h"
#include "data.h"
#include "types.h"
#include "gbiex.h"

#ifndef PLATFORM_N64
#include "../../port/vr/vr_openxr.h"
#endif

/**
 * With this function stubbed, light glares do not render,
 * nor do suns or their lens flares.
 */
void func0f0b2150(Gfx **gdlptr, f32 *arg1, f32 *arg2, s32 width, s32 height, s32 arg5, s32 arg6, s32 arg7, s32 arg8, s32 tile, s32 arg10)
{
	if (arg2[0] > 0.0f && arg2[1] > 0.0f) {
		Gfx *gdl = *gdlptr;
		s32 xl;
		s32 yl;
		s32 xh;
		s32 yh;
		s32 s = 0;
		s32 t = 0;
		s32 dsdx;
		s32 dtdy;
		s32 widthx4;
		s32 heightx4;
		s32 sp20 = 0;
		s32 sp1c = 0;

		/**
		 * The sprite is clipped to the viewport below, because a texture
		 * rectangle's coordinates are packed into unsigned 12 bit fields and
		 * cannot address anything outside it.
		 *
		 * In VR the viewport only covers the middle of the eye's field of
		 * view, so that clip leaves a hard rectangular edge sitting in the
		 * player's periphery. It shows up most on a light glare, which is one
		 * big soft sprite that is meant to fade out rather than stop dead.
		 *
		 * The wide texrect opcode takes signed 24 bit coordinates, so under VR
		 * the clip box grows by a viewport in every direction instead. That is
		 * past the edge of the eye, so the sprite runs out on its own texture.
		 */
#ifndef PLATFORM_N64
		const bool wide = vr_init_done;
#else
		const bool wide = false;
#endif
		const s32 marginx = wide ? viGetWidth() * 4 : 0;
		const s32 marginy = wide ? viGetHeight() * 4 : 0;

		gDPSetTexturePersp(gdl++, G_TP_NONE);

		xl = (arg1[0] - arg2[0]) * 4.0f;
		yl = (arg1[1] - arg2[1]) * 4.0f;
		xh = (arg1[0] + arg2[0]) * 4.0f;
		yh = (arg1[1] + arg2[1]) * 4.0f;

		if (arg10) {
			sp20 = xl & 3;
			sp1c = yl & 3;
			xl -= sp20;
			yl -= sp1c;
		}

		if (xh >= -marginx && yh >= -marginy) {
			if (arg8) {
				width *= 2;
				height *= 2;
				s = -(width * 16);
				t = -(height * 16);
			}

			if (xl < -marginx) {
				if (arg5) {
					t += (((-marginx - xl) * height) << 5) / (xh - xl);
				} else {
					s += (((-marginx - xl) * width) << 5) / (xh - xl);
				}

				xl = -marginx;
			}

			if (yl < -marginy) {
				if (arg5) {
					s += (((-marginy - yl) * width) << 5) / (yh - yl);
				} else {
					t += (((-marginy - yl) * height) << 5) / (yh - yl);
				}

				yl = -marginy;
			}

			widthx4 = viGetWidth() * 4 + marginx;
			heightx4 = viGetHeight() * 4 + marginy;

			if (widthx4 < xh) {
				xh = widthx4;
			}

			if (heightx4 < yh) {
				yh = heightx4;
			}

			if (arg5) {
				dsdx = width / (2.0f * arg2[1]) * 1024.0f;
				dtdy = height / (2.0f * arg2[0]) * 1024.0f;
			} else {
				dsdx = width / (2.0f * arg2[0]) * 1024.0f;
				dtdy = height / (2.0f * arg2[1]) * 1024.0f;
			}

			if (arg10) {
				s -= (sp20 * dsdx) >> 7;
				t -= (sp1c * dtdy) >> 7;
			}

			if (arg6) {
				dsdx = 0x10000 - dsdx;

				if (arg8) {
					s = (((width >> 1) - 1) << 5) - s;
				} else {
					s = ((width - 1) << 5) - s;
				}
			}

			if (arg7) {
				dtdy = 0x10000 - dtdy;

				if (arg8) {
					t = (((height >> 1) - 1) << 5) - t;
				} else {
					t = ((height - 1) << 5) - t;
				}
			}

			if (wide) {
#ifndef PLATFORM_N64
				gSPTextureRectangleWideEXT(gdl++, xl, yl, xh, yh, tile, s, t, dsdx, dtdy, arg5 ? G_ON : G_OFF);
#endif
			} else if (arg5) {
				gSPTextureRectangleFlip(gdl++, xl, yl, xh, yh, tile, s, t, dsdx, dtdy);
			} else {
				gSPTextureRectangle(gdl++, xl, yl, xh, yh, tile, s, t, dsdx, dtdy);
			}
		}

		gDPSetTexturePersp(gdl++, G_TP_PERSP);

		*gdlptr = gdl;
	}
}

void func0f0b26f0(Gfx **gdl, f32 *arg1, f32 *arg2, s32 width, s32 height, bool arg5, bool arg6, bool arg7, bool arg8, s32 tile)
{
	func0f0b2150(gdl, arg1, arg2, width, height, arg5, arg6, arg7, arg8, tile, false);
}

void func0f0b2740(Gfx **gdl, f32 *arg1, f32 *arg2, s32 width, s32 height, bool arg5, bool arg6, bool arg7, u32 arg8)
{
	func0f0b2150(gdl, arg1, arg2, width, height, arg5, arg6, arg7, arg8, G_TX_RENDERTILE, false);
}

void func0f0b278c(Gfx **gdlptr, f32 *arg1, f32 *arg2, s32 twidth, u32 theight, u32 arg5, u32 arg6, u32 arg7, u32 r, u32 g, u32 b, u32 alpha, u32 arg12, u32 arg13)
{
	if (arg2[0] > 0.0f && arg2[1] > 0.0f) {
		Gfx *gdl = *gdlptr;

		gDPSetEnvColor(gdl++, r, g, b, alpha);

		if (arg12) {
			gDPSetCombineMode(gdl++, G_CC_TRILERP, G_CC_CUSTOM_03);
		} else if (arg13) {
			gDPSetCombineMode(gdl++, G_CC_CUSTOM_00, G_CC_PASS2);
		} else {
			gDPSetCombineMode(gdl++, G_CC_CUSTOM_00, G_CC_CUSTOM_00);
		}

		*gdlptr = gdl;

		func0f0b2150(gdlptr, arg1, arg2, twidth, theight, arg5, arg6, arg7, false, G_TX_RENDERTILE, false);
	}
}
