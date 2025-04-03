#pragma once
#include "code/shader.h"
#include <vector>
#include <queue>

namespace Render{

struct VisualElement{
    glm::vec3 pos;
    glm::vec3 color;
};

class BufferChunk {
    public:
        BufferChunk(const BufferChunk&) = delete;
        BufferChunk& operator=(const BufferChunk&) = delete;
        BufferChunk(BufferChunk&& other) noexcept;
        BufferChunk(size_t maxCount);
        ~BufferChunk();
        bool Update(int index,int num,const VisualElement* data);
        int Add(int num,const VisualElement* data);
        unsigned int inline GetElementCount(){return count;};
        inline GLuint GetVAO(){return vao;}
    private:    
        // std::queue<VisualElement> _updateDateRegister;
        GLuint vao = 0;
        GLuint vbo = 0;
        static constexpr size_t elementSize = sizeof(VisualElement);   
        unsigned int count = 0;                                      // 现存元素的数量
        size_t maxCount;                                   // 现存元素的数量
};
    

    class Marker {
        public:
            Marker(size_t maxPoints, size_t maxLines);
        
            int AddPoint(const std::vector<glm::vec3>& positions, const std::vector<glm::vec3>& colors);
            void SetPoint(int index, const std::vector<glm::vec3>& positions, const std::vector<glm::vec3>& colors);
            
             // 批量添加线
            int AddLine(const std::vector<glm::vec3>& startPositions, const std::vector<glm::vec3>& startColors, 
                const std::vector<glm::vec3>& endPositions, const std::vector<glm::vec3>& endColors);
            void SetLine(int index, const std::vector<glm::vec3>& startPositions, const std::vector<glm::vec3>& startColors, 
                const std::vector<glm::vec3>& endPositions, const std::vector<glm::vec3>& endColors);
        
            void Draw();
            void SetPointSize(float size);
            void SetLineWidth(float width);
            void SetMatrix(glm::mat4 perspective, glm::mat4 view , glm::mat4 model);
            ~Marker();
        
        private:
            glm::mat4 _view;
            glm::mat4 _perspective;
            glm::mat4 _model;

            BufferChunk pointBuffer;
            GLuint pointBufferVAO;
            BufferChunk lineBuffer;
            GLuint lineBufferVAO;
        
            ShaderProgram _shaderProgram;


        };

}
