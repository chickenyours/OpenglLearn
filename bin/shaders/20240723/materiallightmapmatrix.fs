#version 330 core

struct Material{
    sampler2D diffuse;
    sampler2D specular;
    sampler2D emission;
    float shininess;
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

uniform Material material;
uniform Light light;
uniform vec3 viewPos;
uniform float time;


out vec4 FragColor;

void main(){
    vec3 norm = normalize(Normal);
    vec3 ambient =light.ambientStrength * vec3(texture(material.diffuse,TexCoord));
    vec3 diffusion =   max(dot(norm,normalize(light.position - FragPos)),0.0) * light.diffusionStrength * vec3(texture(material.diffuse,TexCoord));
    vec3 specular = pow(max(dot(normalize(reflect(-normalize(light.position - FragPos),norm)),normalize(viewPos-FragPos)),0.0),32) * light.specularStrength * vec3(texture(material.specular,TexCoord));
    vec3 emission = vec3(0);
    if(texture(material.specular,TexCoord).r == 0.0){
        emission = vec3(texture(material.emission,TexCoord+vec2(0.0,time)));
    }
    FragColor = vec4((ambient+diffusion+specular+emission)*light.lightColor,1.0);
}