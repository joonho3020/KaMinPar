/*******************************************************************************
 * Factory functions to instantiate partitioning composed based on their
 * respective enum constant.
 *
 * @file:   factories.cc
 * @author: Daniel Seemaier
 * @date:   21.09.2021
 ******************************************************************************/
#include "kaminpar-shm/factories.h"

#include <memory>

// Partitioning schemes
#include "kaminpar-shm/partitioning/deep/deep_multilevel.h"
#include "kaminpar-shm/partitioning/kway/kway_multilevel.h"
#include "kaminpar-shm/partitioning/rb/rb_multilevel.h"

// Clusterings
#include "kaminpar-shm/coarsening/cluster_coarsener.h"
#include "kaminpar-shm/coarsening/lp_clustering.h"

// Coarsening
#include "kaminpar-shm/coarsening/noop_coarsener.h"

// Refinement
#include "kaminpar-shm/refinement/balancer/greedy_balancer.h"
#include "kaminpar-shm/refinement/fm/fm_refiner.h"
#include "kaminpar-shm/refinement/jet/jet_refiner.h"
#include "kaminpar-shm/refinement/lp/lp_refiner.h"
#include "kaminpar-shm/refinement/mtkahypar_refiner.h"
#include "kaminpar-shm/refinement/multi_refiner.h"

// Gain cache strategies for the FM algorithm
#include "kaminpar-shm/refinement/gains/dense_gain_cache.h"
#include "kaminpar-shm/refinement/gains/hybrid_gain_cache.h"
#include "kaminpar-shm/refinement/gains/on_the_fly_gain_cache.h"

namespace kaminpar::shm::factory {
SET_DEBUG(true);

std::unique_ptr<Partitioner> create_partitioner(const Graph &graph, const Context &ctx) {
  SCOPED_HEAP_PROFILER("Create partitioner");

  switch (ctx.partitioning.mode) {
  case PartitioningMode::DEEP: {
    return std::make_unique<DeepMultilevelPartitioner>(graph, ctx);
  }

  case PartitioningMode::RB: {
    return std::make_unique<RBMultilevelPartitioner>(graph, ctx);
  }

  case PartitioningMode::KWAY: {
    return std::make_unique<KWayMultilevelPartitioner>(graph, ctx);
  }
  }

  __builtin_unreachable();
}

std::unique_ptr<Coarsener> create_coarsener(const Graph &graph, const CoarseningContext &c_ctx) {
  SCOPED_HEAP_PROFILER("Coarsener allocation");
  SCOPED_TIMER("Allocation");

  switch (c_ctx.algorithm) {
  case ClusteringAlgorithm::NOOP: {
    return std::make_unique<NoopCoarsener>();
  }

  case ClusteringAlgorithm::LABEL_PROPAGATION: {
    auto clustering_algorithm = std::make_unique<LPClustering>(graph.n(), c_ctx);
    auto coarsener =
        std::make_unique<ClusteringCoarsener>(std::move(clustering_algorithm), graph, c_ctx);
    return coarsener;
  }
  }

  __builtin_unreachable();
}

namespace {
std::unique_ptr<Refiner> create_refiner(const Context &ctx, const RefinementAlgorithm algorithm) {
  switch (algorithm) {
  case RefinementAlgorithm::NOOP:
    return std::make_unique<NoopRefiner>();

  case RefinementAlgorithm::LABEL_PROPAGATION:
    return std::make_unique<LabelPropagationRefiner>(ctx);

  case RefinementAlgorithm::GREEDY_BALANCER:
    return std::make_unique<GreedyBalancer>(ctx);

  case RefinementAlgorithm::KWAY_FM: {
    if (ctx.refinement.kway_fm.gain_cache_strategy == GainCacheStrategy::DENSE) {
      return std::make_unique<FMRefiner<fm::DefaultDeltaPartitionedGraph, fm::DenseGainCache>>(ctx);
    } else if (ctx.refinement.kway_fm.gain_cache_strategy == GainCacheStrategy::ON_THE_FLY) {
      return std::make_unique<FMRefiner<fm::DefaultDeltaPartitionedGraph, fm::OnTheFlyGainCache>>(
          ctx
      );
    } else if (ctx.refinement.kway_fm.gain_cache_strategy == GainCacheStrategy::HYBRID) {
      return std::make_unique<FMRefiner<fm::DefaultDeltaPartitionedGraph, fm::HighDegreeGainCache>>(
          ctx
      );
    }
    __builtin_unreachable();
  }

  case RefinementAlgorithm::JET:
    return std::make_unique<JetRefiner>(ctx);

  case RefinementAlgorithm::MTKAHYPAR:
    return std::make_unique<MtKaHyParRefiner>(ctx);
  }

  __builtin_unreachable();
}
} // namespace

std::unique_ptr<Refiner> create_refiner(const Context &ctx) {
  SCOPED_HEAP_PROFILER("Refiner Allocation");
  SCOPED_TIMER("Allocation");

  if (ctx.refinement.algorithms.empty()) {
    return std::make_unique<NoopRefiner>();
  }
  if (ctx.refinement.algorithms.size() == 1) {
    return create_refiner(ctx, ctx.refinement.algorithms.front());
  }

  std::unordered_map<RefinementAlgorithm, std::unique_ptr<Refiner>> refiners;
  for (const RefinementAlgorithm algorithm : ctx.refinement.algorithms) {
    if (refiners.find(algorithm) == refiners.end()) {
      refiners[algorithm] = create_refiner(ctx, algorithm);
    }
  }

  return std::make_unique<MultiRefiner>(std::move(refiners), ctx.refinement.algorithms);
}
} // namespace kaminpar::shm::factory
