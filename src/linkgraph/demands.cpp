/** @file demands.h Definition of demand calculating link graph handler. */

#include "demands.h"
#include "../station_base.h"
#include "../settings_type.h"
#include "../newgrf_cargo.h"
#include "../cargotype.h"
#include "../core/math_func.hpp"
#include <list>

typedef std::list<NodeID> NodeList;

/**
 * Do the actual demand calculation, called from constructor.
 * @param graph the component to calculate the demands for
 */
void DemandCalculator::CalcDemand(LinkGraphComponent *graph) {
	NodeList supplies;
	NodeList demands;
	uint supply_sum = 0;
	uint num_demands = 0;
	uint num_supplies = 0;
	for(NodeID node = 0; node < graph->GetSize(); node++) {
		Node & n = graph->GetNode(node);
		if (n.supply > 0) {
			supplies.push_back(node);
			supply_sum += n.supply;
			num_supplies++;
		}
		if (n.demand > 0) {
			demands.push_back(node);
			num_demands++;
		}
	}

	if (supply_sum == 0 || num_demands == 0) {
		return;
	}

	uint demand_per_node = max(supply_sum / num_demands, (uint)1);
	uint chance = 0;

	while(!supplies.empty() && !demands.empty()) {
		NodeID node1 = supplies.front();
		supplies.pop_front();

		Node & from = graph->GetNode(node1);

		for(uint i = 0; i < num_demands; ++i) {
			assert(!demands.empty());
			NodeID node2 = demands.front();
			demands.pop_front();
			if (node1 == node2) {
				if (demands.empty() && supplies.empty()) {
					/* only one node with supply and demand left */
					return;
				} else {
					demands.push_back(node2);
					continue;
				}
			}
			Node &to = graph->GetNode(node2);
			Edge &forward = graph->GetEdge(node1, node2);
			Edge &backward = graph->GetEdge(node2, node1);

			int32 supply = from.supply;
			if (this->mod_size > 0) {
				supply = max(1, (int32)(supply * to.supply * this->mod_size / 100 / demand_per_node));
			}
			assert(supply > 0);

			/* scale the distance by mod_dist around max_distance */
			int32 distance = this->max_distance - (this->max_distance - (int32)forward.distance) * this->mod_dist / 100;

			/* scale the accuracy by distance around accuracy / 2 */
			int32 divisor = this->accuracy * (this->mod_dist - 50) / 100 + this->accuracy * distance / this->max_distance + 1;
			assert(divisor > 0);

			uint demand_forw = 0;
			if (divisor < supply) {
				demand_forw = supply / divisor;
			} else if (++chance > this->accuracy * num_demands * num_supplies) {
				/* after some trying distribute demand also to other nodes */
				demand_forw = 1;
			}

			demand_forw = min(demand_forw, from.undelivered_supply);

			if (this->mod_size > 0 && from.demand > 0) {
				uint demand_back = demand_forw * this->mod_size / 100;
				if (demand_back > to.undelivered_supply) {
					demand_back = to.undelivered_supply;
					demand_forw = demand_back * 100 / this->mod_size;
				}
				backward.demand += demand_back;
				to.undelivered_supply -= demand_back;
			}

			forward.demand += demand_forw;
			from.undelivered_supply -= demand_forw;

			if (this->mod_size == 0 || to.undelivered_supply > 0) {
				demands.push_back(node2);
			} else {
				num_demands--;
			}

			if (from.undelivered_supply == 0) {
				break;
			}
		}
		if (from.undelivered_supply != 0) {
			supplies.push_back(node1);
		}
	}
}

/**
 * Create the DemandCalculator and immediately do the calculation.
 * @param graph the component to calculate the demands for
 */
DemandCalculator::DemandCalculator(LinkGraphComponent *graph) :
	max_distance(MapSizeX() + MapSizeY() + 1)
{
	CargoID cargo = graph->GetCargo();
	const LinkGraphSettings &settings = graph->GetSettings();

	this->accuracy = settings.accuracy;
	this->mod_size = settings.demand_size;
	this->mod_dist = settings.demand_distance;
	if (this->mod_dist > 100) {
		/* increase effect of mod_dist > 100 */
		int over100 = this->mod_dist - 100;
		this->mod_dist = 100 + over100 * over100;
	}

	switch (settings.GetDistributionType(cargo)) {
	case DT_SYMMETRIC:
		this->CalcDemand(graph);
		break;
	case DT_ASYMMETRIC:
		this->mod_size = 0;
		this->CalcDemand(graph);
		break;
	default:
		NOT_REACHED();
	}
}
