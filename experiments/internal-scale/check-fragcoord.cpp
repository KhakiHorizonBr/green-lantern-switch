#include "native_experiment/native/scaled_fragcoord.h"
#include <fstream>
#include <iterator>
#include <cassert>
#include <iostream>
int main() {
  std::ifstream f("test.spv",std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
  assert(!bytes.empty());
  std::vector<uint32_t> out;uint32_t patched=0;
  assert(green_lantern::native::scaling::FixFragCoord(bytes,4.0f/3.0f,out,patched));
  assert(patched>=3);
  assert(out.size()>bytes.size()/4);
  for(size_t i=5;i<out.size();) { auto n=out[i]>>16;assert(n && i+n<=out.size());i+=n; }
  std::ofstream result("test-scaled.spv",std::ios::binary);
  result.write(reinterpret_cast<const char*>(out.data()),out.size()*4);
  assert(!green_lantern::native::scaling::FixFragCoord({},2,out,patched));
  bytes.pop_back();assert(!green_lantern::native::scaling::FixFragCoord(bytes,2,out,patched));
  std::cout<<"FragCoord vector+scalar rewrites and malformed input checks passed\n";
}
