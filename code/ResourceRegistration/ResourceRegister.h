#include <memory>
#include <unordered_map>
#include <string>

template <typename T>
class ResourcePool{
    public:
        std::unordered_map<std::string, std::weak_ptr<T>> map;
        std::shared_ptr<T> GetResource(std::string);
        void AddResource(std::string);
};
class ResourceRegister;

