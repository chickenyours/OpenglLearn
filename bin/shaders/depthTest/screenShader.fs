//后期处理 (离屏对虚拟世界渲染后处理，结果渲染到屏幕)

#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D screenTexture;
uniform float TIME;
void main()
{
    //保留
    //FragColor  = vec4(texture(screenTexture, TexCoords).rgb,1.0);     

    //颜色反转
    //FragColor = vec4(vec3(1.0 - texture(screenTexture, TexCoords)), 1.0);

    //转灰度
    // FragColor = texture(screenTexture, TexCoords);
    // float average = (FragColor.r + FragColor.g + FragColor.b) / 3.0;
    // FragColor = vec4(average, average, average, 1.0);

    //敏感灰度
    //human eye tends to be more sensitive to green colors and the least to blue. 
    //So to get the most physically accurate results we'll need to use weighted channels:
    // FragColor = texture(screenTexture, TexCoords);
    // float average = 0.2126 * FragColor.r + 0.7152 * FragColor.g + 0.0722 * FragColor.b;
    // FragColor = vec4(average, average, average, 1.0);

    //Kernel effects
    const float offset = 1.0 / 300.0;  
    vec2 offsets[9] = vec2[](
        vec2(-offset,  offset), // top-left
        vec2( 0.0f,    offset), // top-center
        vec2( offset,  offset), // top-right
        vec2(-offset,  0.0f),   // center-left
        vec2( 0.0f,    0.0f),   // center-center
        vec2( offset,  0.0f),   // center-right
        vec2(-offset, -offset), // bottom-left
        vec2( 0.0f,   -offset), // bottom-center
        vec2( offset, -offset)  // bottom-right    
    );

    // //emboss
    // float kernel[9] = float[](
    //     -2, -1, 0,
    //     -1,  1, 1,
    //     0, 1, 2
    // );
    

    // //高斯模糊
    // float kernel[9] = float[](
    //     1.0 / 16, 2.0 / 16, 1.0 / 16,
    //     2.0 / 16, 4.0 / 16, 2.0 / 16,
    //     1.0 / 16, 2.0 / 16, 1.0 / 16  
    // );
    

    //边缘探测
    float kernel[9] = float[](
        1,1,1,
        1,-8,1,
        1,1,1
    );
    vec3 sampleTex[9];
    for(int i = 0; i < 9; i++)
    {
        sampleTex[i] = vec3(texture(screenTexture, TexCoords.st + offsets[i]));
    }
    vec3 col = vec3(0.0);
    for(int i = 0; i < 9; i++)
        col += sampleTex[i] * kernel[i];
    
    FragColor = vec4(col, 1.0);

    /*
    增强中心像素：中心元素（15）的权重远高于周围元素（-2），这会显著增强该像素的亮度。
    抑制周围像素：周围的元素都是负值，这意味着这些像素的值会被减去，从而使边缘周围的对比度增加。
    边缘强化：当应用这个卷积核时，图像中的边缘会变得更加明显，产生一种清晰的效果。
    这种锐化操作在图像处理和计算机视觉中常用于突出图像细节，增强清晰度。不过，过度使用可能会引入噪声和伪影
    */
   
    /*
    高斯模糊
    模糊效果：卷积核的中心元素（4/16）具有最大的权重，而周围的元素权重较小，这种权重分布模仿了高斯函数的特征，可以有效地将周围像素的颜色混合，从而产生模糊效果。
    平滑过渡：由于周围像素对中心像素的影响较小，模糊效果会非常平滑，减少图像中的噪声和细节，适用于去除不必要的纹理和细节。
    边缘保持：相对于一些简单的平均模糊，使用高斯核模糊时，能够更好地保留边缘信息，同时实现平滑效果。
    */

    //泛光
    // 从屏幕纹理中获取当前像素颜色
    // float threshold = 0.2; // 泛光阈值
    // float intensity = 1.5; // 泛光强度
    // vec4 color = texture(screenTexture, TexCoords);

    // // 计算亮度
    // float brightness = max(max(color.r, color.g), color.b);

    // // 如果亮度高于阈值，则应用泛光效果
    // if (brightness > threshold)
    // {
    //     // 使用高斯模糊或其他方法处理泛光
    //     vec4 glowColor = vec4(0.0);
        
    //     // 在此处你可以实现模糊效果，例如用一个简单的卷积核
    //     // 这里为了简单起见，只是将颜色加倍
    //     glowColor = color * intensity;

    //     // 混合原始颜色和泛光颜色
    //     FragColor = color + glowColor;
    // }
    // else
    // {
    //     // 如果不高于阈值，则使用原始颜色
    //     FragColor = color;
    // }

    //Vignette效果
        // // 计算纹理坐标相对于中心的偏移量
        
        // vec2 v = TexCoords - vec2(0.5, 0.5);
        // // 计算偏移量
        // vec2 offset = sin((length(v) * 5.0 + TIME) * 3.14159 * 2.0) * normalize(v) * 0.1;

        // // 采样并加上偏移量
        // FragColor = texture(screenTexture, TexCoords + offset);

} 