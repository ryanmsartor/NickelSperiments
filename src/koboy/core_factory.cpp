/*
 * Copyright (C) 2026 NickelSperiments contributors
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "core_factory.h"
#include "gambatte_core.h"

namespace koboy {

std::unique_ptr<EmulatorCore> create_default_core() {
    return std::unique_ptr<EmulatorCore>(new GambatteCore);
}

} // namespace koboy
