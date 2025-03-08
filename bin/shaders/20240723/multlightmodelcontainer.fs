#version 330 core

//罗德里格旋转公式
vec3 rotateAroundAxis(vec3 v, vec3 axis, float degree) {
    vec3 k = normalize(axis);           // 归一化旋转轴
    float s = sin(degree/57);               // 正弦值
    float c = cos(degree/57);               // 余弦值

    return v * c + cross(k, v) * s + k * dot(k, v) * (1.0 - c);
}

struct DirectionLight{
    vec3 color;
    vec3 direction;

    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
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
struct SpotLight{
    vec3 color;
    vec3 pos;
    vec3 direction;
    float cutOff;
    float outerCutOff;
    float constant;
    float linear;
    float quadratic;
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};

struct CookieSpotLight{
    vec3 color;
    vec3 pos;
    vec3 direction;
    float cutOff;
    float outerCutOff;
    float constant;
    float linear;
    float quadratic;
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;

    sampler2D cookie;
    float degree;
};

struct Material{
    vec3 color;
    sampler2D diffuse;
    sampler2D specular;
    float shininess;
};
vec3 CalDirectionLight(DirectionLight light,vec3 normal,vec3 viewDir);
vec3 CalPointLight(PointLight light,vec3 normal,vec3 fragPos,vec3 viewDir);
vec3 CalSpotLight(SpotLight light,vec3 normal,vec3 fragPos,vec3 viewDir);
vec3 CalCookieSpotLight(CookieSpotLight light,vec3 normal,vec3 fragPos,vec3 viewDir);

in vec3 Normal;
in vec3 FragPos;
in vec2 TexCoord;
in vec3 EyeView;

#define NR_POINT_LIGHTS 3

uniform DirectionLight dlight;
uniform PointLight plight[NR_POINT_LIGHTS];
uniform CookieSpotLight slight;
uniform Material material;
uniform vec3 viewPos;

out vec4 FragColor;

void main(){
    vec3 norm = normalize(Normal);
    vec3 outcolor = vec3(0.0);
    vec3 viewDir = normalize(viewPos - FragPos);
    outcolor += CalDirectionLight(dlight,norm,viewDir);
    for(int i =0;i<NR_POINT_LIGHTS;i++){
         outcolor += CalPointLight(plight[i],norm,FragPos,viewDir);
     }
    outcolor += CalCookieSpotLight(slight,norm,FragPos,viewDir);
    FragColor = vec4(outcolor,1.0);
}

vec3 CalDirectionLight(DirectionLight light,vec3 normal,vec3 viewDir){
    vec3 lightDir = normalize(-light.direction);
    // diffuse shading
    float diff = max(dot(normal, lightDir), 0.0);
    // specular shading
    vec3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
    // combine results
    vec3 ambient = light.ambient * vec3(texture(material.diffuse, TexCoord));
    vec3 diffuse = light.diffuse * diff * vec3(texture(material.diffuse, TexCoord));
    vec3 specular = light.specular * spec * vec3(texture(material.specular, TexCoord));
    return (ambient + diffuse + specular)*light.color;
}
vec3 CalPointLight(PointLight light,vec3 normal,vec3 fragPos,vec3 viewDir){
    vec3 lightDir = normalize(light.pos - fragPos);
    // diffuse shading
    float diff = max(dot(normal, lightDir), 0.0);
    // specular shading
    vec3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
    // attenuation
    float distance = length(light.pos - fragPos);
    float attenuation = 1.0 / (light.constant + light.linear * distance + light.quadratic * (distance * distance));    
    // combine results
    vec3 ambient = light.ambient * vec3(texture(material.diffuse, TexCoord));
    vec3 diffuse = light.diffuse * diff * vec3(texture(material.diffuse, TexCoord));
    vec3 specular = light.specular * spec * vec3(texture(material.specular, TexCoord));
    ambient *= attenuation;
    diffuse *= attenuation;
    specular *= attenuation;
    return (ambient + diffuse + specular)*light.color;
}
vec3 CalSpotLight(SpotLight light,vec3 normal,vec3 fragPos,vec3 viewDir){
    vec3 lightDir = normalize(light.pos - fragPos);
    // diffuse shading
    float diff = max(dot(normal, lightDir), 0.0);
    // specular shading
    vec3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
    // attenuation
    float distance = length(light.pos - fragPos);
    float attenuation = 1.0 / (light.constant + light.linear * distance + light.quadratic * (distance * distance));    
    // spotlight intensity
    float theta = dot(lightDir, normalize(-light.direction)); 
    float epsilon = light.cutOff - light.outerCutOff;
    float intensity = clamp((theta - light.outerCutOff) / epsilon, 0.0, 1.0);
    // combine results
    vec3 ambient = light.ambient * vec3(texture(material.diffuse, TexCoord));
    vec3 diffuse = light.diffuse * diff * vec3(texture(material.diffuse, TexCoord));
    vec3 specular = light.specular * spec * vec3(texture(material.specular, TexCoord));
    ambient *= attenuation * intensity;
    diffuse *= attenuation * intensity;
    specular *= attenuation * intensity;
    return (ambient + diffuse + specular);
}

vec3 CalCookieSpotLight(CookieSpotLight light,vec3 normal,vec3 fragPos,vec3 viewDir){
    vec3 lightDir = normalize(light.pos - fragPos);
    vec3 z = light.direction;
    vec3 up = abs(z.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 1.0, 0.001);
    vec3 x = rotateAroundAxis(normalize(cross(z,up)),z,light.degree);
    
    vec3 y = normalize(cross(x,z));
    float sinoutcutoff = pow(1-light.outerCutOff*light.outerCutOff,0.5);
    mat3 viewmat = mat3(x.x,y.x,z.x,
                        x.y,y.y,z.y,
                        x.z,y.z,z.z
                        );
    // 光锥投射纹理半径
    float radian = 1.0;
    // 光锥投射纹理中心坐标
    vec2 castUVPos = vec2(0.5,0.5);
    vec2 lightCoord = (normalize(viewmat * lightDir)).xy/sinoutcutoff*radian;
    lightCoord.x += castUVPos.x;
    lightCoord.y += castUVPos.y;
    vec3 lightColor = light.color * vec3(texture(light.cookie,lightCoord)); 

    // diffuse shading
    float diff = max(dot(normal, lightDir), 0.0);
    // specular shading
    vec3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
    // attenuation
    float distance = length(light.pos - fragPos);
    float attenuation = 1.0 / (light.constant + light.linear * distance + light.quadratic * (distance * distance));    
    // spotlight intensity
    float theta = dot(lightDir, normalize(-light.direction)); 
    float epsilon = light.cutOff - light.outerCutOff;
    float intensity = clamp((theta - light.outerCutOff) / epsilon, 0.0, 1.0);
    // combine results
    vec3 ambient = light.ambient * vec3(texture(material.diffuse, TexCoord));
    vec3 diffuse = light.diffuse * diff * vec3(texture(material.diffuse, TexCoord));
    vec3 specular = light.specular * spec * vec3(texture(material.specular, TexCoord));
    ambient *= attenuation * intensity;
    diffuse *= attenuation * intensity;
    specular *= attenuation * intensity;
    return (ambient + diffuse + specular)*lightColor;
}

