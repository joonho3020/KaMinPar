/*******************************************************************************
 * @file:   kway.h
 *
 * @author: Daniel Seemaier
 * @date:   25.10.2021
 * @brief:  Partitioning scheme using direct k-way partitioning.
 ******************************************************************************/
#pragma once

#include "dkaminpar/context.h"
#include "dkaminpar/datastructure/distributed_graph.h"

namespace dkaminpar {
class KWayPartitioningScheme {
public:
    KWayPartitioningScheme(const DistributedGraph& graph, const Context& ctx);

    DistributedPartitionedGraph partition();

private:
    const DistributedGraph& _graph;
    const Context&          _ctx;
};
} // namespace dkaminpar
