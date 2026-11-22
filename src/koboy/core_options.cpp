/* SPDX-License-Identifier: GPL-2.0-only */

#include "core_options.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace koboy {

namespace {
const char *const kCoreOptionsConfig = "/mnt/onboard/KoBoy/core-options.conf";
}

CoreOptions::CoreOptions(const std::string &core_id)
    : update_display_callback_(0), updated_(false) {
    load_config();
    if (core_id == "armgba") {
        /* Ko Boy owns BIOS selection; these are intentionally not UI options. */
        policy_["armgba_skip_bios"] = "disabled";
        policy_["armgba_bios"] = "nintendo";
        policy_["armgba_gb_boot_rom"] = "enabled";
        policy_["armgba_gb_tilt"] = "d-pad";
        curated_.push_back(Curated("armgba_solar_sensor", "SOLAR LEVEL",
                                   false));
        curated_.push_back(Curated("armgba_layer_selection",
                                   "LAYER SELECTION", true));
        curated_.push_back(Curated("armgba_layer_bg0", "BACKGROUND 0", true));
        curated_.push_back(Curated("armgba_layer_bg1", "BACKGROUND 1", true));
        curated_.push_back(Curated("armgba_layer_bg2", "BACKGROUND 2", true));
        curated_.push_back(Curated("armgba_layer_bg3", "BACKGROUND 3", true));
        curated_.push_back(Curated("armgba_layer_gb_background",
                                   "GB BACKGROUND", true));
        curated_.push_back(Curated("armgba_layer_gb_window", "GB WINDOW",
                                   true));
        curated_.push_back(Curated("armgba_layer_sprites", "SPRITES", true));
    } else if (core_id == "armnes") {
        curated_.push_back(Curated("armnes_ppu", "PPU ACCURACY", true));
        curated_.push_back(Curated("armnes_fds_fast", "FAST DISK LOADING",
                                   false));
        curated_.push_back(Curated("armnes_sprite_limit", "SPRITE LIMIT",
                                   false));
        curated_.push_back(Curated("armnes_layer_selection",
                                   "LAYER SELECTION", true));
        curated_.push_back(Curated("armnes_layer_background",
                                   "BACKGROUND", true));
        curated_.push_back(Curated("armnes_layer_sprites_front",
                                   "SPRITES FRONT", true));
        curated_.push_back(Curated("armnes_layer_sprites_behind",
                                   "SPRITES BEHIND", true));
    } else if (core_id == "superwhizkid") {
        policy_["superwhizkid_palette"] = "greyscale";
        policy_["superwhizkid_tap_latch"] = "on";
        policy_["superwhizkid_lcd_ghosting"] = "off";
        policy_["superwhizkid_bios"] = "umc";
        policy_["superwhizkid_skip_intro"] = "off";
        defaults_["superwhizkid_flicker_reduction"] = "1";
        curated_.push_back(Curated("superwhizkid_audio_output",
                                   "AUDIO OUTPUT", false));
        curated_.push_back(Curated("superwhizkid_flicker_reduction",
                                   "FLICKER REDUCTION", true));
        curated_.push_back(Curated("superwhizkid_contrast", "CONTRAST",
                                   true));
    }
}

void CoreOptions::register_option(const char *key, const char *default_value,
                                  const std::vector<std::string> &values,
                                  const std::vector<std::string> &labels) {
    if (!key || !*key || !default_value || !*default_value)
        return;
    Option &option = options_[key];
    option.values = values;
    option.labels = labels;
    const std::map<std::string, std::string>::const_iterator policy =
        policy_.find(key);
    const std::map<std::string, std::string>::const_iterator configured =
        configured_.find(key);
    const std::map<std::string, std::string>::const_iterator overridden_default =
        defaults_.find(key);
    const std::string wanted = policy != policy_.end() ? policy->second :
        configured != configured_.end() ? configured->second :
        overridden_default != defaults_.end() ? overridden_default->second :
        default_value;
    option.value = default_value;
    for (std::size_t i = 0; i < option.values.size(); ++i)
        if (option.values[i] == wanted) {
            option.value = wanted;
            break;
        }
}

void CoreOptions::register_variables(const retro_variable *variables) {
    if (!variables)
        return;
    for (const retro_variable *variable = variables; variable->key; ++variable) {
        if (!variable->value)
            continue;
        const char *separator = std::strchr(variable->value, ';');
        if (!separator)
            continue;
        const char *first = separator + 1;
        while (*first && std::isspace(static_cast<unsigned char>(*first)))
            ++first;
        const char *end = std::strchr(first, '|');
        std::vector<std::string> values;
        std::vector<std::string> labels;
        const char *at = first;
        while (*at) {
            const char *next = std::strchr(at, '|');
            values.push_back(std::string(at, next ? next : at + std::strlen(at)));
            labels.push_back(values.back());
            if (!next)
                break;
            at = next + 1;
        }
        register_option(variable->key,
                        std::string(first, end ? end : first + std::strlen(first))
                            .c_str(), values, labels);
    }
}

