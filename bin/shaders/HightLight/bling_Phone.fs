#version 330 core

in vec3 Normal;
in vec3 FragPos;
in vec2 TexCoord;
in vec3 EyePos;

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

vec3 CalPointBlingPhoneLight(PointLight light,vec3 fragPos,vec3 normal,vec3 eyePos);

uniform PointLight plight;
uniform Material material;
uniform vec3 eye;
uniform int use;

void main(){
    vec3 diffusionColor = CalPointBlingPhoneLight(plight,FragPos,Normal,eye);
    FragColor = vec4(diffusionColor,1.0);
}

vec3 CalPointBlingPhoneLight(PointLight light,vec3 fragPos,vec3 normal,vec3 eyePos){
    vec3 viewDir = normalize(eyePos - fragPos); // 从片段位置到相机的方向
    vec3 lightDir = normalize(light.pos - fragPos);
    // diffuse shading
    float diff = max(dot(normal, lightDir), 0.0);
    // specular shading
    float spec = 0.0;
    if (diff != 0.0){
        if(use==1){
            vec3 halfwayDir = normalize(viewDir+lightDir); //计算半程向量
            spec = pow(max(dot(normal,halfwayDir), 0.0), material.shininess);
        }
        else{
            vec3 reflectDir = reflect(-lightDir, normal);
            spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
        }
    }
    // attenuation
    float distance = length(light.pos - fragPos);
    float attenuation = 1.0 / (light.constant + light.linear * distance + light.quadratic * (distance * distance));    
    // combine results
    vec3 ambient = light.ambient * vec3(texture(material.diffuse, TexCoord));
    vec3 diffuse = light.diffuse * diff * vec3(texture(material.diffuse, TexCoord));
    vec3 specular = light.specular * spec; //* vec3(texture(material.specular, TexCoord));
    diffuse *= attenuation;
    specular *= attenuation;
    return (ambient + diffuse + specular)*light.color;
}

