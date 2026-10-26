#pragma once
#include <string>
namespace audioimage {
// Broad seeded regions share one map between source motion, feedback advection,
// and optical focus. Animation is bounded; multiplying time by a moving mask
// would eventually create violent acceleration at the region boundaries.
constexpr const char* regionalFunctions=R"GLSL(
vec2 regions(vec2 uv,vec4 motion,vec4 weave) {
  vec2 p=(uv-.5)*vec2(aspectRatio,1.0);
  p=rot(motion.w*.7)*p;
  p+=.055*vec2(sin(time*.021+motion.z),cos(time*.017+weave.w));
  float pools=.5+.28*sin(p.x*4.1+p.y*1.6+motion.z)+.22*cos(p.y*5.3-p.x*1.8+weave.w);
  float soft=smoothstep(.34,.76,pools);
  float current=.5+.29*sin(p.x*3.7-p.y*2.3+weave.w)+.21*cos(p.y*4.6+motion.w);
  return vec2(soft,mix(.28,2.15,smoothstep(.20,.82,current)));
}
)GLSL";
// A short history of actual drum onsets, in the audio clock. These local
// pressure fronts keep moving between beats instead of flashing a whole layer.
constexpr const char* musicFunctions=R"GLSL(
uniform vec4 musicPulse,musicSpectrum,musicNotes,musicWaves[4];
uniform vec4 musicGesture,musicDrive,musicMotion,musicFans[2],musicClapRipples[2];
uniform float musicBeat,musicPhrase;
vec2 musicFronts(float z) {
  vec2 pressure=vec2(0);
  for(int i=0;i<4;i++) {
    float t=max(0.0,1.0-abs(z-musicWaves[i].x)*musicWaves[i].y);
    float tail=max(0.0,1.0-abs((z-musicWaves[i].x)*musicWaves[i].y+1.8)/1.6);
    pressure+=vec2(t*t*(3.0-2.0*t),tail*tail*(3.0-2.0*tail))*musicWaves[i].z;
  }
  return min(pressure,vec2(1.2));
}
float musicPressure(float z) {return musicFronts(z).x;}
float musicClapFan(float z) {
  float fan=0.0;
  for(int i=0;i<2;i++) {
    float t=max(0.0,1.0-abs(z-musicFans[i].x)*musicFans[i].y);
    fan+=t*t*(3.0-2.0*t)*musicFans[i].z;
  }
  return min(fan,1.0);
}
float musicRing(vec2 p) {
  float r=length(p),pressure=0.0;
  for(int i=0;i<4;i++) {
    float age=musicWaves[i].w;
    float t=max(0.0,1.0-abs(r-(.09+age*.57))/(.075+max(age,0.0)*.028));
    pressure+=t*t*(3.0-2.0*t)*musicWaves[i].z;
  }
  return min(pressure,1.2);
}
float musicClapRing(vec2 p) {
  float ring=0.0,r=length(p);
  for(int i=0;i<2;i++) {
    float t=max(0.0,1.0-abs(r-musicClapRipples[i].x)*musicClapRipples[i].y);
    ring+=t*t*(3.0-2.0*t)*musicClapRipples[i].z;
  }
  return min(ring,1.0);
}
)GLSL";
const std::string fieldFragment=std::string(R"GLSL(#version 330
in vec2 fragTexCoord;
out vec4 finalColor;
uniform float time,aspectRatio,worldMix;
uniform float worldFamilyA,worldFamilyB;
uniform vec4 worldShapeA,worldShapeB,worldMotionA,worldMotionB;
uniform vec4 worldPaletteA,worldPaletteB,worldFieldA,worldFieldB,worldWeaveA,worldWeaveB;
uniform vec4 interaction;
mat2 rot(float a) {return mat2(cos(a),-sin(a),sin(a),cos(a));}
float hash(vec2 p) {return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
float noise(vec2 p) {
  vec2 cell=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);
  return mix(mix(hash(cell),hash(cell+vec2(1,0)),f.x),
    mix(hash(cell+vec2(0,1)),hash(cell+vec2(1,1)),f.x),f.y);
}
)GLSL")+regionalFunctions+musicFunctions+R"GLSL(
vec2 currentField(vec2 uv,float family,vec4 shape,vec4 motion,vec4 palette,vec4 field,vec4 weave) {
  float t=time*motion.x+motion.z;
  vec2 p=(uv-.5)*vec2(aspectRatio,1.0);
  p-=(field.xy-.5)*.65;
  p-=.11*vec2(sin(t*.83+motion.w),cos(t*.67+motion.z));
  p=rot(time*motion.y+.12*sin(t*.31+motion.w)+musicPhrase*.11)*p;
  p*=vec2(field.z,1.0/field.z)*shape.x;
  vec2 q=p+field.w*.17*vec2(sin(p.y*4.1+t*.47),cos(p.x*4.7-t*.39));
  float fast=(regions(uv,motion,weave).y-.28)/1.87;
  // Faster local eddies pass through the underlying slow world drift. Both
  // components are periodic and bounded, so a long session stays tranquil.
  q+=fast*.12*vec2(sin(p.y*5.0+time*.53+motion.z),cos(p.x*4.3-time*.43+motion.w));
  q+=(1.0-fast)*.045*vec2(sin(p.y*3.0+time*.07),cos(p.x*3.4-time*.06));
  q+=weave.y*.075*vec2(sin(q.y*7.0-t*.19),cos(q.x*6.0+t*.21));
  q+=shape.z*.026*vec2(sin(q.y*13.0+q.x*3.0+t*.29),cos(q.x*11.0-q.y*4.0-t*.23));
  // A gently smoothed gathering field bends the current around the player.
  vec2 toward=(interaction.xy-uv)*vec2(aspectRatio,1.0);
  float influence=exp(-dot(toward,toward)*13.0);
  q+=toward*influence*(.018+.085*interaction.z);
  q+=vec2(-toward.y,toward.x)*influence*.10*interaction.z*sin(length(toward)*18.0-time*.22);
  // Musical pressure changes the source field, leaving gameplay untouched.
  float pressure=musicRing(p);
  q*=1.0+musicMotion.x*.008;
  q+=p/max(length(p),.12)*pressure*.040;
  q+=musicSpectrum.x*.035*vec2(sin(q.y*3.0+musicBeat*.09817477),cos(q.x*3.1-musicBeat*.19634954));
  q+=vec2(-p.y,p.x)*musicSpectrum.w*.025;
  q+=vec2(sin(q.y*4.0+musicNotes.y*3.0),cos(q.x*3.0-musicNotes.y*2.0))*
    musicMotion.y*(.018+.026*musicMotion.w);
  q+=vec2(-p.y,p.x)*musicClapRing(p)*.020;
  q+=vec2(sin(q.y*2.0+musicBeat*.09817477),cos(q.x*2.0-musicBeat*.09817477))*musicMotion.z*.025;
  float r=length(q),a=atan(q.y,q.x+.000001),signal=0.0,secondary=0.0;
  if(family<.5) {
    signal=sin(q.x*5.0+shape.z*sin(q.y*4.0+t*.38));
    signal+=.65*cos(q.y*6.0-q.x*shape.y+t*.29);
    signal+=.28*sin(dot(q,vec2(9.0,-7.0))*shape.w-t*.21);
    secondary=q.y*4.3+sin(q.x*3.7-t*.17)+.3*signal;
  } else if(family<1.5) {
    float spiral=a*shape.y+shape.z*3.2*log(r+.16)+t*.37;
    signal=sin(spiral)+.42*cos(r*9.0-t*.27);
    signal+=.22*sin(a*2.0+r*5.0+t*.43);
    secondary=cos(a*(shape.y+1.0)+log(r+.24)*2.8-t*.19)+r*4.6;
  } else if(family<2.5) {
    float sector=6.283185/shape.y;
    float folded=abs(mod(a+t*.16+sector*.5,sector)-sector*.5);
    vec2 prism=vec2(cos(folded),sin(folded))*r;
    signal=prism.x*3.4+sin(prism.y*8.0*shape.z-t*.4)*.54;
    signal+=.34*sin(r*11.0-t*.61);
    secondary=prism.y*6.0+.45*sin(prism.x*7.0+t*.23);
  } else if(family<3.5) {
    vec2 c1=.24*vec2(sin(t*.33),cos(t*.29));
    vec2 c2=.31*vec2(cos(t*.27+motion.w),sin(t*.41));
    signal=sin(length(q-c1)*13.0*shape.z-t*.61);
    signal+=.75*sin(length(q-c2)*10.0+t*.47);
    secondary=cos(length(q-c1)*9.0+t*.31)-.65*sin(length(q-c2)*12.0-t*.22);
  } else if(family<4.5) {
    float twist=a*shape.y+shape.z*3.0/(r+.24)+t*.20;
    signal=sin(twist)+.46*cos(r*13.0-t*.4);
    signal+=.2*sin(a*2.0-r*7.0+t*.17);
    secondary=cos(a*(shape.y+2.0)+shape.z*2.0/(r+.4)-t*.21)+r*4.0;
  } else {
    vec2 drift=vec2(t*.18,-t*.13);
    float cloud=noise(q*3.0+drift+motion.w);
    cloud+=.5*noise(q*6.3-drift*.7+shape.z);
    signal=sin(q.x*6.0+cloud*5.0+t*.24)*cos(q.y*5.0-cloud*4.0-t*.31);
    signal+=cloud*.8;
    secondary=cloud*3.1+sin(q.y*4.1-cloud*2.0+t*.21);
  }
  signal+=palette.x+.07*sin(t*.19+motion.w)+musicPhrase*.11+musicNotes.y*.065+musicNotes.x*.025;
  secondary=secondary*weave.x+weave.w*.4;
  return vec2(signal,secondary);
}
void main() {
  vec2 current=currentField(fragTexCoord,worldFamilyA,worldShapeA,worldMotionA,worldPaletteA,worldFieldA,worldWeaveA);
  vec2 previous=current;
  if(worldMix<.999) previous=currentField(fragTexCoord,worldFamilyB,worldShapeB,worldMotionB,worldPaletteB,worldFieldB,worldWeaveB);
  finalColor=vec4(current,previous);
}
)GLSL";
}
