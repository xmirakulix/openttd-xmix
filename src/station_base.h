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
#include "newgrf_airport.h"
#include "cargopacket.h"
#include "industry_type.h"
#include "linkgraph/linkgraph_type.h"
#include "newgrf_storage.h"
#include "moving_average.h"
#include <map>
#include <set>

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
	 * Capacity of the link.
	 * This is a moving average. Use MovingAverage::Monthly() to get a meaningful value.
	 */
	uint capacity;

	/**
	 * Capacity of currently loading vehicles.
	 */
	uint frozen;

	/**
	 * Usage of the link.
	 * This is a moving average. Use MovingAverage::Monthly() to get a meaningful value.
	 */
	uint usage;

public:
	/**
	 * Minimum length of moving averages for capacity and usage.
	 */
	static const uint MIN_AVERAGE_LENGTH = 96;

	friend const SaveLoad *GetLinkStatDesc();

	FORCEINLINE LinkStat(uint distance = 1, uint capacity = 0, uint frozen = 0, uint usage = 0) :
		MovingAverage<uint>(distance), capacity(capacity), frozen(frozen), usage(usage) {}

	/**
	 * Reset everything to 0.
	 */
	FORCEINLINE void Clear()
	{
		this->capacity = 0;
		this->usage = 0;
		this->frozen = 0;
	}

	/**
	 * Apply the moving averages to usage and capacity.
	 */
	FORCEINLINE void Decrease()
	{
		this->MovingAverage<uint>::Decrease(this->usage);
		this->capacity = max(this->MovingAverage<uint>::Decrease(this->capacity), this->frozen);
	}

	/**
	 * Get an estimate of the current the capacity by calculating the moving average.
	 * @return Capacity.
	 */
	FORCEINLINE uint Capacity() const
	{
		return this->MovingAverage<uint>::Monthly(this->capacity);
	}

	/**
	 * Get an estimage of the current usage by calculating the moving average.
	 * @return Usage.
	 */
	FORCEINLINE uint Usage() const
	{
		return this->MovingAverage<uint>::Monthly(this->usage);
	}

	/**
	 * Get the amount of frozen capacity.
	 * @return Frozen capacity.
	 */
	FORCEINLINE uint Frozen() const
	{
		return this->frozen;
	}

	/**
	 * Add some capacity and usage.
	 * @param capacity Additional capacity.
	 * @param usage Additional usage.
	 */
	FORCEINLINE void Increase(uint capacity, uint usage)
	{
		this->capacity += capacity;
		this->usage += usage;
	}

	/**
	 * Freeze some of the capacity and prevent it from being decreased by the
	 * moving average.
	 * @param capacity Amount of capacity to be frozen.
	 */
	FORCEINLINE void Freeze(uint capacity)
	{
		this->frozen += capacity;
		this->capacity = max(this->frozen, this->capacity);
	}

	/**
	 * Thaw some of the frozen capacity and make it available for Decrease().
	 * @oaram capacity Capacity to be thawed.
	 */
	FORCEINLINE void Unfreeze(uint capacity)
	{
		this->frozen -= capacity;
	}

	/**
	 * Thaw all frozen capacity.
	 */
	FORCEINLINE void Unfreeze()
	{
		this->frozen = 0;
	}

	/**
	 * Check if the capacity is 0. This is necessary as Capacity() might return
	 * 0 even if there is a miniscule amount of capacity left.
	 * @return If capacity is 0.
	 */
	FORCEINLINE bool HasCapacity() const
	{
		return this->capacity == 0;
	}
};

/**
 * Flow statistics telling how much flow should be and was sent along a link.
 */
class FlowStat : private MovingAverage<uint> {
private:
	uint planned;  ///< Cargo planned to be sent along a link each "month" (30 units of time, determined by moving average).
	uint sent;     ///< Moving average of cargo being sent.
	StationID via; ///< Other end of the link. Can be this station, then it means "deliver here".

public:
	friend const SaveLoad *GetFlowStatDesc();

	/**
	 * Create a flow stat.
	 * @param distance Distance to be used as length of moving average.
	 * @param st Remote station.
	 * @param planned Cargo planned to be sent along this link.
	 * @param sent Cargo already sent along this link.
	 */
	FORCEINLINE FlowStat(uint distance = 1, StationID st = INVALID_STATION, uint planned = 0, uint sent = 0) :
		MovingAverage<uint>(distance), planned(planned), sent(sent), via(st) {}

	/**
	 * Clone an existing flow stat, changing the plan.
	 * @param prev Flow stat to be cloned.
	 * @param new_plan New value for planned.
	 */
	FORCEINLINE FlowStat(const FlowStat &prev, uint new_plan) :
		MovingAverage<uint>(prev.length), planned(new_plan), sent(prev.sent), via(prev.via) {}

	/**
	 * Prevents one copy operation when moving a flowstat from one set to another and decreasing it at the same time.
	 */
	FORCEINLINE FlowStat GetDecreasedCopy() const
	{
		FlowStat ret(this->length, this->via, this->planned, this->sent);
		this->MovingAverage<uint>::Decrease(ret.sent);
		return ret;
	}

	/**
	 * Increase the sent value.
	 * @param sent Amount to be added to sent.
	 */
	FORCEINLINE void Increase(uint sent)
	{
		this->sent += sent;
	}

	/**
	 * Get an estimate of cargo sent along this link during the last 30 time units.
	 * @return Cargo sent along this link.
	 */
	FORCEINLINE uint Sent() const
	{
		return this->MovingAverage<uint>::Monthly(sent);
	}

