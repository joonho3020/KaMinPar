#include <gmock/gmock.h>

#include "tests/shm/graph_factories.h"

#include "kaminpar-shm/datastructures/compressed_graph.h"
#include "kaminpar-shm/datastructures/compressed_graph_builder.h"
#include "kaminpar-shm/datastructures/csr_graph.h"
#include "kaminpar-shm/graphutils/permutator.h"

#define HIGH_DEGREE_NUM (CompressedGraph::kHighDegreeThreshold * 5)
#define TEST_ON_ALL_GRAPHS(test_function)                                                          \
  test_function(make_empty_graph(0));                                                              \
  test_function(make_empty_graph(100));                                                            \
  test_function(make_path_graph(100));                                                             \
  test_function(make_star_graph(100));                                                             \
  test_function(make_grid_graph(100, 100));                                                        \
  test_function(make_complete_bipartite_graph(100, 100));                                          \
  test_function(make_complete_graph(100));                                                         \
  test_function(make_matching_graph(100));                                                         \
  test_function(make_star_graph(HIGH_DEGREE_NUM));

#define TEST_ON_WEIGHTED_GRAPHS(test_function)                                                     \
  test_function(make_complete_graph(100, [](const NodeID u, const NodeID v) {                      \
    return static_cast<EdgeWeight>(u + v);                                                         \
  }));                                                                                             \
  test_function(make_complete_bipartite_graph(100, 100, [](const NodeID u, const NodeID v) {       \
    return static_cast<EdgeWeight>(u + v);                                                         \
  }));                                                                                             \
  test_function(make_star_graph(HIGH_DEGREE_NUM, [](const NodeID u, const NodeID v) {              \
    return static_cast<EdgeWeight>(u + v);                                                         \
  }));

