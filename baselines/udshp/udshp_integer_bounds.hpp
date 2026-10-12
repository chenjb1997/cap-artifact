#pragma once
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <ostream>

// Input admission only. Native Count, EdgeIdx, VertexIdx and all algorithms stay int32.
// Degree is the maximum active physical-incident degree over all legal stream prefixes.
// Bounds cover cumulative copy IDs and native four-load arithmetic.
struct UdshpIntegerBounds {
 int64_t insertions,peak_edges,peak_degree,copies;
 void verify()const {
  static_assert(sizeof(int)==4,"This admission proof requires native signed int32");
  constexpr int64_t limit=std::numeric_limits<int>::max();
  if(insertions<0||peak_edges<0||peak_degree<0||copies<1||peak_edges>insertions||peak_degree>peak_edges)
   throw std::runtime_error("Invalid input integer bounds");
  // Final next-ID, not merely the last assigned ID, must fit; -1 is reserved.
  if(insertions>limit/copies)throw std::runtime_error("Native cumulative copy-ID int32 budget exceeded");
  // Both native neighbor-visit budgets compute 4*load before double division by eta=1.
  if(peak_degree>(limit/4)/copies)throw std::runtime_error("Native four-load int32 budget exceeded");
 }
 void write_json(std::ostream&out)const {
  out<<"{\"logical_insertions\":"<<insertions<<",\"peak_active_edges\":"<<peak_edges
     <<",\"peak_incident_degree\":"<<peak_degree<<",\"copies\":"<<copies
     <<",\"cumulative_copy_ids\":"<<insertions*copies<<",\"peak_active_copies\":"<<peak_edges*copies
     <<",\"max_load_upper\":"<<peak_degree*copies<<",\"max_four_load_upper\":"<<4*peak_degree*copies
     <<",\"signed_int_max\":"<<std::numeric_limits<int>::max()<<'}';
 }
};
