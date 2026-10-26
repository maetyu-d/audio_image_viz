#include "youtube.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
namespace {
using Clock=std::chrono::steady_clock;
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
std::optional<av::YouTubeReady> awaitTrack(av::YouTube& queue) {
  auto deadline=Clock::now()+std::chrono::seconds(4);
  while(Clock::now()<deadline) {if(auto ready=queue.takeReady()) return ready;std::this_thread::sleep_for(std::chrono::milliseconds(20));}
  return {};
}
}
int main(int argc,char** argv) {
  if(argc!=2) return 2;
  auto root=std::filesystem::temp_directory_path()/("audioimage-youtube-tests-"+std::to_string(Clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root/"tools");
  std::filesystem::copy_file(argv[1],root/"tools/yt-dlp");
  std::filesystem::permissions(root/"tools/yt-dlp",std::filesystem::perms::owner_all);
  try {
    require(av::youtubePlaylistURL("https://www.youtube.com/watch?v=8HCNdlYlUCI&list=PLfIEImhbuF0o")=="https://www.youtube.com/playlist?list=PLfIEImhbuF0o","watch links retain their playlist");
    require(!av::youtubePlaylistURL("https://music.youtube.com/playlist?list=PL_TEST").empty(),"music links work");
    require(av::youtubePlaylistURL("https://youtube.com.attacker.example/playlist?list=PL_TEST").empty(),"unrelated hosts rejected");
    require(av::youtubePlaylistURL("file:///tmp/video?list=PL_TEST").empty(),"non-network input rejected");
    require(av::youtubePlaylistURL("https://youtube.com/playlist?list=PL_TEST;touch bad").empty(),"invalid playlist identifier rejected");
    auto parsed=av::parseYouTubeList(R"({"title":"List","entries":[null,{"id":"Good_abc123","title":"Good"},{"id":"Priv_abc123","title":"[Private video]"},{"id":"../escape","title":"Invalid"}]})");
    require(parsed.tracks.size()==1,"private and malformed entries are excluded");
    av::YouTube queue(root/"tools",root/"audio%cache");
    require(queue.open("https://youtube.com/playlist?list=PL_TEST"),"playlist accepted");
    auto first=awaitTrack(queue);require(first&&first->index==0,"first track prepared");
    require(std::filesystem::exists(first->path)&&queue.state().count==3,"audio cached and playlist count correct");
    require(queue.next(),"next accepted");auto next=awaitTrack(queue);
    require(next&&next->index==2,"unavailable next track skipped");
    require(queue.next(-1),"previous accepted");auto previous=awaitTrack(queue);
    require(previous&&previous->index==0,"previous skips unavailable entries in reverse order");
    queue.failed("Cached file could not be decoded.");auto recovered=awaitTrack(queue);
    require(recovered&&recovered->index==2&&!std::filesystem::exists(first->path),"bad cached audio is removed and skipped");
    require(queue.open("https://youtube.com/playlist?list=SLOW"),"slow request starts");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));auto began=Clock::now();queue.stop();
    require(Clock::now()-began<std::chrono::milliseconds(100),"cancellation does not block graphics");
    require(!queue.state().active&&!queue.takeReady(),"cancellation clears pending playback");
    require(queue.open("https://youtube.com/playlist?list=PL_TEST"),"replacement accepted");
    require(awaitTrack(queue).has_value(),"replacement survives cancellation of old helper");
    require(!queue.open("https://example.com/playlist?list=PL_TEST")&&queue.state().active,"invalid link preserves current playlist");
    require(queue.open("https://youtube.com/playlist?list=ERROR"),"unavailable playlist request starts");
    auto deadline=Clock::now()+std::chrono::seconds(3);
    while(queue.state().busy&&Clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    require(!queue.state().error.empty()&&!queue.takeReady(),"unavailable playlist reports an error");
    queue.stop();
    std::cout<<"PASS: playlist links, metadata, queue order, unavailable entries, caching, cancellation and replacement.\n";
  } catch(const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<"\n";std::filesystem::remove_all(root);return 1;}
  std::filesystem::remove_all(root);return 0;
}
