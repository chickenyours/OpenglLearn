#version 330 core

in VS_OUT {
    vec3 FragPos;
    vec2 TexCoords;
    vec4 LightFragPos;
    //vec3 Normal;
    mat3 TBN;
} fs_in;

out vec4 FragColor;

struct Material{
    vec3 color;
    sampler2D diffuse;
    sampler2D specular;
    sampler2D normal;
    float shininess;
};

struct DirectLight{
    vec3 color;
    vec3 direct;
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
    sampler2D shadowMap;
};

struct PointLight{
    vec3 color;
    vec3 pos;
    float constant;
    float linear;
    float quadratic;
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
    float shadowFarPlane;
    samplerCube shadowMap;
};

uniform Material material;
uniform DirectLight dlight;
uniform PointLight plight;

uniform vec3 viewPos;

vec3 CalDirectBlingPhoneLightWithNormalMap(DirectLight light,Material material,vec3 fragPos,vec2 texCoord,mat3 TBN,vec3 viewPos);
vec3 CalDirectBlingPhoneLightWithShadowMap(DirectLight light,Material material,vec3 fragPos,vec2 texCoord,mat3 TBN,vec3 viewPos,vec4 fragPosLightSpace);
vec3 CalPointBlingPhoneLightWithCubeShadowMap(PointLight light,Material material,vec3 fragPos,vec2 texCoord,mat3 TBN,vec3 viewPos);

void main(){
    vec3 color = vec3(0.0);
    //平行光照射
    color += CalDirectBlingPhoneLightWithNormalMap(dlight,material,fs_in.FragPos,fs_in.TexCoords,fs_in.TBN,viewPos); 
    //color += CalPointBlingPhoneLightWithCubeShadowMap(plight,material,fs_in.FragPos,fs_in.TexCoords,fs_in.Normal,vpos);
    FragColor = vec4(color,1.0);
    //FragColor = vec4(1.0);
} 

vec3 CalDirectBlingPhoneLightWithShadowMap(DirectLight light,Material material,vec3 fragPos,vec2 texCoord,mat3 TBN,vec3 viewPos,vec4 fragPosLightSpace){
    vec3 viewDir = normalize(viewPos - fragPos); // 从片段位置到相机的方向
    vec3 lightDir = normalize(-light.direct);

    vec3 normal = texture(material.normal,texCoord).rgb;
    normal = normal * 2.0 - 1.0;
    normal = TBN * normal;

    // diffuse shading
    float diff = max(dot(normal, lightDir), 0.0);
    // specular shading
    float spec = 0.0;
    if (diff != 0.0){
        vec3 halfwayDir = normalize(viewDir+lightDir); //计算半程向量
        spec = pow(max(dot(normal,halfwayDir), 0.0), material.shininess);
    }
    // combine results
    vec3 ambient = light.ambient * vec3(texture(material.diffuse, texCoord));
    vec3 diffuse = light.diffuse * diff * vec3(texture(material.diffuse, texCoord));
    vec3 specular = light.specular * spec; //* vec3(texture(material.specular, TexCoord));
    //计算平行阴影
    //在计算之前验证 fragPosLightSpace.w 的值：
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;  //透视除法（如果光坐标的点是做过透视投影的话，w不一定为1）
    projCoords = projCoords * 0.5 + 0.5;                            //在NDC中,各分量位于区间[-1,1],需要线性变换成深度贴图数据范围区间[0,1]
    /*if (projCoords.x >= 0.0 && projCoords.x <= 1.0 && projCoords.y >= 0.0 && projCoords.y <= 1.0) {
    float closestDepth = texture(shadowMap, projCoords.xy).r;
} else {
    // 处理越界情况
    float closestDepth = 1.0; // 默认为无阴影
}*/
    
    // float closestDepth = texture(shadowMap, projCoords.xy).r;       //获取片段对应光照裁剪裁剪后经变换的坐标上的深度值
    // float currentDepth = projCoords.z;                              //获取当前片段上的深度值
    // float bias = 0.0015 + max(0.0005*(1 -(dot(normal,lightDir))),0.0); //可能出现精度问题出现伪阴影
    // //if (projCoords.z >= 0.0 && projCoords.z <= 1.0) { 有时，projCoords.z 可能超出深度贴图的范围（通常是 [0, 1]）。在这种情况下，应忽略阴影计算。  
    // float shadow = currentDepth-bias > closestDepth  ? 0.0 : 1.0;        //比较值，确定是否最近于光

    //PCF
    vec2 offsets[9] = vec2[](
    vec2(-1.0, 1.0), vec2(0.0, 1.0), vec2(1.0, 1.0),
    vec2(-1.0, 0.0), vec2(0.0, 0.0), vec2(1.0, 0.0),
    vec2(-1.0, -1.0), vec2(0.0, -1.0), vec2(1.0, -1.0)
    );
    float shadow = 0.0;
    for(int i = 0;i<9;i++){
        vec2 offset = offsets[i]/textureSize(light.shadowMap, 0);
        float closestDepth = texture(light.shadowMap, projCoords.xy+offset).r;
        float currentDepth = projCoords.z;
        float bias = 0.0015 + max(0.0005*(1 -(dot(normal,lightDir))),0.0);
        shadow += currentDepth-bias > closestDepth?0.0:1.0;
    }
    shadow/=9.0;




    return (ambient + (diffuse + specular)*shadow)*light.color;
    //return (vec3(shadow));
}
vec3 CalDirectBlingPhoneLightWithNormalMap(DirectLight light,Material material,vec3 fragPos,vec2 texCoord,mat3 TBN,vec3 viewPos){
    vec3 viewDir = normalize(viewPos - fragPos); // 从片段位置到相机的方向
    vec3 lightDir = normalize(-light.direct);

    vec3 normal = texture(material.normal,texCoord).rgb;
    normal = normal * 2.0 - 1.0;
    normal = TBN * normal;

    // diffuse shading
    float diff = max(dot(normal, lightDir), 0.0);
    // specular shading
    float spec = 0.0;
    if (diff != 0.0){
        vec3 halfwayDir = normalize(viewDir+lightDir); //计算半程向量
        spec = pow(max(dot(normal,halfwayDir), 0.0), material.shininess);
    }
    // combine results
    vec3 ambient = light.ambient * vec3(texture(material.diffuse, texCoord));
    vec3 diffuse = light.diffuse * diff * vec3(texture(material.diffuse, texCoord));
    vec3 specular = light.specular * spec * vec3(texture(material.specular, texCoord).r);

    vec3 color = texture(material.normal,texCoord).rgb;
    return (ambient + diffuse + specular)*light.color;
    //return color;

}

