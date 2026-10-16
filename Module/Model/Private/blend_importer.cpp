#include "blend_importer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace Model {
namespace {
namespace fs = std::filesystem;

std::string Utf8(const fs::path& value) {
    const auto bytes = value.generic_u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

fs::path DefaultScript() {
#ifdef MODEL_SOURCE_DIR
    return fs::path(MODEL_SOURCE_DIR) / "Module/Model/Tools/blend_export.py";
#else
    return fs::current_path() / "Module/Model/Tools/blend_export.py";
#endif
}

fs::path DefaultCache() {
#ifdef MODEL_SOURCE_DIR
    return fs::path(MODEL_SOURCE_DIR) / "build/blend_import_cache";
#else
    return fs::current_path() / "build/blend_import_cache";
#endif
}

fs::path NewRevision(const fs::path& root) {
    fs::create_directories(root);
    static std::atomic_uint64_t serial{0};
    const auto ticks = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    for(unsigned attempt = 0; attempt != 100; ++attempt) {
        const auto path = root / ("blend-" + std::to_string(ticks) + "-" + std::to_string(serial.fetch_add(1)));
        if(fs::create_directory(path)) return path;
    }
    throw std::runtime_error("Cannot create a new Blender import cache revision: " + Utf8(root));
}

bool FileHasContent(const fs::path& file, std::uintmax_t minimum = 1) {
    std::error_code error;
    if(!fs::is_regular_file(file, error) || error) return false;
    const auto size = fs::file_size(file, error);
    return !error && size >= minimum;
}

#ifdef _WIN32
std::wstring Environment(const wchar_t* name) {
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if(!length) return {};
    std::wstring value(length, L'\0');
    const DWORD count = GetEnvironmentVariableW(name, value.data(), length);
    if(!count || count >= length) return {};
    value.resize(count);
    return value;
}

fs::path ResolveExecutable(const fs::path& requested) {
    auto checked = [](const fs::path& candidate) {
        if(!FileHasContent(candidate))
            throw std::runtime_error("Blender executable does not exist: " + Utf8(candidate));
        return fs::absolute(candidate).lexically_normal();
    };
    if(!requested.empty()) return checked(requested);
    for(const auto* name : {L"MODEL_BLENDER_EXECUTABLE", L"BLENDER_EXECUTABLE"}) {
        const auto value = Environment(name);
        if(!value.empty()) return checked(fs::path(value));
    }
    std::vector<fs::path> installed;
    for(const auto* name : {L"ProgramW6432", L"ProgramFiles", L"ProgramFiles(x86)"}) {
        const auto value = Environment(name);
        if(value.empty()) continue;
        const auto directory = fs::path(value) / L"Blender Foundation";
        std::error_code error;
        if(!fs::is_directory(directory, error) || error) continue;
        for(fs::directory_iterator iterator(directory, fs::directory_options::skip_permission_denied, error), end;
            !error && iterator != end; iterator.increment(error)) {
            const auto candidate = iterator->path() / L"blender.exe";
            if(FileHasContent(candidate)) installed.push_back(candidate);
        }
    }
    // Prefer the greatest numeric Blender version, rather than lexicographic
    // ordering (which would incorrectly place Blender 4.9 after Blender 4.10).
    auto version = [](const fs::path& path) {
        std::vector<unsigned> values;
        unsigned number = 0; bool collecting = false;
        for(const auto c : path.parent_path().filename().wstring()) {
            if(c >= L'0' && c <= L'9') { number = number * 10 + static_cast<unsigned>(c - L'0'); collecting = true; }
            else if(collecting) { values.push_back(number); number = 0; collecting = false; }
        }
        if(collecting) values.push_back(number);
        return values;
    };
    if(!installed.empty()) {
        std::sort(installed.begin(), installed.end(), [&](const fs::path& a, const fs::path& b) {
            const auto av = version(a), bv = version(b);
            return av == bv ? a.native() > b.native() : av > bv;
        });
        return checked(installed.front());
    }
    std::wstring searched(32768, L'\0');
    const DWORD count = SearchPathW(nullptr, L"blender.exe", nullptr, static_cast<DWORD>(searched.size()), searched.data(), nullptr);
    if(count && count < searched.size()) { searched.resize(count); return checked(fs::path(searched)); }
    throw std::runtime_error("Loading .blend requires Blender. Install Blender or set MODEL_BLENDER_EXECUTABLE / LoadOptions.blenderExecutable to blender.exe.");
}

class WinHandle {
public:
    WinHandle() = default;
    explicit WinHandle(HANDLE value) : value_(value) {}
    WinHandle(const WinHandle&) = delete;
    WinHandle& operator=(const WinHandle&) = delete;
    ~WinHandle() { if(value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
    HANDLE Get() const { return value_; }
    bool Valid() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
private:
    HANDLE value_ = nullptr;
};

std::wstring QuoteArgument(const std::wstring& value) {
    // The Windows CRT parses backslash runs before quotes specially. Quote
    // every argument, doubling only those runs that precede a quote or end.
    std::wstring quoted(1, L'"');
    std::size_t slashes = 0;
    for(const auto c : value) {
        if(c == L'\\') { ++slashes; continue; }
        if(c == L'"') quoted.append(slashes * 2 + 1, L'\\');
        else quoted.append(slashes, L'\\');
        slashes = 0;
        quoted.push_back(c);
    }
    quoted.append(slashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

void RunBlender(const fs::path& executable, const fs::path& script,
    const fs::path& source, const fs::path& directory, const fs::path& log,
    unsigned timeoutSeconds, bool bakeStaticPose) {
    std::vector<std::wstring> arguments{executable.native(), L"--background", L"--factory-startup",
        L"--disable-autoexec", L"--threads", L"2", L"--python-exit-code", L"1",
        L"--python", script.native(), L"--", L"--input", source.native(),
        L"--output-dir", directory.native()};
    if(bakeStaticPose) arguments.push_back(L"--static-pose");
    std::wstring command;
    for(const auto& argument : arguments) {
        if(!command.empty()) command.push_back(L' ');
        command += QuoteArgument(argument);
    }
    if(command.size() >= 32760) throw std::runtime_error("Blender import command exceeds the Windows command line limit.");
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    WinHandle output(CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    WinHandle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if(!output.Valid() || !input.Valid()) throw std::runtime_error("Cannot create Blender conversion log: " + Utf8(log));
    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    std::vector<unsigned char> attributeStorage(attributeBytes);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());
    if(!InitializeProcThreadAttributeList(attributes, 1, 0, &attributeBytes))
        throw std::runtime_error("Cannot initialize Blender process handle inheritance.");
    struct AttributeCleanup {
        LPPROC_THREAD_ATTRIBUTE_LIST value;
        ~AttributeCleanup() { DeleteProcThreadAttributeList(value); }
    } attributeCleanup{attributes};
    HANDLE inherited[]{output.Get(), input.Get()};
    if(!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        inherited, sizeof(inherited), nullptr, nullptr))
        throw std::runtime_error("Cannot restrict Blender process handle inheritance.");
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input.Get();
    startup.StartupInfo.hStdOutput = output.Get();
    startup.StartupInfo.hStdError = output.Get();
    startup.lpAttributeList = attributes;
    WinHandle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!job.Valid() || !SetInformationJobObject(job.Get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        throw std::runtime_error("Cannot create Blender conversion process lifetime guard.");
    PROCESS_INFORMATION processInfo{};
    const DWORD flags = CREATE_NO_WINDOW | CREATE_SUSPENDED | BELOW_NORMAL_PRIORITY_CLASS | EXTENDED_STARTUPINFO_PRESENT;
    if(!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
        flags, nullptr, directory.c_str(), &startup.StartupInfo, &processInfo))
        throw std::runtime_error("Cannot start Blender (Windows error " + std::to_string(GetLastError()) + "). Log: " + Utf8(log));
    WinHandle process(processInfo.hProcess), thread(processInfo.hThread);
    if(!AssignProcessToJobObject(job.Get(), process.Get())) {
        TerminateProcess(process.Get(), 1);
        throw std::runtime_error("Cannot guard Blender subprocess lifetime (Windows error " + std::to_string(GetLastError()) + "). Log: " + Utf8(log));
    }
    if(ResumeThread(thread.Get()) == static_cast<DWORD>(-1))
        throw std::runtime_error("Cannot resume Blender conversion. Log: " + Utf8(log));
    const auto maximumSeconds = static_cast<unsigned>((std::numeric_limits<DWORD>::max() - 1) / 1000);
    const DWORD milliseconds = std::min(timeoutSeconds, maximumSeconds) * 1000;
    const DWORD wait = WaitForSingleObject(process.Get(), milliseconds);
    if(wait == WAIT_TIMEOUT) {
        TerminateJobObject(job.Get(), 1);
        WaitForSingleObject(process.Get(), 1000);
        throw std::runtime_error("Blender conversion timed out after " + std::to_string(timeoutSeconds) + " seconds. Log: " + Utf8(log));
    }
    if(wait != WAIT_OBJECT_0) throw std::runtime_error("Cannot wait for Blender conversion. Log: " + Utf8(log));
    DWORD exitCode = 1;
    if(!GetExitCodeProcess(process.Get(), &exitCode) || exitCode != 0)
        throw std::runtime_error("Blender conversion failed (exit " + std::to_string(exitCode) + "). Log: " + Utf8(log));
}
#endif
} // namespace

std::optional<PreparedBlendSource> PrepareBlendSource(const fs::path& source,
    const BlendImportOptions& options, std::string* error) {
    if(error) error->clear();
    try {
        auto extension = Utf8(source.extension());
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if(extension != ".blend") throw std::runtime_error("The Blender bridge requires an explicit .blend source path.");
        if(!FileHasContent(source, 12)) throw std::runtime_error("Cannot open Blender source: " + Utf8(source));
        if(!options.timeoutSeconds) throw std::runtime_error("Blender import timeout must be positive.");
        PreparedBlendSource result;
        result.sourcePath = fs::absolute(source).lexically_normal();
        const auto script = fs::absolute(options.scriptPath.empty() ? DefaultScript() : options.scriptPath).lexically_normal();
        if(!FileHasContent(script)) throw std::runtime_error("Blender export script is missing: " + Utf8(script));
#ifdef _WIN32
        const auto executable = ResolveExecutable(options.blenderExecutable);
        const auto root = fs::absolute(options.cacheDirectory.empty() ? DefaultCache() : options.cacheDirectory).lexically_normal();
        const auto directory = NewRevision(root);
        result.glbPath = directory / "scene.glb";
        result.metadataPath = directory / "source.blend.json";
        result.logPath = directory / "blender.log";
        RunBlender(executable, script, result.sourcePath, directory, result.logPath, options.timeoutSeconds, options.bakeStaticPose);
        if(!FileHasContent(result.glbPath, 20) || !FileHasContent(result.metadataPath, 32))
            throw std::runtime_error("Blender conversion did not produce a complete GLB and source sidecar. Log: " + Utf8(result.logPath));
        result.warnings.push_back("Blender source was converted read-only with embedded Python scripts disabled; interchange and source metadata: " + Utf8(directory));
        if(options.bakeStaticPose)
            result.warnings.push_back("Blender saved source pose and default morphs are baked into the static model geometry.");
        result.warnings.push_back("Blender material graphs and physics authoring are not directly executable engine resources; inspect the source metadata and garment classification report.");
        return result;
#else
        throw std::runtime_error("The .blend background conversion bridge currently requires Windows and a local Blender installation.");
#endif
    } catch(const std::exception& exception) {
        if(error) *error = exception.what();
        return {};
    }
}

} // namespace Model
