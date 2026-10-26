#include "audio.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

namespace {
using Clock=std::chrono::steady_clock;
void wait(int milliseconds) {std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));}
struct Measurement {
  double leftHz=0,rightHz=0,coverage=0,power=0;
  bool finite=true;
};
Measurement measure(const av::SampleRing& ring) {
  Measurement result;
  const auto end=ring.count(),start=std::max(ring.sourceStart(),end>12000?end-12000:0);
  double last[2]{},previous[2]{};bool armed[2]{},crossed[2]{};int frames=0,sounding=0;
  std::vector<double> periods[2];
  for(auto i=start;i<end;i++) {
    float l=0,r=0;if(!ring.frame(i,l,r)) continue;
    const double sample[2]{l,r};
    for(int channel=0;channel<2;channel++) {
      if(!std::isfinite(sample[channel])||std::abs(sample[channel])>1) result.finite=false;
      if(sample[channel]<-.01) armed[channel]=true;
      if(armed[channel]&&frames>0&&previous[channel]<0&&sample[channel]>=0) {
        const double crossing=double(i)-sample[channel]/(sample[channel]-previous[channel]);
        if(crossed[channel]) periods[channel].push_back(crossing-last[channel]);
        last[channel]=crossing;crossed[channel]=true;armed[channel]=false;
      }
      previous[channel]=sample[channel];result.power+=sample[channel]*sample[channel];
    }
    frames++;if(std::abs(l)+std::abs(r)>1e-4f) sounding++;
  }
  result.coverage=frames?double(sounding)/frames:0;result.power/=std::max(1,frames);
  auto pitch=[](std::vector<double>& values) {
    if(values.empty()) return 0.;auto middle=values.begin()+values.size()/2;
    std::nth_element(values.begin(),middle,values.end());return 48000. / (*middle);
  };
  result.leftHz=pitch(periods[0]);result.rightHz=pitch(periods[1]);
  return result;
}
}

// Native-device check. Fixtures are one-second tones: L=997 Hz, R=1499 Hz
// (997 Hz on both sides for filenames containing "mono"). Speaker output is muted.
int main(int argc,char** argv) {
  if(argc<2) {std::cerr<<"Run tests/audio_sample_rates.py to generate rate fixtures.\n";return 2;}
  SetTraceLogLevel(LOG_WARNING);av::Audio audio;
  if(!audio.init(false,17)) {std::cerr<<audio.status<<"\n";return 1;}
  audio.update(false,true,1);int failures=0;
  for(int i=1;i<argc;i++) {
    const auto path=std::filesystem::path(argv[i]);const auto name=path.filename().string();
    const bool mono=name.find("mono")!=std::string::npos;
    auto require=[&](bool condition,const char* message) {
      if(!condition) {std::cerr<<"FAIL "<<name<<": "<<message<<"\n";failures++;}
    };
    if(!audio.load(path.string())) {require(false,audio.status.c_str());continue;}
    audio.update(false,true,1);const auto started=Clock::now();const auto firstSample=audio.ring.count();
    wait(450);audio.update(false,true,1);
    const double elapsed=std::chrono::duration<double>(Clock::now()-started).count();
    const double sampleTime=double(audio.ring.count()-firstSample)/48000.;
    const auto measured=measure(audio.ring);
    require(std::abs(measured.leftHz-997)<4,"left pitch survives sample-rate conversion");
    require(std::abs(measured.rightHz-(mono?997:1499))<4,"right pitch/channel layout survives conversion");
    require(measured.finite&&measured.power>.001&&measured.coverage>.97,"continuous, valid stereo PCM reaches the analyser");
    require(std::abs(sampleTime-elapsed)<.04,"analysis uses the real 48 kHz output clock");
    require(std::abs(audio.position-sampleTime)<.04,"playback position advances at the correct speed");
    const auto extension=path.extension().string();
    const double durationTolerance=(extension==".mp3"||extension==".m4a")?.06:.002;
    require(std::abs(audio.duration-1)<durationTolerance,"one-second source retains its duration");
    audio.update(true,true,1);const auto paused=audio.position;const auto pausedSamples=audio.ring.count();
    wait(70);audio.update(true,true,1);
    require(audio.position==paused&&audio.ring.count()==pausedSamples,"pause holds playback and analysis clocks");
    audio.update(false,true,1);wait(650);audio.update(false,true,1);
    const auto looped=measure(audio.ring);
    require(looped.coverage>.97&&std::abs(looped.leftHz-997)<4,"resumed playback loops at the original pitch without gaps");
    std::cout<<name<<" pitch="<<measured.leftHz<<"/"<<measured.rightHz<<" Hz, duration="<<audio.duration
      <<" s, clock error="<<(sampleTime-elapsed)*1000<<" ms, coverage="<<measured.coverage
      <<", loop pitch="<<looped.leftHz<<" Hz, loop coverage="<<looped.coverage<<std::endl;
  }
  audio.shutdown();std::cout<<(failures?"FAIL":"PASS")<<": "<<argc-1<<" sample-rate fixtures; "<<failures<<" failures.\n";
  return failures?1:0;
}
