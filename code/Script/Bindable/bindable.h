#pragma once

#include "code/Script/Bindable/bindable_base.h"

template<typename T>
class Bindable : public BindableBase {
public:  
    Bindable(const std::string& name, T defaultVal)
        : name_(name), value_(defaultVal) {}

    void fromJson(const Json::Value& json) override {
        if (json.isMember(name_)) {
            value_ = json[name_].as<T>(); // 自动类型转换
        }
    }

    Json::Value toJson() const override {
        return Json::Value(value_); 
    }

    const std::string& getName() const override {
        return name_;
    }

    T& get() { return value_; }
    const T& get() const { return value_; }

private:
    std::string name_;
    T value_;
};

