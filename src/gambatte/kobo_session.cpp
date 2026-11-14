/*
 * Copyright (C) 2026 NickelSperiments contributors
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "kobo_session.h"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <stdexcept>
#include <string>

namespace gambatte_kobo {
namespace {

pid_t find_process(const char *name) {
    DIR *directory = opendir("/proc");
    if (!directory)
        throw std::runtime_error("cannot inspect /proc");
    pid_t found = -1;
    for (dirent *entry = readdir(directory); entry; entry = readdir(directory)) {
        char *end = 0;
        const long candidate = std::strtol(entry->d_name, &end, 10);
        if (!end || *end != '\0' || candidate <= 0)
            continue;
        std::ifstream comm(std::string("/proc/") + entry->d_name + "/comm");
        std::string process_name;
        std::getline(comm, process_name);
        if (process_name == name) {
            found = static_cast<pid_t>(candidate);
            break;
        }
    }
    closedir(directory);
    return found;
}

} // namespace

NickelSession::NickelSession() : nickel_pid_(-1), suspended_(false) {}

NickelSession::~NickelSession() {
    try {
        resume();
    } catch (...) {
    }
}

void NickelSession::suspend() {
    if (suspended_)
        return;
    nickel_pid_ = find_process("nickel");
    if (nickel_pid_ <= 0)
        throw std::runtime_error("Nickel process was not found");
    if (kill(nickel_pid_, SIGSTOP) != 0)
        throw std::runtime_error(std::string("cannot suspend Nickel: ") +
                                 std::strerror(errno));
    suspended_ = true;
}

void NickelSession::resume() {
    if (!suspended_)
        return;
    if (kill(nickel_pid_, SIGCONT) != 0 && errno != ESRCH)
        throw std::runtime_error(std::string("cannot resume Nickel: ") +
                                 std::strerror(errno));
    suspended_ = false;
}

} // namespace gambatte_kobo
