// SPDX-License-Identifier: GPL-3.0-or-later
#include "core.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <random>
#include <regex>
#include <sstream>
#include <cctype>
extern "C" {
#include "discovery.h"
}
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/stat.h>
#endif
namespace tray {
using json = nlohmann::json;
using Clock = std::chrono::steady_clock;
static std::string randomHex(unsigned bytes) {
    std::random_device rng;
    std::ostringstream out;
    for (unsigned i = 0; i < bytes; ++i) out << std::hex << std::setw(2) << std::setfill('0') << (rng() & 255);
    return out.str();
}
std::string newId() { return randomHex(16); }
std::string newMac() {
    auto hex = randomHex(5);
    std::string mac = "02";
    for (unsigned i = 0; i < hex.size(); i += 2) mac += ":" + hex.substr(i, 2);
    return mac;
}
unsigned parsePort(const std::string &text) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("Enter a numeric port");
    auto value = std::stoull(text);
    if (!value || value > 65535) throw std::runtime_error("Ports must be 1–65535");
    return (unsigned)value;
}
void validate(const Instance &i) {
    if (!std::regex_match(i.id, std::regex("[a-zA-Z0-9_-]{1,64}"))) throw std::runtime_error("Invalid instance ID");
    if (i.name.empty() || i.name.size() > 64 || i.name.find_first_of("\r\n") != std::string::npos)
        throw std::runtime_error("Player name must be 1–64 UTF-8 bytes without line breaks");
    if (!std::regex_match(i.mac, std::regex("([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}")) ||
        i.mac == "00:00:00:00:00:00" || (std::stoul(i.mac.substr(0, 2), nullptr, 16) & 1))
        throw std::runtime_error("Player identifier must be a nonzero unicast MAC-style address");
    if (!i.port || i.port > 65535 || !i.webPort || i.webPort > 65535) throw std::runtime_error("Ports must be 1–65535");
    if (i.host.find_first_of("/\\: \t\r\n") != std::string::npos)
        throw std::runtime_error("Enter a hostname or IPv4 address, without a URL or port");
    if (!i.webURL.empty() && !std::regex_match(i.webURL, std::regex("https?://[^ /\\r\\n]+([/?#].*)?")))
        throw std::runtime_error("Web URL must start with http:// or https://");
}
std::string encodeConfig(const std::vector<Instance> &instances) {
    json result = {{"schemaVersion", 1}, {"instances", json::array()}};
    for (const auto &i : instances) {
        validate(i);
        result["instances"].push_back({{"id", i.id}, {"name", i.name}, {"host", i.host},
            {"mac", i.mac}, {"port", i.port}, {"webPort", i.webPort}, {"webURL", i.webURL}, {"autoStart", i.autoStart}, {"importPending", i.importPending}});
    }
    return result.dump(2);
}
std::vector<Instance> decodeConfig(const std::string &text) {
    auto root = json::parse(text);
    if (root.at("schemaVersion") != 1 || !root.at("instances").is_array())
        throw std::runtime_error("Unsupported configuration format");
    std::vector<Instance> result;
    for (const auto &value : root["instances"]) {
        Instance i;
        i.id = value.at("id"); i.name = value.at("name"); i.host = value.at("host"); i.mac = value.at("mac");
        std::transform(i.mac.begin(), i.mac.end(), i.mac.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        i.port = parsePort(value.at("port").dump()); i.webPort = parsePort(value.value("webPort", json(9000)).dump());
        i.webURL = value.value("webURL", ""); i.autoStart = value.value("autoStart", true); i.importPending = value.value("importPending", false);
        validate(i);
        for (const auto &previous : result)
            if (previous.id == i.id || previous.mac == i.mac) throw std::runtime_error("Duplicate instance or player identifier");
        result.push_back(i);
    }
    return result;
}
Manager::Manager(fs::path dir) : directory(std::move(dir)) {
    try {
        auto path = directory / "config.json";
        if (fs::exists(path)) {
            std::ifstream in(path);
            if (!in) throw std::runtime_error("Cannot read configuration");
            instances = decodeConfig(std::string(std::istreambuf_iterator<char>(in), {}));
        }
    } catch (const std::exception &e) { error = std::string("Configuration was preserved but could not be loaded: ") + e.what(); }
}
Manager::~Manager() {
    *scanCancel = true;
    for (auto &[id, r] : runtime) if (r.child) { r.child->stop(); r.child->kill(); }
}
Instance Manager::draft() const {
    Instance i; i.id = newId(); i.name = "Lyrion Player"; i.mac = newMac();
    while (std::any_of(instances.begin(), instances.end(), [&](const auto &old) { return old.mac == i.mac; })) i.mac = newMac();
    return i;
}
const Instance *Manager::find(const std::string &id) const {
    auto it = std::find_if(instances.begin(), instances.end(), [&](const auto &i) { return i.id == id; });
    return it == instances.end() ? nullptr : &*it;
}
void Manager::persist(const std::vector<Instance> &values) {
    if (!error.empty()) throw std::runtime_error("Repair or move the existing configuration before saving: " + error);
    auto text = encodeConfig(values);
    fs::create_directories(directory);
#ifndef _WIN32
    chmod(directory.c_str(), 0700);
#endif
    auto temp = directory / ("config-" + newId() + ".tmp");
    { std::ofstream out(temp, std::ios::binary); out << text; out.flush(); if (!out) throw std::runtime_error("Could not save configuration"); }
#ifndef _WIN32
    chmod(temp.c_str(), 0600);
#endif
    try { platform::replaceFile(temp, directory / "config.json"); }
    catch (...) { fs::remove(temp); throw; }
}
void Manager::save(Instance i) {
    std::transform(i.mac.begin(), i.mac.end(), i.mac.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    validate(i);
    auto next = instances;
    for (const auto &old : next) if (old.id != i.id && old.mac == i.mac) throw std::runtime_error("Another instance already uses this player identifier");
    auto it = std::find_if(next.begin(), next.end(), [&](const auto &old) { return old.id == i.id; });
    if (it == next.end()) next.push_back(i); else *it = i;
    persist(next);
    instances = std::move(next);
    auto &r = runtime[i.id];
    if (r.child) { r.editing = r.desired; stop(i.id); r.desired = r.editing; }
}
void Manager::remove(const std::string &id) {
    auto next = instances;
    next.erase(std::remove_if(next.begin(), next.end(), [&](const auto &i) { return i.id == id; }), next.end());
    persist(next);
    stop(id);
    runtime[id].desired = false;
    instances = std::move(next);
}
fs::path Manager::logPath(const std::string &id) const { return platform::logDir() / ("player-" + id + ".log"); }
void Manager::launch(const Instance &i, Runtime &r) {
    std::vector<std::string> args = {"--managed", "--name", i.name, "--mac", i.mac,
        "--debug", "output=info"};
    if (!i.host.empty()) { args.push_back("--server"); args.push_back(i.host + ":" + std::to_string(i.port)); }
    try {
        fs::create_directories(platform::logDir());
        r.child = platform::spawn(platform::backend(), args, logPath(i.id));
        r.state = "Starting"; r.error.clear(); r.audio.clear(); r.buffer.clear(); r.host.clear(); r.port = 0;
    } catch (const std::exception &e) { r.desired = false; r.state = "Error"; r.error = e.what(); }
}
void Manager::start(const std::string &id) {
    auto i = find(id); if (!i || quitting) return;
    auto &r = runtime[id];
    if (r.child) return;
    if (i->importPending) {
        r.desired = false; r.state = "Error";
        r.error = "This player has an incomplete migration. Remove the old startup entry, then remove and recreate this player.";
        return;
    }
    r.desired = true; r.stopping = false; r.failures.clear();
    launch(*i, r);
}
void Manager::stop(const std::string &id) {
    auto &r = runtime[id];
    r.desired = false;
    if (r.child) { r.child->stop(); r.stopping = true; r.deadline = Clock::now() + std::chrono::seconds(5); r.state = "Stopping"; }
    else r.state = "Stopped";
}
void Manager::startAll() { for (auto &i : instances) start(i.id); }
void Manager::stopAll() { for (auto &[id, r] : runtime) stop(id); }
void Manager::startConfigured() { for (const auto &i : instances) if (i.autoStart) start(i.id); }
void Manager::quit() { quitting = true; *scanCancel = true; stopAll(); }
bool Manager::stopped() const {
    for (const auto &[id, r] : runtime) if (r.child) return false;
    return true;
}
std::string Manager::status(const std::string &id) const {
    auto it = runtime.find(id);
    if (it == runtime.end()) return "Stopped";
    auto text = it->second.state;
    if (it->second.audio == "unavailable") text += " — audio unavailable";
    if (!it->second.error.empty()) text += " — " + it->second.error;
    return text;
}
std::string Manager::serverAddress(const std::string &id) const {
    auto i = find(id); if (!i) return {};
    if (!i->host.empty()) return i->host + ":" + std::to_string(i->port);
    auto r = runtime.find(id);
    if (r == runtime.end() || r->second.host.empty() || !r->second.port) return {};
    return r->second.host + ":" + std::to_string(r->second.port);
}
Server Manager::automaticServer(const std::string &id) const {
    auto i = find(id);
    auto r = runtime.find(id);
    if (i && i->host.empty() && r != runtime.end() && !r->second.host.empty()) {
        for (const auto &server : servers)
            if (server.host == r->second.host && server.port == r->second.port) return server;
        Server server; server.host = r->second.host; server.port = r->second.port;
        server.webPort = i->webPort; return server;
    }
    return servers.empty() ? Server{} : servers.front();
}
std::string Manager::url(const std::string &id) const {
    auto i = find(id); if (!i) return {};
    if (!i->webURL.empty()) return i->webURL;
    auto r = runtime.find(id);
    auto host = r != runtime.end() && !r->second.host.empty() ? r->second.host : i->host;
    if (host.empty()) return {};
    unsigned webPort = i->webPort;
    for (const auto &s : servers) if (s.host == host) webPort = s.webPort;
    return "http://" + host + ":" + std::to_string(webPort);
}
void Manager::tick() {
    auto now = Clock::now();
    for (auto &[id, r] : runtime) {
        if (r.child) {
            r.buffer += r.child->read();
            if (r.buffer.size() > 65536) { r.child->kill(); r.buffer.clear(); r.error = "Invalid backend status stream"; }
            size_t end;
            while ((end = r.buffer.find('\n')) != std::string::npos) {
                auto line = r.buffer.substr(0, end); r.buffer.erase(0, end + 1);
                try {
                    auto event = json::parse(line);
                    if (event.at("version") != 1 || r.stopping) continue;
                    if (event.at("type") == "audio") r.audio = event.at("state");
                    if (event.at("type") == "connection") {
                        std::string state = event.at("state");
                        static const std::map<std::string, std::string> names = {{"discovering","Discovering"},
                            {"connecting","Connecting"}, {"connected","Connected"}, {"disconnected","Connecting"}, {"stopped","Stopped"}};
                        if (names.count(state)) r.state = names.at(state);
                        if (state == "discovering") { r.host.clear(); r.port = 0; }
                        if (event.contains("host") && !event.at("host").get<std::string>().empty()) {
                            auto port = parsePort(event.at("port").dump());
                            r.host = event.at("host"); r.port = port;
                        }
                    }
                } catch (...) { /* Diagnostics remain in the instance log. */ }
            }
            if (r.stopping && now >= r.deadline) r.child->kill();
            if (!r.child->alive()) {
                auto code = r.child->exitCode();
                r.child.reset();
                if (r.stopping) {
                    r.stopping = false; r.state = "Stopped";
                    if (r.desired && !quitting) if (auto i = find(id)) launch(*i, r);
                } else if (r.desired && !quitting) {
                    while (!r.failures.empty() && now - r.failures.front() > std::chrono::minutes(1)) r.failures.pop_front();
                    r.failures.push_back(now);
                    if (r.failures.size() > 3) {
                        r.state = "Error"; r.desired = false; r.error = "Player exited repeatedly (code " + std::to_string(code) + "). View log and retry.";
                    } else {
                        r.state = "Restarting"; r.retry = now + std::chrono::seconds(1 << (r.failures.size() - 1));
                    }
                }
            }
        } else if (r.desired && !quitting && now >= r.retry) {
            if (auto i = find(id)) launch(*i, r); else r.desired = false;
        }
    }
    if (scanning && scanTask.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try { servers = scanTask.get(); } catch (...) { servers.clear(); }
        scanning = false;
        nextScan = now + std::chrono::seconds(15);
    }
    if (automaticScanning && !scanning && !quitting && now >= nextScan) scan();
}
struct ScanContext { std::vector<Server> servers; std::shared_ptr<std::atomic_bool> cancel; };
static void scanResult(const player_discovered_server *found, void *opaque) {
    auto &ctx = *static_cast<ScanContext *>(opaque);
    struct in_addr address; address.s_addr = found->address;
    Server s{found->name, inet_ntoa(address), found->uuid, found->port, found->web_port};
    if (s.name.empty()) s.name = s.host;
    for (auto &old : ctx.servers)
        if ((!s.uuid.empty() && s.uuid == old.uuid) || (s.host == old.host && s.port == old.port)) return;
    ctx.servers.push_back(s);
}
void Manager::scan() {
    if (scanning || quitting) return;
    automaticScanning = true;
    scanning = true; *scanCancel = false;
    auto cancel = scanCancel;
    scanTask = std::async(std::launch::async, [cancel] {
        ScanContext ctx{{}, cancel};
        player_discovery_scan(3483, 3000, scanResult, &ctx, [](const char *) {},
            [](void *p) { return static_cast<ScanContext *>(p)->cancel->load() ? 1 : 0; });
        return ctx.servers;
    });
}
}
