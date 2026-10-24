#version 450 core
layout(location=0) in vec3 aPosition;
layout(location=1) in vec2 aUV;
layout(location=2) in vec3 aNormal;
layout(location=3) in vec4 aColour;
uniform vec4 uCamera;
uniform vec4 uViewport;
uniform vec4 uObject;
uniform vec4 uMaterial;
uniform vec4 uControls;
uniform vec4 uLightRow0;
uniform vec4 uLightRow1;
uniform vec4 uLightRow2;
uniform vec4 uLightRow3;
uniform vec4 uScenePass;
out vec2 vUV;
out vec3 vWorld;
out vec3 vNormal;
out float vCrown;
out vec4 vShadowPos;
flat out float vParticlePhase;
vec3 mod289(vec3 x){return x-floor(x*(1./289.))*289.;}
vec2 mod289(vec2 x){return x-floor(x*(1./289.))*289.;}
vec3 permute(vec3 x){return mod289(((x*34.)+1.)*x);}
float snoise(vec2 v){
 const vec4 C=vec4(.211324865405187,.366025403784439,-.577350269189626,.024390243902439);
 vec2 i=floor(v+dot(v,C.yy)),x0=v-i+dot(i,C.xx);
 vec2 i1=x0.x>x0.y?vec2(1,0):vec2(0,1);
 vec4 x12=x0.xyxy+C.xxzz;x12.xy-=i1;i=mod289(i);
 vec3 p=permute(permute(i.y+vec3(0,i1.y,1))+i.x+vec3(0,i1.x,1));
 vec3 m=max(.5-vec3(dot(x0,x0),dot(x12.xy,x12.xy),dot(x12.zw,x12.zw)),0.);m=m*m;m=m*m;
 vec3 x=2.*fract(p*C.www)-1.,h=abs(x)-.5,ox=floor(x+.5),a0=x-ox;
 m*=1.79284291400159-.85373472095314*(a0*a0+h*h);
 vec3 g;g.x=a0.x*x0.x+h.x*x0.y;g.yz=a0.yz*x12.xz+h.yz*x12.yw;
 return 130.*dot(m,g);
}
void main(){
    vec3 p=aPosition;
    // MainCamera X=53.130104 degrees. Laboratory +Zsouth = -Unity Znorth:
    // source screenUp=.6Y+.8Z becomes our screenDown=.8Zsouth-.6Y.
    // One ground pixel is Z=.0125, an upright pixel Y=.01666667.
    float pin=pow(clamp(1.-aUV.y,0.,1.),2.);
    float gust=sin(uCamera.w*.85+p.z*.012)*.5+.5;
    if(uMaterial.x==2. && uControls.x>0.){
        if(uMaterial.w>0.){
            // Original 3DObject DXBC: local COLOR.r^3 anchors roots; phase
            // uses object translation rather than time alone.
            float phase=20.*(uObject.x+uObject.y-uObject.z)*.01+uCamera.w;
            float amplitude=uMaterial.y;
#ifdef LAB_SHADOW_PASS
            // Shadow-only batches keep per-instance phase and amplitude in
            // unused metadata G/B; original root R and crown A remain intact.
            if(uMaterial.w>1.5){phase=20.*aColour.g*.01+uCamera.w;amplitude=aColour.b;}
#endif
            float g=smoothstep(0.,1.,clamp(uControls.x,0.,1.));
            p.x+=sin(phase)*amplitude*g*pow(aColour.r,3.);
        }else{
            // DeformingGrass original UV.y weighting, with V inverted only
            // because the extracted PNGs have a top-left origin.
            float rootUV=clamp(1.-aUV.y,0.,1.);
            float x0=p.x*.01-aUV.x*(1./192.)*24.;
            float worldZ=-p.z*.01,s=1.;float q=.1*x0*worldZ;
            float wave=sin(x0*s+sin(q*s*s)+.77*worldZ*s+uCamera.w*3.);
            float amplitude=(uControls.x*.15+.025)*100.;
            float sharedGust=uControls.x*.5*snoise(vec2(uCamera.w/20.*.8,0.))*100.;
            p.x-=pow(rootUV,2.)*(amplitude*wave+uControls.x*20.+sharedGust);
        }
    }
    vec3 projected=p;
    if(uScenePass.x>.5)projected.y=2.*uScenePass.y-p.y;
    vec2 screen=vec2(projected.x-uCamera.x,(projected.z-uCamera.y)*.8-projected.y*.6)*uCamera.z;
    vec4 hp=vec4(p,1.);
    vShadowPos=vec4(dot(uLightRow0,hp),dot(uLightRow1,hp),dot(uLightRow2,hp),dot(uLightRow3,hp));
#ifdef LAB_SHADOW_PASS
    gl_Position=vShadowPos;
#else
    gl_Position=vec4(screen.x*2./uViewport.x,-screen.y*2./uViewport.y,
                     -(projected.z*.6+projected.y*.8)/4000.,1.);
    // Original game gives ground decoration a screen-preserving layer offset.
    // Keep the physical position unchanged, and resolve its coplanar depth tie.
    if(uMaterial.w>.5||uMaterial.x==1.||uMaterial.x==3.)gl_Position.z-=aColour.g;
#endif
    vUV=aUV;vWorld=p;vNormal=aNormal;
    vParticlePhase=uMaterial.x==4.?aColour.b:uObject.w;
    // The lab uses existing alpha metadata to select a single-layer crown
    // approximation; the game's separate fm two-layer blending is not imported.
    vCrown=uMaterial.x==2.&&uMaterial.w>0.?aColour.a:0.;
}
