// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace Fooyin {
class SettingsManager;
class ActionManager;
}
class QAction;

bool setSessionLockEnabled(Fooyin::SettingsManager* settings, bool enabled);
bool checkGlobalHotkeys(Fooyin::ActionManager* actions, QAction* toggle);
