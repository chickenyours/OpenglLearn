#version 450 core
uniform sampler2D uTexture;
uniform vec4 uCamera;
uniform vec4 uControls;
uniform vec4 uMaterial;
in vec2 vUV;
in float vCrown;
out vec4 fragColour;
float crownHash(vec2 q){vec2 a=50.*fract(q*.318310+vec2(.71,.113));return 2.*fract((a.x+a.y)*a.x*a.y)-1.;}
float crownNoise(vec2 p){vec2 i=floor(p),f=fract(p),u=f*f*(3.-2.*f);return mix(mix(crownHash(i),crownHash(i+vec2(1,0)),u.x),mix(crownHash(i+vec2(0,1)),crownHash(i+vec2(1)),u.x),u.y);}
void main(){
    vec2 uv=vUV;
    if(vCrown>.5 && uControls.x>0.){
        vec2 p=40.*.3*fract(vUV)+vec2(.2*uCamera.w*3.*(1.+uControls.x));
        vec2 n=vec2(crownNoise(p),crownNoise(p+vec2(1,0)));
        uv+=.2*(.5+uControls.x*1.5)*vec2(n.x,(.5+uControls.x*.5)*n.y)/vec2(textureSize(uTexture,0));
    }
    // Procedural terrain/cliffs are opaque. Original mesh and sprite cards use
    // the same .5 cutout as the colour pass, so transparent rectangles do not cast.
    if(uMaterial.x!=1. && uMaterial.x!=3. && texture(uTexture,uv).a<.5)discard;
    float d=min(gl_FragCoord.z,1.-1./16581375.);
    vec4 enc=fract(d*vec4(1.,255.,65025.,16581375.));
    enc-=enc.yzww*vec4(1./255.,1./255.,1./255.,0.);
    fragColour=enc;
}
