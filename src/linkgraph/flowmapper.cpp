/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file flowmapper.cpp Definition of flowmapper. */

#include "flowmapper.h"

/**
 * Map the paths generated by the MCF solver into flows associated with nodes.
 * @param component the link graph component to be used.
 */
void FlowMapper::Run(LinkGraphComponent *component)
{
	for (NodeID node_id = 0; node_id < component->GetSize(); ++node_id) {
		Node &prev_node = component->GetNode(node_id);
		StationID prev = prev_node.station;
		PathSet &paths = prev_node.paths;
		for(PathSet::iterator i = paths.begin(); i != paths.end(); ++i) {
			Path *path = *i;
			uint flow = path->GetFlow();
			if (flow == 0) continue;
			Node &node = component->GetNode(path->GetNode());
			StationID via = node.station;
			assert(prev != via);
			StationID origin = component->GetNode(path->GetOrigin()).station;
			assert(via != origin);
			/* mark all of the flow for local consumation at "first" */
			node.flows[origin][via] += flow;
			/* pass some of the flow marked for local consumation at "prev" on
			 * to this node
			 */
			prev_node.flows[origin][via] += flow;
			/* find simple circular flows ... */
			assert(node.flows[origin][prev] == 0);
			if (prev != origin) {
				prev_node.flows[origin][prev] -= flow;
			}
		}
	}
	for (NodeID node_id = 0; node_id < component->GetSize(); ++node_id) {
		PathSet &paths = component->GetNode(node_id).paths;
		for (PathSet::iterator i = paths.begin(); i != paths.end(); ++i) {
			delete (*i);
		}
		paths.clear();
	}
}
