// SPDX-License-Identifier: GPL-3.0-or-later
// API-independent upload contract: the PC checks the same ranges and sizes
// that the native deko recorder uses. Source data is already prepared by PM4:
// vertex fetch addresses refer to the supplied shared-memory SSBO, not to RAM.
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include "system_upload_layout.h"
namespace green_lantern::deko {
struct DrawUploadSources {
  std::array<std::span<const uint8_t>,5> constants{};
  std::array<std::span<const uint8_t>,16> storage{};
  std::span<const uint8_t> indices;
};
struct DrawUploadRequirements {
  std::array<uint32_t,3> constant_masks{}; // vertex, fragment, geometry
  std::array<uint32_t,3> storage_counts{};
  uint32_t count=0,first_index=0,index_bytes=0;
  bool indexed=false;
};
enum class UploadKind {Constant,Storage,Indices};
struct DrawAllocation {
  UploadKind kind;
  uint32_t binding=0,source_bytes=0,destination_bytes=0;
  uint64_t offset=0;
};
struct DrawUploadPlan {
  std::array<DrawAllocation,22> allocations{};
  uint32_t count=0;
  uint64_t bytes=0;
};
inline bool PlanDrawUploads(const DrawUploadRequirements& requirements,
    const DrawUploadSources& sources,DrawUploadPlan& plan) {
  plan={};if(!requirements.count)return false;
  uint32_t constants=0,storage=0;
  for(uint32_t stage=0;stage<3;++stage) {
    if(requirements.constant_masks[stage]&~31u || requirements.storage_counts[stage]>16)return false;
    constants|=requirements.constant_masks[stage];
    if(requirements.storage_counts[stage]>storage)storage=requirements.storage_counts[stage];
  }
  auto add=[&](UploadKind kind,uint32_t binding,uint32_t source,uint32_t destination) {
    const uint64_t offset=(plan.bytes+255)&~uint64_t(255);
    plan.allocations[plan.count++]={kind,binding,source,destination,offset};
    plan.bytes=offset+destination;
  };
  for(uint32_t binding=0;binding<5;++binding)if(constants&(1u<<binding)) {
    const auto bytes=sources.constants[binding].size();
    if(!binding) {
      if(bytes<kSystemSourceBytes)return false;
      add(UploadKind::Constant,0,kSystemSourceBytes,kSystemDestinationBytes);
    } else {
      if(!bytes || bytes>65536)return false;
      add(UploadKind::Constant,binding,uint32_t(bytes),(uint32_t(bytes)+15)&~15u);
    }
  }
  for(uint32_t binding=0;binding<storage;++binding) {
    const auto bytes=sources.storage[binding].size();
    if(!bytes || bytes>0x20000000)return false;
    add(UploadKind::Storage,binding,uint32_t(bytes),uint32_t(bytes));
  }
  if(requirements.indexed) {
    if((requirements.index_bytes!=2 && requirements.index_bytes!=4) ||
        sources.indices.empty() || sources.indices.size()>0x20000000 ||
        sources.indices.size()%requirements.index_bytes ||
        (uint64_t(requirements.first_index)+requirements.count)*requirements.index_bytes>sources.indices.size())return false;
    add(UploadKind::Indices,0,uint32_t(sources.indices.size()),uint32_t(sources.indices.size()));
  }
  return plan.bytes<=UINT32_MAX-255u;
}
} // namespace green_lantern::deko
