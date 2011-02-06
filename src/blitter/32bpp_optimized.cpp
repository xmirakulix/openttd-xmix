/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file 32bpp_optimized.cpp Implementation of the optimized 32 bpp blitter. */

#include "../stdafx.h"
#include "../core/alloc_func.hpp"
#include "../zoom_func.h"
#include "../core/math_func.hpp"
#include "32bpp_optimized.hpp"

static const int MAX_PALETTE_TABLES = 50;

struct RecolourTable {
	SpriteID   id;
	Colour     tables[256];
} _rgb_palettes[MAX_PALETTE_TABLES] = {{0,{{0}}}};

Colour _rgb_stringremap[3] = {{0}};

static FBlitter_32bppOptimized iFBlitter_32bppOptimized;

/**
 * Draws a sprite to a (screen) buffer. It is templated to allow faster operation.
 *
 * @tparam mode blitter mode
 * @param bp further blitting parameters
 * @param zoom zoom level at which we are drawing
 */
template <BlitterMode mode>
inline void Blitter_32bppOptimized::Draw(const Blitter::BlitterParams *bp, ZoomLevel zoom)
{
	const SpriteData *src = (const SpriteData *)bp->sprite;

	/* src_px : each line begins with uint32 n = 'number of bytes in this line',
	 *          then n times is the Colour struct for this line */
	zoom = ZOOM_LVL_BEGIN;
	const Colour *src_px = (const Colour *)(src->data + src->offset[zoom][0]);
	/* src_n  : each line begins with uint32 n = 'number of bytes in this line',
	 *          then interleaved stream of 'm' and 'n' channels. 'm' is remap,
	 *          'n' is number of bytes with the same alpha channel class */
	const uint8  *src_n  = (const uint8  *)(src->data + src->offset[zoom][1]);

	/* skip upper lines in src_px and src_n */
	for (uint i = bp->skip_top; i != 0; i--) {
		src_px = (const Colour *)((const byte *)src_px + *(const uint32 *)src_px);
		src_n += *(uint32 *)src_n;
	}

	/* skip lines in dst */
	uint32 *dst = (uint32 *)bp->dst + bp->top * bp->pitch + bp->left;

	/* store so we don't have to access it via bp everytime (compiler assumes pointer aliasing) */
	const Colour *remap = (const Colour *)bp->remap;
	for (int y = 0; y < bp->height; y++) {
		/* next dst line begins here */
		uint32 *dst_ln = dst + bp->pitch;

		/* next src line begins here */
		const Colour *src_px_ln = (const Colour *)((const byte *)src_px + *(const uint32 *)src_px);
		src_px++;

		/* next src_n line begins here */
		const uint8 *src_n_ln = src_n + *(uint32 *)src_n;
		src_n += 4;

		/* we will end this line when we reach this point */
		uint32 *dst_end = dst + bp->skip_left;

		/* number of pixels with the same aplha channel class */
		uint n;

		while (dst < dst_end) {
			n = *src_n++;

			if (src_px->a == 0) {
				dst += n;
				src_px ++;
				src_n++;
			} else {
				if (dst + n > dst_end) {
					uint d = dst_end - dst;
					src_px += d;
					src_n += d;

					dst = dst_end - bp->skip_left;
					dst_end = dst + bp->width;

					n = min<uint>(n - d, (uint)bp->width);
					goto draw;
				}
				dst += n;
				src_px += n;
				src_n += n;
			}
		}

		dst -= bp->skip_left;
		dst_end -= bp->skip_left;

		dst_end += bp->width;

		while (dst < dst_end) {
			n = min<uint>(*src_n++, (uint)(dst_end - dst));

			if (src_px->a == 0) {
				dst += n;
				src_px++;
				src_n++;
				continue;
			}

			draw:;

			switch (mode) {
				case BM_COLOUR_REMAP:
					do {
						uint m = *src_n;
						/* In case the m-channel is zero, do not remap this pixel in any way */
						if (m == 0) {
							if (src_px->a == 255) {
								*dst = src_px->data;
							} else {
								*dst = ComposeColourRGBANoCheck(src_px->r, src_px->g, src_px->b, src_px->a, *dst);
							}
						} else {
							Colour r = remap[m];
							if (r.a != 0) {
								uint src_col = ComposeColour(src_px->a, src_px->r, src_px->g, src_px->b);
								uint comp_col = ComposeColourBlend(r.data, src_col);
								*dst = ComposeColourPA(comp_col, src_px->a, *dst);
							}
						}
						dst++;
						src_px++;
						src_n++;
					} while (--n != 0);
					break;
				case BM_COLOUR_OPAQUE:
					do {
						uint m = *src_n;
						/* In case the m-channel is zero, do not remap this pixel in any way */
						if (m == 0) {
							*dst = ComposeColourRGBA(src_px->r, src_px->g, src_px->b, src_px->a, *dst);
						} else {
							Colour r = remap[m];
							if (r.a != 0) {
								*dst = ComposeColourPA(r.data, src_px->a, *dst);
							}
						}
						dst++;
						src_px++;
						src_n++;
					} while (--n != 0);
					break;
				case BM_TRANSPARENT:
					do {
						uint m = *src_n;
						if (m == 0) {
							*dst = ComposeColourRGBA(src_px->r, src_px->g, src_px->b, src_px->a / 2, *dst);
						} else {
							if (remap){
								Colour r = remap[m];
								if (r.a != 0) *dst = ComposeColourPA(r.data, src_px->a / 2, *dst);
							}
							else {
								*dst = ComposeColourRGBA(src_px->r, src_px->g, src_px->b, src_px->a / 2, *dst);
							}
						}

						dst++;
						src_px++;
						src_n++;
					/* TODO -- We make an assumption here that the remap in fact is transparency, not some colour.
					 *  This is never a problem with the code we produce, but newgrfs can make it fail... or at least:
					 *  we produce a result the newgrf maker didn't expect ;) */
					} while (--n != 0);
					break;
				case BM_SHADOW:
					/* Make the current colour a bit more black */
					src_n += n;
					if (src_px->a == 255) {
						src_px += n;
						do {
							*dst = MakeTransparent(*dst, 3, 4);
							dst++;
						} while (--n != 0);
					} else {
						do {
							*dst = MakeTransparent(*dst, (256 * 4 - src_px->a), 256 * 4);
							dst++;
							src_px++;
						} while (--n != 0);
					}

					break;
				default:
					if (src_px->a == 255) {
						/* faster than memcpy(), n is usually low */
						src_n += n;
						do {
							*dst = src_px->data;
							dst++;
							src_px++;
						} while (--n != 0);
					} else {
						src_n += n;
						do {
							*dst = ComposeColourRGBANoCheck(src_px->r, src_px->g, src_px->b, src_px->a, *dst);
							dst++;
							src_px++;
						} while (--n != 0);
					}
					break;
			}
		}
		dst = dst_ln;
		src_px = src_px_ln;
		src_n  = src_n_ln;
	}
}

