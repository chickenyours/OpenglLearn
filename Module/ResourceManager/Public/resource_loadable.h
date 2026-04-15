#pragma once

#include <string>

namespace Resource {

/**
 * @brief 可加载接口 - 资源类的根基
 * 
 * @deprecated 新设计不再要求资源类型继承此接口。
 *             资源管理器现在可以管理任意类型的资源。
 *             此接口仅保留用于向后兼容。
 *
 * 所有资源类都应继承此接口，提供基本的加载状态查询和释放功能。
 */
class ILoadable {
public:
    virtual ~ILoadable() = default;
    ILoadable() = default;

    /**
     * @brief 检查资源是否已加载
     * @return true 如果资源已加载，false 否则
     */
    inline bool IsLoad() const { return isLoad_; }

    /**
     * @brief 释放资源
     *
     * 纯虚函数，由具体资源类实现释放逻辑。
     */
    virtual void Release() = 0;

protected:
    bool isLoad_ = false;
};

/**
 * @brief 可通过配置文件加载接口
 * 
 * @deprecated 新设计不再要求资源类型继承此接口。
 *             使用 FromConfig 工厂函数时传入 loader 回调即可。
 *
 * 继承自 ILoadable，提供从配置文件加载资源的能力。
 */
class ILoadFromConfig : public ILoadable {
public:
    /**
     * @brief 从配置文件加载资源
     * @param configFile 配置文件路径
     * @return true 如果加载成功，false 否则
     */
    virtual bool LoadFromConfigFile(const std::string& configFile) = 0;
};

} // namespace Resource
