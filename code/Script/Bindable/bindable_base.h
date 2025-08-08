#pragma once

#include <Json/json.h>

class BindableBase {
public:
    virtual void fromJson(const Json::Value& value) = 0;
    virtual Json::Value toJson() const = 0;
    virtual const std::string& getName() const = 0;
    virtual ~BindableBase() = default;
};