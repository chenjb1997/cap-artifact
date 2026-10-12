#include "gated_query_service.h"
#include "dyn_edge_orientation_improved_opt.h"
#include "dyn_edge_orientation_improved_opt_fixed.h"
#include "dyn_edge_orientation_strong_opt.h"
using namespace gated_service;
template<class Algorithm>struct ExactAdapter{
 std::shared_ptr<dyn_graph_access> shell;DeltaOrientationsResult result;Algorithm algorithm;
 template<class D,class I>ExactAdapter(const GraphInfo&g,const D&,const I&):shell(std::make_shared<dyn_graph_access>(g.n)),result(),algorithm(shell,DeltaOrientationsConfig{},result){}
 void handle_insertion(std::pair<int,int>e){algorithm.handleInsertion(e.first,e.second);}
 void handle_deletion(std::pair<int,int>e){algorithm.handleDeletion(e.first,e.second);}
 void begin_update(){algorithm.begin_update();}
 const std::vector<int>&changed_vertices()const{return algorithm.changed_vertices();}
 int degree_units(int v)const{return algorithm.degree_units(v);}
 template<class Visitor>void visit_fractional_assignments(Visitor visitor)const{algorithm.visit_fractional_assignments(visitor);}
};
int main(int argc,char**argv)try{
 if(argc<5){std::cerr<<"exact_orientation_stream_gated input.events queries.csv cadence improved|fixed|strong [epsilon=.1] [combined|sweep|peel] [check=0] [dump=-] [gate=1]\n";return 1;}
 Config c;c.cadence=std::stoull(argv[3]);const std::string variant=argv[4];c.eps=epsilon_ratio(argc>5?argv[5]:".1");if(argc>6)c.mode=argv[6];if(argc>7)c.verify=std::stoi(argv[7])!=0;if(argc>8)c.dump=argv[8];if(argc>9)c.gate=std::stoi(argv[9])!=0;
 const Input input=read_input(argv[1]);
 if(variant=="improved")return run<ExactAdapter<dyn_edge_orientation_improved_opt>>(input,argv[2],c);
 if(variant=="fixed")return run<ExactAdapter<dyn_edge_orientation_improved_opt_fixed>>(input,argv[2],c);
 if(variant=="strong")return run<ExactAdapter<dyn_edge_orientation_strong_opt>>(input,argv[2],c);
 throw std::runtime_error("unknown exact orientation variant");
}catch(const std::exception&e){std::cerr<<"ERROR: "<<e.what()<<'\n';return 2;}
