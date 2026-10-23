// SPDX-License-Identifier: GPL-3.0-or-later
#include "contexts.h"
#include "lockcoordinator.h"
#include "lockruntime.h"
#include "playlisthost.h"

#include <gui/guiconstants.h>
#include <utils/actions/actioncontainer.h>
#include <utils/actions/actionmanager.h>
#include <utils/actions/command.h>
#include <utils/settings/settingsmanager.h>

#include <QAction>
#include <QFile>
#include <QMainWindow>
#include <QMap>
#include <QMenu>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Fooyin;
using namespace Fooyin::SessionLock;
using namespace Fooyin::SessionLock::Tests;
using State = LockCoordinator::State;

namespace {
RecoveryRecord setting(const QString& owner = QStringLiteral("browse"))
{
    return {owner, QStringLiteral("setting"), QStringLiteral("Test/CursorFollow"), true, false};
}

// A small independent host model. The recovery adapter deliberately refuses
// unknown settings and third-party values, and verifies write-ahead ordering by
// reopening the actual disk journal before changing persistent state.
struct Host
{
    QVariant value{true};
    QStringList calls;
    QString journalPath;
    bool failEntry{false};
    bool failLeave{false};
    bool failRecovery{false};
    bool ephemeral{false};
    bool durableBeforeMutation{false};
};

class Guard : public LockGuard
{
public:
    explicit Guard(Host& host, QString name = QStringLiteral("browse")) : m_host{host}, m_name{std::move(name)} { }
    QString id() const override { return m_name; }
    bool enter(RecoveryJournal& journal, QString& error) override
    {
        m_host.calls.append(m_name + QStringLiteral(".enter"));
        m_host.ephemeral = true;
        const auto record = setting(m_name);
        if(!journal.remember(record, error)) {
            return false;
        }
        RecoveryJournal disk{m_host.journalPath};
        m_host.durableBeforeMutation = disk.load(error) && disk.records() == std::vector{record};
        if(!m_host.durableBeforeMutation) {
            return false;
        }
        m_host.value = false;
        if(m_host.failEntry) {
            error = QStringLiteral("injected entry failure");
            return false;
        }
        return true;
    }
    bool leave(QString& error) override
    {
        m_host.calls.append(m_name + QStringLiteral(".leave"));
        if(m_host.failLeave) {
            error = QStringLiteral("injected cleanup failure");
            return false;
        }
        m_host.ephemeral = false;
        return true;
    }
    bool recover(const RecoveryRecord& record, QString& error) override
    {
        m_host.calls.append(m_name + QStringLiteral(".recover"));
        if(m_host.failRecovery || record != setting(m_name)
           || (m_host.value != record.original && m_host.value != record.temporary)) {
            error = QStringLiteral("unresolved original or unrelated current value");
            return false;
        }
        m_host.value = record.original;
        return true;
    }
private:
    Host& m_host;
    QString m_name;
};

class EphemeralGuard : public LockGuard
{
public:
    explicit EphemeralGuard(QStringList& calls) : m_calls{calls} { }
    QString id() const override { return QStringLiteral("actions"); }
    bool enter(RecoveryJournal&, QString&) override { m_calls.append(QStringLiteral("actions.enter")); return true; }
    bool leave(QString&) override { m_calls.append(QStringLiteral("actions.leave")); return true; }
    bool recover(const RecoveryRecord&, QString&) override { return false; }
private:
    QStringList& m_calls;
};

// Recovery-only adapter modelling ticket 005's persistent marker lookup. The
// same-named replacement has another marker and must never be touched.
class PlaylistRecoveryGuard : public LockGuard
{
public:
    explicit PlaylistRecoveryGuard(QMap<QString, bool>& locks) : m_locks{locks} { }
    QString id() const override { return QStringLiteral("playlists"); }
    bool enter(RecoveryJournal&, QString&) override { return true; }
    bool leave(QString&) override { return true; }
    bool recover(const RecoveryRecord& record, QString&) override
    {
        if(record.kind != QStringLiteral("playlist-lock")) {
            return false;
        }
        if(m_locks.contains(record.target)) {
            m_locks[record.target] = record.original.toBool();
        }
        return true;
    }
private:
    QMap<QString, bool>& m_locks;
};

QByteArray contents(const QString& path)
{
    QFile file{path};
    if(!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}
}

class CoordinatorTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void repeatedTransitionsAndOrdering()
    {
        QTemporaryDir directory;
        Host host;
        host.journalPath = directory.filePath(QStringLiteral("recovery.json"));
        LockCoordinator coordinator{host.journalPath};
        QVERIFY(coordinator.addGuard(std::make_unique<EphemeralGuard>(host.calls)));
        QVERIFY(coordinator.addGuard(std::make_unique<Guard>(host)));
        QVERIFY(!coordinator.addGuard(std::make_unique<Guard>(host)));
        QVERIFY(!coordinator.setLocked(true)); // Host not ready yet.
        QVERIFY(coordinator.recover());
        QVERIFY(!coordinator.addGuard(std::make_unique<Guard>(host, QStringLiteral("late"))));
        QSignalSpy states{&coordinator, &LockCoordinator::stateChanged};
        for(int i = 0; i < 2; ++i) {
            QVERIFY(coordinator.setLocked(true));
            QVERIFY(coordinator.setLocked(true));
            QCOMPARE(coordinator.state(), State::Locked);
            QVERIFY(host.durableBeforeMutation);
            QCOMPARE(host.value, QVariant{false});
            QVERIFY(coordinator.setLocked(false));
            QVERIFY(coordinator.setLocked(false));
            QCOMPARE(host.value, QVariant{true});
            QVERIFY(!host.ephemeral);
        }
        QCOMPARE(host.calls, QStringList({"actions.enter", "browse.enter", "browse.leave", "actions.leave", "browse.recover",
                                         "actions.enter", "browse.enter", "browse.leave", "actions.leave", "browse.recover"}));
        QCOMPARE(states.size(), 8);
        QCOMPARE(states.at(0).at(0).value<State>(), State::Entering);
        QCOMPARE(states.at(1).at(0).value<State>(), State::Locked);
        RecoveryJournal disk{host.journalPath};
        QString error;
        QVERIFY(disk.load(error));
        QVERIFY(disk.records().empty());
    }

