// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace Fooyin {
class ActionManager;
class PlayerController;
class WidgetProvider;
}
class QWidget;
bool checkPlaybackInputs(Fooyin::WidgetProvider* provider, Fooyin::ActionManager* actions,
                         Fooyin::PlayerController* player, QWidget* playlist);
