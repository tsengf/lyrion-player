// SPDX-License-Identifier: GPL-3.0-or-later
#include "core.hpp"
#include <fstream>
#include <iostream>
#include <functional>

namespace {
tray::fs::path root;
struct FakeState { bool running = true, stopped = false, killed = false; int code = 0; std::string output; };
std::vector<std::shared_ptr<FakeState>> children;
std::vector<std::vector<std::string>> arguments;
class FakeChild : public tray::Child {
    std::shared_ptr<FakeState> state;
public:
    explicit FakeChild(std::shared_ptr<FakeState> s) : state(std::move(s)) {}
    bool alive() override { return state->running; }
    int exitCode() override { return state->code; }
    std::string read() override { auto value=state->output;state->output.clear();return value; }
    void stop() override { state->stopped=true;state->running=false; }
    void kill() override { state->killed=true;state->running=false; }
};
void require(bool value, const char *message) { if(!value)throw std::runtime_error(message); }
void fails(const std::function<void()> &fn) { bool caught=false;try{fn();}catch(...){caught=true;}require(caught,"Expected validation failure"); }
}
namespace tray::platform {
fs::path configDir(){return root/"config";}
fs::path logDir(){return root/"logs";}
fs::path backend(){return root/"player";}
void replaceFile(const fs::path &a,const fs::path &b){if(fs::exists(b))fs::remove(b);fs::rename(a,b);}
void openFile(const fs::path &){}
void openURL(const std::string &){}
std::string loginStatus(){return "off";}
void setLogin(bool){}
std::unique_ptr<Child> spawn(const fs::path &,const std::vector<std::string> &args,const fs::path &){
    auto child=std::make_shared<FakeState>();
    arguments.push_back(args);children.push_back(child);
    return std::make_unique<FakeChild>(child);
}
}
int main() {
    root=tray::fs::temp_directory_path()/("lyrion-core-test-"+tray::newId());
    try {
        tray::Manager manager;
        auto a=manager.draft(),b=manager.draft();a.name="Music & Unicode 🎵";a.host="192.168.1.9";
        require(a.id!=b.id&&a.mac!=b.mac,"Generated identities collide");
        manager.save(a);manager.save(b);
        auto decoded=tray::decodeConfig(tray::encodeConfig(manager.instances));
        require(decoded.size()==2&&decoded[0].name==a.name,"Configuration did not round-trip");
        b.mac=a.mac;fails([&]{manager.save(b);});b=decoded[1];
        auto invalid=a;invalid.host="http://192.168.1.9";fails([&]{tray::validate(invalid);});
        invalid=a;invalid.port=70000;fails([&]{tray::validate(invalid);});
        invalid=a;invalid.mac="01:00:00:00:00:01";fails([&]{tray::validate(invalid);});
        fails([]{tray::parsePort("4294970779");});
        fails([]{tray::parsePort("-1");});
        require(manager.serverAddress(a.id)=="192.168.1.9:3483","Manual server address missing");
        require(manager.serverAddress(b.id).empty(),"Automatic server appeared before discovery");
        require(manager.automaticServer(b.id).host.empty(),"Automatic preview invented a server");
        manager.servers.push_back({"First server","192.168.1.99","first",3483,9001});
        require(manager.automaticServer(b.id).host=="192.168.1.99","Automatic preview ignored discovered server");
        manager.startAll();
        require(children.size()==2,"Did not start both instances");
        require(arguments[0][0]=="--managed","Backend is not owned by tray");
        children[0]->output="{\"version\":1,\"type\":\"connection\",\"state\":\"connected\",\"host\":\"192.168.1.9\",\"port\":3483}\n";
        manager.tick();
        require(manager.status(a.id)=="Connected","Connection event not reflected in UI model");
        require(manager.url(a.id)=="http://192.168.1.9:9000","Incorrect browser URL");
        children[1]->output="{\"version\":1,\"type\":\"connection\",\"state\":\"connected\",\"host\":\"192.168.1.20\",\"port\":3483}\n";
        manager.tick();require(manager.serverAddress(b.id)=="192.168.1.20:3483","Discovered IP and port missing");
        require(manager.automaticServer(b.id).host=="192.168.1.20","Automatic preview ignored active player endpoint");
        require(manager.automaticServer(a.id).host=="192.168.1.99","Switching manual player to Automatic reused its fixed address");
        require(manager.find(b.id)->host.empty(),"Automatic preview pinned discovery to a fixed server");
        children[1]->output="{\"version\":1,\"type\":\"connection\",\"state\":\"connected\",\"host\":\"192.168.1.21\",\"port\":3484}\n";
        manager.tick();require(manager.serverAddress(b.id)=="192.168.1.21:3484","Changed discovered IP and port missing");
        children[1]->output="{\"version\":1,\"type\":\"connection\",\"state\":\"discovering\",\"host\":\"\",\"port\":3483}\n";
        manager.tick();require(manager.serverAddress(b.id).empty(),"Rediscovery retained a stale IP");
        children[0]->output="{\"version\":1,\"type\":\"audio\",\"state\":\"unavailable\"}\n";
        manager.tick();require(manager.status(a.id).find("audio unavailable")!=std::string::npos,"Audio state missing");
        manager.stop(a.id);manager.tick();
        require(children[0]->stopped&&children[1]->running,"Stopping one affected the other");
        b.name="Updated";manager.save(b);manager.tick();
        require(children.size()==3&&!children[1]->running&&children[2]->running,"Editing did not restart only that instance");
        require(children[0]->stopped,"Editing restarted a stopped instance");
        children[2]->running=false;children[2]->code=1;manager.tick();
        require(manager.status(b.id)=="Restarting","Unexpected exit was not retried");
        manager.runtime[b.id].retry=std::chrono::steady_clock::now();manager.tick();
        require(children.back()->running,"Retry did not launch a new process");
        // Exhausting the restart budget leaves only that instance in Error.
        for(int failure=0;failure<3;++failure){
            children.back()->running=false;manager.tick();
            manager.runtime[b.id].retry=std::chrono::steady_clock::now();manager.tick();
        }
        require(manager.status(b.id).find("Error")==0&&!manager.runtime[b.id].desired,"Repeated crashes were not bounded");
        manager.start(b.id);
        manager.quit();manager.tick();
        require(manager.stopped()&&children.back()->stopped,"Quit left a child running");

        {
            // Existing incomplete migration drafts must remain stopped after removal of the importer.
            auto path=root/"blocked";
            tray::Manager oldDraft(path);auto pending=oldDraft.draft();
            pending.importPending=true;pending.autoStart=false;oldDraft.save(pending);
            tray::Manager preserved(path);preserved.start(pending.id);
            require(preserved.stopped()&&preserved.status(pending.id).find("Error")==0,"Old blocked draft started a duplicate player");
        }
        {
            auto bad=root/"invalid";tray::fs::create_directories(bad);
            std::ofstream(bad/"config.json")<<"invalid";
            tray::Manager preserved(bad);require(!preserved.error.empty(),"Corrupt configuration was silently reset");
            fails([&]{preserved.save(preserved.draft());});
            std::ifstream file(bad/"config.json");std::string value;file>>value;require(value=="invalid","Corrupt configuration was overwritten");
        }
        tray::fs::remove_all(root);
        std::cout<<"Tray configuration, lifecycle and compatibility tests passed\n";
        return 0;
    }catch(const std::exception &e){
        tray::fs::remove_all(root);std::cerr<<e.what()<<"\n";return 1;
    }
}
