/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file 32bpp_base.hpp Base for all 32 bits blitters. */

#ifndef BLITTER_32BPP_BASE_HPP
#define BLITTER_32BPP_BASE_HPP

#include "base.hpp"
#include "../core/bitmath_func.hpp"
#include "../core/math_func.hpp"
#include "../gfx_func.h"
#include "../debug.h"
#include <math.h>

extern int _sat, _li;

class Blitter_32bppBase : public Blitter {
public:
	/* virtual */ uint8 GetScreenDepth() { return 32; }
//	/* virtual */ void Draw(Blitter::BlitterParams *bp, BlitterMode mode, ZoomLevel zoom);
//	/* virtual */ void DrawColourMappingRect(void *dst, int width, int height, PaletteID pal);
//	/* virtual */ Sprite *Encode(SpriteLoader::Sprite *sprite, AllocatorProc *allocator);
	/* virtual */ void *MoveTo(const void *video, int x, int y);
	/* virtual */ void SetPixel(void *video, int x, int y, uint8 colour);
	/* virtual */ void DrawRect(void *video, int width, int height, uint8 colour);
	/* virtual */ void DrawLine(void *video, int x, int y, int x2, int y2, int screen_width, int screen_height, uint8 colour);
	/* virtual */ void CopyFromBuffer(void *video, const void *src, int width, int height);
	/* virtual */ void CopyToBuffer(const void *video, void *dst, int width, int height);
	/* virtual */ void CopyImageToBuffer(const void *video, void *dst, int width, int height, int dst_pitch);
	/* virtual */ void ScrollBuffer(void *video, int &left, int &top, int &width, int &height, int scroll_x, int scroll_y);
	/* virtual */ int BufferSize(int width, int height);
	/* virtual */ void PaletteAnimate(uint start, uint count);
	/* virtual */ Blitter::PaletteAnimation UsePaletteAnimation();
	/* virtual */ int GetBytesPerPixel() { return 4; }

	/**
	 * Compose a colour based on RGB values.
	 */
	static inline uint32 ComposeColour(uint a, uint r, uint g, uint b)
	{
		return (((a) << 24) & 0xFF000000) | (((r) << 16) & 0x00FF0000) | (((g) << 8) & 0x0000FF00) | ((b) & 0x000000FF);
	}

	/**
	 * Look up the colour in the current palette.
	 */
	static inline uint32 LookupColourInPalette(uint index)
	{
		return _cur_palette[index].data;
	}

	/**
	 * Compose a colour based on RGBA values and the current pixel value.
	 */
	static inline uint32 ComposeColourRGBANoCheck(uint r, uint g, uint b, uint a, uint32 current)
	{
		uint cr = GB(current, 16, 8);
		uint cg = GB(current, 8,  8);
		uint cb = GB(current, 0,  8);

		/* The 256 is wrong, it should be 255, but 256 is much faster... */
		return ComposeColour(0xFF,
							((int)(r - cr) * a) / 256 + cr,
							((int)(g - cg) * a) / 256 + cg,
							((int)(b - cb) * a) / 256 + cb);
	}

	/**
	 * Compose a colour based on RGBA values and the current pixel value.
	 * Handles fully transparent and solid pixels in a special (faster) way.
	 */
	static inline uint32 ComposeColourRGBA(uint r, uint g, uint b, uint a, uint32 current)
	{
		if (a == 0) return current;
		if (a >= 255) return ComposeColour(0xFF, r, g, b);

		return ComposeColourRGBANoCheck(r, g, b, a, current);
	}

	/**
	 * Compose a colour based on Pixel value, alpha value, and the current pixel value.
	 */
	static inline uint32 ComposeColourPANoCheck(uint32 colour, uint a, uint32 current)
	{
		uint r  = GB(colour,  16, 8);
		uint g  = GB(colour,  8,  8);
		uint b  = GB(colour,  0,  8);

		return ComposeColourRGBANoCheck(r, g, b, a, current);
	}

	/**
	 * Compose a colour based on Pixel value, alpha value, and the current pixel value.
	 * Handles fully transparent and solid pixels in a special (faster) way.
	 */
	static inline uint32 ComposeColourPA(uint32 colour, uint a, uint32 current)
	{
		if (a == 0) return current;
		if (a >= 255) return (colour | 0xFF000000);

		return ComposeColourPANoCheck(colour, a, current);
	}

