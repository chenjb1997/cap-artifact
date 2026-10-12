#pragma once
#include <cstdint>
#ifndef HB_PROFILE
#define HB_PROFILE 0
#endif
struct HyperBaselineCounters {
    uint64_t insert_copies=0,delete_copies=0,flips=0;
    uint64_t tight_calls=0,tight_budget=0,tight_scans=0,tight_endpoint_visits=0;
    uint64_t notify_calls=0,notify_budget=0,notify_scans=0,notify_endpoint_visits=0,notify_rekeys=0;
    uint64_t temporary_hash_inserts=0,edge_vector_copies=0,copied_vertices=0;
};
inline HyperBaselineCounters hb_counters;
#if HB_PROFILE
#define HB_ADD(field,value) (hb_counters.field += (value))
#else
#define HB_ADD(field,value) ((void)0)
#endif
