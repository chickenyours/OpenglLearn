#version 330 core
layout (location = 0) in vec3 aPos; // the position variable has attribute position 0
layout (location = 1) in vec2 aTexCoord; // the position variable has attribute position 0
layout (location = 2) in vec3 aNormal;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;

out vec3 Normal;
out vec2 texCoord;

void main()
{
    gl_Position = projection * view * model * vec4(aPos,1.0);
    Normal = aNormal;
    texCoord = aTexCoord;
}