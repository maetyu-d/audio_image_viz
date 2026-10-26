#include "music.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>

namespace audioimage {
namespace {
constexpr float tau=6.28318530718f,rate=1.f/TechnoSynth::sampleRate;
float wrap(float phase) {return phase-std::floor(phase);}
float blep(float phase,float step) {
  if(phase<step) {float t=phase/step;return t+t-t*t-1;}
  if(phase>1-step) {float t=(phase-1)/step;return t*t+t+t+1;}
  return 0;
}
float saw(float phase,float step) {return phase*2-1-blep(phase,step);}
float note(int semitones) {return 36.70809599f*std::pow(2.f,semitones/12.f);}
uint32_t hash(uint32_t n) {n^=n>>16;n*=0x7feb352du;n^=n>>15;n*=0x846ca68bu;return n^(n>>16);}
}
void TechnoSynth::reset(uint32_t requested) {
  seed=requested?requested:77;random=seed;sample=0;
  kickBeats.fill(-100);hatBeat=clapBeat=chordBeat=-100;
  clapBeats.fill(-100);
  bassBeat=openHatBeat=-100;
  analysisLow=analysisMid=stereoEnergy=0;bandEnergy.fill(0);
  delayL.fill(0);delayR.fill(0);metalPhase.fill(0);chordPhase.fill(0);ladder.fill(0);
  dcInput.fill(0);dcOutput.fill(0);delayIndexL=delayIndexR=0;clapAge=48000;
  kickPhase=kickEnv=kickFast=kickSlow=kickClick=0;
  bassPhase=bassEnv=accent=0;bassFrequency=bassTarget=note(0);
  filterG=filterTarget=.04f;subPhase=subEnv=0;subFrequency=note(0);
  hatEnv=openEnv=hatPan=hatLow=hatAir=0;clapBurst=clapTail=clapLow=clapBand=0;
  chordEnv=chordLow=rimEnv=rimPhase=0;echoLowL=echoLowR=rumble=energy=0;
  for(int i=0;i<4;i++) chordFrequency[i]=note(24+(i==0?0:i==1?3:i==2?7:10));
}
float TechnoSynth::noise() {
  random^=random<<13;random^=random>>17;random^=random<<5;
  return float(random>>8)*(2.f/16777216.f)-1;
}
void TechnoSynth::trigger(uint64_t step) {
  int tick=int(step%16),bar=int((step/16)%64),phrase=bar/8;
  uint32_t pattern=hash(seed^uint32_t(step/64)*0x9e3779b9u);
  // One short turn-around every sixteen bars; the rest is a firm 4/4 floor.
  if(tick%4==0&&!(bar%16==15&&tick==12)) {
    for(int i=3;i>0;i--) kickBeats[i]=kickBeats[i-1];
    kickBeats[0]=double(sample)/beatSamples;
    kickPhase=0;kickEnv=1;kickFast=1;kickSlow=1;kickClick=1;
  }
  if(tick==4||tick==12) {
    clapAge=0;clapTail=1;clapBeat=double(sample)/beatSamples;
    clapBeats[1]=clapBeats[0];clapBeats[0]=clapBeat;
  }
  hatBeat=double(sample)/beatSamples;
  hatEnv=tick%2?.30f:.58f;hatPan=tick%2?-.28f:.18f;
  if(tick%4==2) {openEnv=.85f;openHatBeat=double(sample)/beatSamples;}
  if(bar%4==3&&(tick==13||tick==15)) hatEnv=.67f;
  if((tick==7||tick==15)&&bar%2==1) {rimEnv=.85f;rimPhase=0;}
  constexpr int line[4][16]={
    {0,-1,0,12,-1,0,3,-1,0,-1,10,0,-1,7,0,12},
    {0,0,-1,7,0,-1,12,-1,0,3,-1,0,10,-1,7,0},
    {0,-1,12,0,-1,7,0,3,0,-1,10,-1,0,12,7,-1},
    {0,0,3,-1,7,-1,0,12,0,-1,7,10,-1,0,12,0}};
  int degree=line[(pattern>>8)%4][tick];
  int transpose=phrase%4==2?-2:phrase%4==3?3:0;
  if(degree>=0) {
    bassBeat=double(sample)/beatSamples;
    bassTarget=note(degree+transpose);bassEnv=1;accent=(tick==0||tick==6||tick==10||tick==14)?1.f:.52f;
    if(tick%4==0) bassFrequency=bassTarget;
  }
  if(tick%4==2) {subEnv=1;subFrequency=note(transpose);}
  if((bar%2==0&&(tick==3||tick==11))||(bar%2==1&&tick==7)) {
    chordEnv=1;chordBeat=double(sample)/beatSamples;
    const int degrees[]={0,3,7,10};
    for(int i=0;i<4;i++) chordFrequency[i]=note(24+transpose+degrees[i])*(1+(i%2?.0017f:-.0012f));
  }
}
void TechnoSynth::render(float* stereo,int frames) {
  constexpr float metalHz[]={4073,5879,7211,8323,10331,11873};
  for(int frame=0;frame<frames;frame++,sample++) {
    if(sample%stepSamples==0) trigger(sample/stepSamples);
    float n=noise();
    kickFast*=.998668f;kickSlow*=.999625f;kickEnv*=.9998125f;kickClick*=.99377f;
    kickPhase=wrap(kickPhase+(46+170*kickFast+35*kickSlow)*rate);
    float kick=std::tanh(std::sin(kickPhase*tau)*2.45f)*kickEnv;
    kick+=n*kickClick*.13f;
    bassEnv*=.99980f;subEnv*=.999855f;
    bassFrequency+=(bassTarget-bassFrequency)*.0026f;
    float increment=bassFrequency*rate;bassPhase=wrap(bassPhase+increment);
    float oscillator=saw(bassPhase,increment)*.78f+saw(wrap(bassPhase+.47f),increment)*.22f;
    if((sample&63u)==0) {
      float movement=.5f+.5f*std::sin(float(double(sample)/(beatSamples*64.0))*tau);
      float cutoff=220+1900*bassEnv*(.45f+accent*.55f)+movement*500;
      filterTarget=1-std::exp(-tau*cutoff*rate);
    }
    filterG+=(filterTarget-filterG)*.004f;
    float input=std::tanh(oscillator*.8f-ladder[3]*3.25f);
    for(int i=0;i<4;i++) {ladder[i]+=filterG*(input-ladder[i]);input=ladder[i];}
    float duck=1-kickEnv*.78f;
    float bass=std::tanh(ladder[3]*3.3f)*bassEnv*duck;
    subPhase=wrap(subPhase+subFrequency*rate);
    float sub=std::sin(subPhase*tau)*subEnv*duck;
    float metal=0;
    for(int i=0;i<6;i++) {metalPhase[i]=wrap(metalPhase[i]+metalHz[i]*rate);metal+=metalPhase[i]<.5f?.16f:-.16f;}
    float hatSource=n*.65f+metal*.35f;
    hatLow+=(hatSource-hatLow)*.47f;float high=hatSource-hatLow;
    hatAir+=(high-hatAir)*.80f;
    float hats=hatAir*(hatEnv+openEnv*.65f);
    hatEnv*=.99854f;openEnv*=.99973f;
    if(clapAge==0||clapAge==432||clapAge==1008) clapBurst=1;
    clapAge=std::min(clapAge+1,48000);clapBurst*=.99532f;clapTail*=.99934f;
    clapLow+=(n-clapLow)*.12f;clapBand+=(n-clapLow-clapBand)*.55f;
    float clap=clapBand*(clapBurst*.65f+clapTail*.18f);
    float chord=0;
    for(int i=0;i<4;i++) {
      chordPhase[i]=wrap(chordPhase[i]+chordFrequency[i]*rate);
      chord+=std::sin(chordPhase[i]*tau)*.19f+saw(chordPhase[i],chordFrequency[i]*rate)*.06f;
    }
    chordLow+=(chord-chordLow)*.075f;chordEnv*=.9999583f;
    chord=chordLow*chordEnv*duck;
    rimPhase=wrap(rimPhase+820*rate);rimEnv*=.9981f;
    float rim=std::sin(rimPhase*tau)*std::sin(rimPhase*tau*.57f)*rimEnv;
    float left=delayL[delayIndexL],right=delayR[delayIndexR];
    echoLowL+=(left-echoLowL)*.12f;echoLowR+=(right-echoLowR)*.12f;
    float send=chord*.33f+bass*.075f+clap*.035f;
    delayL[delayIndexL]=send*.7f+echoLowR*.37f;
    delayR[delayIndexR]=send+echoLowL*.37f;
    if(++delayIndexL==16875) delayIndexL=0;
    if(++delayIndexR==22500) delayIndexR=0;
    rumble+=(std::tanh((echoLowL+echoLowR)*4)*.4f-rumble)*.014f;
    float mono=kick*.74f+bass*.30f+sub*.23f+rumble*.17f+clap*.24f+rim*.14f;
    float channels[]={mono+hats*(1-hatPan)*.25f+chord*.20f+echoLowL*.32f*duck,
      mono+hats*(1+hatPan)*.25f+chord*.18f+echoLowR*.32f*duck};
    float frameEnergy=0;
    for(int c=0;c<2;c++) {
      float dc=channels[c]-dcInput[c]+dcOutput[c]*.99935f;
      dcInput[c]=channels[c];dcOutput[c]=dc;
      float output=std::tanh(dc*1.45f)*.88f;
      output*=std::min(1.f,float(sample)/960.f);
      stereo[frame*2+c]=output;frameEnergy+=output*output*.5f;
    }
    energy+=(frameEnergy-energy)*.0015f;
    float monoOutput=(stereo[frame*2]+stereo[frame*2+1])*.5f;
    analysisLow+=(monoOutput-analysisLow)*.0233f;
    analysisMid+=(monoOutput-analysisMid)*.230f;
    float split[]={analysisLow,analysisMid-analysisLow,monoOutput-analysisMid};
    for(int i=0;i<3;i++) {
      float power=split[i]*split[i];
      bandEnergy[i]+=(power-bandEnergy[i])*(power>bandEnergy[i]?.0045f:.00022f);
    }
    float lateral=stereo[frame*2+1]*stereo[frame*2+1]-stereo[frame*2]*stereo[frame*2];
    stereoEnergy+=(lateral-stereoEnergy)*.001f;
  }
}
MusicFrame TechnoSynth::reaction() const {
  double beats=double(sample)/beatSamples;
  float phrase=.5f-.5f*std::cos(float(std::fmod(beats,64.0))*tau/64);
  MusicFrame result{beats,kickEnv,bassEnv*accent,std::min(1.f,hatEnv+openEnv*.65f),
    std::min(1.f,clapBurst+clapTail*.2f),phrase,std::min(1.f,std::sqrt(energy)*2.5f)};
  const float gain[]={1.8f,3.3f,6.0f};
  for(int i=0;i<3;i++) result.bands[i]=1-std::exp(-std::sqrt(bandEnergy[i])*gain[i]);
  result.stereo=std::clamp(stereoEnergy*12.f,-1.f,1.f);
  result.chord=chordEnv;result.pitch=std::clamp(std::log2(bassFrequency/note(0))/2.f,0.f,1.f);result.active=1;
  result.kickBeats=kickBeats;result.hatBeat=hatBeat;result.clapBeat=clapBeat;result.chordBeat=chordBeat;
  result.bassBeat=bassBeat;result.openHatBeat=openHatBeat;result.bassAccent=accent;
  result.clapBeats=clapBeats;
  result.filter=std::clamp((filterG-.028f)/.26f,0.f,1.f);
  return result;
}
float MusicVisual::pressure(float depth) const {
  float result=0;
  for(const auto& wave:waves) {
    float t=std::max(0.f,1-std::abs(depth-wave[0])*wave[1]);
    result+=t*t*(3-2*t)*wave[2];
  }
  return std::min(result,1.2f);
}
float MusicVisual::geometryPressure(float depth) const {
  float result=0;
  for(const auto& wave:waves) {
    // Several wall rings must resolve a crest. Narrow light accents can pass
    // between vertices; using them as geometry made the wall hop ring to ring.
    if(wave[2]<=0||wave[1]<=0) continue;
    float radius=std::max(1.6f/wave[1],8+std::max(0.f,depth)*.10f);
    float t=std::max(0.f,1-std::abs(depth-wave[0])/radius);
    result+=t*t*(3-2*t)*wave[2];
  }
  return std::min(result,1.2f);
}
const MusicVisual& MusicReactivity::update(const MusicFrame& frame,float renderDelta,bool reduced) {
  if(frame.beats+.001<lastBeat) {
    visual={};lastBeat=-1;lastKick=lastHat=lastClap=lastBass=lastOpenHat=lastChord=-100;
  }
  double advance=lastBeat>=0?frame.beats-lastBeat:0;
  float dt=advance>0?float(std::min(.5,advance*60/TechnoSynth::bpm)):std::clamp(renderDelta,.001f,.25f);
  float strength=reduced?.28f:1.f,gate=std::clamp(frame.active,0.f,1.f)*strength;
  if(lastBeat<0) bandBaseline=frame.bands;
  bool freshKick=frame.kickBeats[0]>lastKick&&frame.beats-frame.kickBeats[0]<.9;
  bool freshHat=frame.hatBeat>lastHat&&frame.beats-frame.hatBeat<.5;
  bool freshClap=frame.clapBeat>lastClap&&frame.beats-frame.clapBeat<.9;
  float targets[]={std::max(std::sqrt(std::max(0.f,frame.kick*frame.active))*strength,freshKick?gate*.92f:0.f),
    frame.bass*strength,std::max(frame.hats*strength,freshHat?gate*.65f:0.f),
    std::max(frame.clap*strength,freshClap?gate:0.f)};
  const float release[]={11,7,22,10};
  for(int i=0;i<4;i++) {
    float target=std::clamp(targets[i],0.f,1.f);
    // Catch each transient on the first available video frame. Releases follow
    // elapsed audio time, instead of the simulation's capped frame delta.
    visual.pulse[i]=target>visual.pulse[i]?target:target+(visual.pulse[i]-target)*std::exp(-dt*release[i]);
    float spectral=i<3?frame.bands[i]*strength:frame.stereo*strength;
    visual.spectrum[i]+=(spectral-visual.spectrum[i])*(1-std::exp(-dt*(i==2?18.f:8.f)));
  }
  float phraseTarget=frame.phrase*gate;
  visual.phrase+=(phraseTarget-visual.phrase)*(1-std::exp(-dt*2));
  float noteTargets[]={frame.chord*strength,frame.pitch*gate,frame.energy*strength,gate};
  for(int i=0;i<4;i++) visual.notes[i]+=(noteTargets[i]-visual.notes[i])*(1-std::exp(-dt*(i==1?4.f:12.f)));
  // Instrument-specific envelopes give each voice a different spatial gesture.
  // Their travelled distance uses actual onset ages, not video frame counters.
  const double events[]={frame.bassBeat,frame.openHatBeat,frame.clapBeat,frame.chordBeat};
  const double previous[]={lastBass,lastOpenHat,lastClap,lastChord};
  const float attack[]={0,20,14,5},decay[]={8,3.8f,1.55f,.72f};
  const float catchLevel[]={.85f,.70f,.78f,0};
  for(int i=0;i<4;i++) {
    float age=float(frame.beats-events[i]);
    visual.eventAge[i]=std::clamp(age,0.f,32.f);
    float envelope=age>=0&&age<16?std::exp(-age*decay[i])*(i?1-std::exp(-age*attack[i]):1):0;
    bool fresh=events[i]>previous[i]&&age>=0&&age<.65f;
    float target=std::max(envelope,fresh?catchLevel[i]:0.f)*gate;
    if(i==0) target*=.65f+.35f*std::clamp(frame.bassAccent,0.f,1.f);
    float release=i==3?3.f:i==2?7.f:18.f;
    visual.gesture[i]=target>visual.gesture[i]?target:target+(visual.gesture[i]-target)*std::exp(-dt*release);
  }
  // A steady loud mix should still breathe: respond to changes around the
  // local energy baseline rather than pinning an entire band at one brightness.
  visual.drive[0]+=(frame.filter*gate-visual.drive[0])*(1-std::exp(-dt*12));
  for(int i=0;i<3;i++) {
    float contrast=std::clamp((frame.bands[i]-bandBaseline[i])*4.f,-.65f,.85f)*gate;
    visual.drive[i+1]+=(contrast-visual.drive[i+1])*(1-std::exp(-dt*14));
    bandBaseline[i]+=(frame.bands[i]-bandBaseline[i])*(1-std::exp(-dt*.7f));
  }
  const float motionTargets[]={visual.pulse[0],visual.gesture[0],visual.gesture[3],visual.drive[0]};
  const float motionRates[]={3.5f,2.5f,1.8f,3.f};
  for(int i=0;i<4;i++) visual.motion[i]+=(motionTargets[i]-visual.motion[i])*(1-std::exp(-dt*motionRates[i]));
  auto lifetime=[](float age) {
    float fade=std::clamp(age-3.f,0.f,1.f);
    return 1-fade*fade*(3-2*fade);
  };
  for(int i=0;i<2;i++) {
    float age=float(frame.beats-frame.clapBeats[i]);
    float boundedAge=std::clamp(age,0.f,32.f);
    float attack=1-std::exp(-std::max(0.f,age)*8);
    float amplitude=age>=0&&age<4?attack*attack*std::exp(-age*.85f)*lifetime(age)*gate:0;
    // Do not carry a smoothed previous clap envelope into a newborn wave.
    // Both old and new fans coexist, and expire at zero strength.
    visual.fans[i]={18+boundedAge*42,1/(12+boundedAge*8),amplitude,boundedAge};
    visual.clapRipples[i]={.12f+boundedAge*.46f,1/(.10f+boundedAge*.045f),amplitude,boundedAge};
  }
  for(int i=0;i<4;i++) {
    float age=float(frame.beats-frame.kickBeats[i]);
    float attack=1-std::exp(-std::max(0.f,age)*12);
    float amplitude=age>=0&&age<4?attack*attack*std::exp(-age*.55f)*lifetime(age)*gate:0;
    visual.waves[i]={12+age*48,1/(6.f+std::max(0.f,age)*2.2f),amplitude,age};
  }
  lastBeat=frame.beats;lastKick=frame.kickBeats[0];lastHat=frame.hatBeat;lastClap=frame.clapBeat;
  lastBass=frame.bassBeat;lastOpenHat=frame.openHatBeat;lastChord=frame.chordBeat;
  return visual;
}

bool exportTechno(const std::string& path,uint32_t seed,int bars) {
  if(bars<1||bars>256) return false;
  std::ofstream file(path,std::ios::binary);if(!file) return false;
  uint32_t frames=uint32_t(bars)*4*TechnoSynth::beatSamples,bytes=frames*4;
  auto u16=[&](uint16_t n) {char b[]={char(n),char(n>>8)};file.write(b,2);};
  auto u32=[&](uint32_t n) {char b[]={char(n),char(n>>8),char(n>>16),char(n>>24)};file.write(b,4);};
  file.write("RIFF",4);u32(bytes+36);file.write("WAVEfmt ",8);u32(16);u16(1);u16(2);
  u32(TechnoSynth::sampleRate);u32(TechnoSynth::sampleRate*4);u16(4);u16(16);file.write("data",4);u32(bytes);
  TechnoSynth synth(seed);std::array<float,1024> block{};
  for(uint32_t written=0;written<frames;) {
    int count=int(std::min(512u,frames-written));synth.render(block.data(),count);
    for(int i=0;i<count*2;i++) u16(uint16_t(int16_t(std::lround(block[i]*32767))));
    written+=count;
  }
  file.flush();return bool(file);
}
}
