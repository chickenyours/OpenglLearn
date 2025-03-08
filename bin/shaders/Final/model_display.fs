#version 330 core

out vec4 FragColor;

in vec2 TexCoords;
in vec3 FragPos;
in vec3 Normal;

// 光照参数 (平行光) - 硬编码
const vec3 lightDirection = normalize(vec3(-0.2, -1.0, -0.3));   // 平行光照的方向 (例如向下倾斜)
const vec3 lightColor = vec3(1.0, 1.0, 1.0);                      // 白色的光照
const vec3 ambientColor = vec3(0.2, 0.2, 0.2);                    // 环境光颜色（略暗的灰色）
const float ambientStrength = 0.2;                                 // 环境光强度（较弱）

uniform vec3 viewPos;


uniform sampler2D diffuse;
uniform sampler2D specular;

// 材质参数 - 硬编码
const vec3 objectColor = vec3(1.0, 1.0, 1.0); // 物体的颜色（橙色）
const float shininess = 32.0;                    // 高光系数（较大）

// 计算平行光照颜色的函数
vec3 calculateDirectionalLight(vec3 fragPos, vec3 normal, vec3 lightDir, vec3 lightColor, 
                                vec3 ambientColor, float ambientStrength, vec3 viewPos, float shininess) 
{
    // 法线归一化
    normal = normalize(normal);

    // 环境光
    float ambient = ambientStrength;

    // 漫反射光照
    float diff = max(dot(normal, -lightDir), 0.0);
    
    float spec = 0.0;
    // 如果漫反射值小于0，跳过高光计算
    if (diff > 0.0) {
        // 高光计算
        vec3 viewDir = normalize(viewPos - fragPos);
        vec3 halfDir = normalize(lightDir + viewDir); // 半程向量
        spec = pow(max(dot(normal, halfDir), 0.0), shininess);
    }


    // 返回最终光照值（环境光 + 漫反射光 + 高光）
    return ((ambient + diff) * texture(diffuse,TexCoords).rgb + spec * texture(specular,TexCoords).rgb) * lightColor * objectColor;
}

void main()
{
    // 调用计算平行光的函数
    vec3 result = calculateDirectionalLight(FragPos, Normal, lightDirection, lightColor, ambientColor, 
                                            ambientStrength, viewPos, shininess);

    // 输出片段颜色
    FragColor = vec4(result, 1.0);
}
