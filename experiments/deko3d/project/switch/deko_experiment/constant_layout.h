// Host-side std140 ABI plan. Each shader decoration and upload byte move comes
// from the same plan; never change offsets without repacking the constants.
#pragma once
#include <spirv_glsl.hpp>
#include <algorithm>
#include <cstring>
#include <map>
#include <stdexcept>
#include <vector>

namespace green_lantern::deko {
struct Move { uint32_t source, destination, bytes; };
struct BufferLayout {
  uint32_t id, set, binding, source_bytes, destination_bytes;
  std::string name;
  std::vector<Move> moves;
  std::vector<uint8_t> Pack(const std::vector<uint8_t>& source) const {
    if (source.size() != source_bytes) throw std::runtime_error("constant input size mismatch");
    std::vector<uint8_t> out(destination_bytes, 0);
    for (const auto& m : moves) {
      if (uint64_t(m.source)+m.bytes > source.size() ||
          uint64_t(m.destination)+m.bytes > out.size()) throw std::runtime_error("constant move out of bounds");
      std::memcpy(out.data()+m.destination, source.data()+m.source, m.bytes);
    }
    return out;
  }
};
struct LayoutPlan {
  std::vector<uint32_t> spirv;
  std::vector<BufferLayout> buffers;
};

inline LayoutPlan AdaptConstants(const std::vector<uint32_t>& words) {
  spirv_cross::Compiler reflection(words);
  LayoutPlan out{words, {}};
  // Decoration key = target, member (UINT32_MAX means a type), decoration.
  using Key = std::tuple<uint32_t,uint32_t,uint32_t>;
  std::map<Key,uint32_t> replacements;
  auto round = [](uint32_t n, uint32_t a) { return (n+a-1)/a*a; };
  struct Shape { uint32_t alignment, bytes; std::vector<Move> moves; };
  auto shape = [&](auto&& self, uint32_t type_id) -> Shape {
    const auto& t = reflection.get_type(type_id);
    if (t.pointer || t.columns != 1 || t.width != 32 ||
        (t.basetype != spirv_cross::SPIRType::Float &&
         t.basetype != spirv_cross::SPIRType::Int &&
         t.basetype != spirv_cross::SPIRType::UInt))
      throw std::runtime_error("unsupported constant type (requires explicit ABI implementation)");
    if (t.array.size() > 1) throw std::runtime_error("nested constant arrays unsupported");
    const uint32_t alignment = t.vecsize == 1 ? 4 : t.vecsize == 2 ? 8 : 16;
    const uint32_t bytes = t.vecsize * 4;
    if (t.array.empty()) return {alignment,bytes,{{0,0,bytes}}};
    if (!t.array_size_literal[0] || !t.array[0] || t.array[0] > 65536)
      throw std::runtime_error("nonliteral or excessive constant array");
    const uint32_t stride = round(bytes,16);
    const uint32_t old_stride = reflection.get_decoration(type_id,spv::DecorationArrayStride);
    if (!old_stride) throw std::runtime_error("constant array missing stride");
    replacements[{type_id,UINT32_MAX,spv::DecorationArrayStride}] = stride;
    Shape s{std::max(16u,alignment),stride*t.array[0],{}};
    for (uint32_t i=0;i<t.array[0];++i) s.moves.push_back({i*old_stride,i*stride,bytes});
    return s;
  };
  for (const auto& resource : reflection.get_shader_resources().uniform_buffers) {
    const auto& t = reflection.get_type(resource.base_type_id);
    BufferLayout b{resource.id,
      reflection.get_decoration(resource.id,spv::DecorationDescriptorSet),
      reflection.get_decoration(resource.id,spv::DecorationBinding),
      uint32_t(reflection.get_declared_struct_size(t)),0,resource.name,{}};
    uint32_t offset = 0;
    for (uint32_t i=0;i<t.member_types.size();++i) {
      Shape s = shape(shape,t.member_types[i]);
      offset = round(offset,s.alignment);
      const uint32_t old_offset = reflection.type_struct_member_offset(t,i);
      replacements[{resource.base_type_id,i,spv::DecorationOffset}] = offset;
      for (const auto& m:s.moves) b.moves.push_back({old_offset+m.source,offset+m.destination,m.bytes});
      offset += s.bytes;
    }
    b.destination_bytes = round(offset,16);
    if (b.destination_bytes > 65536) throw std::runtime_error("constant block exceeds 64 KiB");
    out.buffers.push_back(std::move(b));
  }
  for (size_t i=5;i<out.spirv.size();) {
    const uint32_t n=out.spirv[i]>>16, op=out.spirv[i]&65535;
    if (!n || i+n>out.spirv.size()) throw std::runtime_error("invalid SPIR-V instruction");
    if (op==spv::OpMemberDecorate && n==5) {
      auto it=replacements.find({out.spirv[i+1],out.spirv[i+2],out.spirv[i+3]});
      if(it!=replacements.end()) {out.spirv[i+4]=it->second;replacements.erase(it);}
    } else if(op==spv::OpDecorate && n==4) {
      auto it=replacements.find({out.spirv[i+1],UINT32_MAX,out.spirv[i+2]});
      if(it!=replacements.end()) {out.spirv[i+3]=it->second;replacements.erase(it);}
    }
    i+=n;
  }
  if (!replacements.empty()) throw std::runtime_error("required decoration not found");
  return out;
}
} // namespace green_lantern::deko
