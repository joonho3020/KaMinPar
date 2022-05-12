/*******************************************************************************
 * @file:   apps.h
 *
 * @author: Daniel Seemaier
 * @date:   21.09.21
 * @brief:
 ******************************************************************************/
#pragma once

#include "apps/environment.h"
#include "kaminpar/context.h"
#include "kaminpar/definitions.h"
#include "kaminpar/utils/logger.h"

#include <kassert/kassert.hpp>

#if __has_include(<numa.h>)
    #include <numa.h>
#endif // __has_include(<numa.h>)

#include <tbb/global_control.h>

namespace kaminpar {
inline void print_identifier(int argc, char* argv[]) {
    LLOG << "BUILD ";
    LLOG << "commit=" << Environment::GIT_SHA1 << " ";
    LLOG << "date='" << __DATE__ << "' ";
    LLOG << "time=" << __TIME__ << " ";
    LLOG << "hostname='" << Environment::HOSTNAME << "' ";
    LOG;

    LLOG << "MACROS ";
    LLOG << "KASSERT_ASSERTION_LEVEL=" << KASSERT_ASSERTION_LEVEL << " ";
    LLOG << "ASSERTION_LEVEL_ALWAYS=" << ASSERTION_LEVEL_ALWAYS << " ";
    LLOG << "ASSERTION_LEVEL_LIGHT=" << ASSERTION_LEVEL_LIGHT << " ";
    LLOG << "ASSERTION_LEVEL_NORMAL=" << ASSERTION_LEVEL_NORMAL << " ";
    LLOG << "ASSERTION_LEVEL_HEAVY=" << ASSERTION_LEVEL_HEAVY << " ";
    LLOG << "KAMINPAR_ENABLE_STATISTICS=" << DETECT_EXIST(KAMINPAR_ENABLE_STATISTICS) << " ";
    LLOG << "KAMINPAR_64BIT_EDGE_IDS=" << DETECT_EXIST(KAMINPAR_64BIT_EDGE_IDS) << " ";
    LLOG << "KAMINPAR_64BIT_NODE_IDS=" << DETECT_EXIST(KAMINPAR_64BIT_NODE_IDS) << " ";
    LLOG << "KAMINPAR_64BIT_WEIGHT=" << DETECT_EXIST(KAMINPAR_64BIT_WEIGHTS) << " ";
    LLOG << "KAMINPAR_ENABLE_BACKWARD_CPP=" << DETECT_EXIST(KAMINPAR_ENABLE_BACKWARD_CPP) << " ";
    LOG;

    LOG << "MODIFIED files={" << Environment::GIT_MODIFIED_FILES << "}";

    LLOG << "ARGS ";
    for (int i = 0; i < argc; ++i) {
        LLOG << "argv[" << i << "]='" << argv[i] << "' ";
    }
    LOG;

#if KASSERT_ENABLED(ASSERTION_LEVEL_NORMAL)
    LOG << std::string(80, '*');
    LOG << "!!! RUNNING WITH ASSERTIONS !!!";
    LOG << std::string(80, '*');
#endif
}

inline tbb::global_control init_parallelism(const std::size_t num_threads) {
    return tbb::global_control{tbb::global_control::max_allowed_parallelism, num_threads};
}

inline void init_numa() {
#if __has_include(<numa.h>)
    if (numa_available() >= 0) {
        numa_set_interleave_mask(numa_all_nodes_ptr);
        LOG << "NUMA using round-robin allocations";
        return;
    }
#endif // __has_include(<numa.h>)
    LOG << "NUMA not available";
}
} // namespace kaminpar
