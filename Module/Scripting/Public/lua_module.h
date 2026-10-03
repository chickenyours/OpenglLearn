#pragma once
#include "module_base.h"
#include <filesystem>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <variant>
#include <functional>
#include <optional>
#include <utility>
struct lua_State;
struct lua_Debug;
namespace Scripting {
struct Command {
    std::string operation,id,text;
    double x=0,y=0;
    bool enabled=false;
    double zoom=1,followSpeed=-1;
    std::string anchor;
    int minCount=12,maxCount=24;
    double radius=1.5,speed=15,lifetime=3,phase=0;
};
struct Event {std::string callback,name,id,phase;std::map<std::string,std::variant<double,bool,std::string>> properties;};
// One VM per active room. C++ retains ownership of simulation and entity memory.
class LuaModule final : public IModule {
public:
    ~LuaModule() override {Shutdown();}
    const char* GetName() const noexcept override {return "LuaModule";}
    bool Startup() override;
    void Shutdown() override;
    bool IsStarted() const noexcept override {return state_!=nullptr;}
    bool Load(const std::filesystem::path& file);
    bool LoadSource(const std::string& source,const std::string& name);
    bool Call(const Event& event);
    void Tick(double dt);
    std::vector<Command> TakeCommands();
    const std::string& Error() const {return error_;}
    // Read-only host query. No ECS or math-library types cross the module boundary.
    using Position=std::pair<double,double>;
    std::function<std::optional<Position>(const std::string&)> queryPosition;
private:
    lua_State* state_=nullptr;
    int module_=-1;
    size_t bytes_=0;
    int instructions_=0;
    double time_=0;
    std::string error_;
    struct Timer {double due;std::string name,id;};
    std::vector<Timer> timers_;
    std::vector<Command> commands_;
    std::set<std::string> once_;
    Event event_;
    static void* Allocate(void*,void*,size_t,size_t);
    static void Hook(lua_State*,lua_Debug*);
    static int Api(lua_State*);
    static int Dispatch(lua_State*);
    static int StoreModule(lua_State*);
    void PushContext();
    bool ProtectedCall(int arguments,int results);
};
}
