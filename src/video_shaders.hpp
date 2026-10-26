#pragma once
#include <string>
namespace av {
// Geometry, colour and transport belong to the scene, including the finishing
// and percussion passes. A shared phase field modulates these instruments;
// it does not impose the same contours on all of them.
inline const std::string sceneFunctions=R"GLSL(
uniform float scene,previousScene,morph;
const float PI=3.14159265;
float syncAxis() {return .24*sin(time*.43)+.50*sin(time*.19);}
vec2 horizonCentre() {return vec2(.20*sin(time*.23)+.09*sin(time*.61),.13*cos(time*.29));}
mat2 rot(float a) {return mat2(cos(a),-sin(a),sin(a),cos(a));}
vec3 palette(float p) {
  return pow(.5+.5*cos(6.2831853*(p+vec3(0.0,.34,.67))),vec3(1.5));
}
vec3 scenePalette(float mode,float x) {
  if(mode<.5) return palette(x);
  float a=.5+.5*sin(x*6.2831853),b=pow(.5+.5*cos(x*6.2831853),4.0);
  if(mode<1.5) return mix(mix(vec3(.025,.04,.36),vec3(.90,.025,.22),a),vec3(.035,.70,.88),b*.85);
  if(mode<2.5) return mix(vec3(.025,.60,.48),vec3(.94,.29,.025),a)+vec3(.22,.065,.48)*b;
  if(mode<3.5) return mix(vec3(.015,.25,.16),vec3(.18,1.0,.62),a)+vec3(.10,.22,.28)*b;
  if(mode<4.5) return mix(vec3(.015,.12,.76),vec3(.025,.82,.92),a)+vec3(1.0,.22,.045)*b*.85;
  return mix(vec3(.12,.015,.65),vec3(1.0,.13,.018),a)+vec3(.5,.13,.02)*b;
}
vec3 sceneAccent(float mode,vec3 ink) {
  if(mode<.5) return ink;
  if(mode<1.5) return mix(vec3(.12,.75,1),vec3(1,.035,.38),ink.r);
  if(mode<2.5) return mix(vec3(.03,1,.66),vec3(1,.43,.08),ink.g);
  if(mode<3.5) return ink.r>.65?vec3(1,.24,.12):mix(vec3(.12,1,.40),vec3(.04,.66,1),ink.b);
  if(mode<4.5) return mix(vec3(.06,.55,1),vec3(1,.24,.08),ink.g);
  return mix(vec3(.35,.12,1),vec3(1,.42,.06),ink.r);
}
)GLSL";
inline const std::string reactiveFunctions=R"GLSL(
uniform vec4 bassDrive,hitEvents[12],hitColours[12];
vec2 hitCentre(vec4 event) {return vec2(cos(event.z)*.28,sin(event.z)*.21);}
float hitLine(float distance,float width) {
  return 1.0-smoothstep(width,width+max(fwidth(distance),.0005),abs(distance));
}
vec2 bassCoordinates(vec2 p) {
  float angle=bassDrive.x*.24*sin(p.y*3.2+time*.13);
  vec2 q=mat2(cos(angle),-sin(angle),sin(angle),cos(angle))*p;
  q*=vec2(1.0+bassDrive.y*.18,1.0-bassDrive.y*.11);
  q+=bassDrive.x*.052*vec2(sin(p.y*5.0+time*.19),sin(p.x*4.0-time*.17));
  return q;
}
vec2 kickPressure(vec2 p) {
  vec2 pressure=vec2(0);
  for(int i=0;i<4;i++) {
    vec4 event=hitEvents[i];
    if(event.y<=0.0||event.x>.62) continue;
    vec2 q=p-hitCentre(event);float d=length(q),radius=.045+event.x*1.4;
    float width=.045+event.x*.035;
    float front=max(0.0,1.0-abs(d-radius)/width);
    float wake=max(0.0,1.0-abs(d-radius+width*1.65)/(width*1.3));
    float envelope=pow(max(0.0,1.0-event.x/.62),1.5);
    pressure+=q/max(d,.08)*(front*front-wake*wake*.45)*event.y*envelope;
  }
  return pressure;
}
vec3 hitInk(vec2 p,vec2 f,float fine,vec4 event,vec3 pigment,float mode) {
    float band=event.w;
    float life=band<.5?.62:band<1.5?.34:.21;
    if(event.y<=0.0||event.x>life) return vec3(0);
    float age=event.x;
    float envelope=event.y*pow(max(0.0,1.0-age/life),band<1.5?1.8:1.4);
    if(mode>2.5&&mode<3.5) {
      float angle=event.z-PI+.45*sin(time*.19+event.z)+age*.8;
      vec2 q=rot(angle)*(p-hitCentre(event));
      float reach=.07+age*(band<.5?1.2:band<1.5?2.0:3.0);
      float width=band<.5?.0028:band<1.5?.0016:.0008;
      float extent=smoothstep(-.003,.009,q.x)*(1.0-smoothstep(reach-.025,reach,q.x));
      float trunk=hitLine(q.y-sin(q.x*35.0+event.z)*.009,width);
      float forks=hitLine(q.y-q.x*.20,width)+hitLine(q.y+q.x*.20,width);
      float fan=hitLine(sin(atan(q.y,max(q.x,.008))*mix(18.0,48.0,fine)),.035);
      fan*=1.0-smoothstep(q.x*.22+.002,q.x*.22+.012,abs(q.y));
      float teeth=.25+.75*pow(.5+.5*cos(q.x*(band<1.5?100.0:230.0)+event.z),5.0);
      vec3 ink=sceneAccent(mode,pigment);
      return ink*(trunk*.8+forks*teeth+fan*(band<1.5?.3:.9))*extent*envelope*3.2;
    }
    if(mode>4.5) {
      vec2 q=p-hitCentre(event);float radius=length(q);
      float angle=atan(q.y,q.x+.000001)+age*(band<.5?-2.0:3.0);
      float front=band<.5?.045+age*1.4:band<1.5?.055+age*2.2:.10+age*3.5;
      float support=band<.5?.0025+age*.003:band<1.5?.065+age*.10:.17;
      float area=max(0.0,1.0-abs(radius-front)/support);
      float spin=angle*(band<.5?7.0:band<1.5?13.0:31.0)+radius*34.0+event.z;
      float shard=pow(.5+.5*sin(spin),band<.5?4.0:12.0);
      float needles=hitLine(sin(spin*3.0+radius*mix(80.0,240.0,fine)),.035);
      float crest=hitLine(radius-front,band<.5?support:.0015);
      vec3 ink=sceneAccent(mode,pigment);
      float sector=smoothstep(.05,.48,sin(angle*3.0+event.z+age*4.0));
      return (ink*(crest*(.08+shard*1.4)+area*shard*(.35+needles*1.6))+
        vec3(.08,.55,1.0)*area*needles*shard*.35)*sector*envelope*2.9;
    }
    if(mode>.5&&mode<1.5) {
      // Travelling packets excite the signal conduits inside image memory.
      // A separate display-space trace would float above the returning video.
      return vec3(0);
    }
    // Signal instruments write phase resets and signal faults into the image.
    // Native rasterisation uses the same travel and support bounds (video.cpp).
    if((mode>1.5&&mode<2.5)||(mode>3.5&&mode<4.5)) {
      bool crossed=mode>3.5;
      float angle=crossed?PI*.5+.35*sin(event.z):(event.z-PI)*.38;
      vec2 q=rot(angle)*(p-hitCentre(event));
      float travel=age*(crossed?.70:.90)*(sin(event.z)>.0?1.0:-1.0);
      float width=band<.5?.009:band<1.5?.0035:.0013;
      float bend=sin(q.x*7.0+event.z+age*4.0)*(crossed?.038:.018);
      float ribbon=hitLine(q.y-travel+bend,width);
      float carrier=q.x*mix(75.0,230.0,fine)+f.y*.055+event.z;
      if(crossed) carrier+=sin(q.x*23.0+f.x*.12+event.z)*5.0;
      float comb=hitLine(sin(carrier),.06);
      float gate=.25+.75*step(.30,fract(q.x*7.0+event.z));
      float tear=band<.5?ribbon*(.2+.8*pow(.5+.5*cos(carrier*.31),4.0)):
        ribbon*gate*(band<1.5?.45+comb:comb*2.2);
      vec3 ink=sceneAccent(mode,pigment);
      return ink*tear*envelope*(band<.5?2.3:4.0);
    }
    vec2 q=p-hitCentre(event),axis=vec2(cos(event.z),sin(event.z));
    float d=length(q),mark=0.0,beads=0.0;
    if(band<.5) {
      float radius=.045+age*1.4;
      float crest=hitLine(d-radius,.0025+age*.003);
      float ribs=pow(.5+.5*cos(dot(q,axis)*48.0+f.y*.35),5.0);
      float broken=.25+.75*smoothstep(-.5,.5,sin(f.x*.6+f.y*.4+event.z));
      mark=crest*(.07+ribs*1.6)*broken;
    } else if(band<1.5) {
      float radius=.055+age*2.2;
      float area=max(0.0,1.0-abs(d-radius)/(.065+age*.10));
      float spokes=hitLine(sin(dot(q,axis)*mix(35.0,80.0,fine)+f.x*.55),.035);
      float fringe=hitLine(d-radius,.0035+age*.002);
      float broken=smoothstep(-.25,.55,sin(f.x*.7+f.y*.4+event.z));
      mark=area*area*spokes*1.2+fringe*(.06+spokes*.65)*broken;
      beads=area*pow(.5+.5*cos(f.y*17.0+event.z),20.0)*spokes;
    } else {
      float radius=.10+age*3.5;
      float area=max(0.0,1.0-abs(d-radius)/.17);
      float needle=hitLine(sin(f.x*mix(12.0,33.0,fine)+f.y*4.5+event.z),.025);
      beads=pow(.5+.5*cos(f.y*mix(13.0,37.0,fine)+event.z),20.0)*needle;
      mark=area*(needle*.8+beads*2.2);
    }
    float angle=atan(q.y,q.x+.000001);
    if(mode>4.5) {
      float rays=pow(.5+.5*cos(angle*53.0+event.z),16.0);
      mark*=.1+rays*2.0;beads*=rays;
    }
    pigment=sceneAccent(mode,pigment);
    return (pigment*mark+sceneAccent(mode,pigment.gbr)*beads*.65)*envelope*2.7;
}
vec3 percussionInk(vec2 p,vec2 f,float fine,float mode) {
  vec3 ink=vec3(0);
  for(int i=0;i<12;i++) {
    ink+=hitInk(p,f,fine,hitEvents[i],hitColours[i].rgb,mode);
  }
  return ink;
}
)GLSL";
inline const std::string feedbackFragment=std::string(R"GLSL(#version 330
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0,flowTexture,delayTexture,spectrumTexture,radianceTexture;
uniform float time,aspect,delta;
uniform vec4 audio,character,controls,colour,transients,lowBands,highBands;
)GLSL")+sceneFunctions+reactiveFunctions+R"GLSL(
vec2 mirrorUV(vec2 uv) {return 1.0-abs(mod(uv,2.0)-1.0);}
vec2 phases(vec2 uv) {return texture(flowTexture,clamp(uv,.001,.999)).rg;}
float spectrum(float x) {return texture(spectrumTexture,vec2(clamp(x,0.0,1.0),.5)).r;}
float line(float x,float width) {
  float aa=max(fwidth(x),.0008);
  return 1.0-smoothstep(width,width+aa,abs(x));
}
float hash(vec2 p) {return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
// Travelling excitation follows the curved signal coordinate. It is written
// into memory, where the returning video can split, stretch and recolour it.
vec4 signalPackets(float along,float tint,float aa) {
  vec4 packet=vec4(0);
  for(int i=0;i<6;i++) {
    int band=i/2,index=band*4+i%2;vec4 event=hitEvents[index];
    float life=band==0?1.1:band==1?.8:.55;
    if(event.y<=0.0||event.x>life) continue;
    float travel=abs(along-.4*sin(event.z+tint*5.0))-event.x*(band==0?.65:band==1?1.2:1.8);
    float width=max(band==0?.040:band==1?.020:.009,aa*1.6);
    float x=travel/width,envelope=max(0.0,1.0-event.x/life);
    float energy=event.y*envelope*envelope*(exp(-x*x)+.18*exp(-x*x*.08));
    vec3 ink=sceneAccent(1.0,hitColours[index].rgb);
    packet+=vec4(ink,1)*energy;
  }
  return packet;
}
vec4 source(vec2 p,vec2 f,float mode) {
  float r=length(p),a=atan(p.y,p.x+.000001),t=time*.23;
  float detail=mix(3.0,13.0,colour.y);
  float bass=audio.x,mid=audio.y,air=audio.z;
  vec3 c=vec3(0);float coverage=0.0;
  if(mode<.5) {
    // Continuous, asymmetric liquid sheets: the original analogue instrument.
    float phase=f.x*.27+f.y*.19+colour.x+character.x*.16;
    float front=.5+.5*sin(f.x*1.35+sin(f.y*.8)*(1.1+lowBands.w*.1));
    float rear=.5+.5*cos(f.y*1.65-f.x*.48);
    float crease=pow(front,mix(3.0,16.0,colour.y));
    float silk=pow(.5+.5*cos(f.x*detail+sin(f.y*2.0)),22.0);
    float lace=pow(.5+.5*sin(f.y*detail*1.7-f.x*3.0),38.0);
    c=palette(phase)*(crease*.39+front*.028);
    c+=palette(phase+.38)*(pow(rear,10.0)*.27+silk*.22*(.3+rear+highBands.y*.18));
    c+=palette(phase+.13)*lace*crease*(.6+air*.7+transients.z*.75);
    c*=.55+1.3*spectrum(fract(f.y*.15));
  } else if(mode<1.5) {
    // Hollow conduits are neighbouring voltage contours, not lit cylinders.
    // No surface normal, depth stack or opaque mask is inserted over memory.
    vec3 memory=texture(texture0,p/vec2(aspect,1.0)+.5).rgb;
    vec3 delayed=texture(delayTexture,mirrorUV((p+vec2(.018,-.012))/vec2(aspect,1.0)+.5)).rgb;
    float voltage=(memory.r-memory.b)*.75+(delayed.g-delayed.r)*.45;
    float level=max(memory.r,max(memory.g,memory.b));
    float seedKey=.28+.72*(1.0-smoothstep(.12,.65,level));
    vec3 returnedInk=memory/(.15+level);
    for(int bank=0;bank<3;bank++) {
      float lane=float(bank),axis=syncAxis()+lane*.73+.12*sin(time*.13+lane*1.7);
      vec2 q=rot(axis)*bassCoordinates(p);
      float phase=q.y*15.0+sin(q.x*3.4+time*.27+lane*2.0)*2.8;
      phase+=sin(q.x*7.3-time*.21+lane)*.55+voltage*.85;
      float folded=mod(phase+PI*.5,PI)-PI*.5;
      float aa=max(fwidth(phase),.012);
      float width=.095+.028*sin(q.x*2.1+lane+time*.17)+bass*.020;
      float positive=1.0-smoothstep(.018,.018+aa,abs(folded-width));
      float negative=1.0-smoothstep(.018,.018+aa,abs(folded+width));
      float veil=exp(-pow(folded/width,2.0)*2.0);
      float broken=.12+.88*smoothstep(-.35,.55,sin(q.x*3.1-lane*1.7+phase*.17+voltage*2.4));
      float tint=lane*.29+q.x*.065+colour.x+time*.018+voltage*.15;
      vec3 ink=mix(scenePalette(1.0,tint),returnedInk.gbr,.28);
      vec3 opposite=scenePalette(1.0,tint+.34);
      vec4 packets=signalPackets(q.x,lane*.31,aa/15.0);
      float carrier=sin(q.x*(65.0+colour.y*90.0)+voltage*5.0+lane);
      float fringe=1.0-smoothstep(.045,.045+max(fwidth(carrier),.012),abs(carrier));
      vec3 trace=(ink*positive+opposite*negative)*(.70+broken*1.8);
      trace+=ink*(veil*.045+(positive+negative)*fringe*(.035+min(packets.a,1.0)*.30));
      trace+=packets.rgb*((positive+negative)*3.8+veil*.30);
      c+=trace*seedKey;
    }
    // A small free-running voltage keeps the circuit alive in silence; audio
    // excitation takes over without a permanently bright geometric layer.
    c*=mix(2.6,1.05,smoothstep(.005,.08,audio.x+audio.y+audio.z));
  } else if(mode<2.5) {
    // A small cross-patched oscillator stimulus. Its repeated, keyed image
    // returns create the composition; no flower, object or radial mask exists.
    float u=p.x*.78+p.y*.63,v=p.y*.9-p.x*.42;
    float sweep=u*10.0+sin(v*4.0+time*.19)*.8-time*.32+f.x*.025;
    float pressure=smoothstep(.01,1.15,bass);
    sweep+=sin(v*2.6+time*.15)*bassDrive.x*.28;
    float crossing=v*12.0+sin(u*3.0-time*.13)*.9+time*.22;
    // Bass also opens the oscillator pulse width and luminance key. Sparse
    // idle returns can then become a broad deformation under a sustained note.
    float ribbon=pow(.5+.5*sin(sweep),mix(18.0,8.0,pressure));
    float window=smoothstep(.43-pressure*.18,.72-pressure*.15,.5+.5*cos(crossing*.37));
    float edge=line(sin(sweep),.018)*window;
    float cross=pow(.5+.5*cos(crossing),30.0)*(1.0-window);
    c=scenePalette(mode,sweep*.055+colour.x*.55)*(ribbon*window*.18+edge*.12);
    c+=scenePalette(mode,crossing*.06+colour.x*.55+.42)*cross*(.10+mid*.04);
    c*=1.0+pressure*.8;
  } else if(mode<3.5) {
    // Actual stereo XY strokes are injected as batched beam geometry after
    // this pass. They recirculate through the same half-float image memory.
    c=vec3(0);
  } else if(mode<4.5) {
    // Two cross-patched video oscillators. Returning red/blue voltages
    // phase-modulate the carriers, so each generation changes the next one.
    vec2 q=rot(.32+.18*sin(time*.083))*p;
    vec3 memory=texture(texture0,p/vec2(aspect,1.0)+.5).rgb;
    vec3 delayed=texture(delayTexture,mirrorUV((p+vec2(.035,-.021))/vec2(aspect,1.0)+.5)).rgb;
    float u=q.x*(12.0+detail*.40)+sin(q.y*3.0-time*.17)*1.8;
    float v=q.y*(15.0+detail*.55)+sin(q.x*2.7+time*.13)*1.5;
    float aPhase=u+sin(v+time*.21)*(2.4+bass*.45)+(memory.r-memory.b)*5.5+delayed.g*2.0;
    float bPhase=v+sin(u-time*.18)*2.9+(memory.b-memory.g)*5.0-delayed.r*1.6;
    float product=sin(aPhase)*cos(bPhase);
    float voltage=.5+.5*sin(aPhase+bPhase*.65+product*3.3);
    float bands=pow(voltage,10.0);
    float beatPattern=line(sin(aPhase-bPhase),.035);
    float fine=line(sin(aPhase*2.0+bPhase*(1.7+air*.13)),.025);
    float tint=aPhase*.035-bPhase*.019+colour.x+time*.009;
    c=scenePalette(mode,tint)*(bands*.18+beatPattern*.12);
    c+=scenePalette(mode,tint+.41)*fine*(.06+air*.07)*(.25+bands);
  } else {
    // A sparse bent oscillator ribbon feeds a moving polar return circuit.
    // Depth comes from recursively transformed video, rather than fixed struts.
    vec2 q=p-horizonCentre();
    q=rot(.38*sin(time*.37))*q;
    q.y*=1.10+.24*sin(time*.31);
    float radius=max(length(q),.018),angle=atan(q.y,q.x+.000001);
    float depth=-log(radius);
    float spiral=angle*3.0+depth*(6.0+sin(time*.21)*1.4)-time*1.05;
    float bend=sin(angle*2.0-time*.43)*.8+sin(depth*4.0+time*.37)*.6;
    float carrier=spiral+bend;
    float ribbon=pow(.5+.5*sin(carrier),24.0);
    float slit=line(sin(carrier*1.7-depth*2.0),.035);
    float gate=smoothstep(-.25,.30,sin(angle*2.0+depth*3.0+time*.41));
    float fine=line(sin(depth*(25.0+detail*1.2)+angle*5.0-time*2.0),.025);
    float tint=depth*.23+angle*.10+colour.x+time*.017;
    c=scenePalette(mode,tint)*(ribbon*gate*.33+slit*gate*.16);
    c+=vec3(.09,.39,1)*fine*ribbon*(.24+air*.30);
    c+=vec3(1,.35,.055)*line(sin(carrier),.018)*gate*.16;
    c*=smoothstep(.045,.095,radius)*(1.0-smoothstep(.75,1.3,radius));
  }
  return vec4(c*(.65+bass*.6+mid*.35+air*.25),coverage);
}
// Sampling coordinates describe each instrument's memory, not just its source.
vec2 transport(vec2 p,vec2 f,float mode,float stepScale) {
  float r=length(p),warp=controls.z;
  vec2 q=p;
  if(mode<.5) {
    float sectors=2.0+floor(controls.w*10.0),sector=2.0*PI/sectors;
    float angle=atan(p.y,p.x+.000001);
    float folded=abs(mod(angle+sector*.5+time*.035,sector)-sector*.5);
    q=mix(p,vec2(cos(folded),sin(folded))*r,smoothstep(.25,1.0,controls.w)*.5);
    float bend=(bassDrive.x*.014+bassDrive.y*.008)*sin(p.y*3.5+f.x*.13)*stepScale;
    bend+=bassDrive.z*.42*sin(p.y*3.1+time*.17);
    q=rot((.0012+.006*warp*sin(time*.11))*stepScale+bend)*q;
    q*=vec2(exp(bassDrive.w*.32),exp(-bassDrive.z*.23));
    q*=pow(1.0035+warp*.004+bassDrive.x*.004,stepScale);
    vec2 velocity=vec2(sin(f.y*.9+time*.1),cos(f.x*.85-time*.12));
    velocity+=vec2(-p.y,p.x)*(sin(f.x*.4-f.y*.6)*.5+character.y*transients.w*.25);
    q+=velocity*(.0007+warp*.0055+bassDrive.x*.007)*stepScale;
    q+=vec2(sin(p.y*4.5+time*.21),sin(p.x*3.8-time*.19))*bassDrive.z*.13;
    q+=kickPressure(p)*.021*stepScale+p/max(r,.12)*bassDrive.y*.0035*stepScale;
  } else if(mode<1.5) {
    vec3 memory=texture(texture0,p/vec2(aspect,1.0)+.5).rgb;
    vec2 voltage=vec2(memory.r-memory.b,memory.g-memory.r);
    float axis=syncAxis();q=rot(axis)*p;
    // Returning colour steers an uneven drag field. Old pipe impressions
    // stretch into brush-like ribbons and curls before the next return.
    q=rot((.0035*sin(time*.31)+voltage.x*.003)*stepScale)*q;
    q*=vec2(pow(1.004+warp*.004,stepScale),pow(.994,stepScale));
    vec2 drag=vec2(sin(q.y*5.0-time*.41+voltage.x*4.0),
      cos(q.x*4.7+time*.34+voltage.y*4.0));
    vec2 curl=vec2(-q.y,q.x)*sin(q.x*3.0-q.y*4.0+time*.21+voltage.x*3.0);
    q+=(drag*(.0015+warp*.0045)+curl*(.0025+warp*.005))*stepScale;
    q.x+=(.0015+sin(q.y*3.1-time*.17+voltage.y*2.0)*bassDrive.x*.003)*stepScale;
    q.x+=sin(q.y*4.1+time*.2+voltage.x)*bassDrive.z*.62;
    q.y+=sin(q.x*4.8-time*.3+voltage.y)*bassDrive.w*.44;
    q*=vec2(exp(bassDrive.w*.30),exp(-bassDrive.z*.24));
    q=rot(-axis)*q;
    q+=kickPressure(p)*.030*stepScale;
  } else if(mode<2.5) {
    vec3 memory=texture(texture0,p/vec2(aspect,1.0)+.5).rgb;
    vec2 anchor=vec2(.18*sin(time*.071),.13*cos(time*.053));
    q=(p-anchor)*vec2(pow(1.008+warp*.005,stepScale),pow(.995,stepScale))+anchor;
    // Sustained bass shears the whole keyed return, including dim image
    // generations. Its motion must not depend solely on the attack impulse.
    q.x+=p.y*(.004+warp*.003+bassDrive.x*.022)*stepScale;
    q.y-=(.0015+bassDrive.y*.008)*stepScale;
    q+=vec2(memory.r-memory.b,memory.g-memory.r)*(.001+warp*.003+bassDrive.x*.012)*stepScale;
    q+=vec2(sin(p.y*6.0+time*.17),sin(p.x*4.0-time*.13))*bassDrive.z*.19;
    q*=vec2(exp(bassDrive.w*.20),exp(-bassDrive.z*.17));
    q+=kickPressure(p)*.015*stepScale;
  } else if(mode<3.5) {
    vec3 memory=texture(texture0,p/vec2(aspect,1.0)+.5).rgb;
    float torque=(.0025+warp*.004+bassDrive.x*.005)*stepScale;
    torque+=bassDrive.z*.30*sin(p.y*4.0+time*.17);
    q=rot(torque)*p;
    q*=vec2(pow(.992-warp*.003,stepScale),pow(1.003+warp*.002,stepScale));
    q.x+=p.y*.008*sin(time*.19)*stepScale;
    q+=vec2(sin(p.y*7.0+memory.g*3.0),cos(p.x*6.0-memory.b*3.0))*(.0006+bassDrive.x*.003)*stepScale;
    q*=vec2(exp(bassDrive.w*.28),exp(-bassDrive.z*.25));
    q+=kickPressure(p)*.022*stepScale;
  } else if(mode<4.5) {
    vec3 memory=texture(texture0,p/vec2(aspect,1.0)+.5).rgb;
    vec2 voltage=vec2(memory.r-memory.b,memory.g-memory.r);
    vec2 carrier=vec2(sin(p.y*7.0+voltage.x*6.0+time*.12),
      sin(p.x*6.0+voltage.y*6.0-time*.15));
    q+=carrier*(.0008+warp*.003+bassDrive.x*.005)*stepScale;
    q+=vec2(sin(p.y*5.0+voltage.y*3.0),cos(p.x*4.0-voltage.x*3.0))*bassDrive.z*.23;
    q*=vec2(exp(bassDrive.w*.23),exp(-bassDrive.z*.18));
    q=rot(.0012*sin(time*.11)*stepScale)*q;
    q+=kickPressure(p)*.018*stepScale;
  } else {
    vec2 centre=horizonCentre();vec2 z=p-centre;
    float radius=max(length(z),.025),angle=atan(z.y,z.x+.000001);
    vec3 memory=texture(texture0,p/vec2(aspect,1.0)+.5).rgb;
    float differential=memory.r-memory.b;
    float twist=(.008+warp*.012+bassDrive.x*.017)/(1.0+radius*2.0);
    // Opposing angular sectors counter-rotate, pulling each old frame into
    // folds and curls rather than a uniform spinning tunnel.
    twist*=sin(angle*2.0+time*.37)*1.20+.45;
    twist+=differential*.009+bassDrive.z*.68*sin(radius*5.0+angle*2.0);
    z=rot(twist*stepScale)*z;
    float collapse=.995+sin(angle*3.0+time*.41)*.013-warp*.008-bassDrive.y*.014;
    z*=pow(collapse,stepScale);
    z*=vec2(exp(bassDrive.w*.50),exp(-bassDrive.z*.42));
    z+=vec2(sin(angle*3.0+time*.43),cos(angle*2.0-time*.31))*(.0008+warp*.002)*stepScale;
    z+=z/radius*sin(radius*19.0-angle*3.0)*bassDrive.w*.22;
    q=z+centre+kickPressure(p)*.030*stepScale;
  }
  return q;
}
// retention ceiling, delayed return, channel circulation, chromatic separation.
vec4 memoryProfile(float mode) {
  if(mode<.5) return vec4(.992,1,1,1);
  if(mode<1.5) return vec4(.998,1.65,.23,1.15);
  if(mode<2.5) return vec4(.996,1.7,.45,.65);
  if(mode<3.5) return vec4(.996,1.3,0,.16);
  if(mode<4.5) return vec4(.994,.85,.14,1.25);
  return vec4(.994,1.1,.07,.65);
}
// Distinct return paths operate on the actual previous/delayed image. The
// luminance and chroma of that image become control voltages for the next one.
vec3 returnSignal(vec3 memory,vec2 p,float mode,float stepScale) {
  if(mode<.5) {
    // Release common-mode energy before successive liquid returns wash out
    // their colour. The circulating chromatic component keeps its fine folds.
    vec3 veil=texture(radianceTexture,mirrorUV(p/vec2(aspect,1.0)+.5)).rgb;
    float activity=smoothstep(.005,.10,audio.x+audio.y+audio.z);
    vec3 returned=max(vec3(0),memory-veil*.085*activity*stepScale);
    float neutral=min(returned.r,min(returned.g,returned.b));
    return max(vec3(0),returned-vec3(neutral*.065+.0008)*stepScale);
  }
  if(mode>.5&&mode<1.5) {
    float activity=smoothstep(.005,.08,audio.x+audio.y+audio.z);
    float luma=dot(memory,vec3(.299,.587,.114));
    float axis=syncAxis();vec2 q=rot(axis)*p;
    vec2 tapped=q*vec2(.90,1.07);
    tapped.y+=.065*sin(q.x*4.5+time*.33);
    tapped.x+=.052*cos(q.y*5.0-time*.27);
    vec2 uv=rot(-axis)*tapped/vec2(aspect,1.0)+.5;
    vec2 split=vec2(.004+controls.z*.004+transients.y*.012,.002+transients.z*.006);
    vec3 centre=texture(texture0,mirrorUV(uv)).rgb;
    vec3 slipped=vec3(texture(texture0,mirrorUV(uv+split)).r,
      centre.g,texture(texture0,mirrorUV(uv-split)).b);
    // Drag pigment along the actual returning contours, rather than adding
    // a decorative texture. These smeared taps re-enter image memory.
    vec2 pixel=vec2(2.0)/vec2(textureSize(texture0,0));
    vec3 right=texture(texture0,mirrorUV(uv+vec2(pixel.x,0))).rgb;
    vec3 above=texture(texture0,mirrorUV(uv+vec2(0,pixel.y))).rgb;
    vec2 gradient=vec2(dot(right-centre,vec3(.299,.587,.114)),dot(above-centre,vec3(.299,.587,.114)));
    vec2 brush=vec2(-gradient.y,gradient.x)/max(length(gradient),.012);
    brush*=.0015+controls.y*.008;
    vec3 dragged=(texture(texture0,mirrorUV(uv+brush)).rgb+
      texture(texture0,mirrorUV(uv-brush*.65)).rgb)*.5;
    slipped=mix(slipped,dragged,.20+controls.y*.38);
    vec2 echoPoint=rot(-.23-.15*sin(time*.19))*p*vec2(.78,1.16)+vec2(.055,-.035);
    echoPoint.y+=sin(echoPoint.x*4.0-time*.3)*.065;
    vec3 echo=texture(delayTexture,mirrorUV(echoPoint/vec2(aspect,1.0)+.5)).rgb;
    echo=mix(echo,echo.brg,.30);
    float echoLevel=max(echo.r,max(echo.g,echo.b));
    float key=smoothstep(.015,.16,max(luma,echoLevel*.45))*(1.0-smoothstep(.75,1.25,luma));
    vec3 difference=abs(slipped-echo*.72);
    vec3 overprint=max(slipped*.68,echo*.92)+difference*.28;
    vec3 curved=mix(slipped,overprint,key*(.45+controls.y*.30));
    float edge=length(right-centre)+length(above-centre);
    curved+=scenePalette(1.0,luma*.7+colour.x+.12)*min(edge,.35)*(.035+colour.y*.045);
    vec3 returned=mix(memory,curved,clamp((.11+controls.y*.24)*stepScale,0.0,.6));
    float edgeHold=smoothstep(.005,.10,edge);
    // Regenerate bright contours while releasing the flat colour between
    // them. Neutral accumulation must not turn the dense circuit into fog.
    returned*=1.0+(mix(-.006,-.020,activity)+edgeHold*.055)*stepScale;
    returned=mix(returned,smoothstep(vec3(mix(.005,.025,activity)),vec3(.82),returned),clamp(mix(.010,.060,activity)*stepScale,0.0,.2));
    float neutral=min(returned.r,min(returned.g,returned.b));
    returned-=max(vec3(0),returned-.85)*.06*stepScale;
    returned=max(vec3(0),returned-vec3(neutral*mix(.06,.20,activity)+mix(.00025,.0020,activity))*stepScale);
    // The preceding glow sets a local video-key threshold. Bright contours
    // regenerate; broad low-contrast returns release into real dark gaps.
    vec3 glow=texture(radianceTexture,mirrorUV(p/vec2(aspect,1.0)+.5)).rgb;
    float surround=max(glow.r,max(glow.g,glow.b));
    float peak=max(returned.r,max(returned.g,returned.b));
    // With no incoming audio the free-running video oscillator still sustains
    // itself. Continuous band energy tightens the key as excitation arrives.
    float threshold=mix(.045,.075,activity)+min(surround,.8)*mix(.65,.75,activity);
    float lightKey=smoothstep(threshold,threshold+.24,peak);
    // Let dim, displaced impressions survive between bright voltage edges.
    // AC coupling releases broad light without restoring a solid pipe surface.
    returned=max(vec3(0),returned-glow*mix(.003,.020,activity)*stepScale);
    return returned*pow(mix(mix(.992,.92,activity),1.006,lightKey),stepScale);
  }
  if(mode>1.5&&mode<2.5) {
    vec2 centre=vec2(.16*sin(time*.071),.12*cos(time*.053));
    vec2 tapped=(p-centre)*vec2(.87,1.09)+centre+vec2(.034,-.019);
    tapped=rot(.045*sin(time*.09))*tapped;
    vec3 delayed=texture(delayTexture,mirrorUV(tapped/vec2(aspect,1.0)+.5)).gbr;
    float luma=dot(delayed,vec3(.299,.587,.114));
    float key=smoothstep(.045,.16,luma)*(1.0-smoothstep(.50,.9,luma));
    float mixAmount=clamp((.035+controls.y*.16)*stepScale*key,0.0,.65);
    vec3 keyed=max(vec3(0),delayed-vec3(.018))*1.04;
    vec3 returned=mix(memory,keyed,mixAmount);
    vec2 texel=vec2(1.6)/vec2(textureSize(texture0,0));
    vec3 neighbour=texture(texture0,mirrorUV(p/vec2(aspect,1.0)+.5+texel)).rgb;
    float edge=length(memory-neighbour);
    returned+=scenePalette(mode,luma*.6+colour.x)*min(edge,.35)*(.025+colour.y*.04)*stepScale;
    // Regenerative keyer contrast is inside the loop: bright returns harden
    // into colour layers while low voltages leave holes for the next image.
    returned=mix(returned,smoothstep(vec3(.025),vec3(.65),returned),
      clamp((.06+controls.z*.035)*stepScale,0.0,.25));
    // Repeated channel exchange can accumulate neutral energy under a dense
    // soundtrack. Reject that common component before it erases the chroma key.
    float neutral=min(returned.r,min(returned.g,returned.b));
    returned=max(vec3(0),returned-vec3(neutral*.13*stepScale));
    // A black-level clamp makes keyed holes survive repeated generations.
    returned=max(vec3(0),returned*(1.0+.020*stepScale)-vec3(.0022*stepScale));
    float peak=max(returned.r,max(returned.g,returned.b));
    return returned/(1.0+max(peak-.95,0.0)*.025*stepScale);
  }
  if(mode>2.5&&mode<3.5) {
    vec2 tapped=rot(-.13+.10*sin(time*.19))*p*vec2(.86,1.10)+vec2(.042,-.028);
    vec3 echo=texture(delayTexture,mirrorUV(tapped/vec2(aspect,1.0)+.5)).rgb;
    float key=smoothstep(.025,.22,max(echo.r,max(echo.g,echo.b)));
    vec3 returned=mix(memory,max(memory*.30,echo*vec3(.85,1.0,.98)),
      clamp((.025+controls.y*.075)*stepScale*key,0.0,.3));
    vec2 pixel=vec2(2.5)/vec2(textureSize(texture0,0));
    vec2 uv=p/vec2(aspect,1.0)+.5;
    vec3 neighbour=(texture(texture0,mirrorUV(uv+pixel)).rgb+texture(texture0,mirrorUV(uv-pixel)).rgb)*.5;
    returned+=max(vec3(0),memory-neighbour)*.035*stepScale;
    returned-=min(returned,neighbour)*.065*stepScale;
    // Keep the field between beams black while regenerating their bright edges.
    return max(vec3(0),returned-vec3(.006*stepScale));
  }
  if(mode>3.5&&mode<4.5) {
    // Ring modulation and differentiated returns happen inside the feedback
    // circuit. These are two views of the previous video, not a drawn PCB.
    vec2 uv=p/vec2(aspect,1.0)+.5;
    vec2 voltage=vec2(memory.r-memory.b,memory.g-memory.b);
    vec2 separation=voltage*(.003+controls.z*.008)+vec2(.003,-.002);
    vec3 opposite=texture(texture0,mirrorUV(uv-separation)).brg;
    vec3 delayed=texture(delayTexture,mirrorUV(uv+separation*2.0)).rgb;
    vec3 difference=abs(memory-opposite);
    vec3 multiplied=memory*(.48+clamp(delayed.gbr,0.0,1.0)*.70);
    vec3 returned=mix(memory,multiplied+difference*.85,
      clamp((.035+controls.y*.06)*stepScale,0.0,.35));
    float edge=length(difference);
    returned+=scenePalette(mode,edge*.8+colour.x)*min(edge,.3)*.025*stepScale;
    // Separate spatial contrast from the channel difference: a flat blue
    // return has colour voltage but must not regenerate as a solid membrane.
    float contour=length(memory-opposite.gbr);
    returned*=1.0+(-.030+smoothstep(.012,.14,contour)*.040)*stepScale;
    // AC coupling subtracts the broad returning light while the fast fringes
    // survive. Dense audio can then charge edges without filling their gaps.
    vec3 veil=texture(radianceTexture,mirrorUV(uv)).rgb;
    float activity=smoothstep(.005,.10,audio.x+audio.y+audio.z);
    returned=max(vec3(0),returned-veil*(.025+activity*.050)*stepScale);
    float neutral=min(returned.r,min(returned.g,returned.b));
    return max(vec3(0),returned-vec3(neutral*.09+.0012)*stepScale);
  }
  if(mode>4.5) {
    vec2 centre=horizonCentre(),z=p-centre;
    float radius=max(length(z),.035),angle=atan(z.y,z.x+.000001);
    float bend=.12*sin(angle*3.0+time*.31)+.10*sin(time*.47);
    vec2 tapped=rot(.20+bend)*z*vec2(.78,1.08)+centre;
    vec3 echo=texture(delayTexture,mirrorUV(tapped/vec2(aspect,1.0)+.5)).rgb;
    vec2 counter=rot(-.13-bend)*z*1.16+centre+vec2(.04,-.025);
    vec3 opposite=texture(texture0,mirrorUV(counter/vec2(aspect,1.0)+.5)).brg;
    float key=smoothstep(.035,.24,max(echo.r,max(echo.g,echo.b)));
    float gate=.3+.7*smoothstep(-.2,.4,sin(angle*2.0-log(radius)*4.0+time*.51));
    vec3 folded=abs(echo-opposite*.48)*vec3(1.12,.94,1.08);
    vec3 returned=mix(memory,folded,clamp((.045+controls.y*.105)*stepScale*key*gate,0.0,.5));
    vec2 uv=p/vec2(aspect,1.0)+.5,pixel=vec2(1.8)/vec2(textureSize(texture0,0));
    vec3 neighbour=(texture(texture0,mirrorUV(uv+pixel)).rgb+texture(texture0,mirrorUV(uv-pixel)).rgb)*.5;
    returned+=max(vec3(0),memory-neighbour)*.12*stepScale;
    returned=mix(returned,smoothstep(vec3(.020),vec3(.90),returned),clamp(.045*stepScale,0.0,.2));
    float neutral=min(returned.r,min(returned.g,returned.b));
    return max(vec3(0),returned-vec3(neutral*.10+.0018)*stepScale);
  }
  return memory;
}
float aperture(vec2 p,float mode) {
  if(mode>4.5) {
    vec2 q=p-horizonCentre();float radius=length(q);
    float angle=atan(q.y,q.x+.000001);
    float opening=.047+.015*sin(angle*3.0+time*.73)+.012*sin(time*.41);
    opening+=bassDrive.y*.018;
    return smoothstep(opening,opening+.035,radius);
  }
  return 1.0;
}
void main() {
  vec2 uv=fragTexCoord,p=(uv-.5)*vec2(aspect,1.0);
  float r=length(p);
  vec2 f=phases(bassCoordinates(p)/vec2(aspect,1.0)+.5);
  float stepScale=clamp(delta*60.0,.1,3.0),warp=controls.z;
  vec4 profile=memoryProfile(scene);
  vec2 samplePoint=transport(p,f,scene,stepScale);
  bool changing=morph<.9999&&previousScene!=scene;
  if(changing) {
    samplePoint=mix(transport(p,f,previousScene,stepScale),samplePoint,morph);
    profile=mix(memoryProfile(previousScene),profile,morph);
  }
  vec2 previousUV=samplePoint/vec2(aspect,1.0)+.5;
  vec2 split=vec2(.00065+warp*.0016+transients.y*.0022,transients.z*.0008)*stepScale*profile.w;
  vec3 old;
  old.r=texture(texture0,mirrorUV(previousUV+split)).r;
  old.g=texture(texture0,mirrorUV(previousUV)).g;
  old.b=texture(texture0,mirrorUV(previousUV-split)).b;
  vec2 delayUV=mix(p,rot(.09*sin(time*.07))*p*(.90+.035*sin(time*.12)),profile.y);
  vec3 echo=texture(delayTexture,mirrorUV(delayUV/vec2(aspect,1.0)+.5)).rgb;
  float key=smoothstep(.07,.38,max(echo.r,max(echo.g,echo.b)));
  old=mix(old,echo,controls.y*.09*key*stepScale*profile.y);
  vec3 returned=returnSignal(old,p,scene,stepScale);
  if(changing) returned=mix(returnSignal(old,p,previousScene,stepScale),returned,morph);
  old=returned;
  // Channel cross-modulation circulates colour without runaway white clipping.
  old=mix(old,old.gbr,(.001+controls.y*.005)*stepScale*profile.z);
  old=max(vec3(0),old-vec3(min(old.r,min(old.g,old.b))*.018*stepScale));
  // A temporary extra release lets dark/graphic scenes emerge from bright
  // liquid memory during a live morph, without a destructive history clear.
  float release=changing?1.0-.13*sin(morph*PI):1.0;
  float pipes=(scene>.5&&scene<1.5)?1.0:0.0;
  float hold=pipes>.5?mix(.90,.998,controls.x):(scene>2.5&&scene<3.5)?mix(.91,.996,controls.x):mix(.78,memoryProfile(scene).x,controls.x);
  if(changing) {
    float previousPipes=(previousScene>.5&&previousScene<1.5)?1.0:0.0;
    float previousHold=previousPipes>.5?mix(.90,.998,controls.x):(previousScene>2.5&&previousScene<3.5)?mix(.91,.996,controls.x):mix(.78,memoryProfile(previousScene).x,controls.x);
    hold=mix(previousHold,hold,morph);
    pipes=mix(previousPipes,pipes,morph);
  }
  float retention=pow(hold*release,stepScale);
  vec4 injected=source(p,f,scene);
  if(changing) injected=mix(source(p,f,previousScene),injected,morph);
  float edge=smoothstep(1.5,.3,r);
  // All stimuli share the same transparent video memory. The signal conduits
  // never occlude their older generations or replace them with object surfaces.
  float stimulus=mix(1.0,.82,pipes);
  vec3 c=old*retention+injected.rgb*(1.0-retention)*3.1*stimulus;
  c+=injected.rgb*.012*stepScale*stimulus;
  vec3 ink=percussionInk(p,f,.30,scene);
  if(changing) ink=mix(percussionInk(p,f,.30,previousScene),ink,morph);
  c+=ink*.075*stepScale;
  float opening=aperture(p,scene);
  if(changing) opening=mix(aperture(p,previousScene),opening,morph);
  c*=opening;
  c*=.999;
  // Soft energy limit retains saturated contours in a half-float feedback loop.
  c=max(c,vec3(0));c=c/(1.0+max(c-1.5,vec3(0))*.15);
  finalColor=vec4(c*(.99+.01*edge),1);
}
)GLSL";
// One batched draw resolves thousands of actual XY beam segments. Texture
// coordinates span each beam's padded width, giving an antialiased phosphor core.
inline constexpr const char* vectorBeamFragment=R"GLSL(#version 330
in vec2 fragTexCoord;in vec4 fragColor;out vec4 finalColor;
void main() {
  float distance=abs(fragTexCoord.y*2.0-1.0);
  float core=1.0-smoothstep(.24,.24+max(fwidth(distance),.025),distance);
  float glow=exp(-distance*distance*9.0)*.20;
  finalColor=vec4(fragColor.rgb,fragColor.a*(core+glow));
})GLSL";
inline constexpr const char* blurFragment=R"GLSL(#version 330
in vec2 fragTexCoord;out vec4 finalColor;
uniform sampler2D texture0;uniform vec2 direction;uniform float extract;
vec3 tap(vec2 uv) {vec3 c=texture(texture0,uv).rgb;return c*mix(1.0,smoothstep(.18,.8,max(c.r,max(c.g,c.b))),extract);}
void main() {
  vec3 c=tap(fragTexCoord)*.227027;
  c+=(tap(fragTexCoord+direction*1.384615)+tap(fragTexCoord-direction*1.384615))*.316216;
  c+=(tap(fragTexCoord+direction*3.230769)+tap(fragTexCoord-direction*3.230769))*.070270;
  finalColor=vec4(c,1);
})GLSL";
inline const std::string postFragment=std::string(R"GLSL(#version 330
in vec2 fragTexCoord;out vec4 finalColor;
uniform sampler2D texture0,bloomTexture,flowTexture;uniform float exposure,time,aspect,detail,phase;
)GLSL")+sceneFunctions+reactiveFunctions+R"GLSL(
vec4 finishProfile(float mode) {
  if(mode<.5) return vec4(.22,.72,1.22,.96);
  if(mode<1.5) return vec4(.19,.0,1.16,1.16);
  if(mode<2.5) return vec4(.20,.0,1.23,1.22);
  if(mode<3.5) return vec4(.23,.0,1.12,1.10);
  if(mode<4.5) return vec4(.14,.0,1.22,1.06);
  return vec4(.20,.0,1.30,1.14);
}
void main() {
  vec2 p=(fragTexCoord-.5)*vec2(aspect,1.0);
  vec4 style=finishProfile(scene);
  if(morph<.9999&&previousScene!=scene) style=mix(finishProfile(previousScene),style,morph);
  vec3 c=texture(texture0,fragTexCoord).rgb;
  c+=texture(bloomTexture,fragTexCoord).rgb*style.x;
  if(style.y>.001) {
    vec2 f=texture(flowTexture,clamp(bassCoordinates(p)/vec2(aspect,1.0)+.5,.001,.999)).rg;
    float threadPhase=f.x*mix(8.0,22.0,detail)+sin(f.y*2.0);
    float width=max(fwidth(threadPhase),.015);
    float thread=1.0-smoothstep(.035,.035+width,abs(sin(threadPhase)));
    float folds=pow(.5+.5*sin(f.x*1.35+sin(f.y*.8)*1.1),6.0);
    float knots=pow(.5+.5*cos(f.y*19.0),24.0)*thread;
    c+=palette(f.x*.27+f.y*.19+phase)*(thread*folds*.12+knots*.19)*smoothstep(.02,.2,max(c.r,max(c.g,c.b)))*style.y;
  }
  float phosphor=(scene>2.5&&scene<3.5)?1.0:0.0;
  float raster=(scene>.5&&scene<1.5)?1.0:0.0;
  float keyed=(scene>1.5&&scene<2.5)?1.0:0.0;
  float crossed=(scene>3.5&&scene<4.5)?1.0:0.0;
  float horizon=scene>4.5?1.0:0.0;
  if(morph<.9999&&previousScene!=scene) {
    phosphor=mix((previousScene>2.5&&previousScene<3.5)?1.0:0.0,phosphor,morph);
    raster=mix((previousScene>.5&&previousScene<1.5)?1.0:0.0,raster,morph);
    keyed=mix((previousScene>1.5&&previousScene<2.5)?1.0:0.0,keyed,morph);
    crossed=mix((previousScene>3.5&&previousScene<4.5)?1.0:0.0,crossed,morph);
    horizon=mix(previousScene>4.5?1.0:0.0,horizon,morph);
  }
  c*=1.0-raster*.045*(.5+.5*cos(gl_FragCoord.y*PI));
  c+=(abs(dFdx(c))+abs(dFdy(c)))*keyed*(.20+detail*.35);
  // Fine comparator edges follow the returned video voltage itself. They are
  // regenerated after resampling rather than being painted on a geometric mask.
  float voltage=dot(c,vec3(.299,.587,.114));
  float comparator=sin(voltage*(27.0+detail*18.0)+phase*6.2831853);
  float edge=1.0-smoothstep(.025,.025+max(fwidth(comparator),.008),abs(comparator));
  c+=scenePalette(2.0,voltage*.7+phase)*edge*keyed*.13*smoothstep(.035,.12,voltage);
  // Resolve a fine difference-signal comparator at native resolution; it
  // follows the video channels rather than a separate decorative phase field.
  float differencePhase=(c.r-c.b)*(32.0+detail*30.0)+c.g*11.0+phase*6.2831853;
  float crossing=sin(differencePhase);
  float fringe=1.0-smoothstep(.025,.025+max(fwidth(crossing),.008),abs(crossing));
  c+=scenePalette(4.0,voltage+phase)*fringe*crossed*.12*smoothstep(.025,.10,voltage);
  float syncPhase=(c.r-c.b)*31.0+c.g*(14.0+detail*12.0)+phase*6.2831853;
  float syncCarrier=sin(syncPhase);
  float syncEdge=1.0-smoothstep(.025,.025+max(fwidth(syncCarrier),.008),abs(syncCarrier));
  c+=scenePalette(1.0,voltage*.8+phase)*syncEdge*raster*.045*smoothstep(.025,.13,voltage);
  c+=(abs(dFdx(c))+abs(dFdy(c)))*raster*.16;
  vec3 curlEdge=abs(dFdx(c))+abs(dFdy(c));
  c+=scenePalette(5.0,voltage*.9+phase)*max(curlEdge.r,max(curlEdge.g,curlEdge.b))*horizon*.50;
  vec3 beamEdge=abs(dFdx(c))+abs(dFdy(c));
  c+=vec3(.12,.28,.22)*max(beamEdge.r,max(beamEdge.g,beamEdge.b))*phosphor*.4;
  // Keep luminous signal cores separate from the recirculating colour veil.
  // This follows video luminance, without imposing a geometric lighting mask.
  float toe=.012+raster*.013-phosphor*.008+crossed*.013-horizon*.004;
  vec3 signalLight=max(c-vec3(toe),vec3(0));
  float lightLevel=dot(signalLight,vec3(.299,.587,.114));
  signalLight*=mix(.70,1.08,smoothstep(.08,.65,lightLevel));
  signalLight*=smoothstep(.025,.16,max(c.r,max(c.g,c.b)));
  float sculpt=.35+raster*.65+keyed*.10-phosphor*.20+crossed*.45-horizon*.15;
  c=mix(c,signalLight,sculpt);
  c*=mix(.7,2.8,exposure)*style.w;
  // Compress the peak as one light signal, retaining its channel ratios.
  // Independent channel clipping turns overlapping coloured returns pastel.
  float peak=max(c.r,max(c.g,c.b));
  vec3 chromaticLight=c/max(peak,.00001)*(1.0-exp(-peak*1.55));
  float chromaHold=.50+raster*.35+keyed*.05-phosphor*.20+crossed*.25+horizon*.10;
  c=mix(1.0-exp(-c*1.55),chromaticLight,chromaHold);
  float l=dot(c,vec3(.2126,.7152,.0722));c=mix(vec3(l),c,style.z);
  float gamma=1.08+raster*.42-keyed*.02-phosphor*.06+crossed*.02-horizon*.06;
  c=max(c,vec3(0));c=pow(c,vec3(gamma));
  c*=1.0-.12*dot(fragTexCoord-.5,fragTexCoord-.5);
  finalColor=vec4(clamp(c,0.0,1.0),1);
}
)GLSL";
// Native detail uses bounded rings or signal strips, avoiding a full-screen
// loop over every hit at Retina resolution. Each side of a morph draws its own
// bounds, so changing between rings and strips cannot clip the new geometry.
inline const std::string percussionFragment=std::string(R"GLSL(#version 330
out vec4 finalColor;
uniform sampler2D flowTexture;
uniform vec2 resolution;
uniform float time,aspect,eventScene,eventMix;
uniform vec4 singleEvent,singleColour;
)GLSL")+sceneFunctions+reactiveFunctions+R"GLSL(
void main() {
  vec2 uv=gl_FragCoord.xy/resolution;
  vec2 p=(uv-.5)*vec2(aspect,1.0);
  vec2 f=texture(flowTexture,clamp(bassCoordinates(p)/vec2(aspect,1.0)+.5,.001,.999)).rg;
  vec3 ink=hitInk(p,f,1.0,singleEvent,singleColour.rgb,eventScene)*eventMix;
  float coverage=max(ink.r,max(ink.g,ink.b));
  finalColor=vec4(ink/max(coverage,.00001),(1.0-exp(-coverage*1.1))*.94);
})GLSL";
}
