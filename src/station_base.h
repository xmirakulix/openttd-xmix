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
#include <set>

typedef Pool<BaseStation, StationID, 32, 64000> StationPool;
extern StationPool _station_pool;

static const byte INITIAL_STATION_RATING = 175;

class LinkStat : private MovingAverage<uint> {
private:
	/**
	 * capacity of the link.
	 * This is a moving average use MovingAverage::Monthly() to get a meaningful value 
	 */
	uint capacity;
	
	/**
	 * capacity of currently loading vehicles
	 */
	uint frozen;
	
	/**
	 * usage of the link.
	 * This is a moving average use MovingAverage::Monthly() to get a meaningful value 
	 */
	uint usage;

public:
	static const uint MIN_AVERAGE_LENGTH = 64;

	friend const SaveLoad *GetLinkStatDesc();

	FORCEINLINE LinkStat(uint distance = 1, uint capacity = 0, uint frozen = 0, uint usage = 0) :
		MovingAverage<uint>(distance), capacity(capacity), frozen(frozen), usage(usage) {}

	FORCEINLINE void Clear()
	{
		this->capacity = 0;
		this->usage = 0;
		this->frozen = 0;
	}

	FORCEINLINE void Decrease()
	{
		this->MovingAverage<uint>::Decrease(this->usage);
		this->capacity = max(this->MovingAverage<uint>::Decrease(this->capacity), this->frozen);
	}

	FORCEINLINE uint Capacity() const
	{
		return this->MovingAverage<uint>::Monthly(this->capacity);
	}

	FORCEINLINE uint Usage() const
	{
		return this->MovingAverage<uint>::Monthly(this->usage);
	}

	FORCEINLINE uint Frozen() const
	{
		return this->frozen;
	}

	FORCEINLINE void Increase(uint capacity, uint usage)
	{
		this->capacity += capacity;
		this->usage += usage;
	}

	FORCEINLINE void Freeze(uint capacity)
	{
		this->frozen += capacity;
		this->capacity = max(this->frozen, this->capacity);
	}

	FORCEINLINE void Unfreeze(uint capacity)
	{
		this->frozen -= capacity;
	}

	FORCEINLINE void Unfreeze()
	{
		this->frozen = 0;
	}

	FORCEINLINE bool IsNull() const
	{
		return this->capacity == 0;
	}
};

class FlowStat : private MovingAverage<uint> {
private:
	uint planned;
	uint sent;
	StationID via;

public:
	friend const SaveLoad *GetFlowStatDesc();

	FORCEINLINE FlowStat(uint distance = 1, StationID st = INVALID_STATION, uint p = 0, uint s = 0) :
		MovingAverage<uint>(distance), planned(p), sent(s), via(st) {}

	FORCEINLINE FlowStat(const FlowStat &prev, uint new_plan) :
		MovingAverage<uint>(prev.length), planned(new_plan), sent(prev.sent), via(prev.via) {}

	/**
	 * prevents one copy operation when moving a flowstat from one set to another and decreasing it at the same time
	 */
	FORCEINLINE FlowStat GetDecreasedCopy() const
	{
		FlowStat ret(this->length, this->via, this->planned, this->sent);
		this->MovingAverage<uint>::Decrease(ret.sent);
		return ret;
	}

	FORCEINLINE void Increase(int sent)
	{
		this->sent += sent;
	}

	FORCEINLINE uint Sent() const
	{
		return this->MovingAverage<uint>::Monthly(sent);
	}

	FORCEINLINE uint Planned() const
	{
		return this->planned;
	}

	FORCEINLINE StationID Via() const
	{
		return this->via;
	}

	struct comp {
		bool operator()(const FlowStat & x, const FlowStat & y) const {
			int diff_x = (int)x.Planned() - (int)x.Sent();
			int diff_y = (int)y.Planned() - (int)y.Sent();
			if (diff_x != diff_y) {
				return diff_x > diff_y;
			} else {
				return x.Via() > y.Via();
			}
		}
	};

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

	FORCEINLINE void Clear()
	{
		this->planned = 0;
		this->sent = 0;
		this->via = INVALID_STATION;
	}
};

typedef std::set<FlowStat, FlowStat::comp> FlowStatSet; ///< percentage of flow to be sent via specified station (or consumed locally)

typedef std::map<StationID, LinkStat> LinkStatMap;
typedef std::map<StationID, FlowStatSet> FlowStatMap; ///< flow descriptions by origin stations

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
	StationCargoList cargo;              ///< The cargo packets of cargo waiting in this station
	uint supply;                         ///< Cargo supplied last month
	uint supply_new;                     ///< Cargo supplied so far this month
	FlowStatMap flows;                   ///< The planned flows through this station
	LinkStatMap link_stats;              ///< capacities and usage statistics for outgoing links
	LinkGraphComponentID last_component; ///< the component this station was last part of in this cargo's link graph
	uint max_waiting_cargo;              ///< max cargo from this station waiting at any station
	
	FlowStat GetSumFlowVia(StationID via) const;

	/**
	 * update the flow stats for count cargo from source sent to next
	 */
	void UpdateFlowStats(StationID source, uint count, StationID next);
	void UpdateFlowStats(FlowStatSet &flow_stats, uint count, StationID next);
	void UpdateFlowStats(FlowStatSet &flow_stats, FlowStatSet::iterator flow_it, uint count);

	/**
	 * update the flow stats for count cargo that cannot be delivered here
	 * return the direction where it is sent
	 */
	StationID UpdateFlowStatsTransfer(StationID source, uint count, StationID curr);
};

/** All airport-related information. Only valid if tile != INVALID_TILE. */
struct Airport : public TileArea {
	Airport() : TileArea(INVALID_TILE, 0, 0) {}

	uint64 flags;       ///< stores which blocks on the airport are taken. was 16 bit earlier on, then 32
	byte type;

	const AirportSpec *GetSpec() const
	{
		if (this->tile == INVALID_TILE) return &AirportSpec::dummy;
		return AirportSpec::Get(this->type);
	}

	const AirportFTAClass *GetFTA() const
	{
		return this->GetSpec()->fsm;
	}

	FORCEINLINE bool HasHangar() const
	{
		return this->GetSpec()->nof_depots > 0;
	}

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
