#include "material.h"

#include <iostream>

using namespace std;

using namespace Render;

map<string, float> Material::GlobalFloatParameterMap;
map<string, int> Material::GlobalIntParameterMap;
map<string, glm::vec3> Material::GlobalVec3ParameterMap;
map<string, glm::vec4> Material::GlobalVec4ParameterMap;
map<string, glm::mat4> Material::GlobalMat4ParameterMap;

Material::Material(const aiMaterial& material, const Json::Value& materialJson):name("default"),id(0),propertyFlag(0){
    LoadParameterFromModelAiMaterial(material);
    if (!materialJson.isNull() && !materialJson.empty()){
        LoadParameterFromConfigFile(materialJson);
    }
}

Material::Material(Material&& other) {
    if (this != &other) { // 避免自我赋值
        std::cout<< "调用ShaderPrograme转移构造函数: Name: " << other.name << std::endl;
        name = std::move(other.name);
        id = other.id;
        textures = std::move(other.textures);
        textureChannel = std::move(other.textureChannel);
        floatParameterMap = std::move(other.floatParameterMap);
        intParameterMap = std::move(other.intParameterMap);
        vec3ParameterMap = std::move(other.vec3ParameterMap);
        vec4ParameterMap = std::move(other.vec4ParameterMap);
        mat4ParameterMap = std::move(other.mat4ParameterMap);
        propertyFlag = other.propertyFlag;

        // 释放当前指针 (如果已有)
        shaderProgram = std::move(other.shaderProgram);  
        other.shaderProgram = nullptr;

        other.id = 0;
        other.propertyFlag = 0;
    }
}

void Material::BindAllTexture(){
    // 设置纹理
    for(int i = 0;i < textureChannel.size();i++){
        glActiveTexture(GL_TEXTURE0 + textureChannel[i]);
        glBindTexture(GL_TEXTURE_2D,textures[i].GetID());
    }
}

void Material::SetShaderParams(){
    // 使用着色器
    if(!shaderProgram){
        return;
    }
    shaderProgram->Use();
   
    // 设置参数
    for(auto it = floatParameterMap.begin();it != floatParameterMap.end();it++){
        ShaderU1f(*shaderProgram,it->first,it->second);
    }
    for(auto it = GlobalFloatParameterMap.begin();it != GlobalFloatParameterMap.end();it++){
        ShaderU1f(*shaderProgram,it->first,it->second);
    }
    
    for(auto it = intParameterMap.begin();it != intParameterMap.end();it++){
        ShaderU1i(*shaderProgram,it->first,it->second);
    }
    for(auto it = GlobalIntParameterMap.begin();it != GlobalIntParameterMap.end();it++){
        ShaderU1i(*shaderProgram,it->first,it->second);
    }
    for(auto it = vec3ParameterMap.begin();it != vec3ParameterMap.end();it++){
        ShaderUvec3(*shaderProgram,it->first,it->second);
    }
    for(auto it = GlobalVec3ParameterMap.begin();it != GlobalVec3ParameterMap.end();it++){
        ShaderUvec3(*shaderProgram,it->first,it->second);
    }
    for(auto it = vec4ParameterMap.begin();it != vec4ParameterMap.end();it++){
        ShaderUvec4(*shaderProgram,it->first,it->second);
    }
    for(auto it = GlobalVec4ParameterMap.begin();it != GlobalVec4ParameterMap.end();it++){
        ShaderUvec4(*shaderProgram,it->first,it->second);
    }
    for(auto it = mat4ParameterMap.begin();it != mat4ParameterMap.end();it++){
        ShaderUmatf4(*shaderProgram,it->first,it->second);
    }
    for(auto it = GlobalMat4ParameterMap.begin();it != GlobalMat4ParameterMap.end();it++){
        ShaderUmatf4(*shaderProgram,it->first,it->second);
    }
}

