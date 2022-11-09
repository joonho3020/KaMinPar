/*******************************************************************************
 * @file:   lp_refiner.h
 * @author: Daniel Seemaier
 * @date:   30.09.2021
 * @brief:  Refiner based on label propagation.
 ******************************************************************************/
#pragma once

#include "dkaminpar/context.h"
#include "dkaminpar/datastructures/distributed_graph.h"
#include "dkaminpar/refinement/refiner.h"

namespace kaminpar::dist {
class LPRefiner : public Refiner {
public:
    LPRefiner(const Context& ctx);

    LPRefiner(const LPRefiner&)            = delete;
    LPRefiner& operator=(const LPRefiner&) = delete;
    LPRefiner(LPRefiner&&) noexcept        = default;
    LPRefiner& operator=(LPRefiner&&)      = delete;

    ~LPRefiner();

    void initialize(const DistributedGraph& graph, const PartitionContext& p_ctx) final;
    void refine(DistributedPartitionedGraph& p_graph) final;

private:
    std::unique_ptr<class LPRefinerImpl> _impl;
};
} // namespace kaminpar::dist
