/* $Id$ */

/** @file cargopacket.h Base class for cargo packets. */

#ifndef CARGOPACKET_H
#define CARGOPACKET_H

#include "core/pool_type.hpp"
#include "economy_type.h"
#include "tile_type.h"
#include "station_type.h"
#include "order_type.h"
#include "cargo_type.h"
#include "vehicle_type.h"
#include <map>
#include <list>

typedef uint32 CargoPacketID;
struct CargoPacket;
struct GoodsEntry;
class Payment;

/** We want to use a pool */
typedef Pool<CargoPacket, CargoPacketID, 1024, 1048576> CargoPacketPool;
extern CargoPacketPool _cargopacket_pool;

/**
 * Container for cargo from the same location and time
 */
struct CargoPacket : CargoPacketPool::PoolItem<&_cargopacket_pool> {
	Money feeder_share;     ///< Value of feeder pickup to be paid for on delivery of cargo
	TileIndex source_xy;    ///< The origin of the cargo (first station in feeder chain)
	TileIndex loaded_at_xy; ///< Location where this cargo has been loaded into the vehicle
	StationID source;       ///< The station where the cargo came from first
	StationID next;         ///< The next hop where this cargo is trying to go

	uint16 count;           ///< The amount of cargo in this packet
	byte days_in_transit;   ///< Amount of days this packet has been in transit

	SourceTypeByte source_type; ///< Type of #source_id
	SourceID source_id;         ///< Index of source, INVALID_SOURCE if unknown/invalid

	/**
	 * Creates a new cargo packet
	 * @param source the source of the packet
	 * @param count  the number of cargo entities to put in this packet
	 * @pre count != 0 || source == INVALID_STATION
	 */
	CargoPacket(StationID source = INVALID_STATION, StationID next = INVALID_STATION, uint16 count = 0, SourceType source_type = ST_INDUSTRY, SourceID source_id = INVALID_SOURCE);

	/** Destroy the packet */
	~CargoPacket() { }

	/**
	 * Checks whether the cargo packet is from (exactly) the same source
	 * in time and location.
	 * @param cp the cargo packet to compare to
	 * @return true if and only if days_in_transit and source_xy are equal
	 */
	FORCEINLINE bool SameSource(const CargoPacket *cp) const
	{
		return this->source_xy == cp->source_xy && this->days_in_transit == cp->days_in_transit && this->next == cp->next &&
				this->source_type == cp->source_type && this->source_id == cp->source_id;
	}
	
	CargoPacket *Split(uint new_size);
	void Merge(CargoPacket *other);

	static void InvalidateAllFrom(SourceType src_type, SourceID src);
};

/**
 * Iterate over all _valid_ cargo packets from the given start
 * @param var   the variable used as "iterator"
 * @param start the cargo packet ID of the first packet to iterate over
 */
#define FOR_ALL_CARGOPACKETS_FROM(var, start) FOR_ALL_ITEMS_FROM(CargoPacket, cargopacket_index, var, start)

/**
 * Iterate over all _valid_ cargo packets from the begin of the pool
 * @param var   the variable used as "iterator"
 */
#define FOR_ALL_CARGOPACKETS(var) FOR_ALL_CARGOPACKETS_FROM(var, 0)

extern const struct SaveLoad *GetGoodsDesc();

enum UnloadType {
	UL_KEEP     = 0,      ///< keep cargo on vehicle
	UL_DELIVER  = 1 << 0, ///< deliver cargo
	UL_TRANSFER = 1 << 1, ///< transfer cargo
	UL_ACCEPTED = 1 << 2, ///< cargo is accepted
};

struct UnloadDescription {
	UnloadDescription(GoodsEntry * d, StationID curr, StationID next, OrderUnloadFlags f);
	GoodsEntry * dest;
	/**
	 * station we are trying to unload at now
	 */
	StationID curr_station;
	/**
	 * station the vehicle will unload at next
	 */
	StationID next_station;
	/**
	 * delivery flags
	 */
	byte flags;
};

