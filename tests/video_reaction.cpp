#include "video.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <vector>
namespace {
std::vector<Color> pixels(av::Video& video) {
  Image image=LoadImageFromTexture(video.output());Color* data=LoadImageColors(image);
  std::vector<Color> copy(data,data+image.width*image.height);UnloadImageColors(data);UnloadImage(image);return copy;
}
struct Difference {double colour=0,fine=0,area=0;};
Difference compare(const std::vector<Color>& a,const std::vector<Color>& b,int width) {
  Difference result;
  auto diff=[&](int i,int c){const auto* x=reinterpret_cast<const unsigned char*>(&a[i]);const auto* y=reinterpret_cast<const unsigned char*>(&b[i]);return float(x[c])-y[c];};
  for(int i=0;i<int(a.size());i++) {
    float colour=0;for(int c=0;c<3;c++) colour+=std::abs(diff(i,c))/3;result.colour+=colour;if(colour>20) result.area++;
    if(i>=width&&i%width) for(int c=0;c<3;c++) result.fine+=(std::abs(diff(i,c)-diff(i-1,c))+std::abs(diff(i,c)-diff(i-width,c)))/6;
  }
  result.colour/=a.size();result.fine/=a.size();result.area/=a.size();return result;
}
}
int main(int argc,char** argv) {
  using namespace av;
  SetTraceLogLevel(LOG_WARNING);SetConfigFlags(FLAG_WINDOW_HIDDEN|FLAG_WINDOW_HIGHDPI);
  InitWindow(960,600,"AudioImage / musical reaction check");SetTargetFPS(0);if(!IsWindowReady()) return 1;
  const std::filesystem::path folder=argc>1?argv[1]:"captures/reaction";std::filesystem::create_directories(folder);
  int scene=0;
  if(argc>2) {
    if(argc!=4||std::string(argv[2])!="--scene") return 2;
    scene=std::stoi(argv[3])-1;if(scene<0||scene>=sceneCount) return 2;
  }
  bool okay=true;
  for(int fixture=0;fixture<4;fixture++) {
    const char* name=fixture==0?"bass":fixture==1?"mid-burst":fixture==2?"high-detail":"reaction-off";
    Video quiet,reactive;Patch base,patch;base.automatic=patch.automatic=false;
    base.scene=base.previousScene=patch.scene=patch.previousScene=scene;
    if(fixture==3) base.values[6]=patch.values[6]=0;
    if(!quiet.init(77,600)||!reactive.init(77,600)) {std::cerr<<"Shader initialization failed.\n";CloseWindow();return 1;}
    Signal silence{},sound{};
    for(int i=0;i<120;i++) {silence.analyzedSamples+=800;sound.analyzedSamples+=800;quiet.render(silence,base,1.f/60);reactive.render(sound,patch,1.f/60);}
    if(fixture==0||fixture==3) {sound.bass=.85f;sound.bands[0]=.8f;sound.bands[1]=.75f;}
    if(fixture>0) {int band=fixture==1?1:2;sound.hits[band][0]={sound.analyzedSamples,1};sound.onsetCount[band]=1;sound.attacks[band]=1;}
    if(fixture==3) {sound.mid=sound.high=sound.onset=.9f;sound.width=.8f;sound.stereo=.6f;sound.centroid=.7f;sound.spectrum.fill(.8f);sound.scope.fill(.2f);}
    Difference maximum{};int width=reactive.output().width;
    for(int frame=0;frame<24;frame++) {
      silence.analyzedSamples+=800;sound.analyzedSamples+=800;
      quiet.render(silence,base,1.f/60);reactive.render(sound,patch,1.f/60);
      auto delta=compare(pixels(quiet),pixels(reactive),width);
      maximum.colour=std::max(maximum.colour,delta.colour);maximum.fine=std::max(maximum.fine,delta.fine);maximum.area=std::max(maximum.area,delta.area);
      if(frame==2||frame==11) {quiet.capture((folder/(std::string(name)+"-quiet-"+std::to_string(frame)+".png")).string());reactive.capture((folder/(std::string(name)+"-audio-"+std::to_string(frame)+".png")).string());}
    }
    std::cout<<name<<": colour difference="<<maximum.colour<<" fine detail="<<maximum.fine<<" affected area="<<maximum.area*100<<"%\n";
    if(fixture==0) okay=okay&&maximum.colour>8&&maximum.area>.12;
    else if(fixture<3) okay=okay&&maximum.colour>.5&&maximum.fine>.08&&maximum.area>.005&&maximum.area<.85;
    else okay=okay&&maximum.colour<.05;
    quiet.shutdown();reactive.shutdown();
  }
  std::cout<<(okay?"PASS":"FAIL")<<": bass displacement, local percussion colour/detail, and a true zero-reaction setting.\n";
  CloseWindow();return okay?0:1;
}
