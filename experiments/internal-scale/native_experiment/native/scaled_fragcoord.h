// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <unordered_map>

namespace green_lantern::native::scaling {
// SPIR-V transformation limited to the translator's FragCoord input loads.
// Preserve guest pixel coordinates while the raster viewport is smaller.
inline bool FixFragCoord(const std::vector<uint8_t>& bytes, float inverse,
                         std::vector<uint32_t>& result, uint32_t& patched) {
  patched = 0;
  if (bytes.size() < 20 || bytes.size() % 4) return false;
  std::vector<uint32_t> src(bytes.size()/4);
  std::memcpy(src.data(), bytes.data(), bytes.size());
  if (src[0] != 0x07230203) return false;
  uint32_t frag = 0, f32 = 0, v4 = 0;
  std::unordered_map<uint32_t,uint32_t> constants;
  for (size_t i=5; i<src.size();) {
    const uint32_t n=src[i]>>16, op=src[i]&65535;
    if (!n || i+n>src.size()) return false;
    if (op==71 && n==5 && src[i+2]==11 && src[i+3]==15) return false;
    if (op==71 && n==4 && src[i+2]==11 && src[i+3]==15) frag=src[i+1];
    if (op==22 && n==3 && src[i+2]==32) f32=src[i+1];
    if (op==43 && n==4) constants[src[i+2]]=src[i+3];
    i+=n;
  }
  if (!frag) { result=std::move(src); return true; }
  for (size_t i=5; i<src.size(); i+=src[i]>>16)
    if ((src[i]&65535)==23 && (src[i]>>16)==4 && src[i+2]==f32 && src[i+3]==4) v4=src[i+1];
  if (!f32 || !v4) return false;
  uint32_t next=src[3], factor=next++, one=next++, factor4=next++;
  uint32_t factor_bits,one_bits; const float one_float=1.0f;
  std::memcpy(&factor_bits,&inverse,4); std::memcpy(&one_bits,&one_float,4);
  const std::vector<uint32_t> globals = {
    (4u<<16)|43, f32, factor, factor_bits,
    (4u<<16)|43, f32, one, one_bits,
    (7u<<16)|44, v4, factor4, factor, factor, one, one};
  // -1 = vector input, 0..3 = direct component pointer.
  std::unordered_map<uint32_t,int> pointers; pointers[frag]=-1;
  result.assign(src.begin(),src.begin()+5); bool inserted=false;
  for (size_t i=5; i<src.size();) {
    const uint32_t n=src[i]>>16,op=src[i]&65535;
    if (op==54 && !inserted) { result.insert(result.end(),globals.begin(),globals.end()); inserted=true; }
    if ((op==65 || op==66) && n>=5 && pointers.count(src[i+3])) {
      if (n!=5 || pointers[src[i+3]]!=-1 || !constants.count(src[i+4]) || constants[src[i+4]]>3) return false;
      pointers[src[i+2]]=int(constants[src[i+4]]);
    }
    if (op==83 && n==4 && pointers.count(src[i+3])) pointers[src[i+2]]=pointers[src[i+3]];
    if (op==61 && n>=4 && pointers.count(src[i+3])) {
      const int component=pointers[src[i+3]];
      if (component<2) {
        if ((component==-1 && src[i+1]!=v4) || (component>=0 && src[i+1]!=f32)) return false;
        const uint32_t loaded=next++;
        const size_t begin=result.size();
        result.insert(result.end(),src.begin()+i,src.begin()+i+n);
        result[begin+2]=loaded;
        result.insert(result.end(),{(5u<<16)|133,src[i+1],src[i+2],loaded,component==-1?factor4:factor});
        ++patched; i+=n; continue;
      }
    }
    result.insert(result.end(),src.begin()+i,src.begin()+i+n); i+=n;
  }
  if (!inserted) return false;
  result[3]=next; return true;
}
}
