// SPDX-License-Identifier: GPL-3.0-or-later
#include <core/plugins/plugin.h>
#include <core/plugins/coreplugin.h>
#include <core/playlist/playlisthandler.h>
#include <gui/plugins/guiplugin.h>
#include <gui/guiconstants.h>
#include <utils/actions/actionmanager.h>
#include <utils/actions/command.h>

#include <QAction>
#include <QCoreApplication>
#include <QPointer>
#include <QTimer>

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
        action->trigger();
        if(!action->isChecked() || !action->isEnabled() || !action->text().contains(QStringLiteral("Locked"))) {
            qCritical("Session Lock smoke: lock transition/indicator failed");
            QCoreApplication::exit(1);
            return;
        }
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
        action->trigger(); // Also exercise normal host shutdown while locked.
        qInfo("Session Lock smoke: registered toggle locked, unlocked, and relocked for shutdown");
        QCoreApplication::quit();
    }

    static void fail(const char* detail)
    {
        qCritical("Session Lock smoke: %s", detail);
        QCoreApplication::exit(1);
    }

    QPointer<Fooyin::ActionManager> m_actions;
    int m_attempts{0};
    bool m_guiReady{false};
    bool m_playlistsReady{false};
};

#include "smokedriver.moc"
