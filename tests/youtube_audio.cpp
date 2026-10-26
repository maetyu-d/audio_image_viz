#include "audio.hpp"
#include "youtube.hpp"
#include <chrono>
#include <iostream>
#include <thread>
int main(int argc,char** argv) {
  if(argc!=4&&argc!=5) {std::cerr<<"Usage: youtube_audio_tests TOOLS CACHE YOUTUBE_OR_SPOTIFY_PLAYLIST_URL [--auto]\n";return 2;}
  bool automatic=argc==5&&std::string(argv[4])=="--auto";
  SetTraceLogLevel(LOG_WARNING);av::Audio audio;av::Analyzer analyser;
  if(!audio.init(false,17)) return 1;
  audio.update(false,true,.7f);av::YouTube youtube(argv[1],argv[2]);
  if(!youtube.open(argv[3])) return 1;
  using Clock=std::chrono::steady_clock;
  auto deadline=Clock::now()+std::chrono::minutes(5);auto playing=Clock::time_point{};int played=0,index=-1;float peakRms=0;std::string lastStatus;
  while(Clock::now()<deadline) {
    if(auto track=youtube.takeReady()) {
      if(!audio.load(track->path.string(),false)) {std::cerr<<audio.status<<"\n";return 1;}
      index=track->index;played++;playing=Clock::now();peakRms=0;
      std::cout<<"Playing "<<index+1<<" / "<<youtube.state().count<<": "<<track->title<<"; duration="<<audio.duration<<"\n"<<std::flush;
      std::cout<<"Audio source: "<<track->audioURL<<"\n"<<std::flush;
    }
    auto state=youtube.state();if(!state.error.empty()) {std::cerr<<state.error<<"\n";return 1;}
    if(state.status!=lastStatus) {lastStatus=state.status;std::cout<<state.status<<"\n"<<std::flush;}
    audio.update(false,true,.7f);peakRms=std::max(peakRms,analyser.update(audio.ring,.02f).rms);
    bool complete=automatic?audio.finished():Clock::now()-playing>std::chrono::seconds(3)&&(peakRms>.001f||Clock::now()-playing>std::chrono::seconds(20));
    if(playing!=Clock::time_point{}&&complete) {
      if(peakRms<.001f) {std::cerr<<"No decoded audio reached the analyser after 20 seconds. Position="<<audio.position<<", analysed samples="<<audio.ring.count()-audio.ring.sourceStart()<<", status="<<audio.status<<"\n";return 1;}
      std::cout<<"Decoded audio RMS="<<peakRms<<", position="<<audio.position<<"\n"<<std::flush;
      if(played>=2) {youtube.stop();audio.shutdown();std::cout<<"PASS: playlist, two tracks, "<<(automatic?"automatic advance":"next navigation")<<" and native audio analysis.\n";return 0;}
      youtube.next();playing=Clock::time_point{};
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  std::cerr<<"Playlist integration timed out.\n";return 1;
}