void CoreOptions::register_definitions(
    const retro_core_option_definition *definitions) {
    if (!definitions)
        return;
    for (const retro_core_option_definition *option = definitions;
         option->key; ++option)
    {
        std::vector<std::string> values;
        std::vector<std::string> labels;
        for (unsigned i = 0; option->values[i].value; ++i) {
            values.push_back(option->values[i].value);
            labels.push_back(option->values[i].label ?
                             option->values[i].label : option->values[i].value);
        }
        register_option(option->key, option->default_value, values, labels);
    }
}

void CoreOptions::register_definitions_v2(
    const retro_core_option_v2_definition *definitions) {
    if (!definitions)
        return;
    for (const retro_core_option_v2_definition *option = definitions;
         option->key; ++option)
    {
        std::vector<std::string> values;
        std::vector<std::string> labels;
        for (unsigned i = 0; option->values[i].value; ++i) {
            values.push_back(option->values[i].value);
            labels.push_back(option->values[i].label ?
                             option->values[i].label : option->values[i].value);
        }
        register_option(option->key, option->default_value, values, labels);
    }
}

const char *CoreOptions::value(const char *key) const {
    if (!key)
        return 0;
    const std::map<std::string, Option>::const_iterator found =
        options_.find(key);
    if (found != options_.end())
        return found->second.value.c_str();
    const std::map<std::string, std::string>::const_iterator policy =
        policy_.find(key);
    return policy != policy_.end() ? policy->second.c_str() : 0;
}

void CoreOptions::set_visible(const char *key, bool visible) {
    if (key)
        options_[key].visible = visible;
}

void CoreOptions::set_update_display_callback(
    retro_core_options_update_display_callback_t callback) {
    update_display_callback_ = callback;
}

const CoreOptions::Curated *CoreOptions::curated_at(unsigned index,
                                                    bool developer) const {
    for (std::size_t i = 0; i < curated_.size(); ++i) {
        const Curated &curated = curated_[i];
        const std::map<std::string, Option>::const_iterator option =
            options_.find(curated.key);
        if (option == options_.end() || !option->second.visible ||
            (curated.developer_only && !developer))
            continue;
        if (!index--)
            return &curated;
    }
    return 0;
}

unsigned CoreOptions::curated_count(bool developer) const {
    unsigned count = 0;
    while (curated_at(count, developer))
        ++count;
    return count;
}

const char *CoreOptions::curated_label(unsigned index, bool developer) const {
    const Curated *curated = curated_at(index, developer);
    return curated ? curated->label.c_str() : 0;
}

const char *CoreOptions::curated_value_label(unsigned index,
                                             bool developer) const {
    const Curated *curated = curated_at(index, developer);
    if (!curated)
        return 0;
    const Option &option = options_.find(curated->key)->second;
    for (std::size_t i = 0; i < option.values.size(); ++i)
        if (option.values[i] == option.value)
            return option.labels[i].c_str();
    return option.value.c_str();
}

bool CoreOptions::cycle_curated(unsigned index, bool developer) {
    const Curated *curated = curated_at(index, developer);
    if (!curated)
        return false;
    Option &option = options_[curated->key];
    if (option.values.empty())
        return false;
    std::size_t selected = 0;
    while (selected < option.values.size() &&
           option.values[selected] != option.value)
        ++selected;
    option.value = option.values[(selected + 1U) % option.values.size()];
    configured_[curated->key] = option.value;
    updated_ = true;
    save_config();
    if (update_display_callback_)
        update_display_callback_();
    return true;
}

void CoreOptions::load_config() {
    std::ifstream input(kCoreOptionsConfig);
    std::string line;
    while (std::getline(input, line)) {
        const std::string::size_type equals = line.find('=');
        if (equals != std::string::npos && equals != 0U)
            configured_[line.substr(0U, equals)] = line.substr(equals + 1U);
    }
}

void CoreOptions::save_config() const {
    const std::string temporary = std::string(kCoreOptionsConfig) + ".tmp";
    std::ofstream output(temporary.c_str(), std::ios::trunc);
    for (std::map<std::string, std::string>::const_iterator entry =
             configured_.begin(); entry != configured_.end(); ++entry)
        output << entry->first << '=' << entry->second << '\n';
    output.close();
    if (output)
        std::rename(temporary.c_str(), kCoreOptionsConfig);
}

bool CoreOptions::take_updated() {
    const bool result = updated_;
    updated_ = false;
    return result;
}

} // namespace koboy
