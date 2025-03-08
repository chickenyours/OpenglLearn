#pragma once
#include <string>
#include <json/json.h>

using namespace std;

namespace JsonFileAssistant{
    void WriteFileFromString(const std::string &filename,const std::string &body);
    bool ReadJsonFile(const std::string &filename,Json::Value &root);
    bool ReadJsonFromString(const std::string &mystr,Json::Value &root);
    void WriteJsonFile(const std::string &filename,const Json::Value &root);
    void SetError(const string &error);
    string GetError();
}









