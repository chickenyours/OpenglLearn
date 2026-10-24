#version 450 core
uniform sampler2D uTexture;
layout(binding=1) uniform sampler2D uDepth;
in vec2 vUV;
out vec4 fragColour;
void main(){
  fragColour=texture(uTexture,vUV);
  gl_FragDepth=texture(uDepth,vUV).r;
}