	/**
	 * Blend a colour based on Pixel value and the current pixel value.
	 */
	static inline uint ComposeColourBlend(uint colour, uint32 current)
	{
		if (colour == current) return colour;

		/* Converting to HSL */

		/* Colour (cc) */
		int r_colour = GB(colour,  16, 8);
		int g_colour = GB(colour,  8,  8);
		int b_colour = GB(colour,  0,  8);
		int hue = 0;
		int saturation = 0;
		int lightness_colour = 0;

		/* Find max and min cc */
		int min_colour = min(min(r_colour, g_colour),b_colour);
		
		if ((r_colour > g_colour) && (r_colour > b_colour)) {
			if (min_colour != r_colour) {
				hue = 60 * (g_colour - b_colour) / (r_colour - min_colour) + 360;
				hue %= 360;
				if ((r_colour + min_colour) <= 256) {
					saturation = (r_colour - min_colour) * 255 / (r_colour + min_colour);
				} else {
					saturation = (r_colour - min_colour) * 255 / (512 - (r_colour + min_colour));
				}
			} else {
				saturation = 0;
				lightness_colour = r_colour;
			}
		} else if (g_colour > b_colour){
			if (min_colour != g_colour) {
				hue = 60 * (b_colour - r_colour) / (g_colour - min_colour) + 120;
				if ((g_colour + min_colour) <= 256) {
					saturation = (g_colour - min_colour) * 255 / (g_colour + min_colour);
				} else {
					saturation = (g_colour - min_colour) * 255 / (512 - (g_colour + min_colour));
				}
			} else {
				saturation = 0;
				lightness_colour = g_colour;
			}
		} else {
			if (min_colour != b_colour) {
				hue = 60 * (r_colour - g_colour) / (b_colour - min_colour) + 240;
				if ((b_colour + min_colour) <= 256) {
					saturation = (b_colour - min_colour) * 255 / (b_colour + min_colour);
				} else {
					saturation = (b_colour - min_colour) * 255 / (512 - (b_colour + min_colour));
				}
			} else {
				saturation = 0;
				lightness_colour = b_colour;
			}
		}

		/* Original colour */
		int r_current = GB(current,  16, 8);
		int g_current = GB(current,  8,  8);
		int b_current = GB(current,  0,  8);
		/* Find max and min original colour */
		int min_current = min(min(r_current, g_current),b_current);
		int max_current = max(max(r_current, g_current),b_current);

		/* Lightness original colour */
		int lightness_current = (max_current + min_current) / 2;

		/* Converting to RGB */
		unsigned int red, green, blue;

		if (saturation == 0) {
			red = (lightness_colour + lightness_current) / 2;
			green = red;
			blue = red;
		} else {
			float q;
			if (lightness_current < 128) {
				q = lightness_current * (1 + saturation / 255.0);
			} else {
				q = lightness_current + saturation - (lightness_current * saturation / 255.0);
			}

			float p = (2 * lightness_current) - q;

			/* Red */
			int hue_r = hue + 120;
			if (hue_r > 360) {
				hue_r -= 360;
			}
			if (hue_r < 60) {
				red = p + ((q - p) * hue_r / 60.0);
			} else if (hue_r < 180){
				red = q;
			} else if (hue_r < 240) {
				red = p + ((q - p) * (4.0 - hue_r / 60.0));
			} else {
				red = p;
			}

			/* Green */
			int hue_g = hue;

			if (hue_g < 60) {
				green = p + ((q - p) * hue_g / 60.0);
			} else if (hue_g < 180){
				green = q;
			} else if (hue_g < 240) {
				green = p + ((q - p) * (4.0 - hue_g / 60.0));
			} else {
				green = p;
			}

			/* Blue */
			int hue_b = hue - 120;
			if (hue_b < 0.0) {
				hue_b += 360;
			} 
			if (hue_b < 60) {
				blue = p + ((q - p) * hue_b / 60.0);
			} else if (hue_b < 180){
				blue = q;
			} else if (hue_b < 240) {
				blue = p + ((q - p) * (4.0 - hue_b / 60.0));
			} else {
				blue = p;
			}
		}

		return ComposeColour(0xff, red, green, blue);
	}

	/**
	 * Make a pixel looks like it is transparent.
	 * @param colour the colour already on the screen.
	 * @param nom the amount of transparency, nominator, makes colour lighter.
	 * @param denom denominator, makes colour darker.
	 * @return the new colour for the screen.
	 */
	static inline uint32 MakeTransparent(uint32 colour, uint nom, uint denom = 256)
	{
		uint r = GB(colour, 16, 8);
		uint g = GB(colour, 8,  8);
		uint b = GB(colour, 0,  8);

		return ComposeColour(0xFF, r * nom / denom, g * nom / denom, b * nom / denom);
	}

	/**
	 * Make a colour grey - based.
	 * @param colour the colour to make grey.
	 * @return the new colour, now grey.
	 */
	static inline uint32 MakeGrey(uint32 colour)
	{
		uint r = GB(colour, 16, 8);
		uint g = GB(colour, 8,  8);
		uint b = GB(colour, 0,  8);

		/* To avoid doubles and stuff, multiple it with a total of 65536 (16bits), then
		 *  divide by it to normalize the value to a byte again. See heightmap.cpp for
		 *  information about the formula. */
		colour = ((r * 19595) + (g * 38470) + (b * 7471)) / 65536;

		return ComposeColour(0xFF, colour, colour, colour);
	}
};

#endif /* BLITTER_32BPP_BASE_HPP */
