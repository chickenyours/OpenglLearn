#include "memory_insert.h"

int int_insert(const Json::Value& args, void* addr){
    if (!addr) return 1; // 空指针
    if(args.size() != 1){
        return 2;
    }
    if(!args[0].isInt()){
        return 3;
    }
    *reinterpret_cast<int*>(addr) = args[0].asInt();
    return 0;
}

int float_insert(const Json::Value& args, void* addr){
    if (!addr) return 1;
    if (!args.isArray() || args.size() != 1) return 2;
    const auto& v = args[0];
    if (!v.isNumeric()) return 3;
    *reinterpret_cast<float*>(addr) = v.asFloat();
    return 0;
}

int string_insert(const Json::Value& args, void* addr){
    if (!addr) return 1; // 空指针
    if(args.size() != 1){
        return 2;
    }
    if(!args[0].isString()){
        return 3;
    }
    *reinterpret_cast<std::string*>(addr) = args[0].asString();
    return 0;
}

int vec3_insert(const Json::Value& args, void* addr){
    if (!addr) return 1; // 空指针
    auto out = reinterpret_cast<glm::vec3*>(addr);

    auto num = [](const Json::Value& v, float& o) -> bool {
        if (!v.isNumeric()) return false;
        o = v.asFloat();
        return std::isfinite(o);
    };

    // 支持数组写法
    if (args.isArray()) {
        if (args.size() == 1) {
            float a;
            if (!num(args[0], a)) return 3; // 类型不对
            *out = glm::vec3(a);
            return 0;
        } else if (args.size() == 3) {
            float x, y, z;
            if (!num(args[0], x) || !num(args[1], y) || !num(args[2], z)) return 3;
            *out = glm::vec3(x, y, z);
            return 0;
        } else {
            return 2; // 形状不对
        }
    }

    // 可选：支持对象写法 { "x":.., "y":.., "z":.. }
    if (args.isObject()) {
        float x, y, z;
        if (!num(args["x"], x) || !num(args["y"], y) || !num(args["z"], z)) return 3;
        *out = glm::vec3(x, y, z);
        return 0;
    }

    return 2; // 形状不对
}

std::unordered_map<std::string, MemoryInsert>
MemoryInsertManager::functions = {
    {"int", int_insert},
    {"float", float_insert},
    {"std::string", string_insert},
    {"glm::vec3", vec3_insert}
};

int SetObjectProperty(const Json::Value& data, const Json::Value& classBluePrint, void* addr){
   const Json::Value* fields;
    if (!Tool::JsonHelper::TryGetArray(classBluePrint, "fields", fields)) {
        return 1;
    }
    std::vector<const Json::Value*> properties;
    if (!Tool::JsonHelper::TryTraverseArray(*fields, properties)) {
        return 1;
    }
    for (const Json::Value* property : properties) {
        std::string propertyName, propertyTypeName;
        int propertyBitOffset = 0;

        if (!Tool::JsonHelper::TryGetString(*property, "name", propertyName)) {
            LOG_WARNING("SetObjectProperty", "cant find name");
            continue;
        }

        const Json::Value* fieldData;
        if (!Tool::JsonHelper::TryGetObject(data, propertyName, fieldData)) {
            // 数据里没有该字段，跳过（可选：提示）
            continue;
        }

        if (!Tool::JsonHelper::TryGetString(*property, "type", propertyTypeName)) {
            LOG_WARNING("SetObjectProperty", "cant find typeName");
            continue;
        }

        // 先找插入器
        const auto& funcs = MemoryInsertManager::GetFunctions();
        auto it = funcs.find(propertyTypeName);
        if (it == funcs.end()) {
            LOG_WARNING("SetObjectProperty", "no inserter for type: " + propertyTypeName);
            continue;
        }

        // 校验数据声明的 type 是否与蓝图一致（这里修正为 ||）
        std::string fieldTypeName;
        if (!Tool::JsonHelper::TryGetString(*fieldData, "type", fieldTypeName) || fieldTypeName != propertyTypeName) {
            LOG_WARNING("SetObjectProperty", "data type mismatch: data=" + fieldTypeName + " blueprint=" + propertyTypeName);
            continue;
        }

        if (!Tool::JsonHelper::TryGetInt(*property, "offset_bits", propertyBitOffset)) {
            LOG_WARNING("SetObjectProperty", "cant find bitOffset");
            continue;
        }
        if (propertyBitOffset % 8 != 0) {
            LOG_ERROR("SetObjectProperty", "offset_bits not byte-aligned: " + std::to_string(propertyBitOffset));
            continue;
        }

        const Json::Value* args;
        if (!Tool::JsonHelper::TryGetArray(*fieldData, "args", args)) {
            LOG_WARNING("SetObjectProperty", "no args array for field: " + propertyName);
            continue;
        }

        // 关键：按字节偏移到字段地址
        auto* base = static_cast<std::byte*>(addr);
        void* dst = base + (propertyBitOffset / 8);

        int code = it->second(*args, dst);
        if (code != 0) {
            LOG_ERROR("SetObjectProperty", "error when load property '" + propertyName + "', code: " + std::to_string(code));
            continue;
        }
    }

    return 0;
}