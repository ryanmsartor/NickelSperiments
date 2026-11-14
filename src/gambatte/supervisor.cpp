/*
 * Copyright (C) 2026 NickelSperiments contributors
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "framebuffer_backend.h"
#include "kobo_session.h"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

volatile sig_atomic_t child_pid = -1;

void forward_signal(int signal_number) {
    const pid_t child = static_cast<pid_t>(child_pid);
    if (child > 0)
        kill(child, signal_number);
}

std::string player_path() {
    char executable[1024];
    const ssize_t length = readlink("/proc/self/exe", executable,
                                    sizeof(executable) - 1U);
    if (length < 0)
        throw std::runtime_error(std::string("cannot resolve supervisor path: ") +
                                 std::strerror(errno));
    executable[length] = '\0';
    std::string path(executable);
    const std::string::size_type slash = path.rfind('/');
    if (slash == std::string::npos)
        throw std::runtime_error("supervisor path has no directory");
    return path.substr(0, slash + 1U) + "kobo-gambatte-display";
}

int child_exit_code(int status) {
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return 1;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0]
                  << " ROM [gambatte display options]\n";
        return 2;
    }

    try {
        const std::string player = player_path();
        gambatte_kobo::NickelSession nickel;
        gambatte_kobo::FramebufferBackend framebuffer;

        nickel.suspend();
        framebuffer.open_device("/dev/fb0");
        framebuffer.save_screen();

        std::signal(SIGINT, forward_signal);
        std::signal(SIGTERM, forward_signal);
        std::signal(SIGHUP, forward_signal);

        const pid_t forked = fork();
        if (forked < 0)
            throw std::runtime_error(std::string("fork failed: ") +
                                     std::strerror(errno));
        if (forked == 0) {
            if (setenv("GAMBATTE_SUPERVISED", "1", 1) != 0)
                _exit(126);
            std::vector<char *> arguments;
            arguments.push_back(const_cast<char *>(player.c_str()));
            for (int index = 1; index < argc; ++index)
                arguments.push_back(argv[index]);
            arguments.push_back(0);
            execv(player.c_str(), &arguments[0]);
            _exit(127);
        }

        child_pid = forked;
        int status = 0;
        while (waitpid(forked, &status, 0) < 0) {
            if (errno == EINTR)
                continue;
            throw std::runtime_error(std::string("waitpid failed: ") +
                                     std::strerror(errno));
        }
        child_pid = -1;

        framebuffer.restore(0xFFFF0001U);
        nickel.resume();

        const int result = child_exit_code(status);
        std::cout << "supervisor_child_status=" << result << '\n';
        std::cout << "supervisor_cleanup=pass\n";
        return result;
    } catch (const std::exception &error) {
        std::cerr << "supervisor error: " << error.what() << '\n';
        return 1;
    }
}
