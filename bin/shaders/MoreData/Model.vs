#version 330 core

layout (std140) uniform Matrices {
    uniform mat4 view;
    uniform mat4 projection;
};

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec3 aNormal;

uniform mat4 model;

out VS_OUT {
    //vec3 Pos;
    //vec3 Normal;
    vec2 texCoords;
} vs_out;





out vec3 Eye;


void main() {
    gl_Position = view * model * vec4(aPos, 1.0);
    //Pos = (model * vec4(aPos, 1.0)).xyz;
    //Normal = normalize(mat3(transpose(inverse(model))) * aNormal);
    //Eye = vec3(view[3][0], view[3][1], view[3][2]);
    vs_out.texCoords = aTexCoord;  // 确保这里正确地将 aTexCoord 赋值给 TexCoord
}