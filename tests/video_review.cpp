#include "video.hpp"
#include "music.hpp"
#include <filesystem>
#include <iostream>
#include <memory>

// Compact, repeatable visual review at quiet, attack and sustained moments.
// All scenes receive the same actual soundtrack and analysis sample clock.
int main(int argc,char** argv) {
  using namespace av;
  if(argc!=2&&argc!=4) {std::cerr<<"Usage: video_review OUTPUT_FOLDER [--scene 1..6]\n";return 2;}
  int selected=-1;
  if(argc==4) {
    if(std::string(argv[2])!="--scene") return 2;
    selected=std::stoi(argv[3])-1;if(selected<0||selected>=sceneCount) return 2;
  }
  std::filesystem::path folder=argv[1];std::filesystem::create_directories(folder);
  SetTraceLogLevel(LOG_WARNING);SetConfigFlags(FLAG_WINDOW_HIDDEN|FLAG_WINDOW_HIGHDPI);
  InitWindow(960,600,"AudioImage / visual review");SetTargetFPS(0);
  if(!IsWindowReady()) return 1;
  std::cout.setf(std::ios::unitbuf);bool okay=true;
  for(int scene=0;scene<sceneCount;scene++) {
    if(selected>=0&&selected!=scene) continue;
    Video video;Patch patch;patch.scene=patch.previousScene=scene;patch.automatic=false;
    if(!video.init(77,600)) {CloseWindow();return 1;}
    auto capture=[&](const char* moment) {
      Image image=LoadImageFromTexture(video.output());ImageFlipVertical(&image);ImageResize(&image,800,500);
      auto path=folder/(std::to_string(scene+1)+"-"+moment+".png");
      okay=ExportImage(image,path.string().c_str())&&okay;UnloadImage(image);
    };
    Signal silent{};
    for(int f=0;f<150;f++) {silent.analyzedSamples+=800;okay=video.render(silent,patch,1.f/60)&&okay;}
    capture("quiet");
    auto ring=std::make_unique<SampleRing>();auto analyzer=std::make_unique<Analyzer>();
    auto synth=std::make_unique<audioimage::TechnoSynth>(77);std::array<float,1600> block{};
    for(int f=0;f<720;f++) {
      synth->render(block.data(),800);ring->push(block.data(),800);
      okay=video.render(analyzer->update(*ring,1.f/60),patch,1.f/60)&&okay;
      if(f==3) capture("attack");
      if(f==119) capture("early");
      if(f==359) capture("middle");
      if(f==719) capture("sustained");
    }
    std::cout<<scenes[scene]<<": quiet, attack, 2 s, 6 s and 12 s captured.\n";
    video.shutdown();
  }
  CloseWindow();return okay?0:1;
}
