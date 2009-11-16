/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file station_func.h Functions related to stations. */

#ifndef STATION_FUNC_H
#define STATION_FUNC_H

#include "station_type.h"
#include "sprite.h"
#include "rail_type.h"
#include "road_type.h"
#include "tile_type.h"
#include "cargo_type.h"
#include "vehicle_type.h"

void ModifyStationRatingAround(TileIndex tile, Owner owner, int amount, uint radius);

void FindStationsAroundTiles(TileIndex tile, int w_prod, int h_prod, StationList *stations);

void ShowStationViewWindow(StationID station);
void UpdateAllStationVirtCoords();

CargoArray GetProductionAroundTiles(TileIndex tile, int w, int h, int rad);
CargoArray GetAcceptanceAroundTiles(TileIndex tile, int w, int h, int rad, uint32 *always_accepted = NULL);

void UpdateStationAcceptance(Station *st, bool show_msg);

const DrawTileSprites *GetStationTileLayout(StationType st, byte gfx);
void StationPickerDrawSprite(int x, int y, StationType st, RailType railtype, RoadType roadtype, int image);

bool HasStationInUse(StationID station, CompanyID company);

RoadStop *GetRoadStopByTile(TileIndex tile, RoadStopType type);
uint GetNumRoadStops(const Station *st, RoadStopType type);

void ClearSlot(struct RoadVehicle *v);

void DeleteOilRig(TileIndex t);

/* Check if a rail station tile is traversable. */
bool IsStationTileBlocked(TileIndex tile);

/* Check if a rail station tile is electrifiable. */
bool IsStationTileElectrifiable(TileIndex tile);

void UpdateAirportsNoise();

void DecreaseFrozen(Station *st, const Vehicle *v, StationID next_station_id);

void IncreaseFrozen(Station *st, const Vehicle *v, StationID next_station_id);

void RecalcFrozen(Station * st);

void RecalcFrozenIfLoading(const Vehicle * v);

void IncreaseStats(Station *st, const Vehicle *v, StationID next_station_id);

#endif /* STATION_FUNC_H */
