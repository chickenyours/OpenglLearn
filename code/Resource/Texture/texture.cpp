#include "texture.h"

#include <stb_image.h>
#include <filesystem>

#include "code/DebugTool/ConsoleHelp/color_log.h"

#include "code/ToolAndAlgorithm/Json/json_loader.h"


using namespace Resource;

bool Texture::LoadFromConfigFile(const std::string& configFile){

    Json::Value config;

    
    if(!Tool::LoadJsonValueFromFile(configFile, config)){
        LOG_ERROR("RESOURCE TEXTURE","texture config file can't to be load" + configFile);
        return false;
    }
    
    

    if(!config.isObject()){
        LOG_ERROR("RESOURCE TEXTURE","config Object is not exist: " + configFile);
        return false;
    }


    if(!config.isMember("configType") || config["configType"].asString() != "resource"){
        LOG_ERROR("RESOURCE TEXTURE","configType Error: " + configFile);
        return false;
    }

    if(!config.isMember("resource") || !config["resource"].isObject()){
        LOG_ERROR("RESOURCE TEXTURE", "resource Object is not exist: " + configFile);
        return false;
    }

    const Json::Value& resource = config["resource"];

    if(!resource.isMember("resourceType") || resource["resourceType"].asString() != "texture"){
        LOG_ERROR("RESOURCE TEXTURE","resourceType Error: " + configFile);
        return false;
    }

    if(!resource.isMember("texture") || !resource["texture"].isObject()){
        LOG_ERROR("RESOURCE TEXTURE","resource Object is not exist: " + configFile);
        return false;
    }

    const Json::Value& textureConfig = resource["texture"];

    if(!textureConfig.isMember("textureType")){
        LOG_ERROR("RESOURCE TEXTURE","textureType Error: " + configFile);
        return false;
    }

    if(!textureConfig.isMember("args") || !textureConfig["args"].isObject()){
        LOG_ERROR("RESOURCE TEXTURE","args Object is not exist: " + configFile);
        return false;
    }


    



        
    const Json::Value& args = textureConfig["args"];
    std::string textureType = textureConfig["textureType"].asString();

    std::string imagePath = args["path"].asString();
    if (!std::filesystem::exists(imagePath)) {
        LOG_ERROR("RESOURCE TEXTURE","Image file does not exist: " + imagePath);
        return false;
    }

    if(textureType == "2D"){
        const Json::Value& args = textureConfig["args"]; 

        

        uint32_t wrapS = [&]() {
            std::string wrapMode = args["wrapS"].asString();
            if (wrapMode == "REPEAT") return GL_REPEAT;
            else if (wrapMode == "CLAMP_TO_EDGE") return GL_CLAMP_TO_EDGE;
            else if (wrapMode == "MIRRORED_REPEAT") return GL_MIRRORED_REPEAT;
            else if (wrapMode == "CLAMP_TO_BORDER") return GL_CLAMP_TO_BORDER;
            else {
            LOG_WARNING("RESOURCE TEXTURE", "Unknown wrapS mode: " + wrapMode);
            return GL_REPEAT; // Default fallback
            }
        }();
        
        uint32_t wrapT = [&]() {
            std::string wrapMode = args["wrapT"].asString();
            if (wrapMode == "REPEAT") return GL_REPEAT;
            else if (wrapMode == "CLAMP_TO_EDGE") return GL_CLAMP_TO_EDGE;
            else if (wrapMode == "MIRRORED_REPEAT") return GL_MIRRORED_REPEAT;
            else if (wrapMode == "CLAMP_TO_BORDER") return GL_CLAMP_TO_BORDER;
            else {
            LOG_WARNING("RESOURCE TEXTURE", "Unknown wrapT mode: " + wrapMode);
            return GL_REPEAT; // Default fallback
            }
        }();

        uint32_t minFilter = [&]() {
            std::string filterMode = args["minFilter"].asString();
            if (filterMode == "NEAREST") return GL_NEAREST;
            else if (filterMode == "LINEAR") return GL_LINEAR;
            else if (filterMode == "NEAREST_MIPMAP_NEAREST") return GL_NEAREST_MIPMAP_NEAREST;
            else if (filterMode == "LINEAR_MIPMAP_NEAREST") return GL_LINEAR_MIPMAP_NEAREST;
            else if (filterMode == "NEAREST_MIPMAP_LINEAR") return GL_NEAREST_MIPMAP_LINEAR;
            else if (filterMode == "LINEAR_MIPMAP_LINEAR") return GL_LINEAR_MIPMAP_LINEAR;
            else {
            LOG_WARNING("RESOURCE TEXTURE", "Unknown minFilter mode: " + filterMode);
            return GL_LINEAR; // Default fallback
            }
        }();

        uint32_t magFilter = [&]() {
            std::string filterMode = args["magFilter"].asString();
            if (filterMode == "NEAREST") return GL_NEAREST;
            else if (filterMode == "LINEAR") return GL_LINEAR;
            else {
            LOG_WARNING("RESOURCE TEXTURE", "Unknown magFilter mode: " + filterMode);
            return GL_LINEAR; // Default fallback
            }
        }();

        bool needHDR = args.get("needHDR", false).asBool();
        bool needMipMap = args.get("needMipMap", true).asBool();


        if(!LoadFromFile2D(imagePath, wrapS, wrapT, minFilter, magFilter, needHDR, needMipMap)){
            LOG_ERROR("RESOURCE TEXTURE","Failed to load 2D texture from file: " + imagePath);
            return false;
        }
    }
        else if(textureType == "Cube"){
            const Json::Value& args = config["args"];

            // 解析六个面的路径
            std::string posX = args["posX"].asString();
            std::string negX = args["negX"].asString();
            std::string posY = args["posY"].asString();
            std::string negY = args["negY"].asString();
            std::string posZ = args["posZ"].asString();
            std::string negZ = args["negZ"].asString();
        
            // wrapS
            uint32_t wrapS = [&]() {
                std::string wrapMode = args.get("wrapS", "CLAMP_TO_EDGE").asString();
                if (wrapMode == "REPEAT") return GL_REPEAT;
                else if (wrapMode == "CLAMP_TO_EDGE") return GL_CLAMP_TO_EDGE;
                else if (wrapMode == "MIRRORED_REPEAT") return GL_MIRRORED_REPEAT;
                else if (wrapMode == "CLAMP_TO_BORDER") return GL_CLAMP_TO_BORDER;
                else {
                    LOG_WARNING("RESOURCE TEXTURE", "Unknown wrapS mode: " + wrapMode);
                    return GL_CLAMP_TO_EDGE;
                }
            }();
        
            // wrapT
            uint32_t wrapT = [&]() {
                std::string wrapMode = args.get("wrapT", "CLAMP_TO_EDGE").asString();
                if (wrapMode == "REPEAT") return GL_REPEAT;
                else if (wrapMode == "CLAMP_TO_EDGE") return GL_CLAMP_TO_EDGE;
                else if (wrapMode == "MIRRORED_REPEAT") return GL_MIRRORED_REPEAT;
                else if (wrapMode == "CLAMP_TO_BORDER") return GL_CLAMP_TO_BORDER;
                else {
                    LOG_WARNING("RESOURCE TEXTURE", "Unknown wrapT mode: " + wrapMode);
                    return GL_CLAMP_TO_EDGE;
                }
            }();
        
            // wrapR
            uint32_t wrapR = [&]() {
                std::string wrapMode = args.get("wrapR", "CLAMP_TO_EDGE").asString();
                if (wrapMode == "REPEAT") return GL_REPEAT;
                else if (wrapMode == "CLAMP_TO_EDGE") return GL_CLAMP_TO_EDGE;
                else if (wrapMode == "MIRRORED_REPEAT") return GL_MIRRORED_REPEAT;
                else if (wrapMode == "CLAMP_TO_BORDER") return GL_CLAMP_TO_BORDER;
                else {
                    LOG_WARNING("RESOURCE TEXTURE", "Unknown wrapR mode: " + wrapMode);
                    return GL_CLAMP_TO_EDGE;
                }
            }();
        
            // minFilter
            uint32_t minFilter = [&]() {
                std::string filterMode = args.get("minFilter", "LINEAR").asString();
                if (filterMode == "NEAREST") return GL_NEAREST;
                else if (filterMode == "LINEAR") return GL_LINEAR;
                else if (filterMode == "NEAREST_MIPMAP_NEAREST") return GL_NEAREST_MIPMAP_NEAREST;
                else if (filterMode == "LINEAR_MIPMAP_NEAREST") return GL_LINEAR_MIPMAP_NEAREST;
                else if (filterMode == "NEAREST_MIPMAP_LINEAR") return GL_NEAREST_MIPMAP_LINEAR;
                else if (filterMode == "LINEAR_MIPMAP_LINEAR") return GL_LINEAR_MIPMAP_LINEAR;
                else {
                    LOG_WARNING("RESOURCE TEXTURE", "Unknown minFilter mode: " + filterMode);
                    return GL_LINEAR;
                }
            }();
        
            // magFilter
            uint32_t magFilter = [&]() {
                std::string filterMode = args.get("magFilter", "LINEAR").asString();
                if (filterMode == "NEAREST") return GL_NEAREST;
                else if (filterMode == "LINEAR") return GL_LINEAR;
                else {
                    LOG_WARNING("RESOURCE TEXTURE", "Unknown magFilter mode: " + filterMode);
                    return GL_LINEAR;
                }
            }();
        
            bool needHDR = args.get("needHDR", false).asBool();
            bool needMipMap = args.get("needMipMap", true).asBool();
        
            if(!LoadFromFileCube(
                posX, negX,
                posY, negY,
                posZ, negZ,
                wrapS, wrapT, wrapR,
                minFilter, magFilter,
                needHDR, needMipMap
            )){

            }
        }
        else if(textureType == "2DArray"){
            return false;
        }
        else if(textureType == "3D"){
            return false;
        }
        else{
            LOG_WARNING("RESOURCE TEXTURE", "Unknow texture type from config data: " + configFile);
            return false;
        }
    return true;
}
    




