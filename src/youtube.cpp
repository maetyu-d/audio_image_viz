#include "youtube.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <csignal>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <spawn.h>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;

namespace av {
namespace {
using Clock=std::chrono::steady_clock;
bool idCharacter(unsigned char c) {return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-';}
std::string message(std::string error) {
  auto at=error.rfind("ERROR:");if(at!=std::string::npos) error.erase(0,at+6);
  while(!error.empty()&&std::isspace(static_cast<unsigned char>(error.front()))) error.erase(0,1);
  for(auto& c:error) if(c=='\r'||c=='\n'||c=='\t') c=' ';
  if(error.size()>400) error.resize(400);
  return error.empty()?"YouTube could not provide audio for this track.":error;
}
std::string outputTemplate(const std::filesystem::path& path) {
  std::string result;for(char c:path.string()) {result+=c;if(c=='%') result+='%';}return result;
}
std::string cacheKey(const YouTubeTrack& track) {return (track.spotify?"spotify-":"")+track.id;}
std::string trackTitle(const YouTubeTrack& track) {return track.artist.empty()?track.title:track.artist+" / "+track.title;}
bool videoID(const std::string& id) {return id.size()==11&&std::all_of(id.begin(),id.end(),idCharacter);}
}
std::string youtubePlaylistURL(std::string text) {
  auto first=text.find_first_not_of(" \t\r\n"),last=text.find_last_not_of(" \t\r\n");
  if(first==std::string::npos||text.size()>4096) return {};
  text=text.substr(first,last-first+1);
  if(text.rfind("youtube.com/",0)==0||text.rfind("www.youtube.com/",0)==0||text.rfind("music.youtube.com/",0)==0||text.rfind("youtu.be/",0)==0) text="https://"+text;
  auto scheme=text.find("://");if(scheme==std::string::npos) return {};
  auto protocol=text.substr(0,scheme);if(protocol!="https"&&protocol!="http") return {};
  auto end=text.find_first_of("/?#",scheme+3);if(end==std::string::npos) return {};
  auto host=text.substr(scheme+3,end-scheme-3);
  for(auto& c:host) c=char(std::tolower(static_cast<unsigned char>(c)));
  if(host!="youtube.com"&&host!="www.youtube.com"&&host!="m.youtube.com"&&host!="music.youtube.com"&&host!="youtu.be") return {};
  auto query=text.find('?',end);if(query==std::string::npos) return {};
  for(size_t at=query+1;at<text.size();) {
    auto next=text.find_first_of("&#",at);auto part=text.substr(at,next==std::string::npos?next:next-at);
    if(part.rfind("list=",0)==0) {
      auto id=part.substr(5);if(id.empty()||id.size()>128||!std::all_of(id.begin(),id.end(),idCharacter)) return {};
      return "https://www.youtube.com/playlist?list="+id;
    }
    if(next==std::string::npos||text[next]=='#') break;at=next+1;
  }
  return {};
}
YouTube::YouTube(std::filesystem::path tools,std::filesystem::path cache,SpotifyReader reader):tools(std::move(tools)),cache(std::move(cache)),spotifyReader(std::move(reader)) {
  worker=std::thread([this]{work();});
}
YouTube::~YouTube() {closing=true;revision++;cancelProcess();wake.notify_one();if(worker.joinable()) worker.join();}
bool YouTube::alive(uint64_t version) const {return !closing&&revision.load()==version;}
void YouTube::cancelProcess() {int pid=child.load();if(pid>0) kill(-pid,SIGTERM);}
bool YouTube::open(const std::string& url) {
  auto canonical=playlistURL(url);
  if(canonical.empty()) {std::lock_guard<std::mutex> lock(mutex);if(!current.active) current.error="Paste a YouTube or public Spotify playlist link.";return false;}
  auto version=++revision;cancelProcess();
  {std::lock_guard<std::mutex> lock(mutex);tracks.clear();ready.reset();current={};current.active=current.busy=true;
    current.url=canonical;current.spotify=!spotifyPlaylistURL(canonical).empty();
    current.status=current.spotify?"Loading public Spotify playlist...":"Loading YouTube playlist...";job=Job{version,true,0,canonical};}
  wake.notify_one();return true;
}
bool YouTube::next(int step) {
  {std::lock_guard<std::mutex> lock(mutex);
    if(!current.active||tracks.empty()) return false;
    auto version=++revision;cancelProcess();
    int n=int(tracks.size());current.index=((current.index+step)%n+n)%n;current.busy=true;current.error.clear();
    current.status="Preparing next track...";ready.reset();job=Job{version,false,current.index,current.url,step<0?-1:1};}
  wake.notify_one();return true;
}
void YouTube::stop() {
  revision++;cancelProcess();std::lock_guard<std::mutex> lock(mutex);
  current.active=current.busy=false;current.error.clear();current.status.clear();ready.reset();job.reset();
}
YouTubeState YouTube::state() const {std::lock_guard<std::mutex> lock(mutex);return current;}
std::optional<YouTubeReady> YouTube::takeReady() {
  std::lock_guard<std::mutex> lock(mutex);
  if(ready&&ready->revision!=revision.load()) ready.reset();
  auto result=std::move(ready);ready.reset();return result;
}
void YouTube::failed(const std::string& error) {
  std::filesystem::path invalid;
  {std::lock_guard<std::mutex> lock(mutex);if(!current.active||tracks.empty()) return;tracks[current.index].failed=true;current.error=error;invalid=cache/(cacheKey(tracks[current.index])+".m4a");}
  std::error_code ignored;std::filesystem::remove(invalid,ignored);
  next();
}
YouTube::Process YouTube::run(const std::vector<std::string>& options,uint64_t version,int seconds) {
  Process result;if(!alive(version)) {result.cancelled=true;return result;}
  auto executable=tools/"yt-dlp";
  std::vector<std::string> arguments{executable.string(),"--ignore-config","--no-colors","--no-warnings","--no-progress",
    "--no-cache-dir","--no-plugin-dirs","--js-runtimes","quickjs:"+(tools/"qjs").string(),"--socket-timeout","15","--retries","2"};
  arguments.insert(arguments.end(),options.begin(),options.end());
  std::vector<char*> pointers;for(auto& value:arguments) pointers.push_back(value.data());pointers.push_back(nullptr);
  int out[2]{-1,-1},err[2]{-1,-1};
  if(pipe(out)!=0||pipe(err)!=0) {for(int fd:{out[0],out[1],err[0],err[1]}) if(fd>=0) close(fd);result.error="Could not create YouTube helper pipes.";return result;}
  posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions,out[1],STDOUT_FILENO);posix_spawn_file_actions_adddup2(&actions,err[1],STDERR_FILENO);
  posix_spawn_file_actions_addopen(&actions,STDIN_FILENO,"/dev/null",O_RDONLY,0);
  for(int fd:{out[0],out[1],err[0],err[1]}) posix_spawn_file_actions_addclose(&actions,fd);
  posix_spawnattr_t attributes;posix_spawnattr_init(&attributes);posix_spawnattr_setflags(&attributes,POSIX_SPAWN_SETPGROUP);posix_spawnattr_setpgroup(&attributes,0);
  pid_t pid=0;int error=posix_spawn(&pid,executable.c_str(),&actions,&attributes,pointers.data(),environ);
  posix_spawnattr_destroy(&attributes);posix_spawn_file_actions_destroy(&actions);close(out[1]);close(err[1]);
  if(error) {close(out[0]);close(err[0]);result.error="YouTube helper unavailable. Reinstall the complete app bundle.";return result;}
  child=pid;fcntl(out[0],F_SETFL,O_NONBLOCK);fcntl(err[0],F_SETFL,O_NONBLOCK);
  const auto started=Clock::now();auto terminating=Clock::time_point{};int status=0;
  auto drain=[&](int fd,std::string& text,size_t limit) {
    std::array<char,8192> bytes{};for(;;) {auto n=read(fd,bytes.data(),bytes.size());if(n<=0) break;
      if(text.size()+size_t(n)>limit) {kill(-pid,SIGTERM);result.error="YouTube returned too much data.";return;}
      text.append(bytes.data(),size_t(n));}
  };
  for(;;) {
    pollfd descriptors[2]{{out[0],POLLIN,0},{err[0],POLLIN,0}};poll(descriptors,2,40);
    drain(out[0],result.output,8*1024*1024);drain(err[0],result.error,64*1024);
    auto done=waitpid(pid,&status,WNOHANG);if(done==pid) break;
    if(done<0&&errno!=EINTR) break;
    bool cancelled=!alive(version);bool timeout=Clock::now()-started>std::chrono::seconds(seconds);
    if(cancelled||timeout) {
      result.cancelled=cancelled;
      if(timeout&&!cancelled) result.error="YouTube timed out. Check your connection and try again.";
      if(terminating==Clock::time_point{}) {terminating=Clock::now();kill(-pid,SIGTERM);}
      if(Clock::now()-terminating>std::chrono::milliseconds(500)) kill(-pid,SIGKILL);
    }
  }
  drain(out[0],result.output,8*1024*1024);drain(err[0],result.error,64*1024);close(out[0]);close(err[0]);child=0;
  result.code=WIFEXITED(status)?WEXITSTATUS(status):-1;result.cancelled=result.cancelled||!alive(version);return result;
}
bool YouTube::prepare(const YouTubeTrack& track,uint64_t version,std::filesystem::path& path,std::string& error,std::string& audioURL) {
  std::error_code failure;std::filesystem::create_directories(cache,failure);
  if(failure) {error="Could not create the temporary YouTube audio cache.";return false;}
  auto key=cacheKey(track);path=cache/(key+".m4a");
  std::string video=track.id;
  auto source=cache/(key+".youtube");
  if(track.spotify) {std::ifstream saved(source);std::getline(saved,video);if(!videoID(video)) video.clear();}
  if(std::filesystem::is_regular_file(path,failure)&&std::filesystem::file_size(path,failure)>0&&videoID(video)) {
    audioURL="https://www.youtube.com/watch?v="+video;return alive(version);
  }
  std::filesystem::remove(path,failure);
  if(track.spotify) {
    {std::lock_guard<std::mutex> lock(mutex);if(alive(version)&&current.busy) current.status="Matching on YouTube / "+trackTitle(track);}
    auto search=run({"--flat-playlist","--dump-single-json","--skip-download","--","ytsearch5:"+track.artist+" "+track.title+" audio"},version,90);
    if(search.cancelled) return false;
    if(search.code!=0) {error=message(search.error);return false;}
    video=matchSpotifyTrack(track,search.output);
    if(video.empty()) {error="No close YouTube recording matched "+trackTitle(track)+". Unmatched tracks are skipped.";return false;}
  }
  audioURL="https://www.youtube.com/watch?v="+video;
  auto result=run({"--no-playlist","--format","bestaudio[ext=m4a]","--fixup","never","--no-mtime","--max-filesize","512M",
    "--fragment-retries","2","--output",outputTemplate(path),"--",audioURL},version,240);
  if(result.cancelled) return false;
  if(result.code!=0||!std::filesystem::is_regular_file(path,failure)||std::filesystem::file_size(path,failure)==0) {
    error=message(result.error);return false;
  }
  if(!alive(version)) return false;
  if(track.spotify) {
    std::ofstream saved(source,std::ios::trunc);saved<<video<<'\n';
    if(!saved) {error="Could not save the matched recording in the temporary cache.";return false;}
  }
  return true;
}
void YouTube::trimCache(const std::string& keep,const std::string& next) {
  std::error_code error;std::vector<std::filesystem::directory_entry> files;uintmax_t bytes=0;
  for(auto& file:std::filesystem::directory_iterator(cache,error)) {
    if(file.is_regular_file(error)) {bytes+=file.file_size(error);files.push_back(file);}
  }
  std::sort(files.begin(),files.end(),[&](const auto& a,const auto& b){return a.last_write_time(error)<b.last_write_time(error);});
  for(auto& file:files) {
    if(bytes<=384ull*1024*1024) break;
    auto name=file.path().stem().string();if(name==keep||name==next) continue;
    auto size=file.file_size(error);if(std::filesystem::remove(file.path(),error)) bytes-=std::min(bytes,size);
  }
}
void YouTube::work() {
  while(!closing) {
    Job request;
    {std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[&]{return closing||job.has_value();});if(closing) break;request=*job;job.reset();}
    try {
      if(request.list) {
        YouTubeList list;
        if(!spotifyPlaylistURL(request.url).empty()) {
          auto page=spotifyReader(request.url,[&]{return alive(request.revision);});
          if(!alive(request.revision)) continue;
          list=parseSpotifyList(page);
        } else {
          auto result=run({"--flat-playlist","--dump-single-json","--skip-download","--yes-playlist","--",request.url},request.revision,90);
          if(result.cancelled) continue;
          if(result.code!=0) throw std::runtime_error(message(result.error));
          list=parseYouTubeList(result.output);
        }
        std::lock_guard<std::mutex> lock(mutex);if(!alive(request.revision)) continue;
        tracks=std::move(list.tracks);current.title=list.title;current.partial=list.partial;
        current.count=int(tracks.size());current.index=0;current.status="Preparing first track...";
      }
      std::vector<YouTubeTrack> entries;
      {std::lock_guard<std::mutex> lock(mutex);if(!alive(request.revision)) continue;entries=tracks;}
      bool found=false;int selected=request.index;std::string lastError;
      for(int attempt=0;attempt<int(entries.size())&&alive(request.revision);attempt++,selected=(selected+request.direction+int(entries.size()))%int(entries.size())) {
        if(entries[selected].failed) continue;
        std::filesystem::path path;std::string audioURL;
        if(prepare(entries[selected],request.revision,path,lastError,audioURL)) {
          std::lock_guard<std::mutex> lock(mutex);if(!alive(request.revision)) break;
          current.index=selected;current.track=trackTitle(entries[selected]);current.busy=false;current.error.clear();current.audioURL=audioURL;
          current.status=(current.spotify?"Spotify order / YouTube audio / ":"YouTube / ")+std::to_string(selected+1)+" of "+std::to_string(entries.size());
          if(current.partial) current.status+=" public entries (may be partial)";
          ready=YouTubeReady{path,current.track,selected,request.revision,audioURL};found=true;break;
        }
        if(!alive(request.revision)) break;
        std::lock_guard<std::mutex> lock(mutex);tracks[selected].failed=true;
        current.status="Skipping unavailable track / "+trackTitle(entries[selected]);
      }
      if(!alive(request.revision)) continue;
      if(!found) throw std::runtime_error(lastError.empty()?"No playable tracks remain. Open the playlist again to retry.":lastError);
      // Prepare one next track while this one plays. Cached files use the same
      // native decoder/analyser as local audio; the graphics loop never waits.
      for(int step=1;step<int(entries.size())&&alive(request.revision);step++) {
        int next=(selected+step)%int(entries.size());
        {std::lock_guard<std::mutex> lock(mutex);if(tracks[next].failed) continue;}
        std::filesystem::path path;std::string error,audioURL;
        if(prepare(entries[next],request.revision,path,error,audioURL)) {trimCache(cacheKey(entries[selected]),cacheKey(entries[next]));break;}
        if(alive(request.revision)) {std::lock_guard<std::mutex> lock(mutex);tracks[next].failed=true;}
      }
    } catch(const std::exception& error) {
      std::lock_guard<std::mutex> lock(mutex);if(alive(request.revision)) {current.busy=false;current.error=error.what();current.status=current.spotify?"Spotify playlist import could not start playback.":"YouTube could not start playback.";}
    }
  }
}
}
