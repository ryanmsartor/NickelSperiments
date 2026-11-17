/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NICKELSPERIMENTS_CORE_OPTIONS_H
#define NICKELSPERIMENTS_CORE_OPTIONS_H

#include "libretro.h"

#include <map>
#include <string>
#include <vector>

namespace koboy {

/*
 * Owns the option strings advertised by a libretro core.  Registration does
 * not make an option user-facing: Ko Boy exposes only entries it explicitly
 * curates, while still giving every core a stable and standards-compliant
 * default value.
 */
class CoreOptions {
public:
    explicit CoreOptions(const std::string &core_id);

    void register_variables(const retro_variable *variables);
    void register_definitions(const retro_core_option_definition *definitions);
    void register_definitions_v2(
        const retro_core_option_v2_definition *definitions);

    const char *value(const char *key) const;
    bool take_updated();
    void set_visible(const char *key, bool visible);
    void set_update_display_callback(
        retro_core_options_update_display_callback_t callback);

    unsigned curated_count(bool developer) const;
    const char *curated_label(unsigned index, bool developer) const;
    const char *curated_value_label(unsigned index, bool developer) const;
    bool cycle_curated(unsigned index, bool developer);

private:
    struct Option {
        std::string value;
        std::vector<std::string> values;
        std::vector<std::string> labels;
        bool visible;
        Option() : visible(true) {}
    };
    struct Curated {
        std::string key;
        std::string label;
        bool developer_only;
        Curated(const char *option_key, const char *option_label,
                bool developer)
            : key(option_key), label(option_label),
              developer_only(developer) {}
    };

    void register_option(const char *key, const char *default_value,
                         const std::vector<std::string> &values,
                         const std::vector<std::string> &labels);
    const Curated *curated_at(unsigned index, bool developer) const;
    void load_config();
    void save_config() const;

    std::map<std::string, Option> options_;
    std::map<std::string, std::string> policy_;
    std::map<std::string, std::string> configured_;
    std::vector<Curated> curated_;
    retro_core_options_update_display_callback_t update_display_callback_;
    bool updated_;
};

} // namespace koboy

#endif
