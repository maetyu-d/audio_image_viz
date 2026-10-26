#include "video.hpp"
#include "field_shader.hpp"
#include "video_shaders.hpp"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
namespace av {
namespace {
std::unordered_map<unsigned int,std::unordered_map<std::string,int>> locations;
int loc(Shader s,const char* name) {auto& m=locations[s.id];auto it=m.find(name);if(it!=m.end()) return it->second;return m.emplace(name,GetShaderLocation(s,name)).first->second;}
void value(Shader s,const char* name,float v) {int l=loc(s,name);if(l>=0) SetShaderValue(s,l,&v,SHADER_UNIFORM_FLOAT);}
void vec(Shader s,const char* name,std::array<float,4> v) {int l=loc(s,name);if(l>=0) SetShaderValue(s,l,v.data(),SHADER_UNIFORM_VEC4);}
void tex(Shader s,const char* name,Texture2D t) {int l=loc(s,name);if(l>=0) SetShaderValueTexture(s,l,t);}
void events(Shader s,const char* name,const std::array<std::array<float,4>,12>& values) {
  int l=loc(s,name);if(l>=0) SetShaderValueV(s,l,values.data(),SHADER_UNIFORM_VEC4,12);
}
void quad(Texture2D input,int w,int h) {DrawTexturePro(input,{0,0,float(input.width),-float(input.height)},{0,0,float(w),float(h)},{0,0},0,WHITE);}
RenderTexture2D target(int w,int h,bool full=false) {
  auto t=LoadRenderTexture(w,h);if(!IsRenderTextureValid(t)) return t;
  rlUnloadTexture(t.texture.id);int format=full?PIXELFORMAT_UNCOMPRESSED_R32G32B32A32:PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
  t.texture.id=rlLoadTexture(nullptr,w,h,format,1);t.texture.format=format;
  rlEnableFramebuffer(t.id);rlFramebufferAttach(t.id,t.texture.id,RL_ATTACHMENT_COLOR_CHANNEL0,RL_ATTACHMENT_TEXTURE2D,0);
  bool complete=rlFramebufferComplete(t.id);rlDisableFramebuffer();
  if(!complete) {UnloadRenderTexture(t);return {};}
  SetTextureFilter(t.texture,TEXTURE_FILTER_BILINEAR);SetTextureWrap(t.texture,TEXTURE_WRAP_CLAMP);
  BeginTextureMode(t);ClearBackground(BLACK);EndTextureMode();return t;
}
}
bool Video::init(uint32_t seed,int resolution) {
  maxHeight=resolution;world=audioimage::generateWorld(seed,0);previousWorld=world;
  field=LoadShaderFromMemory(nullptr,audioimage::fieldFragment.c_str());
  feedback=LoadShaderFromMemory(nullptr,av::feedbackFragment.c_str());
  blur=LoadShaderFromMemory(nullptr,av::blurFragment);post=LoadShaderFromMemory(nullptr,av::postFragment.c_str());
  percussion=LoadShaderFromMemory(nullptr,av::percussionFragment.c_str());
  vectorBeam=LoadShaderFromMemory(nullptr,av::vectorBeamFragment);
  for(auto s:{field,feedback,blur,post,percussion,vectorBeam}) if(s.id==rlGetShaderIdDefault()||!s.id) return false;
  std::array<float,spectrumSize*4> fft{};std::array<float,scopeSize*4> waveform{};
  spectrum={rlLoadTexture(fft.data(),spectrumSize,1,PIXELFORMAT_UNCOMPRESSED_R32G32B32A32,1),spectrumSize,1,1,PIXELFORMAT_UNCOMPRESSED_R32G32B32A32};
  scope={rlLoadTexture(waveform.data(),scopeSize,1,PIXELFORMAT_UNCOMPRESSED_R32G32B32A32,1),scopeSize,1,1,PIXELFORMAT_UNCOMPRESSED_R32G32B32A32};
  SetTextureFilter(spectrum,TEXTURE_FILTER_BILINEAR);SetTextureFilter(scope,TEXTURE_FILTER_BILINEAR);
  SetTextureWrap(spectrum,TEXTURE_WRAP_CLAMP);SetTextureWrap(scope,TEXTURE_WRAP_CLAMP);
  return spectrum.id&&scope.id&&resize();
}
bool Video::resize() {
  int w=std::max(1,GetRenderWidth()),h=std::max(1,GetRenderHeight());
  if(image.texture.width==w&&image.texture.height==h) return true;
  int hh=std::min(h,maxHeight),hw=std::max(1,int(std::round(float(w)*hh/h)));
  int fh=std::min(h,512),fw=std::max(1,int(std::round(float(w)*fh/h)));
  int bh=std::min(h,300),bw=std::max(1,int(std::round(float(w)*bh/h)));
  RenderTexture2D next[10]{};
  RenderTexture2D* targets[]={&history[0],&history[1],&delay[0],&delay[1],&delay[2],&delay[3],&flow,&bloom[0],&bloom[1],&image};
  for(int i=0;i<10;i++) {
    next[i]=i<6?target(hw,hh):i==6?target(fw,fh,true):i<9?target(bw,bh):LoadRenderTexture(w,h);
    if(!IsRenderTextureValid(next[i])) {for(auto t:next) if(t.id) UnloadRenderTexture(t);return false;}
    SetTextureFilter(next[i].texture,TEXTURE_FILTER_BILINEAR);
  }
  for(int i=0;i<10;i++) {
    if(targets[i]->id) {BeginTextureMode(next[i]);rlDisableColorBlend();quad(targets[i]->texture,next[i].texture.width,next[i].texture.height);rlDrawRenderBatchActive();rlEnableColorBlend();EndTextureMode();UnloadRenderTexture(*targets[i]);}
    *targets[i]=next[i];
  }
  return true;
}
void Video::mutate(uint32_t seed) {previousWorld=world;world=audioimage::generateWorld(seed,0);worldAge=0;}
void Video::clear() {for(auto t:{history[0],history[1],delay[0],delay[1],delay[2],delay[3],bloom[0],bloom[1]}) {BeginTextureMode(t);ClearBackground(BLACK);EndTextureMode();}}
bool Video::render(const Signal& signal,Patch& patch,float dt) {
  if(!resize()) return false;
  clock+=dt;worldAge+=dt;delayClock+=dt;
  patch.transition=std::min(1.f,patch.transition+dt/3.f);
  float blend=std::clamp(worldAge/9.f,0.f,1.f);blend=blend*blend*(3-2*blend);
  float aspect=float(image.texture.width)/image.texture.height;
  float reaction=patch.values[6]*2;
  if(signal.analyzedSamples<lastAudioSample) bassBend=subBend=0;
  lastAudioSample=signal.analyzedSamples;
  auto envelope=[&](float old,float target){return target+(old-target)*std::exp(-dt/(target>old?.035f:.18f));};
  float oldBass=bassBend,oldSub=subBend;
  bassBend=envelope(bassBend,std::pow(std::clamp(signal.bass,0.f,1.f),.8f));
  subBend=envelope(subBend,std::pow(std::clamp(signal.bands[0],0.f,1.f),.85f));
  std::array<float,4> bend{bassBend*reaction,subBend*reaction,
    std::clamp((bassBend-oldBass)*reaction,-.35f,.35f),std::clamp((subBend-oldSub)*reaction,-.35f,.35f)};
  std::array<std::array<float,4>,12> hits{},hitColours{};
  for(int band=0;band<3;band++) for(int j=0;j<4;j++) {
    const auto& hit=signal.hits[band][j];int i=band*4+j;
    float age=hit.strength>0&&signal.analyzedSamples>=hit.sample?float(signal.analyzedSamples-hit.sample)/analysisRate:100;
    uint32_t hash=uint32_t(hit.sample)^uint32_t(hit.sample>>32)^uint32_t(band+1)*0x9e3779b9u;
    hash^=hash>>16;hash*=0x7feb352du;hash^=hash>>15;hash*=0x846ca68bu;hash^=hash>>16;
    float seed=float(hash&0xffff)/65536.f;
    hits[i]={age,hit.strength*reaction,seed*6.283185307f,float(band)};
    for(int c=0;c<3;c++) hitColours[i][c]=std::pow(.5f+.5f*std::cos(6.283185307f*(seed+patch.values[4]+c*.333333f)),1.5f);
  }
  auto reactionUniforms=[&](Shader shader) {
    vec(shader,"bassDrive",bend);events(shader,"hitEvents[0]",hits);events(shader,"hitColours[0]",hitColours);
  };
  std::array<float,4> audio{signal.bass*reaction,signal.mid*reaction,signal.high*reaction,signal.onset*reaction};
  std::array<float,spectrumSize*4> fft{};std::array<float,scopeSize*4> wave{};
  for(int i=0;i<spectrumSize;i++) fft[i*4]=signal.spectrum[i]*reaction;
  for(int i=0;i<scopeSize;i++) {wave[i*4]=signal.scope[i*2]*reaction;wave[i*4+1]=signal.scope[i*2+1]*reaction;}
  UpdateTexture(spectrum,fft.data());UpdateTexture(scope,wave.data());
  value(field,"time",clock*.7f);value(field,"aspectRatio",aspect);value(field,"worldMix",blend);
  auto worldValues=[&](const audioimage::World& w,const char* suffix) {
    auto n=[&](const char* base){return std::string(base)+suffix;};
    value(field,n("worldFamily").c_str(),float(w.family));
    vec(field,n("worldShape").c_str(),w.shape);vec(field,n("worldMotion").c_str(),w.motion);
    vec(field,n("worldPalette").c_str(),w.palette);vec(field,n("worldField").c_str(),w.field);vec(field,n("worldWeave").c_str(),w.weave);
  };
  worldValues(world,"A");worldValues(previousWorld,"B");
  vec(field,"musicSpectrum",{audio[0]*1.8f,audio[1],audio[2],signal.stereo*reaction});
  vec(field,"musicMotion",{audio[0]*2.3f,audio[1]*1.4f,audio[2],signal.centroid*reaction});
  vec(field,"interaction",{.5f,.5f,0,0});
  BeginTextureMode(flow);rlDisableColorBlend();BeginShaderMode(field);quad(history[current].texture,flow.texture.width,flow.texture.height);EndShaderMode();rlEnableColorBlend();EndTextureMode();
  int next=1-current;
  value(feedback,"time",clock);value(feedback,"aspect",aspect);value(feedback,"delta",dt);
  for(auto shader:{feedback,post,percussion}) {
    value(shader,"scene",float(patch.scene));value(shader,"previousScene",float(patch.previousScene));
    value(shader,"morph",patch.transition*patch.transition*(3-2*patch.transition));
  }
  reactionUniforms(feedback);
  vec(feedback,"audio",audio);vec(feedback,"character",{signal.centroid*reaction,signal.stereo*reaction,signal.rms,signal.peak});
  vec(feedback,"transients",{signal.attacks[0]*reaction,signal.attacks[1]*reaction,signal.attacks[2]*reaction,signal.width*reaction});
  vec(feedback,"lowBands",{signal.bands[0]*reaction,signal.bands[1]*reaction,signal.bands[2]*reaction,signal.bands[3]*reaction});
  vec(feedback,"highBands",{signal.bands[4]*reaction,signal.bands[5]*reaction,signal.bands[6]*reaction,signal.level*reaction});
  vec(feedback,"controls",{patch.values[0],patch.values[1],patch.values[2],patch.values[3]});
  vec(feedback,"colour",{patch.values[4],patch.values[5],0,0});
  BeginTextureMode(history[next]);rlDisableColorBlend();BeginShaderMode(feedback);
  tex(feedback,"flowTexture",flow.texture);tex(feedback,"delayTexture",delay[delayIndex].texture);
  // Last frame's blurred emission becomes illumination for the next frame.
  tex(feedback,"spectrumTexture",spectrum);tex(feedback,"radianceTexture",bloom[1].texture);
  quad(history[current].texture,history[next].texture.width,history[next].texture.height);
  EndShaderMode();rlEnableColorBlend();EndTextureMode();current=next;
  injectVectors(signal,patch,reaction,blend,dt);
  if(delayClock>=.24f) {delayClock=std::fmod(delayClock,.24f);
    BeginTextureMode(delay[delayIndex]);rlDisableColorBlend();quad(history[current].texture,delay[delayIndex].texture.width,delay[delayIndex].texture.height);rlDrawRenderBatchActive();rlEnableColorBlend();EndTextureMode();
    delayIndex=(delayIndex+1)%4;}
  for(int pass=0;pass<2;pass++) {
    auto input=pass==0?history[current].texture:bloom[0].texture;
    BeginTextureMode(bloom[pass]);BeginShaderMode(blur);
    Vector2 direction=pass==0?Vector2{2.f/input.width,0}:Vector2{0,2.f/input.height};
    int l=loc(blur,"direction");SetShaderValue(blur,l,&direction,SHADER_UNIFORM_VEC2);value(blur,"extract",pass==0?1.f:0.f);
    quad(input,bloom[pass].texture.width,bloom[pass].texture.height);EndShaderMode();EndTextureMode();
  }
  BeginTextureMode(image);ClearBackground(BLACK);BeginShaderMode(post);
  tex(post,"bloomTexture",bloom[1].texture);value(post,"exposure",patch.values[7]);
  tex(post,"flowTexture",flow.texture);value(post,"detail",patch.values[5]);value(post,"phase",patch.values[4]);
  value(post,"time",clock);value(post,"aspect",aspect);reactionUniforms(post);
  quad(history[current].texture,image.texture.width,image.texture.height);EndShaderMode();
  value(percussion,"time",clock);value(percussion,"aspect",aspect);vec(percussion,"bassDrive",bend);
  Vector2 resolution{float(image.texture.width),float(image.texture.height)};
  SetShaderValue(percussion,loc(percussion,"resolution"),&resolution,SHADER_UNIFORM_VEC2);
  for(int i=0;i<12;i++) {
    const auto& hit=hits[i];int band=i/4;float age=hit[0],life=band==0?.62f:band==1?.34f:.21f;
    if(hit[1]<=0||age>life) continue;
    Vector2 centre{resolution.x*.5f+std::cos(hit[2])*.28f*resolution.y,
      resolution.y*(.5f-std::sin(hit[2])*.21f)};
    auto drawEvent=[&](int mode,float weight) {
      // Mode 2 percussion excites its signal conduits inside feedback.
      if(weight<=.0001f||mode==1) return;
      BeginShaderMode(percussion);tex(percussion,"flowTexture",flow.texture);
      vec(percussion,"singleEvent",hit);vec(percussion,"singleColour",hitColours[i]);
      value(percussion,"eventScene",float(mode));
      value(percussion,"eventMix",weight*((mode==2||mode==4)?.78f:1.f));
      if(mode==3) {
        float angle=hit[2]-PI+.45f*std::sin(clock*.19f+hit[2])+age*.8f;
        float reach=.07f+age*(band==0?1.2f:band==1?2.f:3.f);
        float support=(reach*.22f+.014f+(band==0?.0028f:band==1?.0016f:.0008f))*resolution.y+4;
        Vector2 moved{centre.x+std::cos(angle)*reach*.5f*resolution.y,
          centre.y-std::sin(angle)*reach*.5f*resolution.y};
        float span=reach*resolution.y+8;
        DrawRectanglePro({moved.x,moved.y,span,support*2},{span*.5f,support},-angle*RAD2DEG,WHITE);
      } else if(mode==2||mode==4) {
        // Match hitInk's rotated signal strip, including bend and AA support.
        float angle=mode==4?PI*.5f+.35f*std::sin(hit[2]):(hit[2]-PI)*.38f;
        float travel=age*(mode==4?.70f:.90f)*(std::sin(hit[2])>0?1.f:-1.f);
        float width=band==0?.009f:band==1?.0035f:.0013f;
        float support=(width+(mode==4?.038f:.018f))*resolution.y+4;
        float span=std::hypot(resolution.x,resolution.y);
        Vector2 moved{centre.x-std::sin(angle)*travel*resolution.y,
          centre.y-std::cos(angle)*travel*resolution.y};
        // Shader coordinates have upward Y; screen rotations have downward Y.
        DrawRectanglePro({moved.x,moved.y,span*2,support*2},{span,support},-angle*RAD2DEG,WHITE);
      } else {
        float radius=band==0?.045f+age*1.4f:band==1?.055f+age*2.2f:.10f+age*3.5f;
        float support=(band==0?.0025f+age*.003f:band==1?.065f+age*.10f:.17f)+3/resolution.y;
        DrawRing(centre,std::max(0.f,radius-support)*resolution.y,(radius+support)*resolution.y,
          0,360,std::clamp(int((radius+support)*resolution.y/4),64,384),WHITE);
      }
      EndShaderMode();
    };
    float morph=patch.transition*patch.transition*(3-2*patch.transition);
    if(patch.scene!=patch.previousScene&&morph<.9999f) {
      drawEvent(patch.previousScene,1-morph);drawEvent(patch.scene,morph);
    } else drawEvent(patch.scene,1);
  }
  EndTextureMode();return true;
}
void Video::injectVectors(const Signal& signal,const Patch& patch,float reaction,float blend,float dt) {
  float morph=patch.transition*patch.transition*(3-2*patch.transition);
  float weight=patch.scene==3?1.f:0.f;
  if(patch.scene!=patch.previousScene&&morph<.9999f) weight=(patch.previousScene==3?1-morph:0)+(patch.scene==3?morph:0);
  if(weight<=.0001f) return;
  const float w=float(history[current].texture.width),h=float(history[current].texture.height);
  float step=std::clamp(dt*60.f,.1f,3.f),bass=signal.bass*reaction;
  float variant=previousWorld.shape[0]*(1-blend)+world.shape[0]*blend;
  float yaw=clock*.19f+.22f*std::sin(clock*.31f),pitch=.55f*std::sin(clock*.23f);
  float roll=.42f*std::sin(clock*.17f)+bass*.16f;
  auto project=[&](int i,int copy) {
    float u=float(i)/(scopeSize-1),phase=u*2*PI;
    int other=(i+copy*29)%scopeSize;
    float left=signal.scope[i*2]*reaction,right=signal.scope[other*2+1]*reaction;
    float carrier=2.7f+.16f*std::sin(variant);
    float x=left*.37f+.16f*std::sin(phase*carrier+clock*.43f+copy*.65f);
    float y=right*.34f+.16f*std::sin(phase*2.f-clock*.37f+copy*.73f);
    float z=(left-right)*.18f+.19f*std::cos(phase*3.f+clock*.21f+copy*.39f);
    float xx=x*std::cos(yaw)+z*std::sin(yaw),zz=-x*std::sin(yaw)+z*std::cos(yaw);
    float yy=y*std::cos(pitch)-zz*std::sin(pitch);zz=y*std::sin(pitch)+zz*std::cos(pitch);
    float scale=(.82f+copy*.14f)*(.95f+bass*.28f)/(1.f+zz*.65f);
    x=(xx*std::cos(roll)-yy*std::sin(roll))*scale+.13f*std::sin(clock*.27f+copy*1.9f);
    y=(xx*std::sin(roll)+yy*std::cos(roll))*scale+.10f*std::cos(clock*.22f+copy*1.7f);
    // Near strokes carry more phosphor; distant returns retain their space
    // without giving every crossing the same luminous weight.
    return Vector3{w*.5f+x*h,h*(.5f-y),std::clamp(1.f-zz*.75f,.5f,1.25f)};
  };
  BeginTextureMode(history[current]);BeginShaderMode(vectorBeam);BeginBlendMode(BLEND_ALPHA);
  const Color pigments[]={{36,255,105,255},{16,175,255,255},{116,255,182,255},{44,255,229,255}};
  for(int copy=0;copy<4;copy++) {
    rlCheckRenderBatchLimit(scopeSize*4);rlSetTexture(rlGetTextureIdDefault());rlBegin(RL_QUADS);
    Color colour=pigments[copy];
    if(std::abs(patch.values[4]-.12f)>.0001f) {
      Vector3 hsv=ColorToHSV(colour);colour=ColorFromHSV(hsv.x+(patch.values[4]-.12f)*180.f,hsv.y,hsv.z);
    }
    float alpha=(.14f+signal.high*reaction*.025f)*weight*step*(1.f-copy*.13f);
    Vector3 a=project(0,copy);
    for(int i=1;i<scopeSize;i++) {
      Vector3 b=project(i,copy);float dx=b.x-a.x,dy=b.y-a.y,length=std::hypot(dx,dy);
      if(length>.75f) {
        float intensity=alpha*(a.z+b.z)*.5f;
        rlColor4ub(colour.r,colour.g,colour.b,(unsigned char)std::clamp(intensity*255.f,0.f,255.f));
        float width=std::max(1.35f,h*.0022f),nx=-dy/length*width,ny=dx/length*width;
        rlTexCoord2f(0,0);rlVertex2f(a.x-nx,a.y-ny);
        rlTexCoord2f(0,1);rlVertex2f(a.x+nx,a.y+ny);
        rlTexCoord2f(1,1);rlVertex2f(b.x+nx,b.y+ny);
        rlTexCoord2f(1,0);rlVertex2f(b.x-nx,b.y-ny);
        a=b;
      }
    }
    rlEnd();rlSetTexture(0);
  }
  EndBlendMode();EndShaderMode();EndTextureMode();
}
void Video::present() const {quad(image.texture,GetScreenWidth(),GetScreenHeight());}
void Video::capture(const std::string& path) const {Image pixels=LoadImageFromTexture(image.texture);ImageFlipVertical(&pixels);ExportImage(pixels,path.c_str());UnloadImage(pixels);}
void Video::shutdown() {
  for(auto t:{history[0],history[1],delay[0],delay[1],delay[2],delay[3],flow,bloom[0],bloom[1],image}) if(t.id) UnloadRenderTexture(t);
  for(auto s:{field,feedback,blur,post,percussion,vectorBeam}) if(s.id&&s.id!=rlGetShaderIdDefault()) UnloadShader(s);
  if(spectrum.id) UnloadTexture(spectrum);if(scope.id) UnloadTexture(scope);locations.clear();
}
}
