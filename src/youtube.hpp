#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace av {
struct YouTubeTrack {
  std::string id,title;double duration=0;bool failed=false;
  bool spotify=false;std::string artist;
};
struct YouTubeList {std::string title;std::vector<YouTubeTrack> tracks;bool partial=false;};
struct YouTubeState {
  bool active=false,busy=false,spotify=false,partial=false;
  int index=0,count=0;
  std::string url,title,track,status,error,audioURL;
};
struct YouTubeReady {std::filesystem::path path;std::string title;int index=0;uint64_t revision=0;std::string audioURL;};
std::string youtubePlaylistURL(std::string text);
std::string spotifyPlaylistURL(std::string text);
std::string playlistURL(const std::string& text);
YouTubeList parseYouTubeList(const std::string& json);
YouTubeList parseSpotifyList(const std::string& html);
std::string matchSpotifyTrack(const YouTubeTrack& track,const std::string& json);
using SpotifyReader=std::function<std::string(const std::string&,const std::function<bool()>&)>;
std::string readSpotifyPlaylist(const std::string& url,const std::function<bool()>& active);

class YouTube {
public:
  // Spotify imports its public track order; the audio transport remains YouTube.
  YouTube(std::filesystem::path tools,std::filesystem::path cache,SpotifyReader reader=readSpotifyPlaylist);
  ~YouTube();
  bool open(const std::string& url);
  bool next(int step=1);
  void stop();
  YouTubeState state() const;
  std::optional<YouTubeReady> takeReady();
  void failed(const std::string& message);
private:
  struct Job {uint64_t revision=0;bool list=false;int index=0;std::string url;int direction=1;};
  struct Process {int code=-1;bool cancelled=false;std::string output,error;};
  const std::filesystem::path tools,cache;
  const SpotifyReader spotifyReader;
  mutable std::mutex mutex;
  std::condition_variable wake;
  std::thread worker;
  std::atomic<bool> closing{false};
  std::atomic<uint64_t> revision{0};
  std::atomic<int> child{0};
  YouTubeState current;
  std::vector<YouTubeTrack> tracks;
  std::optional<Job> job;
  std::optional<YouTubeReady> ready;
  bool alive(uint64_t version) const;
  void cancelProcess();
  void work();
  Process run(const std::vector<std::string>& arguments,uint64_t version,int seconds);
  bool prepare(const YouTubeTrack&,uint64_t,std::filesystem::path&,std::string&,std::string&);
  void trimCache(const std::string& keep,const std::string& next);
};
}