vec3 CalPointBlingPhoneLightWithCubeShadowMap(PointLight light,Material material,vec3 fragPos,vec2 texCoord,mat3 TBN,vec3 viewPos){
    vec3 viewDir = normalize(viewPos - fragPos); // 从片段位置到相机的方向
    vec3 lightDir = normalize(light.pos - fragPos);
    // diffuse shading

    vec3 normal = texture(material.normal,texCoord).rgb;
    normal = normal * 2.0 - 1.0;
    normal = TBN * normal;

    float diff = max(dot(normal, lightDir), 0.0);
    // specular shading
    float spec = 0.0;
    if (diff != 0.0){
        vec3 halfwayDir = normalize(viewDir+lightDir); //计算半程向量
        spec = pow(max(dot(normal,halfwayDir), 0.0), material.shininess);
    }
    // vec3 reflectDir = reflect(-lightDir, normal);
    // spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
    // attenuation
    float distance = length(light.pos - fragPos);
    float attenuation = 1.0 / (light.constant + light.linear * distance + light.quadratic * (distance * distance));    
    // combine results
    vec3 ambient = light.ambient * vec3(texture(material.diffuse, texCoord));
    vec3 diffuse = light.diffuse * diff * vec3(texture(material.diffuse, texCoord));
    vec3 specular = light.specular * spec; //* vec3(texture(material.specular, texCoord));
    diffuse *= attenuation;
    specular *= attenuation;
    //计算透视阴影
    // vec3 fragToLight = fragPos - light.pos;
    // float closestDepth = texture(light.depthMap, fragToLight).r;
    // closestDepth *= light.shadowFarPlane;
    // float currentDepth = length(fragToLight);
    // float bias = 0.0005 + max((currentDepth+1) * 0.01 * (1 -(dot(normal,lightDir))),0.0); // We use a much larger bias since depth is now in [near_plane, far_plane] range
    // float shadow = (currentDepth < closestDepth+bias) ? 1.0 :0.0;

    //PCF
    float shadow = 0.0;
    float samples = 4.0;
    float offset = 0.1;
    vec3 fragToLight = fragPos - light.pos;
    float currentDepth = length(fragToLight);
    float bias = 0.0005 + max((currentDepth+1) * 0.01 * (1 -(dot(normal,lightDir))),0.0); 
    for(float x = -offset; x < offset; x += offset / (samples * 0.5))
    {
        for(float y = -offset; y < offset; y += offset / (samples * 0.5))
        {
            for(float z = -offset; z < offset; z += offset / (samples * 0.5))
            {
                float closestDepth = texture(light.shadowMap, fragToLight + vec3(x, y, z)).r; 
                closestDepth *= light.shadowFarPlane; //* 0.82;   // Undo mapping [0;1]
                if(currentDepth < closestDepth+bias){
                    shadow += 1.0;
                }
                    
            }
        }
    }
    shadow /= (samples * samples * samples);
//         vec3 sampleOffsetDirections[20] = vec3[]
// (
//    vec3( 1,  1,  1), vec3( 1, -1,  1), vec3(-1, -1,  1), vec3(-1,  1,  1), 
//    vec3( 1,  1, -1), vec3( 1, -1, -1), vec3(-1, -1, -1), vec3(-1,  1, -1),
//    vec3( 1,  1,  0), vec3( 1, -1,  0), vec3(-1, -1,  0), vec3(-1,  1,  0),
//    vec3( 1,  0,  1), vec3(-1,  0,  1), vec3( 1,  0, -1), vec3(-1,  0, -1),
//    vec3( 0,  1,  1), vec3( 0, -1,  1), vec3( 0, -1, -1), vec3( 0,  1, -1)
// );
//     float diskRadius = (1.0 + (viewDistance / far_plane)) / 25.0;
//     for(int i = 0; i < samples; ++i)
//     {
//         float closestDepth = texture(light.depthMap, fragToLight + sampleOffsetDirections[i] * diskRadius).r;
//         closestDepth *= light.shadowFarPlane * 0.82;   // Undo mapping [0;1]
//         if(currentDepth - bias > closestDepth)
//             shadow += 1.0;
//     }
//     shadow /= float(samples);
    

    return (ambient + (diffuse + specular)*(shadow))*light.color;
    //return vec3()
    //return vec3(shadow);
    //vec3 col = vec3(texture(depthMap,normal).r);
    //return col;
}
