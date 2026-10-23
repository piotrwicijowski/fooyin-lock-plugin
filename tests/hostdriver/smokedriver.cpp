// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgetinputchecks.h"
#include "playbackchecks.h"
#include <core/plugins/plugin.h>
#include <core/plugins/coreplugin.h>
#include <core/playlist/playlisthandler.h>
#include <core/player/playercontroller.h>
#include <gui/plugins/guiplugin.h>
#include <gui/guiconstants.h>
#include <gui/fywidget.h>
#include <gui/widgetprovider.h>
#include <utils/actions/actionmanager.h>
#include <utils/actions/command.h>

#include <QAction>
#include <QCoreApplication>
#include <QApplication>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QHeaderView>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointer>
#include <QTabBar>
#include <QTimer>

#include <cstdlib>

// A receiver filter must never see protected inputs. This verifies ordering
// against actual host surfaces and viewports, not just a rule function result.
class InputProbe : public QObject
{
public:
    QEvent::Type watched{QEvent::None};
    int seen{0};
    bool eventFilter(QObject*, QEvent* event) override
    {
        if(event->type() == watched) { ++seen; return true; }
        return false;
    }
    bool reaches(QWidget* widget, QEvent& event)
    {
        watched = event.type(); seen = 0;
        widget->installEventFilter(this);
        QCoreApplication::sendEvent(widget, &event);
        widget->removeEventFilter(this);
        return seen != 0;
    }
};

class LockSmokeDriver : public QObject,
                        public Fooyin::Plugin,
                        public Fooyin::CorePlugin,
                        public Fooyin::GuiPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID FOOYIN_PLUGIN_IID FILE "locksmokedriver.json")
    Q_INTERFACES(Fooyin::Plugin Fooyin::CorePlugin Fooyin::GuiPlugin)

public:
    void initialise(const Fooyin::CorePluginContext& context) override
    {
        if(qEnvironmentVariableIntValue("FOOYIN_LOCK_SMOKE_TEST") == 1 && context.playlistHandler) {
            m_playlists = context.playlistHandler;
            m_player = context.playerController;
            connect(context.playlistHandler, &Fooyin::PlaylistHandler::playlistsPopulated, this, [this]() {
                m_playlistsReady = true;
                quitWhenReady();
            });
        }
    }

    void initialise(const Fooyin::GuiPluginContext& context) override
    {
        if(qEnvironmentVariableIntValue("FOOYIN_LOCK_SMOKE_TEST") == 1) {
            m_guiReady = true;
            m_actions = context.actionManager;
            m_widgets = context.widgetProvider;
            quitWhenReady();
        }
    }

