/*
 * Copyright (C) 2026 NickelSperiments contributors
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "core_factory.h"
#include "frame_scaler.h"
#include "framebuffer_backend.h"
#include "touch_input.h"

#include <file/archive_file.h>
#include <lists/string_list.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace {

const char *const kKoBoyRoot = "/mnt/onboard/KoBoy";
const char *const kKoBoyRomDirectory = "/mnt/onboard/KoBoy/Roms";
const char *const kKoBoyBiosDirectory = "/mnt/onboard/KoBoy/BIOS";
const char *const kKoBoyStateDirectory = "/mnt/onboard/KoBoy/States";
const char *const kKoBoySaveDirectory = "/mnt/onboard/KoBoy/Saves";
const char *const kSystemFoldersInitialized =
    "/mnt/onboard/KoBoy/.system-folders-initialized";
const char *const kDeveloperFlag = "/mnt/onboard/KoBoy/developer";
const char *const kDiagnosticsFlag = "/mnt/onboard/KoBoy/diagnostics";
const char *const kNoGameplayRefreshFlag =
    "/mnt/onboard/KoBoy/isolate-no-gameplay-refresh";
const char *const kNoTouchFlag = "/mnt/onboard/KoBoy/isolate-no-touch";
const char *const kDisplayConfig = "/mnt/onboard/KoBoy/display.conf";
const char *const kAudioConfig = "/mnt/onboard/KoBoy/audio.conf";
const char *const kRefreshBreadcrumb =
    "/mnt/onboard/KoBoy/refresh-active.log";
const char *const kPidPath = "/tmp/koboy.pid";
volatile sig_atomic_t interrupted = 0;
volatile sig_atomic_t sleep_requested = 0;
volatile sig_atomic_t wake_requested = 0;

enum RefreshType {
    REFRESH_A2,
    REFRESH_GC16_FULL,
    REFRESH_TYPE_COUNT
};

struct DisplaySettings {
    unsigned full_refresh_seconds;
    bool grid_effect;
    unsigned target_refresh_quarters;
    DisplaySettings()
        : full_refresh_seconds(60U), grid_effect(false),
          target_refresh_quarters(36U) {}
};

void handle_signal(int) { interrupted = 1; }
void handle_sleep_signal(int) { sleep_requested = 1; }
void handle_wake_signal(int) { wake_requested = 1; }

class PidFile {
public:
    PidFile() {
        const int descriptor = open(kPidPath,
                                    O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
                                    0644);
        if (descriptor < 0)
            throw std::runtime_error(std::string("cannot create ") + kPidPath);
        char text[32];
        const int length = std::snprintf(text, sizeof(text), "%ld\n",
                                         static_cast<long>(getpid()));
        const ssize_t written = write(descriptor, text,
                                      static_cast<size_t>(length));
        const int saved_errno = errno;
        close(descriptor);
        if (written != length) {
            unlink(kPidPath);
            errno = saved_errno;
            throw std::runtime_error(std::string("cannot write ") + kPidPath);
        }
    }

    ~PidFile() { unlink(kPidPath); }

private:
    PidFile(const PidFile &);
    PidFile &operator=(const PidFile &);
};

double monotonic_seconds() {
    timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
        throw std::runtime_error("clock_gettime failed");
    return static_cast<double>(value.tv_sec) +
           static_cast<double>(value.tv_nsec) / 1000000000.0;
}

void sleep_until(double deadline) {
    timespec target;
    target.tv_sec = static_cast<time_t>(deadline);
    target.tv_nsec = static_cast<long>((deadline - target.tv_sec) * 1000000000.0);
    int result;
    while ((result = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME,
                                     &target, 0)) != 0) {
        if (result == EINTR && interrupted)
            return;
        if (result != EINTR)
            throw std::runtime_error(std::string("clock_nanosleep: ") +
                                     std::strerror(result));
    }
}

unsigned parse_unsigned(const char *value, const char *option) {
    errno = 0;
    char *end = 0;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (errno || !end || *end != '\0' || parsed == 0UL || parsed > UINT_MAX)
        throw std::runtime_error(std::string("invalid value for ") + option);
    return static_cast<unsigned>(parsed);
}

std::vector<unsigned char> read_file(const char *path) {
    if (std::strchr(path, '#')) {
        void *contents = 0;
        int64_t size = 0;
        if (!file_archive_compressed_read(path, &contents, 0, &size) ||
            !contents || size <= 0 || size > static_cast<int64_t>(UINT_MAX)) {
            std::free(contents);
            throw std::runtime_error("cannot read a supported ROM from archive");
        }
        const unsigned char *begin = static_cast<unsigned char *>(contents);
        std::vector<unsigned char> data(begin, begin + size);
        std::free(contents);
        return data;
    }
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        throw std::runtime_error(std::string("cannot open ROM: ") + path);
    const std::ifstream::pos_type end = input.tellg();
    if (end <= 0 || end > static_cast<std::ifstream::pos_type>(UINT_MAX))
        throw std::runtime_error("ROM has an invalid or unsupported size");
    std::vector<unsigned char> data(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char *>(&data[0]), data.size());
    if (!input)
        throw std::runtime_error("failed to read complete ROM");
    return data;
}

std::string loadable_rom_path(const std::string &path) {
    if (path.size() < 4U || path[path.size() - 4U] != '.' ||
        std::tolower(static_cast<unsigned char>(path[path.size() - 3U])) != 'z' ||
        std::tolower(static_cast<unsigned char>(path[path.size() - 2U])) != 'i' ||
        std::tolower(static_cast<unsigned char>(path[path.size() - 1U])) != 'p')
        return path;

    struct string_list *entries = file_archive_get_file_list(
        path.c_str(), "gb|gbc|gba|bin|duck|md0|md1|md2|md20s|md25s");
    if (!entries || entries->size == 0U) {
        string_list_free(entries);
        throw std::runtime_error("ZIP contains no supported ROM");
    }
    const std::string member(entries->elems[0].data);
    string_list_free(entries);
    return path + "#" + member;
}

bool ensure_directory(const char *path) {
    if (mkdir(path, 0755) == 0 || errno == EEXIST)
        return true;
    std::cerr << "warning: cannot create " << path << ": "
              << std::strerror(errno) << '\n';
    return false;
}

bool file_exists(const char *path) {
    struct stat info;
    return stat(path, &info) == 0;
}

const char *refresh_type_name(RefreshType type) {
    switch (type) {
        case REFRESH_A2: return "A2";
        case REFRESH_GC16_FULL: return "GC16 FULL FLASH";
        default: return "UNKNOWN";
    }
}

bool write_all(int descriptor, const std::string &contents) {
    std::string::size_type offset = 0U;
    while (offset < contents.size()) {
        const ssize_t count = write(descriptor, contents.data() + offset,
                                    contents.size() - offset);
        if (count < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        offset += static_cast<std::string::size_type>(count);
    }
    return true;
}

bool valid_full_refresh_seconds(unsigned value) {
    const unsigned values[] = {0U, 5U, 15U, 30U, 60U, 90U};
    for (unsigned i = 0U; i < sizeof(values) / sizeof(values[0]); ++i)
        if (value == values[i])
            return true;
    return false;
}

bool valid_target_refresh_quarters(unsigned value) {
    return value >= 32U && value <= 48U;
}

void load_display_settings(DisplaySettings &settings) {
    std::ifstream input(kDisplayConfig);
    std::string line;
    while (std::getline(input, line)) {
        const std::string::size_type equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        const std::string key = line.substr(0U, equals);
        const unsigned value = static_cast<unsigned>(
            std::strtoul(line.substr(equals + 1U).c_str(), 0, 10));
        if (key == "full_refresh_seconds" &&
                 valid_full_refresh_seconds(value))
            settings.full_refresh_seconds = value;
        else if (key == "grid_effect" && value <= 1U)
            settings.grid_effect = value != 0U;
        else if (key == "target_refresh_quarters" &&
                 valid_target_refresh_quarters(value))
            settings.target_refresh_quarters = value;
    }
}

void save_display_settings(const DisplaySettings &settings) {
    if (!ensure_directory(kKoBoyRoot))
        return;
    char text[192];
    const int length = std::snprintf(
        text, sizeof(text),
        "full_refresh_seconds=%u\ngrid_effect=%u\n"
        "target_refresh_quarters=%u\n",
        settings.full_refresh_seconds, settings.grid_effect ? 1U : 0U,
        settings.target_refresh_quarters);
    const std::string temporary = std::string(kDisplayConfig) + ".tmp";
    const int output = open(temporary.c_str(),
                            O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    bool saved = output >= 0 && length > 0 &&
        write_all(output, std::string(text, static_cast<std::size_t>(length))) &&
        fsync(output) == 0;
    if (output >= 0 && close(output) != 0)
        saved = false;
    if (!saved || rename(temporary.c_str(), kDisplayConfig) != 0)
        std::cerr << "warning: cannot save display settings\n";
}

unsigned load_audio_volume() {
    std::ifstream input(kAudioConfig);
    std::string line;
    while (std::getline(input, line)) {
        if (line.compare(0U, 7U, "volume=") == 0) {
            const unsigned volume = static_cast<unsigned>(
                std::strtoul(line.substr(7U).c_str(), 0, 10));
            if (volume <= 20U)
                return volume;
        }
    }
    return 10U;
}

void save_audio_volume(unsigned volume) {
    if (!ensure_directory(kKoBoyRoot))
        return;
    const std::string temporary = std::string(kAudioConfig) + ".tmp";
    const int output = open(temporary.c_str(),
                            O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    char text[32];
    const int length = std::snprintf(text, sizeof(text), "volume=%u\n", volume);
    bool saved = output >= 0 && length > 0 &&
        write_all(output, std::string(text, static_cast<std::size_t>(length))) &&
        fsync(output) == 0;
    if (output >= 0 && close(output) != 0)
        saved = false;
    if (!saved || rename(temporary.c_str(), kAudioConfig) != 0)
        std::cerr << "warning: cannot save audio volume\n";
}

void cycle_full_refresh_frequency(DisplaySettings &settings) {
    const unsigned values[] = {0U, 5U, 15U, 30U, 60U, 90U};
    unsigned index = 0U;
    while (index + 1U < sizeof(values) / sizeof(values[0]) &&
           values[index] != settings.full_refresh_seconds)
        ++index;
    settings.full_refresh_seconds =
        values[(index + 1U) % (sizeof(values) / sizeof(values[0]))];
}

void cycle_target_refresh_rate(DisplaySettings &settings) {
    const unsigned values[] = {
        32U, 33U, 34U, 35U, 36U, 37U, 38U, 39U, 40U,
        41U, 42U, 43U, 44U, 45U, 46U, 47U, 48U
    };
    unsigned index = 0U;
    while (index + 1U < sizeof(values) / sizeof(values[0]) &&
           values[index] != settings.target_refresh_quarters)
        ++index;
    settings.target_refresh_quarters =
        values[(index + 1U) % (sizeof(values) / sizeof(values[0]))];
}

unsigned long long read_unsigned_file(const char *path) {
    std::ifstream input(path);
    unsigned long long value = 0ULL;
    input >> value;
    return input ? value : 0ULL;
}

struct SystemMetrics {
    unsigned long long memory_available_kb;
    unsigned long long resident_kb;
    unsigned long long cpu_frequency_khz;
    long long cpu_temperature_mc;
    long long battery_temperature_mc;
    long long ap_temperature_mc;
    SystemMetrics()
        : memory_available_kb(0ULL), resident_kb(0ULL),
          cpu_frequency_khz(0ULL), cpu_temperature_mc(0LL),
          battery_temperature_mc(0LL), ap_temperature_mc(0LL) {}
};

SystemMetrics read_system_metrics() {
    SystemMetrics metrics;
    std::ifstream memory("/proc/meminfo");
    std::string key;
    unsigned long long value;
    std::string unit;
    while (memory >> key >> value >> unit)
        if (key == "MemAvailable:") {
            metrics.memory_available_kb = value;
            break;
        }

    std::ifstream statm("/proc/self/statm");
    unsigned long long total_pages = 0ULL;
    unsigned long long resident_pages = 0ULL;
    if (statm >> total_pages >> resident_pages) {
        const long page_size = sysconf(_SC_PAGESIZE);
        if (page_size > 0)
            metrics.resident_kb = resident_pages *
                static_cast<unsigned long long>(page_size) / 1024ULL;
    }

    metrics.cpu_frequency_khz = read_unsigned_file(
        "/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    metrics.cpu_temperature_mc = static_cast<long long>(read_unsigned_file(
        "/sys/class/thermal/thermal_zone0/temp"));
    metrics.battery_temperature_mc = static_cast<long long>(read_unsigned_file(
        "/sys/class/thermal/thermal_zone1/temp"));
    metrics.ap_temperature_mc = static_cast<long long>(read_unsigned_file(
        "/sys/class/thermal/thermal_zone2/temp"));
    return metrics;
}

class CpuUsageSampler {
public:
    CpuUsageSampler() : total_(0ULL), idle_(0ULL), process_(0ULL) {}

    void sample(unsigned &system_tenths, unsigned &process_tenths) {
        unsigned long long total = 0ULL;
        unsigned long long idle = 0ULL;
        unsigned long long process = 0ULL;
        system_tenths = 0U;
        process_tenths = 0U;
        if (!read_system(total, idle) || !read_process(process))
            return;
        if (total_ && total > total_) {
            const unsigned long long total_delta = total - total_;
            const unsigned long long idle_delta = idle >= idle_
                ? idle - idle_ : 0ULL;
            const unsigned long long process_delta = process >= process_
                ? process - process_ : 0ULL;
            system_tenths = static_cast<unsigned>(
                (total_delta - std::min(total_delta, idle_delta)) * 1000ULL /
                total_delta);
            const long cpus = sysconf(_SC_NPROCESSORS_ONLN);
            process_tenths = static_cast<unsigned>(
                process_delta * static_cast<unsigned long long>(
                    cpus > 0 ? cpus : 1L) * 1000ULL / total_delta);
        }
        total_ = total;
        idle_ = idle;
        process_ = process;
    }

private:
    bool read_system(unsigned long long &total,
                     unsigned long long &idle) const {
        std::ifstream input("/proc/stat");
        std::string label;
        unsigned long long user = 0ULL, nice = 0ULL, system = 0ULL;
        unsigned long long idle_ticks = 0ULL, iowait = 0ULL, irq = 0ULL;
        unsigned long long softirq = 0ULL, steal = 0ULL;
        if (!(input >> label >> user >> nice >> system >> idle_ticks >>
              iowait >> irq >> softirq >> steal) || label != "cpu")
            return false;
        total = user + nice + system + idle_ticks + iowait + irq +
                softirq + steal;
        idle = idle_ticks + iowait;
        return true;
    }

    bool read_process(unsigned long long &ticks) const {
        std::ifstream input("/proc/self/stat");
        std::string line;
        if (!std::getline(input, line))
            return false;
        const std::string::size_type close = line.rfind(')');
        if (close == std::string::npos || close + 2U >= line.size())
            return false;
        std::istringstream fields(line.substr(close + 2U));
        char state = 0;
        long long ignored = 0LL;
        if (!(fields >> state))
            return false;
        for (unsigned field = 4U; field <= 13U; ++field)
            if (!(fields >> ignored))
                return false;
        unsigned long long user = 0ULL, system = 0ULL;
        if (!(fields >> user >> system))
            return false;
        ticks = user + system;
        return true;
    }

    unsigned long long total_;
    unsigned long long idle_;
    unsigned long long process_;
};

void record_menu_refresh(const char *stage, uint32_t marker,
                         double elapsed_seconds) {
    if (!ensure_directory(kKoBoyRoot))
        return;
    const SystemMetrics metrics = read_system_metrics();
    char entry[384];
    const int length = elapsed_seconds < 0.0
        ? std::snprintf(entry, sizeof(entry),
                        "menu_refresh %s pid=%ld uptime=%.3f marker=%u "
                        "mem_available_kb=%llu rss_kb=%llu cpu_khz=%llu "
                        "cpu_temp_mc=%lld battery_temp_mc=%lld "
                        "ap_temp_mc=%lld\n",
                        stage, static_cast<long>(getpid()), monotonic_seconds(),
                        marker, metrics.memory_available_kb,
                        metrics.resident_kb, metrics.cpu_frequency_khz,
                        metrics.cpu_temperature_mc,
                        metrics.battery_temperature_mc,
                        metrics.ap_temperature_mc)
        : std::snprintf(entry, sizeof(entry),
                        "menu_refresh %s pid=%ld uptime=%.3f marker=%u "
                        "elapsed=%.3f mem_available_kb=%llu rss_kb=%llu "
                        "cpu_khz=%llu cpu_temp_mc=%lld battery_temp_mc=%lld "
                        "ap_temp_mc=%lld\n",
                        stage, static_cast<long>(getpid()), monotonic_seconds(),
                        marker, elapsed_seconds, metrics.memory_available_kb,
                        metrics.resident_kb, metrics.cpu_frequency_khz,
                        metrics.cpu_temperature_mc,
                        metrics.battery_temperature_mc,
                        metrics.ap_temperature_mc);
    const int output = open(kRefreshBreadcrumb,
                            O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (output < 0 || length <= 0 ||
        !write_all(output, std::string(entry, static_cast<std::size_t>(length))) ||
        fsync(output) != 0)
        std::cerr << "warning: cannot record menu refresh\n";
    if (output >= 0)
        close(output);
}

void record_simple_breadcrumb(const char *event, uint32_t marker) {
    if (!ensure_directory(kKoBoyRoot))
        return;
    char entry[160];
    const int length = std::snprintf(
        entry, sizeof(entry), "%s pid=%ld uptime=%.3f marker=%u\n",
        event, static_cast<long>(getpid()), monotonic_seconds(), marker);
    const int output = open(kRefreshBreadcrumb,
                            O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (output < 0 || length <= 0 ||
        !write_all(output, std::string(entry, static_cast<std::size_t>(length))) ||
        fsync(output) != 0)
        std::cerr << "warning: cannot record breadcrumb\n";
    if (output >= 0)
        close(output);
}

void record_gameplay_refresh(const char *stage, uint32_t marker,
                             RefreshType type, double elapsed_seconds) {
    if (!ensure_directory(kKoBoyRoot))
        return;
    char entry[224];
    const int length = elapsed_seconds < 0.0
        ? std::snprintf(entry, sizeof(entry),
                        "gameplay_refresh %s pid=%ld uptime=%.3f marker=%u "
                        "type=%s\n",
                        stage, static_cast<long>(getpid()), monotonic_seconds(),
                        marker, refresh_type_name(type))
        : std::snprintf(entry, sizeof(entry),
                        "gameplay_refresh %s pid=%ld uptime=%.3f marker=%u "
                        "type=%s elapsed=%.3f\n",
                        stage, static_cast<long>(getpid()), monotonic_seconds(),
                        marker, refresh_type_name(type), elapsed_seconds);
    const int output = open(kRefreshBreadcrumb,
                            O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (output < 0 || length <= 0 ||
        !write_all(output, std::string(entry, static_cast<std::size_t>(length))) ||
        fsync(output) != 0)
        std::cerr << "warning: cannot record gameplay refresh\n";
    if (output >= 0)
        close(output);
}

void record_gameplay_heartbeat(uint32_t submitted_marker,
                               RefreshType submitted_type,
                               uint32_t completed_marker,
                               double last_wait_seconds,
                               const unsigned counts[REFRESH_TYPE_COUNT],
                               unsigned system_cpu_tenths,
                               unsigned process_cpu_tenths,
                               uint64_t audio_queued,
                               uint64_t audio_dropped) {
    if (!ensure_directory(kKoBoyRoot))
        return;
    const SystemMetrics metrics = read_system_metrics();
    char entry[512];
    const int length = std::snprintf(
        entry, sizeof(entry),
        "heartbeat pid=%ld uptime=%.3f submitted_marker=%u "
        "submitted_type=%s completed_marker=%u last_wait=%.3f "
        "count_a2=%u count_gc16_full=%u "
        "system_cpu_pct=%u.%u process_cpu_pct=%u.%u "
        "audio_frames_queued=%llu audio_frames_dropped=%llu "
        "mem_available_kb=%llu rss_kb=%llu "
        "cpu_khz=%llu cpu_temp_mc=%lld battery_temp_mc=%lld ap_temp_mc=%lld\n",
        static_cast<long>(getpid()), monotonic_seconds(), submitted_marker,
        refresh_type_name(submitted_type), completed_marker,
        last_wait_seconds, counts[REFRESH_A2], counts[REFRESH_GC16_FULL],
        system_cpu_tenths / 10U,
        system_cpu_tenths % 10U, process_cpu_tenths / 10U,
        process_cpu_tenths % 10U,
        static_cast<unsigned long long>(audio_queued),
        static_cast<unsigned long long>(audio_dropped),
        metrics.memory_available_kb,
        metrics.resident_kb, metrics.cpu_frequency_khz,
        metrics.cpu_temperature_mc, metrics.battery_temperature_mc,
        metrics.ap_temperature_mc);
    const int output = open(kRefreshBreadcrumb,
                            O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (output < 0 || length <= 0 ||
        !write_all(output, std::string(entry, static_cast<std::size_t>(length))) ||
        fsync(output) != 0)
        std::cerr << "warning: cannot record gameplay heartbeat\n";
    if (output >= 0)
        close(output);
}

std::string lowercase(std::string value) {
    for (std::string::size_type i = 0; i < value.size(); ++i)
        value[i] = static_cast<char>(std::tolower(
            static_cast<unsigned char>(value[i])));
    return value;
}

std::string uppercase(std::string value) {
    for (std::string::size_type i = 0; i < value.size(); ++i)
        value[i] = static_cast<char>(std::toupper(
            static_cast<unsigned char>(value[i])));
    return value;
}

struct SystemFolder {
    std::string name;
    std::string path;
};

bool is_directory(const std::string &path) {
    struct stat information;
    return stat(path.c_str(), &information) == 0 &&
           S_ISDIR(information.st_mode);
}

void initialize_system_folders() {
    if (file_exists(kSystemFoldersInitialized))
        return;
    if (!ensure_directory(kKoBoyRoot) ||
        !ensure_directory(kKoBoyRomDirectory))
        return;
    const char *const names[] = {
        "Game Boy", "Game Boy Color", "Game Boy Advance", "Mega Duck"
    };
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        const std::string path = std::string(kKoBoyRomDirectory) + "/" +
                                 names[i];
        ensure_directory(path.c_str());
    }
    std::ofstream marker(kSystemFoldersInitialized,
                         std::ios::out | std::ios::trunc);
    marker << "Ko Boy system folders initialized\n";
}

std::vector<SystemFolder> list_systems() {
    initialize_system_folders();
    std::vector<SystemFolder> systems;
    DIR *directory = opendir(kKoBoyRomDirectory);
    if (!directory)
        return systems;
    for (dirent *entry = readdir(directory); entry; entry = readdir(directory)) {
        const std::string name(entry->d_name);
        if (name.empty() || name[0] == '.')
            continue;
        const std::string path = std::string(kKoBoyRomDirectory) + "/" + name;
        if (is_directory(path)) {
            SystemFolder system;
            system.name = name;
            system.path = path;
            systems.push_back(system);
        }
    }
    closedir(directory);
    std::sort(systems.begin(), systems.end(),
              [](const SystemFolder &left, const SystemFolder &right) {
                  const char *const stock[] = {
                      "game boy", "game boy color", "game boy advance",
                      "mega duck"
                  };
                  unsigned left_rank = 4U;
                  unsigned right_rank = 4U;
                  const std::string left_name = lowercase(left.name);
                  const std::string right_name = lowercase(right.name);
                  for (unsigned i = 0; i < 4U; ++i) {
                      if (left_name == stock[i]) left_rank = i;
                      if (right_name == stock[i]) right_rank = i;
                  }
                  if (left_rank != right_rank)
                      return left_rank < right_rank;
                  return lowercase(left.name) < lowercase(right.name);
              });
    return systems;
}

bool is_supported_rom(const std::string &name) {
    const std::string folded = lowercase(name);
    const char *const extensions[] = {
        ".gb", ".gbc", ".gb2", ".gba", ".gba2", ".bin", ".duck",
        ".md0", ".md1", ".md2", ".md20s", ".md25s", ".zip"
    };
    for (unsigned i = 0; i < sizeof(extensions) / sizeof(extensions[0]); ++i) {
        const std::string extension(extensions[i]);
        if (folded.size() >= extension.size() &&
            folded.substr(folded.size() - extension.size()) == extension)
            return true;
    }
    return false;
}

std::vector<std::string> list_roms(const std::string &system_path) {
    std::vector<std::string> roms;
    DIR *directory = opendir(system_path.c_str());
    if (!directory)
        return roms;
    for (dirent *entry = readdir(directory); entry; entry = readdir(directory)) {
        const std::string name(entry->d_name);
        if (!is_supported_rom(name))
            continue;
        const std::string path = system_path + "/" + name;
        if (!is_directory(path))
            roms.push_back(path);
    }
    closedir(directory);
    std::sort(roms.begin(), roms.end(),
              [](const std::string &left, const std::string &right) {
                  return lowercase(left) < lowercase(right);
              });
    return roms;
}

std::string rom_title(const std::string &path) {
    std::string title(path);
    const std::string::size_type slash = title.rfind('/');
    if (slash != std::string::npos)
        title.erase(0, slash + 1U);
    const std::string::size_type dot = title.rfind('.');
    if (dot != std::string::npos)
        title.erase(dot);
    return title;
}

std::string storage_name(const char *rom_path) {
    std::string name(rom_path);
    const std::string prefix = std::string(kKoBoyRomDirectory) + "/";
    if (name.compare(0, prefix.size(), prefix) == 0)
        name.erase(0, prefix.size());
    else {
        const std::string::size_type slash = name.rfind('/');
        if (slash != std::string::npos)
            name.erase(0, slash + 1U);
    }
    const std::string::size_type slash = name.rfind('/');
    const std::string::size_type filename = slash == std::string::npos
        ? 0U : slash + 1U;
    for (std::string::size_type index = filename; index < name.size(); ++index) {
        const unsigned char character = static_cast<unsigned char>(name[index]);
        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') ||
              character == '.' || character == '-' || character == '_'))
            name[index] = '_';
    }
    return name;
}

bool ensure_parent_directory(const std::string &path) {
    const std::string::size_type slash = path.rfind('/');
    return slash == std::string::npos ||
           ensure_directory(path.substr(0, slash).c_str());
}

std::string state_path(const char *rom_path, unsigned slot) {
    return std::string(kKoBoyStateDirectory) + "/" +
           storage_name(rom_path) + ".state" +
           static_cast<char>('0' + slot);
}

std::string save_path(const char *rom_path) {
    return std::string(kKoBoySaveDirectory) + "/" +
           storage_name(rom_path) + ".sav";
}

bool load_state(koboy::EmulatorCore &core, const std::string &path) {
    std::ifstream input(path.c_str(), std::ios::binary | std::ios::ate);
    if (!input)
        return false;
    const std::ifstream::pos_type end = input.tellg();
    if (end <= 0)
        return false;
    std::vector<unsigned char> state(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char *>(&state[0]), state.size());
    if (!input || !core.load_state(&state[0], state.size())) {
        std::cerr << "warning: rejected save state " << path << '\n';
        return false;
    }
    return true;
}

bool save_state(koboy::EmulatorCore &core, const std::string &path) {
    if (!ensure_directory(kKoBoyRoot) ||
        !ensure_directory(kKoBoyStateDirectory) ||
        !ensure_parent_directory(path))
        return false;
    const std::size_t size = core.state_size();
    if (!size)
        return false;
    std::vector<unsigned char> state(size);
    if (!core.save_state(&state[0], state.size()))
        return false;
    const std::string temporary = path + ".tmp";
    std::ofstream output(temporary.c_str(),
                         std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char *>(&state[0]), state.size());
    output.close();
    if (!output) {
        std::cerr << "warning: cannot write save state " << temporary << '\n';
        return false;
    }
    if (rename(temporary.c_str(), path.c_str()) != 0) {
        std::cerr << "warning: cannot install save state " << path << ": "
                  << std::strerror(errno) << '\n';
        return false;
    }
    return true;
}

bool load_sram(koboy::EmulatorCore &core, const std::string &path) {
    const std::size_t size = core.save_ram_size();
    void *const data = core.save_ram_data();
    if (!size || !data)
        return false;
    std::ifstream input(path.c_str(), std::ios::binary | std::ios::ate);
    if (!input)
        return false;
    const std::ifstream::pos_type end = input.tellg();
    if (end != static_cast<std::ifstream::pos_type>(size)) {
        std::cerr << "warning: rejected save RAM with unexpected size "
                  << path << '\n';
        return false;
    }
    std::vector<unsigned char> saved(size);
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char *>(&saved[0]), saved.size());
    if (!input) {
        std::cerr << "warning: cannot read complete save RAM " << path << '\n';
        return false;
    }
    std::memcpy(data, &saved[0], size);
    return true;
}

bool save_sram(koboy::EmulatorCore &core, const std::string &path) {
    const std::size_t size = core.save_ram_size();
    void *const data = core.save_ram_data();
    if (!size || !data)
        return true;
    if (!ensure_directory(kKoBoyRoot) ||
        !ensure_directory(kKoBoySaveDirectory) ||
        !ensure_parent_directory(path))
        return false;
    const std::string temporary = path + ".tmp";
    std::ofstream output(temporary.c_str(),
                         std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char *>(data), size);
    output.close();
    if (!output) {
        std::cerr << "warning: cannot write save RAM " << temporary << '\n';
        return false;
    }
    if (rename(temporary.c_str(), path.c_str()) != 0) {
        std::cerr << "warning: cannot install save RAM " << path << ": "
                  << std::strerror(errno) << '\n';
        return false;
    }
    return true;
}

struct Options {
    std::string rom;
    unsigned scale;
    unsigned seconds;
    Options() : scale(6), seconds(0) {}
};

Options parse_options(int argc, char **argv) {
    Options options;
    int first_option = 1;
    if (argc > 1 && std::strncmp(argv[1], "--", 2) != 0) {
        options.rom = argv[1];
        first_option = 2;
    }
    for (int i = first_option; i < argc; i += 2) {
        if (i + 1 >= argc)
            throw std::runtime_error("every option requires a value");
        if (std::strcmp(argv[i], "--scale") == 0)
            options.scale = parse_unsigned(argv[i + 1], "--scale");
        else if (std::strcmp(argv[i], "--seconds") == 0)
            options.seconds = parse_unsigned(argv[i + 1], "--seconds");
        else
            throw std::runtime_error(std::string("unknown option: ") + argv[i]);
    }
    if (options.scale > 6U)
        throw std::runtime_error("--scale must not exceed 6");
    return options;
}

void fill_rectangle(std::vector<uint32_t> &screen, unsigned width,
                    unsigned height, int left, int top, int right, int bottom,
                    uint32_t color) {
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > static_cast<int>(width)) right = width;
    if (bottom > static_cast<int>(height)) bottom = height;
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x)
            screen[static_cast<size_t>(y) * width + x] = color;
}

void fill_circle(std::vector<uint32_t> &screen, unsigned width,
                 unsigned height, int center_x, int center_y, int radius,
                 uint32_t color) {
    for (int y = -radius; y <= radius; ++y) {
        const int target_y = center_y + y;
        if (target_y < 0 || target_y >= static_cast<int>(height))
            continue;
        for (int x = -radius; x <= radius; ++x) {
            const int target_x = center_x + x;
            if (target_x >= 0 && target_x < static_cast<int>(width) &&
                x * x + y * y <= radius * radius)
                screen[static_cast<size_t>(target_y) * width + target_x] = color;
        }
    }
}

const unsigned char *glyph(char character) {
    static const unsigned char a[7] = {14, 17, 17, 31, 17, 17, 17};
    static const unsigned char b[7] = {30, 17, 17, 30, 17, 17, 30};
    static const unsigned char d[7] = {30, 17, 17, 17, 17, 17, 30};
    static const unsigned char e[7] = {31, 16, 16, 30, 16, 16, 31};
    static const unsigned char f[7] = {31, 16, 16, 30, 16, 16, 16};
    static const unsigned char g[7] = {15, 16, 16, 23, 17, 17, 15};
    static const unsigned char h[7] = {17, 17, 17, 31, 17, 17, 17};
    static const unsigned char i[7] = {31, 4, 4, 4, 4, 4, 31};
    static const unsigned char j[7] = {7, 2, 2, 2, 18, 18, 12};
    static const unsigned char k[7] = {17, 18, 20, 24, 20, 18, 17};
    static const unsigned char l[7] = {16, 16, 16, 16, 16, 16, 31};
    static const unsigned char m[7] = {17, 27, 21, 21, 17, 17, 17};
    static const unsigned char n[7] = {17, 25, 21, 19, 17, 17, 17};
    static const unsigned char o[7] = {14, 17, 17, 17, 17, 17, 14};
    static const unsigned char p[7] = {30, 17, 17, 30, 16, 16, 16};
    static const unsigned char q[7] = {14, 17, 17, 17, 21, 18, 13};
    static const unsigned char r[7] = {30, 17, 17, 30, 20, 18, 17};
    static const unsigned char s[7] = {15, 16, 16, 14, 1, 1, 30};
    static const unsigned char t[7] = {31, 4, 4, 4, 4, 4, 4};
    static const unsigned char u[7] = {17, 17, 17, 17, 17, 17, 14};
    static const unsigned char x[7] = {17, 17, 10, 4, 10, 17, 17};
    static const unsigned char v[7] = {17, 17, 17, 17, 17, 10, 4};
    static const unsigned char w[7] = {17, 17, 17, 21, 21, 21, 10};
    static const unsigned char y[7] = {17, 17, 10, 4, 4, 4, 4};
    static const unsigned char z[7] = {31, 1, 2, 4, 8, 16, 31};
    static const unsigned char c[7] = {15, 16, 16, 16, 16, 16, 15};
    static const unsigned char zero[7] = {14, 17, 19, 21, 25, 17, 14};
    static const unsigned char one[7] = {4, 12, 4, 4, 4, 4, 14};
    static const unsigned char two[7] = {14, 17, 1, 2, 4, 8, 31};
    static const unsigned char three[7] = {30, 1, 1, 14, 1, 1, 30};
    static const unsigned char four[7] = {2, 6, 10, 18, 31, 2, 2};
    static const unsigned char five[7] = {31, 16, 16, 30, 1, 1, 30};
    static const unsigned char six[7] = {14, 16, 16, 30, 17, 17, 14};
    static const unsigned char seven[7] = {31, 1, 2, 4, 8, 8, 8};
    static const unsigned char eight[7] = {14, 17, 17, 14, 17, 17, 14};
    static const unsigned char nine[7] = {14, 17, 17, 15, 1, 1, 14};
    static const unsigned char hyphen[7] = {0, 0, 0, 31, 0, 0, 0};
    static const unsigned char dot[7] = {0, 0, 0, 0, 0, 12, 12};
    static const unsigned char colon[7] = {0, 12, 12, 0, 12, 12, 0};
    static const unsigned char left_parenthesis[7] = {2, 4, 8, 8, 8, 4, 2};
    static const unsigned char right_parenthesis[7] = {8, 4, 2, 2, 2, 4, 8};
    static const unsigned char less_than[7] = {1, 2, 4, 8, 4, 2, 1};
    static const unsigned char greater_than[7] = {16, 8, 4, 2, 4, 8, 16};
    static const unsigned char slash[7] = {1, 2, 2, 4, 8, 8, 16};
    static const unsigned char percent[7] = {17, 2, 4, 4, 8, 16, 17};
    static const unsigned char comma[7] = {0, 0, 0, 0, 0, 4, 8};
    static const unsigned char apostrophe[7] = {4, 4, 8, 0, 0, 0, 0};
    static const unsigned char plus[7] = {0, 4, 4, 31, 4, 4, 0};
    static const unsigned char equals[7] = {0, 31, 0, 31, 0, 0, 0};
    static const unsigned char exclamation[7] = {4, 4, 4, 4, 4, 0, 4};
    static const unsigned char question[7] = {14, 17, 1, 2, 4, 0, 4};
    static const unsigned char underscore[7] = {0, 0, 0, 0, 0, 0, 31};
    switch (std::toupper(static_cast<unsigned char>(character))) {
        case 'A': return a;
        case 'B': return b;
        case 'C': return c;
        case 'D': return d;
        case 'E': return e;
        case 'F': return f;
        case 'G': return g;
        case 'H': return h;
        case 'I': return i;
        case 'J': return j;
        case 'K': return k;
        case 'L': return l;
        case 'M': return m;
        case 'N': return n;
        case 'O': return o;
        case 'P': return p;
        case 'Q': return q;
        case 'R': return r;
        case 'S': return s;
        case 'T': return t;
        case 'U': return u;
        case 'V': return v;
        case 'W': return w;
        case 'X': return x;
        case 'Y': return y;
        case 'Z': return z;
        case '0': return zero;
        case '1': return one;
        case '2': return two;
        case '3': return three;
        case '4': return four;
        case '5': return five;
        case '6': return six;
        case '7': return seven;
        case '8': return eight;
        case '9': return nine;
        case '-': return hyphen;
        case '.': return dot;
        case ':': return colon;
        case '(': return left_parenthesis;
        case ')': return right_parenthesis;
        case '<': return less_than;
        case '>': return greater_than;
        case '/': return slash;
        case '%': return percent;
        case ',': return comma;
        case '\'': return apostrophe;
        case '+': return plus;
        case '=': return equals;
        case '!': return exclamation;
        case '?': return question;
        case '_': return underscore;
        default: return 0;
    }
}

void draw_label(std::vector<uint32_t> &screen, unsigned width,
                unsigned height, int center_x, int center_y,
                const char *label, int scale, uint32_t color) {
    const int length = std::strlen(label);
    const int total_width = (length * 6 - 1) * scale;
    int left = center_x - total_width / 2;
    for (int index = 0; index < length; ++index) {
        const unsigned char *rows = glyph(label[index]);
        if (!rows)
            continue;
        for (int y = 0; y < 7; ++y)
            for (int x = 0; x < 5; ++x)
                if (rows[y] & (1U << (4 - x)))
                    fill_rectangle(screen, width, height,
                                   left + (index * 6 + x) * scale,
                                   center_y - 7 * scale / 2 + y * scale,
                                   left + (index * 6 + x + 1) * scale,
                                   center_y - 7 * scale / 2 + (y + 1) * scale,
                                   color);
    }
}

void draw_controls(std::vector<uint32_t> &screen, unsigned width,
                   unsigned height, bool shoulder_buttons) {
    const uint32_t dark = 0xFF202020U;
    const uint32_t light = 0xFFFFFFFFU;
    const int dpad_x = width * 21 / 100;
    const int dpad_y = height * 82 / 100;
    const int dpad_extent = width * 16 / 100;
    const int dpad_half_width = width * 6 / 100;
    fill_rectangle(screen, width, height, dpad_x - dpad_half_width,
                   dpad_y - dpad_extent, dpad_x + dpad_half_width,
                   dpad_y + dpad_extent, dark);
    fill_rectangle(screen, width, height, dpad_x - dpad_extent,
                   dpad_y - dpad_half_width, dpad_x + dpad_extent,
                   dpad_y + dpad_half_width, dark);

    const int radius = width * 9 / 100;
    const int a_x = width * 85 / 100;
    const int a_y = height * 76 / 100;
    const int b_x = width * 70 / 100;
    const int b_y = height * 84 / 100;
    fill_circle(screen, width, height, a_x, a_y, radius, dark);
    fill_circle(screen, width, height, b_x, b_y, radius, dark);
    draw_label(screen, width, height, a_x, a_y, "A", 10, light);
    draw_label(screen, width, height, b_x, b_y, "B", 10, light);
    if (shoulder_buttons) {
        const int shoulder_radius = width * 5 / 100;
        const int shoulder_y = height * 64 / 100;
        const int l_x = width * 70 / 100;
        const int r_x = width * 85 / 100;
        fill_circle(screen, width, height, l_x, shoulder_y,
                    shoulder_radius, dark);
        fill_circle(screen, width, height, r_x, shoulder_y,
                    shoulder_radius, dark);
        draw_label(screen, width, height, l_x, shoulder_y, "L", 5, light);
        draw_label(screen, width, height, r_x, shoulder_y, "R", 5, light);
    }

    const int menu_y = height * 94 / 100;
    const int menu_half_width = width * 8 / 100;
    const int menu_half_height = height * 3 / 100;
    const int select_x = width * 43 / 100;
    const int start_x = width * 60 / 100;
    fill_rectangle(screen, width, height, select_x - menu_half_width,
                   menu_y - menu_half_height, select_x + menu_half_width,
                   menu_y + menu_half_height, dark);
    fill_rectangle(screen, width, height, start_x - menu_half_width,
                   menu_y - menu_half_height, start_x + menu_half_width,
                   menu_y + menu_half_height, dark);
    draw_label(screen, width, height, select_x, menu_y, "SELECT", 3, light);
    draw_label(screen, width, height, start_x, menu_y, "START", 3, light);

    const int system_x = width * 52 / 100;
    const int system_y = height * 75 / 100;
    const int system_half_width = width * 8 / 100;
    const int system_half_height = height * 2 / 100;
    fill_rectangle(screen, width, height, system_x - system_half_width,
                   system_y - system_half_height, system_x + system_half_width,
                   system_y + system_half_height, dark);
    draw_label(screen, width, height, system_x, system_y, "MENU", 3, light);
}

void draw_menu(std::vector<uint32_t> &screen, unsigned width,
               unsigned height, bool developer, unsigned volume) {
    (void)developer;
    const uint32_t dark = 0xFF000000U;
    const uint32_t light = 0xFFFFFFFFU;
    screen.assign(static_cast<size_t>(width) * height, light);
    draw_label(screen, width, height, width / 2, height * 12 / 100,
               "KO BOY", 10, dark);

    const int left = width * 18 / 100;
    const int right = width * 82 / 100;
    const int half_height = height * 3 / 100;
    const int resume_y = height * 27 / 100;
    const int state_y = height * 37 / 100;
    const int settings_y = height * 47 / 100;
    const int volume_y = height * 57 / 100;
    const int reset_y = height * 67 / 100;
    const int select_y = height * 77 / 100;
    const int exit_y = height * 87 / 100;
    const int state_gap = width * 1 / 100;
    const int state_middle = width / 2;
    fill_rectangle(screen, width, height, left, resume_y - half_height,
                   right, resume_y + half_height, dark);
    fill_rectangle(screen, width, height, left, state_y - half_height,
                   state_middle - state_gap, state_y + half_height, dark);
    fill_rectangle(screen, width, height, state_middle + state_gap,
                   state_y - half_height, right, state_y + half_height, dark);
    fill_rectangle(screen, width, height, left, reset_y - half_height,
                   right, reset_y + half_height, dark);
    fill_rectangle(screen, width, height, left, select_y - half_height,
                   right, select_y + half_height, dark);
    fill_rectangle(screen, width, height, left, settings_y - half_height,
                   right, settings_y + half_height, dark);
    fill_rectangle(screen, width, height, left, volume_y - half_height,
                   right, volume_y + half_height, dark);
    fill_rectangle(screen, width, height, left, exit_y - half_height,
                   right, exit_y + half_height, dark);
    draw_label(screen, width, height, width / 2, resume_y,
               "RESUME GAME", 6, light);
    draw_label(screen, width, height, (left + state_middle - state_gap) / 2,
               state_y, "LOAD STATE", 4, light);
    draw_label(screen, width, height, (state_middle + state_gap + right) / 2,
               state_y, "SAVE STATE", 4, light);
    draw_label(screen, width, height, width / 2, reset_y,
               "RESET GAME", 6, light);
    draw_label(screen, width, height, width / 2, select_y,
               "CHANGE GAME", 6, light);
    draw_label(screen, width, height, width / 2, settings_y,
               "DISPLAY SETTINGS", 5, light);
    char volume_label[32];
    std::snprintf(volume_label, sizeof(volume_label),
                  "< VOLUME: %u/20 >", volume);
    draw_label(screen, width, height, width / 2, volume_y,
               volume_label, 5, light);
    draw_label(screen, width, height, width / 2, exit_y,
               "EXIT TO NICKEL", 5, light);
}

std::string full_refresh_frequency_name(unsigned seconds) {
    if (!seconds)
        return "OFF";
    char text[24];
    std::snprintf(text, sizeof(text), "%uS", seconds);
    return text;
}

std::string target_refresh_rate_name(unsigned quarters) {
    if (!quarters)
        return "UNLIMITED";
    char text[24];
    const unsigned remainder = quarters % 4U;
    if (!remainder)
        std::snprintf(text, sizeof(text), "%u HZ", quarters / 4U);
    else
        std::snprintf(text, sizeof(text), "%u.%s HZ", quarters / 4U,
                      remainder == 1U ? "25" :
                      remainder == 2U ? "5" : "75");
    return text;
}

void draw_display_settings(std::vector<uint32_t> &screen, unsigned width,
                           unsigned height,
                           const DisplaySettings &settings,
                           bool developer) {
    const uint32_t dark = 0xFF000000U;
    const uint32_t light = 0xFFFFFFFFU;
    screen.assign(static_cast<size_t>(width) * height, light);
    draw_label(screen, width, height, width / 2, height * 10 / 100,
               "DISPLAY SETTINGS", 6, dark);
    const int left = width * 8 / 100;
    const int right = width * 92 / 100;
    const int half_height = height * 4 / 100;
    const unsigned developer_centers[] = {30U, 50U, 70U};
    const std::string developer_rows[] = {
        std::string("FULL REFRESH  ") +
            full_refresh_frequency_name(settings.full_refresh_seconds),
        std::string("GRID EFFECT  ") +
            (settings.grid_effect ? "ON" : "OFF"),
        std::string("TARGET REFRESH  ") +
            target_refresh_rate_name(settings.target_refresh_quarters)
    };
    const unsigned user_centers[] = {36U, 60U};
    const std::string user_rows[] = {
        std::string("FULL REFRESH  ") +
            full_refresh_frequency_name(settings.full_refresh_seconds),
        std::string("GRID EFFECT  ") +
            (settings.grid_effect ? "ON" : "OFF")
    };
    const unsigned *centers = developer ? developer_centers : user_centers;
    const std::string *rows = developer ? developer_rows : user_rows;
    const unsigned count = developer ? 3U : 2U;
    for (unsigned i = 0U; i < count; ++i) {
        const int center = height * centers[i] / 100;
        fill_rectangle(screen, width, height, left, center - half_height,
                       right, center + half_height, dark);
        draw_label(screen, width, height, width / 2, center,
                   rows[i].c_str(), 4, light);
    }
    draw_label(screen, width, height, width / 2, height * 91 / 100,
               "BACK", 5, dark);
}

void draw_game_list(std::vector<uint32_t> &screen, unsigned width,
                    unsigned height, const std::vector<std::string> &roms,
                    const std::string &system_name, std::size_t page) {
    const uint32_t dark = 0xFF000000U;
    const uint32_t light = 0xFFFFFFFFU;
    const std::size_t page_size = 7U;
    screen.assign(static_cast<size_t>(width) * height, light);
    std::string heading = uppercase(system_name);
    if (heading.size() > 36U)
        heading = heading.substr(0, 33U) + "...";
    draw_label(screen, width, height, width / 2, height * 8 / 100,
               heading.c_str(), 6, dark);

    const int left = width * 6 / 100;
    const int right = width * 94 / 100;
    const int row_top = height * 16 / 100;
    const int row_height = height * 9 / 100;
    const std::size_t first = page * page_size;
    if (roms.empty()) {
        std::string message = "NO " + uppercase(system_name) + " GAMES FOUND";
        if (message.size() > 44U)
            message = message.substr(0, 41U) + "...";
        draw_label(screen, width, height, width / 2, height * 48 / 100,
                   message.c_str(), 4, dark);
    } else {
        for (std::size_t row = 0; row < page_size && first + row < roms.size();
             ++row) {
            const int top = row_top + static_cast<int>(row) * row_height;
            const int bottom = top + row_height - height / 100;
            fill_rectangle(screen, width, height, left, top, right, bottom,
                           dark);
            std::string title = rom_title(roms[first + row]);
            const std::size_t maximum = 48U;
            if (title.size() > maximum)
                title = title.substr(0, maximum - 3U) + "...";
            draw_label(screen, width, height, width / 2,
                       top + (bottom - top) / 2, title.c_str(), 3, light);
        }
    }

    const int footer_y = height * 91 / 100;
    draw_label(screen, width, height, width / 6, footer_y,
               "PREVIOUS", 4, dark);
    draw_label(screen, width, height, width / 2, footer_y,
               "BACK", 4, dark);
    draw_label(screen, width, height, width * 5 / 6, footer_y,
               "NEXT", 4, dark);
}

void draw_system_list(std::vector<uint32_t> &screen, unsigned width,
                      unsigned height,
                      const std::vector<SystemFolder> &systems,
                      std::size_t page, bool allow_cancel) {
    const uint32_t dark = 0xFF000000U;
    const uint32_t light = 0xFFFFFFFFU;
    const std::size_t page_size = 7U;
    screen.assign(static_cast<size_t>(width) * height, light);
    draw_label(screen, width, height, width / 2, height * 8 / 100,
               "SELECT SYSTEM", 7, dark);
    const int left = width * 6 / 100;
    const int right = width * 94 / 100;
    const int row_top = height * 16 / 100;
    const int row_height = height * 9 / 100;
    const std::size_t first = page * page_size;
    if (systems.empty()) {
        draw_label(screen, width, height, width / 2, height * 48 / 100,
                   "NO SYSTEM FOLDERS FOUND", 5, dark);
    } else {
        for (std::size_t row = 0;
             row < page_size && first + row < systems.size(); ++row) {
            const int top = row_top + static_cast<int>(row) * row_height;
            const int bottom = top + row_height - height / 100;
            fill_rectangle(screen, width, height, left, top, right, bottom,
                           dark);
            std::string name = systems[first + row].name;
            if (name.size() > 48U)
                name = name.substr(0, 45U) + "...";
            draw_label(screen, width, height, width / 2,
                       top + (bottom - top) / 2, name.c_str(), 4, light);
        }
    }
    const int footer_y = height * 91 / 100;
    draw_label(screen, width, height, width / 6, footer_y,
               "PREVIOUS", 4, dark);
    draw_label(screen, width, height, width / 2, footer_y,
               allow_cancel ? "BACK" : "EXIT", 4, dark);
    draw_label(screen, width, height, width * 5 / 6, footer_y,
               "NEXT", 4, dark);
}

void draw_loading_screen(std::vector<uint32_t> &screen, unsigned width,
                         unsigned height, const std::string &rom_path) {
    const uint32_t dark = 0xFF202020U;
    const uint32_t light = 0xFFFFFFFFU;
    screen.assign(static_cast<size_t>(width) * height, light);
    draw_label(screen, width, height, width / 2, height * 42 / 100,
               "LOADING", 9, dark);
    std::string title = rom_title(rom_path);
    const std::size_t maximum = 48U;
    if (title.size() > maximum)
        title = title.substr(0, maximum - 3U) + "...";
    draw_label(screen, width, height, width / 2, height * 54 / 100,
               title.c_str(), 3, dark);
}

std::string choose_game(koboy::FramebufferBackend &framebuffer,
                        koboy::TouchInput &input,
                        std::vector<uint32_t> &screen, uint32_t &marker,
                        bool allow_cancel) {
    const std::size_t page_size = 7U;
    bool entering_menu = !allow_cancel;
    const auto present_browser_screen = [&]() {
        framebuffer.draw_screen(screen);
        if (entering_menu)
            framebuffer.refresh_screen_gc16(marker++, true);
        else
            framebuffer.refresh_screen_du(marker++, true);
        entering_menu = false;
        input.suppress_input_until_release();
    };
    for (;;) {
        const std::vector<SystemFolder> systems = list_systems();
        const std::size_t system_page_count = systems.empty() ? 1U :
            (systems.size() + page_size - 1U) / page_size;
        std::size_t system_page = 0U;
        std::size_t selected_system = systems.size();
        for (;;) {
            if (interrupted)
                return std::string();
            draw_system_list(screen, framebuffer.variable_info().xres,
                             framebuffer.variable_info().yres, systems,
                             system_page, allow_cancel);
            present_browser_screen();
            for (;;) {
                if (interrupted)
                    return std::string();
                const koboy::UiAction action =
                    input.take_ui_action(koboy::UI_SCREEN_GAME_LIST);
                if (action == koboy::UI_LIST_PREVIOUS) {
                    if (system_page > 0U)
                        --system_page;
                    break;
                }
                if (action == koboy::UI_LIST_NEXT) {
                    if (system_page + 1U < system_page_count)
                        ++system_page;
                    break;
                }
                if (action == koboy::UI_LIST_CANCEL) {
                    input.wait_for_release();
                    return std::string();
                }
                if (action >= koboy::UI_GAME_0 &&
                    action <= koboy::UI_GAME_6) {
                    const std::size_t row = static_cast<std::size_t>(
                        action - koboy::UI_GAME_0);
                    const std::size_t index = system_page * page_size + row;
                    if (index < systems.size()) {
                        selected_system = index;
                        break;
                    }
                }
                usleep(20000);
            }
            if (selected_system < systems.size())
                break;
        }

        const SystemFolder &system = systems[selected_system];
        const std::vector<std::string> roms = list_roms(system.path);
        const std::size_t game_page_count = roms.empty() ? 1U :
            (roms.size() + page_size - 1U) / page_size;
        std::size_t game_page = 0U;
        bool return_to_systems = false;
        while (!return_to_systems) {
            draw_game_list(screen, framebuffer.variable_info().xres,
                           framebuffer.variable_info().yres, roms,
                           system.name, game_page);
            present_browser_screen();
            for (;;) {
                if (interrupted)
                    return std::string();
                const koboy::UiAction action =
                    input.take_ui_action(koboy::UI_SCREEN_GAME_LIST);
                if (action == koboy::UI_LIST_PREVIOUS) {
                    if (game_page > 0U)
                        --game_page;
                    break;
                }
                if (action == koboy::UI_LIST_NEXT) {
                    if (game_page + 1U < game_page_count)
                        ++game_page;
                    break;
                }
                if (action == koboy::UI_LIST_CANCEL) {
                    return_to_systems = true;
                    break;
                }
                if (action >= koboy::UI_GAME_0 &&
                    action <= koboy::UI_GAME_6) {
                    const std::size_t row = static_cast<std::size_t>(
                        action - koboy::UI_GAME_0);
                    const std::size_t index = game_page * page_size + row;
                    if (index < roms.size())
                        return roms[index];
                }
                usleep(20000);
            }
        }
    }
}

bool refresh_menu_screen(
        koboy::FramebufferBackend &framebuffer,
        koboy::TouchInput &input, uint32_t marker,
        bool diagnostics, bool full_refresh) {
    if (diagnostics)
        record_menu_refresh("begin", marker, -1.0);
    const double elapsed = full_refresh
        ? framebuffer.refresh_screen_gc16(marker, true)
        : framebuffer.refresh_screen_du(marker, true);
    if (diagnostics)
        record_menu_refresh("end", marker, elapsed);
    input.suppress_input_until_release();
    if (elapsed >= 4.5) {
        std::cerr << "warning: menu refresh approached HWTCON timeout ("
                  << elapsed << " seconds); closing refresh settings\n";
        return false;
    }
    return true;
}

bool drain_gameplay_refresh(
        koboy::FramebufferBackend &framebuffer,
        koboy::TouchInput &input, uint32_t marker,
        bool diagnostics, double &elapsed) {
    if (diagnostics)
        record_menu_refresh("drain_begin", marker, -1.0);
    elapsed = framebuffer.wait_for_update(marker);
    if (diagnostics)
        record_menu_refresh("drain_end", marker, elapsed);
    input.suppress_input_until_release();
    if (elapsed >= 4.5) {
        std::cerr << "warning: gameplay refresh drain approached HWTCON "
                  << "timeout; menu open cancelled\n";
        return false;
    }
    return true;
}

double pause_for_sleep(
        koboy::FramebufferBackend &framebuffer,
        koboy::TouchInput &input,
        const std::vector<uint32_t> &screen,
        const std::vector<uint32_t> &scaled,
        uint32_t marker, bool diagnostics) {
    const double paused_at = monotonic_seconds();
    if (diagnostics && sleep_requested)
        record_simple_breadcrumb("sleep", marker);
    input.suppress_input_until_release();
    sleep_requested = 0;
    while (!interrupted && !wake_requested)
        usleep(20000);
    if (interrupted)
        return monotonic_seconds() - paused_at;
    wake_requested = 0;

    /* Let Nickel finish its wake transition before reclaiming the display. */
    usleep(300000);
    input.suppress_input_until_release();
    framebuffer.draw_screen(screen);
    if (!scaled.empty())
        framebuffer.draw(scaled);
    framebuffer.refresh_screen_gc16(marker, true);
    if (diagnostics)
        record_simple_breadcrumb("wake", marker);
    return monotonic_seconds() - paused_at;
}

} // namespace