	/**
	 * Get the amount of cargo planned to be sent along this link in 30 time units.
	 * @return Cargo planned to be sent.
	 */
	FORCEINLINE uint Planned() const
	{
		return this->planned;
	}

	/**
	 * Get the station this link is connected to.
	 * @return Remote station.
	 */
	FORCEINLINE StationID Via() const
	{
		return this->via;
	}

	/**
	 * Comparator for two flow stats for ordering them in a way that makes
	 * the next flow stat to sent cargo for show up as first element.
	 */
	struct Comparator {
		/**
		 * Comparator function. Decides by planned - sent or via, if those
		 * are equal.
		 * @param x First flow stat.
		 * @param y Second flow stat.
		 * @return True if x.planned - x.sent is greater than y.planned - y.sent.
		 */
		bool operator()(const FlowStat &x, const FlowStat &y) const
		{
			int diff_x = (int)x.Planned() - (int)x.Sent();
			int diff_y = (int)y.Planned() - (int)y.Sent();
			if (diff_x != diff_y) {
				return diff_x > diff_y;
			} else {
				return x.Via() > y.Via();
			}
		}
	};

	/**
	 * Add up two flow stats' planned and sent figures and assign via from the other one to this one.
	 * @param other Flow stat to add to this one.
	 * @return This flow stat.
	 */
	FORCEINLINE FlowStat &operator+=(const FlowStat &other)
	{
		assert(this->via == INVALID_STATION || other.via == INVALID_STATION || this->via == other.via);
		if (other.via != INVALID_STATION) this->via = other.via;
		this->planned += other.planned;
		uint sent = this->sent + other.sent;
		if (sent > 0) {
			this->length = (this->length * this->sent + other.length * other.sent) / sent;
			assert(this->length > 0);
		}
		this->sent = sent;
		return *this;
	}

	/**
	 * Clear this flow stat.
	 */
	FORCEINLINE void Clear()
	{
		this->planned = 0;
		this->sent = 0;
		this->via = INVALID_STATION;
	}
};

typedef std::set<FlowStat, FlowStat::Comparator> FlowStatSet; ///< Percentage of flow to be sent via specified station (or consumed locally).

typedef std::map<StationID, LinkStat> LinkStatMap;
typedef std::map<StationID, FlowStatSet> FlowStatMap; ///< Flow descriptions by origin stations.

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
		last_component(INVALID_LINKGRAPH_COMPONENT),
		max_waiting_cargo(0)
	{}

	byte acceptance_pickup;
	byte days_since_pickup;
	byte rating;
	byte last_speed;
	byte last_age;
	byte amount_fract;                   ///< Fractional part of the amount in the cargo list.
	StationCargoList cargo;              ///< The cargo packets of cargo waiting in this station.
	uint supply;                         ///< Cargo supplied last month.
	uint supply_new;                     ///< Cargo supplied so far this month.
	FlowStatMap flows;                   ///< Planned flows through this station.
	LinkStatMap link_stats;              ///< Capacities and usage statistics for outgoing links.
	LinkGraphComponentID last_component; ///< Component this station was last part of in this cargo's link graph.
	uint max_waiting_cargo;              ///< Max cargo from this station waiting at any station.
	FlowStat GetSumFlowVia(StationID via) const;

	void UpdateFlowStats(StationID source, uint count, StationID next);
	void UpdateFlowStats(FlowStatSet &flow_stats, uint count, StationID next);
	void UpdateFlowStats(FlowStatSet &flow_stats, FlowStatSet::iterator flow_it, uint count);

	StationID UpdateFlowStatsTransfer(StationID source, uint count, StationID curr);
};

/** All airport-related information. Only valid if tile != INVALID_TILE. */
struct Airport : public TileArea {
	typedef PersistentStorageArray<int32, 16> PersistentStorage;

	Airport() : TileArea(INVALID_TILE, 0, 0) {}

	uint64 flags;       ///< stores which blocks on the airport are taken. was 16 bit earlier on, then 32
	byte type;          ///< Type of this airport, @see AirportTypes.
	byte layout;        ///< Airport layout number.
	Direction rotation; ///< How this airport is rotated.
	PersistentStorage psa; ///< Persistent storage for NewGRF airports

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
	 * Add the tileoffset to the base tile of this airport but rotate it first.
	 * The base tile is the northernmost tile of this airport. This function
	 * helps to make sure that getting the tile of a hangar works even for
	 * rotated airport layouts without requiring a rotated array of hangar tiles.
	 * @param tidc The tilediff to add to the airport tile.
	 * @return The tile of this airport plus the rotated offset.
	 */
	FORCEINLINE TileIndex GetRotatedTileFromOffset(TileIndexDiffC tidc) const
	{
		const AirportSpec *as = this->GetSpec();
		switch (this->rotation) {
			case DIR_N: return this->tile + ToTileIndexDiff(tidc);

			case DIR_E: return this->tile + TileDiffXY(tidc.y, as->size_x - 1 - tidc.x);

			case DIR_S: return this->tile + TileDiffXY(as->size_x - 1 - tidc.x, as->size_y - 1 - tidc.y);

			case DIR_W: return this->tile + TileDiffXY(as->size_y - 1 - tidc.y, tidc.x);

			default: NOT_REACHED();
		}
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
				return this->GetRotatedTileFromOffset(as->depot_table[i].ti);
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
			if (this->GetRotatedTileFromOffset(as->depot_table[i].ti) == tile) {
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
