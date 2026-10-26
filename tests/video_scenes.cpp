#include "video.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <vector>
namespace {
constexpr int columns=48,rows=30;
struct Picture {std::vector<float> colour,structure;double dark=0,brightness=0;};
Picture inspect(av::Video& video) {
  Image image=LoadImageFromTexture(video.output());Color* pixels=LoadImageColors(image);
  Picture result;result.colour.resize(columns*rows*3);result.structure.resize(columns*rows);
  for(int y=0;y<rows;y++) for(int x=0;x<columns;x++) {
    double rgb[3]={};int count=0;
    for(int py=y*image.height/rows;py<(y+1)*image.height/rows;py++)
      for(int px=x*image.width/columns;px<(x+1)*image.width/columns;px++) {
        Color p=pixels[py*image.width+px];rgb[0]+=p.r;rgb[1]+=p.g;rgb[2]+=p.b;count++;
        if(std::max({p.r,p.g,p.b})<24) result.dark++;
      }
    int i=y*columns+x;for(int c=0;c<3;c++) result.colour[i*3+c]=rgb[c]/count/255;
    result.structure[i]=(.2126*rgb[0]+.7152*rgb[1]+.0722*rgb[2])/count/255;
    result.brightness+=result.structure[i]/(columns*rows);
  }
  // Normalise luminance so a palette change alone cannot satisfy the geometry check.
  for(auto& p:result.structure) p=std::min(1.f,p/float(result.brightness*2+.02));
  result.dark/=image.width*image.height;UnloadImageColors(pixels);UnloadImage(image);return result;
}
double distance(const std::vector<float>& a,const std::vector<float>& b) {
  double total=0;for(size_t i=0;i<a.size();i++) total+=std::abs(a[i]-b[i]);return total/a.size();
}
av::Signal soundAt(int frame) {
  av::Signal s{};s.analyzedSamples=uint64_t(frame+1)*800;
  float t=frame/60.f,beat=std::exp(-std::fmod(t,.46875f)*9);
  s.bass=.22f+beat*.65f;s.mid=.25f+.12f*std::sin(t*3);s.high=.16f+.1f*std::cos(t*5);
  s.onset=beat;s.centroid=.45f;s.width=.6f;s.stereo=.13f*std::sin(t);s.rms=.15f;s.peak=.6f;s.level=.65f;
  for(int i=0;i<7;i++) s.bands[i]=(.2f+.12f*std::sin(t*(i+1)+i))+(i<2?beat*.5f:0);
  for(int i=0;i<av::spectrumSize;i++) s.spectrum[i]=.15f+.3f*std::pow(.5f+.5f*std::sin(i*.19f+t),4.f);
  for(int i=0;i<av::scopeSize;i++) {
    float phase=float(i)/av::scopeSize*6.2831853f;
    s.scope[i*2]=(.48f*std::sin(phase*3)+.16f*std::sin(phase*13+t))*(.6f+beat*.4f);
    s.scope[i*2+1]=.45f*std::sin(phase*4+t*.5f)+.14f*std::sin(phase*17);
  }
  for(int b=0;b<3;b++) {
    int period=b==0?28:b==1?42:14,offset=b==1?14:0;
    for(int j=0;j<4;j++) {
      int f=(frame-offset)/period*period+offset-j*period;
      if(f>=0&&f<=frame) s.hits[b][j]={uint64_t(f+1)*800,.8f};
    }
    s.attacks[b]=std::exp(-float((frame-offset+period)%period)/5);
  }
  return s;
}
}
int main(int argc,char** argv) {
  using namespace av;SetTraceLogLevel(LOG_WARNING);SetConfigFlags(FLAG_WINDOW_HIDDEN|FLAG_WINDOW_HIGHDPI);
  InitWindow(960,600,"AudioImage / mode identity check");SetTargetFPS(0);if(!IsWindowReady()) return 1;
  std::filesystem::path folder=argc>1?argv[1]:"captures/scenes";std::filesystem::create_directories(folder);
  std::vector<Picture> pictures;bool okay=true;
  for(int scene=0;scene<sceneCount;scene++) {
    Video video;Patch patch;patch.scene=patch.previousScene=scene;patch.automatic=false;
    if(!video.init(77,600)) {std::cerr<<"Shader initialization failed.\n";return 1;}
    for(int f=0;f<240;f++) if(!video.render(soundAt(f),patch,1.f/60)) okay=false;
    video.capture((folder/(std::to_string(scene+1)+".png")).string());pictures.push_back(inspect(video));
    std::cout<<scenes[scene]<<": luminance="<<pictures.back().brightness<<" dark area="<<pictures.back().dark*100<<"%\n";
    okay=okay&&pictures.back().brightness>.012&&pictures.back().brightness<.8;
    video.shutdown();
  }
  for(int i=0;i<sceneCount;i++) for(int j=i+1;j<sceneCount;j++) {
    double colour=distance(pictures[i].colour,pictures[j].colour),geometry=distance(pictures[i].structure,pictures[j].structure);
    std::cout<<i+1<<" / "<<j+1<<": colour="<<colour<<" structure="<<geometry<<"\n";
    okay=okay&&colour>.045&&geometry>.065;
  }
  // Regenerated phosphor beams must remain distinct against dark image memory.
  okay=okay&&pictures[3].dark>.25;
  // Verify a live switch can shed a bright liquid scene without clearing memory.
  Video video;Patch patch;patch.automatic=false;
  if(!video.init(77,600)) return 1;
  for(int f=0;f<240;f++) video.render(soundAt(f),patch,1.f/60);
  patch.previousScene=0;patch.scene=3;patch.transition=0;
  for(int f=240;f<480;f++) video.render(soundAt(f),patch,1.f/60);
  video.capture((folder/"live-mode-1-to-4.png").string());auto switched=inspect(video);
  okay=okay&&switched.dark>.25&&patch.transition==1;
  std::cout<<"Live switch to Mode 4: dark area="<<switched.dark*100<<"%\n";
  video.shutdown();CloseWindow();
  std::cout<<(okay?"PASS":"FAIL")<<": distinct mode colour and spatial structure, negative space, and live feedback morph.\n";
  return okay?0:1;
}
