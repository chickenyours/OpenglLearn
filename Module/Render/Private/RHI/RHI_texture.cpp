#include "Render/Public/RHI/RHI_texture.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <json/json.h>
#include <stb_image.h>

#include "engine/DebugTool/ConsoleHelp/color_log.h"

namespace Render::RHI {
namespace {

    bool LoadJsonFile(const std::string& filePath, Json::Value& root) {
        std::ifstream ifs(filePath, std::ios::binary);
        if (!ifs.is_open()) {
            return false;
        }

        Json::CharReaderBuilder builder;
        builder["collectComments"] = false;

        JSONCPP_STRING errs;
        return Json::parseFromStream(builder, ifs, &root, &errs);
    }

    std::string GetStringOrDefault(const Json::Value& obj, const char* key, const std::string& defaultValue) {
        if (!obj.isObject() || !obj.isMember(key) || !obj[key].isString()) {
            return defaultValue;
        }
        return obj[key].asString();
    }

    bool GetBoolOrDefault(const Json::Value& obj, const char* key, bool defaultValue) {
        if (!obj.isObject() || !obj.isMember(key) || !obj[key].isBool()) {
            return defaultValue;
        }
        return obj[key].asBool();
    }

    std::filesystem::path ResolveRelativePath(
        const std::filesystem::path& baseFile,
        const std::string& maybeRelative) {
        std::filesystem::path p(maybeRelative);
        if (p.is_absolute()) {
            return p;
        }
        return baseFile.parent_path() / p;
    }

} // namespace

    void TextureAsset::Reset() noexcept {
        desc_ = TextureDesc{};
        source_.Reset();
        sourcePath_.clear();
        configPath_.clear();
    }

    WrapMode TextureAsset::ParseWrapMode(const std::string& text) {
        if (text == "REPEAT") {
            return WrapMode::Repeat;
        }
        if (text == "CLAMP_TO_EDGE") {
            return WrapMode::ClampToEdge;
        }
        if (text == "MIRRORED_REPEAT") {
            return WrapMode::MirroredRepeat;
        }
        if (text == "CLAMP_TO_BORDER") {
            return WrapMode::ClampToBorder;
        }
        return WrapMode::Repeat;
    }

    FilterMode TextureAsset::ParseFilterMode(const std::string& text) {
        if (text == "NEAREST") {
            return FilterMode::Nearest;
        }
        if (text == "LINEAR") {
            return FilterMode::Linear;
        }
        if (text == "NEAREST_MIPMAP_NEAREST") {
            return FilterMode::NearestMipmapNearest;
        }
        if (text == "LINEAR_MIPMAP_NEAREST") {
            return FilterMode::LinearMipmapNearest;
        }
        if (text == "NEAREST_MIPMAP_LINEAR") {
            return FilterMode::NearestMipmapLinear;
        }
        if (text == "LINEAR_MIPMAP_LINEAR") {
            return FilterMode::LinearMipmapLinear;
        }
        return FilterMode::Linear;
    }

    uint32_t TextureAsset::CalculateMipLevels(uint32_t width, uint32_t height, uint32_t depth) {
        const uint32_t maxDim = std::max(width, std::max(height, depth));
        uint32_t levels = 1;
        uint32_t value = maxDim;
        while (value > 1) {
            value >>= 1;
            ++levels;
        }
        return levels;
    }

    Format TextureAsset::DeduceFormatFromChannels(uint32_t channelCount, bool hdr) {
        if (hdr) {
            switch (channelCount) {
            case 1: return Format::R16_Float;
            case 2: return Format::RG16_Float;
            case 4: return Format::RGBA16_Float;
            default: return Format::RGBA16_Float;
            }
        }

        switch (channelCount) {
        case 1: return Format::R8_UNorm;
        case 2: return Format::RG8_UNorm;
        case 3: return Format::RGB8_UNorm;
        case 4: return Format::RGBA8_UNorm;
        default: return Format::Unknown;
        }
    }

