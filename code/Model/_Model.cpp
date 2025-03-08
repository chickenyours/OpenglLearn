#include "Model.h"
#include <glad/glad.h>
#include <iostream>
#define print(msg) std::cout<<(msg)<<std::endl;

#include "stb_image.h"

vector<MyTool::Texture> MyTool::Texture::textures;

MyTool::Mesh::Mesh(vector<float> vertexs,vector<unsigned int> indices,vector<Texture> textures){
     //VBO
    glGenVertexArrays(1,&VAO);
    glGenBuffers(1,&VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER,VBO);
    glBufferData(GL_ARRAY_BUFFER, vertexs.size() * sizeof(float), vertexs.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,11*sizeof(float),(void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,11*sizeof(float),(void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,11*sizeof(float),(void*)(5*sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3,3,GL_FLOAT,GL_FALSE,11*sizeof(float),(void*)(8*sizeof(float)));
    glEnableVertexAttribArray(3);
    glGenBuffers(1,&EBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,indices.size()*sizeof(unsigned int),indices.data(),GL_STATIC_DRAW);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER,0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
    this->textures = std::move(textures);
    indicesCount = indices.size();
    vertexsCount = vertexs.size()/11;
}

void MyTool::Mesh::Draw(){
    glBindVertexArray(VAO);
    //向纹理通道载入纹理
    for(int i =0;i<textures.size();i++){
        glActiveTexture(GL_TEXTURE0+i);
        glBindTexture(GL_TEXTURE_2D,textures[i].id);
    }
    glDrawElements(GL_TRIANGLES,indicesCount,GL_UNSIGNED_INT,0);
}

MyTool::Model::Model(string path){
    LoadModel(path);
}

void MyTool::Model::LoadModel(string path){
    Assimp::Importer improter;
    const aiScene* scene = improter.ReadFile(path, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_CalcTangentSpace);
    if(!scene){
        std::cout<<"Error loading scene"<<std::endl;
    }
    //提取模型文件目录
    directory = path.substr(0, path.find_last_of('/')+1);
    ProcessNode(scene->mRootNode,scene);
}

void MyTool::Model::ProcessNode(aiNode *node,const aiScene *scene){
    //处理mesh
    for(unsigned int i =0;i<node->mNumMeshes;i++){
        _meshArray.push_back(ProcessMesh(scene->mMeshes[node->mMeshes[i]],scene));
    }
    //处理node
    for(unsigned int i =0;i<node->mNumChildren;i++){
        ProcessNode(node->mChildren[i],scene);
    }
}


MyTool::Mesh MyTool::Model::ProcessMesh(aiMesh *mesh, const aiScene *scene){
    vector<float> vertices;
    vector<unsigned int> indices;
    vector<Texture> textures;

    // 提前为顶点和索引分配内存
    vertices.reserve(mesh->mNumVertices * 11);  // 每个顶点有 11 个数据 (位置 + 纹理坐标 + 法线 + 切线)
    indices.reserve(mesh->mNumFaces * 3);      // 每个面由 3 个索引组成

    // 解析顶点数据
    for (int i = 0; i < mesh->mNumVertices; i++) {
        // 位置
        vertices.push_back(mesh->mVertices[i].x);
        vertices.push_back(mesh->mVertices[i].y);
        vertices.push_back(mesh->mVertices[i].z);

        // 纹理坐标
        if (mesh->mTextureCoords[0]) {
            vertices.push_back(mesh->mTextureCoords[0][i].x);
            vertices.push_back(mesh->mTextureCoords[0][i].y);
        } else {
            vertices.push_back(0.0f); // 默认值
            vertices.push_back(0.0f); // 默认值
        }

        // 法线
        vertices.push_back(mesh->mNormals[i].x);
        vertices.push_back(mesh->mNormals[i].y);
        vertices.push_back(mesh->mNormals[i].z);

        //切线
        vertices.push_back(mesh->mTangents[i].x);
        vertices.push_back(mesh->mTangents[i].y);
        vertices.push_back(mesh->mTangents[i].z);
    }

    // 解析索引数据
    for (int i = 0; i < mesh->mNumFaces; i++) {
        aiFace face = mesh->mFaces[i];
        indices.insert(indices.end(), face.mIndices, face.mIndices + face.mNumIndices);
    }

    // 解析材质

    // 解析纹理
    aiMaterial *material = scene->mMaterials[mesh->mMaterialIndex];

    auto loadTextures = [&](aiTextureType type, const string& typeName) {
        vector<Texture> loadedTextures = LoadMaterialTexture(material, type, typeName);
        textures.insert(textures.end(), loadedTextures.begin(), loadedTextures.end());
    };


    // 解析不同类型的纹理
    loadTextures(aiTextureType_DIFFUSE, "texture_diffusion");
    loadTextures(aiTextureType_SPECULAR, "texture_specular");
    loadTextures(aiTextureType_HEIGHT , "texture_normal"); // 如果需要支持法线纹理，可以添加更多类型

    return Mesh(vertices, indices, textures);
}



 vector<MyTool::Texture> MyTool::Model::LoadMaterialTexture(aiMaterial *mat,aiTextureType type,string typeName){
    aiString str;
    vector<Texture> textures;
    for (unsigned int i=0;i<mat->GetTextureCount(type);i++){
        mat->GetTexture(type,i, &str);
        bool skip = false;
        for(int i =0;i<MyTool::Texture::textures.size();i++){
            if(std::strcmp(MyTool::Texture::textures[i].path.data(),str.C_Str())==0){//????
                textures.push_back(MyTool::Texture::textures[i]);
                skip = true;
                break;
            }
        }
        if(!skip){
            //还有另一种方式:texture自身构造
            Texture texture;
            texture.id = TextureFromFile(str.C_Str());
            texture.typeName = typeName;
            texture.path = str.C_Str();
            textures.push_back(texture);
            texture.textures.push_back(texture);
        }
    }
    return textures;
}

unsigned int MyTool::Model::TextureFromFile(const char* fileName){
    string path = directory + fileName;
    int width,height,channels;
    unsigned int texture;
    glGenTextures(1,&texture);
    glBindTexture(GL_TEXTURE_2D,texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    unsigned char* data = stbi_load(path.c_str(),&width,&height,&channels,0);
    if(data){
        GLenum format;
        if (channels == 1)
            format = GL_RED;
        else if (channels == 3)
            format = GL_RGB;
        else if (channels == 4)
            format = GL_RGBA;
        glTexImage2D(GL_TEXTURE_2D,0,format,width,height,0,format,GL_UNSIGNED_BYTE,data);
        glGenerateMipmap(GL_TEXTURE_2D);
        stbi_image_free(data);
    }
    else{
        std::cout<<"imagepath:"<<path<< "cannot be opened";
        return 0;
    }
    return texture;
}

void MyTool::Model::Draw(){
    for(int i = 0;i<_meshArray.size();i++){
        _meshArray[i].Draw();
    }
}

