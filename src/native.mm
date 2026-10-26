#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <string>
#include "youtube.hpp"
namespace av {
std::string choosePlaylist(const std::string& current,bool spotify) {
  @autoreleasepool {
    NSWindow* host=[NSApp keyWindow];NSAlert* alert=[[NSAlert alloc] init];
    [alert setMessageText:spotify?@"Spotify playlist":@"YouTube playlist"];
    [alert setInformativeText:spotify?@"Imports Spotify's public track listing and plays matching YouTube recordings. Versions may differ, and large playlists may be partial.":@"Paste a public or unlisted playlist link."];
    [alert addButtonWithTitle:@"Play"];[alert addButtonWithTitle:@"Cancel"];
    NSTextField* field=[[NSTextField alloc] initWithFrame:NSMakeRect(0,0,440,28)];
    [field setPlaceholderString:spotify?@"https://open.spotify.com/playlist/...":@"https://www.youtube.com/playlist?list=..."];
    std::string initial=spotify==!spotifyPlaylistURL(current).empty()?current:std::string{};
    if(initial.empty()) {
      NSString* copied=[[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
      if(copied) {
        auto candidate=playlistURL(std::string([copied UTF8String]));
        if(spotify==!spotifyPlaylistURL(candidate).empty()) initial=candidate;
      }
    }
    [field setStringValue:[NSString stringWithUTF8String:initial.c_str()]];
    [alert setAccessoryView:field];[[alert window] setInitialFirstResponder:field];
    auto response=[alert runModal];std::string url;
    if(response==NSAlertFirstButtonReturn) url=std::string([[field stringValue] UTF8String]);
    [host makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];
    [field release];[alert release];return url;
  }
}
std::string chooseAudioFile() {
  @autoreleasepool {
    NSWindow* host=[NSApp keyWindow];
    NSOpenPanel* panel=[NSOpenPanel openPanel];
    [panel setTitle:@"Choose audio for AudioImage"];
    [panel setCanChooseDirectories:NO];[panel setAllowsMultipleSelection:NO];
    NSMutableArray<UTType*>* types=[NSMutableArray array];
    for(NSString* extension in @[@"wav",@"mp3",@"flac",@"ogg",@"m4a",@"aac",@"aif",@"aiff",@"caf"]) {
      UTType* type=[UTType typeWithFilenameExtension:extension];if(type) [types addObject:type];
    }
    [panel setAllowedContentTypes:types];
    auto response=[panel runModal];std::string path;
    if(response==NSModalResponseOK) path=std::string([[[panel URL] path] UTF8String]);
    [host makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];
    return path;
  }
  return {};
}
}
