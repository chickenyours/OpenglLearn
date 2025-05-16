#include <iostream>
#include <vector>
#include <string>
#include <fstream>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <json/json.h>


namespace Tool{
    


class JsonHelper {
public:

    static bool LoadJsonValueFromFile(const std::string& filePath, Json::Value& out) {
        Json::CharReaderBuilder builder;
        std::ifstream in(filePath, std::ios::binary);

        if (!in.is_open()) {
            
            return false;
        }

        std::string errs;
        Json::Value root;
        if (!Json::parseFromStream(builder, in, &root, &errs)) {
            return false;
        }
        if(!root.isObject()){
            return false;
        }
        out = std::move(root);
        return true;
    }

    static bool TryGetInt(const Json::Value& value, const std::string& key, int& out) {
        if (value.isMember(key) && value[key].isInt()) {
            out = value[key].asInt();
            return true;
        }
        return false;
    }

    static bool TryGetInt(const Json::Value& value, const std::string& key, int& out, int defaultValue) {
        if (value.isMember(key) && value[key].isInt()) {
            out = value[key].asInt();
            return true;
        }
        out = defaultValue;
        return false;
    }

    static bool TryGetString(const Json::Value& value, const std::string& key, std::string& out, const std::string& defaultValue) {
        if (value.isMember(key) && value[key].isString()) {
            out = value[key].asString();
            return true;
        }
        out = defaultValue;
        return false;
    }

    static bool TryGetString(const Json::Value& value, const std::string& key, std::string& out) {
        if (value.isMember(key) && value[key].isString()) {
            out = value[key].asString();
            return true;
        }
        return false;
    }

    static bool TryGetBool(const Json::Value& value, const std::string& key, bool& out) {
        if (value.isMember(key) && value[key].isBool()) {
            out = value[key].asBool();
            return true;
        }
        return false;
    }

    static bool TryGetBool(const Json::Value& value, const std::string& key, bool& out, bool defaultValue) {
        if (value.isMember(key) && value[key].isBool()) {
            out = value[key].asBool();
            return true;
        }
        out = defaultValue;
        return false;
    }

    static bool TryGetFloat(const Json::Value& value, const std::string& key, float& out) {
        if (value.isMember(key) && value[key].isDouble()) {
            out = static_cast<float>(value[key].asDouble());
            return true;
        }
        return false;
    }

    static bool TryGetFloat(const Json::Value& value, const std::string& key, float& out, float defaultValue) {
        if (value.isMember(key) && value[key].isDouble()) {
            out = static_cast<float>(value[key].asDouble());
            return true;
        }
        out = defaultValue;
        return false;
    }

    static bool TryGetObject(const Json::Value& value, const std::string& key,const Json::Value*& out) {
        if (value.isMember(key) && value[key].isObject()) {
            out = &value[key];
            return true;
        }
        return false;
    }

    static bool TryGetVec3(const Json::Value& value, const std::string& key, glm::vec3& out) {
        if (value.isMember(key) && value[key].isArray() && value[key].size() == 3) {
            const Json::Value& arr = value[key];
            if (arr[0].isNumeric() && arr[1].isNumeric() && arr[2].isNumeric()) {
            out = glm::vec3(arr[0].asFloat(), arr[1].asFloat(), arr[2].asFloat());
            return true;
            }
        }
        return false;
    }    

    static bool TryGetVec3(const Json::Value& value, const std::string& key, glm::vec3& out, glm::vec3 defaultValue){
        if (value.isMember(key) && value[key].isArray() && value[key].size() == 3) {
            const Json::Value& arr = value[key];
            if (arr[0].isNumeric() && arr[1].isNumeric() && arr[2].isNumeric()) {
                out = glm::vec3(arr[0].asFloat(), arr[1].asFloat(), arr[2].asFloat());
                return true;
            }
        }
        out = defaultValue;
        return false;
    }


    static bool TryGetArray(const Json::Value& value, const std::string& key,const Json::Value*& out) {
        if (value.isMember(key) && value[key].isArray()) {
            out = &value[key];
            return true;
        }
        return false;
    }

    static bool TryFindElement(const Json::Value& value, const std::string& key){
        return value.isMember(key);
    }

    static bool TryFindElement(const Json::Value& value, const std::string& key, std::string& type){
        if (!value.isMember(key)) {
            return false;
        }
        const Json::Value& v = value[key];
        if (v.isInt()) {
            type = "int";
        } else if (v.isDouble()) {
            type = "float";
        } else if (v.isString()) {
            type = "string";
        } else if (v.isBool()) {
            type = "bool";
        } else if (v.isObject()) {
            type = "object";
        } else if (v.isArray()) {
            type = "array";
        } else {
            type = "unknown";
            return false;
        }
        return true;
    }

    // 只能遍历同类型数据
    static bool TryTraverseArray(const Json::Value& value, std::vector<int>& out){
        if(!value.isArray()){
            return false;
        }
        std::vector<int> temp;
        temp.reserve(value.size());
        
        for (const auto& element : value) {
            if (element.isInt()) {
                temp.push_back(element.asInt());
            } else {
                return false;
            }
        }
        
        out = std::move(temp);
        return true;
    }

    static bool TryTraverseArray(const Json::Value& value, std::vector<float>& out){
         if(!value.isArray()){
            return false;
        }
        std::vector<float> temp;
        temp.reserve(value.size());
        
        for (const auto& element : value) {
            if (element.isDouble()) {
                temp.push_back(element.asFloat());
            } else {
                return false;
            }
        }
        
        out = std::move(temp);
        return true;
    }

    static bool TryTraverseArray(const Json::Value& value, std::vector<std::string>& out){
     if(!value.isArray()){
            return false;
        }
        std::vector<std::string> temp;
        temp.reserve(value.size());
        
        for (const auto& element : value) {
            if (element.isString()) {
                temp.push_back(element.asString());
            } else {
                return false;
            }
        }
        
        out = std::move(temp);
        return true;
    }

    static bool TryTraverseArray(const Json::Value& value, std::vector<bool>& out){
       if(!value.isArray()){
            return false;
        }
        std::vector<bool> temp;
        temp.reserve(value.size());
        
        for (const auto& element : value) {
            if (element.isBool()) {
                temp.push_back(element.asBool());
            } else {
                return false;
            }
        }
        
        out = std::move(temp);
        return true;
    }
};

} // namespace Tool