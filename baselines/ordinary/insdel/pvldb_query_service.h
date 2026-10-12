#pragma once
// Shared scalar-query service for the isolated ESA/ALENEX baselines.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#elif defined(__linux__)
#include <sys/resource.h>
#endif
namespace gated_service {
using I=int64_t;
using Clock=std::chrono::steady_clock;
inline I elapsed(Clock::time_point t){return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-t).count();}
// OS lifetime high-water mark; includes parsed input, upstream state and service.
// Queried once after processing, outside all update/query timing intervals.
inline I peak_working_set_bytes(){
#ifdef _WIN32
 using Reader=BOOL(WINAPI*)(HANDLE,PPROCESS_MEMORY_COUNTERS,DWORD);
 auto reader=reinterpret_cast<Reader>(GetProcAddress(GetModuleHandleA("kernel32.dll"),"K32GetProcessMemoryInfo"));
 PROCESS_MEMORY_COUNTERS counters{};counters.cb=sizeof(counters);
 return reader&&reader(GetCurrentProcess(),&counters,sizeof(counters))?I(counters.PeakWorkingSetSize):-1;
#elif defined(__linux__)
 rusage usage{};return getrusage(RUSAGE_SELF,&usage)==0?I(usage.ru_maxrss)*1024:-1;
#else
 return -1;
#endif
}
struct GraphInfo {using NodeType=int;using StreamMember=std::pair<int,int>;size_t n,changes;size_t currentNumNodes()const{return n;}size_t currentNumEdges()const{return changes;}};
struct Event {char op;I id;int u,v;};
struct Input {GraphInfo graph;std::vector<Event> events;};
inline Input read_input(const std::string& path){
 std::ifstream in(path);size_t n;if(!(in>>n)||!n)throw std::runtime_error("invalid input n");
 std::vector<Event> events;Event e;int k;
 while(in>>e.op>>e.id>>k){if(k!=2||(e.op!='+'&&e.op!='-'))throw std::runtime_error("only graph +/- updates supported");if(!(in>>e.u>>e.v))throw std::runtime_error("incomplete update");if(e.u<0||e.v<0||size_t(e.u)>=n||size_t(e.v)>=n||e.u==e.v)throw std::runtime_error("invalid endpoint");if(e.u>e.v)std::swap(e.u,e.v);events.push_back(e);}
 return {{n,events.size()},std::move(events)};
}
struct Epsilon {I p,q;};
inline Epsilon epsilon_ratio(const std::string& text){
 if(text.empty()||text.find_first_not_of("0123456789.")!=std::string::npos)throw std::runtime_error("epsilon must be a nonnegative decimal");
 I p=0,q=1;bool decimal=false;int digits=0;
 for(char c:text){if(c=='.'){if(decimal)throw std::runtime_error("bad epsilon");decimal=true;continue;}if(++digits>8)throw std::runtime_error("epsilon too precise");p=p*10+c-'0';if(decimal)q*=10;}
 if(p<=0||p>q)throw std::runtime_error("epsilon must be in (0,1]");I g=std::gcd(p,q);return {p/g,q/g};
}
struct Config {size_t cadence=1;int b=1,theta=0;double lambda=.1;Epsilon eps{1,10};std::string mode="combined",dump;bool verify=false,gate=true;};
struct Witness {I edges=0;std::vector<int> nodes;};
inline bool better(const Witness& a,I b_edges,size_t b_size){return !a.nodes.empty() && (b_size==0 || (__int128)a.edges*b_size>(__int128)b_edges*a.nodes.size());}
struct Query {bool skipped=false,certified=false;I time_ns=0;double before_density=0,sweep_density=-1,peel_density=-1,native_density=-1;std::string winner="cached";};
class Service {
public:
 int n,b;Epsilon eps;std::vector<int> load;
 std::map<int,int> histogram;
 std::map<I,std::pair<int,int>> edges;
 std::set<std::pair<int,int>> seen_pairs;
 std::vector<unsigned char> member;
 std::vector<int> witness;
 I induced=0;
 explicit Service(int n_,int b_,Epsilon e):n(n_),b(b_),eps(e),load(n,0),member(n,0){histogram[0]=n;}
 int max_units()const{return histogram.rbegin()->first;}
 double upper()const{return double(max_units())/b;}
 double lower()const{return witness.empty()?0.0:double(induced)/witness.size();}
 bool certificate()const{
  if(max_units()==0)return true;
  if(witness.empty())return false;
  return (__int128)eps.q*max_units()*witness.size()<=(__int128)(eps.q+eps.p)*b*induced;
 }
 void apply_graph(const Event&e,bool verify){
  std::pair<int,int> uv{e.u,e.v};
  if(e.op=='+'){
   if(verify&&(edges.contains(e.id)||seen_pairs.contains(uv)))throw std::runtime_error("duplicate insertion");
   edges[e.id]=uv;if(verify)seen_pairs.insert(uv);
   if(member[e.u]&&member[e.v])++induced;
  }else{
   if(verify&&(!edges.contains(e.id)||edges.at(e.id)!=uv))throw std::runtime_error("invalid deletion");
   edges.erase(e.id);if(verify)seen_pairs.erase(uv);
   if(member[e.u]&&member[e.v])--induced;
  }
 }
 template<class A> void synchronize(const A&a){
  for(int v:a.changed_vertices()){
   int value=a.degree_units(v);if(value<0)throw std::runtime_error("negative orientation load");
   if(value==load[v])continue;
   auto old=histogram.find(load[v]);if(old==histogram.end())throw std::runtime_error("histogram missing old degree");
   if(--old->second==0)histogram.erase(old);
   ++histogram[value];load[v]=value;
  }
 }
 Witness sweep()const{
  std::vector<int> order(n),rank(n);std::vector<I> inc(n,0);std::iota(order.begin(),order.end(),0);
  std::sort(order.begin(),order.end(),[&](int u,int v){return load[u]!=load[v]?load[u]>load[v]:u<v;});
  for(int i=0;i<n;++i)rank[order[i]]=i;
  for(const auto& [id,e]:edges)++inc[std::max(rank[e.first],rank[e.second])];
  I m=0,best_m=0;int best_size=0;
  for(int i=0;i<n;++i){m+=inc[i];if(best_size==0||(__int128)m*best_size>(__int128)best_m*(i+1)){best_m=m;best_size=i+1;}}
  if(best_m==0)return {};order.resize(best_size);return {best_m,std::move(order)};
 }
 Witness peel()const{
  std::vector<std::vector<int>> adj(n);std::vector<int> degree(n,0),order;order.reserve(n);
  for(const auto& [id,e]:edges){adj[e.first].push_back(e.second);adj[e.second].push_back(e.first);++degree[e.first];++degree[e.second];}
  std::priority_queue<std::pair<int,int>,std::vector<std::pair<int,int>>,std::greater<std::pair<int,int>>> heap;
  for(int v=0;v<n;++v)heap.push({degree[v],v});
  std::vector<unsigned char> alive(n,1);I m=edges.size(),best_m=m;int best_removed=0;
  for(int removed=0;removed<n;++removed){
   while(!alive[heap.top().second]||degree[heap.top().second]!=heap.top().first)heap.pop();
   auto [d,v]=heap.top();heap.pop();alive[v]=0;order.push_back(v);m-=d;
   for(int u:adj[v])if(alive[u]){--degree[u];heap.push({degree[u],u});}
   if(removed+1<n && (__int128)m*(n-best_removed)>(__int128)best_m*(n-removed-1)){best_m=m;best_removed=removed+1;}
  }
  if(best_m==0)return {};
  return {best_m,std::vector<int>(order.begin()+best_removed,order.end())};
 }
 void replace(Witness candidate){
  for(int v:witness)member[v]=0;
  witness=std::move(candidate.nodes);induced=candidate.edges;
  for(int v:witness)member[v]=1;
 }
 template<class A> Query query(const Config& config,const A& algorithm){
  const auto start=Clock::now();Query q;q.before_density=lower();
  if(config.gate&&certificate()){q.skipped=true;q.certified=true;q.time_ns=elapsed(start);return q;}
  // No vertex enumeration occurs on the cached scalar-return path above.
  Witness candidate;std::string winner="cached";
  if(config.mode=="native"||config.mode=="combined"){
   auto native=algorithm.native_witness();
   q.native_density=native.nodes.empty()?0.0:double(native.edges)/native.nodes.size();
   if(better(native,induced,witness.size())){candidate=std::move(native);winner="native";}
  }
  if(config.mode=="combined"||config.mode=="sweep"){
   auto s=sweep();q.sweep_density=s.nodes.empty()?0.0:double(s.edges)/s.nodes.size();
   if((candidate.nodes.empty()&&better(s,induced,witness.size()))||(!candidate.nodes.empty()&&better(s,candidate.edges,candidate.nodes.size()))){candidate=std::move(s);winner="sweep";}
  }
  if(config.mode=="combined"||config.mode=="peel"){
   auto p=peel();q.peel_density=p.nodes.empty()?0.0:double(p.edges)/p.nodes.size();
   if((candidate.nodes.empty()&&better(p,induced,witness.size()))||(!candidate.nodes.empty()&&better(p,candidate.edges,candidate.nodes.size()))){candidate=std::move(p);winner="peel";}
  }
  if(!candidate.nodes.empty())replace(std::move(candidate));
  q.winner=winner;q.certified=certificate();q.time_ns=elapsed(start);return q;
 }
 template<class A>void validate(const A&a)const{
  std::map<std::pair<int,int>,double> mass;std::vector<double> actual(n,0.0);
  a.visit_fractional_assignments([&](int u,int v,double amount){if(amount<0)throw std::runtime_error("negative assignment");mass[{std::min(u,v),std::max(u,v)}]+=amount;actual[u]+=amount;});
  if(mass.size()!=edges.size())throw std::runtime_error("assignment count mismatch");
  I real_induced=0;for(const auto& [id,e]:edges){if(std::abs(mass[e]-1.0)>1e-8)throw std::runtime_error("allocation not one");if(member[e.first]&&member[e.second])++real_induced;}
  if(real_induced!=induced)throw std::runtime_error("cached witness count mismatch");
  std::map<int,int> rebuilt;
  for(int v=0;v<n;++v){if(a.degree_units(v)!=load[v]||std::abs(actual[v]-double(load[v])/b)>1e-8)throw std::runtime_error("load notification/assignment mismatch");++rebuilt[load[v]];}
  if(rebuilt!=histogram)throw std::runtime_error("max-load histogram mismatch");
  if(lower()>upper()+1e-8)throw std::runtime_error("witness exceeds feasible upper");
 }
};