void Material::LoadParameterFromModelAiMaterial(const aiMaterial& material){
    //加载材质信息
    aiString name;
    if (material.Get(AI_MATKEY_NAME, name) == AI_SUCCESS) {
        this->name = name.C_Str();
    }
    // Ambient (环境光)
    aiColor4D ambientColor;
    if (material.Get(AI_MATKEY_COLOR_AMBIENT, ambientColor) == AI_SUCCESS) {
        vec4ParameterMap["ambient"] = glm::vec4(ambientColor.r, ambientColor.g, ambientColor.b, ambientColor.a);
    }

    // Diffuse (漫反射)
    aiColor4D diffuseColor;
    if (material.Get(AI_MATKEY_COLOR_DIFFUSE, diffuseColor) == AI_SUCCESS) {
        vec4ParameterMap["diffuse"] = glm::vec4(diffuseColor.r, diffuseColor.g, diffuseColor.b, diffuseColor.a);
    }

    // Specular (镜面反射)
    aiColor4D specularColor;
    if (material.Get(AI_MATKEY_COLOR_SPECULAR, specularColor) == AI_SUCCESS) {
        vec4ParameterMap["specular"] = glm::vec4(specularColor.r, specularColor.g, specularColor.b, specularColor.a);
    }

    // Emissive (自发光)
    aiColor4D emissiveColor;
    if (material.Get(AI_MATKEY_COLOR_EMISSIVE, emissiveColor) == AI_SUCCESS) {
        vec4ParameterMap["emissive"] = glm::vec4(emissiveColor.r, emissiveColor.g, emissiveColor.b, emissiveColor.a);
    }

    // Shininess (光泽度)
    float shininess = 0.0f;
    if (material.Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS) {
        floatParameterMap["shininess"] = shininess;
    }

    // ni(折射率)
    float ni = 0.0f;
    if (material.Get(AI_MATKEY_REFRACTI, ni) == AI_SUCCESS) {
        floatParameterMap["ni"] = ni;
    }

    // 透明度
    float opacity = 1.0f;
    if (material.Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS) {
        floatParameterMap["opacity"] = opacity;
    }

    // Transparency
    float transparency = 1.0f;
    if (material.Get(AI_MATKEY_COLOR_TRANSPARENT, transparency) == AI_SUCCESS) {
        floatParameterMap["transparency"] = transparency;
    }

    // 光照模型
    int shadingModel = 0;
    if (material.Get(AI_MATKEY_SHADING_MODEL, shadingModel) == AI_SUCCESS) {
        intParameterMap["shadingModel"] = shadingModel;
    }

    //PBR属性
    // Metalness (金属度)
    float metalness = 0.0f;
    if (material.Get(AI_MATKEY_METALLIC_FACTOR, metalness) == AI_SUCCESS) {
        floatParameterMap["metalness"] = metalness;
    }



    // Roughness
    float roughness = 0.0f;
    if (material.Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS) {
        floatParameterMap["roughness"] = roughness;
    }

    //加载纹理元数据信息
}