bool Texture::LoadFromFile2D(
    const std::string& imagePath,
    uint32_t wrapS,
    uint32_t wrapT,
    uint32_t minFilter,
    uint32_t magFilter,
    bool needHDR,
    bool needMipMap
){
    int width, height, nrChannels;
    GLenum format = GL_RGB;
    GLenum internalFormat = GL_RGB;

    if (needHDR) {
        stbi_set_flip_vertically_on_load(true);
        float* data = stbi_loadf(imagePath.c_str(), &width, &height, &nrChannels, 0);
        if (!data) {
            LOG_ERROR("RESOURCE TEXTURE", "Failed to load HDR texture at path: " + imagePath);
            return false;
        }

        if (nrChannels == 1) {
            format = internalFormat = GL_RED;
        } else if (nrChannels == 3) {
            format = GL_RGB;
            internalFormat = GL_RGB16F;
        } else if (nrChannels == 4) {
            format = GL_RGBA;
            internalFormat = GL_RGBA16F;
        } else {
            LOG_ERROR("RESOURCE TEXTURE", "Unsupported HDR format: " + imagePath);
            stbi_image_free(data);
            return false;
        }

        glGenTextures(1, &id_);
        glBindTexture(GL_TEXTURE_2D, id_);
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_FLOAT, data);
        stbi_image_free(data);
    } else {
        stbi_set_flip_vertically_on_load(true);
        unsigned char* data = stbi_load(imagePath.c_str(), &width, &height, &nrChannels, 0);
        if (!data) {
            LOG_ERROR("RESOURCE TEXTURE", "Failed to load texture at path: " + imagePath);
            return false;
        }

        if (nrChannels == 1) {
            format = internalFormat = GL_RED;
        } else if (nrChannels == 3) {
            format = internalFormat = GL_RGB;
        } else if (nrChannels == 4) {
            format = internalFormat = GL_RGBA;
        } else {
            LOG_ERROR("RESOURCE TEXTURE", "Unsupported LDR format: " + imagePath);
            stbi_image_free(data);
            return false;
        }

        glGenTextures(1, &id_);
        glBindTexture(GL_TEXTURE_2D, id_);
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        stbi_image_free(data);
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);

    if (needMipMap) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }

    return true;
}

