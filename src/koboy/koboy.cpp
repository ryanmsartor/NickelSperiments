/*
 * Copyright (C) 2026 NickelSperiments contributors
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "core_factory.h"
#include "frame_scaler.h"
#include "framebuffer_backend.h"
#include "touch_input.h"

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
const char *const kKoBoyStateDirectory = "/mnt/onboard/KoBoy/States";
const char *const kKoBoySaveDirectory = "/mnt/onboard/KoBoy/Saves";
const char *const kSystemFoldersInitialized =
    "/mnt/onboard/KoBoy/.system-folders-initialized";
const char *const kDeveloperFlag = "/mnt/onboard/KoBoy/developer";
const char *const kDiagnosticsFlag = "/mnt/onboard/KoBoy/diagnostics";
const char *const kNoGameplayRefreshFlag =
    "/mnt/onboard/KoBoy/isolate-no-gameplay-refresh";
const char *const kNoTouchFlag = "/mnt/onboard/KoBoy/isolate-no-touch";
const char *const kRefreshConfig = "/mnt/onboard/KoBoy/refresh.conf";
const char *const kDisplayConfig = "/mnt/onboard/KoBoy/display.conf";
const char *const kRefreshBreadcrumb =
    "/mnt/onboard/KoBoy/refresh-active.log";
const char *const kPidPath = "/tmp/koboy.pid";
volatile sig_atomic_t interrupted = 0;
volatile sig_atomic_t sleep_requested = 0;
volatile sig_atomic_t wake_requested = 0;

enum RefreshType {
    REFRESH_A2,
    REFRESH_DU,
    REFRESH_GL16,
    REFRESH_GC16_PARTIAL,
    REFRESH_GC16_FULL,
    REFRESH_TYPE_COUNT
};

const unsigned kSlowRefreshTypeCount = 4U;
const double kFastRefreshInterval = 0.1;
const unsigned kDamageTileSize = 24U;
const unsigned kIntermediateTileSize = 24U;
const unsigned kA2AgingTileEquivalents = 8U;
const unsigned kCleanupStableTicks = 2U;
const unsigned kUrgentCleanupTileEquivalents = 4U;
const unsigned kCleanupStartsPerTick = 5U;
const unsigned kMaximumConcurrentCleanups = 20U;
const unsigned kProgressiveCleanupStartsPerTick = 8U;
const unsigned kProgressiveMaximumConcurrentCleanups = 32U;
const double kCleanupRetirementAge = 0.30;

struct ActiveCleanup {
    uint32_t marker;
    double submitted_at;
    koboy::Viewport region;
    RefreshType type;
};

class TileDamageTracker {
public:
    TileDamageTracker()
        : width_(0U), height_(0U), columns_(0U), rows_(0U),
          tile_size_(kDamageTileSize) {}

    void set_tile_size(unsigned tile_size) {
        tile_size_ = tile_size ? tile_size : 1U;
        width_ = height_ = columns_ = rows_ = 0U;
        previous_.clear();
    }

    unsigned tile_size() const { return tile_size_; }

    void reset(const std::vector<uint32_t> &frame,
               unsigned width, unsigned height) {
        width_ = width;
        height_ = height;
        columns_ = (width + tile_size_ - 1U) / tile_size_;
        rows_ = (height + tile_size_ - 1U) / tile_size_;
        previous_ = frame;
        gc16_debt_.assign(
            static_cast<std::size_t>(columns_) * rows_, 0ULL);
        du_debt_.assign(static_cast<std::size_t>(columns_) * rows_, 0ULL);
        a2_aging_debt_.assign(
            static_cast<std::size_t>(columns_) * rows_, 0ULL);
        stable_ticks_.assign(
            static_cast<std::size_t>(columns_) * rows_, 0U);
    }

    void observe(const std::vector<uint32_t> &frame,
                 unsigned width, unsigned height) {
        if (frame.size() != static_cast<std::size_t>(width) * height)
            throw std::runtime_error("damage frame dimensions do not match data");
        if (width != width_ || height != height_ ||
            previous_.size() != frame.size()) {
            reset(frame, width, height);
            return;
        }
        std::vector<bool> changed(
            static_cast<std::size_t>(columns_) * rows_, false);
        for (unsigned y = 0U; y < height; ++y) {
            for (unsigned x = 0U; x < width; ++x) {
                const std::size_t pixel = static_cast<std::size_t>(y) * width + x;
                const unsigned before = previous_[pixel] & 0xFFU;
                const unsigned after = frame[pixel] & 0xFFU;
                if (before == after)
                    continue;
                const std::size_t tile =
                    static_cast<std::size_t>(y / tile_size_) * columns_ +
                    x / tile_size_;
                changed[tile] = true;
                const bool before_endpoint = before == 0U || before == 255U;
                const bool after_endpoint = after == 0U || after == 255U;
                const unsigned magnitude = 1U +
                    (before > after ? before - after : after - before);
                if (!after_endpoint)
                    gc16_debt_[tile] += magnitude;
                else if (!before_endpoint)
                    du_debt_[tile] += magnitude;
                else
                    a2_aging_debt_[tile] += 1ULL;
            }
        }
        for (std::size_t tile = 0U; tile < changed.size(); ++tile) {
            if (changed[tile])
                stable_ticks_[tile] = 0U;
            else if (stable_ticks_[tile] < kCleanupStableTicks)
                ++stable_ticks_[tile];
        }
        previous_ = frame;
    }

    std::vector<koboy::Viewport> take_highest_rectangles(
            unsigned maximum,
            const std::vector<koboy::Viewport> &excluded,
            RefreshType type, bool combined_debt = false,
            bool ignore_stability = false,
            bool include_a2_aging = true) {
        std::vector<bool> blocked(
            static_cast<std::size_t>(columns_) * rows_, false);
        for (std::size_t i = 0U; i < excluded.size(); ++i) {
            const unsigned first_column =
                excluded[i].x / tile_size_;
            const unsigned first_row = excluded[i].y / tile_size_;
            const unsigned last_column = std::min(
                columns_,
                (excluded[i].x + excluded[i].width + tile_size_ - 1U) /
                    tile_size_);
            const unsigned last_row = std::min(
                rows_,
                (excluded[i].y + excluded[i].height + tile_size_ - 1U) /
                    tile_size_);
            for (unsigned row = first_row; row < last_row; ++row)
                for (unsigned column = first_column;
                     column < last_column; ++column)
                    blocked[static_cast<std::size_t>(row) * columns_ +
                            column] = true;
        }
        std::vector<koboy::Viewport> selected;
        for (unsigned count = 0U; count < maximum; ++count) {
            struct Candidate {
                bool valid;
                unsigned left;
                unsigned top;
                unsigned right;
                unsigned bottom;
                unsigned long long score;
            };
            Candidate best = {false, 0U, 0U, 0U, 0U, 0ULL};
            std::vector<bool> usable(columns_);
            std::vector<unsigned long long> column_debt(columns_);
            for (unsigned top = 0U; top < rows_; ++top) {
                std::fill(usable.begin(), usable.end(), true);
                std::fill(column_debt.begin(), column_debt.end(), 0ULL);
                for (unsigned bottom = top; bottom < rows_; ++bottom) {
                    for (unsigned column = 0U; column < columns_; ++column) {
                        const std::size_t cell =
                            static_cast<std::size_t>(bottom) * columns_ +
                            column;
                        const unsigned long long aging_debt =
                            include_a2_aging && a2_aging_debt_[cell] >=
                                static_cast<unsigned long long>(tile_size_) *
                                tile_size_ * kA2AgingTileEquivalents ?
                            a2_aging_debt_[cell] : 0ULL;
                        const unsigned long long waveform_debt =
                            combined_debt ?
                                gc16_debt_[cell] + du_debt_[cell] :
                            type == REFRESH_GC16_FULL ? gc16_debt_[cell] :
                            du_debt_[cell] + aging_debt;
                        const unsigned long long cell_debt =
                            blocked[cell] ||
                            (!combined_debt && type == REFRESH_DU &&
                             gc16_debt_[cell]) ||
                            (!combined_debt && !ignore_stability &&
                             type == REFRESH_GC16_FULL &&
                             stable_ticks_[cell] < kCleanupStableTicks &&
                             waveform_debt <
                                static_cast<unsigned long long>(tile_size_) *
                                tile_size_ * 256ULL *
                                kUrgentCleanupTileEquivalents) ?
                            0ULL : waveform_debt;
                        if (!cell_debt)
                            usable[column] = false;
                        if (usable[column])
                            column_debt[column] += cell_debt;
                    }
                    unsigned column = 0U;
                    while (column < columns_) {
                        while (column < columns_ && !usable[column])
                            ++column;
                        const unsigned left = column;
                        unsigned long long score = 0ULL;
                        while (column < columns_ && usable[column]) {
                            score += column_debt[column];
                            ++column;
                        }
                        if (left != column &&
                            (!best.valid || score > best.score)) {
                            best.valid = true;
                            best.left = left;
                            best.top = top;
                            best.right = column;
                            best.bottom = bottom + 1U;
                            best.score = score;
                        }
                    }
                }
            }
            if (!best.valid)
                break;
            const unsigned x = best.left * tile_size_;
            const unsigned y = best.top * tile_size_;
            koboy::Viewport region = {
                x, y,
                std::min(width_, best.right * tile_size_) - x,
                std::min(height_, best.bottom * tile_size_) - y
            };
            selected.push_back(region);
            for (unsigned row = best.top; row < best.bottom; ++row)
                for (unsigned column = best.left;
                     column < best.right; ++column)
                    {
                        const std::size_t cell =
                            static_cast<std::size_t>(row) * columns_ + column;
                        if (type == REFRESH_GC16_FULL) {
                            gc16_debt_[cell] = 0ULL;
                            du_debt_[cell] = 0ULL;
                            a2_aging_debt_[cell] = 0ULL;
                        } else {
                            du_debt_[cell] = 0ULL;
                            a2_aging_debt_[cell] = 0ULL;
                        }
                    }
        }
        return selected;
    }

    std::vector<koboy::Viewport> take_affected_rectangles() {
        struct Bounds {
            bool valid;
            unsigned left;
            unsigned top;
            unsigned right;
            unsigned bottom;
        };
        const auto find_bounds = [&](bool split_columns, unsigned split,
                                     bool lower) {
            Bounds bounds = {false, columns_, rows_, 0U, 0U};
            for (unsigned row = 0U; row < rows_; ++row)
                for (unsigned column = 0U; column < columns_; ++column) {
                    const bool selected = split_columns ?
                        (lower ? column < split : column >= split) :
                        (lower ? row < split : row >= split);
                    const std::size_t cell =
                        static_cast<std::size_t>(row) * columns_ + column;
                    /* Pure black/white motion remains A2-only. */
                    if (!selected ||
                        (!gc16_debt_[cell] && !du_debt_[cell]))
                        continue;
                    bounds.valid = true;
                    bounds.left = std::min(bounds.left, column);
                    bounds.top = std::min(bounds.top, row);
                    bounds.right = std::max(bounds.right, column + 1U);
                    bounds.bottom = std::max(bounds.bottom, row + 1U);
                }
            return bounds;
        };
        const auto area = [](const Bounds &bounds) {
            return bounds.valid ?
                static_cast<unsigned long long>(bounds.right - bounds.left) *
                    (bounds.bottom - bounds.top) : 0ULL;
        };

        Bounds best_first = find_bounds(true, columns_, true);
        if (!best_first.valid)
            return std::vector<koboy::Viewport>();
        Bounds best_second = {false, 0U, 0U, 0U, 0U};
        unsigned long long best_area = area(best_first);
        for (unsigned axis = 0U; axis < 2U; ++axis) {
            const bool split_columns = axis == 0U;
            const unsigned limit = split_columns ? columns_ : rows_;
            for (unsigned split = 1U; split < limit; ++split) {
                const Bounds first =
                    find_bounds(split_columns, split, true);
                const Bounds second =
                    find_bounds(split_columns, split, false);
                if (!first.valid || !second.valid)
                    continue;
                const unsigned long long combined = area(first) + area(second);
                if (combined < best_area) {
                    best_area = combined;
                    best_first = first;
                    best_second = second;
                }
            }
        }

        std::vector<koboy::Viewport> regions;
        const Bounds bounds[] = {best_first, best_second};
        for (unsigned i = 0U; i < 2U; ++i) {
            if (!bounds[i].valid)
                continue;
            const unsigned x = bounds[i].left * tile_size_;
            const unsigned y = bounds[i].top * tile_size_;
            regions.push_back(koboy::Viewport{
                x, y,
                std::min(width_, bounds[i].right * tile_size_) - x,
                std::min(height_, bounds[i].bottom * tile_size_) - y
            });
            for (unsigned row = bounds[i].top;
                 row < bounds[i].bottom; ++row)
                for (unsigned column = bounds[i].left;
                     column < bounds[i].right; ++column) {
                    const std::size_t cell =
                        static_cast<std::size_t>(row) * columns_ + column;
                    gc16_debt_[cell] = du_debt_[cell] =
                        a2_aging_debt_[cell] = 0ULL;
                }
        }
        return regions;
    }

    std::vector<koboy::Viewport> take_highest_cells(
            unsigned maximum,
            const std::vector<koboy::Viewport> &excluded) {
        std::vector<bool> blocked(
            static_cast<std::size_t>(columns_) * rows_, false);
        for (std::size_t i = 0U; i < excluded.size(); ++i) {
            const unsigned left = excluded[i].x / tile_size_;
            const unsigned top = excluded[i].y / tile_size_;
            const unsigned right = std::min(
                columns_, (excluded[i].x + excluded[i].width +
                           tile_size_ - 1U) / tile_size_);
            const unsigned bottom = std::min(
                rows_, (excluded[i].y + excluded[i].height +
                        tile_size_ - 1U) / tile_size_);
            for (unsigned row = top; row < bottom; ++row)
                for (unsigned column = left; column < right; ++column)
                    blocked[static_cast<std::size_t>(row) * columns_ +
                            column] = true;
        }
        std::vector<koboy::Viewport> selected;
        while (selected.size() < maximum) {
            std::size_t best = blocked.size();
            unsigned long long best_debt = 0ULL;
            for (std::size_t cell = 0U; cell < blocked.size(); ++cell) {
                const unsigned long long debt = gc16_debt_[cell] +
                    du_debt_[cell] + a2_aging_debt_[cell];
                if (!blocked[cell] && debt > best_debt) {
                    best = cell;
                    best_debt = debt;
                }
            }
            if (best == blocked.size())
                break;
            const unsigned column = static_cast<unsigned>(best % columns_);
            const unsigned row = static_cast<unsigned>(best / columns_);
            const unsigned x = column * tile_size_;
            const unsigned y = row * tile_size_;
            selected.push_back(koboy::Viewport{
                x, y, std::min(tile_size_, width_ - x),
                std::min(tile_size_, height_ - y)
            });
            gc16_debt_[best] = du_debt_[best] =
                a2_aging_debt_[best] = 0ULL;
            blocked[best] = true;
        }
        return selected;
    }

