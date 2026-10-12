#pragma once
// Untimed quality observer. Its graph and witness counts are derived from input,
// independently of the kernel's active/adjacency/witness-count data structures.
// Touched-share hooks schedule verification only; they never change decisions.
#include <set>
#include <memory>

struct IndependentObserver {
 struct OEdge {
  const std::vector<int>* vertices=nullptr;
  std::vector<size_t> positions;
  int active_position=-1;
  uint64_t seen=0;
 };
 int n;
 std::vector<OEdge> edges;
 std::vector<int> active;
 std::vector<std::vector<std::pair<int,int>>> adjacency;
 std::vector<unsigned char> membership;
 std::vector<uint64_t> wanted;
 std::vector<int> witness;
 std::vector<I> verified_load;
 std::multiset<I> ordered_loads;
 I induced=0, checked_updates=0, full_checks=0, exact_checks=0;
 I checked_edges=0, checked_vertices=0, witness_incidence_inspections=0;
 uint64_t witness_epoch=0;
 explicit IndependentObserver(int size):n(size),adjacency(size),membership(size),wanted(size),verified_load(size) {
  for(int v=0;v<n;++v)ordered_loads.insert(0);
 }
 bool inside(const std::vector<int>& vs)const {
  for(int v:vs)if(!membership[v])return false;
  return true;
 }
 void apply_input(char op,int id,const std::vector<int>& vs) {
  if(id<0)throw std::runtime_error("observer negative ID");
  if(size_t(id)>=edges.size())edges.resize(size_t(id)+1);
  auto&e=edges[id];
  if(op=='+') {
   if(e.active_position>=0)throw std::runtime_error("observer reused active ID");
   e.vertices=&vs;e.positions.clear();
   for(size_t j=0;j<vs.size();++j){e.positions.push_back(adjacency[vs[j]].size());adjacency[vs[j]].push_back({id,int(j)});}
   e.active_position=int(active.size());active.push_back(id);
   if(inside(vs))++induced;
  } else {
   if(e.active_position<0||*e.vertices!=vs)throw std::runtime_error("observer invalid deletion");
   if(inside(vs))--induced;
   for(size_t j=0;j<vs.size();++j) {
    auto& entries=adjacency[vs[j]];size_t at=e.positions[j];auto last=entries.back();
    entries[at]=last;entries.pop_back();edges[last.first].positions[last.second]=at;
   }
   int last=active.back();active[e.active_position]=last;edges[last].active_position=e.active_position;
   active.pop_back();e.active_position=-1;
  }
 }
 void update_witness(const CapDS& ds) {
  ++witness_epoch;
  for(int v:ds.witness) {
   if(v<0||v>=n||wanted[v]==witness_epoch)throw std::runtime_error("observer bad/duplicate witness vertex");
   wanted[v]=witness_epoch;
  }
  std::vector<int> changed, affected;
  for(int v:witness)if(wanted[v]!=witness_epoch)changed.push_back(v);
  for(int v:ds.witness)if(!membership[v])changed.push_back(v);
  for(int v:changed)for(auto [id,slot]:adjacency[v]) {
   ++witness_incidence_inspections;
   if(edges[id].seen!=witness_epoch){edges[id].seen=witness_epoch;affected.push_back(id);}
  }
  for(int id:affected)if(inside(*edges[id].vertices))--induced;
  for(int v:changed)membership[v]=(wanted[v]==witness_epoch);
  for(int id:affected)if(inside(*edges[id].vertices))++induced;
  witness=ds.witness;
 }
 void verify_edge(const CapDS& ds,int id) {
  if(id<0||size_t(id)>=edges.size()||size_t(id)>=ds.edges.size())throw std::runtime_error("observer missing edge");
  const auto& expected=edges[id];const auto& actual=ds.edges[id];
  if(!expected.vertices||actual.vs!=*expected.vertices)throw std::runtime_error("observer endpoint mismatch");
  if((expected.active_position>=0)!=(actual.active_pos>=0))throw std::runtime_error("observer active state mismatch");
  if(actual.x.size()!=actual.vs.size())throw std::runtime_error("observer share shape mismatch");
  __int128 total=0;
  for(I share:actual.x){if(share<0)throw std::runtime_error("observer negative share");total+=share;}
  if(total!=(expected.active_position>=0?ds.B:0))throw std::runtime_error("observer edge mass mismatch");
  ++checked_edges;
 }
 void verify_vertex(const CapDS&ds,int v) {
  __int128 expected=0;
  for(auto [id,slot]:adjacency[v])expected+=ds.edges[id].x[slot];
  if(expected!=ds.load[v])throw std::runtime_error("observer vertex load mismatch");
  auto old=ordered_loads.find(verified_load[v]);
  if(old==ordered_loads.end())throw std::runtime_error("observer load multiset corruption");
  ordered_loads.erase(old);verified_load[v]=ds.load[v];ordered_loads.insert(ds.load[v]);++checked_vertices;
 }
 void after_update(const CapDS&ds) {
  if(ds.mt.witnesses)update_witness(ds);
  // Membership itself changes only through set_witness, observed above. Avoid
  // comparing O(|C|) vertices on every update; full checks compare contents.
  if(ds.witness.size()!=size_t(ds.ssize)||witness.size()!=ds.witness.size()||induced!=ds.sm||active.size()!=ds.active.size())throw std::runtime_error("observer witness/count mismatch");
  for(int id:ds.observer_edges)verify_edge(ds,id);
  for(int v:ds.observer_vertices)verify_vertex(ds,v);
  if(*ordered_loads.rbegin()!=ds.max_load())throw std::runtime_error("observer maximum mismatch");
  if(active.empty()) {
   if(induced||ds.ssize||ds.max_load())throw std::runtime_error("observer nonempty answer on empty graph");
  } else {
   if(ds.ssize<1||induced<1)throw std::runtime_error("observer empty nonempty-graph witness");
   if(__int128(ds.max_load())*ds.eq*ds.ssize>(__int128(ds.eq)+ds.ep)*ds.B*induced)throw std::runtime_error("observer approximation certificate failed");
  }
  ++checked_updates;
 }
 void full_check(const CapDS&ds) {
  if(witness!=ds.witness)throw std::runtime_error("observer witness membership mismatch");
  I direct=0;
  for(int id:active){verify_edge(ds,id);if(inside(*edges[id].vertices))++direct;}
  if(direct!=induced)throw std::runtime_error("observer independent induced-count check failed");
  for(int v=0;v<n;++v)verify_vertex(ds,v);
  ds.check();++full_checks;
 }
 void exhaustive_check(const CapDS&ds) {
  if(n>16)throw std::runtime_error("check=2 is restricted to n<=16 tiny streams");
  I best_edges=0;int best_size=1;
  std::vector<unsigned> masks;for(int id:active){unsigned mask=0;for(int v:*edges[id].vertices)mask|=1u<<v;masks.push_back(mask);}
  for(unsigned set=1;set<(1u<<n);++set) {
   I count=0;for(unsigned edge:masks)count+=((set&edge)==edge);
   int size=__builtin_popcount(set);if(__int128(count)*best_size>__int128(best_edges)*size){best_edges=count;best_size=size;}
  }
  if(!active.empty()&&__int128(ds.sm)*best_size*(ds.eq+ds.ep)<__int128(best_edges)*ds.ssize*ds.eq)throw std::runtime_error("exhaustive density guarantee failed");
  ++exact_checks;
 }
 void write(std::ostream&out)const {
  out<<"{\"checked_updates\":"<<checked_updates<<",\"full_checks\":"<<full_checks<<",\"exhaustive_checks\":"<<exact_checks<<",\"checked_edges\":"<<checked_edges<<",\"checked_vertices\":"<<checked_vertices<<",\"witness_incidence_inspections\":"<<witness_incidence_inspections<<'}';
 }
};
