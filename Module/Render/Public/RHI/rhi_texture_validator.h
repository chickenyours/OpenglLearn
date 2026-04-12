#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <utility>

namespace Render::RHI {

    class RHITexture;

    /**
     * @brief 纹理验证结果
     */
    struct TextureValidationResult {
        bool isValid = false;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t mipLevels = 0;
        std::size_t nativeHandle = 0;
        std::string errorMessage;
    };

    /**
     * @brief 纹理验证委托函数类型
     * @param texture 要验证的 GPU 纹理资源对象
     * @return 验证结果
     */
    using TextureValidator = std::function<TextureValidationResult(const RHITexture&)>;

    /**
     * @brief 注册纹理验证器
     * @param name 验证器名称
     * @param validator 验证函数
     */
    void RegisterTextureValidator(const std::string& name, TextureValidator validator);

    /**
     * @brief 获取已注册的验证器
     * @param name 验证器名称
     * @return 验证器函数，如果不存在则返回空函数
     */
    TextureValidator GetTextureValidator(const std::string& name);

    /**
     * @brief 使用指定的验证器验证纹理
     * @param texture 要验证的 GPU 纹理资源
     * @param validatorName 验证器名称
     * @return 验证结果
     */
    TextureValidationResult ValidateTexture(const RHITexture& texture, const std::string& validatorName);

    /**
     * @brief 使用所有已注册的验证器验证纹理
     * @param texture 要验证的 GPU 纹理资源
     * @return 所有验证结果
     */
    std::vector<std::pair<std::string, TextureValidationResult>> ValidateTexture(const RHITexture& texture);

} // namespace Render::RHI
