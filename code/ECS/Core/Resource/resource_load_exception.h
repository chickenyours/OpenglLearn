#pragma once

#include <exception>
#include <string>
#include <memory>
#include <sstream>

namespace ECS::Core::ResourceSystem{
    class ResourceLoadException : public std::runtime_error {
    public:
        ResourceLoadException(const std::string& message,
                              const std::string& filePath,
                              const std::exception& cause)
            : std::runtime_error(message),
              _filePath(filePath),
              _nested(std::make_exception_ptr(cause)) {}
    
        ResourceLoadException(const std::string& message,
                              const std::string& filePath)
            : std::runtime_error(message),
              _filePath(filePath),
              _nested(nullptr) {}
            
        // 打印带缩进的嵌套异常链（类似 Python traceback）
        std::string GetStackTrace(int indent = 0) const {
            std::ostringstream oss;
            std::string prefix(indent * 2, ' ');
            oss << prefix << "- " << what() << " [file: " << _filePath << "]\n";
            if (_nested) {
                try {
                    std::rethrow_exception(_nested);
                } catch (const ResourceLoadException& nested) { // Resource 子类对象 层级抛出异常
                    oss << nested.GetStackTrace(indent + 1);
                } catch (const std::exception& e) {             // json 读取模块抛出异常
                    oss << prefix << "  ↳ (inner) " << e.what() << "\n";
                }
            }
            return oss.str();
        }
    
        const std::string& GetFilePath() const { return _filePath; }
    
    private:
        std::string _filePath;
        std::exception_ptr _nested;
    };
}
