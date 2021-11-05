/*******************************************************************************
 * @file:   kway.cc
 *
 * @author: Daniel Seemaier
 * @date:   25.10.2021
 * @brief:  Direct k-way partitioning.
 ******************************************************************************/
#include "dkaminpar/partitioning_scheme/kway.h"

#include "dkaminpar/algorithm/allgather_graph.h"
#include "dkaminpar/coarsening/global_clustering_contraction_redistribution.h"
#include "dkaminpar/coarsening/locking_label_propagation_clustering.h"
#include "dkaminpar/refinement/distributed_probabilistic_label_propagation_refiner.h"
#include "dkaminpar/utility/distributed_metrics.h"
#include "kaminpar/metrics.h"
#include "kaminpar/partitioning_scheme/partitioning.h"
#include "kaminpar/utility/timer.h"

namespace dkaminpar {
SET_DEBUG(true);

KWayPartitioningScheme::KWayPartitioningScheme(const DistributedGraph &graph, const Context &ctx)
    : _graph{graph}, _ctx{ctx} {}

DistributedPartitionedGraph KWayPartitioningScheme::partition() {
  // Coarsen graph
  std::vector<DistributedGraph> graph_hierarchy;
  std::vector<coarsening::GlobalMapping> mapping_hierarchy;

  const DistributedGraph *c_graph = &_graph;
  while (c_graph->n() > _ctx.partition.k * _ctx.coarsening.contraction_limit) {
    SCOPED_TIMER("Coarsening");

    const NodeWeight max_cluster_weight = shm::compute_max_cluster_weight(
        c_graph->global_n(), c_graph->total_node_weight(), _ctx.initial_partitioning.sequential.partition,
        _ctx.initial_partitioning.sequential.coarsening);

    LockingLpClustering coarsener(_ctx);

    auto &clustering = coarsener.compute_clustering(*c_graph, max_cluster_weight);
    auto [contracted_graph, mapping] = coarsening::contract_global_clustering_redistribute(*c_graph, clustering);
    HEAVY_ASSERT(graph::debug::validate(contracted_graph));

    const bool converged = contracted_graph.global_n() == c_graph->global_n();
    graph_hierarchy.push_back(std::move(contracted_graph));
    mapping_hierarchy.push_back(std::move(mapping));
    c_graph = &graph_hierarchy.back();

    LOG << "=> n=" << c_graph->global_n() << " m=" << c_graph->global_m()
        << " max_node_weight=" << c_graph->max_node_weight() << " max_cluster_weight=" << max_cluster_weight;
    graph::print_verbose_stats(*c_graph);
    if (converged) {
      LOG << "==> Coarsening converged";
      break;
    }
  }

  // initial partitioning
  auto shm_graph = graph::allgather(*c_graph);

  auto shm_p_graph = TIMED_SCOPE("Initial partitioning") {
    auto shm_ctx = _ctx.initial_partitioning.sequential;
    shm_ctx.refinement.lp.num_iterations = 1;
    shm_ctx.partition.k = _ctx.partition.k;
    shm_ctx.partition.epsilon = _ctx.partition.epsilon;
    shm_ctx.setup(shm_graph);

    DISABLE_TIMERS();
    shm::Logger::set_quiet_mode(true);
    auto p_graph = shm::partitioning::partition(shm_graph, shm_ctx);
    shm::Logger::set_quiet_mode(_ctx.quiet);
    ENABLE_TIMERS();

    SLOG << "Obtained " << shm_ctx.partition.k << "-way partition with cut=" << shm::metrics::edge_cut(p_graph)
         << " and imbalance=" << shm::metrics::imbalance(p_graph);

    return p_graph;
  };

  DistributedPartitionedGraph dist_p_graph = graph::reduce_scatter(*c_graph, std::move(shm_p_graph));
  HEAVY_ASSERT(graph::debug::validate_partition(dist_p_graph));

  const auto initial_cut = metrics::edge_cut(dist_p_graph);
  const auto initial_imbalance = metrics::imbalance(dist_p_graph);

  LOG << "Initial partition: cut=" << initial_cut << " imbalance=" << initial_imbalance;

  auto refine = [&](DistributedPartitionedGraph &p_graph) {
    SCOPED_TIMER("Refinement");
    if (_ctx.refinement.algorithm == KWayRefinementAlgorithm::NOOP) {
      return;
    }

    DistributedProbabilisticLabelPropagationRefiner refiner(_ctx);
    refiner.initialize(p_graph.graph(), _ctx.partition);
    refiner.refine(p_graph);
    HEAVY_ASSERT(graph::debug::validate_partition(p_graph));
  };

  // Uncoarsen and refine
  while (!graph_hierarchy.empty()) {
    SCOPED_TIMER("Uncoarsening");

    {
      SCOPED_TIMER("Uncontraction");

      const auto *current_graph = graph_hierarchy.size() <= 1 ? &_graph : &graph_hierarchy[graph_hierarchy.size() - 2];
      HEAVY_ASSERT(graph::debug::validate(*current_graph));

      dist_p_graph = coarsening::project_global_contracted_graph(*current_graph, std::move(dist_p_graph),
                                                                 mapping_hierarchy.back());
      HEAVY_ASSERT(graph::debug::validate_partition(dist_p_graph));

      graph_hierarchy.pop_back();
      mapping_hierarchy.pop_back();

      // update graph ptr in case graph_hierarchy was reallocated by the pop_back() operation
      dist_p_graph.UNSAFE_set_graph(graph_hierarchy.empty() ? &_graph : &graph_hierarchy.back());
    }

    refine(dist_p_graph);

    const auto current_cut = metrics::edge_cut(dist_p_graph);
    const auto current_imbalance = metrics::imbalance(dist_p_graph);

    LOG << "Cut after LP: cut=" << current_cut << " imbalance=" << current_imbalance;
  }

  return dist_p_graph;
}
} // namespace dkaminpar