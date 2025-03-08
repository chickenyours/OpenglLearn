#version 330 core 

in vec2 TexCoords;

uniform sampler2D colorBuffer;


out vec4 FragColor;

void main(){
    FragColor = texture(colorBuffer,TexCoords);
    // if(a!=vec4(0.0,0.0,0.0,1.0)){
    //     FragColor = vec4(1.0);
    // }
    // else{
    //     FragColor = vec4(0.0,0.0,0.0,1.0);
    // }
    
     
}