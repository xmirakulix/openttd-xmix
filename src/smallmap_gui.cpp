/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file smallmap_gui.cpp GUI that shows a small map of the world with metadata like owner or height. */

#include "stdafx.h"
#include "clear_map.h"
#include "industry.h"
#include "station_map.h"
#include "landscape.h"
#include "window_gui.h"
#include "tree_map.h"
#include "viewport_func.h"
#include "town.h"
#include "blitter/factory.hpp"
#include "tunnelbridge_map.h"
#include "strings_func.h"
#include "core/endian_func.hpp"
#include "vehicle_base.h"
#include "sound_func.h"
#include "window_func.h"
#include "cargotype.h"
#include "openttd.h"
#include "company_func.h"
#include "station_base.h"
#include "company_base.h"

#include "table/strings.h"

#include <cmath>
#include <vector>

/** Widget numbers of the small map window. */
enum SmallMapWindowWidgets {
	SM_WIDGET_CAPTION,           ///< Caption widget.
	SM_WIDGET_MAP_BORDER,        ///< Border around the smallmap.
	SM_WIDGET_MAP,               ///< Panel containing the smallmap.
	SM_WIDGET_LEGEND,            ///< Bottom panel to display smallmap legends.
	SM_WIDGET_BLANK,
	SM_WIDGET_ZOOM_IN,           ///< Button to zoom in one step.
	SM_WIDGET_ZOOM_OUT,          ///< Button to zoom out one step.
	SM_WIDGET_CONTOUR,           ///< Button to select the contour view (height map).
	SM_WIDGET_VEHICLES,          ///< Button to select the vehicles view.
	SM_WIDGET_INDUSTRIES,        ///< Button to select the industries view.
	SM_WIDGET_LINKSTATS,
	SM_WIDGET_ROUTES,            ///< Button to select the routes view.
	SM_WIDGET_VEGETATION,        ///< Button to select the vegetation view.
	SM_WIDGET_OWNERS,            ///< Button to select the owners view.
	SM_WIDGET_CENTERMAP,         ///< Button to move smallmap center to main window center.
	SM_WIDGET_TOGGLETOWNNAME,    ///< Toggle button to display town names.
	SM_WIDGET_SELECTINDUSTRIES,  ///< Selection widget for the buttons at the industry mode.
	SM_WIDGET_ENABLE_ALL,        ///< Button to enable display of all industries or link stats.
	SM_WIDGET_DISABLE_ALL,       ///< Button to disable display of all industries or link stats.
	SM_WIDGET_SHOW_HEIGHT,       ///< Show heightmap toggle button.
};


static int _smallmap_industry_count; ///< Number of used industries

static int _smallmap_cargo_count; ///< number of cargos in the link stats legend

/** Macro for ordinary entry of LegendAndColour */
#define MK(a, b) {a, b, {INVALID_INDUSTRYTYPE}, true, false, false}

/** Macro for a height legend entry with configurable colour. */
#define MC(height)  {0, STR_TINY_BLACK_HEIGHT, {height}, true, false, false}

/** Macro for end of list marker in arrays of LegendAndColour */
#define MKEND() {0, STR_NULL, {INVALID_INDUSTRYTYPE}, true, true, false}

/**
 * Macro for break marker in arrays of LegendAndColour.
 * It will have valid data, though
 */
#define MS(a, b) {a, b, {INVALID_INDUSTRYTYPE}, true, false, true}

/** Structure for holding relevant data for legends in small map */
struct LegendAndColour {
	uint8 colour;              ///< Colour of the item on the map.
	StringID legend;           ///< String corresponding to the coloured item.
	union {
		IndustryType type; ///< Type of industry.
		uint8 height;      ///< Height in tiles.
	} u;
	bool show_on_map;          ///< For filtering industries, if \c true, industry is shown on the map in colour.
	bool end;                  ///< This is the end of the list.
	bool col_break;            ///< Perform a column break and go further at the next column.
};

/** Legend text giving the colours to look for on the minimap */
static LegendAndColour _legend_land_contours[] = {
	/* The colours for the following values are set at BuildLandLegend() based on each colour scheme. */
	MC(0),
	MC(4),
	MC(8),
	MC(12),
	MC(14),

	MS(0xD7, STR_SMALLMAP_LEGENDA_ROADS),
	MK(0x0A, STR_SMALLMAP_LEGENDA_RAILROADS),
	MK(0x98, STR_SMALLMAP_LEGENDA_STATIONS_AIRPORTS_DOCKS),
	MK(0xB5, STR_SMALLMAP_LEGENDA_BUILDINGS_INDUSTRIES),
	MK(0x0F, STR_SMALLMAP_LEGENDA_VEHICLES),
	MKEND()
};

static const LegendAndColour _legend_vehicles[] = {
	MK(0xB8, STR_SMALLMAP_LEGENDA_TRAINS),
	MK(0xBF, STR_SMALLMAP_LEGENDA_ROAD_VEHICLES),
	MK(0x98, STR_SMALLMAP_LEGENDA_SHIPS),
	MK(0x0F, STR_SMALLMAP_LEGENDA_AIRCRAFT),

	MS(0xD7, STR_SMALLMAP_LEGENDA_TRANSPORT_ROUTES),
	MK(0xB5, STR_SMALLMAP_LEGENDA_BUILDINGS_INDUSTRIES),
	MKEND()
};

static const LegendAndColour _legend_routes[] = {
	MK(0xD7, STR_SMALLMAP_LEGENDA_ROADS),
	MK(0x0A, STR_SMALLMAP_LEGENDA_RAILROADS),
	MK(0xB5, STR_SMALLMAP_LEGENDA_BUILDINGS_INDUSTRIES),

	MS(0x56, STR_SMALLMAP_LEGENDA_RAILROAD_STATION),
	MK(0xC2, STR_SMALLMAP_LEGENDA_TRUCK_LOADING_BAY),
	MK(0xBF, STR_SMALLMAP_LEGENDA_BUS_STATION),
	MK(0xB8, STR_SMALLMAP_LEGENDA_AIRPORT_HELIPORT),
	MK(0x98, STR_SMALLMAP_LEGENDA_DOCK),
	MKEND()
};

static const LegendAndColour _legend_vegetation[] = {
	MK(0x52, STR_SMALLMAP_LEGENDA_ROUGH_LAND),
	MK(0x54, STR_SMALLMAP_LEGENDA_GRASS_LAND),
	MK(0x37, STR_SMALLMAP_LEGENDA_BARE_LAND),
	MK(0x25, STR_SMALLMAP_LEGENDA_FIELDS),
	MK(0x57, STR_SMALLMAP_LEGENDA_TREES),
	MK(0xD0, STR_SMALLMAP_LEGENDA_FOREST),

	MS(0x0A, STR_SMALLMAP_LEGENDA_ROCKS),
	MK(0xC2, STR_SMALLMAP_LEGENDA_DESERT),
	MK(0x98, STR_SMALLMAP_LEGENDA_SNOW),
	MK(0xD7, STR_SMALLMAP_LEGENDA_TRANSPORT_ROUTES),
	MK(0xB5, STR_SMALLMAP_LEGENDA_BUILDINGS_INDUSTRIES),
	MKEND()
};

static const LegendAndColour _legend_land_owners[] = {
	MK(0xCA, STR_SMALLMAP_LEGENDA_WATER),
	MK(0x54, STR_SMALLMAP_LEGENDA_NO_OWNER),
	MK(0xB4, STR_SMALLMAP_LEGENDA_TOWNS),
	MK(0x20, STR_SMALLMAP_LEGENDA_INDUSTRIES),
	MKEND()
};

#undef MK
#undef MC
#undef MS
#undef MKEND

/**
 * Allow room for all industries, plus a terminator entry
 * This is required in order to have the indutry slots all filled up
 */
static LegendAndColour _legend_from_industries[NUM_INDUSTRYTYPES + 1];
/* For connecting industry type to position in industries list(small map legend) */
static uint _industry_to_list_pos[NUM_INDUSTRYTYPES];
/** Show heightmap in smallmap window. */
static bool _smallmap_show_heightmap;

static uint8 _smallmap_link_colours[] = {
	0x0f, 0xd1, 0xd0, 0x57,
	0x55, 0x53, 0xbf, 0xbd,
	0xba, 0xb9, 0xb7, 0xb5
};

/**
 * Fills an array for the industries legends.
 */
void BuildIndustriesLegend()
{
	uint j = 0;

	/* Add each name */
	for (uint8 i = 0; i < NUM_INDUSTRYTYPES; i++) {
		IndustryType ind = _sorted_industry_types[i];
		const IndustrySpec *indsp = GetIndustrySpec(ind);
		if (indsp->enabled) {
			_legend_from_industries[j].legend = indsp->name;
			_legend_from_industries[j].colour = indsp->map_colour;
			_legend_from_industries[j].u.type = ind;
			_legend_from_industries[j].show_on_map = true;
			_legend_from_industries[j].col_break = false;
			_legend_from_industries[j].end = false;

			/* Store widget number for this industry type. */
			_industry_to_list_pos[ind] = j;
			j++;
		}
	}
	/* Terminate the list */
	_legend_from_industries[j].end = true;

	/* Store number of enabled industries */
	_smallmap_industry_count = j;
}

static LegendAndColour _legend_linkstats[NUM_CARGO + 1];

/**
 * Populate legend table for the route map view.
 */
void BuildLinkStatsLegend()
{
	/* Clear the legend */
	memset(_legend_linkstats, 0, sizeof(_legend_linkstats));

	uint i = 0;
	for (; i < _sorted_cargo_specs_size; ++i) {
		const CargoSpec *cs = _sorted_cargo_specs[i];

		_legend_linkstats[i].legend = cs->name;
		_legend_linkstats[i].colour = cs->legend_colour;
		_legend_linkstats[i].u.type = cs->Index();
		_legend_linkstats[i].show_on_map = true;
	}

	_legend_linkstats[i].end = true;

	_smallmap_cargo_count = i;
}

static const LegendAndColour * const _legend_table[] = {
	_legend_land_contours,
	_legend_vehicles,
	_legend_from_industries,
	_legend_linkstats,
	_legend_routes,
	_legend_vegetation,
	_legend_land_owners,
};

#define MKCOLOUR(x) TO_LE32X(x)

/** Height map colours for the green colour scheme, ordered by height. */
static const uint32 _green_map_heights[] = {
	MKCOLOUR(0x5A5A5A5A),
	MKCOLOUR(0x5A5B5A5B),
	MKCOLOUR(0x5B5B5B5B),
	MKCOLOUR(0x5B5C5B5C),
	MKCOLOUR(0x5C5C5C5C),
	MKCOLOUR(0x5C5D5C5D),
	MKCOLOUR(0x5D5D5D5D),
	MKCOLOUR(0x5D5E5D5E),
	MKCOLOUR(0x5E5E5E5E),
	MKCOLOUR(0x5E5F5E5F),
	MKCOLOUR(0x5F5F5F5F),
	MKCOLOUR(0x5F1F5F1F),
	MKCOLOUR(0x1F1F1F1F),
	MKCOLOUR(0x1F271F27),
	MKCOLOUR(0x27272727),
	MKCOLOUR(0x27272727),
};
assert_compile(lengthof(_green_map_heights) == MAX_TILE_HEIGHT + 1);

/** Height map colours for the dark green colour scheme, ordered by height. */
static const uint32 _dark_green_map_heights[] = {
	MKCOLOUR(0x60606060),
	MKCOLOUR(0x60616061),
	MKCOLOUR(0x61616161),
	MKCOLOUR(0x61626162),
	MKCOLOUR(0x62626262),
	MKCOLOUR(0x62636263),
	MKCOLOUR(0x63636363),
	MKCOLOUR(0x63646364),
	MKCOLOUR(0x64646464),
	MKCOLOUR(0x64656465),
	MKCOLOUR(0x65656565),
	MKCOLOUR(0x65666566),
	MKCOLOUR(0x66666666),
	MKCOLOUR(0x66676667),
	MKCOLOUR(0x67676767),
	MKCOLOUR(0x67676767),
};
assert_compile(lengthof(_dark_green_map_heights) == MAX_TILE_HEIGHT + 1);

