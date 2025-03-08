// #version 330 core

// in vec2 TexCoords;

// uniform sampler2D PositionMap;
// uniform sampler2D NormalMap;
// uniform sampler2D NoiseMap;

// uniform vec3 samples[64];
// uniform mat4 projection;

// int kernelSize = 64;
// float radius = 1.0;
// float bias = 0.025;

// out float FragColor;

// void main(){
//     vec2 scaleTex = vec2(textureSize(PositionMap,0)) / vec2(textureSize(NoiseMap,0));
//     vec3 FragPos = texture(PositionMap,TexCoords).xyz;
//     vec3 normal = normalize(texture(NormalMap,TexCoords).xyz);
//     vec3 randomVec = texture(NoiseMap, TexCoords * scaleTex).xyz;
//     vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
//     vec3 bitangent = cross(normal, tangent);
//     tangent = normalize(cross(normal,bitangent));
//     mat3 TBN = mat3(tangent,bitangent,normal);
//     float occlusion = 0.0;
//     for(int i = 0; i < kernelSize; ++i){
//         vec3 sample = TBN * samples[i] + FragPos;
//         vec4 offset = vec4(sample,1.0);
//         offset = projection * offset;
//         offset.xyz /= offset.w;
//         offset.xyz = offset.xyz * 0.5 + 0.5;
//         float sampleDepth = -texture(PositionMap,offset.xy).z;
//         float rangeCheck = smoothstep(0.0, 1.0, radius / abs(FragPos.z - sampleDepth));
//         occlusion += ((sampleDepth>=sample.z + bias)?1.0:0.0) * rangeCheck;
//     }
//     occlusion /=64;
//     FragColor = 1-occlusion;

//     //FragColor = texture(NormalMap,TexCoords).r + texture(NormalMap,TexCoords).g + texture(NormalMap,TexCoords).b;
//     //FragColor = texture(PositionMap,TexCoords).rgb;
//     //FragColor = vec3(TexCoords.x,TexCoords.y,0.0);
// }

#version 330 core
out vec3 FragColor;

in vec2 TexCoords;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D texNoise;

uniform vec3 samples[64];

// parameters (you'd probably want to use them as uniforms to more easily tweak the effect)
int kernelSize = 64;
uniform float radius = 0.4;
float bias = 0.001;//0.025;

uniform mat4 projection;

void main()
{
    vec2 noiseScale = vec2(textureSize(gPosition,0)) / vec2(textureSize(texNoise,0));
    // get input for SSAO algorithm
    vec3 fragPos = texture(gPosition, TexCoords).xyz;
    vec3 normal = normalize(texture(gNormal, TexCoords).rgb);
    vec3 randomVec = -normalize(texture(texNoise, TexCoords * noiseScale ).xyz);
    // create TBN change-of-basis matrix: from tangent-space to view-space
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);
    // iterate over the sample kernel and calculate occlusion factor
    float occlusion = 0.0;
    for(int i = 0; i < kernelSize; ++i)
    {
        // get sample position
        vec3 samplePos = TBN * samples[i]; // from tangent to view-space
        samplePos = fragPos + samplePos * radius; 
         
        // project sample position (to sample texture) (to get position on screen/texture)
        vec4 offset = vec4(samplePos, 1.0);
        offset = projection * offset; // from view to clip-space
        offset.xyz /= offset.w; // perspective divide
        offset.xyz = offset.xyz * 0.5 + 0.5; // transform to range 0.0 - 1.0
        
        // get sample depth
        float sampleDepth = texture(gPosition, offset.xy).z; // get depth value of kernel sample
        
        // range check & accumulate
        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPos.z - sampleDepth));
        occlusion += (sampleDepth >= samplePos.z +bias? 1.0 : 0.0) * rangeCheck;           
    }
    occlusion = 1.0 - (occlusion / kernelSize);
    
    FragColor = vec3(occlusion);
    //FragColor = normal;
}