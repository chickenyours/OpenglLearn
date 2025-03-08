#version 330 core

in vec2 TexCoords;

uniform sampler2D downTex;      //level - i
uniform sampler2D preTex;       //level - i + 1

uniform int level;
uniform int currentLevel;
uniform float baseWeight;
uniform float growthRate;

out vec4 FragColor;

float GetWeightFromLevel(int currentLevel){
    return baseWeight * pow(1.0 + growthRate, currentLevel); // 权重随层数递增
    //return baseWeight * growthRate * currentLevel;
}

void main(){
    FragColor = texture(downTex,TexCoords) * GetWeightFromLevel(currentLevel) + texture(preTex,TexCoords);
}