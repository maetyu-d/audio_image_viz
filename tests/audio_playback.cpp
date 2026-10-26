#include "audio.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

namespace {
void wait(int milliseconds) {std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));}
double coverage(const av::SampleRing& ring,uint64_t start) {
  const auto end=ring.count();
  start=std::max(start,end>16000?end-16000:0);
  uint64_t frames=0,sounding=0;
  for(auto i=start;i<end;i++) {
    float l=0,r=0;
    if(ring.frame(i,l,r)) {frames++;if(std::abs(l)+std::abs(r)>1e-4f) sounding++;}
  }
  return frames?double(sounding)/frames:0;
}
bool clean(const av::SampleRing& ring) {
  auto end=ring.count(),start=end>12000?end-12000:0;
  double power=0;int frames=0;
  for(auto i=start;i<end;i++) {
    float l=0,r=0;if(!ring.frame(i,l,r)) continue;
    if(!std::isfinite(l)||!std::isfinite(r)||std::abs(l)>1||std::abs(r)>1) return false;
    power+=l*l+r*r;frames++;
  }
  return frames>0&&power/frames>.001;
}
}

// Explicit native-device regression: fixtures must contain continuous tones.
// Keep this separate from CTest, which also runs without an audio device.
int main(int argc,char** argv) {
  if(argc<2) {std::cerr<<"Supply continuous-tone audio fixtures.\n";return 2;}
  SetTraceLogLevel(LOG_WARNING);
  av::Audio audio;
  if(!audio.init(false,17)) {std::cerr<<audio.status<<"\n";return 1;}
  audio.update(false,true,1); // Test the analysis tap without speaker output.
  bool okay=true;
  auto require=[&](bool condition,const char* message) {
    if(!condition) {std::cerr<<"FAIL: "<<message<<"\n";okay=false;}
  };
  for(int i=1;i<argc;i++) {
    require(audio.load(argv[i]),"file loads");
    if(audio.source!=av::Source::File) continue;
    audio.update(false,true,1);
    auto start=audio.ring.count();
    // Deliberately skip every rendering/UI update, longer than both buffers.
    // Short fixtures also wrap repeatedly during this interval.
    wait(600);
    double continuity=coverage(audio.ring,start);
    std::cout<<argv[i]<<" uninterrupted coverage="<<continuity<<"\n";
    require(continuity>.97,"file continues through a stalled graphics loop and loops without gaps");
    require(clean(audio.ring),"decoded PCM is finite, bounded and non-silent");
    audio.update(false,true,.5f);
    require(audio.position>=0&&audio.position<audio.duration,"playback position wraps within the track");
    require(GetMasterVolume()==0,"mute affects speaker output while analysis continues");
    audio.update(true,true,1);wait(40);
    audio.update(true,true,1);const auto pausedPosition=audio.position;
    auto pausedAt=audio.ring.count();wait(120);
    require(audio.ring.count()==pausedAt,"pause holds the analysis clock");
    audio.update(true,true,1);
    require(std::abs(audio.position-pausedPosition)<.001,"pause holds the playback position");
    audio.update(false,true,1);start=audio.ring.count();wait(350);
    require(coverage(audio.ring,start)>.97,"resume keeps uninterrupted file playback");
    auto name=audio.name;
    require(!audio.load("/tmp/audioimage-missing-audio.wav"),"missing file is rejected");
    require(audio.source==av::Source::File&&audio.name==name,"failed load preserves the current track");
    start=audio.ring.count();wait(200);
    require(coverage(audio.ring,start)>.97,"current file keeps playing after a failed load");
    if(audio.duration<1) {
      require(audio.load(argv[i],false),"one-shot track loads");audio.update(false,true,1);
      wait(int(audio.duration*1000)+350);audio.update(false,true,1);
      require(audio.finished(),"one-shot track signals completion for playlist advance");
      require(std::abs(audio.position-audio.duration)<.001,"completed track holds its end position");
      require(coverage(audio.ring,audio.ring.count()-std::min(uint64_t(6000),audio.ring.count()))<.01,"completed track does not repeat");
    }
    audio.update(true,true,1); // Replace an already paused track next.
  }
  audio.internal();audio.update(false,true,1);auto start=audio.ring.count();wait(180);
  require(audio.ring.count()>start,"switching back to synth works");
  audio.shutdown();
  std::cout<<(okay?"PASS":"FAIL")<<": file streaming, short loops, pause/resume, mute analysis, replacement and shutdown.\n";
  return okay?0:1;
}