/**
 * Draws a sprite to a (screen) buffer. Calls adequate templated function.
 *
 * @param bp further blitting parameters
 * @param mode blitter mode
 * @param zoom zoom level at which we are drawing
 */
void Blitter_32bppOptimized::Draw(Blitter::BlitterParams *bp, BlitterMode mode, ZoomLevel zoom)
{
		switch (mode) {
		default: NOT_REACHED();
		case BM_NORMAL:        Draw<BM_NORMAL>       (bp, zoom); return;
		case BM_COLOUR_REMAP:  Draw<BM_COLOUR_REMAP> (bp, zoom); return;
		case BM_COLOUR_OPAQUE: Draw<BM_COLOUR_OPAQUE>(bp, zoom); return;
		case BM_TRANSPARENT:   Draw<BM_TRANSPARENT>  (bp, zoom); return;
		case BM_SHADOW:        Draw<BM_SHADOW>       (bp, zoom); return;
	}
}

/**
 * Resizes the sprite in a very simple way, takes every n-th pixel and every n-th row
 * not used in extra zoom patch, because all zoomlevels are in the spritecache
 * @param sprite_src sprite to resize
 * @param zoom resizing scale
 * @return resized sprite
 */
static const SpriteLoader::Sprite *ResizeSprite(const SpriteLoader::Sprite *sprite_src, ZoomLevel zoom)
{
	return sprite_src;
}

