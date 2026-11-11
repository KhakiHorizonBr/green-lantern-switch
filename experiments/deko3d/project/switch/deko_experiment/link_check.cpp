// Link-only check against the installed Switch libraries. Not a game or NRO.
#include "deko_bindings.h"
#include "deko_images.h"
#include "deko_output.h"
int main(int argc,char**) {
  if(argc!=12345) return 0;
  rex::deko3d::Nucleo recorder;
  green_lantern::deko::ShaderLibrary library(recorder);
  green_lantern::deko::StageBindings bindings;
  green_lantern::deko::Images images(recorder,128u<<20);
  green_lantern::deko::Output output;
  rex::deko3d::Dispositivo device;
  output.Initialize(device,0,0,nullptr,{},{});
  images.Create({});images.Release(0);images.Copy(0,{},0,{});
  DkBufExtents upload{};
  const bool a=green_lantern::deko::BindStage(nullptr,DkStage_Vertex,bindings);
  const bool b=green_lantern::deko::UploadSystem(recorder,nullptr,0,upload);
  return a || b || library.Load({},"empty") ? 1 : 0;
}
