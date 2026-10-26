#include "analysis.hpp"
#include "music.hpp"
#include <cmath>
#include <iostream>
#include <memory>
int main() {
  int failed=0;
  for(uint32_t seed:{77u,12345u,937u}) {
    auto ring=std::make_unique<av::SampleRing>();auto analyzer=std::make_unique<av::Analyzer>();
    auto synth=std::make_unique<audioimage::TechnoSynth>(seed);std::array<float,800*2> block{};
    for(int i=0;i<60*16;i++) {synth->render(block.data(),800);ring->push(block.data(),800);analyzer->update(*ring,1.f/60);}
    const auto& signal=analyzer->current();
    std::cout<<"Soundtrack seed "<<seed<<": "<<signal.bpm<<" BPM confidence="<<signal.tempoConfidence<<" low attacks="<<signal.onsetCount[0]<<"\n";
    if(std::abs(signal.bpm-128)>3||signal.tempoConfidence<.5f||signal.droppedSamples) failed++;
  }
  std::cout<<(failed?"FAIL":"PASS")<<": pulse extraction from the actual layered 128 BPM synth soundtrack.\n";
  return failed?1:0;
}
