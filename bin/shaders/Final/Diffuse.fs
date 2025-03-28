#version 460 core

in vec2 TexCoords;

out vec4 fragColor;

layout (binding = 0) uniform sampler2D diffuse;

void main()
{    
    fragColor = texture(diffuse, TexCoords);
}