/** Height map colours for the violet colour scheme, ordered by height. */
static const uint32 _violet_map_heights[] = {
	MKCOLOUR(0x80808080),
	MKCOLOUR(0x80818081),
	MKCOLOUR(0x81818181),
	MKCOLOUR(0x81828182),
	MKCOLOUR(0x82828282),
	MKCOLOUR(0x82838283),
	MKCOLOUR(0x83838383),
	MKCOLOUR(0x83848384),
	MKCOLOUR(0x84848484),
	MKCOLOUR(0x84858485),
	MKCOLOUR(0x85858585),
	MKCOLOUR(0x85868586),
	MKCOLOUR(0x86868686),
	MKCOLOUR(0x86878687),
	MKCOLOUR(0x87878787),
	MKCOLOUR(0x87878787),
};
assert_compile(lengthof(_violet_map_heights) == MAX_TILE_HEIGHT + 1);

/** Colour scheme of the smallmap. */
struct SmallMapColourScheme {
	const uint32 *height_colours; ///< Colour of each level in a heightmap.
	uint32 default_colour;   ///< Default colour of the land.
};

/** Available colour schemes for height maps. */
static const SmallMapColourScheme _heightmap_schemes[] = {
	{_green_map_heights,      MKCOLOUR(0x54545454)}, ///< Green colour scheme.
	{_dark_green_map_heights, MKCOLOUR(0x62626262)}, ///< Dark green colour scheme.
	{_violet_map_heights,     MKCOLOUR(0x82828282)}, ///< Violet colour scheme.
};

void BuildLandLegend()
{
	for (LegendAndColour *lc = _legend_land_contours; lc->legend == STR_TINY_BLACK_HEIGHT; lc++) {
		lc->colour = _heightmap_schemes[_settings_client.gui.smallmap_land_colour].height_colours[lc->u.height];
	}
}

struct AndOr {
	uint32 mor;
	uint32 mand;
};

static inline uint32 ApplyMask(uint32 colour, const AndOr *mask)
{
	return (colour & mask->mand) | mask->mor;
}


/** Colour masks for "Contour" and "Routes" modes. */
static const AndOr _smallmap_contours_andor[] = {
	{MKCOLOUR(0x00000000), MKCOLOUR(0xFFFFFFFF)}, // MP_CLEAR
	{MKCOLOUR(0x000A0A00), MKCOLOUR(0xFF0000FF)}, // MP_RAILWAY
	{MKCOLOUR(0x00D7D700), MKCOLOUR(0xFF0000FF)}, // MP_ROAD
	{MKCOLOUR(0x00B5B500), MKCOLOUR(0xFF0000FF)}, // MP_HOUSE
	{MKCOLOUR(0x00000000), MKCOLOUR(0xFFFFFFFF)}, // MP_TREES
	{MKCOLOUR(0x98989898), MKCOLOUR(0x00000000)}, // MP_STATION
	{MKCOLOUR(0xCACACACA), MKCOLOUR(0x00000000)}, // MP_WATER
	{MKCOLOUR(0x00000000), MKCOLOUR(0xFFFFFFFF)}, // MP_VOID
	{MKCOLOUR(0xB5B5B5B5), MKCOLOUR(0x00000000)}, // MP_INDUSTRY
	{MKCOLOUR(0x00000000), MKCOLOUR(0xFFFFFFFF)}, // MP_TUNNELBRIDGE
	{MKCOLOUR(0x00B5B500), MKCOLOUR(0xFF0000FF)}, // MP_OBJECT
	{MKCOLOUR(0x000A0A00), MKCOLOUR(0xFF0000FF)},
};

/** Colour masks for "Vehicles", "Industry", and "Vegetation" modes. */
static const AndOr _smallmap_vehicles_andor[] = {
	{MKCOLOUR(0x00000000), MKCOLOUR(0xFFFFFFFF)}, // MP_CLEAR
	{MKCOLOUR(0x00D7D700), MKCOLOUR(0xFF0000FF)}, // MP_RAILWAY
	{MKCOLOUR(0x00D7D700), MKCOLOUR(0xFF0000FF)}, // MP_ROAD
	{MKCOLOUR(0x00B5B500), MKCOLOUR(0xFF0000FF)}, // MP_HOUSE
	{MKCOLOUR(0x00000000), MKCOLOUR(0xFFFFFFFF)}, // MP_TREES
	{MKCOLOUR(0x00D7D700), MKCOLOUR(0xFF0000FF)}, // MP_STATION
	{MKCOLOUR(0xCACACACA), MKCOLOUR(0x00000000)}, // MP_WATER
	{MKCOLOUR(0x00000000), MKCOLOUR(0xFFFFFFFF)}, // MP_VOID
	{MKCOLOUR(0xB5B5B5B5), MKCOLOUR(0x00000000)}, // MP_INDUSTRY
	{MKCOLOUR(0x00000000), MKCOLOUR(0xFFFFFFFF)}, // MP_TUNNELBRIDGE
	{MKCOLOUR(0x00B5B500), MKCOLOUR(0xFF0000FF)}, // MP_OBJECT
	{MKCOLOUR(0x00D7D700), MKCOLOUR(0xFF0000FF)},
};

/** Mapping of tile type to importance of the tile (higher number means more interesting to show). */
static const byte _tiletype_importance[] = {
	2, // MP_CLEAR
	8, // MP_RAILWAY
	7, // MP_ROAD
	5, // MP_HOUSE
	2, // MP_TREES
	9, // MP_STATION
	2, // MP_WATER
	1, // MP_VOID
	6, // MP_INDUSTRY
	8, // MP_TUNNELBRIDGE
	2, // MP_OBJECT
	0,
};


static inline TileType GetEffectiveTileType(TileIndex tile)
{
	TileType t = GetTileType(tile);

	if (t == MP_TUNNELBRIDGE) {
		TransportType tt = GetTunnelBridgeTransportType(tile);

		switch (tt) {
			case TRANSPORT_RAIL: t = MP_RAILWAY; break;
			case TRANSPORT_ROAD: t = MP_ROAD;    break;
			default:             t = MP_WATER;   break;
		}
	}
	return t;
}

/**
 * Return the colour a tile would be displayed with in the small map in mode "Contour".
 * @param tile The tile of which we would like to get the colour.
 * @param t    Effective tile type of the tile (see #GetEffectiveTileType).
 * @return The colour of tile in the small map in mode "Contour"
 */
static inline uint32 GetSmallMapContoursPixels(TileIndex tile, TileType t)
{
	const SmallMapColourScheme *cs = &_heightmap_schemes[_settings_client.gui.smallmap_land_colour];
	return ApplyMask(cs->height_colours[TileHeight(tile)], &_smallmap_contours_andor[t]);
}

/**
 * Return the colour a tile would be displayed with in the small map in mode "Vehicles".
 *
 * @param tile The tile of which we would like to get the colour.
 * @param t    Effective tile type of the tile (see #GetEffectiveTileType).
 * @return The colour of tile in the small map in mode "Vehicles"
 */
static inline uint32 GetSmallMapVehiclesPixels(TileIndex tile, TileType t)
{
	const SmallMapColourScheme *cs = &_heightmap_schemes[_settings_client.gui.smallmap_land_colour];
	return ApplyMask(cs->default_colour, &_smallmap_vehicles_andor[t]);
}

/**
 * Return the colour a tile would be displayed with in the small map in mode "Industries".
 *
 * @param tile The tile of which we would like to get the colour.
 * @param t    Effective tile type of the tile (see #GetEffectiveTileType).
 * @return The colour of tile in the small map in mode "Industries"
 */
static inline uint32 GetSmallMapIndustriesPixels(TileIndex tile, TileType t)
{
	if (t == MP_INDUSTRY) {
		/* If industry is allowed to be seen, use its colour on the map */
		if (_legend_from_industries[_industry_to_list_pos[Industry::GetByTile(tile)->type]].show_on_map) {
			return GetIndustrySpec(Industry::GetByTile(tile)->type)->map_colour * 0x01010101;
		} else {
			/* Otherwise, return the colour which will make it disappear */
			t = (IsTileOnWater(tile) ? MP_WATER : MP_CLEAR);
		}
	}

	const SmallMapColourScheme *cs = &_heightmap_schemes[_settings_client.gui.smallmap_land_colour];
	return ApplyMask(_smallmap_show_heightmap ? cs->height_colours[TileHeight(tile)] : cs->default_colour, &_smallmap_vehicles_andor[t]);
}

/**
 * Return the colour a tile would be displayed with in the small map in mode "Routes".
 *
 * @param tile The tile of which we would like to get the colour.
 * @param t    Effective tile type of the tile (see #GetEffectiveTileType).
 * @return The colour of tile  in the small map in mode "Routes"
 */
static inline uint32 GetSmallMapRoutesPixels(TileIndex tile, TileType t)
{
	if (t == MP_STATION) {
		switch (GetStationType(tile)) {
			case STATION_RAIL:    return MKCOLOUR(0x56565656);
			case STATION_AIRPORT: return MKCOLOUR(0xB8B8B8B8);
			case STATION_TRUCK:   return MKCOLOUR(0xC2C2C2C2);
			case STATION_BUS:     return MKCOLOUR(0xBFBFBFBF);
			case STATION_DOCK:    return MKCOLOUR(0x98989898);
			default:              return MKCOLOUR(0xFFFFFFFF);
		}
	} else if (t == MP_RAILWAY) {
		AndOr andor = {
			GetRailTypeInfo(GetRailType(tile))->map_colour * MKCOLOUR(0x00010100),
			_smallmap_contours_andor[t].mand
		};

		const SmallMapColourScheme *cs = &_heightmap_schemes[_settings_client.gui.smallmap_land_colour];
		return ApplyMask(cs->default_colour, &andor);
	}

	/* Ground colour */
	const SmallMapColourScheme *cs = &_heightmap_schemes[_settings_client.gui.smallmap_land_colour];
	return ApplyMask(cs->default_colour, &_smallmap_contours_andor[t]);
}

/**
 * Return the colour a tile would be displayed with in the small map in mode "link stats".
 *
 * @param tile The tile of which we would like to get the colour.
 * @param t    Effective tile type of the tile (see #GetEffectiveTileType).
 * @return The colour of tile in the small map in mode "link stats"
 */
static inline uint32 GetSmallMapLinkStatsPixels(TileIndex tile, TileType t)
{
	return _smallmap_show_heightmap ? GetSmallMapContoursPixels(tile, t) : GetSmallMapRoutesPixels(tile, t);
}


static const uint32 _vegetation_clear_bits[] = {
	MKCOLOUR(0x54545454), ///< full grass
	MKCOLOUR(0x52525252), ///< rough land
	MKCOLOUR(0x0A0A0A0A), ///< rocks
	MKCOLOUR(0x25252525), ///< fields
	MKCOLOUR(0x98989898), ///< snow
	MKCOLOUR(0xC2C2C2C2), ///< desert
	MKCOLOUR(0x54545454), ///< unused
	MKCOLOUR(0x54545454), ///< unused
};

/**
 * Return the colour a tile would be displayed with in the smallmap in mode "Vegetation".
 *
 * @param tile The tile of which we would like to get the colour.
 * @param t    Effective tile type of the tile (see #GetEffectiveTileType).
 * @return The colour of tile  in the smallmap in mode "Vegetation"
 */
static inline uint32 GetSmallMapVegetationPixels(TileIndex tile, TileType t)
{
	switch (t) {
		case MP_CLEAR:
			return (IsClearGround(tile, CLEAR_GRASS) && GetClearDensity(tile) < 3) ? MKCOLOUR(0x37373737) : _vegetation_clear_bits[GetClearGround(tile)];

		case MP_INDUSTRY:
			return GetIndustrySpec(Industry::GetByTile(tile)->type)->check_proc == CHECK_FOREST ? MKCOLOUR(0xD0D0D0D0) : MKCOLOUR(0xB5B5B5B5);

		case MP_TREES:
			if (GetTreeGround(tile) == TREE_GROUND_SNOW_DESERT || GetTreeGround(tile) == TREE_GROUND_ROUGH_SNOW) {
				return (_settings_game.game_creation.landscape == LT_ARCTIC) ? MKCOLOUR(0x98575798) : MKCOLOUR(0xC25757C2);
			}
			return MKCOLOUR(0x54575754);

		default:
			return ApplyMask(MKCOLOUR(0x54545454), &_smallmap_vehicles_andor[t]);
	}
}


static uint32 _owner_colours[OWNER_END + 1];

/**
 * Return the colour a tile would be displayed with in the small map in mode "Owner".
 *
 * @param tile The tile of which we would like to get the colour.
 * @param t    Effective tile type of the tile (see #GetEffectiveTileType).
 * @return The colour of tile in the small map in mode "Owner"
 */
