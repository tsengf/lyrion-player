// SPDX-License-Identifier: GPL-3.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include "platform.hpp"
#include <algorithm>

namespace tray::platform {
class WindowsEnvironment final : public AppEnvironment {
public:
    WindowsEnvironment() {
        if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
            throw std::runtime_error("Cannot initialize Windows shell services");
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data)) {
            CoUninitialize();
            throw std::runtime_error("Cannot initialize Windows networking");
        }
    }
    ~WindowsEnvironment() override { WSACleanup(); CoUninitialize(); }
};
std::unique_ptr<AppEnvironment> initializeApplication() {
    return std::make_unique<WindowsEnvironment>();
}
class WindowsAppLock final : public AppLock {
public:
    HANDLE handle;
    explicit WindowsAppLock(HANDLE value) : handle(value) {}
    ~WindowsAppLock() override { CloseHandle(handle); }
};
std::unique_ptr<AppLock> lockApplication() {
    HANDLE handle = CreateMutexW(nullptr, FALSE, L"Local\\LyrionPlayerTray");
    if (!handle) throw std::runtime_error("Cannot create the tray application lock");
    if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(handle); return nullptr; }
    return std::make_unique<WindowsAppLock>(handle);
}

static std::wstring wide(const std::string &s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    if (!n) throw std::runtime_error("Invalid UTF-8");
    std::wstring out(n, L'\0'); MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n); return out;
}
static void check(HRESULT result) {
    if (FAILED(result)) throw std::runtime_error("Windows operation failed (code " + std::to_string((unsigned long)result) + ")");
}
static std::wstring quote(const std::wstring &value) {
    std::wstring out = L"\""; unsigned slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++slashes; continue; }
        out.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\'); slashes = 0; out += c;
    }
    out.append(slashes * 2, L'\\'); return out + L"\"";
}
static fs::path localData() {
    PWSTR path = nullptr;
    check(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path));
    fs::path result(path); CoTaskMemFree(path); return result / L"lyrion-player";
}
fs::path configDir() { return localData(); }
fs::path logDir() { return localData(); }
static fs::path appPath() {
    std::wstring path(32768, L'\0');
    DWORD size = GetModuleFileNameW(nullptr, path.data(), (DWORD)path.size());
    if (!size || size == path.size()) throw std::runtime_error("Cannot locate tray application");
    path.resize(size); return fs::path(path);
}
fs::path backend() {
    auto path = appPath().parent_path() / L"player.exe";
    if (!fs::exists(path)) throw std::runtime_error("The bundled player.exe is missing");
    return path;
}
class WindowsChild final : public Child {
    HANDLE process = nullptr, job = nullptr, input = nullptr, output = nullptr;
    int code = -1;
public:
    WindowsChild(const fs::path &binary, const std::vector<std::string> &args, const fs::path &log) {
        HANDLE childIn = nullptr, childOut = nullptr, childError = nullptr;
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        PROCESS_INFORMATION info{};
        std::vector<unsigned char> attributes;
        STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup);
        auto cleanup = [&] {
            for (HANDLE h : {childIn, childOut, childError}) if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
            if (startup.lpAttributeList) DeleteProcThreadAttributeList(startup.lpAttributeList);
        };
        try {
            if (!CreatePipe(&childIn, &input, &sa, 0) || !CreatePipe(&output, &childOut, &sa, 0))
                throw std::runtime_error("Could not create player pipes");
            SetHandleInformation(input, HANDLE_FLAG_INHERIT, 0); SetHandleInformation(output, HANDLE_FLAG_INHERIT, 0);
            childError = CreateFileW(log.empty() ? L"NUL" : log.c_str(), FILE_APPEND_DATA,
                FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (childError == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not open instance log");
            job = CreateJobObjectW(nullptr, nullptr);
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
                throw std::runtime_error("Could not create player process group");
            SIZE_T bytes = 0;
            InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
            attributes.resize(bytes);
            startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
            if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &bytes))
                { startup.lpAttributeList = nullptr; throw std::runtime_error("Could not initialize player handles"); }
            HANDLE handles[] = {childIn, childOut, childError};
            if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                    handles, sizeof(handles), nullptr, nullptr)) throw std::runtime_error("Could not restrict inherited handles");
            startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
            startup.StartupInfo.hStdInput = childIn; startup.StartupInfo.hStdOutput = childOut; startup.StartupInfo.hStdError = childError;
            std::wstring command = quote(binary.wstring());
            for (const auto &arg : args) command += L" " + quote(wide(arg));
            if (!CreateProcessW(binary.c_str(), command.data(), nullptr, nullptr, TRUE,
                    CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                    binary.parent_path().c_str(), &startup.StartupInfo, &info))
                throw std::runtime_error("Could not launch player (code " + std::to_string(GetLastError()) + ")");
            process = info.hProcess;
            if (!AssignProcessToJobObject(job, process)) throw std::runtime_error("Could not attach player to its tray owner");
            ResumeThread(info.hThread); CloseHandle(info.hThread); info.hThread = nullptr;
            cleanup();
        } catch (...) {
            if (process) { TerminateProcess(process, 1); CloseHandle(process); process = nullptr; }
            if (info.hThread) CloseHandle(info.hThread);
            cleanup();
            for (HANDLE h : {input, output, job}) if (h) CloseHandle(h);
            input = output = job = nullptr;
            throw;
        }
    }
    ~WindowsChild() override {
        if (alive()) kill();
        for (HANDLE h : {input, output, process, job}) if (h) CloseHandle(h);
    }
    bool alive() override {
        if (!process) return false;
        if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT) return true;
        DWORD exit = 0; GetExitCodeProcess(process, &exit); code = (int)exit; return false;
    }
    int exitCode() override { alive(); return code; }
    std::string read() override {
        std::string result;
        DWORD available = 0, count = 0;
        char buffer[4096];
        while (PeekNamedPipe(output, nullptr, 0, nullptr, &available, nullptr) && available) {
            if (!ReadFile(output, buffer, std::min<DWORD>(available, sizeof(buffer)), &count, nullptr)) break;
            result.append(buffer, count);
            if (result.size() > 4 * 1024 * 1024) break;
        }
        return result;
    }
    void stop() override {
        if (input) { DWORD count; WriteFile(input, "stop\n", 5, &count, nullptr); CloseHandle(input); input = nullptr; }
    }
    void kill() override { if (job) TerminateJobObject(job, 1); if (process) WaitForSingleObject(process, 1000); }
};
std::unique_ptr<Child> spawn(const fs::path &binary, const std::vector<std::string> &args, const fs::path &log) {
    return std::make_unique<WindowsChild>(binary, args, log);
}
void replaceFile(const fs::path &from, const fs::path &to) {
    if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Could not replace configuration");
}
void openFile(const fs::path &path) {
    if ((INT_PTR)ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL) <= 32)
        throw std::runtime_error("Could not open file");
}
void openURL(const std::string &url) {
    if ((INT_PTR)ShellExecuteW(nullptr, L"open", wide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL) <= 32)
        throw std::runtime_error("Could not open browser");
}
static constexpr auto runKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
std::string loginStatus() {
    HKEY key; if (RegOpenKeyExW(HKEY_CURRENT_USER, runKey, 0, KEY_QUERY_VALUE, &key)) return "off";
    DWORD size = 0; auto result = RegQueryValueExW(key, L"LyrionPlayerTray", nullptr, nullptr, nullptr, &size);
    RegCloseKey(key); return result == ERROR_SUCCESS && size > sizeof(wchar_t) ? "on" : "off";
}
void setLogin(bool enabled) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, runKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr))
        throw std::runtime_error("Could not open user startup settings");
    LSTATUS result;
    if (enabled) {
        auto value = quote(appPath().wstring());
        result = RegSetValueExW(key, L"LyrionPlayerTray", 0, REG_SZ,
                               reinterpret_cast<const BYTE *>(value.c_str()), (DWORD)((value.size()+1)*sizeof(wchar_t)));
    } else { result = RegDeleteValueW(key, L"LyrionPlayerTray"); if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS; }
    RegCloseKey(key);
    if (result != ERROR_SUCCESS) throw std::runtime_error("Could not update user startup settings");
}
}
