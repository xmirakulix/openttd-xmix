/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file station_base.h Base classes/functions for stations. */

#ifndef STATION_BASE_H
#define STATION_BASE_H

#include "base_station_base.h"
#include "airport.h"
#include "newgrf_airport.h"
#include "cargopacket.h"
#include "industry_type.h"
#include "linkgraph/linkgraph_type.h"
#include "moving_average.h"
#include <map>

typedef Pool<BaseStation, StationID, 32, 64000> StationPool;
extern StationPool _station_pool;

static const byte INITIAL_STATION_RATING = 175;

/**
 * Link statistics. They include figures for capacity and usage of a link. Both
 * are moving averages which are increased for every vehicle arriving at the
 * destination station and decreased in regular intervals. Additionally while a
 * vehicle is loading at the source station part of the capacity is frozen and
 * prevented from being decreased. This is done so that the link won't break
 * down all the time when the typical "full load" order is used.
 */
class LinkStat : private MovingAverage<uint> {
private:
	/**
	 * capacity of the link.
	 * This is a moving average. Use MovingAverage::Monthly() to get a meaningful value
	 */
	uint capacity;
	
	/**
	 * capacity of currently loading vehicles
	 */
	uint frozen;
	
	/**
	 * usage of the link.
	 * This is a moving average. Use MovingAverage::Monthly() to get a meaningful value
	 */
	uint usage;

public:
	/**
	 * minimum length of moving averages for capacity and usage
	 */
	static const uint MIN_AVERAGE_LENGTH = 96;

	friend const SaveLoad *GetLinkStatDesc();

	FORCEINLINE LinkStat(uint distance = 1, uint capacity = 0, uint frozen = 0, uint usage = 0) :
		MovingAverage<uint>(distance), capacity(capacity), frozen(frozen), usage(usage) {}

	/**
	 * reset everything to 0
	 */
	FORCEINLINE void Clear()
	{
		this->capacity = 0;
		this->usage = 0;
		this->frozen = 0;
	}

	/**
	 * apply the moving averages to usage and capacity
	 */
	FORCEINLINE void Decrease()
	{
		this->MovingAverage<uint>::Decrease(this->usage);
		this->capacity = max(this->MovingAverage<uint>::Decrease(this->capacity), this->frozen);
	}

	/**
	 * get an estimate of the current the capacity
	 * @return the capacity
	 */
	FORCEINLINE uint Capacity() const
	{
		return this->MovingAverage<uint>::Monthly(this->capacity);
	}

	/**
	 * get an estimage of the current usage
	 * @return the usage
	 */
	FORCEINLINE uint Usage() const
	{
		return this->MovingAverage<uint>::Monthly(this->usage);
	}

	/**
	 * get the amount of frozen capacity
	 */
	FORCEINLINE uint Frozen() const
	{
		return this->frozen;
	}

	/**
	 * add some capacity and usage
	 * @param capacity the additional capacity
	 * @param usage the additional usage
	 */
	FORCEINLINE void Increase(uint capacity, uint usage)
	{
		this->capacity += capacity;
		this->usage += usage;
	}

	/**
	 * freeze some of the capacity and prevent it from being decreased by the
	 * moving average
	 * @param capacity the amount of capacity to be frozen
	 */
	FORCEINLINE void Freeze(uint capacity)
	{
		this->frozen += capacity;
		this->capacity = max(this->frozen, this->capacity);
	}

	/**
	 * thaw some of the frozen capacity and make it available for Decrease()
	 * @oaram capacity the capacity to be thawed
	 */
	FORCEINLINE void Unfreeze(uint capacity)
	{
		this->frozen -= capacity;
	}

	/**
	 * thaw all frozen capacity
	 */
	FORCEINLINE void Unfreeze()
	{
		this->frozen = 0;
	}

	/**
	 * check if the capacity is 0. This is necessary as Capacity() might return
	 * 0 even if there is a miniscule amount of capacity left
	 * @return if capacity is 0
	 */
	FORCEINLINE bool IsNull() const
	{
		return this->capacity == 0;
	}
};

typedef std::map<StationID, LinkStat> LinkStatMap;

uint GetMovingAverageLength(const Station *from, const Station *to);

struct GoodsEntry {
	enum AcceptancePickup {
		ACCEPTANCE,
		PICKUP
	};

	GoodsEntry() :
		acceptance_pickup(0),
		days_since_pickup(255),
		rating(INITIAL_STATION_RATING),
		last_speed(0),
		last_age(255),
		supply(0),
		supply_new(0),
		last_component(INVALID_LINKGRAPH_COMPONENT)
	{}

	byte acceptance_pickup;
	byte days_since_pickup;
	byte rating;
	byte last_speed;
	byte last_age;
	StationCargoList cargo;              ///< The cargo packets of cargo waiting in this station
	uint supply;                         ///< Cargo supplied last month
	uint supply_new;                     ///< Cargo supplied so far this month
	LinkStatMap link_stats;              ///< capacities and usage statistics for outgoing links
	LinkGraphComponentID last_component; ///< the component this station was last part of in this cargo's link graph
};

/** All airport-related information. Only valid if tile != INVALID_TILE. */
struct Airport : public TileArea {
	Airport() : TileArea(INVALID_TILE, 0, 0) {}

	uint64 flags; ///< stores which blocks on the airport are taken. was 16 bit earlier on, then 32
	byte type;    ///< Type of this airport, @see AirportTypes.

