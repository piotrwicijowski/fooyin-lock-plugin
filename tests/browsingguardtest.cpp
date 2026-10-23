// SPDX-License-Identifier: GPL-3.0-or-later
#include "browsingguard.h"

#include <gui/fywidget.h>
#include <gui/guisettings.h>
#include <gui/layout/layoutprovider.h>
#include <utils/settings/settingsmanager.h>

#include <QFile>
#include <QCloseEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

// Isolated public-layout protocol fixture. Actual host selection pipelines are
// exercised separately by the real-host driver.
namespace Fooyin {
class LibraryTreeWidget : public FyWidget
{
    Q_OBJECT
public:
    LibraryTreeWidget() : FyWidget{nullptr} { }
    QString name() const override { return QStringLiteral("Tree"); }
    QString layoutName() const override { return QStringLiteral("LibraryTree"); }
    void saveLayoutData(QJsonObject& data) override { data = config; }
    void loadLayoutData(const QJsonObject& data) override { config = data; Q_EMIT configChanged(); }
    QJsonObject config{{QStringLiteral("PlaylistEnabled"), true}, {QStringLiteral("PlaylistName"), "Results"},
                       {QStringLiteral("ShowHeader"), false}};
    bool enabledAtClose{false};
    void closeEvent(QCloseEvent* event) override
    {
        enabledAtClose = config.value(QStringLiteral("PlaylistEnabled")).toBool();
        event->ignore(); // Even an ignored standalone close must retain protection.
    }
Q_SIGNALS:
    void configChanged();
};
}

using namespace Fooyin;
using namespace Fooyin::SessionLock;
namespace {
const QString CursorKey = QStringLiteral("Playlist/PlaybackFollowsCursor");
void writeLayout(const QString& path, const QString& id, bool enabled)
{
    QFile file{path};
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QJsonObject widget{{QStringLiteral("ID"), id}, {QStringLiteral("PlaylistEnabled"), enabled},
                             {QStringLiteral("PlaylistName"), "Do not rename"}};
    const QJsonObject root{{QStringLiteral("Widgets"), QJsonArray{QJsonObject{{QStringLiteral("LibraryTree"), widget}}}}};
    QVERIFY(file.write(QJsonDocument{root}.toJson()) > 0);
}
QJsonObject readLayout(const QString& path)
{
    QFile file{path};
    if(!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("Widgets")).toArray().first()
        .toObject().value(QStringLiteral("LibraryTree")).toObject();
}
}

class BrowsingGuardTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void cursorRestoration_data()
    {
        QTest::addColumn<bool>("original");
        QTest::addColumn<bool>("typed");
        QTest::newRow("enabled") << true << false;
        QTest::newRow("disabled-default") << false << false;
        QTest::newRow("typed-enabled") << true << true;
        QTest::newRow("typed-disabled-default") << false << true;
    }
    void cursorRestoration()
    {
        QFETCH(bool, original);
        QFETCH(bool, typed);
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("settings.ini"));
        SettingsManager settings{path};
        if(typed) settings.createSetting<Settings::Gui::PlaybackFollowsCursor>(original, CursorKey);
        else settings.createSetting(CursorKey, original);
        LockCoordinator coordinator{directory.filePath(QStringLiteral("recovery.json"))};
        QVERIFY(coordinator.addGuard(std::make_unique<BrowsingGuard>(&settings, nullptr, path, directory.path())));
        QVERIFY(coordinator.recover());
        QVERIFY2(coordinator.setLocked(true), qPrintable(coordinator.lastError()));
        QCOMPARE(settings.value(CursorKey), QVariant{false});
        settings.set(CursorKey, true); // Includes settings-page changes, not just the registered action.
        QCOMPARE(settings.value(CursorKey), QVariant{false});
        QVERIFY(coordinator.setLocked(false));
        QCOMPARE(settings.value(CursorKey), QVariant{original});
        QCOMPARE((QSettings{path, QSettings::IniFormat}.value(CursorKey).toBool()), original);
        QVERIFY(coordinator.setLocked(true));
        coordinator.shutdown();
        QCOMPARE(settings.value(CursorKey), QVariant{original});
    }

    void widgetRestorationAndLateConfiguration()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("settings.ini"));
        SettingsManager settings{path}; settings.createSetting(CursorKey, true);
        LibraryTreeWidget tree;
        const auto original = tree.config;
        LockCoordinator coordinator{directory.filePath(QStringLiteral("recovery.json"))};
        QVERIFY(coordinator.addGuard(std::make_unique<BrowsingGuard>(&settings, nullptr, path, directory.path())));
        QVERIFY(coordinator.recover());
        QVERIFY(coordinator.setLocked(true));
        QVERIFY(!tree.config.value(QStringLiteral("PlaylistEnabled")).toBool());
        QCOMPARE(tree.config.value(QStringLiteral("ShowHeader")), original.value(QStringLiteral("ShowHeader")));
        auto changed = tree.config;
        changed[QStringLiteral("PlaylistEnabled")] = true;
        changed[QStringLiteral("PlaylistName")] = QStringLiteral("New destination");
        tree.loadLayoutData(changed);
        QVERIFY(!tree.config.value(QStringLiteral("PlaylistEnabled")).toBool());
        LibraryTreeWidget late;
        QLineEdit editor{&late};
        QTest::keyClicks(&editor, "ordinary text"); // First input discovers the fully constructed late widget.
        QCOMPARE(editor.text(), QStringLiteral("ordinary text"));
        QVERIFY(!late.config.value(QStringLiteral("PlaylistEnabled")).toBool());
        QVERIFY(coordinator.setLocked(false));
        QVERIFY(tree.config.value(QStringLiteral("PlaylistEnabled")).toBool());
        QVERIFY(late.config.value(QStringLiteral("PlaylistEnabled")).toBool());
        QCOMPARE(tree.config.value(QStringLiteral("PlaylistName")).toString(), QStringLiteral("New destination"));
    }

    void interruptedRecoveryRepairsExactIdsAndRetries()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("settings.ini"));
        const auto layout = directory.filePath(QStringLiteral("saved.fyl"));
        const auto other = directory.filePath(QStringLiteral("unrelated.fyl"));
        const auto unchanged = directory.filePath(QStringLiteral("unchanged.fyl"));
        const auto journalPath = directory.filePath(QStringLiteral("recovery.json"));
        SettingsManager settings{path}; settings.createSetting(CursorKey, false);
        RecoveryJournal journal{journalPath};
        QString error;
        QVERIFY(journal.load(error));
        QVERIFY(journal.remember({"browsing", "setting", CursorKey, true, false}, error));
        QVERIFY(journal.remember({"browsing", "widget-setting", "LibraryTree/exact-id", true, false}, error));
        QVERIFY(journal.remember({"browsing", "widget-setting", "LibraryTree/disabled-id", false, false}, error));
        writeLayout(layout, QStringLiteral("exact-id"), false);
        writeLayout(other, QStringLiteral("replacement-id"), false);
        writeLayout(unchanged, QStringLiteral("disabled-id"), true); // Older saved config wasn't changed by the guard.
        // An unreadable/invalid managed layout cannot be treated as proof that a
        // widget disappeared. Leave recovery pending until it can be inspected.
        QFile broken{directory.filePath(QStringLiteral("broken.fyl"))};
        QVERIFY(broken.open(QIODevice::WriteOnly)); broken.write("{"); broken.close();
        LockCoordinator coordinator{journalPath};
        QVERIFY(coordinator.addGuard(std::make_unique<BrowsingGuard>(&settings, nullptr, path, directory.path())));
        QVERIFY(!coordinator.recover());
        QCOMPARE(coordinator.state(), LockCoordinator::State::RecoveryRequired);
        QVERIFY(!coordinator.setLocked(true));
        QVERIFY(broken.remove());
        QVERIFY2(coordinator.recover(), qPrintable(coordinator.lastError()));
        QVERIFY(readLayout(layout).value(QStringLiteral("PlaylistEnabled")).toBool());
        QVERIFY(!readLayout(other).value(QStringLiteral("PlaylistEnabled")).toBool());
        QVERIFY(readLayout(unchanged).value(QStringLiteral("PlaylistEnabled")).toBool());
        QCOMPARE(readLayout(layout).value(QStringLiteral("PlaylistName")).toString(), QStringLiteral("Do not rename"));
        QCOMPARE(settings.value(CursorKey), QVariant{true});
        RecoveryJournal acknowledged{journalPath};
        QVERIFY(acknowledged.load(error)); QVERIFY(acknowledged.records().empty());
    }

    void recreatedLayoutAndStandaloneClose()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("settings.ini"));
        SettingsManager settings{path}; settings.createSetting(CursorKey, false);
        auto tree = std::make_unique<LibraryTreeWidget>();
        const auto persistedId = tree->id().name();
        LockCoordinator coordinator{directory.filePath(QStringLiteral("recovery.json"))};
        QVERIFY(coordinator.addGuard(std::make_unique<BrowsingGuard>(&settings, nullptr, path, directory.path())));
        QVERIFY(coordinator.recover()); QVERIFY(coordinator.setLocked(true));
        tree.reset();
        tree = std::make_unique<LibraryTreeWidget>();
        tree->loadLayout({{QStringLiteral("ID"), persistedId}, {QStringLiteral("PlaylistEnabled"), false}});
        QLineEdit editor{tree.get()};
        QTest::keyClicks(&editor, "first input");
        QCOMPARE(coordinator.state(), LockCoordinator::State::Locked);
        tree->close();
        QVERIFY(tree->enabledAtClose); // The host would persist the original flag.
        QVERIFY(!tree->config.value(QStringLiteral("PlaylistEnabled")).toBool());
        QVERIFY(coordinator.setLocked(false));
        QVERIFY(tree->config.value(QStringLiteral("PlaylistEnabled")).toBool());
    }

    void failureBeforeMutationAndUnknownRecords()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("settings.ini"));
        SettingsManager settings{path}; settings.createSetting(CursorKey, true);
        LibraryTreeWidget tree;
        RecoveryJournal journal{directory.path()}; // A directory cannot be an atomic journal file.
        QString error;
        BrowsingGuard guard{&settings, nullptr, path, directory.path()};
        QVERIFY(!guard.enter(journal, error));
        QCOMPARE(settings.value(CursorKey), QVariant{true});
        QVERIFY(tree.config.value(QStringLiteral("PlaylistEnabled")).toBool());
        QVERIFY(!guard.recover({"browsing", "setting", "unrelated", true, false}, error));
        QVERIFY(!guard.recover({"browsing", "setting", CursorKey, QStringLiteral("true"), false}, error));
        QVERIFY(!guard.recover({"browsing", "widget-setting", "Other/id", true, false}, error));
        QVERIFY(!guard.recover({"browsing", "widget-setting", "SearchBar/id", 5, 2}, error));
    }

    void failedSettingPersistenceRetainsOriginalForRetry()
    {
        QTemporaryDir directory;
        const auto parent = directory.filePath(QStringLiteral("blocked"));
        const auto path = parent + QStringLiteral("/settings.ini");
        QFile blocker{parent}; QVERIFY(blocker.open(QIODevice::WriteOnly)); blocker.write("file"); blocker.close();
        SettingsManager settings{path}; settings.createSetting(CursorKey, true);
        const auto journalPath = directory.filePath(QStringLiteral("recovery.json"));
        LockCoordinator coordinator{journalPath};
        QVERIFY(coordinator.addGuard(std::make_unique<BrowsingGuard>(&settings, nullptr, path, directory.path())));
        QVERIFY(coordinator.recover());
        QVERIFY(!coordinator.setLocked(true));
        QCOMPARE(coordinator.state(), LockCoordinator::State::RecoveryRequired);
        RecoveryJournal pending{journalPath}; QString error;
        QVERIFY(pending.load(error)); QCOMPARE(pending.records().size(), size_t{1});
        QCOMPARE(pending.records().front().original, QVariant{true});
        QVERIFY(blocker.remove()); QVERIFY(QDir{}.mkpath(parent));
        QVERIFY2(coordinator.recover(), qPrintable(coordinator.lastError()));
        QCOMPARE(settings.value(CursorKey), QVariant{true});
        QCOMPARE((QSettings{path, QSettings::IniFormat}.value(CursorKey).toBool()), true);
    }
};
QTEST_MAIN(BrowsingGuardTest)
#include "browsingguardtest.moc"
