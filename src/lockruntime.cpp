// SPDX-License-Identifier: GPL-3.0-or-later
#include "lockruntime.h"

#include "actionguard.h"
#include "lockcoordinator.h"
#include "playlistguard.h"
#include "playbackguard.h"
#include "seekguard.h"

#include <core/player/playercontroller.h>
#include <core/playlist/playlisthandler.h>
#include <core/plugins/coreplugincontext.h>
#include <gui/playlist/currentplaylistcontroller.h>
#include <gui/plugins/guiplugincontext.h>
#include <gui/trackselectioncontroller.h>
#include <gui/guiconstants.h>
#include <utils/actions/actioncontainer.h>
#include <utils/actions/actionmanager.h>
#include <utils/actions/command.h>
#include <utils/fypaths.h>
#include <utils/settings/settingsmanager.h>

#include <QAction>
#include <QDir>
#include <QLoggingCategory>
#include <QTimer>

namespace Fooyin::SessionLock {
LockRuntime::LockRuntime(QString recoveryPath)
    : m_coordinator{std::make_unique<LockCoordinator>(
          recoveryPath.isEmpty() ? QDir{Utils::statePath()}.filePath(QStringLiteral("session-lock-recovery.json"))
                                 : recoveryPath)}
{
    setObjectName(QStringLiteral("SessionLock.Runtime"));
    connect(m_coordinator.get(), &LockCoordinator::stateChanged, this, &LockRuntime::updateAction);
    connect(m_coordinator.get(), &LockCoordinator::failure, this, [this](const QString& error) {
        qWarning().noquote() << "Session Lock:" << error;
        updateAction();
    });
}

LockRuntime::~LockRuntime()
{
    m_coordinator->shutdown(); // Restore while borrowed services and the control still exist.
    if(m_services.actions && m_toggle) {
        m_services.actions->unregisterAction(m_toggle, ToggleActionId);
    }
    delete m_toggle;
}

bool LockRuntime::initialiseCore(const CorePluginContext& context)
{
    if(m_coreInitialised) {
        return false;
    }
    m_services.player    = context.playerController;
    m_services.playlists = context.playlistHandler;
    m_services.settings  = context.settingsManager;
    m_services.database  = context.dbPool;
    if(m_services.playlists) {
        connect(m_services.playlists, &PlaylistHandler::playlistsPopulated, this, [this]() {
            m_playlistsReady = true;
            recoverWhenReady();
        });
    }
    m_coreInitialised    = true;
    return true;
}

bool LockRuntime::initialiseGui(const GuiPluginContext& context)
{
    if(!m_coreInitialised || m_guiInitialised) {
        return false;
    }
    m_services.actions         = context.actionManager;
    m_services.trackSelection  = context.trackSelection;
    m_services.currentPlaylist = context.playlistSelection;
    m_guiInitialised           = true;
    m_coordinator->addGuard(std::make_unique<ActionGuard>(m_services.actions));
    auto playlists = std::make_unique<PlaylistGuard>(m_services.playlists, m_services.database);
    connect(playlists.get(), &PlaylistGuard::protectionFailed, m_coordinator.get(), &LockCoordinator::failProtection);
    m_coordinator->addGuard(std::move(playlists));
    m_coordinator->addGuard(std::make_unique<PlaybackGuard>());
    m_coordinator->addGuard(std::make_unique<SeekGuard>());
    if(m_services.actions) {
        m_toggle = new QAction{this};
        m_toggle->setObjectName(QString::fromLatin1(ToggleActionId));
        m_toggle->setCheckable(true);
        updateAction();
        auto* command = m_services.actions->registerAction(m_toggle, ToggleActionId);
        command->setAttribute(ProxyAction::UpdateText);
        command->setDescription(tr("Session Lock"));
        command->setCategories({tr("Playback")});
        if(auto* menu = m_services.actions->actionContainer(Constants::Menus::Playback)) {
            menu->addAction(command);
        }
        connect(m_toggle, &QAction::triggered, this, [this]() {
            if(m_coordinator->state() == LockCoordinator::State::RecoveryRequired) {
                m_coordinator->recover();
            }
            else {
                m_coordinator->setLocked(m_coordinator->state() != LockCoordinator::State::Locked);
            }
            updateAction();
        });
    }
    recoverWhenReady();
    return true;
}

void LockRuntime::recoverWhenReady()
{
    if(m_guiInitialised && m_playlistsReady) {
        // Finish host population callbacks before restoring persistent state.
        QTimer::singleShot(0, this, [this]() {
            if(m_coordinator->state() == LockCoordinator::State::Recovering) {
                m_coordinator->recover();
            }
        });
    }
}

void LockRuntime::updateAction()
{
    if(!m_toggle) {
        return;
    }
    using State = LockCoordinator::State;
    const auto state = m_coordinator->state();
    QString status;
    switch(state) {
        case State::Recovering: status = tr("Recovering"); break;
        case State::Unlocked: status = m_coordinator->lastError().isEmpty() ? tr("Unlocked") : tr("Unlocked — entry failed"); break;
        case State::Entering: status = tr("Locking"); break;
        case State::Locked: status = tr("Locked"); break;
        case State::Unlocking: status = tr("Unlocking"); break;
        case State::RecoveryRequired: status = tr("Recovery required — Retry"); break;
        case State::Shutdown: status = tr("Shutting down"); break;
    }
    m_toggle->setText(tr("Session Lock: %1").arg(status));
    m_toggle->setChecked(state == State::Locked);
    m_toggle->setEnabled(state == State::Unlocked || state == State::Locked || state == State::RecoveryRequired);
    const auto detail = m_coordinator->lastError().isEmpty()
                            ? tr("Session-only lock. Activate again to unlock. Protection guards are still in development.")
                            : m_coordinator->lastError();
    m_toggle->setStatusTip(detail);
    m_toggle->setToolTip(detail);
}

LockCoordinator* LockRuntime::coordinator() const
{
    return m_coordinator.get();
}

const LockServices& LockRuntime::services() const
{
    return m_services;
}
} // namespace Fooyin::SessionLock