private:
    void quitWhenReady()
    {
        if(m_guiReady && m_playlistsReady) {
            QTimer::singleShot(0, this, [this]() {
                qInfo("Session Lock smoke: host event loop reached; playlists populated");
                checkControl();
            });
        }
    }

    void checkControl()
    {
        auto* command = m_actions ? m_actions->command("SessionLock.Toggle") : nullptr;
        auto* action = command ? command->action() : nullptr;
        if(action && !action->isEnabled() && ++m_attempts < 100) {
            QTimer::singleShot(10, this, &LockSmokeDriver::checkControl);
            return;
        }
        if(!action || !action->isEnabled() || action->isChecked() || !command->defaultShortcuts().empty()) {
            qCritical("Session Lock smoke: toggle not ready/unlocked or has a default shortcut");
            QCoreApplication::exit(1);
            return;
        }
        const bool recoveryRun = qEnvironmentVariable("FOOYIN_LOCK_SMOKE_PHASE") == QStringLiteral("recover");
        auto* normal = m_playlists->playlistByName(QStringLiteral("SessionLock smoke unlocked"));
        auto* prelocked = m_playlists->playlistByName(QStringLiteral("SessionLock smoke prelocked"));
        if(recoveryRun) {
            if(!normal || !prelocked || normal->isLocked() || !prelocked->isLocked()) {
                fail("startup recovery did not restore exact original playlist locks"); return;
            }
            qInfo("Session Lock smoke: interrupted playlist locks recovered after restart");
            QCoreApplication::quit();
            return;
        }
        normal = m_playlists->createPlaylist(QStringLiteral("SessionLock smoke unlocked"));
        prelocked = m_playlists->createPlaylist(QStringLiteral("SessionLock smoke prelocked"));
        if(!normal || !prelocked) { fail("could not create playlist fixtures"); return; }
        Fooyin::Track first{QStringLiteral("/tmp/session-lock-first.flac"), 0}; first.setId(1);
        Fooyin::Track second{QStringLiteral("/tmp/session-lock-second.flac"), 0}; second.setId(2);
        m_player->queueTracks(Fooyin::TrackList{first, second});
        m_playlists->setPlaylistLocked(prelocked->id(), true);
        m_playlists->savePlaylist(prelocked->id());
        // Existing surfaces at entry, plus fresh ones created while locked.
        QPointer<Fooyin::FyWidget> existing{m_widgets->createWidget(QStringLiteral("PlaylistTabs"))};
        if(!existing) { fail("could not create existing tab widget"); return; }
        existing->loadLayoutData({{QStringLiteral("ShowAddButton"), true},
                                  {QStringLiteral("ShowClearButton"), true},
                                  {QStringLiteral("ShowCloseButton"), true}});
        using namespace Fooyin::Constants;
        auto* playCommand = m_actions->command(Actions::PlayPause);
        auto* orderCommand = m_actions->command(Actions::RepeatTrack);
        auto* seekCommand = m_actions->command(Actions::SeekForwardSmall);
        auto* lockCommand = m_actions->command(Actions::LockPlaylist);
        if(!playCommand || !orderCommand || !seekCommand || !lockCommand) {
            fail("expected built-in commands are absent");
            return;
        }
        auto* source = playCommand->actionForContext(Context::Global);
        if(!source) {
            fail("global transport source is absent");
            return;
        }
        int triggers = 0;
        QObject probe;
        const auto connection = connect(source, &QAction::triggered, &probe, [&triggers]() { ++triggers; });
        if(!checkWidgetInputInfrastructure(m_widgets)) { fail("widget input infrastructure checks failed"); return; }
        action->trigger();
        if(!action->isChecked() || !action->isEnabled() || !action->text().contains(QStringLiteral("Locked"))) {
            qCritical("Session Lock smoke: lock transition/indicator failed");
            QCoreApplication::exit(1);
            return;
        }
        if(!normal->isLocked() || !prelocked->isLocked()) { fail("regular playlist locks not applied"); return; }
        if(!checkInputs(existing)) return;
        QWidget* content{nullptr};
        for(auto* candidate : QApplication::allWidgets()) {
            if(candidate->inherits("Fooyin::PlaylistWidget")) { content = candidate; break; }
        }
        if(!content) { fail("could not find the host's nested playlist widget"); return; }
        if(!checkPlaylistInputs(content)) return;
        if(!checkPlaybackInputs(m_widgets, m_actions, m_player, content)) {
            fail("production playback protection failed"); return;
        }
        auto* unrelated = m_widgets->createWidget(QStringLiteral("Spacer"));
        if(!unrelated) { fail("could not create unrelated layout widget"); return; }
        unrelated->setParent(existing);
        InputProbe unrelatedProbe;
        QContextMenuEvent unrelatedMenu{QContextMenuEvent::Mouse, QPoint{2, 2}, QPoint{2, 2}};
        if(!unrelatedProbe.reaches(unrelated, unrelatedMenu)) {
            fail("unrelated content inherited tab restrictions"); return;
        }
        for(const auto& key : {QStringLiteral("PlaylistTabs"), QStringLiteral("PlaylistManager"),
                               QStringLiteral("PlaylistOrganiser"), QStringLiteral("PlaylistSwitcher")}) {
            auto* late = m_widgets->createWidget(key); // Host-parented; explicitly remove to exercise replacement.
            if(!late || !checkInputs(late)) { fail("late widget input protection failed"); return; }
            delete late;
        }
        // A layout replacement is just another fresh subtree. No event-loop
        // scan or constructor-time type snapshot may be required for coverage.
        delete existing;
        existing = m_widgets->createWidget(QStringLiteral("PlaylistTabs"));
        if(!checkInputs(existing)) return;
        auto* added = m_playlists->createPlaylist(QStringLiteral("SessionLock smoke late"));
        if(!added || !added->isLocked()) { fail("new regular playlist was not synchronously locked"); return; }
        for(auto* protectedCommand : {playCommand, orderCommand, seekCommand, lockCommand}) {
            auto* underlying = protectedCommand->actionForContext(Context::Global);
            if(!underlying || underlying->isEnabled() || protectedCommand->action()->isEnabled()) {
                disconnect(connection);
                fail("built-in proxy/source was not suppressed");
                return;
            }
        }
        source->setEnabled(true);
        source->setText(QStringLiteral("Host refresh during lock"));
        playCommand->action()->setEnabled(true);
        source->trigger();
        playCommand->action()->trigger();
        disconnect(connection);
        if(triggers || source->isEnabled() || playCommand->action()->isEnabled()) {
            fail("host refresh escaped transport protection");
            return;
        }
        // A late preset is registered after entry, just like sorting-registry
        // updates. Check it before any queued work/event-loop turn.
        QAction late;
        constexpr auto lateId = "Edit.Sort.Preset.2147483647";
        auto* lateCommand = m_actions->registerAction(&late, lateId);
        if(late.isEnabled() || lateCommand->action()->isEnabled()) {
            fail("late registration escaped protection");
            return;
        }
        source->setEnabled(false); // Silent false-to-false host condition change.
        action->trigger();
        if(action->isChecked() || !action->text().contains(QStringLiteral("Unlocked"))) {
            qCritical("Session Lock smoke: deliberate unlock failed");
            QCoreApplication::exit(1);
            return;
        }
        if(normal->isLocked() || added->isLocked() || !prelocked->isLocked()) {
            fail("playlist unlock did not restore original states"); return;
        }
        if(!checkInputs(existing, false)) return;
        if(source->isEnabled() || playCommand->action()->isEnabled() || !late.isEnabled()
           || !lateCommand->action()->isEnabled()) {
            fail("unlock did not restore current host conditions");
            return;
        }
        m_actions->unregisterAction(&late, lateId);
        for(const auto* allowedId : {Actions::Mute, Actions::VolumeUp, Actions::Exit}) {
            auto* allowed = m_actions->command(allowedId);
            if(!allowed || !allowed->action()->isEnabled()) {
                fail("allowed command was suppressed");
                return;
            }
        }
        qInfo("Session Lock smoke: protected proxies/sources, refresh, late registration, and host-state restoration verified");
        qInfo("Session Lock smoke: playlist locks, parent/viewport input ordering, late/replaced widgets, and restoration verified");
        // The queue fixtures are runtime-only tracks, not library database rows.
        // Remove them after all assertions so normal host persistence stays valid.
        m_player->clearQueue();
        action->trigger(); // Also exercise normal host shutdown while locked.
        qInfo("Session Lock smoke: registered toggle locked, unlocked, and relocked for shutdown");
        delete existing;
        if(qEnvironmentVariable("FOOYIN_LOCK_SMOKE_PHASE") == QStringLiteral("interrupt")) {
            qInfo("Session Lock smoke: interrupting without plugin or host cleanup");
            std::_Exit(0);
        }
        QCoreApplication::quit();
    }

    bool checkInputs(QWidget* root, bool locked = true)
    {
        if(!root) { fail("could not create audited widget"); return false; }
        InputProbe probe;
        // Context-menu delivery to both the root and every view's viewport must
        // be consumed before the receiver's filter or customContextMenu signal.
        auto targets = root->findChildren<QWidget*>();
        targets.push_back(root);
        int tabBars = 0;
        for(auto* target : targets) {
            if(auto* button = qobject_cast<QAbstractButton*>(target);
               button && root->inherits("Fooyin::PlaylistTabs")
               && button->objectName() != QStringLiteral("ScrollLeftButton")
               && button->objectName() != QStringLiteral("ScrollRightButton")) {
                QMouseEvent press{QEvent::MouseButtonPress, QPointF{2, 2}, QPointF{2, 2},
                                  Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
                QKeyEvent space{QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier};
                if(probe.reaches(button, press) == locked || probe.reaches(button, space) == locked) {
                    fail("tab close/add/clear button escaped protection/restoration"); return false;
                }
            }
            if(target == root || (target->parentWidget() && qobject_cast<QAbstractItemView*>(target->parentWidget())
                                 && target == qobject_cast<QAbstractItemView*>(target->parentWidget())->viewport())) {
                QContextMenuEvent context{QContextMenuEvent::Mouse, QPoint{2, 2}, QPoint{2, 2}};
                if(probe.reaches(target, context) == locked) { fail("switcher context menu escaped filter/restoration"); return false; }
                QMouseEvent click{QEvent::MouseButtonPress, QPointF{2, 2}, QPointF{2, 2},
                                  Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
                if(!probe.reaches(target, click)) { fail("ordinary selection was blocked"); return false; }
                QKeyEvent down{QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier};
                if(!probe.reaches(target, down)) { fail("keyboard browsing was blocked"); return false; }
                QMimeData mime;
                mime.setUrls({QUrl::fromLocalFile(QStringLiteral("/tmp/smoke.flac"))});
                QDragEnterEvent drag{QPoint{2, 2}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier};
                if(probe.reaches(target, drag) == locked) { fail("drop input escaped filter/restoration"); return false; }
            }
            if(auto* tab = qobject_cast<QTabBar*>(target)) {
                ++tabBars;
                QMouseEvent middle{QEvent::MouseButtonRelease, QPointF{2, 2}, QPointF{2, 2},
                                   Qt::MiddleButton, Qt::NoButton, Qt::NoModifier};
                if(probe.reaches(tab, middle) == locked) { fail("tab middle-close escaped filter/restoration"); return false; }
                QMouseEvent rename{QEvent::MouseButtonDblClick, QPointF{2, 2}, QPointF{2, 2},
                                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
                if(probe.reaches(tab, rename) == locked) { fail("tab rename escaped filter/restoration"); return false; }
            }
        }
        if(root->inherits("Fooyin::PlaylistTabs") && !tabBars) { fail("audited tab-bar structure changed"); return false; }
        QLineEdit search;
        QKeyEvent text{QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, QStringLiteral("x")};
        QCoreApplication::sendEvent(&search, &text);
        if(search.text() != QStringLiteral("x")) { fail("unrelated text entry was blocked"); return false; }
        return true;
    }

    bool checkPlaylistInputs(QWidget* root)
    {
        InputProbe probe;
        QContextMenuEvent context{QContextMenuEvent::Mouse, QPoint{2, 2}, QPoint{2, 2}};
        if(!probe.reaches(root, context)) { fail("nested playlist lost its content context menu"); return false; }
        auto* view = root->findChild<QAbstractItemView*>();
        auto* header = root->findChild<QHeaderView*>();
        if(!view || !header) { fail("audited playlist view/header structure changed"); return false; }
        QMouseEvent select{QEvent::MouseButtonPress, QPointF{2, 2}, QPointF{2, 2},
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
        if(!probe.reaches(view->viewport(), select)) { fail("playlist selection was blocked"); return false; }
        QMouseEvent sort{QEvent::MouseButtonRelease, QPointF{2, 2}, QPointF{2, 2},
                         Qt::LeftButton, Qt::NoButton, Qt::NoModifier};
        if(probe.reaches(header->viewport(), sort)) { fail("direct header sorting escaped protection"); return false; }
        return true;
    }

    static void fail(const char* detail)
    {
        qCritical("Session Lock smoke: %s", detail);
        QCoreApplication::exit(1);
    }

    QPointer<Fooyin::ActionManager> m_actions;
    QPointer<Fooyin::PlaylistHandler> m_playlists;
    Fooyin::PlayerController* m_player{nullptr};
    Fooyin::WidgetProvider* m_widgets{nullptr};
    int m_attempts{0};
    bool m_guiReady{false};
    bool m_playlistsReady{false};
};

#include "smokedriver.moc"
