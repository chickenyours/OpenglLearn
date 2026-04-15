/**
 * @file resource_manager_test.cpp
 * @brief 测试改进后的 ResourceManager 模块
 * 
 * 测试要点：
 * 1. 资源类型无需继承 ILoadable 接口
 * 2. 支持 FromPointer 接收裸指针/unique_ptr
 * 3. 支持 FromGenerator 接收工厂函数
 * 4. 引用计数管理正确
 * 5. 自定义销毁函数和回调
 */

#include <iostream>
#include <string>
#include <memory>

#include "module_manager.h"
#include "resource_manager_module.h"

// ============================================================================
// 测试资源类 - 无需继承任何接口
// ============================================================================

/**
 * @brief 简单数据资源类 - 纯数据对象，无继承
 */
struct DataResource {
    int id;
    std::string name;
    float value;

    DataResource(int i, const std::string& n, float v)
        : id(i), name(n), value(v) {
        std::cout << "  [DataResource] Constructor called: id=" << id << std::endl;
    }

    ~DataResource() {
        std::cout << "  [DataResource] Destructor called: id=" << id << std::endl;
    }

    void print() const {
        std::cout << "  DataResource{id=" << id << ", name=" << name << ", value=" << value << "}" << std::endl;
    }
};

/**
 * @brief 复杂资源类 - 模拟真实资源
 */
class ComplexResource {
public:
    ComplexResource(const std::string& data)
        : data_(data) {
        std::cout << "  [ComplexResource] Created with data: " << data_ << std::endl;
    }

    ~ComplexResource() {
        std::cout << "  [ComplexResource] Destroyed with data: " << data_ << std::endl;
    }

    void process() const {
        std::cout << "  Processing: " << data_ << std::endl;
    }

    const std::string& getData() const { return data_; }

private:
    std::string data_;
};

// ============================================================================
// 测试函数
// ============================================================================

/**
 * @brief 测试 1: 使用 FromGenerator 创建资源（无需继承 ILoadable）
 */
void testFromGenerator() {
    std::cout << "\n=== Test 1: FromGenerator (no ILoadable inheritance) ===" << std::endl;

    auto& resourceManager = Resource::ResourceManagerModule::GetInstance();

    // 启动模块
    resourceManager.Startup();

    // 使用 FromGenerator 创建资源，类型是普通 struct，无需继承
    auto handle1 = resourceManager.Get<DataResource>(
        Resource::FromGenerator<DataResource>(
            "data_resource_1",
            []() -> std::unique_ptr<DataResource> {
                std::cout << "  Generator called" << std::endl;
                return std::make_unique<DataResource>(1, "Resource1", 100.0f);
            },
            nullptr,  // 默认 delete 销毁
            [](const std::string& key, DataResource* res) {
                std::cout << "  OnZero callback: " << key << std::endl;
            },
            [](const std::string& key, DataResource* res) {
                std::cout << "  OnRestore callback: " << key << std::endl;
            }
        )
    );

    if (handle1) {
        std::cout << "Handle1 acquired successfully" << std::endl;
        handle1->print();
        std::cout << "RefCount: " << handle1.GetRefCount() << std::endl;
    } else {
        std::cerr << "Failed to get handle1!" << std::endl;
    }

    // 拷贝句柄，测试引用计数
    std::cout << "\nCopying handle1 to handle2..." << std::endl;
    auto handle2 = handle1;

    if (handle2) {
        std::cout << "Handle2 acquired via copy" << std::endl;
        handle2->print();
        std::cout << "Handle1 RefCount: " << handle1.GetRefCount() << std::endl;
        std::cout << "Handle2 RefCount: " << handle2.GetRefCount() << std::endl;
    }

    // 再次获取同一资源
    std::cout << "\nGetting same resource by key..." << std::endl;
    auto handle3 = resourceManager.Get<DataResource>(
        Resource::FromKey<DataResource>("data_resource_1")
    );

    if (handle3) {
        std::cout << "Handle3 acquired from existing resource" << std::endl;
        handle3->print();
        std::cout << "Handle1 RefCount: " << handle1.GetRefCount() << std::endl;
        std::cout << "Handle3 RefCount: " << handle3.GetRefCount() << std::endl;
    }

    // 关闭模块前释放所有句柄
    std::cout << "\nReleasing handles..." << std::endl;
    handle1 = Resource::ResourceHandle<DataResource>(nullptr);
    handle2 = Resource::ResourceHandle<DataResource>(nullptr);
    handle3 = Resource::ResourceHandle<DataResource>(nullptr);

    // 关闭模块
    resourceManager.Shutdown();
    std::cout << "Test 1 completed." << std::endl;
}

