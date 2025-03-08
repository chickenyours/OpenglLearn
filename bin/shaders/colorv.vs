#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 vColor;
out vec4 vertexColor;
uniform float movement;
void main(){
    gl_Position = vec4(aPos.x+sin(movement)/2,aPos.y+cos(movement)/2,aPos.z,1.0);
    vertexColor = vec4(vColor,1.0);
}