static inline uint32 GetSmallMapOwnerPixels(TileIndex tile, TileType t)
{
	Owner o;

	switch (t) {
		case MP_INDUSTRY: o = OWNER_END;          break;
		case MP_HOUSE:    o = OWNER_TOWN;         break;
		default:          o = GetTileOwner(tile); break;
		/* FIXME: For MP_ROAD there are multiple owners.
		 * GetTileOwner returns the rail owner (level crossing) resp. the owner of ROADTYPE_ROAD (normal road),
		 * even if there are no ROADTYPE_ROAD bits on the tile.
		 */
	}

	return _owner_colours[o];
}


/** Vehicle colours in #SMT_VEHICLES mode. Indexed by #VehicleTypeByte. */
static const byte _vehicle_type_colours[6] = {
	184, 191, 152, 15, 215, 184
};


void DrawVertex(int x, int y, int size, int colour, int boder_colour)
{
	size--;
	int w1 = size / 2;
	int w2 = size / 2 + size % 2;

	GfxFillRect(x - w1, y - w1, x + w2, y + w2, colour);

	w1++;
	w2++;
	GfxDrawLine(x - w1, y - w1, x + w2, y - w1, boder_colour);
	GfxDrawLine(x - w1, y + w2, x + w2, y + w2, boder_colour);
	GfxDrawLine(x - w1, y - w1, x - w1, y + w2, boder_colour);
	GfxDrawLine(x + w2, y - w1, x + w2, y + w2, boder_colour);
}

/** Class managing the smallmap window. */
class SmallMapWindow : public Window {
	/** Types of legends in the #SM_WIDGET_LEGEND widget. */
	enum SmallMapType {
		SMT_CONTOUR,
		SMT_VEHICLES,
		SMT_INDUSTRY,
		SMT_LINKSTATS,
		SMT_ROUTES,
		SMT_VEGETATION,
		SMT_OWNER,
	};

	/**
	 * Save the Vehicle's old position here, so that we don't get glitches when
	 * redrawing.
	 * The glitches happen when a vehicle occupies a larger area (zoom-in) and
	 * a partial redraw happens which only covers part of the vehicle. If the
	 * vehicle has moved in the meantime, it looks ugly afterwards.
	 */
	struct VehicleAndPosition {
		VehicleAndPosition(const Vehicle *v) : vehicle(v->index)
		{
			this->position.x = v->x_pos;
			this->position.y = v->y_pos;
		}

		Point position;
		VehicleID vehicle;
	};

	typedef std::list<VehicleAndPosition> VehicleList;
	VehicleList vehicles_on_map; ///< cached vehicle positions to avoid glitches
	
	/** Available kinds of zoomlevel changes. */
	enum ZoomLevelChange {
		ZLC_INITIALIZE, ///< Initialize zoom level.
		ZLC_ZOOM_OUT,   ///< Zoom out.
		ZLC_ZOOM_IN,    ///< Zoom in.
	};

	static SmallMapType map_type; ///< Currently displayed legends.
	static bool show_towns;       ///< Display town names in the smallmap.

	static const uint LEGEND_BLOB_WIDTH = 8;              ///< Width of the coloured blob in front of a line text in the #SM_WIDGET_LEGEND widget.
	static const uint INDUSTRY_MIN_NUMBER_OF_COLUMNS = 2; ///< Minimal number of columns in the #SM_WIDGET_LEGEND widget for the #SMT_INDUSTRY legend.
	uint min_number_of_columns;    ///< Minimal number of columns in  legends.
	uint min_number_of_fixed_rows; ///< Minimal number of rows in the legends for the fixed layouts only (all except #SMT_INDUSTRY).
	uint column_width;             ///< Width of a column in the #SM_WIDGET_LEGEND widget.

	bool HasButtons()
	{
		return this->map_type == SMT_INDUSTRY || this->map_type == SMT_LINKSTATS;
	}

	Point cursor;

	struct BaseCargoDetail {
		BaseCargoDetail()
		{
			this->Clear();
		}

		void Clear()
		{
			this->capacity = this->usage = this->planned = 0;
		}

		uint capacity;
		uint usage;
		uint planned;
	};

	struct CargoDetail : public BaseCargoDetail {
		CargoDetail(const LegendAndColour *c, const LinkStat &ls, const FlowStat &fs) : legend(c)
		{
			this->AddLink(ls, fs);
		}

		void AddLink(const LinkStat &orig_link, const FlowStat &orig_flow)
		{
			this->capacity += orig_link.Capacity();
			this->usage += orig_link.Usage();
			this->planned += orig_flow.Planned();
		}

		const LegendAndColour *legend;
	};

	typedef std::vector<CargoDetail> StatVector;

	struct LinkDetails {
		LinkDetails() {this->Clear();}

		StationID sta;
		StationID stb;
		StatVector a_to_b;
		StatVector b_to_a;

		void Clear()
		{
			this->sta = INVALID_STATION;
			this->stb = INVALID_STATION;
			this->a_to_b.clear();
			this->b_to_a.clear();
		}

		bool Empty() const
		{
			return this->sta == INVALID_STATION;
		}
	};

	/**
	 * those are detected while drawing the links and used when drawing
	 * the legend. They don't represent game state.
	 */
	mutable LinkDetails link_details;
	mutable StationID supply_details;

	int32 scroll_x;  ///< Horizontal world coordinate of the base tile left of the top-left corner of the smallmap display.
	int32 scroll_y;  ///< Vertical world coordinate of the base tile left of the top-left corner of the smallmap display.
	int32 subscroll; ///< Number of pixels (0..3) between the right end of the base tile and the pixel at the top-left corner of the smallmap display.
	int zoom;        ///< Zoom level. Bigger number means more zoom-out (further away).

	static const uint8 FORCE_REFRESH_PERIOD = 0x1F; ///< map is redrawn after that many ticks
	static const uint8 REFRESH_NEXT_TICK = 1;       ///< if refresh has this value the map is redrawn in the next tick
	uint8 refresh; ///< refresh counter, zeroed every FORCE_REFRESH_PERIOD ticks

	/**
	 * Remap tile to location on this smallmap.
	 * @param tile_x X coordinate of the tile.
	 * @param tile_y Y coordinate of the tile.
	 * @return Position to draw on.
	 */
	FORCEINLINE Point RemapTile(int tile_x, int tile_y) const
	{
		if (this->zoom > 0) {
			int x_offset = tile_x - this->scroll_x / (int)TILE_SIZE;
			int y_offset = tile_y - this->scroll_y / (int)TILE_SIZE;

			/* For negative offsets, round towards -inf. */
			if (x_offset < 0) x_offset -= this->zoom - 1;
			if (y_offset < 0) y_offset -= this->zoom - 1;

			return RemapCoords(x_offset / this->zoom, y_offset / this->zoom, 0);
		} else {
			int x_offset = tile_x * (-this->zoom) - this->scroll_x * (-this->zoom) / (int)TILE_SIZE;
			int y_offset = tile_y * (-this->zoom) - this->scroll_y * (-this->zoom) / (int)TILE_SIZE;

			return RemapCoords(x_offset, y_offset, 0);
		}
	}

	/**
	 * Determine the world coordinates relative to the base tile of the smallmap, and the pixel position at
	 * that location for a point in the smallmap.
	 * @param px       Horizontal coordinate of the pixel.
	 * @param py       Vertical coordinate of the pixel.
	 * @param sub[out] Pixel position at the tile (0..3).
	 * @param add_sub  Add current #subscroll to the position.
	 * @return world coordinates being displayed at the given position relative to #scroll_x and #scroll_y.
	 * @note The #subscroll offset is already accounted for.
	 */
	FORCEINLINE Point PixelToWorld(int px, int py, int *sub, bool add_sub = true) const
	{
		if (add_sub) px += this->subscroll;  // Total horizontal offset.

		/* For each two rows down, add a x and a y tile, and
		 * For each four pixels to the right, move a tile to the right. */
		Point pt = {
			((py >> 1) - (px >> 2)) * TILE_SIZE,
			((py >> 1) + (px >> 2)) * TILE_SIZE
		};

		if (this->zoom > 0) {
			pt.x *= this->zoom;
			pt.y *= this->zoom;
		} else {
			pt.x /= (-this->zoom);
			pt.y /= (-this->zoom);
		}

		px &= 3;

		if (py & 1) { // Odd number of rows, handle the 2 pixel shift.
			int offset = this->zoom > 0 ? this->zoom * TILE_SIZE : TILE_SIZE / (-this->zoom);
			if (px < 2) {
				pt.x += offset;
				px += 2;
			} else {
				pt.y += offset;
				px -= 2;
			}
		}

		*sub = px;
		return pt;
	}

	/**
	 * Compute base parameters of the smallmap such that tile (\a tx, \a ty) starts at pixel (\a x, \a y).
	 * @param tx        Tile x coordinate.
	 * @param ty        Tile y coordinate.
	 * @param x         Non-negative horizontal position in the display where the tile starts.
	 * @param y         Non-negative vertical position in the display where the tile starts.
	 * @param sub [out] Value of #subscroll needed.
	 * @return #scroll_x, #scroll_y values.
	 */
	Point ComputeScroll(int tx, int ty, int x, int y, int *sub)
	{
		assert(x >= 0 && y >= 0);

		int new_sub;
		Point tile_xy = PixelToWorld(x, y, &new_sub, false);
		tx -= tile_xy.x;
		ty -= tile_xy.y;

		int offset = this->zoom < 0 ? TILE_SIZE / (-this->zoom) : this->zoom * TILE_SIZE;

		Point scroll;
		if (new_sub == 0) {
			*sub = 0;
			scroll.x = tx + offset;
			scroll.y = ty - offset;
		} else {
			*sub = 4 - new_sub;
			scroll.x = tx + 2 * offset;
			scroll.y = ty - 2 * offset;
		}
		return scroll;
	}

	/**
	 * Initialize or change the zoom level.
	 * @param change  Way to change the zoom level.
	 * @param zoom_pt Position to keep fixed while zooming.
	 * @pre \c *zoom_pt should contain a point in the smallmap display when zooming in or out.
	 */
	void SetZoomLevel(ZoomLevelChange change, const Point *zoom_pt)
	{
		static const int zoomlevels[] = {-4, -2, 1, 2, 4, 6, 8}; // Available zoom levels. Bigger number means more zoom-out (further away).
		static const int MIN_ZOOM_INDEX = 0;
		static const int DEFAULT_ZOOM_INDEX = 2;
		static const int MAX_ZOOM_INDEX = lengthof(zoomlevels) - 1;

		int new_index, cur_index, sub;
		Point position;
		switch (change) {
			case ZLC_INITIALIZE:
				cur_index = - 1; // Definitely different from new_index.
				new_index = DEFAULT_ZOOM_INDEX;
				break;

			case ZLC_ZOOM_IN:
			case ZLC_ZOOM_OUT:
				for (cur_index = MIN_ZOOM_INDEX; cur_index <= MAX_ZOOM_INDEX; cur_index++) {
					if (this->zoom == zoomlevels[cur_index]) break;
				}
				assert(cur_index <= MAX_ZOOM_INDEX);

				position = this->PixelToWorld(zoom_pt->x, zoom_pt->y, &sub);
				new_index = Clamp(cur_index + ((change == ZLC_ZOOM_IN) ? -1 : 1), MIN_ZOOM_INDEX, MAX_ZOOM_INDEX);
				break;

			default: NOT_REACHED();
		}

		if (new_index != cur_index) {
			this->zoom = zoomlevels[new_index];
			if (cur_index >= 0) {
				Point new_pos = this->PixelToWorld(zoom_pt->x, zoom_pt->y, &sub);
				this->SetNewScroll(this->scroll_x + position.x - new_pos.x,
						this->scroll_y + position.y - new_pos.y, sub);
			}
			this->SetWidgetDisabledState(SM_WIDGET_ZOOM_IN,  this->zoom == zoomlevels[MIN_ZOOM_INDEX]);
			this->SetWidgetDisabledState(SM_WIDGET_ZOOM_OUT, this->zoom == zoomlevels[MAX_ZOOM_INDEX]);
			this->SetDirty();
		}
	}

