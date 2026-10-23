// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <core/plugins/coreplugincontext.h>
#include <gui/plugins/guiplugincontext.h>

namespace Fooyin::SessionLock::Tests {
inline CorePluginContext coreContext()
{
    return {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, {}, {}, nullptr, {}};
}

inline GuiPluginContext guiContext()
{
    return {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
}
} // namespace Fooyin::SessionLock::Tests
