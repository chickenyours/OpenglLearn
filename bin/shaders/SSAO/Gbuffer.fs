#version 330 core

layout (location = 0) out vec4 gPosition;
layout (location = 1) out vec3 gNormal;
layout (location = 2) out vec4 gAlbedoSpec;



in vec2 TexCoords;
in vec3 FragPos;
in vec3 Normal;

uniform sampler2D diffuse;
uniform sampler2D specualr;

uniform float NEAR;
uniform float FAR;
float LinearizeDepth(float depth)
{
    float z = depth * 2.0 - 1.0; // 回到NDC
    return (2.0 * NEAR * FAR) / (FAR + NEAR - z * (FAR - NEAR));    
}


void main(){
    gPosition.rgb = FragPos.xyz;
    gPosition.a = LinearizeDepth(gl_FragCoord.z);
    

    gNormal = normalize(Normal);
    gAlbedoSpec.rgb = texture(diffuse,TexCoords).rgb;
    gAlbedoSpec.a = texture(specualr,TexCoords).r;
}
