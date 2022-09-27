/*******************************************************************************
 * @file:   allgather_graph.h
 * @author: Daniel Seemaier
 * @date:   27.10.2021
 * @brief:  Allgather a distributed graph to each PE.
 ******************************************************************************/
#pragma once

#include <mpi.h>

#include "dkaminpar/datastructure/distributed_graph.h"
#include "dkaminpar/definitions.h"

#include "kaminpar/datastructure/graph.h"

namespace kaminpar::dist::graph {
shm::Graph                  allgather(const DistributedGraph& graph);
DistributedGraph            allgather_on_groups(const DistributedGraph& graph, MPI_Comm group_comm);
DistributedPartitionedGraph reduce_scatter(const DistributedGraph& dist_graph, shm::PartitionedGraph shm_p_graph);
} // namespace kaminpar::dist::graph
