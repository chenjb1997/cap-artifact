#pragma once
#include <zlib.h>
#include <streambuf>
#include <ostream>
#include <stdexcept>
#include <array>

// Streaming sidecar compression only. All calls happen outside service timers.
class GzipBuffer final:public std::streambuf {
 gzFile file=nullptr;
 std::array<char,65536> data{};
 bool okay=true;
 bool drain() {
  auto count=pptr()-pbase();
  if(count&&gzwrite(file,pbase(),static_cast<unsigned>(count))!=count)okay=false;
  setp(data.data(),data.data()+data.size());
  return okay;
 }
protected:
 int_type overflow(int_type ch)override {
  if(!drain())return traits_type::eof();
  if(!traits_type::eq_int_type(ch,traits_type::eof())){*pptr()=traits_type::to_char_type(ch);pbump(1);}
  return traits_type::not_eof(ch);
 }
 int sync()override {
  if(!file)return okay?0:-1;
  if(!drain()||gzflush(file,Z_SYNC_FLUSH)!=Z_OK){okay=false;return -1;}
  return 0;
 }
public:
 explicit GzipBuffer(const std::string&path) {
  file=gzopen(path.c_str(),"wb1");
  if(!file)throw std::runtime_error("Cannot open gzip query output");
  setp(data.data(),data.data()+data.size());
 }
 bool close() {
  if(file){drain();if(gzclose(file)!=Z_OK)okay=false;file=nullptr;}
  return okay;
 }
 ~GzipBuffer()override{close();}
};
class GzipOutput final:public std::ostream {
 GzipBuffer buffer;
public:
 explicit GzipOutput(const std::string&path):std::ostream(nullptr),buffer(path){rdbuf(&buffer);}
 bool close(){flush();return buffer.close()&&bool(*this);}
};
