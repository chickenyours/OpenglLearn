#version 450 core
uniform sampler2D uTexture;
layout(binding=1) uniform sampler2D uDepth;
uniform vec4 uPost;
uniform vec4 uTint;
uniform vec4 uAO;
uniform vec4 uSceneInfo;
uniform vec4 uViewport;
in vec2 vUV;
out vec4 fragColour;
vec3 worldFromDepth(vec2 uv,float depth){
  float screen=(.5-uv.y)*uViewport.y/max(uSceneInfo.z,.01)+.8*uSceneInfo.y;
  float towardEye=(.5-depth)*8000.;
  return vec3(uSceneInfo.x+(uv.x-.5)*uViewport.x/max(uSceneInfo.z,.01),
              -.6*screen+.8*towardEye,.8*screen+.6*towardEye);
}
float contactOcclusion(vec2 uv,float depth){
  if(uAO.x<.5 || depth>.99999)return 1.;
  vec3 P=worldFromDepth(uv,depth);
  vec3 N=normalize(cross(dFdx(P),dFdy(P)));
  if(dot(N,vec3(0,.8,.6))<0.)N=-N;
  const vec2 K[12]=vec2[12](vec2(1,0),vec2(.5,.866),vec2(-.5,.866),vec2(-1,0),vec2(-.5,-.866),vec2(.5,-.866),vec2(.866,.5),vec2(0,1),vec2(-.866,.5),vec2(-.866,-.5),vec2(0,-1),vec2(.866,-.5));
  float occ=0.;float radius=max(uAO.z,1.);
  float worldRadius=radius/max(uSceneInfo.z,.1)*3.;
  for(int i=0;i<12;i++){
    vec2 offset=K[i]*radius*(i<6?.55:1.0)/uViewport.xy;
    float sd=texture(uDepth,uv+offset).r;
    if(sd>.99999)continue;
    vec3 delta=worldFromDepth(uv+offset,sd)-P;
    float dist=length(delta),height=dot(delta,N);
    float range=1.-smoothstep(worldRadius*.45,worldRadius,dist);
    occ+=smoothstep(.55,2.8,height)*range;
  }
  return clamp(1.-occ/12.*uAO.y*1.8,.35,1.);
}
void main(){
  vec3 c=texture(uTexture,vUV).rgb;
  vec2 px=1./vec2(textureSize(uTexture,0));
  float ao=contactOcclusion(vUV,texture(uDepth,vUV).r);
  if(uAO.w>.5){fragColour=vec4(vec3(ao),1);return;}
  c*=mix(vec3(.66,.72,.83),vec3(1),ao);
  if(uPost.y>0.){
    // Restrained bloom; remove the old fake screen-space sun stripes.
    vec3 bloom=vec3(0);
    const vec2 taps[8]=vec2[8](vec2(1,0),vec2(-1,0),vec2(0,1),vec2(0,-1),vec2(1,1),vec2(-1,1),vec2(1,-1),vec2(-1,-1));
    for(int i=0;i<8;i++){
      vec3 t=texture(uTexture,vUV+taps[i]*px*4.).rgb;
      bloom+=max(t-vec3(.66),vec3(0))*.032;
    }
    c+=bloom*uPost.y;
    // A small Gamma-space approximation of the recovered PPv2 settings:
    // saturation +7 / brightness +6 / contrast -10. No unproven active LUT.
    float luma=dot(c,vec3(.2126,.7152,.0722));
    c=mix(vec3(luma),c,1.07);
    c=(c-.5)*.90+.5;
    c*=1.06;
  }
  float vignette=1.-dot(vUV-.5,vUV-.5)*mix(.10,.025,clamp(uPost.z,0.,1.));
  c*=vignette;
  fragColour=vec4(clamp(c,0.,1.),1);
}