    void partialEntryFailure()
    {
        QTemporaryDir directory;
        Host host;
        host.failEntry = true;
        host.journalPath = directory.filePath(QStringLiteral("recovery.json"));
        LockCoordinator coordinator{host.journalPath};
        QVERIFY(coordinator.addGuard(std::make_unique<EphemeralGuard>(host.calls)));
        QVERIFY(coordinator.addGuard(std::make_unique<Guard>(host)));
        QVERIFY(coordinator.recover());
        QSignalSpy states{&coordinator, &LockCoordinator::stateChanged};
        QSignalSpy failures{&coordinator, &LockCoordinator::failure};
        QVERIFY(!coordinator.setLocked(true));
        QCOMPARE(coordinator.state(), State::Unlocked);
        QCOMPARE(host.value, QVariant{true});
        QVERIFY(!host.ephemeral);
        QCOMPARE(failures.size(), 1);
        QVERIFY(coordinator.lastError().contains(QStringLiteral("injected entry failure")));
        for(const auto& state : states) {
            QVERIFY(state.at(0).value<State>() != State::Locked);
        }
        QCOMPARE(host.calls, QStringList({"actions.enter", "browse.enter", "browse.leave", "actions.leave", "browse.recover"}));
        host.failEntry = false;
        QVERIFY(coordinator.setLocked(true));
        QVERIFY(coordinator.lastError().isEmpty());
    }

    void failedRollbackAndRetry()
    {
        QTemporaryDir directory;
        Host host;
        host.failEntry = host.failLeave = host.failRecovery = true;
        host.journalPath = directory.filePath(QStringLiteral("recovery.json"));
        LockCoordinator coordinator{host.journalPath};
        QVERIFY(coordinator.addGuard(std::make_unique<Guard>(host)));
        QVERIFY(coordinator.recover());
        QVERIFY(!coordinator.setLocked(true));
        QCOMPARE(coordinator.state(), State::RecoveryRequired);
        QVERIFY(!coordinator.setLocked(true));
        RecoveryJournal disk{host.journalPath};
        QString error;
        QVERIFY(disk.load(error));
        QCOMPARE(disk.records().size(), size_t{1});
        host.failEntry = host.failLeave = host.failRecovery = false;
        QVERIFY(coordinator.recover());
        QCOMPARE(coordinator.state(), State::Unlocked);
        QCOMPARE(host.value, QVariant{true});
        QVERIFY(!host.ephemeral);
        QVERIFY(disk.load(error));
        QVERIFY(disk.records().empty());
        QVERIFY(coordinator.setLocked(true));
    }

