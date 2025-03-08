#version 330 core
out vec4 FragColor;

in vec3 TexCoords;

uniform samplerCube skybox;

void main()
{    

    //天空盒早期绘制
    FragColor = texture(skybox, TexCoords);
     

}