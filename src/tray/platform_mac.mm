// SPDX-License-Identifier: GPL-3.0-or-later
#import <AppKit/AppKit.h>
#import <ServiceManagement/ServiceManagement.h>
#include "platform.hpp"
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <crt_externs.h>
#include <thread>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <algorithm>
#include <ctime>

namespace tray::platform {
static std::string utf(NSString *s) { return s ? std::string(s.UTF8String) : ""; }
static NSString *ns(const std::string &s) { return [NSString stringWithUTF8String:s.c_str()]; }
fs::path configDir() { return fs::u8path(utf(NSHomeDirectory())) / "Library/Application Support/lyrion-player"; }
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
std::vector<Legacy> legacyServices() {
    std::vector<Legacy> result;
    auto dir = fs::u8path(utf(NSHomeDirectory())) / "Library/LaunchAgents";
    if (!fs::exists(dir)) return result;
    for (const auto &entry : fs::directory_iterator(dir)) {
        if (!fs::is_regular_file(entry.symlink_status()) || entry.path().extension() != ".plist") continue;
        auto stem = entry.path().stem().u8string();
        if (stem != "org.lyrion.player" && stem.rfind("org.lyrion.player.", 0)) continue;
        NSData *data = [NSData dataWithContentsOfFile:ns(entry.path().u8string())];
        if (!data) continue;
        id value = [NSPropertyListSerialization propertyListWithData:data options:NSPropertyListImmutable format:nil error:nil];
        if (![value isKindOfClass:NSDictionary.class]) continue;
        NSArray *arguments = value[@"ProgramArguments"];
        if (![arguments isKindOfClass:NSArray.class] || !arguments.count) continue;
        std::vector<std::string> args;
        for (id a in arguments) if ([a isKindOfClass:NSString.class]) args.push_back(utf(a));
        if (args.size() != arguments.count || ![value[@"Label"] isKindOfClass:NSString.class]) continue;
        if (args.empty() || fs::u8path(args[0]).filename() != "player" || utf(value[@"Label"]) != stem) continue;
        auto legacy = parseLegacy(entry.path().u8string(), stem, args);
        legacy.definition.assign((const char *)data.bytes, data.length);
        result.push_back(std::move(legacy));
    }
    return result;
}
static int runCode(const std::vector<std::string> &args) {
    auto child = spawn("/bin/launchctl", args, {});
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (child->alive() && std::chrono::steady_clock::now() < end) { child->read(); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
    if (child->alive()) { child->kill(); throw std::runtime_error("launchctl timed out"); }
    return child->exitCode();
}
void removeLegacy(const Legacy &legacy) {
    auto current = legacyServices();
    auto found = std::find_if(current.begin(), current.end(), [&](const auto &i) { return i.token == legacy.token && i.definition == legacy.definition; });
    if (found == current.end()) throw std::runtime_error("Startup entry changed; refresh the import list");
    auto backups = configDir() / "import-backups";
    fs::create_directories(backups);
    auto backup = backups / (legacy.label + "-" + std::to_string(time(nullptr)) + ".plist");
    NSData *data = [NSData dataWithBytes:legacy.definition.data() length:legacy.definition.size()];
    if (![data writeToFile:ns(backup.u8string()) atomically:YES]) throw std::runtime_error("Could not back up startup entry");
    chmod(backup.c_str(), 0600);
    auto target = "gui/" + std::to_string(getuid()) + "/" + legacy.label;
    if (runCode({"bootout", target}) && !runCode({"print", target}))
        throw std::runtime_error("Could not stop existing startup entry");
    if (!fs::remove(fs::u8path(legacy.token))) throw std::runtime_error("Could not remove startup entry");
}
}
