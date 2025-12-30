#pragma once
#include <vector>
#include <memory>

namespace Render{
    class UBOManager{
        public:
            static void Init();
            static GLuint GetUBO(int UBOIndex);
            static void Release();
        private:
            static std::unique_ptr<UBOManager> instance;
            ~UBOManager();
            std::vector<GLuint> UBOList;
    };
}