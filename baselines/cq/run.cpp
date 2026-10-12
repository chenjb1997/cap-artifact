#include "core.h"
#include "gzip_output.hpp"
#include "witness_delta.hpp"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
using Clock=std::chrono::steady_clock;
struct Event {char op;int id;std::vector<int> vertices;};
int main(int argc,char**argv){
    int step=-1;std::string prefix=argc>3?argv[3]:"";
    try{
        if(argc!=8&&argc!=10)throw std::runtime_error("usage: stream queries prefix epsilon rank checking copies_override(0=certified)");
        std::ifstream input(argv[1]),schedule(argv[2]);
        int n;if(!(input>>n)||n<1||!schedule)throw std::runtime_error("invalid inputs");
        int rank=std::stoi(argv[5]),checking=std::stoi(argv[6]);
        size_t stop_after=0;
        if(argc==10){if(std::string(argv[8])!="--stop-after-updates"||!checking)throw std::runtime_error("Prefix replay requires checking");stop_after=std::stoull(argv[9]);if(!stop_after)throw std::runtime_error("Empty prefix");}
        cq::Parameters params(n,rank,std::stold(argv[4]));
        auto override_copies=std::stoll(argv[7]);
        if(override_copies<0)throw std::runtime_error("invalid override");
        if(override_copies)params.copies=override_copies;
        std::vector<Event> events;Event e;int size;
        while(input>>e.op>>e.id>>size){
            if(size<1||size>rank||e.id<0||(e.op!='+'&&e.op!='-'))throw std::runtime_error("invalid event");
            e.vertices.resize(size);
            for(int&v:e.vertices)if(!(input>>v)||v<0||v>=n)throw std::runtime_error("invalid vertex");
            if(!std::is_sorted(e.vertices.begin(),e.vertices.end())||std::adjacent_find(e.vertices.begin(),e.vertices.end())!=e.vertices.end())
                throw std::runtime_error("noncanonical edge");
            events.push_back(e);
        }
        if(!input.eof())throw std::runtime_error("trailing input error");
        std::vector<std::pair<int,std::string>> asks;int at;std::string label;
        while(schedule>>at>>label){
            if(at<0||at>=(int)events.size()||(!asks.empty()&&at<asks.back().first))throw std::runtime_error("invalid schedule");
            asks.emplace_back(at,label);
        }
        if(!schedule.eof())throw std::runtime_error("trailing schedule error");
        GzipOutput output(prefix+".witnesses.jsonl.gz");
        std::ofstream progress(prefix+".progress.jsonl");
        if(!output||!progress)throw std::runtime_error("output open");
        output<<std::setprecision(20);progress<<std::setprecision(20);
        std::ofstream parameters(prefix+".parameters.json");
        parameters<<std::setprecision(22)<<"{\"n\":"<<n<<",\"rank\":"<<rank<<",\"epsilon_requested\":\""<<argv[4]<<"\",\"epsilon\":"<<params.epsilon
            <<",\"alpha\":"<<params.alpha<<",\"multiplier\":"<<params.multiplier<<",\"beta\":"<<params.beta
            <<",\"growth\":"<<params.growth<<",\"levels\":"<<params.levels<<",\"copies\":"<<params.copies
            <<",\"test_override\":"<<(override_copies?"true":"false")<<"}\n";parameters.close();
        auto start=Clock::now();cq::Core ds(params);
        double init_s=std::chrono::duration<double>(Clock::now()-start).count(),update_s=0,query_s=0;
        WitnessDeltaEncoder delta;
        size_t qi=0;auto last_progress=Clock::now();
        size_t completed_updates=0;
        for(step=0;step<(int)events.size()&&(!stop_after||size_t(step)<stop_after);++step){
            start=Clock::now();const auto&e=events[step];
            if(e.op=='+')ds.insert(e.id,e.vertices,checking==2);
            else ds.erase(e.id,e.vertices,checking==2);
            update_s+=std::chrono::duration<double>(Clock::now()-start).count();
            if(checking)ds.check();
            while(qi<asks.size()&&asks[qi].first==step){
                start=Clock::now();auto answer=ds.query();
                query_s+=std::chrono::duration<double>(Clock::now()-start).count();
                if(checking)ds.check(); // validation only, separately excluded from service time
                output<<"{\"query_index\":"<<qi<<",\"step\":"<<step<<",\"label\":\""<<asks[qi].second
                    <<"\",\"induced_edges\":"<<answer.induced<<",\"density\":"
                    <<(answer.vertices.empty()?0.0L:(long double)answer.induced/answer.vertices.size())
                    <<",\"max_load\":"<<answer.max_load<<",\"level\":"<<answer.level;
                delta.write_fields(output,answer.vertices);output<<"}\n";output.flush();
                if(!output)throw std::runtime_error("gzip write failed");++qi;
            }
            ++completed_updates;
            auto current=Clock::now();
            if(step==0||step+1==(int)events.size()||std::chrono::duration<double>(current-last_progress).count()>=2){
                progress<<"{\"completed_updates\":"<<step+1<<",\"queries\":"<<qi<<",\"physical_updates\":"<<ds.physical_updates
                    <<",\"service_seconds\":"<<update_s+query_s<<",\"flips\":"<<ds.flips<<",\"relabels\":"<<ds.relabels<<"}\n";
                progress.flush();last_progress=current;
            }
        }
        if(!output.close())throw std::runtime_error("gzip close failed");
        std::ofstream stats(prefix+".stats.json");
        stats<<std::setprecision(20)<<"{\"status\":\"complete\",\"updates\":"<<completed_updates<<",\"planned_updates\":"<<events.size()<<",\"prefix_replay\":"<<(stop_after?"true":"false")<<",\"queries\":"<<qi
            <<",\"copies\":"<<params.copies<<",\"physical_updates\":"<<ds.physical_updates<<",\"flips\":"<<ds.flips
            <<",\"validation_enabled\":"<<(checking?"true":"false")<<",\"allocation_dump_enabled\":false,\"sidecar_storage\":\"streaming gzip level1\""<<",\"processed_groups\":"<<ds.processed_groups<<",\"relabels\":"<<ds.relabels<<",\"initialization_seconds\":"<<init_s
            <<",\"update_seconds\":"<<update_s<<",\"query_seconds\":"<<query_s<<",\"service_seconds\":"<<update_s+query_s<<"}\n";
        std::cout<<"complete updates="<<events.size()<<" queries="<<qi<<" service_seconds="<<update_s+query_s<<'\n';
    }catch(const std::exception&error){
        std::cerr<<"step="<<step<<" error="<<error.what()<<'\n';
        if(!prefix.empty()){std::ofstream f(prefix+".failure.txt");f<<"step="<<step<<" error="<<error.what()<<'\n';}
        return 2;
    }
}