	/**
	 * Decide which colours to show to the user for a group of tiles.
	 * @param ta Tile area to investigate.
	 * @return Colours to display.
	 */
	inline uint32 GetTileColours(const TileArea &ta) const
	{
		int importance = 0;
		TileIndex tile = INVALID_TILE; // Position of the most important tile.
		TileType et = MP_VOID;         // Effective tile type at that position.

		TILE_AREA_LOOP(ti, ta) {
			TileType ttype = GetEffectiveTileType(ti);
			if (_tiletype_importance[ttype] > importance) {
				importance = _tiletype_importance[ttype];
				tile = ti;
				et = ttype;
			}
		}

		switch (this->map_type) {
			case SMT_CONTOUR:
				return GetSmallMapContoursPixels(tile, et);

			case SMT_VEHICLES:
				return GetSmallMapVehiclesPixels(tile, et);

			case SMT_INDUSTRY:
				return GetSmallMapIndustriesPixels(tile, et);

			case SMT_ROUTES:
				return GetSmallMapRoutesPixels(tile, et);

			case SMT_VEGETATION:
				return GetSmallMapVegetationPixels(tile, et);

			case SMT_OWNER:
				return GetSmallMapOwnerPixels(tile, et);

			case SMT_LINKSTATS:
				return GetSmallMapLinkStatsPixels(tile, et);

			default: NOT_REACHED();
		}
	}

	/**
	 * Draws one column of tiles of the small map in a certain mode onto the screen buffer, skipping the shifted rows in between.
	 *
	 * @param dst Pointer to a part of the screen buffer to write to.
	 * @param xc The world X coordinate of the rightmost place in the column.
	 * @param yc The world Y coordinate of the topmost place in the column.
	 * @param pitch Number of pixels to advance in the screen buffer each time a pixel is written.
	 * @param reps Number of lines to draw
	 * @param start_pos Position of first pixel to draw.
	 * @param end_pos Position of last pixel to draw (exclusive).
	 * @param blitter current blitter
	 * @note If pixel position is below \c 0, skip drawing.
	 * @see GetSmallMapPixels(TileIndex)
	 */
	void DrawSmallMapColumn(void *dst, uint xc, uint yc, int pitch, int reps, int start_pos, int end_pos, Blitter *blitter) const
	{
		void *dst_ptr_abs_end = blitter->MoveTo(_screen.dst_ptr, 0, _screen.height);
		uint min_xy = _settings_game.construction.freeform_edges ? 1 : 0;

		int increment = this->zoom > 0 ? this->zoom * TILE_SIZE : TILE_SIZE / (-this->zoom);
		int extent = this->zoom > 0 ? this->zoom : 1;

		do {
			/* Check if the tile (xc,yc) is within the map range */
			if (xc / TILE_SIZE >= MapMaxX() || yc / TILE_SIZE >= MapMaxY()) continue;

			/* Check if the dst pointer points to a pixel inside the screen buffer */
			if (dst < _screen.dst_ptr) continue;
			if (dst >= dst_ptr_abs_end) continue;

			/* Construct tilearea covered by (xc, yc, xc + this->zoom, yc + this->zoom) such that it is within min_xy limits. */
			TileArea ta;
			if (min_xy == 1 && (xc < TILE_SIZE || yc < TILE_SIZE)) {
				if (this->zoom <= 1) continue; // The tile area is empty, don't draw anything.

				ta = TileArea(TileXY(max(min_xy, xc / TILE_SIZE), max(min_xy, yc / TILE_SIZE)), this->zoom - (xc < TILE_SIZE), this->zoom - (yc < TILE_SIZE));
			} else {
				ta = TileArea(TileXY(xc / TILE_SIZE, yc / TILE_SIZE), extent, extent);
			}
			ta.ClampToMap(); // Clamp to map boundaries (may contain MP_VOID tiles!).

			uint32 val = this->GetTileColours(ta);
			uint8 *val8 = (uint8 *)&val;
			int idx = max(0, -start_pos);
			for (int pos = max(0, start_pos); pos < end_pos; pos++) {
				blitter->SetPixel(dst, idx, 0, val8[idx]);
				idx++;
			}
		/* Switch to next tile in the column */
		} while (xc += increment, yc += increment, dst = blitter->MoveTo(dst, pitch, 0), --reps != 0);
	}

	/**
	 * Adds vehicles to the smallmap.
	 * @param dpi the part of the smallmap to be drawn into
	 * @param blitter current blitter
	 */
	void DrawVehicles(const DrawPixelInfo *dpi, Blitter *blitter) const
	{
		for(VehicleList::const_iterator i = this->vehicles_on_map.begin(); i != this->vehicles_on_map.end(); ++i) {
			const Vehicle *v = Vehicle::GetIfValid(i->vehicle);
			if (v == NULL) continue;

			/* Remap into flat coordinates. */
			Point pt = RemapTile(i->position.x / (int)TILE_SIZE, i->position.y / (int)TILE_SIZE);

			int y = pt.y - dpi->top;
			int x = pt.x - this->subscroll - 3 - dpi->left; // Offset X coordinate.

			int scale = this->zoom < 0 ? -this->zoom : 1;

			/* Calculate pointer to pixel and the colour */
			byte colour = (this->map_type == SMT_VEHICLES) ? _vehicle_type_colours[v->type] : 0xF;

			/* Draw rhombus */
			for (int dy = 0; dy < scale; dy++) {
				for (int dx = 0; dx < scale; dx++) {
					Point pt = RemapCoords(dx, dy, 0);
					if (IsInsideMM(y + pt.y, 0, dpi->height)) {
						if (IsInsideMM(x + pt.x, 0, dpi->width)) {
							blitter->SetPixel(dpi->dst_ptr, x + pt.x, y + pt.y, colour);
						}
						if (IsInsideMM(x + pt.x + 1, 0, dpi->width)) {
							blitter->SetPixel(dpi->dst_ptr, x + pt.x + 1, y + pt.y, colour);
						}
					}
				}
			}
		}
	}

	FORCEINLINE Point GetStationMiddle(const Station *st) const {
		int x = (st->rect.right + st->rect.left + 1) / 2;
		int y = (st->rect.bottom + st->rect.top + 1) / 2;
		Point ret = this->RemapTile(x, y);
		ret.x -= 3 + this->subscroll;
		if (this->zoom < 0) {
			/* add half a tile if width or height is odd */
			if (((st->rect.bottom - st->rect.top) & 1) == 0) {
				Point offset = RemapCoords(0, -this->zoom / 2, 0);
				ret.x += offset.x;
				ret.y += offset.y;
			}
			if (((st->rect.right - st->rect.left) & 1) == 0) {
				Point offset = RemapCoords(-this->zoom / 2, 0, 0);
				ret.x += offset.x;
				ret.y += offset.y;
			}
		}
		return ret;
	}

	StationID DrawStationDots() const {
		const Station *supply_details = NULL;

		const Station *st;
		FOR_ALL_STATIONS(st) {
			if ((st->owner != _local_company && Company::IsValidID(st->owner)) ||
					st->rect.IsEmpty()) continue;

			Point pt = GetStationMiddle(st);

			if (supply_details == NULL && CheckStationSelected(&pt)) {
				supply_details = st;
			}

			/* Add up cargo supplied for each selected cargo type */
			uint q = 0;
			int colour = 0;
			int numCargos = 0;
			for (int i = 0; i < _smallmap_cargo_count; ++i) {
				const LegendAndColour &tbl = _legend_table[this->map_type][i];
				if (!tbl.show_on_map && supply_details != st) continue;
				uint supply = st->goods[tbl.u.type].supply;
				if (supply > 0) {
					q += supply;
					colour += tbl.colour;
					numCargos++;
				}
			}
			if (numCargos > 1) colour /= numCargos;

			uint r = 1;
			if (q >= 20) r++;
			if (q >= 90) r++;
			if (q >= 160) r++;

			DrawVertex(pt.x, pt.y, r, colour, _colour_gradient[COLOUR_GREY][supply_details == st ? 3 : 1]);
		}
		return (supply_details == NULL) ? INVALID_STATION : supply_details->index;
	}

	class LinkLineDrawer {

	public:

		LinkLineDrawer(const SmallMapWindow *w) : window(w), highlight(false)
		{
			this->pta.x = this->pta.y = this->ptb.x = this->ptb.y = -1;
		}

		LinkDetails DrawLinks()
		{
			this->link_details.Clear();
			std::set<StationID> seen_stations;
			std::set<std::pair<StationID, StationID> > seen_links;

			const Station *sta;
			FOR_ALL_STATIONS(sta) {
				if (sta->owner != _local_company && Company::IsValidID(sta->owner)) continue;
				for (int i = 0; i < _smallmap_cargo_count; ++i) {
					const LegendAndColour &tbl = _legend_table[window->map_type][i];
					if (!tbl.show_on_map) continue;

					CargoID c = tbl.u.type;
					const LinkStatMap &links = sta->goods[c].link_stats;
					for (LinkStatMap::const_iterator i = links.begin(); i != links.end(); ++i) {
						StationID from = sta->index;
						StationID to = i->first;
						if (Station::IsValidID(to) && seen_stations.find(to) == seen_stations.end()) {
							const Station *stb = Station::Get(to);

							if (stb->owner != _local_company && Company::IsValidID(stb->owner)) continue;
							if (sta->rect.IsEmpty() || stb->rect.IsEmpty()) continue;
							if (seen_links.find(std::make_pair(to, from)) != seen_links.end()) continue;

							this->pta = this->window->GetStationMiddle(sta);
							this->ptb = this->window->GetStationMiddle(stb);
							if (!this->IsLinkVisible()) continue;

							this->DrawForwBackLinks(sta->index, stb->index);
							seen_stations.insert(to);
						}
						seen_links.insert(std::make_pair(from, to));
					}
				}
				seen_stations.clear();
			}
			return this->link_details;
		}

	protected:

		Point pta, ptb;
		BaseCargoDetail forward, backward;
		LinkDetails link_details;
		const SmallMapWindow *window;
		bool highlight;

		FORCEINLINE bool IsLinkVisible()
		{
			const NWidgetBase *wi = this->window->GetWidget<NWidgetCore>(SM_WIDGET_MAP);
			return !((this->pta.x < 0 && this->ptb.x < 0) ||
					(this->pta.y < 0 && this->ptb.y < 0) ||
					(this->pta.x > (int)wi->current_x && this->ptb.x > (int)wi->current_x) ||
					(this->pta.y > (int)wi->current_y && this->ptb.y > (int)wi->current_y));
		}

		void DrawLink(StationID sta, StationID stb, bool backward) {
			bool highlight_empty = this->link_details.Empty();
			bool highlight =
					(sta == this->link_details.sta && stb == this->link_details.stb) ||
					(highlight_empty && window->CheckLinkSelected(&this->pta, &this->ptb));
			if (highlight_empty && highlight) {
				this->link_details.sta = sta;
				this->link_details.stb = stb;
			}

			bool backward_empty = this->link_details.b_to_a.empty();
			bool highlight_backward = (sta == this->link_details.stb && stb == this->link_details.sta);

			if (highlight || highlight_backward) {
				this->highlight = true;
			}

			for (int i = 0; i < _smallmap_cargo_count; ++i) {
				const LegendAndColour &cargo_entry = _legend_table[this->window->map_type][i];
				CargoID cargo = cargo_entry.u.type;
				if (cargo_entry.show_on_map || highlight || highlight_backward) {
					GoodsEntry &ge = Station::Get(sta)->goods[cargo];
					FlowStat sum_flows = ge.GetSumFlowVia(stb);
					const LinkStatMap &ls_map = ge.link_stats;
					LinkStatMap::const_iterator i = ls_map.find(stb);
					if (i != ls_map.end()) {
						const LinkStat &link_stat = i->second;
						this->AddLink(link_stat, sum_flows, backward ? this->backward : this->forward);
						if (highlight_empty && highlight) {
							this->link_details.a_to_b.push_back(CargoDetail(&cargo_entry, link_stat, sum_flows));
						} else if (backward_empty && highlight_backward) {
							this->link_details.b_to_a.push_back(CargoDetail(&cargo_entry, link_stat, sum_flows));
						}
					}
				}
			}
		}

		void AddLink(const LinkStat &orig_link, const FlowStat &orig_flow, BaseCargoDetail &cargo)
		{
			uint new_cap = orig_link.Capacity();
			uint new_usg = orig_link.Usage();
			uint new_plan = orig_flow.Planned();

			if (cargo.capacity == 0 ||
					max(cargo.usage, cargo.planned) * 8 / (cargo.capacity + 1) < max(new_usg, new_plan) * 8 / (new_cap + 1)) {
				cargo.capacity = new_cap;
				cargo.usage = new_usg;
				cargo.planned = new_plan;
			}
		}