Sprite *Blitter_32bppOptimized::Encode(SpriteLoader::Sprite *sprite, AllocatorProc *allocator)
{
	/* streams of pixels (a, r, g, b channels)
	 *
	 * stored in separated stream so data are always aligned on 4B boundary */
	Colour *dst_px_orig[ZOOM_LVL_COUNT];

	/* interleaved stream of 'm' channel and 'n' channel
	 * 'n' is number if following pixels with the same alpha channel class
	 * there are 3 classes: 0, 255, others
	 *
	 * it has to be stored in one stream so fewer registers are used -
	 * x86 has problems with register allocation even with this solution */
	uint8  *dst_n_orig[ZOOM_LVL_COUNT];

	/* lengths of streams */
	uint32 lengths[ZOOM_LVL_COUNT][2];

	for (ZoomLevel z = ZOOM_LVL_BEGIN; z <= ZOOM_LVL_BEGIN; z++) {
		const SpriteLoader::Sprite *src_orig = ResizeSprite(sprite, z);

		uint size = src_orig->height * src_orig->width;

		dst_px_orig[z] = CallocT<Colour>(size + src_orig->height * 2);
		dst_n_orig[z]  = CallocT<uint8>(size * 2 + src_orig->height * 4 * 2);

		uint32 *dst_px_ln = (uint32 *)dst_px_orig[z];
		uint32 *dst_n_ln  = (uint32 *)dst_n_orig[z];

		const SpriteLoader::CommonPixel *src = (const SpriteLoader::CommonPixel *)src_orig->data;

		for (uint y = src_orig->height; y > 0; y--) {
			Colour *dst_px = (Colour *)(dst_px_ln + 1);
			uint8 *dst_n = (uint8 *)(dst_n_ln + 1);

			uint8 *dst_len = dst_n++;

			uint last = 3;
			int len = 0;

			for (uint x = src_orig->width; x > 0; x--) {
				uint8 a = src->a;
				uint t = a > 0 && a < 255 ? 1 : a;

				if (last != t || len == 255) {
					if (last != 3) {
						*dst_len = len;
						dst_len = dst_n++;
					}
					len = 0;
				}

				last = t;
				len++;

				if (a != 0) {
					dst_px->a = a;
					*dst_n = src->m;

					dst_px->r = src->r;
					dst_px->g = src->g;
					dst_px->b = src->b;

					dst_px++;
					dst_n++;
				} else if (len == 1) {
					dst_px++;
					*dst_n = src->m;
					dst_n++;
				}

				src++;
			}

			if (last != 3) {
				*dst_len = len;
			}

			dst_px = (Colour *)AlignPtr(dst_px, 4);
			dst_n  = (uint8 *)AlignPtr(dst_n, 4);

			*dst_px_ln = (uint8 *)dst_px - (uint8 *)dst_px_ln;
			*dst_n_ln  = (uint8 *)dst_n  - (uint8 *)dst_n_ln;

			dst_px_ln = (uint32 *)dst_px;
			dst_n_ln =  (uint32 *)dst_n;
		}

		lengths[z][0] = (byte *)dst_px_ln - (byte *)dst_px_orig[z]; // all are aligned to 4B boundary
		lengths[z][1] = (byte *)dst_n_ln  - (byte *)dst_n_orig[z];

	}

	uint len = 0; // total length of data
	for (ZoomLevel z = ZOOM_LVL_BEGIN; z <= ZOOM_LVL_BEGIN; z++) {
		len += lengths[z][0] + lengths[z][1];
	}

	Sprite *dest_sprite = (Sprite *)allocator(sizeof(*dest_sprite) + sizeof(SpriteData) + len);

	dest_sprite->height = sprite->height;
	dest_sprite->width  = sprite->width;
	dest_sprite->x_offs = sprite->x_offs;
	dest_sprite->y_offs = sprite->y_offs;

	SpriteData *dst = (SpriteData *)dest_sprite->data;

	ZoomLevel z = ZOOM_LVL_BEGIN;
	dst->offset[z][0] = 0;
	dst->offset[z][1] = lengths[z][0] + dst->offset[z][0];

	memcpy(dst->data + dst->offset[z][0], dst_px_orig[z], lengths[z][0]);
	memcpy(dst->data + dst->offset[z][1], dst_n_orig[z],  lengths[z][1]);

	free(dst_px_orig[z]);
	free(dst_n_orig[z]);

	return dest_sprite;
}

