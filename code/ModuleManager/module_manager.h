#pragma once

#include <memory>

#include "code/ECS/Core/Resource/resource_manager.h"
#include "code/Environment/environment.h"
#include "code/Input/key_get.h"
#include "code/Input/mouse_input.h"

namespace Module{

    struct ModuleHost
    {
        std::shared_ptr<ECS::Core::ResourceModule::ResourceManager> resourceManager = nullptr;
        std::shared_ptr<Environment::Environment> environment = nullptr;
        std::shared_ptr<Input::KeyboardInput> keyInput = nullptr;
        std::shared_ptr<Input::MouseInput> mouseInput = nullptr;
    };
    

    class ModuleManager{
        public:
            static ModuleManager& Instance(){
                static ModuleManager instance;
                return instance;
            }

            void Init(){
                host.resourceManager = std::make_shared<ECS::Core::ResourceModule::ResourceManager>();
                host.environment = std::make_shared<Environment::Environment>();
                host.keyInput = std::make_shared<Input::KeyboardInput>();
                host.mouseInput = std::make_shared<Input::MouseInput>();
                SetMoudle();
            }

            void Init(const ModuleHost& moduleHost){
                host = moduleHost;
                SetMoudle();
            }

            
            const ModuleHost& Export(){
                return host;
            }
        private:
            ModuleHost host;
            void SetMoudle(){
                ECS::Core::ResourceModule::ResourceManager::SetInstance(host.resourceManager.get());
                Environment::Environment::SetInstance(host.environment.get());
                Input::KeyboardInput::SetInstance(host.keyInput.get());
                Input::MouseInput::SetInstance(host.mouseInput.get());
            }
            
    };
} // namespace Module