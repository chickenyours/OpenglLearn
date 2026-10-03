#pragma once
#include "content_format.h"
#include <glm/glm.hpp>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
extern char **environ;
#endif

namespace IWanna {
inline std::vector<std::filesystem::path> PlaytestArguments(const std::filesystem::path& game,const std::filesystem::path& assets,const std::filesystem::path& world,const std::string& room,const std::string& spawn,std::optional<glm::vec2> position={}) {
    std::vector<std::filesystem::path> args{game,"--assets",assets,"--world",world,"--room",Content::Path(room),"--spawn",Content::Path(spawn)};
    if(position){auto number=[](float value){std::ostringstream out;out<<std::setprecision(9)<<value;return std::filesystem::path(out.str());};args.insert(args.end(),{"--position",number(position->x),number(position->y)});}
    return args;
}
#ifdef _WIN32
inline std::wstring QuoteProcessArgument(const std::wstring& arg){
    std::wstring out=L"\"";size_t slash=0;
    for(wchar_t ch:arg){if(ch==L'\\'){++slash;continue;}
        if(ch==L'"'){out.append(slash*2+1,L'\\');out+=ch;}
        else {out.append(slash,L'\\');out+=ch;}slash=0;}
    out.append(slash*2,L'\\');out+=L'"';return out;
}
#endif
inline void LaunchPlaytest(const std::filesystem::path& game,const std::filesystem::path& assets,const std::filesystem::path& world,const std::string& room,const std::string& spawn,std::optional<glm::vec2> position={},bool smoke=false){
    if(!std::filesystem::exists(game))throw std::runtime_error("Game executable is missing; run build.bat");
    auto args=PlaytestArguments(game,assets,world,room,spawn,position);
    if(smoke){args.emplace_back("--smoke-test");args.emplace_back("--mute");}
#ifdef _WIN32
    std::wstring command;for(const auto& arg:args){if(!command.empty())command+=L' ';command+=QuoteProcessArgument(arg.wstring());}
    std::vector<wchar_t> mutableCommand(command.begin(),command.end());mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(game.wstring().c_str(),mutableCommand.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process))throw std::runtime_error("Cannot start game process: Windows error "+std::to_string(GetLastError()));
    CloseHandle(process.hThread);
    if(smoke){WaitForSingleObject(process.hProcess,30000);DWORD code=1;GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hProcess);if(code!=0)throw std::runtime_error("Playtest game process failed");}
    else CloseHandle(process.hProcess);
#else
    std::vector<std::string> strings;for(const auto& arg:args)strings.push_back(arg.string());
    std::vector<char*> argv;for(auto& arg:strings)argv.push_back(arg.data());argv.push_back(nullptr);
    pid_t pid=0;int error=posix_spawn(&pid,game.c_str(),nullptr,nullptr,argv.data(),environ);
    if(error)throw std::runtime_error("Cannot start game process: "+std::to_string(error));
    if(smoke){int status=0;if(waitpid(pid,&status,0)<0||!WIFEXITED(status)||WEXITSTATUS(status)!=0)throw std::runtime_error("Playtest game process failed");}
    else std::thread([pid]{int status=0;waitpid(pid,&status,0);}).detach();
#endif
}
}
