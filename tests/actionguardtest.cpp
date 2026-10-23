// SPDX-License-Identifier: GPL-3.0-or-later
#include "actionguard.h"
#include "contexts.h"
#include "lockruntime.h"
#include "playlisthost.h"

#include <gui/guiconstants.h>
#include <utils/actions/actionmanager.h>
#include <utils/actions/command.h>
#include <utils/settings/settingsmanager.h>

#include <QActionGroup>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include <array>

using namespace Fooyin;
using namespace Fooyin::SessionLock;
using namespace Fooyin::SessionLock::Tests;

namespace {
struct Host
{
    QTemporaryDir directory;
    SettingsManager settings{directory.filePath(QStringLiteral("settings.ini"))};
    QMainWindow window;
    ActionManager actions{&settings};
    RecoveryJournal journal{directory.filePath(QStringLiteral("recovery.json"))};
    Host() { actions.setMainWindow(&window); }
};
} // namespace

class ActionGuardTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void mutationRoutes_data()
    {
        QTest::addColumn<QString>("commandId");
        QTest::addColumn<QString>("contextId");
        QTest::newRow("seek") << QStringLiteral("Seek.Backward.Large") << QStringLiteral("Context.Global");
        QTest::newRow("ab-set-a") << QStringLiteral("Playback.ABRepeat.SetStart") << QStringLiteral("Context.Global");
        QTest::newRow("ab-set-b") << QStringLiteral("Playback.ABRepeat.SetEnd") << QStringLiteral("Context.Global");
        QTest::newRow("ab-enable") << QStringLiteral("Playback.ABRepeat.Enable") << QStringLiteral("Context.Global");
        QTest::newRow("ab-clear") << QStringLiteral("Playback.ABRepeat.Clear") << QStringLiteral("Context.Global");
        QTest::newRow("stop-after") << QStringLiteral("Playback.StopAfterCurrent.Reset") << QStringLiteral("Context.Global");
        QTest::newRow("queue-insert") << QStringLiteral("Tracks.QueueNext") << QStringLiteral("Context.Global");
        QTest::newRow("queue-clear") << QStringLiteral("Edit.Clear") << QStringLiteral("Context.QueueViewer.7");
        QTest::newRow("queue-remove") << QStringLiteral("Edit.Remove") << QStringLiteral("Context.QueueViewer.7");
        QTest::newRow("playlist-undo") << QStringLiteral("Edit.Undo") << QStringLiteral("Fooyin.Context.Playlist7");
        QTest::newRow("playlist-rename") << QStringLiteral("Edit.Rename") << QStringLiteral("Fooyin.Context.PlaylistManager.7");
        QTest::newRow("playlist-unlock") << QStringLiteral("Playlist.Lock") << QStringLiteral("Context.Global");
        QTest::newRow("playlist-import") << QStringLiteral("File.LoadPlaylist") << QStringLiteral("Context.Global");
        QTest::newRow("autoplaylist-query") << QStringLiteral("File.EditAutoPlaylist") << QStringLiteral("Fooyin.Context.PlaylistSwitcher.7");
        QTest::newRow("sort-preset") << QStringLiteral("Edit.Sort.Preset.42") << QStringLiteral("Fooyin.Context.Playlist7");
        QTest::newRow("organiser-sort") << QStringLiteral("PlaylistOrganiser.SortGroupPlaylists") << QStringLiteral("Context.PlaylistOrganiser.7");
    }

    void mutationRoutes()
    {
        QFETCH(QString, commandId);
        QFETCH(QString, contextId);
        Host host;
        QAction source;
        source.setCheckable(true);
        QWidget widget;
        WidgetContext context{&widget, Context{Id{contextId}}, &widget};
        host.actions.addContextObject(&context);
        auto* command = host.actions.registerAction(&source, Id{commandId}, context.context());
        QSignalSpy triggered{&source, &QAction::triggered};
        QSignalSpy toggled{&source, &QAction::toggled};
        ActionGuard guard{&host.actions};
        QString error;
        QVERIFY(guard.enter(host.journal, error));
        QVERIFY(context.isEnabled());
        source.setEnabled(true);
        command->action()->trigger();
        source.trigger();
        QCOMPARE(triggered.size(), 0);
        QCOMPARE(toggled.size(), 0);
        QVERIFY(guard.leave(error));
        command->action()->trigger();
        QCOMPARE(triggered.size(), 1);
        QCOMPARE(toggled.size(), 1);
    }

    void buttonsMenusShortcutsAndRetainedSources_data()
    {
        QTest::addColumn<int>("key");
        QTest::addColumn<Qt::KeyboardModifiers>("modifiers");
        QTest::newRow("keyboard") << int(Qt::Key_K) << Qt::KeyboardModifiers{Qt::ControlModifier};
        for(auto key : {Qt::Key_MediaPlay, Qt::Key_MediaPause, Qt::Key_MediaStop,
                        Qt::Key_MediaNext, Qt::Key_MediaPrevious, Qt::Key_MediaTogglePlayPause}) {
            const auto name = QKeySequence{key}.toString().toLatin1();
            QTest::newRow(name.constData()) << int(key) << Qt::KeyboardModifiers{Qt::NoModifier};
        }
    }

    void buttonsMenusShortcutsAndRetainedSources()
    {
        QFETCH(int, key);
        QFETCH(Qt::KeyboardModifiers, modifiers);
        Host host;
        const auto shortcut = [&]() {
            QKeyEvent press{QEvent::KeyPress, key, modifiers};
            QKeyEvent release{QEvent::KeyRelease, key, modifiers};
            QCoreApplication::sendEvent(&host.window, &press);
            QCoreApplication::sendEvent(&host.window, &release);
        };
        QAction source{QStringLiteral("Play"), &host.window};
        auto* command = host.actions.registerAction(&source, Constants::Actions::PlayPause);
        command->setDefaultShortcut(QKeySequence{QKeyCombination{modifiers, Qt::Key(key)}});
        auto* proxy = command->action();
        QSignalSpy triggered{&source, &QAction::triggered};
        QToolButton button{&host.window};
        button.setDefaultAction(proxy);
        QToolButton retainedButton{&host.window};
        retainedButton.setDefaultAction(&source);
        QMenu menu;
        menu.addAction(proxy);
        host.window.show();
        host.window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&host.window));
        shortcut();
        QCOMPARE(triggered.size(), 1);

        ActionGuard guard{&host.actions};
        QString error;
        QVERIFY(guard.enter(host.journal, error));
        QVERIFY(!source.isEnabled());
        QVERIFY(!proxy->isEnabled());
        QVERIFY(!button.isEnabled());
        QVERIFY(!retainedButton.isEnabled());
        button.click();
        retainedButton.click();
        menu.actions().front()->trigger();
        proxy->trigger();
        source.trigger(); // Same route as the Global Hotkeys context action.
        shortcut();
        QCOMPARE(triggered.size(), 1);
        QVERIFY(guard.leave(error));
        QVERIFY(source.isEnabled());
        QVERIFY(proxy->isEnabled());
        button.click();
        retainedButton.click();
        menu.actions().front()->trigger();
        shortcut();
        QCOMPARE(triggered.size(), 5);
    }

    void repeatContextMenus_data()
    {
        QTest::addColumn<bool>("createdWhileLocked");
        QTest::newRow("existing") << false;
        QTest::newRow("late") << true;
    }

    void repeatContextMenus()
    {
        QFETCH(bool, createdWhileLocked);
        Host host;
        QAction enableSource, clearSource;
        auto* enable = host.actions.registerAction(&enableSource, "Playback.ABRepeat.Enable")->action();
        auto* clear = host.actions.registerAction(&clearSource, "Playback.ABRepeat.Clear")->action();
        ActionGuard guard{&host.actions};
        QString error;
        if(createdWhileLocked) QVERIFY(guard.enter(host.journal, error));

        QMenu root;
        auto* display = root.addAction(QStringLiteral("Display"));
        auto* repeat = root.addMenu(QStringLiteral("Translated menu"));
        // Match ABRepeatControls: A/B copy presentation but have no linked
        // action; Enable/Clear are linked proxies in the same submenu.
        auto* start = new ProxyAction{repeat};
        start->initialise(enable);
        repeat->addAction(start);
        auto* end = new ProxyAction{repeat};
        end->initialise(clear);
        repeat->addAction(end);
        auto* enabled = new ProxyAction{repeat};
        enabled->setAction(enable);
        repeat->addAction(enabled);
        auto* cleared = new ProxyAction{repeat};
        cleared->setAction(clear);
        repeat->addAction(cleared);
        QSignalSpy startTriggered{start, &QAction::triggered};
        QSignalSpy endTriggered{end, &QAction::triggered};
        QSignalSpy displayTriggered{display, &QAction::triggered};
        QSignalSpy enableTriggered{&enableSource, &QAction::triggered};
        QSignalSpy clearTriggered{&clearSource, &QAction::triggered};
        if(!createdWhileLocked) QVERIFY(guard.enter(host.journal, error));
        else root.popup(QPoint{10, 10}); // Show protects child menus before input.
        for(auto* action : repeat->actions()) {
            action->setEnabled(true); // Host refreshes must not bypass inhibition.
            QVERIFY(!action->isEnabled());
            action->trigger();
        }
        QCOMPARE(startTriggered.size(), 0);
        QCOMPARE(endTriggered.size(), 0);
        QCOMPARE(enableTriggered.size(), 0);
        QCOMPARE(clearTriggered.size(), 0);
        display->trigger();
        QCOMPARE(displayTriggered.size(), 1);
        end->setEnabled(false); // Preserve the latest host eligibility on unlock.
        QVERIFY(guard.leave(error));
        QVERIFY(start->isEnabled());
        QVERIFY(!end->isEnabled());
        start->trigger();
        enabled->trigger();
        cleared->trigger();
        QCOMPARE(startTriggered.size(), 1);
        QCOMPARE(enableTriggered.size(), 1);
        QCOMPARE(clearTriggered.size(), 1);
    }

    void currentHostIntentSurvivesSilentUpdates()
    {
        Host host;
        QAction initiallyEnabled, initiallyDisabled, stillDisabled;
        initiallyDisabled.setEnabled(false);
        stillDisabled.setEnabled(false);
        auto* play = host.actions.registerAction(&initiallyEnabled, Constants::Actions::Play);
        auto* pause = host.actions.registerAction(&initiallyDisabled, Constants::Actions::Pause);
        auto* stop = host.actions.registerAction(&stillDisabled, Constants::Actions::Stop);
        ActionGuard guard{&host.actions};
        QString error;
        QVERIFY(guard.enter(host.journal, error));
        QSignalSpy sourceChanges{&initiallyEnabled, &QAction::changed};
        // Critical case: false -> false has no changed signal but updates Qt's
        // explicit host intent. A changed-handler/snapshot implementation fails.
        initiallyEnabled.setEnabled(false);
        QCOMPARE(sourceChanges.size(), 0);
        initiallyDisabled.setEnabled(true);
        for(int i = 0; i < 20; ++i) {
            initiallyDisabled.setEnabled(false);
            initiallyDisabled.setEnabled(true);
            initiallyDisabled.setText(QString::number(i));
            pause->action()->setEnabled(true); // Even a direct proxy refresh cannot escape.
            QVERIFY(!initiallyDisabled.isEnabled());
            QVERIFY(!pause->action()->isEnabled());
        }
        QVERIFY(guard.leave(error));
        QVERIFY(!initiallyEnabled.isEnabled());
        QVERIFY(!play->action()->isEnabled());
        QVERIFY(initiallyDisabled.isEnabled());
        QVERIFY(pause->action()->isEnabled());
        QVERIFY(!stillDisabled.isEnabled());
        QVERIFY(!stop->action()->isEnabled());
        QVERIFY(guard.enter(host.journal, error));
        QVERIFY(guard.leave(error));
        QVERIFY(!play->action()->isEnabled());
        QVERIFY(pause->action()->isEnabled());
    }

    void inactiveContextsAndTextEditing()
    {
        Host host;
        QWidget playlistA, playlistB, editor;
        WidgetContext a{&playlistA, Context{Id{"Fooyin.Context.Playlist.A"}}, &playlistA};
        WidgetContext b{&playlistB, Context{Id{"Fooyin.Context.Playlist.B"}}, &playlistB};
        WidgetContext text{&editor, Context{Id{"TextEditor"}}, &editor};
        host.actions.addContextObject(&a);
        host.actions.addContextObject(&b);
        host.actions.addContextObject(&text);
        QAction cutA, cutB, cutText;
        QSignalSpy aTriggered{&cutA, &QAction::triggered};
        QSignalSpy bTriggered{&cutB, &QAction::triggered};
        QSignalSpy textTriggered{&cutText, &QAction::triggered};
        auto* command = host.actions.registerAction(&cutA, Constants::Actions::Cut, a.context());
        host.actions.registerAction(&cutB, Constants::Actions::Cut, b.context());
        host.actions.registerAction(&cutText, Constants::Actions::Cut, text.context());
        host.actions.overrideContext(&text, true);
        ActionGuard guard{&host.actions};
        QString error;
        QVERIFY(guard.enter(host.journal, error));
        QVERIFY(!cutA.isEnabled());
        QVERIFY(!cutB.isEnabled());
        QVERIFY(cutText.isEnabled());
        QVERIFY(command->action()->isEnabled());
        command->action()->trigger();
        cutA.trigger();
        cutB.trigger();
        QCOMPARE(textTriggered.size(), 1);
        QCOMPARE(aTriggered.size(), 0);
        QCOMPARE(bTriggered.size(), 0);
        for(int i = 0; i < 3; ++i) {
            command->setCurrentContext(a.context());
            QVERIFY(!command->action()->isEnabled());
            command->action()->trigger();
            command->setCurrentContext(b.context());
            QVERIFY(!command->action()->isEnabled());
            command->setCurrentContext(text.context());
            QVERIFY(command->action()->isEnabled());
            command->action()->trigger();
        }
        QCOMPARE(textTriggered.size(), 4);
        QCOMPARE(aTriggered.size(), 0);
        QCOMPARE(bTriggered.size(), 0);
        cutA.setEnabled(false); // Current host conditions still matter on unlock.
        QVERIFY(guard.leave(error));
        command->setCurrentContext(a.context());
        QVERIFY(!command->action()->isEnabled());
        command->setCurrentContext(b.context());
        QVERIFY(command->action()->isEnabled());
        command->action()->trigger();
        QCOMPARE(bTriggered.size(), 1);
    }

    void lateRegistrationReplacementAndDestruction()
    {
        Host host;
        QWidget playlist, editor;
        WidgetContext context{&playlist, Context{Id{"Fooyin.Context.Playlist.Late"}}, &playlist};
        WidgetContext other{&editor, Context{Id{"Editor"}}, &editor};
        host.actions.addContextObject(&other);
        host.actions.overrideContext(&other, true);
        ActionGuard guard{&host.actions};
        QString error;
        QVERIFY(guard.enter(host.journal, error));
        // Constructor-time inactive context: deliberately not registered with
        // ActionManager yet, as in PlaylistBox/PlaylistManagerWidget.
        auto source = std::make_unique<QAction>();
        auto* command = host.actions.registerAction(source.get(), Constants::Actions::Paste, context.context());
        QVERIFY(!source->isEnabled());
        host.actions.addContextObject(&context);
        command->setCurrentContext(context.context());
        QVERIFY(!command->action()->isEnabled());
        QAction replacement;
        QSignalSpy triggered{&replacement, &QAction::triggered};
        host.actions.registerAction(&replacement, Constants::Actions::Paste, context.context());
        QVERIFY(!replacement.isEnabled());
        QVERIFY(!source->isEnabled()); // Retained old source stays protected this session.
        source.reset();
        command->setCurrentContext(context.context());
        command->action()->trigger();
        replacement.trigger();
        QCOMPARE(triggered.size(), 0);
        QVERIFY(host.actions.unregisterAction(&replacement, Constants::Actions::Paste, context.context()));
        QVERIFY(!replacement.isEnabled());
        auto late = std::make_unique<QAction>();
        host.actions.registerAction(late.get(), Constants::Actions::Next);
        QVERIFY(!late->isEnabled());
        late.reset(); // Host keeps an empty command until explicit unregistration.
        QVERIFY(guard.leave(error));
        QVERIFY(replacement.isEnabled());
        replacement.trigger();
        QCOMPARE(triggered.size(), 1);
    }

    void checkedStateAndAllowedActions()
    {
        Host host;
        QAction order;
        order.setCheckable(true);
        auto* orderCmd = host.actions.registerAction(&order, Constants::Actions::RepeatTrack);
        const std::array allowedIds{Constants::Actions::VolumeUp, Constants::Actions::VolumeDown,
            Constants::Actions::Mute, Constants::Actions::Copy, Constants::Actions::SelectAll,
            Constants::Actions::SearchPlaylist, Constants::Actions::Exit, ToggleActionId};
        std::vector<std::unique_ptr<QAction>> allowed;
        for(const auto* id : allowedIds) {
            allowed.push_back(std::make_unique<QAction>());
            host.actions.registerAction(allowed.back().get(), id);
        }
        ActionGuard guard{&host.actions};
        QString error;
        QVERIFY(guard.enter(host.journal, error));
        orderCmd->action()->trigger();
        order.trigger();
        QVERIFY(!order.isChecked());
        order.setChecked(true); // Host/background state notifications still propagate.
        QVERIFY(orderCmd->action()->isChecked());
        for(const auto* id : allowedIds) {
            QSignalSpy triggered{host.actions.command(id)->actionForContext(Constants::Context::Global),
                                 &QAction::triggered};
            QVERIFY(host.actions.command(id)->action()->isEnabled());
            host.actions.command(id)->action()->trigger();
            QCOMPARE(triggered.size(), 1);
        }
        QLineEdit input{QStringLiteral("browse and edit")};
        input.selectAll();
        QTest::keyClicks(&input, "search text");
        QCOMPARE(input.text(), QStringLiteral("search text"));
        QVERIFY(guard.leave(error));
        QVERIFY(order.isChecked());
        QVERIFY(orderCmd->action()->isChecked());
    }

    void groupAndServiceLifetimes()
    {
        Host host;
        QAction source;
        auto original = std::make_unique<QActionGroup>(nullptr);
        original->addAction(&source);
        host.actions.registerAction(&source, Constants::Actions::Play);
        {
            ActionGuard guard{&host.actions};
            QString error;
            QVERIFY(guard.enter(host.journal, error));
            original->setEnabled(false);
            QVERIFY(guard.leave(error));
            QCOMPARE(source.actionGroup(), original.get());
            QVERIFY(!source.isEnabled());
            original->setEnabled(true);
            QVERIFY(source.isEnabled());
            QVERIFY(guard.enter(host.journal, error));
            original.reset();
        }
        QVERIFY(!source.actionGroup());
        QVERIFY(source.isEnabled());
        auto actions = std::make_unique<ActionManager>(&host.settings);
        actions->setMainWindow(&host.window);
        actions->registerAction(&source, Constants::Actions::Play);
        {
            ActionGuard guard{actions.get()};
            QString error;
            QVERIFY(guard.enter(host.journal, error));
            actions.reset();
        }
        QVERIFY(source.isEnabled());
        ActionGuard missing{nullptr};
        QString error;
        QVERIFY(!missing.enter(host.journal, error));
        QVERIFY(!error.isEmpty());
    }

    void runtimeInstallsGuardAndShutdownRestores()
    {
        Host host;
        PlaylistHost playlists;
        playlists.settings.createSetting(QStringLiteral("Playlist/PlaybackFollowsCursor"), false);
        QAction source;
        host.actions.registerAction(&source, Constants::Actions::Play);
        {
            LockRuntime runtime{host.directory.filePath(QStringLiteral("runtime.json")),
                                playlists.directory.filePath(QStringLiteral("settings.ini"))};
            auto gui = guiContext();
            gui.actionManager = &host.actions;
            QVERIFY(runtime.initialiseCore(playlists.context()));
            QVERIFY(runtime.initialiseGui(gui));
            QVERIFY(runtime.coordinator()->recover());
            auto* toggle = host.actions.command(ToggleActionId)->action();
            toggle->trigger();
            QVERIFY(toggle->isChecked());
            QVERIFY(!source.isEnabled());
            toggle->trigger();
            QVERIFY(source.isEnabled());
            toggle->trigger();
            source.setEnabled(false);
        }
        QVERIFY(!source.isEnabled());
        QVERIFY(!source.actionGroup());
        QVERIFY(!host.actions.command(ToggleActionId));
    }
};

QTEST_MAIN(ActionGuardTest)
#include "actionguardtest.moc"
