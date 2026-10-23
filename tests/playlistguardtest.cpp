// SPDX-License-Identifier: GPL-3.0-or-later
#include "playlistguard.h"
#include "playbackguard.h"
#include "playlisthost.h"

#include <core/scripting/scriptregistry.h>
#include <core/coresettings.h>
#include <core/player/playercontroller.h>

#include <QFile>
#include <QSignalSpy>
#include <QTest>

using namespace Fooyin;
using namespace Fooyin::SessionLock;
using namespace Fooyin::SessionLock::Tests;

namespace {
QString identity(const Playlist* playlist)
{
    return playlist->extraProperties().value(QString::fromLatin1(PlaylistIdentityProperty));
}

struct Session
{
    PlaylistHost host;
    QString journalPath{host.directory.filePath(QStringLiteral("recovery.json"))};
    LockCoordinator coordinator{journalPath};
    PlaylistGuard* guard{nullptr};
    explicit Session(bool playback = false)
    {
        auto owned = std::make_unique<PlaylistGuard>(host.handler.get(), host.pool);
        guard = owned.get();
        QObject::connect(guard, &PlaylistGuard::protectionFailed, &coordinator, &LockCoordinator::failProtection);
        coordinator.addGuard(std::move(owned));
        if(playback) coordinator.addGuard(std::make_unique<PlaybackGuard>());
        coordinator.recover();
    }
};
} // namespace

class PlaylistGuardTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() { ScriptRegistry::instance().finalise(); }

    void naturalQueueProgression_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("playlist-overrides") << static_cast<int>(PlaybackQueueMode::PlaylistWithOverrides);
        QTest::newRow("queue-source") << static_cast<int>(PlaybackQueueMode::QueueAsPlaybackSource);
    }

    void naturalQueueProgression()
    {
        QFETCH(int, mode);
        Session s{true};
        s.host.settings.createTempSetting<Settings::Core::PlaybackQueueMode>(mode);
        s.host.settings.createTempSetting<Settings::Core::PlaybackQueueHistoryLimit>(10);
        PlayerController player{&s.host.settings, s.host.handler.get()};
        QCOMPARE(static_cast<int>(player.playbackQueueMode()), mode);
        auto* playlist = s.host.handler->createPlaylist(QStringLiteral("Natural queue"));
        Track first{QStringLiteral("/tmp/first.flac"), 0}; first.setId(1);
        Track second{QStringLiteral("/tmp/second.flac"), 0}; second.setId(2);
        Track third{QStringLiteral("/tmp/third.flac"), 0}; third.setId(3);
        s.host.handler->appendToPlaylist(playlist->id(), {first, second, third});
        player.queueTracks(playlist->playlistTracks());
        std::vector<Player::TrackChangeRequest> requests;
        connect(&player, &PlayerController::trackChangeRequested, &player,
                [&](const Player::TrackChangeRequest& request) {
            requests.push_back(request);
            player.commitCurrentTrack(request); // Model the engine's public commit boundary; no audio device.
        });
        player.play();
        QCOMPARE(player.currentTrack(), first);
        QCOMPARE(requests.size(), 1U);
        QVERIFY(s.coordinator.setLocked(true));
        const auto before = player.playbackQueue().items();
        player.advance(Player::AdvanceReason::NaturalEnd);
        QCOMPARE(requests.size(), 2U);
        QCOMPARE(requests.back().context.reason, Player::AdvanceReason::NaturalEnd);
        QVERIFY(!requests.back().context.userInitiated);
        QCOMPARE(player.currentTrack(), second);
        const auto consumed = player.playbackQueue().items();
        if(mode == static_cast<int>(PlaybackQueueMode::PlaylistWithOverrides)) QVERIFY(consumed.size() < before.size());
        else QVERIFY(player.playbackQueue().currentIndex() > 0);
        QVERIFY(s.coordinator.setLocked(false));
        QCOMPARE(player.playbackQueue().items(), consumed); // Unlock never restores consumed queue state.
        QCOMPARE(player.currentTrack(), second);
        QVERIFY(s.coordinator.setLocked(true));
        player.advance(Player::AdvanceReason::NaturalEnd);
        QCOMPARE(player.currentTrack(), third);
        const auto finalQueue = player.playbackQueue().items();
        s.coordinator.shutdown();
        QCOMPARE(player.playbackQueue().items(), finalQueue);
        QCOMPARE(player.currentTrack(), third);
    }

    void locksAllAndRestoresOnlyOwnedChanges()
    {
        Session s;
        QVERIFY(s.host.handler);
        auto* original = s.host.handler->createPlaylist(QStringLiteral("Already locked"));
        s.host.handler->setPlaylistLocked(original->id(), true);
        s.host.handler->savePlaylist(original->id());
        auto* normal = s.host.handler->createPlaylist(QStringLiteral("Normal"));
        normal->setExtraProperty(QStringLiteral("Unrelated"), QStringLiteral("keep"));
        QVERIFY2(s.coordinator.setLocked(true), qPrintable(s.coordinator.lastError()));
        for(auto* playlist : s.host.handler->playlists()) QVERIFY(playlist->isLocked());
        QVERIFY(s.guard->protects(normal));
        QVERIFY(s.guard->suppressesGeneratedWrites());
        QVERIFY(!identity(normal).isEmpty());
        RecoveryJournal disk{s.journalPath};
        QString error;
        QVERIFY(disk.load(error));
        for(const auto& record : disk.records()) {
            QCOMPARE(record.original, QVariant{false});
            QCOMPARE(record.temporary, QVariant{true});
            QVERIFY(record.target != identity(original));
        }
        auto* late = s.host.handler->createPlaylist(QStringLiteral("Late"));
        QVERIFY(late && late->isLocked()); // Synchronous, before returning to caller.
        const auto marker = identity(normal);
        s.host.handler->renamePlaylist(normal->id(), QStringLiteral("Renamed"));
        QVERIFY(s.coordinator.setLocked(false));
        QVERIFY(original->isLocked());
        QVERIFY(!normal->isLocked());
        QVERIFY(!late->isLocked());
        QCOMPARE(identity(normal), marker);
        QCOMPARE(normal->extraProperties().value(QStringLiteral("Unrelated")), QStringLiteral("keep"));
        QVERIFY(!s.guard->suppressesGeneratedWrites());
        s.host.reload();
        QVERIFY(s.host.handler->playlistByName(QStringLiteral("Already locked"))->isLocked());
        QVERIFY(!s.host.handler->playlistByName(QStringLiteral("Renamed"))->isLocked());
    }

    void rejectsContentMutations()
    {
        Session s;
        auto* playlist = s.host.handler->createPlaylist(QStringLiteral("Tracks"));
        Track first{QStringLiteral("/tmp/first.flac"), 0};
        Track second{QStringLiteral("/tmp/second.flac"), 0};
        first.setId(1); second.setId(2);
        s.host.handler->appendToPlaylist(playlist->id(), {first, second});
        const auto before = playlist->playlistTracks();
        QVERIFY(s.coordinator.setLocked(true));
        s.host.handler->appendToPlaylist(playlist->id(), {second});
        s.host.handler->replacePlaylistTracks(playlist->id(), TrackList{second});
        s.host.handler->replacePlaylistTracks(playlist->id(), PlaylistTrackList{});
        s.host.handler->removePlaylistTracks(playlist->id(), {0});
        s.host.handler->clearPlaylistTracks(playlist->id());
        s.host.handler->createPlaylist(playlist->name(), {second});
        QCOMPARE(playlist->playlistTracks(), before);
        QVERIFY(s.coordinator.setLocked(false));
        s.host.handler->clearPlaylistTracks(playlist->id());
        QCOMPARE(playlist->trackCount(), 0);
    }

    void removedIdentityNeverTargetsReplacement()
    {
        Session s;
        auto* removed = s.host.handler->createPlaylist(QStringLiteral("Same name"));
        QVERIFY(s.coordinator.setLocked(true));
        const auto marker = identity(removed);
        s.host.handler->removePlaylist(removed->id()); // Direct API is outside GUI boundary.
        auto* replacement = s.host.handler->createPlaylist(QStringLiteral("Same name"));
        QVERIFY(replacement->isLocked());
        QVERIFY(identity(replacement) != marker);
        QVERIFY(s.coordinator.setLocked(false));
        QVERIFY(!removed->isLocked());
        QVERIFY(!replacement->isLocked());
        auto* restored = s.host.handler->restorePlaylist(removed->id());
        QVERIFY(restored && !restored->isLocked());
    }

    void recoveryAcrossNewHandlerAndRuntimeIds()
    {
        PlaylistHost host;
        auto* target = host.handler->createPlaylist(QStringLiteral("Interrupted"));
        const auto oldId = target->id();
        const auto path = host.directory.filePath(QStringLiteral("recovery.json"));
        QString marker;
        {
            RecoveryJournal journal{path};
            QString error;
            QVERIFY(journal.load(error));
            PlaylistGuard guard{host.handler.get(), host.pool};
            QVERIFY2(guard.enter(journal, error), qPrintable(error));
            marker = identity(target);
            // Destructor detaches ephemeral enforcement only: emulate process
            // interruption with the real committed host DB and recovery journal.
        }
        host.reload();
        target = host.handler->playlistByName(QStringLiteral("Interrupted"));
        QVERIFY(target->id() != oldId);
        QCOMPARE(identity(target), marker);
        QVERIFY(target->isLocked());
        LockCoordinator restarted{path};
        QVERIFY(restarted.addGuard(std::make_unique<PlaylistGuard>(host.handler.get(), host.pool)));
        QVERIFY2(restarted.recover(), qPrintable(restarted.lastError()));
        QVERIFY(!target->isLocked());
        host.reload();
        QVERIFY(!host.handler->playlistByName(QStringLiteral("Interrupted"))->isLocked());
        QString error;
        RecoveryJournal disk{path};
        QVERIFY(disk.load(error));
        QVERIFY(disk.records().empty());
    }

    void persistenceFailureRetainsJournalAndCanRetry()
    {
        Session s;
        QVERIFY(s.coordinator.setLocked(true));
        QVERIFY(s.host.exec(QStringLiteral("CREATE TRIGGER reject_save BEFORE UPDATE ON Playlists "
                                         "BEGIN SELECT RAISE(ABORT, 'injected save failure'); END")));
        QVERIFY(!s.coordinator.setLocked(false));
        QCOMPARE(s.coordinator.state(), LockCoordinator::State::RecoveryRequired);
        QString error;
        RecoveryJournal disk{s.journalPath};
        QVERIFY(disk.load(error));
        QVERIFY(!disk.records().empty());
        QVERIFY(s.host.exec(QStringLiteral("DROP TRIGGER reject_save")));
        QVERIFY2(s.coordinator.recover(), qPrintable(s.coordinator.lastError()));
        QVERIFY(disk.load(error));
        QVERIFY(disk.records().empty());
    }

    void writeAheadFailureDoesNotLock()
    {
        PlaylistHost host;
        const auto path = host.directory.filePath(QStringLiteral("not-a-directory"));
        QFile blocker{path};
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        RecoveryJournal journal{path + QStringLiteral("/recovery.json")};
        QString error;
        QVERIFY(journal.load(error));
        PlaylistGuard guard{host.handler.get(), host.pool};
        QVERIFY(!guard.enter(journal, error));
        for(auto* playlist : host.handler->playlists()) QVERIFY(!playlist->isLocked());
    }

    void lateFailureStopsClaimingLocked()
    {
        Session s;
        QVERIFY(s.coordinator.setLocked(true));
        QVERIFY(s.host.exec(QStringLiteral("CREATE TRIGGER reject_save BEFORE UPDATE ON Playlists "
                                         "BEGIN SELECT RAISE(ABORT, 'late save failure'); END")));
        auto* late = s.host.handler->createPlaylist(QStringLiteral("Late failure"));
        QVERIFY(late);
        QVERIFY(s.coordinator.state() != LockCoordinator::State::Locked);
        QVERIFY(!late->isLocked());
        QVERIFY(s.coordinator.lastError().contains(QStringLiteral("protection failed")));
        QVERIFY(s.host.exec(QStringLiteral("DROP TRIGGER reject_save")));
        QVERIFY(s.coordinator.recover());
    }

    void duplicateMarkersAreUnresolved()
    {
        Session s;
        auto* first = s.host.handler->createPlaylist(QStringLiteral("First"));
        auto* second = s.host.handler->createPlaylist(QStringLiteral("Second"));
        QVERIFY(s.coordinator.setLocked(true));
        second->setExtraProperty(QString::fromLatin1(PlaylistIdentityProperty), identity(first));
        s.host.handler->savePlaylist(second->id());
        QVERIFY(!s.coordinator.setLocked(false));
        QCOMPARE(s.coordinator.state(), LockCoordinator::State::RecoveryRequired);
        QVERIFY(s.coordinator.lastError().contains(QStringLiteral("Ambiguous")));
    }

    void unavailableServicesAndBadRecordsFail()
    {
        PlaylistGuard missing{nullptr, {}};
        QTemporaryDir dir;
        RecoveryJournal journal{dir.filePath(QStringLiteral("recovery.json"))};
        QString error;
        QVERIFY(journal.load(error));
        QVERIFY(!missing.enter(journal, error));
        Session s;
        QVERIFY(!s.guard->recover({QStringLiteral("playlists"), QStringLiteral("setting"),
                                  QStringLiteral("Other"), false, true}, error));
        QVERIFY(!s.guard->recover({QStringLiteral("playlists"), QStringLiteral("playlist-lock"),
                                  QUuid::createUuid().toString(QUuid::WithoutBraces), true, true}, error));
    }

    void partialEntryRestoresEarlierLocks()
    {
        Session s;
        auto* bad = s.host.handler->createPlaylist(QStringLiteral("Bad identity"));
        bad->setExtraProperty(QString::fromLatin1(PlaylistIdentityProperty), QStringLiteral("not-a-uuid"));
        QVERIFY(!s.coordinator.setLocked(true));
        QCOMPARE(s.coordinator.state(), LockCoordinator::State::Unlocked);
        for(auto* playlist : s.host.handler->playlists()) QVERIFY(!playlist->isLocked());
    }

    void autoplaylistsRemainNaturallyWritable()
    {
        Session s;
        auto* automatic = s.host.handler->createAutoPlaylist(QStringLiteral("Automatic"), QStringLiteral("title PRESENT"));
        QVERIFY(automatic);
        QVERIFY(s.coordinator.setLocked(true));
        QVERIFY(!automatic->isLocked());
        QVERIFY(s.guard->protects(automatic)); // GUI/generated-write policy, not a core content lock.
        Track track{QStringLiteral("/tmp/automatic.flac"), 0};
        track.setId(1);
        track.setTitle(QStringLiteral("Automatic track"));
        QVERIFY(automatic->regenerateTracks({track}));
        QCOMPARE(automatic->trackCount(), 1);
        QVERIFY(s.coordinator.setLocked(false));
        QVERIFY(!automatic->isLocked());
        QCOMPARE(automatic->trackCount(), 1);
    }

    void purgedMissingIdentityAndReusedRowIdAreSafe()
    {
        PlaylistHost host;
        auto* original = host.handler->createPlaylist(QStringLiteral("Original"));
        const auto runtimeId = original->id();
        const int dbId = original->dbId();
        const auto path = host.directory.filePath(QStringLiteral("recovery.json"));
        {
            RecoveryJournal journal{path};
            QString error;
            QVERIFY(journal.load(error));
            PlaylistGuard guard{host.handler.get(), host.pool};
            QVERIFY(guard.enter(journal, error));
        }
        host.handler->removePlaylist(runtimeId);
        host.handler->purgeRemovedPlaylists({runtimeId});
        QVERIFY(host.exec(QStringLiteral("INSERT INTO Playlists (PlaylistID,Name,PlaylistIndex) VALUES (%1,'Original',1)").arg(dbId)));
        host.reload();
        auto* replacement = host.handler->playlistByName(QStringLiteral("Original"));
        QVERIFY(replacement);
        host.handler->setPlaylistLocked(replacement->id(), true);
        host.handler->savePlaylist(replacement->id());
        LockCoordinator recovery{path};
        recovery.addGuard(std::make_unique<PlaylistGuard>(host.handler.get(), host.pool));
        QVERIFY2(recovery.recover(), qPrintable(recovery.lastError()));
        QVERIFY(replacement->isLocked());
    }

    void malformedDurablePropertiesBlockRecovery()
    {
        Session s;
        QVERIFY(s.coordinator.setLocked(true));
        QVERIFY(s.host.exec(QStringLiteral("UPDATE Playlists SET ExtraProperties=x'00000001'")));
        QVERIFY(!s.coordinator.setLocked(false));
        QCOMPARE(s.coordinator.state(), LockCoordinator::State::RecoveryRequired);
        QVERIFY(s.coordinator.lastError().contains(QStringLiteral("Malformed")));
    }

    void uncommittedHostWritesAreNotAcknowledged()
    {
        Session s;
        // WAL permits the independent reader to inspect the last committed
        // original while a host transaction has private, uncommitted changes.
        QVERIFY(s.host.exec(QStringLiteral("PRAGMA journal_mode=WAL")));
        QVERIFY(s.coordinator.setLocked(true));
        QVERIFY(s.host.exec(QStringLiteral("BEGIN TRANSACTION")));
        QVERIFY(!s.coordinator.setLocked(false));
        QCOMPARE(s.coordinator.state(), LockCoordinator::State::RecoveryRequired);
        QVERIFY(s.host.exec(QStringLiteral("ROLLBACK")));
        QVERIFY2(s.coordinator.recover(), qPrintable(s.coordinator.lastError()));
    }

    void missingDiskMarkerDoesNotOverwriteAReusedRow()
    {
        Session s;
        QVERIFY(s.coordinator.setLocked(true));
        QVERIFY(s.host.exec(QStringLiteral("UPDATE Playlists SET ExtraProperties=NULL")));
        QVERIFY(!s.coordinator.setLocked(false));
        QCOMPARE(s.coordinator.state(), LockCoordinator::State::RecoveryRequired);
        for(auto* playlist : s.host.handler->playlists()) QVERIFY(playlist->isLocked());
        QVERIFY(s.coordinator.lastError().contains(QStringLiteral("durable recovery identity")));
    }
};

QTEST_MAIN(PlaylistGuardTest)
#include "playlistguardtest.moc"
