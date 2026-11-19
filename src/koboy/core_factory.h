/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NICKELSPERIMENTS_CORE_FACTORY_H
#define NICKELSPERIMENTS_CORE_FACTORY_H

#include "emulator_core.h"

#include <memory>
#include <vector>

namespace koboy {

bool is_gamate_rom(const std::vector<unsigned char> &rom);
std::unique_ptr<EmulatorCore> create_core_for_rom(
    const std::string &path, const std::vector<unsigned char> &rom);

} // namespace koboy

#endif
