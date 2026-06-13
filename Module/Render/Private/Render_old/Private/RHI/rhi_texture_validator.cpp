#include "Render/Public/RHI/rhi_texture_validator.h"
#include "Render/Public/RHI/RHI_texture.h"

#include <unordered_map>
#include <mutex>

namespace Render::RHI {
namespace {

    std::unordered_map<std::string, TextureValidator> g_validators;
    std::mutex g_validatorMutex;

} // namespace

    void RegisterTextureValidator(const std::string& name, TextureValidator validator) {
        std::lock_guard<std::mutex> lock(g_validatorMutex);
        g_validators[name] = std::move(validator);
    }

    TextureValidator GetTextureValidator(const std::string& name) {
        std::lock_guard<std::mutex> lock(g_validatorMutex);
        auto it = g_validators.find(name);
        if (it != g_validators.end()) {
            return it->second;
        }
        return nullptr;
    }

    TextureValidationResult ValidateTexture(const RHITexture& texture, const std::string& validatorName) {
        auto validator = GetTextureValidator(validatorName);
        if (validator) {
            return validator(texture);
        }
        
        TextureValidationResult result;
        result.errorMessage = "Validator not found: " + validatorName;
        return result;
    }

    std::vector<std::pair<std::string, TextureValidationResult>> ValidateTexture(const RHITexture& texture) {
        std::vector<std::pair<std::string, TextureValidationResult>> results;
        
        std::lock_guard<std::mutex> lock(g_validatorMutex);
        for (const auto& [name, validator] : g_validators) {
            results.emplace_back(name, validator(texture));
        }
        
        return results;
    }

} // namespace Render::RHI
