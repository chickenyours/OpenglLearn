#version 450 core
uniform sampler2D uTexture;
layout(binding=1) uniform sampler2D uDirt;
layout(binding=2) uniform sampler2D uWaterNoise;
layout(binding=3) uniform sampler2D uFoamNoise;
layout(binding=4) uniform sampler2D uShadowMap;
layout(binding=5) uniform sampler2D uCloudMask;
layout(binding=9) uniform sampler2D uGroundDry;
layout(binding=10) uniform sampler2D uGroundShade;
uniform vec4 uCamera;
uniform vec4 uObject;
uniform vec4 uMaterial;
uniform vec4 uControls;
uniform vec4 uLighting;
uniform vec4 uTint;
uniform vec4 uEffects;
uniform vec4 uSunDir;
uniform vec4 uSunColor;
uniform vec4 uAmbient;
uniform vec4 uBacklight;
uniform vec4 uShadowParams;
uniform vec4 uLightOptions;
uniform vec4 uCloudWorld;
uniform vec4 uScenePass;
in vec2 vUV;
in vec3 vWorld;
in vec3 vNormal;
in float vCrown;
flat in float vParticlePhase;
in vec4 vShadowPos;
out vec4 fragColour;
float hash(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
float noise(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(hash(i),hash(i+vec2(1,0)),f.x),mix(hash(i+vec2(0,1)),hash(i+vec2(1)),f.x),f.y);}
float riverX(float z){return 420.+sin(z*.005)*72.+sin(z*.013)*19.;}
float pathX(float z){return 1030.+sin(z*.005+1.)*90.;}
float waveShape(float n){float n2=n*n;return n2+clamp(n2*n2,0.,1.);}
float waterGreen(vec2 uv){return texture(uWaterNoise,fract(uv)).g;}
float crownHash(vec2 q){vec2 a=50.*fract(q*.318310+vec2(.71,.113));return 2.*fract((a.x+a.y)*a.x*a.y)-1.;}
float crownNoise(vec2 p){vec2 i=floor(p),f=fract(p),u=f*f*(3.-2.*f);return mix(mix(crownHash(i),crownHash(i+vec2(1,0)),u.x),mix(crownHash(i+vec2(0,1)),crownHash(i+vec2(1)),u.x),u.y);}
float shadowDepth(vec2 uv){return dot(texture(uShadowMap,uv),vec4(1.,1./255.,1./65025.,1./16581375.));}
float shadowCompareLinear(vec3 s,vec2 dzduv,float bias,vec2 shift){
  float mapSize=max(uShadowParams.w,1.);
  vec2 p=(s.xy+shift/mapSize)*mapSize-.5;
  vec2 base=floor(p),f=fract(p);
  float result=0.;
  for(int y=0;y<2;y++)for(int x=0;x<2;x++){
    vec2 q=clamp((base+vec2(x,y)+.5)/mapSize,vec2(.5/mapSize),vec2(1.-.5/mapSize));
    float weight=(x==0?1.-f.x:f.x)*(y==0?1.-f.y:f.y);
    float receiver=s.z+dot(dzduv,q-s.xy)-bias;
    result+=weight*(receiver<=shadowDepth(q)?1.:0.);
  }
  return result;
}
float sunVisibility(vec3 N){
  if(uShadowParams.x<.5 || uLighting.y<.5)return 1.;
  vec3 clip=vShadowPos.xyz/max(vShadowPos.w,.00001);
  if(any(greaterThan(abs(clip),vec3(1.))))return 1.;
  vec3 s=clip*.5+.5;
  float bias=max(uShadowParams.y*.28,uShadowParams.y*(1.-max(dot(N,uSunDir.xyz),0.)));
  // Receiver-plane depth correction: each PCF tap lies at a slightly different
  // height on this projected plane. Comparing all taps to one constant depth
  // caused a diagonal self-shadow grid on the river and flat terrain.
  vec3 dx=dFdx(s),dy=dFdy(s);
  float det=dx.x*dy.y-dx.y*dy.x;
  vec2 dzduv=abs(det)>1e-10?vec2(dy.y*dx.z-dx.y*dy.z,dx.x*dy.z-dy.x*dx.z)/det:vec2(0);
  dzduv=clamp(dzduv,vec2(-2),vec2(2));
  float mapSize=max(uShadowParams.w,1.);
  vec2 centerUV=(floor(s.xy*mapSize)+.5)/mapSize;
  if(uShadowParams.z<.01)return s.z+dot(dzduv,centerUV-s.xy)-bias<=shadowDepth(centerUV)?1.:0.;
  // Compare first, then interpolate visibility. Unlike stepping a fixed set of
  // nearest taps, this changes continuously when wind crosses a shadow texel.
  float spread=max(0.,uShadowParams.z-1.);
  if(spread<.01)return shadowCompareLinear(s,dzduv,bias,vec2(0));
  return .25*(shadowCompareLinear(s,dzduv,bias,vec2(-spread,-spread))
             +shadowCompareLinear(s,dzduv,bias,vec2(spread,-spread))
             +shadowCompareLinear(s,dzduv,bias,vec2(-spread,spread))
             +shadowCompareLinear(s,dzduv,bias,vec2(spread,spread)));
}
vec3 surfaceNormal(vec3 albedo,float type){
  vec3 N=normalize(vNormal);
  // Selected original grass DXBC exports transformed localUp, not a bent normal.
  if(type==2. && uMaterial.w<.5)return vec3(0,1,0);
  if(vCrown>.5){
    // Original no-normal-map crown branch: TWO red-channel thresholds, not luminance.
    float k=.5*(step(.76,1.-albedo.r)+step(.58,1.-albedo.r));
    vec3 Nt=mix(vec3(0,0,1),normalize(vec3(0,1,1.43)),k);
    vec3 dx=dFdx(vWorld),dy=dFdy(vWorld);
    vec2 tx=dFdx(vUV),ty=dFdy(vUV);
    float det=tx.x*ty.y-tx.y*ty.x;
    if(abs(det)>.00000001){
      vec3 T=(dx*ty.y-dy*tx.y)/det;
      vec3 B=(-dx*ty.x+dy*tx.x)/det;
      T=normalize(T-N*dot(T,N));
      // PNG V was flipped on export; the source Unity bitangent points up the card.
      B=normalize(-B+N*dot(B,N));
      N=normalize(T*Nt.x+B*Nt.y+N*Nt.z);
    }
  }
  return N;
}
#include "water.glsl"
void main(){
  float type=uMaterial.x,time=uCamera.w;
  if(uScenePass.x>.5 && vWorld.y<uScenePass.y-.01)discard;
  if(type==7.){fragColour=renderWater(vWorld);return;}
  vec4 c=texture(uTexture,vUV)*uTint;
  if(vCrown>.5 && uControls.x>0.){
    vec2 p=40.*.3*fract(vUV)+vec2(.2*time*3.*(1.+uControls.x));
    vec2 n=vec2(crownNoise(p),crownNoise(p+vec2(1,0)));
    vec2 duv=.2*(.5+uControls.x*1.5)*vec2(n.x,(.5+uControls.x*.5)*n.y)/vec2(textureSize(uTexture,0));
    c=texture(uTexture,vUV+duv)*uTint;
  }
  if(type==1.){
    vec2 uv=fract(vWorld.xz/192.);
    vec4 grass=texture(uTexture,uv),dirt=texture(uDirt,fract(vWorld.xz/192.));
    float macro=noise(vWorld.xz*.004+vec2(8.2,1.7));
    float dryPatch=smoothstep(.59,.78,macro)*.34;
    float woodland=clamp(smoothstep(1330.,1580.,vWorld.x)+1.-smoothstep(300.,520.,vWorld.x)+1.-smoothstep(350.,470.,vWorld.z),0.,1.);
    float shadePatch=woodland*(.06+.12*noise(vWorld.xz*.009));
    grass.rgb=mix(grass.rgb,texture(uGroundDry,uv).rgb,dryPatch);
    grass.rgb=mix(grass.rgb,texture(uGroundShade,uv).rgb,shadePatch);
    // Broad terrain variation preserves the source tile. High-frequency grain
    // previously covered every patch of grass with a dirty, stippled pattern.
    float grain=noise(vWorld.xz*.0035)*.07;
    c=vec4(grass.rgb*(.97+grain),1.);
    float path=1.-smoothstep(42.,67.,abs(vWorld.x-pathX(vWorld.z))+noise(vWorld.xz*.055)*18.);
    if(vWorld.y>80.)path=max(path,1.-smoothstep(36.,54.,abs(vWorld.z-500.)+noise(vWorld.xz*.06)*11.));
    c.rgb=mix(c.rgb,dirt.rgb*.98,path*.92);
    float bank=abs(vWorld.x-riverX(vWorld.z));
    if(vWorld.y<2.){
      c.rgb=mix(c.rgb,dirt.rgb*vec3(1.02,1.04,.94),1.-smoothstep(91.,109.,bank));
      if(bank<90.){
        // This is now the real, depressed riverbed in the opaque colour/depth
        // target. A separate Y=0 water surface refracts it in a later RHI draw.
        vec3 sediment=dirt.rgb*vec3(.83,.94,.81);
        float pebbles=noise(floor(vWorld.xz*.19));
        c.rgb=sediment*(.86+.21*pebbles);
      }
    }
  }else if(type==3.){
    float strata=sin(vWorld.y*.19+noise(vWorld.xz*.023)*4.);
    vec3 rock=texture(uTexture,fract(vec2(vWorld.x*.012,vWorld.y*.012))).rgb;
    c=vec4(mix(vec3(.29,.24,.17),rock*.72,.50)*( .80+strata*.09+noise(vWorld.xy*.07)*.16),1.);
    float top=smoothstep(118.,158.,vWorld.y);
    c.rgb=mix(c.rgb,vec3(.23,.31,.16),top*.7);
  }else if(type==5.){
    if(c.a<.08)discard;
    fragColour=vec4(.035,.065,.07,c.a*uMaterial.z*uLighting.y);return;
  }else if(type==6.){
    float r=length(vUV-.5)*2.;float a=pow(max(0.,1.-r),2.);
    fragColour=vec4(uTint.rgb,a*uTint.a);return;
  }else{
    if(c.a<.5)discard;
    if(type==4.)c.rgb*=.82+sin(time*5.+vParticlePhase)*.12;
    else c.a=1.; // Alpha-tested scene geometry is opaque after the cutout.
  }
  if(uEffects.x>0.){
    vec3 low=vec3(.13,.24,.36),high=vec3(.9,.61,.18);
    c.rgb=mix(low,high,clamp(vWorld.y/180.,0.,1.));
    c.rgb*=.8+.2*step(.5,fract(vWorld.x*.03125)+fract(vWorld.z*.03125));
    fragColour=vec4(c.rgb,c.a);return;
  }
  float day=uLighting.x;
  vec3 N=surfaceNormal(c.rgb,type),L=normalize(uSunDir.xyz);
  float visibility=mix(1.,sunVisibility(N),uLighting.w);
  if(uEffects.z>.5){fragColour=vec4(vec3(visibility),1);return;}
  // Gamma build, PBR=0: preserve the original Lambert structure. Ambient upload
  // scale is calibrated: 2*Unity's native legacy CB is NOT proven to be 2*disk RGB.
  float ndl=max(dot(N,L),0.),back=max(dot(N,-L),0.);
  vec2 cloudUV=(vWorld.xz-uCloudWorld.xy)/uCloudWorld.zw;
  float cloudMask=all(greaterThanEqual(cloudUV,vec2(0)))&&all(lessThanEqual(cloudUV,vec2(1)))?texture(uCloudMask,cloudUV).a:0.;
  float cloudLight=1.-.35*cloudMask*uLightOptions.w;
  vec3 direct=uSunColor.rgb*visibility*ndl*uLightOptions.y;
  // The actual dither branch is guarded by length(light*atten)>.97. Outdoor
  // presets usually stay below it; no full-screen ordered dithering is invented.
  if(length(uSunColor.rgb*visibility)>.97){
    float grain=hash(floor(gl_FragCoord.xy/2.));
    direct*=mix(1.,mix(1.,grain,0.267)*1.12,clamp(length(uSunColor.rgb)-.97,0.,1.));
  }
  // Explicit preview calibration: saved Unity colours do not establish native
  // ambient CB scale. Brighter skylight keeps leaf clusters legible in shadow.
  float clearDay=smoothstep(.35,.85,day),skyFacing=clamp(N.y*.5+.5,0.,1.);
  vec3 skyTint=mix(vec3(.94,.98,1.06),vec3(.94,1.03,1.12),skyFacing);
  vec3 indirect=uAmbient.rgb*uLightOptions.x*mix(vec3(1),skyTint,clearDay)
               +uBacklight.rgb*back*uLightOptions.z;
  // A cloud attenuates direct sun most; preserve a diffuse sky floor instead of
  // multiplying all three lighting components by a grey veil.
  vec3 illumination=indirect*mix(1.,cloudLight,.3)+direct*cloudLight;
  // Local lamps are three-dimensional: the ground gets a pool, nearby vertical
  // surfaces get their own N dot L. This replaces the old flat Gaussian wash.
  vec3 lamps[4]=vec3[4](vec3(1160,245,590),vec3(1410,245,450),vec3(875,95,1030),vec3(630,95,840));
  for(int i=0;i<4;i++){
    vec3 delta=lamps[i]-vWorld;float d=length(delta);
    float fall=pow(clamp(1.-d/230.,0.,1.),2.)/(1.+d*d*.00009);
    float facing=.12+.88*max(dot(N,delta/max(d,1.)),0.);
    float flicker=.96+.025*sin(time*3.1+float(i)*1.7)+.015*sin(time*7.3+float(i));
    illumination+=vec3(1.,.48,.14)*fall*facing*flicker*(1.-day)*5.2;
  }
  c.rgb*=mix(vec3(1),illumination,uLighting.z);
  fragColour=c;
}
