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
#include "dkaminpar/refinement/distributed_refiner.h"
#include "kaminpar/algorithm/parallel_label_propagation.h"

namespace dkaminpar {
struct DistributedLabelPropagationRefinerConfig : public shm::LabelPropagationConfig {
  using RatingMap = shm::RatingMap<EdgeWeight, shm::FastResetArray<EdgeWeight>>;
  using Graph = DistributedGraph;
  using ClusterID = BlockID;
  using ClusterWeight = BlockWeight;
  static constexpr bool kTrackClusterCount = false;
  static constexpr bool kUseTwoHopClustering = false;
};

class DistributedLabelPropagationRefiner final
    : public shm::InOrderLabelPropagation<DistributedLabelPropagationRefiner, DistributedLabelPropagationRefinerConfig>,
      public DistributedRefiner {
  using Base = shm::InOrderLabelPropagation<DistributedLabelPropagationRefiner,
                                            DistributedLabelPropagationRefinerConfig>;
  SET_DEBUG(true);

public:
  explicit DistributedLabelPropagationRefiner(const Context &ctx)
      : Base{ctx.partition.local_n()},
        _lp_ctx{ctx.refinement.lp},
        _next_partition(ctx.partition.local_n()),
        _gains(ctx.partition.local_n()),
        _block_weights(ctx.partition.k) {}

  void initialize(const DistributedGraph & /* graph */, const PartitionContext &p_ctx) final;

  void refine(DistributedPartitionedGraph &p_graph) final;

private:
  void process_chunk(NodeID from, NodeID to);

  bool perform_moves(NodeID from, NodeID to, const std::vector<BlockWeight> &residual_block_weights,
                     const std::vector<EdgeWeight> &total_gains_to_block);

  void synchronize_state(NodeID from, NodeID to);

public:
  void init_cluster(const NodeID u, const BlockID b) { _next_partition[u] = b; }

  [[nodiscard]] BlockID cluster(const NodeID u) const { return _next_partition[u]; }

  void move_node(const NodeID u, const BlockID b) { _next_partition[u] = b; }

  [[nodiscard]] BlockWeight initial_cluster_weight(const BlockID b) const { return _p_graph->block_weight(b); }

  [[nodiscard]] BlockWeight cluster_weight(const BlockID b) const { return _block_weights[b]; }

  void init_cluster_weight(const BlockID b, const BlockWeight weight) { _block_weights[b] = weight; }

  [[nodiscard]] BlockWeight max_cluster_weight(const BlockID b) const { return _p_ctx->max_block_weight(b); }

  [[nodiscard]] bool move_cluster_weight(const BlockID from, const BlockID to, const BlockWeight delta,
                                         const BlockWeight max_weight) {
    if (_block_weights[to] + delta <= max_weight) {
      _block_weights[to] += delta;
      _block_weights[from] -= delta;
      return true;
    }
    return false;
  }

  [[nodiscard]] bool accept_cluster(const ClusterSelectionState &state) {
    const bool accept = (state.current_gain > state.best_gain ||
                         (state.current_gain == state.best_gain && state.local_rand.random_bool())) &&
                        (state.current_cluster_weight + state.u_weight < max_cluster_weight(state.current_cluster) ||
                         state.current_cluster == state.initial_cluster);
    if (accept) { _gains[state.u] = state.current_gain; }
    return accept;
  }

  [[nodiscard]] bool activate_neighbor(const NodeID u) const { return u < _p_graph->n(); }

private:
#ifdef KAMINPAR_ENABLE_HEAVY_ASSERTIONS
  bool ASSERT_NEXT_PARTITION_STATE();
#endif

  const LabelPropagationRefinementContext &_lp_ctx;

  DistributedPartitionedGraph *_p_graph{nullptr};
  const PartitionContext *_p_ctx{nullptr};

  scalable_vector<BlockID> _next_partition;
  scalable_vector<EdgeWeight> _gains;
  scalable_vector<shm::parallel::IntegralAtomicWrapper<BlockWeight>> _block_weights;
};
} // namespace dkaminpar