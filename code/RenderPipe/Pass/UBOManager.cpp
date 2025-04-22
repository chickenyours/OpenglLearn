#include "UBOManager.h"

#include <iostream>

#include "code/RenderPipe/UniformBindings.h"

using namespace Render;

void UBOManager::Init(){
    if(!instance){
        instance = std::make_unique<UBOManager>();
        instance->UBOList.resize(MAX_UBO_AMOUNT,0);
        // CameraUBO
        glGenBuffers(1,&instance->UBOList[UBO_BINDING_CAMERA]);
        glBindBuffer(GL_UNIFORM_BUFFER, instance->UBOList[UBO_BINDING_CAMERA]);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(CameraDataUBOLayout), nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, UBO_BINDING_CAMERA, instance->UBOList[UBO_BINDING_CAMERA]);
    }
    else{
        std::cout<<"[UBO Manager]: UBOManager instance is already exists, but somewhere still call Init function"<<std::endl;
    }
}

GLuint UBOManager::GetUBO(int index){
    if(!instance){
        Init();
    }
    if (index < 0 || index >= instance->UBOList.size()) {
        std::cerr << "[UBO Manager]: index out of bounds!" << std::endl;
        return 0;
    }
    if(instance->UBOList[index] == 0){
        std::cout<<"[UBO Manager]: UBOList["<< index <<"] is 0"<<std::endl;
    }
    return instance->UBOList[index];
}

void UBOManager::Release(){
    if (instance) {
        instance.reset();
    } else {
        std::cout << "[UBO Manager]: Instance already null on Release()." << std::endl;
    }
}

UBOManager::~UBOManager(){
    for(GLuint UBOID : UBOList){
        if(UBOID){
            glDeleteBuffers(1, &UBOID);
        }
    }
    std::cout<<"[UBO Manager]: UBOManager instance is released successfully."<<std::endl;
}