private:
    unsigned width_;
    unsigned height_;
    unsigned columns_;
    unsigned rows_;
    unsigned tile_size_;
    std::vector<uint32_t> previous_;
    std::vector<unsigned long long> gc16_debt_;
    std::vector<unsigned long long> du_debt_;
    std::vector<unsigned long long> a2_aging_debt_;
    std::vector<unsigned> stable_ticks_;
};

std::vector<koboy::Viewport> regions_outside_tiles(
        unsigned width, unsigned height,
        const std::vector<koboy::Viewport> &locked, unsigned tile_size) {
    const unsigned columns =
        (width + tile_size - 1U) / tile_size;
    const unsigned rows =
        (height + tile_size - 1U) / tile_size;
    std::vector<bool> blocked(static_cast<std::size_t>(columns) * rows, false);
    for (std::size_t i = 0U; i < locked.size(); ++i) {
        const unsigned first_column = locked[i].x / tile_size;
        const unsigned first_row = locked[i].y / tile_size;
        const unsigned last_column = std::min(
            columns, (locked[i].x + locked[i].width + tile_size - 1U) /
                     tile_size);
        const unsigned last_row = std::min(
            rows, (locked[i].y + locked[i].height + tile_size - 1U) /
                  tile_size);
        for (unsigned row = first_row; row < last_row; ++row)
            for (unsigned column = first_column;
                 column < last_column; ++column)
                blocked[static_cast<std::size_t>(row) * columns + column] =
                    true;
    }

    std::vector<koboy::Viewport> regions;
    for (unsigned row = 0U; row < rows; ++row) {
        unsigned column = 0U;
        while (column < columns) {
            while (column < columns &&
                   blocked[static_cast<std::size_t>(row) * columns + column])
                ++column;
            const unsigned first = column;
            while (column < columns &&
                   !blocked[static_cast<std::size_t>(row) * columns + column])
                ++column;
            if (first == column)
                continue;
            const unsigned x = first * tile_size;
            const unsigned right = std::min(width, column * tile_size);
            const unsigned y = row * tile_size;
            const unsigned row_height = std::min(tile_size, height - y);
            bool extended = false;
            for (std::size_t i = 0U; i < regions.size(); ++i) {
                if (regions[i].x == x && regions[i].width == right - x &&
                    regions[i].y + regions[i].height == y) {
                    regions[i].height += row_height;
                    extended = true;
                    break;
                }
            }
            if (!extended) {
                koboy::Viewport region = {x, y, right - x, row_height};
                regions.push_back(region);
            }
        }
    }
    return regions;
}

