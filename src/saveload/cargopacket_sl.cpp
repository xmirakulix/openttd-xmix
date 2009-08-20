/* $Id$ */

/** @file cargopacket_sl.cpp Code handling saving and loading of cargo packets */

#include "../stdafx.h"
#include "../cargopacket.h"

#include "saveload.h"

const SaveLoad *GetCargoPacketDesc()
{
	static const SaveLoad _cargopacket_desc[] = {
		    SLE_VAR(CargoPacket, source,          SLE_UINT16),
		    SLE_VAR(CargoPacket, source_xy,       SLE_UINT32),
		    SLE_VAR(CargoPacket, loaded_at_xy,    SLE_UINT32),
		    SLE_VAR(CargoPacket, count,           SLE_UINT16),
		    SLE_VAR(CargoPacket, days_in_transit, SLE_UINT8),
		    SLE_VAR(CargoPacket, feeder_share,    SLE_INT64),
		SLE_CONDVAR(CargoPacket, source_type,     SLE_UINT8,  125, SL_MAX_VERSION),
		SLE_CONDVAR(CargoPacket, source_id,       SLE_UINT16, 125, SL_MAX_VERSION),

		/* Used to be paid_for, but that got changed. */
		SLE_CONDNULL(1, 0, 120),

		SLE_END()
	};
	return _cargopacket_desc;
}

static void Save_CAPA()
{
	CargoPacket *cp;

	FOR_ALL_CARGOPACKETS(cp) {
		SlSetArrayIndex(cp->index);
		SlObject(cp, GetCargoPacketDesc());
	}
}

static void Load_CAPA()
{
	int index;

	while ((index = SlIterateArray()) != -1) {
		CargoPacket *cp = new (index) CargoPacket();
		SlObject(cp, GetCargoPacketDesc());
	}
}

extern const ChunkHandler _cargopacket_chunk_handlers[] = {
	{ 'CAPA', Save_CAPA, Load_CAPA, NULL, CH_ARRAY | CH_LAST},
};
