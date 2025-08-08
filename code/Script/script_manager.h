#pragma once

#include <windows.h>

#include <string>

#include <unordered_map>

#include "code/ModuleManager/module_manager.h"

#include "code/Script/script_interface.h"

typedef void(*set_dll_module_api)(const Module::ModuleHost& host);
typedef IScript*(*get_script_object_api)();
typedef void(*delete_script_object_api)(IScript*);

class ScriptManager{
    public:
        static ScriptManager& Instance(){
            static ScriptManager instance;
            return instance;
        } 

        int Init(){
            // 插入脚本
            hDll = LoadLibraryA("script.dll");
            if(!hDll){
                // std::cerr << "Failed to load DLL!" << std::endl;
                LOG_ERROR("ScriptManager","Failed to load DLL!");
                return 1;
            }

            isLoadDLL_ = true;

            set_dll_module_api set_module = (set_dll_module_api)GetProcAddress(hDll,"SetModule"); 
            delete_script_object_api delete_script = (delete_script_object_api)GetProcAddress(hDll,"DeleteScriptObject");

            if(!set_module || !delete_script){
                LOG_ERROR("ScriptManager","Failed to load functions!");
                return 2;
            }

            set_module(Module::ModuleManager::Instance().Export());

            return 0;
        }

        IScript* GetScriptObject(std::string scriptName){
            if(!isLoadDLL_){
                LOG_ERROR("ScriptManager","Failed to load ScriptObject because ScriptManager is not init! " + scriptName);
                return nullptr;
            }
            auto it = cache_.find(scriptName);
            get_script_object_api fun;
            if(it != cache_.end()){
                fun = it->second;
                return fun();
            }
            else{
                fun = (get_script_object_api)GetProcAddress(hDll,("create_plugin_object_" + scriptName).c_str());
                if(fun){
                    cache_[scriptName] = fun;
                    return fun();
                }
                else{
                    LOG_ERROR("ScriptManager","Failed to load ScriptObject because cannot find Script: " + scriptName);
                    return nullptr;
                }
            }
          
        }

        ~ScriptManager(){
            if(hDll){
                FreeLibrary(hDll);
            }
        }

        private:
            HMODULE hDll;
            std::unordered_map<std::string,get_script_object_api> cache_;
            bool isLoadDLL_ = false;
};