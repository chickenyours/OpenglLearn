#pragma once

#include <functional>
#include <json/json.h>
#include <string>

#include "code/DebugTool/ConsoleHelp/color_log.h"


namespace ECS {
    namespace Core {
        namespace ResourceModule{

            template <typename T>
            class ResourcePool; // 前向声明

            class ResourceManager;
        }
    }
} 

/*

IsLoadable
|
|= ILoadFromConfig

*/

namespace Resource {
    // 接口类

    // 可加载接口: 具有自我生命周期管理,响应外部环境,是资源类的根基
    class ILoadable {
        public:
            virtual ~ILoadable() = default;
            ILoadable() = default;

            inline bool IsLoad() const { return isLoad_; }
            virtual void Release() = 0;

        protected:
            bool isLoad_ = false;

            template <typename T>
            friend class ECS::Core::ResourceModule::ResourcePool;
    };

    // 可通过配置文件加载接口: 可以使用配置文件地址实现加载的能力
    class ILoadFromConfig : public ILoadable {
        public:
            virtual bool LoadFromConfigFile(const std::string& configFile, Log::StackLogErrorHandle errHandle = nullptr) = 0;
    };

}