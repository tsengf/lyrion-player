// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform.hpp"
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

static void wait(tray::Child &child) {
    auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(child.alive()&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if(child.alive())throw std::runtime_error("Managed player did not stop");
    if(child.exitCode())throw std::runtime_error("Managed player exited unsuccessfully");
}
int main(int argc,char **argv) {
#ifndef _WIN32
    signal(SIGPIPE,SIG_IGN);
#endif
    try {
        if(argc!=2)throw std::runtime_error("Missing silent backend path");
        std::vector<std::string> args={"--managed","--server","127.0.0.1:1","--name","Process lifecycle"};
        auto a=tray::platform::spawn(tray::fs::u8path(argv[1]),args,{});
        auto b=tray::platform::spawn(tray::fs::u8path(argv[1]),args,{});
        a->stop();wait(*a);
        if(!b->alive())throw std::runtime_error("Stopping first child stopped second");
        b->stop();wait(*b);
        auto events=a->read();
        if(events.find("stopped")==std::string::npos)throw std::runtime_error("Status pipe was not inherited");
        a=tray::platform::spawn(tray::fs::u8path(argv[1]),args,{});
        a->kill();if(a->alive())throw std::runtime_error("Kill left player alive");
        std::cout<<"Native child pipes, independent stop and force termination passed\n";
    } catch(const std::exception &e){std::cerr<<e.what()<<"\n";return 1;}
}
