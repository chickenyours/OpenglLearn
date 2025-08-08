#include "texture.h"

#include <iostream>
#include <glad/glad.h>
#include "stb_image.h"

#ifdef __SSE2__

#pragma message("__SSE2__ defined")

#endif



#ifdef __AVX__

#pragma message("__AVX__ defined")

#endif

using namespace Render;

Texture::Texture(std::string type, std::string path):_type(type),_path(path),_id(0){}

bool Texture::Load()
{
    glGenTextures(1, &_id);
    glBindTexture(GL_TEXTURE_2D, _id);
    // set the texture wrapping parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    // set texture filtering parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // load image, create texture and generate mipmaps
    int width, height, nrChannels;
    unsigned char *data = stbi_load(_path.c_str(), &width, &height, &nrChannels, 0);
    if (data)
    {
        GLenum format;
        if (nrChannels == 1)
            format = GL_RED;
        else if (nrChannels == 3)
            format = GL_RGB;
        else if (nrChannels == 4)
            format = GL_RGBA;

        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    else
    {
        std::cout << "Texture failed to load at path: " << _path << std::endl;
        return false;
        
    }
    stbi_image_free(data);
    return true;
}

unsigned int Texture::GetID()
{
    return _id;
}

std::string Texture::GetPath()
{
    return _path;
}


Texture::~Texture(){
    if(_id != 0){
        std::cout<<"destroy Texture "<<_path<< std::endl;
        glDeleteTextures(1, &_id);
    }
}

void Texture::Print(int tabs){
    std::string tab = "";
    for(int i = 0; i< tabs; i++){
        tab += "\t";
    }

    std::cout << tab <<"======TextureInfo======"<<std::endl;
    std::cout << tab <<"UniformName"<< _type << std::endl;
    std::cout << tab <<"LoadPath"<< _path << std::endl;
    std::cout << tab <<"API_ID: "<< _id << std::endl;
    std::cout << tab <<"======EndTextureInfo======"<< std::endl;
}
