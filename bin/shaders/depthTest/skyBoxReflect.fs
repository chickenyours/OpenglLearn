#version 330 core

in vec3 Normal;
//in vec3 EyePos;
in vec3 FragPos;

uniform vec3 EyePos;
uniform samplerCube skyCube;

out vec4 FragColor;

void main(){

    //反射
        // vec3 I = normalize(FragPos - EyePos);
        // vec3 R = reflect(I,normalize(Normal));
        // //入射方向：入射向量 I 通常是朝向法线的反方向（即指向表面），在 GLSL 中通常是从表面指向光源。
        // //反射方向：反射向量 𝑅 是关于法线 𝑁 对称的，意味着它是从入射点出发，沿着反射方向离开表面。
        // FragColor = vec4(texture(skyCube,R).rgb,1.0);
        // //FragColor = vec4(R,1.0);
        // //FragColor = vec4(1.0);
    //折射
    float ratio = 1.00 / 1.52;
    vec3 I = normalize(FragPos - EyePos);
    vec3 R = refract(I, normalize(Normal), ratio);
    FragColor = vec4(texture(skyCube, R).rgb, 1.0);
}