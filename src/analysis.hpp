#pragma once
#include <array>
#include <atomic>
#include <complex>
#include <cstdint>
#include <limits>
namespace av {
constexpr int analysisRate=48000,fftSize=4096,attackSize=1024,hopSize=512;
constexpr int spectrumSize=256,scopeSize=512,bandCount=7;
inline constexpr std::array<float,bandCount+1> bandEdges{20,60,160,400,1200,3500,9000,20000};
struct AttackEvent {uint64_t sample=0;float strength=0;};
struct Signal {
  std::array<float,spectrumSize> spectrum{},spectrumPeaks{};
  std::array<float,scopeSize*2> scope{};
  std::array<float,bandCount> bands{};
  std::array<float,3> attacks{};
  std::array<uint64_t,3> onsetCount{};
  // Keep four actual onsets per band, so a new hit cannot relocate a previous
  // burst and short percussion survives slower video frames.
  std::array<std::array<AttackEvent,4>,3> hits{};
  float bass=0,mid=0,high=0,rms=0,peak=0,onset=0,centroid=0,stereo=0;
  float width=0,correlation=0,gain=1,level=0,bpm=0,tempoConfidence=0,beatPhase=0;
  uint64_t analyzedSamples=0,droppedSamples=0;
};
// Per-slot frame stamps let the reader reject an overwrite without accepting
// a torn stereo pair. The audio writer never allocates or waits.
class SampleRing {
public:
  static constexpr int capacity=32768;
  void push(const float* data,int frames,int channels=2);
  bool frame(uint64_t absolute,float& left,float& right) const;
  void beginSource(); // Call after stopping the previous producer.
  uint64_t count() const {return published.load(std::memory_order_acquire);}
  uint64_t generation() const {return epoch.load(std::memory_order_acquire);}
  uint64_t sourceStart() const {return boundary.load(std::memory_order_acquire);}
private:
  static_assert(std::atomic<float>::is_always_lock_free,"Audio samples require lock-free atomics");
  std::array<std::atomic<float>,capacity*2> samples{};
  std::array<std::atomic<uint64_t>,capacity> stamps{};
  std::atomic_flag producing=ATOMIC_FLAG_INIT;
  uint64_t written=0;
  std::atomic<uint64_t> published{0},epoch{0},boundary{0};
};
template<int Size> struct FftPlan {
  FftPlan();
  void run(const std::array<std::array<float,2>,fftSize>& history,int head,std::array<std::complex<float>,Size>& output) const;
  std::array<float,Size> window{};
  std::array<int,Size> reverse{};
  std::array<std::complex<float>,Size/2> roots{};
  float powerScale=0;
};
class Analyzer {
public:
  Analyzer();
  // Consume all unseen samples; FFT hops, envelope timing and peak picking use
  // the audio sample clock rather than the video frame rate.
  const Signal& update(const SampleRing&,float renderDelta);
  const Signal& current() const {return signal;}
  void reset();
private:
  struct Detector {
    std::array<float,128> history{};
    int position=0,count=0;
    float previous=0,beforePrevious=0,previousThreshold=1,cooldown=0;
  };
  FftPlan<fftSize> longFft;
  FftPlan<attackSize> shortFft;
  std::array<std::array<float,2>,fftSize> history{};
  std::array<std::complex<float>,fftSize> bins{};
  std::array<std::complex<float>,attackSize> attackBins{};
  std::array<std::array<float,fftSize/2>,bandCount> bandWeights{};
  std::array<std::array<float,3>,fftSize/2> broadWeights{};
  std::array<int,spectrumSize> spectrumFirst{},spectrumLast{};
  std::array<float,spectrumSize> spectrumLo{},spectrumHi{};
  std::array<float,attackSize/2> previousMagnitude{};
  std::array<Detector,3> detectors{};
  std::array<float,1024> rhythm{};
  std::array<uint64_t,32> lowIntervals{};
  Signal signal{};
  uint64_t cursor=0,epoch=std::numeric_limits<uint64_t>::max();
  int head=0,hop=0,rhythmHead=0,rhythmCount=0,tempoDivider=0;
  std::array<float,2> dcInput{},dcOutput{};
  double hopEnergy=0,leftEnergy=0,rightEnergy=0,crossEnergy=0,sideEnergy=0;
  float hopPeak=0,reference=.1f,gate=0,peakHold=0,idle=0,peakMemory=0;
  float rhythmicEnvelope=0,lastRhythmAge=100,tempoEnergyPrevious=0;
  uint64_t lastLowSample=0;
  int intervalHead=0,intervalCount=0;
  void clearState();
  void sample(float left,float right);
  void analyzeHop();
  bool detect(int band,float novelty,float rms);
  void estimateTempo();
};
}
