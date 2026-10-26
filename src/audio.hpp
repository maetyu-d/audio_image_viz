#pragma once
#include "analysis.hpp"
#include "music.hpp"
#include "raylib.h"
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#endif
namespace av {
enum class Source { Synth, File, Input, Silent };
class Audio {
public:
  ~Audio();
  bool init(bool silent,uint32_t seed);
  void update(bool paused,bool muted,float volume);
  bool load(const std::string& path,bool repeat=true);
  bool finished() const;
  bool live();
  void internal();
  void shutdown();
  void offline(int frames);
  SampleRing ring;
  std::atomic<Source> source{Source::Silent};
  std::string name="SILENT / AMBIENT",status;
  bool ready=false;
  float position=0,duration=0;
private:
  AudioStream stream{};
  Music file{};
  std::atomic<uint64_t> fileFrames{0};
  std::atomic<uint64_t> fileTotalFrames{0};
  bool loopFile=true;
  std::atomic<bool> fileReadFailed{false};
  std::thread filePump;
  std::mutex pumpMutex;
  std::condition_variable pumpWake;
  bool pumpStopped=true;
  audioimage::TechnoSynth synth;
  std::atomic<bool> analysisActive{true};
  static Audio* active;
  static void generate(void* buffer,unsigned int frames);
  static void analyze(void* buffer,unsigned int frames);
  static void advanceFile(void* buffer,unsigned int frames);
  void stopSource();
  void stopFilePump();
  void refillFile();
#ifdef __APPLE__
  ExtAudioFileRef decodedFile=nullptr;
  SInt64 decodedStart=0;
  uint64_t decodedLoopFrames=0,decodedRead=0;
  AudioQueueRef capture=nullptr;
  int captureChannels=2;
  static void captureCallback(void*,AudioQueueRef,AudioQueueBufferRef,const AudioTimeStamp*,UInt32,const AudioStreamPacketDescription*);
#endif
};
}
