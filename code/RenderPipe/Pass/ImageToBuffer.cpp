#include "ImageToBuffer.h"

#include "code/RenderPipe/RenderContext/PassRenderContext.h"
#include "code/RenderPipe/RenderContext/PassConfig.h"
#include "code/shader.h"

using namespace Render;

ImageToBufferPass::ImageToBufferPass(){
    std::cout<<"创建Pass(未初始化): ImageToBufferPass"<<std::endl;
}

void ImageToBufferPass::Init(const PassConfig& cfg){

    if (cfg.targetBufferWidth <= 0 || cfg.targetBufferHeight <= 0 || cfg.targetBufferWidth >= 3000 || cfg.targetBufferHeight >= 3000) {
        std::cerr << "[ImageToBufferPass::SetConfig] Invalid screen size!" << std::endl;
    }
    screenWidth_ = cfg.targetBufferWidth;
    screenHeight_ = cfg.targetBufferHeight;

    float quadVertices[] = {
        // positions   // texCoords
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,

        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };
    GLuint VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);

    QuadVAO_ = VAO;

    screenTextureShader_ = std::make_unique<ShaderProgram>("shaders/Final/screen.vs","shaders/Final/screen.fs");
    screenTextureArrayShader_ = std::make_unique<ShaderProgram>("shaders/Final/screen.vs","shaders/Final/screenTex3D.fs");
    std::cout<<"初始化Pass: ImageToBufferPass"<<std::endl;
}
void ImageToBufferPass::SetConfig(const PassConfig& cfg){
    if (cfg.targetBufferWidth <= 0 || cfg.targetBufferHeight <= 0 || cfg.targetBufferWidth >= 3000 || cfg.targetBufferHeight >= 3000) {
        std::cerr << "[ImageToBufferPass::SetConfig] Invalid screen size!" << std::endl;
        return;
    }
    screenWidth_ = cfg.targetBufferWidth;
    screenHeight_ = cfg.targetBufferHeight;
    for(auto& passRenderItem : passRenderItemList_){
        if(passRenderItem.scaleViewPortflag){
            passRenderItem.viewPortOffsetX = static_cast<int>(screenWidth_ * passRenderItem.viewPortScaleOffsetX);
            passRenderItem.viewPortOffsetY = static_cast<int>(screenHeight_ * passRenderItem.viewPortScaleOffsetY);
            passRenderItem.viewPortWidth = static_cast<int>(screenWidth_ * passRenderItem.viewPortScaleWidth);
            passRenderItem.viewPortHeight = static_cast<int>(screenHeight_ * passRenderItem.viewPortScaleHeight);
        }
    }
}

// void ImageToBufferPass::SetTexture(unsigned int GLTextureType, GLuint textureID){
//     flag = GLTextureType;
//     texture_ = textureID;
// }

int ImageToBufferPass::AddPassRenderItem(ImageToBufferRenderItem& passRenderItem){
    if(passRenderItem.scaleViewPortflag){
        passRenderItem.viewPortOffsetX = static_cast<int>(screenWidth_ * passRenderItem.viewPortScaleOffsetX);
        passRenderItem.viewPortOffsetY = static_cast<int>(screenHeight_ * passRenderItem.viewPortScaleOffsetY);
        passRenderItem.viewPortWidth = static_cast<int>(screenWidth_ * passRenderItem.viewPortScaleWidth);
        passRenderItem.viewPortHeight = static_cast<int>(screenHeight_ * passRenderItem.viewPortScaleHeight);
    }
    passRenderItemList_.push_back(passRenderItem);
    return static_cast<int>(passRenderItemList_.size()) - 1;
}

bool ImageToBufferPass::SetPassRenderItem(int index, ImageToBufferRenderItem& passRenderItem){
    if (index < 0 || index >= static_cast<int>(passRenderItemList_.size())) {
        return false;
    }
    if(passRenderItem.scaleViewPortflag){
        passRenderItem.viewPortOffsetX = static_cast<int>(screenWidth_ * passRenderItem.viewPortScaleOffsetX);
        passRenderItem.viewPortOffsetY = static_cast<int>(screenHeight_ * passRenderItem.viewPortScaleOffsetY);
        passRenderItem.viewPortWidth = static_cast<int>(screenWidth_ * passRenderItem.viewPortScaleWidth);
        passRenderItem.viewPortHeight = static_cast<int>(screenHeight_ * passRenderItem.viewPortScaleHeight);
    }
    passRenderItemList_[index] = passRenderItem;
    return true;
}

void ImageToBufferPass::ClearPassRenderItem(){
    passRenderItemList_.clear();
}

void ImageToBufferPass::Update(const PassRenderContext& ctx, const std::vector<RenderItem>& renderItemList){
    // if(texture_ > 0){   
    //     glBindFramebuffer(GL_FRAMEBUFFER,framebuffer_);
    //     glClearColor(0.0f, 1.00f, 0.00f, 1.0f);
    //     glClear(GL_COLOR_BUFFER_BIT);
    //     glDisable(GL_DEPTH_TEST);
    //     glBindVertexArray(QuadVAO_);
    //     glViewport(0,0,screenWidth_,screenHeight_);
    //     glActiveTexture(GL_TEXTURE0);
    //     if(flag == GL_TEXTURE_2D){
    //         screenTextureShader_->Use();
    //         glBindTexture(GL_TEXTURE_2D,texture_);
    //     }
    //     else if(flag == GL_TEXTURE_2D_ARRAY){
    //         screenTextureArrayShader_->Use();
    //         ShaderU1i(*screenTextureArrayShader_,"layerIndex",textureArraylayerIndex_);
    //         glBindTexture(GL_TEXTURE_2D_ARRAY,texture_);
    //     }
    //     else{
    //         std::cout<<"未知flag"<<std::endl;
    //     }
    //     glDrawArrays(GL_TRIANGLES,0,6);
    //     glEnable(GL_DEPTH_TEST);
    // }

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glClearColor(0.0f, 1.00f, 0.00f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(QuadVAO_);
    // glViewport(0, 0, screenWidth_, screenHeight_);
    glActiveTexture(GL_TEXTURE0);

    for (const auto& item : passRenderItemList_) {
        glViewport(item.viewPortOffsetX,item.viewPortOffsetY,item.viewPortWidth,item.viewPortHeight);
        if (item.GLTextureType == GL_TEXTURE_2D) {
            screenTextureShader_->Use();
            glBindTexture(GL_TEXTURE_2D, item.textureID);
        } else if (item.GLTextureType == GL_TEXTURE_2D_ARRAY) {
            screenTextureArrayShader_->Use();
            ShaderU1i(*screenTextureArrayShader_, "layerIndex", item.textureArraylayerIndex);
            glBindTexture(GL_TEXTURE_2D_ARRAY, item.textureID);
        } else {
            std::cout << "未知纹理类型" << std::endl;
            continue;
        }
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    glEnable(GL_DEPTH_TEST);

}
void ImageToBufferPass::Release(){
    if (QuadVAO_ != 0) {
        glDeleteVertexArrays(1, &QuadVAO_);
        QuadVAO_ = 0;
    }
    std::cout << "释放Pass: ImageToBufferPass" << std::endl;
}

