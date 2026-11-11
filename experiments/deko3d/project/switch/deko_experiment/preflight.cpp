// Offline probe of the actual SDK GLSL converter, not an emulation of deko3d.
#include <rex/ui/deko3d/glsl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>
#include <spirv_glsl.hpp>
#include "constant_layout.h"
#include "deferred_discard.h"

static std::string Escape(const std::string& s) {
  std::string out;
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else if (c >= 32) out += c;
  }
  return out;
}
// Compare values at offsets reflected independently from the old and patched
// shader modules. This does not use the move list to decide where to read.
static bool SameShaderValues(spirv_cross::Compiler& old_ir, spirv_cross::Compiler& new_ir,
    uint32_t variable, const std::vector<uint8_t>& source, const std::vector<uint8_t>& packed) {
  const auto& old_type=old_ir.get_type(old_ir.get_type_from_variable(variable).self);
  const auto& new_type=new_ir.get_type(new_ir.get_type_from_variable(variable).self);
  if(old_type.member_types.size()!=new_type.member_types.size()) return false;
  for(uint32_t i=0;i<old_type.member_types.size();++i) {
    const auto& a=old_ir.get_type(old_type.member_types[i]);
    const auto& b=new_ir.get_type(new_type.member_types[i]);
    const uint32_t count=a.array.empty()?1:a.array[0];
    const uint32_t bytes=a.width/8*a.vecsize;
    const uint32_t old_stride=a.array.empty()?0:old_ir.type_struct_member_array_stride(old_type,i);
    const uint32_t new_stride=b.array.empty()?0:new_ir.type_struct_member_array_stride(new_type,i);
    for(uint32_t j=0;j<count;++j) {
      const uint32_t src=old_ir.type_struct_member_offset(old_type,i)+old_stride*j;
      const uint32_t dst=new_ir.type_struct_member_offset(new_type,i)+new_stride*j;
      if(uint64_t(src)+bytes>source.size() || uint64_t(dst)+bytes>packed.size() ||
          std::memcmp(source.data()+src,packed.data()+dst,bytes)) return false;
    }
  }
  return true;
}
int main(int argc, char** argv) {
  if (argc != 3 && !(argc == 4 && std::string(argv[3]) == "--adapt-constants")) {
    std::cerr << "deko_preflight input-directory output-directory [--adapt-constants]\n"; return 2;
  }
  const bool adapt = argc == 4;
  std::filesystem::create_directories(argv[2]);
  std::ofstream report(std::filesystem::path(argv[2]) / "conversion.jsonl");
  std::ofstream interfaces(std::filesystem::path(argv[2]) / "interfaces.jsonl");
  std::ofstream moves(std::filesystem::path(argv[2]) / "constant-moves.jsonl");
  unsigned count = 0, converted = 0;
  for (const auto& file : std::filesystem::directory_iterator(argv[1])) {
    if (file.path().extension() != ".spv") continue;
    std::ifstream in(file.path(), std::ios::binary | std::ios::ate);
    auto size = in.tellg();
    if (size < 20 || size > 16 * 1024 * 1024 || size % 4) return 3;
    std::vector<uint32_t> words(static_cast<size_t>(size) / 4);
    in.seekg(0); in.read(reinterpret_cast<char*>(words.data()), size);
    if (!in || words.front() != 0x07230203) return 3;
    bool deferred_discard=false;
    if (adapt) {
      try {
        auto plan = green_lantern::deko::AdaptConstants(words);
        spirv_cross::Compiler original_reflection(words);
        spirv_cross::Compiler adapted_reflection(plan.spirv);
        for (const auto& b : plan.buffers) {
          // Three distinct byte patterns check every mapped value. Source
          // padding is deliberately not compared; destination padding is zero.
          for (uint32_t pattern=0;pattern<3;++pattern) {
            std::vector<uint8_t> src(b.source_bytes);
            for (size_t k=0;k<src.size();++k) src[k]=uint8_t((k*37u+(k>>8)*53u)^(pattern*91u));
            auto packed=b.Pack(src);
            if(!SameShaderValues(original_reflection,adapted_reflection,b.id,src,packed)) return 5;
            // A deliberate corrupt upload must be detected by the independent reader.
            if(!b.moves.empty()) {
              packed[b.moves.front().destination]^=1;
              if(SameShaderValues(original_reflection,adapted_reflection,b.id,src,packed)) return 5;
            }
          }
          moves << "{\"file\":\"" << Escape(file.path().filename().string())
            << "\",\"buffer\":\"" << Escape(b.name) << "\",\"set\":" << b.set
            << ",\"binding\":" << b.binding << ",\"source_bytes\":" << b.source_bytes
            << ",\"destination_bytes\":" << b.destination_bytes << ",\"moves\":[";
          bool first=true;
          for (const auto& m:b.moves) {
            if(!first) moves << ','; first=false;
            moves << '[' << m.source << ',' << m.destination << ',' << m.bytes << ']';
          }
          moves << "]}\n";
        }
        words=std::move(plan.spirv);
        deferred_discard=green_lantern::deko::LowerDeferredDiscard(words);
        std::ofstream spv(std::filesystem::path(argv[2])/(file.path().filename().string()+".adapted.spv"),std::ios::binary);
        spv.write(reinterpret_cast<const char*>(words.data()),words.size()*4);
      } catch (const std::exception& e) {
        std::cerr << file.path().filename() << ": ABI adaptation failed: " << e.what() << '\n';
        return 5;
      }
    }
    try {
      spirv_cross::Compiler reflection(words);
      const auto resources = reflection.get_shader_resources();
      for (const auto& resource : resources.uniform_buffers) {
        const auto& type = reflection.get_type(resource.base_type_id);
        interfaces << "{\"file\":\"" << Escape(file.path().filename().string())
          << "\",\"buffer\":\"" << Escape(resource.name) << "\",\"set\":"
          << reflection.get_decoration(resource.id, spv::DecorationDescriptorSet)
          << ",\"binding\":" << reflection.get_decoration(resource.id, spv::DecorationBinding)
          << ",\"bytes\":" << reflection.get_declared_struct_size(type)
          << ",\"member_offsets\":[";
        for (uint32_t i = 0; i < type.member_types.size(); ++i) {
          if (i) interfaces << ',';
          interfaces << reflection.type_struct_member_offset(type, i);
        }
        interfaces << "]}\n";
      }
    } catch (const std::exception& e) {
      std::cerr << file.path().filename() << ": reflection failed: " << e.what() << '\n';
      return 3;
    }
    // This probes interface conversion only. The SDK specialization value zero
    // does not reproduce the Vulkan modification word or a rendered frame.
    auto result = rex::deko3d::SpirvAGlsl(words, 0);
    ++count;
    const bool ok = !result.glsl.empty();
    converted += ok;
    report << "{\"file\":\"" << Escape(file.path().filename().string())
           << "\",\"converted\":" << (ok ? "true" : "false")
           << ",\"error\":\"" << Escape(result.error) << "\",\"warnings\":"
           << result.avisos.size() << ",\"deferred_discard\":" << (deferred_discard?"true":"false") << "}\n";
    if (ok) {
      std::ofstream glsl(std::filesystem::path(argv[2]) / (file.path().filename().string() + ".glsl"));
      glsl << result.glsl;
    }
  }
  std::cout << "modules=" << count << " converted=" << converted << " rejected=" << count-converted << '\n';
  return !count ? 4 : converted != count ? 1 : 0;
}