namespace kaminpar::shm::testing {

template <typename T> static bool operator==(const IotaRange<T> &a, const IotaRange<T> &b) {
  return a.begin() == b.begin() && a.end() == b.end();
};

static void test_compressed_graph_size(const Graph &graph) {
  const auto &csr_graph = *dynamic_cast<const CSRGraph *>(graph.underlying_graph());
  const auto compressed_graph = CompressedGraphBuilder::compress(csr_graph);

  EXPECT_EQ(csr_graph.n(), compressed_graph.n());
  EXPECT_EQ(csr_graph.m(), compressed_graph.m());
}

TEST(CompressedGraphTest, compressed_graph_size) {
  TEST_ON_ALL_GRAPHS(test_compressed_graph_size);
}

static void test_compressed_graph_nodes_operation(const Graph &graph) {
  const auto &csr_graph = *dynamic_cast<const CSRGraph *>(graph.underlying_graph());
  const auto compressed_graph = CompressedGraphBuilder::compress(csr_graph);

  EXPECT_TRUE(csr_graph.nodes() == compressed_graph.nodes());
}

TEST(CompressedGraphTest, compressed_graph_nodes_operation) {
  TEST_ON_ALL_GRAPHS(test_compressed_graph_nodes_operation);
}

static void test_compressed_graph_edges_operation(const Graph &graph) {
  const auto &csr_graph = *dynamic_cast<const CSRGraph *>(graph.underlying_graph());
  const auto compressed_graph = CompressedGraphBuilder::compress(csr_graph);

  EXPECT_TRUE(csr_graph.edges() == compressed_graph.edges());
}

TEST(CompressedGraphTest, compressed_graph_edges_operation) {
  TEST_ON_ALL_GRAPHS(test_compressed_graph_edges_operation);
}

static void test_compressed_graph_degree_operation(const Graph &graph) {
  const auto &csr_graph = *dynamic_cast<const CSRGraph *>(graph.underlying_graph());
  const auto compressed_graph = CompressedGraphBuilder::compress(csr_graph);

  for (const NodeID node : graph.nodes()) {
    EXPECT_EQ(csr_graph.degree(node), compressed_graph.degree(node));
  }
}

TEST(CompressedGraphTest, compressed_graph_degree_operation) {
  TEST_ON_ALL_GRAPHS(test_compressed_graph_degree_operation);
}

static void test_compressed_graph_incident_edges_operation(const Graph &graph) {
  const auto &csr_graph = *dynamic_cast<const CSRGraph *>(graph.underlying_graph());
  const auto compressed_graph = CompressedGraphBuilder::compress(csr_graph);

  for (const NodeID node : graph.nodes()) {
    EXPECT_TRUE(csr_graph.incident_edges(node) == compressed_graph.incident_edges(node));
  }
}

TEST(CompressedGraphTest, compressed_graph_incident_edges_operation) {
  TEST_ON_ALL_GRAPHS(test_compressed_graph_incident_edges_operation);
}

template <bool rearrange> static void test_compressed_graph_adjacent_nodes_operation(Graph graph) {
  auto &csr_graph = *dynamic_cast<CSRGraph *>(graph.underlying_graph());
  const auto compressed_graph = CompressedGraphBuilder::compress(csr_graph);

  if constexpr (rearrange) {
    graph::reorder_edges_by_compression(csr_graph);
  }

  std::vector<NodeID> graph_neighbours;
  std::vector<NodeID> compressed_graph_neighbours;
  for (const NodeID node : graph.nodes()) {
    graph.adjacent_nodes(node, [&](const NodeID adjacent_node) {
      graph_neighbours.push_back(adjacent_node);
    });

    compressed_graph.adjacent_nodes(node, [&](const NodeID adjacent_node) {
      compressed_graph_neighbours.push_back(adjacent_node);
    });

    EXPECT_EQ(graph_neighbours.size(), compressed_graph_neighbours.size());

    if constexpr (!rearrange) {
      std::sort(graph_neighbours.begin(), graph_neighbours.end());
      std::sort(compressed_graph_neighbours.begin(), compressed_graph_neighbours.end());
    }

    EXPECT_TRUE(graph_neighbours == compressed_graph_neighbours);

    graph_neighbours.clear();
    compressed_graph_neighbours.clear();
  }
}

TEST(CompressedGraphTest, compressed_graph_adjacent_nodes_operation) {
  TEST_ON_ALL_GRAPHS(test_compressed_graph_adjacent_nodes_operation<false>);
  TEST_ON_ALL_GRAPHS(test_compressed_graph_adjacent_nodes_operation<true>);
}

template <bool kRearrange>
static void test_compressed_graph_weighted_adjacent_nodes_operation(Graph graph) {
  auto &csr_graph = *dynamic_cast<CSRGraph *>(graph.underlying_graph());
  const auto compressed_graph = CompressedGraphBuilder::compress(csr_graph);

  if constexpr (kRearrange) {
    graph::reorder_edges_by_compression(csr_graph);
  }

  std::vector<std::pair<NodeID, EdgeWeight>> graph_neighbours;
  std::vector<std::pair<NodeID, EdgeWeight>> compressed_graph_neighbours;
  for (const NodeID u : graph.nodes()) {
    graph.adjacent_nodes(u, [&](const NodeID v, const EdgeWeight w) {
      graph_neighbours.emplace_back(v, w);
    });

    compressed_graph.adjacent_nodes(u, [&](const NodeID v, const EdgeWeight w) {
      compressed_graph_neighbours.emplace_back(v, w);
    });

    EXPECT_EQ(graph_neighbours.size(), compressed_graph_neighbours.size());

    if constexpr (!kRearrange) {
      std::sort(graph_neighbours.begin(), graph_neighbours.end());
      std::sort(compressed_graph_neighbours.begin(), compressed_graph_neighbours.end());
    }

    EXPECT_TRUE(graph_neighbours == compressed_graph_neighbours);

    graph_neighbours.clear();
    compressed_graph_neighbours.clear();
  }
}

TEST(CompressedGraphTest, compressed_graph_weighted_adjacent_nodes_operation) {
  TEST_ON_ALL_GRAPHS(test_compressed_graph_weighted_adjacent_nodes_operation<false>);
  TEST_ON_ALL_GRAPHS(test_compressed_graph_weighted_adjacent_nodes_operation<true>);
}

template <bool rearrange> static void test_compressed_graph_neighbors_operation(Graph graph) {
  auto &csr_graph = *dynamic_cast<CSRGraph *>(graph.underlying_graph());
  const auto compressed_graph = CompressedGraphBuilder::compress(csr_graph);

  if constexpr (rearrange) {
    graph::reorder_edges_by_compression(csr_graph);
  }

  std::vector<EdgeID> graph_incident_edges;
  std::vector<NodeID> graph_adjacent_node;
  std::vector<EdgeID> compressed_graph_incident_edges;
  std::vector<NodeID> compressed_graph_adjacent_node;
  for (const NodeID node : graph.nodes()) {
    for (const auto [incident_edge, adjacent_node] : graph.neighbors(node)) {
      graph_incident_edges.push_back(incident_edge);
      graph_adjacent_node.push_back(adjacent_node);
    }

    compressed_graph.neighbors(node, [&](const EdgeID incident_edge, const NodeID adjacent_node) {
      compressed_graph_incident_edges.push_back(incident_edge);
      compressed_graph_adjacent_node.push_back(adjacent_node);
    });

    EXPECT_EQ(graph_incident_edges.size(), compressed_graph_incident_edges.size());

    if constexpr (!rearrange) {
      std::sort(graph_incident_edges.begin(), graph_incident_edges.end());
      std::sort(graph_adjacent_node.begin(), graph_adjacent_node.end());
      std::sort(compressed_graph_incident_edges.begin(), compressed_graph_incident_edges.end());
      std::sort(compressed_graph_adjacent_node.begin(), compressed_graph_adjacent_node.end());
    }

    EXPECT_TRUE(graph_incident_edges == compressed_graph_incident_edges);
    EXPECT_TRUE(graph_adjacent_node == compressed_graph_adjacent_node);

    graph_incident_edges.clear();
    graph_adjacent_node.clear();
    compressed_graph_incident_edges.clear();
    compressed_graph_adjacent_node.clear();
  }
}

TEST(CompressedGraphTest, compressed_graph_neighbors_operation) {
  TEST_ON_ALL_GRAPHS(test_compressed_graph_neighbors_operation<false>);
  TEST_ON_ALL_GRAPHS(test_compressed_graph_neighbors_operation<true>);
}

template <bool rearrange>
static void test_compressed_graph_weighted_neighbors_operation(Graph graph) {
  auto &csr_graph = *dynamic_cast<CSRGraph *>(graph.underlying_graph());
  const auto compressed_graph = CompressedGraphBuilder::compress(csr_graph);

  if constexpr (rearrange) {
    graph::reorder_edges_by_compression(csr_graph);
  }

  std::vector<EdgeID> graph_incident_edges;
  std::vector<std::pair<NodeID, EdgeWeight>> graph_adjacent_node;
  std::vector<EdgeID> compressed_graph_incident_edges;
  std::vector<std::pair<NodeID, EdgeWeight>> compressed_graph_adjacent_node;
  for (const NodeID u : graph.nodes()) {
    graph.neighbors(u, [&](const EdgeID e, const NodeID v, const EdgeWeight w) {
      graph_incident_edges.push_back(e);
      graph_adjacent_node.emplace_back(v, w);
    });

    compressed_graph.neighbors(u, [&](const EdgeID e, const NodeID v, const EdgeWeight w) {
      compressed_graph_incident_edges.push_back(e);
      compressed_graph_adjacent_node.emplace_back(v, w);
    });

    EXPECT_EQ(graph_incident_edges.size(), compressed_graph_incident_edges.size());

    if constexpr (!rearrange) {
      std::sort(graph_incident_edges.begin(), graph_incident_edges.end());
      std::sort(graph_adjacent_node.begin(), graph_adjacent_node.end());
      std::sort(compressed_graph_incident_edges.begin(), compressed_graph_incident_edges.end());
      std::sort(compressed_graph_adjacent_node.begin(), compressed_graph_adjacent_node.end());
    }

    EXPECT_TRUE(graph_incident_edges == compressed_graph_incident_edges);
    EXPECT_TRUE(graph_adjacent_node == compressed_graph_adjacent_node);

    graph_incident_edges.clear();
    graph_adjacent_node.clear();
    compressed_graph_incident_edges.clear();
    compressed_graph_adjacent_node.clear();
  }
}

TEST(CompressedGraphTest, compressed_graph_weighted_neighbors_operation) {
  TEST_ON_ALL_GRAPHS(test_compressed_graph_weighted_neighbors_operation<false>);
  TEST_ON_ALL_GRAPHS(test_compressed_graph_weighted_neighbors_operation<true>);
}

static void test_compressed_graph_neighbors_limit_operation(Graph graph) {
  auto &csr_graph = *dynamic_cast<CSRGraph *>(graph.underlying_graph());
  const auto compressed_graph = CompressedGraphBuilder::compress(csr_graph);

  graph::reorder_edges_by_compression(csr_graph);

  std::vector<EdgeID> graph_incident_edges;
  std::vector<NodeID> graph_adjacent_node;
  std::vector<EdgeID> compressed_graph_incident_edges;
  std::vector<NodeID> compressed_graph_adjacent_node;
  for (const NodeID node : graph.nodes()) {
    const NodeID max_neighbor_count = std::max<NodeID>(1, graph.degree(node) / 2);

    csr_graph.neighbors(
        node,
        max_neighbor_count,
        [&](const EdgeID incident_edge, const NodeID adjacent_node) {
          graph_incident_edges.push_back(incident_edge);
          graph_adjacent_node.push_back(adjacent_node);
        }
    );

    compressed_graph.neighbors(
        node,
        max_neighbor_count,
        [&](const EdgeID incident_edge, const NodeID adjacent_node) {
          compressed_graph_incident_edges.push_back(incident_edge);
          compressed_graph_adjacent_node.push_back(adjacent_node);
        }
    );

    EXPECT_EQ(graph_incident_edges.size(), compressed_graph_incident_edges.size());
    EXPECT_TRUE(graph_incident_edges == compressed_graph_incident_edges);
    EXPECT_TRUE(graph_adjacent_node == compressed_graph_adjacent_node);

    graph_incident_edges.clear();
    graph_adjacent_node.clear();
    compressed_graph_incident_edges.clear();
    compressed_graph_adjacent_node.clear();
  }
}

TEST(CompressedGraphTest, compressed_graph_neighbors_limit_operation) {
  TEST_ON_ALL_GRAPHS(test_compressed_graph_neighbors_limit_operation);
}

static void test_compressed_graph_pfor_neighbors_operation(const Graph &graph) {
  const auto &csr_graph = *dynamic_cast<const CSRGraph *>(graph.underlying_graph());
  const auto compressed_graph = CompressedGraphBuilder::compress(csr_graph);

  tbb::concurrent_vector<EdgeID> graph_incident_edges;
  tbb::concurrent_vector<EdgeID> compressed_graph_incident_edges;
  tbb::concurrent_vector<std::pair<NodeID, EdgeWeight>> graph_adjacent_node;
  tbb::concurrent_vector<std::pair<NodeID, EdgeWeight>> compressed_graph_adjacent_node;
  for (const NodeID u : graph.nodes()) {
    graph.pfor_neighbors(
        u,
        std::numeric_limits<NodeID>::max(),
        1,
        [&](const EdgeID e, const NodeID v, const EdgeWeight w) {
          graph_incident_edges.push_back(e);
          graph_adjacent_node.emplace_back(v, w);
        }
    );

    compressed_graph.pfor_neighbors(
        u,
        std::numeric_limits<NodeID>::max(),
        1,
        [&](const EdgeID e, const NodeID v, const EdgeWeight w) {
          compressed_graph_incident_edges.push_back(e);
          compressed_graph_adjacent_node.emplace_back(v, w);
        }
    );

    EXPECT_EQ(graph_incident_edges.size(), compressed_graph_incident_edges.size());

    std::sort(graph_incident_edges.begin(), graph_incident_edges.end());
    std::sort(compressed_graph_incident_edges.begin(), compressed_graph_incident_edges.end());
    EXPECT_TRUE(graph_incident_edges == compressed_graph_incident_edges);

    std::sort(graph_adjacent_node.begin(), graph_adjacent_node.end());
    std::sort(compressed_graph_adjacent_node.begin(), compressed_graph_adjacent_node.end());
    EXPECT_TRUE(graph_adjacent_node == compressed_graph_adjacent_node);

    graph_incident_edges.clear();
    compressed_graph_incident_edges.clear();
    graph_adjacent_node.clear();
    compressed_graph_adjacent_node.clear();
  }
}

TEST(CompressedGraphTest, compressed_graph_pfor_neighbors_operation) {
  TEST_ON_ALL_GRAPHS(test_compressed_graph_pfor_neighbors_operation);
}

} // namespace kaminpar::shm::testing
