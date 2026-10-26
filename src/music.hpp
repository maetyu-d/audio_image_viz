#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace audioimage {
struct MusicFrame {
  double beats=0;
  float kick=0,bass=0,hats=0,clap=0,phrase=0,energy=0;
  std::array<float,3> bands{}; // Actual low, middle, and high output energy.
  float stereo=0,chord=0,pitch=0,active=0;
  std::array<double,4> kickBeats{{-100,-100,-100,-100}};
  double hatBeat=-100,clapBeat=-100,chordBeat=-100;
  double bassBeat=-100,openHatBeat=-100;
  float bassAccent=0,filter=0;
  std::array<double,2> clapBeats{{-100,-100}};
};
// Original, sample-clocked 4/4 techno. No files, devices, allocations, or locks
// are needed while rendering; block size never changes the sequence or tempo.
class TechnoSynth {
public:
  static constexpr int sampleRate=48000,bpm=128;
  static constexpr int beatSamples=22500,stepSamples=5625;
  explicit TechnoSynth(uint32_t seed=77) {reset(seed);}
  void reset(uint32_t seed);
  void render(float* stereo,int frames);
  MusicFrame reaction() const;
  uint64_t position() const {return sample;}
private:
  uint64_t sample=0;
  uint32_t seed=77,random=77;
  std::array<float,22500> delayL{},delayR{};
  std::array<float,6> metalPhase{};
  std::array<float,4> chordPhase{},chordFrequency{};
  int delayIndexL=0,delayIndexR=0,clapAge=48000;
  float kickPhase=0,kickEnv=0,kickFast=0,kickSlow=0,kickClick=0;
  float bassPhase=0,bassFrequency=36.7081f,bassTarget=36.7081f,bassEnv=0,accent=0;
  std::array<float,4> ladder{};
  float filterG=.04f,filterTarget=.04f,subPhase=0,subEnv=0,subFrequency=36.7081f;
  float hatEnv=0,openEnv=0,hatPan=0,hatLow=0,hatAir=0;
  float clapBurst=0,clapTail=0,clapLow=0,clapBand=0;
  float chordEnv=0,chordLow=0,rimEnv=0,rimPhase=0;
  float echoLowL=0,echoLowR=0,rumble=0,energy=0;
  std::array<float,2> dcInput{},dcOutput{};
  std::array<double,4> kickBeats{{-100,-100,-100,-100}};
  double hatBeat=-100,clapBeat=-100,chordBeat=-100;
  double bassBeat=-100,openHatBeat=-100;
  std::array<double,2> clapBeats{{-100,-100}};
  float analysisLow=0,analysisMid=0,stereoEnergy=0;
  std::array<float,3> bandEnergy{};
  float noise();
  void trigger(uint64_t step);
};
struct MusicVisual {
  std::array<float,4> pulse{},spectrum{},notes{};
  // Bass pluck, open hat, clap fan, and slow chord veil; ages share the audio clock.
  std::array<float,4> gesture{},eventAge{};
  // Acid filter opening and signed low/mid/high contrast against a slow baseline.
  std::array<float,4> drive{};
  // Slower, continuous controls for broad deformation: kick, bass, chord, filter.
  std::array<float,4> motion{};
  // Each clap keeps its own centre/radius, reciprocal width, strength, and age.
  std::array<std::array<float,4>,2> fans{},clapRipples{};
  // Centre in tunnel units, reciprocal width, strength, and age in beats.
  std::array<std::array<float,4>,4> waves{};
  float phrase=0;
  float pressure(float depth) const;
  float geometryPressure(float depth) const;
};
class MusicReactivity {
public:
  const MusicVisual& update(const MusicFrame& frame,float renderDelta,bool reduced=false);
  const MusicVisual& current() const {return visual;}
private:
  MusicVisual visual{};
  double lastBeat=-1,lastKick=-100,lastHat=-100,lastClap=-100,lastBass=-100,lastOpenHat=-100,lastChord=-100;
  std::array<float,3> bandBaseline{};
};
bool exportTechno(const std::string& path,uint32_t seed=77,int bars=16);
}
