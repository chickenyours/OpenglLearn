#version 330 core

struct Material{
    sampler2D diffuse;
    sampler2D specular;
    float shininess;
};

struct Light{
    vec3 position;

    vec3 lightColor;
    vec3 ambientStrength;
    vec3 diffusionStrength;
    vec3 specularStrength;

    float constant;
    float linear;
    float quadratic;
};

in vec3 Normal;
in vec3 FragPos;
in vec2 TexCoord;

uniform Material material;
uniform Light light;
uniform vec3 viewPos;


out vec4 FragColor;

void main(){
    vec3 norm = normalize(Normal);
    vec3 ambient =light.ambientStrength * vec3(texture(material.diffuse,TexCoord));
    vec3 diffusion =   max(dot(norm,normalize(light.position - FragPos)),0.0) * light.diffusionStrength * vec3(texture(material.diffuse,TexCoord));
    vec3 specular = pow(max(dot(normalize(reflect(-normalize(light.position - FragPos),norm)),normalize(viewPos-FragPos)),0.0),32) * light.specularStrength * vec3(texture(material.specular,TexCoord));
    float distance    = length(light.position - FragPos);
    float attenuation = 1.0 / (light.constant + light.linear * distance + light.quadratic * (distance * distance));    
    FragColor = vec4((ambient+diffusion+specular)*light.lightColor*attenuation,1.0);
}