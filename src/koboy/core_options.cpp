/* SPDX-License-Identifier: GPL-2.0-only */

#include "core_options.h"

#include <cctype>
#include <cstring>

namespace koboy {

CoreOptions::CoreOptions(const std::string &core_id) : updated_(false) {
    if (core_id == "armgba") {
        /* Ko Boy owns BIOS selection; these are intentionally not UI options. */
        policy_["armgba_skip_bios"] = "disabled";
        policy_["armgba_bios"] = "nintendo";
        policy_["armgba_gb_boot_rom"] = "enabled";
    }
}

void CoreOptions::register_default(const char *key, const char *value) {
    if (!key || !*key || !value || !*value)
        return;
    const std::map<std::string, std::string>::const_iterator policy =
        policy_.find(key);
    values_[key] = policy != policy_.end() ? policy->second : value;
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
        register_default(variable->key,
                         std::string(first, end ? end : first + std::strlen(first))
                             .c_str());
    }
}

void CoreOptions::register_definitions(
    const retro_core_option_definition *definitions) {
    if (!definitions)
        return;
    for (const retro_core_option_definition *option = definitions;
         option->key; ++option)
        register_default(option->key, option->default_value);
}

void CoreOptions::register_definitions_v2(
    const retro_core_option_v2_definition *definitions) {
    if (!definitions)
        return;
    for (const retro_core_option_v2_definition *option = definitions;
         option->key; ++option)
        register_default(option->key, option->default_value);
}

const char *CoreOptions::value(const char *key) const {
    if (!key)
        return 0;
    const std::map<std::string, std::string>::const_iterator found =
        values_.find(key);
    if (found != values_.end())
        return found->second.c_str();
    const std::map<std::string, std::string>::const_iterator policy =
        policy_.find(key);
    return policy != policy_.end() ? policy->second.c_str() : 0;
}

bool CoreOptions::take_updated() {
    const bool result = updated_;
    updated_ = false;
    return result;
}

} // namespace koboy
