#version 330 core

out vec4 FragColor;

struct Material{
    vec3 color;
    sampler2D diffuse;
    sampler2D specular;
    float shininess;
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
};

struct DirectLight{
    vec3 color;
    vec3 direct;
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};

vec3 CalPointBlingPhoneLight(PointLight light,vec3 fragPos,vec3 normal,vec3 eyePos);
vec3 CalDirectBlingPhoneLight(DirectLight light, vec3 fragPos, vec3 normal, vec3 eyePos);

in vec3 FragPos;
in vec2 TexCoords;
in vec3 Normal;

uniform PointLight plight[4];
uniform Material material;

uniform vec3 viewPos;

void main(){
    vec3 color = vec3(0.0);
    for(int i = 0; i < 4; i++) {
        color += CalPointBlingPhoneLight(plight[i], FragPos, Normal, viewPos);
    }

    color = color / (color + vec3(1.0));
    //color = pow(color, vec3(1.0/2.2));  

    FragColor = vec4(color,1.0);
}

vec3 CalPointBlingPhoneLight(PointLight light,vec3 fragPos,vec3 normal,vec3 eyePos){
    vec3 viewDir = normalize(eyePos - fragPos); // 从片段位置到相机的方向
    vec3 lightDir = normalize(light.pos - fragPos);
    // diffuse shading
    float diff = max(dot(normal, lightDir), 0.0);
    // specular shading
    float spec = 0.0;
    if (diff != 0.0){
        vec3 halfwayDir = normalize(viewDir+lightDir); //计算半程向量
        spec = pow(max(dot(normal,halfwayDir), 0.0), material.shininess);
    }
    // attenuation
    float distance = length(light.pos - fragPos);
    float attenuation = 1.0 / (light.constant + light.linear * distance + light.quadratic * (distance * distance));    
    // combine results
    vec3 ambient = light.ambient * vec3(texture(material.diffuse, TexCoords));
    vec3 diffuse = light.diffuse * diff * vec3(texture(material.diffuse, TexCoords));
    vec3 specular = light.specular * spec; //* vec3(texture(material.specular, TexCoords));
    diffuse *= attenuation;
    specular *= attenuation;
    //vec3 outColor = (ambient+diffuse+specular)*light.color;
    vec3 outColor = vec3(diff+spec,0.0,0.0);
    return outColor;
}

vec3 CalDirectBlingPhoneLight(DirectLight light, vec3 fragPos, vec3 normal, vec3 eyePos) {
    vec3 viewDir = normalize(eyePos - fragPos); // 从片段位置到相机的方向
    vec3 lightDir = normalize(-light.direct);

    // diffuse shading
    float diff = max(dot(normal, lightDir), 0.0);
    // specular shading
    float spec = 0.0;
    if (diff != 0.0) {
        vec3 halfwayDir = normalize(viewDir + lightDir); // 计算半程向量
        spec = pow(max(dot(normal, halfwayDir), 0.0), material.shininess);
    }
    // combine results
    vec3 ambient = light.ambient * vec3(texture(material.diffuse, TexCoords));
    vec3 diffuse = light.diffuse * diff * vec3(texture(material.diffuse, TexCoords));
    vec3 specular = light.specular * spec * vec3(texture(material.specular, TexCoords));
    return (ambient + diffuse + specular) * light.color;
}