template<class A>int run(const Input& input,const std::string& output,const Config& config){
 if(!config.cadence||(config.mode!="combined"&&config.mode!="sweep"&&config.mode!="peel"&&config.mode!="native"))throw std::runtime_error("bad cadence/extractor mode");
 auto init_start=Clock::now();
 std::map<std::string,double> dp{{"lambda",config.lambda}};std::map<std::string,int64_t> ip{{"b",config.b},{"theta",config.theta}};
 std::ostringstream construction_log;auto* old=std::cout.rdbuf(construction_log.rdbuf());A a(input.graph,dp,ip);std::cout.rdbuf(old);
 Service service(input.graph.n,config.b,config.eps);a.begin_update();const I initialization_ns=elapsed(init_start);
 std::ofstream out(output),updates(output+".updates.csv"),state;
 if(!out||!updates)throw std::runtime_error("cannot create outputs");if(!config.dump.empty()&&config.dump!="-")state.open(config.dump);
 out<<std::setprecision(17)<<"step,n,m,witness_density,upper_bound,certified_ratio,certificate_pass,witness_size,query_skipped,query_ns,sweep_density,peeling_density,native_density,before_density,winner,cumulative_queries,cumulative_skips,skip_rate\n";
 updates<<std::setprecision(17)<<"step,op,upstream_update_ns,algorithm_update_ns,aux_update_ns,service_update_ns,query_ns,total_service_ns,is_query,query_skipped,certificate_pass,m,cached_witness_density,upper_bound,changed_vertices\n";
 state<<std::setprecision(17);
 I queries=0,skips=0,certified=0,total_upstream=0,total_aux=0,total_service=0,total_query=0;
 for(size_t t=0;t<input.events.size();++t){
  const Event&e=input.events[t];const auto service_start=Clock::now();
  auto aux_start=Clock::now();a.begin_update();service.apply_graph(e,config.verify);I aux=elapsed(aux_start);
  auto update_start=Clock::now();if(e.op=='+')a.handle_insertion({e.u,e.v});else a.handle_deletion({e.u,e.v});const I upstream=elapsed(update_start);
  aux_start=Clock::now();service.synchronize(a);aux+=elapsed(aux_start);const I service_time=elapsed(service_start);
  total_upstream+=upstream;total_aux+=aux;total_service+=service_time;
  const bool is_query=(t+1)%config.cadence==0||t+1==input.events.size();Query q;
  if(is_query){q=service.query(config,a);++queries;skips+=q.skipped;certified+=q.certified;total_query+=q.time_ns;
   out<<t<<','<<input.graph.n<<','<<service.edges.size()<<','<<service.lower()<<','<<service.upper()<<','<<(service.upper()>0?service.lower()/service.upper():1.0)<<','<<q.certified<<','<<service.witness.size()<<','<<q.skipped<<','<<q.time_ns<<','<<q.sweep_density<<','<<q.peel_density<<','<<q.native_density<<','<<q.before_density<<','<<q.winner<<','<<queries<<','<<skips<<','<<double(skips)/queries<<'\n';
   if(state){state<<"{\"step\":"<<t<<",\"witness\":[";for(size_t j=0;j<service.witness.size();++j){if(j)state<<',';state<<service.witness[j];}state<<"],\"loads\":[";for(int v=0;v<service.n;++v){if(v)state<<',';state<<double(service.load[v])/service.b;}state<<"]}\n";}
  }
  if(config.verify)service.validate(a); // intentionally outside all service timers
  updates<<t<<','<<e.op<<','<<upstream<<','<<upstream<<','<<aux<<','<<service_time<<','<<q.time_ns<<','<<service_time+q.time_ns<<','<<is_query<<','<<(is_query?int(q.skipped):-1)<<','<<service.certificate()<<','<<service.edges.size()<<','<<service.lower()<<','<<service.upper()<<','<<a.changed_vertices().size()<<'\n';
 }
 std::ofstream summary(output+".summary.json");
 summary<<std::setprecision(17)<<"{\n  \"updates\": "<<input.events.size()<<",\n  \"queries\": "<<queries<<",\n  \"query_skips\": "<<skips<<",\n  \"query_skip_rate\": "<<(queries?double(skips)/queries:0)<<",\n  \"certified_queries\": "<<certified<<",\n  \"uncertified_queries\": "<<queries-certified<<",\n  \"initialization_ns\": "<<initialization_ns<<",\n  \"upstream_update_ns\": "<<total_upstream<<",\n  \"aux_update_ns\": "<<total_aux<<",\n  \"service_update_ns\": "<<total_service<<",\n  \"query_ns\": "<<total_query<<",\n  \"epsilon_numerator\": "<<config.eps.p<<",\n  \"epsilon_denominator\": "<<config.eps.q<<",\n  \"extractor\": \""<<config.mode<<"\",\n  \"gating\": "<<(config.gate?"true":"false")<<",\n  \"validation\": "<<(config.verify?"true":"false")<<",\n  \"scalar_query\": true,\n  \"enumeration_timed\": false,\n  \"peak_working_set_bytes\": "<<peak_working_set_bytes()<<",\n  \"memory_scope\": \"process lifetime including preloaded input, upstream and query service\"\n}\n";
 std::cerr<<"updates="<<input.events.size()<<" queries="<<queries<<" skipped="<<skips<<" certified="<<certified<<" uncertified="<<queries-certified<<'\n';
 return 0;
}
}
