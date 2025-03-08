#version 330 core

//罗德里格旋转公式
vec3 rotateAroundAxis(vec3 v, vec3 axis, float angle) {
    vec3 k = normalize(axis);           // 归一化旋转轴
    float s = sin(angle/57);               // 正弦值
    float c = cos(angle/57);               // 余弦值

    return v * c + cross(k, v) * s + k * dot(k, v) * (1.0 - c);
}

struct Material{
    vec3 objectColor;
    vec3 ambientStrength;
    vec3 diffusionStrength;
    vec3 specularStrength;
    sampler2D diffuse;
    sampler2D specular;
};

struct Light{
    vec3 position;
    vec3 lightColor;
    vec3 lightDirection;
    vec3 ambientStrength;
    vec3 diffusionStrength;
    vec3 specularStrength;
    float cutoff;
    float outCutoff;
    float constant;
    float linear;
    float quadratic;
    sampler2D cookie;
};


in vec3 Normal;
in vec3 FragPos;
in vec2 TexCoord;

uniform sampler2D tex;
uniform Material material;
uniform Light light;
uniform vec3 viewPos;
uniform float time;

out vec4 FragColor;

void main(){
    vec3 norm = normalize(Normal);
    vec3 lightDirection = normalize(light.position - FragPos);
    vec3 up = rotateAroundAxis(vec3(0.0,1.0,0.0),light.lightDirection,60*time);
    vec3 z = normalize(light.lightDirection);
    vec3 x = normalize(cross(z,up));
    vec3 y = normalize(cross(x,z));
    float sinoutcutoff = pow(1-light.outCutoff*light.outCutoff,0.5);
    mat3 viewmat = mat3(x.x,y.x,z.x,
                        x.y,y.y,z.y,
                        x.z,y.z,z.z
                        );
    // 光锥投射纹理半径
    float radian = 0.5;
    // 光锥投射纹理中心坐标
    vec2 castUVPos = vec2(0.5,0.5);
    vec2 lightCoord = (normalize(viewmat * lightDirection)).xy/sinoutcutoff*radian;
    lightCoord.x += castUVPos.x;
    lightCoord.y += castUVPos.y;
    // 确保光照颜色和纹理采样正确
    vec3 lightColor = light.lightColor * vec3(texture(light.cookie,lightCoord)); 
    float distance = length(light.position - FragPos);
    float attenuation = 1.0 / (light.constant + light.linear *  distance + light.quadratic * ( distance *  distance));
    float theta = dot(-lightDirection,light.lightDirection);
    float epsilon = light.cutoff - light.outCutoff;
    float intensity = (theta-light.outCutoff)/epsilon;
    intensity *= (sin(theta*10000)*0.5+0.5);
    vec3 ambient = material.ambientStrength * light.ambientStrength*vec3(texture(material.diffuse,TexCoord));
    vec3 diffusion =   max(dot(norm,normalize(lightDirection)),0.0) * material.diffusionStrength * light.diffusionStrength*vec3(texture(material.diffuse,TexCoord));
    vec3 specular = pow(max(dot(normalize(reflect(-lightDirection,norm)),normalize(viewPos-FragPos)),0.0),32) * material.specularStrength * light.specularStrength *vec3(texture(material.specular,TexCoord));
    FragColor = vec4(((ambient+diffusion+specular))*material.objectColor*lightColor*attenuation*intensity,1.0);
}