int main(int argc, char **argv) {
    try {
        const Options options = parse_options(argc, argv);
        const bool supervised = std::getenv("KOBOY_SUPERVISED") != 0;
        const bool developer = file_exists(kDeveloperFlag);
        const bool diagnostics = file_exists(kDiagnosticsFlag);
        const bool no_gameplay_refresh =
            file_exists(kNoGameplayRefreshFlag);
        const bool no_touch = file_exists(kNoTouchFlag);
        DisplaySettings display_settings;
        load_display_settings(display_settings);
        unsigned audio_volume = load_audio_volume();
        ensure_directory(kKoBoyRoot);
        ensure_directory(kKoBoyBiosDirectory);
        std::unique_ptr<koboy::EmulatorCore> selected_core =
            koboy::create_default_core();
        koboy::EmulatorCore &core = *selected_core;
        core.set_audio_volume(audio_volume);

        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);
        std::signal(SIGHUP, handle_signal);
        std::signal(SIGUSR1, handle_sleep_signal);
        std::signal(SIGUSR2, handle_wake_signal);

        koboy::TouchInput input(1072U, 1448U);
        if (!no_touch)
            input.open_device();
        koboy::FramebufferBackend framebuffer;
        framebuffer.open_device("/dev/fb0");
        if (!supervised)
            framebuffer.save_screen();

        std::vector<uint32_t> rgb;
        std::vector<uint32_t> scaled;
        std::vector<uint32_t> previous_rgb;
        uint32_t marker = 1U;
        const unsigned screen_width = framebuffer.variable_info().xres;
        const unsigned screen_height = framebuffer.variable_info().yres;
        std::vector<uint32_t> screen(
            static_cast<size_t>(screen_width) * screen_height, 0xFFFFFFFFU);
        PidFile pid_file;

        std::string rom_path = options.rom;
        if (rom_path.empty())
            rom_path = choose_game(framebuffer, input, screen, marker, false);
        if (rom_path.empty()) {
            if (!supervised)
                framebuffer.restore(marker++);
            return interrupted ? 130 : 0;
        }
        if (options.rom.empty()) {
            draw_loading_screen(screen, screen_width, screen_height, rom_path);
            framebuffer.draw_screen(screen);
            if (!no_gameplay_refresh)
                framebuffer.refresh_screen_gc16(marker++, true);
        }
        const std::string load_path = loadable_rom_path(rom_path);
        std::vector<unsigned char> rom = read_file(load_path.c_str());
        std::string core_error;
        if (!core.load_rom(load_path, rom, core_error))
            throw std::runtime_error(core_error);
        const unsigned horizontal_scale =
            framebuffer.variable_info().xres / core.video_width();
        const unsigned vertical_scale =
            framebuffer.variable_info().yres / core.video_height();
        unsigned effective_scale = std::min(
            options.scale, std::min(horizontal_scale, vertical_scale));
        if (effective_scale == 0U)
            throw std::runtime_error("ROM video size does not fit the display");
        framebuffer.configure_viewport(core.video_width() * effective_scale,
                                       core.video_height() * effective_scale);
        bool shoulder_buttons = core.video_width() == 240U &&
                                core.video_height() == 160U;
        input.set_shoulder_buttons_enabled(shoulder_buttons);
        std::string automatic_state = state_path(rom_path.c_str(), 0U);
        std::string manual_state = state_path(rom_path.c_str(), 1U);
        std::string sram_path = save_path(rom_path.c_str());
        bool sram_loaded = load_sram(core, sram_path);
        bool sram_saved = false;
        bool automatic_state_loaded = load_state(core, automatic_state);
        bool automatic_state_saved = false;

        draw_controls(screen, screen_width, screen_height, shoulder_buttons);
        framebuffer.draw_screen(screen);
        if (!no_gameplay_refresh)
            framebuffer.refresh_screen_gc16(marker++, true);

        std::cout << "scale=" << effective_scale << '\n';
        std::cout << "touch_x_range=" << input.x_min() << ',' << input.x_max() << '\n';
        std::cout << "touch_y_range=" << input.y_min() << ',' << input.y_max() << '\n';

        const double started = monotonic_seconds();
        double deadline = options.seconds ? started + options.seconds : 0.0;
        double next_display = started;
        double next_full_refresh = display_settings.full_refresh_seconds ?
            started + display_settings.full_refresh_seconds : 0.0;
        double emulation_accumulator = 1.0;
        double last_emulation_time = started;
        unsigned emulated_frames = 0U;
        unsigned displayed_frames = 0U;
        unsigned last_buttons = 0U;
        unsigned input_transitions = 0U;
        unsigned input_nonzero_polls = 0U;
        unsigned refresh_counts[REFRESH_TYPE_COUNT] = { 0U, 0U };
        uint32_t last_submitted_marker = marker - 1U;
        uint32_t last_completed_marker = marker - 1U;
        RefreshType last_submitted_type = REFRESH_GC16_FULL;
        double last_wait_seconds = 0.0;
        double next_heartbeat = started + 5.0;
        CpuUsageSampler cpu_usage;
        unsigned system_cpu_tenths = 0U;
        unsigned process_cpu_tenths = 0U;
        cpu_usage.sample(system_cpu_tenths, process_cpu_tenths);
        bool exit_to_nickel = false;
        bool force_first_frame_gc16 = !no_gameplay_refresh;
        std::vector<uint32_t> a2_markers;

        while (!interrupted &&
               (!options.seconds || monotonic_seconds() < deadline)) {
            const double emulation_time = monotonic_seconds();
            emulation_accumulator +=
                (emulation_time - last_emulation_time) * core.frame_rate();
            last_emulation_time = emulation_time;
            unsigned frames_this_update = static_cast<unsigned>(emulation_accumulator);
            emulation_accumulator -= frames_this_update;
            while (frames_this_update-- != 0U) {
                input.advance_frame();
                if (!no_gameplay_refresh &&
                    input.take_ui_action(koboy::UI_SCREEN_GAME) ==
                    koboy::UI_OPEN_MENU) {
                    const double menu_started = monotonic_seconds();
                    core.set_audio_paused(true);
                    if (diagnostics)
                        record_simple_breadcrumb("menu_press", marker - 1U);
                    double drain_elapsed = 0.0;
                    if (!drain_gameplay_refresh(framebuffer, input,
                                                marker - 1U, diagnostics,
                                                drain_elapsed)) {
                        core.set_audio_paused(false);
                        next_display = monotonic_seconds();
                        continue;
                    }
                    last_completed_marker = marker - 1U;
                    last_wait_seconds = drain_elapsed;
                    a2_markers.clear();
                    framebuffer.read_viewport(scaled);
                    draw_menu(screen, screen_width, screen_height, developer,
                              audio_volume);
                    framebuffer.draw_screen(screen);
                    koboy::UiAction menu_action =
                        koboy::UI_NONE;
                    bool menu_refresh_healthy =
                        refresh_menu_screen(framebuffer, input, marker++,
                                            diagnostics, true);
                    if (!menu_refresh_healthy)
                        menu_action = koboy::UI_RESUME_GAME;
                    while (!interrupted) {
                        while (!interrupted && menu_action == koboy::UI_NONE) {
                            menu_action = input.take_ui_action(developer
                                ? koboy::UI_SCREEN_MENU_DEVELOPER
                                : koboy::UI_SCREEN_MENU);
                            if (menu_action == koboy::UI_NONE)
                                usleep(20000);
                        }
                        if (menu_action != koboy::UI_VOLUME_DOWN &&
                            menu_action != koboy::UI_VOLUME_UP)
                            break;
                        if (menu_action == koboy::UI_VOLUME_DOWN) {
                            if (audio_volume > 0U)
                                --audio_volume;
                        } else if (audio_volume < 20U) {
                            ++audio_volume;
                        }
                        core.set_audio_volume(audio_volume);
                        save_audio_volume(audio_volume);
                        draw_menu(screen, screen_width, screen_height,
                                  developer, audio_volume);
                        framebuffer.draw_screen(screen);
                        refresh_menu_screen(framebuffer, input, marker++,
                                            diagnostics, false);
                        menu_action = koboy::UI_NONE;
                    }
                    if (menu_action == koboy::UI_DISPLAY_SETTINGS) {
                        const bool previous_grid = display_settings.grid_effect;
                        koboy::UiAction settings_action = koboy::UI_NONE;
                        while (!interrupted &&
                               settings_action != koboy::UI_LIST_CANCEL) {
                            draw_display_settings(screen, screen_width,
                                                  screen_height,
                                                  display_settings,
                                                  developer);
                            framebuffer.draw_screen(screen);
                            refresh_menu_screen(framebuffer, input, marker++,
                                                diagnostics, false);
                            settings_action = koboy::UI_NONE;
                            while (!interrupted &&
                                   settings_action == koboy::UI_NONE) {
                                settings_action = input.take_ui_action(
                                    developer ?
                                    koboy::UI_SCREEN_DISPLAY_SETTINGS_DEVELOPER :
                                    koboy::UI_SCREEN_DISPLAY_SETTINGS);
                                if (settings_action == koboy::UI_NONE)
                                    usleep(20000);
                            }
                            if (settings_action ==
                                koboy::UI_SETTING_FULL_REFRESH)
                                cycle_full_refresh_frequency(display_settings);
                            else if (settings_action ==
                                     koboy::UI_SETTING_GRID_EFFECT)
                                display_settings.grid_effect =
                                    !display_settings.grid_effect;
                            else if (settings_action ==
                                     koboy::UI_SETTING_TARGET_REFRESH)
                                cycle_target_refresh_rate(display_settings);
                            if (settings_action != koboy::UI_LIST_CANCEL) {
                                save_display_settings(display_settings);
                                settings_action = koboy::UI_NONE;
                            }
                        }
                        save_display_settings(display_settings);
                        if (display_settings.grid_effect != previous_grid) {
                            scaled.clear();
                            previous_rgb.clear();
                            force_first_frame_gc16 = true;
                        }
                        menu_action = koboy::UI_RESUME_GAME;
                    }
                    if (menu_action == koboy::UI_EXIT_TO_NICKEL) {
                        exit_to_nickel = true;
                        break;
                    }
                    if (interrupted)
                        break;
                    if (menu_action == koboy::UI_SAVE_STATE) {
                        if (!save_state(core, manual_state))
                            std::cerr << "warning: manual save state failed\n";
                        sram_saved = save_sram(core, sram_path);
                    } else if (menu_action == koboy::UI_LOAD_STATE) {
                        if (load_state(core, manual_state)) {
                            scaled.clear();
                            previous_rgb.clear();
                            force_first_frame_gc16 = true;
                        } else {
                            std::cerr << "warning: no usable manual save state\n";
                        }
                    } else if (menu_action == koboy::UI_RESET_GAME) {
                        core.reset();
                        scaled.clear();
                        previous_rgb.clear();
                        force_first_frame_gc16 = true;
                    }
                    if (menu_action == koboy::UI_SELECT_GAME) {
                        automatic_state_saved = save_state(core, automatic_state);
                        sram_saved = save_sram(core, sram_path);
                        const std::string selected = choose_game(
                            framebuffer, input, screen, marker, true);
                        if (!selected.empty()) {
                            draw_loading_screen(screen, screen_width,
                                                screen_height, selected);
                            framebuffer.draw_screen(screen);
                            framebuffer.refresh_screen_gc16(marker++, true);
                            const std::string selected_load_path =
                                loadable_rom_path(selected);
                            std::vector<unsigned char> selected_rom =
                                read_file(selected_load_path.c_str());
                            if (!core.load_rom(selected_load_path, selected_rom,
                                               core_error))
                                throw std::runtime_error(core_error);
                            const unsigned selected_horizontal_scale =
                                screen_width / core.video_width();
                            const unsigned selected_vertical_scale =
                                screen_height / core.video_height();
                            effective_scale = std::min(
                                options.scale,
                                std::min(selected_horizontal_scale,
                                         selected_vertical_scale));
                            if (effective_scale == 0U)
                                throw std::runtime_error(
                                    "ROM video size does not fit the display");
                            framebuffer.configure_viewport(
                                core.video_width() * effective_scale,
                                core.video_height() * effective_scale);
                            shoulder_buttons = core.video_width() == 240U &&
                                               core.video_height() == 160U;
                            input.set_shoulder_buttons_enabled(
                                shoulder_buttons);
                            rom.swap(selected_rom);
                            rom_path = selected;
                            automatic_state = state_path(rom_path.c_str(), 0U);
                            manual_state = state_path(rom_path.c_str(), 1U);
                            sram_path = save_path(rom_path.c_str());
                            sram_loaded = load_sram(core, sram_path);
                            automatic_state_loaded =
                                load_state(core, automatic_state);
                            automatic_state_saved = false;
                            scaled.clear();
                            previous_rgb.clear();
                            force_first_frame_gc16 = true;
                        }
                    }
                    const double paused_for = monotonic_seconds() - menu_started;
                    if (options.seconds)
                        deadline += paused_for;
                    screen.assign(static_cast<size_t>(screen_width) *
                                  screen_height, 0xFFFFFFFFU);
                    draw_controls(screen, screen_width, screen_height,
                                  shoulder_buttons);
                    if (menu_refresh_healthy) {
                        framebuffer.clear_screen(0xFFFFFFFFU);
                        menu_refresh_healthy = refresh_menu_screen(
                            framebuffer, input, marker++, diagnostics, true);
                    }
                    framebuffer.draw_screen(screen);
                    if (!scaled.empty())
                        framebuffer.draw(scaled);
                    if (menu_refresh_healthy)
                        refresh_menu_screen(framebuffer, input, marker++,
                                            diagnostics, true);
                    scaled.clear();
                    const double menu_resumed = monotonic_seconds();
                    next_display = menu_resumed;
                    next_full_refresh = display_settings.full_refresh_seconds ?
                        menu_resumed + display_settings.full_refresh_seconds :
                        0.0;
                    last_emulation_time = menu_resumed;
                    emulation_accumulator = 0.0;
                    frames_this_update = 0U;
                    core.set_audio_paused(false);
                }
                if (sleep_requested || wake_requested) {
                    core.set_audio_paused(true);
                    frames_this_update = 0U;
                    emulation_accumulator = 0.0;
                    for (std::size_t i = 0; i < a2_markers.size(); ++i)
                        framebuffer.wait_for_update(a2_markers[i]);
                    a2_markers.clear();
                    framebuffer.read_viewport(scaled);
                    const double paused_for = pause_for_sleep(
                        framebuffer, input, screen, scaled, marker++,
                        diagnostics);
                    scaled.clear();
                    if (options.seconds)
                        deadline += paused_for;
                    const double resumed_at = monotonic_seconds();
                    last_emulation_time = resumed_at;
                    next_display = resumed_at;
                    next_full_refresh = display_settings.full_refresh_seconds ?
                        resumed_at + display_settings.full_refresh_seconds :
                        0.0;
                    core.set_audio_paused(false);
                    continue;
                }
                const unsigned current_buttons = input.buttons();
                if (current_buttons != last_buttons) {
                    ++input_transitions;
                    last_buttons = current_buttons;
                }
                if (current_buttons != 0U)
                    ++input_nonzero_polls;
                core.run_frame(current_buttons, rgb,
                               frames_this_update == 0U);
                ++emulated_frames;
            }
            if (exit_to_nickel || interrupted)
                break;
            for (std::size_t i = 0; i < a2_markers.size(); ++i) {
                last_wait_seconds =
                    framebuffer.wait_for_update(a2_markers[i]);
                last_completed_marker = a2_markers[i];
            }
            a2_markers.clear();
            koboy::scale_rgb32_dithered_bichrome(
                rgb, core.video_width(), core.video_height(),
                effective_scale, previous_rgb,
                framebuffer,
                display_settings.grid_effect);

            const double now = monotonic_seconds();
            bool submitted_refresh = false;
            if (force_first_frame_gc16) {
                last_submitted_marker = marker++;
                last_submitted_type = REFRESH_GC16_FULL;
                ++refresh_counts[REFRESH_GC16_FULL];
                if (diagnostics)
                    record_gameplay_refresh("begin", last_submitted_marker,
                                            last_submitted_type, -1.0);
                last_wait_seconds = framebuffer.refresh_gc16(
                    last_submitted_marker, true);
                if (diagnostics)
                    record_gameplay_refresh("end", last_submitted_marker,
                                            last_submitted_type,
                                            last_wait_seconds);
                last_completed_marker = last_submitted_marker;
                force_first_frame_gc16 = false;
                last_emulation_time = monotonic_seconds();
                emulation_accumulator = 0.0;
                next_full_refresh = display_settings.full_refresh_seconds ?
                    last_emulation_time + display_settings.full_refresh_seconds :
                    0.0;
                submitted_refresh = true;
            } else if (no_gameplay_refresh) {
                /*
                 * Isolation mode deliberately keeps emulation, input, scaling,
                 * and framebuffer writes active while issuing no HWTCON ioctl.
                 */
            } else if (next_full_refresh > 0.0 && now >= next_full_refresh) {
                last_submitted_marker = marker++;
                last_submitted_type = REFRESH_GC16_FULL;
                ++refresh_counts[REFRESH_GC16_FULL];
                last_wait_seconds = framebuffer.refresh_gc16(
                    last_submitted_marker, true);
                last_completed_marker = last_submitted_marker;
                next_full_refresh =
                    monotonic_seconds() + display_settings.full_refresh_seconds;
                submitted_refresh = true;
            } else {
                const uint32_t a2_marker = marker++;
                last_submitted_marker = a2_marker;
                last_submitted_type = REFRESH_A2;
                ++refresh_counts[REFRESH_A2];
                last_wait_seconds = framebuffer.refresh_a2(a2_marker, false);
                a2_markers.push_back(a2_marker);
                submitted_refresh = true;
            }
            if (diagnostics && now >= next_heartbeat) {
                cpu_usage.sample(system_cpu_tenths, process_cpu_tenths);
                record_gameplay_heartbeat(
                    last_submitted_marker, last_submitted_type,
                    last_completed_marker, last_wait_seconds, refresh_counts,
                    system_cpu_tenths, process_cpu_tenths,
                    core.audio_frames_queued(),
                    core.audio_frames_dropped());
                next_heartbeat = now + 5.0;
            }
            if (submitted_refresh)
                ++displayed_frames;
            const double after_refresh = monotonic_seconds();
            if (display_settings.target_refresh_quarters) {
                next_display += 4.0 /
                    display_settings.target_refresh_quarters;
                if (next_display < after_refresh)
                    next_display = after_refresh;
                sleep_until(next_display);
            } else {
                next_display = after_refresh;
            }
        }

        for (std::size_t i = 0; i < a2_markers.size(); ++i)
            framebuffer.wait_for_update(a2_markers[i]);
        a2_markers.clear();
        const double elapsed = monotonic_seconds() - started;
        automatic_state_saved = save_state(core, automatic_state);
        sram_saved = save_sram(core, sram_path);
        if (exit_to_nickel)
            input.wait_for_release();
        if (!supervised)
            framebuffer.restore(marker++);
        if (!supervised)
            framebuffer.refresh_screen_gc16(marker++, true);
        std::cout << "name=Ko Boy\n";
        std::cout << "rom_directory=" << kKoBoyRomDirectory << '\n';
        std::cout << "state0_loaded="
                  << (automatic_state_loaded ? "yes" : "no") << '\n';
        std::cout << "state0_saved="
                  << (automatic_state_saved ? "yes" : "no") << '\n';
        std::cout << "core=" << core.name() << '\n';
        std::cout << "sram_bytes=" << core.save_ram_size() << '\n';
        std::cout << "sram_loaded=" << (sram_loaded ? "yes" : "no") << '\n';
        std::cout << "sram_saved=" << (sram_saved ? "yes" : "no") << '\n';
        std::cout << "rom_bytes=" << rom.size() << '\n';
        std::cout << "elapsed_seconds=" << elapsed << '\n';
        std::cout << "emulated_frames=" << emulated_frames << '\n';
        std::cout << "displayed_frames=" << displayed_frames << '\n';
        std::cout << "audio_frames_queued="
                  << core.audio_frames_queued() << '\n';
        std::cout << "audio_frames_dropped="
                  << core.audio_frames_dropped() << '\n';
        std::cout << "input_transitions=" << input_transitions << '\n';
        std::cout << "input_nonzero_polls=" << input_nonzero_polls << '\n';
        std::cout << "result="
                  << (interrupted ? "interrupted" :
                      (exit_to_nickel ? "exit_to_nickel" : "pass")) << '\n';
        return interrupted ? 130 : 0;
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
