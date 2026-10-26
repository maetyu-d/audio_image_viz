#include "video.hpp"
#include "music.hpp"
#include <memory>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <vector>
namespace {
struct Picture {std::vector<Color> pixels;double mean=0;int width=0;};
Picture inspect(av::Video& video) {
  Image image=LoadImageFromTexture(video.output());Color* data=LoadImageColors(image);
  Picture p;p.width=image.width;p.pixels.assign(data,data+image.width*image.height);
  for(auto c:p.pixels) p.mean+=(c.r+c.g+c.b)/3.0/p.pixels.size();
  UnloadImageColors(data);UnloadImage(image);return p;
}
struct Difference {double colour=0,detail=0;};
Difference difference(const Picture& a,const Picture& b) {
  Difference result;
  auto delta=[&](int i,int c){const auto* x=reinterpret_cast<const unsigned char*>(&a.pixels[i]);const auto* y=reinterpret_cast<const unsigned char*>(&b.pixels[i]);return int(x[c])-y[c];};
  for(int i=0;i<int(a.pixels.size());i++) for(int c=0;c<3;c++) {
    result.colour+=std::abs(delta(i,c))/3.0/a.pixels.size();
    if(i>=a.width&&i%a.width) result.detail+=(std::abs(delta(i,c)-delta(i-1,c))+std::abs(delta(i,c)-delta(i-a.width,c)))/6.0/a.pixels.size();
  }
  return result;
}
}
int main(int argc,char** argv) {
  using namespace av;SetTraceLogLevel(LOG_WARNING);SetConfigFlags(FLAG_WINDOW_HIDDEN|FLAG_WINDOW_HIGHDPI);
  InitWindow(800,500,"AudioImage / feedback signal check");SetTargetFPS(0);if(!IsWindowReady()) return 1;
  std::cout.setf(std::ios::unitbuf);
  const std::filesystem::path folder=argc>1?argv[1]:"captures/video-memory";std::filesystem::create_directories(folder);
  bool okay=true;
  bool soundtrackOnly=false;int selected=-1;
  for(int i=2;i<argc;i++) {
    std::string arg=argv[i];
    if(arg=="--soundtrack-only") soundtrackOnly=true;
    else if(arg=="--scene"&&i+1<argc) selected=std::stoi(argv[++i])-1;
    else return 2;
  }
  if(selected>=sceneCount||selected< -1) return 2;
  if(!soundtrackOnly) for(int scene=0;scene<sceneCount;scene++) {
    if(selected>=0&&selected!=scene) continue;
    Video live,cleared,quiet;Patch patch,resetPatch,quietPatch;
    patch.scene=patch.previousScene=resetPatch.scene=resetPatch.previousScene=quietPatch.scene=quietPatch.previousScene=scene;
    patch.automatic=resetPatch.automatic=quietPatch.automatic=false;
    if(!live.init(77,500)||!cleared.init(77,500)||!quiet.init(77,500)) return 1;
    Signal silent{},sound{};
    for(int f=0;f<150;f++) {
      silent.analyzedSamples+=800;cleared.clear();
      live.render(silent,patch,1.f/60);cleared.render(silent,resetPatch,1.f/60);quiet.render(silent,quietPatch,1.f/60);
    }
    auto memory=inspect(live),stimulus=inspect(cleared);auto feedback=difference(memory,stimulus);
    live.capture((folder/(std::to_string(scene+1)+"-memory.png")).string());
    cleared.capture((folder/(std::to_string(scene+1)+"-no-memory.png")).string());
    okay=okay&&feedback.colour>4&&memory.mean>4&&memory.mean<230;
    sound=silent;sound.bass=.85f;sound.bands[0]=.85f;sound.bands[1]=.8f;
    sound.mid=.35f;sound.high=.25f;sound.onset=.8f;
    for(int b=0;b<3;b++) {sound.hits[b][0]={sound.analyzedSamples,1};sound.attacks[b]=1;}
    Difference maximum{};
    for(int f=0;f<24;f++) {
      sound.analyzedSamples+=800;silent.analyzedSamples+=800;
      live.render(sound,patch,1.f/60);quiet.render(silent,quietPatch,1.f/60);
      auto d=difference(inspect(live),inspect(quiet));maximum.colour=std::max(maximum.colour,d.colour);maximum.detail=std::max(maximum.detail,d.detail);
      if(f==3) live.capture((folder/(std::to_string(scene+1)+"-attack.png")).string());
    }
    okay=okay&&maximum.colour>3&&maximum.detail>.06;
    quiet.shutdown();cleared.shutdown();
    auto earlier=inspect(live);double motion=0;
    for(int f=174;f<720;f++) {
      sound.analyzedSamples+=800;float t=f/60.f;
      sound.bass=.3f+.28f*std::sin(t*2.3f);sound.bands[0]=sound.bass;
      if(f%17==0) {int band=(f/17)%3;for(int j=3;j>0;j--) sound.hits[band][j]=sound.hits[band][j-1];sound.hits[band][0]={sound.analyzedSamples,.85f};}
      live.render(sound,patch,1.f/60);
      if(f%120==119) {
        auto now=inspect(live);motion=std::max(motion,difference(earlier,now).colour);earlier=now;
        okay=okay&&now.mean>4&&now.mean<230;
      }
    }
    live.capture((folder/(std::to_string(scene+1)+"-sustained.png")).string());
    okay=okay&&motion>2;
    // Exercise strip-to-ring and ring-to-strip transitions with active onsets.
    patch.previousScene=scene;patch.scene=scene==3?1:3;patch.transition=0;
    for(int f=0;f<210;f++) {sound.analyzedSamples+=800;live.render(sound,patch,1.f/60);}
    patch.previousScene=scene==3?1:3;patch.scene=scene;patch.transition=0;
    for(int f=0;f<210;f++) {
      sound.analyzedSamples+=800;
      if(f%17==0) sound.hits[2][0]={sound.analyzedSamples,.9f};
      live.render(sound,patch,1.f/60);
    }
    live.capture((folder/(std::to_string(scene+1)+"-live-return.png")).string());
    okay=okay&&patch.transition==1&&inspect(live).mean>4;
    std::cout<<scenes[scene]<<": quiet mean="<<memory.mean<<" memory difference="<<feedback.colour<<" audio difference="<<maximum.colour<<" fine detail="<<maximum.detail<<" sustained motion="<<motion<<"\n";
    live.shutdown();
  }
  // Real layered music, including two synth seeds and a slower video rate.
  // Synthetic envelopes alone missed neutral energy accumulating in the keyer.
  for(int fixture=0;fixture<sceneCount*2;fixture++) {
    int scene=fixture/2,rate=fixture%2?30:60;
    uint32_t seed=fixture%2?937:77;
    if(selected>=0&&selected!=scene) continue;
    Video video;Patch patch;patch.scene=patch.previousScene=scene;patch.automatic=false;
    if(!video.init(seed,500)) return 1;
    auto ring=std::make_unique<SampleRing>();auto analyzer=std::make_unique<Analyzer>();
    auto synth=std::make_unique<audioimage::TechnoSynth>(seed);std::array<float,1600*2> block{};
    double maximumWhite=0,minimumMean=255,maximumMean=0;
    double shadowSum=0,highlightSum=0,contrastSum=0;int contrastFrames=0;
    for(int f=0;f<rate*30;f++) {
      int count=analysisRate/rate;synth->render(block.data(),count);ring->push(block.data(),count);
      video.render(analyzer->update(*ring,1.f/rate),patch,1.f/rate);
      if(f%rate==rate-1) {
        Image image=LoadImageFromTexture(video.output());ImageResize(&image,96,60);Color* pixels=LoadImageColors(image);
        double white=0,mean=0,shadows=0,highlights=0,peakSum=0,peakSquares=0;
        for(int i=0;i<image.width*image.height;i++) {
          auto c=pixels[i];white+=(std::min({c.r,c.g,c.b})>225);mean+=(c.r+c.g+c.b)/3.0;
          double peak=std::max({c.r,c.g,c.b});shadows+=peak<55;highlights+=peak>180;
          peakSum+=peak;peakSquares+=peak*peak;
        }
        white/=image.width*image.height;mean/=image.width*image.height;
        if(f>=rate*5) {
          double count=image.width*image.height,peakMean=peakSum/count;
          shadowSum+=shadows/count;highlightSum+=highlights/count;
          contrastSum+=std::sqrt(std::max(0.0,peakSquares/count-peakMean*peakMean));contrastFrames++;
        }
        maximumWhite=std::max(maximumWhite,white);minimumMean=std::min(minimumMean,mean);maximumMean=std::max(maximumMean,mean);
        okay=okay&&white<.35&&mean>4&&mean<215;
        UnloadImageColors(pixels);UnloadImage(image);
      }
    }
    video.capture((folder/(std::to_string(scene+1)+"-soundtrack-"+std::to_string(seed)+".png")).string());
    std::cout<<scenes[scene]<<" / synth "<<seed<<" / "<<rate<<" fps: maximum neutral white="<<maximumWhite*100<<"% luminance="<<minimumMean<<".."<<maximumMean<<"\n";
    if(scene==1&&contrastFrames>0) {
      // Dense pipe returns must retain both dark gaps and luminous cores.
      // Mean brightness alone allowed the low-contrast pastel wash to pass.
      double shadows=shadowSum/contrastFrames,highlights=highlightSum/contrastFrames,contrast=contrastSum/contrastFrames;
      std::cout<<"Pipe tonal separation: shadows="<<shadows*100<<"% highlights="<<highlights*100<<"% peak contrast="<<contrast<<"\n";
      okay=okay&&shadows>.035&&highlights>.015&&contrast>28;
    }
    video.shutdown();
  }
  CloseWindow();std::cout<<(okay?"PASS":"FAIL")<<": feedback-dependent image, sharp audio response, sustained synthesis, real soundtrack headroom and live strip/ring morphs.\n";return okay?0:1;
}