		void DrawForwBackLinks(StationID sta, StationID stb) {
			this->DrawLink(sta, stb, false);
			this->DrawLink(stb, sta, true);
			this->DrawContent();
			this->highlight = false;
			this->forward.Clear();
			this->backward.Clear();
		}

		void DrawContent() {
			GfxDrawLine(this->pta.x, this->pta.y, this->ptb.x, this->ptb.y, _colour_gradient[COLOUR_GREY][1]);

			int direction_y = (this->pta.x < this->ptb.x ? 1 : -1);
			int direction_x = (this->pta.y > this->ptb.y ? 1 : -1);;

			if (this->forward.capacity > 0) {
				uint usage_or_plan = min(this->forward.capacity * 2, max(this->forward.usage, this->forward.planned));
				int colour = _smallmap_link_colours[usage_or_plan * lengthof(_smallmap_link_colours) / (this->forward.capacity * 2 + 1)];
				GfxDrawLine(this->pta.x + direction_x, this->pta.y, this->ptb.x + direction_x, this->ptb.y, colour);
				GfxDrawLine(this->pta.x, this->pta.y + direction_y, this->ptb.x, this->ptb.y + direction_y, colour);
			}

			if (this->backward.capacity > 0) {
				uint usage_or_plan = min(this->backward.capacity * 2, max(this->backward.usage, this->backward.planned));
				int colour = _smallmap_link_colours[usage_or_plan * lengthof(_smallmap_link_colours) / (this->backward.capacity * 2 + 1)];
				GfxDrawLine(this->pta.x - direction_x, this->pta.y, this->ptb.x - direction_x, this->ptb.y, colour);
				GfxDrawLine(this->pta.x, this->pta.y - direction_y, this->ptb.x, this->ptb.y - direction_y, colour);
			}
		}
	};

	static const uint MORE_SPACE_NEEDED = 0x1000;

	uint DrawLinkDetails(StatVector &details, uint x, uint y, uint right, uint bottom) const {
		uint x_orig = x;
		SetDParam(0, 9999);
		static uint entry_width = LEGEND_BLOB_WIDTH +
				GetStringBoundingBox(STR_ABBREV_PASSENGERS).width +
				GetStringBoundingBox(STR_SMALLMAP_LINK_CAPACITY).width +
				GetStringBoundingBox(STR_SMALLMAP_LINK_USAGE).width +
				GetStringBoundingBox(STR_SMALLMAP_LINK_PLANNED).width;
		uint entries_per_row = (right - x_orig) / entry_width;
		if (details.empty()) {
			DrawString(x, x + entry_width, y, STR_TINY_NOTHING, TC_BLACK);
			return y + FONT_HEIGHT_SMALL;
		}
		for (uint i = 0; i < details.size(); ++i) {
			CargoDetail &detail = details[i];
			if (x + entry_width >= right) {
				x = x_orig;
				y += FONT_HEIGHT_SMALL;
				if (y + 2 * FONT_HEIGHT_SMALL > bottom && details.size() - i > entries_per_row) {
					return y | MORE_SPACE_NEEDED;
				}
			}
			uint x_next = x + entry_width;
			GfxFillRect(x, y + 1, x + LEGEND_BLOB_WIDTH, y + FONT_HEIGHT_SMALL - 1, 0); // outer border of the legend colour
			GfxFillRect(x + 1, y + 2, x + LEGEND_BLOB_WIDTH - 1, y + FONT_HEIGHT_SMALL - 2, detail.legend->colour); // legend colour
			x += LEGEND_BLOB_WIDTH + WD_FRAMERECT_LEFT;

			SetDParam(0, CargoSpec::Get(detail.legend->u.type)->abbrev);
			TextColour tc = detail.legend->show_on_map ? TC_BLACK : TC_GREY;
			x = DrawString(x, x_next - 1, y, STR_SMALLMAP_LINK, tc);
			SetDParam(0, detail.capacity);
			x = DrawString(x, x_next - 1, y, STR_SMALLMAP_LINK_CAPACITY, tc);
			SetDParam(0, detail.usage);
			x = DrawString(x, x_next - 1, y, STR_SMALLMAP_LINK_USAGE, tc);
			SetDParam(0, detail.planned);
			x = DrawString(x, x_next - 1, y, STR_SMALLMAP_LINK_PLANNED, tc);
			x = x_next;
		}
		return y + FONT_HEIGHT_SMALL;
	}

	uint DrawLinkDetailCaption(uint x, uint y, uint right, StationID sta, StationID stb) const {
		SetDParam(0, sta);
		SetDParam(1, stb);
		static uint height = GetStringBoundingBox(STR_SMALLMAP_LINK_CAPTION).height;
		DrawString(x, right - 1, y, STR_SMALLMAP_LINK_CAPTION, TC_BLACK);
		y += height;
		return y;
	}

	void DrawLinkDetails(uint x, uint y, uint right, uint bottom) const {
		y = DrawLinkDetailCaption(x, y, right, this->link_details.sta, this->link_details.stb);
		if (y + 2 * FONT_HEIGHT_SMALL > bottom) {
			DrawString(x, right, y, "...", TC_BLACK);
			return;
		}
		y = DrawLinkDetails(this->link_details.a_to_b, x, y, right, bottom);
		if (y + 3 * FONT_HEIGHT_SMALL > bottom) {
			/* caption takes more space -> 3 * row height */
			DrawString(x, right, y, "...", TC_BLACK);
			return;
		}
		y = DrawLinkDetailCaption(x, y + 2, right, this->link_details.stb, this->link_details.sta);
		if (y + 2 * FONT_HEIGHT_SMALL > bottom) {
			DrawString(x, right, y, "...", TC_BLACK);
			return;
		}
		y = DrawLinkDetails(this->link_details.b_to_a, x, y, right, bottom);
		if (y & MORE_SPACE_NEEDED) {
			/* only draw "..." if more entries would have been drawn */
			DrawString(x, right, y ^ MORE_SPACE_NEEDED, "...", TC_BLACK);
			return;
		}
	}

	void DrawSupplyDetails(uint x, uint y_org, uint bottom) const {
		const Station *st = Station::GetIfValid(this->supply_details);
		if (st == NULL) return;
		SetDParam(0, this->supply_details);
		static uint height = GetStringBoundingBox(STR_SMALLMAP_SUPPLY_CAPTION).height;
		DrawString(x, x + 2 * this->column_width - 1, y_org, STR_SMALLMAP_SUPPLY_CAPTION, TC_BLACK);
		y_org += height;
		uint y = y_org;
		for (int i = 0; i < _smallmap_cargo_count; ++i) {
			if (y + FONT_HEIGHT_SMALL - 1 >= bottom) {
				/* Column break needed, continue at top, SD_LEGEND_COLUMN_WIDTH pixels
				 * (one "row") to the right. */
				x += this->column_width;
				y = y_org;
			}

			const LegendAndColour &tbl = _legend_table[this->map_type][i];

			CargoID c = tbl.u.type;
			uint supply = st->goods[c].supply;
			if (supply > 0) {
				TextColour textcol = TC_BLACK;
				if (tbl.show_on_map) {
					GfxFillRect(x, y + 1, x + LEGEND_BLOB_WIDTH, y + FONT_HEIGHT_SMALL - 1, 0); // outer border of the legend colour
				} else {
					textcol = TC_GREY;
				}
				SetDParam(0, c);
				SetDParam(1, supply);
				DrawString(x + LEGEND_BLOB_WIDTH + WD_FRAMERECT_LEFT, x + this->column_width - 1, y, STR_SMALLMAP_SUPPLY, textcol);
				GfxFillRect(x + 1, y + 2, x + LEGEND_BLOB_WIDTH - 1, y + FONT_HEIGHT_SMALL - 2, tbl.colour); // legend colour
				y += FONT_HEIGHT_SMALL;
			}
		}
	}

	/**
	 * Adds town names to the smallmap.
	 * @param dpi the part of the smallmap to be drawn into
	 */
	void DrawTowns(const DrawPixelInfo *dpi) const
	{
		const Town *t;
		FOR_ALL_TOWNS(t) {
			/* Remap the town coordinate */
			Point pt = this->RemapTile(TileX(t->xy), TileY(t->xy));
			int x = pt.x - this->subscroll - (t->sign.width_small >> 1);
			int y = pt.y;

			/* Check if the town sign is within bounds */
			if (x + t->sign.width_small > dpi->left &&
					x < dpi->left + dpi->width &&
					y + FONT_HEIGHT_SMALL > dpi->top &&
					y < dpi->top + dpi->height) {
				/* And draw it. */
				SetDParam(0, t->index);
				DrawString(x, x + t->sign.width_small, y, STR_SMALLMAP_TOWN);
			}
		}
	}

	/**
	 * Draws vertical part of map indicator
	 * @param x X coord of left/right border of main viewport
	 * @param y Y coord of top border of main viewport
	 * @param y2 Y coord of bottom border of main viewport
	 */
	static inline void DrawVertMapIndicator(int x, int y, int y2)
	{
		GfxFillRect(x, y,      x, y + 3, 69);
		GfxFillRect(x, y2 - 3, x, y2,    69);
	}

	/**
	 * Draws horizontal part of map indicator
	 * @param x X coord of left border of main viewport
	 * @param x2 X coord of right border of main viewport
	 * @param y Y coord of top/bottom border of main viewport
	 */
	static inline void DrawHorizMapIndicator(int x, int x2, int y)
	{
		GfxFillRect(x,      y, x + 3, y, 69);
		GfxFillRect(x2 - 3, y, x2,    y, 69);
	}

	/**
	 * Adds map indicators to the smallmap.
	 */
	void DrawMapIndicators() const
	{
		/* Find main viewport. */
		const ViewPort *vp = FindWindowById(WC_MAIN_WINDOW, 0)->viewport;

		Point tile = InverseRemapCoords(vp->virtual_left, vp->virtual_top);
		Point tl = this->RemapTile(tile.x >> 4, tile.y >> 4);
		tl.x -= this->subscroll;

		tile = InverseRemapCoords(vp->virtual_left + vp->virtual_width, vp->virtual_top + vp->virtual_height);
		Point br = this->RemapTile(tile.x >> 4, tile.y >> 4);
		br.x -= this->subscroll;

		SmallMapWindow::DrawVertMapIndicator(tl.x, tl.y, br.y);
		SmallMapWindow::DrawVertMapIndicator(br.x, tl.y, br.y);

		SmallMapWindow::DrawHorizMapIndicator(tl.x, br.x, tl.y);
		SmallMapWindow::DrawHorizMapIndicator(tl.x, br.x, br.y);
	}

	/**
	 * Draws the small map.
	 *
	 * Basically, the small map is draw column of pixels by column of pixels. The pixels
	 * are drawn directly into the screen buffer. The final map is drawn in multiple passes.
	 * The passes are:
	 * <ol><li>The colours of tiles in the different modes.</li>
	 * <li>Town names (optional)</li></ol>
	 *
	 * @param dpi pointer to pixel to write onto
	 */
	void DrawSmallMap(DrawPixelInfo *dpi) const
	{
		Blitter *blitter = BlitterFactoryBase::GetCurrentBlitter();
		DrawPixelInfo *old_dpi;

		old_dpi = _cur_dpi;
		_cur_dpi = dpi;

		/* Clear it */
		GfxFillRect(dpi->left, dpi->top, dpi->left + dpi->width - 1, dpi->top + dpi->height - 1, 0);

		/* Setup owner table */
		if (this->map_type == SMT_OWNER) {
			const Company *c;

			/* Fill with some special colours */
			_owner_colours[OWNER_TOWN]  = MKCOLOUR(0xB4B4B4B4);
			_owner_colours[OWNER_NONE]  = _heightmap_schemes[_settings_client.gui.smallmap_land_colour].default_colour;
			_owner_colours[OWNER_WATER] = MKCOLOUR(0xCACACACA);
			_owner_colours[OWNER_END]   = MKCOLOUR(0x20202020); // Industry

			/* Now fill with the company colours */
			FOR_ALL_COMPANIES(c) {
				_owner_colours[c->index] = _colour_gradient[c->colour][5] * 0x01010101;
			}
		}

		/* Which tile is displayed at (dpi->left, dpi->top)? */
		int dx;
		Point position = this->PixelToWorld(dpi->left, dpi->top, &dx);
		int pos_x = this->scroll_x + position.x;
		int pos_y = this->scroll_y + position.y;

		void *ptr = blitter->MoveTo(dpi->dst_ptr, -dx - 4, 0);
		int x = - dx - 4;
		int y = 0;
		int increment = this->zoom > 0 ? this->zoom * TILE_SIZE : TILE_SIZE / (-this->zoom); 

		for (;;) {
			/* Distance from left edge */
			if (x >= -3) {
				if (x >= dpi->width) break; // Exit the loop.

				int end_pos = min(dpi->width, x + 4);
				int reps = (dpi->height - y + 1) / 2; // Number of lines.
				if (reps > 0) {
					this->DrawSmallMapColumn(ptr, pos_x, pos_y, dpi->pitch * 2, reps, x, end_pos, blitter);
				}
			}

			if (y == 0) {
				pos_y += increment;
				y++;
				ptr = blitter->MoveTo(ptr, 0, 1);
			} else {
				pos_x -= increment;
				y--;
				ptr = blitter->MoveTo(ptr, 0, -1);
			}
			ptr = blitter->MoveTo(ptr, 2, 0);
			x += 2;
		}

		/* Draw vehicles */
		if (this->map_type == SMT_CONTOUR || this->map_type == SMT_VEHICLES) this->DrawVehicles(dpi, blitter);

		if (this->map_type == SMT_LINKSTATS && _game_mode == GM_NORMAL) {
			LinkLineDrawer lines(this);
			this->link_details = lines.DrawLinks();

			this->supply_details = DrawStationDots();
		}

		/* Draw town names */
		if (this->show_towns) this->DrawTowns(dpi);

		/* Draw map indicators */
		this->DrawMapIndicators();

		_cur_dpi = old_dpi;
	}

