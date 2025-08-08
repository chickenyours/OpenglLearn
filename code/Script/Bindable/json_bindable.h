#pragma once

#include "code/Script/Bindable/bindable.h"

class JsonBindable {
protected:
    void registerBindable(BindableBase* field) {
        fields_.push_back(field);
    }

public:
    void loadFromJson(const Json::Value& json) {
        for (auto* f : fields_) {
            f->fromJson(json);
        }
    }

    Json::Value toJson() const {
        Json::Value json;
        for (auto* f : fields_) {
            json[f->getName()] = f->toJson();
        }
        return json;
    }
  
private:
    std::vector<BindableBase*> fields_;
};

#define BINDABLE(type, varname, defaultVal) \
    Bindable<type> varname{#varname, defaultVal}; \
    registerBindable(&varname);

