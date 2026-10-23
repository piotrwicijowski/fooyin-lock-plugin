// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "contexts.h"

#include <core/engine/audioloader.h>
#include <core/library/musiclibrary.h>
#include <core/playlist/playlisthandler.h>
#include <utils/database/dbconnectionhandler.h>
#include <utils/database/dbconnectionprovider.h>
#include <utils/settings/settingsmanager.h>

#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

namespace Fooyin::SessionLock::Tests {
class EmptyLibrary : public MusicLibrary
{
public:
    bool hasLibrary() const override { return false; }
    std::optional<LibraryInfo> libraryInfo(int) const override { return {}; }
    std::optional<LibraryInfo> libraryForPath(const QString&) const override { return {}; }
    void loadAllTracks() override { Q_EMIT tracksLoaded({}); }
    bool isEmpty() const override { return true; }
    void refreshAll() override { }
    void rescanAll() override { }
    ScanRequest refresh(const LibraryInfo&) override { return {}; }
    ScanRequest rescan(const LibraryInfo&) override { return {}; }
    void cancelScan(int) override { }
    ScanRequest scanTracks(const TrackList&) override { return {}; }
    ScanRequest scanModifiedTracks(const TrackList&) override { return {}; }
    ScanRequest scanFiles(const QList<QUrl>&) override { return {}; }
    ScanRequest loadPlaylist(const QList<QUrl>&) override { return {}; }
    TrackList tracks() const override { return {}; }
    TrackList libraryTracks() const override { return {}; }
    Track trackForId(int) const override { return {}; }
    TrackList tracksForIds(const TrackIds&) const override { return {}; }
    std::shared_ptr<TrackMetadataStore> metadataStore() const override { return {}; }
    void updateTrack(const Track&) override { }
    void updateTracks(const TrackList&) override { }
    void updateTrackMetadata(const TrackList&) override { }
    WriteRequest writeTrackMetadata(const TrackList&) override { return {}; }
    WriteRequest writeTrackCovers(const TrackCoverData&) override { return {}; }
    PendingTrackCoverProvider* pendingTrackCoverProvider() const override { return nullptr; }
    void updateTrackStats(const TrackList&, Track::Stats) override { }
    void updateTrackStats(const Track&, Track::Stats) override { }
    WriteRequest removeUnavailbleTracks() override { return {}; }
    WriteRequest deleteTracks(const TrackList&) override { return {}; }
};

// Actual installed public PlaylistHandler/database, not a lock-state model.
struct PlaylistHost
{
    QTemporaryDir directory;
    SettingsManager settings{directory.filePath(QStringLiteral("settings.ini"))};
    DbConnectionPoolPtr pool{DbConnectionPool::create(
        {QStringLiteral("QSQLITE"), {}, {}, directory.filePath(QStringLiteral("host.db"))},
        QUuid::createUuid().toString())};
    DbConnectionHandler connection{pool};
    EmptyLibrary library;
    std::unique_ptr<PlaylistHandler> handler;

    PlaylistHost()
    {
        if(!exec(QStringLiteral("CREATE TABLE IF NOT EXISTS Playlists (PlaylistID INTEGER PRIMARY KEY AUTOINCREMENT, "
                  "Name TEXT NOT NULL UNIQUE, PlaylistIndex INTEGER, IsAutoPlaylist INTEGER DEFAULT 0, "
                  "Query TEXT, SortQuery TEXT, ForceSorted INTEGER DEFAULT 1, ExtraProperties BLOB)"))
           || !exec(QStringLiteral("CREATE TABLE IF NOT EXISTS PlaylistTracks "
                                   "(PlaylistID INTEGER, TrackID INTEGER, TrackIndex INTEGER)"))) return;
        reload();
    }

    bool exec(const QString& sql) const { QSqlQuery query{DbConnectionProvider{pool}.db()}; return query.exec(sql); }
    void reload()
    {
        handler = std::make_unique<PlaylistHandler>(pool, std::make_shared<AudioLoader>(), &library, &settings);
    }
    CorePluginContext context()
    {
        auto core = coreContext();
        core.playlistHandler = handler.get();
        core.settingsManager = &settings;
        core.dbPool = pool;
        return core;
    }
};
} // namespace Fooyin::SessionLock::Tests
