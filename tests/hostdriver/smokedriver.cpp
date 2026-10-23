// SPDX-License-Identifier: GPL-3.0-or-later
#include <core/plugins/plugin.h>
#include <core/plugins/coreplugin.h>
#include <core/playlist/playlisthandler.h>
#include <gui/plugins/guiplugin.h>
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
        action->trigger();
        if(!action->isChecked() || !action->isEnabled() || !action->text().contains(QStringLiteral("Locked"))) {
            qCritical("Session Lock smoke: lock transition/indicator failed");
            QCoreApplication::exit(1);
            return;
        }
        action->trigger();
        if(action->isChecked() || !action->text().contains(QStringLiteral("Unlocked"))) {
            qCritical("Session Lock smoke: deliberate unlock failed");
            QCoreApplication::exit(1);
            return;
        }
        action->trigger(); // Also exercise normal host shutdown while locked.
        qInfo("Session Lock smoke: registered toggle locked, unlocked, and relocked for shutdown");
        QCoreApplication::quit();
    }

    QPointer<Fooyin::ActionManager> m_actions;
    int m_attempts{0};
    bool m_guiReady{false};
    bool m_playlistsReady{false};
};

#include "smokedriver.moc"
