#import <Foundation/Foundation.h>
#include "youtube.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <memory>
#include <set>
#include <stdexcept>

namespace av {
namespace {
bool spotifyID(const std::string& id) {
  return id.size()==22&&id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789")==std::string::npos;
}
NSDictionary* object(id value) {return [value isKindOfClass:[NSDictionary class]]?value:nil;}
std::string text(id value) {return [value isKindOfClass:[NSString class]]?std::string([value UTF8String]):std::string{};}
std::string clean(id value) {
  auto s=text(value);if(s.size()>1024) return {};
  for(auto& c:s) if(static_cast<unsigned char>(c)<32) c=' ';
  return s;
}
NSDictionary* jsonObject(const std::string& json) {
  NSData* data=[NSData dataWithBytes:json.data() length:json.size()];
  return object([NSJSONSerialization JSONObjectWithData:data options:0 error:nullptr]);
}
std::set<std::string> words(const std::string& source) {
  NSString* s=[[NSString alloc] initWithBytes:source.data() length:source.size() encoding:NSUTF8StringEncoding];
  s=[[s stringByFoldingWithOptions:NSDiacriticInsensitiveSearch locale:[NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"]] lowercaseString];
  NSArray* parts=[s componentsSeparatedByCharactersInSet:[[NSCharacterSet alphanumericCharacterSet] invertedSet]];
  std::set<std::string> result;
  for(NSString* part in parts) if(part.length) result.insert(text(part));
  return result;
}
double coverage(const std::set<std::string>& wanted,const std::set<std::string>& actual) {
  if(wanted.empty()) return 0;
  int count=0;for(const auto& word:wanted) count+=actual.count(word)>0;
  return double(count)/wanted.size();
}
}
std::string spotifyPlaylistURL(std::string value) {
  auto first=value.find_first_not_of(" \t\r\n"),last=value.find_last_not_of(" \t\r\n");
  if(first==std::string::npos||value.size()>4096) return {};
  value=value.substr(first,last-first+1);std::string id;
  if(value.rfind("spotify:playlist:",0)==0) id=value.substr(17);
  else {
    if(value.rfind("open.spotify.com/",0)==0) value="https://"+value;
    auto scheme=value.find("://");if(scheme==std::string::npos) return {};
    auto protocol=value.substr(0,scheme);if(protocol!="https"&&protocol!="http") return {};
    auto end=value.find_first_of("/?#",scheme+3);if(end==std::string::npos) return {};
    auto host=value.substr(scheme+3,end-scheme-3);
    std::transform(host.begin(),host.end(),host.begin(),[](unsigned char c){return char(std::tolower(c));});
    if(host!="open.spotify.com") return {};
    auto tail=value.substr(end,value.find_first_of("?#",end)-end);
    if(tail.rfind("/embed/",0)==0) tail.erase(0,6);
    else if(tail.rfind("/intl-",0)==0) {
      auto slash=tail.find('/',1);if(slash==std::string::npos) return {};tail.erase(0,slash);
    }
    if(tail.rfind("/playlist/",0)!=0) return {};
    id=tail.substr(10);if(!id.empty()&&id.back()=='/') id.pop_back();
  }
  return spotifyID(id)?"https://open.spotify.com/playlist/"+id:std::string{};
}
std::string playlistURL(const std::string& text) {
  auto youtube=youtubePlaylistURL(text);return youtube.empty()?spotifyPlaylistURL(text):youtube;
}
YouTubeList parseSpotifyList(const std::string& html) {
  @autoreleasepool {
    if(html.size()>8*1024*1024) throw std::runtime_error("Spotify returned too much playlist data.");
    NSString* page=[[NSString alloc] initWithBytes:html.data() length:html.size() encoding:NSUTF8StringEncoding];
    auto pattern=@"<script\\b(?=[^>]*\\bid\\s*=\\s*[\"']__NEXT_DATA__[\"'])[^>]*>([\\s\\S]*?)</script\\s*>";
    NSRegularExpression* expression=[NSRegularExpression regularExpressionWithPattern:pattern options:NSRegularExpressionCaseInsensitive error:nullptr];
    NSTextCheckingResult* match=page?[expression firstMatchInString:page options:0 range:NSMakeRange(0,page.length)]:nil;
    if(!match) throw std::runtime_error("Spotify did not expose a public track list. Check the playlist is public and try again.");
    auto root=jsonObject(text([page substringWithRange:[match rangeAtIndex:1]]));
    NSDictionary* props=object(root[@"props"]),*pageProps=object(props[@"pageProps"]),*state=object(pageProps[@"state"]);
    NSDictionary* data=object(state[@"data"]),*entity=object(data[@"entity"]);
    if(text(entity[@"type"])!="playlist") throw std::runtime_error("That Spotify link did not return a public playlist.");
    id entries=entity[@"trackList"];
    if(![entries isKindOfClass:[NSArray class]]) throw std::runtime_error("Spotify's public playlist data was unreadable.");
    YouTubeList result;result.title=clean(entity[@"title"]);if(result.title.empty()) result.title="Spotify playlist";
    // Public embeds can expose only the first 100 entries. Never describe the
    // returned portion as the complete playlist or silently invent pagination.
    result.partial=[entries count]>=100;
    for(id item in entries) {
      auto entry=object(item);auto uri=text(entry[@"uri"]),title=clean(entry[@"title"]),artist=clean(entry[@"subtitle"]);
      if(uri.rfind("spotify:track:",0)!=0||!spotifyID(uri.substr(14))||title.empty()||artist.empty()) continue;
      id length=entry[@"duration"];double duration=[length isKindOfClass:[NSNumber class]]?[length doubleValue]/1000.0:0;
      if(!std::isfinite(duration)||duration<=0||duration>86400) continue;
      YouTubeTrack track;track.id=uri.substr(14);track.title=title;track.artist=artist;track.duration=duration;track.spotify=true;
      result.tracks.push_back(std::move(track));
    }
    if(result.tracks.empty()) throw std::runtime_error("No public music tracks were exposed by that Spotify playlist. Private playlists, local files and episodes cannot be imported.");
    return result;
  }
}
std::string matchSpotifyTrack(const YouTubeTrack& track,const std::string& json) {
  @autoreleasepool {
    auto root=jsonObject(json);id entries=root[@"entries"];
    if(![entries isKindOfClass:[NSArray class]]||track.duration<=0) return {};
    auto titleWords=words(track.title),artistWords=words(track.artist.substr(0,track.artist.find(',')));
    double best=-1;std::string selected;
    for(id item in entries) {
      auto entry=object(item);auto videoId=text(entry[@"id"]),title=clean(entry[@"title"]);
      auto availability=text(entry[@"availability"]),live=text(entry[@"live_status"]);
      if(videoId.size()!=11||videoId.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")!=std::string::npos
          ||live=="is_live"||live=="is_upcoming"||availability=="private"||availability=="needs_auth"||availability=="premium_only"||availability=="subscriber_only") continue;
      id length=entry[@"duration"];double duration=[length isKindOfClass:[NSNumber class]]?[length doubleValue]:0;
      double tolerance=std::min(35.0,std::max(12.0,track.duration*.08)),delta=std::abs(duration-track.duration);
      if(!std::isfinite(duration)||duration<=0||delta>tolerance) continue;
      auto candidateTitle=words(title);
      auto candidateArtist=words(title+" "+text(entry[@"channel"])+" "+text(entry[@"uploader"])+" "+text(entry[@"artist"]));
      bool wrongVersion=false;
      for(const char* version:{"remix","live","cover","karaoke","instrumental","slowed","nightcore","sped"})
        if(bool(candidateTitle.count(version))!=bool(titleWords.count(version))) wrongVersion=true;
      for(const char* distraction:{"reaction","tutorial","review"})
        if(candidateTitle.count(distraction)&&!titleWords.count(distraction)) wrongVersion=true;
      if(wrongVersion) continue;
      double song=coverage(titleWords,candidateTitle),artist=coverage(artistWords,candidateArtist);
      if(song<(titleWords.size()>2?.75:1.0)||artist<.67) continue;
      double score=song*60+artist*30+(1-delta/tolerance)*10;
      auto channel=text(entry[@"channel"]);
      if(channel.find(" - Topic")!=std::string::npos||channel.find("VEVO")!=std::string::npos) score+=4;
      if(score>best) {best=score;selected=videoId;}
    }
    return selected;
  }
}
std::string readSpotifyPlaylist(const std::string& url,const std::function<bool()>& active) {
  @autoreleasepool {
    auto canonical=spotifyPlaylistURL(url);
    if(canonical.empty()) throw std::runtime_error("Paste a public open.spotify.com playlist link.");
    if(!active()) return {};
    // Fetch public HTML only. The anonymous session data and preview-audio URLs
    // embedded by Spotify are never used, logged or sent to another endpoint.
    auto endpoint="https://open.spotify.com/embed/playlist/"+canonical.substr(canonical.rfind('/')+1);
    NSURLSessionConfiguration* configuration=[NSURLSessionConfiguration ephemeralSessionConfiguration];
    configuration.timeoutIntervalForRequest=20;configuration.timeoutIntervalForResource=30;
    configuration.requestCachePolicy=NSURLRequestReloadIgnoringLocalCacheData;
    configuration.HTTPAdditionalHeaders=@{@"Accept":@"text/html",@"User-Agent":@"AudioImage/1.21.0"};
    NSURLSession* session=[NSURLSession sessionWithConfiguration:configuration];
    struct Response {std::mutex mutex;std::condition_variable wake;bool done=false;std::string html,error;};
    auto response=std::make_shared<Response>();
    NSURL* address=[NSURL URLWithString:[NSString stringWithUTF8String:endpoint.c_str()]];
    NSURLSessionDataTask* task=[session dataTaskWithURL:address completionHandler:^(NSData* body,NSURLResponse* reply,NSError* error) {
      std::lock_guard<std::mutex> lock(response->mutex);
      auto code=[reply isKindOfClass:[NSHTTPURLResponse class]]?[(NSHTTPURLResponse*)reply statusCode]:0;
      if(error) response->error="Could not reach Spotify. Check your connection and try again.";
      else if(code!=200||![[reply.URL.host lowercaseString] isEqualToString:@"open.spotify.com"]||![reply.URL.scheme isEqualToString:@"https"])
        response->error="Spotify could not provide that public playlist. Check the link and its visibility.";
      else if(body.length>8*1024*1024) response->error="Spotify returned too much playlist data.";
      else if(body.length) response->html.assign(static_cast<const char*>(body.bytes),body.length);
      else response->error="Spotify returned an empty playlist page.";
      response->done=true;response->wake.notify_one();
    }];
    [task resume];std::unique_lock<std::mutex> lock(response->mutex);
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(32);
    while(!response->done&&active()&&std::chrono::steady_clock::now()<deadline) response->wake.wait_for(lock,std::chrono::milliseconds(50));
    if(!response->done) {
      lock.unlock();[task cancel];[session invalidateAndCancel];
      if(!active()) return {};
      throw std::runtime_error("Spotify timed out. Check your connection and try again.");
    }
    auto html=std::move(response->html),error=std::move(response->error);lock.unlock();[session finishTasksAndInvalidate];
    if(!error.empty()) throw std::runtime_error(error);
    return html;
  }
}
}