	bool CheckStationSelected(Point *pt) const {
		return abs(this->cursor.x - pt->x) < 7 && abs(this->cursor.y - pt->y) < 7;
	}

	bool CheckLinkSelected(Point *pta, Point *ptb) const {
		if (this->cursor.x == -1 && this->cursor.y == -1) return false;
		if (CheckStationSelected(pta) || CheckStationSelected(ptb)) return false;
		if (pta->x > ptb->x) Swap(pta, ptb);
		int minx = min(pta->x, ptb->x);
		int maxx = max(pta->x, ptb->x);
		int miny = min(pta->y, ptb->y);
		int maxy = max(pta->y, ptb->y);
		if (!IsInsideMM(cursor.x, minx - 3, maxx + 3) || !IsInsideMM(cursor.y, miny - 3, maxy + 3)) {
			return false;
		}

		if (pta->x == ptb->x || ptb->y == pta->y) {
			return true;
		} else {
			int incliney = (ptb->y - pta->y);
			int inclinex = (ptb->x - pta->x);
			int diff = (cursor.x - minx) * incliney / inclinex - (cursor.y - miny);
			if (incliney < 0) {
				diff += maxy - miny;
			}
			return abs(diff) < 4;
		}
	}

	/**
	 * recalculate which vehicles are visible and their positions.
	 */
	void RecalcVehiclePositions() {
		this->vehicles_on_map.clear();
		const Vehicle *v;
		const NWidgetCore *wi = this->GetWidget<NWidgetCore>(SM_WIDGET_MAP);
		int scale = this->zoom < 0 ? -this->zoom : 1;

		FOR_ALL_VEHICLES(v) {
			if (v->type == VEH_EFFECT) continue;
			if (v->vehstatus & (VS_HIDDEN | VS_UNCLICKABLE)) continue;

			/* Remap into flat coordinates. We have to do that again in DrawVehicles to account for scrolling. */
			Point pos = RemapTile(v->x_pos / (int)TILE_SIZE, v->y_pos / (int)TILE_SIZE);

			/* Check if rhombus is inside bounds */
			if (IsInsideMM(pos.x, -2 * scale, wi->current_x + 2 * scale) &&
				IsInsideMM(pos.y, -2 * scale, wi->current_y + 2 * scale)) {

				this->vehicles_on_map.push_back(VehicleAndPosition(v));
			}
		}
	}

public:
	SmallMapWindow(const WindowDesc *desc, int window_number) : Window(), supply_details(INVALID_STATION), refresh(FORCE_REFRESH_PERIOD)
	{
		this->cursor.x = -1;
		this->cursor.y = -1;
		this->InitNested(desc, window_number);
		if (_smallmap_cargo_count == 0) {
			this->DisableWidget(SM_WIDGET_LINKSTATS);
			if (this->map_type == SMT_LINKSTATS) {
				this->map_type = SMT_CONTOUR;
			}
		}

		this->LowerWidget(this->map_type + SM_WIDGET_CONTOUR);

		_smallmap_show_heightmap = (this->map_type != SMT_INDUSTRY);
		BuildLandLegend();
		this->SetWidgetLoweredState(SM_WIDGET_SHOW_HEIGHT, _smallmap_show_heightmap);

		this->SetWidgetLoweredState(SM_WIDGET_TOGGLETOWNNAME, this->show_towns);
		this->GetWidget<NWidgetStacked>(SM_WIDGET_SELECTINDUSTRIES)->SetDisplayedPlane(this->map_type != SMT_INDUSTRY && this->map_type != SMT_LINKSTATS);

		this->SetZoomLevel(ZLC_INITIALIZE, NULL);
		this->SmallMapCenterOnCurrentPos();
	}

	/**
	 * Compute maximal required height of the legends.
	 * @return Maximally needed height for displaying the smallmap legends in pixels.
	 */
	inline uint GetMaxLegendHeight() const
	{
		return WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM + this->GetMaxNumberRowsLegend(this->min_number_of_columns) * FONT_HEIGHT_SMALL;
	}

	/**
	 * Compute minimal required width of the legends.
	 * @return Minimally needed width for displaying the smallmap legends in pixels.
	 */
	inline uint GetMinLegendWidth() const
	{
		return WD_FRAMERECT_LEFT + this->min_number_of_columns * this->column_width;
	}

	/**
	 * Return number of columns that can be displayed in \a width pixels.
	 * @return Number of columns to display.
	 */
	inline uint GetNumberColumnsLegend(uint width) const
	{
		return width / this->column_width;
	}

	/**
	 * Compute height given a width.
	 * @return Needed height for displaying the smallmap legends in pixels.
	 */
	uint GetLegendHeight(uint width) const
	{
		uint num_columns = this->GetNumberColumnsLegend(width);
		return WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM + this->GetMaxNumberRowsLegend(num_columns) * FONT_HEIGHT_SMALL;
	}

	virtual void SetStringParameters(int widget) const
	{
		switch (widget) {
			case SM_WIDGET_CAPTION:
				SetDParam(0, STR_SMALLMAP_TYPE_CONTOURS + this->map_type);
				break;
		}
	}

	virtual void OnInit()
	{
		uint min_width = 0;
		this->min_number_of_columns = INDUSTRY_MIN_NUMBER_OF_COLUMNS;
		this->min_number_of_fixed_rows = 0;
		for (uint i = 0; i < lengthof(_legend_table); i++) {
			uint height = 0;
			uint num_columns = 1;
			for (const LegendAndColour *tbl = _legend_table[i]; !tbl->end; ++tbl) {
				StringID str;
				if (i == SMT_INDUSTRY || i == SMT_LINKSTATS) {
					SetDParam(0, tbl->legend);
					SetDParam(1, IndustryPool::MAX_SIZE);
					str = (i == SMT_INDUSTRY) ? STR_SMALLMAP_INDUSTRY : STR_SMALLMAP_LINKSTATS_LEGEND;
				} else {
					if (tbl->col_break) {
						this->min_number_of_fixed_rows = max(this->min_number_of_fixed_rows, height);
						height = 0;
						num_columns++;
					}
					height++;
					str = tbl->legend;
				}
				min_width = max(GetStringBoundingBox(str).width, min_width);
			}
			this->min_number_of_fixed_rows = max(this->min_number_of_fixed_rows, height);
			this->min_number_of_columns = max(this->min_number_of_columns, num_columns);
		}

		/* The width of a column is the minimum width of all texts + the size of the blob + some spacing */
		this->column_width = min_width + LEGEND_BLOB_WIDTH + WD_FRAMERECT_LEFT + WD_FRAMERECT_RIGHT;
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		switch (widget) {
			case SM_WIDGET_MAP: {
				DrawPixelInfo new_dpi;
				if (!FillDrawPixelInfo(&new_dpi, r.left + 1, r.top + 1, r.right - r.left - 1, r.bottom - r.top - 1)) return;
				this->DrawSmallMap(&new_dpi);
				break;
			}

			case SM_WIDGET_LEGEND: {
				DrawLegend(r);
			} break;
		}
	}

	uint GetNumberRowsLegend(uint columns) const {
		uint number_of_rows = this->min_number_of_fixed_rows;
		if (this->map_type == SMT_INDUSTRY) {
			number_of_rows = max(number_of_rows, (_smallmap_industry_count + columns - 1) / columns);
		} else if (this->map_type == SMT_LINKSTATS) {
			number_of_rows = max(number_of_rows, (_smallmap_cargo_count + columns - 2) / (columns - 1));
		}
		return number_of_rows;
	}

	uint GetMaxNumberRowsLegend(uint columns) const {
		uint number_of_rows = this->min_number_of_fixed_rows;
		number_of_rows = max(number_of_rows, CeilDiv(_smallmap_industry_count, columns));
		number_of_rows = max(number_of_rows, CeilDiv(_smallmap_cargo_count, (columns - 1)));
		return number_of_rows;
	}

	void DrawLegend(const Rect &r) const {
		uint y_org = r.top + WD_FRAMERECT_TOP;
		uint x = r.left + WD_FRAMERECT_LEFT;
		if (this->supply_details != INVALID_STATION) {
			this->DrawSupplyDetails(x, y_org, r.bottom - WD_FRAMERECT_BOTTOM);
		} else if (!this->link_details.Empty()) {
			this->DrawLinkDetails(x, y_org, r.right - WD_FRAMERECT_RIGHT, r.bottom - WD_FRAMERECT_BOTTOM);
		} else {
			uint columns = this->GetNumberColumnsLegend(r.right - r.left + 1);

			uint number_of_rows = this->GetNumberRowsLegend(columns);

			bool rtl = _current_text_dir == TD_RTL;
			uint y_org = r.top + WD_FRAMERECT_TOP;
			uint x = rtl ? r.right - this->column_width - WD_FRAMERECT_RIGHT : r.left + WD_FRAMERECT_LEFT;
			uint y = y_org;
			uint i = 0; // Row counter for industry legend.
			uint row_height = FONT_HEIGHT_SMALL;

			uint text_left  = rtl ? 0 : LEGEND_BLOB_WIDTH + WD_FRAMERECT_LEFT;
			uint text_right = this->column_width - 1 - (rtl ? LEGEND_BLOB_WIDTH + WD_FRAMERECT_RIGHT : 0);
			uint blob_left  = rtl ? this->column_width - 1 - LEGEND_BLOB_WIDTH : 0;
			uint blob_right = rtl ? this->column_width - 1 : LEGEND_BLOB_WIDTH;

			StringID string = (this->map_type == SMT_INDUSTRY) ? STR_SMALLMAP_INDUSTRY : STR_SMALLMAP_LINKSTATS_LEGEND;

			for (const LegendAndColour *tbl = _legend_table[this->map_type]; !tbl->end; ++tbl) {
				if (tbl->col_break || ((this->map_type == SMT_INDUSTRY || this->map_type == SMT_LINKSTATS) && i++ >= number_of_rows)) {
					/* Column break needed, continue at top, COLUMN_WIDTH pixels
					 * (one "row") to the right. */
					x += rtl ? -(int)this->column_width : this->column_width;
					y = y_org;
					i = 1;
				}

				switch(this->map_type) {
					case SMT_INDUSTRY:
						/* Industry name must be formatted, since it's not in tiny font in the specs.
						 * So, draw with a parameter and use the STR_SMALLMAP_INDUSTRY string, which is tiny font */
						SetDParam(1, Industry::GetIndustryTypeCount(tbl->u.type));
					case SMT_LINKSTATS:
						SetDParam(0, tbl->legend);
						if (!tbl->show_on_map) {
							/* Simply draw the string, not the black border of the legend colour.
							 * This will enforce the idea of the disabled item */
							DrawString(x + text_left, x + text_right, y, string, TC_GREY);
						} else {
							DrawString(x + text_left, x + text_right, y, string, TC_BLACK);
							GfxFillRect(x + blob_left, y + 1, x + blob_right, y + row_height - 1, 0); // outer border of the legend colour
						}
						break;
					default:
						if (this->map_type == SMT_CONTOUR) SetDParam(0, tbl->u.height * TILE_HEIGHT_STEP);

						/* Anything that is not an industry is using normal process */
						GfxFillRect(x + blob_left, y + 1, x + blob_right, y + row_height - 1, 0);
						DrawString(x + text_left, x + text_right, y, tbl->legend);
				}
				GfxFillRect(x + blob_left + 1, y + 2, x + blob_right - 1, y + row_height - 2, tbl->colour); // Legend colour

				y += row_height;
			}
		}
	}

