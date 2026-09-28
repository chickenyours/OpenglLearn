#include "Scripting/Public/lua_module.h"
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <cstring>
namespace Scripting {
void* LuaModule::Allocate(void* user,void* old,size_t oldSize,size_t newSize) {
    auto& self=*static_cast<LuaModule*>(user);if(!old)oldSize=0;
    if(!newSize){std::free(old);self.bytes_-=oldSize;return nullptr;}
    if(self.bytes_-oldSize+newSize>8*1024*1024)return nullptr;
    auto result=std::realloc(old,newSize);if(result)self.bytes_=self.bytes_-oldSize+newSize;return result;
}
bool LuaModule::Startup() {
    if(state_)return true;error_.clear();state_=lua_newstate(Allocate,this);
    if(!state_){error_="Cannot create Lua VM";return false;}
    *static_cast<LuaModule**>(lua_getextraspace(state_))=this;
    for(auto library:{std::pair<const char*,lua_CFunction>{"_G",luaopen_base},{LUA_TABLIBNAME,luaopen_table},
            {LUA_STRLIBNAME,luaopen_string},{LUA_MATHLIBNAME,luaopen_math},{LUA_UTF8LIBNAME,luaopen_utf8}}) {
        luaL_requiref(state_,library.first,library.second,1);lua_pop(state_,1);
    }
    for(auto name:{"dofile","loadfile","load","collectgarbage"}){lua_pushnil(state_);lua_setglobal(state_,name);}
    return true;
}
void LuaModule::Shutdown(){if(state_)lua_close(state_);state_=nullptr;module_=-1;bytes_=0;time_=0;commands_.clear();timers_.clear();once_.clear();}
void LuaModule::Hook(lua_State* state,lua_Debug*) {
    auto self=*static_cast<LuaModule**>(lua_getextraspace(state));
    if((self->instructions_-=1000)<=0)luaL_error(state,"Script instruction budget exceeded");
}
bool LuaModule::ProtectedCall(int args,int results) {
    instructions_=100000;lua_sethook(state_,Hook,LUA_MASKCOUNT,1000);
    int result=lua_pcall(state_,args,results,0);lua_sethook(state_,nullptr,0,0);
    if(result!=LUA_OK){auto text=lua_tostring(state_,-1);error_=text?text:"Lua callback failed";lua_pop(state_,1);commands_.clear();timers_.clear();return false;}
    return true;
}
bool LuaModule::Load(const std::filesystem::path& file) {
    Shutdown();if(!Startup())return false;
    if(luaL_loadfilex(state_,file.string().c_str(),"t")!=LUA_OK){error_=lua_tostring(state_,-1);lua_pop(state_,1);return false;}
    if(!ProtectedCall(0,1))return false;
    if(!lua_istable(state_,-1)){error_="Room Lua script must return a callback table";lua_pop(state_,1);return false;}
    lua_pushcfunction(state_,StoreModule);lua_insert(state_,-2);return ProtectedCall(1,0);
}
bool LuaModule::LoadSource(const std::string& source,const std::string& name) {
    Shutdown();if(!Startup())return false;
    if(luaL_loadbufferx(state_,source.data(),source.size(),name.c_str(),"t")!=LUA_OK){error_=lua_tostring(state_,-1);lua_pop(state_,1);return false;}
    if(!ProtectedCall(0,1))return false;
    if(!lua_istable(state_,-1)){error_="Room Lua must return a callback table";lua_pop(state_,1);return false;}
    lua_pushcfunction(state_,StoreModule);lua_insert(state_,-2);return ProtectedCall(1,0);
}
int LuaModule::StoreModule(lua_State* state) {
    auto self=*static_cast<LuaModule**>(lua_getextraspace(state));self->module_=luaL_ref(state,LUA_REGISTRYINDEX);return 0;
}
void LuaModule::PushContext() {
    lua_newtable(state_);
    for(auto name:{"set_velocity","set_enabled","spawn","destroy","sound","message","change_room","complete","after","once"}) {
        lua_pushlightuserdata(state_,this);lua_pushstring(state_,name);lua_pushcclosure(state_,Api,2);lua_setfield(state_,-2,name);
    }
}
int LuaModule::Api(lua_State* state) {
    auto self=static_cast<LuaModule*>(lua_touserdata(state,lua_upvalueindex(1)));
    const char* op=lua_tostring(state,lua_upvalueindex(2));
    // Validate Lua arguments before constructing C++ objects: luaL_error longjmps.
    const char* id="";const char* text="";double x=0,y=0;bool enabled=false;
    if(!std::strcmp(op,"once")){
        id=luaL_checkstring(state,2);if(std::strlen(id)>128||self->once_.size()>=4096)return luaL_error(state,"Invalid or excessive once keys");
        lua_pushboolean(state,self->once_.insert(id).second);return 1;
    }
    if(!std::strcmp(op,"after")) {
        x=luaL_checknumber(state,2);id=luaL_checkstring(state,3);text=luaL_optstring(state,4,"");
        if(!std::isfinite(x)||x<0||x>3600||self->timers_.size()>=128||std::strlen(id)>128||std::strlen(text)>128)return luaL_error(state,"Invalid or excessive timers");
        self->timers_.push_back({self->time_+x,id,text});return 0;
    }
    if(std::strcmp(op,"complete"))id=luaL_checkstring(state,2);
    if(!std::strcmp(op,"set_velocity")){x=luaL_checknumber(state,3);y=luaL_checknumber(state,4);}
    if(!std::strcmp(op,"set_enabled")){luaL_checktype(state,3,LUA_TBOOLEAN);enabled=lua_toboolean(state,3)!=0;}
    if(!std::strcmp(op,"spawn")){text=luaL_checkstring(state,3);x=luaL_checknumber(state,4);y=luaL_checknumber(state,5);}
    if(!std::strcmp(op,"change_room"))text=luaL_checkstring(state,3);
    if(!std::isfinite(x)||!std::isfinite(y)||std::abs(x)>1000||std::abs(y)>1000||self->commands_.size()>=256||std::strlen(id)>512||std::strlen(text)>128)return luaL_error(state,"Invalid or excessive commands");
    self->commands_.push_back({op,id,text,x,y,enabled});return 0;
}
bool LuaModule::Call(const Event& event) {
    if(!state_||module_<0||!error_.empty())return false;
    event_=event;lua_pushcfunction(state_,Dispatch);return ProtectedCall(0,0);
}
int LuaModule::Dispatch(lua_State* state) {
    auto self=*static_cast<LuaModule**>(lua_getextraspace(state));const auto& event=self->event_;
    lua_rawgeti(state,LUA_REGISTRYINDEX,self->module_);lua_getfield(state,-1,event.callback.c_str());lua_remove(state,-2);
    if(lua_isnil(state,-1)){lua_pop(state,1);return 0;}
    if(!lua_isfunction(state,-1))return luaL_error(state,"Callback must be a function");
    // Context/event allocation also occurs under pcall, including memory errors.
    self->PushContext();lua_newtable(state);
    lua_pushlstring(state,event.name.data(),event.name.size());lua_setfield(state,-2,"name");
    lua_pushlstring(state,event.id.data(),event.id.size());lua_setfield(state,-2,"id");
    lua_pushlstring(state,event.phase.data(),event.phase.size());lua_setfield(state,-2,"phase");
    lua_newtable(state);
    for(const auto& [key,value]:event.properties) {
        if(auto v=std::get_if<double>(&value))lua_pushnumber(state,*v);
        else if(auto v=std::get_if<bool>(&value))lua_pushboolean(state,*v);
        else {const auto& text=std::get<std::string>(value);lua_pushlstring(state,text.data(),text.size());}
        lua_setfield(state,-2,key.c_str());
    }
    lua_setfield(state,-2,"properties");
    lua_call(state,2,0);return 0;
}
void LuaModule::Tick(double dt) {
    if(!error_.empty())return;time_+=dt;
    std::vector<Timer> ready;
    for(auto i=timers_.begin();i!=timers_.end();)if(i->due<=time_){ready.push_back(*i);i=timers_.erase(i);}else ++i;
    for(const auto& timer:ready)if(!Call({"on_timer",timer.name,timer.id,""}))break;
}
std::vector<Command> LuaModule::TakeCommands(){auto result=std::move(commands_);commands_.clear();return result;}
}
