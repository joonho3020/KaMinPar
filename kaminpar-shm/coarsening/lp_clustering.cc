/******************************************************************************
 * Label propagation for graph coarsening / clustering.
 *
 * @file:   lp_clustering.cc
 * @author: Daniel Seemaier
 * @date:   29.09.2021
 ******************************************************************************/
#include "kaminpar-shm/coarsening/lp_clustering.h"

#include <memory>

#include "kaminpar-shm/coarsening/clusterer.h"
#include "kaminpar-shm/context.h"
#include "kaminpar-shm/datastructures/graph.h"
#include "kaminpar-shm/label_propagation.h"

#include "kaminpar-common/heap_profiler.h"
#include "kaminpar-common/timer.h"

namespace kaminpar::shm {
//
// Actual implementation -- not exposed in header
//

struct LPClusteringConfig : public LabelPropagationConfig {
  using ClusterID = NodeID;
  using ClusterWeight = BlockWeight;
  static constexpr bool kTrackClusterCount = true;
  static constexpr bool kUseTwoHopClustering = true;
};

class LPClusteringImpl final
    : public ChunkRandomdLabelPropagation<LPClusteringImpl, LPClusteringConfig>,
      public OwnedRelaxedClusterWeightVector<NodeID, NodeWeight>,
      public OwnedClusterVector<NodeID, NodeID>,
      public Clusterer {
  SET_DEBUG(false);

  using Base = ChunkRandomdLabelPropagation<LPClusteringImpl, LPClusteringConfig>;
  using ClusterWeightBase = OwnedRelaxedClusterWeightVector<NodeID, NodeWeight>;
  using ClusterBase = OwnedClusterVector<NodeID, NodeID>;

public:
  LPClusteringImpl(const NodeID max_n, const CoarseningContext &c_ctx)
      : ClusterWeightBase{max_n},
        ClusterBase{max_n},
        _c_ctx{c_ctx} {
    allocate(max_n, max_n);
    set_max_degree(c_ctx.lp.large_degree_threshold);
    set_max_num_neighbors(c_ctx.lp.max_num_neighbors);
    set_use_two_phases(c_ctx.lp.use_two_phases);
  }

  void set_max_cluster_weight(const NodeWeight max_cluster_weight) final {
    _max_cluster_weight = max_cluster_weight;
  }

  const AtomicClusterArray &compute_clustering(const Graph &graph) final {
    START_HEAP_PROFILER("Initialization");
    initialize(&graph, graph.n());
    STOP_HEAP_PROFILER();

    for (std::size_t iteration = 0; iteration < _c_ctx.lp.num_iterations; ++iteration) {
      SCOPED_HEAP_PROFILER("Iteration", std::to_string(iteration));
      SCOPED_TIMER("Iteration", std::to_string(iteration));
      if (perform_iteration() == 0) {
        break;
      }
    }

    if (_c_ctx.lp.isolated_nodes_strategy == IsolatedNodesClusteringStrategy::MATCH) {
      SCOPED_HEAP_PROFILER("Handle isolated nodes");
      SCOPED_TIMER("Handle isolated nodes");
      handle_isolated_nodes<true>();
    } else if (_c_ctx.lp.isolated_nodes_strategy == IsolatedNodesClusteringStrategy::CLUSTER) {
      SCOPED_HEAP_PROFILER("Handle isolated nodes");
      SCOPED_TIMER("Handle isolated nodes");
      handle_isolated_nodes<false>();
    }

    if (_c_ctx.lp.use_two_hop_clustering(_graph->n(), _current_num_clusters)) {
      if (_c_ctx.lp.isolated_nodes_strategy ==
          IsolatedNodesClusteringStrategy::MATCH_DURING_TWO_HOP) {
        SCOPED_HEAP_PROFILER("Handle isolated nodes");
        SCOPED_TIMER("Handle isolated nodes");
        LOG << "match";
        handle_isolated_nodes<true>();
      } else if (_c_ctx.lp.isolated_nodes_strategy == IsolatedNodesClusteringStrategy::CLUSTER_DURING_TWO_HOP) {
        SCOPED_HEAP_PROFILER("Handle isolated nodes");
        SCOPED_TIMER("Handle isolated nodes");
        handle_isolated_nodes<false>();
      }

      SCOPED_HEAP_PROFILER("2-hop Clustering");
      SCOPED_TIMER("2-hop clustering");
      perform_two_hop_clustering();
    }

    return clusters();
  }

public:
  [[nodiscard]] NodeID initial_cluster(const NodeID u) {
    return u;
  }

  [[nodiscard]] NodeWeight initial_cluster_weight(const NodeID cluster) {
    return _graph->node_weight(cluster);
  }

  [[nodiscard]] NodeWeight max_cluster_weight(const NodeID /* cluster */) {
    return _max_cluster_weight;
  }

  [[nodiscard]] bool accept_cluster(const Base::ClusterSelectionState &state) {
    return (state.current_gain > state.best_gain ||
            (state.current_gain == state.best_gain && state.local_rand.random_bool())) &&
           (state.current_cluster_weight + state.u_weight <=
                max_cluster_weight(state.current_cluster) ||
            state.current_cluster == state.initial_cluster);
  }

  using Base::_current_num_clusters;
  using Base::_graph;

  const CoarseningContext &_c_ctx;
  NodeWeight _max_cluster_weight{kInvalidBlockWeight};
};

//
// Exposed wrapper
//

LPClustering::LPClustering(const NodeID max_n, const CoarseningContext &c_ctx)
    : _core{std::make_unique<LPClusteringImpl>(max_n, c_ctx)} {}

// we must declare the destructor explicitly here, otherwise, it is implicitly
// generated before LabelPropagationClusterCore is complete
LPClustering::~LPClustering() = default;

void LPClustering::set_max_cluster_weight(const NodeWeight max_cluster_weight) {
  _core->set_max_cluster_weight(max_cluster_weight);
}

void LPClustering::set_desired_cluster_count(const NodeID count) {
  _core->set_desired_num_clusters(count);
}

const Clusterer::AtomicClusterArray &LPClustering::compute_clustering(const Graph &graph) {
  return _core->compute_clustering(graph);
}
} // namespace kaminpar::shm
