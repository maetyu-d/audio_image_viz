#import <Foundation/Foundation.h>
#include "youtube.hpp"
#include <stdexcept>

namespace av {
YouTubeList parseYouTubeList(const std::string& json) {
  @autoreleasepool {
    NSData* data=[NSData dataWithBytes:json.data() length:json.size()];NSError* error=nil;
    id root=[NSJSONSerialization JSONObjectWithData:data options:0 error:&error];
    if(error||![root isKindOfClass:[NSDictionary class]]) throw std::runtime_error("YouTube returned an unreadable playlist.");
    auto text=[](id value)->std::string {return [value isKindOfClass:[NSString class]]?std::string([value UTF8String]):std::string{};};
    YouTubeList result;result.title=text(root[@"title"]);if(result.title.empty()) result.title="YouTube playlist";
    id entries=root[@"entries"];
    if(![entries isKindOfClass:[NSArray class]]) throw std::runtime_error("That link did not return a playlist.");
    for(id entry in entries) {
      if(![entry isKindOfClass:[NSDictionary class]]) continue;
      auto videoId=text(entry[@"id"]),title=text(entry[@"title"]),availability=text(entry[@"availability"]);
      if(videoId.size()!=11||videoId.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")!=std::string::npos) continue;
      if(title=="[Private video]"||title=="[Deleted video]"||availability=="private"||availability=="needs_auth"
         ||availability=="premium_only"||availability=="subscriber_only"||text(entry[@"live_status"])=="is_live") continue;
      id length=entry[@"duration"];double duration=[length isKindOfClass:[NSNumber class]]?[length doubleValue]:0;
      YouTubeTrack track;track.id=videoId;track.title=title.empty()?videoId:title;track.duration=duration;
      result.tracks.push_back(std::move(track));
    }
    if(result.tracks.empty()) throw std::runtime_error("No public, playable tracks were found in that playlist.");
    return result;
  }
}
}
