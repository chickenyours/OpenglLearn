#pragma once

class IModule {
public:
    virtual ~IModule() = default;
    virtual const char* GetName() const noexcept = 0;
    virtual bool Startup() = 0;
    virtual void Shutdown() = 0;
    virtual bool IsStarted() const noexcept = 0;
};