#pragma once
// Shared CAP and ablation kernel; positive incidences and compressed integer shares.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
using I = int64_t;
enum class MechanismPolicy { CAP, Tight, NoReserve, NoRefresh, Unit };

#ifdef HYPER_SUPPORT_DIAGNOSTIC
struct HyperSupportMetrics {
 I P=0,J=0,expanded_edges=0,searches=0,failed=0,successful=0;
 I hmax=0,Qmax=0,path_edges=0,path_length_max=0;
 I path_lookup_calls=0,path_lookup_comparisons=0,path_lookup_rank_sum=0;
 I internal_path_rank_max=0,terminal_path_rank_max=0;
 I fallback_calls=0,fallback_P=0,fallback_edges=0,fallback_J=0;
 I witness_clears=0,witness_marks=0;
 __int128 sum_hQ=0,sum_endpoint_bound=0;
};
#define HYPER_DIAG(action) do { action; } while(false)
#else
#define HYPER_DIAG(action) do {} while(false)
#endif

struct Edge {std::vector<int> vs, pos, residual_pos; std::vector<I> x; int active_pos=-1; I peel_stamp=0,search_stamp=0,witness_stamp=0;};
struct Metrics {I bfs=0, scans=0, augment=0, moved=0, witnesses=0, peels=0, endpoint_scans=0, path_edges=0, path_transport=0;};
class CapDS {
public:
 int n, rank, ep, eq, mode; I B, cap=0, sm=0, debt=0, peel_epoch=0; int ssize=0;
 std::vector<Edge> edges;
 std::vector<std::vector<int>> adj;
 std::vector<std::vector<std::pair<int,int>>> residual;
 std::vector<I> load;
 std::vector<int> heap,hpos;
 std::vector<int> active, witness, mark, parent, pedge, pslot;
 std::vector<I> seen;
 I epoch=0,witness_epoch=0;
 Metrics mt;
 MechanismPolicy policy=MechanismPolicy::CAP;
 bool observer_enabled=false;
 uint64_t observer_epoch=0;
 std::vector<uint64_t> observer_vertex_seen, observer_edge_seen;
 std::vector<int> observer_vertices, observer_edges;
 void enable_observer(){observer_enabled=true;observer_vertex_seen.assign(n,0);}
 void begin_update(){mt={};if(observer_enabled){++observer_epoch;observer_vertices.clear();observer_edges.clear();}}
 void note_vertex(int v){if(!observer_enabled)return;if(observer_vertex_seen[v]!=observer_epoch){observer_vertex_seen[v]=observer_epoch;observer_vertices.push_back(v);}}
 void note_edge(int id){if(!observer_enabled)return;if(observer_edge_seen.size()<edges.size())observer_edge_seen.resize(edges.size());if(observer_edge_seen[id]!=observer_epoch){observer_edge_seen[id]=observer_epoch;observer_edges.push_back(id);}}


#ifdef HYPER_SUPPORT_DIAGNOSTIC
 HyperSupportMetrics hyper_stats;
 void hyper_finish_search(I h,I maximum,I startP,I startJ,I startE,bool success) {
  I P=hyper_stats.P-startP,J=hyper_stats.J-startJ,E=hyper_stats.expanded_edges-startE;
  __int128 pbound=__int128(h)*maximum,jbound=__int128(std::min<I>(rank,h))*E+(success?1:0);
  if(h<1 || P<0 || E<0 || E>P || __int128(P)>pbound || __int128(J)>jbound)
   throw std::runtime_error("hypergraph positive-support search bound");
  hyper_stats.searches++;hyper_stats.successful+=success;hyper_stats.failed+=!success;
  hyper_stats.hmax=std::max(hyper_stats.hmax,h);hyper_stats.Qmax=std::max(hyper_stats.Qmax,maximum);
  hyper_stats.sum_hQ+=pbound;hyper_stats.sum_endpoint_bound+=jbound;
 }
#endif

