#pragma once
#include <json/json.h>
#include <filesystem>
namespace IWanna::Content {
inline std::filesystem::path Path(const std::string& utf8){return std::filesystem::path(std::u8string(utf8.begin(),utf8.end()));}
Json::Value Read(const std::filesystem::path& path);
void Write(const std::filesystem::path& path,const Json::Value& value);
Json::Value ExpandWorld(const std::filesystem::path& path);
Json::Value ToTiled(const Json::Value& room);
Json::Value FromTiled(const Json::Value& map);
}
