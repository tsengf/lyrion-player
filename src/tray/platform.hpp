// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <stdexcept>

namespace tray {
namespace fs = std::filesystem;
class Child {
public:
    virtual ~Child() = default;
    virtual bool alive() = 0;
    virtual int exitCode() = 0;
    virtual std::string read() = 0;
    virtual void stop() = 0;
    virtual void kill() = 0;
};
namespace platform {
fs::path configDir();
fs::path logDir();
fs::path backend();
std::unique_ptr<Child> spawn(const fs::path &, const std::vector<std::string> &, const fs::path &log);
void replaceFile(const fs::path &from, const fs::path &to);
void openFile(const fs::path &);
void openURL(const std::string &);
// Return "off", "on", or "approval".
std::string loginStatus();
void setLogin(bool enabled);
}
}
