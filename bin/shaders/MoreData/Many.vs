#version 330 core

// layout (std140) uniform Matrices {
//     uniform mat4 view;
//     uniform mat4 projection;
// };

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in mat4 aModels;

uniform mat4 view;
uniform mat4 projection;
//uniform mat4 model;
uniform mat4 baseModel;

uniform vec2 offsets[100];

out vec3 Eye;
out vec2 TexCoord;
out vec3 Pos;
out vec3 Normal;

void main() {
    //vec2 offset = offsets[gl_InstanceID];
    //gl_Position = vec4(aPos.xy * 2.0 + offset,0.0,1.0);
    gl_Position = projection * view * baseModel * aModels * vec4(aPos, 1.0);
    Pos = (aModels * baseModel * vec4(aPos, 1.0)).xyz;
    Normal = normalize(mat3(transpose(inverse(aModels))) * aNormal);
    Eye = vec3(view[3][0], view[3][1], view[3][2]);
    TexCoord = aTexCoord;
}