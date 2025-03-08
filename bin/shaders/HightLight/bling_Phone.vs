#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec3 aNormal;

out vec3 Normal;
out vec3 FragPos;
out vec2 TexCoord;
out vec3 EyePos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main(){
    vec4 pos = model * vec4(aPos,1.0);
    gl_Position = projection * view * pos;
    FragPos = vec3(model * vec4(aPos,1.0)) ;
    TexCoord = aTexCoord;
    mat3 normalMatrix = mat3(transpose(inverse(model)));
    Normal = normalize(vec3(vec4(normalMatrix * aNormal, 0.0)));
    //Normal = normalize(aNormal);
    EyePos = vec3(view[3][0], view[3][1], view[3][2]);
}