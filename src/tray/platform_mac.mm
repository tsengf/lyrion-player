// SPDX-License-Identifier: GPL-3.0-or-later
#import <AppKit/AppKit.h>
#import <ServiceManagement/ServiceManagement.h>
#include "platform.hpp"
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
#include <crt_externs.h>
#include <cerrno>
#include <cstring>

namespace tray::platform {
std::unique_ptr<AppEnvironment> initializeApplication() { return nullptr; }
static std::string utf(NSString *s) { return s ? std::string(s.UTF8String) : ""; }
static NSString *ns(const std::string &s) { return [NSString stringWithUTF8String:s.c_str()]; }
fs::path configDir() { return fs::u8path(utf(NSHomeDirectory())) / "Library/Application Support/lyrion-player"; }
class MacAppLock final : public AppLock {
public:
    int fd;
    explicit MacAppLock(int descriptor) : fd(descriptor) {}
    ~MacAppLock() override { close(fd); }
};
std::unique_ptr<AppLock> lockApplication() {
    fs::create_directories(configDir());
    int fd = open((configDir() / "tray.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (fd < 0) throw std::runtime_error("Cannot open the tray application lock");
    if (flock(fd, LOCK_EX | LOCK_NB)) {
        int error = errno; close(fd);
        if (error == EWOULDBLOCK || error == EAGAIN) return nullptr;
        throw std::runtime_error("Cannot lock the tray application");
    }
    return std::make_unique<MacAppLock>(fd);
}
fs::path logDir() { return fs::u8path(utf(NSHomeDirectory())) / "Library/Logs/lyrion-player"; }
fs::path backend() {
    auto path = fs::u8path(utf(NSBundle.mainBundle.executablePath)).parent_path() / "player";
    if (!fs::exists(path)) throw std::runtime_error("The bundled player executable is missing");
    return path;
}
class PosixChild final : public Child {
    pid_t pid = -1;
    int input = -1, output = -1, code = -1;
public:
    PosixChild(const fs::path &binary, const std::vector<std::string> &args, const fs::path &log) {
        int in[2] = {-1,-1}, out[2] = {-1,-1}, err = -1;
        auto cleanup = [&] { for (int fd : {in[0],in[1],out[0],out[1],err}) if (fd >= 0) close(fd); };
        if (pipe(in) || pipe(out)) { cleanup(); throw std::runtime_error("Could not create player pipes"); }
        err = open(log.empty() ? "/dev/null" : log.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
        if (err < 0) { cleanup(); throw std::runtime_error("Could not open instance log"); }
        for (int fd : {in[0],in[1],out[0],out[1],err}) fcntl(fd, F_SETFD, FD_CLOEXEC);
        std::string executable = binary.u8string();
        std::vector<char *> argv{executable.data()};
        for (const auto &a : args) argv.push_back(const_cast<char *>(a.c_str()));
        argv.push_back(nullptr);
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, in[0], STDIN_FILENO);
        posix_spawn_file_actions_adddup2(&actions, out[1], STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, err, STDERR_FILENO);
        for (int fd : {in[0],in[1],out[0],out[1],err}) posix_spawn_file_actions_addclose(&actions, fd);
        int result = posix_spawn(&pid, executable.c_str(), &actions, nullptr, argv.data(), *_NSGetEnviron());
        posix_spawn_file_actions_destroy(&actions);
        if (result) { pid = -1; cleanup(); throw std::runtime_error(std::string("Could not start player: ") + strerror(result)); }
        input = in[1]; output = out[0];
        close(in[0]); close(out[1]); close(err);
        fcntl(output, F_SETFL, O_NONBLOCK);
    }
    ~PosixChild() override {
        if (alive()) kill();
        if (input >= 0) close(input);
        if (output >= 0) close(output);
    }
    bool alive() override {
        if (pid < 0) return false;
        int status;
        auto result = waitpid(pid, &status, WNOHANG);
        if (!result || (result < 0 && errno == EINTR)) return true;
        code = result < 0 ? -1 : WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        pid = -1;
        return false;
    }
    int exitCode() override { alive(); return code; }
    std::string read() override {
        std::string result; char bytes[4096]; ssize_t count;
        while ((count = ::read(output, bytes, sizeof(bytes))) > 0) {
            result.append(bytes, (size_t)count);
            if (result.size() > 4 * 1024 * 1024) break;
        }
        return result;
    }
    void stop() override {
        if (input >= 0) { write(input, "stop\n", 5); close(input); input = -1; }
    }
    void kill() override {
        if (!alive()) return;
        ::kill(pid, SIGKILL);
        int status; while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        pid = -1; code = 137;
    }
};
std::unique_ptr<Child> spawn(const fs::path &binary, const std::vector<std::string> &args, const fs::path &log) {
    return std::make_unique<PosixChild>(binary, args, log);
}
void replaceFile(const fs::path &from, const fs::path &to) {
    if (rename(from.c_str(), to.c_str())) throw std::runtime_error("Could not replace configuration");
}
void openFile(const fs::path &p) { [NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:ns(p.u8string())]]; }
void openURL(const std::string &url) { [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:ns(url)]]; }
std::string loginStatus() {
    switch (SMAppService.mainAppService.status) {
        case SMAppServiceStatusEnabled: return "on";
        case SMAppServiceStatusRequiresApproval: return "approval";
        default: return "off";
    }
}
void setLogin(bool enabled) {
    NSError *error = nil;
    BOOL ok = enabled ? [SMAppService.mainAppService registerAndReturnError:&error]
                      : [SMAppService.mainAppService unregisterAndReturnError:&error];
    if (!ok) throw std::runtime_error(utf(error.localizedDescription));
}
}
