/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file linkgraph.h Declaration of link graph classes used for cargo distribution. */

#ifndef LINKGRAPH_H_
#define LINKGRAPH_H_

#include "../stdafx.h"
#include "../station_base.h"
#include "../cargo_type.h"
#include "../thread/thread.h"
#include "../settings_type.h"
#include "../date_func.h"
#include "linkgraph_type.h"
#include <list>
#include <vector>

struct SaveLoad;

/**
 * Node of the link graph. contains all relevant information from the associated
 * station. It's copied so that the link graph job can work on its own data set
 * in a separate thread.
 */
class Node {
public:
	uint supply;             ///< supply at the station
	uint undelivered_supply; ///< amount of supply that hasn't been distributed yet
	uint demand;             ///< acceptance at the station
	StationID station;       ///< the station's ID

	void Init(StationID st = INVALID_STATION, uint sup = 0, uint dem = 0);
};

/**
 * An edge in the link graph. Corresponds to a link between two stations.
 */
class Edge {
public:
	uint distance; ///< length of the link
	uint capacity; ///< capacity of the link
	uint demand;   ///< transport demand between the nodes

	void Init(uint distance = 0, uint capacity = 0);
};

/**
 * A connected component of a link graph. Contains a complete set of stations
 * connected by links as nodes and edges. Each component also holds a copy of
 * the link graph settings at the time of its creation. The global settings
 * might change between the creation and join time so we can't rely on them.
 */
class LinkGraphComponent {
private:
	typedef std::vector<Node> NodeVector;
	typedef std::vector<std::vector<Edge> > EdgeMatrix;

public:
	LinkGraphComponent();

	void Init(LinkGraphComponentID id);

	/**
	 * Get a reference to an edge.
	 * @param from the origin node
	 * @param the destination node
	 * @return the edge between from and to
	 */
	FORCEINLINE Edge &GetEdge(NodeID from, NodeID to)
	{
		return this->edges[from][to];
	}

	/**
	 * Get a reference to a node with the specified id.
	 * @param num ID of the node
	 * @return the requested node
	 */
	FORCEINLINE Node &GetNode(NodeID num)
	{
		return this->nodes[num];
	}

	/**
	 * Get the current size of the component.
	 * @return the size
	 */
	FORCEINLINE uint GetSize() const
	{
		return this->num_nodes;
	}

	void SetSize();

	NodeID AddNode(Station *st);

	void AddEdge(NodeID from, NodeID to, uint capacity);

	/**
	 * Get the ID of this component.
	 * @return the ID
	 */
	FORCEINLINE LinkGraphComponentID GetIndex() const
	{
		return this->index;
	}

	/**
	 * Get the cargo ID this component's link graph refers to.
	 * @return the cargo ID
	 */
	FORCEINLINE CargoID GetCargo() const
	{
		return this->cargo;
	}

	/**
	 * Get the link graph settings for this component.
	 * @return the settings
	 */
	FORCEINLINE const LinkGraphSettings &GetSettings() const
	{
		return this->settings;
	}

	/**
	 * Mark this component as empty.
	 */
	FORCEINLINE void Clear()
	{
		this->num_nodes = 0;
	}

protected:
	LinkGraphSettings settings; ///< Copy of _settings_game.linkgraph at creation time
	CargoID cargo;              ///< Cargo of this component's link graph
	uint num_nodes;             ///< Number of nodes in the component
	LinkGraphComponentID index; ///< ID of the component
	NodeVector nodes;           ///< Nodes in the component
	EdgeMatrix edges;           ///< Edges in the component
};

/**
 * A handler doing "something" on a link graph component. It must not keep any
 * state as it is called concurrently from different threads.
 */
class ComponentHandler {
public:
	/**
	 * Destroy the handler. Must be given due to virtual Run.
	 */
	virtual ~ComponentHandler() {}

	/**
	 * Run the handler. A link graph handler must not read or write any data
	 * outside the given component as that would create a potential desync.
	 */
	virtual void Run(LinkGraphComponent *component) = 0;
};

/**
 * A job to be executed on a link graph component. It inherits a component and
 * keeps a static list of handlers to be run on it. It may or may not run in a
 * thread and contains a thread object for this option.
 */
class LinkGraphJob : public LinkGraphComponent {
private:
	typedef std::list<ComponentHandler *> HandlerList;

public:

	LinkGraphJob() : thread(NULL) {}

	/**
	 * Destructor; Clean up the thread if it's there.
	 */
	~LinkGraphJob()
	{
		this->Join();
	}

	static void RunLinkGraphJob(void *j);

	/**
	 * Add a handler to the end of the list.
	 * @param handler the handler to be added
	 */
	static void AddHandler(ComponentHandler *handler)
	{
		LinkGraphJob::_handlers.push_back(handler);
	}

	static void ClearHandlers();

	void SpawnThread();

	void Join();

private:
	static HandlerList _handlers;   ///< Handlers the job is executing
	ThreadObject *thread;           ///< Thread the job is running in or NULL if it's running in the main thread

	/**
	 * Private Copy-Constructor: there cannot be two identical LinkGraphJobs.
	 * @param other hypothetical other job to be copied.
	 */
	LinkGraphJob(const LinkGraphJob &other) {NOT_REACHED();}
};

/**
 * A link graph, inheriting one job.
 */
class LinkGraph : public LinkGraphJob {
public:
	static const uint COMPONENTS_JOIN_TICK  = 21; ///< tick when jobs are joined every day
	static const uint COMPONENTS_SPAWN_TICK = 58; ///< tick when jobs are spawned every day

	/**
	 * Create a link graph.
	 */
	LinkGraph() : current_station_id(0) {}

	void Init(CargoID cargo);

	void NextComponent();

	void Join();

private:
	StationID current_station_id; ///< ID of the last station examined while creating components

	friend const SaveLoad *GetLinkGraphDesc();

	void CreateComponent(Station *first);
};

void InitializeLinkGraphs();
extern LinkGraph _link_graphs[NUM_CARGO];

#endif /* LINKGRAPH_H_ */
