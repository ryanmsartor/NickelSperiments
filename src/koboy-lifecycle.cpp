/*
 * Copyright (C) 2026 NickelSperiments contributors
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <NickelHook.h>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace {

const char kPidPath[] = "/tmp/koboy.pid";

typedef void (*LifecycleMethod)(void *self);
LifecycleMethod originalAboutToSleep = nullptr;
LifecycleMethod originalResumedFromSleep = nullptr;

bool readKoBoyPid(pid_t &pid)
{
    const int descriptor = open(kPidPath, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0)
        return false;
    char text[32] = { 0 };
    const ssize_t length = read(descriptor, text, sizeof(text) - 1U);
    close(descriptor);
    if (length <= 0)
        return false;
    char *end = nullptr;
    errno = 0;
    const long parsed = std::strtol(text, &end, 10);
    if (errno || end == text || (*end != '\0' && *end != '\n') || parsed <= 1)
        return false;
    pid = static_cast<pid_t>(parsed);
    return true;
}

void notifyKoBoy(int signalNumber, const char *event)
{
    pid_t pid = -1;
    if (!readKoBoyPid(pid))
        return;
    if (kill(pid, signalNumber) == 0)
        nh_log("Sent %s to Ko Boy pid %ld", event, static_cast<long>(pid));
    else if (errno != ESRCH)
        nh_log("Could not send %s to Ko Boy pid %ld: %s", event,
               static_cast<long>(pid), std::strerror(errno));
}

} // namespace

extern "C" __attribute__((visibility("default")))
void koboyAboutToSleep(void *self)
{
    notifyKoBoy(SIGUSR1, "sleep");
    originalAboutToSleep(self);
}

extern "C" __attribute__((visibility("default")))
void koboyResumedFromSleep(void *self)
{
    originalResumedFromSleep(self);
    notifyKoBoy(SIGUSR2, "wake");
}

namespace {

struct nh_info info = {
    .name = "KoBoyLifecycle",
    .desc = "Ko Boy sleep and wake lifecycle bridge",
    .uninstall_flag = "/mnt/onboard/.adds/koboy/uninstall-lifecycle-hook",
};

struct nh_hook hooks[] = {
    {
        .sym = "_ZN22N3PowerWorkflowManager12aboutToSleepEv",
        .sym_new = "koboyAboutToSleep",
        .lib = "libnickel.so.1.0.0",
        .out = nh_symoutptr(originalAboutToSleep),
        .desc = "Pause Ko Boy when Nickel begins sleeping",
        .optional = true,
    },
    {
        .sym = "_ZN22N3PowerWorkflowManager16resumedFromSleepEv",
        .sym_new = "koboyResumedFromSleep",
        .lib = "libnickel.so.1.0.0",
        .out = nh_symoutptr(originalResumedFromSleep),
        .desc = "Resume Ko Boy when Nickel finishes waking",
        .optional = true,
    },
    {0},
};

struct nh_dlsym symbols[] = {{0}};

} // namespace

NickelHook(
    .init = nullptr,
    .info = &info,
    .hook = hooks,
    .dlsym = symbols,
)
