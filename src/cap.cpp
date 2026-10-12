// Isolated compact calendar adapter; no per-update trace file or output buffer.
#include "platform.hpp"
#include "mechanism_kernel.hpp"
#include "independent_observer.hpp"
#include "gzip_output.hpp"
#include "witness_delta.hpp"
#include <map>
#include <sstream>
#include <filesystem>

using Clock=std::chrono::steady_clock;
static I elapsed_ns(Clock::time_point start){return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();}
static std::string json_string(const std::string& text) {
 std::ostringstream out;out<<'"';
 for(unsigned char c:text) {
  if(c=='"'||c=='\\')out<<'\\'<<c;
  else if(c=='\n')out<<"\\n";else if(c=='\r')out<<"\\r";else if(c=='\t')out<<"\\t";
  else if(c<32)out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<int(c)<<std::dec;
  else out<<c;
 }
 return out.str()+'"';
}
struct Event {char op;int id;std::vector<int> edge;};
struct Report {size_t step;std::string label;};
struct Query {size_t step;std::string label;I numerator;int denominator;I maximum;double density,upper;std::vector<int> vertices;};

// Two explicit modulo-2^64 checksums, not a cryptographic allocation hash.
struct Digest {
 uint64_t a=14695981039346656037ull,b=0x6a09e667f3bcc909ull;
 void byte(unsigned char x){a=(a^x)*1099511628211ull;b=(b+x+0x9e3779b97f4a7c15ull)*0xbf58476d1ce4e5b9ull;b^=b>>29;}
 void integer(uint64_t x){for(int j=0;j<8;++j){byte(static_cast<unsigned char>(x));x>>=8;}}
 void text(const std::string&s){integer(s.size());for(unsigned char c:s)byte(c);}
 std::string value()const{std::ostringstream out;out<<std::hex<<std::setfill('0')<<std::setw(16)<<a<<std::setw(16)<<b;return out.str();}
};
static void add_metrics(Digest&d,const Metrics&m) {
 for(I value:{m.bfs,m.scans,m.augment,m.moved,m.witnesses,m.peels,m.endpoint_scans,m.path_edges,m.path_transport})d.integer(value);
}
struct Aggregate {
 I updates=0,no_repair=0,no_bfs=0,bfs=0,scans=0,augment=0,moved=0,witnesses=0,peels=0,endpoints=0,path_edges=0,path_transport=0;
 void add(const Metrics&m) {
  ++updates;no_repair+=(m.bfs==0&&m.peels==0);no_bfs+=(m.bfs==0);
  bfs+=m.bfs;scans+=m.scans;augment+=m.augment;moved+=m.moved;witnesses+=m.witnesses;peels+=m.peels;
  endpoints+=m.endpoint_scans;path_edges+=m.path_edges;path_transport+=m.path_transport;
 }
 void fields(std::ostream&out)const {
  out<<"\"updates\":"<<updates<<",\"updates_without_repair\":"<<no_repair<<",\"updates_without_bfs\":"<<no_bfs
     <<",\"bfs\":"<<bfs<<",\"scans\":"<<scans<<",\"augment\":"<<augment<<",\"moved\":"<<moved
     <<",\"failed_searches\":"<<bfs-augment<<",\"peels\":"<<peels<<",\"witness_changes\":"<<witnesses
     <<",\"endpoint_scans\":"<<endpoints<<",\"path_edges\":"<<path_edges<<",\"path_transport\":"<<path_transport;
 }
 void write(std::ostream&out)const{out<<'{';fields(out);out<<'}';}
};
static std::vector<Report> read_reports(const std::string&path,size_t count) {
 std::ifstream input(path);if(!input)throw std::runtime_error("Cannot open query TSV");
 std::vector<Report> reports;std::string line;
 while(std::getline(input,line)) {
  if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;
  auto tab=line.find('\t');if(tab==std::string::npos)throw std::runtime_error("Query must be step<TAB>label, no header");
  auto token=line.substr(0,tab);if(token.empty()||token.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("Invalid query step");
  auto step=std::stoull(token);if(step>=count||(!reports.empty()&&step<reports.back().step))throw std::runtime_error("Query schedule not in-range/nondecreasing");
  reports.push_back({size_t(step),line.substr(tab+1)});
 }
 return reports;
}
static void write_query(std::ostream&out,const Query&q,I b) {
 out<<"{\"step\":"<<q.step<<",\"label\":"<<json_string(q.label)<<",\"numerator\":"<<q.numerator<<",\"denominator\":"<<q.denominator<<",\"density\":"<<q.density<<",\"upper\":"<<q.upper<<",\"upper_numerator\":"<<q.maximum<<",\"upper_denominator\":"<<b<<",\"vertices\":[";
 for(size_t j=0;j<q.vertices.size();++j){if(j)out<<',';out<<q.vertices[j];}out<<"]}\n";
}
static void write_query_delta(std::ostream&out,const Query&q,I b,WitnessDeltaEncoder&evidence,uint64_t revision) {
 out<<"{\"step\":"<<q.step<<",\"label\":"<<json_string(q.label)<<",\"numerator\":"<<q.numerator<<",\"denominator\":"<<q.denominator<<",\"density\":"<<q.density<<",\"upper\":"<<q.upper<<",\"upper_numerator\":"<<q.maximum<<",\"upper_denominator\":"<<b;
 evidence.write_fields(out,q.vertices,revision);out<<"}\n";
}
static void write_progress(const std::string&prefix,const std::string&name,size_t count,size_t queries,I update_ns,I query_ns,const Aggregate&all,const Digest&digest,const Digest&maintenance_digest,bool complete) {
 const std::string path=prefix+".progress.json",temporary=path+".tmp";
 std::ofstream out(temporary);if(!out)throw std::runtime_error("Cannot open progress output");
 out<<"{\"status\":"<<json_string(complete?(size_t(all.updates)==count?"complete":"prefix_complete"):"running")<<",\"policy\":"<<json_string(name)<<",\"processed_updates\":"<<all.updates<<",\"total_updates\":"<<count<<",\"queries\":"<<queries<<",\"update_ns\":"<<update_ns<<",\"query_ns\":"<<query_ns<<",\"non_time_digest\":"<<json_string(digest.value())<<",\"maintenance_digest\":"<<json_string(maintenance_digest.value())<<",\"metrics\":";
 all.write(out);out<<"}\n";out.close();if(!out)throw std::runtime_error("Progress write failed");
 // A monitor can briefly hold the destination open on Windows. Progress is
 // best-effort telemetry; a transient publication failure must not kill a run.
 if(!cap_platform::replace_file(temporary,path))std::cerr<<"progress publication deferred\n";
}

int main(int argc,char**argv)try {
 if(argc<10){std::cerr<<"cap UPDATES PREFIX ep eq rank B POLICY check QUERIES.tsv [--query-mode full|size] [--evidence-mode delta|full] [--full-checkpoints 64] [--stop-after-updates N]\n";return 1;}
 const std::string prefix=argv[2],name=argv[7],query_path=argv[9];
 int ep=std::stoi(argv[3]),eq=std::stoi(argv[4]),rank=std::stoi(argv[5]),check=std::stoi(argv[8]);I b=std::stoll(argv[6]);
 if(check<0||check>2)throw std::runtime_error("check must be 0, 1, or 2");
 size_t stop_after=0,full_checkpoints=64;std::string query_mode="full",evidence_mode="delta";
 for(int arg=10;arg<argc;arg+=2) {
  if(arg+1>=argc)throw std::runtime_error("Missing option value");
  std::string key=argv[arg],value=argv[arg+1];
  if(key=="--stop-after-updates"){if(check==0)throw std::runtime_error("Prefix replay requires check=1/2");stop_after=std::stoull(value);if(!stop_after)throw std::runtime_error("Empty prefix");}
  else if(key=="--query-mode"){query_mode=value;if(value!="full"&&value!="size")throw std::runtime_error("Query mode must be full or size");}
  else if(key=="--evidence-mode"){evidence_mode=value;if(value!="full"&&value!="delta")throw std::runtime_error("Evidence mode must be full or delta");}
  else if(key=="--full-checkpoints"){full_checkpoints=std::stoull(value);if(full_checkpoints>4096)throw std::runtime_error("Too many full checkpoints");}
  else throw std::runtime_error("Unknown option");
 }
 MechanismPolicy policy;
 if(name=="CAP")policy=MechanismPolicy::CAP;else if(name=="Tight")policy=MechanismPolicy::Tight;
 else if(name=="NoReserve")policy=MechanismPolicy::NoReserve;else if(name=="NoRefresh")policy=MechanismPolicy::NoRefresh;
 else if(name=="Unit")policy=MechanismPolicy::Unit;else throw std::runtime_error("Unknown or unaudited policy");
 for(const auto& suffix:{".stats.json",".witnesses.jsonl.gz",".certificates.jsonl",".progress.json"})if(std::filesystem::exists(prefix+suffix))throw std::runtime_error("Output prefix already exists");
 auto parse_start=Clock::now();std::ifstream input(argv[1]);int n;
 if(!(input>>n)||n<1)throw std::runtime_error("Invalid stream header");
 if(check==2&&n>16)throw std::runtime_error("Tiny exhaustive check requires n<=16");
 std::vector<Event>events;std::map<int,std::vector<int>>current;std::string line,extra;std::getline(input,line);
 while(std::getline(input,line)) {
  if(line.empty())continue;std::istringstream row(line);Event event;int k;
  if(!(row>>event.op>>event.id>>k)||event.id<0||k<1||k>rank||(event.op!='+'&&event.op!='-'))throw std::runtime_error("Invalid event");
  event.edge.resize(k);for(int&v:event.edge)if(!(row>>v)||v<0||v>=n)throw std::runtime_error("Invalid endpoint");
  if((row>>extra)||!std::is_sorted(event.edge.begin(),event.edge.end())||std::adjacent_find(event.edge.begin(),event.edge.end())!=event.edge.end())throw std::runtime_error("Noncanonical edge");
  if(event.op=='+'){if(current.count(event.id))throw std::runtime_error("Active ID reused");current[event.id]=event.edge;}
  else{auto found=current.find(event.id);if(found==current.end()||found->second!=event.edge)throw std::runtime_error("Invalid deletion");current.erase(found);}
  events.push_back(std::move(event));
 }
 if(events.empty())throw std::runtime_error("Empty stream");current.clear();
 const size_t update_limit=stop_after?stop_after:events.size();
 if(update_limit>events.size())throw std::runtime_error("Requested prefix exceeds stream length");
 auto reports=query_path=="-"?std::vector<Report>{}:read_reports(query_path,events.size());
 reports.erase(std::lower_bound(reports.begin(),reports.end(),update_limit,[](const Report&r,size_t limit){return r.step<limit;}),reports.end());
 const bool complete_stream=(update_limit==events.size());
 std::set<size_t> checkpoint_steps;
 for(size_t j=1;j<=full_checkpoints;++j){size_t completed=events.size()*j/(full_checkpoints+1);if(completed&&completed<events.size())checkpoint_steps.insert(completed-1);}
 auto next_checkpoint=checkpoint_steps.begin();I parse_ns=elapsed_ns(parse_start);
 int mode=(policy==MechanismPolicy::NoRefresh?0:(policy==MechanismPolicy::Tight?5:1));
 auto init_start=Clock::now();CapDS ds(n,rank,ep,eq,b,mode);ds.policy=policy;I init_ns=elapsed_ns(init_start);
 std::unique_ptr<IndependentObserver> observer;
 if(check){ds.enable_observer();observer=std::make_unique<IndependentObserver>(n);}
 std::unique_ptr<GzipOutput> witnesses;
 if(query_mode=="full"){witnesses=std::make_unique<GzipOutput>(prefix+".witnesses.jsonl.gz");*witnesses<<std::setprecision(17);}
 std::ofstream certificates;
 if(observer){certificates.open(prefix+".certificates.jsonl");if(!certificates)throw std::runtime_error("Cannot open certificate output");}
 Aggregate all,insertions,deletions,internal,outside;
 Digest digest,witness_digest,maintenance_digest,size_digest;digest.text("cap-output-v1");digest.text(name);digest.integer(ep);digest.integer(eq);digest.integer(rank);digest.integer(b);digest.integer(n);
 maintenance_digest.text("cap-maintenance-v1");maintenance_digest.text(name);for(I x:{I(ep),I(eq),I(rank),b,I(n)})maintenance_digest.integer(x);
 size_digest.text("size-after-each-update-v1");
 WitnessDeltaEncoder evidence;uint64_t witness_revision=0;
 I update_ns=0,query_ns=0;size_t next_report=0,queries_emitted=0,certified_snapshots=0;
 auto batch_start=Clock::now(),last_progress=batch_start;
 write_progress(prefix,name,events.size(),queries_emitted,update_ns,query_ns,all,digest,maintenance_digest,false);
 for(size_t step=0;step<update_limit;++step) {
  const auto& event=events[step];bool internal_deletion=false;
  if(event.op=='-'){internal_deletion=true;for(int v:event.edge)if(!ds.mark[v]){internal_deletion=false;break;}}
  ds.begin_update();
  if(observer)observer->apply_input(event.op,event.id,event.edge);
  auto update_start=Clock::now();
  if(event.op=='+')ds.insert(event.id,event.edge);else ds.erase(event.id);
  update_ns+=elapsed_ns(update_start);
  all.add(ds.mt);if(event.op=='+')insertions.add(ds.mt);else{deletions.add(ds.mt);if(internal_deletion)internal.add(ds.mt);else outside.add(ds.mt);}
  witness_revision+=ds.mt.witnesses;
  if(ds.mt.witnesses){witness_digest=Digest{};witness_digest.integer(ds.witness.size());for(int v:ds.witness)witness_digest.integer(v);}
  auto record_update=[&](Digest&digest){
  digest.integer(0x555044415445ull);digest.integer(step);digest.integer(event.op);digest.integer(event.id);digest.integer(event.edge.size());for(int v:event.edge)digest.integer(v);
  for(I value:{ds.sm,I(ds.ssize),ds.max_load(),I(ds.active.size()),ds.cap})digest.integer(value);
  digest.integer(witness_digest.a);digest.integer(witness_digest.b);add_metrics(digest,ds.mt);digest.integer(internal_deletion);
  };record_update(digest);record_update(maintenance_digest);
  if(observer)observer->after_update(ds);
  if(query_mode=="size") {
   auto query_start=Clock::now();uint32_t reported_size=static_cast<uint32_t>(ds.ssize);
   query_ns+=elapsed_ns(query_start);
   if(ds.ssize<0||size_t(reported_size)!=ds.witness.size())throw std::runtime_error("Size query differs from maintained witness");
   size_digest.integer(step);size_digest.integer(reported_size);
   digest.integer(0x53495a4551ull);digest.integer(step);digest.integer(reported_size);
   ++queries_emitted;
   if(observer)observer->query_check(ds);
   while(next_report<reports.size()&&reports[next_report].step==step) {
    if(observer) {
     certificates<<"{\"step\":"<<step<<",\"label\":"<<json_string(reports[next_report].label)<<",\"density_numerator\":"<<observer->induced<<",\"density_denominator\":"<<ds.ssize<<",\"upper_numerator\":"<<ds.max_load()<<",\"upper_denominator\":"<<b<<",\"allocation_certified\":true,\"induced_density_certified\":true}\n";
     ++certified_snapshots;
    }
    ++next_report;
   }
  } else while(next_report<reports.size()&&reports[next_report].step==step) {
   auto query_start=Clock::now();
   Query q{step,reports[next_report].label,ds.sm,ds.ssize,ds.max_load(),ds.density(),ds.upper(),ds.witness};
   query_ns+=elapsed_ns(query_start);
   digest.integer(0x5155455259ull);digest.integer(q.step);digest.text(q.label);digest.integer(q.numerator);digest.integer(q.denominator);digest.integer(q.maximum);digest.integer(witness_digest.a);digest.integer(witness_digest.b);
   if(evidence_mode=="delta")write_query_delta(*witnesses,q,b,evidence,witness_revision);else write_query(*witnesses,q,b);
   if(observer) {
    observer->query_check(ds);
    certificates<<"{\"step\":"<<q.step<<",\"label\":"<<json_string(q.label)<<",\"density_numerator\":"<<observer->induced<<",\"density_denominator\":"<<q.denominator<<",\"upper_numerator\":"<<q.maximum<<",\"upper_denominator\":"<<b<<",\"allocation_certified\":true,\"induced_density_certified\":true}\n";
    ++certified_snapshots;
   }
   ++next_report;++queries_emitted;
  }
  if(observer) {
   if(next_checkpoint!=checkpoint_steps.end()&&*next_checkpoint==step){observer->full_check(ds);++next_checkpoint;}
   if(check==2)observer->exhaustive_check(ds);
  }
  if((step+1)%65536==0||elapsed_ns(last_progress)>=10000000000ll) {
   if(witnesses){witnesses->flush();if(!*witnesses)throw std::runtime_error("Witness write failed");}if(certificates.is_open()){certificates.flush();if(!certificates)throw std::runtime_error("Certificate write failed");}
   write_progress(prefix,name,events.size(),queries_emitted,update_ns,query_ns,all,digest,maintenance_digest,false);last_progress=Clock::now();
  }
 }
 if(observer)observer->full_check(ds);
 I batch_ns=elapsed_ns(batch_start);
 if(next_report!=reports.size())throw std::runtime_error("Unconsumed schedule");
 const auto peak_working_set_bytes=cap_platform::peak_working_set_bytes();
 if(witnesses&&!witnesses->close())throw std::runtime_error("Witness write failed");if(certificates.is_open()){certificates.close();if(!certificates)throw std::runtime_error("Certificate write failed");}
 std::ofstream stats(prefix+".stats.json");if(!stats)throw std::runtime_error("Cannot open stats output");
 stats<<"{\"status\":"<<json_string(complete_stream?"complete":"prefix_complete")<<",\"complete_stream\":"<<(complete_stream?"true":"false")<<",\"total_input_updates\":"<<events.size()<<",\"schema\":\"cap-output-v1\",\"policy\":"<<json_string(name)<<",\"updates\":"<<update_limit<<",\"queries\":"<<queries_emitted
      <<",\"epsilon_numerator\":"<<ep<<",\"epsilon_denominator\":"<<eq<<",\"rank\":"<<rank<<",\"B\":"<<b<<",\"mode\":"<<mode<<",\"check_level\":"<<check
      <<",\"check_mode\":"<<check<<",\"validation_pass\":"<<(check?"true":"false")<<",\"timing_eligible\":"<<(check==0?"true":"false")<<",\"parse_ns\":"<<parse_ns<<",\"initialization_ns\":"<<init_ns<<",\"update_ns\":"<<update_ns<<",\"query_ns\":"<<query_ns<<",\"batch_ns\":"<<batch_ns
      <<",\"peak_process_working_set_bytes\":"<<peak_working_set_bytes<<",\"historical_edge_slots\":"<<ds.edges.size()<<",\"query_schedule\":"<<json_string(query_path)
      <<",\"non_time_digest\":"<<json_string(digest.value())<<",\"digest_scope\":\"Two noncryptographic 64-bit checksums over input events, per-update exact bound/count scalars, maintained witness-order checksum, mechanism counters, deletion class and query scalars. Full query membership uses the maintained witness checksum, avoiding repeated evidence scans of unchanged sets. Excludes clocks/checker fields; not a complete allocation hash.\""
      <<",\"query_mode\":"<<json_string(query_mode)<<",\"maintenance_digest\":"<<json_string(maintenance_digest.value())<<",\"size_digest\":"<<json_string(size_digest.value())<<",\"full_checkpoints_requested\":"<<full_checkpoints<<",\"certified_snapshots\":"<<certified_snapshots
      <<",\"size_query_contract\":\"Reads maintained ssize after every update without enumerating or storing returned vertex sets; keeps a size checksum only.\""
      <<",\"evidence_mode\":"<<json_string(evidence_mode)<<",\"evidence_statistics\":";evidence.write_stats(stats);
 stats<<",\"metrics\":{";all.fields(stats);
 stats<<",\"insertions\":"<<insertions.updates<<",\"deletions\":"<<deletions.updates<<",\"internal_deletions\":"<<internal.updates<<",\"outside_solution_deletions\":"<<outside.updates
      <<",\"internal_deletions_without_repair\":"<<internal.no_repair<<",\"outside_solution_deletions_without_repair\":"<<outside.no_repair<<"},\"by_update_kind\":{\"insertion\":";
 insertions.write(stats);stats<<",\"deletion\":";deletions.write(stats);stats<<",\"internal_deletion\":";internal.write(stats);stats<<",\"outside_solution_deletion\":";outside.write(stats);stats<<'}';
 stats<<",\"quality_observer\":";if(observer)observer->write(stats);else stats<<"null";
 stats<<",\"batch_scope\":\"Includes update/query loop, checksums, scalar aggregation, query serialization, progress writes and any enabled observer; excludes parsing and initialization. update_ns and query_ns are separate service timers excluding serialization and progress I/O.\",\"memory_scope\":\"Whole process including preloaded stream, historical kernel slots and enabled independent observer. Full queries are streamed as gzip with no retained query trajectory; size mode stores no per-update output. Query serialization and compression are outside service timers.\",\"updates_without_repair_definition\":\"No residual search and no peeling refresh during the update; updates_without_bfs is also provided. Normalization and input handling are still charged.\"}\n";
 stats.close();if(!stats)throw std::runtime_error("Stats write failed");
 write_progress(prefix,name,events.size(),queries_emitted,update_ns,query_ns,all,digest,maintenance_digest,true);
 std::cerr<<"completed "<<update_limit<<" updates, "<<queries_emitted<<" queries, "<<name<<", check="<<check<<'\n';return 0;
}catch(const std::exception&error){std::cerr<<error.what()<<'\n';return 2;}
