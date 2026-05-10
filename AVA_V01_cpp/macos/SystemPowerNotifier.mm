#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include "SystemPowerNotifier.h"

#include <algorithm>
#include <mutex>
#include <vector>

namespace {

struct SystemPowerCallbackEntry {
    void* context = nullptr;
    AvaSystemPowerCallback onWillSleep = nullptr;
    AvaSystemPowerCallback onDidWake = nullptr;
};

std::mutex& callbackEntriesMutex() {
    static std::mutex mutex;
    return mutex;
}

std::vector<SystemPowerCallbackEntry>& callbackEntries() {
    static std::vector<SystemPowerCallbackEntry> entries;
    return entries;
}

id willSleepObserverToken = nil;
id didWakeObserverToken = nil;

std::vector<SystemPowerCallbackEntry> snapshotCallbackEntries() {
    std::lock_guard<std::mutex> lock(callbackEntriesMutex());
    return callbackEntries();
}

void invokeWillSleepCallbacks() {
    for (const auto& entry : snapshotCallbackEntries()) {
        if (entry.onWillSleep) {
            entry.onWillSleep(entry.context);
        }
    }
}

void invokeDidWakeCallbacks() {
    for (const auto& entry : snapshotCallbackEntries()) {
        if (entry.onDidWake) {
            entry.onDidWake(entry.context);
        }
    }
}

void ensureWorkspaceObserversRegistered() {
    NSNotificationCenter* center = [[NSWorkspace sharedWorkspace] notificationCenter];

    if (willSleepObserverToken == nil) {
        willSleepObserverToken =
            [center addObserverForName:NSWorkspaceWillSleepNotification
                                object:nil
                                 queue:[NSOperationQueue mainQueue]
                            usingBlock:^(NSNotification* /*note*/) {
                                invokeWillSleepCallbacks();
                            }];
    }
    if (didWakeObserverToken == nil) {
        didWakeObserverToken =
            [center addObserverForName:NSWorkspaceDidWakeNotification
                                object:nil
                                 queue:[NSOperationQueue mainQueue]
                            usingBlock:^(NSNotification* /*note*/) {
                                invokeDidWakeCallbacks();
                            }];
    }
}

void teardownWorkspaceObserversIfUnused() {
    bool isEmpty = false;
    {
        std::lock_guard<std::mutex> lock(callbackEntriesMutex());
        isEmpty = callbackEntries().empty();
    }
    if (!isEmpty) return;

    NSNotificationCenter* center = [[NSWorkspace sharedWorkspace] notificationCenter];
    if (willSleepObserverToken != nil) {
        [center removeObserver:willSleepObserverToken];
        willSleepObserverToken = nil;
    }
    if (didWakeObserverToken != nil) {
        [center removeObserver:didWakeObserverToken];
        didWakeObserverToken = nil;
    }
}

} // namespace

void avaInstallSystemPowerObservers(void* context,
                                    AvaSystemPowerCallback onWillSleep,
                                    AvaSystemPowerCallback onDidWake) {
    if (context == nullptr) return;

    ensureWorkspaceObserversRegistered();

    std::lock_guard<std::mutex> lock(callbackEntriesMutex());
    auto& entries = callbackEntries();
    auto existing = std::find_if(
        entries.begin(), entries.end(),
        [context](const SystemPowerCallbackEntry& entry) { return entry.context == context; });

    SystemPowerCallbackEntry newEntry{context, onWillSleep, onDidWake};
    if (existing == entries.end()) {
        entries.push_back(newEntry);
    } else {
        *existing = newEntry;
    }
}

void avaRemoveSystemPowerObservers(void* context) {
    if (context == nullptr) return;

    {
        std::lock_guard<std::mutex> lock(callbackEntriesMutex());
        auto& entries = callbackEntries();
        entries.erase(
            std::remove_if(
                entries.begin(), entries.end(),
                [context](const SystemPowerCallbackEntry& entry) { return entry.context == context; }),
            entries.end());
    }
    teardownWorkspaceObserversIfUnused();
}