	/**
	 * Get the AirportSpec that from the airport type of this airport. If there
	 * is no airport (\c tile == INVALID_TILE) then return the dummy AirportSpec.
	 * @return The AirportSpec for this airport.
	 */
	const AirportSpec *GetSpec() const
	{
		if (this->tile == INVALID_TILE) return &AirportSpec::dummy;
		return AirportSpec::Get(this->type);
	}

	/**
	 * Get the finite-state machine for this airport or the finite-state machine
	 * for the dummy airport in case this isn't an airport.
	 * @pre this->type < NEW_AIRPORT_OFFSET.
	 * @return The state machine for this airport.
	 */
	const AirportFTAClass *GetFTA() const
	{
		return this->GetSpec()->fsm;
	}

	/** Check if this airport has at least one hangar. */
	FORCEINLINE bool HasHangar() const
	{
		return this->GetSpec()->nof_depots > 0;
	}

	/**
	 * Get the first tile of the given hangar.
	 * @param hangar_num The hangar to get the location of.
	 * @pre hangar_num < GetNumHangars().
	 * @return A tile with the given hangar.
	 */
	FORCEINLINE TileIndex GetHangarTile(uint hangar_num) const
	{
		const AirportSpec *as = this->GetSpec();
		for (uint i = 0; i < as->nof_depots; i++) {
			if (as->depot_table[i].hangar_num == hangar_num) {
				return this->tile + ToTileIndexDiff(as->depot_table[i].ti);
			}
		}
		NOT_REACHED();
	}

	/**
	 * Get the hangar number of the hangar on a specific tile.
	 * @param tile The tile to query.
	 * @pre IsHangarTile(tile).
	 * @return The hangar number of the hangar at the given tile.
	 */
	FORCEINLINE uint GetHangarNum(TileIndex tile) const
	{
		const AirportSpec *as = this->GetSpec();
		for (uint i = 0; i < as->nof_depots; i++) {
			if (this->tile + ToTileIndexDiff(as->depot_table[i].ti) == tile) {
				return as->depot_table[i].hangar_num;
			}
		}
		NOT_REACHED();
	}

	/** Get the number of hangars on this airport. */
	FORCEINLINE uint GetNumHangars() const
	{
		uint num = 0;
		uint counted = 0;
		const AirportSpec *as = this->GetSpec();
		for (uint i = 0; i < as->nof_depots; i++) {
			if (!HasBit(counted, as->depot_table[i].hangar_num)) {
				num++;
				SetBit(counted, as->depot_table[i].hangar_num);
			}
		}
		return num;
	}
};

typedef SmallVector<Industry *, 2> IndustryVector;

/** Station data structure */
struct Station : SpecializedStation<Station, false> {
public:
	RoadStop *GetPrimaryRoadStop(RoadStopType type) const
	{
		return type == ROADSTOP_BUS ? bus_stops : truck_stops;
	}

	RoadStop *GetPrimaryRoadStop(const struct RoadVehicle *v) const;

	RoadStop *bus_stops;    ///< All the road stops
	TileArea bus_station;   ///< Tile area the bus 'station' part covers
	RoadStop *truck_stops;  ///< All the truck stops
	TileArea truck_station; ///< Tile area the truck 'station' part covers

	Airport airport;        ///< Tile area the airport covers
	TileIndex dock_tile;    ///< The location of the dock

	IndustryType indtype;   ///< Industry type to get the name from

	StationHadVehicleOfTypeByte had_vehicle_of_type;

	byte time_since_load;
	byte time_since_unload;

	byte last_vehicle_type;
	std::list<Vehicle *> loading_vehicles;
	GoodsEntry goods[NUM_CARGO];  ///< Goods at this station
	uint32 always_accepted;       ///< Bitmask of always accepted cargo types (by houses, HQs, industry tiles when industry doesn't accept cargo)

	IndustryVector industries_near; ///< Cached list of industries near the station that can accept cargo, @see DeliverGoodsToIndustry()

	Station(TileIndex tile = INVALID_TILE);
	~Station();

	void AddFacility(StationFacility new_facility_bit, TileIndex facil_xy);

	/**
	 * Marks the tiles of the station as dirty.
	 *
	 * @ingroup dirty
	 */
	void MarkTilesDirty(bool cargo_change) const;

	void UpdateVirtCoord();

	/* virtual */ uint GetPlatformLength(TileIndex tile, DiagDirection dir) const;
	/* virtual */ uint GetPlatformLength(TileIndex tile) const;
	void RecomputeIndustriesNear();
	static void RecomputeIndustriesNearForAll();

	uint GetCatchmentRadius() const;
	Rect GetCatchmentRect() const;

	/* virtual */ FORCEINLINE bool TileBelongsToRailStation(TileIndex tile) const
	{
		return IsRailStationTile(tile) && GetStationIndex(tile) == this->index;
	}

	FORCEINLINE bool TileBelongsToAirport(TileIndex tile) const
	{
		return IsAirportTile(tile) && GetStationIndex(tile) == this->index;
	}

	/* virtual */ uint32 GetNewGRFVariable(const ResolverObject *object, byte variable, byte parameter, bool *available) const;

	/* virtual */ void GetTileArea(TileArea *ta, StationType type) const;

	void RunAverages();
};

#define FOR_ALL_STATIONS(var) FOR_ALL_BASE_STATIONS_OF_TYPE(Station, var)

#endif /* STATION_BASE_H */
