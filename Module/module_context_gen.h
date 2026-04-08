#pragma once

// =============================================
// Auto generated module context type list
// =============================================

#include "Application/public/application_module.h"
#include "Render/Public/render_module.h"
#include "ResourceManager/Public/resource_manager_module.h"

/**
 * @brief 定义当前上下文需要初始化的所有模块类型
 * 
 * 此宏由构建系统或代码生成工具维护
 * 修改此列表后，所有使用 GeneratedModuleContext 的地方将自动同步
 */
#define MODULE_CONTEXT_GEN_TYPES \
    Application::ApplicationModule, \
    Render::RenderModule, \
    Resource::ResourceManagerModule

/**
 * @brief 自动生成的模块上下文类型
 * 
 * 使用此类型可在主程序或插件中统一访问模块
 * 示例:
 * @code
 * GeneratedModuleContext context;
 * if (!context.Initialize(ModuleManager::Instance())) {
 *     return false;
 * }
 * auto* app = context.Get<Application::ApplicationModule>();
 * @endcode
 */
using GeneratedModuleContext = ModuleContextManager<MODULE_CONTEXT_GEN_TYPES>;
