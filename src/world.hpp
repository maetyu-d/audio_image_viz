#pragma once
#include <array>
#include <cstdint>

namespace audioimage {
struct World {
  using Pigments=std::array<std::array<float,3>,4>;
  uint32_t identity=0;
  int family=0,paletteIndex=0;
  float duration=72,transition=12;
  std::array<float,4> shape{1,3,1,1};
  std::array<float,4> motion{.12f,.02f,0,1};
  std::array<float,4> palette{0,.65f,.85f,.15f};
  // Shadow, body, light, and a contrasting accent. Colours remain related
  // throughout a passage instead of continuously cycling the entire spectrum.
  Pigments pigments{{{.002f,.005f,.019f},{.12f,.006f,.78f},{.008f,1.f,.84f},{1.f,.008f,.30f}}};
  std::array<float,4> field{.5f,.5f,1,.6f};
  std::array<float,4> pattern{12,54,.6f,.08f};
  // Secondary current scale, braid strength, atmospheric density, light phase.
  std::array<float,4> weave{1,.7f,.55f,0};
  // Sheet coverage, filament weight, beaded lace, crystalline facets.
  std::array<float,4> style{.8f,.8f,.1f,.1f};
  // Reflective sheen, polish, along-fold scale, stable material phase.
  std::array<float,4> mirror{0,.8f,2,0};
  const char* name() const;
  bool finite() const;
};
uint32_t freshSeed();
World generateWorld(uint32_t runSeed,uint64_t index);
}
