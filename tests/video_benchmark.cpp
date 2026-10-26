#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#include "video.hpp"
#include "music.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <vector>

// GPU-complete batches measure steady rendering, excluding shader compilation,
// audio-device startup, image export and the window's presentation/vsync wait.
int main(int argc,char** argv) {
  using namespace av;
  if(argc<2||argc>4) {std::cerr<<"Usage: video_benchmark OUTPUT_JSON [SCENE=2] [QUALITY=900]\n";return 2;}
  int scene=argc>2?std::stoi(argv[2])-1:1,quality=argc>3?std::stoi(argv[3]):900;
  if(scene<0||scene>=sceneCount||quality<300||quality>2160) return 2;
  SetTraceLogLevel(LOG_WARNING);SetConfigFlags(FLAG_WINDOW_HIDDEN|FLAG_WINDOW_HIGHDPI);
  InitWindow(1280,800,"AudioImage / rendering benchmark");SetTargetFPS(0);
  if(!IsWindowReady()) return 1;
  Video video;Patch patch;patch.scene=patch.previousScene=scene;patch.automatic=false;
  if(!video.init(77,quality)) {CloseWindow();return 1;}
  auto ring=std::make_unique<SampleRing>();auto analyzer=std::make_unique<Analyzer>();
  auto synth=std::make_unique<audioimage::TechnoSynth>(77);std::array<float,1600> block{};
  bool okay=true;
  auto frame=[&] {
    synth->render(block.data(),800);ring->push(block.data(),800);
    okay=video.render(analyzer->update(*ring,1.f/60),patch,1.f/60)&&okay;
  };
  constexpr int warmup=90,batch=15,measured=180;
  for(int i=0;i<warmup;i++) frame();glFinish();
  std::vector<double> milliseconds;
  for(int i=0;i<measured/batch;i++) {
    auto start=std::chrono::steady_clock::now();
    for(int j=0;j<batch;j++) frame();glFinish();
    milliseconds.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/batch);
  }
  double mean=std::accumulate(milliseconds.begin(),milliseconds.end(),0.0)/milliseconds.size();
  std::sort(milliseconds.begin(),milliseconds.end());
  double median=(milliseconds[5]+milliseconds[6])*.5,p95=milliseconds.back();
  std::ofstream output(argv[1]);output<<std::fixed<<std::setprecision(3)
    <<"{\n  \"scene\": "<<scene+1<<",\n  \"quality\": "<<quality
    <<",\n  \"output_width\": "<<video.output().width<<",\n  \"output_height\": "<<video.output().height
    <<",\n  \"warmup_frames\": "<<warmup<<",\n  \"measured_frames\": "<<measured
    <<",\n  \"mean_ms\": "<<mean<<",\n  \"median_ms\": "<<median<<",\n  \"p95_ms\": "<<p95<<"\n}\n";
  okay=okay&&bool(output);output.close();
  std::cout<<std::fixed<<std::setprecision(2)<<scenes[scene]<<" / "<<quality<<"-pixel feedback: mean "<<mean
    <<" ms, median "<<median<<" ms, p95 "<<p95<<" ms per frame (GPU-complete batches).\n";
  video.shutdown();CloseWindow();return okay?0:1;
}
