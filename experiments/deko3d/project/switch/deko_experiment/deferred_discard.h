// Restricted lowering for pure fragment shaders. Preserve execution (including
// derivatives) after demote and discard at entry-point return. Reject observable
// side effects, helper queries, calls and unknown store destinations.
#pragma once
#include <spirv_glsl.hpp>
#include <map>
#include <stdexcept>
#include <vector>
namespace green_lantern::deko {
inline bool LowerDeferredDiscard(std::vector<uint32_t>& words) {
  bool demote=false; uint32_t entry=0, entries=0;
  std::map<uint32_t,uint32_t> storage;
  for(size_t i=5;i<words.size();) {
    const uint32_t n=words[i]>>16, op=words[i]&65535;
    if(!n || i+n>words.size()) throw std::runtime_error("invalid discard input");
    demote|=op==5380;
    if(op==15) {++entries;if(words[i+1]==4) entry=words[i+2];}
    if(op==59) storage[words[i+2]]=words[i+3];
    if((op==65 || op==66) && storage.count(words[i+3])) storage[words[i+2]]=storage.at(words[i+3]);
    i+=n;
  }
  if(!demote) return false;
  if(entries!=1 || !entry) throw std::runtime_error("discard lowering requires one fragment entry");
  for(size_t i=5;i<words.size();) {
    const uint32_t n=words[i]>>16, op=words[i]&65535;
    if(op==5381 || op==57 || op==99 || (op>=227 && op<=242) ||
       op==318 || op==319 || op==63 || op==64 || op==254 ||
       (op>=333 && op<=366) ||
       (op==71 && n==4 && words[i+2]==spv::DecorationBuiltIn &&
        words[i+3]==spv::BuiltInHelperInvocation))
      throw std::runtime_error("discard lowering refuses helper queries, calls or side effects");
    if(op==62) {
      auto it=storage.find(words[i+1]);
      if(it==storage.end() || (it->second!=3 && it->second!=6 && it->second!=7))
        throw std::runtime_error("discard lowering refuses unproven store");
    }
    i+=n;
  }
  uint32_t next=words[3];
  const uint32_t boolean=next++, pointer=next++, yes=next++, no=next++, flag=next++;
  std::vector<uint32_t> out(words.begin(),words.begin()+5);
  auto emit=[&](uint32_t op,std::initializer_list<uint32_t> operands) {
    out.push_back(uint32_t((operands.size()+1)<<16)|op);
    out.insert(out.end(),operands.begin(),operands.end());
  };
  bool globals=false;uint32_t function=0;
  for(size_t i=5;i<words.size();) {
    const uint32_t n=words[i]>>16, op=words[i]&65535;
    if(op==17 && words[i+1]==5379) {i+=n;continue;}
    if(op==10) {
      std::string extension(reinterpret_cast<const char*>(&words[i+1]),(n-1)*4);
      if(extension.find("SPV_EXT_demote_to_helper_invocation")!=std::string::npos) {i+=n;continue;}
    }
    if(op==54) {
      function=words[i+2];
      if(!globals) {
        emit(20,{boolean});emit(32,{pointer,6,boolean});
        emit(41,{boolean,yes});emit(42,{boolean,no});emit(59,{pointer,flag,6,no});globals=true;
      }
    }
    // SPIR-V 1.4+ lists all global variables used by an entry point, including
    // Private storage. Without it SPIRV-Cross may omit the flag declaration.
    if(op==15 && words[1]>=0x00010400) {
      out.push_back(((n+1)<<16)|op);
      out.insert(out.end(),words.begin()+i+1,words.begin()+i+n);
      out.push_back(flag);
    } else if(op==5380) emit(62,{flag,yes});
    else if(op==253 && function==entry) {
      const uint32_t value=next++,kill=next++,merge=next++;
      emit(61,{boolean,value,flag});emit(247,{merge,0});emit(250,{value,kill,merge});
      emit(248,{kill});emit(252,{});emit(248,{merge});emit(253,{});
    } else out.insert(out.end(),words.begin()+i,words.begin()+i+n);
    i+=n;
  }
  out[3]=next;words=std::move(out);return true;
}
}