 CapDS(int n_,int r,int p,int q,I b,int mode_=0):n(n_),rank(r),ep(p),eq(q),mode(mode_),B(b),adj(n),residual(n),load(n),heap(n),hpos(n),mark(n),parent(n),pedge(n),pslot(n),seen(n) {
  if(p<=0||q<=0||p>q||r<2||(mode!=0&&mode!=1&&mode!=4&&mode!=5)||B< (2LL*r*q+p-1)/p) throw std::runtime_error("invalid reserve precision/rank/mode or B too small");
  for(int v=0;v<n;v++){heap[v]=n-1-v;hpos[n-1-v]=v;}
 }
 void set_share(int id,int slot,I value) {
  auto &e=edges[id];int v=e.vs[slot];I old=e.x[slot];if(old!=value){note_edge(id);note_vertex(v);}
  if(old==0 && value>0){e.residual_pos[slot]=(int)residual[v].size();residual[v].push_back({id,slot});}
  if(old>0 && value==0){
   int at=e.residual_pos[slot];auto last=residual[v].back();
   residual[v][at]=last;residual[v].pop_back();
   edges[last.first].residual_pos[last.second]=at;e.residual_pos[slot]=-1;
  }
  e.x[slot]=value;
 }
 bool greater(int u,int v)const{return load[u]!=load[v]?load[u]>load[v]:u>v;}
 void swap_heap(int a,int b){std::swap(heap[a],heap[b]);hpos[heap[a]]=a;hpos[heap[b]]=b;}
 void change(int v,I d) {
  if(!d)return;note_vertex(v);load[v]+=d;int i=hpos[v];
  if(d>0){while(i>0){int p=(i-1)/2;if(!greater(heap[i],heap[p]))break;swap_heap(i,p);i=p;}}
  else{while(2*i+1<n){int j=2*i+1;if(j+1<n&&greater(heap[j+1],heap[j]))j++;if(!greater(heap[j],heap[i]))break;swap_heap(i,j);i=j;}}
 }
 I max_load()const{return load[heap[0]];}
 I reserve_target()const{if(policy==MechanismPolicy::Tight||policy==MechanismPolicy::NoReserve)return cap;return ssize?I(((__int128(2)*eq+ep)*B*sm)/(__int128(2)*eq*ssize)):0;}
 bool contained(const Edge&e)const {for(int v:e.vs)if(!mark[v])return false;return true;}
 void set_witness(const std::vector<int>& vs,I known_edges=-1) {
  HYPER_DIAG(hyper_stats.witness_clears+=(I)witness.size();hyper_stats.witness_marks+=(I)vs.size());
  for(int v:witness)mark[v]=0;
  witness=vs;ssize=(int)vs.size();
  for(int v:witness)mark[v]=1;
  sm=known_edges<0?0:known_edges;
  // A separate witness epoch counts each encountered edge once. Every
  // contained edge has a positive endpoint in the witness. No zero-share
  // prefix or full adjacency scan is needed; containment stops at first exit.
  if(known_edges<0) {
   ++witness_epoch;HYPER_DIAG(hyper_stats.fallback_calls++);
   for(int v:witness)for(auto incidence:residual[v]) {
    HYPER_DIAG(hyper_stats.fallback_P++);
    auto&e=edges[incidence.first];if(e.witness_stamp==witness_epoch)continue;
    e.witness_stamp=witness_epoch;HYPER_DIAG(hyper_stats.fallback_edges++);
    bool internal=true;
    for(int u:e.vs){HYPER_DIAG(hyper_stats.fallback_J++);if(!mark[u]){internal=false;break;}}
    if(internal)sm++;
   }
  }
  mt.witnesses++;
 }
 void peel_refresh() {
  mt.peels++;debt=0;
  std::vector<int>deg(n),removed(n),sequence;sequence.reserve(n);peel_epoch++;
  std::priority_queue<std::pair<int,int>,std::vector<std::pair<int,int>>,std::greater<std::pair<int,int>>>pq;
  for(int v=0;v<n;v++){deg[v]=(int)adj[v].size();pq.emplace(deg[v],v);}
  I m=(I)active.size(),bestm=sm;int left=n,bestn=ssize,bestcut=-1;
  if(left>0 && (bestn==0||m*bestn>bestm*left)){bestm=m;bestn=left;bestcut=0;}
  while(left){auto [d,v]=pq.top();pq.pop();if(removed[v]||d!=deg[v])continue;removed[v]=1;sequence.push_back(v);left--;
   for(int id:adj[v])if(edges[id].peel_stamp!=peel_epoch){edges[id].peel_stamp=peel_epoch;m--;for(int u:edges[id].vs)if(!removed[u]){deg[u]--;pq.emplace(deg[u],u);}}
   if(left>0&&(bestn==0||m*bestn>bestm*left)){bestm=m;bestn=left;bestcut=(int)sequence.size();}
  }
  if(bestcut>=0){std::vector<int>vs(sequence.begin()+bestcut,sequence.end());set_witness(vs,bestm);capacity(true);}
 }
 void capacity(bool raise=false) {
  if(active.empty()){set_witness({});cap=0;return;}
  // A surviving edge gives a universal nonzero witness of density >= 1/rank.
  if(ssize==0 || sm*rank<ssize)set_witness(edges[active.back()].vs);
  __int128 value=((__int128(eq)+ep)*B*sm)/(__int128(eq)*ssize);
  if(value>std::numeric_limits<I>::max())throw std::runtime_error("capacity exceeds int64");
  I outer=I(value);
  if(mode&4)cap=I((__int128(B)*sm+ssize-1)/ssize);
  else if(mode&2){I inner=I(((__int128(2)*eq+ep)*B*sm)/(__int128(2)*eq*ssize));if(cap==0||cap>outer)cap=inner;else if(raise)cap=std::max(cap,inner);}
  else cap=outer;
 }
 void insert(int id,std::vector<int> vs) {
  HYPER_DIAG(hyper_stats={});
  if(id>=(int)edges.size())edges.resize(id+1);
  if(edges[id].active_pos>=0)throw std::runtime_error("duplicate insertion");
  std::sort(vs.begin(),vs.end());
  if((int)vs.size()>rank||vs.empty()||std::adjacent_find(vs.begin(),vs.end())!=vs.end())throw std::runtime_error("bad rank");
  Edge&e=edges[id];e.vs=vs;e.x.assign(vs.size(),0);e.residual_pos.assign(vs.size(),-1);e.pos.clear();
  for(int v:vs){if(v<0||v>=n)throw std::runtime_error("bad vertex");e.pos.push_back((int)adj[v].size());adj[v].push_back(id);}
  e.active_pos=(int)active.size();active.push_back(id);
  if(contained(e))sm++;
  // Batched discrete water filling, without explicitly making B parallel edges.
  I lo=load[vs[0]],hi=lo+B;
  for(int v:vs)lo=std::min(lo,load[v]);
  hi=lo+B;
  while(lo<hi){I mid=(lo+hi)/2,used=0;for(int v:vs)used+=std::max<I>(0,mid-load[v]);if(used>=B)hi=mid;else lo=mid+1;}
  I rem=B;
  for(size_t j=0;j<vs.size();j++){I take=std::max<I>(0,lo-1-load[vs[j]]);e.x[j]=take;rem-=take;}
  for(size_t j=0;j<vs.size()&&rem;j++)if(load[vs[j]]+e.x[j]<lo){e.x[j]++;rem--;}
  if(rem)throw std::runtime_error("waterfill error");
  for(size_t j=0;j<vs.size();j++){I value=e.x[j];e.x[j]=0;set_share(id,(int)j,value);change(vs[j],value);}
  capacity();repair();
 }
 void erase(int id) {
  HYPER_DIAG(hyper_stats={});
  if(id>=(int)edges.size()||edges[id].active_pos<0)throw std::runtime_error("invalid deletion");
  Edge&e=edges[id];if(contained(e))sm--;
  for(size_t j=0;j<e.vs.size();j++){
   int v=e.vs[j];change(v,-e.x[j]);set_share(id,(int)j,0);
   int other=adj[v].back(),idx=e.pos[j];adj[v][idx]=other;adj[v].pop_back();
   if(other!=id){auto&oe=edges[other];auto it=std::lower_bound(oe.vs.begin(),oe.vs.end(),v);oe.pos[it-oe.vs.begin()]=idx;}
  }
  int last=active.back();active[e.active_pos]=last;edges[last].active_pos=e.active_pos;active.pop_back();e.active_pos=-1;
  capacity();repair();
 }
 void repair() {
  bool refreshed=false;
  while(max_load()>cap) {
   if((mode&1) && !refreshed && debt>4*(n+(I)rank*(I)active.size())){peel_refresh();refreshed=true;if(max_load()<=cap)break;}
   I target=reserve_target();int root=heap[0];std::vector<int> queue{root};seen[root]=++epoch;parent[root]=-1;int dest=-1;
   mt.bfs++;
#ifdef HYPER_SUPPORT_DIAGNOSTIC
   I hyper_h=0;for(int v=0;v<n;v++)if(__int128(B)*adj[v].size()>=target)++hyper_h;
   const I hyper_Q=max_load(),hyper_P0=hyper_stats.P,hyper_J0=hyper_stats.J,hyper_E0=hyper_stats.expanded_edges;
#endif
   for(size_t h=0;h<queue.size()&&dest<0;h++){
    int u=queue[h];
    for(auto incidence:residual[u]){
     int id=incidence.first,j=incidence.second;
     mt.scans++;debt++;HYPER_DIAG(hyper_stats.P++);auto&e=edges[id];if(e.search_stamp==epoch)continue;
     e.search_stamp=epoch;HYPER_DIAG(hyper_stats.expanded_edges++);
     for(int v:e.vs){++mt.endpoint_scans;HYPER_DIAG(hyper_stats.J++);if(seen[v]!=epoch){seen[v]=epoch;parent[v]=u;pedge[v]=id;pslot[v]=j;queue.push_back(v);if(load[v]<target){dest=v;break;}}}
     if(dest>=0)break;
    }
   }
   if(dest<0){HYPER_DIAG(hyper_finish_search(hyper_h,hyper_Q,hyper_P0,hyper_J0,hyper_E0,false));I oldcap=cap,total=0;for(int v:queue)total+=load[v];if(total%B)throw std::runtime_error("nonintegral closed-set edge count");I newm=total/B;if(__int128(newm)*ssize>__int128(sm)*queue.size())set_witness(queue,newm);capacity(true);if(cap<oldcap || reserve_target()<=target)throw std::runtime_error("reserve thresholds failed to progress");continue;}
   HYPER_DIAG(hyper_finish_search(hyper_h,hyper_Q,hyper_P0,hyper_J0,hyper_E0,true));
   I amount=std::min(load[root]-cap,cap-load[dest]);
#ifdef HYPER_SUPPORT_DIAGNOSTIC
   I hyper_length=0;
#endif
   I path_length=0;for(int v=dest;v!=root;v=parent[v]){++path_length;HYPER_DIAG(++hyper_length);amount=std::min(amount,edges[pedge[v]].x[pslot[v]]);}
   if(policy==MechanismPolicy::Unit)amount=std::min<I>(amount,1);
   mt.path_edges+=path_length;mt.path_transport+=path_length*amount;
   HYPER_DIAG(if(hyper_length>hyper_h)throw std::runtime_error("hypergraph path/support bound");hyper_stats.path_edges+=hyper_length;hyper_stats.path_length_max=std::max(hyper_stats.path_length_max,hyper_length));
   if(amount<=0)throw std::runtime_error("nonpositive augmentation");
   for(int v=dest;v!=root;v=parent[v]){auto&e=edges[pedge[v]];
#ifdef HYPER_SUPPORT_DIAGNOSTIC
    hyper_stats.path_lookup_calls++;hyper_stats.path_lookup_rank_sum+=(I)e.vs.size();
    if(v==dest)hyper_stats.terminal_path_rank_max=std::max<I>(hyper_stats.terminal_path_rank_max,e.vs.size());
    else {hyper_stats.internal_path_rank_max=std::max<I>(hyper_stats.internal_path_rank_max,e.vs.size());if((I)e.vs.size()>hyper_h)throw std::runtime_error("nonterminal path edge rank exceeds support");}
    int j=(int)(std::lower_bound(e.vs.begin(),e.vs.end(),v,[&](int a,int b){hyper_stats.path_lookup_comparisons++;return a<b;})-e.vs.begin());
#else
    int j=(int)(std::lower_bound(e.vs.begin(),e.vs.end(),v)-e.vs.begin());
#endif
    set_share(pedge[v],pslot[v],e.x[pslot[v]]-amount);set_share(pedge[v],j,e.x[j]+amount);}

   change(root,-amount);change(dest,amount);mt.augment++;mt.moved+=amount;
  }
 }
 void check()const {
  std::vector<I> actual(n);I cnt=0;
  for(int id:active){const auto&e=edges[id];I sum=0;for(size_t j=0;j<e.vs.size();j++){if(e.x[j]<0)throw std::runtime_error("negative share");actual[e.vs[j]]+=e.x[j];sum+=e.x[j];}if(sum!=B)throw std::runtime_error("edge sum");if(contained(e))cnt++;}
  for(int v=0;v<n;v++)for(size_t at=0;at<residual[v].size();at++){
   auto [id,j]=residual[v][at];const auto&e=edges[id];
   if(e.active_pos<0||e.vs[j]!=v||e.x[j]<=0||e.residual_pos[j]!=(int)at)throw std::runtime_error("residual index");
  }
  for(int id:active){const auto&e=edges[id];for(size_t j=0;j<e.vs.size();j++){
   int at=e.residual_pos[j];if((e.x[j]>0)!=(at>=0))throw std::runtime_error("residual membership");
   if(at>=0 && residual[e.vs[j]][at]!=std::make_pair(id,(int)j))throw std::runtime_error("residual backpointer");
  }}
  if(actual!=load||cnt!=sm)throw std::runtime_error("accounting");
  if(!active.empty()&&__int128(max_load())*eq*ssize>(__int128(eq)+ep)*B*sm)throw std::runtime_error("certificate");
  if(max_load()!=*std::max_element(load.begin(),load.end()))throw std::runtime_error("heap maximum");
 }
 double density()const{return ssize?double(sm)/ssize:0;}
 double upper()const{return double(max_load())/B;}
};
