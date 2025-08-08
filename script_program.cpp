#include "code/ModuleManager/module_manager.h"

#include "code/Script/script_interface.h"

extern "C" __declspec(dllexport) void SetModule(const Module::ModuleHost& host){
    Module::ModuleManager::Instance().Init(host);
}

extern "C" __declspec(dllexport) void DeleteScriptObject(IScript* object){
    delete object;
}

extern "C" __declspec(dllexport) void ShowInfo(){
    
}