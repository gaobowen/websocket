
#ifndef GWS_COLLECTOR_H
#define GWS_COLLECTOR_H

#include <tbb/concurrent_hash_map.h>

#include "SockHandle.h"

namespace GWS {


typedef typename tbb::concurrent_hash_map<void*, std::shared_ptr<SockHandle>>::accessor SockAccessor;
typedef typename tbb::concurrent_hash_map<void*, std::shared_ptr<SockHandle>>::const_accessor SockReadAccessor;

typedef typename std::pair<void*, std::shared_ptr<SockHandle>> SockPair;


//Garbage Collector
extern tbb::concurrent_hash_map<void*, std::shared_ptr<SockHandle>> collector;




}


#endif





