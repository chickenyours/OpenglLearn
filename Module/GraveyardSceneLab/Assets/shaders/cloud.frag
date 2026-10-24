#version 450 core
uniform sampler2D uTexture;
uniform vec4 uCloudWorld;     // world minimum X,Z, world extent X,Z
uniform vec4 uCloudSettings;  // density, cutoff, opacity, edge range
uniform vec4 uCloudMotion;    // accumulated wind offset, seconds, wind direction XY
uniform vec4 uCloudNoiseInfo; // original volume width, height, depth, atlas columns
uniform vec4 uCloudAtlasInfo; // atlas width, height, tile pitch X,Y (one pixel padding)
in vec2 vUV;
out vec4 fragColour;

float noiseSlice(vec2 p,float slice){
  float depth=uCloudNoiseInfo.z;
  slice=mod(mod(slice,depth)+depth,depth);
  vec2 tile=vec2(mod(slice,uCloudNoiseInfo.w),floor(slice/uCloudNoiseInfo.w));
  // stbi row zero maps to v=0; the atlas preserves the native volume Y order.
  // Wrapped padding implements bilinear XY without bleeding between Z slices.
  vec2 uv=(tile*uCloudAtlasInfo.zw+1.+fract(p)*uCloudNoiseInfo.xy)/uCloudAtlasInfo.xy;
  return texture(uTexture,uv).r;
}
float volumeNoise(vec3 p){
  float z=fract(p.z)*uCloudNoiseInfo.z-.5;
  return mix(noiseSlice(p.xy,floor(z)),noiseSlice(p.xy,floor(z)+1.),fract(z));
}
void main(){
  vec2 world=uCloudWorld.xy+vUV*uCloudWorld.zw;
  vec2 nativeXZ=vec2(world.x,-world.y)*.01;
  vec2 wind=uCloudMotion.zw;
  wind=length(wind)>.0001?normalize(wind):vec2(-1,0);
  vec2 p=nativeXZ/30.+wind*uCloudMotion.x;
  float z=uCloudMotion.y*.01;
  // Recovered Custom/Clouds Shadow Procedural fragment DXBC, s0-o1062.
  // The volume data and erosion equation are original. Linear/repeating volume
  // sampling is a deliberate preview choice: saved texture Point/Clamp does not
  // establish the original shader's separate, unresolved inline sampler state.
  float cluster=clamp(volumeNoise(vec3(p*.819,z))+uCloudSettings.x-uCloudSettings.y,0.,1.);
  float detail=volumeNoise(vec3(p*1.42,z*1.7));
  float eroded=cluster*clamp((detail-(1.-cluster))/max(cluster,.001),0.,1.);
  float alpha=uCloudSettings.z*smoothstep(0.,max(uCloudSettings.w,.0001),eroded);
  fragColour=vec4(0,0,0,alpha);
}
