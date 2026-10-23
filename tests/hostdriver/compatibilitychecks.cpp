// SPDX-License-Identifier: GPL-3.0-or-later
#include "compatibilitychecks.h"

#include <gui/guiconstants.h>
#include <utils/actions/actionmanager.h>
#include <utils/actions/command.h>
#include <utils/settings/settingsdialogcontroller.h>
#include <utils/settings/settingsmanager.h>
#include <utils/settings/settingspage.h>

#include <QApplication>
#include <QLibrary>
#include <QMessageBox>
#include <QPluginLoader>
#include <QTest>
#include <QTimer>
#include <QTreeView>

bool setSessionLockEnabled(Fooyin::SettingsManager* settings, bool enabled)
{
    auto* dialog = settings->settingsDialog();
    if(!dialog) return false;
    dialog->openAtPage(Fooyin::Constants::Page::Plugins);
    Fooyin::SettingsPageWidget* page{nullptr};
    for(auto* widget : QApplication::allWidgets()) {
        if(widget->inherits("Fooyin::PluginPageWidget")) {
            page = qobject_cast<Fooyin::SettingsPageWidget*>(widget);
            break;
        }
    }
    auto* tree = page ? page->findChild<QTreeView*>() : nullptr;
    if(!tree) return false;
    auto* model = tree->model();
    const auto matches = model->match(model->index(0, 0), Qt::DisplayRole, QStringLiteral("Session Lock"),
                                     2, Qt::MatchExactly | Qt::MatchRecursive);
    if(matches.size() != 1) return false;
    const auto check = matches.front().siblingAtColumn(3);
    if(check.data(Qt::CheckStateRole).toInt() != (enabled ? Qt::Unchecked : Qt::Checked)
       || !model->setData(check, enabled ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole)) return false;
    bool prompted{false};
    QTimer promptTimer;
    promptTimer.setSingleShot(true);
    QObject::connect(&promptTimer, &QTimer::timeout, page, [&prompted]() {
        if(auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            prompted = true;
            prompt->done(QMessageBox::No); // The runner starts the next independent host process.
        }
    });
    promptTimer.start(0);
    page->apply();
    page->window()->hide();
    const bool disabled = settings->value(QStringLiteral("Plugins/Disabled")).toStringList()
                              .contains(QStringLiteral("fooyin-lock-plugin.sessionlock"));
    return prompted && disabled == !enabled;
}

bool checkGlobalHotkeys(Fooyin::ActionManager* actions, QAction* toggle)
{
    const auto path = qEnvironmentVariable("FOOYIN_LOCK_SMOKE_GLOBAL_HOTKEYS");
    if(path.isEmpty()) return true;
    QPluginLoader loader{path};
    auto* plugin = loader.instance();
    bool initialised{false};
    if(plugin) {
        for(auto* child : plugin->children())
            initialised |= child->inherits("Fooyin::GlobalHotkeys::GlobalShortcutManager");
    }
    if(!initialised || actions->globalShortcutManagement() != Fooyin::GlobalShortcutManagement::ApplicationManaged)
        return false;

    // Exercise the actual X11 backend via server-delivered keys, without private
    // backend headers or transport requests. A late sort command is protected;
    // a separate allowed command fences delivery of each pair of native inputs.
    QLibrary x11{QStringLiteral("libX11.so.6")}, xtst{QStringLiteral("libXtst.so.6")};
    const auto open = reinterpret_cast<void* (*)(const char*)>(x11.resolve("XOpenDisplay"));
    const auto close = reinterpret_cast<int (*)(void*)>(x11.resolve("XCloseDisplay"));
    const auto keycode = reinterpret_cast<unsigned char (*)(void*, unsigned long)>(x11.resolve("XKeysymToKeycode"));
    const auto flush = reinterpret_cast<int (*)(void*)>(x11.resolve("XFlush"));
    const auto send = reinterpret_cast<int (*)(void*, unsigned int, int, unsigned long)>(xtst.resolve("XTestFakeKeyEvent"));
    if(!open || !close || !keycode || !flush || !send) return false;
    auto* display = open(nullptr);
    if(!display) return false;
    const auto play = keycode(display, 0x1008ff14); // XF86AudioPlay
    const auto stop = keycode(display, 0x1008ff15); // XF86AudioStop
    if(!play || !stop) { close(display); return false; }
    QAction protectedSource, allowedSource;
    constexpr auto protectedId = "Edit.Sort.Preset.2147483646";
    constexpr auto allowedId = "SessionLock.Smoke.NativeFence";
    auto* protectedCommand = actions->registerAction(&protectedSource, protectedId);
    auto* allowedCommand = actions->registerAction(&allowedSource, allowedId);
    protectedCommand->setGlobalShortcuts({QKeySequence{Qt::Key_MediaPlay}});
    allowedCommand->setGlobalShortcuts({QKeySequence{Qt::Key_MediaStop}});
    int protectedCount{0}, allowedCount{0};
    QObject receiver;
    QObject::connect(&protectedSource, &QAction::triggered, &receiver, [&]() { ++protectedCount; });
    QObject::connect(&allowedSource, &QAction::triggered, &receiver, [&]() { ++allowedCount; });
    QTest::qWait(100); // Let the module's queued binding synchronisation complete.
    const auto nativeBindings = [&]() {
        return protectedCommand->globalShortcuts() == Fooyin::ShortcutList{QKeySequence{Qt::Key_MediaPlay}}
            && allowedCommand->globalShortcuts() == Fooyin::ShortcutList{QKeySequence{Qt::Key_MediaStop}}
            && protectedCommand->shortcuts().empty() && allowedCommand->shortcuts().empty();
    };
    const auto press = [&]() {
        for(auto key : {play, stop}) {
            send(display, key, true, 0);
            send(display, key, false, 0);
        }
        flush(display);
    };
    press();
    bool ok = QTest::qWaitFor([&]() { return allowedCount == 1; }, 2000) && protectedCount == 1 && nativeBindings();
    toggle->trigger();
    ok &= toggle->isChecked();
    protectedSource.setEnabled(true); // The global source must withstand host refresh.
    press();
    ok &= QTest::qWaitFor([&]() { return allowedCount == 2; }, 2000) && protectedCount == 1;
    toggle->trigger();
    ok &= !toggle->isChecked();
    press();
    ok &= QTest::qWaitFor([&]() { return allowedCount == 3; }, 2000) && protectedCount == 2 && nativeBindings();
    protectedCommand->setGlobalShortcuts({});
    allowedCommand->setGlobalShortcuts({});
    actions->unregisterAction(&protectedSource, protectedId);
    actions->unregisterAction(&allowedSource, allowedId);
    close(display);
    if(ok) qInfo("Session Lock smoke: native X11 Global Hotkeys media-key dispatch, suppression, and restoration verified");
    return ok;
}