	/**
	 * Select and toggle a legend item. When CTRL is pressed, disable all other
	 * items in the group defined by begin_legend_item and end_legend_item and
	 * keep the clicked one enabled even if it was already enabled before. If
	 * the other items in the group are all disabled already and CTRL is pressed
	 * enable them instead.
	 * @param click_pos the index of the item being selected
	 * @param legend the legend from which we select
	 * @param end_legend_item index one past the last item in the group to be inverted
	 * @param begin_legend_item index of the first item in the group to be inverted
	 */
	void SelectLegendItem(int click_pos, LegendAndColour *legend, int end_legend_item, int begin_legend_item = 0)
	{
		if (_ctrl_pressed) {
			/* Disable all, except the clicked one */
			bool changes = false;
			for (int i = begin_legend_item; i != end_legend_item; i++) {
				bool new_state = i == click_pos;
				if (legend[i].show_on_map != new_state) {
					changes = true;
					legend[i].show_on_map = new_state;
				}
			}
			if (!changes) {
				/* Nothing changed? Then show all (again). */
				for (int i = begin_legend_item; i != end_legend_item; i++) {
					legend[i].show_on_map = true;
				}
			}
		} else {
			legend[click_pos].show_on_map = !legend[click_pos].show_on_map;
		}
	}

	/*
	 * Select a new map type.
	 * @param map_type New map type.
	 */
	void SwitchMapType(SmallMapType map_type)
	{
		this->RaiseWidget(this->map_type + SM_WIDGET_CONTOUR);
		this->map_type = map_type;
		this->LowerWidget(this->map_type + SM_WIDGET_CONTOUR);

		/* Hide Enable all/Disable all buttons if is not industry type small map */
		this->GetWidget<NWidgetStacked>(SM_WIDGET_SELECTINDUSTRIES)->SetDisplayedPlane(this->map_type != SMT_INDUSTRY && this->map_type != SMT_LINKSTATS);

		this->SetDirty();
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		/* User clicked something, notify the industry chain window to stop sending newly selected industries. */
		InvalidateWindowClassesData(WC_INDUSTRY_CARGOES, NUM_INDUSTRYTYPES);

		switch (widget) {
			case SM_WIDGET_MAP: { // Map window
				/*
				 * XXX: scrolling with the left mouse button is done by subsequently
				 * clicking with the left mouse button; clicking once centers the
				 * large map at the selected point. So by unclicking the left mouse
				 * button here, it gets reclicked during the next inputloop, which
				 * would make it look like the mouse is being dragged, while it is
				 * actually being (virtually) clicked every inputloop.
				 */
				_left_button_clicked = false;

				const NWidgetBase *wid = this->GetWidget<NWidgetBase>(SM_WIDGET_MAP);
				Window *w = FindWindowById(WC_MAIN_WINDOW, 0);
				int sub;
				pt = this->PixelToWorld(pt.x - wid->pos_x, pt.y - wid->pos_y, &sub);
				int offset = this->zoom > 0 ? this->zoom * TILE_SIZE : TILE_SIZE / (-this->zoom);
				pt = RemapCoords(this->scroll_x + pt.x + offset - offset * sub / 4,
						this->scroll_y + pt.y + sub * offset / 4, 0);

				w->viewport->follow_vehicle = INVALID_VEHICLE;
				w->viewport->dest_scrollpos_x = pt.x - (w->viewport->virtual_width  >> 1);
				w->viewport->dest_scrollpos_y = pt.y - (w->viewport->virtual_height >> 1);

				this->SetDirty();
				break;
			}

			case SM_WIDGET_ZOOM_IN:
			case SM_WIDGET_ZOOM_OUT: {
				const NWidgetBase *wid = this->GetWidget<NWidgetBase>(SM_WIDGET_MAP);
				Point pt = {wid->current_x / 2, wid->current_y / 2};
				this->SetZoomLevel((widget == SM_WIDGET_ZOOM_IN) ? ZLC_ZOOM_IN : ZLC_ZOOM_OUT, &pt);
				SndPlayFx(SND_15_BEEP);
				break;
			}

			case SM_WIDGET_CONTOUR:    // Show land contours
			case SM_WIDGET_VEHICLES:   // Show vehicles
			case SM_WIDGET_INDUSTRIES: // Show industries
			case SM_WIDGET_LINKSTATS:   // Show route map
			case SM_WIDGET_ROUTES:     // Show transport routes
			case SM_WIDGET_VEGETATION: // Show vegetation
			case SM_WIDGET_OWNERS:     // Show land owners
				this->SwitchMapType((SmallMapType)(widget - SM_WIDGET_CONTOUR));
				SndPlayFx(SND_15_BEEP);
				break;

			case SM_WIDGET_CENTERMAP: // Center the smallmap again
				this->SmallMapCenterOnCurrentPos();
				this->HandleButtonClick(SM_WIDGET_CENTERMAP);
				SndPlayFx(SND_15_BEEP);
				break;

			case SM_WIDGET_TOGGLETOWNNAME: // Toggle town names
				this->show_towns = !this->show_towns;
				this->SetWidgetLoweredState(SM_WIDGET_TOGGLETOWNNAME, this->show_towns);

				this->SetDirty();
				SndPlayFx(SND_15_BEEP);
				break;

			case SM_WIDGET_LEGEND: // Legend
				/* If industry type small map*/
				if (this->map_type == SMT_INDUSTRY || this->map_type == SMT_LINKSTATS) {
					/* If click on industries label, find right industry type and enable/disable it */
					const NWidgetBase *wi = this->GetWidget<NWidgetBase>(SM_WIDGET_LEGEND); // Label panel
					uint line = (pt.y - wi->pos_y - WD_FRAMERECT_TOP) / FONT_HEIGHT_SMALL;
					uint columns = this->GetNumberColumnsLegend(wi->current_x);
					uint entry_count = 0;
					if (this->map_type == SMT_LINKSTATS) {
						columns--; // one column is reserved for stats legend
						entry_count = _smallmap_cargo_count;
					} else {
						entry_count = _smallmap_industry_count;
					}
					uint number_of_rows = max(CeilDiv(entry_count, columns), this->min_number_of_fixed_rows);
					if (line >= number_of_rows) break;

					bool rtl = _current_text_dir == TD_RTL;
					int x = pt.x - wi->pos_x;
					if (rtl) x = wi->current_x - x;
					uint column = (x - WD_FRAMERECT_LEFT) / this->column_width;

					/* Check if click is on industry label*/
					int click_pos = (column * number_of_rows) + line;
					if (this->map_type == SMT_INDUSTRY) {
						if (click_pos < _smallmap_industry_count) {
							this->SelectLegendItem(click_pos, _legend_from_industries, _smallmap_industry_count);
						}
					} else if (this->map_type == SMT_LINKSTATS) {
						if (click_pos < _smallmap_cargo_count) {
							this->SelectLegendItem(click_pos, _legend_linkstats, _smallmap_cargo_count);
						}
					}
					this->SetDirty();
				}
				break;

			case SM_WIDGET_ENABLE_ALL: { // Enable all items
				LegendAndColour *tbl = (this->map_type == SMT_INDUSTRY) ? _legend_from_industries : _legend_linkstats;
				for (; !tbl->end; ++tbl) {
					tbl->show_on_map = true;
				}
				this->SetDirty();
				break;
			}

			case SM_WIDGET_DISABLE_ALL: { // Disable all items
				LegendAndColour *tbl = (this->map_type == SMT_INDUSTRY) ? _legend_from_industries : _legend_linkstats;
				for (; !tbl->end; ++tbl) {
					tbl->show_on_map = false;
				}
				this->SetDirty();
				break;
			}

			case SM_WIDGET_SHOW_HEIGHT: // Enable/disable showing of heightmap.
				_smallmap_show_heightmap = !_smallmap_show_heightmap;
				this->SetWidgetLoweredState(SM_WIDGET_SHOW_HEIGHT, _smallmap_show_heightmap);
				this->SetDirty();
				break;
		}
	}

	virtual void OnMouseOver(Point pt, int widget) {
		static Point invalid = {-1, -1};
		if (widget == SM_WIDGET_MAP) {
			const NWidgetBase *wid = this->GetWidget<NWidgetBase>(SM_WIDGET_MAP);
			pt.x -= wid->pos_x;
			pt.y -= wid->pos_y;
			if (pt.x != cursor.x || pt.y != cursor.y) {
				this->refresh = 1;
				cursor = pt;
			}
		} else {
			cursor = invalid;
		}
	}


	/**
	 * Notifications for the smallmap window.
	 * - data = 0: Displayed industries at the industry chain window have changed.
	 */
	virtual void OnInvalidateData(int data)
	{
		extern uint64 _displayed_industries;
		if (this->map_type != SMT_INDUSTRY) this->SwitchMapType(SMT_INDUSTRY);

		for (int i = 0; i != _smallmap_industry_count; i++) {
			_legend_from_industries[i].show_on_map = HasBit(_displayed_industries, _legend_from_industries[i].u.type);
		}
		this->SetDirty();
	}

	virtual bool OnRightClick(Point pt, int widget)
	{
		if (widget != SM_WIDGET_MAP || _scrolling_viewport) return false;

		_scrolling_viewport = true;
		return true;
	}

	virtual void OnMouseWheel(int wheel)
	{
		const NWidgetBase *wid = this->GetWidget<NWidgetBase>(SM_WIDGET_MAP);
		int cursor_x = _cursor.pos.x - this->left - wid->pos_x;
		int cursor_y = _cursor.pos.y - this->top  - wid->pos_y;
		if (IsInsideMM(cursor_x, 0, wid->current_x) && IsInsideMM(cursor_y, 0, wid->current_y)) {
			Point pt = {cursor_x, cursor_y};
			this->SetZoomLevel((wheel < 0) ? ZLC_ZOOM_IN : ZLC_ZOOM_OUT, &pt);
		}
	}

	virtual void OnTick()
	{
		/* Update the window every now and then */
		if (--this->refresh != 0) return;

		this->RecalcVehiclePositions();

		this->refresh = FORCE_REFRESH_PERIOD;
		this->SetDirty();
	}

	/**
	 * Set new #scroll_x, #scroll_y, and #subscroll values after limiting them such that the center
	 * of the smallmap always contains a part of the map.
	 * @param sx  Proposed new #scroll_x
	 * @param sy  Proposed new #scroll_y
	 * @param sub Proposed new #subscroll
	 */
	void SetNewScroll(int sx, int sy, int sub)
	{
		const NWidgetBase *wi = this->GetWidget<NWidgetBase>(SM_WIDGET_MAP);
		Point hv = InverseRemapCoords(wi->current_x * TILE_SIZE / 2, wi->current_y * TILE_SIZE / 2);
		if (this->zoom > 0) {
			hv.x *= this->zoom;
			hv.y *= this->zoom;
		} else {
			hv.x /= (-this->zoom);
			hv.y /= (-this->zoom);
		}

		if (sx < -hv.x) {
			sx = -hv.x;
			sub = 0;
		}
		if (sx > (int)(MapMaxX() * TILE_SIZE) - hv.x) {
			sx = MapMaxX() * TILE_SIZE - hv.x;
			sub = 0;
		}
		if (sy < -hv.y) {
			sy = -hv.y;
			sub = 0;
		}
		if (sy > (int)(MapMaxY() * TILE_SIZE) - hv.y) {
			sy = MapMaxY() * TILE_SIZE - hv.y;
			sub = 0;
		}

		this->scroll_x = sx;
		this->scroll_y = sy;
		this->subscroll = sub;
	}

	virtual void OnScroll(Point delta)
	{
		_cursor.fix_at = true;

		/* While tile is at (delta.x, delta.y)? */
		int sub;
		Point pt = this->PixelToWorld(delta.x, delta.y, &sub);
		this->SetNewScroll(this->scroll_x + pt.x, this->scroll_y + pt.y, sub);

		this->SetDirty();
	}

