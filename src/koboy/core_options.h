/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NICKELSPERIMENTS_CORE_OPTIONS_H
#define NICKELSPERIMENTS_CORE_OPTIONS_H

#include "libretro.h"

#include <map>
#include <string>

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

private:
    void register_default(const char *key, const char *value);

    std::map<std::string, std::string> values_;
    std::map<std::string, std::string> policy_;
    bool updated_;
};

} // namespace koboy

#endif
