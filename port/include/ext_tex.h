#ifndef _IN_EXT_TEX_H
#define _IN_EXT_TEX_H

#include <PR/ultratypes.h>

#define MASK_FONT_OUTLINE 0x80

s32 extTexInit();
void extTexFree();
u8 *extTexLoad(u8 type, u16 id, s32 texnum, u32 *width, u32 *height);
u8 extTexExists(u8 type, u16 id, s32 texnum);
u8 extTexFontID(struct font *font);

s32 extTexPollReady(u8 *outType, u16 *outId, s32 *outTexnum, s32 maxOut);
void extTexAsyncInit();
void extTexAsyncShutdown();
#endif
