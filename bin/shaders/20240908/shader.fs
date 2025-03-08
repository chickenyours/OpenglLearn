#version 330 core

in vec3 Normal;
in vec2 texCoord;

uniform sampler2D diffusion;

out vec4 FragColor;
void main()
{
    FragColor = texture(diffusion,texCoord);
} 