#include "texture.h"

#include <stb_image.h>

#include "code/DebugTool/ConsoleHelp/color_log.h"

using namespace Resource;

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



 // if(textureType == GL_TEXTURE_CUBE_MAP){
    //     glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE); // ✅ 必须,不然会出现裂纹
    // }