/**
 * @brief 测试 2: 使用 FromPointer 接收外部创建的对象
 */
void testFromPointer() {
    std::cout << "\n=== Test 2: FromPointer (external object) ===" << std::endl;

    auto& resourceManager = Resource::ResourceManagerModule::GetInstance();
    resourceManager.Startup();

    // 外部创建对象
    std::cout << "Creating external object..." << std::endl;
    auto externalPtr = std::make_unique<ComplexResource>("ExternalResource");

    // 交给 ResourceManager 管理
    auto handle = resourceManager.Get<ComplexResource>(
        Resource::FromPointer<ComplexResource>(
            "external_resource",
            std::move(externalPtr),
            [](ComplexResource* p) {
                std::cout << "  Custom destroy function called" << std::endl;
                delete p;
            },
            [](const std::string& key, ComplexResource* res) {
                std::cout << "  OnZero: " << key << std::endl;
            }
        )
    );

    if (handle) {
        std::cout << "Handle acquired successfully" << std::endl;
        handle->process();
        std::cout << "RefCount: " << handle.GetRefCount() << std::endl;
    }

    resourceManager.Shutdown();
    std::cout << "Test 2 completed." << std::endl;
}

/**
 * @brief 测试 3: 自定义销毁策略
 */
void testCustomDestroy() {
    std::cout << "\n=== Test 3: Custom destroy strategy ===" << std::endl;

    auto& resourceManager = Resource::ResourceManagerModule::GetInstance();
    resourceManager.Startup();

    bool customDestroyCalled = false;

    auto handle = resourceManager.Get<DataResource>(
        Resource::FromGenerator<DataResource>(
            "custom_destroy_resource",
            []() -> std::unique_ptr<DataResource> {
                return std::make_unique<DataResource>(99, "CustomDestroy", 999.0f);
            },
            [&customDestroyCalled](DataResource* p) {
                std::cout << "  >>> Custom destroy function called!" << std::endl;
                customDestroyCalled = true;
                delete p;
            }
        )
    );

    if (handle) {
        std::cout << "Resource acquired" << std::endl;
        handle->print();
    }

    // 释放句柄，触发销毁
    std::cout << "Releasing handle..." << std::endl;
    handle = Resource::ResourceHandle<DataResource>(nullptr);

    std::cout << "Custom destroy called: " << (customDestroyCalled ? "yes" : "no") << std::endl;

    resourceManager.Shutdown();
    std::cout << "Test 3 completed." << std::endl;
}

/**
 * @brief 测试 4: 多种资源类型管理
 */
void testMultipleTypes() {
    std::cout << "\n=== Test 4: Multiple resource types ===" << std::endl;

    auto& resourceManager = Resource::ResourceManagerModule::GetInstance();
    resourceManager.Startup();

    // 管理 int 类型资源
    auto intHandle = resourceManager.Get<int>(
        Resource::FromGenerator<int>(
            "int_value",
            []() -> std::unique_ptr<int> {
                return std::make_unique<int>(42);
            }
        )
    );

    // 管理 std::string 类型资源
    auto stringHandle = resourceManager.Get<std::string>(
        Resource::FromGenerator<std::string>(
            "string_value",
            []() -> std::unique_ptr<std::string> {
                return std::make_unique<std::string>("Hello, ResourceManager!");
            }
        )
    );

    // 管理自定义类型资源
    auto dataHandle = resourceManager.Get<DataResource>(
        Resource::FromGenerator<DataResource>(
            "data_resource",
            []() -> std::unique_ptr<DataResource> {
                return std::make_unique<DataResource>(100, "MultiTypeTest", 3.14f);
            }
        )
    );

    if (intHandle) {
        std::cout << "int value: " << *intHandle << std::endl;
    }

    if (stringHandle) {
        std::cout << "string value: " << *stringHandle << std::endl;
    }

    if (dataHandle) {
        std::cout << "DataResource: ";
        dataHandle->print();
    }

    resourceManager.Shutdown();
    std::cout << "Test 4 completed." << std::endl;
}

// ============================================================================
// 主函数
// ============================================================================

int main() {
    std::cout << "=== ResourceManager Improvement Test ===" << std::endl;
    std::cout << "Testing new design: No ILoadable inheritance required" << std::endl;
    std::cout << "========================================" << std::endl;

    testFromGenerator();
    testFromPointer();
    testCustomDestroy();
    testMultipleTypes();

    std::cout << "\n=== All Tests Completed ===" << std::endl;
    return 0;
}
