// Positive-incidence CAP driver for ordinary graphs.
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
struct Edge {std::vector<int> vs, pos, residual_pos; std::vector<I> x; int active_pos=-1; I peel_stamp=0,search_stamp=0;};
struct Metrics {I bfs=0, scans=0, augment=0, moved=0, witnesses=0, peels=0;};
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
 I epoch=0;
 Metrics mt;
 CapDS(int n_,int r,int p,int q,I b,int mode_=0):n(n_),rank(r),ep(p),eq(q),mode(mode_),B(b),adj(n),residual(n),load(n),heap(n),hpos(n),mark(n),parent(n),pedge(n),pslot(n),seen(n) {
  if(p<=0||q<=0||p>q||r<2||mode<0||mode>1||B< (2LL*r*q+p-1)/p) throw std::runtime_error("invalid reserve precision/rank/mode or B too small");
  for(int v=0;v<n;v++){heap[v]=n-1-v;hpos[n-1-v]=v;}
 }
 void set_share(int id,int slot,I value) {
  auto &e=edges[id];int v=e.vs[slot];I old=e.x[slot];
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
  if(!d)return;load[v]+=d;int i=hpos[v];
  if(d>0){while(i>0){int p=(i-1)/2;if(!greater(heap[i],heap[p]))break;swap_heap(i,p);i=p;}}
  else{while(2*i+1<n){int j=2*i+1;if(j+1<n&&greater(heap[j+1],heap[j]))j++;if(!greater(heap[j],heap[i]))break;swap_heap(i,j);i=j;}}
 }
 I max_load()const{return load[heap[0]];}
 I reserve_target()const{return ssize?I(((__int128(2)*eq+ep)*B*sm)/(__int128(2)*eq*ssize)):0;}
 bool contained(const Edge&e)const {for(int v:e.vs)if(!mark[v])return false;return true;}
 void set_witness(const std::vector<int>& vs,I known_edges=-1) {
  for(int v:witness)mark[v]=0;
  witness=vs;ssize=(int)vs.size();
  for(int v:witness)mark[v]=1;
  sm=known_edges<0?0:known_edges;
  // Every internal edge has a positive endpoint. Count at its first such slot.
  // This also avoids scanning zero-residual incidences during fallback.
  if(known_edges<0)for(int v:witness)for(auto incidence:residual[v]) {
   const auto&e=edges[incidence.first];int slot=incidence.second;bool first=true;
   for(int j=0;j<slot;j++)if(e.x[j]>0){first=false;break;}
   if(first&&contained(e))sm++;
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
   for(size_t h=0;h<queue.size()&&dest<0;h++){
    int u=queue[h];
    for(auto incidence:residual[u]){
     int id=incidence.first,j=incidence.second;
     mt.scans++;debt++;auto&e=edges[id];if(e.search_stamp==epoch)continue;
     e.search_stamp=epoch;
     for(int v:e.vs)if(seen[v]!=epoch){seen[v]=epoch;parent[v]=u;pedge[v]=id;pslot[v]=j;queue.push_back(v);if(load[v]<target){dest=v;break;}}
     if(dest>=0)break;
    }
   }
   if(dest<0){I oldcap=cap,total=0;for(int v:queue)total+=load[v];if(total%B)throw std::runtime_error("nonintegral closed-set edge count");I newm=total/B;if(__int128(newm)*ssize>__int128(sm)*queue.size())set_witness(queue,newm);capacity(true);if(cap<oldcap || reserve_target()<=target)throw std::runtime_error("reserve thresholds failed to progress");continue;}
   I amount=std::min(load[root]-cap,cap-load[dest]);
   for(int v=dest;v!=root;v=parent[v])amount=std::min(amount,edges[pedge[v]].x[pslot[v]]);
   if(amount<=0)throw std::runtime_error("nonpositive augmentation");
   for(int v=dest;v!=root;v=parent[v]){auto&e=edges[pedge[v]];int j=(int)(std::lower_bound(e.vs.begin(),e.vs.end(),v)-e.vs.begin());set_share(pedge[v],pslot[v],e.x[pslot[v]]-amount);set_share(pedge[v],j,e.x[j]+amount);}
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
int main(int argc,char**argv){try{
 if(argc<3){std::cerr<<"cap_graph UPDATES OUTPUT.csv [epsilon_numerator=1] [epsilon_denominator=10] [rank=2] [B=auto] [check=0] [primal_refresh=0]\n";return 1;}
 int p=argc>3?std::stoi(argv[3]):1,q=argc>4?std::stoi(argv[4]):10,r=argc>5?std::stoi(argv[5]):2;
 int mode=argc>8?std::stoi(argv[8]):0;
 if(p<=0||q<=0||p>q||r<2||mode<0||mode>1)throw std::runtime_error("invalid parameter");
 I b=argc>6?std::stoll(argv[6]):(2LL*r*q+p-1)/p;bool verify=argc>7?std::stoi(argv[7]):false;
 std::ifstream in(argv[1]);std::ofstream out(argv[2]);if(!in||!out)throw std::runtime_error("file open");int n;if(!(in>>n)||n<=0)throw std::runtime_error("invalid n");CapDS ds(n,r,p,q,b,mode);
 out<<"step,op,update_us,density,upper,witness_size,edges,bfs,scans,augment,moved,witness_changes,cap,peels\n";
 int step=0,id,k;char op;out<<std::setprecision(12);
 while(in>>op>>id>>k){if(id<0||k<1||k>r)throw std::runtime_error("invalid edge id/rank");std::vector<int>vs(k);for(int&v:vs)if(!(in>>v))throw std::runtime_error("truncated input");ds.mt={};
  auto t0=std::chrono::steady_clock::now();if(op=='+')ds.insert(id,vs);else if(op=='-')ds.erase(id);else throw std::runtime_error("bad operation");auto t1=std::chrono::steady_clock::now();
  double us=std::chrono::duration<double,std::micro>(t1-t0).count();if(verify)ds.check();
  out<<step++<<','<<op<<','<<us<<','<<ds.density()<<','<<ds.upper()<<','<<ds.ssize<<','<<ds.active.size()<<','<<ds.mt.bfs<<','<<ds.mt.scans<<','<<ds.mt.augment<<','<<ds.mt.moved<<','<<ds.mt.witnesses<<','<<ds.cap<<','<<ds.mt.peels<<'\n';
 }
 std::cerr<<"updates="<<step<<" lower="<<ds.density()<<" upper="<<ds.upper()<<" B="<<b<<'\n';
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 2;}}
