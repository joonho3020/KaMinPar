/*******************************************************************************
 * This file is part of KaMinPar.
 *
 * Copyright (C) 2021 Daniel Seemaier <daniel.seemaier@kit.edu>
 *
 * KaMinPar is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * KaMinPar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with KaMinPar.  If not, see <http://www.gnu.org/licenses/>.
 *
******************************************************************************/
#include "dkaminpar/algorithm/local_graph_contraction.h"
#include "dkaminpar/datastructure/distributed_graph.h"
#include "dkaminpar/datastructure/distributed_graph_builder.h"
#include "dkaminpar/mpi_wrapper.h"
#include "mpi_test.h"

#include <gmock/gmock.h>

using ::testing::Each;
using ::testing::Eq;
using ::testing::UnorderedElementsAre;

namespace dkaminpar::test {
//  0-1 # 2-3
// ###########
//     4-5
class DistributedEdgesFixture : public DistributedGraphFixture {
protected:
  void SetUp() override {
    DistributedGraphFixture::SetUp();

    std::tie(size, rank) = mpi::get_comm_info(MPI_COMM_WORLD);
    ALWAYS_ASSERT(size == 3) << "must be tested on three PEs";

    scalable_vector<GlobalNodeID> node_distribution{0, 2, 4, 6};
    const GlobalNodeID global_n = 6;
    const GlobalEdgeID global_m = 6;

    n0 = 2 * rank;
    graph = dkaminpar::graph::Builder()
                .initialize(global_n, global_m, rank, std::move(node_distribution))
                .create_node(1)
                .create_edge(1, n0 + 1)
                .create_node(1)
                .create_edge(1, n0)
                .finalize();
  }

  DistributedGraph graph;
  GlobalNodeID n0;
};

TEST_F(DistributedEdgesFixture, DistributedEdgesAreAsExpected) {
  mpi::barrier(MPI_COMM_WORLD);

  EXPECT_EQ(graph.n(), 2);
  EXPECT_EQ(graph.m(), 2);
  EXPECT_EQ(graph.global_n(), 6);
  EXPECT_EQ(graph.global_m(), 6);
  EXPECT_EQ(graph.ghost_n(), 0);
}

TEST_F(DistributedEdgesFixture, ContractingEdgesSimultaneouslyWorks) {
  mpi::barrier(MPI_COMM_WORLD);

  auto [c_graph, mapping, m_ctx] = dkaminpar::graph::contract_local_clustering(graph, {0, 0});

  EXPECT_EQ(c_graph.n(), 1);
  EXPECT_EQ(c_graph.m(), 0);
  EXPECT_EQ(c_graph.global_n(), 3);
  EXPECT_EQ(c_graph.global_m(), 0);
}

TEST_F(DistributedEdgesFixture, ContractingEdgeOnOnePEWorks) {
  mpi::barrier(MPI_COMM_WORLD);

  scalable_vector<shm::parallel::IntegralAtomicWrapper<NodeID>> clustering;
  clustering.push_back(0);
  clustering.push_back((rank == 0) ? 0 : 1);
  // {0, 0} on PE 0, {0, 1} on PEs 1, 2

  auto [c_graph, mapping, m_ctx] = dkaminpar::graph::contract_local_clustering(graph, clustering);
  if (rank == 0) {
    EXPECT_EQ(c_graph.n(), 1);
    EXPECT_EQ(c_graph.m(), 0);
  } else {
    EXPECT_EQ(c_graph.n(), 2);
    EXPECT_EQ(c_graph.m(), 2);
  }

  EXPECT_EQ(c_graph.global_n(), 5);
  EXPECT_EQ(c_graph.global_m(), 4);
}

//  0---1-#-3---4
//  |\ /  #  \ /|
//  | 2---#---5 |
//  |  \  #  /  |
// ###############
//  |    \ /    |
//  |     8     |
//  |    / \    |
//  +---7---6---+
class DistributedTrianglesFixture : public DistributedGraphFixture {
protected:
  void SetUp() override {
    DistributedGraphFixture::SetUp();

    const auto [size, rank] = mpi::get_comm_info(MPI_COMM_WORLD);
    ALWAYS_ASSERT(size == 3) << "must be tested on three PEs";

    scalable_vector<GlobalNodeID> node_distribution{0, 3, 6, 9};
    const GlobalNodeID global_n = 9;
    const GlobalEdgeID global_m = 30;

    n0 = 3 * rank;
    graph = dkaminpar::graph::Builder{}
                .initialize(global_n, global_m, rank, std::move(node_distribution))
                .create_node(1)
                .create_edge(1, n0 + 1)
                .create_edge(1, n0 + 2)
                .create_edge(1, prev(n0, 2, 9))
                .create_node(1)
                .create_edge(1, n0)
                .create_edge(1, n0 + 2)
                .create_edge(1, next(n0 + 1, 2, 9))
                .create_node(1)
                .create_edge(1, n0)
                .create_edge(1, n0 + 1)
                .create_edge(1, next(n0 + 2, 3, 9))
                .create_edge(1, prev(n0 + 2, 3, 9))
                .finalize();
  }

