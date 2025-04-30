#pragma once

#include <json/json.h>

namespace ECS {
    namespace Core {
        class ResourcePool; // 前向声明
    }

} // namespace ECS

namespace Resource {

        class AbstractResource  {
        protected:
            AbstractResource () = default;

            // 虚析构是必须的：确保 delete 时能析构子类
            virtual ~AbstractResource () = default;

            // 更通用的接口，接受配置对象或流式数据更灵活
            // 可重复加载(覆盖)
            virtual bool Load(const Json::Value& Config) = 0;
            virtual void Release() = 0;

            friend class ECS::Core::ResourcePool;
        };

    } // namespace Resource