void Blitter_32bppOptimized::RescaleSpriteHalfSize(const SpriteLoader::Sprite *src_sprite,
																			SpriteLoader::Sprite *dest_sprite,
																			bool                 prevent_alpha_bleeding)
{
	SpriteLoader::CommonPixel *dst;
	SpriteLoader::CommonPixel *src;
	int width, height;
	int x, y;
	bool  extra_px_x = false;
	bool  extra_px_y = false;

	SpriteLoader::CommonPixel clr;

	width = (src_sprite->width + 1) / 2;
	height = (src_sprite->height + 1) / 2;

	/* src sprite width is odd, just copy last pixel iso taking average */
	if (src_sprite->width & 1) {
		extra_px_x = true;
	}
	/* src sprite height is odd, just copy last pixel iso taking average */
	if (src_sprite->height & 1) {
		extra_px_y = true;
	}


	dest_sprite->data = CallocT<SpriteLoader::CommonPixel>(height * width);
	dst = (SpriteLoader::CommonPixel *)dest_sprite->data;
	src = (SpriteLoader::CommonPixel *)src_sprite->data;

	dest_sprite->width = width ;
	dest_sprite->height = height;

	dest_sprite->x_offs = src_sprite->x_offs / 2;
	dest_sprite->y_offs = src_sprite->y_offs / 2;

	for (y = 0; y < height + (extra_px_y ? -1 : 0); y++) {
		for (x = 0; x < width + (extra_px_x ? -1 : 0); x++) {
				uint ma = 0;
				uint a = 0;
				uint r = 0;
				uint g = 0;
				uint b = 0;
				uint i;
				uint j;

				for (i = 0; i < 2; i++) {
					for (j = 0; j < 2; j++) {
						uint ca;
						uint cr;
						uint cg;
						uint cb;

						clr = src[((2 * y + i ) * src_sprite->width) + (2 * x + j )];
						ca = clr.a;
						cr = clr.r;
						cg = clr.g;
						cb = clr.b;

						a += ca;
						r += ca * cr;
						g += ca * cg;
						b += ca * cb;
						if (prevent_alpha_bleeding) {
							if (ca > ma) ma = ca;
						} else {
							ma += ca;
						}
					}
				}

				if (a == 0) {
					dst[y * width + x].r = 0;
					dst[y * width + x].g = 0;
					dst[y * width + x].b = 0;
					dst[y * width + x].a = 0;
					dst[y * width + x].m = 0;
				} else {
					r /= a;
					g /= a;
					b /= a;
					if (prevent_alpha_bleeding) {
						a = ma;
					} else {
						a /= 4;
					}
					dst[y * width + x].r = r;
					dst[y * width + x].g = g;
					dst[y * width + x].b = b;
					dst[y * width + x].a = a;
					dst[y * width + x].m = clr.m;
				}
		}
		if (extra_px_x) {
			clr = src[((2 * y ) * src_sprite->width) + (2 * x)];
			dst[y * width + x] = clr;
		}
	}

}
/*
void Blitter_32bppOptimized::RescaleSpriteDoubleSizePrev(const SpriteLoader::Sprite *src_sprite,
																			  SpriteLoader::Sprite *dest_sprite)
{
	int width, height;
	SpriteLoader::CommonPixel *dst;
	SpriteLoader::CommonPixel *src;

	width = src_sprite->width * 2;
	height = src_sprite->height * 2;

	dest_sprite->data = CallocT<SpriteLoader::CommonPixel>(height * width);
	dst = (SpriteLoader::CommonPixel *)dest_sprite->data;
	src = (SpriteLoader::CommonPixel *)src_sprite->data;

	dest_sprite->width = width;
	dest_sprite->height = height;
	dest_sprite->x_offs = src_sprite->x_offs * 2;
	dest_sprite->y_offs = src_sprite->y_offs * 2;

	uint dst_y = 0;
	uint src_y_idx = 0;
	for (int y = 0; y < src_sprite->height - 1; y++) {
	   int x;

		uint dst_x = 0;
		for (x = 0; x < src_sprite->width - 1; x++) {
			dst[dst_y + dst_x] = src[src_y_idx + x];
			dst_x++;
			dst[dst_y + dst_x].r = (src[src_y_idx + x + 1].r + src[src_y_idx + x].r) / 2;
			dst[dst_y + dst_x].g = (src[src_y_idx + x + 1].g + src[src_y_idx + x].g) / 2;
			dst[dst_y + dst_x].b = (src[src_y_idx + x + 1].b + src[src_y_idx + x].b) / 2;
			dst[dst_y + dst_x].a = (src[src_y_idx + x + 1].a + src[src_y_idx + x].a) / 2;
			dst[dst_y + dst_x].m =  src[src_y_idx + x].m;
			dst_x--;
			dst_y += width;
			dst[dst_y + dst_x].r = (src[src_y_idx + x].r + src[src_y_idx + src_sprite->width + x].r) / 2;
			dst[dst_y + dst_x].g = (src[src_y_idx + x].g + src[src_y_idx + src_sprite->width + x].g) / 2;
			dst[dst_y + dst_x].b = (src[src_y_idx + x].b + src[src_y_idx + src_sprite->width + x].b) / 2;
			dst[dst_y + dst_x].a = (src[src_y_idx + x].a + src[src_y_idx + src_sprite->width + x].a) / 2;
			dst[dst_y + dst_x].m =  src[src_y_idx + x].m;
			dst_x++;
			dst[dst_y + dst_x].r = (src[src_y_idx + x].r + src[src_y_idx + src_sprite->width + x].r +
			                        src[src_y_idx + x + 1].r + src[src_y_idx + src_sprite->width + x + 1].r ) / 4;
			dst[dst_y + dst_x].g = (src[src_y_idx + x].g + src[src_y_idx + src_sprite->width + x].g +
			                        src[src_y_idx + x + 1].g + src[src_y_idx + src_sprite->width + x + 1].g ) / 4;
			dst[dst_y + dst_x].b = (src[src_y_idx + x].b + src[src_y_idx + src_sprite->width + x].b +
			                        src[src_y_idx + x + 1].b + src[src_y_idx + src_sprite->width + x + 1].b ) / 4;
			dst[dst_y + dst_x].a = (src[src_y_idx + x].a + src[src_y_idx + src_sprite->width + x].a +
			                        src[src_y_idx + x + 1].a + src[src_y_idx + src_sprite->width + x + 1].a ) / 4;
			dst[dst_y + dst_x].m =  src[src_y_idx + x].m;
			dst_y -= width;
			dst_x++;
		}

		 last pixels in row cannot be interpolated */