	void SmallMapCenterOnCurrentPos()
	{
		const ViewPort *vp = FindWindowById(WC_MAIN_WINDOW, 0)->viewport;
		Point pt = InverseRemapCoords(vp->virtual_left + vp->virtual_width  / 2, vp->virtual_top  + vp->virtual_height / 2);

		int sub;
		const NWidgetBase *wid = this->GetWidget<NWidgetBase>(SM_WIDGET_MAP);
		Point sxy = this->ComputeScroll(pt.x, pt.y, max(0, (int)wid->current_x / 2 - 2), wid->current_y / 2, &sub);
		this->SetNewScroll(sxy.x, sxy.y, sub);
		this->SetDirty();
	}

	uint ColumnWidth() const {return column_width;}
};

SmallMapWindow::SmallMapType SmallMapWindow::map_type = SMT_CONTOUR;
bool SmallMapWindow::show_towns = true;

/**
 * Custom container class for displaying smallmap with a vertically resizing legend panel.
 * The legend panel has a smallest height that depends on its width. Standard containers cannot handle this case.
 *
 * @note The container assumes it has two childs, the first is the display, the second is the bar with legends and selection image buttons.
 *       Both childs should be both horizontally and vertically resizable and horizontally fillable.
 *       The bar should have a minimal size with a zero-size legends display. Child padding is not supported.
 */
class NWidgetSmallmapDisplay : public NWidgetContainer {
	const SmallMapWindow *smallmap_window; ///< Window manager instance.
public:
	NWidgetSmallmapDisplay() : NWidgetContainer(NWID_VERTICAL)
	{
		this->smallmap_window = NULL;
	}

	virtual void SetupSmallestSize(Window *w, bool init_array)
	{
		NWidgetBase *display = this->head;
		NWidgetBase *bar = display->next;

		display->SetupSmallestSize(w, init_array);
		bar->SetupSmallestSize(w, init_array);

		this->smallmap_window = dynamic_cast<SmallMapWindow *>(w);
		this->smallest_x = max(display->smallest_x, bar->smallest_x + smallmap_window->GetMinLegendWidth());
		this->smallest_y = display->smallest_y + max(bar->smallest_y, smallmap_window->GetMaxLegendHeight());
		this->fill_x = max(display->fill_x, bar->fill_x);
		this->fill_y = (display->fill_y == 0 && bar->fill_y == 0) ? 0 : min(display->fill_y, bar->fill_y);
		this->resize_x = max(display->resize_x, bar->resize_x);
		this->resize_y = min(display->resize_y, bar->resize_y);
	}

	virtual void AssignSizePosition(SizingType sizing, uint x, uint y, uint given_width, uint given_height, bool rtl)
	{
		this->pos_x = x;
		this->pos_y = y;
		this->current_x = given_width;
		this->current_y = given_height;

		NWidgetBase *display = this->head;
		NWidgetBase *bar = display->next;

		if (sizing == ST_SMALLEST) {
			this->smallest_x = given_width;
			this->smallest_y = given_height;
			/* Make display and bar exactly equal to their minimal size. */
			display->AssignSizePosition(ST_SMALLEST, x, y, display->smallest_x, display->smallest_y, rtl);
			bar->AssignSizePosition(ST_SMALLEST, x, y + display->smallest_y, bar->smallest_x, bar->smallest_y, rtl);
		}

		uint bar_height = max(bar->smallest_y, this->smallmap_window->GetLegendHeight(given_width - bar->smallest_x));
		uint display_height = given_height - bar_height;
		display->AssignSizePosition(ST_RESIZE, x, y, given_width, display_height, rtl);
		bar->AssignSizePosition(ST_RESIZE, x, y + display_height, given_width, bar_height, rtl);
	}

	virtual NWidgetCore *GetWidgetFromPos(int x, int y)
	{
		if (!IsInsideBS(x, this->pos_x, this->current_x) || !IsInsideBS(y, this->pos_y, this->current_y)) return NULL;
		for (NWidgetBase *child_wid = this->head; child_wid != NULL; child_wid = child_wid->next) {
			NWidgetCore *widget = child_wid->GetWidgetFromPos(x, y);
			if (widget != NULL) return widget;
		}
		return NULL;
	}

	virtual void Draw(const Window *w)
	{
		for (NWidgetBase *child_wid = this->head; child_wid != NULL; child_wid = child_wid->next) child_wid->Draw(w);
	}
};

/** Widget parts of the smallmap display. */
static const NWidgetPart _nested_smallmap_display[] = {
	NWidget(WWT_PANEL, COLOUR_BROWN, SM_WIDGET_MAP_BORDER),
		NWidget(WWT_INSET, COLOUR_BROWN, SM_WIDGET_MAP), SetMinimalSize(346, 140), SetResize(1, 1), SetPadding(2, 2, 2, 2), EndContainer(),
	EndContainer(),
};

/** Widget parts of the smallmap legend bar + image buttons. */
static const NWidgetPart _nested_smallmap_bar[] = {
	NWidget(WWT_PANEL, COLOUR_BROWN),
		NWidget(NWID_HORIZONTAL),
			NWidget(WWT_EMPTY, INVALID_COLOUR, SM_WIDGET_LEGEND), SetResize(1, 1),
			NWidget(NWID_VERTICAL),
				/* Top button row. */
				NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
					NWidget(WWT_PUSHIMGBTN, COLOUR_BROWN, SM_WIDGET_ZOOM_IN),
							SetDataTip(SPR_IMG_ZOOMIN, STR_TOOLBAR_TOOLTIP_ZOOM_THE_VIEW_IN), SetFill(1, 1),
					NWidget(WWT_PUSHIMGBTN, COLOUR_BROWN, SM_WIDGET_CENTERMAP),
							SetDataTip(SPR_IMG_SMALLMAP, STR_SMALLMAP_CENTER), SetFill(1, 1),
					NWidget(WWT_IMGBTN, COLOUR_BROWN, SM_WIDGET_BLANK),
							SetDataTip(SPR_DOT_SMALL, STR_NULL), SetFill(1, 1),
					NWidget(WWT_IMGBTN, COLOUR_BROWN, SM_WIDGET_CONTOUR),
							SetDataTip(SPR_IMG_SHOW_COUNTOURS, STR_SMALLMAP_TOOLTIP_SHOW_LAND_CONTOURS_ON_MAP), SetFill(1, 1),
					NWidget(WWT_IMGBTN, COLOUR_BROWN, SM_WIDGET_VEHICLES),
							SetDataTip(SPR_IMG_SHOW_VEHICLES, STR_SMALLMAP_TOOLTIP_SHOW_VEHICLES_ON_MAP), SetFill(1, 1),
					NWidget(WWT_IMGBTN, COLOUR_BROWN, SM_WIDGET_INDUSTRIES),
							SetDataTip(SPR_IMG_INDUSTRY, STR_SMALLMAP_TOOLTIP_SHOW_INDUSTRIES_ON_MAP), SetFill(1, 1),
				EndContainer(),
				/* Bottom button row. */
				NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
					NWidget(WWT_PUSHIMGBTN, COLOUR_BROWN, SM_WIDGET_ZOOM_OUT),
							SetDataTip(SPR_IMG_ZOOMOUT, STR_TOOLBAR_TOOLTIP_ZOOM_THE_VIEW_OUT), SetFill(1, 1),
					NWidget(WWT_IMGBTN, COLOUR_BROWN, SM_WIDGET_TOGGLETOWNNAME),
							SetDataTip(SPR_IMG_TOWN, STR_SMALLMAP_TOOLTIP_TOGGLE_TOWN_NAMES_ON_OFF), SetFill(1, 1),
					NWidget(WWT_IMGBTN, COLOUR_BROWN, SM_WIDGET_LINKSTATS),
							SetDataTip(SPR_IMG_GRAPHS, STR_SMALLMAP_TOOLTIP_SHOW_LINK_STATS_ON_MAP), SetFill(1, 1),
					NWidget(WWT_IMGBTN, COLOUR_BROWN, SM_WIDGET_ROUTES),
							SetDataTip(SPR_IMG_SHOW_ROUTES, STR_SMALLMAP_TOOLTIP_SHOW_TRANSPORT_ROUTES_ON), SetFill(1, 1),
					NWidget(WWT_IMGBTN, COLOUR_BROWN, SM_WIDGET_VEGETATION),
							SetDataTip(SPR_IMG_PLANTTREES, STR_SMALLMAP_TOOLTIP_SHOW_VEGETATION_ON_MAP), SetFill(1, 1),
					NWidget(WWT_IMGBTN, COLOUR_BROWN, SM_WIDGET_OWNERS),
							SetDataTip(SPR_IMG_COMPANY_GENERAL, STR_SMALLMAP_TOOLTIP_SHOW_LAND_OWNERS_ON_MAP), SetFill(1, 1),
				EndContainer(),
				NWidget(NWID_SPACER), SetResize(0, 1),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

static NWidgetBase *SmallMapDisplay(int *biggest_index)
{
	NWidgetContainer *map_display = new NWidgetSmallmapDisplay;

	MakeNWidgets(_nested_smallmap_display, lengthof(_nested_smallmap_display), biggest_index, map_display);
	MakeNWidgets(_nested_smallmap_bar, lengthof(_nested_smallmap_bar), biggest_index, map_display);
	return map_display;
}


static const NWidgetPart _nested_smallmap_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_BROWN),
		NWidget(WWT_CAPTION, COLOUR_BROWN, SM_WIDGET_CAPTION), SetDataTip(STR_SMALLMAP_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_BROWN),
		NWidget(WWT_STICKYBOX, COLOUR_BROWN),
	EndContainer(),
	NWidgetFunction(SmallMapDisplay), // Smallmap display and legend bar + image buttons.
	/* Bottom button row and resize box. */
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_BROWN),
			NWidget(NWID_HORIZONTAL),
				NWidget(NWID_SELECTION, INVALID_COLOUR, SM_WIDGET_SELECTINDUSTRIES),
					NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
						NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, SM_WIDGET_ENABLE_ALL), SetDataTip(STR_SMALLMAP_ENABLE_ALL, STR_SMALLMAP_TOOLTIP_ENABLE_ALL),
						NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, SM_WIDGET_DISABLE_ALL), SetDataTip(STR_SMALLMAP_DISABLE_ALL, STR_SMALLMAP_TOOLTIP_DISABLE_ALL),
						NWidget(WWT_TEXTBTN, COLOUR_BROWN, SM_WIDGET_SHOW_HEIGHT), SetDataTip(STR_SMALLMAP_SHOW_HEIGHT, STR_SMALLMAP_TOOLTIP_SHOW_HEIGHT),
					EndContainer(),
					NWidget(NWID_SPACER), SetFill(1, 1),
				EndContainer(),
				NWidget(NWID_SPACER), SetFill(1, 0), SetResize(1, 0),
			EndContainer(),
		EndContainer(),
		NWidget(WWT_RESIZEBOX, COLOUR_BROWN),
	EndContainer(),
};

static const WindowDesc _smallmap_desc(
	WDP_AUTO, 488, 314,
	WC_SMALLMAP, WC_NONE,
	WDF_UNCLICK_BUTTONS,
	_nested_smallmap_widgets, lengthof(_nested_smallmap_widgets)
);

void ShowSmallMap()
{
	AllocateWindowDescFront<SmallMapWindow>(&_smallmap_desc, 0);
}

/**
 * Scrolls the main window to given coordinates.
 * @param x x coordinate
 * @param y y coordinate
 * @param z z coordinate; -1 to scroll to terrain height
 * @param instant scroll instantly (meaningful only when smooth_scrolling is active)
 * @return did the viewport position change?
 */
bool ScrollMainWindowTo(int x, int y, int z, bool instant)
{
	bool res = ScrollWindowTo(x, y, z, FindWindowById(WC_MAIN_WINDOW, 0), instant);

	/* If a user scrolls to a tile (via what way what so ever) and already is on
	 * that tile (e.g.: pressed twice), move the smallmap to that location,
	 * so you directly see where you are on the smallmap. */

	if (res) return res;

	SmallMapWindow *w = dynamic_cast<SmallMapWindow*>(FindWindowById(WC_SMALLMAP, 0));
	if (w != NULL) w->SmallMapCenterOnCurrentPos();

	return res;
}
