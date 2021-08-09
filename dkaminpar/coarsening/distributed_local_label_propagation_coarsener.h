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
#pragma once

#include "dkaminpar/datastructure/distributed_graph.h"
#include "dkaminpar/distributed_context.h"
#include "kaminpar/algorithm/parallel_label_propagation.h"
#include "kaminpar/datastructure/fast_reset_array.h"
#include "kaminpar/datastructure/rating_map.h"

namespace dkaminpar {
struct DistributedLocalLabelPropagationClusteringConfig : public shm::LabelPropagationConfig {
  using Graph = DistributedGraph;
  using ClusterID = NodeID;
  using ClusterWeight = NodeWeight;
  static constexpr bool kUseHardWeightConstraint = false;
  static constexpr bool kReportEmptyClusters = true;
};

class DistributedLocalLabelPropagationClustering final
    : public shm::LabelPropagation<DistributedLocalLabelPropagationClustering,
                                   DistributedLocalLabelPropagationClusteringConfig> {
  SET_DEBUG(true);

  using Base = shm::LabelPropagation<DistributedLocalLabelPropagationClustering,
                                     DistributedLocalLabelPropagationClusteringConfig>;
  friend Base;

  static constexpr std::size_t kInfiniteIterations{std::numeric_limits<std::size_t>::max()};

public:
  DistributedLocalLabelPropagationClustering(const NodeID max_n, const double shrink_factor,
                                             const LabelPropagationCoarseningContext &lp_ctx)
      : Base{max_n, max_n},
        _shrink_factor{shrink_factor},
        _lp_ctx{lp_ctx},
        _max_cluster_weight{kInvalidBlockWeight} {
    _clustering.resize(max_n);
    _favored_clustering.resize(max_n);
    set_max_degree(lp_ctx.large_degree_threshold);
    set_max_num_neighbors(lp_ctx.max_num_neighbors);
  }

  const scalable_vector<NodeID> &cluster(const DistributedGraph &graph, const NodeWeight max_cluster_weight,
                                          const std::size_t max_iterations = kInfiniteIterations) {
    ASSERT(_clustering.size() >= graph.n());

    initialize(&graph);
    _max_cluster_weight = max_cluster_weight;
    _current_size = graph.n();
    _target_size = static_cast<NodeID>(_shrink_factor * _current_size);
    NodeID total_num_emptied_clusters = 0;

    for (std::size_t iteration = 0; iteration < max_iterations; ++iteration) {
      const auto [num_moved_nodes, num_emptied_clusters] = randomized_iteration();
      _current_size -= num_emptied_clusters;
      total_num_emptied_clusters += num_emptied_clusters;
      if (num_moved_nodes == 0) { break; }
    }

//    if (_lp_ctx.should_merge_nonadjacent_clusters(_graph->n(), _graph->n() - total_num_emptied_clusters)) {
//      DBG << "Empty clusters after LP: " << total_num_emptied_clusters << " of " << _graph->n();
//      join_singleton_clusters_by_favored_cluster(total_num_emptied_clusters);
//    }

    return _clustering;
  }

private:
  void join_singleton_clusters_by_favored_cluster(const NodeID emptied_clusters) {
    const auto desired_no_of_coarse_nodes = _graph->n() * (1.0 - _lp_ctx.merge_nonadjacent_clusters_threshold);
    shm::parallel::IntegralAtomicWrapper<NodeID> current_no_of_coarse_nodes = _graph->n() - emptied_clusters;

    _graph->pfor_nodes([&](const NodeID u) {
      if (current_no_of_coarse_nodes <= desired_no_of_coarse_nodes) { return; }

      const NodeID leader = _clustering[u]; // first && part avoids _cluster_weights cache miss
      const bool singleton = leader == u && _cluster_weights[u] == _graph->node_weight(u);

      if (singleton) {
        NodeID favored_leader = _favored_clustering[u];
        if (_lp_ctx.merge_singleton_clusters && u == favored_leader) { favored_leader = 0; }

        do {
          NodeID expected_value = favored_leader;
          if (_favored_clustering[favored_leader].compare_exchange_strong(expected_value, u)) {
            break; // if this worked, we replaced favored_leader with u
          }

          // if this didn't work, there is another node that has the same favored leader -> try to join that nodes
          // cluster
          const NodeID partner = expected_value;
          if (_favored_clustering[favored_leader].compare_exchange_strong(expected_value, favored_leader)) {
            if (_cluster_weights[partner] + _graph->node_weight(u) < _max_cluster_weight) {
              _clustering[u] = partner;
              _cluster_weights[partner] += _graph->node_weight(u);
              --current_no_of_coarse_nodes;
            }
            break;
          }
        } while (true);
      }
    });
  }

  // used in Base class
  void reset_node_state(const NodeID u) {
    _clustering[u] = u;
    _favored_clustering[u] = u;
  }

  [[nodiscard]] NodeID cluster(const NodeID u) const { return _clustering[u]; }
  void set_cluster(const NodeID u, const NodeID cluster) { _clustering[u] = cluster; }
  void set_favored_cluster(const NodeID u, const NodeID cluster) { _favored_clustering[u] = cluster; }
  [[nodiscard]] NodeID num_clusters() const { return _graph->n(); }
  [[nodiscard]] NodeWeight initial_cluster_weight(const NodeID cluster) const { return _graph->node_weight(cluster); }
  [[nodiscard]] NodeWeight max_cluster_weight(const NodeID) const { return _max_cluster_weight; }

  // do not join clusters of ghost nodes
  [[nodiscard]] bool accept_cluster(const Base::ClusterSelectionState &state) const {
    return _graph->is_owned_node(state.current_cluster) &&
           (state.current_gain > state.best_gain ||
            (state.current_gain == state.best_gain && state.local_rand.random_bool())) &&
           (state.current_cluster_weight + state.u_weight < max_cluster_weight(state.current_cluster) ||
            state.current_cluster == state.initial_cluster);
  }

  [[nodiscard]] bool should_stop(const NodeID num_emptied_clusters) const {
    return _current_size - num_emptied_clusters < _target_size;
  }

  double _shrink_factor;
  const LabelPropagationCoarseningContext &_lp_ctx;
  scalable_vector<NodeID> _clustering;
  scalable_vector<shm::parallel::IntegralAtomicWrapper<NodeID>> _favored_clustering;
  NodeWeight _max_cluster_weight;

  NodeID _current_size{};
  NodeID _target_size{};
};
} // namespace dkaminpar