    void shutdownWhileLocked()
    {
        QTemporaryDir directory;
        Host host;
        host.journalPath = directory.filePath(QStringLiteral("recovery.json"));
        {
            LockCoordinator coordinator{host.journalPath};
            QVERIFY(coordinator.addGuard(std::make_unique<Guard>(host)));
            QVERIFY(coordinator.recover());
            QVERIFY(coordinator.setLocked(true));
            coordinator.shutdown();
            coordinator.shutdown();
            QCOMPARE(coordinator.state(), State::Shutdown);
            QVERIFY(!coordinator.setLocked(true));
            QVERIFY(!coordinator.recover());
        }
        QCOMPARE(host.value, QVariant{true});
        QVERIFY(!host.ephemeral);
        QCOMPARE(host.calls.count(QStringLiteral("browse.leave")), 1);
        // Destructor fallback performs the same restoration.
        {
            LockCoordinator coordinator{host.journalPath};
            QVERIFY(coordinator.addGuard(std::make_unique<Guard>(host)));
            QVERIFY(coordinator.recover());
            QVERIFY(coordinator.setLocked(true));
        }
        QCOMPARE(host.value, QVariant{true});
        QCOMPARE(host.calls.count(QStringLiteral("browse.leave")), 2);
    }

    void startupRecovery_data()
    {
        QTest::addColumn<bool>("alreadyRestored");
        QTest::newRow("interrupted-locked-session") << false;
        QTest::newRow("interrupted-after-restore-before-journal-clear") << true;
    }

    void startupRecovery()
    {
        QFETCH(bool, alreadyRestored);
        QTemporaryDir directory;
        Host host;
        host.value = alreadyRestored;
        host.journalPath = directory.filePath(QStringLiteral("recovery.json"));
        QString error;
        RecoveryJournal previous{host.journalPath};
        QVERIFY(previous.load(error));
        QVERIFY(previous.remember(setting(), error));
        LockCoordinator coordinator{host.journalPath};
        QVERIFY(coordinator.addGuard(std::make_unique<Guard>(host)));
        QCOMPARE(coordinator.state(), State::Recovering);
        QVERIFY(coordinator.recover());
        QCOMPARE(coordinator.state(), State::Unlocked);
        QCOMPARE(host.value, QVariant{true});
        QCOMPARE(host.calls, QStringList{"browse.recover"});
        QVERIFY(previous.load(error));
        QVERIFY(previous.records().empty());
    }

    void unresolvedRecoveryPreservesRecordAndUnrelatedState()
    {
        QTemporaryDir directory;
        Host host;
        host.value = QStringLiteral("changed by another owner");
        host.journalPath = directory.filePath(QStringLiteral("recovery.json"));
        RecoveryJournal previous{host.journalPath};
        QString error;
        QVERIFY(previous.load(error));
        QVERIFY(previous.remember(setting(), error));
        const auto originalBytes = contents(host.journalPath);
        {
            LockCoordinator missingHandler{host.journalPath};
            QVERIFY(!missingHandler.recover());
            QCOMPARE(missingHandler.state(), State::RecoveryRequired);
            QVERIFY(!missingHandler.setLocked(true));
        }
        LockCoordinator coordinator{host.journalPath};
        QVERIFY(coordinator.addGuard(std::make_unique<Guard>(host)));
        QVERIFY(!coordinator.recover());
        QCOMPARE(coordinator.state(), State::RecoveryRequired);
        QCOMPARE(host.value, QVariant{QStringLiteral("changed by another owner")});
        QCOMPARE(contents(host.journalPath), originalBytes);
        host.value = false;
        QVERIFY(coordinator.recover());
    }