void Material::LoadParameterFromConfigFile(const Json::Value& materialJson){
    // 检索材质配置文件,已知materialJson是一个数组,循环找到同名的材质配置
    try {
        // Load shader program
        Json::Value shaderProgramPath = materialJson["shaderProgramPath"];
        std::string vertexShaderPath = shaderProgramPath["vertexShader"].asString();
        std::string fragmentShaderPath = shaderProgramPath["fragmentShader"].asString();
        std::string geometryShaderPath = shaderProgramPath["geometryShader"].asString();
        
        shaderProgram = std::make_unique<ShaderProgram>(vertexShaderPath, fragmentShaderPath, geometryShaderPath);
        

        // Load textures
        Json::Value materialTextures = materialJson["textures"];
        for (Json::Value::const_iterator it = materialTextures.begin(); it != materialTextures.end(); ++it) {
            std::string textureType = it.key().asString();
            std::string texturePath = (*it)[0].asString();
            unsigned int textureChannelValue = (*it)[1].asUInt();
            textures.push_back(Texture(textureType,texturePath));
            textureChannel.push_back(textureChannelValue);
        }

        // Load properties
        Json::Value properties = materialJson["properties"];
        
        // Load float properties
        Json::Value floatProperties = properties["float"];
        for (Json::Value::const_iterator it = floatProperties.begin(); it != floatProperties.end(); ++it) {
            std::string propertyName = it.key().asString();
            float propertyValue = (*it).asFloat();
            floatParameterMap[propertyName] = propertyValue;
        }

        // Load int properties
        Json::Value intProperties = properties["int"];
        for (Json::Value::const_iterator it = intProperties.begin(); it != intProperties.end(); ++it) {
            std::string propertyName = it.key().asString();
            int propertyValue = (*it).asInt();
            intParameterMap[propertyName] = propertyValue;
        }

        // Load vec3 properties
        Json::Value vec3Properties = properties["vec3"];
        for (Json::Value::const_iterator it = vec3Properties.begin(); it != vec3Properties.end(); ++it) {
            std::string propertyName = it.key().asString();
            glm::vec3 propertyValue((*it)[0].asFloat(), (*it)[1].asFloat(), (*it)[2].asFloat());
            vec3ParameterMap[propertyName] = propertyValue;
        }

        // Load vec4 properties
        Json::Value vec4Properties = properties["vec4"];
        for (Json::Value::const_iterator it = vec4Properties.begin(); it != vec4Properties.end(); ++it) {
            std::string propertyName = it.key().asString();
            glm::vec4 propertyValue((*it)[0].asFloat(), (*it)[1].asFloat(), (*it)[2].asFloat(), (*it)[3].asFloat());
            vec4ParameterMap[propertyName] = propertyValue;
        }

        // Load mat4 properties
        Json::Value mat4Properties = properties["mat4"];
        for (Json::Value::const_iterator it = mat4Properties.begin(); it != mat4Properties.end(); ++it) {
            std::string propertyName = it.key().asString();
            glm::mat4 propertyValue;
            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 4; j++) {
                    propertyValue[i][j] = (*it)[i][j].asFloat();
                }
            }
            mat4ParameterMap[propertyName] = propertyValue;
        }

        // 获取属性标志位
        Json::Value propertyFlagValue = materialJson["flags"];
        for (int i = 0; i < 2; i++) {
            if (propertyFlagValue[i].asBool()) {
                propertyFlag |= (1 << i);
            }
        }
    }
    catch (const std::exception& e) {
        std::cout<< "Error loading material: " << name << std::endl;
        //打印 Json::Value& materialJson的所有数据
        std::cout << materialJson << std::endl;
        std::cerr << e.what() << std::endl;
    }
    
}

void Material::LoadAllTexture(){
    for(int i = 0;i < textures.size();i++){
        // 如果加载成功,则将通道值加入m_TextureChannel
        textures[i].Load();
    }
}

void Material::Print(int tabs){
    string tab = "";
    for(int i = 0; i< tabs; i++){
        tab += "\t";
    }

    cout << tab <<"======MaterialInfo======"<<endl;

    cout << tab << "Name: "<<name<<endl;

    if(shaderProgram){
        shaderProgram->Print(tabs + 1);
    }

    cout << tab << "Textures:"<<endl;
    for(int i = 0;i < textures.size();i++){
        textures[i].Print(tabs + 1);
    }

    cout << tab << "Properties:"<<endl;

    cout << tab + "\t" << "float:"<<endl;
    for(auto it = floatParameterMap.begin();it != floatParameterMap.end();it++){
        cout<< tab + "\t\t" <<it->first<<" "<<it->second<<endl;
    }

    cout << tab + "\t" << "int:"<<endl;
    for(auto it = intParameterMap.begin();it != intParameterMap.end();it++){
        cout<< tab + "\t\t" <<it->first<<" "<<it->second<<endl;
    }

    cout << tab + "\t" << "vec3:"<<endl;
    for(auto it = vec3ParameterMap.begin();it != vec3ParameterMap.end();it++){
        cout<< tab + "\t\t" <<it->first<<" "<<it->second.x<<" "<<it->second.y<<" "<<it->second.z<<endl;
    }

    cout << tab + "\t" << "vec4:"<<endl;
    for(auto it = vec4ParameterMap.begin();it != vec4ParameterMap.end();it++){
        cout<< tab + "\t\t" <<it->first<<" "<<it->second.x<<" "<<it->second.y<<" "<<it->second.z<<" "<<it->second.w<<endl;
    }

    cout << tab + "\t" << "mat4:"<<endl;
    for(auto it = mat4ParameterMap.begin();it != mat4ParameterMap.end();it++){
        cout<< tab + "\t\t" <<it->first<<endl;
        for(int i = 0;i < 4;i++){
            for(int j = 0;j < 4;j++){
                cout<<it->second[i][j]<<" ";
            }
            cout<<endl;
        }
    }

    cout << tab <<"======EndMaterialInfo======"<<endl;

}


Material::~Material(){
    if(!name.empty()){
        cout<<"destroy material: "<<name<<std::endl;
    }
}
