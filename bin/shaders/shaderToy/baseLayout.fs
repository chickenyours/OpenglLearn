/*
这是shadertoy代码,我让你转换为GLSL,我的shader格式为
#version 460 core

in vec2 TexCoords; // 输入的纹理坐标作为 fragCoord 使用
out vec4 FragColor;

uniform float iTime;
uniform vec2 iResolution;
 
注意fragCoord是像素位置,不是纹理坐标,请转换时修正一下:

*/


#version 460 core

in vec2 TexCoords; // 输入的纹理坐标作为 fragCoord 使用
out vec4 FragColor;

uniform float iTime;
uniform vec2 iResolution;

