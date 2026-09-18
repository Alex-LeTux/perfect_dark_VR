/**
 * PNG decoder.
 *
 * The other half of pngwrite.c, and the same reasoning: PNG is a zlib stream in
 * chunks and zlib is already linked, so this is a few hundred lines rather than
 * another vendored library.
 *
 * Everything comes out as RGBA32 with the top row first, because that is what
 * the renderer uploads and it saves every caller a conversion.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>
#include <PR/ultratypes.h>
#include "system.h"
#include "pngread.h"

#define PNG_COLOUR_GREY       0
#define PNG_COLOUR_RGB        2
#define PNG_COLOUR_PALETTE    3
#define PNG_COLOUR_GREY_ALPHA 4
#define PNG_COLOUR_RGBA       6

// Guards against a corrupt or hostile IHDR asking for an allocation the size of
// the address space. 16k a side is four times the largest texture any pack has
// reason to ship.
#define PNG_MAX_DIMENSION 16384

struct pngstate {
	const u8 *file;
	u32 fileSize;
	s32 width;
	s32 height;
	s32 colourType;
	s32 bitDepth;
	s32 channels;
	s32 interlaced; // Adam7: seven subsampled passes rather than one image
	u8 palette[256][4];
	s32 numPaletteEntries;
};

/**
 * Adam7. An interlaced file is seven images back to back, each a subsample of
 * the whole - pass n holds the pixels at (x0 + i * dx, y0 + j * dy) - and each
 * with its own filter bytes, so it unfilters as a small image of its own and
 * its pixels are then scattered into place. A pass can be empty for a small
 * image, and contributes nothing to the stream then.
 */
static const s32 pngAdamX0[7] = { 0, 4, 0, 2, 0, 1, 0 };
static const s32 pngAdamY0[7] = { 0, 0, 4, 0, 2, 0, 1 };
static const s32 pngAdamDX[7] = { 8, 8, 4, 4, 2, 2, 1 };
static const s32 pngAdamDY[7] = { 8, 8, 8, 4, 4, 2, 2 };

static void pngPassSize(const struct pngstate *st, s32 pass, s32 *outWidth, s32 *outHeight)
{
	if (!st->interlaced) {
		*outWidth = st->width;
		*outHeight = st->height;
		return;
	}

	*outWidth = (st->width - pngAdamX0[pass] + pngAdamDX[pass] - 1) / pngAdamDX[pass];
	*outHeight = (st->height - pngAdamY0[pass] + pngAdamDY[pass] - 1) / pngAdamDY[pass];
}

static u32 pngRowBytes(const struct pngstate *st, s32 width)
{
	return ((u32)width * (u32)st->channels * (u32)st->bitDepth + 7) / 8;
}

