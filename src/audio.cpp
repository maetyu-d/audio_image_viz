#include "audio.hpp"
#include "mp3_gapless.hpp"
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <system_error>
namespace av {
Audio* Audio::active=nullptr;
Audio::~Audio() {shutdown();}
bool Audio::init(bool silent,uint32_t seed) {
  synth.reset(seed);ring.beginSource();
  if(silent) return true;
  InitAudioDevice();ready=IsAudioDeviceReady();
  if(!ready) {status="Audio device unavailable. Ambient visuals remain active.";return false;}
  active=this;SetAudioStreamBufferSizeDefault(512);
  stream=LoadAudioStream(48000,32,2);
  if(!IsAudioStreamValid(stream)) {CloseAudioDevice();ready=false;active=nullptr;return false;}
  SetAudioStreamCallback(stream,generate);
  AttachAudioMixedProcessor(analyze);
  internal();return true;
}
void Audio::generate(void* buffer,unsigned int frames) {if(active) active->synth.render(static_cast<float*>(buffer),int(frames));}
void Audio::analyze(void* buffer,unsigned int frames) {
  if(active&&active->analysisActive.load()&&(active->source==Source::Synth||active->source==Source::File)) active->ring.push(static_cast<float*>(buffer),int(frames));
}
void Audio::advanceFile(void* buffer,unsigned int frames) {
  if(!active) return;
  const auto before=active->fileFrames.fetch_add(frames,std::memory_order_relaxed);
  if(!active->loopFile) {
    const auto end=active->fileTotalFrames.load(std::memory_order_relaxed);
    const auto keep=unsigned(std::min(uint64_t(frames),end>before?end-before:0));
    auto* samples=static_cast<float*>(buffer);std::fill(samples+keep*2,samples+frames*2,0.f);
  }
}
void Audio::stopSource() {
  if(!ready) return;
  Source previous=source.exchange(Source::Silent);
  if(previous==Source::Synth) StopAudioStream(stream);
  if(previous==Source::File) {
    stopFilePump();StopAudioStream(file.stream);DetachAudioStreamProcessor(file.stream,advanceFile);
#ifdef __APPLE__
    if(decodedFile) {UnloadAudioStream(file.stream);ExtAudioFileDispose(decodedFile);decodedFile=nullptr;}
    else
#endif
      UnloadMusicStream(file);
    file={};
  }
#ifdef __APPLE__
  if(capture) {AudioQueueStop(capture,true);AudioQueueDispose(capture,true);capture=nullptr;}
#endif
  // Output callbacks are synchronized by the raylib device lock in Stop*.
  ring.beginSource();position=duration=0;
}
void Audio::stopFilePump() {
  {
    std::lock_guard<std::mutex> lock(pumpMutex);pumpStopped=true;
  }
  pumpWake.notify_one();
  if(filePump.joinable()) filePump.join();
}
void Audio::refillFile() {
#ifdef __APPLE__
  if(decodedFile) {
    // Core Audio converts integer/float PCM, sample rates and channel layouts
    // into one consistent stereo format. Disk I/O stays off the device callback.
    constexpr UInt32 blockFrames=4096;
    std::array<float,blockFrames*2> block{};
    while(IsAudioStreamProcessed(file.stream)) {
      UInt32 filled=0;bool justRewound=false;
      while(filled<blockFrames&&!fileReadFailed.load()) {
        if(decodedLoopFrames&&decodedRead==decodedLoopFrames) {
          if(!loopFile) break;
          if(decodedStart>0&&ExtAudioFileSeek(decodedFile,0)!=noErr) {fileReadFailed=true;break;}
          if(ExtAudioFileSeek(decodedFile,decodedStart)!=noErr) {fileReadFailed=true;break;}
          decodedRead=0;
        }
        UInt32 count=blockFrames-filled;
        if(decodedLoopFrames) count=UInt32(std::min(uint64_t(count),decodedLoopFrames-decodedRead));
        AudioBufferList buffers{};buffers.mNumberBuffers=1;
        buffers.mBuffers[0]={2,UInt32(count*2*sizeof(float)),block.data()+filled*2};
        if(ExtAudioFileRead(decodedFile,&count,&buffers)!=noErr) {fileReadFailed=true;break;}
        if(count>0) {filled+=count;decodedRead+=count;justRewound=false;}
        else {
          if(!loopFile) {
            fileTotalFrames=decodedRead;decodedLoopFrames=decodedRead;
            if(decodedRead==0) fileReadFailed=true;
            break;
          }
          // Loop at the decoded end, including tracks shorter than one block.
          // An unreadable/truncated stream must never spin in an EOF loop.
          if(justRewound||ExtAudioFileSeek(decodedFile,decodedStart)!=noErr) {fileReadFailed=true;break;}
          decodedRead=0;justRewound=true;
        }
      }
      std::fill(block.begin()+filled*2,block.end(),0.f);
      UpdateAudioStream(file.stream,block.data(),blockFrames);
    }
    return;
  }
#endif
  UpdateMusicStream(file);
}
void Audio::internal() {
  if(!ready) return;
  stopSource();source=Source::Synth;name="INTERNAL / AUDIOIMAGE TECHNO";status.clear();PlayAudioStream(stream);
}
bool Audio::load(const std::string& path,bool repeat) {
  if(!ready) {status="Audio output unavailable. Relaunch without --no-audio.";return false;}
  const auto extension=std::filesystem::path(path).extension().string();
  std::string ext=extension;for(auto& c:ext) c=char(std::tolower(static_cast<unsigned char>(c)));
  if(ext!=".wav"&&ext!=".mp3"&&ext!=".ogg"&&ext!=".flac"
#ifdef __APPLE__
     &&ext!=".m4a"&&ext!=".aac"&&ext!=".aif"&&ext!=".aiff"&&ext!=".caf"
#endif
  ) {status="Choose WAV, MP3, FLAC, OGG, M4A or AIFF audio.";return false;}
  // The synth callback needs a small buffer, but files need enough headroom
  // for decoding and disk reads. Never depend on the video frame rate to fill it.
  SetAudioStreamBufferSizeDefault(4096);
  Music next{};float nextDuration=0;
#ifdef __APPLE__
  ExtAudioFileRef nextDecoder=nullptr;
  SInt64 nextStart=0;uint64_t nextLoopFrames=0;
  auto* url=CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,
    reinterpret_cast<const UInt8*>(path.data()),path.size(),false);
  if(url) {
    if(ExtAudioFileOpenURL(url,&nextDecoder)!=noErr) nextDecoder=nullptr;
    CFRelease(url);
  }
  if(nextDecoder) {
    AudioStreamBasicDescription original{};UInt32 size=sizeof(original);SInt64 length=0;
    bool valid=ExtAudioFileGetProperty(nextDecoder,kExtAudioFileProperty_FileDataFormat,&size,&original)==noErr;
    size=sizeof(length);
    valid=valid&&ExtAudioFileGetProperty(nextDecoder,kExtAudioFileProperty_FileLengthFrames,&size,&length)==noErr
      &&length>0&&original.mSampleRate>0;
    if(valid&&original.mFormatID==kAudioFormatMPEGLayer3) {
      AudioFilePacketTableInfo table{};size=sizeof(table);
      if(ExtAudioFileGetProperty(nextDecoder,kExtAudioFileProperty_PacketTable,&size,&table)!=noErr) {
        if(auto gapless=mp3Gapless(path);gapless&&gapless->frames+gapless->delay<=uint64_t(length)) {
          nextStart=SInt64(gapless->delay);length=SInt64(gapless->frames);
          nextLoopFrames=uint64_t(std::llround(double(length)*48000./original.mSampleRate));
        }
      }
    }
    AudioStreamBasicDescription pcm{};pcm.mSampleRate=48000;pcm.mFormatID=kAudioFormatLinearPCM;
    pcm.mFormatFlags=kAudioFormatFlagIsFloat|kAudioFormatFlagIsPacked;
    pcm.mFramesPerPacket=1;pcm.mChannelsPerFrame=2;pcm.mBitsPerChannel=32;
    pcm.mBytesPerPacket=pcm.mBytesPerFrame=2*sizeof(float);
    valid=valid&&ExtAudioFileSetProperty(nextDecoder,kExtAudioFileProperty_ClientDataFormat,sizeof(pcm),&pcm)==noErr;
    if(valid&&nextStart>0) valid=ExtAudioFileSeek(nextDecoder,nextStart)==noErr;
    // A one-frame read followed by another non-zero seek can advance the
    // system MPEG-2 decoder twice. Trimmed MP3s are validated by refillFile().
    if(valid&&nextStart==0) {
      float firstFrame[2]{};UInt32 count=1;AudioBufferList buffers{};buffers.mNumberBuffers=1;
      buffers.mBuffers[0]={2,sizeof(firstFrame),firstFrame};
      valid=ExtAudioFileRead(nextDecoder,&count,&buffers)==noErr&&count>0&&ExtAudioFileSeek(nextDecoder,nextStart)==noErr;
    }
    if(valid) {next.stream=LoadAudioStream(48000,32,2);nextDuration=float(length/original.mSampleRate);}
    if(!IsAudioStreamValid(next.stream)) {ExtAudioFileDispose(nextDecoder);nextDecoder=nullptr;}
  }
  // Vorbis is not a system codec on macOS; retain raylib's streaming decoder.
  if(!nextDecoder)
#endif
  {
    next=LoadMusicStream(path.c_str());
    if(IsMusicValid(next)) nextDuration=GetMusicTimeLength(next);
  }
  SetAudioStreamBufferSizeDefault(512);
  if(nextDuration<=0||!IsAudioStreamValid(next.stream)) {
    UnloadMusicStream(next);status="Could not decode that audio file.";return false;
  }
  stopSource();file=next;file.looping=true;loopFile=repeat;
  fileTotalFrames=uint64_t(std::llround(double(nextDuration)*48000.));
#ifdef __APPLE__
  decodedFile=nextDecoder;
  decodedStart=nextStart;decodedLoopFrames=repeat?nextLoopFrames:fileTotalFrames.load();decodedRead=0;
#endif
  fileFrames=0;fileReadFailed=false;refillFile(); // Prime both halves before playback.
  if(fileReadFailed) {
#ifdef __APPLE__
    if(decodedFile) {UnloadAudioStream(file.stream);ExtAudioFileDispose(decodedFile);decodedFile=nullptr;}
#endif
    file={};internal();status="Could not read audio from that file.";return false;
  }
  AttachAudioStreamProcessor(file.stream,advanceFile);
  pumpStopped=false;
  try {
    filePump=std::thread([this] {
      std::unique_lock<std::mutex> lock(pumpMutex);
      while(!pumpStopped) {
        lock.unlock();refillFile();lock.lock();
        pumpWake.wait_for(lock,std::chrono::milliseconds(4),[this]{return pumpStopped;});
      }
    });
  } catch(const std::system_error&) {
    source=Source::File;stopSource();internal();
    status="Could not start audio streaming. Try opening the file again.";return false;
  }
  source=Source::File;
  name=std::filesystem::path(path).filename().string();duration=nextDuration;
  status=repeat?"FILE LOOPS / DROP ANOTHER TO REPLACE":"FILE / PLAYING ONCE";PlayAudioStream(file.stream);return true;
}
bool Audio::finished() const {
  return source==Source::File&&!loopFile&&fileTotalFrames.load()>0&&fileFrames.load()>=fileTotalFrames.load();
}
bool Audio::live() {
  if(!ready) {status="Audio device unavailable.";return false;}
#ifdef __APPLE__
  stopSource();
  AudioStreamBasicDescription format{};format.mSampleRate=48000;
  format.mFormatID=kAudioFormatLinearPCM;format.mFormatFlags=kAudioFormatFlagIsFloat|kAudioFormatFlagIsPacked;
  format.mFramesPerPacket=1;format.mBitsPerChannel=32;
  OSStatus result=-1;
  for(int channels:{2,1}) {
    captureChannels=channels;format.mChannelsPerFrame=channels;
    format.mBytesPerPacket=format.mBytesPerFrame=channels*sizeof(float);
    result=AudioQueueNewInput(&format,captureCallback,this,nullptr,nullptr,0,&capture);
    if(result==noErr) {
      for(int i=0;i<3&&result==noErr;i++) {AudioQueueBufferRef buffer=nullptr;
        result=AudioQueueAllocateBuffer(capture,512*channels*sizeof(float),&buffer);
        if(result==noErr) result=AudioQueueEnqueueBuffer(capture,buffer,0,nullptr);}
      if(result==noErr) result=AudioQueueStart(capture,nullptr);
    }
    if(result==noErr) break;
    if(capture) {AudioQueueDispose(capture,true);capture=nullptr;}
  }
  if(result!=noErr) {if(capture) {AudioQueueDispose(capture,true);capture=nullptr;}
    internal();status="Live input unavailable. Check microphone permission and default input.";return false;}
  source=Source::Input;name="LIVE / DEFAULT AUDIO INPUT";
  status="Select your input or loopback device in macOS Sound settings.";return true;
#else
  status="Live capture is available in the macOS build. Drop an audio file here.";return false;
#endif
}
#ifdef __APPLE__
void Audio::captureCallback(void* userdata,AudioQueueRef queue,AudioQueueBufferRef buffer,const AudioTimeStamp*,UInt32,const AudioStreamPacketDescription*) {
  auto* self=static_cast<Audio*>(userdata);
  self->ring.push(static_cast<float*>(buffer->mAudioData),int(buffer->mAudioDataByteSize/(sizeof(float)*self->captureChannels)),self->captureChannels);
  AudioQueueEnqueueBuffer(queue,buffer,0,nullptr);
}
#endif
void Audio::update(bool paused,bool muted,float volume) {
  if(!ready) return;
  analysisActive.store(!paused);
  SetMasterVolume(muted?0:volume);
  if(source==Source::File) {
    if(paused||finished()) PauseAudioStream(file.stream);else if(!IsAudioStreamPlaying(file.stream)) ResumeAudioStream(file.stream);
    double played=double(fileFrames.load(std::memory_order_relaxed))/48000.;
    position=duration>0?(loopFile?float(std::fmod(played,double(duration))):std::min(float(played),duration)):0;
    if(fileReadFailed.load()) status="Audio file could not be read completely. Open another file.";
  } else if(source==Source::Synth) {if(paused) PauseAudioStream(stream);else if(!IsAudioStreamPlaying(stream)) ResumeAudioStream(stream);}
}
void Audio::offline(int frames) {
  std::array<float,512*2> block{};
  while(frames>0) {int n=std::min(frames,512);synth.render(block.data(),n);ring.push(block.data(),n);frames-=n;}
}
void Audio::shutdown() {
  if(!ready) return;
  stopSource();DetachAudioMixedProcessor(analyze);UnloadAudioStream(stream);
  CloseAudioDevice();active=nullptr;ready=false;
}
}
