#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <vector>

// Lossless set evidence, outside the query timer. Never stores query history.
// A supplied revision must change whenever membership may change. Callers
// without such a maintained revision omit it and compare actual returned sets.
class WitnessDeltaEncoder {
 std::vector<int> previous;
 bool initialized=false;
 std::optional<uint64_t> previous_revision;
 static void array(std::ostream&out,const std::vector<int>&values) {
  out<<'[';for(size_t j=0;j<values.size();++j){if(j)out<<',';out<<values[j];}out<<']';
 }
public:
 uint64_t records=0,initial_vertices=0,added_vertices=0,removed_vertices=0,unchanged_revision_records=0;
 void write_fields(std::ostream&out,const std::vector<int>&vertices,std::optional<uint64_t> revision=std::nullopt) {
  out<<",\"encoding\":\"set-delta-v1\",\"vertex_count\":"<<vertices.size();++records;
  if(initialized&&revision&&previous_revision&&*revision==*previous_revision) {
   if(vertices.size()!=previous.size())throw std::runtime_error("Witness revision contract violated");
   ++unchanged_revision_records;out<<",\"added_vertices\":[],\"removed_vertices\":[]";return;
  }
  std::vector<int> current(vertices);std::sort(current.begin(),current.end());
  if(std::adjacent_find(current.begin(),current.end())!=current.end())throw std::runtime_error("Duplicate vertex in returned set");
  if(!initialized) {
   out<<",\"vertices\":";array(out,current);initial_vertices+=current.size();initialized=true;
  } else {
   out<<",\"added_vertices\":[";size_t p=0;bool first=true;
   for(int v:current){while(p<previous.size()&&previous[p]<v)++p;if(p==previous.size()||previous[p]!=v){if(!first)out<<',';first=false;out<<v;++added_vertices;}}
   out<<"],\"removed_vertices\":[";p=0;first=true;
   for(int v:previous){while(p<current.size()&&current[p]<v)++p;if(p==current.size()||current[p]!=v){if(!first)out<<',';first=false;out<<v;++removed_vertices;}}
   out<<']';
  }
  previous.swap(current);previous_revision=revision;
 }
 void write_stats(std::ostream&out)const {
  out<<"{\"records\":"<<records<<",\"initial_vertices\":"<<initial_vertices<<",\"added_vertices\":"<<added_vertices<<",\"removed_vertices\":"<<removed_vertices<<",\"unchanged_revision_records\":"<<unchanged_revision_records<<'}';
 }
};
