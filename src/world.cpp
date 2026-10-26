#include "world.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <random>

namespace audioimage {
namespace {
uint32_t hash(uint32_t x) {
  x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;
}
struct Random {
  uint32_t state;
  uint32_t next() {state+=0x9e3779b9u;return hash(state);}
  float range(float lo,float hi) {return lo+(hi-lo)*(next()/4294967295.f);}
};
constexpr World::Pigments palettes[]={
  {{{.002f,.005f,.019f},{.12f,.006f,.78f},{.008f,1.f,.84f},{1.f,.008f,.30f}}}, // ultraviolet / cyan / hot pink
  {{{.009f,.002f,.015f},{.66f,.005f,.24f},{1.f,.012f,.62f},{.56f,1.f,.006f}}}, // fuchsia / acid lime
  {{{.003f,.003f,.023f},{.025f,.012f,.80f},{.40f,.015f,1.f},{1.f,.62f,.004f}}}, // electric violet / marigold
  {{{.002f,.007f,.019f},{.004f,.20f,.83f},{.004f,.96f,1.f},{1.f,.13f,.008f}}}, // laser cyan / tangerine
  {{{.015f,.002f,.009f},{.66f,.008f,.075f},{1.f,.25f,.004f},{.39f,.012f,1.f}}}, // molten orange / ultraviolet
  {{{.002f,.009f,.005f},{.008f,.34f,.055f},{.30f,1.f,.004f},{1.f,.008f,.43f}}}, // toxic green / candy pink
  {{{.002f,.004f,.022f},{.004f,.035f,.84f},{.008f,.32f,1.f},{1.f,.006f,.67f}}}, // cobalt / electric pink
  {{{.008f,.002f,.017f},{.43f,.004f,.73f},{.90f,.008f,1.f},{.008f,1.f,.54f}}}  // orchid / laser mint
};
constexpr std::array<float,4> materials[]={
  {1.f,.84f,.06f,.06f},   // tidal silk: broad, translucent sheets
  {.46f,.82f,.82f,.08f},  // orbital garden: suspended pearls and filaments
  {.72f,.62f,.14f,.94f},  // prism current: layered, crystalline facets
  {.42f,.62f,.92f,.17f},  // ripple choir: caustic cells and liquid beads
  {.14f,.78f,1.f,.12f},   // velvet bloom: open darkness and luminous lace
  {.92f,.92f,.10f,.42f}   // woven light: crossing curtains and fine embroidery
};
// Appearance chance and maximum sheen. Most passages have no reflective glaze;
// even the richer families expose only occasional stretches of their folds.
constexpr std::array<float,3> mirrors[]={
  {.22f,.14f,.28f},{.12f,.10f,.22f},{.65f,.22f,.42f},
  {.48f,.18f,.36f},{.06f,.08f,.16f},{.18f,.12f,.26f}
};
std::array<int,6> order(uint32_t seed,uint64_t cycle) {
  Random random{hash(seed^uint32_t(cycle)^hash(uint32_t(cycle>>32)))};
  std::array<int,6> result{0,1,2,3,4,5};
  for(int i=5;i>0;i--) std::swap(result[i],result[random.next()%uint32_t(i+1)]);
  return result;
}
}
uint32_t freshSeed() {
  static const uint32_t salt=[] {
    uint64_t clock=uint64_t(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    uint32_t entropy=uint32_t(clock)^uint32_t(clock>>32);
    try {entropy^=std::random_device{}();} catch(...) {}
    return hash(entropy);
  }();
  static std::atomic<uint32_t> serial{0};
  // The hash is a permutation: successive requests in a process do not repeat.
  uint32_t result=0;
  while(result==0) result=hash(salt+serial.fetch_add(1,std::memory_order_relaxed));
  return result;
}
World generateWorld(uint32_t runSeed,uint64_t index) {
  uint32_t key=hash(runSeed^hash(uint32_t(index)+0x632be59bu)^hash(uint32_t(index>>32)));
  Random random{key};
  uint64_t cycle=index/6;
  auto sequence=order(runSeed,cycle);
  if(cycle>0&&sequence[0]==order(runSeed,cycle-1)[5]) std::swap(sequence[0],sequence[1]);
  World world;world.identity=key;world.family=sequence[index%6];
  world.duration=random.range(60,90);world.transition=random.range(10,16);
  world.shape={random.range(.72f,1.65f),float(2+random.next()%6),random.range(.55f,1.8f),random.range(.45f,1.65f)};
  world.motion={random.range(.085f,.18f),random.range(.018f,.052f)*(random.next()%2?1:-1),random.range(0,6.28318f),random.range(0,6.28318f)};
  world.paletteIndex=int(random.next()%std::size(palettes));
  world.palette={random.range(-.08f,.08f),random.range(.86f,1.18f),random.range(.78f,1.f),random.range(.045f,.095f)};
  world.pigments=palettes[world.paletteIndex];
  // Keep saturated pigments and almost-black shadows through seeded tinting.
  std::array<float,3> tint{random.range(.93f,1.06f),random.range(.93f,1.06f),random.range(.93f,1.06f)};
  for(auto& colour:world.pigments) for(size_t channel=0;channel<3;channel++)
    colour[channel]=std::clamp(colour[channel]*tint[channel],0.f,1.f);
  world.field={random.range(.31f,.69f),random.range(.30f,.66f),random.range(.76f,1.32f),random.range(.35f,.95f)};
  world.pattern={float(9+random.next()%6),random.range(44,61),random.range(.52f,.78f),random.range(.045f,.095f)};
  world.weave={random.range(.8f,1.35f),random.range(.45f,1.1f),random.range(.40f,.85f),random.range(0,6.28318f)};
  world.style=materials[world.family];
  for(float& value:world.style) value=std::clamp(value*random.range(.9f,1.08f),0.f,1.f);
  Random reflective{hash(key^0x6a09e667u)};
  const auto& material=mirrors[world.family];
  bool present=reflective.range(0,1)<material[0];
  world.mirror={present?reflective.range(material[1],material[2]):0.f,
    reflective.range(.64f,.96f),reflective.range(1.4f,3.2f),reflective.range(0,6.28318f)};
  return world;
}
const char* World::name() const {
  static const char* names[]={"TIDAL SILK","ORBITAL GARDEN","PRISM CURRENT","RIPPLE CHOIR","VELVET BLOOM","WOVEN LIGHT"};
  return names[std::clamp(family,0,5)];
}
bool World::finite() const {
  if(family<0||family>5||paletteIndex<0||paletteIndex>=int(std::size(palettes))||!std::isfinite(duration)||duration<60||duration>90||!std::isfinite(transition)||transition<10||transition>16) return false;
  for(const auto* a:{&shape,&motion,&palette,&field,&pattern,&weave,&style,&mirror}) for(float value:*a) if(!std::isfinite(value)) return false;
  for(float value:style) if(value<0||value>1) return false;
  if(mirror[0]<0||mirror[0]>1||mirror[1]<.64f||mirror[1]>.96f||mirror[2]<1.4f||mirror[2]>3.2f||mirror[3]<0||mirror[3]>6.28318f) return false;
  for(const auto& colour:pigments) for(float value:colour) if(!std::isfinite(value)||value<0||value>1) return false;
  return motion[0]>=.085f&&std::abs(motion[1])>=.018f&&pattern[1]>=44&&pattern[1]<=61&&weave[0]>=.8f&&weave[0]<=1.35f&&weave[1]>=.45f&&weave[1]<=1.1f&&weave[2]>=.4f&&weave[2]<=.85f;
}
}