struct RefreshSettings {
    RefreshType slow_type;
    RefreshType slowest_type;
    unsigned slow_tenths_hz;
    RefreshSettings()
        : slow_type(REFRESH_DU), slowest_type(REFRESH_GC16_FULL),
          slow_tenths_hz(20U) {}
};

enum DisplayRefreshPolicy {
    DISPLAY_REFRESH_PROGRESSIVE,
    DISPLAY_REFRESH_INTERMEDIATE,
    DISPLAY_REFRESH_AGGRESSIVE,
    DISPLAY_REFRESH_LAZY,
    DISPLAY_REFRESH_POLICY_COUNT
};

struct DisplaySettings {
    DisplayRefreshPolicy refresh_policy;
    unsigned full_refresh_seconds;
    bool grid_effect;
    unsigned bichrome_threshold;
    DisplaySettings()
        : refresh_policy(DISPLAY_REFRESH_AGGRESSIVE),
          full_refresh_seconds(0U), grid_effect(false),
          bichrome_threshold(0U) {}
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
        case REFRESH_DU: return "DU";
        case REFRESH_GL16: return "GL16";
        case REFRESH_GC16_PARTIAL: return "GC16 PARTIAL";
        case REFRESH_GC16_FULL: return "GC16 FULL FLASH";
        default: return "UNKNOWN";
    }
}

bool load_refresh_settings_file(const char *path, RefreshSettings &settings) {
    std::ifstream input(path);
    if (!input)
        return false;
    std::string line;
    unsigned recognized = 0U;
    while (std::getline(input, line)) {
        const std::string::size_type equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        const std::string key = line.substr(0, equals);
        const std::string encoded = line.substr(equals + 1U);
        char *end = 0;
        errno = 0;
        const unsigned long parsed = std::strtoul(encoded.c_str(), &end, 10);
        if (errno || !end || *end != '\0' || parsed > UINT_MAX)
            continue;
        const unsigned value = static_cast<unsigned>(parsed);
        if (key == "slow_type" && value < kSlowRefreshTypeCount) {
            settings.slow_type = static_cast<RefreshType>(value);
            recognized |= 1U;
        } else if (key == "slowest_type" &&
                   value >= REFRESH_GL16 && value <= REFRESH_GC16_FULL) {
            settings.slowest_type = static_cast<RefreshType>(value);
            recognized |= 2U;
        } else if (key == "slow_tenths_hz" &&
                 (value == 0U || value == 1U || value == 2U ||
                  value == 5U || value == 10U || value == 20U)) {
            settings.slow_tenths_hz = value;
            recognized |= 4U;
        }
    }
    return recognized == 7U;
}

