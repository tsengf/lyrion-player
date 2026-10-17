// SPDX-License-Identifier: GPL-3.0-or-later
#import <AppKit/AppKit.h>
#import <ServiceManagement/ServiceManagement.h>
#include "core.hpp"
#include <csignal>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

static NSString *S(const std::string &s) { return [NSString stringWithUTF8String:s.c_str()] ?: @"Invalid UTF-8"; }
static std::string U(NSString *s) { return s ? std::string(s.UTF8String) : ""; }

@interface PlayerApp : NSObject <NSApplicationDelegate, NSTableViewDataSource, NSTableViewDelegate, NSWindowDelegate, NSMenuDelegate> {
    std::unique_ptr<tray::Manager> manager;
    NSStatusItem *statusItem;
    NSWindow *settings, *editor;
    NSTableView *table;
    NSButton *login, *autoStart;
    NSTextField *summary, *nameField, *hostField, *portField, *macField, *webField;
    NSPopUpButton *serverChoice;
    tray::Instance editing;
    std::vector<tray::Server> editorServers;
    std::string tableSnapshot;
    int lockFD;
    bool lastScanning, smoke, menuOpen;
}
- (void)openSettings:(id)sender;
@end

@implementation PlayerApp
- (void)alert:(NSString *)message {
    NSAlert *a = [NSAlert new]; a.messageText = @"Lyrion Player"; a.informativeText = message;
    [a addButtonWithTitle:@"OK"]; [a runModal];
}
- (BOOL)confirm:(NSString *)message {
    NSAlert *a = [NSAlert new]; a.messageText = @"Lyrion Player"; a.informativeText = message;
    [a addButtonWithTitle:@"Continue"]; [a addButtonWithTitle:@"Cancel"];
    return [a runModal] == NSAlertFirstButtonReturn;
}
- (NSString *)selected {
    NSInteger row = table.selectedRow;
    return row >= 0 && row < (NSInteger)manager->instances.size() ? S(manager->instances[row].id) : nil;
}
- (NSButton *)button:(NSString *)title action:(SEL)action frame:(NSRect)frame in:(NSView *)view {
    NSButton *b = [NSButton buttonWithTitle:title target:self action:action]; b.frame = frame;
    [view addSubview:b]; return b;
}
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    try {
    smoke = [NSProcessInfo.processInfo.arguments containsObject:@"--smoke-test"];
    if (!smoke) {
        auto apps = [NSRunningApplication runningApplicationsWithBundleIdentifier:NSBundle.mainBundle.bundleIdentifier];
        for (NSRunningApplication *app in apps) {
            if (app.processIdentifier != getpid()) {
                [app activateWithOptions:0];
                [NSApp terminate:nil]; return;
            }
        }
        auto directory = tray::platform::configDir();
        tray::fs::create_directories(directory);
        lockFD = open((directory / "tray.lock").c_str(), O_CREAT | O_RDWR, 0600);
        if (lockFD < 0 || flock(lockFD, LOCK_EX | LOCK_NB)) { [self alert:@"Another tray app is already running."]; [NSApp terminate:nil]; return; }
        fcntl(lockFD, F_SETFD, FD_CLOEXEC);
    }
    auto directory = smoke ? tray::fs::temp_directory_path() / ("lyrion-tray-smoke-" + tray::newId()) : tray::platform::configDir();
    manager = std::make_unique<tray::Manager>(directory);
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    statusItem = [NSStatusBar.systemStatusBar statusItemWithLength:NSVariableStatusItemLength];
    statusItem.button.image = [NSImage imageWithSystemSymbolName:@"hifispeaker.fill" accessibilityDescription:@"Lyrion Player"];
    [statusItem.button.image setTemplate:YES];
    [self buildSettings];
    [self rebuildMenu];
    if (smoke) {
        [self openSettings:nil];
        [self showEditor:manager->draft()];
        if (!settings || !table || !login) exit(1);
        if (!nameField || !macField || !serverChoice || serverChoice.numberOfItems < 2) exit(1);
        [self cancelEditor:nil];
        tray::fs::remove_all(directory);
        [NSApp terminate:nil]; return;
    }
    manager->startConfigured();
    manager->scan();
    NSTimer *timer = [NSTimer timerWithTimeInterval:0.25 target:self selector:@selector(tick:) userInfo:nil repeats:YES];
    [NSRunLoop.mainRunLoop addTimer:timer forMode:NSRunLoopCommonModes];
    [NSRunLoop.mainRunLoop addTimer:timer forMode:NSModalPanelRunLoopMode];
    if (manager->instances.empty() || !manager->error.empty()) [self openSettings:nil];
    if (!manager->error.empty()) [self alert:S(manager->error)];
    } catch (const std::exception &e) { [self alert:S(e.what())]; [NSApp terminate:nil]; }
}
- (BOOL)applicationShouldHandleReopen:(NSApplication *)app hasVisibleWindows:(BOOL)visible { [self openSettings:nil]; return YES; }
- (void)buildSettings {
    settings = [[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,820,430)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
    settings.title = @"Lyrion Player — Instances"; settings.releasedWhenClosed = NO;
    [settings center];
    NSView *view = settings.contentView;
    NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(20,120,780,285)];
    scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable; scroll.hasVerticalScroller = YES;
    table = [[NSTableView alloc] initWithFrame:scroll.bounds];
    NSArray *keys = @[@"name", @"server", @"status"];
    NSArray *titles = @[@"Player", @"Server", @"Status"];
    for (NSUInteger i=0; i<keys.count; ++i) {
        NSTableColumn *column = [[NSTableColumn alloc] initWithIdentifier:keys[i]];
        column.title = titles[i]; column.width = i == 2 ? 300 : 220;
        [table addTableColumn:column];
    }
    table.delegate = self; table.dataSource = self; table.rowHeight = 27;
    table.target = self; table.doubleAction = @selector(editInstance:);
    scroll.documentView = table; [view addSubview:scroll];
    NSArray *labels = @[@"Add…", @"Edit…", @"Remove", @"Start / Stop", @"Import…", @"Scan servers"];
    SEL actions[] = {@selector(addInstance:), @selector(editInstance:), @selector(removeInstance:),
        @selector(toggleSelected:), @selector(importInstances:), @selector(scanServers:)};
    for (NSUInteger i=0; i<labels.count; ++i)
        [self button:labels[i] action:actions[i] frame:NSMakeRect(20+i*127,78,120,30) in:view];
    login = [NSButton checkboxWithTitle:@"Start at sign-in" target:self action:@selector(changeLogin:)];
    login.frame = NSMakeRect(20,40,170,24); [view addSubview:login];
    [self button:@"Open configuration folder" action:@selector(openConfig:) frame:NSMakeRect(200,38,210,28) in:view];
    summary = [NSTextField labelWithString:@""]; summary.frame = NSMakeRect(20,10,780,24);
    [view addSubview:summary];
}
- (NSInteger)numberOfRowsInTableView:(NSTableView *)view { return manager ? manager->instances.size() : 0; }
- (id)tableView:(NSTableView *)view objectValueForTableColumn:(NSTableColumn *)column row:(NSInteger)row {
    if (row < 0 || row >= (NSInteger)manager->instances.size()) return @"";
    const auto &i = manager->instances[row];
    if ([column.identifier isEqualToString:@"name"]) return S(i.name);
    if ([column.identifier isEqualToString:@"server"]) return i.host.empty() ? @"Automatic discovery" : S(i.host+":"+std::to_string(i.port));
    return S(manager->status(i.id));
}
- (void)openSettings:(id)sender { [settings makeKeyAndOrderFront:nil]; [NSApp activateIgnoringOtherApps:YES]; }
- (void)openConfig:(id)sender {
    try { tray::fs::create_directories(tray::platform::configDir()); tray::platform::openFile(tray::platform::configDir()); }
    catch (const std::exception &e) { [self alert:S(e.what())]; }
}
- (void)changeLogin:(id)sender {
    try {
        tray::platform::setLogin(login.state == NSControlStateValueOn);
        if (tray::platform::loginStatus() == "approval") {
            [self alert:@"Approve Lyrion Player in System Settings → General → Login Items."];
            [SMAppService openSystemSettingsLoginItems];
        }
    } catch (const std::exception &e) { [self alert:S(e.what())]; }
}
- (void)scanServers:(id)sender { manager->scan(); }
- (void)addInstance:(id)sender { [self showEditor:manager->draft()]; }
- (void)editInstance:(id)sender { if (auto i = manager->find(U([self selected]))) [self showEditor:*i]; }
- (NSTextField *)field:(NSString *)label value:(const std::string &)value y:(CGFloat)y view:(NSView *)view {
    NSTextField *title = [NSTextField labelWithString:label]; title.frame = NSMakeRect(20,y,150,25); [view addSubview:title];
    NSTextField *field = [[NSTextField alloc] initWithFrame:NSMakeRect(175,y,365,25)];
    field.stringValue = S(value); [view addSubview:field]; return field;
}
- (void)refreshServerChoices {
    NSInteger selected = serverChoice.indexOfSelectedItem;
    std::string selectedHost;
    if (selected >= 2 && selected-2 < (NSInteger)editorServers.size()) selectedHost = editorServers[selected-2].host;
    editorServers = manager->servers;
    if (selected >= 2) {
        selected = 1;
        for (size_t i=0; i<editorServers.size(); ++i) if (editorServers[i].host == selectedHost) selected = i+2;
        hostField.enabled = selected == 1; portField.enabled = selected == 1;
    }
    [serverChoice removeAllItems];
    [serverChoice addItemWithTitle:@"Automatic — first server to reply"];
    [serverChoice addItemWithTitle:@"Manual address"];
    for (auto &s : editorServers) [serverChoice addItemWithTitle:S(s.name+" ("+s.host+")")];
    [serverChoice selectItemAtIndex: selected >= 0 && selected < serverChoice.numberOfItems ? selected : 0];
}
- (void)showEditor:(tray::Instance)value {
    if (editor) { [editor close]; editor = nil; }
    editing = std::move(value);
    editor = [[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,565,390)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:NO];
    editor.title = @"Configure instance"; editor.releasedWhenClosed = NO; [editor center];
    NSView *view = editor.contentView;
    nameField = [self field:@"Player name" value:editing.name y:340 view:view];
    NSTextField *label = [NSTextField labelWithString:@"Server"]; label.frame = NSMakeRect(20,300,150,25); [view addSubview:label];
    serverChoice = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(175,300,365,28) pullsDown:NO];
    serverChoice.target = self; serverChoice.action = @selector(chooseServer:); [view addSubview:serverChoice];
    [self refreshServerChoices]; [serverChoice selectItemAtIndex:editing.host.empty() ? 0 : 1];
    hostField = [self field:@"Host / IPv4 address" value:editing.host y:260 view:view];
    portField = [self field:@"SlimProto port" value:std::to_string(editing.port) y:220 view:view];
    macField = [self field:@"Player ID (advanced)" value:editing.mac y:180 view:view];
    webField = [self field:@"Web URL (optional)" value:editing.webURL y:140 view:view];
    webField.placeholderString = @"http://server:9000";
    autoStart = [NSButton checkboxWithTitle:@"Start this instance with the app" target:nil action:nil];
    autoStart.frame = NSMakeRect(175,100,360,25); autoStart.state = editing.autoStart ? NSControlStateValueOn : NSControlStateValueOff;
    [view addSubview:autoStart];
    [self button:@"Scan servers" action:@selector(scanServers:) frame:NSMakeRect(20,35,130,30) in:view];
    [self button:@"Cancel" action:@selector(cancelEditor:) frame:NSMakeRect(315,35,100,30) in:view];
    NSButton *save = [self button:@"Save" action:@selector(saveEditor:) frame:NSMakeRect(435,35,100,30) in:view];
    save.keyEquivalent = @"\r";
    [self chooseServer:nil]; [editor makeKeyAndOrderFront:nil]; [NSApp activateIgnoringOtherApps:YES];
}
- (void)chooseServer:(id)sender {
    NSInteger choice = serverChoice.indexOfSelectedItem;
    if (choice == 0) hostField.stringValue = @"";
    if (choice >= 2 && choice-2 < (NSInteger)editorServers.size()) {
        const auto &s = editorServers[choice-2];
        hostField.stringValue = S(s.host); portField.stringValue = S(std::to_string(s.port)); editing.webPort = s.webPort;
    }
    hostField.enabled = choice == 1; portField.enabled = choice == 1;
}
- (void)cancelEditor:(id)sender { [editor close]; editor = nil; }
- (void)saveEditor:(id)sender {
    try {
        auto value = editing;
        value.name = U(nameField.stringValue); value.host = U(hostField.stringValue);
        auto port = U(portField.stringValue);
        value.port = tray::parsePort(port); value.mac = U(macField.stringValue);
        value.webURL = U(webField.stringValue); value.autoStart = autoStart.state == NSControlStateValueOn;
        manager->save(value); [self cancelEditor:nil]; [table reloadData];
    } catch (const std::exception &e) { [self alert:S(e.what())]; }
}
- (void)removeInstance:(id)sender {
    auto id = U([self selected]); auto i = manager->find(id); if (!i) return;
    if (![self confirm:S("Remove " + i->name + "? Its player will stop; logs will be retained.")]) return;
    try { manager->remove(id); [table reloadData]; } catch (const std::exception &e) { [self alert:S(e.what())]; }
}
- (void)toggleSelected:(id)sender { [self toggle:U([self selected])]; }
- (void)toggle:(const std::string &)id {
    if (manager->runtime[id].desired) manager->stop(id); else manager->start(id);
}
- (void)instanceAction:(NSMenuItem *)item {
    NSString *identity = item.representedObject;
    auto id = U(identity);
    if (item.tag == 0) [self toggle:id];
    else if (item.tag == 1) {
        auto url = manager->url(id);
        if (url.empty()) [self alert:@"Start the instance and wait for it to connect, or set a web URL in Settings."];
        else tray::platform::openURL(url);
    } else if (item.tag == 2) {
        auto path = manager->logPath(id);
        if (tray::fs::exists(path)) tray::platform::openFile(path); else [self alert:@"No log exists yet. Start the instance first."];
    } else if (auto i = manager->find(id)) [self showEditor:*i];
}
- (void)startAll:(id)sender { manager->startAll(); }
- (void)stopAll:(id)sender { manager->stopAll(); }
- (void)quit:(id)sender { [NSApp terminate:nil]; }
- (void)rebuildMenu {
    // Avoid replacing a menu while the user is interacting with it.
    if (menuOpen) return;
    NSMenu *menu = [NSMenu new];
    menu.delegate = self;
    NSMenuItem *configure = [menu addItemWithTitle:@"Instances…" action:@selector(openSettings:) keyEquivalent:@","];
    configure.target = self;
    for (const auto &i : manager->instances) {
        NSMenuItem *entry = [menu addItemWithTitle:S(i.name+" — "+manager->status(i.id)) action:nil keyEquivalent:@""];
        NSMenu *sub = [NSMenu new];
        NSArray *titles = @[manager->runtime[i.id].desired ? @"Stop" : @"Start", @"Open Lyrion", @"View Log", @"Edit…"];
        for (NSInteger action = 0; action < 4; ++action) {
            NSMenuItem *item = [sub addItemWithTitle:titles[action] action:@selector(instanceAction:) keyEquivalent:@""];
            item.target = self; item.tag = action; item.representedObject = S(i.id);
        }
        entry.submenu = sub;
    }
    [menu addItem:NSMenuItem.separatorItem];
    NSMenuItem *start = [menu addItemWithTitle:@"Start All" action:@selector(startAll:) keyEquivalent:@""]; start.target = self;
    NSMenuItem *stop = [menu addItemWithTitle:@"Stop All" action:@selector(stopAll:) keyEquivalent:@""]; stop.target = self;
    [menu addItem:NSMenuItem.separatorItem];
    NSMenuItem *quit = [menu addItemWithTitle:@"Quit Lyrion Player" action:@selector(quit:) keyEquivalent:@"q"]; quit.target = self;
    statusItem.menu = menu;
}
- (void)menuWillOpen:(NSMenu *)menu { menuOpen = true; }
- (void)menuDidClose:(NSMenu *)menu { menuOpen = false; }
- (void)importInstances:(id)sender {
    try {
        auto entries = tray::platform::legacyServices();
        if (entries.empty()) { [self alert:@"No startup entries created by the service scripts were found."]; return; }
        for (auto &entry : entries) {
            if (!entry.error.empty()) { [self alert:S(entry.label+": "+entry.error)]; continue; }
            std::string description = "Import " + entry.name + "?\nServer: " + (entry.host.empty() ? "Automatic" : entry.host) +
                "\nPlayer ID: " + (entry.mac.empty() ? "Current network identity (resolved on import)" : entry.mac) +
                "\n\nThe original startup entry will be backed up and removed. Only the tray app will manage this player. Start at sign-in remains unchanged.";
            if ([self confirm:S(description)]) manager->importLegacy(entry);
        }
        [table reloadData];
    } catch (const std::exception &e) { [self alert:S(e.what())]; }
}
- (void)tick:(NSTimer *)timer {
    if (!manager) return;
    try {
        manager->tick();
        auto snapshot = tray::encodeConfig(manager->instances);
        for (const auto &i : manager->instances) snapshot += manager->status(i.id)+"\n";
        if (snapshot != tableSnapshot) {
            tableSnapshot = std::move(snapshot);
            NSInteger row = table.selectedRow; [table reloadData];
            if (row >= 0 && row < (NSInteger)manager->instances.size()) [table selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
        }
        auto status = tray::platform::loginStatus();
        login.state = status == "on" ? NSControlStateValueOn : NSControlStateValueOff;
        summary.stringValue = manager->quitting ? @"Stopping players…" : manager->scanning ? @"Scanning servers…" :
            S(std::to_string(manager->instances.size())+" instances · "+std::to_string(manager->servers.size())+" servers found" +
              (status == "approval" ? " · Sign-in startup needs approval" : ""));
        if (lastScanning && !manager->scanning && editor) [self refreshServerChoices];
        lastScanning = manager->scanning;
        [self rebuildMenu];
        if (manager->quitting && manager->stopped()) [NSApp replyToApplicationShouldTerminate:YES];
    } catch (const std::exception &e) { summary.stringValue = S(e.what()); }
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)app {
    if (!manager || smoke || manager->stopped()) return NSTerminateNow;
    manager->quit();
    return NSTerminateLater;
}
- (void)applicationWillTerminate:(NSNotification *)notification {
    manager.reset();
    if (lockFD > 0) { close(lockFD); lockFD = -1; }
}
@end

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    @autoreleasepool {
        NSApplication *app = NSApplication.sharedApplication;
        PlayerApp *delegate = [PlayerApp new]; app.delegate = delegate;
        [app run];
    }
    return 0;
}