typedef std::list<CargoPacket *> CargoPacketList;
typedef std::multimap<StationID, CargoPacket *> StationCargoPacketMap;

inline CargoPacket *Deref(StationCargoPacketMap::iterator iter) {return iter->second;}
inline CargoPacket *Deref(StationCargoPacketMap::const_iterator iter) {return iter->second;}
inline CargoPacket *Deref(CargoPacketList::iterator iter) {return *iter;}
inline CargoPacket *Deref(CargoPacketList::const_iterator iter) {return *iter;}

/**
 * Simple collection class for a list of cargo packets
 */
template<class LIST>
class CargoList {
protected:
	typedef typename LIST::iterator Iterator;
	typedef typename LIST::const_iterator ConstIterator;

	LIST packets;         ///< The cargo packets in this list

	bool empty;           ///< Cache for whether this list is empty or not
	uint count;           ///< Cache for the number of cargo entities
	Money feeder_share;   ///< Cache for the feeder share
	StationID source;     ///< Cache for the source of the packet
	uint days_in_transit; ///< Cache for the number of days in transit

	void CachePacket(CargoPacket *cp, uint &dit);

	virtual Iterator LowerBound(const CargoPacket *cp) = 0;
	virtual Iterator UpperBound(const CargoPacket *cp) = 0;

public:

	friend const struct SaveLoad *GetGoodsDesc();

	bool LoadPacket(CargoPacket *packet, uint &cap, TileIndex load_place = INVALID_TILE);

	/** Create the cargo list */
	FORCEINLINE CargoList() { this->InvalidateCache(); }
	/** And destroy it ("frees" all cargo packets) */
	virtual ~CargoList();

	/**
	 * Returns a pointer to the cargo packet list (so you can iterate over it etc).
	 * @return pointer to the packet list
	 */
	FORCEINLINE const LIST *Packets() const { return &this->packets; }

	/**
	 * Ages the all cargo in this list
	 */
	void AgeCargo();

	/**
	 * Checks whether this list is empty
	 * @return true if and only if the list is empty
	 */
	FORCEINLINE bool Empty() const { return this->empty; }

	/**
	 * Returns the number of cargo entities in this list
	 * @return the before mentioned number
	 */
	FORCEINLINE uint Count() const { return this->count; }

	/**
	 * Returns total sum of the feeder share for all packets
	 * @return the before mentioned number
	 */
	FORCEINLINE Money FeederShare() const { return this->feeder_share; }

	/**
	 * Returns source of the first cargo packet in this list
	 * @return the before mentioned source
	 */
	FORCEINLINE StationID Source() const { return this->source; }

	/**
	 * Returns average number of days in transit for a cargo entity
	 * @return the before mentioned number
	 */
	FORCEINLINE uint DaysInTransit() const { return this->days_in_transit; }


	/**
	 * Appends the given cargo packet
	 * @warning After appending this packet may not exist anymore!
	 * @note Do not use the cargo packet anymore after it has been appended to this CargoList!
	 * @param cp the cargo packet to add
	 * @pre cp != NULL
	 */
	virtual void Append(CargoPacket *cp);

	/**
	 * Truncates the cargo in this list to the given amount. It leaves the
	 * first count cargo entities and removes the rest.
	 * @param count the maximum amount of entities to be in the list after the command
	 */
	void Truncate(uint count);

	/**
	 * send all packets to the specified station and update the flow stats at the GoodsEntry accordingly
	 */
	void UpdateFlows(StationID next, GoodsEntry * ge);

	/** Invalidates the cached data and rebuild it */
	void InvalidateCache();

	virtual void Insert(CargoPacket *cp) = 0;
};

/**
 * unsorted CargoList
 */
class VehicleCargoList : public CargoList<CargoPacketList> {
protected:
	void DeliverPacket(Iterator & c, uint & remaining_unload, CargoPayment *payment);
	CargoPacket * TransferPacket(Iterator & c, uint & remaining_unload, GoodsEntry * dest, CargoPayment *payment);

