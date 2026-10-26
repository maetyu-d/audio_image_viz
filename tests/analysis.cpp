#include "analysis.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <thread>
#include <vector>
using namespace av;
namespace {
constexpr float pi=3.14159265358979323846f;
using AudioData=std::vector<float>;
AudioData make(float seconds,const std::function<std::array<float,2>(double)>& generator) {
  AudioData result(size_t(seconds*analysisRate)*2);
  for(size_t i=0;i<result.size()/2;i++) {auto x=generator(double(i)/analysisRate);result[i*2]=x[0];result[i*2+1]=x[1];}return result;
}
AudioData tone(float hz,float amplitude=.2f,float seconds=2,bool anti=false) {
  return make(seconds,[=](double t){float x=amplitude*std::sin(2*pi*hz*t);return std::array<float,2>{x,anti?-x:x};});
}
Signal run(const AudioData& input,int renderFrames=800,int callbackFrames=256) {
  auto ring=std::make_unique<SampleRing>();auto analyzer=std::make_unique<Analyzer>();
  int end=int(input.size()/2),next=renderFrames,position=0;
  while(position<end) {
    int n=std::min({callbackFrames,end-position,next-position});
    ring->push(input.data()+position*2,n);position+=n;
    if(position==next) {analyzer->update(*ring,float(renderFrames)/analysisRate);next+=renderFrames;}
  }
  analyzer->update(*ring,.001f);return analyzer->current();
}
}
int main() {
  int failures=0;auto check=[&](bool okay,const char* message){if(!okay) {std::cerr<<"FAIL: "<<message<<"\n";failures++;}};
  for(int b=0;b<bandCount;b++) {
    constexpr float frequencies[]={40,94,250,800,2200,6000,15000};auto result=run(tone(frequencies[b]));
    int strongest=int(std::max_element(result.bands.begin(),result.bands.end())-result.bands.begin());
    check(strongest==b&&result.bands[b]>.35f,"Each test tone must light its matching frequency band.");
    check(std::abs(result.rms-.141421f)<.012f,"Stereo RMS must retain its calibrated amplitude.");
    if(result.onsetCount[0]+result.onsetCount[1]+result.onsetCount[2]>3) {
      std::cerr<<"Held "<<frequencies[b]<<" Hz attacks "<<result.onsetCount[0]<<","<<result.onsetCount[1]<<","<<result.onsetCount[2]<<"\n";
      check(false,"Held tones across all seven bands must not produce repeated attacks.");
    }
    if(b==6) {int peak=int(std::max_element(result.spectrum.begin(),result.spectrum.end())-result.spectrum.begin());
      float hz=20*std::pow(1000.f,float(peak)/255);check(std::abs(hz-frequencies[b])<frequencies[b]*.025f,"High harmonics between display centres must remain visible.");}
  }
  auto mono=run(tone(94)),anti=run(tone(94,.2f,2,true));
  check(std::abs(mono.bass-anti.bass)<.001f,"Antiphase stereo must preserve spectral energy.");
  check(mono.correlation>.95f&&mono.width<.01f&&anti.correlation<-.95f&&anti.width>.95f,"Stereo correlation and width must distinguish mono and antiphase.");
  auto pan=run(make(2,[](double t){return std::array<float,2>{.2f*float(std::sin(2*pi*1000*t)),0};}));
  check(pan.stereo<-.99f,"Left-only audio must produce the correct stereo balance.");
  auto zero=make(3,[](double){return std::array<float,2>{0,0};});auto silence=run(zero);
  check(silence.rms==0&&silence.onset==0&&silence.bpm==0&&silence.level==0,"Silence must not invent sound, attacks or a tempo.");
  auto dc=run(make(3,[](double){return std::array<float,2>{.6f,.6f};}));
  check(dc.rms<.00001f&&dc.bass<.003f,"DC offsets must not drive the picture as sub bass.");
  auto quiet=run(tone(1000,.005f,8)),normal=run(tone(1000,.2f,8));
  check(quiet.mid>normal.mid*.55f&&quiet.gain>8,"Quiet music must obtain useful automatic analysis gain.");
  auto held=run(tone(440,.2f,8));
  check(held.onsetCount[0]+held.onsetCount[1]+held.onsetCount[2]<=3&&held.tempoConfidence<.1f,"A held tone must not become a train of drum hits or a confident tempo.");
  auto pulses=make(10,[](double t){double age=std::fmod(std::max(0.,t-.2),.5);
    float envelope=t<.2?0:std::exp(-float(age)*55);float x=.45f*envelope*std::sin(2*pi*94*t);return std::array<float,2>{x,x};});
  auto fast=run(pulses,333,127),slow=run(pulses,4000,511),normalRate=run(pulses,800,512);
  std::cout<<"Pulse fixture: low/mid/high="<<fast.onsetCount[0]<<","<<fast.onsetCount[1]<<","<<fast.onsetCount[2]<<" tempo="<<fast.bpm<<" confidence="<<fast.tempoConfidence<<"\n";
  check(fast.onsetCount[0]>=18&&fast.onsetCount[0]<=21,"Repeated bass attacks must be counted once per hit.");
  check(fast.onsetCount==slow.onsetCount&&fast.onsetCount==normalRate.onsetCount,"12, 60 and 144 Hz rendering must detect the same attacks.");
  for(int band=0;band<3;band++) for(int i=0;i<4;i++)
    check(fast.hits[band][i].sample==slow.hits[band][i].sample&&fast.hits[band][i].strength==slow.hits[band][i].strength,
      "Percussion history must retain the same audio timestamps at every video frame rate.");
  check(std::abs(fast.bass-slow.bass)<1e-6f&&fast.analyzedSamples==slow.analyzedSamples,"Band timing must be independent of video and callback block size.");
  check(std::abs(fast.bpm-120)<2&&fast.tempoConfidence>.6f,"A regular 120 BPM pulse must produce a credible tempo estimate.");
  for(float tempo:{90.f,128.f,174.f}) {
    auto beat=make(10,[=](double t){double age=std::fmod(std::max(0.,t-.2),60/tempo);
      float x=t<.2?0:.4f*std::exp(-float(age)*55)*std::sin(2*pi*94*t);return std::array<float,2>{x,x};});
    auto measured=run(beat);std::cout<<"Tempo "<<tempo<<": "<<measured.bpm<<" confidence="<<measured.tempoConfidence<<"\n";
    check(std::abs(measured.bpm-tempo)<3&&measured.tempoConfidence>.55f,"Tempo estimation must work across slow, medium and fast pulses.");
  }
  auto hats=make(4.3f,[](double t){double age=std::fmod(std::max(0.,t-.2),.125);
    float envelope=t<.2||age>.012?0:std::sin(float(age/.012)*pi);float x=.3f*envelope*std::sin(2*pi*8000*t);return std::array<float,2>{x,-x};});
  auto hatResult=run(hats,4000,117);std::cout<<"12 ms hat fixture: "<<hatResult.onsetCount[2]<<" high attacks\n";
  check(hatResult.onsetCount[2]>=31&&hatResult.onsetCount[2]<=34,"Fast short high-frequency hits must survive slow video frames.");
  {
    auto ring=std::make_unique<SampleRing>();auto analyzer=std::make_unique<Analyzer>();auto sound=tone(1000);
    for(int i=0;i<int(sound.size()/2);i+=800) {int n=std::min(800,int(sound.size()/2)-i);ring->push(sound.data()+i*2,n);analyzer->update(*ring,1.f/60);}
    ring->beginSource();ring->push(zero.data(),fftSize);auto reset=analyzer->update(*ring,.09f);
    check(reset.mid==0&&reset.onsetCount[1]==0&&reset.analyzedSamples==fftSize,"Source switches must clear old levels, history and beat detection.");
    ring->push(zero.data(),analysisRate);auto gap=analyzer->update(*ring,1);
    check(gap.droppedSamples==analysisRate-SampleRing::capacity&&std::isfinite(gap.rms),"An audio overrun must recover and report the skipped samples.");
    float l=0,r=0;check(!ring->frame(0,l,r)&&ring->frame(ring->count()-1,l,r),"Ring stamps must reject overwritten frames.");
  }
  auto bad=zero;bad[10]=std::numeric_limits<float>::quiet_NaN();bad[200]=std::numeric_limits<float>::infinity();bad[222]=std::numeric_limits<float>::max();
  auto safe=run(bad);bool finite=std::isfinite(safe.rms)&&std::isfinite(safe.bpm);
  for(float x:safe.spectrum) finite=finite&&std::isfinite(x);for(float x:safe.scope) finite=finite&&std::isfinite(x);
  check(finite,"Malformed samples must not poison the FFT or waveform.");
  uint32_t noiseSeed=93;
  auto hiss=make(4,[&](double){noiseSeed=noiseSeed*1664525u+1013904223u;float n=(float(noiseSeed>>8)/16777216.f-.5f)*.00002f;return std::array<float,2>{n,-n};});
  auto noise=run(hiss);check(noise.level<.001f&&noise.onsetCount[2]==0&&noise.tempoConfidence==0,"Very low background hiss must not be amplified into musical events.");
  {
    auto ring=std::make_unique<SampleRing>();std::atomic<bool> done{false};std::atomic<int> torn{0};
    std::thread producer([&]{std::array<float,256*2> block{};for(int base=0;base<200000;base+=256) {
      for(int i=0;i<256;i++) {block[i*2]=float((base+i)%16384);block[i*2+1]=-block[i*2];}ring->push(block.data(),256);}done=true;});
    while(!done.load()) {uint64_t end=ring->count();for(uint64_t i=end>512?end-512:0;i<end;i++) {float l=0,r=0;if(ring->frame(i,l,r)&&(l!=float(i%16384)||r!=-l)) torn++;}}
    producer.join();check(torn==0,"Concurrent wrapping must never publish a torn stereo pair.");
  }
  std::cout<<(failures?"FAIL":"PASS")<<": seven bands, calibrated RMS, stereo phase, quiet input, DC, fixed-clock attacks, tempo, source changes and concurrent sample history.\n";
  return failures?1:0;
}
