#include "analysis.hpp"
#include <algorithm>
#include <cmath>
namespace av {
namespace {
constexpr float pi=3.14159265358979323846f,hopSeconds=float(hopSize)/analysisRate;
float follow(float old,float target,float attack,float release,float dt=hopSeconds) {
  return target+(old-target)*std::exp(-dt/(target>old?attack:release));
}
float overlap(float a,float b,float lo,float hi) {return std::max(0.f,std::min(b,hi)-std::max(a,lo))/(b-a);}
float clean(float value) {return std::isfinite(value)?std::clamp(value,-8.f,8.f):0;}
float compress(float value) {return -std::expm1(-std::max(0.f,value)*5.5f);}
}
void SampleRing::push(const float* data,int frames,int channels) {
  if(!data||frames<=0||channels<=0||producing.test_and_set(std::memory_order_acquire)) return;
  for(int i=0;i<frames;i++) {
    int slot=int(written%capacity);
    // Order the validity marker and both sample stores against readers,
    // including a reader that overlaps the producer wrapping around the ring.
    stamps[slot].store(0);
    samples[slot*2].store(data[i*channels]);
    samples[slot*2+1].store(data[i*channels+(channels>1?1:0)]);
    stamps[slot].store(written+1);written++;
  }
  published.store(written,std::memory_order_release);producing.clear(std::memory_order_release);
}
bool SampleRing::frame(uint64_t absolute,float& left,float& right) const {
  int slot=int(absolute%capacity);
  if(stamps[slot].load()!=absolute+1) return false;
  left=samples[slot*2].load();right=samples[slot*2+1].load();
  return stamps[slot].load()==absolute+1;
}
void SampleRing::beginSource() {boundary.store(count(),std::memory_order_release);epoch.fetch_add(1,std::memory_order_release);}
template<int Size> FftPlan<Size>::FftPlan() {
  float sum=0;
  for(int i=0;i<Size;i++) {
    window[i]=.5f-.5f*std::cos(2*pi*i/(Size-1));sum+=window[i]*window[i];
    int value=i,reversed=0;for(int n=Size;n>1;n>>=1) {reversed=(reversed<<1)|(value&1);value>>=1;}reverse[i]=reversed;
  }
  for(int i=0;i<Size/2;i++) roots[i]=std::polar(1.f,-2*pi*i/Size);
  // One-sided Hann power integrates to the mean stereo signal energy.
  powerScale=2.f/(Size*sum);
}
template<int Size> void FftPlan<Size>::run(const std::array<std::array<float,2>,fftSize>& history,int head,std::array<std::complex<float>,Size>& output) const {
  for(int i=0;i<Size;i++) {const auto& p=history[(head+fftSize-Size+i)%fftSize];output[reverse[i]]={p[0]*window[i],p[1]*window[i]};}
  for(int n=2;n<=Size;n<<=1) for(int start=0;start<Size;start+=n) for(int j=0;j<n/2;j++) {
    auto a=output[start+j],b=output[start+j+n/2]*roots[j*Size/n];output[start+j]=a+b;output[start+j+n/2]=a-b;
  }
}
Analyzer::Analyzer() {
  constexpr float binWidth=float(analysisRate)/fftSize;
  for(int i=1;i<fftSize/2;i++) {
    float lo=(i-.5f)*binWidth,hi=(i+.5f)*binWidth;
    for(int b=0;b<bandCount;b++) bandWeights[b][i]=overlap(lo,hi,bandEdges[b],bandEdges[b+1]);
    broadWeights[i]={overlap(lo,hi,20,250),overlap(lo,hi,250,3500),overlap(lo,hi,3500,20000)};
  }
  for(int i=0;i<spectrumSize;i++) {
    float centre=20*std::pow(1000.f,float(i)/(spectrumSize-1));
    float halfStep=std::pow(1000.f,.5f/(spectrumSize-1));
    spectrumLo[i]=centre/halfStep;spectrumHi[i]=centre*halfStep;
    spectrumFirst[i]=std::clamp(int(std::floor(spectrumLo[i]/binWidth-.5f)),1,fftSize/2-1);
    spectrumLast[i]=std::clamp(int(std::ceil(spectrumHi[i]/binWidth+.5f)),1,fftSize/2-1);
  }
  clearState();
}
void Analyzer::clearState() {
  history={};signal={};previousMagnitude={};detectors={};rhythm={};lowIntervals={};dcInput={};dcOutput={};
  head=hop=rhythmHead=rhythmCount=tempoDivider=0;
  hopEnergy=leftEnergy=rightEnergy=crossEnergy=sideEnergy=0;
  hopPeak=gate=peakHold=idle=peakMemory=rhythmicEnvelope=0;reference=.1f;lastRhythmAge=100;
  lastLowSample=0;intervalHead=intervalCount=0;
  tempoEnergyPrevious=0;
}
void Analyzer::reset() {clearState();cursor=0;epoch=std::numeric_limits<uint64_t>::max();}
const Signal& Analyzer::update(const SampleRing& ring,float renderDelta) {
  auto generation=ring.generation();
  if(epoch!=generation) {clearState();epoch=generation;cursor=ring.sourceStart();}
  uint64_t end=ring.count(),first=cursor;
  if(end-cursor>SampleRing::capacity) {
    uint64_t missed=end-cursor-SampleRing::capacity,lost=signal.droppedSamples+missed;
    clearState();signal.droppedSamples=lost;cursor=end-SampleRing::capacity;
  }
  for(;cursor<end;cursor++) {
    float left=0,right=0;
    if(!ring.frame(cursor,left,right)) {
      uint64_t lost=signal.droppedSamples+1;clearState();signal.droppedSamples=lost;continue;
    }
    sample(left,right);
  }
  if(cursor==first) {
    idle+=std::clamp(renderDelta,0.f,1.f);
    if(idle>.25f) {uint64_t lost=signal.droppedSamples;clearState();signal.droppedSamples=lost;idle=.26f;}
  } else idle=0;
  return signal;
}
void Analyzer::sample(float left,float right) {
  std::array<float,2> input{clean(left),clean(right)};
  for(int c=0;c<2;c++) {
    // A 12 Hz DC blocker preserves sub bass and removes input offsets.
    float filtered=input[c]-dcInput[c]+.99843044f*dcOutput[c];
    dcInput[c]=input[c];dcOutput[c]=filtered;history[head][c]=filtered;
  }
  float l=history[head][0],r=history[head][1];head=(head+1)%fftSize;
  hopEnergy+=(double(l)*l+double(r)*r)*.5;leftEnergy+=double(l)*l;rightEnergy+=double(r)*r;
  crossEnergy+=double(l)*r;sideEnergy+=double(l-r)*(l-r)*.5;
  hopPeak=std::max({hopPeak,std::abs(input[0]),std::abs(input[1])});signal.analyzedSamples++;
  if(++hop==hopSize) {analyzeHop();hop=0;hopEnergy=leftEnergy=rightEnergy=crossEnergy=sideEnergy=0;hopPeak=0;}
}
bool Analyzer::detect(int band,float novelty,float rms) {
  auto& d=detectors[band];
  std::array<float,128> sorted=d.history;float median=0,deviation=0;
  if(d.count>0) {
    auto mid=sorted.begin()+d.count/2;std::nth_element(sorted.begin(),mid,sorted.begin()+d.count);median=*mid;
    for(int i=0;i<d.count;i++) sorted[i]=std::abs(d.history[i]-median);
    std::nth_element(sorted.begin(),mid,sorted.begin()+d.count);deviation=*mid;
  }
  float threshold=median*1.35f+deviation*2.8f+.018f;
  d.cooldown=std::max(0.f,d.cooldown-hopSeconds);
  bool event=d.count>=4&&d.cooldown==0&&rms>.0003f&&d.previous>d.previousThreshold&&d.previous>d.beforePrevious&&d.previous>=novelty;
  if(event) {
    signal.attacks[band]=std::clamp(.45f+(d.previous-d.previousThreshold)/(.15f+d.previousThreshold),.45f,1.f);
    signal.hits[band][signal.onsetCount[band]%4]={signal.analyzedSamples-hopSize,signal.attacks[band]};
    signal.onsetCount[band]++;d.cooldown=band==0?.16f:band==1?.09f:.055f;
  }
  d.beforePrevious=d.previous;d.previous=novelty;d.previousThreshold=threshold;
  d.history[d.position]=novelty;d.position=(d.position+1)%int(d.history.size());d.count=std::min(d.count+1,int(d.history.size()));
  return event;
}
void Analyzer::analyzeHop() {
  float rms=std::sqrt(float(hopEnergy/hopSize));
  signal.rms=std::sqrt(follow(signal.rms*signal.rms,rms*rms,.04f,.04f));
  reference=follow(reference,std::max(rms,.0005f),.3f,1.5f);
  float desiredGain=std::clamp(.14f/std::max(reference,.0005f),.25f,24.f);
  signal.gain=follow(signal.gain,desiredGain,.7f,.12f);
  float opening=std::clamp((signal.rms-.00012f)/.00065f,0.f,1.f);opening*=opening*(3-2*opening);
  gate=follow(gate,opening,.018f,.12f);signal.level=compress(signal.rms*signal.gain)*gate;
  if(hopPeak>=peakMemory) {peakMemory=hopPeak;peakHold=.4f;}
  else if(peakHold>0) peakHold-=hopSeconds;else peakMemory*=std::exp(-hopSeconds/.35f);
  signal.peak=std::max(hopPeak,peakMemory);
  float sum=float(leftEnergy+rightEnergy);
  signal.stereo=follow(signal.stereo,float(rightEnergy-leftEnergy)/(sum+1e-12f),.08f,.12f);
  signal.correlation=follow(signal.correlation,float(crossEnergy/std::sqrt(leftEnergy*rightEnergy+1e-24)),.08f,.15f);
  signal.width=follow(signal.width,std::clamp(float(sideEnergy)/(sum+1e-12f),0.f,1.f),.08f,.15f);
  longFft.run(history,head,bins);
  std::array<float,fftSize/2> power{};
  std::array<float,bandCount> bandPower{};std::array<float,3> broadPower{};
  float total=0,weighted=0;constexpr float binWidth=float(analysisRate)/fftSize;
  for(int i=1;i<fftSize/2;i++) {
    auto other=std::conj(bins[fftSize-i]);auto l=(bins[i]+other)*.5f,r=(bins[i]-other)*std::complex<float>(0,-.5f);
    power[i]=(std::norm(l)+std::norm(r))*.5f*longFft.powerScale;
    for(int b=0;b<bandCount;b++) bandPower[b]+=power[i]*bandWeights[b][i];
    for(int b=0;b<3;b++) broadPower[b]+=power[i]*broadWeights[i][b];
    float hz=i*binWidth;if(hz>=20&&hz<=20000) {total+=power[i];weighted+=power[i]*std::log(hz/20)/std::log(1000.f);}
  }
  for(int b=0;b<bandCount;b++) signal.bands[b]=follow(signal.bands[b],compress(std::sqrt(bandPower[b])*signal.gain)*gate,.022f,.2f);
  float* broad[]={&signal.bass,&signal.mid,&signal.high};
  for(int b=0;b<3;b++) *broad[b]=follow(*broad[b],compress(std::sqrt(broadPower[b])*signal.gain)*gate,.018f,b==0?.22f:.16f);
  signal.centroid=follow(signal.centroid,total>1e-12f?std::clamp(weighted/total,0.f,1.f):0,.065f,.18f);
  for(int i=0;i<spectrumSize;i++) {
    float energy=0,coverage=0;
    for(int k=spectrumFirst[i];k<=spectrumLast[i];k++) {
      float weight=overlap((k-.5f)*binWidth,(k+.5f)*binWidth,spectrumLo[i],spectrumHi[i]);energy+=power[k]*weight;coverage+=weight;
    }
    // Cover the whole logarithmic cell, including narrow high harmonics that
    // interpolation at the cell centre would miss entirely.
    float amplitude=std::sqrt(energy/std::min(1.f,std::max(coverage,1e-6f)))*signal.gain;
    float target=std::clamp((20*std::log10(amplitude+1e-9f)+66)/60.f,0.f,1.f)*gate;
    signal.spectrum[i]=follow(signal.spectrum[i],target,.018f,.18f);
    signal.spectrumPeaks[i]=std::max(signal.spectrum[i],signal.spectrumPeaks[i]-hopSeconds*.23f);
  }
  shortFft.run(history,head,attackBins);
  std::array<float,3> novelty{},counts{};
  for(int i=1;i<attackSize/2;i++) {
    auto other=std::conj(attackBins[attackSize-i]);auto l=(attackBins[i]+other)*.5f,r=(attackBins[i]-other)*std::complex<float>(0,-.5f);
    float magnitude=std::sqrt((std::norm(l)+std::norm(r))*.5f)*4/attackSize;
    float compressed=std::log1p(magnitude*40*signal.gain);
    float hz=float(i)*analysisRate/attackSize;
    if(hz>=30&&hz<20000) {int b=hz<250?0:hz<3500?1:2;novelty[b]+=std::max(0.f,compressed-previousMagnitude[i]);counts[b]++;}
    previousMagnitude[i]=compressed;
  }
  bool rhythmEvent=false;
  for(int b=0;b<3;b++) {
    signal.attacks[b]*=std::exp(-hopSeconds/(b==0?.15f:b==1?.12f:.09f));
    // Peak picking looks back one hop. A 12 ms hit may already have ended in
    // the current hop, so gate against recent RMS instead of this hop alone.
    bool event=detect(b,novelty[b]/std::sqrt(std::max(counts[b],1.f)),signal.rms);
    if(event&&b==0) {
      uint64_t interval=signal.analyzedSamples-lastLowSample;
      if(lastLowSample&&interval>=uint64_t(analysisRate*.18f)&&interval<=uint64_t(analysisRate*1.5f)) {
        lowIntervals[intervalHead]=interval;intervalHead=(intervalHead+1)%int(lowIntervals.size());intervalCount=std::min(intervalCount+1,int(lowIntervals.size()));
      }
      lastLowSample=signal.analyzedSamples;
    }
    if(event&&b<2) rhythmEvent=true;
  }
  signal.onset=std::max({signal.attacks[0],signal.attacks[1]*.85f,signal.attacks[2]*.6f});
  // Use the measured strength of low-frequency energy rises for tempo. Giving
  // every bass pluck and midrange attack equal weight can promote a syncopated
  // bass line over the kick. Sub-bass energy receives more weight here.
  float tempoEnergy=std::sqrt(bandPower[0]*4+bandPower[1])*signal.gain;
  float tempoFlux=std::max(0.f,tempoEnergy-tempoEnergyPrevious);tempoEnergyPrevious=tempoEnergy;
  rhythmicEnvelope=std::max(tempoFlux,rhythmicEnvelope*std::exp(-hopSeconds/.04f));rhythm[rhythmHead]=rhythmicEnvelope;
  rhythmHead=(rhythmHead+1)%int(rhythm.size());rhythmCount=std::min(rhythmCount+1,int(rhythm.size()));
  lastRhythmAge=rhythmEvent?0:lastRhythmAge+hopSeconds;
  if(++tempoDivider>=32) {tempoDivider=0;estimateTempo();}
  if(signal.tempoConfidence>.4f&&signal.bpm>0) {
    signal.beatPhase=std::fmod(signal.beatPhase+hopSeconds*signal.bpm/60,1.f);
    if(rhythmEvent&&signal.attacks[0]>.5f) {float error=signal.beatPhase>.5f?signal.beatPhase-1:signal.beatPhase;signal.beatPhase=std::fmod(signal.beatPhase-error*.3f+1,1.f);}
  }
  // A recent rising crossing stabilises the scope. Keep contiguous 48 kHz
  // samples, avoiding the old fourfold downsampling without an anti-alias filter.
  int trigger=fftSize-scopeSize;int channel=leftEnergy>=rightEnergy?0:1;
  for(int i=fftSize-scopeSize-1;i>=fftSize-scopeSize*2;i--) {
    if(history[(head+i)%fftSize][channel]<=0&&history[(head+i+1)%fftSize][channel]>0) {trigger=i+1;break;}
  }
  for(int i=0;i<scopeSize;i++) for(int c=0;c<2;c++) signal.scope[i*2+c]=std::tanh(history[(head+trigger+i)%fftSize][c]*signal.gain*2)*gate;
}
void Analyzer::estimateTempo() {
  int count=std::min(rhythmCount,768);
  if(count<375||lastRhythmAge>2.5f||signal.onsetCount[0]+signal.onsetCount[1]<6) {
    signal.tempoConfidence*=.75f;if(signal.tempoConfidence<.05f) signal.bpm=0;return;
  }
  std::array<float,128> scores{};float best=0;int lagBest=0;
  float hint=120;
  if(intervalCount>=4) {
    auto sorted=lowIntervals;auto middle=sorted.begin()+intervalCount/2;
    std::nth_element(sorted.begin(),middle,sorted.begin()+intervalCount);float median=float(*middle);
    double sum=0;int accepted=0;
    for(int i=0;i<intervalCount;i++) if(std::abs(float(lowIntervals[i])-median)<median*.2f) {sum+=lowIntervals[i];accepted++;}
    if(accepted) hint=float(60.0*analysisRate*accepted/sum);
    while(hint>200) hint*=.5f;while(hint<55) hint*=2;
  }
  auto at=[&](int age){return rhythm[(rhythmHead+int(rhythm.size())-1-age)%int(rhythm.size())];};
  for(int lag=28;lag<=102;lag++) {
    double dot=0,a=0,b=0;
    for(int i=0;i<count-lag;i++) {float x=at(i),y=at(i+lag);dot+=x*y;a+=x*x;b+=y*y;}
    float correlation=float(dot/std::sqrt(a*b+1e-20)),bpm=60/(hopSeconds*lag);
    // Timing between real low attacks distinguishes a beat from a stronger
    // autocorrelation peak two or three beats later (common at fast tempos).
    float prior=.8f+.2f*std::exp(-std::pow(std::log2(bpm/hint),2)*10);
    if(signal.bpm>0&&std::abs(std::log2(bpm/signal.bpm))<.08f) prior+=.02f;
    scores[lag]=correlation*prior;if(scores[lag]>best) {best=scores[lag];lagBest=lag;}
  }
  if(best<.35f||lagBest==0) {signal.tempoConfidence*=.8f;return;}
  float offset=0;
  if(lagBest>28&&lagBest<102) {float a=scores[lagBest-1],b=scores[lagBest],c=scores[lagBest+1];if(std::abs(a-2*b+c)>1e-6f) offset=std::clamp(.5f*(a-c)/(a-2*b+c),-.5f,.5f);}
  float bpm=60/(hopSeconds*(lagBest+offset));
  signal.bpm=signal.bpm==0||std::abs(bpm-signal.bpm)>12?bpm:signal.bpm+(bpm-signal.bpm)*.22f;
  signal.tempoConfidence=std::clamp((best-.25f)/.65f,0.f,1.f);
}
}
