#include "youtube.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
namespace {
using Clock=std::chrono::steady_clock;
void require(bool value,const char* error) {if(!value) throw std::runtime_error(error);}
std::optional<av::YouTubeReady> awaitTrack(av::YouTube& queue) {
  auto deadline=Clock::now()+std::chrono::seconds(4);
  while(Clock::now()<deadline) {if(auto ready=queue.takeReady()) return ready;std::this_thread::sleep_for(std::chrono::milliseconds(10));}
  return {};
}
std::string page(const std::string& tracks) {
  return "<html><script type='application/json' id='__NEXT_DATA__'>{\"props\":{\"pageProps\":{\"state\":{\"data\":{\"entity\":{\"type\":\"playlist\",\"title\":\"Public test playlist\",\"trackList\":["+tracks+"]}}}}}}</script></html>";
}
const std::string first=R"JSON({"uri":"spotify:track:1111111111111111111111","title":"First","subtitle":"Test Artist","duration":60000})JSON";
const std::string missing=R"JSON({"uri":"spotify:track:2222222222222222222222","title":"Missing","subtitle":"Test Artist","duration":60000})JSON";
const std::string second=R"JSON({"uri":"spotify:track:3333333333333333333333","title":"Second","subtitle":"Test Artist","duration":60000})JSON";
}
int main(int argc,char** argv) {
  if(argc!=2) return 2;
  auto root=std::filesystem::temp_directory_path()/("audioimage-spotify-tests-"+std::to_string(Clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root/"tools");std::filesystem::copy_file(argv[1],root/"tools/yt-dlp");
  std::filesystem::permissions(root/"tools/yt-dlp",std::filesystem::perms::owner_all);
  try {
    const std::string canonical="https://open.spotify.com/playlist/3cEYpjA9oz9GiPac4AsH4n";
    for(auto value:{canonical+"?si=abc",std::string(" spotify:playlist:3cEYpjA9oz9GiPac4AsH4n\n"),std::string("https://open.spotify.com/intl-de/playlist/3cEYpjA9oz9GiPac4AsH4n"),std::string("https://open.spotify.com/embed/playlist/3cEYpjA9oz9GiPac4AsH4n?utm_source=test")})
      require(av::spotifyPlaylistURL(value)==canonical,"Spotify share, URI, locale and embed links normalise");
    for(auto value:{"https://open.spotify.com.attacker.example/playlist/3cEYpjA9oz9GiPac4AsH4n","https://open.spotify.com@evil.example/playlist/3cEYpjA9oz9GiPac4AsH4n","https://open.spotify.com/playlist/../../bad","file:///playlist/3cEYpjA9oz9GiPac4AsH4n","https://open.spotify.com/track/3cEYpjA9oz9GiPac4AsH4n","spotify:playlist:3cEYpjA9oz9GiPac4AsH4n;touch nope"})
      require(av::spotifyPlaylistURL(value).empty(),"unrelated hosts, tracks and malformed identifiers rejected");
    require(av::playlistURL(canonical)==canonical&&!av::playlistURL("https://youtube.com/playlist?list=PL_TEST").empty(),"both providers share the playlist entry point");
    auto parsed=av::parseSpotifyList(page(first+","+missing+","+second+",null,{\"uri\":\"spotify:episode:1111111111111111111111\"}"));
    require(parsed.tracks.size()==3&&parsed.tracks[0].spotify&&parsed.tracks[0].artist=="Test Artist"&&parsed.tracks[0].duration==60,"track order, artist and millisecond duration preserved; episodes omitted");
    auto duplicates=av::parseSpotifyList(page(first+","+first));require(duplicates.tracks.size()==2,"intentional playlist duplicates preserved");
    std::string large=first;for(int i=1;i<100;i++) large+=","+first;
    require(av::parseSpotifyList(page(large)).partial,"a 100-entry public listing is flagged as possibly partial");
    bool refused=false;try {av::parseSpotifyList("<html>Sign in</html>");} catch(...) {refused=true;}
    require(refused,"private or changed Spotify pages fail clearly");
    refused=false;try {av::parseSpotifyList(page("null,{\"uri\":\"spotify:local:private\"}"));} catch(...) {refused=true;}
    require(refused,"empty and local-only playlists are rejected");
    auto song=parsed.tracks[0];
    require(av::matchSpotifyTrack(song,R"JSON({"entries":[{"id":"Cover_abc12","title":"First (Cover)","channel":"Test Artist","duration":60},{"id":"Long__abc12","title":"First","channel":"Test Artist","duration":120},{"id":"Wrong_abc12","title":"First","channel":"Other Artist","duration":60},{"id":"Good_abc123","title":"First","channel":"Test Artist - Topic","duration":60}]})JSON")=="Good_abc123","matching rejects covers, wrong artists and wrong durations");
    song.title="First Live";
    require(av::matchSpotifyTrack(song,R"JSON({"entries":[{"id":"Good_abc123","title":"First","channel":"Test Artist - Topic","duration":60}]})JSON").empty(),"requested live versions cannot fall back to studio recordings");
    song.title="Caf\xc3\xa9";song.artist="Bj\xc3\xb6rk";
    require(av::matchSpotifyTrack(song,R"JSON({"entries":[{"id":"Good_abc123","title":"Cafe","channel":"Bjork - Topic","duration":60}]})JSON")=="Good_abc123","matching handles diacritics");
    av::YouTube queue(root/"tools",root/"audio%cache",[&](const std::string& url,const std::function<bool()>& active) {
      if(url.find("4444444444444444444444")!=std::string::npos) {
        while(active()) std::this_thread::sleep_for(std::chrono::milliseconds(10));return std::string{};
      }
      return page(first+","+missing+","+second);
    });
    require(queue.open(canonical),"public Spotify playlist accepted");auto ready=awaitTrack(queue);
    require(ready&&ready->index==0&&ready->title=="Test Artist / First"&&ready->audioURL=="https://www.youtube.com/watch?v=Good_abc123","first matched recording preserves Spotify artist/title and exposes its real source");
    require(queue.state().spotify&&queue.state().count==3&&queue.state().status.find("YouTube audio")!=std::string::npos,"Spotify import and YouTube playback are labelled distinctly");
    require(ready->path.filename()=="spotify-1111111111111111111111.m4a"&&std::filesystem::exists(ready->path),"matched audio has a separate stable cache key");
    auto firstPath=ready->path;
    require(queue.next(),"next accepted");ready=awaitTrack(queue);require(ready&&ready->index==2,"unmatched songs are skipped in playlist order");
    require(queue.next(),"wrap accepted");ready=awaitTrack(queue);require(ready&&ready->index==0,"playlist wraps to its first matched track");
    require(queue.next(-1),"previous accepted");ready=awaitTrack(queue);require(ready&&ready->index==2,"previous navigation preserves Spotify order");
    require(!queue.open("https://example.com/playlist/bad")&&queue.state().active&&queue.state().error.empty(),"invalid replacements preserve the current playable queue");
    require(queue.open("https://youtube.com/playlist?list=PL_TEST"),"switch to YouTube accepted");ready=awaitTrack(queue);
    require(ready&&!queue.state().spotify&&ready->path.filename()=="Good_abc123.m4a","provider switching keeps audio cache identities separate");
    require(queue.open(canonical),"Spotify replacement accepted");ready=awaitTrack(queue);
    require(ready&&ready->path==firstPath&&ready->audioURL.find("Good_abc123")!=std::string::npos,"cached Spotify audio retains its matched source");
    queue.failed("Bad cached audio.");ready=awaitTrack(queue);require(ready&&ready->index==2&&!std::filesystem::exists(firstPath),"corrupt matched audio is removed and skipped");
    require(queue.open("spotify:playlist:4444444444444444444444"),"slow Spotify fetch starts");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));auto began=Clock::now();queue.stop();
    require(Clock::now()-began<std::chrono::milliseconds(100)&&!queue.takeReady(),"stopping Spotify import does not block graphics or release stale audio");
    require(queue.open(canonical)&&awaitTrack(queue).has_value(),"replacement survives cancellation of Spotify fetch");queue.stop();
    std::cout<<"PASS: public Spotify links, metadata, ordering, partial lists, conservative matching, cache source attribution, provider switching and cancellation.\n";
  } catch(const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<"\n";std::filesystem::remove_all(root);return 1;}
  std::filesystem::remove_all(root);return 0;
}
