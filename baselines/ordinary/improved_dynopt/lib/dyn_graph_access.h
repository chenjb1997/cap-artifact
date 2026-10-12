#pragma once
// Portable shell only: upstream ImprovedDynOpt reads n at construction and
// writes edges only in end(). Its actual dynamic adjacency lives in the solver.
// The query driver never calls end(); new_edge is preserved for completeness.
#include <vector>
#include <utility>
#include "definitions.h"
class dyn_graph_access {
  NodeID n_;
  std::vector<std::pair<NodeID,NodeID>> exported_edges_;
 public:
  explicit dyn_graph_access(NodeID n):n_(n){}
  NodeID number_of_nodes() const{return n_;}
  bool new_edge(NodeID u,NodeID v){exported_edges_.push_back({u,v});return true;}
};