static u32 pngReadU32(const u8 *p)
{
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

/**
 * Undoes the per-row filter PNG applies before deflating. Each row is prefixed
 * with its filter type and predicts from the pixel to the left (a), the one
 * above (b) and the one above-left (c); bpp is the distance back to a.
 */
static s32 pngUnfilter(u8 *rows, s32 height, u32 rowSize, u32 bpp)
{
	const u32 stride = rowSize + 1;
	u8 *prev = NULL;
	s32 y;

	for (y = 0; y < height; y++) {
		u8 *row = rows + stride * (u32)y;
		const u8 filter = *row++;
		u32 x;

		switch (filter) {
		case 0: // none
			break;
		case 1: // sub
			for (x = bpp; x < rowSize; x++) {
				row[x] += row[x - bpp];
			}
			break;
		case 2: // up
			if (prev) {
				for (x = 0; x < rowSize; x++) {
					row[x] += prev[x];
				}
			}
			break;
		case 3: // average
			for (x = 0; x < rowSize; x++) {
				const u32 a = x >= bpp ? row[x - bpp] : 0;
				const u32 b = prev ? prev[x] : 0;
				row[x] += (u8)((a + b) >> 1);
			}
			break;
		case 4: // paeth
			for (x = 0; x < rowSize; x++) {
				const s32 a = x >= bpp ? row[x - bpp] : 0;
				const s32 b = prev ? prev[x] : 0;
				const s32 c = (prev && x >= bpp) ? prev[x - bpp] : 0;
				const s32 p = a + b - c;
				const s32 pa = abs(p - a);
				const s32 pb = abs(p - b);
				const s32 pc = abs(p - c);
				s32 pred;

				if (pa <= pb && pa <= pc) {
					pred = a;
				} else if (pb <= pc) {
					pred = b;
				} else {
					pred = c;
				}

				row[x] += (u8)pred;
			}
			break;
		default:
			return 0;
		}

		prev = row;
	}

	return 1;
}

/**
 * Walks the chunk list, checking the header and collecting the palette and the
 * IDAT payload. IDAT may be split across any number of chunks, so they are
 * concatenated into one deflate stream.
 */
static u8 *pngGatherChunks(struct pngstate *st, const char *path, u32 *outSize)
{
	u32 pos = 8;
	u8 *idat = NULL;
	u32 idatSize = 0;
	s32 seenHeader = 0;

	while (pos + 12 <= st->fileSize) {
		const u32 len = pngReadU32(st->file + pos);
		const u8 *type = st->file + pos + 4;
		const u8 *data = st->file + pos + 8;

		// len is attacker controlled: check it against what is left before
		// trusting it, and in a way that cannot itself overflow.
		if (len > st->fileSize - pos - 12) {
			sysLogPrintf(LOG_ERROR, "png: %s has a chunk running past the end of the file", path);
			free(idat);
			return NULL;
		}

		if (!memcmp(type, "IHDR", 4)) {
			if (len < 13) {
				sysLogPrintf(LOG_ERROR, "png: %s has a short IHDR", path);
				goto fail;
			}

			st->width = (s32)pngReadU32(data);
			st->height = (s32)pngReadU32(data + 4);
			st->colourType = data[9];

			if (st->width <= 0 || st->height <= 0
					|| st->width > PNG_MAX_DIMENSION || st->height > PNG_MAX_DIMENSION) {
				sysLogPrintf(LOG_ERROR, "png: %s is %dx%d, which is not a usable size",
						path, st->width, st->height);
				goto fail;
			}

			st->bitDepth = data[8];

			// Sub-byte depths exist only for greyscale and palette images, and
			// image editors do emit them - a 16 colour texture saves as depth
			// 4 without being asked. 16 bit channels are refused: nothing
			// gains from them here and half of every sample would be dropped.
			if (st->bitDepth != 8 && st->bitDepth != 4 && st->bitDepth != 2 && st->bitDepth != 1) {
				sysLogPrintf(LOG_ERROR, "png: %s is %d bits per channel, which is not supported",
						path, st->bitDepth);
				goto fail;
			}

			if (data[12] > 1) {
				sysLogPrintf(LOG_ERROR, "png: %s has interlace method %d, which does not exist",
						path, data[12]);
				goto fail;
			}

			st->interlaced = data[12];

			switch (st->colourType) {
			case PNG_COLOUR_GREY:       st->channels = 1; break;
			case PNG_COLOUR_RGB:        st->channels = 3; break;
			case PNG_COLOUR_PALETTE:    st->channels = 1; break;
			case PNG_COLOUR_GREY_ALPHA: st->channels = 2; break;
			case PNG_COLOUR_RGBA:       st->channels = 4; break;
			default:
				sysLogPrintf(LOG_ERROR, "png: %s has colour type %d, which is not supported",
						path, st->colourType);
				goto fail;
			}

			if (st->bitDepth != 8 && st->channels != 1) {
				sysLogPrintf(LOG_ERROR, "png: %s is colour type %d at %d bits, which cannot exist",
						path, st->colourType, st->bitDepth);
				goto fail;
			}

			seenHeader = 1;
		} else if (!memcmp(type, "PLTE", 4)) {
			u32 i;

			st->numPaletteEntries = (s32)(len / 3);

			if (st->numPaletteEntries > 256) {
				st->numPaletteEntries = 256;
			}

			for (i = 0; i < (u32)st->numPaletteEntries; i++) {
				st->palette[i][0] = data[i * 3];
				st->palette[i][1] = data[i * 3 + 1];
				st->palette[i][2] = data[i * 3 + 2];
				st->palette[i][3] = 255;
			}
		} else if (!memcmp(type, "tRNS", 4)) {
			// Only the palette form is honoured. The greyscale and truecolour
			// forms name a single transparent colour, which no texture pack
			// uses and which would be silently wrong to ignore in the middle
			// of an image.
			if (st->colourType == PNG_COLOUR_PALETTE) {
				u32 i;

				for (i = 0; i < len && i < 256; i++) {
					st->palette[i][3] = data[i];
				}
			}
		} else if (!memcmp(type, "IDAT", 4)) {
			u8 *grown = realloc(idat, idatSize + len);

			if (!grown) {
				sysLogPrintf(LOG_ERROR, "png: could not alloc %u bytes for %s", idatSize + len, path);
				free(idat);
				return NULL;
			}

			idat = grown;
			memcpy(idat + idatSize, data, len);
			idatSize += len;
		} else if (!memcmp(type, "IEND", 4)) {
			break;
		}

		pos += 12 + len;
	}

	if (!seenHeader || !idat) {
		sysLogPrintf(LOG_ERROR, "png: %s has no %s", path, seenHeader ? "image data" : "header");
		goto fail;
	}

	*outSize = idatSize;

	return idat;

fail:
	// IDAT may already have been gathered: the chunk order is the file's to
	// choose, so a header this refuses can arrive after megabytes of it.
	free(idat);

	return NULL;
}

/**
 * Spreads a row of sub-byte samples out to one byte each, in place of the
 * caller's scratch buffer. Palette indices are left as they are; greyscale is
 * scaled so that the deepest value reaches 255 rather than a fraction of it.
 */
static void pngUnpackRow(const struct pngstate *st, const u8 *src, u8 *dst, s32 width)
{
	const s32 perByte = 8 / st->bitDepth;
	const u32 mask = (1u << st->bitDepth) - 1;
	const u32 scale = st->colourType == PNG_COLOUR_PALETTE ? 1 : 255 / mask;
	s32 x;

	for (x = 0; x < width; x++) {
		const s32 shift = 8 - st->bitDepth * (x % perByte + 1);
		dst[x] = (u8)(((src[x / perByte] >> shift) & mask) * scale);
	}
}

/**
 * Expands one unfiltered row into RGBA. Kept separate from the unfilter pass so
 * the colour type is switched on once per row rather than once per pixel.
 */
static void pngRowToRgba(const struct pngstate *st, const u8 *src, u8 *dst, s32 width)
{
	s32 x;

	switch (st->colourType) {
	case PNG_COLOUR_GREY:
		for (x = 0; x < width; x++, dst += 4) {
			dst[0] = dst[1] = dst[2] = src[x];
			dst[3] = 255;
		}
		break;
	case PNG_COLOUR_GREY_ALPHA:
		for (x = 0; x < width; x++, dst += 4) {
			dst[0] = dst[1] = dst[2] = src[x * 2];
			dst[3] = src[x * 2 + 1];
		}
		break;
	case PNG_COLOUR_RGB:
		for (x = 0; x < width; x++, dst += 4) {
			dst[0] = src[x * 3];
			dst[1] = src[x * 3 + 1];
			dst[2] = src[x * 3 + 2];
			dst[3] = 255;
		}
		break;
	case PNG_COLOUR_RGBA:
		memcpy(dst, src, (size_t)width * 4);
		break;
	case PNG_COLOUR_PALETTE:
		for (x = 0; x < width; x++, dst += 4) {
			// An index past the end of PLTE is a broken file; black is a
			// better answer than reading whatever follows the palette.
			const u8 idx = src[x] < st->numPaletteEntries ? src[x] : 0;
			dst[0] = st->palette[idx][0];
			dst[1] = st->palette[idx][1];
			dst[2] = st->palette[idx][2];
			dst[3] = st->palette[idx][3];
		}
		break;
	}
}

/**
 * The decode, over a whole PNG already in memory.
 *
 * Split out from pngRead() for the pictures the game carries inside itself - a
 * community pack's cover art is bytes in the binary rather than a file on disk
 * - so that a file and a blob go through the same decoder rather than a second
 * one written for the easy case. path is only ever used to say which image a
 * complaint is about.
 */
static u8 *pngDecode(const u8 *file, u32 fileSize, const char *path, s32 *outWidth, s32 *outHeight)
{
	static const u8 signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
	struct pngstate st;
	u8 *idat = NULL;
	u32 idatSize = 0;
	u8 *rows = NULL;
	u8 *rgba = NULL;
	u8 *unpacked = NULL;
	u8 *line = NULL;
	uLongf rowsSize;
	u32 bpp;
	s32 numPasses;
	s32 pass;
	s32 y;

	memset(&st, 0, sizeof(st));

	if (fileSize < 8 || memcmp(file, signature, sizeof(signature))) {
		sysLogPrintf(LOG_ERROR, "png: %s is not a PNG", path);
		return NULL;
	}

	st.file = file;
	st.fileSize = fileSize;

	idat = pngGatherChunks(&st, path, &idatSize);

	if (!idat) {
		return NULL;
	}

	// Every row is its filter byte plus one byte per channel per pixel, and an
	// interlaced file is its seven passes back to back, so the inflated size
	// is known exactly either way and zlib can be given a fixed buffer.
	numPasses = st.interlaced ? 7 : 1;
	bpp = ((u32)st.channels * (u32)st.bitDepth + 7) / 8;
	rowsSize = 0;

	for (pass = 0; pass < numPasses; pass++) {
		s32 passWidth;
		s32 passHeight;

		pngPassSize(&st, pass, &passWidth, &passHeight);

		if (passWidth > 0 && passHeight > 0) {
			rowsSize += (uLongf)(pngRowBytes(&st, passWidth) + 1) * (u32)passHeight;
		}
	}

	rows = malloc(rowsSize);
	rgba = malloc((size_t)st.width * st.height * 4);

	if (!rows || !rgba) {
		sysLogPrintf(LOG_ERROR, "png: could not alloc %dx%d for %s", st.width, st.height, path);
		goto fail;
	}

	{
		const uLongf expected = rowsSize;

		if (uncompress(rows, &rowsSize, idat, idatSize) != Z_OK || rowsSize != expected) {
			sysLogPrintf(LOG_ERROR, "png: could not inflate %s", path);
			goto fail;
		}
	}

	if (st.bitDepth != 8) {
		unpacked = malloc((size_t)st.width);

		if (!unpacked) {
			sysLogPrintf(LOG_ERROR, "png: could not alloc a row for %s", path);
			goto fail;
		}
	}

	if (st.interlaced) {
		line = malloc((size_t)st.width * 4);

		if (!line) {
			sysLogPrintf(LOG_ERROR, "png: could not alloc a row for %s", path);
			goto fail;
		}
	}

	{
		u8 *cursor = rows;

		for (pass = 0; pass < numPasses; pass++) {
			s32 passWidth;
			s32 passHeight;
			u32 rowSize;

			pngPassSize(&st, pass, &passWidth, &passHeight);

			if (passWidth <= 0 || passHeight <= 0) {
				continue;
			}

			rowSize = pngRowBytes(&st, passWidth);

			if (!pngUnfilter(cursor, passHeight, rowSize, bpp)) {
				sysLogPrintf(LOG_ERROR, "png: %s uses a filter type that does not exist", path);
				goto fail;
			}

			for (y = 0; y < passHeight; y++) {
				const u8 *row = cursor + (rowSize + 1) * (u32)y + 1;

				if (unpacked) {
					pngUnpackRow(&st, row, unpacked, passWidth);
					row = unpacked;
				}

				if (!st.interlaced) {
					pngRowToRgba(&st, row, rgba + (size_t)st.width * 4 * y, passWidth);
				} else {
					// The pass's row lands on every dx-th pixel of its row of
					// the image.
					const s32 imageY = pngAdamY0[pass] + y * pngAdamDY[pass];
					u8 *dst = rgba + ((size_t)imageY * st.width + pngAdamX0[pass]) * 4;
					s32 x;

					pngRowToRgba(&st, row, line, passWidth);

					for (x = 0; x < passWidth; x++) {
						memcpy(dst + (size_t)x * pngAdamDX[pass] * 4, line + (size_t)x * 4, 4);
					}
				}
			}

			cursor += (rowSize + 1) * (u32)passHeight;
		}
	}

	free(line);
	free(unpacked);

	free(rows);
	free(idat);

	*outWidth = st.width;
	*outHeight = st.height;

	return rgba;

fail:
	free(line);
	free(unpacked);
	free(rows);
	free(rgba);
	free(idat);

	return NULL;
}

u8 *pngRead(const char *path, s32 *outWidth, s32 *outHeight)
{
	FILE *f;
	long fileSize;
	u8 *file;
	u8 *rgba;

	f = fopen(path, "rb");

	if (!f) {
		sysLogPrintf(LOG_ERROR, "png: could not open %s", path);
		return NULL;
	}

	if (fseek(f, 0, SEEK_END) != 0 || (fileSize = ftell(f)) < 8 || fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		sysLogPrintf(LOG_ERROR, "png: could not size %s", path);
		return NULL;
	}

	file = malloc((size_t)fileSize);

	if (!file || fread(file, 1, (size_t)fileSize, f) != (size_t)fileSize) {
		fclose(f);
		free(file);
		sysLogPrintf(LOG_ERROR, "png: could not read %s", path);
		return NULL;
	}

	fclose(f);

	rgba = pngDecode(file, (u32)fileSize, path, outWidth, outHeight);

	free(file);

	return rgba;
}

u8 *pngReadMem(const void *data, u32 size, const char *name, s32 *outWidth, s32 *outHeight)
{
	if (data == NULL || size == 0) {
		return NULL;
	}

	return pngDecode(data, size, name ? name : "image", outWidth, outHeight);
}
