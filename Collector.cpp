#include "Collector.h"

namespace GWS {
    tbb::concurrent_hash_map<void*, std::shared_ptr<SockHandle>> collector;
}
