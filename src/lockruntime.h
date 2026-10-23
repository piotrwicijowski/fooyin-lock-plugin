// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPointer>

#include <memory>

class QAction;

namespace Fooyin {
class ActionManager;
struct CorePluginContext;
class CurrentPlaylistController;
class DbConnectionPool;
struct GuiPluginContext;
class PlayerController;
class PlaylistHandler;
class SettingsManager;
class TrackSelectionController;
}

namespace Fooyin::SessionLock {
class LockCoordinator;
inline constexpr auto ToggleActionId = "SessionLock.Toggle";
// Borrowed host services, never deleted by this plugin. QPointer also handles
// partial host teardown. Null services are retained as null; each guard must
// validate its requirements before allowing entry into the Locked state.
struct LockServices
{
    QPointer<PlayerController> player;
    QPointer<PlaylistHandler> playlists;
    QPointer<SettingsManager> settings;
    QPointer<ActionManager> actions;
    QPointer<TrackSelectionController> trackSelection;
    QPointer<CurrentPlaylistController> currentPlaylist;
    std::weak_ptr<DbConnectionPool> database;
};

// The plugin's single runtime ownership root.
class LockRuntime : public QObject
{
    Q_OBJECT

public:
    explicit LockRuntime(QString recoveryPath = {});
    ~LockRuntime() override;

    bool initialiseCore(const CorePluginContext& context);
    bool initialiseGui(const GuiPluginContext& context);
    [[nodiscard]] const LockServices& services() const;
    [[nodiscard]] LockCoordinator* coordinator() const;

private:
    void recoverWhenReady();
    void updateAction();

    LockServices m_services;
    std::unique_ptr<LockCoordinator> m_coordinator;
    QAction* m_toggle{nullptr};
    bool m_playlistsReady{false};
    bool m_coreInitialised{false};
    bool m_guiInitialised{false};
};
} // namespace Fooyin::SessionLock
