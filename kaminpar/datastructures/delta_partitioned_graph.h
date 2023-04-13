/*******************************************************************************
 * @file:   delta_partitioned_graph.h
 * @author: Daniel Seemaier
 * @date:   15.03.2023
 * @brief:  Stores changes to a static partitioned graph.
 ******************************************************************************/
#pragma once

#include <google/dense_hash_map>

#include "kaminpar/datastructures/graph.h"
#include "kaminpar/datastructures/partitioned_graph.h"
#include "kaminpar/definitions.h"

#include "common/ranges.h"

namespace kaminpar::shm {
class DeltaPartitionedGraph : public GraphDelegate {
public:
  DeltaPartitionedGraph(const PartitionedGraph *p_graph)
      : GraphDelegate(&p_graph->graph()),
        _p_graph(p_graph) {
    //_block_weights_delta.set_empty_key(kInvalidBlockID);
    //_partition_delta.set_empty_key(kInvalidNodeID);
  }

  [[nodiscard]] const PartitionedGraph &p_graph() const {
    return *_p_graph;
  }

  [[nodiscard]] inline BlockID k() const {
    return _p_graph->k();
  }

  [[nodiscard]] inline IotaRange<BlockID> blocks() const {
    return _p_graph->blocks();
  }

  template <typename Lambda> inline void pfor_blocks(Lambda &&lambda) const {
    tbb::parallel_for(
        static_cast<BlockID>(0), k(), std::forward<Lambda>(lambda)
    );
  }

  [[nodiscard]] inline BlockID block(const NodeID node) const {
    const auto it = _partition_delta.find(node);
    if (it != _partition_delta.end()) {
      return it->second;
    }

    return _p_graph->block(node);
  }

  template <bool update_block_weight = true>
  void set_block(const NodeID node, const BlockID new_block) {
    KASSERT(node < n(), "invalid node id " << node);
    KASSERT(
        new_block < k(),
        "invalid block id " << new_block << " for node " << node
    );

    if constexpr (update_block_weight) {
      const BlockID old_block = block(node);
      KASSERT(old_block < k());

      _block_weights_delta[old_block] -= node_weight(node);
      _block_weights_delta[new_block] += node_weight(node);
    }

    _partition_delta[node] = new_block;
  }

  [[nodiscard]] inline NodeWeight block_weight(const BlockID block) const {
    NodeWeight delta = 0;
    const auto it = _block_weights_delta.find(block);
    if (it != _block_weights_delta.end()) {
      delta = it->second;
    }

    return _p_graph->block_weight(block) + delta;
  }

  const auto &delta() const {
    return _partition_delta;
  }

  void clear() {
    _block_weights_delta.clear();
    _partition_delta.clear();
  }

private:
  const PartitionedGraph *_p_graph;

  // google::dense_hash_map<BlockID, NodeWeight> _block_weights_delta;
  // google::dense_hash_map<NodeID, BlockID> _partition_delta;

  std::unordered_map<BlockID, NodeWeight> _block_weights_delta;
  std::unordered_map<NodeID, BlockID> _partition_delta;
};
} // namespace kaminpar::shm

