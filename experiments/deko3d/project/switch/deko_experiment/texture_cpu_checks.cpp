// SPDX-License-Identifier: GPL-3.0-or-later
// Storage/endianness checks with synthetic, non-game texture data.
#include "../native_experiment/native/native_texture_cpu.h"
#include <iostream>
#include <array>
#include <stdexcept>
#include <vector>
namespace cpu=green_lantern::native::texture_cpu;
namespace x=rex::graphics::xenos;
namespace tu=rex::graphics::texture_util;
void Require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
uint8_t Value(uint32_t level,uint32_t layer,uint32_t y,uint32_t col,uint32_t byte) {
  return uint8_t(level*79+layer*41+y*17+col*7+byte*31+13);
}
void Check(x::TextureFormat format,uint32_t side,bool tiled,bool packed,bool cube,x::Endian endian) {
  x::xe_gpu_texture_fetch_t fetch{};
  fetch.dimension=cube?x::DataDimension::kCube:x::DataDimension::k2DOrStacked;
  fetch.size_2d.width=side-1;fetch.size_2d.height=side-1;fetch.size_2d.stack_depth=cube?5:0;
  fetch.base_address=1;fetch.mip_address=4096;fetch.mip_max_level=6;
  fetch.pitch=8;fetch.format=format;fetch.tiled=tiled;fetch.packed_mips=packed;fetch.endianness=endian;
  cpu::DecodePlan p;Require(cpu::Prepare(fetch,false,p),"prepare failed");
  // Pad sources to whole words: endian gather may read another byte in the
  // final word of a tiny image, as the real guest mapping permits.
  std::vector<uint8_t> base((p.base_bytes+3)&~3u),mips((p.mip_bytes+3)&~3u);
  std::vector<uint8_t> expected(size_t(p.staging_bytes),0),actual(size_t(p.staging_bytes)+64,0xA7);
  const auto& j=p.job;
  for(uint32_t l=0;l<j.level_count;++l) {
    const auto& dst=j.levels[l];const bool tail=j.layout.packed_level!=UINT32_MAX && l>=j.layout.packed_level;
    const uint32_t stored=tail?j.layout.packed_level:l;
    const auto& src=l?j.layout.mips[stored]:j.layout.base;
    uint32_t ox=0,oy=0,oz=0;
    if(tail)tu::GetPackedMipOffset(j.width,j.height,1,j.format,l,ox,oy,oz);
    auto& bytes=l?mips:base;
    for(uint32_t layer=0;layer<j.layers;++layer)
      for(uint32_t y=0;y<dst.blocks_y;++y)for(uint32_t col=0;col<dst.blocks_x;++col) {
        const uint64_t offset=(l?j.layout.mip_offsets_bytes[stored]:0)+
            uint64_t(src.array_slice_stride_bytes)*layer+
            (tiled?uint64_t(tu::GetTiledOffset2D(col+ox,y+oy,src.row_pitch_bytes>>j.bpb_log2,j.bpb_log2)):
              uint64_t(y+oy)*src.row_pitch_bytes+uint64_t(col+ox)*j.guest_bytes_per_block);
        for(uint32_t b=0;b<j.guest_bytes_per_block;++b) {
          const uint32_t swap=endian==x::Endian::k8in16?1:endian==x::Endian::k8in32?3:endian==x::Endian::k16in32?2:0;
          Require(((offset+b)^swap)<bytes.size(),"source extent too small");
          bytes[size_t((offset+b)^swap)]=Value(l,layer,y,col,b);
        }
        const uint32_t host_block=j.conversion==cpu::Conversion::kDXT3A?16:j.guest_bytes_per_block;
        const size_t target=size_t(dst.offset+dst.layer_bytes*layer+uint64_t(y)*dst.host_row_bytes+col*host_block);
        for(uint32_t b=0;b<host_block;++b)
          expected[target+b]=b<j.guest_bytes_per_block?Value(l,layer,y,col,b):0;
      }
  }
  Require(cpu::Decode(p,base,mips,std::span<uint8_t>(actual).first(size_t(p.staging_bytes))),"decode failed");
  if(!std::equal(expected.begin(),expected.end(),actual.begin())) {
    const auto at=std::mismatch(expected.begin(),expected.end(),actual.begin()).first-expected.begin();
    std::cerr<<"format="<<uint32_t(format)<<" size="<<side<<" tiled="<<tiled<<" packed="<<packed
        <<" cube="<<cube<<" endian="<<uint32_t(endian)<<" byte="<<at
        <<" expected="<<uint32_t(expected[at])<<" actual="<<uint32_t(actual[at])<<'\n';
    Require(false,"texel/mip/layer mismatch");
  }
  Require(std::all_of(actual.end()-64,actual.end(),[](uint8_t v){return v==0xA7;}),"output overrun");
  Require(!cpu::Decode(p,{},mips,actual),"short base accepted");
  Require(!cpu::Decode(p,base,mips,std::span<uint8_t>(actual).first(size_t(p.staging_bytes)-1)),"short output accepted");
}
int main() {
  try {
    uint32_t cases=0;
    for(auto f:{x::TextureFormat::k_8,x::TextureFormat::k_8_8,x::TextureFormat::k_8_8_8_8,
        x::TextureFormat::k_16_16_16_16,x::TextureFormat::k_32_32_32_32_FLOAT,
        x::TextureFormat::k_DXT1,x::TextureFormat::k_DXT4_5,x::TextureFormat::k_DXT3A})
      for(uint32_t side:{8u,32u,63u})for(bool tiled:{false,true})for(bool packed:{false,true})
        for(bool cube:{false,true})for(uint32_t e=0;e<4;++e) {
          Check(f,side,tiled,packed,cube,x::Endian(e));++cases;
        }
    // A two-byte texel inside a 32-bit swapped word must gather its bytes,
    // rather than perform a zero-length four-byte copy.
    alignas(4) const uint8_t input[4]={0,0,0xF8,0x00};
    uint16_t out=0;
    cpu::ConvertBlock(cpu::Conversion::kSwapRB565,x::Endian::k8in32,
        reinterpret_cast<uint8_t*>(&out),input,2,2);
    Require(out==0x001F,"packed16 endian gather mismatch");
    for(uint32_t e=0;e<4;++e) {
      const uint32_t swap=e==1?1:e==2?3:e==3?2:0;
      alignas(4) uint8_t ctx[8]{};
      const uint8_t logical[]={30,240,120,0,0xE4,0xE4,0xE4,0xE4};
      for(uint32_t b=0;b<8;++b)ctx[b^swap]=logical[b];
      std::array<uint8_t,48> texels;texels.fill(0xA7);
      cpu::ConvertBlock(cpu::Conversion::kCTX1,x::Endian(e),texels.data(),ctx,8,8);
      const uint8_t red[]={240,0,160,80},green[]={30,120,60,90};
      for(uint32_t row=0;row<4;++row)for(uint32_t col=0;col<4;++col) {
        Require(texels[row*8+col*2]==red[col] && texels[row*8+col*2+1]==green[col],"CTX1 colors mismatch");
      }
      Require(std::all_of(texels.begin()+32,texels.end(),[](uint8_t b){return b==0xA7;}),"CTX1 overrun");
    }
    std::cout<<"PASS "<<cases<<" mip/layer/format/endian cases, CTX1, packed16, bounds and guards\n";
    return 0;
  }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