    void deletedPlaylistAndPreexistingLock()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("recovery.json"));
        const QString deleted = QStringLiteral("12345678-9389-4c63-a990-e6268862b576");
        const QString prelocked = QStringLiteral("12345678-9389-4c63-a990-e6268862b577");
        const QString changed = QStringLiteral("12345678-9389-4c63-a990-e6268862b578");
        const QString replacement = QStringLiteral("12345678-9389-4c63-a990-e6268862b579");
        QMap<QString, bool> locks{{prelocked, true}, {changed, true}, {replacement, true}};
        RecoveryJournal journal{path};
        QString error;
        QVERIFY(journal.load(error));
        QVERIFY(journal.remember({"playlists", "playlist-lock", deleted, false, true}, error));
        QVERIFY(journal.remember({"playlists", "playlist-lock", prelocked, true, true}, error));
        QVERIFY(journal.remember({"playlists", "playlist-lock", changed, false, true}, error));
        LockCoordinator coordinator{path};
        QVERIFY(coordinator.addGuard(std::make_unique<PlaylistRecoveryGuard>(locks)));
        QVERIFY(coordinator.recover());
        QCOMPARE(locks.size(), 3);
        QVERIFY(locks[prelocked]);
        QVERIFY(!locks[changed]);
        QVERIFY(locks[replacement]);
        QVERIFY(!locks.contains(deleted));
        QVERIFY(journal.load(error));
        QVERIFY(journal.records().empty());
    }

    void failedTeardownLeavesRecoverableOriginal()
    {
        QTemporaryDir directory;
        Host host;
        host.journalPath = directory.filePath(QStringLiteral("recovery.json"));
        {
            LockCoordinator coordinator{host.journalPath};
            QVERIFY(coordinator.addGuard(std::make_unique<Guard>(host)));
            QVERIFY(coordinator.recover());
            QVERIFY(coordinator.setLocked(true));
            host.failRecovery = true;
        }
        QCOMPARE(host.value, QVariant{false});
        host.failRecovery = false;
        LockCoordinator nextStartup{host.journalPath};
        QVERIFY(nextStartup.addGuard(std::make_unique<Guard>(host)));
        QVERIFY(nextStartup.recover());
        QCOMPARE(host.value, QVariant{true});
        QCOMPARE(nextStartup.state(), State::Unlocked);
    }

    void reentrantTransitionsAreRejected()
    {
        QTemporaryDir directory;
        LockCoordinator coordinator{directory.filePath(QStringLiteral("recovery.json"))};
        QVERIFY(coordinator.recover());
        int callbacks = 0;
        const auto connection = connect(&coordinator, &LockCoordinator::stateChanged, &coordinator, [&](State) {
            ++callbacks;
            QVERIFY(!coordinator.setLocked(true));
            QVERIFY(!coordinator.setLocked(false));
            QVERIFY(!coordinator.recover());
        });
        QVERIFY(coordinator.setLocked(true));
        QVERIFY(coordinator.setLocked(false));
        QCOMPARE(callbacks, 4);
        disconnect(connection); // callbacks is destroyed before coordinator.
    }

    void invalidRecovery_data()
    {
        QTest::addColumn<QByteArray>("data");
        QTest::newRow("truncated") << QByteArray{"{\"version\":1,"};
        QTest::newRow("unsupported-version") << QByteArray{"{\"version\":2,\"records\":[]}"};
        QTest::newRow("missing-original") << QByteArray{R"({"version":1,"records":[{"guard":"browse","kind":"setting","target":"Test/CursorFollow","temporary":{"type":"bool","value":false}}]})"};
        QTest::newRow("wrong-original-type") << QByteArray{R"({"version":1,"records":[{"guard":"browse","kind":"setting","target":"Test/CursorFollow","original":{"type":"bool","value":"false"},"temporary":{"type":"bool","value":false}}]})"};
    }

    void invalidRecovery()
    {
        QFETCH(QByteArray, data);
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("recovery.json"));
        QFile file{path};
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(data), data.size());
        file.close();
        LockCoordinator coordinator{path};
        QVERIFY(!coordinator.recover());
        QCOMPARE(coordinator.state(), State::RecoveryRequired);
        QVERIFY(!coordinator.setLocked(true));
        QCOMPARE(contents(path), data);
        // Repairing the file allows an explicit retry; no original is guessed.
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("{\"version\":1,\"records\":[]}");
        file.close();
        QVERIFY(coordinator.recover());
        QCOMPARE(coordinator.state(), State::Unlocked);
    }

    void writeFailureDoesNotMutateHost()
    {
        QTemporaryDir directory;
        QFile blocker{directory.filePath(QStringLiteral("blocker"))};
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        Host host;
        host.journalPath = blocker.fileName() + QStringLiteral("/recovery.json");
        LockCoordinator coordinator{host.journalPath};
        QVERIFY(coordinator.addGuard(std::make_unique<Guard>(host)));
        QVERIFY(coordinator.recover());
        QVERIFY(!coordinator.setLocked(true));
        QCOMPARE(host.value, QVariant{true});
        QVERIFY(!host.durableBeforeMutation);
        QVERIFY(!host.ephemeral);
        QCOMPARE(coordinator.state(), State::RecoveryRequired); // Cannot clear the unresolved intent yet.
        QVERIFY(blocker.remove());
        QVERIFY(coordinator.recover());
        QCOMPARE(coordinator.state(), State::Unlocked);
    }

    void recordTypesAndOriginalOwnership()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("recovery.json"));
        RecoveryJournal journal{path};
        QString error;
        QVERIFY(journal.load(error));
        const QString uuid = QStringLiteral("12bd25e8-9389-4c63-a990-e6268862b576");
        const std::vector<RecoveryRecord> records{
            {"playlists", "playlist-lock", uuid, true, true}, // Pre-existing lock must survive.
            {"browse", "setting", "Test/Count", 12, 0},
            {"browse", "setting", "Test/Name", QStringLiteral("original"), QString{}},
            {"browse", "setting", "Test/List", QStringList{"one", "two"}, QStringList{}}};
        for(const auto& record : records) {
            QVERIFY2(journal.remember(record, error), qPrintable(error));
            QVERIFY(journal.remember(record, error));
        }
        auto conflicting = records.front();
        conflicting.original = false;
        QVERIFY(!journal.remember(conflicting, error));
        conflicting.target = QStringLiteral("Default");
        QVERIFY(!journal.remember(conflicting, error));
        conflicting.target = QStringLiteral("1");
        QVERIFY(!journal.remember(conflicting, error));
        RecoveryJournal disk{path};
        QVERIFY(disk.load(error));
        QCOMPARE(disk.records(), records);
        QCOMPARE(disk.records().front().original, QVariant{true});
    }

    void actionIndicatesCompletedStateAndRemainsUsable()
    {
        PlaylistHost playlists;
        QTemporaryDir directory;
        SettingsManager settings{directory.filePath(QStringLiteral("settings.ini"))};
        settings.createSetting(QStringLiteral("Playlist/PlaybackFollowsCursor"), false);
        ActionManager actions{&settings};
        QMainWindow window;
        actions.setMainWindow(&window);
        auto* playback = actions.createMenu(Constants::Menus::Playback);
        Host host;
        host.journalPath = directory.filePath(QStringLiteral("recovery.json"));
        {
            LockRuntime runtime{host.journalPath, directory.filePath(QStringLiteral("settings.ini"))};
            QVERIFY(runtime.coordinator()->addGuard(std::make_unique<Guard>(host)));
            auto core = playlists.context();
            core.settingsManager = &settings;
            auto gui = guiContext();
            gui.actionManager = &actions;
            QVERIFY(runtime.initialiseCore(core));
            QVERIFY(runtime.initialiseGui(gui));
            auto* command = actions.command(ToggleActionId);
            QVERIFY(command);
            QVERIFY(command->defaultShortcuts().empty());
            QVERIFY(command->actionForContext(Constants::Context::Global));
            command->setCurrentContext(Context{Constants::Context::Global});
            auto* action = command->action();
            QVERIFY(playback->menu()->actions().contains(command->action()));
            QVERIFY(!action->isEnabled());
            QVERIFY(!action->isChecked());
            QVERIFY(runtime.coordinator()->recover());
            connect(runtime.coordinator(), &LockCoordinator::stateChanged, &runtime, [action](State state) {
                if(state == State::Entering || state == State::Unlocking) {
                    QVERIFY(!action->isChecked());
                    QVERIFY(!action->isEnabled());
                }
            });
            action->trigger();
            QVERIFY(action->isChecked());
            QVERIFY(action->isEnabled()); // Deliberate unlock remains available.
            QVERIFY(action->text().endsWith(QStringLiteral("Locked")));
            action->trigger();
            QVERIFY(!action->isChecked());
            host.failEntry = true;
            action->trigger();
            QVERIFY(!action->isChecked());
            QVERIFY(action->text().contains(QStringLiteral("entry failed")));
            QVERIFY(action->statusTip().contains(QStringLiteral("injected entry failure")));
            host.failLeave = host.failRecovery = true;
            action->trigger();
            QVERIFY(action->text().contains(QStringLiteral("Recovery required")));
            QVERIFY(action->isEnabled());
            host.failEntry = host.failLeave = host.failRecovery = false;
            action->trigger(); // Recovery retry, not a new lock.
            QCOMPARE(runtime.coordinator()->state(), State::Unlocked);
            action->trigger(); // Runtime teardown restores this lock.
        }
        QCOMPARE(host.value, QVariant{true});
        QVERIFY(!actions.command(ToggleActionId));
        QVERIFY(playback->menu()->actions().empty());
    }
};

QTEST_MAIN(CoordinatorTest)
#include "coordinatortest.moc"
