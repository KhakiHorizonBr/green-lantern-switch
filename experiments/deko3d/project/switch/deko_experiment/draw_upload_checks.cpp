// SPDX-License-Identifier: GPL-3.0-or-later
#include "draw_upload_plan.h"
#include "attachment_plan.h"
#include "presentation_plan.h"
#include "target_plan.h"
#include <iostream>
#include <vector>
#include <stdexcept>
#include <algorithm>
using namespace green_lantern::deko;
void Require(bool valid,const char* error){if(!valid)throw std::runtime_error(error);}
int main() {
  try {
    Require(TargetHeight(352,720)==720,"resolve must grow truncated 352-row target to 720");
    Require(TargetHeight(720,352)==720,"later draw must not shrink expanded target");
    Require(TargetHeight(0,721)==736 && TargetHeight(0,9000)==2048,"target alignment/limit invalid");
    const auto all=[](uint32_t){return true;};
    Require(TileSpan(5,16,0,8,all)==3,"copy crossed source row");
    Require(TileSpan(2040,16,0,0,all)==8,"scratch copy exceeded EDRAM");
    Require(TileSpan(5,16,0,0,[](uint32_t tile){return tile<8;})==3,"copy crossed ownership change");
    // Compare each grouped source coordinate to the old per-tile coordinates
    // across different source and destination pitches, including row splits.
    for(uint32_t sp=1;sp<33;++sp)for(uint32_t dp=1;dp<33;++dp) {
      uint32_t x=0;const uint32_t tile0=37;
      while(x<dp) {
        const uint32_t tile=tile0+x,n=TileSpan(tile,dp-x,0,sp,all);
        Require(n && n<=dp-x,"empty/oversized grouped copy");
        for(uint32_t i=0;i<n;++i)Require((tile+i)/sp==tile/sp &&
            (tile+i)%sp==tile%sp+i,"grouped copy differs from per-tile mapping");
        x+=n;
      }
    }
    PresentationPlan presentation;
    Require(PlanPresentation(1280,1440,1280,720,1280,720,presentation) &&
        presentation.viewport_width==1280 && presentation.viewport_height==1440 &&
        presentation.height==720,"padded resolve must crop to visible height");
    Require(PlanPresentation(1280,360,1280,360,1280,720,presentation) &&
        presentation.viewport_height==720 && presentation.height==720,
        "low-resolution frontbuffer must fill the video display");
    Require(PlanPresentation(1280,720,1280,720,1280,720,presentation) &&
        presentation.viewport_height==720,"normal presentation changed");
    Require(!PlanPresentation(0,720,1280,720,1280,720,presentation),"empty source accepted");
    // The real crash-prone API boundary: RT1-only must keep output index 1,
    // while supplying a non-null, write-disabled view in slot 0.
    int a=1,b=2;uint32_t holes=0;
    std::array<const int*,4> targets{nullptr,&a,nullptr,&b};
    Require(PlanAttachments(targets,4,holes) && holes==5 && targets[0]==&a &&
        targets[1]==&a && targets[2]==&a && targets[3]==&b,"sparse MRT plan invalid");
    targets={};Require(!PlanAttachments(targets,1,holes),"all-null MRT accepted");
    Require(PlanAttachments(targets,0,holes) && !holes,"depth-only draw rejected");
    Require(!PlanAttachments(targets,5,holes),"MRT count overflow accepted");
    std::vector<uint8_t> system(504),vs(32),ps(64),boolean(160),fetch(768),vertices(1024),indices(12);
    DrawUploadSources source;source.constants={system,vs,ps,boolean,fetch};
    source.storage[0]=vertices;source.indices=indices;
    DrawUploadRequirements request;request.constant_masks={27,29,1};request.storage_counts={1,0,0};
    request.count=3;request.first_index=1;request.index_bytes=2;request.indexed=true;
    DrawUploadPlan plan;
    Require(PlanDrawUploads(request,source,plan),"valid indexed draw rejected");
    Require(plan.count==7 && plan.bytes==3340,"shared buffers duplicated or allocation size wrong");
    const uint64_t offsets[]={0,768,1024,1280,1536,2304,3328};
    const uint32_t bytes[]={528,32,64,160,768,1024,12};
    for(uint32_t i=0;i<7;++i)Require(plan.allocations[i].offset==offsets[i] &&
        plan.allocations[i].destination_bytes==bytes[i],"wrong constant/storage/index range");
    auto bad=request;bad.first_index=UINT32_MAX;
    Require(!PlanDrawUploads(bad,source,plan),"index offset overflow accepted");
    bad=request;bad.count=7;Require(!PlanDrawUploads(bad,source,plan),"index overrun accepted");
    bad=request;bad.index_bytes=1;Require(!PlanDrawUploads(bad,source,plan),"8-bit index buffer accepted");
    bad=request;bad.constant_masks[0]=32;Require(!PlanDrawUploads(bad,source,plan),"unknown UBO binding accepted");
    bad=request;bad.storage_counts[2]=17;Require(!PlanDrawUploads(bad,source,plan),"too many SSBOs accepted");
    auto missing=source;missing.constants[0]=std::span<const uint8_t>(system).first(503);
    Require(!PlanDrawUploads(request,missing,plan),"truncated system constants accepted");
    missing=source;missing.storage[0]={};Require(!PlanDrawUploads(request,missing,plan),"absent vertex memory accepted");
    missing=source;missing.constants[2]={};Require(!PlanDrawUploads(request,missing,plan),"absent PS constants accepted");
    std::vector<uint8_t> huge(65537);missing=source;missing.constants[1]=huge;
    Require(!PlanDrawUploads(request,missing,plan),"oversized UBO accepted");
    bad={};bad.count=3;bad.constant_masks[0]=1;bad.storage_counts[0]=1;
    Require(PlanDrawUploads(bad,source,plan) && plan.count==2 && plan.bytes==1792,
        "non-indexed draw requires unused fragment/index data");
    for(uint32_t i=0;i<504;++i)system[i]=uint8_t(i);
    std::array<uint8_t,528> packed;PackSystem(system.data(),packed.data());
    Require(std::equal(system.begin()+496,system.begin()+500,packed.begin()+128) &&
        std::equal(system.begin()+500,system.end(),packed.begin()+132),"system ABI changed");
    std::cout<<"PASS deko: target growth 352->720, no shrink, 1024 tile-row mappings, scratch/ownership bounds; presentation, MRT, uploads and ABI\n";
    return 0;
  }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