void load_refresh_settings(RefreshSettings &settings) {
    const std::string temporary = std::string(kRefreshConfig) + ".tmp";
    const std::string backup = std::string(kRefreshConfig) + ".bak";
    RefreshSettings candidate;
    if (load_refresh_settings_file(kRefreshConfig, candidate) ||
        load_refresh_settings_file(temporary.c_str(), candidate) ||
        load_refresh_settings_file(backup.c_str(), candidate))
        settings = candidate;
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
    const unsigned values[] = {0U, 2U, 5U, 10U, 15U, 30U, 60U};
    for (unsigned i = 0U; i < sizeof(values) / sizeof(values[0]); ++i)
        if (value == values[i])
            return true;
    return false;
}

bool valid_bichrome_threshold(unsigned value) {
    const unsigned values[] = {0U, 25U, 33U, 50U, 67U, 75U};
    for (unsigned i = 0U; i < sizeof(values) / sizeof(values[0]); ++i)
        if (value == values[i])
            return true;
    return false;
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
        if (key == "refresh_policy" && value < DISPLAY_REFRESH_POLICY_COUNT)
            settings.refresh_policy = static_cast<DisplayRefreshPolicy>(value);
        else if (key == "full_refresh_seconds" &&
                 valid_full_refresh_seconds(value))
            settings.full_refresh_seconds = value;
        else if (key == "grid_effect" && value <= 1U)
            settings.grid_effect = value != 0U;
        else if (key == "bichrome_threshold" &&
                 valid_bichrome_threshold(value))
            settings.bichrome_threshold = value;
    }
}

