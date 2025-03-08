#version 330 core 

in vec2 TexCoords;

uniform sampler2D colorBuffer;
uniform int isy;

out vec4 FragColor;

void main()
{
//     ivec2 texSize = textureSize(colorBuffer, 0);
//     vec2 offset = vec2(1.0 / texSize.x, 1.0 / texSize.y);
//     vec3 col = vec3(0.0);
//     float kernel[5] = float[](0.0545,0.2447,0.4026,0.2447,0.0545);
//    if (isy == 1) {  // 垂直模糊
//         col += kernel[0] * texture(colorBuffer, vec2(TexCoords.x, clamp(TexCoords.y - 2.0 * offset.y, 0.0, 1.0))).rgb;
//         col += kernel[1] * texture(colorBuffer, vec2(TexCoords.x, clamp(TexCoords.y - offset.y, 0.0, 1.0))).rgb;
//         col += kernel[2] * texture(colorBuffer, TexCoords).rgb;
//         col += kernel[3] * texture(colorBuffer, vec2(TexCoords.x, clamp(TexCoords.y + offset.y, 0.0, 1.0))).rgb;
//         col += kernel[4] * texture(colorBuffer, vec2(TexCoords.x, clamp(TexCoords.y + 2.0 * offset.y, 0.0, 1.0))).rgb;
//     } else {  // 水平模糊
//         col += kernel[0] * texture(colorBuffer, vec2(clamp(TexCoords.x - 2.0 * offset.x, 0.0, 1.0), TexCoords.y)).rgb;
//         col += kernel[1] * texture(colorBuffer, vec2(clamp(TexCoords.x - offset.x, 0.0, 1.0), TexCoords.y)).rgb;
//         col += kernel[2] * texture(colorBuffer, TexCoords).rgb;
//         col += kernel[3] * texture(colorBuffer, vec2(clamp(TexCoords.x + offset.x, 0.0, 1.0), TexCoords.y)).rgb;
//         col += kernel[4] * texture(colorBuffer, vec2(clamp(TexCoords.x + 2.0 * offset.x, 0.0, 1.0), TexCoords.y)).rgb;
//     }
    
    vec2 texSize = vec2(textureSize(colorBuffer, 0));
    vec2 offset = vec2(1.0) / texSize;

    float kernel[5] = float[](0.0545, 0.2447, 0.4026, 0.2447, 0.0545);
    vec3 col = vec3(0.0);
    for (int i = -2; i <= 2; ++i) {
        vec2 texOffset = isy == 1 ? vec2(0, i * offset.y) : vec2(i * offset.x, 0);
        vec3 sample = texture(colorBuffer, TexCoords + texOffset).rgb;

        // 增强边缘对比
        float weight = kernel[i + 2]; 
        col += weight * sample;
    }

    FragColor = vec4(col, 1.0);

   
}


// #version 330 core
// out vec4 FragColor;

// in vec2 TexCoords;

// uniform sampler2D screenTexture;

// void main()
// {
//     ivec2 texSize = textureSize(screenTexture, 0); // 获取纹理尺寸
//     vec2 offset = vec2(1.0 / texSize.x, 1.0 / texSize.y); // 计算偏移量

//     // 5x5 高斯模糊核展平成一维数组
//     float kernel[25] = float[](
//         0.003, 0.013, 0.022, 0.013, 0.003,
//         0.013, 0.059, 0.097, 0.059, 0.013,
//         0.022, 0.097, 0.159, 0.097, 0.022,
//         0.013, 0.059, 0.097, 0.059, 0.013,
//         0.003, 0.013, 0.022, 0.013, 0.003
//     );
    
//     vec3 col = vec3(0.0);
//     int idx = 0;

//     // 执行高斯模糊：应用 5x5 高斯核
//     for (int i = -2; i <= 2; i++) {
//         for (int j = -2; j <= 2; j++) {
//             // 计算偏移后的纹理坐标
//             vec2 offsetCoords = TexCoords + vec2(j * offset.x, i * offset.y);
//             // 采样纹理颜色并应用高斯核
//             col += texture(screenTexture, offsetCoords).rgb * kernel[idx];
//             idx++;
//         }
//     }

//     FragColor = vec4(col, 1.0); // 输出模糊后的颜色
// }