	UnloadType WillUnloadOld(const UnloadDescription & ul, const CargoPacket * p) const;
	UnloadType WillUnloadCargoDist(const UnloadDescription & ul, const CargoPacket * p) const;

	virtual Iterator LowerBound(const CargoPacket *cp) {return packets.begin();}
	virtual Iterator UpperBound(const CargoPacket *cp) {return packets.end();}

public:
	/**
	 * Moves the given amount of cargo from a vehicle to a station.
	 * Depending on the value of flags and dest the side effects of this function differ:
	 *  - dest->acceptance_pickup & GoodsEntry::ACCEPTANCE:
	 *                        => MoveToStation sets OUF_UNLOAD_IF_POSSIBLE in the flags
	 *                        packets are accepted here and may be unloaded and/or delivered (=destroyed);
	 *                        if not using cargodist: all packets are unloaded and delivered
	 *                        if using cargodist: only packets which have this station as final destination are unloaded and delivered
	 *                        if using cargodist: other packets may or may not be unloaded, depending on next_station
	 *                        if not set and using cargodist: packets may still be unloaded, but not delivered.
	 *  - OUFB_UNLOAD: unload all packets unconditionally;
	 *                        if OUF_UNLOAD_IF_POSSIBLE set and OUFB_TRANSFER not set: also deliver packets (no matter if using cargodist)
	 *  - OUFB_TRANSFER: don't deliver any packets;
	 *                        overrides delivering aspect of OUF_UNLOAD_IF_POSSIBLE
	 * @param dest         the destination to move the cargo to
	 * @param max_unload   the maximum amount of cargo entities to move
	 * @param flags        how to handle the moving (side effects)
	 * @param curr_station the station where the cargo currently resides
	 * @param next_station the next unloading station in the vehicle's order list
	 * @return the number of cargo entities actually moved
	 */
	uint MoveToStation(GoodsEntry * dest, uint max_unload, OrderUnloadFlags flags, StationID curr_station, StationID next_station, CargoPayment *payment);

	UnloadType WillUnload(const UnloadDescription & ul, const CargoPacket * p) const;

	/**
	 * Moves the given amount of cargo to a vehicle.
	 * @param dest         the destination to move the cargo to
	 * @param max_load     the maximum amount of cargo entities to move
	 */
	uint MoveToOtherVehicle(VehicleCargoList *dest, uint max_load);

	virtual void Insert(CargoPacket * cp) {packets.push_back(cp);}
};

/**
 * CargoList sorted by next hop
 */
class StationCargoList : public CargoList<StationCargoPacketMap> {
public:
	typedef std::multimap<VehicleID, CargoPacket *> CargoReservation;
	typedef std::map<VehicleID, uint> ReservationAmounts;

	void ReservePacketsForLoading(VehicleID v, uint cap, StationID next_station);

	void Unreserve(VehicleID v);

	/**
	 * route all packets with station "to" as next hop to a different place, except "curr"
	 */
	void RerouteStalePackets(StationID curr, StationID to, GoodsEntry * ge);

	uint AmountReserved(VehicleID v) {return reserved_amounts[v];}

	uint LoadReserved(VehicleCargoList *dest, VehicleID v, uint max_load, TileIndex load_place);

	bool HasReservations() {return !reserved.empty();}

	virtual ~StationCargoList();

	virtual void Insert(CargoPacket * cp) {packets.insert(std::make_pair(cp->next, cp));}
protected:
	virtual Iterator LowerBound(const CargoPacket *cp) {return packets.lower_bound(cp->next);}
	virtual Iterator UpperBound(const CargoPacket *cp) {return packets.upper_bound(cp->next);}
	uint ReservePackets(VehicleID v, uint cap, Iterator begin, Iterator end);
	CargoReservation reserved;
	ReservationAmounts reserved_amounts;
};


#endif /* CARGOPACKET_H */
