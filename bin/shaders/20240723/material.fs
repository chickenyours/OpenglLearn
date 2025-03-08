#version 330 core

struct Material{
    vec3 objectColor;
    vec3 ambientStrength;
    vec3 diffusionStrength;
    vec3 specularStrength;
};

struct Light{
    vec3 position;
    vec3 lightColor;
    vec3 ambientStrength;
    vec3 diffusionStrength;
    vec3 specularStrength;
    
};

in vec3 Normal;
in vec3 FragPos;
in vec2 TexCoord;

uniform sampler2D tex;
uniform Material material;
uniform Light light;
uniform vec3 viewPos;


out vec4 FragColor;

void main(){
    vec3 norm = normalize(Normal);
    vec3 ambient = material.ambientStrength * light.ambientStrength;
    vec3 diffusion =   max(dot(norm,normalize(light.position - FragPos)),0.0) * material.diffusionStrength * light.diffusionStrength;
    vec3 specular = pow(max(dot(normalize(reflect(-normalize(light.position - FragPos),norm)),normalize(viewPos-FragPos)),0.0),32) * material.specularStrength * light.specularStrength;
    FragColor = vec4(((ambient+diffusion+specular)*vec3(texture(tex,TexCoord)))*material.objectColor*light.lightColor,1.0);
}