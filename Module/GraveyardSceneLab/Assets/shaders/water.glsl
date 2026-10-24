// Water study: selected original Water Surface v2.17 High wave constants and
// depth/refraction/normal/cubemap evidence are documented in water-v3/findings.md.
// Beer-Lambert absorption, Snell screen refraction and planar scene reflection
// below are migration implementations, not a claim of identical original code.
// Include after scene uniforms + sunVisibility(), before main().
// The colour/depth inputs MUST be the opaque scene without this water surface.
layout(binding=6) uniform sampler2D uWaterSceneColor;
layout(binding=7) uniform sampler2D uWaterSceneDepth;
layout(binding=8) uniform sampler2D uWaterReflection;
uniform vec4 uWaterViewport;       // scene width,height,1/width,1/height
uniform vec4 uWaterReflectionInfo; // enabled,reflection width,height,water plane Y
uniform vec4 uWaterFeatures;       // depth tint,refraction,reflection,shore foam

float waterLinearGreen(vec2 p){
    // The preview uses point-filtered source textures. Interpolate this height
    // field explicitly so its derivatives form a continuous surface normal.
    ivec2 size=textureSize(uWaterNoise,0);
    vec2 q=fract(p)*vec2(size)-.5;
    ivec2 i=ivec2(floor(q));vec2 f=fract(q);
    ivec2 a=ivec2((i.x%size.x+size.x)%size.x,(i.y%size.y+size.y)%size.y);
    ivec2 b=(a+ivec2(1))%size;
    return mix(mix(texelFetch(uWaterNoise,a,0).g,
                   texelFetch(uWaterNoise,ivec2(b.x,a.y),0).g,f.x),
               mix(texelFetch(uWaterNoise,ivec2(a.x,b.y),0).g,
                   texelFetch(uWaterNoise,b,0).g,f.x),f.y);
}
float waterWaveShape(float n){float q=n*n;return q+clamp(q*q,0.,1.);}
vec3 waterWaveComponents(vec2 nativeXZ,float seconds){
    vec2 flow=vec2(0,1);
    vec2 majorUV=.1*(nativeXZ*vec2(.38,.53)+mod(seconds*flow/1.8,10.));
    float major=waterWaveShape(waterLinearGreen(majorUV))*.15;
    vec2 minorUV=.1*nativeXZ*vec2(3.7);
    vec2 effectiveFlow=flow-min(abs(flow),vec2(1))+1.;
    vec2 minorOffset=.1*mod(seconds*effectiveFlow/.7,28800.);
    float a=clamp(waterWaveShape(waterLinearGreen(minorUV+minorOffset)),0.,1.);
    float b=clamp(waterWaveShape(waterLinearGreen(minorUV-minorOffset)),0.,1.);
    float minor=(a+b-1.+.17)*.2;
    return vec3(major,minor,a*b);
}
vec3 waterPositionFromDepth(vec2 uv,float depth){
    // Exact inverse of scene.vert's orthographic camera, in laboratory pixels.
    float screen=(.5-uv.y)*uWaterViewport.y/max(uCamera.z,.01)+.8*uCamera.y;
    float towardEye=(.5-depth)*8000.;
    return vec3(uCamera.x+(uv.x-.5)*uWaterViewport.x/max(uCamera.z,.01),
                -.6*screen+.8*towardEye,.8*screen+.6*towardEye);
}
vec2 waterProject(vec3 p){
    return vec2(.5+(p.x-uCamera.x)*uCamera.z/uWaterViewport.x,
                .5-((p.z-uCamera.y)*.8-p.y*.6)*uCamera.z/uWaterViewport.y);
}
vec3 waterSurfaceNormal(vec3 world,vec3 waves){
    // Original uses derivatives of 5*major + .7*minor + 1.6*peak in screen
    // tangent space. This port recovers a world gradient from the two imported
    // continuous wave fields; the original peak/shore Voronoi branch is omitted.
    float height=100.*(waves.x*5.+waves.y*.7);
    vec2 dx=dFdx(world.xz),dy=dFdy(world.xz);
    float hx=dFdx(height),hy=dFdy(height),det=dx.x*dy.y-dx.y*dy.x;
    vec2 gradient=abs(det)>1e-8?vec2(dy.y*hx-dx.y*hy,dx.x*hy-dy.x*hx)/det:vec2(0);
    gradient=clamp(gradient,vec2(-.45),vec2(.45));
    return normalize(vec3(-gradient.x,1.,-gradient.y));
}
vec4 renderWater(vec3 world){
    vec2 pixel=uWaterViewport.zw;
    vec2 uv=gl_FragCoord.xy*pixel;
    vec3 opaque=texture(uWaterSceneColor,uv).rgb;
    float rawDepth=texture(uWaterSceneDepth,uv).r;
    vec3 bed=waterPositionFromDepth(uv,rawDepth);
    float verticalDepth=rawDepth<.99999?max(world.y-bed.y,0.):25.;
    verticalDepth=min(verticalDepth,120.);
    float coverage=smoothstep(.05,1.4,verticalDepth);
    vec2 nativeXZ=vec2(world.x,-world.z)*.01;
    float seconds=uCamera.w*uControls.y;
    vec3 waves=waterWaveComponents(nativeXZ,seconds);
    vec3 N=waterSurfaceNormal(world,waves);
    const vec3 V=vec3(0,.8,.6);
    vec3 L=normalize(uSunDir.xyz);

    // Sample a ray bent into the water. Reject offsets that would pull bridge,
    // bank or character pixels from above the surface into the underwater view.
    vec3 transmittedRay=refract(-V,N,1./1.333);
    vec3 hit=world+transmittedRay*(verticalDepth/max(-transmittedRay.y,.15));
    vec2 refractedUV=waterProject(hit);
    vec2 distortion=clamp(refractedUV-uv,-pixel*18.,pixel*18.);
    refractedUV=clamp(uv+distortion*uWaterFeatures.y,1.5*pixel,1.-1.5*pixel);
    float bentDepth=texture(uWaterSceneDepth,refractedUV).r;
    vec3 bentBed=waterPositionFromDepth(refractedUV,bentDepth);
    if(bentDepth>=.99999 || bentBed.y>world.y-.15)refractedUV=uv;
    else verticalDepth=mix(verticalDepth,clamp(world.y-bentBed.y,0.,120.),uWaterFeatures.y);
    vec3 bottom=texture(uWaterSceneColor,refractedUV).rgb;

    float visibility=mix(1.,sunVisibility(N),uLighting.w);
    vec2 cloudUV=(world.xz-uCloudWorld.xy)/uCloudWorld.zw;
    float cloud=all(greaterThanEqual(cloudUV,vec2(0)))&&all(lessThanEqual(cloudUV,vec2(1)))?texture(uCloudMask,cloudUV).a:0.;
    float cloudLight=1.-.35*cloud*uLightOptions.w;
    vec3 lighting=uAmbient.rgb*uLightOptions.x
        +uSunColor.rgb*max(dot(N,L),0.)*visibility*uLightOptions.y
        +uBacklight.rgb*max(dot(N,-L),0.)*uLightOptions.z;
    lighting=mix(vec3(1),lighting*cloudLight,uLighting.z);

    // True optical distance reveals the visible river bed at the shallow edge;
    // red light is absorbed faster, leaving a deeper blue-green centre.
    float opticalDepth=mix(18.,verticalDepth,uWaterFeatures.x)/max(-transmittedRay.y,.3);
    vec3 transmittance=exp(-vec3(.048,.025,.018)*opticalDepth);
    vec3 deepColour=vec3(.025,.202,.255); // original D4 colour family
    vec3 colour=bottom*transmittance+deepColour*lighting*(1.-transmittance);

    // Gentle shallow-bottom caustics are anchored to the river bed, never a
    // full-screen sparkle. This is a restrained preview of the original branch.
    vec2 causticUV=vec2(bed.x,-bed.z)*.01*.8+vec2(seconds*.025,-seconds*.018);
    float caustic=texture(uFoamNoise,fract(causticUV)).r;
    float shallow=smoothstep(.2,3.,verticalDepth)*(1.-smoothstep(10.,30.,verticalDepth));
    colour+=bottom*vec3(.13,.18,.10)*smoothstep(.56,.80,caustic)*shallow*visibility;

    // Original high water samples Unity reflection probes. The laboratory adds
    // a real planar scene capture for nearby mirrored objects, with procedural
    // sky only where that capture has no alpha coverage (clear must be RGBA0).
    vec3 R=reflect(-V,N);
    vec3 sky=mix(vec3(.020,.035,.075),vec3(.36,.53,.60),clamp(uLighting.x,0.,1.));
    sky=mix(sky*.76,sky,clamp(R.y*.8+.2,0.,1.));
    sky+=uSunColor.rgb*.05*pow(max(dot(R,L),0.),40.);
    vec2 reflectionUV=clamp(uv+vec2(N.x,-N.z)*vec2(19.,15.)*pixel,1.5*pixel,1.-1.5*pixel);
    vec4 sceneReflection=texture(uWaterReflection,reflectionUV);
    vec3 reflection=mix(sky,sceneReflection.rgb,clamp(sceneReflection.a,0.,1.)*uWaterReflectionInfo.x);
    // .10 follows the shipped material's stylized reflectivity. Pure-water
    // dielectric F0 would be about .02; this is not asserted as a physical fit.
    float fresnel=.10+.90*pow(1.-clamp(dot(N,V),0.,1.),5.);
    float reflectionWeight=fresnel*coverage*uWaterFeatures.z;
    colour=mix(colour,reflection,reflectionWeight);
    vec3 H=normalize(L+V);
    float specular=pow(max(dot(N,H),0.),96.);
    colour+=uSunColor.rgb*specular*.65*visibility*cloudLight*uLightOptions.y*uLighting.z;

    // Foam follows measured contact depth, so a shallow shelf or an immersed
    // stone can make foam even away from the procedural river's side boundary.
    float edge=(1.-smoothstep(2.,8.5,verticalDepth))*smoothstep(.15,1.3,verticalDepth);
    float foamNoise=texture(uFoamNoise,fract(nativeXZ*.8+vec2(0,seconds*.06))).r;
    float phase=verticalDepth*.85-seconds*2.2+foamNoise*3.5;
    float ripple=pow(.5+.5*sin(phase),5.);
    float foam=edge*smoothstep(.38,.70,foamNoise+ripple*.28)*uWaterFeatures.w;
    vec3 foamColour=vec3(.947,1.,.816)*lighting;
    colour=mix(colour,foamColour,foam*.60);
    // At zero thickness the water pass exactly reveals the opaque shore.
    colour=mix(opaque,colour,coverage);
    if(uEffects.y>.5)colour=mix(vec3(.90,.74,.25),vec3(.025,.12,.25),clamp(verticalDepth/25.,0.,1.));
    return vec4(max(colour,vec3(0)),1);
}