/*     dst[dst_y + dst_x] = src[src_y_idx + x];
		dst_x++;
		dst[dst_y + dst_x] = src[src_y_idx + x];
		dst_x--;
		dst_y += width;
		dst[dst_y + dst_x].r = (src[src_y_idx + x].r + src[src_y_idx + src_sprite->width + x].r) / 2;
		dst[dst_y + dst_x].g = (src[src_y_idx + x].g + src[src_y_idx + src_sprite->width + x].g) / 2;
		dst[dst_y + dst_x].b = (src[src_y_idx + x].b + src[src_y_idx + src_sprite->width + x].b) / 2;
		dst[dst_y + dst_x].a = (src[src_y_idx + x].a + src[src_y_idx + src_sprite->width + x].a) / 2;
		dst[dst_y + dst_x].m =  src[src_y_idx + x].m;
		dst_x++;
		dst[dst_y + dst_x].r = (src[src_y_idx + x].r + src[src_y_idx + src_sprite->width + x].r) / 2;
		dst[dst_y + dst_x].g = (src[src_y_idx + x].g + src[src_y_idx + src_sprite->width + x].g) / 2;
		dst[dst_y + dst_x].b = (src[src_y_idx + x].b + src[src_y_idx + src_sprite->width + x].b) / 2;
		dst[dst_y + dst_x].a = (src[src_y_idx + x].a + src[src_y_idx + src_sprite->width + x].a) / 2;
		dst[dst_y + dst_x].m =  src[src_y_idx + x].m;

		dst_y += width;
		src_y_idx += src_sprite->width;
	}
	 last row can not be interpolated */
