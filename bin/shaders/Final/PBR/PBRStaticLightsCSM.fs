#version 460 core

out vec4 FragColor;

in vec3 FragPos;
in vec2 TexCoords;
in mat3 TBN;

//material map
layout(binding = 0) uniform sampler2D albedoMap;
layout(binding = 1) uniform sampler2D normalMap;
layout(binding = 2) uniform sampler2D metallicMap;
layout(binding = 3) uniform sampler2D roughnessMap;
layout(binding = 4) uniform sampler2D aoMap;



uniform float iTime;
uniform vec3 viewPos;



// 新增，替代 metallicMap
uniform bool useMetallicMap;
uniform float metallicValue;

//CSM
layout(binding = 5) uniform sampler2DArray shadowMap;
layout (std140, binding = 3) uniform CSMLightMatrices
{
    mat4 lightSpaceMatrices[16];
};
uniform mat4 view;
uniform float cascadePlaneDistances[16];
uniform int cascadeCount;   // number of frusta - 1
float ShadowCalculation(vec3 fragPosWorldSpace);

const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness);
float GeometrySchlickGGX(float NdotV, float roughness);
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness);
vec3 fresnelSchlick(float cosTheta, vec3 F0);

vec3 GetLight();

void main()
{		

    //计算PBR光照
    vec3 Lo = GetLight();
  
    //计算环境光
    vec3 albedo = texture(albedoMap, TexCoords).rgb;
    float ao = texture(aoMap, TexCoords).r;
    vec3 ambient = vec3(0.03) * albedo * ao;

    vec3 color = ambient + Lo;
	
    color = color / (color + vec3(0.2));
    //color = pow(color, vec3(1.0/2.2));   //gama

    FragColor = vec4(color, 1.0);

    //FragColor = vec4(1.0);
}  

vec3 GetLight(){
    // lights
    vec3 lightPositions[4];
    vec3 lightColors[4];

    lightPositions[0] = vec3(5.0, 5.0, 0.0);
    lightPositions[1] = vec3(-5.0, 5.0, 0.0);
    lightPositions[2] = vec3(0.0, -5.0, -5.0);
    lightPositions[3] = vec3(0.0, -5.0, 5.0);

    float qiangdu = 100.0;

    lightColors[0] = vec3(qiangdu, qiangdu, qiangdu);
    lightColors[1] = vec3(qiangdu, qiangdu, qiangdu);
    lightColors[2] = vec3(qiangdu, qiangdu, qiangdu);
    lightColors[3] = vec3(qiangdu, qiangdu, qiangdu);

    mat2 r = mat2(cos(iTime * 0.5),-sin(iTime * 0.5),sin(iTime * 0.5),cos(iTime * 0.5));

    for(int i = 0; i<4; i++){
        lightPositions[i].xz  = r * lightPositions[i].xz;
    }


    vec3 albedo = texture(albedoMap, TexCoords).rgb;
    float roughness = texture(roughnessMap, TexCoords).r;
    // float metallic = texture(metallicMap, TexCoords).r;
    float metallic = useMetallicMap ? texture(metallicMap, TexCoords).r : metallicValue;

    vec3 N = normalize(texture(normalMap, TexCoords).rgb * 2.0 - 1.0);
    N = normalize(TBN * N);

    vec3 V = normalize(viewPos - FragPos);

    vec3 F0 = vec3(0.05); 
    F0 = mix(F0, albedo, metallic);

    // reflectance equation
    vec3 Lo = vec3(0.0);
    for(int i = 0; i < 4; ++i) 
    {
        // calculate per-light radiance
        vec3 L = normalize(lightPositions[i] - FragPos);
        vec3 H = normalize(V + L);
        float lightDistance    = length(lightPositions[i] - FragPos);
        float attenuation = 1.0 / (lightDistance * lightDistance);
        vec3 radiance     = lightColors[i] * attenuation;        
        
        // cook-torrance brdf
        float NDF = DistributionGGX(N, H, roughness);        
        float G   = GeometrySmith(N, V, L, roughness);      
        vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);       
        
        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;	  
        
        vec3 numerator    = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        vec3 specular     = numerator / denominator;  
            
        // add to outgoing radiance Lo
        float NdotL = max(dot(N, L), 0.0);                
        Lo += (kD * albedo / PI + specular) * radiance * NdotL; 
    }   
    return Lo;

}


vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}  

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a      = roughness*roughness;
    float a2     = a*a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;
	
    float num   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
	
    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float num   = NdotV;
    float denom = NdotV * (1.0 - k) + k;
	
    return num / denom;
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2  = GeometrySchlickGGX(NdotV, roughness);
    float ggx1  = GeometrySchlickGGX(NdotL, roughness);
	
    return ggx1 * ggx2;
}

float ShadowCalculation(vec3 fragPosWorldSpace)
{
    // select cascade layer
    vec4 fragPosViewSpace = view * vec4(fragPosWorldSpace, 1.0);
    float depthValue = abs(fragPosViewSpace.z);

    int layer = -1;
    for (int i = 0; i < cascadeCount; ++i)
    {
        if (depthValue < cascadePlaneDistances[i])
        {
            layer = i;
            break;
        }
    }
    if (layer == -1)
    {
        layer = cascadeCount;
    }

    vec4 fragPosLightSpace = lightSpaceMatrices[layer] * vec4(fragPosWorldSpace, 1.0);
    // perform perspective divide
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    // transform to [0,1] range
    projCoords = projCoords * 0.5 + 0.5;

    // get depth of current fragment from light's perspective
    float currentDepth = projCoords.z;

    // keep the shadow at 0.0 when outside the far_plane region of the light's frustum.
    if (currentDepth > 1.0)
    {
        return 0.0;
    }
    // calculate bias (based on depth map resolution and slope)
    vec3 normal = normalize(fs_in.Normal);
    float bias = max(0.05 * (1.0 - dot(normal, lightDir)), 0.005);
    const float biasModifier = 0.5f;
    if (layer == cascadeCount)
    {
        bias *= 1 / (farPlane * biasModifier);
    }
    else
    {
        bias *= 1 / (cascadePlaneDistances[layer] * biasModifier);
    }

    // PCF
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    for(int x = -1; x <= 1; ++x)
    {
        for(int y = -1; y <= 1; ++y)
        {
            float pcfDepth = texture(shadowMap, vec3(projCoords.xy + vec2(x, y) * texelSize, layer)).r;
            shadow += (currentDepth - bias) > pcfDepth ? 1.0 : 0.0;        
        }    
    }
    shadow /= 9.0;
        
    return shadow;
}

