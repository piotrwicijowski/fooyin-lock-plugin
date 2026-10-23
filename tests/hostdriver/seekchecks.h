// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
class QAction;
namespace Fooyin { class WidgetProvider; class PlayerController; }
bool checkSeekInputs(Fooyin::WidgetProvider* provider, Fooyin::PlayerController* player, QAction* toggle);
