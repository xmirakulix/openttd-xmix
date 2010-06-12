/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file moving_average.cpp Implementation of moving average functions */

#include "moving_average.h"
#include "variables.h"
#include "station_base.h"

/**
 * Run moving average decrease function on all items from a pool which are due
 * this tick. This function expects to be run every tick. It calls a method
 * "RunAverages()" on all items for which id % DAY_TICKS == _tick_counter % DAY_TICKS.
 * So each item is called once a day.
 */
template <class Titem> void RunAverages()
{
	for(uint id = _tick_counter % DAY_TICKS; id < Titem::GetPoolSize(); id += DAY_TICKS) {
		Titem *item = Titem::GetIfValid(id);
		if (item != NULL) {
			item->RunAverages();
		}
	}
}

/**
 * Decrease the given value using this moving average
 * @param value the moving average value to be decreased
 * @return the decreased value
 */
template <class Tvalue>
FORCEINLINE Tvalue &MovingAverage<Tvalue>::Decrease(Tvalue &value) const
{
	value *= this->length;
	value /= (this->length + 1);
	return value;
}

template class MovingAverage<uint>;
template void RunAverages<Station>();