/* uint dst_x = 0;
	for (int x = 0; x < src_sprite->width - 1; x++) {
		dst[dst_y + dst_x] = src[src_y_idx + x];
		dst_x++;
		dst[dst_y + dst_x].r = (src[src_y_idx + x + 1].r + src[src_y_idx + x].r) / 2;
		dst[dst_y + dst_x].g = (src[src_y_idx + x + 1].g + src[src_y_idx + x].g) / 2;
		dst[dst_y + dst_x].b = (src[src_y_idx + x + 1].b + src[src_y_idx + x].b) / 2;
		dst[dst_y + dst_x].a = (src[src_y_idx + x + 1].a + src[src_y_idx + x].a) / 2;
		dst[dst_y + dst_x].m =  src[src_y_idx + x].m;
		dst_x--;
		dst_y += width;
		dst[dst_y + dst_x] = src[src_y_idx + x];
		dst_x++;
		dst[dst_y + dst_x].r = (src[src_y_idx + x + 1].r + src[src_y_idx + x].r) / 2;
		dst[dst_y + dst_x].g = (src[src_y_idx + x + 1].g + src[src_y_idx + x].g) / 2;
		dst[dst_y + dst_x].b = (src[src_y_idx + x + 1].b + src[src_y_idx + x].b) / 2;
		dst[dst_y + dst_x].a = (src[src_y_idx + x + 1].a + src[src_y_idx + x].a) / 2;
		dst[dst_y + dst_x].m =  src[src_y_idx + x].m;
		dst_y -= width;
		dst_x++;
	}
}

*/

