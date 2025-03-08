// #version 330 core
// layout (triangles) in;
// layout (triangle_strip, max_vertices = 3) out;

// in VS_OUT {
//     vec2 texCoords;
// } gs_in[];

// out vec2 TexCoord; 

// uniform float time;

// vec3 GetNormal()
// {
//    vec3 a = vec3(gl_in[0].gl_Position) - vec3(gl_in[1].gl_Position);
//    vec3 b = vec3(gl_in[2].gl_Position) - vec3(gl_in[1].gl_Position);
//    return normalize(cross(a, b));
// }

// vec4 explode(vec4 position, vec3 normal) {
//     float magnitude = 2.0;
//     vec3 direction = normal * ((sin(time) + 1.0) / 2.0) * magnitude; 
//     return position + vec4(direction, 0.0);
// }

// void main() {    
//     vec3 normal = GetNormal();
//     gl_Position = explode(gl_in[0].gl_Position, normal);
//     TexCoord = gs_in[0].texCoords;
//     EmitVertex();
//     gl_Position = explode(gl_in[1].gl_Position, normal);
//     TexCoord = gs_in[1].texCoords;
//     EmitVertex();
//     gl_Position = explode(gl_in[2].gl_Position, normal);
//     TexCoord = gs_in[2].texCoords;
//     EmitVertex();
//     EndPrimitive();
// }

#version 330 core
layout (triangles) in;
layout (triangle_strip, max_vertices = 3) out;

layout (std140) uniform Matrices {
    mat4 view;
    mat4 projection;
};


in VS_OUT {
    vec2 texCoords;
} gs_in[];

out vec2 TexCoords; 

uniform float time;

vec4 explode(vec4 position, vec3 normal)
{
    float magnitude = 5;
    vec3 direction = normal * ((sin(time*3.0) + 1.0) / 2.0) * magnitude; 
    return projection*(position+vec4(direction,0.0));
}

vec3 GetNormal()
{
    vec3 a = vec3(gl_in[0].gl_Position) - vec3(gl_in[1].gl_Position);
    vec3 b = vec3(gl_in[2].gl_Position) - vec3(gl_in[1].gl_Position);
    return normalize(cross(a, b));
}

void main() {    
    // vec3 normal = GetNormal();

    // gl_Position = explode(gl_in[0].gl_Position, normal);
    // TexCoords = gs_in[0].texCoords;
    // EmitVertex();
    // gl_Position = explode(gl_in[1].gl_Position, normal);
    // TexCoords = gs_in[1].texCoords;
    // EmitVertex();
    // gl_Position = explode(gl_in[2].gl_Position, normal);
    // TexCoords = gs_in[2].texCoords;
    // EmitVertex();
    // EndPrimitive();

    gl_Position = projection * gl_in[0].gl_Position;
    TexCoords = gs_in[0].texCoords;
    EmitVertex();
    gl_Position = projection * gl_in[1].gl_Position;
    TexCoords = gs_in[1].texCoords;
    EmitVertex();
    gl_Position = projection * gl_in[2].gl_Position;
    TexCoords = gs_in[2].texCoords;
    EmitVertex();
    EndPrimitive();
}