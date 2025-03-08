#version 330 core

in VS_OUT {
    vec3 FragPos;
    vec2 TexCoords;
    //切线空间的使用
    vec3 TangentLightPos;
    vec3 TangentFragPos;
    vec3 TangentViewPos;
} fs_in;

out vec4 FragColor;

uniform sampler2D diffuseMap;
uniform sampler2D normalMap;
uniform sampler2D parallaxMap;

uniform float heightScale;

vec2 ParallaxMapping(vec2 texCoords, vec3 viewDir);
vec2 ComputeParallaxMapping(vec2 texCoords, vec3 viewDir, sampler2D parallaxMap);

void main(){

    vec3 lightDir = normalize(fs_in.TangentLightPos - fs_in.TangentFragPos);
    vec3 viewDir = normalize(fs_in.TangentViewPos - fs_in.TangentFragPos);

    vec2 parallaxTexCoords  = ParallaxMapping(fs_in.TexCoords,  viewDir); 
    //vec2 parallaxTexCoords  = ComputeParallaxMapping(fs_in.TexCoords,  viewDir,parallaxMap);
    vec3 normal  = texture(normalMap, parallaxTexCoords).xyz;
    normal = normalize(normal * 2.0 - 1.0);
    
    vec3 color = texture(diffuseMap, parallaxTexCoords ).rgb;

    float ambient = 0.1;

    float diff = max(dot(normal, lightDir), 0.0);

    float spec = 0.0;
    if (diff != 0.0){
        vec3 halfwayDir = normalize(viewDir+lightDir); //计算半程向量
        spec = pow(max(dot(normal,halfwayDir), 0.0),32.0);
    }

    FragColor = vec4((ambient + diff + spec) * color,1.0);
}

vec2 ParallaxMapping(vec2 texCoords, vec3 viewDir)
{ 
    //高度模近似视差处理(特定视角下误差极大)
    // float height =  texture(parallaxMap, texCoords).r;     
    // return texCoords - viewDir.xy / viewDir.z * (height * heightScale); 

    //(陡峭视差映射,线性多级深度层比较)
    // const float numLayers = 100;             //等分层数
    // float layerDepth = 1.0 / numLayers;     //间隔距离
    // float currentLayerDepth = 0.0;          //初始化深度
    // vec2 P = viewDir.xy * heightScale;      //总映射向量
    // vec2 deltaTexCoords = P / numLayers;    
    // vec2  currentTexCoords     = texCoords;
    // float currentDepthMapValue = texture(parallaxMap, currentTexCoords).r;
    // while(currentLayerDepth < currentDepthMapValue)
    // {
    //     currentTexCoords -= deltaTexCoords;
    //     currentDepthMapValue = texture(parallaxMap, currentTexCoords).r;  
    //     currentLayerDepth += layerDepth;  
    // }
    // return currentTexCoords;


    // // number of depth layers
    const float minLayers = 30;
    const float maxLayers = 200;
    float numLayers = mix(maxLayers, minLayers, abs(dot(vec3(0.0, 0.0, 1.0), viewDir)));  
    // calculate the size of each layer
    float layerDepth = 1.0 / numLayers;
    // depth of current layer
    float currentLayerDepth = 0.0;
    // the amount to shift the texture coordinates per layer (from vector P)
    vec2 P = viewDir.xy / viewDir.z * heightScale; 
    vec2 deltaTexCoords = P / numLayers;
  
    // get initial values
    vec2  currentTexCoords     = texCoords;
    float currentDepthMapValue = texture(parallaxMap, currentTexCoords).r;
      
    while(currentLayerDepth < currentDepthMapValue)
    {
        // shift texture coordinates along direction of P
        currentTexCoords -= deltaTexCoords;
        // get depthmap value at current texture coordinates
        currentDepthMapValue = texture(parallaxMap, currentTexCoords).r;  
        // get depth of next layer
        currentLayerDepth += layerDepth;  
    }
    
    // -- parallax occlusion mapping interpolation from here on
    // get texture coordinates before collision (reverse operations)
    vec2 prevTexCoords = currentTexCoords + deltaTexCoords;

    // get depth after and before collision for linear interpolation
    float afterDepth  = currentDepthMapValue - currentLayerDepth; //结果为负
    float beforeDepth = texture(parallaxMap, prevTexCoords).r - currentLayerDepth + layerDepth; //正
 
    // interpolation of texture coordinates
    float weight = afterDepth / (afterDepth - beforeDepth);
    vec2 finalTexCoords = prevTexCoords * weight + currentTexCoords * (1.0 - weight);

    return finalTexCoords;


   
}


vec2 ComputeParallaxMapping(vec2 texCoords, vec3 viewDir, sampler2D parallaxMap) {
    // 常量设置：最大视差和偏置
    const float maxParallax = 0.1;  // 最大视差量，控制凹陷效果的强度
    const float bias = 0.05;         // 偏置值，控制视差的开始位置

    // 计算视差偏移：根据视距来调整偏移
    float height = texture(parallaxMap, texCoords).r; // 取红色通道的值作为深度信息
    float parallax = bias + height * maxParallax;

    // 视差偏移量，viewDir.xy 是视角的水平和垂直分量，viewDir.z 是视距（深度）
    vec2 P = viewDir.xy / viewDir.z * parallax;  // 调整偏移量比例
    float layerDepth = 1.0 / 200.0;  // 每一层的深度（200层）
    float currentLayerDepth = 0.0;
    vec2 currentTexCoords = texCoords;
    float currentDepthMapValue = texture(parallaxMap, currentTexCoords).r;

    // 计算每一层的纹理坐标偏移，直到达到深度图的值
    while(currentLayerDepth < currentDepthMapValue) {
        currentTexCoords -= P / 200.0;  // 每次按比例偏移
        currentDepthMapValue = texture(parallaxMap, currentTexCoords).r;
        currentLayerDepth += layerDepth;
    }

    // 反向插值以平滑视差过渡
    vec2 prevTexCoords = currentTexCoords + P / 200.0; // 恢复到上一个层级的坐标
    float afterDepth = currentDepthMapValue - currentLayerDepth;
    float beforeDepth = texture(parallaxMap, prevTexCoords).r - currentLayerDepth + layerDepth;

    // 线性插值纹理坐标
    float weight = afterDepth / (afterDepth - beforeDepth);
    vec2 finalTexCoords = prevTexCoords * weight + currentTexCoords * (1.0 - weight);

    // 返回最终计算出的纹理坐标
    return finalTexCoords;
}
