/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NICKELSPERIMENTS_CORE_FACTORY_H
#define NICKELSPERIMENTS_CORE_FACTORY_H

#include "emulator_core.h"

#include <memory>

namespace koboy {

std::unique_ptr<EmulatorCore> create_core_for_rom(const std::string &path);

} // namespace koboy

#endif
