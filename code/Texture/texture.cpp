#include "texture.h"

#include <iostream>
#include <glad/glad.h>
#include "stb_image.h"

using namespace std;
using namespace Render;

Texture::Texture(string type, string path, unsigned int channel):_type(type),_path(path),_id(0),_channel(channel){}

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
        cout << "Texture failed to load at path: " << _path << endl;
        return false;
        
    }
    stbi_image_free(data);
    return true;
}

unsigned int Texture::GetID()
{
    return _id;
}

string Texture::GetPath()
{
    return _path;
}


Texture::~Texture(){
    if(_id != 0){
        glDeleteTextures(1, &_id);
    }
}

void Texture::Print(){
    cout<<"Texture "<<_path<<"Channel "<< _channel <<endl;
}