    bool TextureAsset::LoadFromConfigFile(const std::string& configFile) {
        Reset();
        configPath_ = configFile;

        Json::Value root;
        if (!LoadJsonFile(configFile, root)) {
            return false;
        }

        if (!root.isObject()) {
            return false;
        }

        if (GetStringOrDefault(root, "configType", "") != "resource") {
            return false;
        }

        const Json::Value& resource = root["resource"];
        if (!resource.isObject()) {
            return false;
        }

        if (GetStringOrDefault(resource, "resourceType", "") != "texture") {
            return false;
        }

        const Json::Value& texture = resource["texture"];
        if (!texture.isObject()) {
            return false;
        }

        const std::string textureType = GetStringOrDefault(texture, "textureType", "");
        if (textureType != "2D") {
            return false;
        }

        const Json::Value& args = texture["args"];
        if (!args.isObject()) {
            return false;
        }

        std::string imagePath = GetStringOrDefault(args, "path", "");
        if (imagePath.empty()) {
            return false;
        }

        const std::filesystem::path resolvedImagePath =
            ResolveRelativePath(std::filesystem::path(configFile), imagePath);

        desc_.dimension = TextureDimension::Tex2D;
        desc_.wrapS = ParseWrapMode(GetStringOrDefault(args, "wrapS", "REPEAT"));
        desc_.wrapT = ParseWrapMode(GetStringOrDefault(args, "wrapT", "REPEAT"));
        desc_.wrapR = WrapMode::Repeat;

        desc_.minFilter = ParseFilterMode(GetStringOrDefault(args, "minFilter", "LINEAR"));
        desc_.magFilter = ParseFilterMode(GetStringOrDefault(args, "magFilter", "LINEAR"));

        const bool needHDR = GetBoolOrDefault(args, "needHDR", false);
        const bool needMipMap = GetBoolOrDefault(args, "needMipMap", true);
        const bool flipVertical = GetBoolOrDefault(args, "flipVerticalOnLoad", true);

        return LoadImageFile2DInternal(resolvedImagePath.string(), needHDR, needMipMap, flipVertical);
    }

    bool TextureAsset::LoadFromImageFile2D(const std::string& imagePath) {
        Reset();
        return LoadImageFile2DInternal(imagePath, false, true, false);
    }

    bool TextureAsset::LoadImageFile2DInternal(
        const std::string& imagePath,
        bool needHDR,
        bool needMipMap,
        bool flipVerticalOnLoad
    ) {
        desc_.dimension = TextureDimension::Tex2D;
        desc_.generateMips = needMipMap;
        desc_.flipVerticalOnLoad = flipVerticalOnLoad;
        desc_.usage = TextureUsage::ShaderResource;

        sourcePath_ = imagePath;
        stbi_set_flip_vertically_on_load(flipVerticalOnLoad ? 1 : 0);

        int width = 0;
        int height = 0;
        int channels = 0;

        if (needHDR) {
            float* data = stbi_loadf(imagePath.c_str(), &width, &height, &channels, 0);
            if (data == nullptr) {
                return false;
            }

            if (width <= 0 || height <= 0 || channels <= 0) {
                stbi_image_free(data);
                return false;
            }

            const std::size_t count =
                static_cast<std::size_t>(width) *
                static_cast<std::size_t>(height) *
                static_cast<std::size_t>(channels);

            source_.Reset();
            source_.dataType = TextureDataType::Float32;
            source_.width = static_cast<uint32_t>(width);
            source_.height = static_cast<uint32_t>(height);
            source_.depth = 1;
            source_.channelCount = static_cast<uint32_t>(channels);
            source_.bytesPerChannel = sizeof(float);
            source_.valid = true;
            source_.rawData.resize(count * sizeof(float));

            std::memcpy(source_.rawData.data(), data, source_.rawData.size());
            stbi_image_free(data);

            desc_.width = source_.width;
            desc_.height = source_.height;
            desc_.depth = 1;
            desc_.arrayLayers = 1;
            desc_.sampleCount = 1;
            desc_.format = DeduceFormatFromChannels(source_.channelCount, true);
            desc_.mipLevels = needMipMap ? CalculateMipLevels(desc_.width, desc_.height, 1) : 1;
            return desc_.format != Format::Unknown;
        }

        unsigned char* data = stbi_load(imagePath.c_str(), &width, &height, &channels, 0);
        if (data == nullptr) {
            LOG_ERROR("LoadImageFile2DInternal","error," + imagePath);
            return false;
        }

        if (width <= 0 || height <= 0 || channels <= 0) {
            stbi_image_free(data);
            return false;
        }

        const std::size_t count =
            static_cast<std::size_t>(width) *
            static_cast<std::size_t>(height) *
            static_cast<std::size_t>(channels);

        source_.Reset();
        source_.dataType = TextureDataType::UInt8;
        source_.width = static_cast<uint32_t>(width);
        source_.height = static_cast<uint32_t>(height);
        source_.depth = 1;
        source_.channelCount = static_cast<uint32_t>(channels);
        source_.bytesPerChannel = sizeof(std::uint8_t);
        source_.valid = true;
        source_.rawData.resize(count);

        std::memcpy(source_.rawData.data(), data, source_.rawData.size());
        stbi_image_free(data);

        desc_.width = source_.width;
        desc_.height = source_.height;
        desc_.depth = 1;
        desc_.arrayLayers = 1;
        desc_.sampleCount = 1;
        desc_.format = DeduceFormatFromChannels(source_.channelCount, false);
        desc_.mipLevels = needMipMap ? CalculateMipLevels(desc_.width, desc_.height, 1) : 1;

        return desc_.format != Format::Unknown;
    }

} // namespace Render::RHI