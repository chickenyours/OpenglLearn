#version 330 core 

in vec2 TexCoords;

uniform sampler2D colorBuffer;


out vec4 FragColor;

void main(){
    const float brightnessThreshold = 0.8; // 亮度阈值
    // 从颜色缓冲区中获取颜色
    vec4 color = texture(colorBuffer, TexCoords);
    
    // 计算亮度，可以使用简单的加权平均法
    float brightness = dot(color.rgb, vec3(0.299, 0.587, 0.114)); // 使用人眼对颜色的敏感度
    
    // 进行亮度阈值测试
    if (brightness > brightnessThreshold) {
        FragColor = color*1.0; // 亮度高于阈值，输出原颜色
    } else {
        FragColor = vec4(0.0); // 亮度低于阈值，输出黑色（或其他颜色）
    }
    
}