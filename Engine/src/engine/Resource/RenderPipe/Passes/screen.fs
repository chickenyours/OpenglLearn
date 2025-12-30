#version 460 core

in vec2 TexCoord;

uniform sampler2D colorBufferTex;

out vec4 FragColor;

void main()
{
    // Sample the color buffer texture at the given texture coordinates
    vec4 color = texture(colorBufferTex, TexCoord);
    
    // Output the sampled color
    FragColor = color;
}