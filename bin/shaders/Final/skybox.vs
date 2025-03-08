#version 330 core 

layout(location = 0) in vec3 aPos;

out vec3 FragPos;

uniform mat4 projection;
uniform mat4 view;

void main(){
    vec4 Pos =  vec4(aPos,1.0);
    gl_Position = (projection * mat4(mat3(view)) * Pos).xyww;
    FragPos = Pos.xyz;
}