bool Texture::CreateEmpty2D(
    unsigned int width,
    unsigned int height,
    bool needHDR,
    bool needMipMap
) {
    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);

    // 根据是否 HDR 选择格式
    GLenum internalFormat, format, type;
    if (needHDR) {
        internalFormat = GL_RGBA16F;
        format = GL_RGBA;
        type = GL_FLOAT;
    } else {
        internalFormat = GL_RGBA;
        format = GL_RGBA;
        type = GL_UNSIGNED_BYTE;
    }

    // 分配空纹理
    glTexImage2D(
        GL_TEXTURE_2D,
        0,                  // mip level
        internalFormat,     // 内部格式
        width,
        height,
        0,                  // border, 必须为 0
        format,             // 读入数据格式
        type,               // 数据类型
        nullptr             // 空数据
    );

    // 设置默认包裹与采样方式
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
        needMipMap ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (needMipMap) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }

    return true;
}

bool Texture::LoadFromFileCube(
    const std::string& posX, const std::string& negX,
    const std::string& posY, const std::string& negY,
    const std::string& posZ, const std::string& negZ,
    uint32_t wrapS,
    uint32_t wrapT,
    uint32_t wrapR,
    uint32_t minFilter,
    uint32_t magFilter,
    bool needHDR,
    bool needMipMap
) {
    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_CUBE_MAP, id_);
    textureType_ = GL_TEXTURE_CUBE_MAP;

    const std::string paths[6] = { posX, negX, posY, negY, posZ, negZ };
    GLenum format = GL_RGB;
    GLenum internalFormat = GL_RGB;
    GLenum type = GL_UNSIGNED_BYTE;

    for (int i = 0; i < 6; ++i) {
        int width, height, nrChannels;

        if (needHDR) {
            float* data = stbi_loadf(paths[i].c_str(), &width, &height, &nrChannels, 0);
            if (!data) {
                LOG_ERROR("TEXTURE CUBE", "Failed to load HDR cubemap face: " + paths[i]);
                return false;
            }

            if (nrChannels == 1) {
                format = internalFormat = GL_RED;
            } else if (nrChannels == 3) {
                format = GL_RGB; internalFormat = GL_RGB16F;
            } else if (nrChannels == 4) {
                format = GL_RGBA; internalFormat = GL_RGBA16F;
            }

            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
                         0, internalFormat, width, height, 0,
                         format, GL_FLOAT, data);
            stbi_image_free(data);
        } else {
            unsigned char* data = stbi_load(paths[i].c_str(), &width, &height, &nrChannels, 0);
            if (!data) {
                LOG_ERROR("TEXTURE CUBE", "Failed to load cubemap face: " + paths[i]);
                return false;
            }

            if (nrChannels == 1) {
                format = internalFormat = GL_RED;
            } else if (nrChannels == 3) {
                format = internalFormat = GL_RGB;
            } else if (nrChannels == 4) {
                format = internalFormat = GL_RGBA;
            }

            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
                         0, internalFormat, width, height, 0,
                         format, GL_UNSIGNED_BYTE, data);
            stbi_image_free(data);
        }
    }

    // 设置 Wrap 和 Filter
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, wrapS);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, wrapT);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, wrapR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, magFilter);

    if (needMipMap) {
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    }

    return true;
}

void Texture::Release(){
    if (glIsTexture(id_)) {
        LOG_INFO("RESOURCE TEXTURE", "释放纹理: id_: " + std::to_string(id_));
        glDeleteTextures(1, &id_);
        id_ = 0; // 避免悬空引用
    }
}
