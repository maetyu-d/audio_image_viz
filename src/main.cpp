#include "audio.hpp"
#include "video.hpp"
#include "youtube.hpp"
#include "rlgl.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
using namespace av;
#ifdef __APPLE__
namespace av {std::string chooseAudioFile();std::string choosePlaylist(const std::string& current,bool spotify);}
#endif
namespace {
constexpr Color paper{230,238,233,255},dim{143,164,161,255},lime{197,255,83,255},stroke{55,74,73,255};
void alignFullscreen() {
  if(IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE)) {
    auto position=GetMonitorPosition(GetCurrentMonitor());SetWindowPosition(int(position.x),int(position.y));
  }
}
void toggleFullscreen() {ToggleBorderlessWindowed();ClearWindowState(FLAG_WINDOW_TOPMOST);alignFullscreen();}
struct Console {
  static constexpr float width=336,height=532;
  bool visible=false,paused=false,muted=false,details=false;
  bool snapshotRequested=false,fullscreenRequested=false,clearRequested=false,openFileRequested=false;
  bool synthRequested=false,liveRequested=false,youtubeRequested=false,spotifyRequested=false;
  int trackStep=0;
  float volume=.72f,dragX=0;int selected=0,drag=-1;Font font{};bool ownFont=false;
  std::string notice,hint;double noticeUntil=0;
  float scale() const {return std::min({1.f,(GetScreenHeight()-24.f)/height,(GetScreenWidth()-24.f)/width});}
  Vector2 origin() const {float s=scale();return {GetScreenWidth()-width*s-12,12};}
  Vector2 mouse() const {auto p=GetMousePosition(),o=origin();float s=scale();return {(p.x-o.x)/s,(p.y-o.y)/s};}
  bool shift() const {return IsKeyDown(KEY_LEFT_SHIFT)||IsKeyDown(KEY_RIGHT_SHIFT);}
  void init() {auto path=std::filesystem::path(GetApplicationDirectory())/
#ifdef __APPLE__
      "../Resources/assets/fonts/Cousine-Regular.ttf";
#else
      "assets/fonts/Cousine-Regular.ttf";
#endif
    font=LoadFontEx(path.string().c_str(),36,nullptr,0);ownFont=font.texture.id!=0;
    if(!ownFont) font=GetFontDefault();else {GenTextureMipmaps(&font.texture);SetTextureFilter(font.texture,TEXTURE_FILTER_TRILINEAR);}}
  void text(const std::string& s,float x,float y,float size=11,Color c=paper) {DrawTextEx(font,s.c_str(),{x,y},size,0,c);}
  void rightText(const std::string& s,float right,float y,float size=11,Color c=paper) {text(s,right-MeasureTextEx(font,s.c_str(),size,0).x,y,size,c);}
  void clipped(const std::string& s,float x,float y,float size,float space,Color c=paper) {
    std::string shortName=s;
    while(!shortName.empty()&&MeasureTextEx(font,shortName.c_str(),size,0).x>space) shortName.pop_back();
    if(shortName!=s&&shortName.size()>3) shortName.replace(shortName.size()-3,3,"...");text(shortName,x,y,size,c);
  }
  void wrapped(std::string s,float x,float y,float size,float space,int lines=2,Color c=dim) {
    for(int line=0;line<lines&&!s.empty();line++) {
      size_t count=s.size();
      while(count>0&&MeasureTextEx(font,s.substr(0,count).c_str(),size,0).x>space) count--;
      if(count<s.size()) {auto word=s.rfind(' ',count);if(word!=std::string::npos&&word>0) count=word;}
      if(line==lines-1) {clipped(s,x,y,size,space,c);break;}
      text(s.substr(0,count),x,y,size,c);s.erase(0,count);while(!s.empty()&&s.front()==' ') s.erase(0,1);y+=13;
    }
  }
  bool hover(Rectangle r) const {return visible&&CheckCollisionPointRec(mouse(),r);}
  bool hit(Rectangle r) const {return hover(r)&&IsMouseButtonPressed(MOUSE_BUTTON_LEFT);}
  bool button(Rectangle r,const char* label,bool active=false,const char* help=nullptr) {
    bool over=hover(r);
    DrawRectangleRec(r,active?Color{32,45,31,255}:over?Color{33,42,43,255}:Color{19,27,30,255});
    if(active) DrawRectangle(int(r.x),int(r.y+r.height-2),int(r.width),2,lime);
    float size=10.5f;auto extent=MeasureTextEx(font,label,size,0);
    text(label,r.x+(r.width-extent.x)*.5f,r.y+(r.height-size)*.5f,size,active?lime:paper);
    if(over&&help) hint=help;
    return hit(r);
  }
  void adjust(Rectangle row,float x,float length,float& v,int id,float fallback) {
    auto m=mouse();
    if(hover(row)) {
      if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if(id<8) selected=id;
        if(m.x>=x-6) {drag=id;dragX=m.x;}
      }
      v=std::clamp(v+GetMouseWheelMove()*(shift()?.005f:.025f),0.f,1.f);
      if(IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {v=fallback;if(id<8) selected=id;}
      hint=shift()?"Fine tuning / drag or scroll":"Drag / scroll to tune / Shift for fine control";
    }
    if(drag==id&&IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
      v=std::clamp(shift()?v+(m.x-dragX)/length*.15f:(m.x-x)/length,0.f,1.f);dragX=m.x;
    }
  }
  void track(float x,float y,float length,float v,bool active=true) {
    DrawRectangle(int(x),int(y),int(length),2,stroke);
    DrawRectangle(int(x),int(y),int(length*v),2,active?lime:dim);
    DrawCircle(int(x+length*v),int(y+1),active?3:2,active?lime:dim);
  }
  void scene(Patch& p,int next) {p.previousScene=p.scene;p.scene=(next+sceneCount)%sceneCount;p.transition=0;}
  void draw(Audio& a,Video& video,Patch& patch,const Signal& signal,uint32_t& seed,const YouTubeState& youtube,bool youtubePlaying) {
    if(!visible) return;
    if(noticeUntil>0&&GetTime()>noticeUntil) {notice.clear();noticeUntil=0;}
    hint.clear();Vector2 o=origin();float s=scale();
    rlPushMatrix();rlTranslatef(o.x,o.y,0);rlScalef(s,s,1);
    DrawRectangle(4,5,int(width),int(height),{0,0,0,75});
    DrawRectangle(0,0,int(width),int(height),{10,16,20,250});DrawRectangleLinesEx({0,0,width,height},1,stroke);
    text("AudioImage",16,16,14);
    if(button({276,9,44,26},"HIDE",false,"C / hide controls")) {visible=false;drag=-1;}
    text(scenes[patch.scene],16,51,12,lime);
    for(int i=0;i<sceneCount;i++) {
      if(button({16.f+i*38,76,34,26},TextFormat("%d",i+1),i==patch.scene,scenes[i])) {
        if(i!=patch.scene) scene(patch,i);patch.automatic=false;
      }
    }
    if(button({248,76,72,26},"AUTO",patch.automatic,"A / morph modes every 48 seconds")) patch.automatic=!patch.automatic;
    if(button({16,116,52,28},"SYNTH",a.source==Source::Synth&&!youtube.active,"D / internal soundtrack")) synthRequested=true;
    if(button({72,116,43,28},"LIVE",a.source==Source::Input&&!youtube.active,"I / default audio input")) liveRequested=true;
    if(button({119,116,43,28},"FILE",a.source==Source::File&&!youtube.active,"Open or drop WAV / MP3 / FLAC / OGG / M4A / AIFF")) {
#ifdef __APPLE__
      openFileRequested=true;
#else
      notice="Drop WAV / MP3 / FLAC / OGG onto the image.";
#endif
    }
    if(button({166,116,73,28},"YOUTUBE",youtube.active&&!youtube.spotify,"Y / paste a YouTube playlist link")) youtubeRequested=true;
    if(button({243,116,77,28},"SPOTIFY",youtube.active&&youtube.spotify,"P / public Spotify playlist, matched YouTube audio")) spotifyRequested=true;
    if(a.source==Source::File&&a.duration>0) {
      clipped(a.name,16,153,10,youtubePlaying?170:205);
      auto clock=TextFormat("%d:%02d / %d:%02d",int(a.position)/60,int(a.position)%60,int(a.duration)/60,int(a.duration)%60);
      rightText(youtubePlaying?(youtube.busy?"PREPARING...":std::to_string(youtube.index+1)+"/"+std::to_string(youtube.count)+"  "+clock):clock,320,153,youtubePlaying?8:9,dim);
      DrawRectangle(16,169,304,2,stroke);DrawRectangle(16,169,int(304*std::clamp(a.position/a.duration,0.f,1.f)),2,dim);
    } else text(a.source==Source::Synth?"Internal 128 BPM synth":a.source==Source::Input?"Default input / no monitoring":"Ambient synthesis",16,153,10,dim);
    if(youtubePlaying) {
      if(button({16,182,28,26},"<",false,", / previous playlist track")) trackStep=-1;
      if(button({49,182,56,26},paused?"PLAY":"PAUSE",paused,"Space / pause picture and playback")) paused=!paused;
      if(button({110,182,28,26},">",false,". / next playlist track")) trackStep=1;
      if(button({143,182,49,26},muted?"UNMUTE":"MUTE",muted,"M / toggle playback mute")) muted=!muted;
      adjust({199,182,121,26},205,109,volume,8,.72f);track(205,194,109,volume,false);
    } else {
      if(button({16,182,65,26},paused?"RESUME":"PAUSE",paused,"Space / pause picture and playback")) paused=!paused;
      if(button({86,182,57,26},muted?"UNMUTE":"MUTE",muted,"M / toggle playback mute")) muted=!muted;
      text("VOL",153,190,9,dim);
      adjust({178,182,142,26},184,130,volume,8,.72f);track(184,194,130,volume,false);
    }
    DrawLine(16,220,320,220,stroke);
    if(!details) {
      constexpr const char* names[]={"FEEDBACK","SMEAR","WARP","SYMMETRY","COLOUR","DETAIL","REACTION","EXPOSURE"};
      for(int i=0;i<8;i++) {
        float y=232+i*27;Rectangle row{16,y-4,304,27};
        if(hover(row)) DrawRectangleRec(row,{22,31,33,255});
        adjust(row,118,157,patch.values[i],i,Patch{}.values[i]);
        text(names[i],16,y+2,11,i==selected?lime:dim);
        track(118,y+7,157,patch.values[i],i==selected||hover(row));
        rightText(TextFormat("%.0f",patch.values[i]*100),320,y+1,11);
      }
    } else {
      text("PULSE",16,233,9,dim);text("LEVEL",125,233,9,dim);text("WIDTH",239,233,9,dim);
      text(signal.tempoConfidence>.5f?TextFormat("%.0f BPM",signal.bpm):"-- BPM",16,250,15,signal.tempoConfidence>.5f?lime:dim);
      text(TextFormat("%.0f dBFS",20*std::log10(std::max(signal.rms,.000001f))),125,250,15);
      text(TextFormat("%.0f%%",signal.width*100),239,250,15);
      for(int i=0;i<76;i++) {
        float level=0,peak=0;for(int j=i*spectrumSize/76;j<(i+1)*spectrumSize/76;j++) {level=std::max(level,signal.spectrum[j]);peak=std::max(peak,signal.spectrumPeaks[j]);}
        Color c=i<25?lime:i<50?Color{49,231,219,255}:Color{255,103,200,255};
        DrawRectangle(16+i*4,315-int(level*30),2,std::max(1,int(level*30)),c);DrawRectangle(16+i*4,315-int(peak*30),2,1,paper);
      }
      for(int i=0;i<bandCount;i++) {int x=16+i*44;DrawRectangle(x,327,40,3,stroke);DrawRectangle(x,327,int(40*signal.bands[i]),3,i<2?lime:i<5?Color{49,231,219,255}:Color{255,103,200,255});}
      text("20 Hz",16,338,8,dim);rightText("20 kHz",320,338,8,dim);
      if(button({16,360,149,27},"FULLSCREEN / F")) fullscreenRequested=true;
      if(button({171,360,149,27},"SNAPSHOT / S",false,"Save a clean image without controls")) snapshotRequested=true;
      if(youtube.active) {
        clipped(youtube.title.empty()?(youtube.spotify?"Spotify public playlist":"YouTube playlist"):youtube.title,16,401,9,244,lime);
        if(hover({16,393,244,24})) hint="Open the original playlist";
        if(hit({16,393,244,24})) OpenURL(youtube.url.c_str());
        if(!youtube.audioURL.empty()&&button({266,393,54,24},"AUDIO",false,"Open the YouTube recording being played")) OpenURL(youtube.audioURL.c_str());
      } else clipped("SPACE pause / 1-6 modes / R mutate",16,401,9,304,dim);
      text(youtube.active?(youtube.spotify?"Spotify order / YouTube audio":", previous / . next / Y playlist"):"TAB select / [ ] tune / SHIFT fine",16,422,9,dim);
      wrapped(!notice.empty()?notice:youtube.active?(!youtube.error.empty()?youtube.error:youtube.status):a.status,16,436,9,304);
    }
    DrawLine(16,459,320,459,stroke);
    if(button({16,470,83,26},"MUTATE",false,"R / generate new phase fields")) {seed=audioimage::freshSeed();video.mutate(seed);}
    if(button({104,470,63,26},"CLEAR",false,"Backspace / clear feedback memory")) clearRequested=true;
    if(button({172,470,65,26},"RESET",false,"Reset all eight visual controls")) {patch.values=Patch{}.values;drag=-1;}
    if(button({242,470,78,26},details?"PATCH":"MORE",details,details?"Return to visual controls":"Meters, shortcuts, fullscreen and snapshot")) {details=!details;drag=-1;}
    std::string message=!hint.empty()?hint:!notice.empty()?notice:youtube.active?(!youtube.error.empty()?youtube.error:youtube.status):!a.status.empty()?a.status:"C hide / right-click a control to reset";
    clipped(message,16,512,9,304,dim);
    if(IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) drag=-1;
    rlPopMatrix();
  }
};
}
int main(int argc,char** argv) {
  bool windowed=false,hidden=false,silent=false,offline=false,selfCheck=false;
  int frames=-1,resolution=900,startScene=0;uint32_t seed=audioimage::freshSeed();
  std::string filePath,screenshot,playlistURL;bool controls=false;float seek=0;
  auto number=[](const char* str,int& result){const char* end=str+std::char_traits<char>::length(str);auto parsed=std::from_chars(str,end,result);return parsed.ec==std::errc{}&&parsed.ptr==end;};
  for(int i=1;i<argc;i++) {
    std::string arg=argv[i];
    auto integer=[&](int& value){return i+1<argc&&number(argv[++i],value);};
    if(arg=="--windowed") windowed=true;
    else if(arg=="--hidden") {hidden=true;windowed=true;}
    else if(arg=="--no-audio") silent=true;
    else if(arg=="--offline-audio") {silent=true;offline=true;}
    else if(arg=="--controls") controls=true;
    else if(arg=="--self-check") {selfCheck=true;windowed=true;offline=true;silent=true;frames=600;}
    else if(arg=="--frames") {if(!integer(frames)||frames<1) return 2;}
    else if(arg=="--scene") {if(!integer(startScene)||startScene<1||startScene>6) return 2;startScene--;}
    else if(arg=="--quality") {if(!integer(resolution)||resolution<300||resolution>2160) return 2;}
    else if(arg=="--seed") {int n;if(!integer(n)||n<1) return 2;seed=uint32_t(n);}
    else if(arg=="--seek") {int n;if(!integer(n)||n<0||n>180) return 2;seek=float(n);}
    else if(arg=="--screenshot"&&i+1<argc) screenshot=argv[++i];
    else if(arg=="--file"&&i+1<argc) filePath=argv[++i];
    else if((arg=="--youtube"||arg=="--spotify"||arg=="--playlist")&&i+1<argc) playlistURL=argv[++i];
    else if(arg=="--help") {std::cout<<"AudioImage / C++ audio and video feedback instrument\n"
      <<"C controls (hidden at startup) | Space pause | F fullscreen | Esc hide controls / quit\n"
      <<"1-6 modes | Left/Right modes | A auto | R mutate | Backspace clear\n"
      <<"I live input | D internal soundtrack | M mute | -/= volume | S snapshot\n"
      <<"Y YouTube playlist | P Spotify playlist (YouTube matches) | , previous / . next\n"
      <<"Drop WAV/MP3/FLAC/OGG/M4A/AIFF to play and visualise. Select an input/loopback\ndevice in macOS Sound for live input. No input monitoring.\n"
      <<"--windowed --file PATH --playlist URL (--youtube / --spotify also work)\n"
      <<"--scene 1..6 --seed N --quality 300..2160\n"
      <<"--frames N --screenshot PATH --controls --seek 0..180 --no-audio\n"
      <<"--offline-audio deterministic silent synth for rendering | --self-check\n";return 0;}
    else {std::cerr<<"Unknown or incomplete option: "<<arg<<"\n";return 2;}
  }
  if(selfCheck) frames=600;
  if(hidden&&frames<1) {std::cerr<<"--hidden requires --frames N or --self-check.\n";return 2;}
  SetTraceLogLevel(LOG_WARNING);
  SetConfigFlags(FLAG_WINDOW_RESIZABLE|FLAG_VSYNC_HINT|FLAG_WINDOW_HIGHDPI|(hidden?FLAG_WINDOW_HIDDEN:0));
  InitWindow(1280,800,"AudioImage / audio visualiser");
  if(!IsWindowReady()) return 1;
  SetWindowMinSize(720,480);SetExitKey(KEY_NULL);SetTargetFPS(hidden?0:60);
  if(!windowed) toggleFullscreen();
  Video video;if(!video.init(seed,resolution)) {std::cerr<<"Video shader or framebuffer initialization failed.\n";video.shutdown();CloseWindow();return 1;}
  Audio audio;audio.init(true,seed);
  bool youtubePlaying=false;YouTubeState youtubeState;
#ifdef __APPLE__
  YouTube youtube((std::filesystem::path(GetApplicationDirectory())/"../Helpers").lexically_normal(),std::filesystem::temp_directory_path()/"audioimage-youtube");
#endif
  auto stopYouTube=[&] {
#ifdef __APPLE__
    youtube.stop();
#endif
    youtubePlaying=false;youtubeState.active=false;
  };
  Analyzer analyzer;Patch patch;patch.scene=patch.previousScene=startScene;Console console;console.visible=controls;console.init();
  float autoClock=0,mutationClock=0;
  int frame=0;bool okay=true;std::array<double,sceneCount> sceneMeans{};std::array<double,sceneCount> sceneDifferences{};
  std::vector<Color> previousImage;
  auto changeScene=[&](int next){console.scene(patch,next);autoClock=0;};
  auto snapshot=[&](){auto folder=std::filesystem::path(GetApplicationDirectory());
    // raylib returns a trailing separator. Remove its empty final component
    // before walking out of Contents/MacOS to the directory beside the app.
    if(folder.filename().empty()) folder=folder.parent_path();
    // Keep captures alongside the distributable app rather than inside it.
#ifdef __APPLE__
    folder=folder.parent_path().parent_path().parent_path();
#endif
    folder/="AudioImage captures";std::error_code error;std::filesystem::create_directories(folder,error);
    auto path=folder/("audioimage-"+std::to_string(std::time(nullptr))+".png");
    if(!error) video.capture(path.string());
    console.notice=(!error&&std::filesystem::exists(path,error))?"SAVED / "+path.filename().string():"Could not save snapshot.";
    console.noticeUntil=GetTime()+5;};
  for(int n=0;n<int(seek*60);n++) {audio.offline(800);video.render(analyzer.update(audio.ring,1.f/60),patch,1.f/60);}
  if(!silent) audio.init(false,seed);
  audio.update(console.paused,console.muted,console.volume);
  if(!filePath.empty()&&!audio.load(filePath)) {
    std::cerr<<audio.status<<"\n";audio.shutdown();if(console.ownFont) UnloadFont(console.font);video.shutdown();CloseWindow();return 1;
  }
#ifdef __APPLE__
  if(!playlistURL.empty()) youtube.open(playlistURL);
#endif
  while(!WindowShouldClose()) {
    float dt=frames>0?1.f/60:std::clamp(GetFrameTime(),.001f,.05f);
    std::array<bool,512> edges{};for(int k=GetKeyPressed();k;k=GetKeyPressed()) if(k>0&&k<512) edges[k]=true;
    auto key=[&](int k){return edges[k]||IsKeyPressed(k);};
    if((IsKeyDown(KEY_LEFT_SUPER)||IsKeyDown(KEY_RIGHT_SUPER))&&key(KEY_Q)) break;
    if(key(KEY_ESCAPE)) {if(console.visible) {console.visible=false;console.drag=-1;}else break;}
    if(key(KEY_C)) {console.visible=!console.visible;console.drag=-1;}
    if(key(KEY_SPACE)) console.paused=!console.paused;
    if(key(KEY_F)||console.fullscreenRequested) {console.fullscreenRequested=false;toggleFullscreen();}
    if(key(KEY_M)) console.muted=!console.muted;
    if(key(KEY_MINUS)) console.volume=std::max(0.f,console.volume-.05f);
    if(key(KEY_EQUAL)) console.volume=std::min(1.f,console.volume+.05f);
    if(key(KEY_I)||console.liveRequested) {console.liveRequested=false;stopYouTube();audio.live();console.notice.clear();}
    if(key(KEY_D)||console.synthRequested) {console.synthRequested=false;stopYouTube();audio.internal();console.notice.clear();}
    if(key(KEY_A)) patch.automatic=!patch.automatic;
    if(key(KEY_RIGHT)) {changeScene(patch.scene+1);patch.automatic=false;}
    if(key(KEY_LEFT)) {changeScene(patch.scene-1);patch.automatic=false;}
    for(int i=0;i<6;i++) if(key(KEY_ONE+i)) {changeScene(i);patch.automatic=false;}
    if(key(KEY_R)) {seed=audioimage::freshSeed();video.mutate(seed);mutationClock=0;}
    if(key(KEY_BACKSPACE)||console.clearRequested) {console.clearRequested=false;video.clear();}
    if(console.visible) {
      if(key(KEY_TAB)) {console.details=false;console.selected=(console.selected+(console.shift()?7:1))%8;}
      float tuning=float(IsKeyDown(KEY_RIGHT_BRACKET))-float(IsKeyDown(KEY_LEFT_BRACKET));
      if(tuning!=0) console.details=false;
      patch.values[console.selected]=std::clamp(patch.values[console.selected]+tuning*dt*(console.shift()?.05f:.25f),0.f,1.f);
    }
    auto loadFile=[&](const std::string& path) {
      if(audio.load(path)) {stopYouTube();console.paused=false;console.notice.clear();}
    };
#ifdef __APPLE__
    if(key(KEY_Y)||key(KEY_P)||console.youtubeRequested||console.spotifyRequested) {
      bool spotify=key(KEY_P)||console.spotifyRequested;
      console.youtubeRequested=console.spotifyRequested=false;auto url=choosePlaylist(youtube.state().url,spotify);alignFullscreen();
      if(!url.empty()) {
        if(youtube.open(url)) {console.paused=false;console.notice.clear();}
        else {console.notice="Paste a YouTube or public Spotify playlist link.";console.noticeUntil=GetTime()+8;}
      }
    }
    if(key(KEY_COMMA)||key(KEY_PERIOD)||console.trackStep) {
      int step=console.trackStep?console.trackStep:key(KEY_COMMA)?-1:1;console.trackStep=0;
      if(youtube.state().active) youtube.next(step);
    }
    if(console.openFileRequested) {
      console.openFileRequested=false;
      // Present the native dialog between drawing frames. The audio worker
      // keeps playing while the picker is open; cancelling preserves transport.
      // AppKit can move a borderless window below the menu bar on activation.
      // Restore its monitor origin so the bottom of the image stays on screen.
      auto path=chooseAudioFile();
      alignFullscreen();
      if(!path.empty()) loadFile(path);
    }
#endif
    if(IsFileDropped()) {auto files=LoadDroppedFiles();if(files.count) loadFile(files.paths[0]);UnloadDroppedFiles(files);}
#ifdef __APPLE__
    if(auto track=youtube.takeReady()) {
      if(audio.load(track->path.string(),false)) {audio.name=track->title;youtubePlaying=true;console.notice.clear();}
      else youtube.failed(audio.status);
    }
    youtubeState=youtube.state();
    if(youtubePlaying&&!console.paused&&audio.finished()&&!youtubeState.busy&&youtubeState.error.empty()) {youtube.next();youtubeState=youtube.state();}
#endif
    if(selfCheck) {
      patch.automatic=false;
      if(frame%100==0) {changeScene(frame/100);patch.transition=1;video.clear();}
      console.visible=frame>=80&&frame<100;
      if(frame==200) SetWindowSize(1040,680);
      if(frame==300) SetWindowSize(1280,800);
    }
    audio.update(console.paused,console.muted,console.volume);
    if(console.paused&&audio.source==Source::Input) analyzer.update(audio.ring,dt);
    if(!console.paused) {
      if(offline) audio.offline(800);
      analyzer.update(audio.ring,dt);
      autoClock+=dt;mutationClock+=dt;
      if(patch.automatic&&autoClock>48) changeScene(patch.scene+1);
      if(patch.automatic&&mutationClock>96) {video.mutate(audioimage::freshSeed());mutationClock=0;}
      if(!video.render(analyzer.current(),patch,dt)) {okay=false;break;}
    }
    if(!hidden) {if(console.visible) ShowCursor();else HideCursor();}
    BeginDrawing();ClearBackground(BLACK);video.present();console.draw(audio,video,patch,analyzer.current(),seed,youtubeState,youtubePlaying);EndDrawing();
    if(key(KEY_S)||console.snapshotRequested) {console.snapshotRequested=false;snapshot();}
    if(selfCheck&&(frame%100==75||frame%100==90)) {
      Image image=LoadImageFromTexture(video.output());ImageResize(&image,96,60);Color* pixels=LoadImageColors(image);
      double mean=0,variation=0,difference=0;
      for(int i=0;i<image.width*image.height;i++) {float l=(pixels[i].r+pixels[i].g+pixels[i].b)/3.f;mean+=l;variation+=std::abs(float(pixels[i].r)-pixels[i].b);
        if(previousImage.size()==size_t(image.width*image.height)) difference+=std::abs(int(pixels[i].r)-previousImage[i].r)+std::abs(int(pixels[i].g)-previousImage[i].g)+std::abs(int(pixels[i].b)-previousImage[i].b);}
      mean/=image.width*image.height;variation/=image.width*image.height;difference/=image.width*image.height*3;
      if(frame%100==90) {sceneMeans[patch.scene]=mean;sceneDifferences[patch.scene]=difference;
        if(mean<2||mean>245||variation<2||difference<.1) {std::cerr<<"Invalid or static mode "<<patch.scene+1<<" mean="<<mean<<" colour="<<variation<<" motion="<<difference<<"\n";okay=false;}}
      previousImage.assign(pixels,pixels+image.width*image.height);UnloadImageColors(pixels);UnloadImage(image);
    }
    frame++;if(frames>0&&frame>=frames) break;
  }
  if(!screenshot.empty()) video.capture(screenshot);
  if(selfCheck) {for(int i=0;i<6;i++) std::cout<<scenes[i]<<" brightness="<<sceneMeans[i]<<" motion="<<sceneDifferences[i]<<"\n";
    std::cout<<(okay?"PASS":"FAIL")<<": six modes, live FFT, feedback animation, target resize, clean output.\n";}
  std::cout<<"Rendered "<<frame<<" frames; controls "<<(console.visible?"visible":"hidden")<<"; audio samples "<<audio.ring.count()
    <<"; RMS "<<analyzer.current().rms<<"; bands "<<analyzer.current().bass<<","<<analyzer.current().mid<<","<<analyzer.current().high
    <<"; attacks "<<analyzer.current().onsetCount[0]<<","<<analyzer.current().onsetCount[1]<<","<<analyzer.current().onsetCount[2]
    <<"; tempo "<<analyzer.current().bpm<<" ("<<analyzer.current().tempoConfidence<<"); dropped "<<analyzer.current().droppedSamples<<".\n";
  audio.shutdown();if(console.ownFont) UnloadFont(console.font);video.shutdown();ShowCursor();CloseWindow();return okay?0:1;
}
