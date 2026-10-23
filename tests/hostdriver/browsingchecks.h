// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
class QAction;
namespace Fooyin {
struct CorePluginContext;
struct GuiPluginContext;
}
bool checkBrowsing(const Fooyin::CorePluginContext& core, const Fooyin::GuiPluginContext& gui, QAction* toggle);
