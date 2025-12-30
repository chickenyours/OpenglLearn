#pragma once

#define REGISTER_CLASS_PLUGIN(baseType, derivedType) \
    extern "C" __declspec(dllexport) baseType* create_plugin_object_##derivedType() { \
        return new derivedType(); \
    } \
    extern "C" __declspec(dllexport) bool delete_plugin_object_##derivedType(baseType* object){ \
        auto dervied = dynamic_cast<derivedType*>(object); \
        if(dervied){ \
            delete dervied; \
            return true; \
        } \
        return false; \
    } 
