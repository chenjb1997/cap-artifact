// Scheduled full-set adapter for the original Udshp algorithm.
// Algorithm update logic is unchanged; see baselines/PROVENANCE.md.
#include "DynamicGraph.h"
#include "DynOpManager.h"
#include "EdgeManager.h"
#include "hyper_baseline_profile.h"
#include "gzip_output.hpp"
#include "witness_delta.hpp"
#include "udshp_integer_bounds.hpp"
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <limits>

using Clock=std::chrono::steady_clock;
static int64_t ns(Clock::time_point start){return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();}
struct NullBuffer:std::streambuf{int overflow(int c)override{return traits_type::not_eof(c);}};
struct Silence{NullBuffer buffer;std::streambuf*old;Silence():old(std::cout.rdbuf(&buffer)){}~Silence(){std::cout.rdbuf(old);}};
struct Event{char op;long long id;edgeVector vertices;};
struct RecordedUpdate{
    int64_t algorithm,service,query;size_t active;
#if HB_PROFILE
    HyperBaselineCounters profile;
#endif
};
struct RecordedQuery{size_t step;std::string label;double density,upper;int64_t ns;bool certified;std::vector<int> nodes,loads;std::map<int,int> heads;};

struct CalendarQuery{size_t step;std::string label;};
static std::string json_string(const std::string&text){
    const char quote=34,backslash=92;std::ostringstream out;out<<quote;
    for(unsigned char c:text){
        if(c==quote||c==backslash)out<<backslash<<c;
        else if(c==10)out<<backslash<<'n';else if(c==13)out<<backslash<<'r';else if(c==9)out<<backslash<<'t';
        else if(c<32)out<<backslash<<'u'<<std::hex<<std::setw(4)<<std::setfill('0')<<int(c)<<std::dec;
        else out<<c;
    }return out.str()+quote;
}
static std::string csv_label(const std::string&label){
    const char quote=34;std::string out(1,quote);for(char c:label){if(c==quote)out+=quote;out+=c;}return out+quote;
}
static std::vector<CalendarQuery> read_calendar(const std::string&path,size_t count){
    std::ifstream input(path);if(!input)throw std::runtime_error("Cannot open query schedule");
    std::vector<CalendarQuery> schedule;std::string line;bool first=true;
    while(std::getline(input,line)){
        if(!line.empty()&&line.back()==13)line.pop_back();if(line.empty())continue;
        if(first&&line==std::string("step")+char(9)+"label"){first=false;continue;}first=false;
        size_t tab=line.find(char(9));if(tab==std::string::npos)throw std::runtime_error("Expected step<TAB>label");
        std::string token=line.substr(0,tab);
        if(token.empty()||token.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("Query step must be a nonnegative zero-based index");
        size_t used=0;unsigned long long value=std::stoull(token,&used);
        if(used!=token.size()||value>=count||(!schedule.empty()&&value<schedule.back().step))throw std::runtime_error("Query steps must be nondecreasing and in range");
        schedule.push_back({size_t(value),line.substr(tab+1)});
    }return schedule;
}

static void array(std::ostream&out,const std::vector<int>&values){out<<'[';for(size_t j=0;j<values.size();j++){if(j)out<<',';out<<values[j];}out<<']';}
static void counters(std::ostream&out,const HyperBaselineCounters&c){
    out<<c.insert_copies<<','<<c.delete_copies<<','<<c.flips<<','<<c.tight_calls<<','<<c.tight_budget<<','<<c.tight_scans<<','<<c.tight_endpoint_visits<<','
       <<c.notify_calls<<','<<c.notify_budget<<','<<c.notify_scans<<','<<c.notify_endpoint_visits<<','<<c.notify_rekeys<<','
       <<c.temporary_hash_inserts<<','<<c.edge_vector_copies<<','<<c.copied_vertices;
}
static long long induced(const std::set<int>&witness,const std::map<long long,edgeVector>&live){
    long long count=0;for(const auto&entry:live){bool inside=true;for(int v:entry.second)if(!witness.count(v)){inside=false;break;}count+=inside;}return count;
}
static void validate(DynamicGraph&graph,EdgeManager&em,int copies,const std::map<long long,edgeVector>&live){
    std::vector<int> loads(graph.researchLoads().size(),0);
    long long count=0;
    for(const auto&entry:em.edge2IdsList)for(int id:entry.second){
        auto it=graph.researchHeads().find(id);if(it==graph.researchHeads().end())throw std::runtime_error("Missing orientation");
        const auto&e=em.edgeDupMap.at(id);int head=it->second;
        if(std::find(e.begin(),e.end(),head)==e.end())throw std::runtime_error("Head outside hyperedge");
        loads.at(head)++;count++;
    }
    if(count!=static_cast<long long>(live.size())*copies||loads!=graph.researchLoads())throw std::runtime_error("Allocation/load mismatch");
}

int main(int argc,char**argv)try{
    if(argc<3)throw std::runtime_error("usage: udshp INPUT.updates OUTPUT_PREFIX [--queries FILE] [--epsilon .2] [--cadence 100] [--check 0] [--dump-assignments 0] [--checkpoint 1000] [--budget-seconds 0]");
    std::string input=argv[1],prefix=argv[2],query_file;bool use_calendar=false,cadence_given=false;double epsilon=.2,budget=0;int copies=0,cadence=100,check=0,dump=0,checkpoint=1000;size_t stop_after=0;
    for(int i=3;i<argc;i+=2){
        if(i+1==argc)throw std::runtime_error("Option missing value");std::string key=argv[i],value=argv[i+1];
        if(key=="--epsilon")epsilon=std::stod(value);else if(key=="--cadence"){cadence=std::stoi(value);cadence_given=true;}
        else if(key=="--queries"){query_file=value;use_calendar=true;}
        else if(key=="--check")check=std::stoi(value);else if(key=="--dump-assignments")dump=std::stoi(value);
        else if(key=="--checkpoint")checkpoint=std::stoi(value);else if(key=="--budget-seconds")budget=std::stod(value);
        else if(key=="--stop-after-updates")stop_after=std::stoull(value);
        else throw std::runtime_error("Unknown option: "+key);
    }
    if(!(epsilon>0&&epsilon<=1)||cadence<1||checkpoint<0||budget<0)throw std::runtime_error("Require epsilon in (0,1], cadence>=1, checkpoint/budget>=0; eta/alpha overrides are not supported");
    if(stop_after&&!check)throw std::runtime_error("Prefix replay requires check>0");
    if(use_calendar&&cadence_given)throw std::runtime_error("--queries and --cadence are mutually exclusive");
    // Original main parses epsilon into a float before rounding replication.
    float author_epsilon=epsilon;double wanted_copies=std::round(1.0/std::pow(author_epsilon,2));
    if(!std::isfinite(wanted_copies)||wanted_copies>std::numeric_limits<int>::max()/4)throw std::runtime_error("Replication exceeds conservative author int32 budget");
    copies=(int)wanted_copies;
    if(copies<1)throw std::runtime_error("Replication must be positive");
    auto input_start=Clock::now();std::ifstream source(input);int n;source>>n;if(!source||n<1)throw std::runtime_error("Invalid input header");
    std::string line;std::getline(source,line);std::vector<Event> events;std::map<long long,edgeVector> verify_live;
    long long insertions=0,peak_edges=0,peak_degree=0;int rank=0;std::vector<long long> incident_degree(n,0);
    while(std::getline(source,line)){
        if(line.empty())continue;std::istringstream in(line);Event e;int k;in>>e.op>>e.id>>k;
        if(!in||(e.op!='+'&&e.op!='-')||k<1)throw std::runtime_error("Malformed update");
        e.vertices.resize(k);for(int&v:e.vertices){in>>v;if(!in||v<0||v>=n)throw std::runtime_error("Invalid endpoint");}
        std::string extra;if(in>>extra)throw std::runtime_error("Trailing update token");
        std::sort(e.vertices.begin(),e.vertices.end());
        if(std::adjacent_find(e.vertices.begin(),e.vertices.end())!=e.vertices.end())throw std::runtime_error("Repeated endpoint");
        if(e.op=='+'){if(verify_live.count(e.id))throw std::runtime_error("Active edge ID reused");verify_live[e.id]=e.vertices;insertions++;}
        else{auto it=verify_live.find(e.id);if(it==verify_live.end()||it->second!=e.vertices)throw std::runtime_error("Illegal deletion");verify_live.erase(it);}
        for(int v:e.vertices){incident_degree[v]+=(e.op=='+'?1:-1);peak_degree=std::max(peak_degree,incident_degree[v]);}
        peak_edges=std::max(peak_edges,static_cast<long long>(verify_live.size()));
        rank=std::max(rank,k);events.push_back(std::move(e));
    }
    // Input-only admission: separate cumulative-ID and local four-load bounds.
    UdshpIntegerBounds integer_bounds{insertions,peak_edges,peak_degree,copies};integer_bounds.verify();
    std::vector<long long>().swap(incident_degree);
    const auto calendar=use_calendar?read_calendar(query_file,events.size()):std::vector<CalendarQuery>{};size_t next_query=0;
    verify_live.clear();int64_t input_ns=ns(input_start);
    Silence silence;EdgeManager em;
    auto construct=Clock::now();std::vector<DynamicGraph> instances;
    for(int i=0;i<=1;i++)instances.emplace_back(i,n,author_epsilon);
    DynOpManager dom(1,prefix+".author.output");dom.bindGraph(instances);
    auto&graph=dom.researchGraph();double eta=graph.researchEta();
    int64_t initialization=ns(construct),total_algorithm=0,total_service=0,total_query=0,total_validation=0;
    vectorListMap main_edges;std::map<long long,edgeVector> live;
    int logical_id=0;size_t active=0,queries=0,certified=0;
    std::vector<RecordedUpdate> update_records;update_records.reserve(1);
    std::vector<RecordedQuery> query_records;query_records.reserve(1);
    GzipOutput update(prefix+".updates.csv.gz"),query(prefix+".csv.gz"),witness(prefix+".witnesses.jsonl.gz");
    if(!update||!query||!witness)throw std::runtime_error("Cannot open output; parent directory must exist");
    update<<"step,op,algorithm_update_ns,service_update_ns,query_ns,total_service_ns,m,insert_copies,delete_copies,flips,tight_calls,tight_budget,tight_scans,tight_endpoint_visits,notify_calls,notify_budget,notify_scans,notify_endpoint_visits,notify_rekeys,temporary_hash_inserts,edge_vector_copies,copied_vertices\n";
    query<<std::setprecision(17)<<"step,witness_density,upper_bound,witness_size,certificate_pass,certified_ratio,query_ns,eta,alpha,label\n";witness<<std::setprecision(17);
    WitnessDeltaEncoder delta;
    size_t written_updates=0,written_queries=0;int64_t checkpoint_ns=0;bool budget_exceeded=false;
    auto write_pending=[&](bool flush_outputs=false){
        for(const auto&r:update_records){size_t step=written_updates++;
            update<<step<<','<<events[step].op<<','<<r.algorithm<<','<<r.service<<','<<r.query<<','<<r.service+r.query<<','<<r.active<<',';
#if HB_PROFILE
            counters(update,r.profile);
#else
            counters(update,HyperBaselineCounters{});
#endif
            update<<'\n';}
        for(const auto&r:query_records){++written_queries;
            query<<r.step<<','<<r.density<<','<<r.upper<<','<<r.nodes.size()<<','<<r.certified<<','<<(r.upper?r.density/r.upper:1)<<','<<r.ns<<','<<eta<<','<<copies<<','<<csv_label(r.label)<<'\n';
            witness<<"{\"step\":"<<r.step<<",\"label\":"<<json_string(r.label)<<",\"density\":"<<r.density<<",\"upper\":"<<r.upper;delta.write_fields(witness,r.nodes);
            if(dump){witness<<",\"loads\":";array(witness,r.loads);witness<<",\"heads\":[";bool comma=false;for(auto e:r.heads){if(comma)witness<<',';witness<<'['<<e.first<<','<<e.second<<']';comma=true;}witness<<']';}
            witness<<"}\n";
        }
        update_records.clear();query_records.clear();
        if(flush_outputs){update.flush();query.flush();witness.flush();}
        if(!update||!query||!witness)throw std::runtime_error("Cannot flush completed records");
    };
    auto batch_start=Clock::now();
    for(size_t step=0;step<events.size()&&(!stop_after||step<stop_after);step++){
        auto&e=events[step];hb_counters={};auto service_start=Clock::now();int64_t algorithm;
        if(e.op=='+'){
            main_edges[e.vertices].push_back(logical_id++);auto ids=em.getEdgeIdsAfterDuplication(e.vertices,copies);
            auto start=Clock::now();dom.addEdge(e.vertices,ids,em);algorithm=ns(start);active++;
        }else{
            auto ids=em.retrieveDuplicatedEdgeIds(e.vertices,copies);auto start=Clock::now();dom.removeEdge(e.vertices,ids,em);algorithm=ns(start);
            main_edges[e.vertices].pop_front();if(main_edges[e.vertices].empty())main_edges.erase(e.vertices);active--;
        }
        int64_t service=ns(service_start),query_time=0;total_algorithm+=algorithm;total_service+=service;
        if(check){auto start=Clock::now();if(e.op=='+')live[e.id]=e.vertices;else live.erase(e.id);validate(graph,em,copies,live);total_validation+=ns(start);}
        bool scheduled=use_calendar?(next_query<calendar.size()&&calendar[next_query].step==step):((step+1)%cadence==0||step+1==events.size());
        while(scheduled){
            auto start=Clock::now();
            // Full original path, including the unused candidate and author report.
            const std::string label=use_calendar?calendar[next_query].label:std::to_string(step);
            dom.getDensityEstimate(em,copies,main_edges,0,1,label);
            double lower=graph.researchDensity/copies,upper=double(graph.getMaxLabel())/copies;
            RecordedQuery recorded;recorded.step=step;recorded.label=label;recorded.density=lower;recorded.upper=upper;
            recorded.nodes.assign(graph.researchWitness.begin(),graph.researchWitness.end());
            int64_t one_query_time=ns(start);query_time+=one_query_time;total_query+=one_query_time;queries++;
            const auto&nodes=graph.researchWitness;
            bool pass=upper<=(1+epsilon)*lower+1e-12;certified+=pass;
            recorded.ns=one_query_time;recorded.certified=pass;
            if(check){auto start=Clock::now();long long count=induced(nodes,live);double density=nodes.empty()?0:double(count)/nodes.size();
                if(std::abs(density-lower)>1e-9||lower>upper+1e-9)throw std::runtime_error("Returned witness density mismatch");total_validation+=ns(start);}
            if(dump){recorded.loads=graph.researchLoads();for(const auto&entry:em.edge2IdsList)for(int id:entry.second)recorded.heads[id]=graph.researchHeads().at(id);}
            query_records.push_back(std::move(recorded));write_pending(true);
            if(use_calendar){++next_query;scheduled=next_query<calendar.size()&&calendar[next_query].step==step;}else scheduled=false;
        }
        update_records.push_back({algorithm,service,query_time,active
#if HB_PROFILE
            ,hb_counters
#endif
        });
        write_pending();
        if(checkpoint&&(step+1)%checkpoint==0){
            auto checkpoint_start=Clock::now();write_pending(true);
            // Unique monotonically increasing files: a interrupted write does not
            // replace the last valid checkpoint. Sidecar prefixes match row counts.
            std::ofstream state(prefix+".checkpoint."+std::to_string(step+1)+".json");
            state<<"{\"status\":\"checkpoint\",\"completed_updates\":"<<step+1<<",\"planned_updates\":"<<events.size()<<",\"last_step\":"<<step
                 <<",\"queries\":"<<queries<<",\"algorithm_update_ns\":"<<total_algorithm<<",\"service_update_ns\":"<<total_service<<",\"query_ns\":"<<total_query
                 <<",\"batch_elapsed_ns_includes_checkpoint_io\":"<<ns(batch_start)<<",\"previous_checkpoint_ns\":"<<checkpoint_ns
                 <<",\"csv_completed_update_rows\":"<<written_updates<<",\"csv_completed_query_rows\":"<<written_queries<<"}\n";
            state.close();if(!state)throw std::runtime_error("Cannot write checkpoint");checkpoint_ns+=ns(checkpoint_start);
        }
        if(budget&&step+1<events.size()&&ns(batch_start)>=budget*1e9){budget_exceeded=true;break;}
    }
    int64_t batch_ns=ns(batch_start);
    auto final_serialization_start=Clock::now();write_pending(true);if(!update.close()||!query.close()||!witness.close())throw std::runtime_error("gzip close failed");int64_t final_serialization_ns=ns(final_serialization_start);
    std::ofstream summary(prefix+".summary.json");if(!summary)throw std::runtime_error("Cannot open summary");
    summary<<std::setprecision(17)<<"{\"status\":\""<<(budget_exceeded?"budget_exceeded":"complete")<<"\",\"updates\":"<<written_updates<<",\"planned_updates\":"<<events.size()<<",\"queries\":"<<queries<<",\"certified_queries\":"<<certified
           <<",\"integer_bounds\":";integer_bounds.write_json(summary);summary<<",\"calendar_schedule\":"<<(use_calendar?"true":"false")<<",\"query_schedule\":"<<json_string(query_file)
           <<",\"scheduled_queries\":"<<(use_calendar?std::to_string(calendar.size()):"null")
           <<",\"query_step_definition\":\"zero-based update index; query after that update; calendar mode has no implicit final query\""
           <<",\"epsilon\":"<<epsilon<<",\"alpha\":"<<copies<<",\"eta\":"<<eta<<",\"rank\":"<<rank<<",\"n\":"<<n
           <<",\"profile_enabled\":"<<(HB_PROFILE?"true":"false")<<",\"validation_enabled\":"<<(check?"true":"false")
           <<",\"algorithm_update_ns\":"<<total_algorithm<<",\"service_update_ns\":"<<total_service<<",\"query_ns\":"<<total_query
           <<",\"initialization_ns\":"<<initialization<<",\"input_ns\":"<<input_ns<<",\"batch_ns\":"<<batch_ns<<",\"validation_ns_excluded\":"<<total_validation
           <<",\"checkpoint_ns\":"<<checkpoint_ns<<",\"checkpoint_cadence\":"<<checkpoint<<",\"budget_seconds\":"<<budget<<",\"batch_includes_checkpoint_io\":true,\"final_sidecar_serialization_ns\":"<<final_serialization_ns
           <<",\"historical_copies_retained\":"<<em.edgeDupMap.size()<<",\"active_edges\":"<<active
           <<",\"input_preloaded\":true,\"enumeration_timed\":true,\"sidecar_serialization_timed\":false,\"author_report_formatting_timed\":true,\"legacy_extra_candidate\":true"
           <<",\"buffered_update_bytes\":"<<update_records.capacity()*sizeof(RecordedUpdate)<<",\"sidecar_storage\":\"streaming gzip level1; no historical answer retention\",\"prefix_replay\":"<<(stop_after?"true":"false")
           <<",\"query_scope\":\"full original DOM.getDensityEstimate plus retaining/materializing attaining set; original report formatting/output included; our sidecar serialization excluded\",\"parameter_scope\":\"author native epsilon only; alpha=round(epsilon^-2), eta=1; no new parameter variants\"}\n";
    return budget_exceeded?124:0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 2;}