void save_display_settings(const DisplaySettings &settings) {
    if (!ensure_directory(kKoBoyRoot))
        return;
    char text[160];
    const int length = std::snprintf(
        text, sizeof(text),
        "refresh_policy=%u\nfull_refresh_seconds=%u\ngrid_effect=%u\n"
        "bichrome_threshold=%u\n",
        static_cast<unsigned>(settings.refresh_policy),
        settings.full_refresh_seconds, settings.grid_effect ? 1U : 0U,
        settings.bichrome_threshold);
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

void cycle_display_refresh_policy(DisplaySettings &settings) {
    switch (settings.refresh_policy) {
        case DISPLAY_REFRESH_PROGRESSIVE:
            settings.refresh_policy = DISPLAY_REFRESH_LAZY;
            break;
        case DISPLAY_REFRESH_LAZY:
            settings.refresh_policy = DISPLAY_REFRESH_INTERMEDIATE;
            break;
        case DISPLAY_REFRESH_INTERMEDIATE:
            settings.refresh_policy = DISPLAY_REFRESH_AGGRESSIVE;
            break;
        default:
            settings.refresh_policy = DISPLAY_REFRESH_PROGRESSIVE;
            break;
    }
}

void cycle_full_refresh_frequency(DisplaySettings &settings) {
    const unsigned values[] = {0U, 2U, 5U, 10U, 15U, 30U, 60U};
    unsigned index = 0U;
    while (index + 1U < sizeof(values) / sizeof(values[0]) &&
           values[index] != settings.full_refresh_seconds)
        ++index;
    settings.full_refresh_seconds =
        values[(index + 1U) % (sizeof(values) / sizeof(values[0]))];
}

void cycle_bichrome_threshold(DisplaySettings &settings) {
    const unsigned values[] = {0U, 25U, 33U, 50U, 67U, 75U};
    unsigned index = 0U;
    while (index + 1U < sizeof(values) / sizeof(values[0]) &&
           values[index] != settings.bichrome_threshold)
        ++index;
    settings.bichrome_threshold =
        values[(index + 1U) % (sizeof(values) / sizeof(values[0]))];
}

void apply_bichrome(std::vector<uint32_t> &pixels, unsigned threshold) {
    if (!threshold)
        return;
    const unsigned split = (threshold * 255U + 50U) / 100U;
    for (std::size_t i = 0U; i < pixels.size(); ++i)
        pixels[i] = (pixels[i] & 0xFFU) < split ?
            0xFF000000U : 0xFFFFFFFFU;
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

std::string serialize_refresh_settings(const RefreshSettings &settings) {
    char contents[128];
    const int length = std::snprintf(
        contents, sizeof(contents),
        "slow_type=%u\nslowest_type=%u\nslow_tenths_hz=%u\n",
        static_cast<unsigned>(settings.slow_type),
        static_cast<unsigned>(settings.slowest_type),
        settings.slow_tenths_hz);
    return length > 0 ? std::string(contents, static_cast<std::size_t>(length))
                      : std::string();
}

void sync_koboy_directory() {
    const int directory = open(kKoBoyRoot, O_RDONLY | O_DIRECTORY);
    if (directory >= 0) {
        fsync(directory);
        close(directory);
    }
}

void save_refresh_settings(const RefreshSettings &settings) {
    if (!ensure_directory(kKoBoyRoot))
        return;
    const std::string temporary = std::string(kRefreshConfig) + ".tmp";
    const std::string backup = std::string(kRefreshConfig) + ".bak";
    const std::string contents = serialize_refresh_settings(settings);
    const int output = open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC,
                            0644);
    bool durable = output >= 0 && write_all(output, contents) &&
                   fsync(output) == 0;
    if (output >= 0 && close(output) != 0)
        durable = false;
    if (!durable) {
        std::cerr << "warning: cannot durably write refresh settings\n";
        return;
    }
    unlink(backup.c_str());
    if (rename(kRefreshConfig, backup.c_str()) != 0 && errno != ENOENT) {
        std::cerr << "warning: cannot back up refresh settings: "
                  << std::strerror(errno) << '\n';
        return;
    }
    if (rename(temporary.c_str(), kRefreshConfig) != 0) {
        std::cerr << "warning: cannot install refresh settings: "
                  << std::strerror(errno) << '\n';
        rename(backup.c_str(), kRefreshConfig);
        return;
    }
    sync_koboy_directory();
}

void record_active_refresh_settings(const RefreshSettings &settings) {
    if (!ensure_directory(kKoBoyRoot))
        return;
    const SystemMetrics metrics = read_system_metrics();
    char entry[384];
    const int length = std::snprintf(
        entry, sizeof(entry),
        "active pid=%ld uptime=%.3f fast=A2 fast_hz=10.0 slow=%s "
        "slow_hz=%.1f slowest=%s mem_available_kb=%llu rss_kb=%llu cpu_khz=%llu "
        "cpu_temp_mc=%lld battery_temp_mc=%lld ap_temp_mc=%lld\n",
        static_cast<long>(getpid()), monotonic_seconds(),
        refresh_type_name(settings.slow_type),
        static_cast<double>(settings.slow_tenths_hz) / 10.0,
        refresh_type_name(settings.slowest_type),
        metrics.memory_available_kb, metrics.resident_kb,
        metrics.cpu_frequency_khz, metrics.cpu_temperature_mc,
        metrics.battery_temperature_mc, metrics.ap_temperature_mc);
    const int output = open(kRefreshBreadcrumb,
                            O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (output < 0 || length <= 0 ||
        !write_all(output, std::string(entry, static_cast<std::size_t>(length))) ||
        fsync(output) != 0)
        std::cerr << "warning: cannot record active refresh settings\n";
    if (output >= 0)
        close(output);
}

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
                               unsigned process_cpu_tenths) {
    if (!ensure_directory(kKoBoyRoot))
        return;
    const SystemMetrics metrics = read_system_metrics();
    char entry[512];
    const int length = std::snprintf(
        entry, sizeof(entry),
        "heartbeat pid=%ld uptime=%.3f submitted_marker=%u "
        "submitted_type=%s completed_marker=%u last_wait=%.3f "
        "count_a2=%u count_du=%u count_gl16=%u count_gc16_partial=%u "
        "count_gc16_full=%u system_cpu_pct=%u.%u process_cpu_pct=%u.%u "
        "mem_available_kb=%llu rss_kb=%llu "
        "cpu_khz=%llu cpu_temp_mc=%lld battery_temp_mc=%lld ap_temp_mc=%lld\n",
        static_cast<long>(getpid()), monotonic_seconds(), submitted_marker,
        refresh_type_name(submitted_type), completed_marker,
        last_wait_seconds, counts[REFRESH_A2], counts[REFRESH_DU],
        counts[REFRESH_GL16], counts[REFRESH_GC16_PARTIAL],
        counts[REFRESH_GC16_FULL], system_cpu_tenths / 10U,
        system_cpu_tenths % 10U, process_cpu_tenths / 10U,
        process_cpu_tenths % 10U, metrics.memory_available_kb,
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

void cycle_slow_type(RefreshType &type) {
    type = static_cast<RefreshType>(
        (static_cast<unsigned>(type) + 1U) % kSlowRefreshTypeCount);
}

void cycle_slowest_type(RefreshSettings &settings) {
    if (settings.slowest_type == REFRESH_GL16)
        settings.slowest_type = REFRESH_GC16_PARTIAL;
    else if (settings.slowest_type == REFRESH_GC16_PARTIAL)
        settings.slowest_type = REFRESH_GC16_FULL;
    else
        settings.slowest_type = REFRESH_GL16;
}

void cycle_slow_frequency(RefreshSettings &settings) {
    const unsigned values[] = {0U, 1U, 2U, 5U, 10U, 20U};
    for (unsigned i = 0; i < 6U; ++i)
        if (settings.slow_tenths_hz == values[i]) {
            settings.slow_tenths_hz = values[(i + 1U) % 6U];
            return;
        }
    settings.slow_tenths_hz = 10U;
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
        ".md0", ".md1", ".md2", ".md20s", ".md25s"
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
    unsigned cleanup_seconds;
    bool cleanup_set;
    Options() : scale(6), seconds(0), cleanup_seconds(1),
                cleanup_set(false) {}
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
        else if (std::strcmp(argv[i], "--cleanup-seconds") == 0) {
            options.cleanup_seconds = parse_unsigned(argv[i + 1], "--cleanup-seconds");
            options.cleanup_set = true;
        } else
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
               unsigned height, bool developer) {
    (void)developer;
    const uint32_t dark = 0xFF000000U;
    const uint32_t light = 0xFFFFFFFFU;
    screen.assign(static_cast<size_t>(width) * height, light);
    draw_label(screen, width, height, width / 2, height * 12 / 100,
               "KO BOY", 10, dark);

    const int left = width * 18 / 100;
    const int right = width * 82 / 100;
    const int half_height = height * 3 / 100;
    const int resume_y = height * 22 / 100;
    const int save_y = height * 31 / 100;
    const int load_y = height * 40 / 100;
    const int reset_y = height * 49 / 100;
    const int select_y = height * 58 / 100;
    const int settings_y = height * 67 / 100;
    const int exit_y = height * 78 / 100;
    fill_rectangle(screen, width, height, left, resume_y - half_height,
                   right, resume_y + half_height, dark);
    fill_rectangle(screen, width, height, left, save_y - half_height,
                   right, save_y + half_height, dark);
    fill_rectangle(screen, width, height, left, load_y - half_height,
                   right, load_y + half_height, dark);
    fill_rectangle(screen, width, height, left, reset_y - half_height,
                   right, reset_y + half_height, dark);
    fill_rectangle(screen, width, height, left, select_y - half_height,
                   right, select_y + half_height, dark);
    fill_rectangle(screen, width, height, left, settings_y - half_height,
                   right, settings_y + half_height, dark);
    fill_rectangle(screen, width, height, left, exit_y - half_height,
                   right, exit_y + half_height, dark);
    draw_label(screen, width, height, width / 2, resume_y,
               "RESUME GAME", 6, light);
    draw_label(screen, width, height, width / 2, save_y,
               "SAVE STATE", 6, light);
    draw_label(screen, width, height, width / 2, load_y,
               "LOAD STATE", 6, light);
    draw_label(screen, width, height, width / 2, reset_y,
               "RESET GAME", 6, light);
    draw_label(screen, width, height, width / 2, select_y,
               "SELECT GAME", 6, light);
    draw_label(screen, width, height, width / 2, settings_y,
               "DISPLAY SETTINGS", 5, light);
    draw_label(screen, width, height, width / 2, exit_y,
               "EXIT TO NICKEL", 5, light);
}

const char *display_refresh_policy_name(DisplayRefreshPolicy policy) {
    switch (policy) {
        case DISPLAY_REFRESH_PROGRESSIVE: return "PROGRESSIVE";
        case DISPLAY_REFRESH_INTERMEDIATE: return "MODERATE";
        case DISPLAY_REFRESH_AGGRESSIVE: return "AGGRESSIVE";
        case DISPLAY_REFRESH_LAZY: return "LAZY";
        default: return "UNKNOWN";
    }
}

std::string full_refresh_frequency_name(unsigned seconds) {
    if (!seconds)
        return "OFF";
    char text[24];
    std::snprintf(text, sizeof(text), "%uS", seconds);
    return text;
}

std::string bichrome_threshold_name(unsigned threshold) {
    if (!threshold)
        return "OFF";
    char text[16];
    std::snprintf(text, sizeof(text), "%u%%", threshold);
    return text;
}

void draw_display_settings(std::vector<uint32_t> &screen, unsigned width,
                           unsigned height,
                           const DisplaySettings &settings) {
    const uint32_t dark = 0xFF000000U;
    const uint32_t light = 0xFFFFFFFFU;
    screen.assign(static_cast<size_t>(width) * height, light);
    draw_label(screen, width, height, width / 2, height * 10 / 100,
               "DISPLAY SETTINGS", 6, dark);
    const int left = width * 8 / 100;
    const int right = width * 92 / 100;
    const int half_height = height * 4 / 100;
    const unsigned centers[] = {24U, 40U, 56U, 72U};
    const std::string rows[] = {
        std::string("REFRESH TYPE  ") +
            display_refresh_policy_name(settings.refresh_policy),
        std::string("FULL REFRESH  ") +
            full_refresh_frequency_name(settings.full_refresh_seconds),
        std::string("GRID EFFECT  ") +
            (settings.grid_effect ? "ON" : "OFF"),
        std::string("BICHROME  ") +
            bichrome_threshold_name(settings.bichrome_threshold)
    };
    for (unsigned i = 0U; i < 4U; ++i) {
        const int center = height * centers[i] / 100;
        fill_rectangle(screen, width, height, left, center - half_height,
                       right, center + half_height, dark);
        draw_label(screen, width, height, width / 2, center,
                   rows[i].c_str(), 4, light);
    }
    draw_label(screen, width, height, width / 2, height * 91 / 100,
               "BACK", 5, dark);
}

std::string frequency_name(unsigned tenths_hz) {
    if (!tenths_hz)
        return "OFF";
    char text[32];
    if (tenths_hz % 10U)
        std::snprintf(text, sizeof(text), "%u.%u HZ",
                      tenths_hz / 10U, tenths_hz % 10U);
    else
        std::snprintf(text, sizeof(text), "%u HZ", tenths_hz / 10U);
    return text;
}

void draw_refresh_settings(std::vector<uint32_t> &screen, unsigned width,
                           unsigned height,
                           const RefreshSettings &settings) {
    const uint32_t dark = 0xFF000000U;
    const uint32_t light = 0xFFFFFFFFU;
    screen.assign(static_cast<size_t>(width) * height, light);
    draw_label(screen, width, height, width / 2, height * 10 / 100,
               "REFRESH SETTINGS", 6, dark);
    const int left = width * 8 / 100;
    const int right = width * 92 / 100;
    const int half_height = height * 4 / 100;
    const unsigned centers[] = {28U, 46U, 64U};
    const std::string rows[] = {
        std::string("SLOW TYPE  ") + refresh_type_name(settings.slow_type),
        std::string("SLOWEST TYPE  ") +
            refresh_type_name(settings.slowest_type),
        std::string("SLOW FREQUENCY  ") +
            frequency_name(settings.slow_tenths_hz)
    };
    for (unsigned i = 0; i < 3U; ++i) {
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

double refresh_viewport(koboy::FramebufferBackend &framebuffer,
                        RefreshType type, uint32_t marker,
                        bool wait_for_completion) {
    switch (type) {
        case REFRESH_A2:
            return framebuffer.refresh_a2(marker, wait_for_completion);
        case REFRESH_DU:
            return framebuffer.refresh_du(marker, wait_for_completion);
        case REFRESH_GL16:
            return framebuffer.refresh_gl16(marker, wait_for_completion);
        case REFRESH_GC16_PARTIAL:
            return framebuffer.refresh_gc16_partial(marker,
                                                     wait_for_completion);
        case REFRESH_GC16_FULL:
            return framebuffer.refresh_gc16(marker, wait_for_completion);
        default:
            return framebuffer.refresh_a2(marker, wait_for_completion);
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
        const bool developer = false;
        const bool diagnostics = file_exists(kDiagnosticsFlag);
        const bool no_gameplay_refresh =
            file_exists(kNoGameplayRefreshFlag);
        const bool no_touch = file_exists(kNoTouchFlag);
        RefreshSettings refresh_settings;
        DisplaySettings display_settings;
        load_display_settings(display_settings);
        std::unique_ptr<koboy::EmulatorCore> selected_core =
            koboy::create_default_core();
        koboy::EmulatorCore &core = *selected_core;

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
        TileDamageTracker damage_tracker;
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
        std::vector<unsigned char> rom = read_file(rom_path.c_str());
        std::string core_error;
        if (!core.load_rom(rom_path, rom, core_error))
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
        damage_tracker.set_tile_size(
            display_settings.refresh_policy == DISPLAY_REFRESH_INTERMEDIATE ?
            kIntermediateTileSize : kDamageTileSize);
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
        double next_policy_refresh = started;
        double next_full_refresh = display_settings.full_refresh_seconds ?
            started + display_settings.full_refresh_seconds : 0.0;
        double emulation_accumulator = 1.0;
        double last_emulation_time = started;
        unsigned emulated_frames = 0U;
        unsigned displayed_frames = 0U;
        unsigned cleanups = 0U;
        unsigned last_buttons = 0U;
        unsigned input_transitions = 0U;
        unsigned input_nonzero_polls = 0U;
        unsigned refresh_counts[REFRESH_TYPE_COUNT] = { 0U, 0U, 0U, 0U, 0U };
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
        std::vector<ActiveCleanup> active_cleanups;

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
                    if (diagnostics)
                        record_simple_breadcrumb("menu_press", marker - 1U);
                    double drain_elapsed = 0.0;
                    if (!drain_gameplay_refresh(framebuffer, input,
                                                marker - 1U, diagnostics,
                                                drain_elapsed)) {
                        next_display = monotonic_seconds();
                        continue;
                    }
                    last_completed_marker = marker - 1U;
                    last_wait_seconds = drain_elapsed;
                    a2_markers.clear();
                    if (!active_cleanups.empty()) {
                        for (std::size_t i = 0;
                             i < active_cleanups.size(); ++i) {
                            last_wait_seconds =
                                framebuffer.wait_for_update(
                                    active_cleanups[i].marker);
                            last_completed_marker = active_cleanups[i].marker;
                        }
                        active_cleanups.clear();
                    }
                    draw_menu(screen, screen_width, screen_height, developer);
                    framebuffer.draw_screen(screen);
                    koboy::UiAction menu_action =
                        koboy::UI_NONE;
                    bool menu_refresh_healthy =
                        refresh_menu_screen(framebuffer, input, marker++,
                                            diagnostics, true);
                    if (!menu_refresh_healthy)
                        menu_action = koboy::UI_RESUME_GAME;
                    while (!interrupted && menu_action == koboy::UI_NONE) {
                        menu_action = input.take_ui_action(developer
                            ? koboy::UI_SCREEN_MENU_DEVELOPER
                            : koboy::UI_SCREEN_MENU);
                        if (menu_action == koboy::UI_NONE)
                            usleep(20000);
                    }
                    if (menu_action == koboy::UI_DISPLAY_SETTINGS) {
                        const bool previous_grid = display_settings.grid_effect;
                        const unsigned previous_bichrome =
                            display_settings.bichrome_threshold;
                        koboy::UiAction settings_action = koboy::UI_NONE;
                        while (!interrupted &&
                               settings_action != koboy::UI_LIST_CANCEL) {
                            draw_display_settings(screen, screen_width,
                                                  screen_height,
                                                  display_settings);
                            framebuffer.draw_screen(screen);
                            refresh_menu_screen(framebuffer, input, marker++,
                                                diagnostics, false);
                            settings_action = koboy::UI_NONE;
                            while (!interrupted &&
                                   settings_action == koboy::UI_NONE) {
                                settings_action = input.take_ui_action(
                                    koboy::UI_SCREEN_DISPLAY_SETTINGS);
                                if (settings_action == koboy::UI_NONE)
                                    usleep(20000);
                            }
                            if (settings_action ==
                                koboy::UI_SETTING_REFRESH_POLICY)
                                cycle_display_refresh_policy(display_settings);
                            else if (settings_action ==
                                     koboy::UI_SETTING_FULL_REFRESH)
                                cycle_full_refresh_frequency(display_settings);
                            else if (settings_action ==
                                     koboy::UI_SETTING_GRID_EFFECT)
                                display_settings.grid_effect =
                                    !display_settings.grid_effect;
                            else if (settings_action ==
                                     koboy::UI_SETTING_BICHROME)
                                cycle_bichrome_threshold(display_settings);
                            if (settings_action != koboy::UI_LIST_CANCEL) {
                                save_display_settings(display_settings);
                                settings_action = koboy::UI_NONE;
                            }
                        }
                        save_display_settings(display_settings);
                        damage_tracker.set_tile_size(
                            display_settings.refresh_policy ==
                                DISPLAY_REFRESH_INTERMEDIATE ?
                            kIntermediateTileSize : kDamageTileSize);
                        if (display_settings.grid_effect != previous_grid ||
                            display_settings.bichrome_threshold !=
                                previous_bichrome) {
                            scaled.clear();
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
                            force_first_frame_gc16 = true;
                        } else {
                            std::cerr << "warning: no usable manual save state\n";
                        }
                    } else if (menu_action == koboy::UI_RESET_GAME) {
                        core.reset();
                        scaled.clear();
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
                            std::vector<unsigned char> selected_rom =
                                read_file(selected.c_str());
                            if (!core.load_rom(selected, selected_rom,
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
                            damage_tracker.set_tile_size(
                                display_settings.refresh_policy ==
                                    DISPLAY_REFRESH_INTERMEDIATE ?
                                kIntermediateTileSize : kDamageTileSize);
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
                    framebuffer.draw_screen(screen);
                    if (!scaled.empty())
                        framebuffer.draw(scaled);
                    if (menu_refresh_healthy)
                        refresh_menu_screen(framebuffer, input, marker++,
                                            diagnostics, true);
                    damage_tracker.reset(
                        scaled, framebuffer.viewport().width,
                        framebuffer.viewport().height);
                    const double menu_resumed = monotonic_seconds();
                    next_display = menu_resumed;
                    next_policy_refresh = menu_resumed;
                    next_full_refresh = display_settings.full_refresh_seconds ?
                        menu_resumed + display_settings.full_refresh_seconds :
                        0.0;
                    last_emulation_time = menu_resumed;
                    emulation_accumulator = 0.0;
                    frames_this_update = 0U;
                }
                if (sleep_requested || wake_requested) {
                    frames_this_update = 0U;
                    emulation_accumulator = 0.0;
                    for (std::size_t i = 0; i < a2_markers.size(); ++i)
                        framebuffer.wait_for_update(a2_markers[i]);
                    a2_markers.clear();
                    for (std::size_t i = 0; i < active_cleanups.size(); ++i)
                        framebuffer.wait_for_update(
                            active_cleanups[i].marker);
                    active_cleanups.clear();
                    const double paused_for = pause_for_sleep(
                        framebuffer, input, screen, scaled, marker++,
                        diagnostics);
                    if (options.seconds)
                        deadline += paused_for;
                    const double resumed_at = monotonic_seconds();
                    damage_tracker.reset(
                        scaled, framebuffer.viewport().width,
                        framebuffer.viewport().height);
                    last_emulation_time = resumed_at;
                    next_display = resumed_at;
                    next_policy_refresh = resumed_at;
                    next_full_refresh = display_settings.full_refresh_seconds ?
                        resumed_at + display_settings.full_refresh_seconds :
                        0.0;
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
            koboy::scale_rgb32_grayscale(
                rgb, core.video_width(), core.video_height(),
                effective_scale, scaled, display_settings.grid_effect);
            apply_bichrome(scaled, display_settings.bichrome_threshold);
            for (std::size_t i = 0; i < a2_markers.size(); ++i) {
                last_wait_seconds =
                    framebuffer.wait_for_update(a2_markers[i]);
                last_completed_marker = a2_markers[i];
            }
            a2_markers.clear();

            double now = monotonic_seconds();
            for (std::size_t i = 0U; i < active_cleanups.size();) {
                if (now - active_cleanups[i].submitted_at <
                    kCleanupRetirementAge) {
                    ++i;
                    continue;
                }
                last_wait_seconds = framebuffer.wait_for_update(
                    active_cleanups[i].marker);
                last_completed_marker = active_cleanups[i].marker;
                active_cleanups.erase(active_cleanups.begin() + i);
                now = monotonic_seconds();
            }

            std::vector<koboy::Viewport> gc16_regions;
            for (std::size_t i = 0U; i < active_cleanups.size(); ++i)
                gc16_regions.push_back(active_cleanups[i].region);

            damage_tracker.observe(
                scaled, framebuffer.viewport().width,
                framebuffer.viewport().height);

            std::vector<koboy::Viewport> a2_regions;
            if (!active_cleanups.empty()) {
                a2_regions = regions_outside_tiles(
                    framebuffer.viewport().width,
                    framebuffer.viewport().height, gc16_regions,
                    damage_tracker.tile_size());
                for (std::size_t i = 0; i < a2_regions.size(); ++i)
                    framebuffer.draw_region(scaled, a2_regions[i]);
            } else {
                framebuffer.draw(scaled);
                a2_regions.push_back(koboy::Viewport{
                    0U, 0U, framebuffer.viewport().width,
                    framebuffer.viewport().height
                });
            }

            bool submitted_refresh = false;
            if (force_first_frame_gc16) {
                for (std::size_t i = 0; i < active_cleanups.size(); ++i) {
                    last_wait_seconds =
                        framebuffer.wait_for_update(active_cleanups[i].marker);
                    last_completed_marker = active_cleanups[i].marker;
                }
                active_cleanups.clear();
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
                damage_tracker.reset(
                    scaled, framebuffer.viewport().width,
                    framebuffer.viewport().height);
                next_policy_refresh = last_emulation_time;
                next_full_refresh = display_settings.full_refresh_seconds ?
                    last_emulation_time + display_settings.full_refresh_seconds :
                    0.0;
                submitted_refresh = true;
            } else if (no_gameplay_refresh) {
                /*
                 * Isolation mode deliberately keeps emulation, input, scaling,
                 * and framebuffer writes active while issuing no HWTCON ioctl,
                 * including during startup and the first emulated frame.
                 */
            } else {
                if (next_full_refresh > 0.0 && now >= next_full_refresh) {
                    for (std::size_t i = 0U;
                         i < active_cleanups.size(); ++i)
                        framebuffer.wait_for_update(
                            active_cleanups[i].marker);
                    active_cleanups.clear();
                    gc16_regions.clear();
                    framebuffer.draw(scaled);
                    last_submitted_marker = marker++;
                    last_submitted_type = REFRESH_GC16_FULL;
                    ++refresh_counts[REFRESH_GC16_FULL];
                    last_wait_seconds = framebuffer.refresh_gc16(
                        last_submitted_marker, true);
                    last_completed_marker = last_submitted_marker;
                    damage_tracker.reset(
                        scaled, framebuffer.viewport().width,
                        framebuffer.viewport().height);
                    next_full_refresh = now +
                        display_settings.full_refresh_seconds;
                    next_policy_refresh = now;
                    submitted_refresh = true;
                    a2_regions.clear();
                }
                const bool progressive = display_settings.refresh_policy ==
                    DISPLAY_REFRESH_PROGRESSIVE;
                const unsigned maximum_concurrent = progressive ?
                    kProgressiveMaximumConcurrentCleanups :
                    kMaximumConcurrentCleanups;
                const unsigned available = active_cleanups.size() >=
                    maximum_concurrent ? 0U :
                    static_cast<unsigned>(maximum_concurrent -
                                          active_cleanups.size());
                unsigned starts = std::min(progressive ?
                    kProgressiveCleanupStartsPerTick :
                    kCleanupStartsPerTick, available);
                std::vector<koboy::Viewport> new_gc16_cleanups;
                std::vector<koboy::Viewport> new_du_cleanups;
                if (!submitted_refresh && display_settings.refresh_policy ==
                    DISPLAY_REFRESH_PROGRESSIVE) {
                    new_gc16_cleanups =
                        damage_tracker.take_highest_rectangles(
                            starts, gc16_regions, REFRESH_GC16_FULL);
                } else if (!submitted_refresh &&
                           display_settings.refresh_policy ==
                               DISPLAY_REFRESH_INTERMEDIATE &&
                           now >= next_policy_refresh) {
                    starts = std::min(10U, available);
                    new_gc16_cleanups =
                        damage_tracker.take_highest_rectangles(
                            starts, gc16_regions, REFRESH_GC16_FULL,
                            false, true, false);
                    for (std::size_t i = 0U;
                         i < new_gc16_cleanups.size(); ++i)
                        gc16_regions.push_back(new_gc16_cleanups[i]);
                    const unsigned intermediate_remaining = starts -
                        static_cast<unsigned>(new_gc16_cleanups.size());
                    new_du_cleanups =
                        damage_tracker.take_highest_rectangles(
                            intermediate_remaining, gc16_regions, REFRESH_DU,
                            false, true, false);
                    next_policy_refresh = now + 0.5;
                } else if (!submitted_refresh &&
                           display_settings.refresh_policy ==
                               DISPLAY_REFRESH_AGGRESSIVE &&
                           now >= next_policy_refresh &&
                           active_cleanups.empty()) {
                    new_gc16_cleanups =
                        damage_tracker.take_affected_rectangles();
                    next_policy_refresh = now + 1.0;
                }
                if (display_settings.refresh_policy !=
                    DISPLAY_REFRESH_INTERMEDIATE)
                    for (std::size_t i = 0U;
                         i < new_gc16_cleanups.size(); ++i)
                        gc16_regions.push_back(new_gc16_cleanups[i]);
                const unsigned remaining_starts = starts -
                    static_cast<unsigned>(new_gc16_cleanups.size());
                if (display_settings.refresh_policy ==
                    DISPLAY_REFRESH_PROGRESSIVE)
                    new_du_cleanups =
                        damage_tracker.take_highest_rectangles(
                            remaining_starts, gc16_regions, REFRESH_DU);

                for (std::size_t i = 0U;
                     i < new_gc16_cleanups.size(); ++i) {
                    const uint32_t gc16_marker = marker++;
                    last_submitted_marker = gc16_marker;
                    last_submitted_type = REFRESH_GC16_FULL;
                    ++refresh_counts[REFRESH_GC16_FULL];
                    last_wait_seconds = framebuffer.refresh_gc16_region(
                        new_gc16_cleanups[i], gc16_marker, false);
                    ActiveCleanup cleanup = {
                        gc16_marker, now, new_gc16_cleanups[i],
                        REFRESH_GC16_FULL
                    };
                    active_cleanups.push_back(cleanup);
                    ++cleanups;
                    submitted_refresh = true;
                }
                for (std::size_t i = 0U; i < new_du_cleanups.size(); ++i) {
                    const uint32_t du_marker = marker++;
                    last_submitted_marker = du_marker;
                    last_submitted_type = REFRESH_DU;
                    ++refresh_counts[REFRESH_DU];
                    last_wait_seconds = framebuffer.refresh_du_region(
                        new_du_cleanups[i], du_marker, false);
                    ActiveCleanup cleanup = {
                        du_marker, now, new_du_cleanups[i], REFRESH_DU
                    };
                    active_cleanups.push_back(cleanup);
                    gc16_regions.push_back(new_du_cleanups[i]);
                    ++cleanups;
                    submitted_refresh = true;
                }
                if (!new_gc16_cleanups.empty() || !new_du_cleanups.empty())
                    a2_regions = regions_outside_tiles(
                        framebuffer.viewport().width,
                        framebuffer.viewport().height, gc16_regions,
                        damage_tracker.tile_size());

                for (std::size_t i = 0; i < a2_regions.size(); ++i) {
                    const uint32_t a2_marker = marker++;
                    last_submitted_marker = a2_marker;
                    last_submitted_type = REFRESH_A2;
                    ++refresh_counts[REFRESH_A2];
                    last_wait_seconds = framebuffer.refresh_a2_region(
                        a2_regions[i], a2_marker, false);
                    a2_markers.push_back(a2_marker);
                    submitted_refresh = true;
                }
            }
            if (diagnostics && now >= next_heartbeat) {
                cpu_usage.sample(system_cpu_tenths, process_cpu_tenths);
                record_gameplay_heartbeat(
                    last_submitted_marker, last_submitted_type,
                    last_completed_marker, last_wait_seconds, refresh_counts,
                    system_cpu_tenths, process_cpu_tenths);
                next_heartbeat = now + 5.0;
            }
            if (submitted_refresh)
                ++displayed_frames;
            const double after_refresh = monotonic_seconds();
            next_display += kFastRefreshInterval;
            if (next_display < after_refresh)
                next_display = after_refresh;
            sleep_until(next_display);
        }

        for (std::size_t i = 0; i < a2_markers.size(); ++i)
            framebuffer.wait_for_update(a2_markers[i]);
        a2_markers.clear();
        for (std::size_t i = 0; i < active_cleanups.size(); ++i)
            framebuffer.wait_for_update(active_cleanups[i].marker);
        active_cleanups.clear();
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
        std::cout << "cleanup_refreshes=" << cleanups << '\n';
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
