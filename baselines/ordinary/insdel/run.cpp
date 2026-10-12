// Official PVLDB 2024 INS/DEL with explicit arbitrary-stream input lifecycle.
#include "insdel.h"
#include "pvldb_query_service.h"
using namespace gated_service;

struct PVLDBInsDel {
 pvldb2024::Graph graph;
 int n;
 template<class D,class J> PVLDBInsDel(const GraphInfo& info,const D&,const J&):n(int(info.n)){
  graph.resize(n-1);graph.enable_notifications(n);
 }
 void begin_update(){graph.begin_update();}
 const std::vector<int>& changed_vertices()const{return graph.gated_touched;}
 int degree_units(int v)const{return v<int(graph.rd.size())?graph.rd[v]:0;}
 void handle_insertion(std::pair<int,int> edge){
  int u=edge.first,v=edge.second;
  // The official batch driver preloads all endpoints. Arbitrary update streams
  // can reactivate isolated vertices or exceed the storage shrunk by delet().
  if(std::max(u,v)>graph.sz)graph.resize(std::max(u,v));
  graph.node.insert(u);graph.node.insert(v);
  graph.insert(u,v);
 }
 void handle_deletion(std::pair<int,int> edge){graph.delet(edge.first,edge.second);}
 Witness native_witness()const{
  if(graph.PA==0)return {};
  // D_top consists of all predecessors of maximum-indegree vertices.
  // Recompute on a failed query gate; INS/DEL do not maintain this set.
  std::vector<unsigned char> seen(n,0);std::vector<int> queue;
  for(int v:graph.node)if(graph.rd[v]==graph.PA){seen[v]=1;queue.push_back(v);}
  for(size_t h=0;h<queue.size();++h){int v=queue[h];
   for(const auto&e:graph.e[v])if(e.to==v&&!seen[e.from]){seen[e.from]=1;queue.push_back(e.from);}
  }
  I induced=0;
  for(int v:queue)for(const auto&e:graph.e[v])if(e.to==v&&seen[e.from])++induced;
  return {induced,std::move(queue)};
 }
 template<class Visitor>void visit_fractional_assignments(Visitor visitor)const{
  int maximum=0,top_count=0;
  for(int v=0;v<n;++v)maximum=std::max(maximum,degree_units(v));
  if(maximum!=graph.PA)throw std::runtime_error("PVLDB PA differs from actual maximum indegree");
  for(int v:graph.node){
   if(graph.e[v].empty())throw std::runtime_error("PVLDB node contains an isolated vertex");
   if(graph.rd[v]==graph.PA)++top_count;
  }
  if(graph.PA>0&&top_count!=graph.rd_PA)throw std::runtime_error("PVLDB maximum-degree count mismatch");
  for(size_t v=0;v<graph.e.size();++v){
   if(!graph.e[v].empty()&&!graph.node.contains(int(v)))throw std::runtime_error("PVLDB active node missing");
   for(const auto&e:graph.e[v])if(e.to==int(v)){
    if(!graph.e[e.from].contains(e))throw std::runtime_error("PVLDB missing reverse incidence");
    visitor(e.to,e.from,1.0); // incoming orientation allocates one unit to head
   }
  }
  // Check the paper's unreversibility invariant only in validation mode.
  if(graph.PA>0){
   std::vector<unsigned char> seen(n,0);std::vector<int> queue;
   for(int v:graph.node)if(graph.rd[v]==graph.PA){seen[v]=1;queue.push_back(v);}
   for(size_t h=0;h<queue.size();++h){int v=queue[h];
    if(graph.rd[v]<=graph.PA-2)throw std::runtime_error("PVLDB reversible path remains");
    for(const auto&e:graph.e[v])if(e.to==v&&!seen[e.from]){seen[e.from]=1;queue.push_back(e.from);}
   }
  }
 }
};

int main(int argc,char**argv)try{
 if(argc<4){std::cerr<<"pvldb2024_stream_gated input.events queries.csv cadence [epsilon=.1] [native|combined|sweep|peel] [check=0] [dump=-] [gate=1]\n";return 1;}
 Config config;config.cadence=std::stoull(argv[3]);config.eps=epsilon_ratio(argc>4?argv[4]:".1");
 if(argc>5)config.mode=argv[5];if(argc>6)config.verify=std::stoi(argv[6])!=0;if(argc>7)config.dump=argv[7];if(argc>8)config.gate=std::stoi(argv[8])!=0;
 const Input input=read_input(argv[1]);return run<PVLDBInsDel>(input,argv[2],config);
}catch(const std::exception&e){std::cerr<<"ERROR: "<<e.what()<<'\n';return 2;}