void Blitter_32bppOptimized::RescaleSpriteDoubleSize(const SpriteLoader::Sprite *src_sprite,
																			  SpriteLoader::Sprite *dest_sprite)
{
	int width, height;
	SpriteLoader::CommonPixel *dst;
	SpriteLoader::CommonPixel *src;

	width = src_sprite->width * 2;
	height = src_sprite->height * 2;

	dest_sprite->data = CallocT<SpriteLoader::CommonPixel>(height * width);
	dst = (SpriteLoader::CommonPixel *)dest_sprite->data;
	src = (SpriteLoader::CommonPixel *)src_sprite->data;

	dest_sprite->width = width;
	dest_sprite->height = height;
	dest_sprite->x_offs = src_sprite->x_offs * 2;
	dest_sprite->y_offs = src_sprite->y_offs * 2;
	SpriteLoader::CommonPixel B, D, E, H, F;
	SpriteLoader::CommonPixel E0, E1, E2, E3;
	uint dst_y = 0;
	uint src_y_idx = 0;
	for (int y = 0; y < src_sprite->height ; y++) {
		int x;

		uint dst_x = 0;
		for (x = 0; x < src_sprite->width - 1; x++) {
			E = src[src_y_idx + x];
			if (src_y_idx) {
				B = src[src_y_idx + x - src_sprite->width];
			} else {
				B = src[src_y_idx + x];
			}
			if (x) {
				D = src[src_y_idx + x - 1];
			} else {
				D = src[src_y_idx + x ];
			}
			if (x < src_sprite->width - 1){
				F = src[src_y_idx + x + 1];
			} else {
				F = src[src_y_idx + x ];
			}
			if (y < src_sprite->height - 1){
				H = src[src_y_idx + x + src_sprite->width];
			} else {
				H = src[src_y_idx + x];
			}
			if ((B.r != H.r && D.r != F.r) ||
				(B.g != H.g && D.g != F.g) ||
				(B.b != H.b && D.b != F.b) ||
				(B.a != H.a && D.a != F.a))
			 {
				if ((D.r == B.r) &&
					(D.g == B.g) &&
					(D.b == B.b) &&
					(D.a == B.a)) {
					E0 = D;
				} else {
					E0 = E;
				}
				if ((B.r == F.r) &&
					(B.g == F.g) &&
					(B.b == F.b) &&
					(B.a == F.a)) {
					E1 = F;
				} else {
					E1 = E;
				}
				if ((D.r == H.r) &&
					(D.g == H.g) &&
					(D.b == H.b) &&
					(D.a == H.a)) {
					E2 = D;
				} else {
					E2 = E;
				}
				if ((H.r == F.r) &&
					(H.g == F.g) &&
					(H.b == F.b) &&
					(H.a == F.a)) {
					E3 = F;
				} else {
					E3 = E;
				}
			} else {
				E0 = E;
				E1 = E;
				E2 = E;
				E3 = E;
			}
			dst[dst_y + dst_x] = E0;
			dst_x++;
			dst[dst_y + dst_x] = E1;
			dst_x--;
			dst_y += width;
			dst[dst_y + dst_x] = E2;
			dst_x++;
			dst[dst_y + dst_x] = E3;
			dst_y -= width;
			dst_x++;
		}

		dst_y += width;
dst_y += width;
		src_y_idx += src_sprite->width;
	}
}

void Blitter_32bppOptimized::FillRGBFromPalette(SpriteLoader::Sprite *sprite)
{
	SpriteLoader::CommonPixel *spr = sprite->data;

	for (uint y = 0; y < sprite->height; y++) {
		uint y_idx  = y * sprite->width;
		for (uint x = 0; x < sprite->width; x++) {
			if (spr[y_idx + x].a == 0) {
				spr[y_idx + x].r = 0;
				spr[y_idx + x].g = 0;
				spr[y_idx + x].b = 0;
				spr[y_idx + x].m = 0;
			} else {
				if (spr[y_idx + x].m != 0) {
					/* Pre-convert the mapping channel to a RGB value */
					uint color = this->LookupColourInPalette(spr[y_idx + x].m);
					spr[y_idx + x].r = GB(color, 16, 8);
					spr[y_idx + x].g = GB(color, 8,  8);
					spr[y_idx + x].b = GB(color, 0,  8);
				}
			}
		}
	}
}

byte *Blitter_32bppOptimized::FillRGBPalette(SpriteID id, byte *remap_data)
{
	for (int idx = 0; (idx < MAX_PALETTE_TABLES); idx++) {
		if ((id == _rgb_palettes[idx].id) || (_rgb_palettes[idx].id == 0)) { 
			_rgb_palettes[idx].id = id;
			for (int col_idx = 0; col_idx < 256; col_idx++) { 
				_rgb_palettes[idx].tables[col_idx].data = this->LookupColourInPalette(remap_data[col_idx + 1]);
			}
			return (byte *)&(_rgb_palettes[idx].tables[0]);
		}
	}
	error("No more rgb palette tables available");
	return NULL;
}

