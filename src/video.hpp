#pragma once
#include "analysis.hpp"
#include "world.hpp"
#include "raylib.h"
#include <array>
#include <string>
namespace av {
constexpr int sceneCount=6;
inline constexpr const char* scenes[]={"Mode 1","Mode 2","Mode 3","Mode 4","Mode 5","Mode 6"};
inline constexpr const char* labels[]={"FEEDBACK","TIME SMEAR","WARP","SYMMETRY","COLOUR PHASE","DETAIL","AUDIO REACTION","EXPOSURE"};
struct Patch {
  std::array<float,8> values{.78f,.48f,.58f,.42f,.12f,.62f,.82f,.52f};
  int scene=0,previousScene=0;
  float transition=1;
  bool automatic=true;
};
class Video {
public:
  bool init(uint32_t seed,int resolution=900);
  void shutdown();
  bool render(const Signal&,Patch&,float dt);
  void present() const;
  void clear();
  void mutate(uint32_t seed);
  void capture(const std::string& path) const;
  Texture2D output() const {return image.texture;}
  float seconds() const {return clock;}
private:
  Shader field{},feedback{},blur{},post{},percussion{},vectorBeam{};
  RenderTexture2D history[2]{},delay[4]{},flow{},bloom[2]{},image{};
  Texture2D spectrum{},scope{};
  audioimage::World world{},previousWorld{};
  int current=0,delayIndex=0,maxHeight=900;
  float clock=0,worldAge=100,delayClock=0;
  float bassBend=0,subBend=0;
  uint64_t lastAudioSample=0;
  bool resize();
  void injectVectors(const Signal&,const Patch&,float reaction,float blend,float dt);
};
}