  DistributedGraph graph;
  GlobalNodeID n0;
};

TEST_F(DistributedTrianglesFixture, DistributedTrianglesAreAsExpected) {
  mpi::barrier(MPI_COMM_WORLD);

  EXPECT_EQ(graph.n(), 3);
  EXPECT_EQ(graph.m(), 10); // 2x3 internal edges, 4 edges to ghost nodes
  EXPECT_EQ(graph.ghost_n(), 4);
  EXPECT_EQ(graph.global_n(), 9);
  EXPECT_EQ(graph.global_m(), 30);
  EXPECT_EQ(graph.total_node_weight(), 3);
}

TEST_F(DistributedTrianglesFixture, ContractingTriangleOnOnePEWorks) {
  mpi::barrier(MPI_COMM_WORLD);

  // contract all nodes on PE 0, keep nodes on PEs 1, 2
  scalable_vector<shm::parallel::IntegralAtomicWrapper<NodeID>> clustering;
  clustering.push_back(0);
  clustering.push_back((rank == 0) ? 0 : 1);
  clustering.push_back((rank == 0) ? 0 : 2);

  auto [c_graph, mapping, m_ctx] = dkaminpar::graph::contract_local_clustering(graph, clustering);

  if (rank == 0) {
    EXPECT_EQ(c_graph.n(), 1);
    EXPECT_EQ(c_graph.m(), 4);
    EXPECT_THAT(c_graph.edge_weights(), Each(Eq(1)));
    EXPECT_THAT(c_graph.node_weights(), UnorderedElementsAre(3, 1, 1, 1, 1)); // includes ghost nodes
    EXPECT_EQ(c_graph.total_node_weight(), 3);
    EXPECT_EQ(c_graph.ghost_n(), 4);
  } else {
    EXPECT_EQ(c_graph.n(), 3);
    EXPECT_EQ(c_graph.m(), 10);
    EXPECT_THAT(c_graph.edge_weights(), Each(Eq(1)));
    EXPECT_THAT(c_graph.node_weights(), UnorderedElementsAre(1, 1, 1, 1, 1, 3)); // includes ghost nodes
    EXPECT_EQ(c_graph.total_node_weight(), 3);
    EXPECT_EQ(c_graph.ghost_n(), 3);
  }

  EXPECT_EQ(c_graph.global_n(), 7);
  EXPECT_EQ(c_graph.global_m(), 24);
}

TEST_F(DistributedTrianglesFixture, ContractigTrianglesOnTwoPEsWorks) {
  mpi::barrier(MPI_COMM_WORLD);

  // contract all nodes on PE 0 and 1, keep nodes on PEs 2
  scalable_vector<shm::parallel::IntegralAtomicWrapper<NodeID>> clustering;
  clustering.push_back(0);
  clustering.push_back((rank < 2) ? 0 : 1);
  clustering.push_back((rank < 2) ? 0 : 2);

  auto [c_graph, mapping, m_ctx] = dkaminpar::graph::contract_local_clustering(graph, clustering);

  if (rank < 2) {
    EXPECT_EQ(c_graph.n(), 1);
    EXPECT_EQ(c_graph.m(), 3);
    EXPECT_THAT(c_graph.edge_weights(), UnorderedElementsAre(2, 1, 1));
    EXPECT_THAT(c_graph.node_weights(), UnorderedElementsAre(3, 3, 1, 1)); // includes ghost nodes
    EXPECT_EQ(c_graph.total_node_weight(), 3);
    EXPECT_EQ(c_graph.ghost_n(), 3);
  } else { // rank == 2
    EXPECT_EQ(c_graph.n(), 3);
    EXPECT_EQ(c_graph.m(), 10);
    EXPECT_THAT(c_graph.edge_weights(), Each(Eq(1)));
    EXPECT_THAT(c_graph.node_weights(), UnorderedElementsAre(1, 1, 1, 3, 3)); // includes ghost nodes
    EXPECT_EQ(c_graph.total_node_weight(), 3);
    EXPECT_EQ(c_graph.ghost_n(), 2);
  }

  EXPECT_EQ(c_graph.global_n(), 5);
  EXPECT_EQ(c_graph.global_m(), 16);
}

TEST_F(DistributedTrianglesFixture, ContractingAllTrianglesWorks) {
  mpi::barrier(MPI_COMM_WORLD);

  auto [c_graph, mapping, m_ctx] = dkaminpar::graph::contract_local_clustering(graph, {0, 0, 0});

  EXPECT_EQ(c_graph.n(), 1);
  EXPECT_EQ(c_graph.m(), 2);
  EXPECT_THAT(c_graph.edge_weights(), Each(Eq(2)));
  EXPECT_THAT(c_graph.node_weights(), Each(Eq(3)));
  EXPECT_EQ(c_graph.ghost_n(), 2);
  EXPECT_EQ(c_graph.global_n(), 3);
  EXPECT_EQ(c_graph.global_m(), 6);
  EXPECT_EQ(c_graph.total_node_weight(), 3);
}
} // namespace dkaminpar::test