// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "platform.hpp"
#include "json.hpp"
#include <atomic>
#include <chrono>
#include <deque>
#include <future>
#include <map>

namespace tray {
struct Instance {
    std::string id, name, host, mac, webURL;
    unsigned port = 3483, webPort = 9000;
    bool autoStart = true, importPending = false;
};
struct Server { std::string name, host, uuid; unsigned port = 3483, webPort = 9000; };
struct Runtime {
    std::unique_ptr<Child> child;
    std::string state = "Stopped", audio, host, buffer, error;
    unsigned port = 0;
    bool desired = false, stopping = false, editing = false;
    std::chrono::steady_clock::time_point deadline{}, retry{};
    std::deque<std::chrono::steady_clock::time_point> failures;
};
std::string newId();
std::string newMac();
unsigned parsePort(const std::string &);
void validate(const Instance &);
std::string encodeConfig(const std::vector<Instance> &);
std::vector<Instance> decodeConfig(const std::string &);
class Manager {
    fs::path directory;
    std::future<std::vector<Server>> scanTask;
    std::shared_ptr<std::atomic_bool> scanCancel = std::make_shared<std::atomic_bool>(false);
    void launch(const Instance &, Runtime &);
    void persist(const std::vector<Instance> &);
public:
    std::vector<Instance> instances;
    std::map<std::string, Runtime> runtime;
    std::vector<Server> servers;
    std::string error;
    bool scanning = false, quitting = false;
    explicit Manager(fs::path directory = platform::configDir());
    ~Manager();
    Instance draft() const;
    const Instance *find(const std::string &) const;
    void save(Instance);
    void remove(const std::string &);
    void start(const std::string &);
    void stop(const std::string &);
    void startAll();
    void stopAll();
    void startConfigured();
    void tick();
    void quit();
    bool stopped() const;
    fs::path logPath(const std::string &) const;
    std::string status(const std::string &) const;
    std::string serverAddress(const std::string &) const;
    std::string url(const std::string &) const;
    void scan();
    void importLegacy(Legacy);
};
}
