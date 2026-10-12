// Calendar-query adapter. The included CAP kernel is unchanged.
#include "platform.hpp"
#define CAP_VARIANT 5
#include "cap_hyper_kernel.cpp"
#include <map>
#include <sstream>

using Clock = std::chrono::steady_clock;
static I ns(Clock::time_point start) {
 return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();
}
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
struct Trace {I update_ns,query_ns;size_t query_count;double lower,upper;int size;I m,cap;Metrics mt;};
struct Query {size_t step;std::string label;I numerator;int denominator;double upper;std::vector<int> vertices;};

static std::vector<Report> read_reports(const std::string& path,size_t count) {
 std::ifstream input(path);if(!input)throw std::runtime_error("Cannot open queries TSV");
 std::vector<Report> reports;std::string line;
 while(std::getline(input,line)) {
  if(!line.empty()&&line.back()=='\r')line.pop_back();
  if(line.empty())continue;
  auto tab=line.find('\t');if(tab==std::string::npos)throw std::runtime_error("Query must be step<TAB>label, no header");
  auto token=line.substr(0,tab);
  if(token.empty()||token.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("Invalid zero-based query step");
  auto step=std::stoull(token);
  if(step>=count||(!reports.empty()&&step<reports.back().step))throw std::runtime_error("Query steps must be in-range and nondecreasing");
  reports.push_back({size_t(step),line.substr(tab+1)});
 }
 return reports;
}

int main(int argc,char**argv) try {
 if(argc!=10) {
  std::cerr<<"cap_calendar UPDATES PREFIX eps_num eps_den rank B mode check_level QUERIES.tsv\n";return 1;
 }
 const std::string prefix=argv[2],query_path=argv[9];
 int ep=std::stoi(argv[3]),eq=std::stoi(argv[4]),rank=std::stoi(argv[5]);
 I b=std::stoll(argv[6]);int mode=std::stoi(argv[7]),check=std::stoi(argv[8]);
 if(check<0||check>2)throw std::runtime_error("Bad check level");
 for(const std::string suffix:{".csv",".witnesses.jsonl",".stats.json"})
  if(std::ifstream(prefix+suffix))throw std::runtime_error("Output prefix already exists");
 auto parse_start=Clock::now();std::ifstream input(argv[1]);int n;
 if(!(input>>n)||n<1)throw std::runtime_error("Invalid input header");
 std::vector<Event> events;std::map<int,std::vector<int>> current;
 std::string line,extra;std::getline(input,line);
 while(std::getline(input,line)) {
  if(line.empty())continue;
  std::istringstream row(line);Event event;int k;
  if(!(row>>event.op>>event.id>>k)||event.id<0||k<1||k>rank||(event.op!='+'&&event.op!='-'))throw std::runtime_error("Invalid event");
  event.edge.resize(k);
  for(int&v:event.edge)if(!(row>>v)||v<0||v>=n)throw std::runtime_error("Invalid endpoint");
  if((row>>extra)||!std::is_sorted(event.edge.begin(),event.edge.end())||std::adjacent_find(event.edge.begin(),event.edge.end())!=event.edge.end())throw std::runtime_error("Noncanonical hyperedge or extra token");
  if(event.op=='+'){if(current.count(event.id))throw std::runtime_error("Active ID reused");current[event.id]=event.edge;}
  else {auto found=current.find(event.id);if(found==current.end()||found->second!=event.edge)throw std::runtime_error("Invalid deletion");current.erase(found);}
  events.push_back(std::move(event));
 }
 if(events.empty())throw std::runtime_error("Empty stream");current.clear();
 const auto reports=read_reports(query_path,events.size());I parse_ns=ns(parse_start);
 std::vector<Trace> trace;trace.reserve(events.size());
 std::vector<Query> queries;queries.reserve(reports.size());
 auto init_start=Clock::now();CapDS ds(n,rank,ep,eq,b,mode);I init_ns=ns(init_start);
 I total_update_ns=0,total_query_ns=0;size_t next_report=0;
 auto batch_start=Clock::now();
 for(size_t step=0;step<events.size();step++) {
  const auto&event=events[step];ds.mt={};auto update_start=Clock::now();
  if(event.op=='+')ds.insert(event.id,event.edge);else ds.erase(event.id);
  I update_ns=ns(update_start),query_ns=0;total_update_ns+=update_ns;size_t query_count=0;
  while(next_report<reports.size()&&reports[next_report].step==step) {
   auto query_start=Clock::now();
   double lower=ds.density(),upper=ds.upper();int size=ds.ssize;
   queries.push_back({step,reports[next_report].label,ds.sm,size,upper,ds.witness});
   I one_query_ns=ns(query_start);query_ns+=one_query_ns;total_query_ns+=one_query_ns;
   if(size&&std::abs(lower-double(ds.sm)/size)>1e-12)throw std::runtime_error("Query density mismatch");
   ++next_report;++query_count;
  }
  if(check==2||(check==1&&query_count))ds.check();
  trace.push_back({update_ns,query_ns,query_count,ds.density(),ds.upper(),ds.ssize,(I)ds.active.size(),ds.cap,ds.mt});
 }
 I batch_ns=ns(batch_start);
 if(next_report!=reports.size())throw std::runtime_error("Unconsumed query schedule");
 const auto peak_working_set_bytes=cap_platform::peak_working_set_bytes();
 std::ofstream output(prefix+".csv"),witnesses(prefix+".witnesses.jsonl"),stats(prefix+".stats.json");
 if(!output||!witnesses||!stats)throw std::runtime_error("Output open failed");
 output<<"step,op,update_ns,query_ns,density,upper,witness_size,edges,bfs,scans,augment,moved,witness_changes,cap,peels,query_count\n"<<std::setprecision(17);
 for(size_t step=0;step<trace.size();step++) {
  const auto&t=trace[step];output<<step<<','<<events[step].op<<','<<t.update_ns<<','<<t.query_ns<<','<<t.lower<<','<<t.upper<<','<<t.size<<','<<t.m<<','<<t.mt.bfs<<','<<t.mt.scans<<','<<t.mt.augment<<','<<t.mt.moved<<','<<t.mt.witnesses<<','<<t.cap<<','<<t.mt.peels<<','<<t.query_count<<'\n';
 }
 witnesses<<std::setprecision(17);
 for(const auto&q:queries) {
  witnesses<<"{\"step\":"<<q.step<<",\"label\":"<<json_string(q.label)<<",\"numerator\":"<<q.numerator<<",\"denominator\":"<<q.denominator<<",\"upper\":"<<q.upper<<",\"vertices\":[";
  for(size_t i=0;i<q.vertices.size();i++){if(i)witnesses<<',';witnesses<<q.vertices[i];}witnesses<<"]}\n";
 }
 stats<<"{\"status\":\"complete\",\"updates\":"<<events.size()<<",\"queries\":"<<queries.size()<<",\"variant\":"<<CAP_VARIANT<<",\"B\":"<<b<<",\"epsilon_numerator\":"<<ep<<",\"epsilon_denominator\":"<<eq<<",\"rank\":"<<rank<<",\"mode\":"<<mode<<",\"check_level\":"<<check<<",\"parse_ns\":"<<parse_ns<<",\"initialization_ns\":"<<init_ns<<",\"update_ns\":"<<total_update_ns<<",\"query_ns\":"<<total_query_ns<<",\"batch_ns\":"<<batch_ns<<",\"peak_process_working_set_bytes\":"<<peak_working_set_bytes<<",\"historical_edge_slots\":"<<ds.edges.size()<<",\"query_schedule\":"<<json_string(query_path)<<",\"timing_eligible\":"<<(check==0?"true":"false")<<",\"batch_scope\":\"Includes loop instrumentation, output buffering, witness export copies and enabled checks; excludes parsing, initialization and final serialization.\",\"memory_scope\":\"Whole process including preloaded input and buffered output; not algorithm-only space.\"}\n";
 if(!output||!witnesses||!stats)throw std::runtime_error("Output write failed");
 std::cerr<<"completed "<<events.size()<<" updates, "<<queries.size()<<" calendar reports, variant="<<CAP_VARIANT<<'\n';return 0;
}catch(const std::exception&error){std::cerr<<error.what()<<'\n';return 2;}
